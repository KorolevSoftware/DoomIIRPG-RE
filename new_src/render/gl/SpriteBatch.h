#ifndef NEW_RENDER_GL_SPRITEBATCH_H
#define NEW_RENDER_GL_SPRITEBATCH_H

#include <cstdint>
#include <vector>

#include "render/gl/GlCommon.h"
#include "render/gl/Shader.h"
#include "render/gl/Texture.h"

namespace newcore {

// Batching 2D quad renderer over a dynamic vertex buffer.
// Supports palette-indexed textures (index texture + palette LUT) and plain
// RGBA textures, per-quad tint/alpha, and the legacy rotation modes.
class SpriteBatch {
public:
	static constexpr int kMaxQuads = 4096;
	static constexpr int kMaxVertices = kMaxQuads * 6;

	struct Vertex {
		float x, y;     // canvas position
		float u, v;     // texel coords (0..1 in texture space)
		float r, g, b, a;
	};

	SpriteBatch();
	~SpriteBatch();

	SpriteBatch(const SpriteBatch&) = delete;
	SpriteBatch& operator=(const SpriteBatch&) = delete;

	// canvasWidth/Height: logical canvas size (480x320). Must be called once
	// before drawing (requires a current GL context).
	bool initialize(int canvasWidth, int canvasHeight);

	void begin();
	void end();

	// Draws a textured quad. src in texture pixels; dst in canvas pixels.
	// rotateMode matches legacy Image::DrawTexture (0..8). tint applied as
	// vertex color (multiplied by sampled color). alpha overrides tint.a.
	void draw(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
		int dstX, int dstY, int dstW, int dstH, int rotateMode,
		float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);

	// Solid color quad (1x1 white texture).
	void fillRect(int x, int y, int w, int h,
		float r, float g, float b, float a = 1.f);

	// Fills a solid rectangle covering the whole canvas.
	void clear(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);

	int canvasWidth() const { return canvasWidth_; }
	int canvasHeight() const { return canvasHeight_; }

private:
	void flush();
	void flushColor();
	void flushIndexed();
	void ensureCapacity(int quads);
	void emitQuad(const Vertex* v);

	Shader indexedShader_;
	Shader rgbaShader_;
	Shader colorShader_;

	GLuint whiteTex_ = 0;
	GLuint vao_ = 0;
	GLuint vbo_ = 0;
	GLsizei vertexCount_ = 0;

	const Texture* boundTex_ = nullptr;
	bool boundIndexed_ = false;

	// Current draw state.
	const Texture* currentTex_ = nullptr;
	bool currentIndexed_ = false;

	std::vector<Vertex> vertices_;
	GLint locViewport_ = -1;

	int canvasWidth_ = 0;
	int canvasHeight_ = 0;
	bool initialized_ = false;
	bool begun_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_GL_SPRITEBATCH_H