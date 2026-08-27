#include "domain/game/SpriteLerps.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "domain/game/EntityDb.h"
#include "domain/game/Enums.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"

namespace newcore {

void SpriteLerps::init(const Env& env) {
	env_ = env;
	for (auto& ls : spriteLerps_) ls.hSprite = 0;
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

void SpriteLerps::SpriteLerp::calcDist() {
	int64_t dx = dstX - srcX;
	int64_t dy = dstY - srcY;
	// Legacy dist = sqrt(S) exactly: FixedSqrt shifts v<<=8 internally
	// (src/Game.cpp:3636-3641), so <<16 here cancels its >>8 result shift.
	dist = (int)(isqrt64(((int64_t)dx * dx + (int64_t)dy * dy) << 16) >> 8);
}

// src/Game.cpp:3596-3628 with b=true: thresholds ±32, result <<7.
int SpriteLerps::vecToDir(int dx, int dy) {
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
SpriteLerps::SpriteLerp* SpriteLerps::allocLerpSprite(ScriptThread* thread, int sprite, bool block) {
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
		Entity* ent = env_.db->findEntityBySprite(sprite);
		if (ent != nullptr && ent->def != nullptr &&
		    ent->def->eType == Enums::ET_MONSTER) {
			int n3 = (env_.map->mapSpriteInfo[sprite] >> 8) & 0xF0;
			n3 = (n3 == Enums::MANIM_WALK_FRONT || (ls->flags & SpriteLerp::kFlagAutoFace))
			         ? Enums::MANIM_IDLE
			     : (n3 == Enums::MANIM_WALK_BACK) ? Enums::MANIM_IDLE_BACK
			                                      : n3;
			env_.map->mapSpriteInfo[sprite] =
				(env_.map->mapSpriteInfo[sprite] & 0xFFFF00FF) | (n3 << 8);
			dbgLerpAnimAudit(sprite, env_.map->mapSpriteInfo[sprite]);
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
int SpriteLerps::spriteZBias(int sprite, int x, int y) const {
	if (!env_.map || env_.map->heightMap.empty()) return 0;
	int h = env_.map->heightMap[((y & 0x7FF) >> 6) * 32 + ((x & 0x7FF) >> 6)] << 3;
	if (sprite >= env_.map->numNormalSprites) h -= 32;
	return h;
}

// Single tick of updateLerpSprite (src/Game.cpp:2855-2956), script-lerp
// subset: relink keeps linked entities on their tile; S_NORELINK/
// ENT_NORELINK never apply to script lerps and staleView culling,
// walk-frame anim + boss footsteps have no rewrite counterpart yet.
int SpriteLerps::updateLerpSprite(SpriteLerp* ls) {
	if (!env_.map || ls->hSprite == 0) return 4;
	const int n = env_.map->numSprites;
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

	env_.map->mapSprites[sprite + 0 * n] = (int16_t)x;
	env_.map->mapSprites[sprite + 1 * n] = (int16_t)y;
	env_.map->mapSprites[sprite + 8 * n] = (int16_t)scale;
	// z interpolates in legacy baked space; storage is raw-relative, so
	// strip the terrain bias of the tile currently written to.
	int zStored = z - spriteZBias(sprite, x, y);
	env_.map->mapSprites[sprite + 2 * n] = (int16_t)zStored;

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
	Entity* ent = env_.db->findEntityBySprite(sprite);
	if (ent != nullptr && ent->def != nullptr &&
	    !(env_.map->mapSpriteInfo[sprite] & Enums::SPRITE_FLAG_HIDDEN) &&
	    (ent->def->eType == Enums::ET_NPC || ent->def->eType == Enums::ET_MONSTER)) {
		int info = env_.map->mapSpriteInfo[sprite];
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
			env_.map->mapSpriteInfo[sprite] =
				((info & 0xFFFF00FF) | ((phase | anim) << 8));   // :2943
			dbgLerpAnimAudit(sprite, env_.map->mapSpriteInfo[sprite]);
		}
	}

	// Per-tick relinkSprite analog: keep a LINKED entity on its tile.
	if (ent != nullptr && (ent->info & Entity::kInfoLinked) != 0) {
		int tx = x >> 6, ty = y >> 6;
		if ((ent->linkIndex % 32) != tx || (ent->linkIndex / 32) != ty) {
			env_.db->linkEntity(ent, tx, ty);
		}
	}
	return 0;
}

// Completion snap (freeLerpSprite head, src/Game.cpp:3078-3119, subset):
// dst X/Y/(Z)/scale written back, entity relinked to the dst tile, slot
// freed. Door/secret/chicken tails are not ported (door lerps use DoorAnim).
void SpriteLerps::freeLerpSprite(SpriteLerp* ls) {
	const int sprite = ls->hSprite - 1;
	if (env_.map && sprite >= 0) {
		const int n = env_.map->numSprites;
		env_.map->mapSprites[sprite + 0 * n] = (int16_t)ls->dstX;
		env_.map->mapSprites[sprite + 1 * n] = (int16_t)ls->dstY;
		env_.map->mapSprites[sprite + 2 * n] =
			(int16_t)(ls->dstZ - spriteZBias(sprite, ls->dstX, ls->dstY));
		env_.map->mapSprites[sprite + 8 * n] = (int16_t)ls->dstScale;
		Entity* ent = env_.db->findEntityBySprite(sprite);
		if (ent != nullptr && (ent->info & Entity::kInfoLinked) != 0 &&
		    ((ent->linkIndex % 32) != (ls->dstX >> 6) || (ent->linkIndex / 32) != (ls->dstY >> 6))) {
			env_.db->linkEntity(ent, ls->dstX >> 6, ls->dstY >> 6);
		}
		// Completion idle restore (src/Game.cpp:3101-3111): back-facing walks
		// without AUTO_FACE park in IDLE_BACK, everything else in IDLE.
		if (ent != nullptr && ent->def != nullptr &&
		    !(env_.map->mapSpriteInfo[sprite] & Enums::SPRITE_FLAG_HIDDEN) &&
		    (ent->def->eType == Enums::ET_NPC || ent->def->eType == Enums::ET_MONSTER)) {
			int n5 = (env_.map->mapSpriteInfo[sprite] >> 8) & 0xF0;
			int restore = (n5 == Enums::MANIM_IDLE_BACK || n5 == Enums::MANIM_WALK_BACK) &&
			                  !(ls->flags & SpriteLerp::kFlagAutoFace)
			              ? 0x1000
			              : 0x0000;
			env_.map->mapSpriteInfo[sprite] =
				(env_.map->mapSpriteInfo[sprite] & 0xFFFF00FF) | restore;
			dbgLerpAnimAudit(sprite, env_.map->mapSpriteInfo[sprite]);
		}
	}
	ls->hSprite = 0;
	ls->ownerThread = nullptr;
}

// snapLerpSprites (src/Game.cpp:1149-1166): zero the timings and run one tick
// so the completion snap + slot free happen immediately. Owner-thread resume
// omitted (the callers run mid-dispatch of some thread and re-entrant run() is
// unsafe in the rewrite VM).
void SpriteLerps::snap(int sprite) {
	for (SpriteLerp& ls : spriteLerps_) {
		if (ls.hSprite != sprite + 1) continue;
		ls.startTime = 0;                          // (:1159-1160)
		ls.travelTime = 0;
		updateLerpSprite(&ls);                     // zero travel -> completion snap + free
	}
}

void SpriteLerps::update(int dtMs) {
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
		if (env_.vm != nullptr) env_.vm->resumeThread(done[i]);
	}
}

} // namespace newcore
