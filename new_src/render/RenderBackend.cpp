#include "render/RenderBackend.h"

#include <cstdio>

#include "platform/Window.h"
#include "render/gl/GlCommon.h"

namespace newcore {

RenderBackend::RenderBackend() {
	g2d_.setBatch(&batch_);
}

RenderBackend::~RenderBackend() = default;

bool RenderBackend::initialize(Window& window) {
	std::fprintf(stdout, "OpenGL %s | GLSL %s | Renderer: %s\n",
		(const char*)glGetString(GL_VERSION),
		(const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
		(const char*)glGetString(GL_RENDERER));

	if (!batch_.initialize(Window::kCanvasWidth, Window::kCanvasHeight)) {
		std::fprintf(stderr, "SpriteBatch init failed\n");
		return false;
	}

	initialized_ = true;
	return true;
}

void RenderBackend::applyViewport(Window& window) {
	int vx, vy, vw, vh;
	window.computeViewport(vx, vy, vw, vh);
	canvasVp_[0] = vx;
	canvasVp_[1] = vy;
	canvasVp_[2] = vw;
	canvasVp_[3] = vh;
	glViewport(vx, vy, vw, vh);
}

void RenderBackend::setCanvasViewport(int x, int y, int w, int h) {
	if (canvasVp_[2] <= 0 || canvasVp_[3] <= 0 || w <= 0 || h <= 0) return;
	float scaleX = static_cast<float>(canvasVp_[2]) / Window::kCanvasWidth;
	float scaleY = static_cast<float>(canvasVp_[3]) / Window::kCanvasHeight;
	float scale = scaleX < scaleY ? scaleX : scaleY;
	// GL y-axis is bottom-up; canvas y is top-down.
	int gx = canvasVp_[0] + static_cast<int>(x * scale);
	int gy = canvasVp_[1] + static_cast<int>((Window::kCanvasHeight - y - h) * scale);
	int gw = static_cast<int>(w * scale);
	int gh = static_cast<int>(h * scale);
	if (gw < 1 || gh < 1) return;
	glViewport(gx, gy, gw, gh);
}

void RenderBackend::restoreCanvasViewport(Window& window) {
	applyViewport(window);
}

void RenderBackend::beginFrame(Window& window) {
	// Clear the full framebuffer first (letterbox bars).
	glViewport(0, 0, window.drawableWidth(), window.drawableHeight());
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	// Restrict drawing to the letterboxed canvas area.
	applyViewport(window);
	batch_.begin();
}

void RenderBackend::endFrame(Window& window) {
	batch_.end();
	window.swapBuffers();
}

void RenderBackend::flushFrame() {
	batch_.end();
	glFlush();
}

} // namespace newcore