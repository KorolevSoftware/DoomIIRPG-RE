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

// Do-nothing devices so the game's call sites keep working until G3/G4/G5
// bring the real SgTextureStore, SgDraw2D and SgScene3D (spec §8, group G2).
class SgNullDraw2D final : public Draw2D {
public:
	void drawQuad(TextureId, const SrcRect&, const DstRect&, int, const ColorF&) override {}
	void fillQuad(const DstRect&, const ColorF&) override {}
	void setRenderMode(int) override {}
	void setClipCanvas(int, int, int, int) override {}
	void clearClip() override {}
	void flush() override {}
};

class SgNullScene3D final : public Scene3D {
public:
	void beginScene(const SceneView&) override {}
	void endScene() override {}
	void setTexture(TextureId) override {}
	void setRenderMode(int) override {}
	void submitTriangles(const WorldVertex*, int) override {}
	void setFog(bool, float, float, const float[4]) override {}
	void drawSky(TextureId, float) override {}
	void flush() override {}
};

class SgNullTextureStore final : public TextureStore {
public:
	TextureId createIndexed(const uint8_t*, int, int, const uint16_t*, int,
		TextureFlags) override {
		return TextureId{};
	}
	TextureId createRgba(const uint8_t*, int, int, TextureFlags) override {
		return TextureId{};
	}
	void destroy(TextureId) override {}
	bool query(TextureId, int&, int&) const override { return false; }
	size_t textureBytes() const override { return 0; }
};

SgNullDraw2D g_nullDraw2D;
SgNullScene3D g_nullScene3D;
SgNullTextureStore g_nullTextures;

} // namespace

SgRenderBackend::~SgRenderBackend() {
	if (sgValid_) {
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
	// The viewport command itself is recorded into SgFrame from G4 on; nothing
	// is drawn yet in G2.
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
	// G4 records a Viewport command here; there is no 2D work to flush yet.
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void SgRenderBackend::restoreCanvasViewport(Window& window) {
	applyViewport(window);
}

void SgRenderBackend::beginFrame(Window& window) {
	applyViewport(window);
}

void SgRenderBackend::endFrame(Window& window) {
	presentFrame(window);
}

void SgRenderBackend::flushFrame() {
	// The loading screen presents mid-startup. Without a Window& here there is
	// nothing to acquire a swapchain from, and G2 draws nothing anyway, so the
	// real implementation arrives with the command list in G4.
}

void SgRenderBackend::presentFrame(Window& window) {
	if (!sgValid_) return;

	sg_pass pass = {};
	pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
	pass.action.colors[0].clear_value = sg_color{ 0.0f, 0.0f, 0.0f, 1.0f };
	pass.swapchain = env_->acquireSwapchain(window);
	sg_begin_pass(&pass);
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

Draw2D& SgRenderBackend::draw2d() { return g_nullDraw2D; }
Scene3D& SgRenderBackend::scene3d() { return g_nullScene3D; }
TextureStore& SgRenderBackend::textures() { return g_nullTextures; }

} // namespace newcore
