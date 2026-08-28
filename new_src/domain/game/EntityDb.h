#ifndef NEW_DOMAIN_GAME_ENTITYDB_H
#define NEW_DOMAIN_GAME_ENTITYDB_H

#include <vector>

#include "domain/game/Entity.h"

namespace newcore {

class MapData;
class Player;

// Peer subsystem owning the entity array and the 32x32 tile lists
// (spec 2026-08-26-decomposition §P2-GF). Single owner of the link/unlink and
// lookup helpers that P2-GA..GE had to copy into five subsystems; every
// subsystem's Env now carries one EntityDb* instead of the
// (entities, entityDb) pair. Moved verbatim out of Game.
class EntityDb {
public:
	static constexpr int kEntities = 275;

	// Cycle guard for tile-list walks. A corrupted list cannot exist by
	// construction, but one did (the unlinkEntity gate removed above) and it
	// froze the game at 100% CPU for a kill -9. A walk longer than the whole
	// entity array is impossible, so bail out there and print the visited
	// chain instead of spinning. Usage:
	//   EntityDb::TileWalk walk("Where::what");
	//   for (Entity* e = head; e && walk.ok(e); e = e->nextOnTile) ...
	class TileWalk {
	public:
		explicit TileWalk(const char* where) : where_(where) {}
		bool ok(const Entity* e);

	private:
		const char* where_;
		int steps_ = 0;
	};

	// Non-owning views on the world. map is read by removeEntity to hide the
	// bound sprite.
	struct Env {
		MapData* map = nullptr;
	};

	// Wiring (called from Game::loadEntities). The tile heads are NOT cleared
	// here: the pre-P2-GF code never cleared them on load either.
	void init(const Env& env) { env_ = env; }

	// Per-level entity array reset (was the entities_.clear()/resize pair at
	// the head of Game::loadEntities).
	void resetEntities();

	// removeEntity's facingEntity clear (src/Game.cpp:192); set through
	// Game::setXPSystems.
	void setPlayer(Player* player) { player_ = player; }

	// ---- entityDb (32x32 tile lists) ----

	// Entities at a world tile (head of list).
	Entity* findMapEntity(int x, int y) const;
	void linkEntity(Entity* e, int tx, int ty);
	void unlinkEntity(Entity* e);

	// Raw tile head by linear index, for the trace broadphase that walks the
	// bbox tile range directly (src/Game.cpp:218-220). idx must be in [0,1024).
	Entity* tileHead(int idx) const { return entityDb_[idx]; }

	// Returns the player entity (entities[1]).
	Entity* playerEntity() { return entities_.empty() ? nullptr : &entities_[1]; }

	// Air-shot/world-slot entity (legacy app->game->entities[0],
	// src/PlayingInputHandler.cpp:515,:536): def == nullptr so combat math
	// reads it as eType 0 (spec 2026-08-26-combat-stage1 deviation 13).
	Entity* worldEntity() { return entities_.empty() ? nullptr : &entities_[0]; }

	// Entity bound to a map sprite (legacy S_ENT lookup analog).
	Entity* findEntityBySprite(int sprite);

	// Port of Game::removeEntity (src/Game.cpp:183-193): hide the bound
	// sprite (info bit 0x10000) and unlink it from entityDb, then clear
	// player->facingEntity (:192).
	void removeEntity(Entity* e);

	const std::vector<Entity>& entities() const { return entities_; }
	std::vector<Entity>& entities() { return entities_; }

private:
	Env env_;

	Player* player_ = nullptr;

	std::vector<Entity> entities_;
	Entity* entityDb_[1024] = { nullptr }; // 32x32 tile lists
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ENTITYDB_H
