#include "domain/game/DoorSystem.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Enums.h"
#include "domain/game/ScriptVM.h"
#include "domain/game/TraceSystem.h"
#include "domain/world/MapData.h"

namespace newcore {

void DoorSystem::init(const Env& env) {
	env_ = env;
	for (auto& a : doorAnims_) { a.active = false; a.door = nullptr; a.ownerThread = nullptr; }
	for (auto& d : openDoors_) d = nullptr;
}

// ---- entityDb tile-list access (copies of Game's; see DoorSystem.h) ----

Entity* DoorSystem::findMapEntity(int x, int y) const {
	if (x < 0 || y < 0 || x >= 32 || y >= 32) return nullptr;
	return env_.entityDb[y * 32 + x];
}

void DoorSystem::linkEntity(Entity* e, int tx, int ty) {
	if (tx < 0 || ty < 0 || tx >= 32 || ty >= 32) return;
	unlinkEntity(e);
	int idx = ty * 32 + tx;
	e->nextOnTile = env_.entityDb[idx];
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e;
	e->prevOnTile = nullptr;
	env_.entityDb[idx] = e;
	e->linkIndex = (short)idx;
	e->info |= Entity::kInfoLinked;
}

void DoorSystem::unlinkEntity(Entity* e) {
	if (!(e->info & Entity::kInfoLinked)) return;
	if (e->prevOnTile) e->prevOnTile->nextOnTile = e->nextOnTile;
	else {
		int idx = e->linkIndex;
		if (idx >= 0 && idx < 1024 && env_.entityDb[idx] == e)
			env_.entityDb[idx] = e->nextOnTile;
	}
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e->prevOnTile;
	e->nextOnTile = e->prevOnTile = nullptr;
	e->info &= ~Entity::kInfoLinked;
}

// Legacy interact (see DoorSystem.h): LINKED doors on the player's tile and on
// the adjacent tile in the facing direction; own tile wins (ray fraction ~0).
// Unlinked (open) doors are not traceable — legacy traces walk entityDb,
// which holds only linked entities. Locked doors are refused without
// animating (src/PlayingInputHandler.cpp:447-449).
DoorSystem::DoorUseResult DoorSystem::useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY) {
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

bool DoorSystem::performDoorEvent(int n, Entity* door, int n2, ScriptThread* ownerThread) {
	if (!door || !door->isDoor()) return false;
	if (door->def->eSubType == Enums::DOOR_LOCKED) {       // needs key
		return false;
	}

	int sprite = door->getSprite();
	if (sprite < 0 || !env_.map) return false;

	int info = env_.map->mapSpriteInfo[sprite];
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

	int sx = env_.map->mapSprites[sprite + 0 * env_.map->numSprites]; // S_X (canvas units)
	int sy = env_.map->mapSprites[sprite + 1 * env_.map->numSprites]; // S_Y
	int curScale = env_.map->mapSprites[sprite + 8 * env_.map->numSprites]; // S_SCALEFACTOR

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
	env_.map->mapSpriteInfo[sprite] |= 0x80000000;

	// Register/keep open doors for auto-close; unregister when closing.
	if (n == 0) registerOpenDoor(door);
	else unregisterOpenDoor(door);

	// Texture frame 1 while open/animating (src/Game.cpp:1150-1152).
	if (n == 0 && family) {
		env_.map->mapSpriteInfo[sprite] = (env_.map->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x100;
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

bool DoorSystem::doorRegistered(Entity* door) const {
	if (!door) return false;
	for (auto* d : openDoors_) if (d == door) return true;
	return false;
}

void DoorSystem::registerOpenDoor(Entity* door) {
	for (auto& d : openDoors_) {
		if (d == nullptr) { d = door; return; }
	}
}

void DoorSystem::unregisterOpenDoor(Entity* door) {
	for (auto& d : openDoors_) {
		if (d == door) d = nullptr;
	}
}

void DoorSystem::unlinkDoor(Entity* door) {
	if (env_.map && door->info & Entity::kInfoLinked) {
		unlinkEntity(door);
	}
}

// Legacy CanCloseDoor (src/Game.cpp:1215-1236): tile-granular occupancy —
// the player or a monster on the door tile, or on either neighbor tile along
// the passage axis, blocks auto-close.
bool DoorSystem::canCloseDoor(Entity* door) {
	if (!door || !env_.map) return false;
	int link = door->linkIndex;
	int tx = link % 32, ty = link / 32;
	int cx = tx * 64 + 32, cy = ty * 64 + 32;

	auto occupied = [&](int x, int y) -> bool {
		// Player resolved tile-granularly (legacy compares destX/destY tiles,
		// src/Game.cpp:741-743; identical to viewX/viewY at advanceTurn times).
		if (env_.trace->playerX() >= 0 && (env_.trace->playerX() >> 6) == (x >> 6) &&
		    (env_.trace->playerY() >> 6) == (y >> 6)) return true;
		for (Entity* e = findMapEntity(x >> 6, y >> 6); e; e = e->nextOnTile)
			if (e->def && e->def->eType == Enums::ET_MONSTER) return true; // mask 6 = player|monster (src/Game.cpp:1224)
		return false;
	};

	if (occupied(cx, cy)) return false;
	int info = env_.map->mapSpriteInfo[door->getSprite()];
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
void DoorSystem::advanceTurnDoors() {
	for (auto* door : openDoors_) {
		if (!door) continue;
		if (canCloseDoor(door)) {
			performDoorEvent(1, door, 2);      // snap-if-offscreen mode (src/Game.cpp:1275)
		}
	}
}

void DoorSystem::updateDoors() {
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
		if (env_.map && a.sprite >= 0) {
			int n = env_.map->numSprites;
			env_.map->mapSprites[a.sprite + 0 * n] = (int16_t)x;
			env_.map->mapSprites[a.sprite + 1 * n] = (int16_t)y;
			env_.map->mapSprites[a.sprite + 8 * n] = (int16_t)s;
		}
		if (a.t >= a.dur) {
			a.active = false;
			if (a.door) {
				if (a.opening) {
					unlinkDoor(a.door); // door fully open: passable (src/Game.cpp:3131)
					std::fprintf(stderr,
						"[dbg] doorDone spr=%d OPEN unlinked=%d\n", a.sprite,
						(a.door->info & Entity::kInfoLinked) != 0 ? 0 : 1); // TEMP [dbg]
				} else if (env_.map && a.sprite >= 0) {
					// Close completed: texture frame back to 0 and clear the
					// DOORLERP bit (src/Game.cpp:3120-3125). The door was
					// re-linked at close start in performDoorEvent.
					env_.map->mapSpriteInfo[a.sprite] &= 0xFFFF00FF;
					env_.map->mapSpriteInfo[a.sprite] &= 0x7FFFFFFF;
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
		if (done[i] != nullptr && env_.vm != nullptr) env_.vm->resumeThread(done[i]);
	}
}

void DoorSystem::update(int dtMs) {
	for (auto& a : doorAnims_) {
		if (!a.active) continue;
		a.t += dtMs;
	}
	updateDoors();
}

} // namespace newcore
