#include "domain/game/TraceSystem.h"

#include <algorithm>

#include "domain/game/EntityDb.h"
#include "domain/game/Enums.h"
#include "domain/world/MapBits.h"
#include "domain/world/MapData.h"

namespace newcore {

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

// Resolves the legacy "world hit == entities[0], def == nullptr" convention
// once (ADR 0011 decision 2): every other decode site is gone.
void TraceSystem::pushHit(int frac, Entity* ent) {
	if (ent == nullptr) {
		// Honesty guard (review 2026-08-27): World is a real entity slot
		// (entities[0], def == nullptr by design), so a null pointer is not a
		// world hit — it is no hit at all (kind None), and a None record has
		// no place in the sorted hit list. Unreachable today: traceEntityHits
		// dereferences ent->def before pushing, and trace() pushes
		// db.worldEntity(), non-null once loadEntities ran.
		return;
	}
	TraceHit h;
	h.entity = ent;
	h.frac = frac;
	if (ent->def != nullptr) {                 // ent != nullptr guaranteed above
		h.kind = TraceHitKind::Ent;
		h.eType = ent->def->eType;
		h.eSubType = ent->def->eSubType;
	} else {
		h.kind = TraceHitKind::World;
		h.eType = Enums::ET_WORLD;
		h.eSubType = 0;
	}
	traceHits_.push_back(h);
}

// entityDb broadphase pass of the move trace (src/Game.cpp:216-296): every
// linked candidate matching mask & (1<<eType), tested as an oriented-sprite
// wall segment or a circle, appending hits to traceHits_.
void TraceSystem::traceEntityHits(const MapData& map, Entity* skipEnt, int mask, int radius) {
	for (int i = traceBBox_[0] >> 6; i < (traceBBox_[2] >> 6) + 1; ++i) {        // :216
		for (int j = traceBBox_[1] >> 6; j < (traceBBox_[3] >> 6) + 1; ++j) {    // :217
			EntityDb::TileWalk walk("TraceSystem::traceEntityHits");
			for (Entity* ent = env_.db->tileHead(i + 32 * j); ent && walk.ok(ent); ent = ent->nextOnTile) { // :218-220
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
					// walking: the player is skipEnt and PLAYER ∉ PLAYERSOLID.
					cx = playerX_; cy = playerY_;
				} else {                                                        // :244-247
					cx = (sprite >= 0) ? map.mapSprites[sprite + 0 * map.numSprites] : 0; // S_X
					cy = (sprite >= 0) ? map.mapSprites[sprite + 1 * map.numSprites] : 0; // S_Y
					// sprite<0 fallback (0,0) avoids legacy's benign OOB read;
					// unreachable: loadEntities always sets a sprite.
				}
				if (sprite >= 0 && (map.mapSpriteInfo[sprite] & SpriteInfo::ORIENTED) != 0) { // :249
					// Oriented sprite -> wall segment +/-32 through the CURRENT
					// (animated) sprite position. Axis: N/S bits (HORIZONTAL)
					// => horizontal; ANYTHING ELSE => vertical (legacy tests
					// only HORIZONTAL — do NOT add a VERTICAL check).  (:250-263)
					int ex = cx, ey = cy;
					if (map.mapSpriteInfo[sprite] & SpriteInfo::HORIZONTAL) { cx -= 32; ex += 32; }
					else                                                    { cy -= 32; ey += 32; }
					const int line[4] = { cx, cy, ex, ey };                     // :260-263
					const int frac = capsuleToLineTrace(tracePoints_, radius * radius, line); // :264
					if (frac < 16384) pushHit(frac, ent);                        // :265-268
				} else {                                                        // :270-288
					const int circleR2 = (ent->def->eType == Enums::ET_ENV_DAMAGE) ? 256 : 625; // :271-274
					const int frac = capsuleToCircleTrace(tracePoints_, radius * radius, cx, cy, circleR2); // :275
					// Z filter (src/Game.cpp:277-283) omitted: walking always
					// uses the z-less 7-arg trace (zCheck=false, :195-197).
					if (frac < 16384) pushHit(frac, ent);                        // :276-287
				}
			}
		}
	}
}

// World-line pass of the move trace (leaf body of Render::traceWorld,
// src/Render.cpp:1226-1267, walked flat over all lines — equivalence proof in
// spec §3.3). Returns the minimum hit fraction or 16384.
int TraceSystem::traceWorldFrac(const MapData& map, int mask, int radius2) {
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
		// PLAYERCLIP/MONSTERBLOCK_ITEM gates :1242-1244 (was 0x10 / 0x800).
		if (flag == 5 && (mask & Contents::PLAYERCLIP) == 0 &&
		    (mask & Contents::MONSTERBLOCK_ITEM) == 0) continue;
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
// TraceSystem.h for the contract.
TraceHit TraceSystem::trace(int x0, int y0, int x1, int y1,
                            Entity* skipEnt, int mask, int radius) {
	const MapData& map = *env_.map;
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
			pushHit(wf, env_.db->worldEntity());                                // :299-301 (entities[0] = world slot)
			traceCollisionX_ = x0 + ((wf * (x1 - x0)) >> 14);                   // :302-304 contact point
			traceCollisionY_ = y0 + ((wf * (y1 - y0)) >> 14);
		} else {
			traceCollisionX_ = x1;                                              // :306-308 a miss keeps the ray end
			traceCollisionY_ = y1;
		}
	}
	if (traceHits_.empty()) {                                                   // commit gate :332-333
		return TraceHit();                 // kind None -> clear, commit allowed
	}
	std::stable_sort(traceHits_.begin(), traceHits_.end(),                      // bubble sort asc :312-326
	                 [](const TraceHit& l, const TraceHit& r) { return l.frac < r.frac; });
	return traceHits_[0];                  // blocked by the closest hit
}

int TraceSystem::distFrom(const TraceHit& h, int x, int y) const {
	if (h.kind == TraceHitKind::World) {
		// calcPosition's ET_WORLD branch reads the last trace's collision
		// point (src/Entity.cpp:1375-1378).
		return std::max((x - traceCollisionX_) * (x - traceCollisionX_),
		                (y - traceCollisionY_) * (y - traceCollisionY_));
	}
	return distFrom(h.entity, x, y);
}

int TraceSystem::distFrom(const Entity* e, int x, int y) const {
	// Chebyshev^2 distFrom (src/Entity.cpp:1155-1158); position read from
	// mapSprites exactly like traceEntityHits (S_X/S_Y, src/Game.cpp:244-247).
	if (e == nullptr || env_.map == nullptr) return 0;
	if (e->def == nullptr || e->def->eType == Enums::ET_WORLD) {
		// calcPosition's ET_WORLD branch reads the last trace's collision
		// point (src/Entity.cpp:1375-1378); def == nullptr is the rewrite's
		// world slot.
		return std::max((x - traceCollisionX_) * (x - traceCollisionX_),
		                (y - traceCollisionY_) * (y - traceCollisionY_));
	}
	const int sprite = e->getSprite();
	const int ex = (sprite >= 0) ? env_.map->mapSprites[sprite + 0 * env_.map->numSprites] : 0;
	const int ey = (sprite >= 0) ? env_.map->mapSprites[sprite + 1 * env_.map->numSprites] : 0;
	return std::max((x - ex) * (x - ex), (y - ey) * (y - ey));
}

} // namespace newcore
