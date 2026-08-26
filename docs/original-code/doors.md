# Doors (original `src/`) — verified mechanics

How the legacy RE port creates, triggers, animates, renders and collides doors,
with the exact constants/formulas. Every claim cites `src/<file>:<line>`.
Comparison with the rewrite lives in the "Rewrite status" section at the end
and in `docs/research/2026-08-23-doors.md`.

Key units: map coordinates are **canvas units, 1 tile = 64 units**
(`src/Game.cpp:1023-1024`, `src/MovementController.cpp:329`);
`scales` are stored as **bytes where 64 = full size** (`src/LoadingManager.cpp:497`),
converted to 16.16 by `scale << 10` (`src/Render.cpp:1512`).

--------------------------------------------------------------------------------
## 1. Door entity creation

* Doors are **map sprites with `SPRITE_FLAG_TILE` (0x400000)** whose effective
  tile number `tileNum = (mapSpriteInfo & 0xFF) + 257` falls in the wall range.
  The `+257` is applied everywhere consistently: entity scan
  (`src/Game.cpp:375-378`), rendering (`src/Render.cpp:1515-1517`),
  BSP leaf assignment (`src/Render.cpp:2406-2408`), lerp bookkeeping
  (`src/Game.cpp:1082-1085` area, `src/Game.cpp:2864-2867`,
  `src/Game.cpp:3082-3085`).
* Tile constants (`src/Enums.h:832-842`):

  | tileNum | constant | entities.bin def |
  |---|---|---|
  | 271 | TILENUM_RED_DOOR_LOCKED   | eType 5, eSubType 1 (locked),   parm 1 |
  | 272 | TILENUM_RED_DOOR_UNLOCKED | eType 5, eSubType 2 (unlocked), parm 1 |
  | 273 | TILENUM_BLUE_DOOR_LOCKED  | eSubType 1, parm 1 |
  | 274 | TILENUM_BLUE_DOOR_UNLOCKED| eSubType 2, parm 1 |
  | 275 | TILENUM_DOOR_LOCKED       | eSubType 1, parm 0 |
  | 276 | TILENUM_DOOR_UNLOCKED     | eSubType 2, parm 0 |
  | 277 | TILENUM_LEVEL_DOOR_LOCKED | eSubType 1, parm 2 |
  | 278 | TILENUM_LEVEL_DOOR_UNLOCKED| eSubType 2, parm 2 |

  `TILENUM_FIRST_DOOR = 271`, `TILENUM_LAST_DOOR = 281`
  (`src/Enums.h:833,842`). Def values verified by decoding the
  `entities.bin` table loaded by `EntityDefManager::startup`
  (`src/EntityDef.cpp:20-44`, format `tileIndex i16, eType/eSubType/parm u8,
  name/longName/description i8`).
* `loadMapEntities` does **not** special-case doors: for every map sprite it
  computes `n6 = tileNum (+257 if TILE)` and calls
  `entityDefManager->lookup(n6)`; if a def exists an `Entity` is created with
  `def`, `entity->info = spriteIndex+1`, `initspawn()`,
  `mapSprites[S_ENT] = entityIndex`, then `linkEntity(tx, ty)` unless the
  sprite has HIDDEN (0x10000) (`src/Game.cpp:374-460`; sprite accessor
  `src/Entity.cpp:114-116`). `ET_DOOR = 5` comes purely from the def table
  (`src/Enums.h:15`).
* `initspawn()` has **no door-specific branch** (`src/Entity.cpp:50-112`);
  scale stays at the default 64 set for every sprite at load time
  (`src/LoadingManager.cpp:497`).
* `linkEntity` marks the entity "on grid": `entity->info |= 0x100000`
  (`src/Game.cpp:118`); `unlinkEntity` clears it
  (`src/Game.cpp:92`). This bit IS the closed/open gameplay state:
  `performDoorEvent` derives `bool b2 = (watchLine->info & 0x100000) == 0`
  ("is currently open/unlinked") from it (`src/Game.cpp:1053`).
