#include "ui/LootView.h"

#include <algorithm>
#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

// Geometry, moved verbatim with the drawing out of LootSession::draw (was
// new_src/core/LootSession.cpp:140-165, port of
// src/LootingSystem.cpp:118-150).
constexpr int kCanvasW = 480;
constexpr int kCanvasH = 320;
constexpr int kScrCx = 240;            // Canvas::SCR_CX (src/Canvas.cpp:122)
constexpr int kViewY = 20;             // viewRect[1], hardcoded (src/Canvas.cpp:124-127)
constexpr int kLineH = 16;             // drawString line height (src/Graphics.cpp:554)

// dialogRect = { viewRect[0], viewRect[1]+16, viewRect[2]-x-1, 48 }
// (src/LootingSystem.cpp:123-127) => {0,36,479,48} on the 480x320 canvas.
constexpr int kBodyX = 0;
constexpr int kBodyY = kViewY + 16;
constexpr int kBodyW = kCanvasW - 1;
constexpr int kBodyH = 48;

constexpr uint32_t kBodyFill = 0xFF660000;    // dark red (:128-129)
constexpr uint32_t kTitleFill = 0xFF000000;   // (:130-131)
constexpr uint32_t kBorder = 0xFFFFFFFF;      // (:132-134)

// The ONLY touch area of this screen, and it is the whole display: legacy
// touchStart dispatches ST_LOOTING to a bare `canvas->handleEvent(6)` with
// pressX/pressY never looked at (src/TouchController.cpp:29-31). Key 6 resolves
// through keys_numeric[5] to ACTION_FIRE (src/InputEventController.cpp:104,
// table 5 = [5,9,1,10,3,6,4,12,2,14]), i.e. exactly what the `E` key does:
// page by 3, close on the last page.
constexpr UiRect kScreenHit{ 0, 0, kCanvasW, kCanvasH };

} // namespace

// Presentation half of the legacy drawLootingMenu (src/LootingSystem.cpp:
// 118-150). Deviation D1 applies: no scissor here, the rows stay whole kLineH
// slots and exactly m.visibleRows of them.
UiResult drawLootList(Ui& ui, const LootListModel& m) {
	UiResult res;
	if (m.text == nullptr || m.text->length() == 0) return res;

	const UiRect body{ kBodyX, kBodyY, kBodyW, kBodyH };
	const UiRect titleBar{ kBodyX, kBodyY - 18, kBodyW, 18 };
	// Four separate calls in the legacy order: body fill, title fill, title
	// outline, body outline. Not folded into two panel(fill, border) calls
	// because that would reorder the two outlines against the title fill.
	ui.panel(body, kBodyFill);
	ui.panel(titleBar, kTitleFill);
	ui.frame(titleBar, kBorder);
	ui.frame(body, kBorder);

	if (m.title != nullptr) {                                     // (:135-139)
		ui.label(*m.title, kScrCx, kBodyY - 16, Graphics2D::kAnchorHCenter, kLineH);
	}

	ui.textRows(*m.text, m.lineIndex, m.indexCapacity, m.topLine, m.visibleRows,
		kBodyX + 5, kBodyY + 1, kLineH,
		Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft);        // (:140-144)

	if (m.lineCount > m.visibleRows) {                            // (:145-150)
		ui.scrollBar(kBodyX + kBodyW, kBodyY + 1, kBodyH - 1, m.topLine,
			std::min(m.topLine + m.visibleRows, m.lineCount), m.lineCount,
			m.visibleRows);
	}

	// A tap anywhere = ACTION_FIRE (see kScreenHit). The rows and the scrollbar
	// get no touch areas of their own: the original registers no buttons at all
	// for this state, so there is nothing to hit-test them with.
	if (ui.buttonRect(UiId::LootBody, kScreenHit)) {
		res.action = UiAction::Activate;
	} else if (ui.in().wheel != 0) {
		// NEW, no original: one notch scrolls one line, going through the same
		// queue entries the arrow keys use (spec §5).
		res.action = ui.in().wheel > 0 ? UiAction::ScrollUp : UiAction::ScrollDown;
	}

	return res;
}

} // namespace newcore
