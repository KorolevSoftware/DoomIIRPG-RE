#ifndef NEW_DOMAIN_GAME_ENTITYMONSTER_H
#define NEW_DOMAIN_GAME_ENTITYMONSTER_H

#include "domain/game/CombatEntity.h"

namespace newcore {

class Entity;

// Per-monster gameplay payload (plain struct, fixed pool on Game — ADR 0008).
// Field-for-field mirror of src/EntityMonster.h:13-42 minus the save/load and
// touch sentinels. The goal/AI fields are driven by MonsterAI (ADR 0028).
struct EntityMonster {
	// goalType values (docs/original-code/monster-turn.md §3.2; names are
	// ours, values legacy).
	static constexpr int kGoalNone = 0;
	static constexpr int kGoalMoveTile = 1;
	static constexpr int kGoalChase = 2;
	static constexpr int kGoalAttack = 3;
	static constexpr int kGoalFlee = 4;
	static constexpr int kGoalRetreat = 5;
	static constexpr int kGoalWait = 6;

	// goalFlags bits (docs/original-code/monster-turn.md §1.3).
	static constexpr int kGoalFlagLerping = 0x1;   // one-tile step lerp running
	static constexpr int kGoalFlagRetreat = 0x8;   // evading monster retreats after its attack
	static constexpr int kGoalFlagRepath = 0x10;   // no setter in src/

	// goalParam value naming the player as the goal entity (entities[1]).
	static constexpr int kGoalParamPlayer = 1;

	CombatEntity ce;
	Entity* nextOnList = nullptr;   // circular ring links (src/EntityMonster.h:20-21)
	Entity* prevOnList = nullptr;
	Entity* nextAttacker = nullptr; // pending-attack chain (src/Entity.cpp:1160-1167)
	Entity* target = nullptr;
	int frameTime = 0;              // pose hold deadline on Game's lerp clock
	short flags = 0;                // MFLAG_* bits (domain/game/Enums.h:275-290)
	int monsterEffects = 0;
	uint8_t goalType = 0;
	uint8_t goalFlags = 0;
	uint8_t goalTurns = 0;
	int goalX = 0;
	int goalY = 0;
	int goalParam = 0;

	// Zeroes everything, weapon -1 (src/EntityMonster.cpp:14-23 + spec §2.1).
	void reset() {
		ce = CombatEntity();
		ce.weapon = -1;
		nextOnList = prevOnList = nullptr;
		nextAttacker = nullptr;
		target = nullptr;
		frameTime = 0;
		flags = 0;
		monsterEffects = 0;
		resetGoal();
	}

	// Goal-state clear (src/EntityMonster.cpp:39-46) plus target. Clearing
	// `target` is a deviation (legacy leaves it), harmless: target is read only
	// for monsters queued in combatMonsters, and the sole queuing path
	// (moveToGoal, goal 3) writes it after the reset.
	void resetGoal() {
		goalType = 0;
		goalFlags = 0;
		goalTurns = 0;
		goalX = 0;
		goalY = 0;
		goalParam = 0;
		target = nullptr;
	}
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ENTITYMONSTER_H
