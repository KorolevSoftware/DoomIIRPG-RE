# Sprite placement math (original `src/`) vs rewrite (`new_src`)

Topic: exact world-space placement of map sprites in the legacy renderer
(`src/Render.cpp`), and a line-by-line comparison with the GL 3.3 rewrite
(`new_src/render/World3D.cpp`). Written while hunting the "sprites render
slightly shifted" bug. Every claim cites `file:line`.

Legacy constants used below: `MAPTILE_SIZE = 64`, `TILE_MASK = 63`,
`MAP_MAXWORLDVALUE = 2047`, `SPRITE_SIZE = 64` (`src/Render.h:45-52`),
field indices `S_X=0 … S_SCALEFACTOR=8` (`src/Render.h:56-64`),
`BASIC_SCALE_FACTOR = 64` (`src/Render.h:65`),
`VIEW_NUDGE = 9` / `SPRITE_Z_NUDGE = 84` (`src/Render.h:43-44`).

--------------------------------------------------------------------------------
## Original behavior

### A1. Coordinate units

* `mapSprites` is a struct-of-arrays with stride `numSprites`;
  `S_X = numSprites*0 … S_SCALEFACTOR = numSprites*8`
  (`src/LoadingManager.cpp:439-448`). Per-sprite field index is
  `S_X+i`, `S_Y+i`, … as used everywhere (e.g. `src/Render.cpp:1507-1512`).
* X/Y are read from the map as **bytes scaled by 8**
  (`readCoordArray` → `shiftCoord`: `(byte & 0xFF) * 8`,
  `src/Resource.cpp:40-49,154-156`; loaded at `src/LoadingManager.cpp:488-489`).
  So sprite X/Y live in **map units where one tile = 64 units** and all stored
  values are multiples of 8. Tile index = `coord >> 6`
  (`src/Game.cpp:1023`, `src/MovementController.cpp:329`).
  Entities spawned by script sit at tile centers:
  `(tile << 6) + 32` (`src/ScriptThread.cpp:1220`, `src/Entity.cpp:1706-1707`).
* Z defaults to **32** and SCALEFACTOR to **64** for every sprite at load
  (`src/LoadingManager.cpp:497-498`); z-sprites (index ≥ `numNormalSprites`)
  get their Z **byte** overwritten from the map (`src/LoadingManager.cpp:527`).
* At draw time the vertex position is `x << 4`, `y << 4` and
  `z = mapSprites[S_Z] << 4` (`src/Render.cpp:1507-1509`,
  billboard verts `src/Render.cpp:483-485`). BSP world geometry uses
  `byte << 7` per component (`src/Render.cpp:990-992`). Both feed the same
  14.14 view matrix (`src/TinyGL.cpp:68-98`).
* The camera is passed as `(canvas->viewX << 4) + 8` (all three axes)
  (`src/Canvas.cpp:1344`) — i.e. an extra **+8 sub-unit (= +0.5 map unit)**
  bias on top of the `<<4` fixed-point conversion.
* Terrain height lookup: `getHeight(x,y) = heightMap[(y>>6)*32 + (x>>6)] << 3`
  after `x &= 0x7FF; y &= 0x7FF` (`src/Render.cpp:2444-2451`); the value is in
  map units. Entities use it directly: `Z = 32 + getHeight(...)`
  (`src/Entity.cpp:70`).

### A2. postProcessSprites / relinkSprite (load-time snapping)

`postProcessSprites()` runs **once per map load**
(`src/LoadingManager.cpp:569`), not per frame:

* every sprite: `mapSprites[S_Z] += getHeight(X, Y)` (short store)
  (`src/Render.cpp:2464`);
* every **z-sprite** (`i >= numNormalSprites`): `mapSprites[S_Z] -= 32`
  (`src/Render.cpp:2465-2467`) — the "-32 nudge" happens in map units on the
  stored Z;
* RENDERMODE is assigned from a tileNum table
  (`src/Render.cpp:2468-2495`);
* then `relinkSprite(i)` re-inserts the sprite into the BSP
  (`src/Render.cpp:2496`).

