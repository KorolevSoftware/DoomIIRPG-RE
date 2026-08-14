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
};

} // namespace newcore

#endif // NEW_RENDER_RENDERBACKEND_H