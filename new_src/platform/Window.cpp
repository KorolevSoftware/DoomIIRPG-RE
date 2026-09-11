#include "platform/Window.h"

#include <SDL.h>
#include <cstdio>

#include "render/api/CanvasViewport.h"

namespace newcore {

namespace {

struct VideoMode { int width; int height; };

constexpr VideoMode kVideoModes[] = {
	{480, 320}, {640, 480}, {720, 400}, {720, 480}, {720, 576},
	{800, 600}, {832, 624}, {960, 640}, {1024, 768}, {1152, 864},
	{1152, 872}, {1280, 720}, {1280, 800}, {1280, 1024}, {1440, 900},
	{1600, 1000}, {1680, 1050}, {1920, 1080},
};
constexpr int kNumVideoModes = static_cast<int>(sizeof(kVideoModes) / sizeof(kVideoModes[0]));

} // namespace

Window::Window()
	: api_(GraphicsApi::OpenGL)
	, window_(nullptr), glContext_(nullptr), initialized_(false)
	, resolutionIndex_(0), oldResolutionIndex_(-1)
	, windowMode_(WindowMode::Windowed), oldWindowMode_(static_cast<WindowMode>(-1))
	, vSync_(true), oldVSync_(false)
	, winWidth_(0), winHeight_(0)
	, drawableWidth_(0), drawableHeight_(0) {}

Window::~Window() { shutdown(); }

bool Window::initialize(const char* title, GraphicsApi api) {
	if (initialized_) return true;
	api_ = api;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	// GL 4.1 core on every platform: sokol-shdc's lowest desktop target is
	// glsl410 and macOS caps at exactly 4.1 (spec 2026-09-11 §0.7). No depth
	// buffer is requested - the renderer is painter's-order (§0.3).
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	int modeWidth = kVideoModes[resolutionIndex_].width;
	int modeHeight = kVideoModes[resolutionIndex_].height;

	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
		| SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_OPENGL;
	window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		modeWidth, modeHeight, flags);
	if (!window_) {
		std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return false;
	}

	glContext_ = SDL_GL_CreateContext(window_);
	if (!glContext_) {
		std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(window_);
		window_ = nullptr;
		return false;
	}

	SDL_GetWindowSize(window_, &winWidth_, &winHeight_);
	refreshDrawableSize();

	applyVSync();
	applyResolution();

	SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");

	initialized_ = true;
	return true;
}

void Window::shutdown() {
	if (glContext_) {
		SDL_GL_DeleteContext(glContext_);
		glContext_ = nullptr;
	}
	if (window_) {
		SDL_DestroyWindow(window_);
		window_ = nullptr;
	}
	if (initialized_) {
		SDL_Quit();
		initialized_ = false;
	}
}

int Window::drawableWidth() const { return drawableWidth_; }
int Window::drawableHeight() const { return drawableHeight_; }
int Window::windowWidth() const { return winWidth_; }
int Window::windowHeight() const { return winHeight_; }

void Window::setResolutionIndex(int index) {
	if (index < 0) index = 0;
	if (index >= kNumVideoModes) index = kNumVideoModes - 1;
	resolutionIndex_ = index;
}

void Window::setWindowMode(WindowMode mode) { windowMode_ = mode; }
void Window::setVSync(bool enabled) { vSync_ = enabled; }

bool Window::applyResolution() {
	if (resolutionIndex_ == oldResolutionIndex_) return true;
	oldResolutionIndex_ = resolutionIndex_;

	int w = kVideoModes[resolutionIndex_].width;
	int h = kVideoModes[resolutionIndex_].height;
	SDL_SetWindowSize(window_, w, h);
	SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

	SDL_GetWindowSize(window_, &winWidth_, &winHeight_);
	refreshDrawableSize();
	return true;
}

bool Window::applyWindowMode() {
	if (windowMode_ == oldWindowMode_) return true;
	oldWindowMode_ = windowMode_;

	SDL_SetWindowFullscreen(window_, 0);
	SDL_SetWindowBordered(window_, SDL_TRUE);

	if (windowMode_ == WindowMode::Borderless) {
		SDL_SetWindowBordered(window_, SDL_FALSE);
	} else if (windowMode_ == WindowMode::Fullscreen) {
		SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN);
	}

	SDL_GetWindowSize(window_, &winWidth_, &winHeight_);
	refreshDrawableSize();
	return true;
}

bool Window::applyVSync() {
	if (vSync_ == oldVSync_) return true;
	oldVSync_ = vSync_;
	SDL_GL_SetSwapInterval(vSync_ ? 1 : 0);
	return true;
}

void Window::refreshDrawableSize() {
	SDL_GL_GetDrawableSize(window_, &drawableWidth_, &drawableHeight_);
}

void Window::applyVideoSettings() {
	applyVSync();
	applyWindowMode();
	applyResolution();
	refreshDrawableSize();
}

void Window::computeViewport(int& x, int& y, int& w, int& h) const {
	// The letterbox math lives once, in the backend-neutral core (spec
	// 2026-09-02-render-backend-split §4.4).
	CanvasViewport vp;
	vp.setFromDrawable(drawableWidth_, drawableHeight_);
	x = vp.x;
	y = vp.y;
	w = vp.w;
	h = vp.h;
}

void Window::windowToDrawable(int wx, int wy, int& px, int& py) const {
	px = wx * drawableWidth_ / (winWidth_ > 0 ? winWidth_ : 1);
	py = wy * drawableHeight_ / (winHeight_ > 0 ? winHeight_ : 1);
}

void Window::present() {
	SDL_GL_SwapWindow(window_);
}

} // namespace newcore
