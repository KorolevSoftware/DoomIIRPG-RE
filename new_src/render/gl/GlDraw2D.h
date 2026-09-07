#ifndef NEW_RENDER_GL_GLDRAW2D_H
#define NEW_RENDER_GL_GLDRAW2D_H

#include <cstdint>
#include <vector>

#include "render/api/Draw2D.h"
#include "render/gl/GlCommon.h"
#include "render/gl/Shader.h"

namespace newcore {

class GlTexture;
class GlTextureStore;

// The GL Draw2D device: a batching 2D quad renderer over a dynamic vertex
// buffer. Supports palette-indexed textures (index texture + palette LUT) and
// plain RGBA textures, per-quad tint/alpha, and the legacy rotation modes.
class GlDraw2D : public Draw2D {
public:
	static constexpr int kMaxQuads = 4096;
	static constexpr int kMaxVertices = kMaxQuads * 6;

	struct Vertex {
		float x, y;     // canvas position
		float u, v;     // texel coords (0..1 in texture space)
		float r, g, b, a;
	};

	GlDraw2D();
	~GlDraw2D() override;

	GlDraw2D(const GlDraw2D&) = delete;
	GlDraw2D& operator=(const GlDraw2D&) = delete;

	// canvasWidth/Height: logical canvas size (480x320). Must be called once
	// before drawing (requires a current GL context). The store resolves the
	// TextureIds handed to drawQuad.
	bool initialize(int canvasWidth, int canvasHeight, const GlTextureStore& store);

	void begin();
	void end();

	// --- Draw2D ---
	void drawQuad(TextureId tex, const SrcRect& src, const DstRect& dst,
		int rotateMode, const ColorF& color) override;
	void fillQuad(const DstRect& dst, const ColorF& color) override;
	// Legacy RENDER_* value; only the row's blend function applies here, see
	// the note in GlDraw2D.cpp.
	void setRenderMode(int renderMode) override;
	void setClipCanvas(int x, int y, int w, int h) override;
	void clearClip() override;
	void flush() override;

	// Latched by GlRenderBackend::applyViewport: the full letterboxed canvas
	// rect in drawable pixels. Needed because vertices are canvas-space while
	// the letterbox lives in the GL viewport.
	void setLetterbox(int x, int y, int w, int h);

private:
	void emitQuad(const Vertex* v);
	void applyScissor();

	Shader indexedShader_;
	Shader rgbaShader_;
	Shader colorShader_;

	const GlTextureStore* store_ = nullptr;

	GLuint whiteTex_ = 0;
	GLuint vao_ = 0;
	GLuint vbo_ = 0;

	const GlTexture* boundTex_ = nullptr;
	bool boundIndexed_ = false;

	std::vector<Vertex> vertices_;

	int canvasWidth_ = 0;
	int canvasHeight_ = 0;
	// Active legacy RENDER_* mode of the batch (was SpriteBatch's private
	// 0/1 blend-mode numbering).
	int renderMode_ = 0;
	// Letterboxed canvas rect in drawable pixels (x, y, w, h).
	int letterbox_[4] = { 0, 0, 0, 0 };
	// Requested scissor rect in canvas coords (x, y, w, h).
	int scissorRect_[4] = { 0, 0, 0, 0 };
	bool scissorActive_ = false;
	bool begun_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_GL_GLDRAW2D_H
