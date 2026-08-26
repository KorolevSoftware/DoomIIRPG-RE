#ifndef NEW_DOMAIN_GAME_PLAYER_H
#define NEW_DOMAIN_GAME_PLAYER_H

#include <cstdint>

#include "domain/game/CombatEntity.h"
#include "domain/game/Enums.h"
#include "io/EntityDefs.h"

namespace newcore {

class Combat;
class Entity;

// The player character: stats, inventory, keys, position/heading. Modern
// minimal port of the legacy Player. Inventory slots match legacy indices
// (18 journal, 19 red key, 20 blue key, ...).
class Player {
public:
	CombatEntity baseCe; // base (permanent) stats
	CombatEntity ce;     // current stats (with buffs/gear)

	// Inventory: slot counts (legacy layout).
	int16_t inventory[26] = { 0 };
	// Ammo per weapon ammo type (0 none,1 bullets,2 shells,3 holy water,
	// 4 cells, 5 rockets, 6 soul cube, 7 sentry bot, 8 item).
	int16_t ammo[9] = { 0 };
	// Bitmask of owned weapons.
	int weapons = 0;
	// Active weapon index.
	int weapon = -1;
	const EntityDef* activeWeaponDef = nullptr;
	// op 60 EV_DISABLED_WEAPONS mask (src/ScriptThread.cpp:1445); consumed
	// by fireWeapon's guard.
	int disabledWeapons = 0;
	// Facing-probe result (src/MovementController.cpp:32-87); fed by the
	// Group-2 facing probe, consumed by the HUD health-bar feed.
	Entity* facingEntity = nullptr;

	int level = 1;
	int currentXP = 0;
	int nextLevelXP = 0;
	int xpGained = 0;
	bool god = false;

	// ---- Discrete movement (legacy Canvas) ----
	// Positions/angles in canvas units (tile = 64). view* is the interpolated
	// (current) position, dest* is where the player is heading.
	int viewX = 0, viewY = 0, viewZ = 0;
	int destX = 0, destY = 0, destZ = 0;
	// Angles are RAW ACCUMULATED ints (legacy never masks them after spawn;
	// src/MovementController.cpp:455-466 marches linearly and every consumer
	// masks with & 0x3FF at use). 1024 units = 360 degrees.
	int viewAngle = 0, destAngle = 0;
	// View pitch, legacy units (1024 = full turn, positive = up; written into
	// the camera view matrix). Only the loot-crouch drives it so far — the
	// terrain-slope target machinery is not ported yet
	// (src/MovementController.cpp:230-281).
	int viewPitch = 0;
	int animFrames = 10;
	int animPos = 0;   // step per frame in X/Y
	int animAngle = 0; // step per frame in angle
	int zStep = 0;     // step per frame in Z
	int viewSin = 0, viewCos = 0;
	int viewStepX = 0, viewStepY = 0;
	int viewRightStepX = 0, viewRightStepY = 0;

	// 8 direction vectors (dx,dy) at 45 deg (legacy Canvas::viewStepValues).
	static const int kViewStepValues[16];

	// Recomputes direction vectors / animation params for destAngle.
	void startRotation();
	// Advances view* toward dest* by one frame. Returns true when arrived.
	bool updateView();
	// Snap view* to dest* (used on arrival).
	void finishRotation();

	// Attempts to move one tile to (tx,ty) in canvas units. Returns true if
	// the move is accepted (caller performs collision check).
	bool attemptMove(int tx, int ty);

	// Sets the target height from a terrain height (legacy: getHeight+36).
	void setDestHeight(int terrainHeight) { destZ = terrainHeight + 36; }
	// Sets the Z step per frame based on height difference.
	void setZStep(int diff) { zStep = (diff < 0 ? -diff : diff); if (zStep) zStep = (zStep + animFrames - 1) / animFrames; }

	// Starts the player at default stats (3 classes not implemented yet).
	void reset();

	// give(kind, slot, amount): kind 0 = inventory, 1 = weapons, 2 = ammo,
	// 3 = health. Mirrors legacy Player::give.
	bool give(int kind, int slot, int amount);

	// requireItem(kind, slot, min, max): inventory count check or weapon bit.
	bool requireItem(int kind, int slot, int min, int max) const;

	bool addHealth(int amount);

	int getHealth() const { return ce.getStat(Enums::STAT_HEALTH); }
	int getMaxHealth() const { return ce.getStat(Enums::STAT_MAX_HEALTH); }

	// ---- XP / leveling (src/Player.cpp:264-362; spec deviation 14) ----

	// State half of legacy addXP: counters + level-up loop. The msg-103
	// composition lives in Game::awardKillXP.
	void addXP(int xp);
	// 500n + 100((n−1)^3 + (n−1)) (src/Player.cpp:360-362).
	int calcLevelXP(int n) const;
	// Subset: level++, nextLevelXP, baseCe MAX_HEALTH +10 clamp 999, health
	// refill (src/Player.cpp:283-307,:342). modifyStat DEF/STR/ACC/AGI bumps
	// are deferred and logged.
	void addLevel();

	// Fire-pipeline entry (src/Player.cpp:754-799): guard chain then
	// combat.performAttack. Returns false when the shot is refused.
	bool fireWeapon(Combat& combat, Entity* target, int x, int y);
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_PLAYER_H