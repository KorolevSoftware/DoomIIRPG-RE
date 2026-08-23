# Player collision & movement (original `src/`) — verified mechanics

How the legacy RE port turns a step input into a committed position, and what
exactly blocks it. Every claim cites `src/<file>:<line>`.

Units: **world/canvas units, 1 tile = 64 units** (`src/Game.cpp:1023-1024`,
`src/MovementController.cpp:329`); trace fractions are ~**14.14 fixed point**
(`16384 == 1.0`, `src/Game.cpp:265,299`; hits are computed in 16.16 and
converted with `>> 2`, minus a `-1` bias, `src/Render.cpp:1119-1123,
1195-1207`). The map is 32×32 tiles = 2048×2048 units
(trace bounding box clamps `[0,2047]`, `src/Game.cpp:212-215`).

--------------------------------------------------------------------------------
## 1. Movement flow end-to-end

Call chain for one step:

```
SDL key -> App/Input mapping
  -> InputEventController (state router)
       ST_PLAYING / ST_AUTOMAP -> canvas->handlePlayingEvents(key, action)
             (src/InputEventController.cpp:354-363; delegate src/Canvas.cpp:1254)
  -> PlayingInputHandler::handlePlayingEvents        (input gating, src/PlayingInputHandler.cpp:19)
       ACTION_UP/DOWN/STRAFELEFT/STRAFERIGHT -> canvas->attemptMove(viewX +/- viewStepX, viewY +/- viewStepY)
             (src/PlayingInputHandler.cpp:100-111; forwarder src/Canvas.cpp:1233)
  -> MovementController::attemptMove(n, n2)          (the move, src/MovementController.cpp:312-359)
       -> Game::eventFlagsForMovement + Game::executeTile (leave-tile script events, may ABORT)
       -> Game::trace(view -> dest, mask 13501, radius 16)   <- THE COLLISION TEST
       -> commit: destX/destY/destZ, startRotation, Player::relink
  -> MovementController::updateView                  (per-frame interpolation, src/MovementController.cpp:377-547)
       arrival -> finishMovement()                    (enter-tile events, touchTile, advanceTurn)
```

### 1.1 Input gating (`src/PlayingInputHandler.cpp:19-75`)

* Input is dropped while the view is interpolating (unless sniper-zoomed):
  `if (!isZoomedIn && (viewX!=destX || viewY!=destY || viewAngle!=destAngle)) return`
  (`src/PlayingInputHandler.cpp:25-27`).
* Dropped during knockback or map change (`:29-31`).
* If input arrives mid-animation, the animation is **snapped to its end**
  first (`finishMovement()` / `finishRotation(true)`), then processing
  continues (`:46-61`).
* `blockInputTime` and weapon-select consume input (`:63-69`).
* Moving/using is refused while propagators are active, monsters are animating
  (`snapMonsters` fails) or effects animate — turning and weapon switching are
  exempt (`:71-74`).

### 1.2 Steps and turns

* Step vectors come from `Canvas::viewStepValues =
  {64,0, 64,-64, 0,-64, -64,-64, -64,0, -64,64, 0,64, 64,64}`
  (`src/Canvas.h:108`), indexed by facing octant
  `((destAngle & 0x3FF) >> 7) << 1` (`src/MovementController.cpp:290-294`).
  Player facing angles are multiples of 256°/1024 (spawn `src/Game.cpp:964`,
  turn = ±256 per press `src/PlayingInputHandler.cpp:112-121`), so **player
  steps are always axis-aligned ±64 units**; forward/back use
  `(viewStepX, viewStepY)`, strafe left/right swap them
  (`src/PlayingInputHandler.cpp:100-111`). The diagonal table entries are used
  by wall-quad rendering, not by player movement.
* Turn: `destAngle += 256` (left) / `-= 256` (right), then
  `startRotation(false)` for the stair-pitch probe (`src/PlayingInputHandler.cpp:112-121`);
  rotation interpolates by `animAngle` and `finishRotation` recomputes
  `viewSin/viewCos/viewStep*` (`src/MovementController.cpp:455-466, 284-310`).

### 1.3 `MovementController::attemptMove` (`src/MovementController.cpp:312-359`)

