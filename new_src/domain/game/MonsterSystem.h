#ifndef NEW_DOMAIN_GAME_MONSTERSYSTEM_H
#define NEW_DOMAIN_GAME_MONSTERSYSTEM_H

#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"

namespace newcore {

class Combat;
class EntityDb;
class Hud;
class Localization;
class MapData;
class Player;
class ScriptVM;
class SpriteLerps;
class TraceSystem;
class EntityDefs;
struct EntityDef;

// Peer subsystem owning the monster payload pool, the activate/deactivate
// rings, the monster turn window and the pain/death/corpse transitions
// (spec 2026-08-26-decomposition §P2-GC). Moved verbatim out of Game.
class MonsterSystem {
public:
	// Non-owning views on the world. db owns the entity array and the 1024
	// tile heads (spec §P2-GF); monstersTurn / facingDirty point at the Game
	// fields those flags still live on; the pose-hold time base is the
	// SpriteLerps clock (spec 2026-08-26-combat-stage1 deviation D-6).
	struct Env {
		EntityDb* db = nullptr;
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
		ScriptVM* vm = nullptr;
		const Combat* combat = nullptr;      // shotsFired + tileDistSq
		const TraceSystem* trace = nullptr;  // player pos + distFrom
		int* monstersTurn = nullptr;
		bool* facingDirty = nullptr;
		// Lerp pool owner: the pose-hold clock (clockMs) and the
		// force-complete of a sprite's active lerps (legacy snapLerpSprites,
		// src/Game.cpp:1149-1166).
		SpriteLerps* lerps = nullptr;
	};

	// Wiring + per-level reset (called from Game::loadEntities).
	void init(const Env& env);

	// Fixed monster payload pool (legacy entityMonsters[80], Error 37 on
	// overflow src/Game.cpp:430-436 — rewrite logs and returns nullptr, and
	// the caller skips the sprite). Lifetime is one map load; init resets
	// the high-water mark.
	EntityMonster* allocMonster();

	// Kill-XP state/presentation bridges (spec 2026-08-26-combat-stage1
	// deviation 14): Player owns the XP state, this composes msg 103.
	// Wired once from Main.cpp (through Game).
	void setXPSystems(Player* player, const Localization* loc, Hud* hud);

	// Monster rings + combat-seq owner (legacy Game::activeMonsters /
	// inactiveMonsters / combatMonsters / interpolatingMonsters,
	// src/Game.cpp:884-939). combatMonsters is the Stage-2 pending-attack
	// queue head — declared only. Nothing sets interpolatingMonsters in
	// Stage 1, so its advanceTurn guard is a defensive log-only branch.
	Entity* activeMonsters = nullptr;
	Entity* inactiveMonsters = nullptr;
	Entity* combatMonsters = nullptr;
	bool interpolatingMonsters = false;

	// Faithful ring moves (src/Game.cpp:752-808, :825-855). activate ports:
	// runStaticFunc fires SCR_MONSTER_ACTIVATE on MFLAG_TRIGGERONACTIVATE,
	// rangeCheck gates at tileDistSq(4), alertSound logs (no audio),
	// b4 unused like legacy.
	void activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4);
	void deactivate(Entity* e);

	// Per-frame monster phase (src/Game.cpp:2458-2474): Stage-1 stub whose
	// only job is closing the monstersTurn window (no AI, no lerps — spec §B).
	void updateMonsters();
	void endMonstersTurn();          // src/Game.cpp:2452-2456
	void snapMonsters(bool b);       // Stage-1 stub (spec §0.B)

	// eSubType in [FIRSTBOSS..LASTBOSS] (src/Entity.cpp:1399 shape).
	static bool isBossDef(const EntityDef* def);

	// Entity::pain ET_MONSTER non-boss subset (src/Entity.cpp:281-394):
	// MFLAG_NOKILL floor, pain/death pose overlay + frameTime hold,
	// resetGoal unless holy-water attacker. Boss phase staticFuncs deferred.
	bool painMonster(Entity* e, int dmg, int attackerWeaponId);

	// Entity::died ET_MONSTER subset (src/Entity.cpp:459-521): death pose,
	// corpse info bits, def swap to ET_CORPSE, deactivate, optional XP.
	void diedMonster(Entity* e, bool giveXP);

	// checkMonsterDeath(b=true) XP half (src/Entity.cpp:407-413) with the
	// message composition split out of Player::addXP (spec deviation 14).
	void awardKillXP(const EntityMonster& m);

	// Port of ScriptThread::corpsifyMonster (src/ScriptThread.cpp:2249-2266),
	// visual/flag subset: death-frame overlay (spriteInfo bits 8-14 = 0x7000),
	// reposition to the tile center at ground+32, corpse info bits
	// (0x1000000|0x20000|0x400000), def swap to find(ET_CORPSE, subtype,
	// parm), relink at the new tile. The inactiveMonsters ring, death sound
	// and name refresh are not ported yet.
	void corpsifyMonster(Entity* e, int x, int y);

private:
	Env env_;

	static constexpr int kMaxMonsters = 80;
	EntityMonster entityMonsters_[kMaxMonsters];
	int numMonsters_ = 0;

	// setXPSystems wiring targets.
	Player* xpPlayer_ = nullptr;
	const Localization* xpLoc_ = nullptr;
	Hud* xpHud_ = nullptr;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_MONSTERSYSTEM_H
