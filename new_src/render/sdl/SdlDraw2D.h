#ifndef NEW_RENDER_SDL_SDLDRAW2D_H
#define NEW_RENDER_SDL_SDLDRAW2D_H

#include <vector>

#include "render/api/Draw2D.h"
#include "render/sdl/SdlCommon.h"

namespace newcore {

class SdlBlendModes;
class SdlTextureStore;

// The SDL_Render Draw2D device: one batch of SDL_Vertex + indices flushed with
// SDL_RenderGeometry, exactly like the GL device's vertex buffer (spec §4.2.4).
// Solid fills go through a 1x1 white texture so they stay in the same batch and
// keep painter order trivially correct.
//
// Coordinates are plain canvas pixels: the backend puts the letterbox rect in
// the SDL viewport and the canvas scale in SDL_RenderSetScale, so no vertex
// math differs from the GL path.
class SdlDraw2D : public Draw2D {
public:
	static constexpr int kMaxQuads = 4096;

	SdlDraw2D() = default;
	~SdlDraw2D() override;

	SdlDraw2D(const SdlDraw2D&) = delete;
	SdlDraw2D& operator=(const SdlDraw2D&) = delete;

	// The store resolves the TextureIds handed to drawQuad; blend maps the
	// legacy RENDER_* rows to SDL blend modes.
	bool initialize(SDL_Renderer* renderer, const SdlTextureStore& store,
		const SdlBlendModes& blend);

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

private:
	// Appends a quad as two triangles: (0,1,2) and (0,2,3) for TL,TR,BR,BL.
	void emitQuad(const SDL_Vertex* v);
	// The caller's tint multiplied by the current row's modulation color —
	// the vertex-color equivalent of the GL device's uColorMod uniform.
	SDL_Color modulatedColor(const ColorF& color) const;

	SDL_Renderer* renderer_ = nullptr;
	const SdlTextureStore* store_ = nullptr;
	const SdlBlendModes* blend_ = nullptr;

	SDL_Texture* whiteTex_ = nullptr;
	// Texture of the pending batch (whiteTex_ for fills).
	SDL_Texture* boundTex_ = nullptr;

	std::vector<SDL_Vertex> vertices_;
	std::vector<int> indices_;

	// Active legacy RENDER_* mode of the batch.
	int renderMode_ = 0;
	bool clipActive_ = false;
	bool begun_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLDRAW2D_H
