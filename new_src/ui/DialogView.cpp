#include "ui/DialogView.h"

#include <cstdint>

#include "render/Graphics2D.h"
#include "render/gl/Texture.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

// Geometry constants, moved verbatim with the drawing out of
// DialogSystem.cpp:19-49 (port of src/DialogSystem.cpp:114-518).
constexpr int kCanvasW = 480;
constexpr int kCanvasH = 320;
constexpr int kScrCx = 240;                 // Canvas::SCR_CX
constexpr int kLineH = 16;                  // drawString line height (src/Graphics.cpp:554)
constexpr int kHudTopPinnedY = 20;          // hudRect[1]+20; hudRect[1]=screenRect[1]=0 (src/Canvas.cpp:139-140)
// Applet::FONT_HEIGHT[fontType] for the 480x320 build (src/App.h:39: font
// types 0 and 1 are both 16), used only for the body touch area of the
// header-strip styles (src/DialogSystem.cpp:243).
constexpr int kFontHeight = 16;

// Style fills (switch at src/DialogSystem.cpp:137-214).
constexpr uint32_t kColorWhite = 0xFFFFFFFF;
constexpr uint32_t kHeaderGray = 0xFF666666;      // default color2 (:139)
constexpr uint32_t kPlayerDlgColor = 0xFF005617;  // Canvas::PLAYER_DLG_COLOR (src/Canvas.h:112)

// Page-icon buttons, registered as dialog buttons 5/6/7 at 90x90
// (src/Canvas.cpp:329-346). The 72x72 art is centred in the 90x90 rect by
// fmButton::SetImage(center=true) (src/Button.cpp:67-75), i.e. +9/+9.
constexpr UiRect kPageUpRect{ 390, 20, 90, 90 };
constexpr UiRect kPageDownRect{ 390, 110, 90, 90 };   // page-ok shares this rect

// normalRenderMode 12 = RENDER_BLEND75 -> glColor4f(1,1,1,0.75)
// (src/Canvas.cpp:333,339,345; src/GLES.cpp:691-695), highlightRenderMode 0 =
// RENDER_NORMAL -> fully opaque. 0.75 * 255 = 191.
constexpr uint8_t kPageIconAlpha = 191;
constexpr uint8_t kPageIconAlphaActive = 255;

// Per-channel brightness scaling of an ARGB color, brightness 0..256
// (gradient row math at src/DialogSystem.cpp:286-287; the legacy packed
// expression is reproduced here per-channel — same visual ramp).
uint32_t scaleColor(uint32_t argb, int brightness) {
	uint32_t r = (((argb >> 16) & 0xFFu) * (uint32_t)brightness) >> 8;
	uint32_t gr = (((argb >> 8) & 0xFFu) * (uint32_t)brightness) >> 8;
	uint32_t b = ((argb & 0xFFu) * (uint32_t)brightness) >> 8;
	return 0xFF000000u | (r << 16) | (gr << 8) | b;
}

// One text run, optionally recomposed with the '^2' colour code. Style 9 draws
// green that way in the legacy code (currentCharColor = 2,
// src/DialogSystem.cpp:249-251,349-351): the two prefix chars are why the
// drawn length is `len + 2` with strBeg 0.
void textRunColored(Ui& ui, const DialogViewModel& m, int start, int len,
	int x, int y, int anchorFlags, bool greenText) {
	if (greenText && m.scratch != nullptr) {
		Text& s = *m.scratch;
		s.setLength(0);
		s.append('^');
		s.append('2');
		s.append(*m.text, start, len);
		ui.textRun(s, 0, len + 2, x, y, anchorFlags, kLineH);
	} else {
		ui.textRun(*m.text, start, len, x, y, anchorFlags, kLineH);
	}
}

// Draws one page icon and hit-tests its 90x90 rect. The icon is only
// clickable on the frames it is drawn on, which is the legacy rule: the touch
// scan skips buttons with drawButton == false
// (src/Button.cpp:277-287 GetTouchedButtonID).
bool pageIcon(Ui& ui, UiId id, const UiRect& r, const Texture& tex) {
	const uint8_t alpha = ui.isActive(id) ? kPageIconAlphaActive : kPageIconAlpha;
	ui.imageAlpha(tex, r.x + (r.w - tex.width()) / 2, r.y + (r.h - tex.height()) / 2,
		alpha);
	return ui.buttonRect(id, r);
}

} // namespace

