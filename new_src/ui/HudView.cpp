#include "ui/HudView.h"

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

// Every blit below uses anchor 20 = TOP|LEFT (docs/original-code/ui.md §1), so
// all coordinates are top-left corners. Numerically 20 and 0 behave the same
// in Graphics2D (only HCENTER/RIGHT/VCENTER/BOTTOM shift the origin,
// new_src/render/Graphics2D.cpp:84-99); the flags are spelled out so the
// legacy anchor value stays greppable.
constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;

// ---- geometry, docs/original-code/ui.md §1 ----
constexpr int kSwitchLeftX = 9,   kSwitchLeftY = 268;    // Switch_Left_*  32x32
constexpr int kSwitchRightX = 438, kSwitchRightY = 268;  // Switch_Right_* 32x32
constexpr int kWeaponX = 268, kWeaponY = 258;
constexpr int kWeaponRowH = 44;                          // 102x616 = 14 rows
// Ammo digit anchor: x + (imgWeaponNormal->width >> 1) + 6, y + 8
// (src/Hud.cpp:1122-1124) = (268 + 51 + 6, 258 + 8) for the HUD slot.
constexpr int kAmmoX = 325, kAmmoY = 266;
constexpr int kShieldX = 49,  kShieldY = 258;
constexpr int kShieldDigitsX = 89,  kShieldDigitsY = 268;
constexpr int kHealthX = 133, kHealthY = 258;
constexpr int kHealthDigitsX = 173, kHealthDigitsY = 268;
constexpr int kFaceX = 224, kFaceY = 263;
constexpr int kFaceRowH = 32;                            // 32x160 = 5 rows
constexpr int kFrameX = 216, kFrameY = 255;
constexpr int kKeysX = 375, kKeysY = 258;
constexpr int kKeysRowH = 44;                            // 55x176 = 4 rows
constexpr int kDigitSpace = 1;                           // drawNumbers `space`

// Weapon-select overlay (src/Hud.cpp:1181-1209): dim below the top bar
// (FMGL_fillRect(0, 20, 480, 320, 0.5f) clipped to the canvas, :1187), rows of
// 4 weapon-panel icons from x = (480 - 16 - 4*102) / 2 (:1184),
// first row y = 320 - 108 - 4 (:1185, :1193), steps +106 / -48 (:1198-1208).
constexpr UiRect kSelDim{ 0, 20, 480, 300 };
constexpr uint32_t kSelDimArgb = 0x80000000;
constexpr int kSelX0 = 28, kSelY0 = 208;
constexpr int kSelStepX = 106, kSelStepY = 48, kSelPerRow = 4;
constexpr int kSelW = 102, kSelH = 44;
constexpr int kSelAmmoDX = 57, kSelAmmoDY = 8;           // (w >> 1) + 6, 8 (:1122-1124)

// Soft-key label anchors (docs/original-code/ui.md §6,
// src/TouchController.cpp:560-577, src/Hud.cpp:715-723).
constexpr int kSoftLeftX = 2,    kSoftLeftY = 320;
constexpr int kSoftCenterX = 240, kSoftCenterY = 320;
constexpr int kSoftRightX = 478, kSoftRightY = 320;

// Familiar bottom bar (src/Hud.cpp:644-681): health colour band, the
// HUD_sentry_active plate while button 3 (pass turn) is held, the bot face
// (green row 0, red row 17) and one 4x14 segment per 5% health, unclamped.
void drawFamiliarBar(Ui& ui, const HudModel& m, UiResult& res) {
	const UiAssets& art = ui.art();
	const int h = m.botHealth;
	uint32_t colour = h > 80 ? 0xFF59A907 : h > 60 ? 0xFF96DA0E : h > 40 ? 0xFFEDD703
	                : h > 20 ? 0xFFFF8B00 : 0xFFFF0C00;
	if (m.interactive && ui.buttonRect(UiId::HudPortrait, m.portraitHit)) {   // :1257-1259
		res.action = UiAction::PassTurn;
	}
	if (ui.isActive(UiId::HudPortrait)) {
		colour = 0xFFFFFFFF;
		ui.image(art.sentryActive, 143, 261, kTopLeft);
	}
	// SCR_CX - (15 + 118) / 2 = 174, anchor 3 = HCENTER | VCENTER.
	ui.imageRegion(art.sentryFace, 0, m.botRed ? 17 : 0, 29, 17, 174, 278,
		Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter);
	const int x0 = 240 + art.sentryFace.width() - 66;
	int segs = h * 20 / 100;
	if (segs < 20 && h != 0) ++segs;
	for (int i = 0; i < segs; ++i) ui.panel({ x0 + 6 * i, 271, 4, 14 }, colour);
}

} // namespace