1. `renderOnly` bypass copies the destination verbatim (`:316-320`).
2. Familiar control: tiles whose `mapFlags` has bit `0x10` refuse movement
   (message 222) unless stepping onto the player's saved tile
   (`saveX/saveY`) (`:322-325`).
3. Clip mask: `mask = player->noclip ? 0 : 13501` (=`CONTENTS_PLAYERSOLID`,
   `src/Enums.h:29`) (`:326`).
4. Leave-tile events: `eventFlagsForMovement(view -> dest)` builds direction
   flags (`eventFlags[0] = 2|dir` for the tile being left, `eventFlags[1] =
   1|opposite-dir` for the tile entered; direction bit table
   `src/Game.cpp:974-1014`), then `executeTile(srcTile, eventFlags[0])`.
   A script `EV_ABORT_MOVE` sets `canvas->abortMove` and cancels the move
   before tracing (`src/MovementController.cpp:327-331`;
   `src/ScriptThread.cpp:663-667`).
5. **The collision test**: `game->trace(viewX, viewY, n, n2, playerEnt,
   13501, 16)` — sweep segment with radius 16 (`:332`).
6. Commit only if `traceEntity == nullptr` (`:333`). On commit:
   `destX/destY = n/n2`, `destZ = 36 + getHeight(destX, destY)`
   (no height restriction, see §6), z is animated with
   `zStep = ceil(|destZ-viewZ| / animFrames)` (`:341-344`),
   `prevX/prevY` recorded (`:345-346`), `startRotation(false)` adjusts
   stair pitch (`:347`), and `player->relink()` moves the player entity to the
   destination tile in `entityDb` (`:348`; `src/Player.cpp:1425-1446`).
7. If blocked **and** in `ST_AUTOMAP` **and** not knocking back, the move is
   still rejected but `advanceTurn()` runs anyway — bumping a wall in automap
   consumes a turn (`:351-353`).
8. During knockback, `updateView` re-issues `attemptMove(viewX + knockbackX*64,
   ...)` once per arrived tile until `knockbackDist` drains
   (`src/MovementController.cpp:398-400`).

### 1.4 Arrival (`src/MovementController.cpp:160-196, 510-512`)

When the interpolated view reaches `destX/destY`, `finishMovement()` runs:
queued `gotoThread` scripts, `executeTile(destTile, flagForFacingDir(8))`
(facing-dependent events, `:214-224`), `executeTile(destTile, eventFlags[1])`
(enter-tile events), `touchTile(destX, destY, true)` (entity `touched()`,
`src/Game.cpp:687-699`), knockback bookkeeping, `uncoverAutomap()` and
`advanceTurn()` (`src/MovementController.cpp:160-196`).

--------------------------------------------------------------------------------
## 2. The trace primitive

### 2.1 Signature and outputs (`src/Game.cpp:195-327`)

```cpp
void Game::trace(x0, y0, x1, y1, Entity* skipEnt, int mask, int radius);        // 7-arg, :195
void Game::trace(x0, y0, z0, x1, y1, z1, Entity* skipEnt, int mask, int radius, bool zCheck); // :199
```

Outputs:

* `traceEntities[i]` / `traceFracs[i]`: **all** hits, sorted ascending by frac;
  `traceEntity` = closest hit or `nullptr` (`:312-326`).
* Frac scale: `16384 == 1.0`. A hit returns `(t16.16 >> 2) - 1`, i.e. at most
  `16382`, and `-1` when the trace starts inside the shape; a miss is exactly
  `16384` (`src/Render.cpp:1119-1125, 1195-1209`).
* Only the world pass records a contact point:
  `traceCollisionX/Y/Z = P0 + frac*(P1-P0)` (`:302-304`).
* There is **no normal vector** output anywhere — consumers only use fracs and
  identities.

Broad phase: bounding box of both endpoints inflated by `radius`, clamped to
`[0,2047]`; every tile overlapping it is visited row-major
(`x + 32*y`, `src/Game.cpp:212-218`) and each tile's `entityDb` linked list is
walked (`:218-293`). Candidates matching `mask & (1 << def->eType)` and
different from `skipEnt` are tested (`:221`).

Candidate test position (`:227-248`):

