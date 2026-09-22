#include "render/sokol/SgEnvironment.h"

#if defined(SOKOL_METAL)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL.h>
#include <cstdio>

namespace newcore {

namespace {

// The Metal environment: SDL2 stays the window and the input, but everything
// the GPU needs is ours — the MTLDevice, the CAMetalLayer and the per-frame
// drawable (spec 2026-09-11-sokol-gfx-backend §3.3).
//
// There is deliberately NO depth surface anywhere in here: the renderer is
// painter's-order with the depth test off (src/GLES.cpp:89, spec §0.3), so the
// swapchain says SG_PIXELFORMAT_NONE and nothing has to be resized alongside
// the color surface. Do not "helpfully" add one back.
class SgEnvironmentMetal final : public SgEnvironment {
public:
	bool create(Window& window) override {
		// Requires SDL_WINDOW_METAL on the window (Window.cpp sets it for
		// GraphicsApi::Metal).
		view_ = SDL_Metal_CreateView(window.nativeHandle());
		if (!view_) {
			std::fprintf(stderr, "SDL_Metal_CreateView failed: %s\n", SDL_GetError());
			return false;
		}
		layer_ = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view_);
		if (!layer_) {
			std::fprintf(stderr, "SDL_Metal_GetLayer returned nil\n");
			return false;
		}
		device_ = MTLCreateSystemDefaultDevice();
		if (!device_) {
			std::fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n");
			return false;
		}
		layer_.device = device_;
		layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
		layer_.framebufferOnly = YES;
		// Vsync lives on the layer here; Window::applyVSync is GL-only, so a
		// runtime toggle does not reach Metal in G6 (spec §6.1 point 6).
		layer_.displaySyncEnabled = window.vSyncEnabled() ? YES : NO;
		resize(window);
		std::fprintf(stdout, "sokol-metal: %s | drawable %.0fx%.0f\n",
			[[device_ name] UTF8String],
			layer_.drawableSize.width, layer_.drawableSize.height);
		std::fflush(stdout);
		return true;
	}

	void destroy() override {
		drawable_ = nil;
		layer_ = nil;
		device_ = nil;
		if (view_) {
			SDL_Metal_DestroyView(view_);
			view_ = nullptr;
		}
	}

	sg_environment environment() const override {
		sg_environment env = {};
		// BGRA8 is the Metal swapchain format (sokol_gfx.h:5274,
		// MTLPixelFormatBGRA8Unorm above).
		env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
		env.defaults.depth_format = SG_PIXELFORMAT_NONE;
		env.defaults.sample_count = 1;
		env.metal.device = (__bridge const void*)device_;
		return env;
	}

	sg_swapchain acquireSwapchain(Window& window) override {
		(void)window;
		sg_swapchain sc = {};
		// The strong member keeps the drawable alive across sg_commit(), which
		// is where sokol presents it (sokol_gfx.h:17207-17209).
		drawable_ = [layer_ nextDrawable];
		if (!drawable_) {
			if (!noDrawableLogged_) {
				noDrawableLogged_ = true;
				std::fprintf(stderr, "sokol-metal: nextDrawable returned nil, "
					"skipping the pass\n");
				std::fflush(stderr);
			}
			sc.invalid = true; // sokol skips the whole pass (sokol_gfx.h:3044-3049)
			return sc;
		}
		sc.width = (int)layer_.drawableSize.width;
		sc.height = (int)layer_.drawableSize.height;
		sc.sample_count = 1;
		sc.color_format = SG_PIXELFORMAT_BGRA8;
		sc.depth_format = SG_PIXELFORMAT_NONE;
		sc.metal.current_drawable = (__bridge const void*)drawable_;
		return sc;
	}

	void present(Window& window) override {
		(void)window;
		// Nothing else: sg_commit already called presentDrawable. Releasing our
		// reference is the whole job (spec §3.3).
		drawable_ = nil;
	}

	void resize(Window& window) override {
		layer_.drawableSize = CGSizeMake((CGFloat)window.drawableWidth(),
			(CGFloat)window.drawableHeight());
	}

	bool readPixels(int x, int y, int w, int h, uint8_t* rgbaBottomUp) override {
		(void)x; (void)y; (void)w; (void)h; (void)rgbaBottomUp;
		// framebufferOnly drawables cannot be read back and sokol offers no
		// readback anyway; the F12 pixel diff stays a glcore/gl tool
		// (ADR 0026). SgRenderBackend::writeCapture logs the refusal.
		return false;
	}

	const char* apiName() const override { return "sokol-metal"; }

private:
	SDL_MetalView view_ = nullptr;
	CAMetalLayer* layer_ = nil;
	id<MTLDevice> device_ = nil;
	id<CAMetalDrawable> drawable_ = nil;
	bool noDrawableLogged_ = false;
};

} // namespace

std::unique_ptr<SgEnvironment> createSgEnvironment() {
	return std::make_unique<SgEnvironmentMetal>();
}

} // namespace newcore

#endif // SOKOL_METAL