* Door orientation flags on the sprite info word
  (`src/Enums.h:1240-1250`): NORTH 0x1000000, SOUTH 0x2000000
  (together `SPRITE_FLAGS_HORIZONTAL = 0x3000000`), EAST 0x4000000,
  WEST 0x8000000 (together `SPRITE_FLAGS_VERTICAL = 0xC000000`),
  `SPRITE_FLAG_DOORLERP = 0x80000000` (shift 31, `src/Enums.h:1228,1247`).
* `def->parm` bit meanings for doors (`src/Enums.h:236-237` names
  DOOR_FLAG_SCIFI=1 / DOOR_FLAG_CENTER=2): **parm bit 0 suppresses the
  positional slide** (red/blue doors have parm=1); level doors carry parm=2
  (slide normally).

## 2. Opening trigger (player use)

* The interact action traces along the view ray with clip mask 13997, which
  contains bit 5 = ET_DOOR (`src/PlayingInputHandler.cpp:200-221`,
  door pick-up at 270-278).
* Door use condition: facing entity is `eType == 5` AND
  `dist2 <= combat->tileDistances[0]` AND weapon != 14
  (`src/PlayingInputHandler.cpp:445`).
  * `distFrom` is **Chebyshev distance squared**: `max(dx*dx, dy*dy)`
    (`src/Entity.cpp:1155-1158`).
  * `tileDistances[j] = (64*(j+1))^2`, so `[0] = 4096` = exactly 1 tile
    (`src/Combat.cpp:42`).
* **Locked handling**: `if (entity->def->eSubType == 1) hud->addMessage(44)`
  and nothing else (`src/PlayingInputHandler.cpp:447-449`). There is **no
  keycard check anywhere in native code** — `Player::requireItem`
  (`src/Player.cpp:253-262`) has no callers.
* Unlocked door: `performDoorEvent(0, entity, 1)` (open) followed by
  `advanceTurn()` (`src/PlayingInputHandler.cpp:451-452`). The `n2 = 1`
  argument means "never snap the animation"
  (`src/Game.cpp:1153-1155`).
* Facing prompts: help id 1 shown when the faced door is locked, help id 7
  for doors generally, within 1 tile (`src/MovementController.cpp:97-114`).
* **Keys are data-driven (scripts)**:
  * Keycard pickups are `IT_INVENTORY` items with `parm == 19` (red) or
    `parm == 20` (blue); they land in `inventory[19]/[20]`
    (`INV_OTHER_RED_KEY/BLUE_KEY`, `src/Enums.h:222-223`; pickup logic
    `src/Entity.cpp:160-195`, special message 84 at `src/Entity.cpp:172-175`;
    HUD display `src/Hud.cpp:1155-1179`).
  * Map bytecode reads them via `EV_ITEM_COUNT`
    (`inventory[n22]`, `src/ScriptThread.cpp:433-454`) and then issues
    `EV_DOOROP` (`src/ScriptThread.cpp:747-780`):
    `sprite = args & 0x3FF`, `op = (args >> 10) & 3`:
    op 0 = open, op 1 = `setLineLocked(false)` **then** open, op 2 = lock +
    sound 1065, op 3 = unlock + sound 1065. Bit 2 of the op field makes the
    event run without pausing input (`n43`, `src/ScriptThread.cpp:751`).
  * `setLineLocked(entity, b)` toggles **bit 0 of the sprite's tileNum byte**
    (lock → even, unlock → odd — 271↔272, 273↔274, 275↔276, 277↔278), writes
    it back into `mapSpriteInfo`, and re-looks-up the entity def by
    `newTileNum + 257` (`src/Game.cpp:2477-2498`).
  * So "red/blue keycards" are: script checks `inventory[19]/[20]`, then
    unlocks (texture swap via tileNum bit 0 + def re-lookup) and/or opens the
    door. A locked door (`eSubType == 1`) is blanket-refused by
    `performDoorEvent` itself (`src/Game.cpp:1059-1061`) until such an unlock
    happens.