// Draw order is the legacy one inside Hud::drawBottomBar (src/Hud.cpp:636-724):
// the soft-key bar first (which in the original also paints the bottom panel
// background — in the rewrite that background is still Hud::drawBottomPanel,
// called from drawTopBar before this view runs), then weapon, shield, keys,
// health, portrait, then the centre "Wait" literal. None of the widgets
// overlap, so the order is documentation rather than pixels.
UiResult drawHud(Ui& ui, const HudModel& m) {
	UiResult res;
	if (!m.showBottomBar) return res;

	const UiAssets& art = ui.art();

	// ---- weapon-select overlay ----
	// Drawn before the bottom bar (src/Hud.cpp:772-782); the bottom panel is
	// repainted over the dim (src/TouchController.cpp:544-545).
	if (m.weaponSelect) {
		ui.panel(kSelDim, kSelDimArgb);
		int k = 0;
		for (int i = 0; i < 15; ++i) {
			if (((m.weaponSelectMask >> i) & 1) == 0) continue;
			const UiRect r{ kSelX0 + kSelStepX * (k % kSelPerRow),
				kSelY0 - kSelStepY * (k / kSelPerRow), kSelW, kSelH };
			++k;
			const UiId id = static_cast<UiId>(static_cast<int>(UiId::HudWeaponSel0) + i);
			if (m.interactive && ui.buttonRect(id, r)) {
				res.action = UiAction::WeaponSelect;
				res.index = i;
			}
			ui.weaponIcon(ui.isActive(id) ? art.weaponActive : art.weaponNormal,
				r.x, r.y, kWeaponRowH, i);
			if (i != 1) {
				ui.number3(art.numbers, r.x + kSelAmmoDX, r.y + kSelAmmoDY, kDigitSpace,
					m.weaponSelectAmmo[i], i == 13);
			}
		}
		ui.image(art.panelBottom, 0, 320 - art.panelBottom.height(), kTopLeft);
	}

	// ---- soft keys + switch arrows ----
	// In the original these are ONE button per side: hud button 0 (0,256,52,64)
	// highlights the LEFT arrow art and fires ACTION_MENU, button 1
	// (428,256,52,64) highlights the RIGHT arrow and fires ACTION_AUTOMAP
	// (src/Hud.cpp:1300-1320 handleUserTouch; art swap
	// src/TouchController.cpp:550-570). The "switch" names in Hud::startup are
	// a misnomer: id 0 sits on the left of the screen and drives the left
	// sheet. So the label and the arrow share one UiId here. The hit rect the
	// model hands us is however only the arrow icon, not the legacy 52x64 box
	// that also covered the label — a deliberate deviation documented at
	// kSoftLeftHit (new_src/core/GameContext.cpp:464).
	// m.interactive == false keeps the labels and the arrows on screen but takes
	// the three hit rects out of the frame, which is what the legacy dispatcher
	// does while a modal screen is up (see HudModel::interactive).
	if (m.softLeft != nullptr) {
		if (ui.softKey(UiId::HudSoftLeft, *m.softLeft, kSoftLeftX, kSoftLeftY,
				Graphics2D::kAnchorLeft | Graphics2D::kAnchorBottom, m.softLeftHit,
				m.interactive)) {
			res.action = UiAction::Menu;
		}
	}
	// Legacy: `!highlighted || (highlighted && softKeyID == -1)` -> Normal
	// sheet, i.e. the arrow is always drawn and only swaps while held with a
	// label present.
	ui.image(ui.isActive(UiId::HudSoftLeft) && m.softLeft != nullptr
			? art.switchLeftActive : art.switchLeftNormal,
		kSwitchLeftX, kSwitchLeftY, kTopLeft);

	if (m.softRight != nullptr) {
		if (ui.softKey(UiId::HudSoftRight, *m.softRight, kSoftRightX, kSoftRightY,
				Graphics2D::kAnchorRight | Graphics2D::kAnchorBottom, m.softRightHit,
				m.interactive)) {
			res.action = m.softRightAction;
		}
	}
	ui.image(ui.isActive(UiId::HudSoftRight) && m.softRight != nullptr
			? art.switchRightActive : art.switchRightNormal,
		kSwitchRightX, kSwitchRightY, kTopLeft);

	// ---- weapon icon + ammo digits ----
	// Hud buttons 2, 4, 5, 6 (docs/architecture/hud-bar-buttons.md): hit test
	// before the blit so the *_Active sheet shows on the press frame
	// (src/Hud.cpp:685-691, :1174-1178), the portrait idiom below.
	if (m.familiar) {
		drawFamiliarBar(ui, m, res);
	} else {
		if (m.interactive && ui.buttonRect(UiId::HudWeapon, m.weaponHit)) res.action = UiAction::WeaponTap;
		ui.weaponIcon(ui.isActive(UiId::HudWeapon) ? art.weaponActive : art.weaponNormal,
			kWeaponX, kWeaponY, kWeaponRowH, m.weapon);   // src/Hud.cpp:1114-1119
		if (m.showAmmo) {
			ui.number3(art.numbers, kAmmoX, kAmmoY, kDigitSpace, m.ammo, m.slashAmmo);
		}

		// ---- shield plate ----
		if (m.interactive && ui.buttonRect(UiId::HudShield, m.shieldHit)) res.action = UiAction::ItemsDrinks;
		ui.image(ui.isActive(UiId::HudShield) ? art.shieldActive : art.shieldNormal,
			kShieldX, kShieldY, kTopLeft);
		ui.number3(art.numbers, kShieldDigitsX, kShieldDigitsY, kDigitSpace, m.shield, false);

		// ---- keys ----
		if (m.interactive && ui.buttonRect(UiId::HudKeys, m.keysHit)) res.action = UiAction::QuestLog;
		ui.keys(ui.isActive(UiId::HudKeys) ? art.keyActive : art.keyNormal,
			kKeysX, kKeysY, kKeysRowH, m.keysRow);

		// ---- health plate ----
		if (m.interactive && ui.buttonRect(UiId::HudHealth, m.healthHit)) res.action = UiAction::Items;
		ui.image(ui.isActive(UiId::HudHealth) ? art.healthActive : art.healthNormal,
			kHealthX, kHealthY, kTopLeft);
		ui.number3(art.numbers, kHealthDigitsX, kHealthDigitsY, kDigitSpace, m.health, false);

		// ---- portrait: face row from health, then the frame on top ----
		// Hud button 3, rect (219,264, imgPlayerFaces->width + 10 = 42, 36)
		// (src/Hud.cpp:76), fires ACTION_PASSTURN (src/Hud.cpp:1343-1345). While it
		// is held the original swaps BOTH the face sheet and the frame to their
		// *_Active variants, keeping the same face row (src/Hud.cpp:700-708).
		// The hit test runs before the blits so the highlight shows on the press
		// frame itself, as it does for the two soft keys above.
		if (m.interactive && ui.buttonRect(UiId::HudPortrait, m.portraitHit)) {
			res.action = UiAction::PassTurn;
		}
		const bool portraitHeld = ui.isActive(UiId::HudPortrait);
		const Texture& faces = m.characterChoice == 2 ? art.playerFacesDoom
			: m.characterChoice == 3 ? art.playerFacesScientist : art.playerFaces;
		const Texture& active = m.characterChoice == 2 ? art.playerActiveDoom
			: m.characterChoice == 3 ? art.playerActiveScientist : art.playerActive;
		ui.face(portraitHeld ? active : faces, kFaceX, kFaceY,
			kFaceRowH, m.health, m.maxHealth);
		ui.image(portraitHeld ? art.playerFrameActive : art.playerFrameNormal,
			kFrameX, kFrameY, kTopLeft);
	}

	// ---- centre "Wait" ----
	// Hardcoded ASCII in the original, drawn only while BOTH soft keys are set
	// (src/Hud.cpp:712-720); the model's two non-null slots are that condition.
	// A pure label: the literal has no touch area in the original, its
	// ACTION_PASSTURN lives on the portrait button above.
	if (m.softCenter != nullptr && m.softLeft != nullptr && m.softRight != nullptr) {
		ui.label(*m.softCenter, kSoftCenterX, kSoftCenterY,
			Graphics2D::kAnchorHCenter | Graphics2D::kAnchorBottom);
	}

	return res;
}

} // namespace newcore
