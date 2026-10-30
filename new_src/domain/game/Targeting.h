#ifndef NEW_DOMAIN_GAME_TARGETING_H
#define NEW_DOMAIN_GAME_TARGETING_H

#include "domain/game/TraceHit.h"

namespace newcore {

class Entity;
class Game;
class Hud;
class MapData;
class Player;
class Tables;

// Everything that answers "what is the player aiming at": the view forward
// vector, the health-bar facing probe and the ordered fire-target election
// (spec 2026-08-26-decomposition §P1-G4). Moved verbatim out of GameContext;
// the probe and its only readout (the HUD monster health bar) live together.
// Owns no persistent state beyond the last fire end point: the probe result keeps living on
// Player::facingEntity, which the HUD reads.
class Targeting {
public:
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		MapData* map = nullptr;
		const Tables* tables = nullptr;   // sin table
		Hud* hud = nullptr;
		const int* view = nullptr;        // 14.14 view matrix of the last world pass (tinyGL->view)
	};

	void init(const Env& env);

	// Player view forward vector in 16.16 (legacy -view[2]/-view[6],
	// src/MovementController.cpp:38).
	void viewForward(int& fwdX, int& fwdY) const;

	// Health-bar feed probe (src/MovementController.cpp:28-93); writes
	// player->facingEntity.
	void updateFacingProbe();

	// Ordered target election over the sorted fire-trace hit list — port of
	// src/PlayingInputHandler.cpp:218-385 (docs/original-code/combat.md §8).
	// Returns the elected hit; kind None (!blocks()) means nothing was elected
	// = air shot (ADR 0011; the old Entity*/int* outFrac pair is gone).
	TraceHit electFireTarget(int weapon);
	// End point {x, y, z} of the last fire trace (legacy n8/n9/n10,
	// src/PlayingInputHandler.cpp:216-218); the zoom collision lerps toward it.
	const int* fireEnd() const { return fireEnd_; }

	// Resolve the facing probe into the HUD monster-health-bar inputs
	// (legacy gates src/Hud.cpp:825-835,866-874).
	void feedHealthBar() const;

private:
	Env env_;
	int fireEnd_[3] = { 0, 0, 0 };
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_TARGETING_H
