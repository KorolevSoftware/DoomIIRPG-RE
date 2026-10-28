#ifndef NEW_DOMAIN_GAME_MONSTERAI_H
#define NEW_DOMAIN_GAME_MONSTERAI_H

#include <cstdint>

namespace newcore {

class Combat;
class DoorSystem;
class Entity;
class EntityDb;
class MapData;
class MonsterSystem;
class Player;
class SpriteLerps;
class TraceSystem;
struct ViewVisibility;

// Per-monster brain (ADR 0028): goal validity and choice, weapon choice,
// attack re-validation and the one-tile step, ported verbatim from the
// Entity::ai* methods (src/Entity.cpp:535-1153). All per-monster state stays
// in EntityMonster; own state is the static monster-clip bitset and the
// path-search scratch. Enqueueing an attack goes through MonsterSystem::attack.
class MonsterAI {
public:
	struct Env {
		EntityDb* db = nullptr;
		MapData* map = nullptr;
		TraceSystem* trace = nullptr;
		Combat* combat = nullptr;          // weaponDef, getMonsterField, tile distances, nextByte
		MonsterSystem* monsters = nullptr; // attack(), interpolatingMonsters
		const Player* player = nullptr;    // viewX/viewY/destX/destY
		SpriteLerps* lerps = nullptr;      // step lerps (snap, alloc, clock)
		DoorSystem* doors = nullptr;       // a blocked step opens the door
		const ViewVisibility* visibility = nullptr;   // last frame's tile bits (on-screen step)
	};

	// Wiring + per-level reset (called from Game::loadEntities); rebuilds
	// baseVisitedTiles from the load-time solid tiles.
	void init(const Env& env);

	void think(Entity* e, bool b);                    // Entity::aiThink           src/Entity.cpp:673-685
	int  weaponForTarget(Entity* e, Entity* target);  // Entity::aiWeaponForTarget src/Entity.cpp:687-850
	bool isAttackValid(Entity* e);                    // Entity::aiIsAttackValid   src/Entity.cpp:644-671

	// Game::baseVisitedTiles bit test (src/Game.h:153); out-of-map tiles read
	// as clear.
	bool baseVisited(int x, int y) const;

	// Script monster clip (EV_MARKTILE / EV_UNMARKTILE flag bit 0,
	// src/Game.cpp:3656-3662).
	void setMonsterClip(int x, int y) { baseVisitedTiles_[y] |= 1u << x; }
	void unsetMonsterClip(int x, int y) { baseVisitedTiles_[y] &= ~(1u << x); }

	// Entity::aiFinishLerp (src/Entity.cpp:873-882), called by
	// SpriteLerps::freeLerpSprite when a step lerp completes.
	void finishLerp(Entity* e);

private:
	// Legacy Game path-search scratch (src/Game.h), reset per goalMove.
	struct PathSearch {
		uint64_t closestPath = 0;
		uint64_t curPath = 0;
		int closestPathDepth = 0;
		int closestPathDist = 0;
		int pathDepth = 0;
		int pathSearchDepth = 0;
		int lineOfSight = 0;
		int lineOfSightWeight = 0;
		int interactClipMask = 0;
		Entity* findEnt = nullptr;
		Entity* skipEnt = nullptr;
		uint32_t visitedTiles[32] = {};
	};

	bool isValidGoal(Entity* e);                      // src/Entity.cpp:605-642
	void chooseNewGoal(Entity* e, bool b);            // src/Entity.cpp:596-603
	void calcSimpleGoal(Entity* e, bool b);           // src/Entity.cpp:535-566
	void moveToGoal(Entity* e);                       // src/Entity.cpp:572-594
	bool goalMove(Entity* e);                         // src/Entity.cpp:1030-1116
	bool calcPath(int n, int n2, int n3, int n4, int n5, bool b);   // src/Entity.cpp:908-1028
	bool checkLineOfSight(int n, int n2, int n3, int n4, int n5);   // src/Entity.cpp:884-906
	void initLerp(Entity* e, int travelTime);         // src/Entity.cpp:852-871
	void reachedGoalMove(Entity* e);                  // src/Entity.cpp:1118-1153
	// Game::findMapEntity(x, y, mask) (src/Game.cpp:738-750), tile coords.
	Entity* firstOnTile(int tx, int ty, int mask);

	Env env_;
	uint32_t baseVisitedTiles_[32] = {};
	PathSearch ps_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_MONSTERAI_H
