#ifndef NEW_DOMAIN_GAME_MONSTERSYSTEM_H
#define NEW_DOMAIN_GAME_MONSTERSYSTEM_H

#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"

namespace newcore {

class Combat;
class EntityDb;
class GameSprites;
class Hud;
class Localization;
class MapData;
class MonsterAI;
class Player;
class ScriptVM;
class SpriteLerps;
class TraceSystem;
class EntityDefs;
struct EntityDef;
struct ViewVisibility;

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
		Combat* combat = nullptr;            // shotsFired + tileDistSq + the monster attack seq
		TraceSystem* trace = nullptr;        // player pos + distFrom + the sight-wake trace
		int* monstersTurn = nullptr;
		bool* facingDirty = nullptr;
		// Lerp pool owner: the pose-hold clock (clockMs) and the
		// force-complete of a sprite's active lerps (legacy snapLerpSprites,
		// src/Game.cpp:1149-1166).
		SpriteLerps* lerps = nullptr;
		MonsterAI* ai = nullptr;             // per-monster decisions (ADR 0028)
		const Player* player = nullptr;      // health check of onMonsterAttackDone
		const GameSprites* sprites = nullptr; // activePropogators gate of renderActivate
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
	// src/Game.cpp:884-939). combatMonsters is the pending-attack LIFO
	// (monster->nextAttacker links). MonsterAI::goalMove sets
	// interpolatingMonsters when a step starts; monsterLerp clears it.
	Entity* activeMonsters = nullptr;
	Entity* inactiveMonsters = nullptr;
	Entity* combatMonsters = nullptr;
	bool interpolatingMonsters = false;
	// monsterAI runs once per monster turn (Game::monstersUpdated,
	// src/Game.cpp:880-883); reset by Game::advanceTurn.
	bool monstersUpdated = false;

	// Faithful ring moves (src/Game.cpp:752-808, :825-855). activate ports:
	// runStaticFunc fires SCR_MONSTER_ACTIVATE on MFLAG_TRIGGERONACTIVATE,
	// rangeCheck gates at tileDistSq(4), alertSound logs (no audio),
	// b4 unused like legacy.
	void activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4);
	void deactivate(Entity* e);

	// Sight wake (src/Render.cpp:1576-1583), run on the next tick over the
	// sprites the last world frame drew (ADR 0029); empties the list.
	void renderActivate(ViewVisibility& vis);

	// Pending-attack queue (ADR 0028).
	void attack(Entity* e);          // Entity::attack     src/Entity.cpp:1160-1167 (LIFO push)
	void undoAttack(Entity* e);      // Entity::undoAttack src/Entity.cpp:1169-1191

	// Monster turn driver.
	void monsterAI();                // src/Game.cpp:874-913
	void monsterLerp();              // src/Game.cpp:915-939 (no invalidateRect)
	void updateMonsters();           // src/Game.cpp:2458-2474, called per Playing tick
	void endMonstersTurn();          // src/Game.cpp:2452-2456
	bool canSnapMonsters() const;    // src/Game.cpp:2374-2389
	bool snapMonsters(bool b);       // src/Game.cpp:2391-2450
	// The monster arm of GameStateRunner::combatState once a monster attack
	// sequence finished (src/GameStateRunner.cpp:41-78): next attacker or end
	// of the monster turn.
	void onMonsterAttackDone();

	// eSubType in [FIRSTBOSS..LASTBOSS] (src/Entity.cpp:1399 shape).
	static bool isBossDef(const EntityDef* def);
	// Entity::isBoss (src/Entity.cpp:1398-1400): bosses except mastermind parm 0.
	static bool isBoss(const Entity* e);
	static bool isHasteResistant(const Entity* e) { return isBoss(e); }   // src/Entity.cpp:1402-1404

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
	// (kInfoCorpse|kInfoActive|kInfoDirty), def swap to find(ET_CORPSE, subtype,
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
