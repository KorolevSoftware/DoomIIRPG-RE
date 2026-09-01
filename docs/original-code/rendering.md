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

## 7. Sprite frame animation: the three independent mechanisms (added 2026-08-31)

A map sprite's frame index lives in `mapSpriteInfo[n]` bits 8..15
(`int n7 = (n2 & 0xFF00) >> 8`, `src/Render.cpp:1509`). There are exactly three
ways a sprite ends up showing more than one frame.

### 7.1 Generic auto-animation — bit `0x80000` (`SPRITE_FLAG_AUTO_ANIMATE`)

`src/Render.cpp:1544-1546`:

```cpp
if ((n2 & 0x80000) != 0x0) {
    n7 = (n + app->time / 100) % n7;
}
```

So bit `0x80000` reinterprets bits 8..15 as a *frame count* (not a frame index),
advances one frame per 100 ms, and phase-shifts by the sprite index `n`.
`Enums.h:1234` names the bit. Note the implicit contract: if `0x80000` is set the
frame-count field MUST be non-zero, otherwise this is a division by zero.

### 7.2 Load-time injections in `Game::loadMapEntities` — the COMPLETE list

The per-sprite loop `src/Game.cpp:374-397` is the only place in map loading that
writes animation data. `n6` = tile number (`info & 0xFF`, +257 when `0x400000` is
set, `:375-378`), `n7` = default frame count from the media table
(`mediaMappings[n6+1] - mediaMappings[n6]`, `:379`).

| Tile | Name | Frames written | Bits set | Extra |
|---|---|---|---|---|
| 234 | `TILENUM_ANIM_FIRE` | `n7 = 4` forced (`:380-382`), written by the 136/234/130 branch | `0x80000` (`:394-397`) | — |
| 156 | `TILENUM_EYE_PORTAL` | `n7 = 2` forced | `0x80200` (`:383-387`) | frame field first cleared with `&= 0xFFFF00FF` |
| 236 | `TILENUM_AIR_VENT` | `n7 = 3` forced | `0x80300` (`:388-392`) | `mapSprites[S_RENDERMODE + n5] = 3` (`:392`) |
| 136 | `TILENUM_OBJ_TORCHIERE` | media count (=1 for 136) | `0x80000` (`:394-397`) | — |
| 130 | `TILENUM_OBJ_FIRE` | media count (=4) | `0x80000` | — |

That is all — five tiles, three `if` blocks. Nothing else in `loadMapEntities`
touches bits 8..15 or `0x80000`; the only other frame-field write in the loop is
`mapSpriteInfo |= 0x200` for tiles 1..12 (doors, `src/Game.cpp:405-407`), a
static frame index, not an animation. Verified by enumerating every
`mapSpriteInfo` reference in `src/Game.cpp` (lines 375-494 for the loader).

Caveat on the `0x80200` / `0x80300` constants: the low byte `0x200` / `0x300`
*is* the `n7 << 8` value, so the OR is redundant with `n7 << 8` — they encode
"2 frames" and "3 frames" respectively.

### 7.3 Hard-coded per-tile render branches (no `0x80000` involved)

`Render::renderSpriteObject` special-cases tiles after the generic frame
computation, and several of these compute their own frame from `app->time`:

- **Tile 134 `TILENUM_WATER_SPOUT`** — `src/Render.cpp:1648-1653`:
  ```cpp
  if (n3 == Enums::TILENUM_WATER_SPOUT) {
      int n15 = app->time / 128;
      this->renderSprite(x, y, z, n3, (n15 & 0x1), n2, renderMode, scaleFactor, n10);
      return;
  }
  ```
  A 2-frame ping-pong at 128 ms per frame (~7.8 fps), global phase (no per-sprite
  offset), and the `return` skips the generic `renderSprite` at `:1727`. The frame
  bits of `mapSpriteInfo` are ignored entirely. `mediaMappings[134..135] = 733,
  735` → exactly 2 media images, matching the `& 1`.
- Tile 156 `EYE_PORTAL` — `:1622-1641`, frame 1 when visible, else frame 0 plus a
  1536 ms XOR flicker on bits 17/18.
- Tile 136 `TORCHIERE` — `:1642-1647`, adds a `SFX_LIGHTGLOW1` sprite whose flags
  XOR bit 17 by `n7 & 1` (that is where the flicker comes from — the tile itself
  has only 1 media image).
