#include "render/api/CanvasViewport.h"

namespace newcore {

void CanvasViewport::setFromDrawable(int drawableW, int drawableH) {
	float scale = static_cast<float>(drawableW) / static_cast<float>(kCanvasW);
	int candH = static_cast<int>(kCanvasH * scale);
	if (candH > drawableH) {
		scale = static_cast<float>(drawableH) / static_cast<float>(kCanvasH);
	}
	w = static_cast<int>(kCanvasW * scale);
	h = static_cast<int>(kCanvasH * scale);
	x = (drawableW - w) / 2;
	y = (drawableH - h) / 2;
}

float CanvasViewport::scale() const {
	const float scaleX = static_cast<float>(w) / kCanvasW;
	const float scaleY = static_cast<float>(h) / kCanvasH;
	return scaleX < scaleY ? scaleX : scaleY;
}

bool CanvasViewport::drawableToCanvas(int px, int py, int& cx, int& cy) const {
	if (w <= 0 || h <= 0) {
		cx = -1;
		cy = -1;
		return false;
	}
	const int dx = px - x;
	const int dy = py - y;
	// Integer division truncates toward zero, so a single negative pixel would
	// map to 0 and read as "inside"; report the bar instead.
	cx = dx < 0 ? -1 : dx * kCanvasW / w;
	cy = dy < 0 ? -1 : dy * kCanvasH / h;
	return cx >= 0 && cx < kCanvasW &&
		cy >= 0 && cy < kCanvasH;
}

bool CanvasViewport::canvasSubRect(int cx, int cy, int cw, int ch, bool glBottomUp,
	int& dx, int& dy, int& dw, int& dh) const {
	if (w <= 0 || h <= 0 || cw <= 0 || ch <= 0) return false;
	const float s = scale();
	dx = x + static_cast<int>(cx * s);
	// GL y-axis is bottom-up; canvas y is top-down.
	dy = glBottomUp ? y + static_cast<int>((kCanvasH - cy - ch) * s)
	                : y + static_cast<int>(cy * s);
	dw = static_cast<int>(cw * s);
	dh = static_cast<int>(ch * s);
	return dw >= 1 && dh >= 1;
}

} // namespace newcore
