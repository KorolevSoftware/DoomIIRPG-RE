# Sprite visibility gating & special render cases — original RE port

Topic: where the legacy renderer decides a map sprite is invisible
(`SPRITE_FLAG_HIDDEN`, `Enums.h:1231`), which paths can bypass the gate, plus
two closely-coupled cases: corpse-prop tiles 137–139 and the `NOENTITY`
sprite→entity binding rule. Every claim cites `file:line`.

## 1. The hidden bit has exactly one funnel, checked before entity lookup

`0x10000` = `SPRITE_FLAG_HIDDEN` (`src/Enums.h:1231`,
`SPRITE_SHIFT_HIDDEN = 16` at `src/Enums.h:1213`).

* **Gate 1 — collection.** `Render::addSprite(short n)` returns immediately if
  `(mapSpriteInfo[n] & 0x10000) != 0` (`src/Render.cpp:830-832`) — **before**
  the entity is looked up from `mapSprites[S_ENT + n]`
  (`src/Render.cpp:834-837`). A hidden sprite therefore never enters the
  per-leaf sorted `viewSprites` linked list (`src/Render.cpp:880-893`).
  All sprites reach `addSprite` via `addNodeSprites` → node sprite chains and
  split sprites (`src/Render.cpp:912-920`); z-sprites are ordinary map sprites
  with index ≥ `numNormalSprites` (`src/LoadingManager.cpp:380-382`).
* **Gate 2 — draw.** `Render::renderBSP` walks the view list calling
  `renderSpriteObject` (`src/Render.cpp:1752-1760`); `renderSpriteObject`
  re-checks the bit (`src/Render.cpp:1501-1504`), again **before** its entity
  lookup (`src/Render.cpp:1565-1574`).
* `Render::renderSpriteAnim` (stacked characters) is only reachable from
  `renderSpriteObject` (`src/Render.cpp:1625,1661`) — no independent entry.
* `walkNode`'s occlusion-line pass also skips hidden sprites before entity
  lookup (`src/Render.cpp:1066-1081`), consistent with the automap
  (`src/AutomapController.cpp:143`).
* The mastermind/Caldex `delayedSpriteBuffer` re-draws
  (`src/Render.cpp:1764-1771`) only hold sprites that already passed gate 1
  (they are captured inside `renderSpriteObject`, `src/Render.cpp:1519-1542`).
* `postProcessSprites` mutates render modes / relinks but never draws
  (`src/Render.cpp:2459-2525`).
* The raw emitter `Render::renderSprite(x,y,z,tileNum,frame,…)`
  (`src/Render.cpp:426`) takes explicit arguments from its caller; it cannot
  surface a hidden map sprite because every map-sprite-driven caller sits
  behind gate 2.

**Verdict: no legacy path renders a hidden map sprite.** Writers of the bit:
script op `EV_HIDE` (`src/ScriptThread.cpp:821`), `Game::removeEntity`
(`src/Game.cpp:186-188`), loot drops (`src/Game.cpp:494`), corpse trimming
(`src/Entity.cpp:1201-1203`), gsprite destroy (`src/Game.cpp:1407`).

## 2. Corpse props: tiles 137/138/139

`TILENUM_OBJ_SCIENTIST_CORPSE=137`, `TILENUM_OBJ_CORPSE=138`,
`TILENUM_OBJ_OTHER_CORPSE=139` (`src/Enums.h:754-756`). Entity defs:
`(138, eType 9 ET_CORPSE, eSubType 17, parm 0)` etc. (defs verified in
`entities.bin`; parse layout `src/EntityDef.cpp:36-43`).

* Drawn as a plain billboard through `renderSpriteObject` → final
  `renderSprite(x, y, z, n3, n7, …)` with frame `n7` = anim byte
  (`src/Render.cpp:1728`). Pre-placed props have anim byte 0 → base art.