`relinkSprite(n)` passes `X<<4, Y<<4, Z<<4` into `getNodeForPoint`
(`src/Render.cpp:2376-2378`), which walks the BSP with classification
`(p·normal >> 14) + nodeOffset` (`src/Render.cpp:926-929`), accepts a node when
the signed distance is within `-128 < d < 128` (except water streams,
`src/Render.cpp:2422`), and finally rejects points outside the leaf bounds
`nodeBounds[i] & 0xFF << 7` (`src/Render.cpp:2434-2440`). Sprites rejected
there (`-1`) are never drawn.

### A3. Billboard sprites (renderSpriteObject → renderSprite)

Entry: `renderSpriteObject(n)` reads raw `x,y`, `z = S_Z << 4`,
`scaleFactor = S_SCALEFACTOR << 10` (so stored 64 ⇒ 65536 = 1.0)
(`src/Render.cpp:1507-1512`), possibly bumps the frame for AUTO_ANIMATE
`(n + time/100) % count` (`src/Render.cpp:1544-1546`), then dispatches to
`renderSprite(x, y, z, tile, frame, flags, …)`.

In `renderSprite` (`src/Render.cpp:426-514`), the billboard branch is taken
when `(flags & 0x2F000000) == 0` (`src/Render.cpp:455`) — i.e. no
NORTH/SOUTH/EAST/WEST/FLAT bits (`src/Enums.h:1240-1248`):

1. `z -= 512` (`src/Render.cpp:456`).
2. Size selection (`src/Render.cpp:461-472`):
   * if `(flags & 0x400000)` (TILE/wall sprite) **or** the texture is raw
     (`textureBaseSize == sWidth*tHeight`; note `textureBaseSize` is the
     texel-data byte length, `src/LoadingManager.cpp:149-151`,
     consumed at `src/Render.cpp:2088`):
     `halfW = ((((boundsW >> 2) << 4) + 7) * scaleFactor) / 0x10000`,
     `height = ((((boundsH >> 1) << 4) + 7) * scaleFactor) / 0x10000`
     where `boundsW/H = imageBounds[1]-[0] / [3]-[2]`
     (`src/Render.cpp:443-444`, bounds from `mediaBounds & 0xFFF`,
     `src/Render.cpp:2104-2106`); UVs stay bounds-derived
     (`src/Render.cpp:445-448`); 4 vertices are emitted and drawn via
     `ClipQuad` (`src/Render.cpp:479-493`).
   * else (RLE frame): **fixed quad 518 × 1036** (half-width 518, height 1036,
     in `<<4` units ⇒ 64.75×64.75 map units at scale 1.0),
     UVs forced to the full texture (`n13=n15=0`, `n14=n16=1024`),
     3 vertices (`n18=3`), `n17 = 2` (`src/Render.cpp:466-471`).
3. Anchor nudge: `n17 += 10` (⇒ **12** RLE, **10** bounds),
   `x -= n17 * viewCos >> 16; y += n17 * viewSin >> 16`
   (`src/Render.cpp:473-475`), with
   `viewSin = sinTable[angle]`, `viewCos = sinTable[angle+256]`
   (`src/Render.cpp:2274-2275`). This pulls the billboard ~12/10 map units
   toward the camera.
4. Crates: `if (tileNum == TILENUM_OBJ_CRATE/*152*/) z -= 224`
   (`src/Render.cpp:476-478`).
5. Corners (`src/Render.cpp:479-489`): for vertex `i`,
   `row = (i & 2) >> 1`, `col = (i & 1) ^ row ^ 1`;
   base `(x<<4, y<<4, z - 84)` (the `-84` is `SPRITE_Z_NUDGE`,
   `src/Render.h:44`), `s = n13 + col*n14`, `t = n15 + row*n16`, then
   `viewMtxMove(v, 0, (col*2-1)*halfW, row*height)`.
   `viewMtxMove` adds `right * view[0],[4],[8] >> 14` and
   `-up * view[1],[5],[9] >> 14` (`src/TinyGL.cpp:203-221`); given the view
   matrix layout (`src/TinyGL.cpp:68-98`) column 0 is the camera-right axis and
   `-column 1` is camera-up, so the quad spans ±halfW horizontally and rises
   `height` upward: **the anchor is the bottom-edge center**, bottom edge at
   `Z<<4 - 512 - 84`.
