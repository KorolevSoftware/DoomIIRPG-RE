#ifndef NEW_CORE_HUDBUTTONS_H
#define NEW_CORE_HUDBUTTONS_H

#include <cstdint>

#include "ui/UiTypes.h"

namespace newcore {

class Ui;
struct HudModel;

// Retained state of the HUD bottom-bar buttons: legacy Hud::isInWeaponSelect /
// weaponPressTime (src/Hud.cpp:94-96) and TouchController's touch-start origin +
// canvas->touched drag latch (src/TouchController.cpp:23, :128-134).
// Spec docs/architecture/hud-bar-buttons.md (ADR 0045).
class HudButtons {
public:
	struct Frame {
		int64_t upTimeMs = 0;
		bool blockInput = false;   // GameContext::blockInputTime != 0
		bool combat = false;       // Game::combat.active
		bool zoomed = false;       // ZoomState::isZoomedIn
	};
	void trackPointer(const UiInput& in);   // before drawHud
	UiResult resolve(UiResult r, const Ui& ui, const HudModel& m, const Frame& f);  // after drawHud
	bool weaponSelectOpen() const { return open_; }

private:
	bool open_ = false;
	int64_t pressTime_ = 0;      // 0 = no pending press (legacy sentinel)
	int pressX_ = 0, pressY_ = 0;
	bool dragged_ = false;
};

} // namespace newcore

#endif // NEW_CORE_HUDBUTTONS_H
