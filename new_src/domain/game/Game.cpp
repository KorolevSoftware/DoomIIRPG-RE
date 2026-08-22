#include "domain/game/Game.h"

#include <algorithm>

#include "domain/game/Enums.h"

namespace newcore {

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
	entities_.clear();
	entities_.resize(kEntities);
	for (auto& a : doorAnims_) { a.active = false; a.door = nullptr; }
	for (auto& d : openDoors_) d = nullptr;

	// Create door entities from TILE-flagged sprites whose tileNum+257 is a
	// door (271-278). Legacy does the same in loadMapEntities: map tile
	// +257 for TILE sprites, then EntityDefs::lookup(tileNum).
	int nextSlot = 2; // entities[0]=world, entities[1]=player (reserved)
	for (int i = 0; i < map.numSprites; ++i) {
		if (nextSlot >= kEntities) break;
		int info = map.mapSpriteInfo[i];
		if (info & 0x10000) continue; // hidden
		int tileNum = info & 0xFF;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		const EntityDef* def = (tileNum >= 0 && tileNum < 512) ? defs.lookup(tileNum) : nullptr;
		if (tileNum < Enums::TILENUM_FIRST_DOOR || tileNum > Enums::TILENUM_LAST_DOOR) continue;
		if (!def || def->eType != Enums::ET_DOOR) continue;

		Entity& e = entities_[nextSlot++];
		e.def = def;
		e.setSprite(i);
		e.info |= Entity::kInfoActive;
		int x = map.mapSprites[i + 0 * map.numSprites];
		int y = map.mapSprites[i + 1 * map.numSprites];
		linkEntity(&e, x >> 6, y >> 6);
		fprintf(stderr, "DOOR entity sprite=%d tile=%d (%d,%d) sub=%d\n",
			i, tileNum, x >> 6, y >> 6, def->eSubType);
	}
}

// Legacy interact (see Game.h): LINKED doors on the player's tile and on the
// adjacent tile in the facing direction; own tile wins (ray fraction ~0).
// Unlinked (open) doors are not traceable — legacy traces walk entityDb,
// which holds only linked entities.
Entity* Game::useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY) {
	(void)map;
	const int tiles[2][2] = {
		{ px >> 6, py >> 6 },
		{ (px + stepX) >> 6, (py + stepY) >> 6 },
	};
	for (auto& t : tiles) {
		for (Entity* e = findMapEntity(t[0], t[1]); e; e = e->nextOnTile) {
			if (!e->isDoor()) continue;
			if (!(e->info & Entity::kInfoLinked)) continue;
			performDoorEvent(0, e);
			return e;
		}
	}
	return nullptr;
}

// Legacy b3: effective tileNum in [271,281) (src/Game.cpp:1058). Excludes
// 281 although TILENUM_LAST_DOOR == 281 (spec C6).
static bool doorFamilyTile(int tileNum) { return tileNum >= 271 && tileNum < 281; }

bool Game::performDoorEvent(int n, Entity* door) {
	if (!door || !door->isDoor()) return false;
	if (door->def->eSubType == Enums::DOOR_LOCKED) return false; // needs key

	int sprite = door->getSprite();
	if (sprite < 0 || !map_) return false;

	int info = map_->mapSpriteInfo[sprite];
	int tileNum = info & 0xFF;
	if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
	bool family = doorFamilyTile(tileNum); // b3

	bool linked = (door->info & Entity::kInfoLinked) != 0;
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

	// NOTE: legacy snaps the animation instantly when offscreen
	// (n2 == 2 + cullBoundingBox, src/Game.cpp:1153-1155); cullBoundingBox is
	// not ported, so doors here always animate.

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
	if (!slot) return false;

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
			performDoorEvent(1, door);
		}
	}
}