6. Rasterization: GL path `DrawWorldSpaceSpriteLine(mv[0], mv[1], mv[2])`
   rebuilds the 4th corner as `v3 = v2 - v1 + v0` (`src/GLES.cpp:511-513`),
   overrides UVs to `{s}={0,1024,1024,0}`, `{t}={0,0,1024,1024}`
   (flip rules `(flags ^ 0x60000) & 0x20000 / & 0x40000`,
   `src/GLES.cpp:515-537`) and crops them by
   `s = s*176/sWidth, t = t*176/tHeight` (`src/GLES.cpp:539-542`) — the
   **176-texel logical frame crop**. The TinyGL fallback samples RLE columns
   directly with the same 176 logical size
   (`(dy << 12)/176`, column `176*(s/z)>>10`, flip `175 - n11`,
   `src/TinyGL.cpp:747-780`). Textures upload unflipped (row 0 first,
   `src/GLES.cpp:1108-1117`).

Monster rendering stacks several billboards per monster (shadow at floor
`(getHeight+32)<<4`, legs/torso/head with idle-bob `+26` steps and part
offsets) — all through the same `renderSprite`
(`src/Render.cpp:3144-3260,3202`).

### A4. Wall decals / planes (`flags & 0x2F000000 != 0`)

`renderSprite` else-branch (`src/Render.cpp:514-663`):

* Terminals (tileNum 179…183): `z -= 256` (`src/Render.cpp:517-519`).
* Portal eyes (tileNum 155…156, `CLOSED_PORTAL_EYE`…`EYE_PORTAL`):
  `z -= 128` (`src/Render.cpp:521-523`; IDs at `src/Enums.h:773-775`).
* Orientation index: EAST(0x4000000)→0, NORTH(0x1000000)→2,
  WEST(0x8000000)→4, SOUTH(0x2000000)→6 (`src/Render.cpp:526-537`).
* Size (`src/Render.cpp:540-551`): for 256×256 textures `h2=64, w4=32`, else
  `h2 = boundsH >> 1`, `w4 = boundsW >> 2`;
  `h = h2*scaleFactor/65536`, `w = w4*scaleFactor/65536`.
* Z correction (`src/Render.cpp:552-558`): walls (non-FLAT):
  `z += (tHeight - bounds[3]) << 4; z -= 16 * (scaleFactor / 2048)`;
  flats: `z -= 512`.
* Door-lerp (`flags & 0x80000000`; forced `scaleFactor = 65536` at function
  entry, `src/Render.cpp:430-432`): door tiles (red/blue door range) scale
  **height** `n31` and recenter `t` by `>>1`; anything else scales **width**
  `n30` and shifts `s` by the full delta (`src/Render.cpp:586-599`).
* Wall quad (`src/Render.cpp:624-638`): for each vertex,
  `off = (col*2-1) * (w<<4)`;
  `vx = (x<<4) + (viewStepValues[((orient+2)&7)<<1 + 0] >> 6) * off`;
  `vy` likewise with `+1`; `vz = z + row * (h<<4)`;
  `swapXY = true`. `Canvas::viewStepValues` entries are ±64 so `>>6` yields
  unit steps (`src/Canvas.h:108`). There is **no extra offset away from the
  wall surface** — the decal lies exactly in the sprite's X/Y plane.
* Plane/flat quad (`src/Render.cpp:639-661`): uses BOTH axes,
  `+ viewStepValues[(orient+2)&7]>>6 * (±w<<4)` and
  `+ viewStepValues[(orient+4)&7]>>6 * (±h<<4 >> 1)`, `swapXY = false`;
  lava flats scroll UVs (`src/Render.cpp:651-657`).
* UV flips for 0x20000 / 0x40000 (`src/Render.cpp:576-583`).

Related: `occludeSpriteLine` (automap fog-of-war) places its occluder segment
at the sprite X/Y offset by `viewStepValues[(orient+2)&7] >> 1 << 4`
(`src/Render.cpp:698-703`).

### A5. Other special cases (exact values)

* Crate `z -= 224` — billboard branch (`src/Render.cpp:476-478`).
* Terminal targets `z -= 256`, portal eyes `z -= 128` — decal branch
  (`src/Render.cpp:517-523`).
* `TILENUM_EYE_PORTAL` in `renderSpriteObject`: visibility check; when the
  portal is closed, `z += 288` for non-oriented sprites
  (`src/Render.cpp:1628-1642`).
* Water stream (240): dedicated `renderStreamSprite` — endpoints
  `destX<<4/destY<<4/destZ<<4` (pitch-dependent constants 176/224/320/496 and
  −112/−320 offsets) or the source entity sprite position
  (`src/Render.cpp:1412-1496`, dispatch `src/Render.cpp:1548-1551`).
