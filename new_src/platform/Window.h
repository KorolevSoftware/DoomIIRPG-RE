#ifndef NEW_PLATFORM_WINDOW_H
#define NEW_PLATFORM_WINDOW_H

#include <cstdint>
#include <string>

struct SDL_Window;

namespace newcore {

enum class WindowMode : int { Windowed = 0, Borderless = 1, Fullscreen = 2 };

class Window {
public:
	Window();
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	bool initialize(const char* title);
	void shutdown();

	// Logical canvas size (fixed game resolution).
	static constexpr int kCanvasWidth = 480;
	static constexpr int kCanvasHeight = 320;

	// Backing resolution.
	int drawableWidth() const;
	int drawableHeight() const;

	// Window size in screen coordinates.
	int windowWidth() const;
	int windowHeight() const;

	// Resolution index into the predefined video mode list.
	int resolutionIndex() const { return resolutionIndex_; }
	void setResolutionIndex(int index);

	WindowMode windowMode() const { return windowMode_; }
	void setWindowMode(WindowMode mode);

	bool vSyncEnabled() const { return vSync_; }
	void setVSync(bool enabled);

	// Apply pending resolution / mode / vsync changes.
	void applyVideoSettings();

	// Letterboxed viewport (in drawable pixels) that preserves canvas aspect.
	void computeViewport(int& x, int& y, int& w, int& h) const;

	// SDL window (screen) coordinates -> drawable pixels. Identity unless the
	// display is HiDPI. The rest of the way to canvas space is
	// RenderBackend::drawableToCanvas.
	void windowToDrawable(int wx, int wy, int& px, int& py) const;

	SDL_Window* nativeHandle() const { return window_; }
	void swapBuffers();

private:
	bool applyResolution();
	bool applyWindowMode();
	bool applyVSync();

	SDL_Window* window_;
	void* glContext_;
	bool initialized_;

	int resolutionIndex_;
	int oldResolutionIndex_;
	WindowMode windowMode_;
	WindowMode oldWindowMode_;
	bool vSync_;
	bool oldVSync_;

	int winWidth_;
	int winHeight_;
	int drawableWidth_;
	int drawableHeight_;
};

} // namespace newcore

#endif // NEW_PLATFORM_WINDOW_H
