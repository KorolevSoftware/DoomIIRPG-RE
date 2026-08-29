#include "ui/MenuView.h"

#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kCanvasW = 480;
constexpr int kCanvasH = 320;
constexpr int kScrCx = 240;              // Canvas::SCR_CX (src/Canvas.cpp:122)
constexpr int kPanelH = 64;              // gameMenu_Panel_bottom height
// yPos = 20 + (IOS_HEIGHT - panelBottom->height) (src/MenuSystem.cpp:740)
constexpr int kStatusY = 20 + (kCanvasH - kPanelH) + 3;

// Row art alpha: in-game rows get normalRenderMode = RENDER_BLEND25 and
// highlightRenderMode = RENDER_NORMAL with no highlight image
// (src/MenuSystem.cpp:4458-4466, src/Button.cpp:178-183); RENDER_BLEND25 is
// glColor4f(1,1,1,0.25) (src/GLES.cpp:636-640).
constexpr uint8_t kRowAlphaIdle = 64;
constexpr uint8_t kRowAlphaHeld = 255;
// Soft-key art: idle is drawn with render mode 2 = RENDER_BLEND50 -> 128,
// held with a plain drawImage (src/MenuSystem.cpp:5199-5207, :5250-5258).
constexpr uint8_t kSoftAlphaIdle = 128;
constexpr uint8_t kSoftAlphaHeld = 255;

// The two 99x37 soft-key boxes (src/MenuSystem.cpp:5199, :5250). Unlike the
// HUD's narrowed arrow rects, the art here IS the whole soft key, so the legacy
// rect is kept as it is.
constexpr UiRect kSoftLeftRect{ 9, 268, 99, 37 };
constexpr UiRect kSoftRightRect{ 372, 268, 99, 37 };
constexpr int kSoftLeftTextX = 42;
constexpr int kSoftRightTextX = 448;
constexpr int kSoftTextY = 295;

// Nine row slots, matching the legacy 9-button pool
// (src/MenuSystem.cpp:4440-4470).
constexpr int kRowSlots = 9;
// A row whose view-relative top is below this keeps its art but loses its touch
// area: the port moves the rect to y = 350 instead of hiding the art
// (src/MenuSystem.cpp:5099-5102, src/Button.cpp:131-143).
constexpr int kNoTouchBelowY = 210;

// Disabled action row: FMGL_fillRect over the whole row box with the menu
// button's normal colour (src/MenuSystem.cpp:1096-1102), which is never
// assigned anywhere, so it stays the fmButton constructor default
// normalRed/Green/Blue/Alpha = 0.2/0.6/0.3/0.2 (src/Button.cpp:58-61) =
// ARGB 0x3333994C, a translucent green.
//
// UNVERIFIED appearance: no data-disabled row has ever been SEEN in the
// reference build — the photographed `Save Game` row is a normal plate, so it
// was not ITEM_DISABLED there. Both the missing plate below and this wash are
// read off the source, never off a screenshot. What would confirm them: a
// reference run of MENU_INGAME with inventory[18] == 0, which is the one rule
// that turns row 2 into a real ITEM_DISABLED (src/MenuSystem.cpp:1558-1562).
constexpr uint32_t kDisabledOverlay = 0x3333994C;

constexpr char kCursorGlyph = '\x8A';    // Graphics::drawCursor (src/Graphics.cpp:670-686)

UiId rowSlotId(int slot) {
	return (UiId)((int)UiId::MenuRow0 + slot);
}

} // namespace