* While NOT in a camera cinematic (`canvas->state != ST_CAMERA`) and
  `entity->param == 0` and the corpse has loot, an extra +12.5 % scale copy is
  drawn under it as the loot-pile indicator
  (`src/Render.cpp:1714-1717`).
* Depth-sort bias `+2` for tiles 137–139 (`src/Render.cpp:866-868`).
* Media: tile 138 maps to a single frame (`mediaMappings[138]`,
  consumed as `mediaMappings[tileNum] + frame` at `src/Render.cpp:2047`);
  table values extracted from `newmappings.bin`: mappings[137]=737,
  [138]=738, [139]=739 — one frame each. A corpse prop can never clamp onto
  standing art: its range has no other frames.

## 3. `SPRITE_FLAG_NOENTITY` (0x200000): decor-only sprites

Map load binds entities to sprites by art-tile def lookup
(`Game::loadMapEntities`, `src/Game.cpp:374-486`): sprites carrying
`0x200000` skip the whole block — the flag is cleared and **no Entity is
created** (`S_ENT` stays −1) (`src/Enums.h:1237`, `src/Game.cpp:398-400`).
Consequences:

* Script ops that resolve `S_ENT` treat them as bare sprites;
  `EV_HIDE` sets only the hidden bit when `S_ENT == -1`
  (`src/ScriptThread.cpp:820-823`) — no corpsify/remove path.
* `EV_WAKEMONSTER` hard-errors on a missing entity
  (`src/ScriptThread.cpp:879-882`), so decor monsters can never become AI.

## 4. Death states vs visibility

* `Entity::died` (ET_MONSTER branch) keeps the sprite **visible**: anim byte
  becomes `0x70` in place (`n3 = ((n3 & 0xFFFF00FF) | 0x7000)`,
  `src/Entity.cpp:463`), position untouched; already-hidden monsters keep
  hiding (`n3 |= 0x17000`, `src/Entity.cpp:465-467`), others get corpse
  markers and a pile trim to 3 corpses/tile
  (`src/Entity.cpp:469-471`, `src/Entity.cpp:1194-1209`). Def swaps to
  `find(9, eSubType, parm)` (`src/Entity.cpp:501`).
* `ScriptThread::corpsifyMonster` clears bits 8–16 (mask `0xFFFE00FF`,
  i.e. anim byte AND hidden bit) then writes `0x7000`, moves the sprite to
  pixel coords given by the caller, relinks, swaps def, and re-links the
  entity at `(x>>6, y>>6)` (`src/ScriptThread.cpp:2249-2266`). Callers that
  want the sprite gone must hide it themselves — `EV_HIDE` does so via
  `removeEntity` afterwards (`src/ScriptThread.cpp:836-840`), and
  `getSprite() = (info & 0xFFFF) - 1` (`src/Entity.cpp:114-116`) survives the
  low-16-preserving info rewrite at `src/ScriptThread.cpp:2261`, so the hide
  lands on the same sprite index (`src/Game.cpp:187`).
* Rendered death pose: single corpse quad frame 13 (+ pulsating loot copy,
  see `character-animation.md` §5) — `src/Render.cpp:3470-3475`.

## 5. Glass tile 178 & the lift-shaft prop assembly (map00 case study)

Facts verified byte-level against `tmp_map00.bin` (layout provenance in
`docs/research/2026-08-25-elevator-glass.md`). Every claim cites `src/`.

* **Tile 178 = `TILENUM_GLASS`** (`src/Enums.h:792`); media range
  `mappings[178]=779..781` → exactly two frames: frame 0 whole, frame 1 broken
  (128×128 each). `postProcessSprites` gives it RENDERMODE **3** (ADD)
  (`src/Render.cpp:2478-2480`).
* A glass pane is an ordinary oriented map sprite (W bit, no TILE flag) drawn
  through the wall-decal branch; with the +257 TILE remap absent, art resolves
  directly from `mediaMappings[178]` at `src/Render.cpp:2047`.