- Tile 240 `WATER_STREAM` — `:1548-1551` → `renderStreamSprite`, which scrolls the
  texture T coordinate by `app->time * 3 & 0x3FF` (`src/Render.cpp:1495`), i.e.
  UV scrolling, not frames.
- Destroyable-object shake (`eType 10 / eSubType 2`) advances the frame from
  `entity->param` every 200 ms, capped at 3 (`src/Render.cpp:1607-1615`).
- Monsters go through `renderSpriteAnim` (`:1620-1621`).

Gate to remember: the whole special-case chain lives in the `else` of
`if ((n2 & 0x400000) != 0x0)` (`src/Render.cpp:1618-1620`). `0x400000` in
*mapSpriteInfo* means "extended tile, +257", so a tile-134 sprite can never have
it, and the spout branch is always reached. (The entity-side `info |= 0x400000`
set by the spout conversion is a different field — it is the "spout is on" flag
read by `Entity.cpp:1466-1470`.)

### 7.4 Consequence for the water spout (both origins animate)

- **Pre-placed map spout** (map00 sprite 44 at (14,10), `info 0x00000086`): no
  `0x80000`, frame field 0 — yet it animates, because tile 134 never uses the
  generic path (`src/Render.cpp:1648-1653`).
- **Converted toilet/sink** (`mapSpriteInfo = (info & 0xFFFFFF00) | 134`,
  `src/ArmorRepairSystem.cpp:61`): identical on screen. Tiles 123/127 receive no
  load-time injection (they are absent from the §7.2 list), so no `0x80000` is
  inherited and the same 128 ms branch runs.

This corrects `docs/original-code/combat.md` §11 / the 2026-08-31 water-spout
report, which claimed the spout is a static sprite because `0x80000` is missing.

## 8. Sprite blend modes and color modulation (added 2026-08-31)

### 8.1 The enum

`src/Render.h:18-31` — a closed 14-value space (`RENDER_MAX = 14`; 11 is unused):

| val | name | notes |
|---|---|---|
| 0 | `RENDER_NORMAL` | default for everything |
| 1 | `RENDER_BLEND25` | |
| 2 | `RENDER_BLEND50` | |
| 3 | `RENDER_ADD` | full additive |
| 4 | `RENDER_ADD75` | additive, 75 % |
| 5 | `RENDER_ADD50` | additive, 50 % |
| 6 | `RENDER_ADD25` | additive, 25 % |
| 7 | `RENDER_SUB` | |
| 8 | `RENDER_UNK` | no GL case → `assert(0)` (`src/GLES.cpp:702-705`) |
| 9 | `RENDER_PERF` | debug ("rasterize debug") mode |
| 10 | `RENDER_NONE` | draws nothing |
| 12 | `RENDER_BLEND75` | "New from IOS", 3D path unreachable |
| 13 | `RENDER_BLENDSPECIALALPHA` | "New from IOS", 3D path unreachable |

`renderMode` travels as `Render::renderSprite(..., renderMode, ...)` /
`draw2DSprite(..., renderMode, ...)` → `Render::setupTexture(tile, frame,
renderMode, renderFlags)` (`src/Render.cpp:442`, `:356`), which forwards it to
**both** back-ends: `gles::SetupTexture` (`src/Render.cpp:2057`) and the
TinyGL span/palette selection (`src/Render.cpp:2060-2070`, plus
`Render::setupPalette` from inside `renderSprite`, `src/Render.cpp:504-507`).

Two overrides happen inside `Render::setupTexture` **before** dispatch
(`src/Render.cpp:2048-2053`):

```cpp
if ((app->canvas->state == Canvas::ST_AUTOMAP) || (this->renderMode & 0x10) == 0x0) {
    renderMode = 10;                 // RENDER_NONE — automap / span rasterizer off
} else if (this->renderMode & 0x20) {
    renderMode = 9;                  // RENDER_PERF — RENDER_RASTERIZE_DEBUG
}
```

(`Render::renderMode` here is the *debug pipeline mask* `RENDER_OFF/TRANSFORM/
CLIP/PROJECT/SPAN/RASTERIZE_SPAN/RASTERIZE_DEBUG`, `src/Render.h:75-83`, default
`RENDER_DEFAULT = 31` — an unrelated field with a confusingly similar name.)

### 8.2 GL path: exact state per mode

All of it lives in one switch in `gles::SetupTexture`, `src/GLES.cpp:615-706`,
guarded by a state cache:

```cpp
if (renderMode != this->renderMode || flags != this->flags)
{
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // [GEC] Default Combiner
    this->renderMode = renderMode;
    this->flags = flags;
    switch (renderMode) { ... }
```

