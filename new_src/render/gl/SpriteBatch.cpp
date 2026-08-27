#include "render/gl/SpriteBatch.h"

#include <cstring>

namespace newcore {

namespace {

const char* kVertex2D = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uCanvasSize;
out vec2 vUV;
out vec4 vColor;
void main() {
	vUV = aUV;
	vColor = aColor;
	// Canvas origin is top-left; OpenGL NDC has +Y up, so flip Y.
	float nx = aPos.x / uCanvasSize.x * 2.0 - 1.0;
	float ny = 1.0 - aPos.y / uCanvasSize.y * 2.0;
	gl_Position = vec4(nx, ny, 0.0, 1.0);
}
)";

const char* kFragmentRgba = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 fragColor;
void main() {
	fragColor = texture(uTexture, vUV) * vColor;
}
)";

const char* kFragmentIndexed = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;   // R8 index texture
uniform sampler2D uPalette;   // RGBA8 palette LUT (256x1)
out vec4 fragColor;
void main() {
	float index = texture(uTexture, vUV).r;
	fragColor = texture(uPalette, vec2(index, 0.5)) * vColor;
}
)";

const char* kFragmentColor = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
	fragColor = vColor;
}
)";

} // namespace

SpriteBatch::SpriteBatch() {
	vertices_.reserve(kMaxVertices);
}

SpriteBatch::~SpriteBatch() {
	if (whiteTex_) glDeleteTextures(1, &whiteTex_);
	if (vao_) glDeleteVertexArrays(1, &vao_);
	if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool SpriteBatch::initialize(int canvasWidth, int canvasHeight) {
	canvasWidth_ = canvasWidth;
	canvasHeight_ = canvasHeight;

	std::string err;
	if (!indexedShader_.compile(kVertex2D, kFragmentIndexed, &err)) {
		fprintf(stderr, "indexed shader: %s\n", err.c_str());
		return false;
	}
	if (!rgbaShader_.compile(kVertex2D, kFragmentRgba, &err)) {
		fprintf(stderr, "rgba shader: %s\n", err.c_str());
		return false;
	}
	if (!colorShader_.compile(kVertex2D, kFragmentColor, &err)) {
		fprintf(stderr, "color shader: %s\n", err.c_str());
		return false;
	}
	fprintf(stdout, "shaders OK\n");

	// 1x1 white texture for solid fills.
	glGenTextures(1, &whiteTex_);
	glBindTexture(GL_TEXTURE_2D, whiteTex_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	const uint8_t white[4] = { 255, 255, 255, 255 };
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenVertexArrays(1, &vao_);
	glBindVertexArray(vao_);
	glGenBuffers(1, &vbo_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, kMaxVertices * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)8);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)16);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	initialized_ = true;
	return true;
}

void SpriteBatch::begin() {
	vertices_.clear();
	vertexCount_ = 0;
	boundTex_ = nullptr;
	currentTex_ = nullptr;
	boundIndexed_ = false;
	blendMode_ = 0;
	begun_ = true;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindVertexArray(vao_);
}

void SpriteBatch::end() {
	flush();
	glBindVertexArray(0);
	begun_ = false;
}

void SpriteBatch::setBlendMode(int mode) {
	if (mode == blendMode_) return;
	flush();
	blendMode_ = mode;
}

void SpriteBatch::emitQuad(const Vertex* v) {
	if ((int)vertices_.size() + 6 > kMaxVertices) flush();
	// Two triangles: (0,1,2) and (0,2,3) for quad TL,TR,BR,BL.
	Vertex tri[6] = { v[0], v[1], v[2], v[0], v[2], v[3] };
	vertices_.insert(vertices_.end(), tri, tri + 6);
	vertexCount_ += 6;
}

void SpriteBatch::flush() {
	if (vertices_.empty()) return;

	// Rebind our VAO: another renderer (World3D) may have left its own VAO
	// (or none) bound after drawing, and this draw call needs our layout.
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);

	if (boundIndexed_) {
		indexedShader_.use();
		indexedShader_.setVec2("uCanvasSize", (float)canvasWidth_, (float)canvasHeight_);
		indexedShader_.setInt("uTexture", 0);
		indexedShader_.setInt("uPalette", 1);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, boundTex_ ? boundTex_->id() : whiteTex_);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, boundTex_ ? boundTex_->paletteId() : whiteTex_);
	} else if (boundTex_) {
		rgbaShader_.use();
		rgbaShader_.setVec2("uCanvasSize", (float)canvasWidth_, (float)canvasHeight_);
		rgbaShader_.setInt("uTexture", 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, boundTex_->id());
	} else {
		colorShader_.use();
		colorShader_.setVec2("uCanvasSize", (float)canvasWidth_, (float)canvasHeight_);
	}

	// Re-assert blend per flush: World3D leaves GL_BLEND disabled after its
	// draws, and quads recorded before endFrame rasterize HERE (beginFrame's
	// glEnable is long gone), which rendered kill-color pixels opaque.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, blendMode_ == 1 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices_.size());

	{
		GLenum err = glGetError();
		if (err != GL_NO_ERROR)
			fprintf(stderr, "GL error after draw (indexed=%d): 0x%04x\n", boundIndexed_ ? 1 : 0, err);
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);

	vertices_.clear();
	vertexCount_ = 0;
	boundTex_ = nullptr;
	boundIndexed_ = false;
}

