# Spec — Faithful player collision (swept-capsule trace)

Date: 2026-08-23
Status: Ready for implementation
Owner: architect
Related: `docs/original-code/player-collision.md` (ground truth),
`docs/research/2026-08-23-player-collision.md`,
`docs/original-code/doors.md` §6, ADR 0001 (amended by ADR 0002),
`docs/architecture/specs/2026-08-23-fix-doors-sprite-placement.md` (supersedes its item A3 collision model)

## Goal

The player must never walk through walls. Today the rewrite's
`CapsuleToLineTrace` math is broken (sign-flipped solve, int32 overflow), so
walls are non-solid. This spec ports the legacy collision **faithfully**:
one swept-capsule trace per step over (a) world BSP lines with exact flag
semantics and (b) the masked `entityDb` pass with oriented-sprite segments and
circles — replacing the ad-hoc `canPlayerStep`.

Everything below cites legacy (`src/file:line`) ↔ rewrite target
(`new_src/file:line`). All math is integer, shifts where legacy shifts,
single-threaded GL loop, no new files.

---

## 1. Verified current defects (audit findings, confirmed against code)

| ID | Defect | Evidence |
|----|--------|----------|
| A | **Sign-flipped s/t**: rewrite solves `s=(d4·d3−d5·d2)/denom`, `t=(d4·d2−d5·d1)/denom`; legacy is `sNum=b·f−e·c`, `tNum=a·f−b·c` (both negated ⇒ both fracs mirrored ⇒ head-on hits land outside the clamp window) | `new_src/domain/game/Game.cpp:271-272` vs `src/Render.cpp:1151-1152` |
| B | **int32 overflow** in the same expressions: `d5 * d2` / `d5 * d1` multiply in 32-bit before the int64 subtract; operands reach ~8.4e6 ⇒ products ~7e13 ≫ 2³¹ | `new_src/domain/game/Game.cpp:271-272,285` vs widened `dot * (int64_t)…` at `src/Render.cpp:1144,1151-1152` |
| C | **Parallel case**: rewrite branches `denom==0 → s=t=0`. Legacy reaches the same live outcome differently — see §2 Conflict-1. Net behavior for `denom==0` is already equivalent | `new_src/domain/game/Game.cpp:273-275` vs `src/Render.cpp:1144-1148,1191-1203` |
| D | **Line-flag filter wrong**: rewrite skips only flag 6; flags 0–3,4,5,7 all block, and flag 7 blocks from both sides. Legacy: 0–3 block, 4/6 never, 5 only for masks with bit 0x10 or 0x800, 7 one-sided via cross test vs trace start | `new_src/domain/game/Game.cpp:306-307` vs `src/Render.cpp:1238-1247` |
| E | **Hit fraction from unclamped numerator**: rewrite divides the raw (and sign-flipped) numerator; legacy returns `(clampedSNum<<16)/sDen >> 2 − 1` | `new_src/domain/game/Game.cpp:285-289` vs `src/Render.cpp:1191-1207` |
| F | **Doors tile-granular**: any LINKED door blocks its whole target tile; legacy traces the door's ±32 oriented segment at its animated sprite position like any entity | `new_src/domain/game/Game.cpp:330-333` vs `src/Game.cpp:249-268` |
| G | **No generic entity trace**: only doors exist in `entityDb`; the trace must already be mask-generic so widening `loadEntities` later just works | `new_src/domain/game/Game.cpp:43-74` |

Dead code: `line[4]` is initialized twice in `canPlayerStep`
(`new_src/domain/game/Game.cpp:308-318`) — first init also has an X/X/Y/Y
ordering bug; disappears with the replacement.

---

## 2. Conflicts found between docs/brief and code