| mode | `glBlendFunc(src, dst)` | primary color | fog |
|---|---|---|---|
| 0 `NORMAL` | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | `1,1,1,1` — or `1.0,0.5,0.5,1.0` if `RENDER_FLAG_BRIGHTREDSHIFT` | on |
| 1 `BLEND25` | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | `1,1,1,0.25` | on |
| 2 `BLEND50` | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | `1,1,1,0.50` | on |
| 3 `ADD` | `SRC_ALPHA, ONE` | `1,1,1,1` | **off** |
| 4 `ADD75` | `SRC_ALPHA, ONE` | `0.75,0.75,0.75,1` | **off** |
| 5 `ADD50` | `SRC_ALPHA, ONE` | `0.50,0.50,0.50,1` | **off** |
| 6 `ADD25` | `SRC_ALPHA, ONE` | `0.25,0.25,0.25,1` | **off** |
| 7 `SUB` | `ZERO, ONE_MINUS_SRC_COLOR` | `1,1,1,1` | **off** |
| 9 `PERF` | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | `1,1,1,0.50` | on |
| 10 `NONE` | `ZERO, ONE` | *not set* (leaks previous color) | **off** |
| 12 `BLEND75` | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | `1,1,1,0.75` | on |
| 13 `BLENDSPECIALALPHA` | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | `1,1,1,canvas->blendSpecialAlpha` | on |

Line anchors (`case` labels in `src/GLES.cpp`): `NORMAL` `:625`, `BLEND25`
`:636`, `BLEND50` `:642`, `ADD` `:648`, `ADD75` `:654`, `ADD50` `:660`, `ADD25`
`:666`, `SUB` `:672`, `PERF` `:679`, `NONE` `:686`, `BLEND75` `:691`,
`BLENDSPECIALALPHA` `:697`.

Facts that matter for a port:

* **The modulation factor is a single global `glColor4f` per batch**, combined
  with the texel by the *fixed* `GL_MODULATE` texture env (re-asserted at
  `src/GLES.cpp:620` on every state change — that is what undoes a previous
  `GL_COMBINE` from `TexCombineShift`). Vertex colors are **not** used at all:
  `glDisableClientState(GL_COLOR_ARRAY)` (`src/GLES.cpp:98`) and the vertex
  struct pushed to `glDrawElements` only has `xyzw` + `st`
  (`src/GLES.cpp:559-571`). So `Cframe = Ctex·Ccolor`, `Aframe = Atex·Acolor`.
* Additive modes keep `GL_SRC_ALPHA` as the *source* factor (not `GL_ONE`; the
  original `glBlendFunc(GL_ONE, GL_ONE)` is commented out at `:650`), so
  `dst += Atex · (k · Ctex)` — a transparent-mask texel with `A = 0` still adds
  nothing, and half-transparent edges add half. `k` is the 1/0.75/0.5/0.25 above.
* `RENDER_SUB` in GL is **not** a true subtract: `(GL_ZERO,
  GL_ONE_MINUS_SRC_COLOR)` yields `dst = dst·(1 − Csrc)`, a multiply-by-inverse.
  The software rasterizer really subtracts (§8.3) — a deliberate divergence.
* Fog is toggled *by mode*: `fogMode = 2` (fog on, `fogColor`) for all
  alpha-blend modes, `0` (fog off) for `ADD*`, `SUB`, `NONE`
  (`src/GLES.cpp:709-715`). Additive sprites are therefore never fogged.
  `fogMode == 1` (`fogBlack`) is read at `:711` but never written — dead.
* **There is no depth buffer.** `glDisable(GL_DEPTH_TEST)` in `gles::SetGLState`
  (`src/GLES.cpp:89`) and `Main.cpp:116`; `grep` finds no `glDepthMask`,
  `glDepthFunc` or `glEnable(GL_DEPTH_TEST)` anywhere in `src/`. Correctness rests
  entirely on the ordering of §8.5. `glEnable(GL_BLEND)` is permanent
  (`src/GLES.cpp:104`).
