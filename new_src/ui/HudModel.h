#ifndef NEW_UI_HUDMODEL_H
#define NEW_UI_HUDMODEL_H

#include "ui/UiTypes.h"

namespace newcore {

class Text;

// Per-frame view model for the HUD (ADR 0012 point 4, spec
// 2026-08-27-ui-layer §4.1): rebuilt from scratch by
// GameContext::buildHudModel() every frame, never fed by setters. The old
// Hud kept weapon_/ammo_/shield_ as setter-fed fields and demo values stayed
// on screen for months, because a field cannot tell you whether it was fed
// this frame or is stale; a struct rebuilt per frame cannot go stale.
//
// GROUP 4 migrates the BOTTOM BAR only. The spec's top-bar, monster-bar,
// timed-text-slot, cockpit and shake fields are deliberately absent until
// their producers move as well: a model field with no producer is exactly the
// stale-value hazard this struct exists to remove.
struct HudModel {
	// The raw stats the legacy widgets read live every frame
	// (src/Hud.cpp:683-709; every `repaintFlags &= ~bit` inside Hud::draw is
	// commented out in the RE port, docs/original-code/ui.md §8).
	int health = 0;
	int maxHealth = 1;
	int shield = 0;
	int weapon = -1;
	int ammo = 0;
	// Legacy drawWeapon's `drawNumbers` flag (src/Hud.cpp:1053-1112): false
	// for weapon 1 and for the default branch (ids >= 15).
	bool showAmmo = true;
	// weapon == 13 renders "N/5" (docs/original-code/ui.md §2).
	bool slashAmmo = false;
	// 0..3, folded from inventory slots 19/20 (docs/original-code/ui.md §5).
	int keysRow = 0;
	// Legacy repaint bit 0x4 for the frame's state (ui.md §8): the caller's
	// gameplay-view gate, carried here instead of wrapping the view call.
	bool showBottomBar = false;
	// False while a modal screen owns input: the bottom bar is still DRAWN
	// (showBottomBar) but its buttons are not touch candidates, because the
	// legacy touch dispatcher only scans the modal screen's own button group
	// and never reaches Hud::handleUserTouch (src/TouchController.cpp:29-31
	// ST_LOOTING, :96-98 ST_DIALOG).
	bool interactive = false;

	// Soft keys (docs/original-code/ui.md §6). Borrowed composed Text, valid
	// for THIS frame only; null == the legacy softKeyLeftID/RightID == -1
	// case, which suppresses the label (and the *_Active arrow art) but not
	// the arrow itself (src/TouchController.cpp:550-570).
	const Text* softLeft = nullptr;
	// Drawn as a plain label: the "Wait" literal has no touch area in the
	// original, ACTION_PASSTURN sits on the portrait button instead
	// (src/Hud.cpp:76, src/Hud.cpp:1343-1345).
	const Text* softCenter = nullptr;
	const Text* softRight = nullptr;
	// Hit boxes are caller constants, all three registered by the original in
	// Hud::startup (src/Hud.cpp:67-76).
	UiRect softLeftHit;
	UiRect softRightHit;
	UiRect portraitHit;
};

} // namespace newcore

#endif // NEW_UI_HUDMODEL_H
