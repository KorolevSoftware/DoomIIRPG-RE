#include "ui/ComicBookView.h"

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kFrameW = 480;
constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;

} // namespace

UiResult drawComicBook(Ui& ui, const ComicBookModel& m) {
	const UiAssets& art = ui.art();
	ui.panel({ 0, 0, 480, 320 }, 0xFFFFFFFF);                    // glClearColor(1,1,1,1) (:137-138)
	ui.image(art.comicFrames[m.frame], m.x, 0, kTopLeft);
	if (m.neighbours) {                                          // (:164-209)
		ui.image(art.comicFrames[m.next], m.x + kFrameW, 0, kTopLeft);
		ui.image(art.comicFrames[m.prev], m.x - kFrameW, 0, kTopLeft);
	}
	if (m.doneButton) {
		// (v << 8) | (v << 16) | 0xFF read as ARGB = RGB(v, v, 255), v = 50 / 150
		// (:954-961, src/Graphics.cpp:88-101).
		ui.panel({ 9, 9, 62, 32 }, 0xFF000000);
		ui.panel({ 10, 10, 60, 30 }, m.doneHighlighted ? 0xFF9696FF : 0xFF3232FF);
		if (m.doneLabel != nullptr) {
			ui.label(*m.doneLabel, 40, 25, Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter);
		}
	}
	return {};
}

} // namespace newcore
