#include "ui/MainMenuView.h"

#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kScrCx = 240;               // Canvas::SCR_CX (src/Canvas.cpp:122)
constexpr int kScrCy = 160;
constexpr int kLogoX = (480 - 170) >> 1;  // logo2.bmp centred at the top (:767-769)

// Back switch soft key (:5212-5217, label :5238).
constexpr UiRect kSoftLeftRect{ 10, 262, 32, 32 };
constexpr int kSoftLeftTextX = 8;
constexpr int kSoftLeftTextY = 314;

// Idle plates: normalRenderMode 1 = RENDER_BLEND25 (src/MenuSystem.cpp:4451-4455).
constexpr uint8_t kRowAlphaIdle = 64;
// The 9-button pool (src/MenuSystem.cpp:4440-4470).
constexpr int kRowSlots = 9;
constexpr int kDialKnobDx = 12;           // drawScrollbar (:5019-5036)

constexpr char kCursorGlyph = '\x8A';     // Graphics::drawCursor (src/Graphics.cpp:670-686)

constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;

// [GEC] Sound screen image slider (drawCustomScrollbar, :5409-5455). Menu_Option_BOX3.bmp
// is only used for its width 284.
constexpr int kOptBoxX = (480 - 284) >> 1;    // 98  (v59)
constexpr int kOptBarX = kOptBoxX + 1;        // 99  (v6)
constexpr int kOptLabelX = kOptBoxX + 15;     // 113 label x of a SCROLLBARTWO row (:1060-1062)
constexpr int kOptKnobMinX = kOptBarX;        // 99
constexpr int kOptDigitsX = kOptBoxX + 246;   // 344 (v10)
constexpr int kOptKnobTravel = 245 - 24;      // 245 - SliderON.w

// ty = label y + FONT_HEIGHT. The disabled overlay (:5450-5453) is dead.
void drawImageSlider(Ui& ui, const UiAssets& art, const MenuRow& r, int ty) {
	ui.image(art.optSliderBar, kOptBarX, ty - 20, kTopLeft);                           // (:5416)
	if (r.sliderDragX >= 0) {                                                         // highlighted (:5417-5430)
		int kx = r.sliderDragX - 12;                                                  // valueScroll - SliderON.w/2
		if (kx < kOptKnobMinX) kx = kOptKnobMinX;
		else if (kx + 24 > kOptDigitsX) kx = kOptDigitsX - 24;
		ui.image(art.optSliderOn, kx, ty, kTopLeft);
	} else {
		ui.image(art.optSliderOff, kOptBarX + kOptKnobTravel * r.sliderValue / 100, ty, kTopLeft);   // (:5432)
	}
	int xr = kOptDigitsX;
	if (r.sliderEdit) {                                                               // (:5435-5441)
		ui.glyph('<', xr + 5, ty + 5, Graphics2D::kAnchorRight | Graphics2D::kAnchorTop);
		xr += 10;
	}
	ui.number3(art.numbers, xr, ty + 3, 0, r.sliderValue, false);                     // (:5442, :5794-5809)
	if (r.sliderEdit) ui.glyph('>', xr + 40, ty + 5, Graphics2D::kAnchorRight | Graphics2D::kAnchorTop);   // (:5443-5448)
}

} // namespace

UiResult drawMainMenu(Ui& ui, const MenuViewModel& m) {
	UiResult res;
	const UiAssets& art = ui.art();

	ui.image(art.mainBackground, kScrCx, kScrCy,
		Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter);   // (:766)
	if (m.drawLogo) ui.image(art.mainLogo, kLogoX, 0, kTopLeft);   // (:767-769)

	bool softLeftFired = false;
	if (m.softLeft != nullptr) {
		ui.image(ui.isActive(UiId::MenuSoftLeft) ? art.switchLeftActive : art.switchLeftNormal,
			kSoftLeftRect.x, kSoftLeftRect.y, kTopLeft);
		softLeftFired = ui.softKey(UiId::MenuSoftLeft, *m.softLeft, kSoftLeftTextX, kSoftLeftTextY,
			Graphics2D::kAnchorLeft | Graphics2D::kAnchorBottom, kSoftLeftRect);
	}

	ui.pushClip(m.clip);

	// Plates + hit areas. No y > 210 touch rule and no info buttons below
	// MENU_INGAME (:5090-5102, :4475-4489).
	int rowHit = -1;
	{
		int y = -m.scrollPx;
		int slot = 0;
		for (int i = 0; i < m.rowCount && y < m.rect.h; ++i) {
			const MenuRow& r = m.rows[i];
			if (y + r.height > 0 && r.action && slot < kRowSlots) {
				const int y0 = y + m.rect.y;
				const UiId id = (UiId)((int)UiId::MenuRow0 + slot);
				if (!r.disabled) {
					if (ui.isActive(id)) ui.image(art.mainButtonOn, m.rect.x, y0, kTopLeft);
					else ui.imageAlpha(art.mainButton, m.rect.x, y0, kRowAlphaIdle);
				}
				if (m.rowHits && ui.buttonRect(id, UiRect{ m.rect.x, y0, m.itemWidth, m.itemHeight }))
					rowHit = i;
				++slot;
			}
			y += r.height;
		}
	}

	if (m.showBar) {                                                   // (:5019-5036)
		ui.image(art.mainDial, m.barRect.x, m.barRect.y, kTopLeft);
		ui.image(art.mainDialKnob, m.barRect.x + kDialKnobDx, m.dialKnobY, kTopLeft);
	}

	// Cursor + labels, the MenuView pass-2 rules without the value column and
	// the disabled overlay (:905-1130).
	{
		int y = -m.scrollPx;
		for (int i = 0; i < m.rowCount && y < m.rect.h; ++i) {
			const MenuRow& r = m.rows[i];
			if (y + r.height > 0 && r.label != nullptr) {
				const int y0 = y + m.rect.y + (r.action ? 0 : r.textDy);
				int labelX;
				if (r.centered) {                                     // (:959-970)
					const int half = r.label->getStringWidth(false) >> 1;
					labelX = r.action ? m.rect.x + (m.itemWidth >> 1) - half
					                  : m.rect.x + (m.rect.w >> 1) - half;
				} else {                                              // (:980-990)
					labelX = r.slider == 2 ? kOptLabelX : r.action ? m.rect.x + 8 : m.rect.x;
				}
				if (i == m.selectedRow) {                             // (:1064-1073)
					ui.glyph(kCursorGlyph, labelX + m.cursorOffset + 3,
						r.action ? y0 + (m.itemHeight >> 1) - 8 : y0, Graphics2D::kAnchorRight);
					labelX += 8;
				}
				if (r.action) {
					ui.label(*r.label, labelX, y0 + (m.itemHeight >> 1), Graphics2D::kAnchorVCenter);
				} else {
					ui.label(*r.label, labelX, y0, Graphics2D::kAnchorNone);
				}
				if (r.slider == 2) drawImageSlider(ui, art, r, y0 + 16);   // label first, then the slider (:1082-1084)
			}
			y += r.height;
		}
	}

	ui.popClip();
	if (m.drawLogo) ui.image(art.mainLogo, kLogoX, 0, kTopLeft);   // drawn again over the list (:1131-1134)

	if (softLeftFired) {
		res.action = UiAction::Back;
	} else if (rowHit >= 0) {
		res.action = UiAction::ListRow;
		res.index = rowHit;
	} else if (ui.in().wheel != 0) {
		res.action = ui.in().wheel > 0 ? UiAction::ScrollUp : UiAction::ScrollDown;
	}
	return res;
}

} // namespace newcore