* **Frame swap is the state machine**: op `EV_ENTITY_FRAME`
  (`src/ScriptThread.cpp:669-687`) rewrites only bits 8-15 of `mapSpriteInfo`,
  so a decor sprite's art frame can be scripted to the broken frame without
  any entity existing (NOENTITY sprites keep `S_ENT == -1`, `src/Game.cpp:398-400`).
* FLAT|TILE z-sprites (e.g. tiles 194/195/198+257 → wall media) are the
  engine's "floor slab" props: horizontal plane quads via the second
  viewStep axis (`src/Render.cpp:639-661`). Their raw tile ranges can be
  EMPTY (mappings[194] has 0 frames) — the +257 wall-range remap
  (`src/Render.cpp:1506-1517`) supplies the art.
* **The oriented branch only draws RAW textures**: the whole wall/slab block
  is guarded by `textureBaseSize == sWidth*tHeight` (portal tiles excepted,
  `src/Render.cpp:538-539`). `textureBaseSize = mediaTexelSizes2[texIdx]` is
  the exact on-disk texel byte count, `(mediaTexelSizes[i] & 0x3FFFFFFF)+1`
  (`src/LoadingManager.cpp:149-151`); RLE art (size ≠ w·h) would skip the
  branch entirely. All five lift-shaft media (779, 780, 888, 941, 942) measure
  RAW full-bounds, so the guard passes for them.
* **TWO_SIDED never doubles a draw**: it only switches the TinyGL software
  rasterizer to `CULL_NONE` (`src/Render.cpp:449-454`); the GLES path never
  enables `GL_CULL_FACE` at all (`src/GLES.cpp:230` is a lone `glDisable`),
  and oriented quads are submitted once (`src/Render.cpp:624-637`).
* **RENDER_ADD state** (glass, fires, anim-fires get mode 3 via
  `src/Render.cpp:2475-2480`): `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` + fog off,
  cached per (renderMode, renderFlags) pair (`src/GLES.cpp:615,648-652,709-715`).
* **Per-frame sprite order** (`src/Render.cpp:1752-1760`): leaves collected
  near→far by `walkNode` (with `cullBoundingBox` early-out, `:1053-1055`),
  drawn reversed; per leaf, sprites sorted by mvp-depth with the additive bias
  chain DECAL→MAX / TILE+6 / water→MIN / oriented+5 / entity±1
  (`src/Render.cpp:827-893`). Glass pane spr155 sorts +5 (oriented), one step
  behind its car side (+6 TILE), so the car draws first.
* **Split-sprite machinery is LIVE, not vestigial** (earlier revision of this
  file claimed otherwise — disproven 2026-08-26,
  `docs/research/2026-08-26-walk-flicker.md`): `getNodeForPoint` returns an
  **internal node** whenever a sprite's classify value lands in `(-128,128)`
  of that node's plane (`src/Render.cpp:2422-2424`; ±8 world units because
  relink feeds coords `<<4`, normals 16384-fixed). Map data snaps props onto
  tile edges that BSP planes reuse, so sprites rest dead-on planes (map00:
  spr53 x=992u == node 205 offset 15872 exactly). Those sprites live in the
  internal node's `nodeSprites` list; every frame `walkNode` snapshots
  `numVisibleNodes` before recursing (`src/Render.cpp:1085`) and afterwards
  feeds its list to `addSplitSprite` (`:1094-1096`), which lists the sprite
  under the FIRST visible leaf of the subtree whose bounds overlap the
  sprite's ±8-unit box (`:896-910`, cap 8/frame, `MAX_SPLIT_SPRITES`
  `src/Render.h:121`). `addNodeSprites` merges those pairs into the leaf's
  sorted draw list (`:917-922`). Net effect: a plane-straddling sprite draws
  once per frame from the correct side — no geometric clipping. Membership
  itself is refreshed per lerp tick by `relinkSprite`
  (`src/Game.cpp:2889-2896`).