UiResult drawMenu(Ui& ui, const MenuViewModel& m) {
	UiResult res;
	const UiAssets& art = ui.art();

	// ---- chrome, in the legacy paint order (src/MenuSystem.cpp:734-773) ----
	if (m.drawBottomPanel) {
		ui.image(art.panelBottom, 0, kCanvasH - kPanelH, Graphics2D::kAnchorTop |
			Graphics2D::kAnchorLeft);
	}
	if (m.drawBackground) {
		// Opaque, full screen: the world is not visible behind the menu (:739).
		ui.image(art.menuBackground, 0, 0, Graphics2D::kAnchorTop |
			Graphics2D::kAnchorLeft);
	}
	if (m.statusLine != nullptr) {                                   // (:742-758)
		const int w = m.statusLine->getStringWidth();
		ui.label(*m.statusLine, kScrCx, kStatusY,
			Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter);
		ui.image(art.menuHealth, ((kCanvasW - w) >> 1) - 5, kStatusY,
			Graphics2D::kAnchorRight | Graphics2D::kAnchorVCenter);
		ui.image(art.menuShield, ((w + kCanvasW) >> 1) + 5, kStatusY,
			Graphics2D::kAnchorLeft | Graphics2D::kAnchorVCenter);
	}

	// ---- soft keys (:5181-5292) ----
	bool softLeftFired = false;
	bool softRightFired = false;
	if (m.softLeft != nullptr) {
		ui.imageAlpha(art.menuSoftKey, kSoftLeftRect.x, kSoftLeftRect.y,
			ui.isActive(UiId::MenuSoftLeft) ? kSoftAlphaHeld : kSoftAlphaIdle);
		softLeftFired = ui.softKey(UiId::MenuSoftLeft, *m.softLeft, kSoftLeftTextX,
			kSoftTextY, Graphics2D::kAnchorLeft | Graphics2D::kAnchorBottom,
			kSoftLeftRect);
	}
	if (m.softRight != nullptr) {
		ui.imageAlpha(art.menuSoftKey, kSoftRightRect.x, kSoftRightRect.y,
			ui.isActive(UiId::MenuSoftRight) ? kSoftAlphaHeld : kSoftAlphaIdle);
		softRightFired = ui.softKey(UiId::MenuSoftRight, *m.softRight, kSoftRightTextX,
			kSoftTextY, Graphics2D::kAnchorRight | Graphics2D::kAnchorBottom,
			kSoftRightRect);
	}

	// ---- the list, clipped like the original (:852) ----
	// Faithful use of the scissor, not new UI: this is the one migrated screen
	// whose original sets a clip rect of its own (spec §8.3).
	ui.pushClip(m.clip);

	// Pass 1: row background art + hit areas (:856-867 art, :5062-5160 rects).
	// Legacy draws non-highlighted art first and highlighted art in a second
	// pass so a pressed plate lands on top; here the 296 px rows share one x and
	// never overlap, so one pass with the held alpha is pixel-identical.
	int rowHit = -1;
	{
		int y = -m.scrollPx;
		int slot = 0;
		for (int i = 0; i < m.rowCount && y < m.rect.h; ++i) {
			const MenuRow& r = m.rows[i];
			if (y + r.height > 0 && r.action) {
				const int y0 = y + m.rect.y;
				// Nine slots is never a real limit here: at 56 px of pitch only
				// five rows can intersect a 241 px region, so `slot` stays < 9.
				if (slot < kRowSlots) {
					const UiId id = rowSlotId(slot);
					// A disabled row draws no plate at all in the in-game tree:
					// drawTouchButtons renders the button only when the item is
					// not ITEM_DISABLED (the else branch is MENU_MAIN_OPTIONS
					// only, :5104-5124). The touch area is still assigned there,
					// and select() is what refuses (:2978-2981).
					if (!r.disabled) {
						ui.imageAlpha(art.menuOptionButton, m.rect.x, y0,
							ui.isActive(id) ? kRowAlphaHeld : kRowAlphaIdle);
					}
					if (y <= kNoTouchBelowY) {
						const UiRect hit{ m.rect.x, y0, m.itemWidth, m.itemHeight };
						if (ui.buttonRect(id, hit)) rowHit = i;
					}
					++slot;
				}
			}
			y += r.height;
		}
	}

	// Pass 2: cursor + labels (:905-1130). The legacy loop draws every plate
	// before any text, which is why this is a second walk over the same rows.
	{
		int y = -m.scrollPx;
		for (int i = 0; i < m.rowCount && y < m.rect.h; ++i) {
			const MenuRow& r = m.rows[i];
			if (y + r.height > 0 && r.label != nullptr) {
				const int y0 = y + m.rect.y;
				int labelX;
				if (r.centered) {                                     // (:959-970)
					const int half = r.label->getStringWidth(false) >> 1;
					labelX = r.action ? m.rect.x + (m.itemWidth >> 1) - half
					                  : m.rect.x + (m.rect.w >> 1) - half;
				} else {                                              // (:980-990)
					labelX = r.action ? m.rect.x + 8 : m.rect.x;
				}
				if (i == m.selectedRow) {                             // (:1064-1073)
					const int cx = labelX + m.cursorOffset + 3;
					ui.glyph(kCursorGlyph, cx,
						r.action ? y0 + (m.itemHeight >> 1) - 8 : y0,
						Graphics2D::kAnchorRight);
					labelX += 8;
				}
				if (r.action) {                                       // (:1075-1079)
					ui.label(*r.label, labelX, y0 + (m.itemHeight >> 1),
						Graphics2D::kAnchorVCenter);
				} else {
					ui.label(*r.label, labelX, y0, Graphics2D::kAnchorNone);
				}
				if (r.disabled && r.action) {                         // (:1086-1102)
					ui.panel(UiRect{ m.rect.x, y0, m.itemWidth, m.itemHeight },
						kDisabledOverlay);
				}
				// A disabled LABEL row redraws its text as a run of '\x89'
				// (:1104-1113). Not implemented here: 16 px label rows only
				// exist on the sub-screens, which arrive with G5.
			}
			y += r.height;
		}
	}

	ui.popClip();

	// One intent per frame, in draw order. Row clicks report the ITEM index:
	// the legacy touch handler assigns selectedIndex = button->selectedIndex and
	// then calls select() (:4787-4792), and the producer does the same with the
	// action the FIRE key queues.
	if (softLeftFired) {
		res.action = UiAction::Back;
	} else if (softRightFired) {
		res.action = UiAction::Resume;
	} else if (rowHit >= 0) {
		res.action = UiAction::ListRow;
		res.index = rowHit;
	} else if (ui.in().wheel != 0) {
		// NEW, no original: one notch behaves like one arrow key, the same
		// convention as the loot list and the dialog box.
		res.action = ui.in().wheel > 0 ? UiAction::ScrollUp : UiAction::ScrollDown;
	}
	return res;
}

} // namespace newcore
