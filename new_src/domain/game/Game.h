#ifndef NEW_DOMAIN_GAME_GAME_H
#define NEW_DOMAIN_GAME_GAME_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "domain/game/Combat.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Player.h"
#include "domain/game/TraceSystem.h"
#include "io/EntityDefs.h"
#include "domain/world/MapData.h"
#include "text/Text.h"

namespace newcore {

class EntityDefs;
class Hud;
class Localization;
class ScriptVM;
struct ScriptThread;
class Tables;

// %NN argument substitution over Localization strings (decode rules of
// Text::composeText, src/Text.cpp:281-326). Defined in Game.cpp; shared with
// Combat.cpp / Player messaging so the pattern has exactly one copy.
void composeArgs(std::string& text, const std::string* args, int numArgs);

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

	// Air-shot/world-slot entity (legacy app->game->entities[0],
	// src/PlayingInputHandler.cpp:515,:536): def == nullptr so combat math
	// reads it as eType 0 (spec 2026-08-26-combat-stage1 deviation 13).
	Entity* worldEntity() { return entities_.empty() ? nullptr : &entities_[0]; }

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B2.
	// Rebuilds the legacy pair view over trace.hits() for the consumers that
	// still walk pairs (GameContext fire election + facing probe).
	const std::vector<std::pair<int, Entity*>>& lastTraceHits() const;

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B1.
	int traceCollisionX() const { return trace.collisionX(); }
	int traceCollisionY() const { return trace.collisionY(); }

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

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B2.
	// Old bool + out-param shape of TraceSystem::trace; see TraceSystem.h for
	// the contract. map is ignored (TraceSystem holds the same MapData).
	bool traceMove(const MapData& map, int x0, int y0, int x1, int y1,
	               Entity* skipEnt, int mask, int radius,
	               Entity** outEntity = nullptr, int* outFrac = nullptr);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	void advanceTurnDoors();

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-F1.
	void setPlayerPos(int x, int y) { trace.setPlayerPos(x, y); }

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

	// Port of Game::removeEntity (src/Game.cpp:183-193): hide the bound
	// sprite (info bit 0x10000) and unlink it from entityDb. The
	// player->facingEntity clear has no counterpart (facingEntity not
	// ported).
	void removeEntity(Entity* e);

	// Faced lootable corpse: legacy ACTION_FIRE traces forward and selects an
	// ET_CORPSE candidate exactly one tile away (dist == tileDistances[0])
	// that is not yet looted and owns a lootSet (src/PlayingInputHandler.cpp:
	// 279-335). Trace-free simplification mirroring useDoorFacing: candidates
	// are LINKED corpses on the adjacent tile in the facing direction (own
	// tile is dist 0, never selected by legacy).
	Entity* findLootableCorpseFacing(int px, int py, int stepX, int stepY);

	// Pooled corpse-loot display state — legacy LootingSystem fields folded
	// into one struct (lootPool / numPoolItems / numLootItems /
	// lootPoolCredits / lootText / lootPoolIndices / lootLineNum).
	struct LootPool {
		static constexpr int kMaxLines = 9;      // lootPoolIndices[18] / 2 pairs
		int entries[Entity::kMaxCorpseLoot] = { 0, 0, 0 }; // packed u16 (cls<<12|idx<<6|cnt)
		int numEntries = 0;                      // numPoolItems (incl. class-6 flavor lines)
		int numItems   = 0;                      // numLootItems (stat only, counts pre-merge)
		int credits   = 0;                       // lootPoolCredits
		Text text;                               // lootText: '|'-separated lines
		short lineIndex[2 * kMaxLines] = { 0 };  // lootPoolIndices: <start,len> per line
		int topLine = 0;                         // lootLineNum (scroll pos, reset by pool)
		static int lineCount(const LootPool& p) { return p.numEntries + (p.credits != 0); }
	};

	// Mark-looted + pool + compose the loot list for ALL eType==9 entities on
	// tile (tx,ty) (src/LoothingSystem.cpp:154-278). Marks BEFORE reading
	// loot sets, per entity: prop ++param (skip when already != 0), monster
	// flag 0x800 (unified into ++param — see Deviations #1 of spec
	// 2026-08-25-loot-dwell-ui), info |= kInfoActivated.
	void poolLootCorpse(int tx, int ty, const Localization& loc, LootPool& out);