* z-sprites parked out-of-bounds of every BSP leaf (e.g. before a cinematic
  moves them into place) simply never render until a scripted lerp completes:
  completion snaps position and calls `relinkSprite` unless `LS_FLAG_S_NORELINK`
  (`src/Game.cpp:3078-3243`; per-tick relink `:2889-2896`); script LERP ops
  never set that flag (`docs/original-code/lerp-opcodes.md` note table).
* Persistent prop states across loads are done with script vars + early
  ENTITY_FRAME on the INIT_MAP chain (map00: var v22 gates the broken-glass
  restorer), not by saving sprite info.

## 6. GL viewport geometry & `draw2DSprite` screen-space quads (added 2026-08-26)

### 6.1 The 3D GL viewport is NOT the full canvas

* `Canvas::startup` builds `screenRect = {0,0,480,320}`, `viewRect = {0, 20, 480,
  250}` (`src/Canvas.cpp:114-127`: `n2 = 320-35-35 = 250`, `n3 = n2 & ~1 = 250`,
  `viewRect[1] = 20` hardcoded), `cinRect = {0, 42, 480, 250}` (`:151-154`),
  `CAMERAVIEW_BAR_HEIGHT = 20` (`:149`).
* `TinyGL::setViewport(0,20,480,250)` shrinks by one pixel per side:
  `_setViewport(posX+1, posY+1, w-2, h-2)` with `posX = x - viewRect[0] = 0`,
  `posY = y - viewRect[1] = 0` → `viewportX/Y = 1/1`,
  **`viewportWidth = 478`, `viewportHeight = 248`** (`src/TinyGL.cpp:149-167`,
  `:129-140`).
