#include "render/sdl/SdlRenderBackend.h"

#include <cstdio>
#include <vector>

#include "platform/Window.h"
#include "render/api/BmpWriter.h"

namespace newcore {

bool SdlRenderBackend::initialize(Window& window) {
	renderer_ = window.sdlRenderer();
	if (renderer_ == nullptr) {
		std::fprintf(stderr, "SdlRenderBackend: the window has no SDL_Renderer\n");
		return false;
	}

	if (!textures_.initialize(renderer_)) return false;
	blendModes_.initialize(renderer_);
	if (!draw2d_.initialize(renderer_, textures_, blendModes_)) {
		std::fprintf(stderr, "SdlDraw2D init failed\n");
		return false;
	}
	scene3d_.initialize(renderer_);
	return true;
}

void SdlRenderBackend::applyRect(int dx, int dy, int dw, int dh, float scaleX, float scaleY) {
	SDL_RenderSetScale(renderer_, 1.f, 1.f);
	SDL_Rect rect;
	rect.x = dx;
	rect.y = dy;
	rect.w = dw;
	rect.h = dh;
	SDL_RenderSetViewport(renderer_, &rect);
	SDL_RenderSetScale(renderer_, scaleX, scaleY);
}

void SdlRenderBackend::applyViewport(Window& window) {
	int vx, vy, vw, vh;
	window.computeViewport(vx, vy, vw, vh);
	vp_.x = vx;
	vp_.y = vy;
	vp_.w = vw;
	vp_.h = vh;
	if (vw <= 0 || vh <= 0) return;
	// The canvas is stretched onto the letterbox rect, the same way the GL
	// path lets glViewport stretch NDC onto it.
	applyRect(vx, vy, vw, vh,
		(float)vw / (float)CanvasViewport::kCanvasW,
		(float)vh / (float)CanvasViewport::kCanvasH);
}

void SdlRenderBackend::letterboxRect(int& x, int& y, int& w, int& h) const {
	x = vp_.x;
	y = vp_.y;
	w = vp_.w;
	h = vp_.h;
}

bool SdlRenderBackend::drawableToCanvas(int px, int py, int& cx, int& cy) const {
	return vp_.drawableToCanvas(px, py, cx, cy);
}

void SdlRenderBackend::setCanvasViewport(int x, int y, int w, int h) {
	// Batched quads rasterize at flush time -> flush before any viewport change.
	draw2d_.flush();
	int dx, dy, dw, dh;
	if (!vp_.canvasSubRect(x, y, w, h, /*glBottomUp=*/false, dx, dy, dw, dh)) return;
	// Inside the band the devices draw in band-local canvas units.
	applyRect(dx, dy, dw, dh, (float)dw / (float)w, (float)dh / (float)h);
}

void SdlRenderBackend::restoreCanvasViewport(Window& window) {
	draw2d_.flush();
	applyViewport(window);
}

void SdlRenderBackend::beginFrame(Window& window) {
	// Clear the full target first (letterbox bars). SDL_RenderClear ignores
	// the viewport, but not the clip rect.
	SDL_RenderSetClipRect(renderer_, nullptr);
	SDL_RenderSetScale(renderer_, 1.f, 1.f);
	SDL_RenderSetViewport(renderer_, nullptr);
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
	SDL_RenderClear(renderer_);

	// Restrict drawing to the letterboxed canvas area.
	applyViewport(window);
	draw2d_.begin();
}

void SdlRenderBackend::endFrame(Window& window) {
	draw2d_.end();
	// Everything is rasterized and the target is still intact: this is the
	// only point where the finished frame can be read back (§8.2).
	if (!capturePath_.empty()) writeCapture();
	window.present();
}

void SdlRenderBackend::flushFrame() {
	draw2d_.end();
	SDL_RenderFlush(renderer_);
}

void SdlRenderBackend::requestCapture(const char* path) {
	if (path == nullptr || path[0] == '\0') return;
	capturePath_ = path;
}

void SdlRenderBackend::writeCapture() {
	const std::string path = capturePath_;
	capturePath_.clear(); // one shot, even if the readback or the write fails

	if (vp_.w <= 0 || vp_.h <= 0) {
		std::fprintf(stderr, "capture: letterbox rect is empty, nothing written\n");
		return;
	}

	// SDL reads back through the viewport/scale/clip in effect, so the rect
	// only means drawable pixels once those are neutral again.
	SDL_RenderSetClipRect(renderer_, nullptr);
	SDL_RenderSetScale(renderer_, 1.f, 1.f);
	SDL_RenderSetViewport(renderer_, nullptr);

	SDL_Rect rect;
	rect.x = vp_.x;
	rect.y = vp_.y;
	rect.w = vp_.w;
	rect.h = vp_.h;
	std::vector<uint8_t> pixels((size_t)vp_.w * (size_t)vp_.h * 4);
	// ABGR8888 is packed 0xAABBGGRR, i.e. R,G,B,A in memory: what writeBmp24
	// expects.
	if (SDL_RenderReadPixels(renderer_, &rect, SDL_PIXELFORMAT_ABGR8888,
			pixels.data(), vp_.w * 4) != 0) {
		std::fprintf(stderr, "capture: SDL_RenderReadPixels failed: %s\n", SDL_GetError());
		return;
	}

	// SDL returns the top row first; BMP stores rows bottom-up.
	if (!writeBmp24(path.c_str(), pixels.data(), vp_.w, vp_.h, /*bottomUp=*/false)) {
		std::fprintf(stderr, "capture: failed to write '%s'\n", path.c_str());
		return;
	}
	std::fprintf(stdout, "capture: %s (%dx%d)\n", path.c_str(), vp_.w, vp_.h);
	std::fflush(stdout);
}

} // namespace newcore
