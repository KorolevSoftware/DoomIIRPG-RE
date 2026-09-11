#ifndef NEW_RENDER_SOKOL_SGRENDERBACKEND_H
#define NEW_RENDER_SOKOL_SGRENDERBACKEND_H

#include <memory>
#include <string>

#include "render/api/CanvasViewport.h"
#include "render/api/RenderBackend.h"
#include "render/sokol/SgDraw2D.h"
#include "render/sokol/SgEnvironment.h"
#include "render/sokol/SgFrame.h"
#include "render/sokol/SgPipelines.h"
#include "render/sokol/SgScene3D.h"
#include "render/sokol/SgTextureStore.h"

namespace newcore {

class Window;

// The sokol_gfx RenderBackend: owns the sg_* context, the letterboxed viewport
// and the frame pass (spec 2026-09-11-sokol-gfx-backend §5).
//
// Group G5 state: both devices are real. The frame is a command list (SgFrame)
// replayed inside one pass; the 2D layer (SgDraw2D), the world/sky/fog
// (SgScene3D) and the textures (SgTextureStore) all draw. What is left are the
// Metal (G6) and D3D11 (G7) environments.
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
	// Latches vp_ and hands the letterbox to the 2D device; recording the
	// Viewport command is the caller's job (applyViewport also runs at init,
	// before the first frame exists).
	void applyViewport(Window& window);
	// One pass: clear to black, replay the frame command list, commit, optional
	// capture, present.
	void presentFrame(Window& window);
	// Reads back the letterbox rect and writes the pending capture file.
	void writeCapture();
	// The letterbox viewport command every frame starts with.
	void recordViewport();

	std::unique_ptr<SgEnvironment> env_;
	SgTextureStore textures_;
	SgPipelines pipelines_;
	SgFrame frame_;
	SgDraw2D draw2d_;
	SgScene3D scene3d_;
	// Letterboxed canvas rect in drawable pixels, latched by applyViewport.
	CanvasViewport vp_;
	// One-shot frame capture request (see requestCapture).
	std::string capturePath_;
	bool sgValid_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGRENDERBACKEND_H