* `gles::BeginFrame` turns that into the GL viewport: `posX = viewportX +
  viewRect[0] = 1`; the Y is **overridden by a hardcoded constant**:
  `posY = (posY == 37) ? 37 : 65` — with `posY = 1+20 = 21` it always takes
  **65** ("Panel_bottom status bar height?") (`src/GLES.cpp:119-127`), so
  `glViewport(1, 65, 478, 248)` in canvas units, GL y measured from the bottom
  (`src/GLES.cpp:82`). Canvas-space (y down) that is
  **x ∈ [1,479), y ∈ [7,255)** — top edge at canvas y = 320−65−248 = **7**.
  (`transformCoord2f` scales it to the window but casts `int vPortRect[4]` to
  `float*`, `src/GLES.cpp:140-141` / `src/GLES.h:56`; at a 480×320 drawable the
  factors are exactly 1.0 so the bit pattern survives — at any other window
  size the port's viewport is garbage. Reference values are the 1:1 ones.)
* HUD bars painted on top afterwards: `HUD_Panel_top.bmp` is 480×20 at (0,0)
  (`src/Hud.cpp:239`), `gameMenu_Panel_bottom.bmp` is 480×64 at
  `y = 320 − 64 = 256` (`src/TouchController.cpp:544-545`). So the visible 3D
  band is canvas y 20…255 while the *projection* is centred on canvas
  y = 7 + 248/2 = **131**.
  (Bottom-bar widget geometry and data sources: `docs/original-code/ui.md`.)
* Projection: `viewAspect = (viewFov<<14) / ((viewportWidth<<14)/viewportHeight)`
  (`src/Render.cpp:2223`) — i.e. `aspect ≈ fov·H/W`; `buildProjectionMatrix`
  sets `m[5] = cot(aspect/2)`, `m[0] = m[5]·aspect/fov`
  (`src/TinyGL.cpp:100-119`). With gameplay `fov = 290`: aspect = 150
  (`(478<<14)/248 = 31578`, `(290<<14)/31578 = 150`). The `m[0]/m[5] = H/W`
  identity makes screen offsets normalized by *width* isotropic — but NOT 1:1;
  the magnification is K ≈ 1.336 (see the CORRECTION in §6.2).

### 6.2 `Render::draw2DSprite(tile, frame, x, y, flags, mode, rFlags, scale)`

`src/Render.cpp:343-421`. Screen-space quad projected into world space so it
goes through the same GL state as the world:

* `v12 = (176 * scaleFactor) / 0x10000` — the quad is **v12 × v12 pixels**
  (`:358`); `0x10000 → 176`, `0x8000 → 88`.
* Corner layout: `vert1 = (x, y+v12)`, `vert2 = (x+v12, y+v12)`,
  `vert3 = (x, y)` (`:360-373`) → **top-left anchor at (x,y)**, extent v12
  down/right.
* World-space triplet: `v27 = ((x − vpW/2)<<15)/vpW`,
  `v28 = (((y+v12) − vpH/2)<<15)/vpW`, `v33 = (v12<<15)/vpW` (`:389-391`);
  `mv[0] = eye + v27·right + v28·col1`, `mv[1] = mv[0] + v33·right`,
  `mv[2] = mv[1] − v33·col1` (`:399-410`). `col1` is screen-DOWN
  (`TinyGL::viewMtxMove` negates its input before applying view[1]/[5]/[9],
  `src/TinyGL.cpp:203-221`), so mv = BL, BR, TR and
  `verts[3] = vert3 − (vert2 − vert1)` = TL (`src/GLES.cpp:511-513`).
* **CORRECTION (2026-08-26, `docs/research/2026-08-26-legacy-draw-path-and-bands.md`):
  the mapping is isotropic but NOT 1:1 — it magnifies by ≈1.336 about the
  viewport centre.** Earlier revisions of this file claimed "1:1 pixels"; that
  was wrong and it is the direct cause of the too-small/too-high view weapon in
  the rewrite. Derivation (exact, gameplay viewport 478×248, `fov = 290`):
  * `x_view = v27>>5`, `y_view = v28>>5`, quad edge `= v33>>5`
    (`src/Render.cpp:399-410`; `view[i]>>5` with `MATRIX_ONE = 16384` gives the
    `/32`), depth `w = 400` world units in front of the eye
    (`src/Render.cpp:386-388`: `5·((view[k]&~31) + 8·(view[k]>>5)) >> 8`
    = `5·1.25·16384/256` = 400, subtracted along `view[2]/[6]/[10]` which
    `viewMtxMove` uses as "forward", `src/TinyGL.cpp:203-208`).
  * `NDC_x = m[0]·x_view/(16384·400)`; window pixel offset from the viewport
    centre `= NDC_x·vpW/2`. Substituting `v27 = ((x−vpW/2)<<15)/vpW` the `vpW`
    cancels: **`Δpx = (x − vpW/2) · m[0]/12800`**. Likewise
    `Δpy = (y+v12 − vpH/2) · m[5]·vpH/(vpW·12800)`, and since
    `m[0] = m[5]·aspect/fov ≈ m[5]·vpH/vpW` both factors are the same K.
  * Numbers from the shipped sine table (`tables.bin` table 9, 65536-scale):
    `aspect = 150`, `m[5] = 33053`, `m[0] = 17098` ⇒
    **`Kx = 17098/12800 = 1.33578`, `Ky = 33053·248/(478·12800) = 1.33975`**
    (the 0.3 % anisotropy is the integer truncation of `aspect` 150.46→150).
  * Hence the canvas rect of a quad drawn at viewport-relative `(x,y)` with
    edge `v12`:
    `X = 240 + (x−239)·Kx`, `Ybottom = 131 + (y+v12−124)·Ky`,
    `Ytop = Ybottom − v12·Ky`, clipped to the viewport `(1,7,478,248)`.
    `(240,131)` is the canvas position of the viewport centre.
  * Cross-check: this is exactly why the software-TinyGL fallback hard-codes
    `scaleFactor *= 1.35f` (`src/Render.cpp:351-353`) — 1.35 is a hand-fitted
    stand-in for K. Both paths land the rifle within ~2 px of each other, see
    §6.4.
* `(x,y)` are pixel coordinates in the viewport's own frame (origin = viewport
  top-left = canvas (1,7), centre `(vpW/2, vpH/2) = (239,124)`), but only the
  centre maps 1:1; everything else is scaled by K as above.
