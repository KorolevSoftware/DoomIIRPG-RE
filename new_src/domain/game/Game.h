#ifndef NEW_DOMAIN_GAME_GAME_H
#define NEW_DOMAIN_GAME_GAME_H

#include <cstdint>
#include <utility>
#include <vector>

#include "domain/game/Entity.h"
#include "domain/game/Player.h"
#include "io/EntityDefs.h"
#include "domain/world/MapData.h"

namespace newcore {

// World simulation: entity database (32x32 tiles), door open/close, item
// pickup. Minimal modern port of the legacy Game focused on the interactive
// subset (doors + items). Monsters/combat come later.
class Game {
public:
	static constexpr int kEntities = 275;
	static constexpr int kOpenDoors = 6;

	Game() = default;

	// Builds door/item entities from the map's sprite table. Must be called
	// once per level load, after MapData is parsed.
	void loadEntities(MapData& map, const EntityDefs& defs);

	// Tick called every frame (dtMs). Advances door animations.
	void update(int dtMs);

	// Returns the player entity (entities[1]).
	Entity* playerEntity() { return entities_.empty() ? nullptr : &entities_[1]; }

	// Entities at a world tile (head of list).
	Entity* findMapEntity(int x, int y);
	void linkEntity(Entity* e, int tx, int ty);
	void unlinkEntity(Entity* e);

	const std::vector<Entity>& entities() const { return entities_; }

	// Open/close a door entity (n=0 open, n=1 close). Animates scale 64->0.
	bool performDoorEvent(int n, Entity* door);

	// Legacy interact: trace along the view ray, first ET_DOOR hit within
	// Chebyshev distance² <= tileDistances[0] = 4096 (1 tile)
	// (src/PlayingInputHandler.cpp:445-459, src/Combat.cpp:42,
	// src/Entity.cpp:1155-1158). Trace-free simplification: candidates are
	// LINKED doors on the player's tile and on the adjacent tile in the
	// facing direction (both satisfy dist² <= 4096 by construction); own
	// tile wins (ray fraction ~0). Locked refusals handled inside
	// performDoorEvent. Returns the attempted entity or nullptr.
	Entity* useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY);

	// Swept-capsule move trace: legacy Game::trace (7-arg wrapper
	// src/Game.cpp:195-197, body :199-327) + Render::traceWorld
	// (src/Render.cpp:1212-1283, flattened — see spec 2026-08-23
	// faithful-player-collision §3.3). Sweeps segment (x0,y0)->(x1,y1) as a
	// capsule of the given radius (canvas units, tile=64) against world lines
	// (if mask & 1) and all entityDb entities matching mask & (1<<eType),
	// skipping skipEnt. Returns TRUE when nothing blocks (commit allowed) —
	// legacy commits iff traceEntity == nullptr (src/MovementController.cpp:332-333).
	// Out-params (optional): closest hit = lowest frac (legacy traceEntity /
	// traceFracs[0], src/Game.cpp:312-326); frac is 14.14 fixed point,
	// 16384 == 1.0, hit <= 16382, start-inside == -1, miss sentinel 16384
	// (src/Render.cpp:1119-1125,1195-1209).
	bool traceMove(const MapData& map, int x0, int y0, int x1, int y1,
	               Entity* skipEnt, int mask, int radius,
	               Entity** outEntity = nullptr, int* outFrac = nullptr);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	void advanceTurnDoors();

	// Player position (canvas units) used by door auto-close checks.
	void setPlayerPos(int x, int y) { playerX_ = x; playerY_ = y; }

private:
	void updateDoors();
	int playerX_ = -1, playerY_ = -1;

	std::vector<Entity> entities_;
	Entity* entityDb_[1024] = { nullptr }; // 32x32 tile lists
	MapData* map_ = nullptr;

	// Trace scratch (reused buffers; single-threaded GL loop).
	int tracePoints_[4] = { 0, 0, 0, 0 };              // x0,y0,x1,y1 (src/Game.cpp:208-211)
	int traceBBox_[4]   = { 0, 0, 0, 0 };              // clamped bbox (src/Game.cpp:212-215)
	std::vector<std::pair<int, Entity*>> traceHits_;   // (frac 14.14, entity)

	void traceEntityHits(const MapData& map, Entity* skipEnt, int mask, int radius); // src/Game.cpp:216-296
	int  traceWorldFrac(const MapData& map, int mask, int radius2);                  // src/Render.cpp:1212-1283 (flat)

	struct DoorAnim {
		Entity* door = nullptr;
		int sprite = -1;
		int srcX = 0, srcY = 0, dstX = 0, dstY = 0; // slide position (canvas units)
		int startScale = 64, endScale = 0;
		int t = 0;
		int dur = 750;
		bool active = false;
		bool opening = false; // true = opening, false = closing
	};
	DoorAnim doorAnims_[kOpenDoors];
	// Open doors that can auto-close (legacy openDoors[6]).
	Entity* openDoors_[kOpenDoors] = { nullptr };
	void unlinkDoor(Entity* door);
	bool doorRegistered(Entity* door) const;
	void registerOpenDoor(Entity* door);
	void unregisterOpenDoor(Entity* door);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	bool canCloseDoor(Entity* door);
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_GAME_H