* monster currently lerping toward a goal (`monster->goalFlags & 1`):
  goal tile center `(goalX<<6)+32, (goalY<<6)+32`, current sprite Z;
* `ET_PLAYER`: `canvas->destX/destY/destZ` (committed destination, not the
  interpolated view);
* everything else: current `mapSprites[S_X/S_Y/S_Z]`.

Shape selection (`:249-289`):

* If the sprite info word has any **orientation bit** in `0xF000000`
  (N=`0x1000000`, S=`0x2000000`, E=`0x4000000`, W=`0x8000000`,
  `src/Enums.h:1240-1250`): treat as a **wall segment** through the *current*
  sprite position — if `info & 0x3000000` (N or S ⇒ horizontal wall) the
  segment is `(x-32, y)..(x+32, y)`, else vertical `(x, y-32)..(x, y+32)`
  (`:250-263`), tested with `CapsuleToLineTrace` (`:264-268`).
* Otherwise: **circle**, squared radius `625` (r=25) — `256` (r=16) for
  `ET_ENV_DAMAGE` (`:270-275`), tested with `CapsuleToCircleTrace`
  (`:275-288`).
* Optional Z filter (only for full 3-D traces with `zCheck=true`):
  the z at the hit fraction must satisfy `|zHit - zEntity| < 32 + radius`
  (`:277-283`). Player movement uses the 7-arg trace (`z=-1, zCheck=false`)
  so this never applies to walking (`:195-197, 332`).

World pass: if `mask & 1` (ET_WORLD), `render->traceWorld(...)` returns the
min line frac; a world hit appends `entities[0]` (`:297-305`).

### 2.2 Clip-mask decomposition

`ET_*` values (`src/Enums.h:10-25`): 0 WORLD, 1 PLAYER, 2 MONSTER, 3 NPC,
4 PLAYERCLIP, 5 DOOR, 6 ITEM, 7 DECOR, 8 ENV_DAMAGE, 9 CORPSE,
10 ATTACK_INTERACTIVE, 11 MONSTERBLOCK_ITEM, 12 SPRITEWALL,
13 NONOBSTRUCTING_SPRITEWALL, 14 DECOR_NOCLIP.

Named constants live at `src/Enums.h:26-40`. Bit decompositions:

| Mask | Value | Bits (types) |
|---|---|---|
| `CONTENTS_PLAYERSOLID` (player move, `src/MovementController.cpp:326`) | 13501 `0x34BD` | WORLD, MONSTER, NPC, PLAYERCLIP, DOOR, DECOR, ATTACK_INTERACTIVE, SPRITEWALL, NONOBSTRUCTING_SPRITEWALL |
| `CONTENTS_WEAPONSOLID` (fire/use, `src/PlayingInputHandler.cpp:200`) | 13997 `0x36AD` | playersolid − PLAYERCLIP + CORPSE |
| facing-entity probe (`src/MovementController.cpp:38`) | 21741 `0x54ED` | WORLD, MONSTER, NPC, DOOR, ITEM, DECOR, ATTACK_INTERACTIVE, SPRITEWALL, DECOR_NOCLIP |
| weapon-lower probe (`src/MovementController.cpp:124`) | 4141 `0x102D` | WORLD, MONSTER, NPC, DOOR, SPRITEWALL |
| rotation pitch probe (`src/MovementController.cpp:233`) | 4133 `0x1025` | WORLD, MONSTER, DOOR, SPRITEWALL |
| `CONTENTS_MONSTERSOLID` (AI paths, `src/Entity.cpp:1078`) | 15535 `0x3CAF` | WORLD, PLAYER, MONSTER, NPC, DOOR, DECOR, ATTACK_INTERACTIVE, MONSTERBLOCK_ITEM, SPRITEWALL, NONOBSTRUCTING_SPRITEWALL |
| `CONTENTS_SPLASH_SOLID` (combat LOS, `src/Combat.cpp:1070,1092`) | 4129 `0x1021` | WORLD, DOOR, SPRITEWALL |

Notes: the player never collides with `ET_PLAYER` (bit 1 absent from 13501);
items/corpses/env-damage never block walking; `NONOBSTRUCTING_SPRITEWALL`
(bit 13) *does* block player movement despite the name.