**Conflict-1 (must-read): the brief's F-C misstates legacy parallel behavior.**
F-C claims "parallel case denom==0 → s=t=0 instead of projecting onto the line
(legacy Render.cpp:1144-1148)". Reading `src/Render.cpp:1144-1148`: that branch
triggers on `denom < 0`. With exact integer dot products, `denom = a·e − b² ≥ 0`
always (Cauchy–Schwarz; equality iff the segments are parallel), so the `< 0`
branch is **dead code**. The live parallel case (`denom == 0`) takes the else
branch where — because parallel means `d1 = k·d2`, hence `b=k·e`, `c=k·f` —
both numerators vanish algebraically: `sNum = b·f − e·c = 0` and
`tNum = a·f − b·c = 0`. The zero-guards at `src/Render.cpp:1191-1203` then skip
the (zero-denominator) divisions, yielding `sFrac = tFrac = 0`: the test
reduces to `|P0 − Q0|² < radius²` (move-start vs line-start). **There is no
live "projection onto the line".** Resolution: port the legacy structure
exactly (numerators → clamps → zero-guarded quotients); do NOT implement an
explicit parallel-projection. Net effect for `denom==0` is identical to what
the rewrite does today, so F-C causes no behavior change — but implementing
the brief's "projection" reading would have been unfaithful.

**Conflict-2 (minor, informational):** legacy reads `mapSprites[S_X+sprite]`
for candidates *before* the `sprite != -1` guard (`src/Game.cpp:227-248` vs
`:249`) — a benign out-of-bounds read when a linked entity had no sprite. The
rewrite must not reproduce UB: candidates with `sprite < 0` fall back to a
circle test centered at `(0,0)`. Unreachable for every entity the rewrite
creates today (see §5.4).

**Conflict-3 (doc staleness, orchestrator-owned):** `docs/status.md:59` and the
wiring paragraph in `docs/architecture/README.md` reference `canPlayerStep`;
this spec renames it to `traceMove` (§5.1). Status row needs updating after
implementation. ADR 0001's "tile-granular collision granularity" consequence
is amended by ADR 0002.

No other conflicts: masks, radii, flag rules, frac conventions, strictly-2-D
movement and unconditional `destZ` in the brief all match ground truth.

---

## 3. Design decisions (one per line, decisive)

1. **One entry point**: `Game::traceMove(...)` replaces `Game::canPlayerStep`.
   Returns `true` when NOTHING blocks (= legacy `traceEntity == nullptr`,
   `src/MovementController.cpp:332-333`). Optional out-params expose the
   closest hit entity + frac (future: door-open-on-block for monsters,
   knockback travel `traceFracs[0]/16384`, `src/Entity.cpp:1220-1278`).
   Rename beats reusing the old name: the research report explicitly flagged
   `canPlayerStep` as implying nonexistent upstream semantics
   (`docs/research/2026-08-23-player-collision.md` "Doc inconsistencies"), and
   there are only two call sites.
2. **Mask constant defined once**: reuse `newcore::Enums::CONTENTS_PLAYERSOLID`
   = 13501 (`new_src/domain/game/Enums.h:31` ↔ `src/Enums.h:29`). No duplicate
   constant; `Game.h` adds none.
3. **World pass = flat O(numLines) walk** with legacy per-line flag rules and
   AABB rejects, instead of BSP leaf descent. Equivalence argument: every line
   belongs to exactly one leaf; legacy prunes subtrees whose node bounds miss
   the trace bbox (`src/Render.cpp:1213-1224`) — any line inside such a subtree
   lies within those bounds and would fail the *identical* per-line AABB
   rejects anyway (`:1248-1258`); the minimum frac over all lines therefore
   equals the minimum over legacy's visited subset, and descent order affects
   only tie identity (irrelevant: ties record the same `entities[0]`).
   Scope pre-approves O(numLines)/step. The classify-point quirk
   (`P << 4`, `src/Render.cpp:1269`) is descent-only and vanishes.
4. **Bit-faithful integer solve**: the port mirrors legacy variables 1:1
   (`n8..n11` pairing, sequential clamp order, zero-guarded quotients) rather
   than a textbook re-derivation. The legacy code interleaves representations
   (s-fraction over `denom` vs absolute t over `e` — see §5.2 notes) that a
   clean rewrite would silently change.
