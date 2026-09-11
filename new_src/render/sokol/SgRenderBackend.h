#ifndef NEW_RENDER_SOKOL_SGRENDERBACKEND_H
#define NEW_RENDER_SOKOL_SGRENDERBACKEND_H

#include <memory>
#include <string>

#include "render/api/CanvasViewport.h"
#include "render/api/RenderBackend.h"
#include "render/sokol/SgEnvironment.h"

namespace newcore {

class Window;

// The sokol_gfx RenderBackend: owns the sg_* context, the letterboxed viewport
// and the frame pass (spec 2026-09-11-sokol-gfx-backend §5).
//
// Group G2 state: the frame is a plain "clear to black" pass and the three
// devices are do-nothing stubs, so the game's call sites work while
// SgTextureStore (G3), SgDraw2D (G4) and SgScene3D (G5) are still missing.
class SgRenderBackend : public RenderBackend {
public:
	~SgRenderBackend() override;

	bool initialize(Window& window) override;

	void beginFrame(Window& window) override;
	void endFrame(Window& window) override;

	void setCanvasViewport(int x, int y, int w, int h) override;
	void restoreCanvasViewport(Window& window) override;

	void letterboxRect(int& x, int& y, int& w, int& h) const override;
	bool drawableToCanvas(int px, int py, int& cx, int& cy) const override;

	void flushFrame() override;

	void requestCapture(const char* path) override;

	Draw2D& draw2d() override;
	Scene3D& scene3d() override;
	TextureStore& textures() override;

	const char* name() const override { return "sokol"; }

private:
	void applyViewport(Window& window);
	// One clear-to-black pass, commit, optional capture, present.
	void presentFrame(Window& window);
	// Reads back the letterbox rect and writes the pending capture file.
	void writeCapture();

	std::unique_ptr<SgEnvironment> env_;
	// Letterboxed canvas rect in drawable pixels, latched by applyViewport.
	CanvasViewport vp_;
	// One-shot frame capture request (see requestCapture).
	std::string capturePath_;
	bool sgValid_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGRENDERBACKEND_H
