# Spec 2026-08-23 — Fix "doors work incorrectly" + "sprites slightly shifted"

Status: READY FOR IMPLEMENTATION
Scope sources: `docs/original-code/doors.md` (R1–R15), `docs/original-code/sprite-placement.md` (#1–#13), `PLAN.md` §"Формат полигонов" / §"GL-путь legacy".
Targets: `new_src/domain/game/Game.{h,cpp}`, `new_src/render/World3D.{h,cpp}`, `new_src/core/Main.cpp`.
No new files ⇒ no CMake reconfigure needed (GLOB picks up nothing new; reconfigure is harmless).

Constraints honored: no writes to `src/` or `Doom 2 RPG Java/`; painter's algorithm kept (no depth buffer); integer shifts preserved where the original uses them (the only floats are the existing `<<4 → /16384` vertex conversions already in `World3D.cpp`); canvas 480×320 letterbox untouched; single-threaded GL loop, no threading changes.

---

## 0. Data ownership & lifetime (unchanged by this spec)

* `MapData::mapSprites` (10 fields × numSprites, stride layout) and `MapData::mapSpriteInfo` remain owned by `MapData`. `Game` mutates them in place during door animation (`S_X=0`, `S_Y=1`, `S_Z=2`, `S_SCALEFACTOR=8` — stride `numSprites`, as already used at `new_src/domain/game/Game.cpp:126-128,352-357`).
* The renderer consumes them read-only per frame. Door state that the renderer needs travels exclusively through `mapSprites[S_SCALEFACTOR]` and the `mapSpriteInfo` bits (`0x80000000` DOORLERP, frame bits 8–15) — exactly like the original (`src/Render.cpp:1507-1517`).
* New per-frame sort-bias array (B5): stack `std::vector<int>` built in `Main.cpp`, passed as `const int*` to `World3D::drawBSP`, borrowed for the duration of the call, never retained.

---

## 1. Conflicts & discoveries found while verifying docs vs code

These must be read before implementing:

* **C1 — doc magnitude error (formula is authoritative).** `sprite-placement.md` #1 claims the missing camera pull-back displaces the scene "~10 map units". From the actual formula `viewX -= 160*sinTable[angle+256] >> 16` with `sinTable` max ≈ 16384 (14.14; verified via `canvas->viewStepX = viewCos * 64 >> 16 == ±64`, `src/PlayingInputHandler.cpp:41-42`), the pull-back is ≤ 40 render units = **≤ 2.5 map units**. Implement the exact formula; expect a small uniform framing change, not a large one.
* **C2 — gap formula notation.** `docs/research/2026-08-23-doors.md` line 54 says gap `(origH - lerpedH)`; `doors.md` R12 says `(origHalfH - lerpedHalfH)<<1`. Code is authoritative: `z + n38*n41 + j*(n37-n36 << 1)` where `n37/n36` are HALF-heights (`src/Render.cpp:604,614`). Both docs describe the same thing; use the code form.
* **C3 — undocumented legacy guard, intentionally not ported.** The entire wall-decal body in `src/Render.cpp:538-662` runs only when the texture is raw (`textureBaseSize == sWidth*tHeight`) OR `tileNum ∈ {155,156,157}`; RLE-textured wall decals are silently skipped by the original. Not mentioned in either doc. All practical wall decals (doors, portal eyes) have raw textures. **Do not port this guard**; noted for the record.
* **C4 — rewrite dead code removed by this spec.** `Game::useDoorNear` declared but never defined (`new_src/domain/game/Game.h:35`; README known-spot #3), `Game::doorScale` defined but never called (`Game.h:38`, `Game.cpp:247-257`), `Game::doorIsOpen` becomes unused after A4 (`Game.cpp:186-194`). All three are deleted.
* **C5 — known remaining deviation (out of scope).** Tie-break order among equal sort keys differs: rewrite stable ascending sprite index vs legacy head-insertion (newest drawn first among equals, `src/Render.cpp:880-893`). Not fixed here.
* **C6 — door range nuance.** Gameplay gating `b3 = tileNum >= 271 && tileNum < 281` (`src/Game.cpp:1058`) excludes 281 although `TILENUM_LAST_DOOR = 281` (`src/Enums.h:842`); the rewrite's loader uses an inclusive `[271,281]` test (`Game.cpp:61`). Loader behavior stays (def-gated, 279–281 have no defs anyway); all gameplay gating added below uses the exact `b3` half-open range.
* **C7 — legacy size-selection gate for sockets.** Portal socket 157 participates in the special `n24/n25` size selection (`src/Render.cpp:538-539`) and the rewrite always computes sizes — equivalent for sockets. B2 therefore only fixes the Z range.
* **C8 — `swapXY` is a no-op in the rewrite.** Legacy sets `tinyGL->swapXY` for wall quads (`src/Render.cpp:620,636`) vs flats (`:659`); the rewrite disables face culling entirely (`new_src/render/World3D.cpp:290`) so there is nothing to swap. Ignore it.

---

## 2. SCOPE A — Doors

All edits in `new_src/domain/game/Game.{h,cpp}` unless stated otherwise.

### A1 + A2 — Renderer doorLerp split (R12, R13)

File: `new_src/render/World3D.cpp`, function `World3D::drawSprite`, wall-decal branch (`:664-743`).

Today the branch collapses only width with no UV compensation (`:710-713`) and draws one quad (`:728-742`). Restructure to the legacy order (`src/Render.cpp:559-661`):

1. Keep orientation `n23` (`:668-673`), sizes `n24/n25` with the 256×256 special case (`:694-697`), z corrections (`:700-705`).
2. Add the second axis index next to `n29`:
   ```cpp
   int n29 = ((n23 + 2) & 0x7) << 1;
   int n28 = ((n23 + 4) & 0x7) << 1;   // NEW: perpendicular axis for FLAT (src/Render.cpp:560)
   int n30 = n27 << 4;                  // width extent  (<<4 render units)
   int n31 = n26 << 4;                  // height extent (<<4 render units)
   ```
3. Move the bounds-derived UV computation + flips (`:720-725`) to BEFORE the doorLerp block (legacy computes `n13..n16` at `src/Render.cpp:443-448`, flips at `:576-583`, lerp at `:585-599`).
4. Replace the current `if (doorLerp) { n30 = ...; }` (`:710-713`) with the exact two-case block (`src/Render.cpp:585-599`), using the already-existing `realScale` (`:691`):
   ```cpp
   int n32 = n31;                                   // FULL height extent saved before collapse
   if (doorLerp) {
       if (tileNum >= 271 && tileNum <= 274) {      // red/blue slip doors: VERTICAL collapse (R12)
           int n33 = t16;                            // src/Render.cpp:588
           n31 = (realScale * n31) / 65536;          // height extent *= lerp fraction   :589
           t16 = (realScale * t16) / 65536;          // v-window shrinks                 :590
           t15 += (n33 - t16) >> 1;                  // v-window RE-CENTERED, half delta :591
       } else {                                      // slide doors / other walls: WIDTH collapse (R13/A2)
           int n34 = s14;                            // src/Render.cpp:594
           n30 = (realScale * n30) / 65536;          // width extent *= lerp fraction   :595
           s14 = (realScale * s14) / 65536;          // u-window shrinks                :596
           s13 += n34 - s14;                         // u-window shifted by FULL delta  :597
       }
   }
   ```
   Precedence note: `(n33 - t16) >> 1` needs explicit parens exactly as written (C++ additive binds tighter than shift — same trap the original comments on in `doors.md` §4).
5. Draw selection, replacing the single-quad tail (`:727-742`), in legacy order:
   * **Slip-door branch** first, triggered by TILE RANGE ALONE (even closed, even without DOORLERP — `src/Render.cpp:601`), tiles `271..274`: two stacked half-quads:
     ```cpp
     // src/Render.cpp:601-623. Work on LOCAL copies of z/t15 (legacy mutates them per j).
     int n35 = t16 >> 1;      // quarter v-window            :602
     int n36 = n31 >> 1;      // lerped half-height          :603
     int n37 = n32 >> 1;      // ORIGINAL half-height        :604
     int zLocal = z; int t15Local = t15;
     for (int j = 0; j < 2; ++j) {
         Vertex quad[4];
         for (int k = 0; k < 4; ++k) {
             int n38 = (k & 2) >> 1;                    // row
             int n39 = (k & 1) ^ n38 ^ 1;               // col (1 = right)
             int n40 = (n39 * 2 - 1) * n30;             // along-wall offset
             int n41 = (n38 * 2 - 1) * n36;             // vertical offset
             float px = (float)(x << 4) + (float)(kViewStepValues[n29 + 0] >> 6) * n40;
             float py = (float)(y << 4) + (float)(kViewStepValues[n29 + 1] >> 6) * n40;
             float pz = (float)zLocal + (float)(n38 * n41)
                      + (float)(j * ((n37 - n36) << 1)); // growing gap (C2)
             float s  = (float)(s13 + n39 * s14) * (1.f / 1024.f);
             float t  = (float)(t15Local + n38 * n35) * (1.f / 1024.f);
             quad[k] = { px * k1, py * k1, pz * k1, s, t };
         }
         emitTriFan(quad);                       // existing 0,1,2 / 0,2,3 emission (:740-742)
         zLocal += n36;                          // src/Render.cpp:618
         t15Local += n35;                        // src/Render.cpp:619
     }
     ```
     (`emitTriFan` is shorthand for the existing inline pattern — no new helper required:
     `Vertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };`
     then the kMaxVerts flush check + `vertices_.insert`, exactly as at `new_src/render/World3D.cpp:660-663,740-742`.)
     Closed (`realScale == 65536`): `n36 == n37`, gap 0 → seamless full door made of two half-quads. Fully open (`realScale == 0`, geometry forced full-size via `scaleFactor = 65536` at `:693` ≡ `src/Render.cpp:430-432`): degenerate invisible quads.
   * **FLAT plane branch** (`info & 0x20000000`) — see B4 below.
   * **Wall single quad**: keep the existing loop (`:729-739`) unchanged except that `n30/n31/s13/s14/t15/t16` now carry the lerped values from step 4.
6. `swapXY` ignored (C8).

Acceptance A1: a red/blue door (tiles 271–274) opening splits top/bottom — bottom half sinks into the floor, top half rises into the ceiling, full width preserved, a horizontal gap line grows between the halves; texture windows shrink symmetrically (v-recenter). Closing plays the same in reverse and the split effect persists through the whole close (see A3).
Acceptance A2: a plain sliding door slides ±32 sideways into the jamb while its texture strip appears glued to the jamb (right edge of the texture window pinned), not compressing/swimming.

### A3 — DOORLERP bit lifetime (R7)

File: `new_src/domain/game/Game.cpp`, `performDoorEvent` (`:153-154`) and `updateDoors` (`:358-369`).

* At every animation start (open AND close): `map_->mapSpriteInfo[sprite] |= 0x80000000;` (`src/Game.cpp:1089`). Delete the `else ... &= ~0x80000000;` clear-at-close-start.
* At close completion ONLY (in `updateDoors`, closing path): `map_->mapSpriteInfo[si] &= 0x7FFFFFFF;` (`src/Game.cpp:3125`).

Acceptance A3: closing a door animates as a door (split/slide via the renderer effect above) rather than collapsing on both axes into the floor; after the close completes the bit is gone (door renders normally afterwards).

### A4 — Solidity timeline (R8)

Files: `Game.cpp` `performDoorEvent`, `updateDoors`, `canPlayerStep`.

1. Close start relink: in `performDoorEvent`, right before slot allocation (mirrors `src/Game.cpp:1076-1081` order):
   ```cpp
   if (n == 1 && isOpenState && family) {
       linkEntity(door, door->linkIndex % 32, door->linkIndex / 32); // solid AGAIN IMMEDIATELY (src/Game.cpp:1079)
   }
   ```
   (`isOpenState` == "currently unlinked/open", `family` == b3 — both introduced in A8 edits below.)
2. Remove the close-end relink in `updateDoors` (`:363-367`). Open-end unlink stays (`:361-362`, ≡ `src/Game.cpp:3131`).
3. `canPlayerStep` door test becomes linked-state based (≡ legacy trace hitting only linked doors):
   ```cpp
   if (e->isDoor() && (e->info & Entity::kInfoLinked)) return false; // solid while linked (R8)
   ```
   Delete `doorIsOpen` (declaration `Game.h:85`, definition `Game.cpp:186-194`).

Timeline result: closed = solid; whole open animation = solid; open end = passable; close start = solid immediately.

Documented simplification: legacy traces a capsule against the door's ±32 wall segment through its ANIMATED position (`src/Game.cpp:249-268`), i.e. the blocking edge slides with the panel; the rewrite is tile-granular (whole target tile blocked while linked). No behavioral timeline difference, only granularity.

Acceptance A4: player cannot step through the doorway while the open animation runs; can step the moment it finishes; a closing door blocks the tile instantly when its close starts.

### A5 — Use trigger (R2)

Files: `Game.h`, `Game.cpp`, `Main.cpp`.

1. In `Game.h`: delete `useDoorNear` (`:35`, C4) and replace `openDoorAt` (`:52`) with:
   ```cpp
   // Legacy interact: trace along the view ray, first ET_DOOR hit within
   // Chebyshev distance² <= tileDistances[0] = 4096 (1 tile)
   // (src/PlayingInputHandler.cpp:445-459, src/Combat.cpp:42,
   // src/Entity.cpp:1155-1158). Trace-free simplification: candidates are
   // LINKED doors on the player's tile and on the adjacent tile in the
   // facing direction (both satisfy dist² <= 4096 by construction); own
   // tile wins (ray fraction ~0). Locked refusals handled inside
   // performDoorEvent. Returns the attempted entity or nullptr.
   Entity* useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY);
   ```
2. In `Game.cpp`: delete `openDoorAt` (`:78-96`), add:
   ```cpp
   Entity* Game::useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY) {
       const int tiles[2][2] = {
           { px >> 6, py >> 6 },
           { (px + stepX) >> 6, (py + stepY) >> 6 },
       };
       for (auto& t : tiles) {
           for (Entity* e = findMapEntity(t[0], t[1]); e; e = e->nextOnTile) {
               if (!e->isDoor()) continue;
               if (!(e->info & Entity::kInfoLinked)) continue; // unlinked (open) doors are not traceable
               performDoorEvent(0, e);
               return e;
           }
       }
       return nullptr;
   }
   ```
   Determinism note: multiple doors on one tile resolve by tile-list insertion order (legacy trace order for coincident segments is equally arbitrary). The linked-only filter matches legacy because traces walk `entityDb`, which holds only linked entities.
3. In `Main.cpp` (`:366-373`):
   ```cpp
   if (wantE) {
       wantE = false;
       game.useDoorFacing(g_map, player.viewX, player.viewY, player.viewStepX, player.viewStepY);
       game.advanceTurnDoors();   // legacy opens then advanceTurn (src/PlayingInputHandler.cpp:451-452)
   }
   ```

Out of scope (stated): HUD message 44 on locked doors and the keycard unlock path (`setLineLocked` / EV_ITEM_COUNT / EV_DOOROP, R3) — Phase 5.

Acceptance A5: E opens the door being faced within 1 tile; a lateral/nearer-but-not-faced door no longer triggers; E facing nothing does nothing.

### A6 — srcScale on re-open (R6)

File: `Game.cpp`, `performDoorEvent` (`:146`). Replace
`slot->startScale = (n == 1) ? curScale : 64;`
with
`slot->startScale = curScale; // always CURRENT S_SCALEFACTOR (src/Game.cpp:1095)`
(`curScale` is already read from `mapSprites[S_SCALEFACTOR]` at `:128`; X/Y already resume from current values `:126-127`.)

Acceptance A6: pressing E mid-close reverses the animation smoothly from its current position/scale — no pop to fully-closed.

### A7 — Texture frame 1 while open/animating (R14)

Files: `Game.cpp`, `World3D.cpp` (texture preload).

1. In `performDoorEvent`, after the open-door registration, gated on family and open only (`src/Game.cpp:1150-1152`):
   ```cpp
   if (n == 0 && family) {
       map_->mapSpriteInfo[sprite] = (map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x100;
   }
   ```
2. In `updateDoors` close-completion block add `map_->mapSpriteInfo[si] &= 0xFFFF00FF;` (frame back to 0, `src/Game.cpp:3122`).
3. Required support edit — `World3D::uploadMapTextures`, sprite preload loop (`:244-283`): today only the load-time frame (`lo + frame`) is uploaded, so the frame-1 door texture would be missing and open doors would vanish. Upload the WHOLE mapping range for each encountered sprite tileNum:
   ```cpp
   ...
   int lo = m.mappings[tileNum];
   int hi = (tileNum + 1 < (int)m.mappings.size()) ? m.mappings[tileNum + 1] : lo + 1;
   if (lo < 0) continue;
   for (int mediaId = lo; mediaId < hi && mediaId < MediaMappings::kMaxMedia; ++mediaId) {
       if (spriteTexByMedia_.count(mediaId)) continue;
       ... existing decode/upload body, unchanged ...
   }
   ```
   (Ranges come from `newMappings.bin`; typically 1–8 frames per tile. Also future-proofs AUTO_ANIMATE frames.)
   The renderer already resolves `mediaId = lo + frame` from bits 8–15 (`World3D.cpp:517,550`) — no renderer change needed.

Acceptance A7: while a door is open/animating it shows its second media frame (if the art differs; minimally: it does NOT disappear), restored to the closed frame when fully closed.

### A8 — Auto-close occupancy + registration semantics (R9)

File: `Game.cpp`, `canCloseDoor` (`:198-229`), `advanceTurnDoors` (`:233-245`), `performDoorEvent`.

1. Introduce the exact b3 predicate (file-local static in `Game.cpp`):
   ```cpp
   // Legacy b3: effective tileNum in [271,281) (src/Game.cpp:1058). Excludes 281 (C6).
   static bool doorFamilyTile(int tileNum) { return tileNum >= 271 && tileNum < 281; }
   ```
   Use it in `performDoorEvent` for the early-outs and registration exactly like legacy:
   * `n == 0 && unlinked && family` → `registerOpenDoor(door); return false;` (`src/Game.cpp:1062-1065`)
   * `n == 1 && linked` → `return false;` (`src/Game.cpp:1066-1068`)
   * monster-blocks-close hook comment at the close-start site (`src/Game.cpp:1070-1075`, R10 — no monsters yet).
   * `registerOpenDoor` on open start / `unregisterOpenDoor` on close start (already present `:157-158`, ≡ `src/Game.cpp:1141,1135`).
2. Replace the radius-32 circle occupancy test in `canCloseDoor` (`:206-228`) with the tile-granular legacy rule (`src/Game.cpp:1215-1236`, `src/Game.cpp:741-743`):
   ```cpp
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
   ```
   Delete the `dx*dx+dy*dy <= 32*32` circle test.
3. `advanceTurnDoors`: remove the animating-skip loop (`:237-240`). It is redundant once A3/A4 land: a door still opening is LINKED, so `performDoorEvent(1, …)` hits the `n == 1 && linked` early-out exactly like legacy (`src/Game.cpp:1271-1278` → `:1066-1068`).

Documented omission (per task): snap-if-offscreen (`n2 = 2` + `cullBoundingBox`, `src/Game.cpp:1153-1155`) is NOT portable without `cullBoundingBox` — doors always animate; `performDoorEvent` gains a comment saying so.

Acceptance A8: walking away from an open door and taking any turn-consuming action closes it automatically once you are beyond the doorway; standing ON the door tile or on either neighbor tile ALONG THE PASSAGE AXIS prevents auto-close (no more radius ghost-blocking).

---

## 3. SCOPE B — Sprite placement/rendering

All edits in `new_src/render/World3D.cpp` / `.h` / `Main.cpp`.

### B1 — Camera pull-back nudge (sprite-placement.md #1)

File: `Main.cpp`, per-frame camera setup (`:380-382`). Apply AFTER the `+8` sub-unit bias (which mirrors `src/Canvas.cpp:1344`), BEFORE `camera.setView` (mirroring `src/Render.cpp:2279-2282` → `:2299`):

```cpp
// Render-view pull-back (src/Render.cpp:2279-2282). Applied to the RENDER
// camera only; gameplay/collision keep using player.viewX/viewY.
const int32_t* sinTbl = tables.sinTable.data();
int yaw = player.viewAngle & 0x3FF;
int viewSin = sinTbl[yaw];
int viewCos = sinTbl[(yaw + 256) & 0x3FF];
int rvx = (player.viewX << 4) + 8 - (160 * viewCos >> 16);
int rvy = (player.viewY << 4) + 8 + (160 * viewSin >> 16);
camera.setView(rvx, rvy, (player.viewZ << 4) + 8, player.viewAngle, 0, 0, 290,
    (290 << 14) / ((480 << 14) / 320));
```

* Placement decision: lives at the `setView` call site in `Main.cpp`, NOT inside `Camera3D` — keeps `Camera3D` a pure math class and matches the legacy layering (`Canvas::renderScene` → `Render::render` nudge → `tinyGL->setView`).
* Gameplay untouched: `canPlayerStep` (`Main.cpp:350,359`) and `game.setPlayerPos` (`:387`) keep reading `player.viewX/viewY`.
* BSP traversal consistency: `walkNode` uses `camera.viewX()/viewY()` (`World3D.cpp:815`), which are now the nudged values — matching legacy, where `renderBSP` walks with the nudged `this->viewX`.
* The one-shot spawn camera (`Main.cpp:219-228`) stays unnudged: it is overwritten by the per-frame `setView` before the first draw; leave as-is.
* Magnitude expectation per C1: ≤ 2.5 map units.

Acceptance B1: scene framing shifts uniformly (everything slightly farther/smaller when facing a direction, by up to ~2.5 map units); no sprite-vs-wall misalignment introduced (walls and sprites move together).

### B2 — Portal-eye Z range (#2)

File: `World3D.cpp` `:717`. Change `tileNum >= 155 && tileNum <= 157` to `tileNum >= 155 && tileNum <= 156` (`TILENUM_CLOSED_PORTAL_EYE`/`TILENUM_EYE_PORTAL`, `src/Enums.h:773-774`; subtraction at `src/Render.cpp:521-523`). Socket 157 keeps the decal path without the drop (`src/Render.cpp:538-539`).

Acceptance B2: portal sockets sit 8 map units higher than before (flush with their wall art); eyes 155/156 unchanged.

### B3 — Billboard UV flips + GL-path s assignment (#5)

File: `World3D.cpp`, billboard branch corner UVs (`:649-659`).

Verified current behavior: for RLE frames the rewrite computes `s = n22 * crop`, i.e. `s = full` at the RIGHT corners — equal to legacy-GL-effective for flag-less sprites (the XOR-default branch of `src/GLES.cpp:520-525` also lands `s=1024` at right corners), and `t = (1-n21)*crop` matches likewise. So the only visible defect is that sprites actually CARRYING flip bits `0x20000`/`0x40000` never mirror. Exact replacement for the RLE (`!useBounds`) path:

```cpp
// GL-path billboard UV override (src/GLES.cpp:515-542): s/t derive from the
// corner index; flip tests XOR 0x60000 so the default (no flags) takes the
// "flipped" branch, which yields the NORMAL orientation. Integer math first,
// then a single float conversion (as legacy: (s*176)/sWidth then /1024).
int sW = (((info ^ 0x60000) & 0x20000) != 0) ? n22 : (n22 ^ 1); // 1 = left column gets full s
int tW = (((info ^ 0x60000) & 0x40000) != 0) ? (n21 ^ 1) : n21;
float s = (float)(((sW * 1024) * 176) / sWidth) * (1.f / 1024.f);
float t = (float)(((tW * 1024) * 176) / tHeight) * (1.f / 1024.f);
```

Corner-index sanity (must hold, already true in the loop at `:650-652`): `n22 = 1` on right corners (ci 0,3), `0` on left; `n21 = 0` bottom row, `1` top row. For flags = 0 this reproduces today's pixels exactly (`sW == n22`, `tW == 1-n21`), so the change is riskless for unaffected sprites. `sWidth/tHeight` are the RLE-branch locals (`:584-587`).

Bounds-based billboards (`useBounds`, i.e. TILE flag or raw texture): KEEP the existing math `s = (n13 + n22*n14)/1024`, `t = (1-(n15+n21*n16)/1024)` — the legacy ClipQuad path keeps those window UVs and applies NO flips there (`src/Render.cpp:479-493`).

Acceptance B3: sprites with flip bits mirror horizontally/vertically as in the original; all other billboards pixel-identical to the previous build.

### B4 — FLAT sprite plane branch (#3)

File: `World3D.cpp`, wall-decal branch draw selection (inserted between the slip-door branch and the wall quad from A1):

```cpp
} else if (info & 0x20000000) {
    // FLAT plane quad (src/Render.cpp:639-661): horizontal quad using BOTH
    // viewStepValues axes; swapXY=false irrelevant here (C8). Lava scroll
    // omitted (out of scope).
    Vertex quad[4];
    for (int k = 0; k < 4; ++k) {
        int n46 = (k & 2) >> 1;
        int n47 = (k & 1) ^ n46 ^ 1;
        int n48 = (n47 * 2 - 1) * n30;                     // along-axis ±(w<<4)
        int n49 = ((n46 * 2 - 1) * n31) >> 1;              // perpendicular ±(h<<4 >> 1)
        float px = (float)(x << 4)
                 + (float)(kViewStepValues[n29 + 0] >> 6) * n48
                 + (float)(kViewStepValues[n28 + 0] >> 6) * n49;
        float py = (float)(y << 4)
                 + (float)(kViewStepValues[n29 + 1] >> 6) * n48
                 + (float)(kViewStepValues[n28 + 1] >> 6) * n49;
        float pz = (float)z;
        float s = (float)(s13 + n47 * s14) * (1.f / 1024.f);
        float t = (float)(t15 + n46 * t16) * (1.f / 1024.f);
        quad[k] = { px * k1, py * k1, pz * k1, s, t };
    }
    emitTriFan(quad);
}
```

Note `n28` comes from A1 step 2; the FLAT `z -= 512` already exists in the shared z correction (`:703-704` ≡ `src/Render.cpp:556-557`). DoorLerp interacts correctly: flats fall into the WIDTH-collapse else-branch of A1 step 4 (they can't be tiles 271–274).

Acceptance B4: floor decals/scorch-type sprites (info & 0x20000000) lie flat on the ground instead of standing up as vertical quads.

### B5 — Sort keys (#6)

Files: `World3D.h`, `World3D.cpp` `drawBSP` (`:811-881`), `Main.cpp`.

1. Signature (`World3D.h:63`):
   ```cpp
   void drawBSP(const MapData& map, const MediaLoader& media, const Camera3D& camera,
                const int* spriteSortBias = nullptr); // per-sprite extra bias (+1/-1 hook), may be null
   ```
2. In the leaf-sprite depth loop (`:855-867`), sort by HEIGHT-SNAPPED Z (post-snap, in map units — legacy sorts the stored `S_Z` after `postProcessSprites` baked terrain height in, `src/Render.cpp:2459-2467,846`):
   ```cpp
   int zsnapped = z;
   if (!map.heightMap.empty()) {
       int hx = x & 0x7FF, hy = y & 0x7FF;
       zsnapped += map.heightMap[((hy >> 6) * 32 + (hx >> 6))] << 3;
       if (i >= map.numNormalSprites) zsnapped -= 32;
   }
   int d = (x * mvp[2] + y * mvp[6] + zsnapped * mvp[10] >> 14) + mvp[14];
   int tn = info & 0xFF;
   if (info & 0x10000000) d = (int)0x7FFFFFFF;               // DECAL bias (src/Render.cpp:839-841) [added for completeness]
   else if (info & 0x400000) d += 6;                         // exists (:863)
   ```
   Keep the existing water `d = (int)0x80000000` line (`:864`, ≡ `src/Render.cpp:850-852`) and orient `+5` (`:865`) as further else-if arms, then extend the final ELSE faithfully (`src/Render.cpp:856-874`; strict else-if semantics preserved):
   ```cpp
   else {
       bool biased = false;
       if (spriteSortBias && spriteSortBias[i] != 0) { d += spriteSortBias[i]; biased = true; }
       if (!biased) {
           if ((tn >= 240 && tn <= 244) || tn == 255) d -= 3;   // src/Render.cpp:863-865
           else if (tn >= 137 && tn <= 139) d += 2;             // :866-868
           else if (tn == 152) d += 5;                          // crate, :869-871
           else if (tn == 239) d -= 3;                          // :872-874
       }
   }
   ```
   (The `biased` guard reproduces the original else-if chain: an entity-biased sprite skips the tileNum biases. Today `spriteSortBias` is all zeros, so behavior equals pure tile biases.)
3. Hook wiring in `Main.cpp`, immediately before the draw call (`:393-395`):
   ```cpp
   std::vector<int> spriteSortBias(g_map.numSprites, 0);
   for (const auto& ent : game.entities()) {
       int si = ent.getSprite();
       if (!ent.def || si < 0 || si >= g_map.numSprites) continue;
       if (ent.info & 0x1010000) spriteSortBias[si] = +1;                    // src/Render.cpp:856-858
       else if (ent.def->eType == Enums::ET_MONSTER) spriteSortBias[si] = -1; // monsters: hook (src/Render.cpp:859-862)
   }
   world.drawBSP(g_map, g_media, camera, spriteSortBias.data());
   ```
   (Monsters don't exist yet — the `-1` arm stays inert.)
4. Known remaining deviation C5 (tie-break order) explicitly not addressed.
5. `drawSprites`/`drawPolys` (`:456-502,746`) remain unused dead paths — untouched.

Acceptance B5: fewer wrong-order pop-ins between overlapping sprites across height transitions (crates, ground debris vs walls); crate/flat layering matches the original's bias scheme.

---

## 4. Implementation order (single PR, build after each group)

1. **Group 1 — gameplay (`Game.h/.cpp`)**: A3, A4, A5 (header swap + `useDoorFacing`), A6, A7 (parts 1–2), A8. Build: compiles, doors animate; expected visual regressions (open-door texture missing) until Group 2 lands.
2. **Group 2 — renderer textures + doors (`World3D.cpp`)**: A7 part 3 (preload frame ranges), A1+A2 (wall-branch restructure incl. `n28`), B2, B4.
3. **Group 3 — billboards + sorting (`World3D.h/.cpp`, `Main.cpp`)**: B3, B5 (signature + loop + Main wiring).
4. **Group 4 — camera (`Main.cpp`)**: B1, A5 call-site swap if not already done in Group 1 (recommended: do the call-site swap in Group 1 together with the Game API change).
5. Final build: `cmake --build build_new -j 8`; run `cd build_new/new_src && ./DoomIIRPG`.

## 5. Manual verification checklist (user = the eyes)

Run the game and confirm, on screen:

- [ ] **Red/blue door splits top/bottom**: finding a red or blue door, pressing E makes it split into a top and a bottom half that separate vertically (gap grows), full width, no horizontal squish.
- [ ] **Seamless when closed**: the same door looks like one continuous panel before opening and after closing (no seam artifact).
- [ ] **Slide doors stay glued to the jamb**: plain unlocked doors slide sideways into the wall while the visible texture strip appears anchored at the doorway edge (not compressing toward a point).
- [ ] **Close animates like a door**: auto-close (walk away, take a step/turn) replays the split/slide visibly in reverse — not a both-axis shrink into the floor.
- [ ] **Solidity timing**: you cannot walk through the doorway while the open animation is running; you pass the moment it finishes; a closing door blocks you immediately.
- [ ] **Re-open smoothness**: press E mid-close — the door reverses smoothly from where it was (no jump to fully closed).
- [ ] **Faced-door trigger**: standing between two doors, E opens the one in front of you, not a side one; E facing a wall within 1 tile does nothing weird.
- [ ] **Auto-close occupancy**: standing in the doorway or immediately before/after it along the passage prevents auto-close; moving one tile past closes it on your next action.
- [ ] **Open-door texture**: while open/animating the door shows its alternate frame (or at minimum does not disappear).
- [ ] **Framing nudge (B1)**: overall view sits slightly farther back than before (small uniform change; up to ~2.5 map units — subtle).
- [ ] **Portal sockets (B2)**: portal sockets align vertically with their surrounding art (raised 8 map units vs previous build).
- [ ] **Flat decals (B4)**: scorch/floor-decal sprites lie flat on the ground, not sticking up vertically.
- [ ] **General sprite shift**: item pickups/billboards still stand on floors and align with walls as before (no regression from B1/B3/B5).
- [ ] Build green; no new console errors (stderr door-entity dump may still appear — unrelated).

## 6. Out of scope (explicit)

Viewport/aspect change (#8 — deliberate deviation pending user decision); keycard unlock path incl. message 44 plumbing beyond refusal (R3, Phase 5 scripts EV_ITEM_COUNT/EV_DOOROP); monster-blocks-close (R10 — hook comment only); door sounds (`playSound 1028/1030`); water stream sprites (240, #9); renderMode blending (#10); mediaId clamp (#11); bounds cast-vs-mask (#12); float-vs-int corner folding (#13); split-sprite leaf duplication; per-frame relinking performance; sort tie-break order (C5); legacy RLE-wall-decal skip guard (C3); secret-wall door variant (`b = true` path, LS_FLAG_SECRET_OPEN/SECRET_HIDE); save/load door state (`watchLine`, binary state).