* Other triggers:
  * Monsters: when a monster's step is blocked by a door it opens it with
    `performDoorEvent(0, traceEntity, 2)` (`src/Entity.cpp:1109-1111`;
    `n2 = 2` = snap-if-offscreen).
  * Save-game restore: `restoreBinaryState` opens/closes doors with `n2 = 0`
    → animation snaps instantly (`src/Entity.cpp:1859-1911`,
    snap at `src/Game.cpp:1153`). Saved binary state for a door is
    `S_SCALEFACTOR != 64` = open; locked adds flag 0x200000
    (`src/Entity.cpp:1436-1446`).

## 3. `performDoorEvent` — exact control flow

`Game::performDoorEvent(n, scriptThread, watchLine, n2, b)`
(`src/Game.cpp:1047-1157`); `n`: 0=open, 1=close; `b`: secret-wall variant.

```
tileNum n4 = mapSpriteInfo[sprite] & 0xFF;  (+257 if info & 0x400000)   // 1050-1056
b2 = !(watchLine->info & 0x100000)          // door currently UNLINKED = open  // 1053
b3 = n4 >= 271 && n4 < 281                  // "door-family tile"            // 1058
if (def->eSubType == 1 /*DOOR_LOCKED*/) return false;                      // 1059-1061
if (n == 0 && b2 && b3)  { updatePlayerDoors(watchLine, true); return false; } // 1062-1065  (already open -> re-register auto-close)
if (n == 1 && !b2)       return false;                                     // 1066-1068  (already closed)
if (b2 && b3) {                          // CLOSING a door:
    for each entity on the door tile:
        if (eType == 2 && eSubType != 17) { this->watchLine = watchLine; return false; } // monster in doorway blocks closing (1070-1075)
    if (game->watchLine == watchLine) game->watchLine = nullptr;                          // 1076-1078
    linkEntity(watchLine, tx, ty);       // SOLID AGAIN IMMEDIATELY at close start // 1079
}
allocLerpSprite(...)                     // reuses existing slot for same sprite (3028-3036)
mapSpriteInfo[sprite] &= ~0x10000;        // clear HIDDEN                            // 1082
flags |= LS_FLAG_DOOROPEN (or SECRET_OPEN); mapSpriteInfo |= 0x80000000 (DOORLERP)  // 1085-1089
srcX/Y/Z/scale = current mapSprites values                                          // 1092-1095
slide = 32; if (n == 1) slide = -slide;                                             // 1097-1100
if      (info & 0x3000000) { if (!secret) { if (!(def->parm & 1)) dstX += slide; } else dstY +/-= 32 }  // 1101-1113
else if (info & 0xC000000) { if (!secret) { if (!(def->parm & 1)) dstY += slide; } else dstX +/-= 32 }  // 1114-1126
startTime = gameTime; travelTime = 750;                                             // 1127-1128
flags |= ENT_NORELINK | S_NORELINK;                                                 // 1129
if (n == 1) { flags = DOORCLOSE (clear DOOROPEN/ENT_NORELINK kept set above);
              dstScale = 64; if (!secret && b3) updatePlayerDoors(false);
              playSound(1028 /*door_close*/); }                                    // 1130-1138
else if (n == 0 && !b) { if (b3) updatePlayerDoors(true);
              dstScale = 0; playSound(1030 /*door_open*/); }                       // 1139-1145
write back srcX/Y/Z/scale into mapSprites                                           // 1146-1149
if (b3 && !DOORCLOSE) mapSpriteInfo frameBits(8-15) = 0x100;   // texture FRAME 1 while open // 1150-1152
if (n2 == 0 || canvas state == ST_AUTOMAP ||
    (n2 == 2 && cullBoundingBox((srcX+dstX)/2, (srcY+dstY)/2)))
        snapLerpSprites(sprite);             // finish instantly                     // 1153-1155
```

