#ifndef NEW_DOMAIN_GAME_GAME_H
#define NEW_DOMAIN_GAME_GAME_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "domain/game/Combat.h"
#include "domain/game/DoorSystem.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/MonsterSystem.h"
#include "domain/game/Player.h"
#include "domain/game/SpriteLerps.h"
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

	Game() = default;

	// Builds door/item entities from the map's sprite table. Must be called
	// once per level load, after MapData is parsed.
	void loadEntities(MapData& map, const EntityDefs& defs);

	// Tick called every frame (dtMs). Advances door animations and the
	// script sprite lerps (LERP* opcodes).
	void update(int dtMs);

	// ---- Script sprite lerps (spec 2026-08-26-decomposition §P2-GD) ----

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-F4.
	// The lerp pool, its clock and the walk-state writer live in SpriteLerps
	// now (see SpriteLerps.h for the contracts); these keep the pre-P2-GD
	// call sites (ScriptVM LERP* opcodes, GameContext wiring) compiling.
	using SpriteLerp = SpriteLerps::SpriteLerp;
	SpriteLerp* allocLerpSprite(ScriptThread* thread, int sprite, bool block) {
		return lerps.allocLerpSprite(thread, sprite, block);
	}
	int updateLerpSprite(SpriteLerp* ls) { return lerps.updateLerpSprite(ls); }
	int spriteZBias(int sprite, int x, int y) const { return lerps.spriteZBias(sprite, x, y); }
	int clockMs() const { return lerps.clockMs(); }
	void setSinTable(const std::vector<int32_t>* sinTable) { lerps.setSinTable(sinTable); }
	void setLerpViewAngle(int a) { lerps.setLerpViewAngle(a); }
	static int vecToDir(int dx, int dy) { return SpriteLerps::vecToDir(dx, dy); }

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
	// still walk pairs (GameContext fire election + facing probe). The returned
	// reference is valid only until the next call (mutable rebuild in place).
	const std::vector<std::pair<int, Entity*>>& lastTraceHits() const;

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B1.
	int traceCollisionX() const { return trace.collisionX(); }
	int traceCollisionY() const { return trace.collisionY(); }

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-F2.
	// Door open/close now lives in DoorSystem (see DoorSystem.h for the
	// contracts); these keep the pre-P2-GB call sites compiling.
	using DoorUseResult = DoorSystem::DoorUseResult;
	bool performDoorEvent(int n, Entity* door, int n2, ScriptThread* ownerThread = nullptr) {
		return doors.performDoorEvent(n, door, n2, ownerThread);
	}
	DoorUseResult useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY) {
		return doors.useDoorFacing(map, px, py, stepX, stepY);
	}

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B2.
	// Old bool + out-param shape of TraceSystem::trace; see TraceSystem.h for
	// the contract. map is ignored: requires loadEntities() first, which is what
	// sets the map TraceSystem::trace dereferences.
	bool traceMove(const MapData& map, int x0, int y0, int x1, int y1,
	               Entity* skipEnt, int mask, int radius,
	               Entity** outEntity = nullptr, int* outFrac = nullptr);

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

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-F3.
	// Kill-XP bridges live on MonsterSystem now; player_ is kept here for
	// removeEntity's facingEntity clear (moves with EntityDb in P2-GF).
	void setXPSystems(Player* player, const Localization* loc, Hud* hud) {
		monsters.setXPSystems(player, loc, hud);
		player_ = player;
	}

	// Turn/script coordination fields (legacy Game members).
	int monstersTurn = 0;
	bool queueAdvanceTurn = false;
	bool skipAdvanceTurn = false;
	bool skipDialog = false;        // set by DialogSystem::closeDialog(skip) around the thread resume
	bool abortMove = false;
	int spawnParam = -1;            // -1 = use the map header spawn
	int eventFlags_[2] = { 0, 0 };

	bool facingDirty = false;       // canvas updateFacingEntity latch analog (src/Entity.cpp:527)

	Combat combat;                  // peer subsystem (ADR 0008)
	TraceSystem trace;              // peer subsystem (spec §P2-GA); wired in loadEntities
	DoorSystem doors;               // peer subsystem (spec §P2-GB); wired in loadEntities
	MonsterSystem monsters;         // peer subsystem (spec §P2-GC); wired in loadEntities
	SpriteLerps lerps;              // peer subsystem (spec §P2-GD); wired in loadEntities

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-F3.
	// Monster wake/turn/pain/death live in MonsterSystem now (see
	// MonsterSystem.h for the contracts); these keep the pre-P2-GC call
	// sites compiling.
	void activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4) {
		monsters.activate(e, runStaticFunc, rangeCheck, alertSound, b4);
	}
	void updateMonsters() { monsters.updateMonsters(); }
	void endMonstersTurn() { monsters.endMonstersTurn(); }
	bool painMonster(Entity* e, int dmg, int attackerWeaponId) {
		return monsters.painMonster(e, dmg, attackerWeaponId);
	}
	void diedMonster(Entity* e, bool giveXP) { monsters.diedMonster(e, giveXP); }
	void corpsifyMonster(Entity* e, int x, int y) { monsters.corpsifyMonster(e, x, y); }
	static bool isBossDef(const EntityDef* def) { return MonsterSystem::isBossDef(def); }

	// Difficulty source: ScriptVM vars[12], default 2 when no VM is wired
	// (spec §1 difficulty note).
	int difficulty() const;

	// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-B2.
	int entityDistFrom(const Entity* e, int x, int y) const { return trace.distFrom(e, x, y); }

private:
	// removeEntity's facingEntity clear (src/Game.cpp:192); set by
	// setXPSystems.
	Player* player_ = nullptr;

	std::vector<Entity> entities_;
	Entity* entityDb_[1024] = { nullptr }; // 32x32 tile lists
	MapData* map_ = nullptr;
	const EntityDefs* defs_ = nullptr;     // set in loadEntities
	ScriptVM* vm_ = nullptr;

	// FORWARDER scratch (spec §3.1): pair view rebuilt by lastTraceHits().
	mutable std::vector<std::pair<int, Entity*>> legacyTraceHits_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_GAME_H