--------------------------------------------------------------------------------
## 3. Wall collision (BSP lines)

### 3.1 Data source (mapXX.bin)

Loaded in `src/LoadingManager.cpp`: header counts `numLines` (`:377`),
arrays allocated `:419-421`, bytes read `:481-483`:

* `lineFlags`: `(numLines+1)/2` bytes, **two 4-bit flags per byte**, line
  `i` uses nibble `i&1` (low nibble first);
* `lineXs`, `lineYs`: `numLines*2` bytes each (endpoints interleaved
  `[x0,x1]`, `[y0,y1]`).

Coordinates are **bytes**: in collision they are shifted `<< 3`
(`src/Render.cpp:1233-1236`), i.e. quantized to 8 units, range ≤ 2040.

The lines belong to BSP leaves: leaf marker `(nodeOffsets[n] & 0xFFFF) ==
0xFFFF`; the leaf's line range packs into `nodeChildOffset2[n]`
(start = `& 0x3FF`, count = `(>>10) & 0x3F`) (`src/Render.cpp:1225-1229`).
Node bounds are bytes `<< 3` (`:1213-1222`); plane side test
`nodeClassifyPoint = ((P·normal) >> 14) + planeOffset` with normals in 14.14
(`src/Render.cpp:926-929`). Note: `traceWorld` classifies with the trace point
shifted `<< 4` (`src/Render.cpp:1269`) whereas rendering classifies with raw
view coords (`src/Render.cpp:1086`) — port both call sites as-is.

### 3.2 `Render::traceWorld` (`src/Render.cpp:1212-1283`)

Descends front/back child based on the start point's side, collecting the
minimum line frac among leaves. Per line (`:1230-1265`):

1. `flag = lineFlags[i>>1] >> ((i&1)<<2) & 0xF & 0x7` — only the low **3**
   bits matter here; bit 3 is the runtime "seen" flag used by the automap
   (set while rendering `src/Render.cpp:735-742`, by the `givemap` cheat
   `src/Game.cpp:1019-1021`, read `src/AutomapController.cpp:121-128`).
2. Flag semantics in collision:
   * `4` → never blocks (skip);
   * `6` → never blocks (skip);
   * `5` → blocks **only** if the trace mask contains bit `0x10`
     (ET_PLAYERCLIP, bit 4) or bit `0x800` (ET_MONSTERBLOCK_ITEM, bit 11)
     — the player mask includes `0x10`, so these lines stop the player but
     not weapons/LOS;
   * `7` → **one-sided**: skipped when
     `(L0x-P0x)*(L1y-L0y) - (L0y-P0y)*(L1x-L0x) <= 0`, i.e. only blocks
     approaches from the front side (cross product > 0);
   * `0..3` → always block (two-sided solid).
3. Cheap AABB rejects against the trace bounding box (`:1248-1258`).
4. `CapsuleToLineTrace(points, radius*radius, line)`; keep the minimum frac
   (starts at 16384, `:1226,1260-1264`).

Automap draws only discovered lines whose flag is `0` or `6`
(`src/AutomapController.cpp:121-128`).

### 3.3 `CapsuleToLineTrace` — segment/segment closest-point
(`src/Render.cpp:1128-1210`)

Inputs: `array = [P0x,P0y,P1x,P1y]` (move sweep), `n = radius²`,
`array2 = [Q0x,Q0y,Q1x,Q1y]` (wall line). With
`d1 = P1-P0`, `d2 = Q1-Q0`, `r  = P0-Q0` and
`a = d1·d1, b = d1·d2, e = d2·d2, c = d1·r, f = d2·r`
(`dot` at `src/Render.cpp:1099-1101`; named `dot..dot5` in code):

```
denom = a*e - b*b                       (if denom < 0: s=0, t=f/e branch)
s     = (b*f - e*c)/denom               clamped to [0, denom]
t     = (a*f - b*c)/denom               clamped to [0, e]
on s-clamp: t := f/e (s=0) or (f+b)/e (s=1)
on t-clamp (t<0):  t:=0; s := clamp(-c/a) expressed as numerators
                   (s=0, or s=denom, or s=-c over denominator a)
on t-clamp (t>e):  t:=e; s := clamp((b-c)/a) same pattern
closest vector v = r + (s_num/denom_s)*d1 - (t_num/denom_t)*d2
                 (computed as ((r<<16) + s*d1 - t*d2) >> 16 per component)
hit if v·v < radius²
return (s_num<<16)/denom_s >> 2  -  1      else 16384
```

