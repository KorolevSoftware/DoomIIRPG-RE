#include "domain/game/Game.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapBits.h"

namespace newcore {

// Monster subtypes of the spawn-scale table (src/Enums.h:71-79; the
// constants are not ported into new_src Enums yet). The default-lootset
// subtypes moved with populateDefaultLootSet (spec §P2-GE).
enum {
	kMonPinky = 5,
	kBossMastermind = 14,
};

// ---- Level load ----

void Game::loadEntities(MapData& map, const EntityDefs& defs) {
	map_ = &map;
	defs_ = &defs;
	db.init({ &map });                             // peer subsystem wiring (spec §P2-GF)
	trace.init({ &db, &map });                     // peer subsystem wiring (spec §P2-GA)
	doors.init({ &db, &map, vm_, &trace });        // peer subsystem wiring (spec §P2-GB)
	monsters.init({ &db, &map, &defs, vm_, &combat, &trace, &monstersTurn,
	                &facingDirty, &lerps });                            // spec §P2-GC
	lerps.init({ &db, &map, vm_ });                     // peer subsystem wiring (spec §P2-GD)
	loot.init({ &db, &defs });                          // peer subsystem wiring (spec §P2-GE)
	db.resetEntities();
	monstersTurn = 0;
	queueAdvanceTurn = false;

	// Load-time AUTO_ANIMATE injection (src/Game.cpp:374-397): raw maps carry
	// no animation bits — OBJ_FIRE 130 / TORCHIERE 136 / ANIM_FIRE 234 get
	// 0x80000 plus the frame count in bits 8-15. Counts are the mediaMappings
	// ranges, verified against tmp_newMappings.bin: 130 -> 726..730 = 4,
	// 136 -> 736..737 = 1; ANIM_FIRE 234 is forced to 4 (src/Game.cpp:380-382).
	for (int i = 0; i < map.numSprites; ++i) {
		const int info = map.mapSpriteInfo[i];
		int tileNum = info & SpriteInfo::kTileNumMask;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		int frameCount;
		if (tileNum == 130 || tileNum == 234) frameCount = 4;
		else if (tileNum == 136)              frameCount = 1;
		else continue;
		map.mapSpriteInfo[i] = (info & 0xFFFF00FF) | (frameCount << 8) | 0x80000;
	}

	// Per-sprite render mode (src/Render.cpp:2459-2495 postProcessSprites):
	// effective tileNum (+257 for wall-tile sprites) selects the blend mode,
	// written to S_RENDERMODE; everything else stays RENDER_NORMAL.
	for (int i = 0; i < map.numSprites; ++i) {
		const int info = map.mapSpriteInfo[i];
		int tileNum = info & SpriteInfo::kTileNumMask;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		int mode = 0;
		if (tileNum == 208 || tileNum == 234 || tileNum == 130 ||
		    tileNum == 242 || tileNum == 178 || tileNum == 236) mode = 3; // RENDER_ADD
		else if (tileNum == 212) mode = 7;                                // RENDER_SUB
		else if (tileNum == 161) mode = 2;                                // BLEND50
		else if (tileNum == 244) mode = 4;                                // ADD75
		map.mapSprites[i + 3 * map.numSprites] = (int16_t)mode;
	}

	// Create entities from TILE-flagged sprites whose tileNum+257 resolves to
	// a door (271-278), plus monster/NPC/corpse sprites so script loot
	// opcodes, the pickup path and the stacked-character renderer have
	// targets (legacy loadMapEntities gives every sprite an entity,
	// src/Game.cpp:374-452; the rewrite limits itself to the families that
	// participate in gameplay this phase — items/decor would change
	// TraceSystem::trace blocking).
	int nextSlot = 2; // entities[0]=world, entities[1]=player (reserved)
	for (int i = 0; i < map.numSprites; ++i) {
		if (nextSlot >= EntityDb::kEntities) break;
		int info = map.mapSpriteInfo[i];
		if (info & 0x10000) continue; // hidden
		if (info & Enums::SPRITE_FLAG_NOENTITY) continue; // no-entity sprites never spawn entities (src/Game.cpp:398-400)
		int tileNum = info & SpriteInfo::kTileNumMask;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		const EntityDef* def = (tileNum >= 0 && tileNum < 512) ? defs.lookup(tileNum) : nullptr;
		if (!def) continue;
		if (def->eType == Enums::ET_DOOR) {
			if (tileNum < Enums::TILENUM_FIRST_DOOR || tileNum > Enums::TILENUM_LAST_DOOR) continue;
		} else if (def->eType != Enums::ET_MONSTER && def->eType != Enums::ET_CORPSE &&
		           def->eType != Enums::ET_NPC) {
			continue;
		}

		Entity& e = db.entities()[nextSlot++];
		e.def = def;
		e.setSprite(i);
		if (def->eType == Enums::ET_DOOR) {
			e.info |= Entity::kInfoActive;
		} else if (def->eType == Enums::ET_MONSTER) {
			// Monster half (src/Game.cpp:430-447 + src/Entity.cpp:59-81):
			// payload from the fixed pool, random art flip, shared-stat clone
			// with the difficulty hp bump, z/scale snap, active marker.
			e.monster = monsters.allocMonster();             // :435 (pool + Error 37)
			if (e.monster == nullptr) {
				--nextSlot;   // give the slot back and skip this sprite
				continue;
			}
			e.monster->reset();                              // :436
			if ((std::rand() & 1) == 0 && !MonsterSystem::isBossDef(def)) { // :438-441 (nextByte analog)
				map.mapSpriteInfo[i] |= Enums::SPRITE_FLAG_FLIP_HORIZONTAL;
			}
			const int tmplIdx = def->eSubType * 3 + (int8_t)def->parm;  // src/Entity.cpp:60
			if (tmplIdx >= 0 && tmplIdx < (int)combat.monsterTemplates.size()) {
				// NOTE direction: rewrite clone(&other) copies FROM the arg
				// into this entity (legacy template.clone(dest) is reversed).
				e.monster->ce.clone(combat.monsterTemplates[tmplIdx]);
			} else {
				std::fprintf(stderr, "[monster] template %d missing (sub=%d parm=%d)\n",
					tmplIdx, def->eSubType, def->parm);
			}
			const int diff = difficulty();                   // :62-67 (+25% hp)
			if (diff == 4 || (diff == 2 && !MonsterSystem::isBossDef(def))) {
				const int stat = e.monster->ce.getStat(1);
				const int n2 = stat + (stat >> 2);
				e.monster->ce.setStat(1, n2);
				e.monster->ce.setStat(0, n2);
			}
			// z snap: stored S_Z is raw-relative in this rewrite (renderer
			// re-adds terrain), so write the bare +32 offset — legacy bakes
			// getHeight+32 (src/Entity.cpp:70, corpsify note Game.cpp:637-640).
			map.mapSprites[i + 2 * map.numSprites] = 32;
			int scale = 64;                                  // :68-75
			if ((def->eSubType == kBossMastermind || def->eSubType == kMonPinky) &&
			    def->parm == 0) scale = 42;
			map.mapSprites[i + 8 * map.numSprites] = (int16_t)scale;
			e.info |= Entity::kInfoActive;                   // :77 (0x20000)
			CorpseLoot::populateDefaultLootSet(e);           // :109-111
		} else {
			// Placed corpse props spawn with info |= 0x420000
			// (src/Entity.cpp:96-98); generalized to every monster/corpse.
			e.info |= Entity::kInfoActive | Entity::kInfoActivated;
			// ET_NPC construction sets param = 1 -> chat-icon overhead in
			// legacy (src/Entity.cpp:99-101); the icon itself is not rendered
			// this cycle (spec 2026-08-25-character-animation §1 elision).
			// Loot sets exist only for monsters/corpses
			// (src/Entity.cpp:103-111).
			if (def->eType == Enums::ET_NPC) e.param = 1;
			else CorpseLoot::populateDefaultLootSet(e);
		}
		int x = map.mapSprites[i + 0 * map.numSprites];
		int y = map.mapSprites[i + 1 * map.numSprites];
		db.linkEntity(&e, x >> 6, y >> 6);
		if (def->eType == Enums::ET_MONSTER) {
			// :441 - legacy sets 0x40000 so spawn deactivate() links the monster onto the inactive ring (src/Game.cpp:441-443)
			e.info |= Entity::kInfoOnActiveList;
			monsters.deactivate(&e); // every monster starts on the inactive ring
			std::fprintf(stderr,
				"[monster] spawn sprite=%d sub=%d parm=%d hp=%d/%d\n",
				i, def->eSubType, def->parm,
				e.monster ? e.monster->ce.getStat(0) : -1,
				e.monster ? e.monster->ce.getStat(1) : -1);
			continue;                // monster log replaces the generic one below
		}
		fprintf(stderr, "%s entity sprite=%d tile=%d (%d,%d) sub=%d loot=[%X %X %X]\n",
			def->eType == Enums::ET_DOOR ? "DOOR" :
			def->eType == Enums::ET_NPC ? "NPC" : "BODY",
			i, tileNum, x >> 6, y >> 6, def->eSubType,
			e.lootSet[0], e.lootSet[1], e.lootSet[2]);
	}
}

// ---- Phase 5: script-facing services ----

// Faithful port (src/Game.cpp:2477-2498): flip bit0 of the sprite-info
// tileNum byte (271<->272, 273<->274, ...) and re-look-up the def by
// tileNum+257. The renderer resolves textures from the same low byte, so
// the locked/unlocked texture swap is automatic.
void Game::setLineLocked(Entity* e, bool locked) {
	if (!e || !map_ || !defs_) return;
	int sprite = e->getSprite();
	if (sprite < 0 || sprite >= map_->numSprites) return;
	int info = map_->mapSpriteInfo[sprite];
	int tn = info & SpriteInfo::kTileNumMask;
	tn = locked ? (tn & 0xFFFFFFFE) : (tn | 0x1);
	map_->mapSpriteInfo[sprite] = (info & 0xFFFFFF00) | tn;
	e->def = defs_->lookup(tn + 257);
	std::fprintf(stderr, "[script] setLineLocked sprite=%d -> %s\n", sprite, locked ? "locked" : "unlocked");
}

// Turn advance (src/Game.cpp:1238-1281, subset). player->advanceTurn stats /
// updateBombs / updateMonsterFX / startRotation pitch refresh are
// placeholders until those systems exist.
void Game::advanceTurn() {
	queueAdvanceTurn = false;                  // (:1240)
	if (monsters.interpolatingMonsters) {      // (:1241-1243) Error 95 guard:
		// nothing sets interpolatingMonsters in Stage 1 (no lerps), so this
		// is defensive only.
		std::fprintf(stderr, "[turn] ERR_NONSNAPPEDMONSTERS (95)\n");
		monsters.snapMonsters(true);
	}
	// haste-parity block (:1244-1262): statusEffects[2] absent ->
	// monstersTurn = 1 always. Player-side ticks (poison/infection/combat
	// decay) deferred with citation src/Player.cpp:51-92.
	monstersTurn = 1;                          // arm the monster phase (:1257-1264); Playing tick step disarms
	facingDirty = true;                        // updateFacingEntity latch (src/Game.cpp:1269; no haste -> b always true)
	doors.advanceTurnDoors();                  // auto-close sweep (:1271-1278)
	if (vm_) vm_->executeStaticFunc(Enums::SCR_PER_TURN); // PER_TURN hook (:1279)
}

// ---- Monsters / combat (spec 2026-08-26-combat-stage1 §0.B, §3.2) ----

int Game::difficulty() const {
	return vm_ != nullptr ? vm_->vars[12] : 2;
}

// %NN arg substitution, decode rules of Localization composeText
// (src/Text.cpp:281-326; DialogSystem.cpp:85-115 in the rewrite). Declared
// in Game.h — Combat.cpp reuses it for the combat message feed.
void composeArgs(std::string& text, const std::string* args, int numArgs) {
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == '%' && i + 2 < text.size() &&
		    text[i + 1] >= '0' && text[i + 1] <= '9' &&
		    text[i + 2] >= '0' && text[i + 2] <= '9') {
			int a = (text[i + 1] - '0') * 10 + (text[i + 2] - '0') - 1; // first arg is %01
			i += 2;
			if (a >= 0 && a < numArgs) out += args[a];
		} else {
			out += c;
		}
	}
	text.swap(out);
}

