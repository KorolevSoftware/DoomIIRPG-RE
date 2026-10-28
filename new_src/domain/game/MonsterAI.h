#ifndef NEW_DOMAIN_GAME_MONSTERAI_H
#define NEW_DOMAIN_GAME_MONSTERAI_H

#include <cstdint>

namespace newcore {

class Combat;
class Entity;
class EntityDb;
class MapData;
class MonsterSystem;
class Player;
class TraceSystem;

// Per-monster brain (ADR 0028): goal validity and choice, weapon choice and
// attack re-validation, ported verbatim from the Entity::ai* methods
// (src/Entity.cpp:535-850). All per-monster state stays in EntityMonster;
// the only own state is the static monster-clip bitset. Enqueueing an attack
// goes through MonsterSystem::attack.
class MonsterAI {
public:
	struct Env {
		EntityDb* db = nullptr;
		MapData* map = nullptr;
		TraceSystem* trace = nullptr;
		Combat* combat = nullptr;          // weaponDef, getMonsterField, tile distances, nextByte
		MonsterSystem* monsters = nullptr; // attack()
		const Player* player = nullptr;    // viewX/viewY/destX/destY
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

private:
	bool isValidGoal(Entity* e);                      // src/Entity.cpp:605-642
	void chooseNewGoal(Entity* e, bool b);            // src/Entity.cpp:596-603
	void calcSimpleGoal(Entity* e, bool b);           // src/Entity.cpp:535-566
	void moveToGoal(Entity* e);                       // src/Entity.cpp:572-594
	bool goalMove(Entity* e);                         // src/Entity.cpp:1030-1116 (movement: G6)
	// Game::findMapEntity(x, y, mask) (src/Game.cpp:738-750), tile coords.
	Entity* firstOnTile(int tx, int ty, int mask);

	Env env_;
	uint32_t baseVisitedTiles_[32] = {};
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_MONSTERAI_H