This is the classic clamped segment-segment closest-distance solve, done in
integers (64-bit intermediates, `src/Render.cpp:1140-1143`); the returned
parameter is the **move segment's** fraction.

### 3.4 `CapsuleToCircleTrace` — segment/circle
(`src/Render.cpp:1103-1126`)

```
d = P1-P0; f = C-P0
tt = clamp(d·f, 0, d·d)                 // param numerator, denom = d·d
t16 = (tt << 16) / (d·d)                // 16.16 fraction
closest = P0 + (d * t16 >> 16)
hit if |closest-C|² < radius² + circleR²      // SUM OF SQUARES, not (r+R)²
return (t16 >> 2) - 1  else 16384
```

Important quirk: the overlap test compares `d² < r_capsule² + R_circle²`
(e.g. `256 + 625 = 881` for player vs normal entity, `256+256` vs
env-damage), **not** `(r+R)² = 1681`. Effective center-blocking distance is
`sqrt(881) ≈ 29.7` units — an intentional/legacy approximation that must be
reproduced (`src/Game.cpp:264-275` passes `radius*radius` as `n` and `625`/
`256` as `n4`; test at `src/Render.cpp:1122`).

### 3.5 Reject, not slide

A move is committed only when the trace yields **zero** hits
(`src/MovementController.cpp:333`). There is no partial move, no
slide-along-wall, no re-attempt along the remaining fraction — the whole
64-unit step is accepted or discarded. Corners are therefore handled purely by
the capsule geometry: with radius 16 (< half tile), parallel wall walking
works until the swept capsule touches a perpendicular line, then the step is
plainly refused.

--------------------------------------------------------------------------------
## 4. Step semantics summary

* One step = exactly one 64-unit tile, axis-aligned (§1.2); destinations are
  always tile centers ± k*64, so committed positions stay **on-grid**
  (`x % 64 == 32`). Sub-tile positions exist only transiently in the rendered
  view interpolation (`src/MovementController.cpp:416-440`), never as
  `destX/destY`. Knockback steps are also 64-unit multiples
  (`src/MovementController.cpp:399`).
* The destination is not tested as a point: the **whole sweep segment**
  (previous tile center → new tile center) is traced as a capsule of
  radius 16 against wall lines, and simultaneously as a moving circle vs
  entity circles/segments (`src/MovementController.cpp:332`,
  `src/Game.cpp:216-311`).
* All-or-nothing commit; blocked steps leave position untouched (§3.5).
  The only post-block side effects: `advanceTurn` in automap
  (`src/MovementController.cpp:351-353`) and clearing `knockbackDist` on an
  aborted knockback step (`:355-357`).

--------------------------------------------------------------------------------
## 5. Doors

Fully documented in [`doors.md`](doors.md) §1–§6; collision-relevant facts,
cross-referenced (do not duplicate here):

* Player move mask 13501 contains `ET_DOOR` (bit 5), so any **linked** door
  blocks; the door entity is traced like any oriented sprite — a ±32-unit
  segment through its current (possibly animating) sprite position
  (`src/Game.cpp:249-268`); see `docs/original-code/doors.md` §6.
* Solidity timeline: closed = linked = solid; stays solid through the whole
  750 ms open animation (`LS_FLAG_ENT_NORELINK` prevents relinking but the
  entity remains in `entityDb`); **unlinked (passable) at open-animation end**
  (`src/Game.cpp:3131`); re-linked (solid) immediately at close start
  (`src/Game.cpp:1079`). Details: `doors.md` §3, §6.
* Sliding panels translate ±32 along their wall axis during the open lerp, so
  the blocking segment slides with the panel; red/blue slip doors
  (`parm & 1`) never translate — their segment stays centered until unlink
  (slide rule `src/Game.cpp:1097-1126`, suppressed by `parm & 1`;
  `doors.md` §3).