// Segment-vs-capsule distance trace (legacy CapsuleToLineTrace): returns a
// "hit fraction" along the move segment (0..16384) or 16384 if no hit.
// Coordinates in canvas units (tile=64). array={x1,y1,x2,y2} move segment,
// line={x1,y1,x2,y2} wall segment.
static int CapsuleToLineTrace(int array[4], int radius2, int line[4]) {
	int mdx = array[2] - array[0], mdy = array[3] - array[1];
	int ldx = line[2] - line[0], ldy = line[3] - line[1];
	int ox = array[0] - line[0], oy = array[1] - line[1];
	int d1 = mdx * mdx + mdy * mdy;
	int d2 = mdx * ldx + mdy * ldy;
	int d3 = ldx * ldx + ldy * ldy;
	int d4 = mdx * ox + mdy * oy;
	int d5 = ldx * ox + ldy * oy;
	int64_t denom = d1 * (int64_t)d3 - d2 * (int64_t)d2;
	int64_t s = 0, t = 0;
	if (denom != 0) {
		s = (d4 * (int64_t)d3 - d5 * d2) / denom;
		t = (d4 * (int64_t)d2 - d5 * d1) / denom;
	} else {
		s = 0; t = 0;
	}
	if (s < 0) s = 0; else if (s > 1) s = 1;
	if (t < 0) t = 0; else if (t > 1) t = 1;
	int px = array[0] + (int)(mdx * s);
	int py = array[1] + (int)(mdy * s);
	int qx = line[0] + (int)(ldx * t);
	int qy = line[1] + (int)(ldy * t);
	int dist = (px - qx) * (px - qx) + (py - qy) * (py - qy);
	if (dist < radius2) {
		// Projection of the closest point along the move segment.
		int64_t frac = (d4 * (int64_t)d3 - d5 * d2);
		int64_t div = denom;
		if (div == 0) div = 1;
		int64_t f = (frac << 16) / div;
		return (int)(f >> 2) - 1;
	}
	return 16384;
}

// Collision: the player may step to (tx,ty) if the segment from the current
// tile centre to the target centre does not cross a solid wall line, and no
// closed door entity occupies the target tile.
bool Game::canPlayerStep(const MapData& map, int x1, int y1, int x2, int y2) {
	int tx = x2 >> 6, ty = y2 >> 6; // canvas units: tile = 64
	if (tx < 0 || ty < 0 || tx >= 32 || ty >= 32) return false;

	// Wall check via line segment trace (legacy traceWorld). Lines are in
	// byte*8 = canvas units (<<3). flag 4 = solid wall (blocks), 6 = opening.
	{
		int move[4] = { x1, y1, x2, y2 };
		for (int i = 0; i < map.numLines; ++i) {
			int flag = (map.lineFlags[i >> 1] >> ((i & 1) << 2)) & 0xF & 0x7;
			if (flag == 6) continue; // doorway opening
			int line[4] = {
				(map.lineXs[i * 2 + 0] & 0xFF) << 3,
				(map.lineXs[i * 2 + 1] & 0xFF) << 3,
				(map.lineYs[i * 2 + 0] & 0xFF) << 3,
				(map.lineYs[i * 2 + 1] & 0xFF) << 3,
			};
			// Reorder: line = {x1,y1,x2,y2}
			line[0] = (map.lineXs[i * 2 + 0] & 0xFF) << 3;
			line[1] = (map.lineYs[i * 2 + 0] & 0xFF) << 3;
			line[2] = (map.lineXs[i * 2 + 1] & 0xFF) << 3;
			line[3] = (map.lineYs[i * 2 + 1] & 0xFF) << 3;
			int hit = CapsuleToLineTrace(move, 16 * 16, line);
			if (hit < 16384) return false; // wall in the way
		}
	}

	// Door entities: LINKED doors are solid (R8 timeline — closed = solid,
	// whole open animation = solid via ENT_NORELINK, close start = solid
	// again immediately; src/Game.cpp:1079,3131). Fully open (unlinked)
	// doors are passable. Legacy traces the door's ±32 wall segment through
	// its animated position (src/Game.cpp:249-268); the rewrite is
	// tile-granular (whole target tile blocked while linked).
	Entity* e = findMapEntity(tx, ty);
	for (; e; e = e->nextOnTile) {
		if (e->isDoor() && (e->info & Entity::kInfoLinked)) return false;
	}
	return true;
}

void Game::updateDoors() {
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
				} else if (map_ && a.sprite >= 0) {
					// Close completed: texture frame back to 0 and clear the
					// DOORLERP bit (src/Game.cpp:3120-3125). The door was
					// re-linked at close start in performDoorEvent.
					map_->mapSpriteInfo[a.sprite] &= 0xFFFF00FF;
					map_->mapSpriteInfo[a.sprite] &= 0x7FFFFFFF;
				}
			}
		}
	}
}

void Game::update(int dtMs) {
	for (auto& a : doorAnims_) {
		if (!a.active) continue;
		a.t += dtMs;
	}
	updateDoors();
}

} // namespace newcore