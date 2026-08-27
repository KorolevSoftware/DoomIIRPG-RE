#ifndef NEW_CORE_LOOTSESSION_H
#define NEW_CORE_LOOTSESSION_H

#include <cstdint>

#include "core/GameStates.h"
#include "domain/game/Game.h"

namespace newcore {

class DialogSystem;
class Font;
class Graphics2D;
class Localization;
class MapData;
class Player;
class Tables;

// The whole ST_LOOTING vertical slice (spec 2026-08-26-decomposition §P1-G3):
// the 500 ms crouch/stand-up pose driver (src/LootingSystem.cpp:35-83), the
// dwell input handling (:85-117) and the loot list overlay (:121-150).
class LootSession {
public:
	static constexpr int kLootPhaseMs = 500;  // LOOTING_CROUCH_TIME (src/Canvas.h:46);
	                                         // promoted from tickLooting's local constant

	struct Env {
		MapData* map = nullptr;
		Game* game = nullptr;
		Player* player = nullptr;
		const Localization* loc = nullptr;
		const Font* font = nullptr;
		DialogSystem* dialogs = nullptr;      // shared drawScrollBar
		const Tables* tables = nullptr;
		StateHost* host = nullptr;
		const int64_t* upTimeMs = nullptr;
	};

	void init(const Env& env);

	void begin();                    // ST_LOOTING enter hook (src/LootingSystem.cpp:26-33)
	void tick();                     // pose driver (:35-83)
	void handleAction(Action a);     // dwell input (:85-117)
	void draw(Graphics2D& g);        // loot list overlay (:121-150)

private:
	void close();                    // grant + stand-up restart (:89-103)

	Env env_;

	// Loot dwell session (legacy LootingSystem field analogs). lootDest*
	// anchors the cached player pose the two 500 ms phases lerp away from and
	// back to (docs/research/2026-08-25-camera-pitch-loot.md).
	bool lootSettleSfx_ = false;              // field_0xac5_: sound 1055 once per session
	Game::LootPool lootPool_;                 // pooled entries + lootText + lineIndex + lootLineNum
	bool lootCrouch_ = false;             // crouchingForLoot (phase selector)
	int64_t lootTime_ = 0;                // lootingTime (app->time latch)
	int lootDestX_ = 0, lootDestY_ = 0, lootDestZ_ = 0, lootDestPitch_ = 0;
	int lootStepX_ = 0, lootStepY_ = 0;   // facing unit steps (viewStep>>6, ±1/0)
};

} // namespace newcore

#endif // NEW_CORE_LOOTSESSION_H
