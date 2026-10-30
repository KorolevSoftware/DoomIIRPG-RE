#include "ui/LoadingView.h"

#include <algorithm>
#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"
#include "ui/UiTypes.h"

namespace newcore {

namespace {

constexpr uint32_t kBlack = 0xFF000000;
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr int kScrCx = 240;                         // Canvas::SCR_CX (src/Canvas.cpp:122)
constexpr int kScrCy = 160;                         // Canvas::SCR_CY
constexpr UiRect kBox{ kScrCx - 75, kScrCy - 29, 150, 58 };
constexpr UiRect kBar{ kScrCx - 67, kScrCy + 11, 134, 12 };
constexpr int kTextAnchor = Graphics2D::kAnchorHCenter | Graphics2D::kAnchorTop;   // 0x11
constexpr int kFlipVertical = 8;                    // legacy rotateMode 6 (src/Image.cpp:139-142)

// Graphics::fillRegion (src/Graphics.cpp:429-454): whole tiles from the
// rect's corner, the last column/row truncated to the rect.
void fillRegion(Ui& ui, const Texture& tex, const UiRect& r) {
	if (!tex.valid()) return;
	for (int y = r.y; y < r.y + r.h; y += tex.height()) {
		const int h = std::min(tex.height(), r.y + r.h - y);
		for (int x = r.x; x < r.x + r.w; x += tex.width()) {
			const int w = std::min(tex.width(), r.x + r.w - x);
			ui.imageRegion(tex, 0, 0, w, h, x, y, 0);
		}
	}
}

} // namespace

void drawLoading(Ui& ui, const LoadingModel& m) {
	ui.panel(UiRect{ 0, 0, 480, 320 }, kBlack);                  // eraseRgn(displayRect)
	fillRegion(ui, ui.art().fabricBG, kBox);
	ui.frame(kBox, kWhite);
	ui.label(*m.caption, kScrCx, kScrCy - 22, kTextAnchor);
	ui.label(*m.subCaption, kScrCx, kScrCy - 6, kTextAnchor);
	ui.panel(kBar, kBlack, kWhite);
	const Texture& fire = ui.art().loadingFire;
	const int w = std::min(26, kScrCx + 67 - m.pacifierX);
	const int rotate = (m.pacifierX / 10) % 6 < 3 ? kFlipVertical : 0;
	ui.imageRegion(fire, 0, 0, w, 9, m.pacifierX, kScrCy + 13, 0, rotate);
}

} // namespace newcore
