#ifndef NEW_DOMAIN_GAME_GAME_H
#define NEW_DOMAIN_GAME_GAME_H

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>

#include "domain/game/Combat.h"
#include "domain/game/CorpseLoot.h"
#include "domain/game/DoorSystem.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityDb.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/GameSprites.h"
#include "domain/game/ItemPickup.h"
#include "domain/game/MonsterAI.h"
#include "domain/game/MonsterSystem.h"
#include "domain/game/Player.h"
#include "domain/game/SpriteLerps.h"
#include "domain/game/TraceSystem.h"
#include "domain/game/ViewVisibility.h"
#include "domain/game/ZoomState.h"
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
	Game() {
		std::fill(std::begin(entityDeathFunctions), std::end(entityDeathFunctions), int16_t(-1));   // src/Game.cpp:50-52
		std::fill(std::begin(mapKeepPitchLevelTiles), std::end(mapKeepPitchLevelTiles), int16_t(-1));
	}

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

	// Crate opening (src/PlayingInputHandler.cpp:387-393): arm the 4-frame
	// sprite animation and unlink immediately, so the crate stops blocking
	// and stops being a trace/facing target BEFORE the animation plays.
	void openCrate(Entity* e);

	// ---- Corpse looting (docs/original-code/loot-inventory.md) ----

	// Arrival tile hook (src/Game.cpp:687-699): walks the destination tile's
	// entity chain and offers every entity to ItemPickup::touched. x/y are
	// canvas units, like the legacy call site (src/MovementController.cpp:170).
	void touchTile(int x, int y, bool b);

	// Run stat (src/Game.cpp:3536-3543): the x/y/z overload only forwards,
	// the counter is the whole body.
	void foundLoot(int sprite, int amount) { (void)sprite; lootFound += (short)amount; }

	// Trigger masks for the movement events (src/Game.cpp:974-1014):
	// eventFlags_[0] = LEAVE mask for the source tile,
	// eventFlags_[1] = ENTER mask for the destination tile.
	void eventFlagsForMovement(int x0, int y0, int x1, int y1);
	static int eventFlagForDirection(int dx, int dy);

	// ScriptVM back-pointer for advanceTurn's PER_TURN hook; forward-declared
	// here, included only in the .cpp (no header cycle). Set after construction.
	void setVM(ScriptVM* vm) { vm_ = vm; }

	// ---- Damage / death dispatch (ADR 0018) ----

	// Twins of the legacy entity methods, which switch on eType
	// (src/Entity.cpp:280-393 / :424-521). Game is the only module that
	// reaches every arm's owner, so the switch lives here; the monster arms
	// stay in MonsterSystem.
	bool entityPain(Entity* e, int damage);
	void entityDied(Entity* e, bool giveXP);

	// ET_ATTACK_INTERACTIVE arms (src/Entity.cpp:371-391 / :437-447). Props
	// carry no HP: any landed hit destroys them outright.
	bool painProp(Entity* e);

	// Entity death functions (src/Game.cpp:3495-3534): 64 {sprite, ip} pairs,
	// free = -1; a bound entity carries info bit 0x2000000.
	void addEntityDeathFunc(Entity* e, int ip);
	void removeEntityFunc(Entity* e);
	void executeEntityFunc(Entity* e);
	int16_t entityDeathFunctions[128];
	bool useCaldexHack = false;     // render->useCaldexHack (src/ScriptThread.cpp:2021-2025)
	bool useMastermindHack = false; // render->useMastermindHack (:1728-1732); renderer side not ported
	bool angryVIOS = false;         // EV_ANGER_VIOS (:1821-1825)
	// EV_PORTAL_EVENT state (:1855-1868); consumer src/Render.cpp:2713-2761 not ported.
	int portalState = 0;            // Render::PORTAL_DNE
	int previousPortalState = 0;
	bool portalScripted = false;
	// EV_PITCH_CONTROL table (:1870-1899), x | y<<5 | dir<<10, free = -1. No
	// reader yet: MovementController::pitchIsControlled needs the slope pitch.
	int16_t mapKeepPitchLevelTiles[20];
	void diedProp(Entity* e);

	// Toilet/sink -> water spout (src/ArmorRepairSystem.cpp:56-63, forwarded
	// from src/Canvas.cpp:1615). Public because the future "pull it off the
	// wall" action (src/PlayingInputHandler.cpp:405-431) shares it.
	void turnEntityIntoWaterSpout(Entity* e);

	// ---- Monsters / combat (spec 2026-08-26-combat-stage1 §0.B, §3.2) ----

	// Cross-subsystem wiring, not a forwarder: kill-XP bridges live on
	// MonsterSystem, and the same Player feeds EntityDb::removeEntity's
	// facingEntity clear (spec §P2-GF). Game owns both peers, so it owns the
	// pairing.
	void setXPSystems(Player* player, const Localization* loc, Hud* hud) {
		player_ = player;
		loc_ = loc;
		hud_ = hud;
		monsters.setXPSystems(player, loc, hud);
		db.setPlayer(player);
	}

	// Player::relink (src/Player.cpp:1425-1445): unlink entities[1] if linked, then link it at
	// (destX>>6, destY>>6) when 0 <= destX,destY <= 2047; mirror the destination into the trace.
	void relinkPlayer(const Player& p);

	// Turn/script coordination fields (legacy Game members).
	int monstersTurn = 0;
	bool queueAdvanceTurn = false;
	bool skipAdvanceTurn = false;
	bool skipDialog = false;        // set by DialogSystem::closeDialog(skip) around the thread resume
	bool abortMove = false;
	int spawnParam = -1;            // -1 = use the map header spawn
	int loadMapID = 0;              // canvas->loadMapID, 1..10 (src/LoadingManager.cpp:67-68); 0 until the boot loadMap
	int totalLevelTime = 0;         // ms on this map before curLevelTime (src/Game.cpp:1585-1587)
	int64_t curLevelTime = 0;       // gameTime at load / last fold (src/LoadingManager.cpp:663)
	// snapAllMovers (src/Game.cpp:2500-2505): force-complete every mover before a map change.
	void snapAllMovers();
	// Player::fillMonsterStats (src/Player.cpp:107-126): total = entities with a
	// monster payload and !(flags & 0x80 NOTRACK); killed = those with info & 0x1010000.
	void fillMonsterStats(int& killed, int& total) const;
	// Player::levelGrade (src/Player.cpp:2022-2041); commit = legacy b.
	int levelGrade(bool commit);
	int eventFlags_[2] = { 0, 0 };

	bool facingDirty = false;       // canvas updateFacingEntity latch analog (src/Entity.cpp:527)
	short lootFound = 0;            // run stat (src/Game.cpp:684, :3541-3543)
	// LootingSystem::lootSource / showingLoot (src/PlayingInputHandler.cpp:189-195,
	// src/ScriptThread.cpp:2122-2134). lootSource is the ingame-text string id
	// naming the container the loot came from, -1 = generic header.
	int lootSource = -1;
	bool showingLoot = false;       // a GIVELOOT dialog is on screen; cleared by DialogSystem::closeDialog (src/DialogSystem.cpp:526)
	int numDestroyableObj = 0;      // map-completion stat: props spawned (src/Game.cpp:448-450)
	int destroyedObj = 0;           // props destroyed so far (src/Game.cpp:3545-3547)
	uint8_t mapSecretsFound = 0;    // secrets found on this map (src/Game.h:130)

	// src/Game.cpp:3473-3491: "Found Secret!", chime (1103 push wall /
	// 1020 script), then +5 XP or, for the map's last secret the first
	// time, the achievement +10 XP and the foundSecretsLevels bit.
	void awardSecret(bool pushWall);

	Combat combat;                  // peer subsystem (ADR 0008)
	EntityDb db;                    // peer subsystem (spec §P2-GF); wired in loadEntities
	TraceSystem trace;              // peer subsystem (spec §P2-GA); wired in loadEntities
	DoorSystem doors;               // peer subsystem (spec §P2-GB); wired in loadEntities
	MonsterSystem monsters;         // peer subsystem (spec §P2-GC); wired in loadEntities
	MonsterAI ai;                   // peer subsystem (ADR 0028); wired in loadEntities
	SpriteLerps lerps;              // peer subsystem (spec §P2-GD); wired in loadEntities
	GameSprites sprites;            // runtime sprites (ADR 0030); wired in loadEntities, ticked by GameContext
	CorpseLoot loot;                // peer subsystem (spec §P2-GE); wired in loadEntities
	ItemPickup items;               // peer subsystem (spec 2026-08-29-world-item-pickup §G2); wired in Main
	ZoomState zoom;                 // sniper zoom data (ADR 0039)

	// Last rendered world frame (ADR 0029); written only by GameContext::render.
	ViewVisibility visibility;

	// Difficulty source: ScriptVM vars[12], default 2 when no VM is wired
	// (spec §1 difficulty note).
	int difficulty() const;

private:
	MapData* map_ = nullptr;
	const EntityDefs* defs_ = nullptr;     // set in loadEntities
	ScriptVM* vm_ = nullptr;
	Player* player_ = nullptr;             // set in setXPSystems
	const Localization* loc_ = nullptr;    // set in setXPSystems
	Hud* hud_ = nullptr;                   // set in setXPSystems

	void addXPMessage(int xp);
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_GAME_H