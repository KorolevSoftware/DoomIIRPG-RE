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
class Hud;
class Localization;
class ScriptVM;
struct ScriptThread;
class Tables;

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

	// Tick called every frame (dtMs). Advances door animations and the
	// script sprite lerps (LERP* opcodes).
	void update(int dtMs);

	// ---- Script sprite lerps (docs/original-code/lerp-opcodes.md) ----

	static constexpr int kMaxLerpSprites = 16;   // pool size (src/Game.cpp:3028)

	// One active script lerp; subset of legacy LerpSprite (save/load,
	// TRUNC/chicken/door/secret tails are not ported).
	struct SpriteLerp {
		// Runtime flag bits (src/Enums.h:293-295 subset).
		static constexpr int kFlagAsync = 0x1;
		static constexpr int kFlagAnimatingEffect = 0x2;
		static constexpr int kFlagParabola = 0x4;
		static constexpr int kFlagAutoFace = 0x800;   // LS_FLAG_AUTO_FACE (src/Enums.h:295)

		int hSprite = 0;          // sprite+1; 0 = free slot (src/Game.cpp:3030)
		ScriptThread* ownerThread = nullptr;
		int startTime = 0;        // ms on the Game::update clock (clockMs)
		int travelTime = 0;       // ms
		int srcX = 0, srcY = 0, srcZ = 0;
		int dstX = 0, dstY = 0, dstZ = 0;
		int srcScale = 64, dstScale = 64;
		int height = 0;           // parabola arc peak (canvas z units)
		int flags = 0;            // SpriteLerp::kFlag* bits
		int dist = 0;             // Euclidean move length, canvas units

		// dist = isqrt((dx²+dy²)<<8) >> 8 (src/LerpSprite.cpp:48); feeds the
		// distance-driven walk phase ((1+(p*dist>>12))&3, one cycle per tile).
		void calcDist();
	};

	// Pool lookup mirroring allocLerpSprite (src/Game.cpp:3028-3066): reuse
	// the slot of a still-active lerp for the same sprite, else take a free
	// one. Exhaustion logs instead of the legacy Error(36) fatal.
	SpriteLerp* allocLerpSprite(ScriptThread* thread, int sprite, bool block);

	// Single tick (src/Game.cpp:2855-2956): writes S_X/S_Y/S_Z/S_SCALEFACTOR
	// with p=(elapsed<<16)/(travelTime<<8), snaps + frees on completion.
	// Returns 3 when completed, 4 when not started yet, else 0.
	int updateLerpSprite(SpriteLerp* ls);

	// Stored-vs-baked S_Z bias: legacy postProcessSprites bakes terrain
	// height (-32 for z-sprites) into stored S_Z at load and the legacy
	// renderer consumes it directly (src/Render.cpp:2459-2467,1424); our
	// renderer keeps stored Z raw-relative and re-adds terrain per frame
	// (World3D.cpp:547-556). Lerps interpolate in legacy baked space:
	// baked = stored + spriteZBias, stored = baked - spriteZBias.
	int spriteZBias(int sprite, int x, int y) const;

	// Internal ms clock for lerp start/elapsed math; advanced by update(dtMs).
	int clockMs() const { return lerpClock_; }

	// 1024-entry fixed-point sine table for the parabola arc (sin << 14);
	// wired once after construction.
	void setSinTable(const std::vector<int32_t>* sinTable) { sinTable_ = sinTable; }

	// View angle used by the walk-state writer's front/back chooser. Fed by
	// GameContext each tick (maya pose during cinematics, else player view) —
	// reproduces legacy reading app->render->viewAngle, i.e. the previous
	// frame's view (src/Game.cpp:2910).
	void setLerpViewAngle(int a) { lerpViewAngle_ = a; }

	// Move vector -> 8-direction angle * 128 with ±32 thresholds
	// (src/Game.cpp:3596-3628, b=true).
	static int vecToDir(int dx, int dy);

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

	// ---- Corpse looting (docs/original-code/loot-inventory.md) ----

	// Port of ScriptThread::corpsifyMonster (src/ScriptThread.cpp:2249-2266),
	// visual/flag subset: death-frame overlay (spriteInfo bits 8-14 = 0x7000),
	// reposition to the tile center at ground+32, corpse info bits
	// (0x1000000|0x20000|0x400000), def swap to find(ET_CORPSE, subtype,
	// parm), relink at the new tile. The inactiveMonsters ring, death sound
	// and name refresh are not ported yet.
	void corpsifyMonster(Entity* e, int x, int y);

	// Faced lootable corpse: legacy ACTION_FIRE traces forward and selects an
	// ET_CORPSE candidate exactly one tile away (dist == tileDistances[0])
	// that is not yet looted and owns a lootSet (src/PlayingInputHandler.cpp:
	// 279-335). Trace-free simplification mirroring useDoorFacing: candidates
	// are LINKED corpses on the adjacent tile in the facing direction (own
	// tile is dist 0, never selected by legacy).
	Entity* findLootableCorpseFacing(int px, int py, int stepX, int stepY);

	// Direct-grant corpse loot — the ST_LOOTING UI is not ported. Marks the
	// source looted (++param; info |= kInfoActivated, src/LootingSystem.cpp:
	// 163-179), pools + merges its lootSet (class-6 lines skipped, class-0
	// idx 24/25 become credits, dupes merge saturated to 63, src/
	// LootingSystem.cpp:180-221), grants via Player::give with weapon starter
	// ammo from tables.weaponData (src/LootingSystem.cpp:281-307), then
	// reports through the HUD center-message path: str84 keycard / str85 got-
	// item / str86 N x item / str228 empty (src/Entity.cpp:171-190).
	// tables may be null (disables weapon starter ammo).
	void lootCorpse(Entity* corpse, const Localization& loc, Hud& hud,
	                Player& player, const Tables* tables);


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
	bool skipDialog = false;        // set by DialogSystem::closeDialog(skip) around the thread resume
	bool abortMove = false;
	int spawnParam = -1;            // -1 = use the map header spawn
	int eventFlags_[2] = { 0, 0 };

private:
	void updateDoors();
	void freeLerpSprite(SpriteLerp* ls);       // completion snap + slot free (src/Game.cpp:3078-3243, subset)
	int playerX_ = -1, playerY_ = -1;

	// Script sprite lerp pool (legacy Game::lerpSprites[16]).
	SpriteLerp spriteLerps_[kMaxLerpSprites];
	int lerpClock_ = 0;
	int lerpViewAngle_ = 0;                // last render view angle (setLerpViewAngle)
	const std::vector<int32_t>* sinTable_ = nullptr;

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