	// Grant pass (src/LoothingSystem.cpp:281-307): give() per non-class-6
	// entry, weapon starter ammo max(usage,10) of tables.weaponData[idx*9+4],
	// credits give(0,24,credits), foundLoot stderr stub, resets pool counters
	// + text. tables may be null (skips starter ammo).
	void giveLootPool(LootPool& pool, Player& player, const Tables* tables);


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

	// ---- Monsters / combat (spec 2026-08-26-combat-stage1 §0.B, §3.2) ----

	// Kill-XP state/presentation bridges (spec deviation 14): Player owns the
	// XP state, Game composes msg 103. Wired once from Main.cpp.
	void setXPSystems(Player* player, const Localization* loc, Hud* hud);

	// Turn/script coordination fields (legacy Game members).
	int monstersTurn = 0;
	bool queueAdvanceTurn = false;
	bool skipAdvanceTurn = false;
	bool skipDialog = false;        // set by DialogSystem::closeDialog(skip) around the thread resume
	bool abortMove = false;
	int spawnParam = -1;            // -1 = use the map header spawn
	int eventFlags_[2] = { 0, 0 };

	// Monster rings + combat-seq owner (legacy Game::activeMonsters /
	// inactiveMonsters / combatMonsters / interpolatingMonsters,
	// src/Game.cpp:884-939). combatMonsters is the Stage-2 pending-attack
	// queue head — declared only. Nothing sets interpolatingMonsters in
	// Stage 1, so its advanceTurn guard is a defensive log-only branch.
	Entity* activeMonsters = nullptr;
	Entity* inactiveMonsters = nullptr;
	Entity* combatMonsters = nullptr;
	bool interpolatingMonsters = false;
	bool facingDirty = false;       // canvas updateFacingEntity latch analog (src/Entity.cpp:527)

	Combat combat;                  // peer subsystem (ADR 0008)
	TraceSystem trace;              // peer subsystem (spec §P2-GA); wired in loadEntities

	// Faithful ring moves (src/Game.cpp:752-808, :825-855). activate ports:
	// runStaticFunc fires SCR_MONSTER_ACTIVATE on MFLAG_TRIGGERONACTIVATE,
	// rangeCheck gates at tileDistances[3], alertSound logs (no audio),
	// b4 unused like legacy.
	void activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4);
	void deactivate(Entity* e);

	// Per-frame monster phase (src/Game.cpp:2458-2474): Stage-1 stub whose
	// only job is closing the monstersTurn window (no AI, no lerps — spec §B).
	void updateMonsters();
	void endMonstersTurn();         // src/Game.cpp:2452-2456
	void snapMonsters(bool b);      // Stage-1 stub (spec §0.B)

	// Difficulty source: ScriptVM vars[12], default 2 when no VM is wired
	// (spec §1 difficulty note).
	int difficulty() const;

	// eSubType in [FIRSTBOSS..LASTBOSS] (src/Entity.cpp:1399 shape).
	static bool isBossDef(const EntityDef* def);

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B2.
	int entityDistFrom(const Entity* e, int x, int y) const { return trace.distFrom(e, x, y); }

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

private:
	void updateDoors();
	void freeLerpSprite(SpriteLerp* ls);       // completion snap + slot free (src/Game.cpp:3078-3243, subset)

	// Fixed monster payload pool (legacy entityMonsters[80], Error 37 on
	// overflow src/Game.cpp:430-436 — rewrite logs and skips). Lifetime is
	// one map load; loadEntities resets numMonsters_.
	static constexpr int kMaxMonsters = 80;
	EntityMonster entityMonsters_[kMaxMonsters];
	int numMonsters_ = 0;

	// setXPSystems wiring targets.
	Player* xpPlayer_ = nullptr;
	const Localization* xpLoc_ = nullptr;
	Hud* xpHud_ = nullptr;

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

	// FORWARDER scratch (spec §3.1): pair view rebuilt by lastTraceHits().
	mutable std::vector<std::pair<int, Entity*>> legacyTraceHits_;

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