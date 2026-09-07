#ifndef NEW_RENDER_API_CANVASVIEWPORT_H
#define NEW_RENDER_API_CANVASVIEWPORT_H

namespace newcore {

// The letterbox rect plus the canvas <-> drawable mapping, so the math exists
// exactly once for every backend (ADR 0009).
struct CanvasViewport {
	static constexpr int kCanvasW = 480;
	static constexpr int kCanvasH = 320;

	int x = 0, y = 0, w = 0, h = 0; // letterbox rect in drawable pixels

	// Aspect-preserving letterbox for a drawable of the given size
	// (= Window::computeViewport, platform/Window.cpp:159-169).
	void setFromDrawable(int drawableW, int drawableH);

	// Drawable pixels per canvas pixel: min(w/480, h/320).
	float scale() const;

	// Drawable-pixel point -> canvas point. Returns false when the point falls
	// in the letterbox bars; cx/cy are still written (not clamped).
	bool drawableToCanvas(int px, int py, int& cx, int& cy) const;

	// Canvas sub-rect -> drawable rect. glBottomUp flips y for GL's bottom-up
	// window space. Returns false when the rect (or the letterbox) is
	// degenerate, in which case the caller must leave the viewport alone.
	bool canvasSubRect(int cx, int cy, int cw, int ch, bool glBottomUp,
		int& dx, int& dy, int& dw, int& dh) const;
};

} // namespace newcore

#endif // NEW_RENDER_API_CANVASVIEWPORT_H