Notes:
* **Red/blue doors (parm&1) do NOT slide** — their only animation is the
  scale 64→0 lerp consumed by the renderer as a vertical split (§4).
* Sound ids resolve to `door_close.wav` (index list around
  `src/Sounds.h:36-39`); played at animation start, once.
* `updatePlayerDoors` fills/clears `openDoors[6]` slots
  (`src/Game.cpp:1196-1213`; array `src/Game.h:116`; reset to 4 entries at
  load `src/Game.cpp:334-336` — legacy quirk, loop bound elsewhere is 6).

### Animation tick

`Game::updateLerpSprite` (`src/Game.cpp:2855-2956`):

```
elapsed = gameTime - startTime;                    // 2862
if (elapsed >= travelTime) { freeLerpSprite(); }   // 2868-2871
frac8 = (elapsed << 16) / (travelTime << 8);       // 2877  -> 0..256
S_X   = srcX + (frac8 * ((dstX-srcX) << 8) >> 16); // 2879  == src + frac*delta
S_Y   = ...                                        // 2880
S_SCALEFACTOR = (uint8_t)(srcScale + (frac8*((dstScale-srcScale)<<8) >> 16)); // 2881
S_Z   = ...                                        // 2882
```

Linear interpolation over **750 ms**; positions in canvas units, scale as a
byte (64 = closed/full, 0 = open). `LS_FLAG_S_NORELINK` prevents BSP re-linking
of the sprite mid-animation (`src/Game.cpp:2889-2896`).

### Animation completion — `freeLerpSprite`

(`src/Game.cpp:3078-3211`)

* Writes final dstX/dstY/(dstZ)/dstScale into mapSprites
  (`src/Game.cpp:3086-3091`).
* `LS_FLAG_DOORCLOSE`: force `S_SCALEFACTOR = 64`, clear frame bits
  (`&= 0xFFFF00FF`, back to texture frame 0), clear `updateFacingEntity`,
  and clear the DOORLERP bit: `mapSpriteInfo &= 0x7fffffff`
  (`src/Game.cpp:3120-3125`). **The DOORLERP bit therefore stays set through
  the whole close animation** (it was left set from the open).
* `LS_FLAG_DOOROPEN` / `SECRET_HIDE`: `unlinkEntity(entity)` — **the door
  becomes passable here, at the END of the open animation**
  (`src/Game.cpp:3131`); non-door movers additionally get
  `SPRITE_FLAG_HIDDEN` (`src/Game.cpp:3132-3134`) — doors stay visible
  (scale 0) with DOORLERP still set.

## 4. Renderer consumption (`Render::renderSprite`)

Call chain: `renderBSP` walks visible leaves and draws them far→near; per leaf
`drawNodeGeometry` then every attached sprite via `renderSpriteObject`
(`src/Render.cpp:1746-1763`). `renderSpriteObject` early-outs on HIDDEN
(`src/Render.cpp:1502-1504`), computes
`scaleFactor = mapSprites[S_SCALEFACTOR] << 10` (`src/Render.cpp:1512`),
applies `+257` for TILE sprites (`src/Render.cpp:1515-1517`) and forwards the
**whole info word as flags** (including bit 31) to
`renderSprite(x, y, z, tileNum, frame, flags, renderMode, scaleFactor, rflags)`
(`src/Render.cpp:1620-1622`).

Inside `renderSprite` (`src/Render.cpp:426-664`):

```cpp
int n10 = scaleFactor;                       // 428: REAL lerp fraction (scale<<10)
if (flags & 0x80000000) scaleFactor = 65536; // 430-432: geometry drawn FULL SIZE
if (scaleFactor == 0) return;                // 433-435: non-door scale-0 skip
```

So a lerping door is **always drawn at full size**; `n10` (0…65536) drives a
one-axis collapse below. Because the override precedes the zero-scale early
out, a fully-open door (scale byte 0) is still drawn — as a degenerate quad.

Wall-sprite setup (`flags & 0x2F000000 != 0`, doors qualify via their
N/S/E/W bits):