void SpriteBatch::draw(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
	int dstX, int dstY, int dstW, int dstH, int rotateMode,
	float r, float g, float b, float a) {
	if (!begun_ || !tex.valid()) return;

	bool indexed = (tex.format() == Texture::Format::Indexed);
	if (boundTex_ != &tex || boundIndexed_ != indexed) {
		flush();
		boundTex_ = &tex;
		boundIndexed_ = indexed;
	}
	currentTex_ = &tex;

	float texW = (float)tex.width();
	float texH = (float)tex.height();
	if (texW <= 0 || texH <= 0) return;

	float u0 = (float)srcX / texW;
	float v0 = (float)srcY / texH;
	float u1 = (float)(srcX + srcW) / texW;
	float v1 = (float)(srcY + srcH) / texH;

	float hw = (float)dstW * 0.5f;
	float hh = (float)dstH * 0.5f;
	float cx = (float)dstX + hw;
	float cy = (float)dstY + hh;

	float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };

	// Destination corner offsets (TL,TR,BR,BL) relative to the center.
	// Each mode replicates legacy Image::DrawTexture glRotatef/glScalef
	// around the quad center (the geometry rotates, so axes swap).
	float pos[8];
	switch (rotateMode) {
		case 1: // glRotatef(90)
			pos[0] = hh; pos[1] = -hw; pos[2] = hh; pos[3] = hw;
			pos[4] = -hh; pos[5] = hw; pos[6] = -hh; pos[7] = -hw; break;
		case 2: // glRotatef(180)
			pos[0] = hw; pos[1] = hh; pos[2] = -hw; pos[3] = hh;
			pos[4] = -hw; pos[5] = -hh; pos[6] = hw; pos[7] = -hh; break;
		case 3: // glRotatef(270)
			pos[0] = -hh; pos[1] = hw; pos[2] = -hh; pos[3] = -hw;
			pos[4] = hh; pos[5] = -hw; pos[6] = hh; pos[7] = hw; break;
		case 4: // glScalef(-1,1)
			pos[0] = hw; pos[1] = -hh; pos[2] = -hw; pos[3] = -hh;
			pos[4] = -hw; pos[5] = hh; pos[6] = hw; pos[7] = hh; break;
		case 5: // glRotatef(90); glScalef(-1,1)
			pos[0] = hh; pos[1] = hw; pos[2] = hh; pos[3] = -hw;
			pos[4] = -hh; pos[5] = -hw; pos[6] = -hh; pos[7] = hw; break;
		case 6: // glRotatef(180); glScalef(-1,1)  == mirror Y
			pos[0] = -hw; pos[1] = hh; pos[2] = hw; pos[3] = hh;
			pos[4] = hw; pos[5] = -hh; pos[6] = -hw; pos[7] = -hh; break;
		case 7: // glRotatef(270); glScalef(-1,1)
			pos[0] = -hh; pos[1] = -hw; pos[2] = -hh; pos[3] = hw;
			pos[4] = hh; pos[5] = hw; pos[6] = hh; pos[7] = -hw; break;
		case 8: // glScalef(1,-1)  == mirror Y
			pos[0] = -hw; pos[1] = hh; pos[2] = hw; pos[3] = hh;
			pos[4] = hw; pos[5] = -hh; pos[6] = -hw; pos[7] = -hh; break;
		case 0:
		default:
			pos[0] = -hw; pos[1] = -hh; pos[2] = hw; pos[3] = -hh;
			pos[4] = hw; pos[5] = hh; pos[6] = -hw; pos[7] = hh; break;
	}

	Vertex v[4];
	for (int i = 0; i < 4; ++i) {
		v[i].x = cx + pos[i * 2];
		v[i].y = cy + pos[i * 2 + 1];
		v[i].u = uv[i * 2];
		v[i].v = uv[i * 2 + 1];
		v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = a;
	}
	emitQuad(v);
}

void SpriteBatch::fillRect(int x, int y, int w, int h,
	float r, float g, float b, float a) {
	if (!begun_ || w <= 0 || h <= 0) return;

	if (boundTex_) flush();

	Vertex v[4] = {
		{ (float)x, (float)y, 0, 0, r, g, b, a },
		{ (float)(x + w), (float)y, 0, 0, r, g, b, a },
		{ (float)(x + w), (float)(y + h), 0, 0, r, g, b, a },
		{ (float)x, (float)(y + h), 0, 0, r, g, b, a },
	};
	emitQuad(v);
}

void SpriteBatch::clear(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
	fillRect(x, y, w, h, r / 255.f, g / 255.f, b / 255.f, 1.f);
}

} // namespace newcore