5. **Sum-of-squares circle test kept**: `d² < radius² + R²` (=881 walking,
   512 env-damage), not `(r+R)²` (`src/Render.cpp:1122`,
   `docs/original-code/player-collision.md` item 7).
6. **Omitted-unreachable legacy branches** (documented, restored later with
   their consumers): monster goal-position branch (`src/Game.cpp:227-237` —
   no monsters yet), Z filter (`:277-283` — walking always uses the 7-arg
   z-less trace, `:195-197`), world contact point `traceCollisionX/Y/Z`
   (`:302-304` — no consumer in the rewrite).
7. **Door solidity timeline untouched**: `performDoorEvent`/`updateDoors`/
   `canCloseDoor` stay as-is; the trace derives solidity purely from the
   linked-state (in `entityDb` = traced) × current-animated-sprite-position,
   which is exactly the legacy model (`src/Game.cpp:249-268`,
   `docs/original-code/doors.md` §6). Collision ignores `S_SCALEFACTOR`.

---

## 4. Files changed

| File | Change |
|---|---|
| `new_src/domain/game/Game.h` | Remove `canPlayerStep` decl (:54-56); add `traceMove` decl; add private trace buffers/helpers |
| `new_src/domain/game/Game.cpp` | Replace static `CapsuleToLineTrace` (:259-292) with faithful `capsuleToLineTrace` + `capsuleToCircleTrace`; replace `canPlayerStep` (:297-335) with `traceEntityHits`, `traceWorldFrac`, `traceMove` |
| `new_src/core/Main.cpp` | Two call sites (:350, :359) switch to `traceMove`; nothing else moves |

No CMake reconfiguration needed (GLOB, no new files). Rebuild only.

---

## 5. Per-file edit list (function level)

### 5.1 `new_src/domain/game/Game.h`

**Remove** `bool canPlayerStep(const MapData& map, int x1, int y1, int x2, int y2);` (:54-56).

**Add public** (after `useDoorFacing`):

```cpp
// Swept-capsule move trace: legacy Game::trace (7-arg wrapper
// src/Game.cpp:195-197, body :199-327) + Render::traceWorld
// (src/Render.cpp:1212-1283, flattened — see spec 2026-08-23
// faithful-player-collision §3.3). Sweeps segment (x0,y0)->(x1,y1) as a
// capsule of the given radius (canvas units, tile=64) against world lines
// (if mask & 1) and all entityDb entities matching mask & (1<<eType),
// skipping skipEnt. Returns TRUE when nothing blocks (commit allowed) —
// legacy commits iff traceEntity == nullptr (src/MovementController.cpp:332-333).
// Out-params (optional): closest hit = lowest frac (legacy traceEntity /
// traceFracs[0], src/Game.cpp:312-326); frac is 14.14 fixed point,
// 16384 == 1.0, hit <= 16382, start-inside == -1, miss sentinel 16384
// (src/Render.cpp:1119-1125,1195-1209).
bool traceMove(const MapData& map, int x0, int y0, int x1, int y1,
               Entity* skipEnt, int mask, int radius,
               Entity** outEntity = nullptr, int* outFrac = nullptr);
```

**Add private** (near `entityDb_`):

```cpp
// Trace scratch (reused buffers; single-threaded GL loop).
int tracePoints_[4] = { 0, 0, 0, 0 };              // x0,y0,x1,y1 (src/Game.cpp:208-211)
int traceBBox_[4]   = { 0, 0, 0, 0 };              // clamped bbox (src/Game.cpp:212-215)
std::vector<std::pair<int, Entity*>> traceHits_;   // (frac 14.14, entity)

void traceEntityHits(const MapData& map, Entity* skipEnt, int mask, int radius); // src/Game.cpp:216-296
int  traceWorldFrac(const MapData& map, int mask, int radius2);                  // src/Render.cpp:1212-1283 (flat)
```

(`#include <utility>` comes transitively; add explicitly if the compiler complains.)