* Facing index `n23`: EAST→0, NORTH→2, WEST→4, SOUTH→6
  (`src/Render.cpp:525-537`).
* Quad size from **texture bounds**: `n11 = bounds.w`, `n12 = bounds.h`;
  for 256×256 textures `n24 = 64` (vertical half-extent) and `n25 = 32`
  (horizontal half-extent), otherwise `n24 = n12>>1`, `n25 = n11>>2`
  (`src/Render.cpp:542-549`). `n26/n27 = n24/n25 * scaleFactor / 65536`
  (`src/Render.cpp:550-551`).
* Z placement (`src/Render.cpp:552-558`):
  `z += (tHeight - imageBounds[3]) << 4; z -= 16 * (scaleFactor / 2048);`
  — during a door lerp `scaleFactor` is the forced 65536, hence a constant
  `-512`.
* Along-wall / perpendicular directions from
  `viewStepValues[n29]`, `viewStepValues[n28]` with
  `n29 = ((n23+2)&7)<<1`, `n28 = ((n23+4)&7)<<1`
  (`src/Render.cpp:559-561`); table =
  `{64,0, 64,-64, 0,-64, -64,-64, -64,0, -64,64, 0,64, 64,64}`
  (`src/Canvas.h:108`). Extents `n30 = n27 << 4` (width),
  `n31 = n26 << 4` (height); `x <<= 4; y <<= 4` (`src/Render.cpp:562-565`).
* UV flips: bit 0x20000 mirrors s (`n13 += n14; n14 = -n14`),
  bit 0x40000 mirrors t (`src/Render.cpp:576-583`).
* Face culling: `CULL_NONE` if `flags & 0x10100000` (TWO_SIDED|DECAL),
  else `CULL_CCW` (`src/Render.cpp:449-454`); wall quads drawn with
  `swapXY = true` (`src/Render.cpp:636`).

### The doorLerp block (`src/Render.cpp:585-599`)

```cpp
int n32 = n31;                                   // save FULL height extent
if (flags & 0x80000000) {
    if (tileNum >= 271 && tileNum <= 274) {      // red/blue doors: VERTICAL collapse
        int n33 = n16;
        n31 = (n10 * n31) / 65536;               // height extent *= lerp fraction
        n16 = (n10 * n16) / 65536;               // v-window shrinks
        n15 += (n33 - n16) >> 1;                 // v-window RE-CENTERED (half delta)
    } else {                                     // all other doors/walls: WIDTH collapse
        int n34 = n14;
        n30 = (n10 * n30) / 65536;               // width extent *= lerp fraction
        n14 = (n10 * n14) / 65536;               // u-window shrinks
        n13 += n34 - n14;                        // u-window shifted by FULL delta
    }
}
```

(C++ precedence: `a - b >> 1` is `(a-b)>>1`; additive binds tighter than
shift.)

### Slip-door drawing for tiles 271–274 (`src/Render.cpp:601-623`)

This branch triggers on **tile range alone**, even with no lerp flag:

* `n35 = n16 >> 1` (quarter v-window), `n36 = n31 >> 1` (lerped half-height),
  `n37 = n32 >> 1` (ORIGINAL half-height).
* **Two stacked quads** `j = 0..1`, each spanning heights `n36`, separated by
  a gap `(n37 - n36) << 1` (`vert.z = z + n38*n41 + j*((n37-n36)<<1)`).
  After each quad: `z += n36; n15 += n35;` — the second quad shows the next
  quarter of the v-window.
* Closed (`n10 = 65536`): `n36 == n37`, gap 0 → seamless full door made of
  two half-quads. Opening: gap grows to the full height while both quads
  collapse → **bottom half sinks into the floor, top half rises into the
  ceiling**, full width preserved, v-windows shrink symmetrically toward the
  middle of the texture (via the `n15` recenter above).

### Plain door/wall drawing (`src/Render.cpp:624-638`)

