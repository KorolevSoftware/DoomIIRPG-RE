#include "render/gl/GlScene3D.h"

#include <cstdio>

#include "render/api/RenderModes.h"
#include "render/gl/GlTexture.h"
#include "render/gl/GlTextureStore.h"

namespace newcore {

namespace {

const char* kWorldVertex = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uView; // view matrix for eye-space depth (fog)
out vec2 vUV;
out float vFogDepth; // eye-space depth (positive forward)
void main() {
	vUV = aUV;
	vec4 eye = uView * vec4(aPos, 1.0);
	vFogDepth = -eye.z;
	gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kWorldFragment = R"(
#version 330 core
in vec2 vUV;
in float vFogDepth;
uniform sampler2D uTexture; // R8 index texture
uniform sampler2D uPalette; // RGBA8 palette LUT (256x1)
uniform vec4 uColorMod;     // legacy primary color of the blend mode (GL_MODULATE)
uniform int uFogEnabled;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec4 uFogColor;
out vec4 fragColor;
void main() {
	float index = texture(uTexture, vUV).r;
	vec4 col = texture(uPalette, vec2(index, 0.5));
	// Fixed-pipeline GL_MODULATE texture env (src/GLES.cpp:620): Cout = Ctex*Ccolor,
	// Aout = Atex*Acolor. Runs BEFORE fog, like the fixed pipeline does.
	col *= uColorMod;
	if (uFogEnabled != 0) {
		// Fog affects RGB only (GL fog leaves alpha untouched) so transparent
		// billboard texels stay transparent instead of fogging into a box.
		float f = clamp((uFogEnd - vFogDepth) / max(uFogEnd - uFogStart, 1e-6), 0.0, 1.0);
		col.rgb = mix(uFogColor.rgb, col.rgb, f);
	}
	fragColor = col;
}
)";

// The neutral BlendFactor of the shared RENDER_* table (render/api/RenderModes.h)
// -> the GL enum this backend feeds to glBlendFunc.
GLenum glBlendFactor(BlendFactor f) {
	switch (f) {
		case BlendFactor::Zero:             return GL_ZERO;
		case BlendFactor::One:              return GL_ONE;
		case BlendFactor::SrcAlpha:         return GL_SRC_ALPHA;
		case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
		case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
	}
	return GL_ONE;
}

} // namespace

GlScene3D::~GlScene3D() {
	if (vao_) glDeleteVertexArrays(1, &vao_);
	if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool GlScene3D::initialize(const GlTextureStore& store) {
	store_ = &store;
	std::string err;
	if (!shader_.compile(kWorldVertex, kWorldFragment, &err)) {
		fprintf(stderr, "World3D shader: %s\n", err.c_str());
		return false;
	}

	glGenVertexArrays(1, &vao_);
	glBindVertexArray(vao_);
	glGenBuffers(1, &vbo_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, kMaxVerts * sizeof(WorldVertex), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)12);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return true;
}

void GlScene3D::beginScene(const SceneView& view) {
	// Original GL path (GLES::SetGLState) disables depth test and relies on
	// painter's algorithm (BSP order + sprite depth sort). Keep depth off.
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	shader_.use();
	shader_.setMat4("uMVP", view.mvp);
	shader_.setMat4("uView", view.view);
	shader_.setInt("uTexture", 0);
	shader_.setInt("uPalette", 1);
	shader_.setInt("uFogEnabled", fogEnabled_ ? 1 : 0);
	shader_.setFloat("uFogStart", fogStart_);
	shader_.setFloat("uFogEnd", fogEnd_);
	shader_.setVec4("uFogColor", fogColor_[0], fogColor_[1], fogColor_[2], fogColor_[3]);
	// Identity modulation, matching the currentRenderMode_ = 0 tracker below
	// (kRenderModes[0].mod). Uniforms survive across frames in the program object.
	shader_.setVec4("uColorMod", 1.f, 1.f, 1.f, 1.f);
	glBindVertexArray(vao_);

	vertices_.clear();
	currentTex_ = 0;
	currentPal_ = 0;
	// beginScene set standard alpha blending + fog above; sync the trackers
	// (setRenderMode early-outs while they match).
	currentRenderMode_ = 0;
	currentFogOn_ = fogEnabled_;
}

void GlScene3D::endScene() {
	flush();
	glBindVertexArray(0);
	// Restore the standard blend equation so code outside the scene never sees
	// an ADD/SUB leftover (src/GLES.cpp:626).
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	currentRenderMode_ = -1;
}

void GlScene3D::flush() {
	if (vertices_.empty()) return;

	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(WorldVertex), vertices_.data(),
		GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices_.size());
	vertices_.clear();
}