### 5.2 `new_src/domain/game/Game.cpp` — math helpers (file-static, replace :259-292)

#### `capsuleToLineTrace` — exact port of `src/Render.cpp:1128-1210`

Variable map (legacy → port): `array`=p (move sweep `{P0x,P0y,P1x,P1y}`),
`array2`=q (line `{Q0x,Q0y,Q1x,Q1y}`), `n`=radius², `dot..dot5` = `a,b,e,c,f`,
`n2..n7` = `d1x,d1y,d2x,d2y,rx,ry`, `n8..n11` = `tDen,sDen,sNum,tNum`,
`n12,n13` = `sFrac,tFrac`, `n14,n15` = `vx,vy`.

```cpp
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
    const int a = d1x * d1x + d1y * d1y;               // dot  :1135 (|d1|²)
    const int b = d1x * d2x + d1y * d2y;               // dot2 :1136 (d1·d2)
    const int e = d2x * d2x + d2y * d2y;               // dot3 :1137 (|d2|²)
    const int c = d1x * rx  + d1y * ry;                // dot4 :1138 (d1·r)
    const int f = d2x * rx  + d2y * ry;                // dot5 :1139 (d2·r)
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
```

Representation notes the implementer MUST respect (they look odd; they are
the legacy):
* `tDen` starts as **denom**, not `e` (`src/Render.cpp:1144` assigns both
  `n8` and `n9` to the same value). In the unclamped path both fractions are
  parameter-fractions over `denom`; the s-clamp switches t to absolute-over-e
  (`tDen = e`). Sequential clamps operate on whatever representation is
  current — copy the structure verbatim, do not normalize.
* Parallel segments (`denom == 0`): algebra forces `sNum = tNum = 0`, the
  zero-guards skip the divisions, and the test reduces to
  `|P0−Q0|² < radius²`. No special case, no division by zero, no projection
  (Conflict-1).
* Strict `<` everywhere: a sweep ending exactly 16 units from a line does NOT
  hit (256 < 256 is false).
* Overflow bounds: coords ≤ 2047 ⇒ `a,b,e ≤ 2·2047² < 2²⁴`, cross products
  < 2⁴⁸, `<<16` < 2⁶⁴ — int64 safe for any future (weapon-ray) trace length.

#### `capsuleToCircleTrace` — exact port of `src/Render.cpp:1103-1126`

```cpp
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
```

### 5.3 `new_src/domain/game/Game.cpp` — trace passes (replace `canPlayerStep` :297-335)

#### `Game::traceEntityHits` (entityDb broadphase, `src/Game.cpp:216-296`)

```cpp
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
```

#### `Game::traceWorldFrac` (world lines, flat walk, `src/Render.cpp:1230-1265`)

```cpp
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
```

(Flat walk instead of BSP descent — equivalence proof in §3.3.)

#### `Game::traceMove` (orchestration, `src/Game.cpp:199-327`)

```cpp
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
        if (wf < 16384) traceHits_.push_back({ wf, &entities_[0] });            // :299-301 (entities_[0] = world slot)
        // World contact point traceCollisionX/Y/Z (src/Game.cpp:302-304):
        // not ported — no consumer in the rewrite yet.
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
```

Notes:
* `entities_[0]` is the reserved world slot with `def == nullptr`; callers
  must treat it as identity-only (same as legacy `&entities[0]`,
  `src/Game.cpp:301`).
* Stable sort keeps insertion order on equal fracs (entity hits before the
  world hit, matching legacy append order).

### 5.4 `new_src/core/Main.cpp` — wiring (minimal)

At :350 and :359 replace:

```cpp
if (game.canPlayerStep(g_map, player.viewX, player.viewY, tx, ty)) {
```
with:
```cpp
if (game.traceMove(g_map, player.viewX, player.viewY, tx, ty,
                   game.playerEntity(), Enums::CONTENTS_PLAYERSOLID, 16)) {
```