Single quad: corners at `xy + viewStepValues[n29>>6-dir]*(±n30)` along the
wall and `z .. z + n31` vertically, `s ∈ [n13, n13+n14]`,
`t ∈ [n15, n15+n16]`.

Why the width-collapse + full-delta `n13` shift matters: for a sliding door
(parm bit 0 clear) the sprite position itself moves ±32 while the geometric
width shrinks to 0; since the half-width at rest equals the half tile
(~32 units), the quad's leading edge stays pinned at the jamb line, and the
matching `s` shift keeps the **right edge of the texture window fixed**, so
the visible sliver always shows exactly the strip of texture that would be
visible if the panel were sliding behind the neighbouring wall — the texture
appears glued to the doorway instead of compressing.

### Frame switch

While open, door sprites carry frame bits 8–15 = 0x100
(`src/Game.cpp:1150-1152`); `setupTexture(tileNum, frame)` resolves
`mediaIdx = mediaMappings[tileNum] + frame` (`src/PLAN.md` media section;
call at `src/Render.cpp:442`), i.e. doors show **media frame 1 while
open/animating**, frame 0 when closed (restored at
`src/Game.cpp:3122`).

## 5. Auto-close

* Registered doors live in `openDoors[6]` (only tile-range 271–280 doors are
  tracked, `b3`).
* `advanceTurn` (`src/Game.cpp:1238-1281`) — executed after every
  turn-consuming player action (move/use; invoked e.g. from
  `src/PlayingInputHandler.cpp:398-403,451-452`) — ends with:

  ```cpp
  for (j = 0; j < 6; ++j)
      if (openDoors[j] && CanCloseDoor(openDoors[j]))
          performDoorEvent(1, openDoors[j], 2);      // 1271-1278
  ```

  `n2 = 2` lets the close **snap instantly when the door is off-screen**
  (`cullBoundingBox` midpoint test, `src/Game.cpp:1153-1155`).
* `CanCloseDoor` (`src/Game.cpp:1215-1236`):
  * familiar → false;
  * tile centre `cx = (linkIndex%32<<6)+32`, `cy = (linkIndex/32<<6)+32`;
  * occupied if `findMapEntity(cx, cy, 6)` — mask 6 = eTypes 1 (player,
    resolved by the special player-tile check `src/Game.cpp:741-743`) or
    2 (monsters) — tile-granular, no radii;
  * then along the **slide axis**: horizontal walls (0x3000000) test
    `(cx, cy±64)`, vertical walls test `(cx±64, cy)`; either side occupied →
    cannot close.
* Closing is additionally blocked at `performDoorEvent` level when a monster
  stands on the door tile (§3). The blocked door is remembered in
  `Game::watchLine` for save-state (`src/Game.cpp:1070-1078`,
  `src/Game.cpp:1718-1724,1890`).

## 6. Player collision vs doors

* Player movement traces with mask `13501` (0 with noclip)
  (`src/MovementController.cpp:326,332`); bit 5 (ET_DOOR) set → any **linked**
  door blocks. The move commits only if `traceEntity == nullptr`
  (`src/MovementController.cpp:333`). There is no CONTENTS system: solidity =
  presence in `entityDb` + clip-mask bit test
  (`src/Game.cpp:216-296`).
* Oriented wall/door entities are traced as a **line segment ±32 units along
  their wall axis through the CURRENT (animated) sprite position**
  (`src/Game.cpp:249-268`, `CapsuleToLineTrace`), other entities as circles
  of radius 25 (`src/Game.cpp:270-288`).
* Timeline for a door:
  * closed: linked → solid;
  * opening (750 ms): stays linked (`ENT_NORELINK` prevents relinking) →
    **solid the whole time**, blocking segment slides with the panel;
  * open: unlinked at animation end (`src/Game.cpp:3131`) → passable;
  * closing: re-linked at animation start (`src/Game.cpp:1079`) → solid
    immediately.
* So the player can pass only after the open animation fully completes.

## 7. Camera-relative rendering specifics

