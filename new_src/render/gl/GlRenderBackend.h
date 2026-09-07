#ifndef NEW_RENDER_GL_GLRENDERBACKEND_H
#define NEW_RENDER_GL_GLRENDERBACKEND_H

#include <string>

#include "render/api/CanvasViewport.h"
#include "render/api/RenderBackend.h"
#include "render/gl/GlDraw2D.h"
#include "render/gl/GlScene3D.h"
#include "render/gl/GlTextureStore.h"

namespace newcore {

class Window;

// The OpenGL RenderBackend: owns the GL state and draw queue for the whole
// frame, sets up the letterboxed viewport and hands out the three devices.
class GlRenderBackend : public RenderBackend {
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

	const char* name() const override { return "gl"; }

private:
	void applyViewport(Window& window);
	// Reads back the letterbox rect and writes the pending capture file.
	void writeCapture();

	GlTextureStore textures_;
	GlDraw2D draw2d_;
	GlScene3D scene3d_;
	// Letterboxed canvas rect in drawable pixels, latched by applyViewport.
	CanvasViewport vp_;
	// One-shot frame capture request (see requestCapture).
	std::string capturePath_;
};

} // namespace newcore

#endif // NEW_RENDER_GL_GLRENDERBACKEND_H