* Mask: `Enums::CONTENTS_PLAYERSOLID` = 13501 (`new_src/domain/game/Enums.h:31`
  ↔ `src/Enums.h:29`) — the single definition. Radius literal `16` matches
  `src/MovementController.cpp:326,332`.
* Everything else stays: input gating at :341-343 (matches legacy
  `src/PlayingInputHandler.cpp:25-27` — keep), commit block
  `attemptMove` + `setDestHeight(getHeight(tx,ty))` + `setZStep(...)`
  (:351-353/:360-362 = legacy `destZ = 36 + getHeight`,
  `src/MovementController.cpp:341-344`, height lookup
  `src/Render.cpp:2444-2451` — already correct), E-key door use (:366-370),
  auto-close on arrival/turn (:371-376, :389-390), `setPlayerPos` (:390).
* `noclip` cheat does not exist in the rewrite Player yet; legacy would pass
  mask 0 (`src/MovementController.cpp:326`). Nothing to do — noted in §8.

### 5.5 Delete

* Static `CapsuleToLineTrace` (`new_src/domain/game/Game.cpp:259-292`) —
  replaced by the file-static faithful pair.
* `Game::canPlayerStep` (`:297-335`) incl. the dead double `line[4]` init.

---

## 6. Implementation order

1. **Math**: replace the static solve with `capsuleToLineTrace` +
   `capsuleToCircleTrace` (§5.2). Compiles standalone.
2. **Passes**: add `traceEntityHits`, `traceWorldFrac`, `traceMove`; delete
   `canPlayerStep`. Add the private members/decls to `Game.h` (§5.1).
   Build will fail only at Main.cpp call sites — proceed immediately to 3.