* Each door sprite belongs to exactly one BSP leaf
  (`relinkSprite` → `getNodeForPoint`, `src/Render.cpp:2376-2442`; TILE
  sprites compare `tileNum+257`, `src/Render.cpp:2406-2408`; on-plane
  oriented sprites pick child1 for N/W, child2 otherwise,
  `src/Render.cpp:2413-2419`). Visibility comes from leaf culling, draw
  order from far→near leaf iteration (painter's algorithm, no depth buffer).
* One quad total — **no separate front/back geometry**; seen from behind the
  same quad is used. Back-face behaviour: TinyGL `faceCull` is `CULL_NONE`
  when the sprite has TWO_SIDED (0x100000) or DECAL (0x10000000) bits,
  otherwise `CULL_CCW` (`src/Render.cpp:449-454`).
* The software rasterizer additionally feeds an occlusion line per wall
  sprite (`occludeSpriteLine`, `src/Render.cpp:666-710`) — TinyGL-only,
  irrelevant to the GL path.
* Automap door colours `COLOR_MAP_DOOR1..3` exist (`src/Enums.h:1371-1373`).

--------------------------------------------------------------------------------
## Rewrite status (`new_src/`, read-only comparison)

Legend: MATCH / MISMATCH / MISSING vs the sections above.

| # | Topic | Original | Rewrite | Verdict |
|---|---|---|---|---|
| R1 | Creation | generic def lookup, all sprites (`src/Game.cpp:374-460`) | door-range filter + `defs.lookup`, links at tile (`new_src/domain/game/Game.cpp:54-73`) | MATCH (subset) |
| R2 | Use trigger | faced entity, Chebyshev ≤ 1 tile, LOS via trace, then advanceTurn (`src/PlayingInputHandler.cpp:445-459`) | E-key → nearest door within Euclidean 3 tiles of faced tile, no facing/LOS/1-tile cap (`new_src/domain/game/Game.cpp:78-96`, `new_src/core/Main.cpp:366-373`) | MISMATCH |
| R3 | Locked doors | refuse + message 44 (`src/PlayingInputHandler.cpp:447-449`); unlock via scripts EV_ITEM_COUNT→EV_DOOROP + `setLineLocked` tileNum bit 0 flip (`src/ScriptThread.cpp:433-454,747-780`, `src/Game.cpp:2477-2498`) | EV_DOOROP acts 1–3 + low-byte-only `setLineLocked` + def swap tn+257 (`new_src/domain/game/ScriptVM.cpp:431-476`, `new_src/domain/game/Game.cpp:580-590`); HUD message 44 still a log stub | MATCH for mechanics, message MISSING |
| R4 | Slide rule | ±32 along wall axis, suppressed when `parm & 1` (`src/Game.cpp:1097-1126`) | identical rule (`new_src/domain/game/Game.cpp:130-139`) | MATCH |
| R5 | Duration/lerp | 750 ms linear, frac8 = `(elapsed<<16)/(travelTime<<8)` (`src/Game.cpp:2877`) | 750 ms linear `t/dur` (`new_src/domain/game/Game.cpp:148-150,348-351`) | MATCH |
| R6 | srcScale on re-open | always current `mapSprites[S_SCALEFACTOR]` (`src/Game.cpp:1095`) | hardcoded 64 on open (`new_src/domain/game/Game.cpp:146`) | MISMATCH (pop if reused mid-anim) |
| R7 | DOORLERP flag lifetime | set at open start; cleared only at close END (`src/Game.cpp:1089,3125`) | set at EVERY anim start (:290), cleared only at close completion (`new_src/domain/game/Game.cpp:1104`) | MATCH |
| R8 | Solidity timeline | solid during whole open (unlink at open end); solid from close START (`src/Game.cpp:1076-1081,3131`) | close-start relink + open-end unlink (`new_src/domain/game/Game.cpp:237-239,1095`) | MATCH |
| R9 | Auto-close | `advanceTurn` → `CanCloseDoor` (player/monsters on door tile + both neighbours along slide axis, tile-granular) → close with snap-if-offscreen (`src/Game.cpp:1238-1281,1215-1236,1153-1155`) | `advanceTurnDoors` tile-granular occupancy incl. fresh player pos, n2=2 kept but cull-snap not ported (animates instead), animating doors early-out via linked check (`new_src/domain/game/Game.cpp:344-382`); LATENT GAP: `else if (info & 0xC000000)` skips neighbour checks for orientation-less doors where legacy tests X unconditionally when 0x3000000 is clear (`src/Game.cpp:1229-1231`) | PARTIAL (verified correct for sp22 `0x0C500010`, docs/research/2026-08-25-blue-door-regression.md) |
| R10 | Monster blocks closing | yes (`src/Game.cpp:1070-1075`) | no monster entities yet | MISSING (by scope) |
| R11 | Renderer: full-size + one-axis collapse | `n10` real scale, geometry forced 65536 (`src/Render.cpp:428-432`) | same pattern (`new_src/render/World3D.cpp:691-697`) | MATCH |
| R12 | Renderer: red/blue vertical split (two quads + growing gap + v-recenter) | `src/Render.cpp:587-592,601-623` | absent — all doors collapse WIDTH only (`new_src/render/World3D.cpp:710-713`) | MISSING |
| R13 | Renderer: UV compensation | u-shift by full delta (slide doors), v-recenter by half delta (`src/Render.cpp:591,597`) | none (`new_src/render/World3D.cpp:720-725` untouched by lerp) | MISSING |
| R14 | Frame 1 while open | bits 8–15 := 0x100 (`src/Game.cpp:1150-1152`) | written at open start for family doors + redundant EV_DOOROP flip; restored at close completion (`new_src/domain/game/Game.cpp:297-299,1103`; `new_src/domain/game/ScriptVM.cpp:456-464`). Bits 8–15 have no solidity reader; orientation bits 24–27 preserved by all `& 0xFFFF00FF` writers | MATCH |
| R15 | Hidden/frame restore, sounds | `src/Game.cpp:1082,3122,1137,1144` | not implemented | MISSING |

Ranked likely causes of "doors work incorrectly":
1. R12 — red/blue doors animate on the wrong axis (squish horizontally
   instead of splitting top/bottom in place).
2. R13 — sliding doors' texture compresses/swims instead of staying anchored
   to the jamb (missing `n13` full-delta shift; `n15` recenter).
3. R3 — red/blue doors can never be opened (no script/key unlock path).
4. R2 — wrong door can open (nearest-in-3-tiles vs faced-within-1-tile+LOS).
5. R7/R8 — closing looks like a both-axis shrink into the floor (flag cleared
   too early) and the door is passable at the wrong times.

## 8. Blue-door regression audit (2026-08-25)

Full timeline + verdicts: `docs/research/2026-08-25-blue-door-regression.md`.
Verified facts:

* sp22 open path is faithful end-to-end: blocking EV_DOOROP open parks the
  thread via `unpauseTime=-1` as DoorAnim owner, 750 ms scale-only collapse
  (`parm&1`), open-end unlink, single owner resume
  (`new_src/domain/game/Game.cpp:204-315,1072-1118`;
  `new_src/domain/game/ScriptVM.cpp:431-476`).
* Auto-close CANNOT close sp22 while the player stands west at (9,19):
  info word `0x0C500010` has `0xC000000` → X-neighbours checked, player tile
  blocks; `playerX_/playerY_` refreshed every tick
  (`new_src/core/GameContext.cpp:153`). A quiet scripted close/op=6 lock would
  snap it solid same-tick — the story trigger EVT 617 tail (@3324 op=1 +
  @3327 op=6, docs/original-code/tile-events-vm.md:362-364) is the prime
  suspect for "задъехалась"; NPC lerp dst tiles (sprites 7/10/19, playersolid
  mask bit 3) are the alternative for "open but solid".
