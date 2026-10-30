#include "ui/AutomapView.h"

#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;
constexpr int kCentered = Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter;     // 3
constexpr int kBottomLeft = Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft;       // 36
constexpr int kBottomRight = Graphics2D::kAnchorBottom | Graphics2D::kAnchorRight;     // 40
// Soft-key buttons 19/20 (src/Canvas.cpp:354-358).
constexpr UiRect kSoftLeftHit{ 0, 250, 100, 70 };
constexpr UiRect kSoftRightHit{ 380, 250, 100, 70 };

} // namespace

UiResult drawAutomap(Ui& ui, const AutomapModel& m) {
	const UiAssets& art = ui.art();
	ui.image(art.automapBackground, 0, 0, kTopLeft);                          // :71
	for (const AutomapPrim& p : m.prims) {
		if (p.line) {
			ui.g().drawLine(p.x0, p.y0, p.x1, p.y1, (uint8_t)(p.argb >> 16), (uint8_t)(p.argb >> 8),
				(uint8_t)p.argb);
		} else {
			ui.panel(UiRect{ p.x0, p.y0, p.x1, p.y1 }, p.argb);
		}
	}
	if (m.cursor) ui.imageRegion(art.automapCursor, 0, m.cursorSrcY, 4, 4, m.cursorX, m.cursorY, kCentered);   // :322

	UiResult r;
	// :326-347; the font render mode written there has no reader (src/Canvas.h:140).
	if (m.softLeft != nullptr && ui.softKey(UiId::AutomapSoftLeft, *m.softLeft, 15, 310, kBottomLeft, kSoftLeftHit))
		r.action = UiAction::Menu;
	if (m.softRight != nullptr && ui.softKey(UiId::AutomapSoftRight, *m.softRight, 465, 310, kBottomRight, kSoftRightHit))
		r.action = UiAction::Automap;
	return r;
}

} // namespace newcore
