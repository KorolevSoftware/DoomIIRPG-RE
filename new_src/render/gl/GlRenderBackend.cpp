#include "render/gl/GlRenderBackend.h"

#include <cstdio>

#include "platform/Window.h"
#include "render/gl/GlCommon.h"

namespace newcore {

bool GlRenderBackend::initialize(Window& window) {
	std::fprintf(stdout, "OpenGL %s | GLSL %s | Renderer: %s\n",
		(const char*)glGetString(GL_VERSION),
		(const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
		(const char*)glGetString(GL_RENDERER));

	if (!draw2d_.initialize(Window::kCanvasWidth, Window::kCanvasHeight, textures_)) {
		std::fprintf(stderr, "GlDraw2D init failed\n");
		return false;
	}
	if (!scene3d_.initialize(textures_)) {
		std::fprintf(stderr, "GlScene3D init failed\n");
		return false;
	}

	return true;
}

void GlRenderBackend::applyViewport(Window& window) {
	int vx, vy, vw, vh;
	window.computeViewport(vx, vy, vw, vh);
	vp_.x = vx;
	vp_.y = vy;
	vp_.w = vw;
	vp_.h = vh;
	glViewport(vx, vy, vw, vh);
	// The device draws in canvas coords; its scissor needs the letterbox rect.
	draw2d_.setLetterbox(vx, vy, vw, vh);
}

void GlRenderBackend::letterboxRect(int& x, int& y, int& w, int& h) const {
	x = vp_.x;
	y = vp_.y;
	w = vp_.w;
	h = vp_.h;
}

bool GlRenderBackend::drawableToCanvas(int px, int py, int& cx, int& cy) const {
	return vp_.drawableToCanvas(px, py, cx, cy);
}

void GlRenderBackend::setCanvasViewport(int x, int y, int w, int h) {
	// Batched quads rasterize at flush time -> flush before any viewport change.
	draw2d_.flush();
	int gx, gy, gw, gh;
	if (!vp_.canvasSubRect(x, y, w, h, /*glBottomUp=*/true, gx, gy, gw, gh)) return;
	glViewport(gx, gy, gw, gh);
}

void GlRenderBackend::restoreCanvasViewport(Window& window) {
	draw2d_.flush();
	applyViewport(window);
}

void GlRenderBackend::beginFrame(Window& window) {
	// Clear the full framebuffer first (letterbox bars).
	glViewport(0, 0, window.drawableWidth(), window.drawableHeight());
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	// Restrict drawing to the letterboxed canvas area.
	applyViewport(window);
	draw2d_.begin();
}

void GlRenderBackend::endFrame(Window& window) {
	draw2d_.end();
	window.swapBuffers();
}

void GlRenderBackend::flushFrame() {
	draw2d_.end();
	glFlush();
}

void GlRenderBackend::requestCapture(const char* path) {
	// Implemented by spec group G4 (glReadPixels of the letterbox rect between
	// flushFrame and the swap, then render/api/BmpWriter.h).
	std::fprintf(stderr, "GlRenderBackend: frame capture to '%s' is not implemented yet\n",
		path != nullptr ? path : "(null)");
}

} // namespace newcore
