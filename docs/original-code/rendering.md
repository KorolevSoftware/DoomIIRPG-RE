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