* Player-tile fire: shifted `+(viewStep>>6)*18` and `z -= 512`
  (`src/Render.cpp:1553-1560`).
* Torchiere light glow: separate additive sprite at
  `z + ((10*scale/65536) << 4)` (`src/Render.cpp:1643-1647`);
  tree tops split trunk/crown at `(36*scale/65536) << 4`
  (`src/Render.cpp:1654-1659`).
* Fear eyes: per-monster eye offsets, e.g. Arch-Vile `eyeZ += 403`,
  Pinky `+35`, positioned with `viewRightStepX/Y` (`src/Render.cpp:3031-3141`).
* Flat geometry tricks in `drawNodeGeometry`: lava flats get
  `faceCull = CULL_NONE` plus a `sinTable >> 14` z-wobble; HELL_HANDS renders
  BLEND50; FADE/SCORCH render SUB in TinyGL (`src/Render.cpp:953-982`).
* Dynamite drops (save-load path): `Z = getHeight + (oriented ? 32 : 31)`
  (`src/Entity.cpp:1768`).

### A6. Rounding conventions

* Everything is integer with **arithmetic shifts**: `n17*viewCos >> 16`
  (`src/Render.cpp:474`), `>> 14` matrix products
  (`src/TinyGL.cpp:91,203-220`), `<< 4` coordinate promotion, `<< 3` height
  scaling, `<< 7` poly coords.
* Half-texel fudge `+ 7` inside the bounds-derived sprite size
  (`src/Render.cpp:462-463`) — after `<<4`, i.e. +7/16 world sub-units.
* Bottom-anchor nudges: `-512` (floor drop), `-84` (z nudge),
  `-224` crates, `-256` terminals, `-128` portal eyes.
* Camera sub-unit bias `+8` after `<<4` (`src/Canvas.cpp:1344`) and camera
  pull-back `viewX -= 160*viewCos >> 16; viewY += 160*viewSin >> 16`
  (`src/Render.cpp:2279-2282`; `skipViewNudge` defaults false,
  `src/Render.cpp:116`).
* Negative handling comes free from arithmetic shifts; division only appears
  as `/65536`, `/0x10000`, `/2048` scale conversions on positive values.

### A7. Depth sorting

Per visible leaf (`renderBSP` walks leaves far→near, geometry then sprites,
`src/Render.cpp:1752-1763`): each sprite gets
`sortZ = (X*mvp[2] + Y*mvp[6] + Z*mvp[10] >> 14) + mvp[14]` using **raw
(postProcess) coordinates** and the CPU `tinyGL->mvp` built BEFORE the GLES
projection sign-fixups (`src/Render.cpp:846`, `src/TinyGL.cpp:191`,
fixups only in the GL copy `src/GLES.cpp:168-176`), then biases:

* DECAL flag 0x10000000 → `INT_MAX`; TILE(+257) → `+6`
  (`src/Render.cpp:839-849`);
* water 240/245/246/247 → `INT_MIN` (`src/Render.cpp:850-852`);
* any orient bit → `+5` (`src/Render.cpp:853-855`);
* entity `info & 0x1010000` → `+1`; monsters → `-1`
  (`src/Render.cpp:856-862`);
* tiles 240-244/241/255 → `-3`, 137-139 → `+2`, 152 (crate) → `+5`,
  239 → `-3` (`src/Render.cpp:863-874`).

Insertion into a sorted linked list keeps it descending; among equal keys the
newer sprite lands closer to the head and is drawn first
(`src/Render.cpp:880-893`). Sprites straddling a node boundary are duplicated
into neighbor leaves via `addSplitSprite` with ±8 tolerance
(`src/Render.cpp:896-910,1094-1096`).

--------------------------------------------------------------------------------
## Discrepancies found in new_src

Active path is `World3D::drawBSP` → `drawSprite`
(`new_src/core/Main.cpp:395`); `drawSprites`/`drawPolys`
(`new_src/render/World3D.cpp:456-502,746`) are currently unused.
Verified matches first, then numbered discrepancies.