* `RENDER_FLAG_*` color shifts are a **second, orthogonal** channel applied in
  the same block (`src/GLES.cpp:717-757`): `PULSATE` (512) overrides the blend
  func with `(SRC_ALPHA, ONE)` and animates `glColor4ub(v,v,v,v)` from
  `app->time >> 2` (triangle wave, floor 31); `RED/GREEN/BLUE_SHIFT` either
  multiply via `glColor4ub` (when `RENDER_FLAG_MULTYPLYSHIFT`, iOS behavior) or
  *add* a constant through `GL_COMBINE`/`GL_ADD` with a `GL_CONSTANT` of
  64/255 per channel (`gles::TexCombineShift`, `src/GLES.cpp:1258-1279`) — the
  J2ME/BREW look. Because these hijack the texture env, mode + flags must be
  cached together, which is exactly what `:615` does.
* The GL path also has one hard tile override *before* the switch:
  `if (n == Enums::TILENUM_SCORCH_MARK) renderMode = Render::RENDER_SUB;`
  (`src/GLES.cpp:609-611`), tile 212.

The 2D image path duplicates the numbering independently in
`Image::setRenderMode` (`src/Image.cpp:158-210`): 0 = `GL_REPLACE` + optional
alpha test, 1/2/12/13 = alpha 0.25/0.5/0.75/`blendSpecialAlpha`, 3 =
`glBlendFunc(GL_SRC_COLOR, GL_ONE)` with `GL_REPLACE`, 8 = modulate by the
current font color. Note 3 differs from the 3D path's `(SRC_ALPHA, ONE)`.

### 8.3 TinyGL software path: same factors, baked into the palette

Two independent pieces:

1. **Span function tables**, built once in the `Render` constructor
   (`src/Render.cpp:50-110`): `_spanTrans[mode]` for sprites/transparent tiles,
   `_spanTexture[mode]` for opaque wall textures; selected at
   `src/Render.cpp:2065-2074`. Notably `ADD/ADD75/ADD50/ADD25` all point at the
   *same* additive span (`src/Render.cpp:66-73`), and `_spanTrans[BLEND25]`
   aliases the plain transparent span (`:60`).
2. **Palette pre-scaling** in `Render::setupPalette` (`src/Render.cpp:1885-2022`).
   When no `RENDER_FLAG_*` shift is active and `renderMode != 0`
   (`:1981-2021`), the fog palette is copied into `scratchPalette` with a
   per-mode RGB565 scale:

| mode | palette transform (`src/Render.cpp`) | factor |
|---|---|---|
| 1 `BLEND25` | `(c & 0xE79C) >> 2` `:1992-1996` | 25 % |
| 3 `ADD` | `(c & 0xF7DE) >> 0` `:1997-2001` | 100 % (only drops 1 LSB/channel) |
| 4 `ADD75` | `((c & 0xE79C) >> 1) + ((c & 0xC718) >> 2)` `:2012-2017` | 75 % |
| 5 `ADD50` | `(c & 0xE79C) >> 1` `:2002-2006` | 50 % |
| 6 `ADD25` | `(c & 0xC718) >> 2` `:2007-2011` | 25 % |
| default (2, 7, 9, 10, …) | `(c & 0xE79C) >> 1` `:1986-1991` | 50 % |

The masks clear the low bits of each 5/6/5 field so the shift cannot bleed
across channels. The span then combines with the destination
(`src/Span.cpp:15-42`):

```cpp
blend25_565(dst,src) = ((dst & 0xF7DE) >> 1) + ((src & 0xC718) >> 2);
blend50_565(dst,src) = ((dst & 0xF7DE) >> 1) + ((src & 0xF7DE) >> 1);
add565 (dst,src)     = per-channel dst+src, clamped to 31/63/31;
sub565 (dst,src)     = per-channel dst-src, clamped to 0;
```

**Cross-check of the semantics:** `RENDER_ADD50` = additive span
(`spanAddTransparent`, `src/Span.cpp:171-185`, `*pixels = add565(*pixels,
palette[texel])`) over a palette halved by `(c & 0xE79C) >> 1` — literally "add
half the texel value", identical in meaning to the GL
`glColor4f(0.5,0.5,0.5,1)` + `(SRC_ALPHA, ONE)`. `RENDER_SUB` uses a true
per-channel clamped subtract here (`src/Span.cpp:230-244`) against a 50 %
palette, whereas GL does `dst·(1−Csrc)`.

Software quirks worth knowing (they explain small GL/TinyGL differences):
`BLEND50` gets *both* the 50 % palette and `blend50_565`, so it ends up
`dst/2 + src/4`; `_spanTrans[BLEND25]` writes the 25 % palette opaquely (no
destination blend) while `_spanTexture[BLEND25]` blends it again.

### 8.4 Who assigns a render mode

Per-sprite storage is `mapSprites[S_RENDERMODE + spriteIdx]`
(`src/Render.h:143`, base offset `numSprites * 3`, `src/LoadingManager.cpp:442`).