// Presentation half of the legacy dialogState (src/DialogSystem.cpp:114-518,
// ported at new_src/domain/game/DialogSystem.cpp:358-574 before this group).
// Deviation D1 applies: no scissor here, the rows stay whole kLineH slots.
UiResult drawDialog(Ui& ui, const DialogViewModel& m) {
	UiResult res;
	if (m.text == nullptr || m.text->length() == 0) return res;

	const UiAssets& art = ui.art();
	const Texture& uiImages = art.uiImages;

	int rx = 0;                                  // -screenRect[0], screen origin 0
	int rw = kCanvasW;                           // hudRect[2]
	int rh = m.viewLines * kLineH + 8;           // (:133)
	int ry = kCanvasH - rh - 1;                  // (:134)
	int textX = rx + 1;                          // (:136)
	uint32_t fill = 0xFF000000;
	uint32_t border = kColorWhite;
	uint32_t headerCol = kHeaderGray;
	bool greenText = false;

	switch (m.style) {
	case 3:                                      // scroll-log layout (141-148)
		ry -= 10;
		// The legacy 12800 literal carries no alpha byte and the port's
		// fillArgb dropped it, so this fill has always been opaque here.
		ui.panel(UiRect{ rx, ry - 10, rw, rh + 20 }, 0xFF003200);
		ui.frame(UiRect{ rx, ry - 10, rw - 1, rh + 19 }, border);
		break;
	case 16: headerCol = 0xFF000066; break;      // (150-153): blue HEADER strip, body stays black
	case 4:                                      // loot popup (154-161)
		fill = (m.flags & 0x1) != 0 ? 0xFFB18A01u : 0xFF005A00u;
		break;
	case 11:                                     // VIOS terminal (162-169)
		fill = 0xFF800000;
		if ((m.flags & 0x2) != 0) ry = kHudTopPinnedY;
		break;
	case 5:                                      // NPC bubble (170-177)
		fill = 0xFF800000;
		if ((m.flags & 0x2) != 0) ry = kHudTopPinnedY;
		break;
	case 8:                                      // hero speech (178-182)
		ry -= 64;
		fill = kPlayerDlgColor;
		break;
	case 14:                                     // (183-191) falls into the navy label
		ry -= 20;
		[[fallthrough]];
	case 1: case 6:                              // (187-190) comm-link navy
		fill = 0xFF002864;
		break;
	case 9:                                      // terminal/log (192-196)
		fill = 0xFF000000;
		headerCol = 0xFF000000;
		greenText = true;
		break;
	case 10:                                     // (197-201)
		fill = 0xFF2E0854;
		ry = kHudTopPinnedY;
		break;
	case 12: case 13:                            // choice boxes (202-209)
		fill = 0xFFB18A01;
		break;
	case 15:
		fill = 0xFFFF9600;
		break;
	default:
		break;
	}

	// Touch area of dialog button 8, the box body. The original sets it only
	// in the header-strip and item-popup branches (src/DialogSystem.cpp:243,
	// :270,:274) and never clears it, so for the remaining styles the button
	// keeps a stale rect — art of a legacy bug, not reproduced: those styles
	// simply get no body hit area here.
	UiRect bodyHit{ 0, 0, 0, 0 };

	if (m.style == 2 || m.style == 16 || m.style == 9) {
		// Title-bar layout (230-253): 18px header strip above the box with the
		// first '|'-line centered in it as the speaker/title.
		ui.panel(UiRect{ rx, ry, rw, rh }, fill);
		ui.panel(UiRect{ rx, ry - 18, rw, 18 }, headerCol);
		ui.frame(UiRect{ rx, ry - 18, rw - 1, 18 }, border);
		ui.frame(UiRect{ rx, ry, rw - 1, rh }, border);
		if (m.titleLen > 0) {                    // legacy drawTitle early-out
			textRunColored(ui, m, m.titleStart, m.titleLen, rx + kScrCx, ry - 16,
				Graphics2D::kAnchorHCenter, greenText);
		}
		bodyHit = UiRect{ rx, ry - kFontHeight - 2, rw, kFontHeight + rh + 2 };
	} else if (m.style == 4) {
		// Item-pickup box (254-274). The dialogItem name bar (259-269) needs
		// the loot composer, so this is the legacy `dialogItem == nullptr`
		// branch, including its body touch area (:274).
		ui.panel(UiRect{ rx, ry, rw, rh }, fill);
		ui.frame(UiRect{ rx, ry, rw - 1, rh }, border);
		bodyHit = UiRect{ rx, ry, rw, rh };
	} else if (m.style != 3) {
		ui.panel(UiRect{ rx, ry, rw, rh }, fill);
		ui.frame(UiRect{ rx, ry, rw - 1, rh }, border);
		if (m.style == 8) {
			// Vertical gradient rows (281-289). One 1px-high panel per row is
			// the same span of pixels the drawLine port emitted.
			int y0 = ry + 1;
			int y1 = y0 + (rh - 1);
			for (int y = y0 + 1; y < y1; ++y) {
				int b = 96 + ((((256 - (((y - y0) << 8) / (y1 - y0))) * 160)) >> 8);
				ui.panel(UiRect{ rx + 1, y, rw - 2, 1 }, scaleColor(fill, b));
			}
			if (uiImages.valid()) {
				ui.imageRegion(uiImages, 30, 0, 15, 9, kScrCx + 10, ry + rh, 0);  // corner icon (290)
			}
			// Hero portrait from Hud_Portrait_Small.bmp (imgPortraitsSM, 20x60,
			// height/3 rows; row = characterChoice-1, :291-308). The rewrite has
			// no character selection, so choice is fixed to the first marine =
			// row 0 (spec §9).
			const Texture& portraits = art.portraitsSmall;
			if (portraits.valid() && portraits.height() / 3 > 0) {
				ui.imageRegion(portraits, 0, 0, portraits.width(), portraits.height() / 3,
					rx + 2, ry + 3, 0);
				textX += portraits.width() + 2;  // text starts after portrait (309-310)
			} else {
				// Documented fallback: colored header strip in lieu of a portrait.
				ui.panel(UiRect{ rx, ry - 12, rw, 12 }, fill);
				ui.frame(UiRect{ rx, ry - 12, rw - 1, 12 }, border);
			}
		} else if (m.style == 5) {
			// Speech-bubble tails (312-319).
			if (uiImages.valid()) {
				if ((m.flags & 0x2) != 0) {
					ui.imageRegion(uiImages, 0, 12, 10, 6, kScrCx - 64, ry + rh + 6,
						Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft);
				} else {
					ui.imageRegion(uiImages, 0, 0, 10, 6, kScrCx - 64, ry + 1,
						Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft);
				}
			}
		} else if (m.style == 1) {
			// Tail arrow above the box (320-322).
			if (uiImages.valid()) {
				ui.imageRegion(uiImages, 10, 0, 10, 6, kScrCx - 64, ry + 1,
					Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft);
			}
		} else if (m.style == 10) {
			if (uiImages.valid()) ui.imageRegion(uiImages, 20, 6, 10, 6, kScrCx + 10, ry + rh, 0);
		} else if (m.style == 14) {
			if (uiImages.valid()) ui.imageRegion(uiImages, 45, 0, 15, 9, kScrCx + 10, ry + rh, 0);
		}
	}

	// Text lines (333-354). The reveal counts come from the producer, which
	// still owns the typewriter clock; an unrevealed row keeps its slot.
	int ty = ry + 2;
	for (int i = 0; i < m.rowCount; ++i) {
		if (m.rows[i].visible > 0) {
			textRunColored(ui, m, m.rows[i].start, m.rows[i].visible, textX, ty,
				Graphics2D::kAnchorLeft, greenText);
		}
		ty += kLineH;
	}

	// var4 Yes/No widgets (flags&2 vertical pair, flags&4/&1 bottom pair,
	// src/DialogSystem.cpp:355-452): still deferred — the map00 intro chain
	// uses only flagless styles 1/8. var4 cursor logic is live in
	// DialogSystem::handleInput.

	// Scrollbar + page icons (454-489).
	if (!m.showScrollBar) {
		if (m.showPageOk && pageIcon(ui, UiId::DialogOk, kPageDownRect, art.pageOk)) {
			if (m.activateFires) res.action = UiAction::Activate;
		}
	} else {
		ui.scrollBar(rx + rw - 1, ry + 2, rh - 4, m.scrollTop, m.scrollPageEnd,
			m.scrollNumLines, m.viewLines);
		if (uiImages.valid()) {
			// Page-up fires ACTION_MENU, which in a dialog is "page back":
			// the touch handler queues AVK_SOFT1 (19) for dialog button 5
			// (src/TouchController.cpp:456-461), and AVK_SOFT1 maps to
			// ACTION_MENU (src/InputEventController.cpp:26).
			if (m.showPageUp && pageIcon(ui, UiId::DialogPageUp, kPageUpRect, art.pageUp)) {
				res.action = UiAction::Menu;
			}
			// Page-down and page-ok both queue AVK_6, which resolves through
			// keys_numeric[5] to ACTION_FIRE (src/TouchController.cpp:379-392,
			// src/InputEventController.cpp:104 with tables.bin table 5).
			if (m.showPageDown &&
				pageIcon(ui, UiId::DialogPageDown, kPageDownRect, art.pageDown)) {
				res.action = UiAction::Activate;
			}
			if (m.showPageOk && pageIcon(ui, UiId::DialogOk, kPageDownRect, art.pageOk)) {
				if (m.activateFires) res.action = UiAction::Activate;
			}
		}
	}

	// Dialog button 8: a tap on the box body is the same ACTION_FIRE
	// (src/TouchController.cpp:379-392 treats button 8 exactly like 7).
	if (bodyHit.w > 0 && bodyHit.h > 0 && ui.buttonRect(UiId::DialogBody, bodyHit)) {
		if (m.activateFires) res.action = UiAction::Activate;
	}

	return res;
}

} // namespace newcore