* Door auto-close occupancy checks are tile-granular `findMapEntity` lookups,
  not traces (`src/Game.cpp:1215-1236`; `doors.md` §5).

--------------------------------------------------------------------------------
## 6. Heights

* `heightMap` is 1024 bytes (one per tile), value `<< 3` = floor height in
  units; `getHeight(x,y)` looks up by tile, no interpolation
  (`src/Render.cpp:2444-2451`; allocation/loading
  `src/LoadingManager.cpp:423,485`).
* **Collision is strictly 2-D.** `attemptMove` accepts or rejects on the XY
  trace alone and then unconditionally sets
  `destZ = 36 + getHeight(destX, destY)` — there is **no max-step-up, no
  cliff rejection, no height comparison of any kind** in the movement accept
  path (`src/MovementController.cpp:341-344`). A one-tile "step" onto a much
  higher/lower tile is legal; only the camera Z animates there
  (`zStep`, `:344`, applied `:442-453`).
* Eye height constant: `36` above the floor (same formula everywhere:
  spawn `src/Game.cpp:967`, reload `src/Player.cpp:1706,2546`).
* Stairs *look*: after committing a move or turn, `startRotation` probes
  384 units ahead (`viewStepValues[dir] * 384 >> 6`, i.e. dir unit × 384) with
  mask 4133, radius 2; if WORLD/SPRITEWALL is hit within ≤ 36 units the pitch
  target stays level, otherwise it aims at `getHeight(nextTile)+36`;
  `destPitch = ((targetZ - destZ) << 7) / dist` clamped to ±64
  (`src/MovementController.cpp:226-282`).
* Knockback raises `destZ += 12` per tile traveled, resnapping to
  `36 + getHeight` at the end (`:171-177`).
* The optional Z filter inside `Game::trace` (`|zHit - zEnt| < 32+radius`,
  `src/Game.cpp:277-283`) applies only to full 3-D traces (weapon fire,
  radius 2) — never to walking.

--------------------------------------------------------------------------------
## 7. Monsters / NPCs (brief)

Same `Game::trace` primitive, different masks/radii:

* Path search `Entity::calcPath`: per candidate neighbor edge
  `trace(center → center, mask 15535 CONTENTS_MONSTERSOLID, radius 16)`
  (`src/Entity.cpp:1004`, mask set at `:1078`); tile-level pre-filter refuses
  tiles holding `ET_ENV_DAMAGE` (`findMapEntity(...,256)`, `:955`) and
  visited tiles; recursion into a blocked edge allowed only when the blocking
  type is in `interactClipMask` (default `32` = doors only, `:1011-1025,1048`).
* Actual step commit: single-tile trace with `interactClipMask` (doors) at
  **radius 25**; clear → unlink/relink onto new tile and lerp 275 ms (500 ms
  for subtypes 13/14-with-parm); blocked by a door → open it via
  `performDoorEvent(0, door, 2)` instead of moving
  (`src/Entity.cpp:1091-1111`).
* Knockback: player uses mask 13501, other entities 15535; travel distance =
  requested tiles × `traceFracs[0]/16384`
  (`src/Entity.cpp:1220-1244,1270-1278`).
* Combat LOS: radius 1, mask 4129 (`src/Combat.cpp:1066-1093`).

--------------------------------------------------------------------------------
## 8. Special modes affecting collision (brief)

* **noclip cheat**: mask forced to 0 — nothing blocks, and the world trace is
  skipped (`mask & 1 == 0`) (`src/MovementController.cpp:326`);
  monster activation also early-outs under noclip (`src/Game.cpp:767-769`).
* **ST_AUTOMAP**: identical handler and trace; differences — view snaps
  instantly to dest (`src/MovementController.cpp:492-498`), a *blocked* move
  still burns a turn (`:351-353`), and `finishMovement` snaps
  monsters/lerpSprites instead of waiting (`:185-195`).
* **Sniper zoom**: movement keys become aim keys; only FIRE falls through to
  the playing handler (`src/ZoomController.cpp:96-107,152-155`); the
  interpolation gate is bypassed while zoomed (`src/PlayingInputHandler.cpp:25`).