**By tile number, at map load** — `Render::postProcessSprites`
(`src/Render.cpp:2472-2494`; `n3` is the tile id, `+257` when
`mapSpriteInfo & 0x400000`):

| tile | mode | tile name |
|---|---|---|
| 479 | 0 `NORMAL` | `TILENUM_FLAT_LAVA` (extended id) |
| 208, 234, 130, 242 | 3 `ADD` | fog-gray, anim-fire, obj-fire, fireball |
| 178 | 3 `ADD` | `TILENUM_GLASS` |
| 236 | 3 `ADD` | `TILENUM_AIR_VENT` |
| 212 | 7 `SUB` | `TILENUM_SCORCH_MARK` |
| 161 | 2 `BLEND50` | `TILENUM_HELL_HANDS` |
| 244 | 4 `ADD75` | `TILENUM_BFG_BALL` / `ENERGY_END` |
| anything else | 0 `NORMAL` | |

The custom-sprite (48) and drop-sprite (16) pools are initialized to mode 0
(`src/Render.cpp:2505`, `:2519`). `Game::loadMapEntities` re-asserts mode 3 for
tile 236 (`src/Game.cpp:392`). `Entity::spawn`-side monster/decor sprites get 0
(`src/Entity.cpp:76`). `Game::gsprite_alloc` resets 0 (`src/Game.cpp:1315`).

**By code, per spawned effect:**

* `Game::gsprite_allocAnim` (`src/Game.cpp:1344-1362`): tile 241 `POOF` → 4,
  tile 234 `ANIM_FIRE` → 3 (+ scale 48), tile 242 `FIRE_BALL` → 3.
* `Combat::allocMissile` stores the caller's mode
  (`src/Combat.cpp:1683`); the value comes from the projectile switch
  (`src/Combat.cpp:1481-1569`): proj 2 → 3 (tile 240), 3 → 3 (243), 4 → 0
  (225/226), 5 → 4 (244), 6 → 0 (227), 7 → 3 (242), 8 → 3 (241), 9 → 0 (171),
  10 → 3 (252), 11 → 3 (248), 13 → 0 (weapon tile).
* Impact/explosion anim (`src/Combat.cpp:1384`) with the mode from
  `src/Combat.cpp:1292-1363`: 4 for anims 242/252/243/244/235, 3 for the
  buff-9 fog variant (tile 208), 0 otherwise.
* BFG (`attackerWeaponProj == 5`) death flash: tile 244, mode 4, scale 32
  (`src/Combat.cpp:1164-1165`).
* Melee hit sparks 245/246/247: mode 5 `ADD50` (`src/Combat.cpp:1415`).
* Monster status effects **force mode 0** so the color shift is visible:
  `monsterEffects & 2/8/1` → `BLUE/RED/GREEN_SHIFT` and `renderMode = 0`
  (`src/Render.cpp:1587-1598`).
* Screen-space sprites: burning-player overlay draws tile 234 with mode 3
  (`src/Combat.cpp:617`); the extra muzzle flash for weapons in mask `0x181`
  draws with mode 5 (`src/Combat.cpp:833`); the weapon itself is always mode 0
  (`src/Combat.cpp:785`).
* Wall geometry (not sprites) in `Render::drawNodeGeometry`
  (`src/Render.cpp:950-963`): tile 161 `HELL_HANDS` → `BLEND50`; tiles 302
  `FADE` / 212 `SCORCH_MARK` → `NORMAL` on GL but `RENDER_SUB` on TinyGL
  ("`[GEC] TinyGL Only like J2ME/BREW`"); tiles 479/480 `FLAT_LAVA*` → `NORMAL`
  with `CULL_NONE`.
* Tile-based override in the GL back-end only: tile 212 → `RENDER_SUB`
  (`src/GLES.cpp:609-611`).

### 8.5 The torchiere glow (tile 136 → tile 193 in `ADD50`)

`src/Render.cpp:1643-1646`, inside the `else` of `if ((n2 & 0x400000) != 0)`:

```cpp
else if (n3 == Enums::TILENUM_OBJ_TORCHIERE) {          // tile 136
    int zheight = ((10 * scaleFactor) / 65536) << 4;
    n2 ^= (n7 & 0x1) << 17;
    this->renderSprite(x, y, z + zheight, Enums::TILENUM_SFX_LIGHTGLOW1, 0,
                       n2, Render::RENDER_ADD50, scaleFactor, n10);
}
```