void Game::touchTile(int x, int y, bool b) {
	// Legacy touchTile drives automap uncover + pickups (absent this phase).
	(void)x; (void)y; (void)b;
}

// src/Game.cpp:974-981.
void Game::eventFlagsForMovement(int x0, int y0, int x1, int y1) {
	int dx = x1 - x0;
	int dy = y1 - y0;
	eventFlags_[0] = 2 | eventFlagForDirection(dx, dy);   // LEAVE mask for source tile
	eventFlags_[1] = 1 | eventFlagForDirection(-dx, -dy); // ENTER mask for destination tile
}

// src/Game.cpp:983-1014 (8-way table; screen Y grows downward).
int Game::eventFlagForDirection(int dx, int dy) {
	if (dx > 0) {
		if (dy < 0) return Enums::EVFL_MOD_NORTHEAST;
		if (dy > 0) return Enums::EVFL_MOD_SOUTHEAST;
		return Enums::EVFL_MOD_EAST;
	}
	if (dx < 0) {
		if (dy < 0) return Enums::EVFL_MOD_NORTHWEST;
		if (dy > 0) return Enums::EVFL_MOD_SOUTHWEST;
		return Enums::EVFL_MOD_WEST;
	}
	return (dy > 0) ? Enums::EVFL_MOD_SOUTH : Enums::EVFL_MOD_NORTH;
}

