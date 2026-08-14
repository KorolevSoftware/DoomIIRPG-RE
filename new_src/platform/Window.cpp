#include "platform/Window.h"

#include <SDL.h>
#include <cstdio>

#include "render/gl/GlCommon.h"

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
	: window_(nullptr), glContext_(nullptr), initialized_(false)
	, resolutionIndex_(0), oldResolutionIndex_(-1)
	, windowMode_(WindowMode::Windowed), oldWindowMode_(static_cast<WindowMode>(-1))
	, vSync_(true), oldVSync_(false)
	, winWidth_(0), winHeight_(0)
	, drawableWidth_(0), drawableHeight_(0) {}

Window::~Window() { shutdown(); }

bool Window::initialize(const char* title) {
	if (initialized_) return true;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	int modeWidth = kVideoModes[resolutionIndex_].width;
	int modeHeight = kVideoModes[resolutionIndex_].height;

	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
		| SDL_WINDOW_ALWAYS_ON_TOP;
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
	SDL_GL_GetDrawableSize(window_, &drawableWidth_, &drawableHeight_);

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
	SDL_GL_GetDrawableSize(window_, &drawableWidth_, &drawableHeight_);
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
	SDL_GL_GetDrawableSize(window_, &drawableWidth_, &drawableHeight_);
	return true;
}

bool Window::applyVSync() {
	if (vSync_ == oldVSync_) return true;
	oldVSync_ = vSync_;
	SDL_GL_SetSwapInterval(vSync_ ? 1 : 0);
	return true;
}

void Window::applyVideoSettings() {
	applyVSync();
	applyWindowMode();
	applyResolution();
	SDL_GL_GetDrawableSize(window_, &drawableWidth_, &drawableHeight_);
}

void Window::computeViewport(int& x, int& y, int& w, int& h) const {
	float scale = static_cast<float>(drawableWidth_) / static_cast<float>(kCanvasWidth);
	int candH = static_cast<int>(kCanvasHeight * scale);
	if (candH > drawableHeight_) {
		scale = static_cast<float>(drawableHeight_) / static_cast<float>(kCanvasHeight);
	}
	w = static_cast<int>(kCanvasWidth * scale);
	h = static_cast<int>(kCanvasHeight * scale);
	x = (drawableWidth_ - w) / 2;
	y = (drawableHeight_ - h) / 2;
}

void Window::screenToCanvas(int sx, int sy, int& cx, int& cy) const {
	int vx, vy, vw, vh;
	computeViewport(vx, vy, vw, vh);
	cx = static_cast<int>((sx - vx) * static_cast<float>(kCanvasWidth) / static_cast<float>(vw));
	cy = static_cast<int>((sy - vy) * static_cast<float>(kCanvasHeight) / static_cast<float>(vh));
}

void Window::swapBuffers() { SDL_GL_SwapWindow(window_); }

} // namespace newcore