* Glow tile is 193 `TILENUM_SFX_LIGHTGLOW1` (`src/Enums.h:796`), frame **0**
  (single media image), blend mode hard-coded `RENDER_ADD50` (5) — it does **not**
  read `S_RENDERMODE` (which is 0 for tile 136, since 136 is absent from the
  §8.4 table).
* Vertical offset: `zheight = ((10 * scaleFactor) / 65536) << 4`. `scaleFactor`
  arrives as `mapSprites[S_SCALEFACTOR] << 10` (`src/Render.cpp:1512`), so for
  the default scale 64 → `scaleFactor = 65536` → `zheight = 10 << 4 = 160` in the
  `z << 4` units used by `renderSprite`, i.e. +10 map-Z units. Scale is passed
  through unchanged → the glow quad is exactly the size of the body quad.
* `n2 ^= (n7 & 1) << 17` flips bit `0x20000` = horizontal mirror
  (`src/GLES.cpp:520-525`, inverted logic `flags ^ 0x60000`) on odd frames. The
  mutated `n2` is reused for the body draw below, so both flicker in sync. Tile
  136 receives `0x80000` auto-animate at load (`src/Game.cpp:394-397`), which is
  what advances `n7`.
* **Order:** the branch does *not* `return`, so execution falls through to the
  generic `this->renderSprite(x, y, z, n3, n7, n2, renderMode, scaleFactor, n10)`
  at `src/Render.cpp:1728`. The glow is drawn **first**, the lamp body **over**
  it. Both quads belong to one sprite entry, hence one sort key — the pair is
  never split by the depth sort.

### 8.6 Tile 240 `TILENUM_WATER_STREAM` — short note

`src/Render.cpp:1548-1551` diverts the tile to `Render::renderStreamSprite`
(`src/Render.cpp:1412-1495`) and returns. It is not a billboard and not frame
animation: a single 4-vertex quad is stretched between two *world* points — the
sprite position and either the camera/target point (`destX/destY/destZ`, entity
index 1, `:1426-1447`) or another entity's sprite (`:1452-1463`) — with `CULL_NONE`
(`:1465`). The illusion of flow comes from UV scrolling: `n36 = n31 -
(app->time * 3 & 0x3FF)` (`src/Render.cpp:1486`) with the V span tiled 3×
(`n32 = (n28 << 10) / tHeight * 3`, `:1473`). Blend mode is whatever
`S_RENDERMODE` holds (`:1420`): 0 for a map-placed stream (240 is not in the
§8.4 table), and 3 `RENDER_ADD` when it is spawned as projectile type 2
(`src/Combat.cpp:1481-1486`). Its sort key is forced to `0x80000000`
(`src/Render.cpp:848-851`) so it is drawn last, on top of everything.
(This supersedes the §7.3 citation of `:1495` for the T scroll — the correct
line is `:1486`.)

### 8.7 Draw order: the mode never affects sorting

* `Render::renderBSP` walks the visible nodes **back to front**:
  `for (int i = this->numVisibleNodes - 1; i >= 0; --i)` — geometry first
  (`drawNodeGeometry`), then that node's sprites (`src/Render.cpp:1752-1763`).
* Inside a node, `Render::addSprite` (`src/Render.cpp:822-892`) computes a
  view-space depth key `n3 = (x·mvp[2] + y·mvp[6] + z·mvp[10] >> 14) + mvp[14]`
  (`:837-845`), applies **tile-based** biases, and inserts into the
  `S_VIEWNEXT` list in *descending* key order (`:882-891`) — farthest drawn
  first. Biases (`:846-873`): extended tiles `+6`; tiles 240/245/246/247 →
  `0x80000000` (always last); `mapSpriteInfo & 0xF000000` → `+5`; player/hidden
  flags `+1`; monsters `−1`; tiles 240-244/255 `−3`; 137/138/139 `+2`; 152 `+5`;
  239 `−3`; `0x10000000` → `0x7fffffff` (always first).
* The key is derived from position and tile only — `S_RENDERMODE` is never
  consulted. There is **no separate additive pass and no batching by mode**:
  every sprite issues its own `SetupTexture` + draw, and the only optimization is
  the `(renderMode, flags)` state cache at `src/GLES.cpp:615`.
* The sole "reorder for looks" hacks are the mastermind/caldex delayed-sprite
  buffers, which defer specific tiles to after the whole walk
  (`src/Render.cpp:1518-1541`, drained at `:1764-1771`).
