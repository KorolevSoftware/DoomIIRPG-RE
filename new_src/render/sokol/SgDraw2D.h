#ifndef NEW_RENDER_SOKOL_SGDRAW2D_H
#define NEW_RENDER_SOKOL_SGDRAW2D_H

#include <cstdint>
#include <vector>

#include "render/api/Draw2D.h"

namespace newcore {

class SgFrame;
class SgPipelines;
class SgTexture;
class SgTextureStore;

// The sokol Draw2D device: the batching 2D quad renderer of GlDraw2D with the
// GL calls replaced by SgFrame::record (spec §5.6). The batching state and the
// flush boundaries are a transliteration on purpose — those boundaries are the
// draw order, and the draw order is the picture.
class SgDraw2D : public Draw2D {
public:
	static constexpr int kMaxQuads = 4096;
	static constexpr int kMaxVertices = kMaxQuads * 6;

	struct Vertex {
		float x, y;     // canvas position
		float u, v;     // texel coords (0..1 in texture space)
		float r, g, b, a;
	};

	SgDraw2D();

	SgDraw2D(const SgDraw2D&) = delete;
	SgDraw2D& operator=(const SgDraw2D&) = delete;

	// canvasWidth/Height: logical canvas size (480x320). The store resolves the
	// TextureIds handed to drawQuad; frame and pipelines are owned by
	// SgRenderBackend and outlive this device.
	bool initialize(int canvasWidth, int canvasHeight, const SgTextureStore& store,
		SgFrame& frame, SgPipelines& pipelines);

	void begin();
	void end();

	// --- Draw2D ---
	void drawQuad(TextureId tex, const SrcRect& src, const DstRect& dst,
		int rotateMode, const ColorF& color) override;
	void fillQuad(const DstRect& dst, const ColorF& color) override;
	void setRenderMode(int renderMode) override;
	void setClipCanvas(int x, int y, int w, int h) override;
	void clearClip() override;
	void flush() override;

	// Latched by SgRenderBackend::applyViewport: the letterboxed canvas rect in
	// drawable pixels. Vertices are canvas-space, the scissor is not.
	void setLetterbox(int x, int y, int w, int h);
	// Full drawable size, needed by clearClip's "scissor everything" command.
	void setDrawableSize(int w, int h);

private:
	void emitQuad(const Vertex* v);
	void applyScissor();

	const SgTextureStore* store_ = nullptr;
	SgFrame* frame_ = nullptr;
	SgPipelines* pipelines_ = nullptr;

	const SgTexture* boundTex_ = nullptr;
	bool boundIndexed_ = false;

	std::vector<Vertex> vertices_;

	int canvasWidth_ = 0;
	int canvasHeight_ = 0;
	// Active legacy RENDER_* mode of the batch.
	int renderMode_ = 0;
	// Letterboxed canvas rect in drawable pixels (x, y, w, h).
	int letterbox_[4] = { 0, 0, 0, 0 };
	int drawableWidth_ = 0;
	int drawableHeight_ = 0;
	// Requested scissor rect in canvas coords (x, y, w, h).
	int scissorRect_[4] = { 0, 0, 0, 0 };
	bool scissorActive_ = false;
	bool begun_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGDRAW2D_H