* **UV window is fixed at 176 texels, not the whole texture**:
  `DrawWorldSpaceSpriteLine` writes s/t ∈ {0,1024} then rescales
  `s = s·176/sWidth`, `t = t·176/tHeight` (`src/GLES.cpp:539-542`). Weapon
  media are 256×256, so the quad samples exactly the **top-left 176×176
  texels** — 1:1 at scale 0x10000, 0.5× at 0x8000. The remaining 80 texel
  columns/rows of the media are never shown by this path.
* t flip: `Render.cpp:415` passes `flags ^ 0x20000`; inside, the s-flip test
  `((flags ^ 0x60000) & 0x20000)` is false and the t-flip test
  `((flags ^ 0x60000) & 0x40000)` is true for `flags = 0`, giving t = 0 at the
  screen-top corners → **image row 0 at the top of the quad** (`src/GLES.cpp:
  520-536`).
* `renderMode` 5 = `RENDER_ADD50`: `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` with
  `glColor4f(0.5,0.5,0.5,1)` (`src/Render.h:23`, `src/GLES.cpp:660-664`).
* `RENDER_FLAG_SCALE_WEAPON` multiplies scaleFactor by 1.35f — software-TinyGL
  path only (`src/Render.cpp:351-353`); it emulates the GL path's K ≈ 1.336
  centre magnification (see the CORRECTION above and §6.4).

### 6.3 Cinematics use the SAME GL viewport — `cinRect` is NOT a viewport rect

Verified 2026-08-26 (all four links re-read):

1. `Canvas::setState` (**not** `Canvas::render`) — the `ST_CAMERA` branch calls
   `app->tinyGL->setViewport(cinRect[0..3])` = `setViewport(0, 42, 480, 250)`
   (`src/Canvas.cpp:1207-1216`, function head `src/Canvas.cpp:1020`).
2. `TinyGL::setViewport` rebases against `viewRect` and shrinks by 1 px/side:
   `posX = 0−0 = 0`, `posY = 42−20 = 22`, `_setViewport(1, 23, 478, 248)`
   (`src/TinyGL.cpp:149-167`, `:129-140`).
3. `TinyGL::setView` (**not** `TinyGL::flush`) forwards them:
   `_gles->BeginFrame(viewportX=1, viewportY=23, 478, 248, view, projection)`
   (`src/TinyGL.cpp:193`, function head `:178`).
4. `gles::BeginFrame` then **discards that y**: `posY = viewportY + viewRect[1]
   = 23+20 = 43`, and `posY = (posY == 37) ? 37 : 65` → **65**
   (`src/GLES.cpp:119-127`).

⇒ The cinematic GL viewport is `glViewport(1, 65, 478, 248)` — **bit-identical
to gameplay**, canvas `(1, 7, 478, 248)`. The 3D image does not move by a single
pixel when a cutscene starts, and `viewportWidth/Height` are unchanged (478×248).

**But the FOV does change** (correction 2026-08-26, second pass): the fov is a
hardcoded literal at each `Render::render` call site, and `MayaCamera::Render`
uses two different ones (`src/MayaCamera.cpp:301-323`):

```
void MayaCamera::Render() {
    if (app->canvas->state == Canvas::ST_DIALOG) {
        app->render->render(this->x, this->y, this->z, this->yaw, this->pitch, this->roll, 290);
    }
    else {
        app->render->render(this->x, this->y, this->z, this->yaw, this->pitch, this->roll, 315);
    }
```

Full inventory of fov literals (`grep 'render->render('` + `'renderScene('`):