const GlTexture* GlScene3D::lookup(TextureId tex) const {
	return store_ != nullptr ? store_->lookup(tex) : nullptr;
}

void GlScene3D::bindUnits(const GlTexture* gt) {
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gt != nullptr ? gt->id() : 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gt != nullptr ? gt->paletteId() : 0);
}

void GlScene3D::setTexture(TextureId tex) {
	const GlTexture* gt = lookup(tex);
	const GLuint id = gt != nullptr ? gt->id() : 0;
	const GLuint pal = gt != nullptr ? gt->paletteId() : 0;
	// If the texture changed, flush the accumulated batch first: the batch is
	// drawn as one glDrawArrays, so all vertices in it must share one texture.
	if (currentTex_ == id && currentPal_ == pal) return;
	flush();
	bindUnits(gt);
	currentTex_ = id;
	currentPal_ = pal;
}

// Legacy gles::SetupTexture renderMode switch (src/GLES.cpp:615-715), a
// kRenderModes lookup: the blend equation, the color modulation factor and the
// fog toggle change per sprite mode; a pending batch is flushed first so its
// vertices draw under their own state and uniform.
void GlScene3D::setRenderMode(int renderMode) {
	const int mode = clampRenderMode(renderMode);
	const RenderModeRow& bm = kRenderModes[mode];
	// The ADD/SUB/NONE rows run with fog off (fogMode = 0, src/GLES.cpp:709-715);
	// fog is only ever enabled globally when fogEnabled_ is set.
	const bool fogOn = bm.fog && fogEnabled_;
	// Compare the clamped mode so an out-of-range value cannot defeat the cache.
	if (mode == currentRenderMode_ && fogOn == currentFogOn_) return;
	flush();
	glBlendFunc(glBlendFactor(bm.src), glBlendFactor(bm.dst));
	shader_.setVec4("uColorMod", bm.mod[0], bm.mod[1], bm.mod[2], bm.mod[3]);
	shader_.setInt("uFogEnabled", fogOn ? 1 : 0);
	currentRenderMode_ = mode;
	currentFogOn_ = fogOn;
}

void GlScene3D::submitTriangles(const WorldVertex* verts, int count) {
	if (verts == nullptr || count <= 0) return;
	if ((int)vertices_.size() + count > kMaxVerts) flush();
	vertices_.insert(vertices_.end(), verts, verts + count);
}

void GlScene3D::setFog(bool enabled, float start, float end, const float rgba[4]) {
	fogEnabled_ = enabled;
	fogStart_ = start;
	fogEnd_ = end;
	for (int i = 0; i < 4; ++i) fogColor_[i] = rgba[i];
}

void GlScene3D::drawSky(TextureId sky, float uOffset) {
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	shader_.use();
	// Identity MVP: sky quad is drawn directly in clip space.
	float identity[16] = {
		1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1,
	};
	shader_.setMat4("uMVP", identity);
	shader_.setInt("uTexture", 0);
	shader_.setInt("uPalette", 1);
	// drawSky does not go through beginScene, and uniforms live in the program
	// object across frames: without this reset a modulated sprite mode from the
	// previous frame (e.g. ADD50) would darken the sky (ADR 0019 point 4).
	shader_.setVec4("uColorMod", 1.f, 1.f, 1.f, 1.f);
	glBindVertexArray(vao_);

	bindUnits(lookup(sky));

	// Legacy DrawSkyMap quad in NDC (xyzw after divide): corners ±1.
	// st = (ndc.x*0.5, ndc.y*-0.5 + 0.5) then st[0] -= viewYaw/256.
	const float yawShift = uOffset;
	struct SkyV { float x, y, z, u, v; };
	SkyV q[4] = {
		{ -1.f, -1.f, 0.f, -0.5f - yawShift, 1.f },
		{  1.f, -1.f, 0.f,  0.5f - yawShift, 1.f },
		{  1.f,  1.f, 0.f,  0.5f - yawShift, 0.f },
		{ -1.f,  1.f, 0.f, -0.5f - yawShift, 0.f },
	};
	WorldVertex tri[6] = {
		{ q[0].x, q[0].y, q[0].z, q[0].u, q[0].v },
		{ q[1].x, q[1].y, q[1].z, q[1].u, q[1].v },
		{ q[2].x, q[2].y, q[2].z, q[2].u, q[2].v },
		{ q[0].x, q[0].y, q[0].z, q[0].u, q[0].v },
		{ q[2].x, q[2].y, q[2].z, q[2].u, q[2].v },
		{ q[3].x, q[3].y, q[3].z, q[3].u, q[3].v },
	};
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
	glDisable(GL_BLEND);
}

} // namespace newcore
