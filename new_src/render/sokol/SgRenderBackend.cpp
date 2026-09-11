#include "render/sokol/SgRenderBackend.h"

#include <cstdio>
#include <vector>

#include "platform/Window.h"
#include "render/api/BmpWriter.h"
#include "render/api/Draw2D.h"
#include "render/api/Scene3D.h"
#include "render/api/TextureStore.h"

namespace newcore {

namespace {

void sgLog(const char* tag, uint32_t level, uint32_t itemId, const char* message,
	uint32_t lineNr, const char* filename, void* userData) {
	(void)itemId;
	(void)userData;
	static const char* kLevel[] = { "panic", "error", "warning", "info" };
	const char* levelName = (level < 4) ? kLevel[level] : "?";
	std::fprintf(stderr, "%s: %s: %s (%s:%u)\n",
		tag ? tag : "sg", levelName, message ? message : "",
		filename ? filename : "sokol_gfx.h", lineNr);
	std::fflush(stderr);
}

} // namespace

SgRenderBackend::~SgRenderBackend() {
	if (sgValid_) {
		// Every sg_destroy_* must happen while the context is still up.
		textures_.shutdown();
		pipelines_.shutdown();
		frame_.shutdown();
		sg_shutdown();
		sgValid_ = false;
	}
	if (env_) env_->destroy();
}

bool SgRenderBackend::initialize(Window& window) {
	env_ = createSgEnvironment();
	if (!env_) return false; // the factory logged why
	if (!env_->create(window)) {
		std::fprintf(stderr, "sokol: environment creation failed\n");
		return false;
	}

	sg_desc desc = {};
	desc.environment = env_->environment();
	desc.buffer_pool_size = 8;
	desc.image_pool_size = 8192;    // 2 images per indexed texture (spec §0.6)
	desc.view_pool_size = 8192;     // 2 views  per indexed texture
	desc.sampler_pool_size = 8;
	desc.shader_pool_size = 8;
	desc.pipeline_pool_size = 32;
	desc.uniform_buffer_size = 8 * 1024 * 1024;
	// Without a logger sokol's validation failures are silent aborts.
	desc.logger.func = sgLog;
	sg_setup(&desc);
	if (!sg_isvalid()) {
		std::fprintf(stderr, "sokol: sg_setup failed\n");
		return false;
	}
	sgValid_ = true;

	// The whole memory advantage is the R8 index texture (spec §0.1).
	if (!sg_query_pixelformat(SG_PIXELFORMAT_R8).sample) {
		std::fprintf(stderr, "sokol: SG_PIXELFORMAT_R8 is not sampleable on this device\n");
		return false;
	}

	if (!textures_.initialize()) return false; // the store logged why
	// A pipeline's color format must match the pass it is used in.
	if (!pipelines_.initialize(env_->environment().defaults.color_format)) return false;
	if (!frame_.initialize()) return false;
	if (!draw2d_.initialize(Window::kCanvasWidth, Window::kCanvasHeight,
			textures_, frame_, pipelines_)) {
		std::fprintf(stderr, "SgDraw2D init failed\n");
		return false;
	}
	if (!scene3d_.initialize(textures_, frame_, pipelines_)) {
		std::fprintf(stderr, "SgScene3D init failed\n");
		return false;
	}

	applyViewport(window);
	std::fprintf(stdout, "sokol_gfx backend: %s | swapchain %dx%d\n",
		env_->apiName(), window.drawableWidth(), window.drawableHeight());
	std::fflush(stdout);
	return true;
}

void SgRenderBackend::applyViewport(Window& window) {
	int vx, vy, vw, vh;
	window.computeViewport(vx, vy, vw, vh);
	vp_.x = vx;
	vp_.y = vy;
	vp_.w = vw;
	vp_.h = vh;
	// The device draws in canvas coords; its scissor needs the letterbox rect,
	// and clearClip needs the full drawable.
	draw2d_.setDrawableSize(window.drawableWidth(), window.drawableHeight());
	draw2d_.setLetterbox(vx, vy, vw, vh);
}

void SgRenderBackend::recordViewport() {
	SgFrame::Cmd cmd;
	cmd.kind = SgFrame::Kind::Viewport;
	cmd.x = vp_.x;
	cmd.y = vp_.y;
	cmd.w = vp_.w;
	cmd.h = vp_.h;
	frame_.record(cmd);
}

void SgRenderBackend::letterboxRect(int& x, int& y, int& w, int& h) const {
	x = vp_.x;
	y = vp_.y;
	w = vp_.w;
	h = vp_.h;
}

bool SgRenderBackend::drawableToCanvas(int px, int py, int& cx, int& cy) const {
	return vp_.drawableToCanvas(px, py, cx, cy);
}

void SgRenderBackend::setCanvasViewport(int x, int y, int w, int h) {
	// Batched quads rasterize at replay time -> flush before any viewport change.
	draw2d_.flush();
	int dx, dy, dw, dh;
	// Top-left origin: sokol does the per-backend flip itself (spec §0.4).
	if (!vp_.canvasSubRect(x, y, w, h, /*glBottomUp=*/false, dx, dy, dw, dh)) return;
	SgFrame::Cmd cmd;
	cmd.kind = SgFrame::Kind::Viewport;
	cmd.x = dx;
	cmd.y = dy;
	cmd.w = dw;
	cmd.h = dh;
	frame_.record(cmd);
}

void SgRenderBackend::restoreCanvasViewport(Window& window) {
	draw2d_.flush();
	applyViewport(window);
	recordViewport();
}

void SgRenderBackend::beginFrame(Window& window) {
	applyViewport(window);
	frame_.beginFrame();
	// sg_begin_pass resets the viewport to the full framebuffer, so the
	// letterbox has to be re-applied at the head of every frame.
	recordViewport();
	draw2d_.begin();
}

void SgRenderBackend::endFrame(Window& window) {
	draw2d_.end();
	presentFrame(window);
}

void SgRenderBackend::flushFrame() {
	// GlRenderBackend::flushFrame ends the 2D device and calls glFlush without
	// presenting (GlRenderBackend.cpp:86-89); the sokol equivalent of that
	// glFlush is nothing at all, because no GPU work has been submitted yet.
	draw2d_.end();
}

void SgRenderBackend::presentFrame(Window& window) {
	if (!sgValid_) return;

	sg_pass pass = {};
	pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
	pass.action.colors[0].clear_value = sg_color{ 0.0f, 0.0f, 0.0f, 1.0f };
	pass.swapchain = env_->acquireSwapchain(window);
	sg_begin_pass(&pass);
	frame_.replay();
	sg_end_pass();
	sg_commit();

	// The back buffer is still intact here, before the swap: the only point
	// where the finished frame can be read back.
	if (!capturePath_.empty()) writeCapture();
	env_->present(window);
}

void SgRenderBackend::requestCapture(const char* path) {
	if (path == nullptr || path[0] == '\0') return;
	capturePath_ = path;
}

void SgRenderBackend::writeCapture() {
	const std::string path = capturePath_;
	capturePath_.clear(); // one shot, even if the readback or the write fails

	if (vp_.w <= 0 || vp_.h <= 0) {
		std::fprintf(stderr, "capture: letterbox rect is empty, nothing written\n");
		return;
	}

	std::vector<uint8_t> pixels((size_t)vp_.w * (size_t)vp_.h * 4);
	if (!env_->readPixels(vp_.x, vp_.y, vp_.w, vp_.h, pixels.data())) {
		std::fprintf(stderr, "capture: not supported on the %s environment\n",
			env_->apiName());
		return;
	}

	// readPixels returns the bottom row first, which is exactly BMP order.
	if (!writeBmp24(path.c_str(), pixels.data(), vp_.w, vp_.h, /*bottomUp=*/true)) {
		std::fprintf(stderr, "capture: failed to write '%s'\n", path.c_str());
		return;
	}
	std::fprintf(stdout, "capture: %s (%dx%d)\n", path.c_str(), vp_.w, vp_.h);
	std::fflush(stdout);
}

Draw2D& SgRenderBackend::draw2d() { return draw2d_; }
Scene3D& SgRenderBackend::scene3d() { return scene3d_; }
TextureStore& SgRenderBackend::textures() { return textures_; }

} // namespace newcore