Matches (no action): camera basis copy of `buildViewMatrix`
(`new_src/render/Camera3D.cpp:15-43` ≡ `src/TinyGL.cpp:68-98`); billboard
branch — `z -= 512`, `n17 = RLE?12:10`, `x -= n17*viewCos>>16; y += n17*viewSin>>16`,
crate `-224`, bottom anchor `z-84`, corner offsets via view columns
`(st[0],st[4],st[8])`/`(st[1],st[5],st[9])`, sizes 518/1036 or
`(((b>>2)<<4)+7)*scale/65536` with the same branch condition
RLE⇔`data.size() != w*h` (`new_src/render/World3D.cpp:615-663,560-594`);
wall branch — orientation mapping, `n24/n25` incl. 256×256 special case,
`z += (tHeight-b3)<<4; z -= 16*(scaleFactor/2048)`, terminal 179-183 and
portal 155..156 ranges (but see #2), corner formulas with
`kViewStepValues[n29]>>6` (`new_src/render/World3D.cpp:664-743`); heightMap
snap `(h<<3)<<4` with z-sprite `-32<<4` and `&0x7FF`, `(y>>6)*32+(x>>6)`
indexing (`new_src/render/World3D.cpp:535-542,828-834`); sort key formula and
+6/water/oriented biases (`new_src/render/World3D.cpp:861-865`); camera
`+8` bias (`new_src/core/Main.cpp:380-382`); `readCoord` byte*8
(`new_src/io/DataReader.cpp:52-54`).

1. **Missing camera pull-back nudge.** Legacy moves the view point
   `viewX -= 160*viewCos>>16; viewY += 160*viewSin >> 16` every frame
   (`src/Render.cpp:2279-2282`); the rewrite passes the raw player position to
   `camera.setView` with no equivalent anywhere
   (`new_src/core/Main.cpp:380-382`; no `160`/nudge code exists in `new_src`).
   Net effect: the whole rewrite scene is viewed from ~10 map units further
   forward than the original — a uniform displacement of everything (sprites
   included) compared side-by-side with the original game.
2. **Portal-socket over-included in the `z -= 128` range.**
   `tileNum >= 155 && tileNum <= 157` (`new_src/render/World3D.cpp:717`)
   includes `TILENUM_PORTAL_SOCKET = 157` (`src/Enums.h:775`); the legacy
   subtraction covers only 155–156 (`src/Render.cpp:521-523`) — sockets get
   the decal path without it (`src/Render.cpp:538-539`). Portal sockets sink
   8 map units too low.
3. **FLAT sprites have no plane branch.** Legacy builds horizontal quads using
   the second `viewStepValues` axis for `SPRITE_FLAG_FLAT`
   (`src/Render.cpp:639-661`); the rewrite's wall branch always emits a
   vertical quad (`new_src/render/World3D.cpp:728-742`) even when
   `info & 0x20000000` (it only copies the FLAT `z -= 512`,
   `new_src/render/World3D.cpp:700-705`). Any flat-flagged sprite is
   structurally mis-oriented/misplaced.
4. **DoorLerp decal behavior diverges.** Rewrite saves `realScale`, forces
   `scaleFactor = 65536`, then collapses only WIDTH with no UV compensation
   (`new_src/render/World3D.cpp:691-697,710-713`). Legacy has two cases: door
   tiles scale HEIGHT (`n31`) and recenter `t` by `>>1`; others scale WIDTH
   and shift `s` by the full delta (`src/Render.cpp:586-599`). Animated doors
   will slide/scale differently mid-transition.
5. **Billboard UV flips ignored; s-axis mirrored vs legacy GL.** Rewrite never
   applies `flags & 0x20000/0x40000` in the billboard branch and assigns
   `s = crop` to the right/bottom corner (`new_src/render/World3D.cpp:649-659`);
   legacy GL assigns `s=0` there and flips on inverted
   `(flags ^ 0x60000)` tests (`src/GLES.cpp:515-537`), TinyGL flips in the
   sampler (`src/TinyGL.cpp:754-756,778-780`). Mirrored content for
   asymmetric flipped sprites; no positional effect.
6. **Sort-key biases and inputs differ.** Rewrite omits the tileNum biases
   (crate 152 `+5`, 239 `-3`, 240-244/255 `-3`, 137-139 `+2`) and entity ±1
   tweaks (`src/Render.cpp:856-874` vs `new_src/render/World3D.cpp:861-867`),
   sorts by RAW Z instead of height-snapped Z
   (`new_src/render/World3D.cpp:858-861`), and breaks ties in the opposite
   order (stable low-index-first vs legacy head-insertion
   `src/Render.cpp:880-893`). Its MVP also carries a constant +256 offset from
   the halved `proj[14]` (`new_src/render/Camera3D.cpp:90-92` vs
   `src/GLES.cpp:174-176`) — order-preserving. Also missing: split-sprite
   duplication across leaves (`src/Render.cpp:896-910,1094-1096`). Effects
   appear only as occasional wrong draw order between overlapping sprites.
7. **Height snap re-derived per frame instead of baked at load.** Legacy snaps
   Z once during load and stores it (`src/Render.cpp:2459-2467`,
   `src/LoadingManager.cpp:569`); the rewrite recomputes it every draw from
   current X/Y (`new_src/render/World3D.cpp:535-542,828-834`). Identical for
   static sprites; diverges whenever gameplay later moves a sprite across
   height-map tiles or modifies Z (legacy keeps the load-time base).
8. **Viewport/aspect source differs.** Legacy computes
   `viewAspect = (fov<<14)/((viewportW<<14)/viewportH)` from the shrunk
   3D viewport `_setViewport(w-2, h-2)` with `viewRect[1]=20`
   (`src/Render.cpp:2223`, `src/TinyGL.cpp:149-166`, `src/Canvas.cpp:124-127`);
   the rewrite uses the full 480×320 canvas
   (`new_src/core/Main.cpp:225,382`). Different focal length ⇒ different
   on-screen scale/framing of the entire scene versus the original.
9. **Water streams (240) are skipped entirely**
   (`new_src/render/World3D.cpp:523` vs `src/Render.cpp:1412-1496`) — missing
   content, not a shift.
10. **renderMode ignored** (`(void)renderMode`,
    `new_src/render/World3D.cpp:530`); legacy assigns blending per tileNum in
    postProcess (`src/Render.cpp:2468-2495`). Additive/subtractive sprites
    render opaque — visual, not positional.
11. **Frame/mediaId clamp added**: `if (mediaId >= hi) mediaId = lo`
    (`new_src/render/World3D.cpp:550-551`) has no legacy counterpart
    (`src/Render.cpp:2047`); out-of-range frames pick a different texture.
12. **Bounds cast vs mask**: rewrite casts `mediaBounds` to `int16_t`
    (`new_src/render/World3D.cpp:565-570,679-684`); legacy masks `& 0xFFF`
    (`src/Render.cpp:2104-2106`). Latent divergence only if a bound ever has
    bits above 0xFFF set.
13. **Float accumulation in corner offsets**: rewrite folds right+up into one
    float expression (`new_src/render/World3D.cpp:643-645`); legacy performs
    two separate truncating `>>14` adds (`src/TinyGL.cpp:210-220`). Worst case
    ≈2 sub-units = 1/8 map unit — sub-pixel, cannot explain a visible shift.

--------------------------------------------------------------------------------
## Verdict

The core placement math of `drawSprite` (billboard anchor, nudges 512/84/224,
sizes 518×1036 and `(((b>>2)<<4)+7)*scale`, wall-decal axes/z-corrections,
heightMap snap, sort key) is a faithful match — I found **no missing/extra
+32-style centering term and no >>-vs-/ rounding bug in the per-sprite math**.

Plausible causes of a *uniform slight displacement*, most likely first:

* **#1 (missing −160·cos/+160·sin camera pull-back)** displaces the entire
  view by ~10 map units along the facing direction relative to the original —
  the single largest systematic offset found. It moves sprites and walls
  together, so it shows up when comparing against the original game rather
  than as sprite-vs-wall misalignment within the rewrite.
* **#8 (full-canvas aspect vs shrunk viewport)** changes projection scale —
  everything is slightly larger/smaller and framed differently than the
  original.
* For *sprite-specific* small shifts: **#2** (portal sockets 8 units low) and,
  on non-flat terrain, **#7** (per-frame re-snap vs baked snap) and **#6**
  (sort by raw Z) can individually displace or reorder some sprites.
* If the report is strictly "every sprite offset by the same small amount
  relative to walls", none of the verified per-sprite formulas account for it;
  the next suspects to instrument would be the UV crop interpretation (#5) —
  which shifts texel content inside the quad, reading as a content shift for
  asymmetric frames — and the camera terms above.
