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

class EntityDefs;
class ScriptVM;
struct ScriptThread;

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

	// Open/close a door entity (n=0 open, n=1 close). n2 is the legacy snap
	// selector (src/Game.cpp:1153-1155): 0 = finish the animation instantly,
	// 1 = animate fully, 2 = snap only when offscreen (turn auto-close passes
	// 2, src/Game.cpp:1275) — cullBoundingBox is not ported so 2 animates
	// like 1 (documented deviation). Player use passes n2=1
	// (src/PlayingInputHandler.cpp:451); scripted opens pass the quiet-bit
	// derived value (src/ScriptThread.cpp:751,760). A snapped open still
	// registers the door in openDoors_ for auto-close (registration precedes
	// the snap decision, src/Game.cpp:1141-1155). ownerThread names the
	// script thread to resume when an OPEN animation completes (blocking
	// EV_DOOROP); nullptr = fire-and-forget. The legacy mapping guarantees
	// ownerThread == nullptr whenever n2 == 0.
	bool performDoorEvent(int n, Entity* door, int n2, ScriptThread* ownerThread = nullptr);

	// Plain-door use outcome (legacy hud msg44 / open+advanceTurn split,
	// src/PlayingInputHandler.cpp:445-453).
	enum class DoorUseResult { None, Opened, Locked };

	// Legacy interact: trace along the view ray, first ET_DOOR hit within
	// Chebyshev distance² <= tileDistances[0] = 4096 (1 tile)
	// (src/PlayingInputHandler.cpp:445-459, src/Combat.cpp:42,
	// src/Entity.cpp:1155-1158). Trace-free simplification: candidates are
	// LINKED doors on the player's tile and on the adjacent tile in the
	// facing direction (both satisfy dist² <= 4096 by construction); own
	// tile wins (ray fraction ~0). Returns the outcome; locked doors are
	// refused without animating.
	DoorUseResult useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY);

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

	// ---- Phase 5 additions ----

	// Locked<->unlocked def swap: flips bit0 of the sprite-info tileNum and
	// re-looks-up the EntityDef by tileNum+257 (src/Game.cpp:2477-2498).
	// Entity::name sync omitted — Entity::name not ported.
	void setLineLocked(Entity* e, bool locked);

	// Turn advance, subset of src/Game.cpp:1238-1281 (stats/bombs/monster
	// bookkeeping are placeholders until those systems exist).
	void advanceTurn();

	// Entity bound to a map sprite (legacy S_ENT lookup analog).
	Entity* findEntityBySprite(int sprite);

	// Arrival tile hook (legacy touchTile -> automap uncover/pickups);
	// stub this phase.
	void touchTile(int x, int y, bool b);

	// Trigger masks for the movement events (src/Game.cpp:974-1014):
	// eventFlags_[0] = LEAVE mask for the source tile,
	// eventFlags_[1] = ENTER mask for the destination tile.
	void eventFlagsForMovement(int x0, int y0, int x1, int y1);
	static int eventFlagForDirection(int dx, int dy);

	// ScriptVM back-pointer for advanceTurn's PER_TURN hook; forward-declared
	// here, included only in the .cpp (no header cycle). Set after construction.
	void setVM(ScriptVM* vm) { vm_ = vm; }

	// Turn/script coordination fields (legacy Game members).
	int monstersTurn = 0;
	bool queueAdvanceTurn = false;
	bool skipAdvanceTurn = false;
	bool abortMove = false;
	int spawnParam = -1;            // -1 = use the map header spawn
	int eventFlags_[2] = { 0, 0 };

private:
	void updateDoors();
	int playerX_ = -1, playerY_ = -1;

	std::vector<Entity> entities_;
	Entity* entityDb_[1024] = { nullptr }; // 32x32 tile lists
	MapData* map_ = nullptr;
	const EntityDefs* defs_ = nullptr;     // set in loadEntities
	ScriptVM* vm_ = nullptr;

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
		ScriptThread* ownerThread = nullptr; // resumed once when the OPEN completes
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