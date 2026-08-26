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
	entities_.clear();
	entities_.resize(kEntities);
	monstersTurn = 0;
	queueAdvanceTurn = false;
	numMonsters_ = 0;                       // pool lifetime = one map load (spec §0.B)
	activeMonsters = inactiveMonsters = nullptr;
	combatMonsters = nullptr;
	interpolatingMonsters = false;
	for (auto& a : doorAnims_) { a.active = false; a.door = nullptr; a.ownerThread = nullptr; }
	for (auto& d : openDoors_) d = nullptr;
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
			if (numMonsters_ >= kMaxMonsters) {
				// Legacy Error(37) ERR_MAX_MONSTERS (src/Game.cpp:431-434).
				std::fprintf(stderr, "[monster] ERR_MAX_MONSTERS (37): pool exhausted\n");
				--nextSlot;   // give the slot back and skip this sprite
				continue;
			}
			e.monster = &entityMonsters_[numMonsters_++];    // :435
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
			deactivate(&e);          // every monster starts on the inactive ring
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

// Legacy interact (see Game.h): LINKED doors on the player's tile and on the
// adjacent tile in the facing direction; own tile wins (ray fraction ~0).
// Unlinked (open) doors are not traceable — legacy traces walk entityDb,
// which holds only linked entities. Locked doors are refused without
// animating (src/PlayingInputHandler.cpp:447-449).
Game::DoorUseResult Game::useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY) {
	(void)map;
	const int tiles[2][2] = {
		{ px >> 6, py >> 6 },
		{ (px + stepX) >> 6, (py + stepY) >> 6 },
	};
	for (auto& t : tiles) {
		for (Entity* e = findMapEntity(t[0], t[1]); e; e = e->nextOnTile) {
			if (!e->isDoor()) continue;
			if (!(e->info & Entity::kInfoLinked)) continue;
			if (e->def->eSubType == Enums::DOOR_LOCKED) return DoorUseResult::Locked;
			performDoorEvent(0, e, 1);             // player use never snaps (src/PlayingInputHandler.cpp:451)
			return DoorUseResult::Opened;
		}
	}
	return DoorUseResult::None;
}

// Legacy b3: effective tileNum in [271,281) (src/Game.cpp:1058). Excludes
// 281 although TILENUM_LAST_DOOR == 281 (spec C6).
static bool doorFamilyTile(int tileNum) { return tileNum >= 271 && tileNum < 281; }

