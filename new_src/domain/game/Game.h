#ifndef NEW_DOMAIN_GAME_GAME_H
#define NEW_DOMAIN_GAME_GAME_H

#include <cstdint>
#include <string>

#include "domain/game/Combat.h"
#include "domain/game/CorpseLoot.h"
#include "domain/game/DoorSystem.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityDb.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/MonsterSystem.h"
#include "domain/game/Player.h"
#include "domain/game/SpriteLerps.h"
#include "domain/game/TraceSystem.h"
#include "io/EntityDefs.h"
#include "domain/world/MapData.h"

namespace newcore {

class EntityDefs;
class Hud;
class Localization;
class ScriptVM;

// %NN argument substitution over Localization strings (decode rules of
// Text::composeText, src/Text.cpp:281-326). Defined in Game.cpp; shared with
// Combat.cpp / Player messaging so the pattern has exactly one copy.
void composeArgs(std::string& text, const std::string* args, int numArgs);

// World simulation: entity database (32x32 tiles), door open/close, item
// pickup. Minimal modern port of the legacy Game focused on the interactive
// subset (doors + items). Monsters/combat come later.
class Game {
public:
	Game() = default;

	// Builds door/item entities from the map's sprite table. Must be called
	// once per level load, after MapData is parsed.
	void loadEntities(MapData& map, const EntityDefs& defs);

	// Tick called every frame (dtMs). Advances door animations and the
	// script sprite lerps (LERP* opcodes).
	void update(int dtMs);

	// ---- Phase 5 additions ----

	// Locked<->unlocked def swap: flips bit0 of the sprite-info tileNum and
	// re-looks-up the EntityDef by tileNum+257 (src/Game.cpp:2477-2498).
	// Entity::name sync omitted — Entity::name not ported.
	void setLineLocked(Entity* e, bool locked);

	// Turn advance, subset of src/Game.cpp:1238-1281 (stats/bombs/monster
	// bookkeeping are placeholders until those systems exist).
	void advanceTurn();

	// ---- Corpse looting (docs/original-code/loot-inventory.md) ----

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

	// Cross-subsystem wiring, not a forwarder: kill-XP bridges live on
	// MonsterSystem, and the same Player feeds EntityDb::removeEntity's
	// facingEntity clear (spec §P2-GF). Game owns both peers, so it owns the
	// pairing.
	void setXPSystems(Player* player, const Localization* loc, Hud* hud) {
		monsters.setXPSystems(player, loc, hud);
		db.setPlayer(player);
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
	EntityDb db;                    // peer subsystem (spec §P2-GF); wired in loadEntities
	TraceSystem trace;              // peer subsystem (spec §P2-GA); wired in loadEntities
	DoorSystem doors;               // peer subsystem (spec §P2-GB); wired in loadEntities
	MonsterSystem monsters;         // peer subsystem (spec §P2-GC); wired in loadEntities
	SpriteLerps lerps;              // peer subsystem (spec §P2-GD); wired in loadEntities
	CorpseLoot loot;                // peer subsystem (spec §P2-GE); wired in loadEntities

	// Difficulty source: ScriptVM vars[12], default 2 when no VM is wired
	// (spec §1 difficulty note).
	int difficulty() const;

private:
	MapData* map_ = nullptr;
	const EntityDefs* defs_ = nullptr;     // set in loadEntities
	ScriptVM* vm_ = nullptr;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_GAME_H