3. **Wire**: swap the two Main.cpp call sites (§5.4).
4. **Build + verify**: `cmake --build build_new -j 8`; run; execute §9.
   Optional temporary instrumentation (remove before done): a stderr counter
   of world-line hits per step, to confirm nonzero hit counts on head-on
   approaches (today's regression showed 0/117).

---

## 7. Acceptance criteria

| Item | Criterion |
|---|---|
| A (solve) | Code review matches `src/Render.cpp:1128-1210` line-for-line under the variable map in §5.2 (clamp order, representation switch, zero-guards, strict `<`, `-1` bias). Behavioral: head-on steps into walls rejected; wall exactly 16 units off the path does NOT block; 15 units does. |
| B (flags) | Flag decode uses low 3 bits of the correct nibble; rules 0–3 / 4 / 5(mask-gated) / 6 / 7(cross ≤ 0 vs trace start) present verbatim from `src/Render.cpp:1238-1247`. Flag-4 lines stop blocking (behavior change, intended). |
| C (entityDb trace) | Broadphase bbox = endpoints ± radius clamped `[0,2047]`, tiles `bbox>>6 .. +(1)`, index `i + 32*j`; filter `skipEnt` + `mask & (1<<eType)` + `eType != 0`; oriented (`info & 0xF000000`) → ±32 segment via CURRENT `mapSprites` X/Y; others → circle r²=625 (env-damage 256) with sum-of-squares test. Doors block as sliding segments (F-F gone). |
| D (wire) | Single decision path `Game::traceMove`; commit iff it returns true; `destZ = 36+getHeight` untouched; E-key use + auto-close untouched (diff shows only the two call-site conditions). |
| E (scope) | None of §8's out-of-scope items crept in. |
| F (behavior changes) | Documented in-game: spots formerly fake-blocked by flag-4 lines are walkable; flag-7 walls directional. |
| Regression | Door timeline intact: solid while closed/linked, solid through the whole 750 ms open animation (segment slides with panel, scale ignored), passable at open-animation end (unlink), solid again at close start. Auto-close occupancy logic untouched. |

Build must be green with no new warnings from the touched files.

---

## 8. Explicitly OUT OF SCOPE this cycle

* Script events: leave-tile `executeTile`/`eventFlagsForMovement` and
  `EV_ABORT_MOVE` (`src/MovementController.cpp:327-331`).
* Enter-tile events / `touchTile` on arrival (`src/MovementController.cpp:160-196`).
* Automap turn-burning nuance on blocked moves (`:351-353`).
* Monster masks (15535 `CONTENTS_MONSTERSOLID`) and AI-path tracing
  (`src/Entity.cpp:1004,1078`).
* Env-damage radius-16 *trace* variant plumbing beyond the constant already
  specified (no env-damage entities exist).
* Widening `loadEntities` beyond doors (SPRITEWALL / PLAYERCLIP / DECOR /
  items) — the trace is already generic for them (F-G satisfied structurally).
* Key-repeat/input filtering beyond the existing view==dest gate.
* Diagonal odd-`spawnDir` corner case: `spawnDir` values whose octant index
  maps to diagonal step vectors produce diagonal first steps; player turns are
  always ±256° multiples in play, so this only matters for unusual map spawn
  bytes. Known limitation, documented, not handled.
* Performance culling (BSP descent, line spatial index) — O(numLines)/step
  approved.
* `noclip` cheat plumbing (no UI/cheat system yet); weapon/NPC/rotation-pitch
  traces with other masks/radii; Z-filtered 3-D traces.

## 9. Manual test checklist (user is the eyes)

Run `cd build_new/new_src && ./DoomIIRPG`, then:

1. **Head-on walls (both sides)**: walk forward into the nearest wall — the
   view must stop at the wall (no clipping, no jitter). Turn 180° and push
   into the opposite wall of the corridor/room — also solid.
2. **Corners**: walk into a convex corner from both approach directions —
   blocked; slide-along behavior must NOT appear (steps are all-or-nothing;
   the view just refuses).
3. **Long-wall hugging**: stand one tile away from a long wall and walk
   parallel to it repeatedly — smooth uninterrupted stepping, no snags, no
   drifting through. Then hug the wall face directly (path one half-tile from
   the wall line where geometry allows) — still no pass-through.
4. **Door segments while animating**: press E to open a door and IMMEDIATELY
   step toward it — the step must be refused during the whole ~750 ms
   animation (the panel is a sliding solid segment), then succeed once the
   door finishes opening. Close it (walk away to force auto-close, or re-use
   if applicable) and confirm it becomes solid again at close start.
5. **Door edge granularity**: standing beside (not in front of) a closed
   doorway, walk past the doorway plane parallel to the wall — movement should
   behave like passing a wall segment (blocked only when the swept capsule
   would cross the panel's ±32 span), not the old whole-tile force field.
6. **Formerly fake-solid spots**: revisit any location that used to stop you
   with no visible wall (flagged during the bug report) — some should now be
   walkable (flag-4 lines no longer block). If EVERY former blocker still
   blocks, suspect a missed flag-rule edit rather than success.
7. **One-sided walls**: find a one-way (flag-7) wall if the map has one —
   passable from behind, solid from the front.
8. **Stairs/height**: walk up/down any stair or ledge tile — vertical movement
   animates as before (collision is strictly 2-D; no new step-up refusals).
9. **General regression**: E-key opens faced doors as before; doors auto-close
   a turn after you pass them once the passage is free; no crash when bumping
   walls rapidly in all four directions.

---

## 10. Known behavior changes vs current rewrite (intended)

1. **Flag-4 lines become passable** (previously blocked): matches original
   (`src/Render.cpp:1238`). Some previously "solid" spots open up.
2. **Flag-7 lines become directional**: block only approaches with
   cross(L0−P0, L1−L0) > 0 relative to the TRACE START (`src/Render.cpp:1245-1247`).
3. **Flag-5 lines gated by mask**: with mask 13501 (has 0x10) they still
   block the player — no visible change for walking; matters only for future
   weapon/LOS masks.
4. **Door blocking becomes segment-granular** and tracks the animated panel
   position; whole-tile force-field gone (ADR 0001 consequence amended).
5. **Exact-16-unit touches pass** (strict `<` in both solve and circle test) —
   legacy quirk, now reproduced.
