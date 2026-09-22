#include "render/sokol/SgEnvironment.h"

#if defined(SOKOL_GLCORE)

#include <SDL.h>
#include <cstdio>

// sokol_gfx.h pulls the platform GL headers in its implementation part only,
// so this file selects them itself (same choice sokol makes at
// sokol_gfx.h:6005-6055). render/gl/GlCommon.h is off limits here (spec §3.2).
#if defined(__APPLE__)
	#include <OpenGL/gl3.h>
#elif defined(_WIN32)
	#include <GL/gl.h>
#else
	#define GL_GLEXT_PROTOTYPES
	#include <GL/gl.h>
#endif

namespace newcore {

namespace {

// The GL environment: Window already created the 4.1 core context, so there is
// no device to make. Only presentation and readback are ours (spec §3.2).
class SgEnvironmentGl final : public SgEnvironment {
public:
	bool create(Window& window) override {
		(void)window;
		std::fprintf(stdout, "sokol-gl: OpenGL %s | GLSL %s | Renderer: %s\n",
			(const char*)glGetString(GL_VERSION),
			(const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
			(const char*)glGetString(GL_RENDERER));
		return true;
	}

	void destroy() override {}

	sg_environment environment() const override {
		sg_environment env = {};
		env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
		// No depth buffer anywhere: the renderer is painter's-order with the
		// depth test off (src/GLES.cpp:89, spec §0.3).
		env.defaults.depth_format = SG_PIXELFORMAT_NONE;
		env.defaults.sample_count = 1;
		return env;
	}

	sg_swapchain acquireSwapchain(Window& window) override {
		sg_swapchain sc = {};
		const int w = window.drawableWidth();
		const int h = window.drawableHeight();
		if (w <= 0 || h <= 0) {
			sc.invalid = true; // sokol skips the whole pass (sokol_gfx.h:3044-3049)
			return sc;
		}
		sc.width = w;
		sc.height = h;
		sc.sample_count = 1;
		sc.color_format = SG_PIXELFORMAT_RGBA8;
		sc.depth_format = SG_PIXELFORMAT_NONE;
		sc.gl.framebuffer = 0; // the default framebuffer
		return sc;
	}

	void present(Window& window) override {
		SDL_GL_SwapWindow(window.nativeHandle());
	}

	void resize(Window& window) override { (void)window; }

	bool readPixels(int x, int y, int w, int h, uint8_t* rgbaBottomUp) override {
		glPixelStorei(GL_PACK_ALIGNMENT, 4); // rows are w*4 bytes, always aligned
		glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBottomUp);
		// GL returns the bottom row first, which is already BMP order.
		return true;
	}

	const char* apiName() const override { return "sokol-gl"; }
};

} // namespace

std::unique_ptr<SgEnvironment> createSgEnvironment() {
	return std::make_unique<SgEnvironmentGl>();
}

} // namespace newcore

#endif // SOKOL_GLCORE
