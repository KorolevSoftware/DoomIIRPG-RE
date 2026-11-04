#include "ui/CharacterSelectView.h"

#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kScrCx = 240;                  // Canvas::SCR_CX (src/Canvas.cpp:122)
constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;

// Slots (src/IntroSequenceManager.cpp:375-427): j = 152 + 75 * i.
constexpr int kSlotX0 = 152;
constexpr int kSlotDx = 75;
constexpr int kFrameY = 55;
constexpr int kFrameW = 62;
constexpr int kFrameH = 83;
constexpr uint32_t kHighlight = 0xFF2896FF;  // fillRect 0x2896ff, alpha forced opaque (src/Graphics.cpp:88-95)
constexpr uint32_t kButtonIdle = 0xFF646464;
constexpr uint32_t kButtonBorder = 0xFF000000;   // drawRect default colour (deviation FE-D5)

// Avatar (:432, :464-488) and stats panel (:538-584, yFix = -2).
constexpr int kAvatarX = -10;
constexpr int kAvatarY = 140;
constexpr int kStatX = 362;
constexpr int kStatY = 115;

// Prompt at screenRect[3] - 85, Back / Yes boxes at screenRect[3] - 60 (:435-460).
constexpr int kPromptY = 235;
constexpr UiRect kBackRect{ kScrCx - 85, 260, 70, 30 };
constexpr UiRect kYesRect{ kScrCx + 15, 260, 70, 30 };
constexpr int kButtonTextY = 270;

constexpr char kCursorGlyph = '\x8A';        // Graphics::drawCursor (src/Graphics.cpp:670-686)

const Texture& facesFor(const UiAssets& art, int choice) {
	return choice == 2 ? art.playerFacesDoom : choice == 3 ? art.playerFacesScientist : art.playerFaces;
}

void drawAvatar(Ui& ui, const UiAssets& art, int choice, int lift) {
	const Texture& legs = choice == 2 ? art.sargeLegs : choice == 3 ? art.rileyLegs : art.majorLegs;
	const Texture& torso = choice == 2 ? art.sargeTorso : choice == 3 ? art.rileyTorso : art.majorTorso;
	ui.image(legs, kAvatarX, kAvatarY, Graphics2D::kAnchorNone);
	ui.image(torso, kAvatarX, kAvatarY - lift, Graphics2D::kAnchorNone);
}

void drawStats(Ui& ui, const UiAssets& art, const CharacterSelectModel& m) {
	ui.image(art.charSelectStatHeader, kStatX - 2, kStatY - 4, Graphics2D::kAnchorNone);
	for (int k = 0; k < 5; ++k) {
		ui.image(art.charSelectStatBar, kStatX - 2, kStatY + 14 + 20 * k, Graphics2D::kAnchorNone);
		if (m.statRow[k] != nullptr) ui.label(*m.statRow[k], kStatX, kStatY + 16 + 20 * k, kTopLeft);
	}
}

// One Back / Yes box (:440-460).
bool drawBox(Ui& ui, UiId id, const UiRect& r, const Text* text) {
	ui.panel(r, ui.isActive(id) ? kHighlight : kButtonIdle);
	ui.frame(r, kButtonBorder);
	const bool fired = ui.buttonRect(id, r);
	if (text != nullptr) ui.label(*text, r.x + 35, kButtonTextY, Graphics2D::kAnchorHCenter);
	return fired;
}

} // namespace

UiResult drawCharacterSelect(Ui& ui, const CharacterSelectModel& m) {
	UiResult res;
	const UiAssets& art = ui.art();
	int fired = -1;

	ui.image(art.charSelectBG, 0, 0, kTopLeft);                                    // (:351)
	ui.image(art.charSelectTopBar, kScrCx - art.charSelectTopBar.width() / 2, 0, kTopLeft);   // (:352)
	if (m.title != nullptr) ui.label(*m.title, kScrCx, 3, Graphics2D::kAnchorHCenter);   // (:373)

	for (int i = 0; i < 3; ++i) {
		const int j = kSlotX0 + kSlotDx * i;
		const UiId id = (UiId)((int)UiId::CharSlot0 + i);
		ui.imageRegion(art.charSelectAtlas, 0, 0, 31, 83, j - 15, kFrameY, kTopLeft);
		ui.imageRegion(art.charSelectAtlas, 0, 0, 31, 83, j + 16, kFrameY, kTopLeft, 4);
		if (ui.isActive(id)) {                                                       // (:383-389)
			ui.panel(UiRect{ j - 15, kFrameY, kFrameW, kFrameH }, kHighlight);
			ui.imageRegion(art.charSelectAtlas, 31, 0, 27, 60, j - 11, 64, kTopLeft);
			ui.imageRegion(art.charSelectAtlas, 31, 0, 27, 60, j + 16, 64, kTopLeft, 4);
		}
		if (ui.buttonRect(id, UiRect{ j - 15, kFrameY, kFrameW, kFrameH })) fired = i;   // (:391)
		if (i < 2) {                                                                 // links (:393-397)
			for (int x = j + 46; x < j + 64; x += 6)
				ui.imageRegion(art.charSelectAtlas, 51, 60, 6, 19, x, 81, kTopLeft);
		}
		if (m.slotChoice[i] == m.highlighted) {                                      // marker (:399-402)
			ui.imageRegion(art.charSelectAtlas, 31, 60, 20, 32, j - 3, 161, kTopLeft);
			ui.imageRegion(art.charSelectAtlas, 31, 60, 19, 32, j + 17, 161, kTopLeft, 4);
		}
		const Texture& faces = facesFor(art, m.slotChoice[i]);                      // (:404-430)
		ui.imageRegion(faces, 0, 0, 32, 32, j, 75, kTopLeft);
		if (m.shortName[i] != nullptr)
			ui.label(*m.shortName[i], j + faces.width() / 2, 142, Graphics2D::kAnchorHCenter);
	}

	drawAvatar(ui, art, m.highlighted, m.torsoLift);
	drawStats(ui, art, m);

	if (m.prompt != nullptr) ui.label(*m.prompt, kScrCx, kPromptY, Graphics2D::kAnchorHCenter);

	if (drawBox(ui, UiId::CharBack, kBackRect, m.back)) fired = 4;
	if (drawBox(ui, UiId::CharYes, kYesRect, m.yes)) fired = 3;
	if (m.confirmCursor) {                                                           // [GEC] (:448-463)
		const Text* t = m.cursorOnYes ? m.yes : m.back;
		const int cx = (m.cursorOnYes ? kScrCx + 50 - 6 : kScrCx - 50 - 6) -
			(t != nullptr ? t->getStringWidth() / 2 : 0) + m.cursorOffset;
		ui.glyph(kCursorGlyph, cx, kButtonTextY, Graphics2D::kAnchorRight | Graphics2D::kAnchorTop);
	}

	if (fired >= 0) {
		res.action = UiAction::ScreenTouch;
		res.index = fired;
	}
	return res;
}

} // namespace newcore
