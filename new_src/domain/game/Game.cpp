#include "domain/game/Game.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/ScriptVM.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "ui/Hud.h"

namespace newcore {

// Monster subtypes of the default-lootset table (src/Enums.h:71-79; the
// constants are not ported into new_src Enums yet).
enum {
	kMonZombie = 0,
	kMonPinky = 5,
	kMonCacodemon = 6,
	kMonMancubus = 8,
	kMonRevenant = 9,
	kMonSentryBot = 11,
	kBossMastermind = 14,
};

// Port of Entity::populateDefaultLootSet (src/Entity.cpp:1997-2048). The
// default (imp etc.) branch fills a per-map joke string via
// findRandomJokeItem (:2050+); that flavor table is not ported, so those
// corpses stay empty (class-6 lines would never grant anyway).
static void populateDefaultLootSet(Entity& e) {
	e.hasLootSet = true;
	e.lootSet[0] = e.lootSet[1] = e.lootSet[2] = 0;
	if (e.def->eType == Enums::ET_CORPSE) {
		if (e.def->eSubType != kMonSentryBot) e.lootSet[0] = 1089; // Health Pack x1 (INV_HEALTH_PACK=17)
		return;
	}
	int s = e.getSprite();
	switch (e.def->eSubType) {
	case kMonZombie:    e.lootSet[0] = 0x600 | (s % 3 + 1); break;
	case kMonCacodemon: e.lootSet[0] = 0x2100 | (s % 5 + 3); break;
	case kMonMancubus:  e.lootSet[0] = 0x2140 | (s % 3 + 1); break;
	case kMonRevenant:  e.lootSet[0] = 0x2140 | (s % 3 + 3); break;
	case kMonSentryBot: e.lootSet[0] = 0x2040 | (s % 6 + 6); break;
	default: break;
	}
}


// ---- entityDb (32x32 tile lists) ----

Entity* Game::findMapEntity(int x, int y) {
	if (x < 0 || y < 0 || x >= 32 || y >= 32) return nullptr;
	return entityDb_[y * 32 + x];
}

void Game::linkEntity(Entity* e, int tx, int ty) {
	if (tx < 0 || ty < 0 || tx >= 32 || ty >= 32) return;
	unlinkEntity(e);
	int idx = ty * 32 + tx;
	e->nextOnTile = entityDb_[idx];
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e;
	e->prevOnTile = nullptr;
	entityDb_[idx] = e;
	e->linkIndex = (short)idx;
	e->info |= Entity::kInfoLinked;
}

void Game::unlinkEntity(Entity* e) {
	if (!(e->info & Entity::kInfoLinked)) return;
	if (e->prevOnTile) e->prevOnTile->nextOnTile = e->nextOnTile;
	else {
		int idx = e->linkIndex;
		if (idx >= 0 && idx < 1024 && entityDb_[idx] == e)
			entityDb_[idx] = e->nextOnTile;
	}
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e->prevOnTile;
	e->nextOnTile = e->prevOnTile = nullptr;
	e->info &= ~Entity::kInfoLinked;
}

// ---- Level load ----

void Game::loadEntities(MapData& map, const EntityDefs& defs) {
	map_ = &map;
	defs_ = &defs;
	trace.init({ &entities_, entityDb_, &map });   // peer subsystem wiring (spec §P2-GA)
	doors.init({ entityDb_, &map, vm_, &trace });  // peer subsystem wiring (spec §P2-GB)
	monsters.init({ entityDb_, &map, &defs, vm_, &combat, &trace, &monstersTurn,
	                &facingDirty, &lerpClock_,
	                [this](int sprite) { snapSpriteLerps(sprite); } });  // spec §P2-GC
	entities_.clear();
	entities_.resize(kEntities);
	monstersTurn = 0;
	queueAdvanceTurn = false;
	for (auto& ls : spriteLerps_) ls.hSprite = 0;

	// Load-time AUTO_ANIMATE injection (src/Game.cpp:374-397): raw maps carry
	// no animation bits — OBJ_FIRE 130 / TORCHIERE 136 / ANIM_FIRE 234 get
	// 0x80000 plus the frame count in bits 8-15. Counts are the mediaMappings
	// ranges, verified against tmp_newMappings.bin: 130 -> 726..730 = 4,
	// 136 -> 736..737 = 1; ANIM_FIRE 234 is forced to 4 (src/Game.cpp:380-382).
	for (int i = 0; i < map.numSprites; ++i) {
		const int info = map.mapSpriteInfo[i];
		int tileNum = info & 0xFF;
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
		int tileNum = info & 0xFF;
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
	// traceMove blocking).
	int nextSlot = 2; // entities[0]=world, entities[1]=player (reserved)
	for (int i = 0; i < map.numSprites; ++i) {
		if (nextSlot >= kEntities) break;
		int info = map.mapSpriteInfo[i];
		if (info & 0x10000) continue; // hidden
		if (info & Enums::SPRITE_FLAG_NOENTITY) continue; // no-entity sprites never spawn entities (src/Game.cpp:398-400)
		int tileNum = info & 0xFF;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		const EntityDef* def = (tileNum >= 0 && tileNum < 512) ? defs.lookup(tileNum) : nullptr;
		if (!def) continue;
		if (def->eType == Enums::ET_DOOR) {
			if (tileNum < Enums::TILENUM_FIRST_DOOR || tileNum > Enums::TILENUM_LAST_DOOR) continue;
		} else if (def->eType != Enums::ET_MONSTER && def->eType != Enums::ET_CORPSE &&
		           def->eType != Enums::ET_NPC) {
			continue;
		}

		Entity& e = entities_[nextSlot++];
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
			if ((std::rand() & 1) == 0 && !isBossDef(def)) { // :438-441 (nextByte analog)
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
			if (diff == 4 || (diff == 2 && !isBossDef(def))) {
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
			populateDefaultLootSet(e);                       // :109-111
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
			else populateDefaultLootSet(e);
		}
		int x = map.mapSprites[i + 0 * map.numSprites];
		int y = map.mapSprites[i + 1 * map.numSprites];
		linkEntity(&e, x >> 6, y >> 6);
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

// FORWARDER bodies (spec 2026-08-26-decomposition §3.1) — the trace itself
// lives in TraceSystem now; these keep the pre-P2-GA call sites compiling.
bool Game::traceMove(const MapData& map, int x0, int y0, int x1, int y1,
                     Entity* skipEnt, int mask, int radius,
                     Entity** outEntity, int* outFrac) {
	(void)map;                                 // TraceSystem holds the same MapData
	const TraceHit hit = trace.trace(x0, y0, x1, y1, skipEnt, mask, radius);
	if (outEntity) *outEntity = hit.entity;
	if (outFrac)   *outFrac   = hit.frac;
	return !hit.blocks();                      // clear -> commit allowed
}

const std::vector<std::pair<int, Entity*>>& Game::lastTraceHits() const {
	legacyTraceHits_.clear();
	for (const TraceHit& h : trace.hits()) legacyTraceHits_.push_back({ h.frac, h.entity });
	return legacyTraceHits_;
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
	int tn = info & 0xFF;
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

Entity* Game::findEntityBySprite(int sprite) {
	for (Entity& e : entities_) {
		if (e.def != nullptr && e.getSprite() == sprite) return &e;
	}
	return nullptr;
}

// Port of Game::removeEntity (src/Game.cpp:183-193); see Game.h.
void Game::removeEntity(Entity* e) {
	if (!e || !map_) return;
	int s = e->getSprite();
	if ((e->info & 0xFFFF) != 0 && s >= 0 && s < map_->numSprites) {   // :186-188
		map_->mapSpriteInfo[s] |= 0x10000;
	}
	if ((e->info & Entity::kInfoLinked) != 0) {                        // :189-191
		unlinkEntity(e);
	}
	if (player_ != nullptr) player_->facingEntity = nullptr;           // :192
}

// See Game.h. Adjacent-tile stand-in for the legacy one-tile trace distance
// (tileDistances[0] = 4096 = distFrom squared across one tile).
Entity* Game::findLootableCorpseFacing(int px, int py, int stepX, int stepY) {
	int tx = (px + stepX) >> 6;
	int ty = (py + stepY) >> 6;
	for (Entity* e = findMapEntity(tx, ty); e != nullptr; e = e->nextOnTile) {
		if (!e->isCorpse()) continue;
		if (!(e->info & Entity::kInfoLinked)) continue;   // unlinked = not traceable
		// Looted gate: prop corpses count prior loots in param
		// (src/PlayingInputHandler.cpp:324-330); the monster flag 0x800 is
		// unified into param here (EntityMonster not ported).
		if (e->param != 0) continue;
		if (!e->hasLootSet) continue;                     // (:325/:331)
		return e;
	}
	return nullptr;
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

static std::string itemLongName(const EntityDefs& defs, const Localization& loc,
                                int cls, int idx) {
	const EntityDef* d = defs.find(Enums::ET_ITEM, cls, idx); // src/LootingSystem.cpp:240
	if (d == nullptr) return {};
	return Localization::titleOf(loc.get(kTextIngame, d->longName));
}

// Pooling half of legacy LootingSystem::poolLoot (src/LoothingSystem.cpp:
// 154-278); see Game.h. Marks every eType-9 entity on the tile looted BEFORE
// reading its loot set, merges entries into `out`, then composes one
// '|'-separated display buffer + the <start,len> line table.
void Game::poolLootCorpse(int tx, int ty, const Localization& loc, LootPool& out) {
	// Reset first (:158-162): a stale buffer would corrupt the line table.
	out.numEntries = 0;
	out.numItems = 0;
	out.credits = 0;
	out.topLine = 0;
	out.text.setLength(0);

	for (Entity* e = findMapEntity(tx, ty); e != nullptr; e = e->nextOnTile) {
		if (!e->isCorpse()) continue;                    // eType == 9 only (:164)
		if (e->param != 0) continue;                     // prop already looted (:166-169)
		// Monster corpses carry a separate flag 0x800 in legacy (:172-177);
		// EntityMonster is not ported, so both markers unify into ++param
		// (spec Deviations #1).
		++e->param;
		e->info |= Entity::kInfoActivated;               // (:179)

		if (!e->hasLootSet) continue;                    // lootSet == nullptr analog
		for (int i = 0; i < Entity::kMaxCorpseLoot; ++i) {
			int entry = e->lootSet[i];
			if (entry == 0) break;                       // stop at first zero slot (:181)
			bool push = true;
			int cls = entry >> 12 & 0xF;
			if (cls == 6) {
				int n2 = entry & 0xFFF;
				for (int j = 0; j < out.numEntries; ++j) {
					// Verbatim legacy quirk: the class bit is tested on the
					// SOURCE entity's lootSet[j] where lootPool[j] was meant
					// (src/LoothingSystem.cpp:186-194; loot-inventory.md §2.4).
					// j >= kMaxCorpseLoot would read past lootSet[] (legacy
					// read adjacent memory) — treated as no match.
					if (j < Entity::kMaxCorpseLoot &&
					    ((e->lootSet[j] >> 12) & 0xF) == 6 &&
					    n2 == (out.entries[j] & 0xFFF)) {
						push = false;
						break;
					}
				}
			} else {
				int cnt = entry & 0x3F;
				++out.numItems;                          // stat counts pre-merge (:197)
				int idx = (entry & 0xFC0) >> 6;
				if (cls == 0 && idx == 24) { out.credits += cnt; continue; }      // (:200-207)
				if (cls == 0 && idx == 25) { out.credits += cnt * 100; continue; }
				int key = entry >> 6;
				for (int k = 0; k < out.numEntries; ++k) { // dupes merge, saturated (:209-216)
					if (key == (out.entries[k] >> 6)) {
						push = false;
						out.entries[k] = (out.entries[k] & 0xFFFFFFC0) |
						                 ((cnt + (out.entries[k] & 0x3F)) & 0x3F);
						break;
					}
				}
			}
			// Legacy had no bound here (its lootPool[9] is larger); entries
			// beyond kMaxCorpseLoot are dropped.
			if (push && out.numEntries < Entity::kMaxCorpseLoot) {
				out.entries[out.numEntries++] = entry;
			}
		}
	}

	// Compose lines into one buffer (:226-261). Format strings end in '|'.
	for (int l = 0; l < out.numEntries; ++l) {
		int entry = out.entries[l];
		int cls = entry >> 12 & 0xF;
		if (cls == 6) {                                  // flavor: raw map string (:229-234)
			out.text.append('\x88');
			out.text.append(loc.get(kTextMap, entry & 0xFFF));
			out.text.append("|");
			continue;
		}
		int idx = (entry & 0xFC0) >> 6;
		int cnt = entry & 0x3F;
		std::string name = itemLongName(defs_ ? *defs_ : EntityDefs(), loc, cls, idx);
		std::string line = loc.get(kTextMain, cls == 1 ? 91 : 90);
		if (cls == 1) {                                  // "%01%02|" (:241-243)
			std::string args[2] = { "\x88", name };
			composeArgs(line, args, 2);
		} else {                                         // "%01%02x %03|" (:245-249)
			std::string args[3] = { "\x88", std::to_string(cnt), name };
			composeArgs(line, args, 3);
		}
		out.text.append(line);
	}
	if (out.credits != 0) {                              // "<icon> N x UAC Credits" (:252-258)
		std::string line = loc.get(kTextMain, 90);
		std::string args[3] = { "\x88", std::to_string(out.credits),
		                        Localization::titleOf(loc.get(kTextIngame, 157)) };
		composeArgs(line, args, 3);
		out.text.append(line);
	}
	if (out.numEntries == 0 && out.credits == 0) {
		out.text.append(loc.get(kTextMain, 228));        // "None found!" (:259-261)
	}

	// Dehyphenate BEFORE recording offsets (legacy order :262-278), then
	// split at '|' into <start,len> pairs; the last pair covers the tail.
	out.text.dehyphenate();
	for (short& v : out.lineIndex) v = 0;
	int length = out.text.length();
	int start = 0;
	int slot = 0;
	for (int i = 0; i < length; ++i) {
		if (out.text.charAt(i) == '|') {
			if (slot < LootPool::kMaxLines) {
				out.lineIndex[slot * 2] = (short)start;
				out.lineIndex[slot * 2 + 1] = (short)(i - start);
			}
			++slot;
			start = i + 1;
		}
	}
	if (slot < LootPool::kMaxLines) {
		out.lineIndex[slot * 2] = (short)start;
		out.lineIndex[slot * 2 + 1] = (short)(length - start);
	}

	std::fprintf(stderr, "[loot] pooled tile=%d,%d entries=%d items=%d credits=%d\n",
		tx, ty, out.numEntries, out.numItems, out.credits);
}

// Grant half of legacy LootingSystem::giveLootPool (src/LoothingSystem.cpp:
// 281-307); see Game.h.
void Game::giveLootPool(LootPool& pool, Player& player, const Tables* tables) {
	for (int i = 0; i < pool.numEntries; ++i) {
		int entry = pool.entries[i];
		int cls = entry >> 12 & 0xF;
		if (cls == 6) continue;                          // display-only flavor (:287)
		int idx = (entry & 0xFC0) >> 6;
		int cnt = entry & 0x3F;
		player.give(cls, idx, cnt);                      // (:289)
		std::fprintf(stderr, "[loot] give class=%d idx=%d cnt=%d\n", cls, idx, cnt);
		if (cls == 1 && tables != nullptr &&
		    (size_t)(idx * 9 + 5) < tables->weaponData.size()) {
			int ammoType = tables->weaponData[idx * 9 + 4];   // AMMOTYPE (src/Combat.h:26-36)
			int usage = tables->weaponData[idx * 9 + 5];      // AMMOUSAGE
			if (usage > 0) player.give(2, ammoType, std::max(usage, 10)); // (:290-296)
		}
	}
	if (pool.credits != 0) {
		player.give(0, 24, pool.credits);                // (:299-302)
		std::fprintf(stderr, "[loot] credits=%d\n", pool.credits);
	}
	std::fprintf(stderr, "[loot] foundLoot items=%d\n", pool.numItems); // run-stat stub (:303)
	pool.numEntries = 0;                                 // counters reset + dispose analog
	pool.numItems = 0;
	pool.credits = 0;
	pool.text.setLength(0);                              // (:304-306)
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

// ---- Script sprite lerps (docs/original-code/lerp-opcodes.md) ----

// Exact integer square root (bit-pair method); the legacy FixedSqrt
// approximation is irrelevant here — the phase quantizes at >>12.
static int64_t isqrt64(int64_t v) {
	if (v <= 0) return 0;
	int64_t res = 0;
	int64_t bit = 1LL << 62;
	while (bit > v) bit >>= 2;
	while (bit != 0) {
		if (v >= res + bit) {
			v -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return res;
}

void Game::SpriteLerp::calcDist() {
	int64_t dx = dstX - srcX;
	int64_t dy = dstY - srcY;
	// Legacy dist = sqrt(S) exactly: FixedSqrt shifts v<<=8 internally
	// (src/Game.cpp:3636-3641), so <<16 here cancels its >>8 result shift.
	dist = (int)(isqrt64(((int64_t)dx * dx + (int64_t)dy * dy) << 16) >> 8);
}

// src/Game.cpp:3596-3628 with b=true: thresholds ±32, result <<7.
int Game::vecToDir(int dx, int dy) {
	int dir = -1;
	if (dx <= -32) dir = 4;
	else if (dx >= 32) dir = 0;
	if (dy >= 32) {
		if (dir == 4) dir = 5;
		else if (dir == 0) dir = 7;
		else dir = 6;
	} else if (dy <= -32) {
		if (dir == 4) dir = 3;
		else if (dir == 0) dir = 1;
		else dir = 2;
	}
	return dir << 7;
}

// TEMP [dbg] lerp audit extension (spec 2026-08-26 §3, remove with the
// other TEMP logs): prints whenever an audited sprite's anim bits 8-15
// change — covers the walk writer, the alloc-reuse reset and the
// completion restore.
static void dbgLerpAnimAudit(int sprite, int info) {
	static const int kDbgSprites[] = {7, 9, 10, 135, 150};
	static int lastAnimByte[5] = {-1, -1, -1, -1, -1};
	for (int di = 0; di < 5; ++di) {
		if (sprite != kDbgSprites[di]) continue;
		const int a = (info >> 8) & 0xFF;
		if (a != lastAnimByte[di]) {
			if (lastAnimByte[di] >= 0)
				std::fprintf(stderr, "[dbg] anim spr=%d animByte 0x%02X -> 0x%02X\n",
					sprite, lastAnimByte[di], a);
			lastAnimByte[di] = a;
		}
		break;
	}
}

// Pool lookup (src/Game.cpp:3028-3066): a still-active lerp for the same
// sprite reuses its slot, otherwise the first free slot is taken. Legacy
// also resets monster anim state on reuse — no EntityMonster here.
Game::SpriteLerp* Game::allocLerpSprite(ScriptThread* thread, int sprite, bool block) {
	SpriteLerp* reuse = nullptr;
	SpriteLerp* freeSlot = nullptr;
	for (SpriteLerp& ls : spriteLerps_) {
		if (ls.hSprite == sprite + 1) { reuse = &ls; break; }   // hSprite stores sprite+1 (:3030)
		if (freeSlot == nullptr && ls.hSprite == 0) freeSlot = &ls;
	}
	SpriteLerp* ls = (reuse != nullptr) ? reuse : freeSlot;
	if (ls == nullptr) {
		// Legacy fatals with Error(36) ERR_MAX_LERPSPRITES
		// (src/Enums.h:1050); bring-up logs and reports failure instead.
		std::fprintf(stderr, "[lerp] ERR_MAX_LERPSPRITES (36): pool exhausted\n");
		return nullptr;
	}
	int flags = 0;
	if (reuse == nullptr) {
		*ls = SpriteLerp{};
		ls->hSprite = sprite + 1;
	} else {
		// Alloc-side idle reset of a stale pose (src/Game.cpp:3049-3063):
		// monsters ONLY (entity->monster != nullptr, src/Game.cpp:3054) —
		// stamping IDLE onto a reused ACTIVE NPC walker's slot caused the
		// mid-walk flicker (RF candidate 2). ls->flags still holds the OLD
		// slot flags here — the async/block bits are OR'd in below, matching
		// legacy read order.
		Entity* ent = findEntityBySprite(sprite);
		if (ent != nullptr && ent->def != nullptr &&
		    ent->def->eType == Enums::ET_MONSTER) {
			int n3 = (map_->mapSpriteInfo[sprite] >> 8) & 0xF0;
			n3 = (n3 == Enums::MANIM_WALK_FRONT || (ls->flags & SpriteLerp::kFlagAutoFace))
			         ? Enums::MANIM_IDLE
			     : (n3 == Enums::MANIM_WALK_BACK) ? Enums::MANIM_IDLE_BACK
			                                      : n3;
			map_->mapSpriteInfo[sprite] =
				(map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | (n3 << 8);
			dbgLerpAnimAudit(sprite, map_->mapSpriteInfo[sprite]);
		}
	}
	if (thread == nullptr) ls->flags |= SpriteLerp::kFlagAsync; // (:3068-3070)
	if (block) flags |= SpriteLerp::kFlagAnimatingEffect;       // (:3071-3074);
	                                                            // the opcode tail overwrites flags again
	ls->flags |= flags;
	ls->ownerThread = thread;
	return ls;
}

// Terrain-height bias between raw stored S_Z and the legacy baked space
// (getHeight == heightMap << 3 with 11-bit coord masks, src/Render.cpp:2444-2451;
// z-sprite -32 nudge from postProcessSprites, :2465-2467).
int Game::spriteZBias(int sprite, int x, int y) const {
	if (!map_ || map_->heightMap.empty()) return 0;
	int h = map_->heightMap[((y & 0x7FF) >> 6) * 32 + ((x & 0x7FF) >> 6)] << 3;
	if (sprite >= map_->numNormalSprites) h -= 32;
	return h;
}

// Single tick of updateLerpSprite (src/Game.cpp:2855-2956), script-lerp
// subset: relink keeps linked entities on their tile; S_NORELINK/
// ENT_NORELINK never apply to script lerps and staleView culling,
// walk-frame anim + boss footsteps have no rewrite counterpart yet.
int Game::updateLerpSprite(SpriteLerp* ls) {
	if (!map_ || ls->hSprite == 0) return 4;
	const int n = map_->numSprites;
	const int sprite = ls->hSprite - 1;

	int elapsed = lerpClock_ - ls->startTime;
	if (elapsed >= ls->travelTime) {                       // completion (:2868-2870)
		freeLerpSprite(ls);
		return 3;
	}
	if (elapsed < 0) return 4;

	int p = 0;
	if (ls->travelTime != 0) p = (elapsed << 16) / (ls->travelTime << 8);   // :2876-2878

	int x = ls->srcX + (p * ((ls->dstX - ls->srcX) << 8) >> 16);            // :2879-2882
	int y = ls->srcY + (p * ((ls->dstY - ls->srcY) << 8) >> 16);
	int scale = ls->srcScale + (p * ((ls->dstScale - ls->srcScale) << 8) >> 16);
	int z = ls->srcZ + (p * ((ls->dstZ - ls->srcZ) << 8) >> 16);
	if ((ls->flags & SpriteLerp::kFlagParabola) != 0 && sinTable_ != nullptr) {
		z += (((*sinTable_)[(p << 1) & 0x3FF] >> 8) * (ls->height << 8)) >> 16;   // :2883-2893
	}

	map_->mapSprites[sprite + 0 * n] = (int16_t)x;
	map_->mapSprites[sprite + 1 * n] = (int16_t)y;
	map_->mapSprites[sprite + 8 * n] = (int16_t)scale;
	// z interpolates in legacy baked space; storage is raw-relative, so
	// strip the terrain bias of the tile currently written to.
	int zStored = z - spriteZBias(sprite, x, y);
	map_->mapSprites[sprite + 2 * n] = (int16_t)zStored;

	// TEMP [dbg] lerp audit (remove after user confirms visually)
	{
		static const int dbgSprites[] = {7, 9, 10, 135, 150};
		for (int di = 0; di < 5; ++di) {
			if (sprite != dbgSprites[di]) continue;
			static int lastBucket[5] = {-1, -1, -1, -1, -1};
			int bucket = elapsed / 100;
			if (bucket != lastBucket[di]) {
				lastBucket[di] = bucket;
				std::fprintf(stderr, "[dbg] tick spr=%d t=%d/%dms p=%d XYZ=%d,%d,%d (bakedZ=%d)\n",
					sprite, elapsed, ls->travelTime, p, x, y, zStored, z);
			}
			break;
		}
	}

	// Walk-state writer (src/Game.cpp:2903-2944): distance-driven anim byte
	// for NPC/monster sprites. The monster half stays dormant until the
	// EntityMonster unit enables stacked monster rendering (ADR 0005).
	Entity* ent = findEntityBySprite(sprite);
	if (ent != nullptr && ent->def != nullptr &&
	    !(map_->mapSpriteInfo[sprite] & Enums::SPRITE_FLAG_HIDDEN) &&
	    (ent->def->eType == Enums::ET_NPC || ent->def->eType == Enums::ET_MONSTER)) {
		int info = map_->mapSpriteInfo[sprite];
		int anim = ((info & 0xFF00) >> 8) & Enums::MANIM_MASK;
		int dx = ls->dstX - ls->srcX;
		int dy = ls->dstY - ls->srcY;

		if ((anim == Enums::MANIM_IDLE || anim == Enums::MANIM_WALK_FRONT ||
		     (anim == Enums::MANIM_WALK_BACK && (ls->flags & SpriteLerp::kFlagAutoFace) != 0)) &&
		    (dx | dy) != 0) {
			// NO angle-wrap normalization — verbatim legacy (:2910).
			int delta = std::abs((lerpViewAngle_ & 0x3FF) - vecToDir(dx, dy));
			// tileDistances[1] = (64*2)^2 = 16384: ">1 tile" Chebyshev test
			// (src/Combat.cpp:42, src/Game.cpp:2911).
			if (std::max(dx * dx, dy * dy) >= 16384 && delta < 256) {
				anim = Enums::MANIM_WALK_BACK;
				ls->flags |= SpriteLerp::kFlagAutoFace;    // :2912-2913
			} else {
				anim = Enums::MANIM_WALK_FRONT;            // :2916
			}
		} else if (anim == Enums::MANIM_IDLE_BACK) {
			anim = Enums::MANIM_WALK_BACK;                 // :2919-2921
		}

		if (anim == Enums::MANIM_WALK_FRONT || anim == Enums::MANIM_WALK_BACK) {
			// Boss footstep-sound tail (:2925-2941) omitted.
			int phase = (1 + ((p * ls->dist) >> 12)) & 3;  // 1 cycle per tile
			map_->mapSpriteInfo[sprite] =
				((info & 0xFFFF00FF) | ((phase | anim) << 8));   // :2943
			dbgLerpAnimAudit(sprite, map_->mapSpriteInfo[sprite]);
		}
	}

	// Per-tick relinkSprite analog: keep a LINKED entity on its tile.
	if (ent != nullptr && (ent->info & Entity::kInfoLinked) != 0) {
		int tx = x >> 6, ty = y >> 6;
		if ((ent->linkIndex % 32) != tx || (ent->linkIndex / 32) != ty) {
			linkEntity(ent, tx, ty);
		}
	}
	return 0;
}

// Completion snap (freeLerpSprite head, src/Game.cpp:3078-3119, subset):
// dst X/Y/(Z)/scale written back, entity relinked to the dst tile, slot
// freed. Door/secret/chicken tails are not ported (door lerps use DoorAnim).
void Game::freeLerpSprite(SpriteLerp* ls) {
	const int sprite = ls->hSprite - 1;
	if (map_ && sprite >= 0) {
		const int n = map_->numSprites;
		map_->mapSprites[sprite + 0 * n] = (int16_t)ls->dstX;
		map_->mapSprites[sprite + 1 * n] = (int16_t)ls->dstY;
		map_->mapSprites[sprite + 2 * n] =
			(int16_t)(ls->dstZ - spriteZBias(sprite, ls->dstX, ls->dstY));
		map_->mapSprites[sprite + 8 * n] = (int16_t)ls->dstScale;
		Entity* ent = findEntityBySprite(sprite);
		if (ent != nullptr && (ent->info & Entity::kInfoLinked) != 0 &&
		    ((ent->linkIndex % 32) != (ls->dstX >> 6) || (ent->linkIndex / 32) != (ls->dstY >> 6))) {
			linkEntity(ent, ls->dstX >> 6, ls->dstY >> 6);
		}
		// Completion idle restore (src/Game.cpp:3101-3111): back-facing walks
		// without AUTO_FACE park in IDLE_BACK, everything else in IDLE.
		if (ent != nullptr && ent->def != nullptr &&
		    !(map_->mapSpriteInfo[sprite] & Enums::SPRITE_FLAG_HIDDEN) &&
		    (ent->def->eType == Enums::ET_NPC || ent->def->eType == Enums::ET_MONSTER)) {
			int n5 = (map_->mapSpriteInfo[sprite] >> 8) & 0xF0;
			int restore = (n5 == Enums::MANIM_IDLE_BACK || n5 == Enums::MANIM_WALK_BACK) &&
			                  !(ls->flags & SpriteLerp::kFlagAutoFace)
			              ? 0x1000
			              : 0x0000;
			map_->mapSpriteInfo[sprite] =
				(map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | restore;
			dbgLerpAnimAudit(sprite, map_->mapSpriteInfo[sprite]);
		}
	}
	ls->hSprite = 0;
	ls->ownerThread = nullptr;
}

// snapLerpSprites (src/Game.cpp:1149-1166): zero the timings and run one tick
// so the completion snap + slot free happen immediately. Owner-thread resume
// omitted (the callers run mid-dispatch of some thread and re-entrant run() is
// unsafe in the rewrite VM).
void Game::snapSpriteLerps(int sprite) {
	for (SpriteLerp& ls : spriteLerps_) {
		if (ls.hSprite != sprite + 1) continue;
		ls.startTime = 0;                          // (:1159-1160)
		ls.travelTime = 0;
		updateLerpSprite(&ls);                     // zero travel -> completion snap + free
	}
}

void Game::update(int dtMs) {
	lerpClock_ += dtMs;
	// updateLerpSprites sweep (src/Game.cpp:2985-3021): completed blocking
	// lerps resume their owner AFTER the loop (legacy callThreads[] flush).
	ScriptThread* done[kMaxLerpSprites];
	int numDone = 0;
	for (SpriteLerp& ls : spriteLerps_) {
		if (ls.hSprite == 0) continue;
		ScriptThread* owner = ls.ownerThread;
		int flags = ls.flags;
		int r = updateLerpSprite(&ls);
		if ((r & 1) != 0 && owner != nullptr && (flags & SpriteLerp::kFlagAsync) == 0 &&
		    numDone < kMaxLerpSprites) {
			done[numDone++] = owner;
		}
	}
	for (int i = 0; i < numDone; ++i) {
		if (vm_ != nullptr) vm_->resumeThread(done[i]);
	}
	doors.update(dtMs);

	// Pain/dodge pose auto-revert (legacy render-side src/Render.cpp:1600-1604,
	// moved into the simulation per spec deviation D-6): anim bytes 96/144
	// fall back to IDLE once the monster's frameTime hold expired. Same
	// guards as the walk writer: hidden sprites skipped, knockback-flagged
	// monsters keep their pose (:1600).
	if (map_ == nullptr) return;
	for (Entity& ent : entities_) {
		if (ent.monster == nullptr || ent.def == nullptr) continue;
		const int s = ent.getSprite();
		if (s < 0 || s >= map_->numSprites) continue;
		const int info = map_->mapSpriteInfo[s];
		if ((info & Enums::SPRITE_FLAG_HIDDEN) != 0) continue;
		const int anim = (info >> 8) & Enums::MANIM_MASK;
		if ((anim == Enums::MANIM_PAIN || anim == Enums::MANIM_DODGE) &&
		    (ent.monster->flags & Enums::MFLAG_KNOCKBACK) == 0 &&
		    lerpClock_ > ent.monster->frameTime) {
			map_->mapSpriteInfo[s] = info & 0xFFFF00FF;   // back to IDLE (:1601-1602)
			ent.monster->frameTime = 0;
		}
	}
}

} // namespace newcore