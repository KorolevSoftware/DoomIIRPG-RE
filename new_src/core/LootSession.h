#ifndef NEW_CORE_LOOTSESSION_H
#define NEW_CORE_LOOTSESSION_H

#include <cstdint>

#include "core/GameStates.h"
#include "domain/game/CorpseLoot.h"
#include "domain/game/Game.h"
#include "text/Text.h"
#include "ui/LootModel.h"

namespace newcore {

class Font;
class Localization;
class MapData;
class Player;
class Tables;

// The whole ST_LOOTING vertical slice (spec 2026-08-26-decomposition §P1-G3):
// the 500 ms crouch/stand-up pose driver (src/LootingSystem.cpp:35-83) and the
// dwell input handling (:85-117). The overlay's drawing moved to
// ui/LootView.cpp (spec 2026-08-27-ui-layer §5) and what stays here is the
// per-frame resolution of the dwell state into the view model.
class LootSession {
public:
	static constexpr int kLootPhaseMs = 500;  // LOOTING_CROUCH_TIME (src/Canvas.h:46);
	                                         // promoted from tickLooting's local constant

	struct Env {
		MapData* map = nullptr;
		Game* game = nullptr;
		Player* player = nullptr;
		const Localization* loc = nullptr;
		// Drawing leftover: the font itself lives on Ui now, but the null probe
		// still gates the overlay the way it gated the old draw call.
		const Font* font = nullptr;
		const Tables* tables = nullptr;
		StateHost* host = nullptr;
		const int64_t* upTimeMs = nullptr;
	};

	void init(const Env& env);

	void begin();                    // ST_LOOTING enter hook (src/LootingSystem.cpp:26-33)
	void tick();                     // pose driver (:35-83)
	void handleAction(Action a);     // dwell input (:85-117)

	// Loot list overlay (:121-150), state half. Returns false when there is
	// nothing to draw — the dwell-window gate the old draw() opened with.
	bool buildViewModel(LootListModel& m);

private:
	void close();                    // grant + stand-up restart (:89-103)

	Env env_;

	// Loot dwell session (legacy LootingSystem field analogs). lootDest*
	// anchors the cached player pose the two 500 ms phases lerp away from and
	// back to (docs/research/2026-08-25-camera-pitch-loot.md).
	bool lootSettleSfx_ = false;              // field_0xac5_: sound 1055 once per session
	CorpseLoot::Pool lootPool_;               // pooled entries + lootText + lineIndex + lootLineNum
	Text titleText_;                          // str 227, recomposed per frame like the legacy small buffer
	bool lootCrouch_ = false;             // crouchingForLoot (phase selector)
	int64_t lootTime_ = 0;                // lootingTime (app->time latch)
	int lootDestX_ = 0, lootDestY_ = 0, lootDestZ_ = 0, lootDestPitch_ = 0;
	int lootStepX_ = 0, lootStepY_ = 0;   // facing unit steps (viewStep>>6, ±1/0)
};

} // namespace newcore

#endif // NEW_CORE_LOOTSESSION_H