void Game::update(int dtMs) {
	lerps.update(dtMs);
	doors.update(dtMs);

	// Pain/dodge pose auto-revert (legacy render-side src/Render.cpp:1600-1604,
	// moved into the simulation per spec deviation D-6): anim bytes 96/144
	// fall back to IDLE once the monster's frameTime hold expired. Same
	// guards as the walk writer: hidden sprites skipped, knockback-flagged
	// monsters keep their pose (:1600).
	if (map_ == nullptr) return;
	for (Entity& ent : db.entities()) {
		if (ent.monster == nullptr || ent.def == nullptr) continue;
		const int s = ent.getSprite();
		if (s < 0 || s >= map_->numSprites) continue;
		const int info = map_->mapSpriteInfo[s];
		if ((info & Enums::SPRITE_FLAG_HIDDEN) != 0) continue;
		const int anim = (info >> 8) & Enums::MANIM_MASK;
		if ((anim == Enums::MANIM_PAIN || anim == Enums::MANIM_DODGE) &&
		    (ent.monster->flags & Enums::MFLAG_KNOCKBACK) == 0 &&
		    lerps.clockMs() > ent.monster->frameTime) {
			map_->mapSpriteInfo[s] = info & 0xFFFF00FF;   // back to IDLE (:1601-1602)
			ent.monster->frameTime = 0;
		}
	}
}

} // namespace newcore