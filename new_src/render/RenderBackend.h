#ifndef NEW_RENDER_RENDERBACKEND_H
#define NEW_RENDER_RENDERBACKEND_H

#include <cstdint>

#include "render/gl/SpriteBatch.h"
#include "render/Graphics2D.h"

namespace newcore {

class Window;

// Owns the OpenGL state and draw queue for the whole frame.
// Sets up the letterboxed viewport and exposes the 2D drawing API.
class RenderBackend {
public:
	RenderBackend();
	~RenderBackend();

	RenderBackend(const RenderBackend&) = delete;
	RenderBackend& operator=(const RenderBackend&) = delete;

	bool initialize(Window& window);

	// Begins a fresh frame: clears the full buffer, then the canvas.
	void beginFrame(Window& window);

	// Ends the frame and presents the back buffer.
	void endFrame(Window& window);

	// Restricts drawing to a sub-rect of the letterboxed canvas, given in
	// canvas coordinates (480x320 logical space, origin top-left). Cinematic
	// letterbox analog of tinyGL setViewport(cinRect) (src/Canvas.cpp:1215).
	void setCanvasViewport(int x, int y, int w, int h);

	// Restores the full letterboxed canvas viewport.
	void restoreCanvasViewport(Window& window);

	// The letterboxed canvas rect in drawable pixels, as latched by the last
	// applyViewport (sub-viewports do not change it).
	void letterboxRect(int& x, int& y, int& w, int& h) const;

	// Drawable-pixel point -> canvas point. Returns false when the point falls
	// in the letterbox bars; cx/cy are still written (not clamped).
	bool drawableToCanvas(int px, int py, int& cx, int& cy) const;

	// Flushes all pending draw calls to the back buffer without presenting.
	// Used for readback/tests before the swap.
	void flushFrame();

	SpriteBatch& batch() { return batch_; }
	Graphics2D& g2d() { return g2d_; }

private:
	void applyViewport(Window& window);

	SpriteBatch batch_;
	Graphics2D g2d_;
	bool initialized_ = false;
	// Letterboxed canvas rect in drawable pixels, latched by applyViewport.
	int canvasVp_[4] = { 0, 0, 0, 0 };
};

} // namespace newcore

#endif // NEW_RENDER_RENDERBACKEND_H