| state | fov | site |
|---|---|---|
| ST_PLAYING / ST_COMBAT, not zoomed | 290 | `src/MovementController.cpp:545` |
| zoomed in (scope) | `zoomFOV`→`zoomDestFOV` lerp, base 190, floor 102 | `src/MovementController.cpp:528-541`, `src/ZoomController.cpp:27,120-121`, touch `src/TouchController.cpp:148` |
| **ST_CAMERA (cinematic)** | **315** | `src/MayaCamera.cpp:309` |
| ST_DIALOG with an active camera | 290 | `src/MayaCamera.cpp:306` |
| ST_BENCHMARK / render-only | 290 | `src/GameStateRunner.cpp:157` |
| dying / familiar-dying / menu / loading / minigames | 290 | `src/GameStateRunner.cpp:267,274,290,308,315`, `src/LoadingManager.cpp:724`, `src/MiniGameManager.cpp:155,207` |

Reached via `Canvas::updateView` → `MovementController::updateView`
(`src/MovementController.cpp:377`): `if (game->isCameraActive() && state !=
ST_INTER_CAMERA) { activeCamera->Update(...); activeCamera->Render(); return; }`
(`:518-523`), and ST_CAMERA dispatches to `updateView` at `src/Canvas.cpp:948-953`.

**There is no per-camera or per-key FOV in the map data.** Maya camera keys have
exactly 7 planes — X, Y, Z, PITCH, YAW, ROLL, MS — `Game::setKeyOffsets`
(`src/Game.cpp:576-584`, fields `src/Game.h:35-41`), and `aggComponents[6]` /
`mayaTweenIndices[i*6+j]` carry the same six channels
(`src/MayaCamera.cpp:135-147,204-240`). `grep -i fov` over `MayaCamera.cpp/.h`
and `Game.h` → nothing.

Resulting projections (`src/Render.cpp:2223` `viewAspect =
(viewFov<<14)/((vpW<<14)/vpH)`, `(478<<14)/248 = 31578`):

| | gameplay | cinematic |
|---|---|---|
| fov | 290 | **315** |
| aspect | `(290<<14)/31578` = **150** | `(315<<14)/31578` = **163** |
| `m[5]` / `m[0]` | 33053 / 17098 | 30203 / 15629 |
| hfov / vfov | 87.56° / 52.73° | 92.70° / 56.96° |
| K (weapon magnification, §6.2) | Kx 1.33578 / Ky 1.33975 | **Kx 1.22102 / Ky 1.22423** |

The cinematic letterbox is **2D painted over the world**, not a viewport change.
CORRECTION (2026-08-26, `docs/research/2026-08-26-cinematic-letterbox.md`): an
earlier revision of this section attributed the letterbox to the cockpit overlay
art. That was wrong — the bars are two **opaque black `eraseRgn` fills** issued
by `Hud::drawCinematicText` (`src/Hud.cpp:455-456`), and the cockpit art is a
separate script-toggled effect (`hud->cockpitOverlayRaw`, opcode 76
`EV_TOGGLE_OVERLAY`, `src/ScriptThread.cpp:1710-1713`) that is OFF by default.
Full geometry, gating and draw order: `docs/original-code/cutscenes-camera.md`
§7. The only *geometry* that reacts to `cinRect`/`CAMERAVIEW_BAR_HEIGHT` is the
cinematic view weapon: `scrY -= CAMERAVIEW_BAR_HEIGHT (20)` when
`state == ST_CAMERA && game->cinematicWeapon != -1` (`src/Combat.cpp:701-705`;
the constant is set in `Canvas::startup`, `src/Canvas.cpp:149`).

**Do not treat `cinRect = {0,42,480,250}` as a render band.** It reaches the GL
layer only to be overwritten by the constant 65. Wiring it into a viewport gives
the world image a visible downward step at cutscene entry (its top edge would go
from canvas 7 to canvas 42, its centre from 131 to 167) — the original has no
step at all, because both states share `glViewport(1,65,478,248)`.

### 6.4 Which draw path a legacy binary actually runs (`gles::isInit`)