* **Familiar (sentry-bot control)**: reuses `attemptMove`
  (`src/Player.cpp:2095`); tiles with `mapFlags & 0x10` are impassable except
  the player's own tile (`src/MovementController.cpp:322-325`); returning to
  the player tile is special-cased against the spawned player-copy entity
  (`:333-336`).
* **renderOnly** (cutscene/photo modes): destination accepted without any
  trace (`:316-320`).
* **Mini-games** reuse `attemptMove` for their back-out step
  (`src/MiniGameManager.cpp:121`).

--------------------------------------------------------------------------------
## Port checklist

The rewrite must reproduce, in order:

1. **Gate** step input while `view != dest` (position or angle) unless
   zoomed; hard-drop during knockback/map-change; snap running animations
   before processing (`src/PlayingInputHandler.cpp:25-61`).
2. **Steps are 64 units, axis-aligned** (table `src/Canvas.h:108`, facing =
   multiples of 256/1024, turn = ±256 per press
   `src/PlayingInputHandler.cpp:112-121`). Committed positions stay at
   `tile*64 + 32`.
3. Before tracing, run leave-tile script events with flag
   `2 | directionBit` and honor `EV_ABORT_MOVE`
   (`src/MovementController.cpp:327-331`; `src/Game.cpp:974-1014`).
4. **One swept capsule trace**: from `viewX/viewY` to candidate tile center,
   skip-self, mask `13501` (0 if noclip), **radius 16**
   (`src/MovementController.cpp:326,332`).
5. Trace mechanics: tile-broadphase over `entityDb` (`bbox` = endpoints ±
   radius, clamp `[0,2047]`); candidates filtered by `mask & (1<<eType)`;
   oriented sprites (`info & 0xF000000`) → segment `pos ± 32` along
   N/S⇒horizontal (`0x3000000`) vs E/W⇒vertical axis through **current sprite
   position**; others → circles r=25 (r=16 for env-damage)
   (`src/Game.cpp:216-289`).
6. Hit fractions: `16384 = miss`; hit = `(t16.16 >> 2) - 1`; sort ascending;
   commit only if zero hits (`src/Game.cpp:265,312-326`;
   `src/MovementController.cpp:333`).
7. Circle overlap uses `d² < r² + R²` (**881** for player vs entity, not
   1681) — reproduce the approximation exactly (`src/Render.cpp:1122`).
8. Segment overlap = integer clamped segment-segment closest-point solve,
   distance² < 256 for walking (`src/Render.cpp:1128-1210`).
9. World lines from mapXX.bin: 4-bit packed flags (use low **3** bits),
   byte coords `<< 3`; BSP leaf descent (classify with `P << 4`); flag rules:
   4/6 never block, 5 blocks only masks with bit 0x10 or 0x800, 7 one-sided
   via `cross(L0-P0, L1-L0) > 0`, 0–3 always
   (`src/Render.cpp:1212-1283`, `src/LoadingManager.cpp:419-421,481-483`).
10. **No sliding / no partial steps**: blocked = reject entirely
    (`src/MovementController.cpp:333-357`).
11. On commit: `destZ = 36 + getHeight(tile)` regardless of height delta
    (collision is 2-D; no step-up limit) (`:343`,
    `src/Render.cpp:2444-2451`); relink the player entity onto the new tile
    (`src/Player.cpp:1425-1446`); run stair-pitch probe (384-unit ray, mask
    4133, radius 2, ≤36 ⇒ level) (`src/MovementController.cpp:226-282`).
12. On arrival: enter-tile events (`eventFlags[1]`), `touchTile`, automap
    uncover, `advanceTurn` (`src/MovementController.cpp:160-196`).
13. Doors: keep the solidity timeline from `doors.md` §6 (solid during open,
    passable at open end `src/Game.cpp:3131`, solid at close start
    `src/Game.cpp:1079`); the blocking segment tracks the animated panel.
14. Automap mode: same trace; blocked move still calls `advanceTurn`
    (`src/MovementController.cpp:351-353`); instant view snapping
    (`:492-498`).
15. Monsters later: path edges mask 15535/radius 16, commit trace
    doors-only/radius 25, door-open-on-block (`src/Entity.cpp:1004,1091-1111`).
