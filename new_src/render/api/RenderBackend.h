#ifndef NEW_RENDER_API_RENDERBACKEND_H
#define NEW_RENDER_API_RENDERBACKEND_H

namespace newcore {

class Window;
class Draw2D;
class Scene3D;
class TextureStore;

// The abstract frame/device owner: frame begin/end, canvas viewport, letterbox
// mapping, capture, plus access to the three sub-interfaces (ADR 0020). Method
// names are unchanged from the concrete RenderBackend this replaces, so
// existing call sites keep compiling.
//
// Graphics2D is deliberately NOT on this interface: it is backend-neutral game
// code that depends on text/Font and therefore cannot live in dr_render_core.
// The composition root owns it.
class RenderBackend {
public:
	virtual ~RenderBackend();

	virtual bool initialize(Window& window) = 0;

	virtual void beginFrame(Window& window) = 0;
	virtual void endFrame(Window& window) = 0;

	// Restricts drawing to a sub-rect of the letterboxed canvas, in canvas
	// coordinates (ADR 0009 world band 1,7,478,248). Flushes 2D work first.
	virtual void setCanvasViewport(int x, int y, int w, int h) = 0;
	virtual void restoreCanvasViewport(Window& window) = 0;

	virtual void letterboxRect(int& x, int& y, int& w, int& h) const = 0;
	virtual bool drawableToCanvas(int px, int py, int& cx, int& cy) const = 0;

	virtual void flushFrame() = 0;

	// Dumps the letterboxed canvas of the NEXT presented frame to a 24-bit BMP
	// at `path` (one shot; the implementation clears the request afterwards).
	virtual void requestCapture(const char* path) = 0;

	virtual Draw2D& draw2d() = 0;
	virtual Scene3D& scene3d() = 0;
	virtual TextureStore& textures() = 0;

	// "gl" / "sdl": window title suffix + log lines + capture file names.
	virtual const char* name() const = 0;
};

} // namespace newcore

#endif // NEW_RENDER_API_RENDERBACKEND_H
