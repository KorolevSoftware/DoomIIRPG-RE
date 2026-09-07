#ifndef NEW_RENDER_SDL_SDLRENDERBACKEND_H
#define NEW_RENDER_SDL_SDLRENDERBACKEND_H

#include <string>

#include "render/api/CanvasViewport.h"
#include "render/api/RenderBackend.h"
#include "render/sdl/SdlBlendModes.h"
#include "render/sdl/SdlDraw2D.h"
#include "render/sdl/SdlScene3D.h"
#include "render/sdl/SdlTextureStore.h"

namespace newcore {

class Window;

// The SDL_Render RenderBackend (ADR 0021): the portable second implementation.
// It owns nothing of SDL itself — the SDL_Renderer belongs to the Window — and
// keeps the letterbox in the SDL viewport plus the canvas scale in
// SDL_RenderSetScale, so both devices draw in plain canvas coordinates, exactly
// like the GL ones (spec §4.4).
class SdlRenderBackend : public RenderBackend {
public:
	bool initialize(Window& window) override;

	void beginFrame(Window& window) override;
	void endFrame(Window& window) override;

	void setCanvasViewport(int x, int y, int w, int h) override;
	void restoreCanvasViewport(Window& window) override;

	void letterboxRect(int& x, int& y, int& w, int& h) const override;
	bool drawableToCanvas(int px, int py, int& cx, int& cy) const override;

	void flushFrame() override;

	void requestCapture(const char* path) override;

	Draw2D& draw2d() override { return draw2d_; }
	Scene3D& scene3d() override { return scene3d_; }
	TextureStore& textures() override { return textures_; }

	const char* name() const override { return "sdl"; }

private:
	void applyViewport(Window& window);
	// Puts the canvas rect (drawable pixels) in the SDL viewport and the
	// canvas scale in SDL_RenderSetScale. Order matters: SDL multiplies the
	// viewport rect by the scale in effect when it is set, so the scale is
	// reset to 1 first (measured on SDL 2.32.10).
	void applyRect(int dx, int dy, int dw, int dh, float scaleX, float scaleY);
	// Reads back the letterbox rect and writes the pending capture file.
	void writeCapture();

	SDL_Renderer* renderer_ = nullptr;
	SdlTextureStore textures_;
	SdlBlendModes blendModes_;
	SdlDraw2D draw2d_;
	SdlScene3D scene3d_;
	// Letterboxed canvas rect in drawable pixels, latched by applyViewport.
	CanvasViewport vp_;
	// One-shot frame capture request (see requestCapture).
	std::string capturePath_;
};

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLRENDERBACKEND_H