There is **no compile-time switch**: `src/CMakeLists.txt` passes no `-D`
whatsoever (confirmed empty `CXX_DEFINES` in
`build/src/CMakeFiles/DoomIIRPG.dir/flags.make`), and `grep '^#if'` over `src/`
finds only `_WIN32` and dead `#if 0 // J2ME/iPhone` blocks. TinyGL-software vs
GL is a **runtime bool**, `gles::isInit` (`src/GLES.h:40`):

* set `true` unconditionally by `gles::GLInit` (`src/GLES.cpp:44`), called from
  `Render::startup` (`src/Render.cpp:162-164`) via `Applet::startup`
  (`src/App.cpp:107`);
* then **overwritten from the config file** a few lines later:
  `Game::loadConfig` reads `_glesObj->isInit = IS.readBoolean()` in the
  "[GEC] Port Configurations" block (`src/Game.cpp:2036`, written at
  `src/Game.cpp:1959`), and `game->loadConfig()` runs AFTER `render->startup()`
  (`src/App.cpp:107` vs `:120`).

So a default run (no `CONFIGFILENAME`) uses the **GL path**; a run with a saved
config uses whatever that file says. `src/` itself contains no UI to toggle it,
but the reference binary `build/src/DoomIIRPG` does (strings `Video Options` /
`Window Mode:` / `VSync:` / `Resolution:` / **`TinyGL:`** / `Apply Changes`,
none of which exist in `src/`), i.e. that binary was built from a newer/extended
tree than this repo's `src/`.

Config layout (`src/Game.cpp:1913-1971`, little-endian, `writeBoolean` = 1 byte)
— `isInit` is at **file offset 146**, total size 791 bytes, trailer
`0xDEADBEEF` at 787.

Rifle (weapon 0, idle, `sx=sy=0`) — the two paths agree within ~2 px, which is
the strongest available cross-check of K:

| | GL path (`isInit == true`) | software TinyGL (`isInit == false`) |
|---|---|---|
| anchors | `scrX = 480/2−44 = 196`, `scrY = 320/2−29 = 131` (`src/Combat.cpp:627-628`) | `scrX = screenW/2−62 = 178`, `scrY = screenH/2−18 = 107` (`src/Combat.cpp:631-633`; `screenW/H = viewRect[2..3] = 480/250`, `src/Render.cpp:156-159`) |
| per-weapon nudge | none (weapon 0) | none for weapon 0 (`:636-668` has 1,2,3-6,7,8,10-13) |
| lowered offset | `LOWEREDWEAPON_Y = 38` | `38+13 = 51` (`:634`) |
| scale flag | none | `RENDER_FLAG_SCALE_WEAPON` → `scaleFactor = (int)(65536·1.35f) = 88473`, `v12 = 237` |
| quad, viewport frame | `(183,43)` size 176 | `(165,19)` size 237 |
| quad, canvas | `(165.2, 22.2)-(400.2, 258.0)` (K applied) | `(165, 22)-(402, 259)` (raster row 0 = backbuffer row 3, `src/TinyGL.cpp:164`; `posY` forced to 3, `:155-157`) |
| visible gun art (media bounds x 81…174, y 104…177 of the 176 window) | canvas **(273, 162)-(399, 261)** | canvas **(274, 162)-(401, 262)** |
| clip | both clipped at the viewport bottom, canvas y = 255 | idem |
| muzzle flash | `(x+flashX+40, y+flashY+40)`, scale 0x8000 → 88 px quad, ≈117 px on canvas at (219,92) | `(x+flashX+55, y+flashY+60)`, scale 0x8000 + 1.35 flag → 118 px quad at (221,94) (`src/Combat.cpp:826-834`) |
| familiar nudge | none | `x += 8, y -= 7` (`src/Combat.cpp:790-794`) |

Both paths render the world into the same canvas band: GL
`glViewport(1,65,478,248)` → canvas `(1,7,478,248)`; software raster base row 3
plus `viewportY = 3+1 = 4` → canvas rows 7…254 as well, and `Render::drawRGB`
forces `viewportY = 0, viewportHeight = 320` for its border rect in that mode
(`src/Render.cpp:2947-2951`).