bool Game::performDoorEvent(int n, Entity* door, int n2, ScriptThread* ownerThread) {
	if (!door || !door->isDoor()) return false;
	if (door->def->eSubType == Enums::DOOR_LOCKED) {       // needs key
		return false;
	}

	int sprite = door->getSprite();
	if (sprite < 0 || !map_) return false;

	int info = map_->mapSpriteInfo[sprite];
	int tileNum = info & 0xFF;
	if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
	bool family = doorFamilyTile(tileNum); // b3

	bool linked = (door->info & Entity::kInfoLinked) != 0;
	std::fprintf(stderr,
		"[dbg] doorEvent spr=%d n=%d n2=%d owner=%d isDoor=%d type=%d sub=%d linked=%d family=%d\n",
		sprite, n, n2, ownerThread != nullptr, door->isDoor(),
		door->def ? door->def->eType : -1, door->def ? door->def->eSubType : -1,
		linked ? 1 : 0, family ? 1 : 0); // TEMP [dbg]
	if (n == 0 && !linked && family) {
		// Already fully open: just keep it registered for auto-close, no new
		// animation (src/Game.cpp:1062-1065).
		registerOpenDoor(door);
		return false;
	}
	if (n == 1 && linked) return false; // already closed (src/Game.cpp:1066-1068)

	// Legacy refuses to close when a monster stands on the door tile
	// (src/Game.cpp:1070-1075, R10) — no monster entities yet.

	// Closing a door-family door becomes SOLID AGAIN IMMEDIATELY at close
	// start (src/Game.cpp:1076-1081).
	if (n == 1 && !linked && family) {
		linkEntity(door, door->linkIndex % 32, door->linkIndex / 32);
	}

	// NOTE: legacy also snaps when n2 == 2 and the door midpoint is culled
	// offscreen (src/Game.cpp:1153-1155); cullBoundingBox is not ported, so
	// n2 == 2 animates like n2 == 1 (documented deviation).

	// Find an animation slot for THIS door (reuse its own if still animating,
	// otherwise an empty slot). Never steal a slot from another open door.
	DoorAnim* slot = nullptr;
	for (auto& a : doorAnims_) {
		if (a.door == door) { slot = &a; break; }
	}
	if (!slot) {
		for (auto& a : doorAnims_) {
			if (!a.active && !doorRegistered(a.door)) { slot = &a; break; }
		}
	}
	if (!slot) {
		std::fprintf(stderr, "[dbg] doorEvent spr=%d NO SLOT -> abort\n", sprite); // TEMP [dbg]
		return false;
	}
	int slotIdx = (int)(slot - doorAnims_); // TEMP [dbg]

	int sx = map_->mapSprites[sprite + 0 * map_->numSprites]; // S_X (canvas units)
	int sy = map_->mapSprites[sprite + 1 * map_->numSprites]; // S_Y
	int curScale = map_->mapSprites[sprite + 8 * map_->numSprites]; // S_SCALEFACTOR

	// Slide direction: N/S door (0x3000000) slides along X, E/W (0xC000000) along Y.
	int slide = 32;
	if (n == 1) slide = -slide;
	int dstX = sx, dstY = sy;
	bool noSlide = door->def->parm & 0x1; // center door: scale only
	if (!noSlide) {
		if (info & 0x3000000) dstX += slide;      // N/S
		else if (info & 0xC000000) dstY += slide; // E/W
	}

	slot->active = true;
	slot->door = door;
	slot->sprite = sprite;
	slot->srcX = sx; slot->srcY = sy;
	slot->dstX = dstX; slot->dstY = dstY;
	slot->startScale = curScale; // always CURRENT S_SCALEFACTOR (src/Game.cpp:1095)
	slot->endScale = (n == 0) ? 0 : 64;
	slot->t = 0;
	slot->dur = 750;
	slot->opening = (n == 0);
	slot->ownerThread = ownerThread;

	// Door-lerp flag: set at EVERY animation start (open AND close), cleared
	// only at close completion in updateDoors (src/Game.cpp:1089,3125).
	map_->mapSpriteInfo[sprite] |= 0x80000000;

	// Register/keep open doors for auto-close; unregister when closing.
	if (n == 0) registerOpenDoor(door);
	else unregisterOpenDoor(door);

	// Texture frame 1 while open/animating (src/Game.cpp:1150-1152).
	if (n == 0 && family) {
		map_->mapSpriteInfo[sprite] = (map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x100;
	}

	// Snap modes (src/Game.cpp:1153-1155): n2 == 0 finishes the animation
	// immediately (quiet-bit EV_DOOROP / entity-state callers); the
	// ST_AUTOMAP force-snap has no counterpart (no automap state in the
	// subset). Finishing through updateDoors keeps every completion side
	// effect (open-end unlink, close-end texture restore, owner resume)
	// identical to a lerp that ran its full 750 ms, and openDoors_
	// registration above is untouched, so auto-close still works.
	if (n2 == 0) {
		slot->t = slot->dur;
		updateDoors();
	}
	std::fprintf(stderr, "[dbg] doorEvent spr=%d started slot=%d linked=%d\n",
		sprite, slotIdx, (door->info & Entity::kInfoLinked) != 0 ? 1 : 0); // TEMP [dbg]
	return true;
}

bool Game::doorRegistered(Entity* door) const {
	if (!door) return false;
	for (auto* d : openDoors_) if (d == door) return true;
	return false;
}

void Game::registerOpenDoor(Entity* door) {
	for (auto& d : openDoors_) {
		if (d == nullptr) { d = door; return; }
	}
}

void Game::unregisterOpenDoor(Entity* door) {
	for (auto& d : openDoors_) {
		if (d == door) d = nullptr;
	}
}

void Game::unlinkDoor(Entity* door) {
	if (map_ && door->info & Entity::kInfoLinked) {
		unlinkEntity(door);
	}
}

// Legacy CanCloseDoor (src/Game.cpp:1215-1236): tile-granular occupancy —
// the player or a monster on the door tile, or on either neighbor tile along
// the passage axis, blocks auto-close.
bool Game::canCloseDoor(Entity* door) {
	if (!door || !map_) return false;
	int link = door->linkIndex;
	int tx = link % 32, ty = link / 32;
	int cx = tx * 64 + 32, cy = ty * 64 + 32;

	auto occupied = [&](int x, int y) -> bool {
		// Player resolved tile-granularly (legacy compares destX/destY tiles,
		// src/Game.cpp:741-743; identical to viewX/viewY at advanceTurn times).
		if (playerX_ >= 0 && (playerX_ >> 6) == (x >> 6) && (playerY_ >> 6) == (y >> 6)) return true;
		for (Entity* e = findMapEntity(x >> 6, y >> 6); e; e = e->nextOnTile)
			if (e->def && e->def->eType == Enums::ET_MONSTER) return true; // mask 6 = player|monster (src/Game.cpp:1224)
		return false;
	};

	if (occupied(cx, cy)) return false;
	int info = map_->mapSpriteInfo[door->getSprite()];
	if (info & 0x3000000) {                       // horizontal-wall flags -> neighbors along Y
		if (occupied(cx, cy - 64)) return false;
		if (occupied(cx, cy + 64)) return false;  // src/Game.cpp:1229-1235 (n3 = 0)
	} else if (info & 0xC000000) {                // vertical-wall flags -> neighbors along X
		if (occupied(cx - 64, cy)) return false;
		if (occupied(cx + 64, cy)) return false;  // (n4 = 0)
	}
	return true;
}

// advanceTurn door closing (legacy Game.cpp:1271-1278): close any open door
// whose passage is now free. A door still opening is LINKED, so
// performDoorEvent(1, ...) hits its already-closed early-out exactly like
// legacy (src/Game.cpp:1066-1068) — no animating-skip needed.
void Game::advanceTurnDoors() {
	for (auto* door : openDoors_) {
		if (!door) continue;
		if (canCloseDoor(door)) {
			performDoorEvent(1, door, 2);      // snap-if-offscreen mode (src/Game.cpp:1275)
		}
	}
}

// Faithful port of Render::CapsuleToLineTrace (src/Render.cpp:1128-1210):
// clamped segment-segment closest-distance solve in integers. Returns the
// move-segment hit fraction ((sFrac>>2)-1, 14.14) or 16384 on miss.
static int capsuleToLineTrace(const int p[4], int radius2, const int q[4]) {
	const int d1x = p[2] - p[0];                       // :1129
	const int d1y = p[3] - p[1];                       // :1130
	const int d2x = q[2] - q[0];                       // :1131
	const int d2y = q[3] - q[1];                       // :1132
	const int rx  = p[0] - q[0];                       // :1133
	const int ry  = p[1] - q[1];                       // :1134
	const int a = d1x * d1x + d1y * d1y;               // dot  :1135 (|d1|^2)
	const int b = d1x * d2x + d1y * d2y;               // dot2 :1136 (d1.d2)
	const int e = d2x * d2x + d2y * d2y;               // dot3 :1137 (|d2|^2)
	const int c = d1x * rx  + d1y * ry;                // dot4 :1138 (d1.r)
	const int f = d2x * rx  + d2y * ry;                // dot5 :1139 (d2.r)
	// 64-bit intermediates from here on (legacy declares int64 :1140-1143;
	// fixes audit F-B: every cross product below MUST widen one operand).
	int64_t sNum, sDen, tNum, tDen;
	tDen = sDen = (int64_t)a * e - (int64_t)b * b;     // n8 = n9 = denom :1144
	if (sDen < 0) {                                    // :1144-1149 — DEAD in
		// exact int math (denom = a*e-b*b >= 0, Cauchy-Schwarz). Kept solely
		// for bit-faithfulness; do not "fix" it (spec Conflict-1).
		sNum = 0; sDen = 1; tNum = f; tDen = e;
	} else {
		sNum = (int64_t)b * f - (int64_t)e * c;        // :1151 (fixes F-A sign)
		tNum = (int64_t)a * f - (int64_t)b * c;        // :1152 (fixes F-A sign)
		if (sNum < 0) {                     // s clamped to 0 -> t := f/e
			sNum = 0; tNum = f; tDen = e;   // :1153-1157
		} else if (sNum > sDen) {           // s clamped to 1 -> t := (f+b)/e
			sNum = sDen; tNum = f + b; tDen = e; // :1158-1162
		}
	}
	if (tNum < 0) {                        // t clamped to 0, re-derive s :1164-1176
		tNum = 0;
		if (-c < 0)       sNum = 0;
		else if (-c > a)  sNum = sDen;
		else            { sNum = -c; sDen = a; }
	} else if (tNum > tDen) {              // t clamped to end, re-derive s :1177-1189
		tNum = tDen;
		if (b - c < 0)       sNum = 0;
		else if (b - c > a)  sNum = sDen;
		else                { sNum = b - c; sDen = a; }
	}
	const int sFrac = (sNum == 0) ? 0 : (int)((sNum << 16) / sDen); // :1191-1196
	const int tFrac = (tNum == 0) ? 0 : (int)((tNum << 16) / tDen); // :1197-1203
	// Closest vector v = r + s*d1 - t*d2 in 16.16 (result fits int32:
	// |term| <= 65536*2047 + 2047*65536 < 2^31; compute in int64 anyway).
	const int vx = (int)((((int64_t)rx << 16) + (int64_t)sFrac * d1x - (int64_t)tFrac * d2x) >> 16); // :1204
	const int vy = (int)((((int64_t)ry << 16) + (int64_t)sFrac * d1y - (int64_t)tFrac * d2y) >> 16); // :1205
	if ((int64_t)vx * vx + (int64_t)vy * vy < radius2) {   // STRICT < :1206
		return (sFrac >> 2) - 1;                           // 16.16 -> 14.14, -1 bias :1207
	}
	return 16384;                                          // miss sentinel :1209
}

// Faithful port of Render::CapsuleToCircleTrace (src/Render.cpp:1103-1126).
// Overlap test is d^2 < radius^2 + circleR2 (SUM OF SQUARES — 881 for the
// walking player vs normal entity: 256+625; NOT (r+R)^2=1681). circleR2 is
// the SQUARED entity-circle radius (625, or 256 for ET_ENV_DAMAGE).
static int capsuleToCircleTrace(const int p[4], int radius2, int cx, int cy, int circleR2) {
	const int dx = p[2] - p[0];                    // :1104
	const int dy = p[3] - p[1];                    // :1105
	const int fx = cx - p[0];                      // :1106
	const int fy = cy - p[1];                      // :1107
	const int dd = dx * dx + dy * dy;              // :1108
	if (dd == 0) return 0;                         // zero-length sweep: inside :1110-1112
	int t = dx * fx + dy * fy;                     // :1109
	if (t < 0)  t = 0;                             // :1113-1115
	if (t > dd) t = dd;                            // :1116-1118
	const int64_t t16 = ((int64_t)t << 16) / dd;   // 16.16 fraction :1119 (widened!)
	const int vx = cx - (p[0] + (int)((dx * t16) >> 16));  // :1120
	const int vy = cy - (p[1] + (int)((dy * t16) >> 16));  // :1121
	if (vx * vx + vy * vy < radius2 + circleR2) {  // :1122
		return (int)(t16 >> 2) - 1;                // :1123
	}
	return 16384;                                  // :1125
}

// entityDb broadphase pass of the move trace (src/Game.cpp:216-296): every
// linked candidate matching mask & (1<<eType), tested as an oriented-sprite
// wall segment or a circle, appending hits to traceHits_.
void Game::traceEntityHits(const MapData& map, Entity* skipEnt, int mask, int radius) {
	for (int i = traceBBox_[0] >> 6; i < (traceBBox_[2] >> 6) + 1; ++i) {        // :216
		for (int j = traceBBox_[1] >> 6; j < (traceBBox_[3] >> 6) + 1; ++j) {    // :217
			for (Entity* ent = entityDb_[i + 32 * j]; ent; ent = ent->nextOnTile) { // :218-220
				if (ent == skipEnt) continue;                                   // :221
				if (ent->def == nullptr || (mask & (1 << ent->def->eType)) == 0) continue; // :221
				if (ent->def->eType == Enums::ET_WORLD) continue;               // :222
				const int sprite = ent->getSprite();                            // :223
				int cx, cy;
				// Monster goal-lerp branch (src/Game.cpp:227-237): omitted —
				// EntityMonster does not exist yet; restore with monsters.
				if (ent->def->eType == Enums::ET_PLAYER) {                      // :239-243
					// Deviation (spec Conflict-2 note): legacy uses canvas DEST
					// coords; rewrite tracks view coords. Unreachable for
					// walking: the player is skipEnt and bit 1 ∉ 13501.
					cx = playerX_; cy = playerY_;
				} else {                                                        // :244-247
					cx = (sprite >= 0) ? map.mapSprites[sprite + 0 * map.numSprites] : 0; // S_X
					cy = (sprite >= 0) ? map.mapSprites[sprite + 1 * map.numSprites] : 0; // S_Y
					// sprite<0 fallback (0,0) avoids legacy's benign OOB read;
					// unreachable: loadEntities always sets a sprite.
				}
				if (sprite >= 0 && (map.mapSpriteInfo[sprite] & 0xF000000) != 0) {   // :249
					// Oriented sprite -> wall segment +/-32 through the CURRENT
					// (animated) sprite position. Axis: N/S bits (0x3000000)
					// => horizontal; ANYTHING ELSE => vertical (legacy tests
					// only 0x3000000 — do NOT add an 0xC000000 check).  (:250-263)
					int ex = cx, ey = cy;
					if (map.mapSpriteInfo[sprite] & 0x3000000) { cx -= 32; ex += 32; }
					else                                       { cy -= 32; ey += 32; }
					const int line[4] = { cx, cy, ex, ey };                     // :260-263
					const int frac = capsuleToLineTrace(tracePoints_, radius * radius, line); // :264
					if (frac < 16384) traceHits_.push_back({ frac, ent });      // :265-268
				} else {                                                        // :270-288
					const int circleR2 = (ent->def->eType == Enums::ET_ENV_DAMAGE) ? 256 : 625; // :271-274
					const int frac = capsuleToCircleTrace(tracePoints_, radius * radius, cx, cy, circleR2); // :275
					// Z filter (src/Game.cpp:277-283) omitted: walking always
					// uses the z-less 7-arg trace (zCheck=false, :195-197).
					if (frac < 16384) traceHits_.push_back({ frac, ent });      // :276-287
				}
			}
		}
	}
}

// World-line pass of the move trace (leaf body of Render::traceWorld,
// src/Render.cpp:1226-1267, walked flat over all lines — equivalence proof in
// spec §3.3). Returns the minimum hit fraction or 16384.
int Game::traceWorldFrac(const MapData& map, int mask, int radius2) {
	int minFrac = 16384;                                                        // :1226
	for (int i = 0; i < map.numLines; ++i) {
		// Packed nibble flags, LOW 3 BITS only (bit 3 = automap "seen"):      // :1232
		const int flag = (map.lineFlags[i >> 1] >> ((i & 1) << 2)) & 0xF & 0x7;
		int line[4];                                                            // byte coords << 3 :1233-1236
		line[0] = (map.lineXs[(i << 1) + 0] & 0xFF) << 3;
		line[2] = (map.lineXs[(i << 1) + 1] & 0xFF) << 3;
		line[1] = (map.lineYs[(i << 1) + 0] & 0xFF) << 3;
		line[3] = (map.lineYs[(i << 1) + 1] & 0xFF) << 3;
		if (flag == 4) continue;                                                // never blocks :1238
		if (flag == 6) continue;                                                // never blocks :1239-1241
		if (flag == 5 && (mask & 0x10) == 0 && (mask & 0x800) == 0) continue;   // PLAYERCLIP/MONSTERBLOCK_ITEM gates :1242-1244
		if (flag == 7) {  // ONE-SIDED: blocks only from the FRONT (cross > 0)  // :1245-1247
			// Verbatim legacy expression; trace START point is P0.
			if ((line[0] - tracePoints_[0]) * (line[3] - line[1]) +
			    (line[1] - tracePoints_[1]) * -(line[2] - line[0]) <= 0) continue;
		}
		// Cheap AABB rejects vs the trace bbox (strict comparisons):           // :1248-1258
		if (line[0] > traceBBox_[2] && line[2] > traceBBox_[2]) continue;
		if (line[0] < traceBBox_[0] && line[2] < traceBBox_[0]) continue;
		if (line[1] > traceBBox_[3] && line[3] > traceBBox_[3]) continue;
		if (line[1] < traceBBox_[1] && line[3] < traceBBox_[1]) continue;
		const int frac = capsuleToLineTrace(tracePoints_, radius2, line);       // :1260
		if (frac >= minFrac) continue;                                          // :1261-1263
		minFrac = frac;                                                         // :1264
	}
	return minFrac;
}

// Orchestrates one swept-capsule move trace (src/Game.cpp:199-327). See
// Game.h for the contract.
bool Game::traceMove(const MapData& map, int x0, int y0, int x1, int y1,
                     Entity* skipEnt, int mask, int radius,
                     Entity** outEntity, int* outFrac) {
	tracePoints_[0] = x0; tracePoints_[1] = y0;                                 // :208-211
	tracePoints_[2] = x1; tracePoints_[3] = y1;
	traceBBox_[0] = std::max(std::min(x0 - radius, x1 - radius), 0);            // :212-215
	traceBBox_[1] = std::max(std::min(y0 - radius, y1 - radius), 0);
	traceBBox_[2] = std::min(std::max(x0 + radius, x1 + radius), 2047);
	traceBBox_[3] = std::min(std::max(y0 + radius, y1 + radius), 2047);
	traceHits_.clear();
	traceEntityHits(map, skipEnt, mask, radius);                                // :216-296
	if (mask & 0x1) {                                                           // :297 ET_WORLD gate
		const int wf = traceWorldFrac(map, mask, radius * radius);              // :298
		if (wf < 16384) {
			traceHits_.push_back({ wf, &entities_[0] });                        // :299-301 (entities_[0] = world slot)
			traceCollisionX_ = x0 + ((wf * (x1 - x0)) >> 14);                   // :302-304 contact point
			traceCollisionY_ = y0 + ((wf * (y1 - y0)) >> 14);
		} else {
			traceCollisionX_ = x1;                                              // :306-308 a miss keeps the ray end
			traceCollisionY_ = y1;
		}
	}
	if (traceHits_.empty()) {                                                   // commit gate :332-333
		if (outEntity) *outEntity = nullptr;
		if (outFrac)   *outFrac = 16384;
		return true;                       // clear -> commit allowed
	}
	std::stable_sort(traceHits_.begin(), traceHits_.end(),                     // bubble sort asc :312-326
	                 [](const auto& l, const auto& r) { return l.first < r.first; });
	if (outEntity) *outEntity = traceHits_[0].second;
	if (outFrac)   *outFrac   = traceHits_[0].first;
	return false;                          // blocked
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
	if (interpolatingMonsters) {               // (:1241-1243) Error 95 guard:
		// nothing sets interpolatingMonsters in Stage 1 (no lerps), so this
		// is defensive only.
		std::fprintf(stderr, "[turn] ERR_NONSNAPPEDMONSTERS (95)\n");
		snapMonsters(true);
	}
	// haste-parity block (:1244-1262): statusEffects[2] absent ->
	// monstersTurn = 1 always. Player-side ticks (poison/infection/combat
	// decay) deferred with citation src/Player.cpp:51-92.
	monstersTurn = 1;                          // arm the monster phase (:1257-1264); Playing tick step disarms
	facingDirty = true;                        // updateFacingEntity latch (src/Game.cpp:1269; no haste -> b always true)
	advanceTurnDoors();                        // auto-close sweep (:1271-1278)
	if (vm_) vm_->executeStaticFunc(Enums::SCR_PER_TURN); // PER_TURN hook (:1279)
}

Entity* Game::findEntityBySprite(int sprite) {
	for (Entity& e : entities_) {
		if (e.def != nullptr && e.getSprite() == sprite) return &e;
	}
	return nullptr;
}

// Port of ScriptThread::corpsifyMonster (src/ScriptThread.cpp:2249-2266),
// see Game.h for the elided parts. Callers guarantee a monster-family
// entity (legacy requires entity->monster != nullptr, src/
// ScriptThread.cpp:1618-1620).
void Game::corpsifyMonster(Entity* e, int x, int y) {
	if (!e || !e->isMonster() || !map_ || !defs_) return;
	int s = e->getSprite();
	if (s < 0 || s >= map_->numSprites) return;
	int n = map_->numSprites;

	// snapLerpSprites(sprite) analog (src/ScriptThread.cpp:2250 -> src/
	// Game.cpp:1149-1166): force-complete any active lerp of this sprite so
	// its per-tick position writes + relink and its completion snap can no
	// longer fight the corpse placement below. Owner-thread resume omitted:
	// MAKE_CORPSE runs mid-dispatch of some thread and re-entrant run() is
	// unsafe in the rewrite VM.
	for (SpriteLerp& ls : spriteLerps_) {
		if (ls.hSprite != s + 1) continue;
		ls.startTime = 0;                          // (:1159-1160)
		ls.travelTime = 0;
		updateLerpSprite(&ls);                     // zero travel -> completion snap + free
	}

	// Visual death state: anim/frame overlay bits 8-14 = 0x7000, low byte
	// keeps the original art tileNum (src/ScriptThread.cpp:2253-2255).
	map_->mapSpriteInfo[s] = (map_->mapSpriteInfo[s] & 0xFFFE00FF) | 0x7000;

	// Position to the tile center; stored S_Z is raw-relative in this
	// rewrite (the renderer adds terrain per frame), so write the bare
	// +32 offset — legacy writes getHeight+32 into its terrain-baked
	// storage (src/ScriptThread.cpp:2256-2257).
	map_->mapSprites[s + 0 * n] = (int16_t)x;
	map_->mapSprites[s + 1 * n] = (int16_t)y;
	map_->mapSprites[s + 2 * n] = 32;

	// Corpse entity info: keep the sprite id, add corpse/inactive marker +
	// active visibility + activated (src/ScriptThread.cpp:2258-2259).
	e->info = (e->info & 0xFFFF) | Entity::kInfoCorpse | Entity::kInfoActive |
		Entity::kInfoActivated;

	// Def swap: same subtype/parm, now an ET_CORPSE def
	// (src/ScriptThread.cpp:2261-2263).
	const EntityDef* corpseDef =
		defs_->find(Enums::ET_CORPSE, e->def ? e->def->eSubType : 0,
		            e->def ? e->def->parm : -1);
	if (corpseDef != nullptr) e->def = corpseDef;

	// Relink at the new tile (:2264-2265). checkMonsterDeath sound omitted.
	linkEntity(e, x >> 6, y >> 6);
	// TEMP [dbg] corpsify audit (remove after user confirms): exactly ONE
	// solid blocker (this linked corpse) must remain on the tile.
	std::fprintf(stderr,
		"[dbg] corpsify spr=%d tile=%d,%d linked=%d corpse=%d anim=0x%X\n",
		s, x >> 6, y >> 6, (e->info & Entity::kInfoLinked) != 0 ? 1 : 0,
		(e->info & Entity::kInfoCorpse) != 0 ? 1 : 0,
		map_->mapSpriteInfo[s] & 0xFF00);
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
	if (xpPlayer_ != nullptr) xpPlayer_->facingEntity = nullptr;       // :192
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

void Game::setXPSystems(Player* player, const Localization* loc, Hud* hud) {
	xpPlayer_ = player;
	xpLoc_ = loc;
	xpHud_ = hud;
}

int Game::difficulty() const {
	return vm_ != nullptr ? vm_->vars[12] : 2;
}

bool Game::isBossDef(const EntityDef* def) {
	// src/Entity.cpp:1399 shape: eSubType within [FIRSTBOSS..LASTBOSS].
	return def != nullptr &&
	       def->eSubType >= Enums::FIRSTBOSS && def->eSubType <= Enums::LASTBOSS;
}

int Game::entityDistFrom(const Entity* e, int x, int y) const {
	// Chebyshev^2 distFrom (src/Entity.cpp:1155-1158); position read from
	// mapSprites exactly like traceEntityHits (S_X/S_Y, src/Game.cpp:244-247).
	if (e == nullptr || map_ == nullptr) return 0;
	if (e->def == nullptr || e->def->eType == Enums::ET_WORLD) {
		// calcPosition's ET_WORLD branch reads the last trace's collision
		// point (src/Entity.cpp:1375-1378); def == nullptr is the rewrite's
		// world slot.
		return std::max((x - traceCollisionX_) * (x - traceCollisionX_),
		                (y - traceCollisionY_) * (y - traceCollisionY_));
	}
	const int sprite = e->getSprite();
	const int ex = (sprite >= 0) ? map_->mapSprites[sprite + 0 * map_->numSprites] : 0;
	const int ey = (sprite >= 0) ? map_->mapSprites[sprite + 1 * map_->numSprites] : 0;
	return std::max((x - ex) * (x - ex), (y - ey) * (y - ey));
}

// Faithful port of Game::activate (src/Game.cpp:752-808). The render-side
// shotsFired latch lives on Combat now (same suppression window).
void Game::activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4) {
	(void)b4;                                  // legacy unused parameter
	if (e == nullptr || e->monster == nullptr || map_ == nullptr) return;
	EntityMonster* monster = e->monster;
	const int sprite = e->getSprite();
	if (((map_->mapSpriteInfo[sprite] & 0xFF00) >> 8 & 0xF0) == Enums::MANIM_IDLE_BACK &&
	    !combat.shotsFired) {
		return;                                // :760-762 back-turned wake guard
	}
	if (rangeCheck && entityDistFrom(e, playerX_, playerY_) > combat.tileDistances[3]) {
		return;                                // :763-765 (> tileDistances[3] = 4 tiles)
	}
	e->info |= Entity::kInfoActivated;         // :766
	// noclip early-out (:767-769): no noclip cheat in the rewrite.
	if ((e->info & Entity::kInfoOnActiveList) != 0) {
		return;                                // :770-772 already active
	}
	map_->mapSpriteInfo[sprite] &= 0xFFFF00FF; // :774 clear anim byte | 0x0
	if (monster->nextOnList != nullptr) {      // :775-786 unhook from inactive ring
		if (e == inactiveMonsters && monster->nextOnList == inactiveMonsters) {
			inactiveMonsters = nullptr;
		} else {
			if (e == inactiveMonsters) inactiveMonsters = monster->nextOnList;
			monster->nextOnList->monster->prevOnList = monster->prevOnList;
			monster->prevOnList->monster->nextOnList = monster->nextOnList;
		}
	}
	if (activeMonsters == nullptr) {           // :787-797 append to active ring
		monster->nextOnList = e;
		monster->prevOnList = e;
		activeMonsters = e;
	} else {
		monster->prevOnList = activeMonsters->monster->prevOnList;
		monster->nextOnList = activeMonsters;
		activeMonsters->monster->prevOnList->monster->nextOnList = e;
		activeMonsters->monster->prevOnList = e;
	}
	e->info |= Entity::kInfoOnActiveList;      // :798
	monster->flags &= ~Enums::MFLAG_NOACTIVATE;               // :799
	if (runStaticFunc && (monster->flags & Enums::MFLAG_TRIGGERONACTIVATE) != 0) {
		if (vm_) vm_->executeStaticFunc(Enums::SCR_MONSTER_ACTIVATE);   // :800-802
		monster->flags &= ~Enums::MFLAG_TRIGGERONACTIVATE;
	}
	if (alertSound) {                          // :804-807 MSOUND_ALERT1, no audio backend
		std::fprintf(stderr, "[monster] alert sound sub=%d parm=%d\n",
			e->def ? e->def->eSubType : -1, e->def ? e->def->parm : -1);
	}
	std::fprintf(stderr, "[monster] activate sprite=%d\n", sprite);
}

// Faithful port of Game::deactivate (src/Game.cpp:825-855).
void Game::deactivate(Entity* e) {
	if (e == nullptr || e->monster == nullptr) return;
	EntityMonster* monster = e->monster;
	if ((e->info & Entity::kInfoOnActiveList) == 0) {
		return;                                // :827-829 not on any ring we manage
	}
	if (monster->nextOnList != nullptr) {      // :830-841 unhook from active ring
		if (e == activeMonsters && monster->nextOnList == activeMonsters) {
			activeMonsters = nullptr;
		} else {
			if (e == activeMonsters) activeMonsters = monster->nextOnList;
			monster->nextOnList->monster->prevOnList = monster->prevOnList;
			monster->prevOnList->monster->nextOnList = monster->nextOnList;
		}
	}
	if (inactiveMonsters == nullptr) {         // :842-853 append to inactive ring
		monster->nextOnList = e;
		monster->prevOnList = e;
		inactiveMonsters = e;
	} else {
		monster->prevOnList = inactiveMonsters->monster->prevOnList;
		monster->nextOnList = inactiveMonsters;
		inactiveMonsters->monster->prevOnList->monster->nextOnList = e;
		inactiveMonsters->monster->prevOnList = e;
	}
	e->info &= ~Entity::kInfoOnActiveList;     // :854
}

// Stage-1 stub (spec §0.B): placed where legacy runs AI + lerps
// (src/Game.cpp:2458-2474); monsters never move or attack, so the window
// just closes.
void Game::updateMonsters() {
	if (monstersTurn != 0) endMonstersTurn();
}

// src/Game.cpp:2452-2456. canvas->startRotation(true) has no rewrite
// counterpart (input gating is idle-based).
void Game::endMonstersTurn() {
	monstersTurn = 0;
}

// Stage-1 stub (spec §0.B): no lerps exist, so snapping degenerates to
// driving/closing the turn — the only externally visible part of
// src/Game.cpp:2411-2449.
void Game::snapMonsters(bool b) {
	(void)b;
	if (monstersTurn != 0) endMonstersTurn();
}

// Non-boss ET_MONSTER subset of Entity::pain (src/Entity.cpp:281-394).
bool Game::painMonster(Entity* e, int dmg, int attackerWeaponId) {
	if (e == nullptr || e->monster == nullptr || !e->isMonster() || map_ == nullptr) return false;
	EntityMonster* m = e->monster;
	const int sprite = e->getSprite();
	if (sprite < 0 || sprite >= map_->numSprites) return false;
	if (!(e->info & Entity::kInfoActive)) return false;        // :286-288
	// Boss phase hooks at 75/50/25% with staticFuncs 2/3/4 (:293-339):
	// deferred (no bosses on the map00 route).
	int n2 = m->ce.getStat(Enums::STAT_HEALTH) - dmg;          // :290-292
	if ((m->flags & Enums::MFLAG_NOKILL) != 0 && n2 <= 0) {    // :341-343
		n2 = 1;
	}
	m->ce.setStat(Enums::STAT_HEALTH, n2);                     // :344
	if (n2 > 0) {
		// MSOUND_PAIN (:347-348) logged — no audio backend.
		std::fprintf(stderr, "[monster] pain sound sub=%d parm=%d hp=%d\n",
			e->def->eSubType, e->def->parm, n2);
		map_->mapSpriteInfo[sprite] =
			(map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x6000;    // :350-353 MANIM_PAIN
		m->frameTime = lerpClock_ + 250;       // nowMs() = lerpClock_ (deviation D-6)
		if (attackerWeaponId != 2 /*holy water*/) m->resetGoal();   // :354-356
	} else {
		map_->mapSpriteInfo[sprite] =
			(map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x6000;    // :358-359 lethal hold pose
		m->frameTime = lerpClock_ + 450;       // :360-368 (250 + 200 lethal hold)
	}
	return false;                              // boss staticFunc return value; always false here
}

// ET_MONSTER subset of Entity::died (src/Entity.cpp:459-521).
void Game::diedMonster(Entity* e, bool giveXP) {
	if (e == nullptr || e->monster == nullptr || !e->isMonster() ||
	    map_ == nullptr || defs_ == nullptr) return;
	EntityMonster* m = e->monster;
	const int sprite = e->getSprite();
	if (sprite < 0 || sprite >= map_->numSprites) return;
	if (!(e->info & Entity::kInfoActive)) return;              // :431 guard
	e->info &= ~Entity::kInfoActive;                           // :434
	e->info |= Entity::kInfoActivated;                         // :460
	m->resetGoal();                                            // :461
	// Snap script lerps of this sprite (corpsifyMonster pattern,
	// src/Game.cpp:620-631) so a running lerp can't fight the death pose.
	for (SpriteLerp& ls : spriteLerps_) {
		if (ls.hSprite != sprite + 1) continue;
		ls.startTime = 0;
		ls.travelTime = 0;
		updateLerpSprite(&ls);
	}
	int info = map_->mapSpriteInfo[sprite];
	info = (info & 0xFFFF00FF) | 0x7000;                       // :463 death-frame overlay
	m->frameTime = lerpClock_;                                 // :464
	if ((map_->mapSpriteInfo[sprite] & 0x10000) != 0) {        // :465-471 hidden branch
		info |= 0x17000;
	} else {
		e->info |= Entity::kInfoCorpse | Entity::kInfoActive;  // :469 (0x1020000); trimCorpsePile skipped
	}
	map_->mapSpriteInfo[sprite] = info;
	// monsterEffects re-stamp (:472-484) and Lost Soul/Cacodemon poof
	// (:491-495): deferred (absent on the map00 route).
	deactivate(e);                                             // :485
	if (giveXP) awardKillXP(*m);                               // :496 (+ :407-413)
	const EntityDef* corpseDef =
		defs_->find(Enums::ET_CORPSE, e->def ? e->def->eSubType : 0,
		            e->def ? e->def->parm : -1);                    // :501 def swap
	if (corpseDef != nullptr) e->def = corpseDef;
	facingDirty = true;                        // :527 canvas updateFacingEntity analog
	std::fprintf(stderr, "[monster] died sprite=%d xpGiven=%d\n", sprite, giveXP ? 1 : 0);
}

// checkMonsterDeath(b=true) XP half (src/Entity.cpp:407-413) plus the msg-103
// composition split out of Player::addXP (spec deviation 14).
void Game::awardKillXP(const EntityMonster& m) {
	if (xpPlayer_ == nullptr) return;
	int xp = m.ce.calcXP();                    // :408
	// boss +130 (:409-411): unreachable while no boss is killable.
	if (xpLoc_ != nullptr && xpHud_ != nullptr) {
		std::string msg = xpLoc_->get(kTextMain, 103);
		std::string args[1] = { std::to_string(xp) };
		composeArgs(msg, args, 1);
		xpHud_->showCenterMessage(msg, 0xAA000000, 3500);
	}
	xpPlayer_->addXP(xp);                      // :412
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

void Game::updateDoors() {
	// Completed-animation owners are collected and resumed AFTER the loop
	// (legacy updateLerpSprites -> callThreads flush, src/Game.cpp:2985-3013):
	// BOTH directions bind the calling thread (EV_DOOROP interactive ops pass
	// it regardless of open/close, src/ScriptThread.cpp:751-779), so a
	// blocking scripted close must resume too — otherwise the script strand
	// dies parked between its close and the ops after it.
	ScriptThread* done[kOpenDoors];
	int numDone = 0;
	for (auto& a : doorAnims_) {
		if (!a.active) continue;
		if (a.dur <= 0) a.t = a.dur;
		int t = a.t;
		if (t > a.dur) t = a.dur;
		// Interpolate X/Y position and scale (legacy updateLerpSprite: S_X,
		// S_Y, S_SCALEFACTOR). Doors slide +32 and collapse 64->0 so the
		// quad never leaves the doorway (avoids drawing over walls).
		int x = a.srcX + ((a.dstX - a.srcX) * t / a.dur);
		int y = a.srcY + ((a.dstY - a.srcY) * t / a.dur);
		int s = a.startScale + ((a.endScale - a.startScale) * t / a.dur);
		s = std::max(0, std::min(64, s));
		if (map_ && a.sprite >= 0) {
			int n = map_->numSprites;
			map_->mapSprites[a.sprite + 0 * n] = (int16_t)x;
			map_->mapSprites[a.sprite + 1 * n] = (int16_t)y;
			map_->mapSprites[a.sprite + 8 * n] = (int16_t)s;
		}
		if (a.t >= a.dur) {
			a.active = false;
			if (a.door) {
				if (a.opening) {
					unlinkDoor(a.door); // door fully open: passable (src/Game.cpp:3131)
					std::fprintf(stderr,
						"[dbg] doorDone spr=%d OPEN unlinked=%d\n", a.sprite,
						(a.door->info & Entity::kInfoLinked) != 0 ? 0 : 1); // TEMP [dbg]
				} else if (map_ && a.sprite >= 0) {
					// Close completed: texture frame back to 0 and clear the
					// DOORLERP bit (src/Game.cpp:3120-3125). The door was
					// re-linked at close start in performDoorEvent.
					map_->mapSpriteInfo[a.sprite] &= 0xFFFF00FF;
					map_->mapSpriteInfo[a.sprite] &= 0x7FFFFFFF;
				}
			}
			// Resume the owning script once the animation completes (external
			// -1 resume protocol; legacy collects callThreads[] then runs them
			// after the sweep, src/Game.cpp:2985-3013).
			if (numDone < kOpenDoors) done[numDone++] = a.ownerThread;
			a.ownerThread = nullptr;
		}
	}
	for (int i = 0; i < numDone; ++i) {
		if (done[i] != nullptr && vm_ != nullptr) vm_->resumeThread(done[i]);
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
	for (auto& a : doorAnims_) {
		if (!a.active) continue;
		a.t += dtMs;
	}
	updateDoors();

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