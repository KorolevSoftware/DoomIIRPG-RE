# Elevator shaft glass & floor-volume sprites (map00 intro)

Date: 2026-08-25 · Topic: identity + render path of the shaft-glass pane and the
shaft floor slabs near the crashed lift; whole→broken glass mechanism; why the
rewrite shows neither. Method: marker-anchored parse of `tmp_map00.bin`
(bytecode file offset 62019, sprites X@59078/Y@59339/info_lo@59600/info_hi@59865/
Z-bytes@60391/anim-bytes@60521, N=261 = 135 normal + 126 z-sprites; staticFuncs
LE-u16 at 60651; tileEvents LE-i32 pairs at 60679), CFG disassembly of the map00
bytecode with legacy-exact operand formats (`src/ScriptThread.cpp:230-2044`,
operands BIG-endian `:2103-2119`, loop-tail `++IP` at `:2043`), leaf-membership
simulation of `getNodeForPoint` against the real BSP arrays, media extraction
from `tmp_newMappings.bin`/palettes/texels, and cross-checks of
`new_src/domain/world/MapParser.cpp`, `new_src/render/World3D.cpp`,
`new_src/core/GameContext.cpp`, `new_src/domain/game/ScriptVM.cpp` plus the
runtime logs in `build_new/new_src/*.log`.

## Headline verdicts

* **Glass sprite = spr155** (tileNum 178 = `TILENUM_GLASS`,
  `src/Enums.h:792`). Two media frames: **frame 0 = whole** (media 779),
  **frame 1 = broken** (media 780). PARTIAL→CONFIRMED identity; the *reason it
  does not draw in the rewrite* is NOT proven to be a single divergence — every
  static stage verifies equal to legacy, so the report names the two remaining
  suspects and gives the stderr probes that decide between them.
* **Whole→broken mechanism = CONFIRMED**: `ENTITY_FRAME` (op 17) writes the
  packed anim byte (`src/ScriptThread.cpp:669-687`) setting spr155's frame to 1,
  once inside the crash cinematic (IP 3797) and once in the per-load state
  restorer gated on script var v22 (IP 1065).
* **Floor-volume sprites = the FLAT|TILE z-sprites spr154/spr152 (+spr149)**:
  horizontal plane quads (tile 194+257 → wall media 451, tile 198+257 → 455,
  tile 195+257 → 452). In the rewrite they cannot lie flat — **no FLAT plane
  branch exists** (known divergence #3 in `docs/original-code/sprite-placement.md`;
  `new_src/render/World3D.cpp:728-742` always emits a vertical quad) — which is
  the single confirmed renderer divergence affecting exactly these sprites.

## 1. The cast around the shaft (tiles x=1..2, y=16..21)

All values parsed from `tmp_map00.bin` (layout above); flags per
`src/Enums.h:1231-1248`; Z below is post-`postProcessSprites`
(`src/Render.cpp:2459-2467`: `S_Z += getHeight`, z-sprites `-32`).

| spr | art | flags | px | S_Z raw | final Z | role |
|---|---|---|---|---|---|---|
| 149 | 195 (+257→wall tex 452) | FLAT\|TILE\|SOLID\|2SIDE\|NOENT | (160,1376)=(2,21) | 128 | 544 | shaft floor slab B |
| 150/151 | 202 E\|W | 2SIDE\|NOENT | (128,1376) | 64 / 16 | 480 / 432 | metal-grate platform halves (animate in cam-1 scene) |
| 152 | 198 (+257→455) | FLAT\|TILE\|SOLID\|FLIPV\|2SIDE\|NOENT | (96,1056)=(1,16) | 64 | 32 | flat slab (moves to (2,19) in crash cinematic) |
| **153** | 52 (+257→**309**, 256×256 full-bounds wall art) | W\|TILE\|SOLID\|FLIPV\|2SIDE\|NOENT | (128,1248)=(2,19) | 128 | 544 | **lift/elevator car side** |
| **154** | 194 (+257→**451**) | FLAT\|TILE\|SOLID\|FLIPV\|2SIDE\|NOENT | (96,1248)=(1,19) | 192 | 608 | **flat slab at cab top height** |
| **155** | **178 GLASS**, frame 0 | W\|2SIDE\|NOENT | (128,1248)=(2,19) | 192 | 608 | **the glass pane** |
| 156/157 | 20 frame 13 (lying pose) | NOENT (157 +FLIPH) | (96,1120)=(1,17) | 64 | 32 | corpse props at shaft bottom |
| 205/206 | 72 | NOENT (206 +FLIPH) | ≈(1,19) | 192 | 608 | debris chunks |

Notes:
* Raw tiles 194/195/198 have **zero frames** in the plain mapping range
  (mappings[194]=799..799 etc.); their art only exists behind the
  SPRITE_FLAG_TILE +257 remap (mappings[451]=941, mappings[452]=799? see
  histogram caveat — 451 and 455 both resolve to one 256×256 full-bounds
  frame each). The +257 remap is load-bearing here
  (`src/Render.cpp:1506-1517`, rewrite mirror
  `new_src/render/World3D.cpp:593`).
* Media stats (decoded via `tools/extract_textures.py` logic): glass f0
  avgRGB(62,85,113) bluish translucent-looking; f1 darker avgRGB(33,46,54)
  with hole pattern — visually "whole" vs "shattered".
* Leaf simulation with the real normals/nodeOffsets/bounds arrays reproduces
  legacy relink: spr153/154/155/205/206 → **leaf 161** (valid), spr158 hazard
  bar → leaf 162, platform grates → leaf 173. spr152/156/157 start out-of-leaf
  (OOB) until the cinematic moves them — matching legacy behaviour where
  `relinkSprite` runs again after each scripted lerp
  (`src/Game.cpp:2889-2896`, completion snap `:3078-3243`).

### Where the glass quad actually sits (both engines' math)

Wall-branch geometry for spr155 (W → orient index 4,
`src/Render.cpp:526-537`): horizontal axis `viewStepValues[((4+2)&7)<<1]`
(`src/Canvas.h:108`) ⇒ the pane lies in the **plane x = 128px** (the x=1/x=2
tile boundary), spanning y ∈ [1216,1280]px. Size from bounds
(b=[0,128,1,129], 128×128 texture): w=32, h=64 map units. Z corrections
`z += (tHeight - bounds[3])<<4; z -= 16*(scaleFactor/2048)`
(`src/Render.cpp:552-558`) put the pane bottom at 575 map units — stacked
directly above the car side spr153 (bottom 512, top 576, same plane). The
rewrite computes identical numbers (`new_src/render/World3D.cpp:708-728`).

## 2. Whole → broken mechanism (bytecode, byte-verified)

`ENTITY_FRAME` payload `B sprite, B frame, B wait×100ms`; effect
`info = (info & 0xFFFF00FF) | (frame<<8)` (`src/ScriptThread.cpp:671-675`).

### (a) Per-load restorer — reached on every INIT_MAP chain

Function body (straight-line, hand-verified byte-by-byte):

```
1028: LERPSPRITE spr=77 dst=(19,4) fl=12          ; unrelated NPC park
1032: EVAL [v22]==1 iff-> 1072                     ; joff byte @1038 = 33
1039: LERPSPRITE spr=154 dst=(1,19) z=24 async     ; instant snap (t absent)
1044: LERPSPRITEOFFSET 155 t=0 dst=(127,1248) dz=+32 async
1051: LERPSPRITEOFFSET 156 t=0 dst=…      async
1058: LERPSPRITEOFFSET 157 t=0 dst=(96,1232) dz=+32 async
1065: ENTITY_FRAME 155 frame=1 wait=0              ; ★ GLASS BREAKS
1069: JUMP -> 1072
```

Polarity (`src/ScriptThread.cpp:306-310`: jump when `pop()==0`):
* **fresh game (v22=0)** → expression false → jump → parking AND the frame
  swap are skipped → glass stays **whole** (map-file initial state already
  equals the parked pose).
* **after the crash played once (v22=1)** → fall-through → pieces re-parked +
  `ENTITY_FRAME 155 frame=1` → every later load renders the **broken** pane.

Runtime proof from `build_new/new_src/boot_dbg.log`:
`[dbg] LERPSPRITE spr=77 … flags=12` (IP 1028 executed) followed by
`[dbg] LERPSPRITE spr=169 …` (IP ~1079, the next block) with **no** sp154/155
lerps and **no** `[script] ENTITY_FRAME sprite=155` — exactly the v22=0 path.

### (b) Crash cinematic — event[48], trigger tile **(18,19)**

`tileEvents[48] = {word0 0x02620000|tile 610, word1 0xFF1}` → IP 3703, fires
when the player enters tile 610 = (18,19) with matching trigger mask
(`src/ScriptThread.cpp:97-115`; rewrite `new_src/domain/game/ScriptVM.cpp:136-163`).
The scripted GOTO walk carries the player east along y=19:
(8,19)@2424 → (11,19)@4735 → (17,19)@4928/5036/5066/5102 → (20,19)@5197 —
crossing (18,19).

```
3703: EVENTOP 32816            ; mark event done (sets bit 0x80000 in word1)
3706: NEXTSTATE 22             ; ★ v22++ → future loads show broken glass
3708: STARTCINEMATIC 9         ; maya camera 9 = keys (160,1248,Z=484) yaw512
3710: LERPSPRITE spr=152 dst=(2,19) fl=12        ; slab slides in
3714: ADV_CAMERAKEY 1
3716/3768/3771: PLAYSOUND 32/38/32 ; 3719/3774: SCREEN_SHAKE
3722/3763: SPAWN_PARTICLES …
3727: WAIT 700ms
3729: LERPSPRITEOFFSET 153 t=0 dst=… dz=-40      ; car drops below floor line
3736: LERPSPRITE spr=154 dst=(1,19) z=24 t=800ms ; blocking lerp
3742: LERPSPRITEOFFSET 155 t=0 dst=(127,1248) dz=+24
3749/3756: LERPSPRITEOFFSET 205/206 t=0 dz=…
3777: HIDE 153                   ; car vanishes into the floor
3779/3781: HIDE 205/206
3783/3790: LERPSPRITEOFFSET 156/157 t=0
3797: ENTITY_FRAME 155 frame=1   ; ★ GLASS BREAKS mid-scene
3801: LERPSPRITE spr=154 dst=(1,19) fl=9 t=300ms
3806: LERPSPRITEOFFSET 155 t=0 dst=(127,1248) dz=+32   ; pane settles, stays VISIBLE
3813-3854: more offsets on 156/157, LERPSCALE sp0 (fire) up/down
3856: EVAL [v116] iff-> 3891
3860: ADV_CAMERAKEY 1 ; 3862: HIDE 152 ; 3864: WAIT 1s ; 3866: DIALOG 45 ; 3869: GOTO (2,19)
```

Correction to `docs/original-code/lerp-opcodes.md` §map00: sp155 is **not**
landing gear and is **not hidden** — it is the glass pane that breaks
(frame swap) and remains on screen; only 153/205/206 (and later 152) hide.

Maya camera 9 parsed from the cameras block (file offset 72038):
2 keys, both at X=160 Y=1248 Z=484, pitch 0, yaw −2→512 — the camera stands
**inside the shaft** at tile (2,19) watching the crash point-blank.

## 3. Why the rewrite shows neither (divergence analysis)

Verified EQUAL to legacy (no bug found at these stages):

1. **Parse** — z-sprite Z bytes and anim bytes load identically
   (`new_src/domain/world/MapParser.cpp:122-132`); info hi-word endianness
   correct (`:117-119`).
2. **renderMode** — 178 → mode 3 written to S_RENDERMODE for every sprite
   (`new_src/domain/game/Game.cpp:112-123` ≡ `src/Render.cpp:2468-2495`);
   applied as ADD blend with fog off
   (`new_src/render/World3D.cpp:380-400` ≡ `src/GLES.cpp:649`).
3. **Textures** — preload covers every encountered tileNum incl. +257 ranges;
   lazy per-mediaId cache with RLE decode
   (`new_src/render/World3D.cpp:258-312`). Glass media 779/780 decode fine
   (verified by independent extraction).
4. **Leaf membership** — rewrite recomputes `spriteLeaf[i]` per frame with the
   same classify/bounds test (`new_src/render/World3D.cpp:1188-1203`,
   `getNodeForPoint :1127-1152` ≡ `src/Render.cpp:2401-2441`); simulation puts
   all five parked sprites in leaf 161.
5. **Script side** — `EV_ENTITY_FRAME`/`EV_HIDE`/`EV_LERPSPRITE(OFFSET)`/
   `EV_GOTO`/`EV_EVAL`/`EV_JUMP` semantics identical including the loop-tail
   `++IP` compensation (`new_src/domain/game/ScriptVM.cpp:237-396`); boot logs
   show the v22=0 path executing correctly.

Confirmed divergences that AFFECT these sprites, ranked:

* **D-A (confirmed, affects the slabs): no FLAT plane branch.** spr152/154/149
  carry SPRITE_FLAG_FLAT (0x20000000); legacy emits a horizontal quad using the
  second viewStep axis (`src/Render.cpp:639-661`); the rewrite's wall branch
  always builds a vertical quad (`new_src/render/World3D.cpp:721-742`). A
  "floor" rendered vertical is edge-on/invisible or sticks through walls —
  matching "the sprite that gives the elevator floor its volume is missing".
* **D-B (suspected, affects whether the scene ever plays): event[48]/camera 9
  never observed firing.** No `STARTCINEMATIC camera=9` and no
  `executeTile(tile=18,19 …)` appears in ANY captured log, while the
  equivalent lines for earlier scenes do appear. If the user's session ended
  before walking (17..20,19), the crash (and hence the on-screen glass-break)
  simply never ran yet — original behaviour, not a bug. If they DID walk the
  corridor and nothing fired, the trigger mask/path needs the D-B probe below.
* Not divergences despite suspicion: ADD blending of the glass (legacy GLES
  path uses RENDER_ADD too); DECAL/TILE sort biases present
  (`new_src/render/World3D.cpp:1240-1256`); charClass hack cannot catch these
  sprites (NOENTITY → no entity → class 0, `new_src/core/GameContext.cpp:968-995`).

## 4. Cheapest runtime proofs (per stage)

1. **MapParser decode** — one temporary line in `World3D::drawSprite`
   (`new_src/render/World3D.cpp`, right after `info` is read):
   `if (i==155||i==153||i==154) fprintf(stderr,"[glass] spr=%d info=%08X tile=%d z=%d\n", i, info, tileNum, map.mapSprites[i+2*n]);`
   Expect `spr=155 info=083001B2 tile=178 z=192` (W|2SIDE|NOENT, frame 0).
   Wrong info ⇒ parser bug; correct ⇒ move on.
2. **postProcess pass** — same line already prints the blend input:
   `applyBatchState(renderMode)` — add renderMode to the trace above; expect 3
   for spr155 (ADD). Existing alternative: none narrower.
3. **BSP leaf membership** — in `drawBSP` after the `spriteLeaf` loop:
   `fprintf(stderr,"[glass] leaves 153=%d 154=%d 155=%d\n", spriteLeaf[153],
   spriteLeaf[154], spriteLeaf[155]);` Expect `161 161 161`; a `-1` names
   `getNodeForPoint` as the dropper.
4. **drawSprite branch** — extend probe 1 with `isWall` and the emitted vertex
   count; expect the wall branch (`isWall=1`) for 153/155 and the (currently
   wrong) vertical quad for 154.
5. **Event trigger / cinematic** — no new code needed: walk east past the
   elevator in-game and watch stderr for
   `[script] executeTile(tile=18,19 mask=…)` followed by
   `[script] STARTCINEMATIC camera=9` (existing lines,
   `ScriptVM.cpp:146`/STARTCINEMATIC handler). Their absence while crossing
   (17..20,19) proves D-B; then `[script] ENTITY_FRAME sprite=155 frame=1`
   (existing line) proves the break executed.
6. **User visual confirmation** (AGENTS.md: user is the eyes): after the probe
   build, ask whether a bluish pane now overlays the car shape at the shaft
   during camera 9, and whether the shaft shows horizontal floor/ceiling slabs.

## Open questions

1. Does event[48] fire in the rewrite on a real walk across (18,19)?
   (probe 5 decides; logs so far contain neither the executeTile nor the
   cinematic line).
2. ~~Is D-A alone responsible for "missing floor volume"~~ — RESOLVED 2026-08-26:
   the FLAT plane branch now exists (`new_src/render/World3D.cpp:831-854`) and
   the slabs still don't show; see the audit below for what actually remains.
3. Exact caller chain into the restorer function @1028 (reached by fall-through
   from the sp199-202 parking block; entry seeded by `EVAL@953/957 → 1000`);
   immaterial to gameplay but would complete the CFG.
4. spr156/157 initial positions are OOB-of-leaf until moved — confirm legacy
   also shows them only after the cinematic moves them (they are HIDE-less
   corpse props at the shaft bottom).

## Glass render-path audit (2026-08-26)

Trigger: fresh stderr probe proves spr155 reaches `drawSprite` every frame
(`info=0x083000B2 tileNum=178 frame=0 branch=vertical-wall mediaId=779
leaf=161`; siblings 153→888/161, 154→941/161, 149→942/173) yet the user sees
no glass from any reachable angle. Correction: yesterday's `0x083001B2` was a
misread — the real info word has frame nibble 0 (WHOLE glass). This audit walks
both engines stage by stage; every claim cites code.

### 1. Legacy draw path for spr155, exact (Q1)

Record: `info=0x083000B2` parsed from tmp_map00.bin (X@59078+i, Y@59339+i,
info_lo@59600+i, info_hi@59865+2i, Z@60391+(i−135); values match the runtime
probe bit-for-bit). Flags = WEST|TWO_SIDED|NOENTITY (`src/Enums.h:1231-1250`);
**no DECAL** (`0x083000B2 & 0x10000000 = 0`), no FLAT/TILE/HIDDEN/FLIP bits,
frame byte 0. Position (128,1248)px, raw Z=192.

1. **Collection**: `postProcessSprites` bakes Z once at load:
   `S_Z += getHeight(x,y)` then `-= 32` for z-sprites
   (`src/Render.cpp:2463-2467`) → final Z = 608 mu, and writes S_RENDERMODE=3
   because effective tileNum is 178 (`src/Render.cpp:2478-2480`). Then
   `relinkSprite(155)` attaches it to leaf **161**
   (`src/Render.cpp:2394-2398` → `getNodeForPoint :2401-2442`; exits at the
   first leaf, so sprites only ever attach to leaves).
2. **Per-frame order**: `walkNode` collects leaves near→far with a
   `cullBoundingBox` early-out (`src/Render.cpp:1053-1055`);
   `renderBSP` iterates them REVERSED (far→near), and per visible leaf rebuilds
   the `viewSprites` list via `addSprite` + draws immediately
   (`src/Render.cpp:1752-1760`). `addSprite` sort key = mvp row-2 dot + biases;
   chain order: DECAL→clamp MAX (:839-841), TILE +6 (:847-849),
   water tiles →MIN (:850-852), oriented +5 (:853-855), entity ±1 (:856-862).
   spr155 gets **+5** (oriented); car side spr153 (TILE) gets **+6**, so the
   car draws before the glass — correct stacking.
3. **Draw call**: `renderSpriteObject(155)` reads x/y/z, renderMode=3,
   scaleFactor = S_SCALEFACTOR<<10 = 65536
   (`src/Render.cpp:1506-1512`), skips all entity branches (NOENTITY →
   `S_ENT == -1`) and reaches `renderSprite(x,y,z,178,0,flags,3,65536,0)`
   (`src/Render.cpp:1728`).
4. **Branch select**: `(flags & 0x2F000000) != 0` → oriented branch;
   WEST → n23=4 (`src/Render.cpp:525-537`). The branch is guarded by
   `textureBaseSize == sWidth*tHeight || portal tiles`
   (`src/Render.cpp:538-539`). `textureBaseSize` is the exact on-disk texel
   byte count: `mediaTexelSizes2[texIdx] = (mediaTexelSizes[i] & 0x3FFFFFFF)+1`
   (`src/LoadingManager.cpp:149-151`). Glass media 779/780 measure
   **16384 == 128×128 → RAW** (decoded from UnPackGameData/newMappings.bin),
   guard passes.
5. **Geometry**: bounds [0,128,1,129] → n11=n12=128; tHeight==sWidth==128 ≠256
   so n24=n12>>1=64, n25=n11>>2=32 (`src/Render.cpp:546-548`);
   n26=64, n27=32 at scale 65536. Wall z-corrections
   `z += (tHeight−b[3])<<4 − 16*(scaleFactor/2048)` → bottom 9200 ru (575 mu),
   top +1024 ru (639 mu) (`src/Render.cpp:552-555,632`). Along-wall axis =
   `viewStepValues[((4+2)&7)<<1]` = viewStepValues[12..13] = (0,64)
   (`src/Canvas.h:108`) → quad lies in **plane x=128px spanning y∈[1216,1280]px**,
   single quad, `swapXY=true`, one `drawModelVerts(mv,4)`
   (`src/Render.cpp:624-637`).
6. **Blend/fog**: `setupTexture` → GLES binds media-779 texture and for
   RENDER_ADD sets `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`, white color,
   fogMode=0 → glDisable(GL_FOG) (`src/GLES.cpp:648-652,709-715`); state is
   cached per (renderMode, flags) pair (`src/GLES.cpp:615`).
7. **TWO_SIDED does NOT draw twice.** It only sets TinyGL software-rasterizer
   `faceCull=CULL_NONE` (`src/Render.cpp:449-454`); the GLES path never enables
   `GL_CULL_FACE` at all (`src/GLES.cpp:230` is a lone `glDisable`), and the
   quad is submitted exactly once either way.

### 2. Rewrite pipeline audit, stage by stage (Q2)

| Stage | Verdict | Evidence |
|---|---|---|
| Parse/info/Z bytes | EQUAL | `new_src/domain/world/MapParser.cpp:93-133` mirrors `src/LoadingManager.cpp:488-539` incl. SoA stride layout |
| renderMode table | EQUAL | `new_src/domain/game/Game.cpp:112-123` ≡ `src/Render.cpp:2459-2495` |
| Height snap / double-add suspect | REFUTED | rewrite does **not** bake height into S_Z at load (Game.cpp writes only S_RENDERMODE), so the draw-time snap in `World3D::drawSprite:669-679` / `drawBSP:1234-1241,1272-1277` is the sole adjustment — same net Z as legacy |
| Leaf attachment / index space | EQUAL | `getNodeForPoint` is a 1:1 port (`new_src/render/World3D.cpp:1167-1192` ≡ `src/Render.cpp:2401-2442`, incl. onSplit tie-break and ±128 snap window); index space is consistent: mapSprites is SoA stride `numSprites` everywhere (`new_src/domain/world/MapParser.cpp:95-108` ≡ `src/LoadingManager.cpp:429-448`); z-sprites occupy [135,261) and 149–155 are consumed by the same indices in parser, postProcess, drawBSP and probe |
| Painter's order vs walls | EQUAL | `drawBSP` iterates collected leaves reversed, geometry of a leaf then its depth-sorted sprites (`new_src/render/World3D.cpp:1254-1311`) ≡ `src/Render.cpp:1752-1760` + bias chain mirrored at `:1280-1297` (glass +5 oriented; DECAL hook exists `:1280`; GameContext builds entity biases only, `new_src/core/GameContext.cpp:964-993` — spr155 unaffected) |
| Corner math | EQUAL | `kViewStepValues` identical table (`new_src/render/World3D.cpp:138` ≡ `src/Canvas.h:108`); wall branch `:855-873` reproduces `src/Render.cpp:624-637`; vertex scales 1/16384 and 1/1024 match `src/GLES.cpp:19-20` |
| Split-sprite mechanism missing | NOT A DIVERGENCE (vestigial upstream too) | legacy `addSplitSprite` only fires for sprites listed on INTERNAL nodes (`src/Render.cpp:1094-1096`), but `getNodeForPoint` never returns an internal node, so those lists are always empty — nothing to port |
| ADD blend state batching | EQUAL | `applyBatchState(3)` flushes pending verts BEFORE switching blend/fog (`new_src/render/World3D.cpp:380-400`); texture change also flushes before bind (`:702-710`); `flush()` submits immediately (`:367-375`) — no state inheritance or mid-batch drop possible by construction |
| Cull face | EQUAL | `begin()` disables GL_CULL_FACE (`new_src/render/World3D.cpp:330`) — both windings drawn, like legacy GLES |

Note: rewrite `walkNode` deliberately drops legacy's `cullBoundingBox`
early-out and visits EVERY leaf every frame
(`new_src/render/World3D.cpp:1194-1215`) — so "the sprite reaches drawSprite"
is guaranteed regardless of view and proves nothing about on-screen pixels.

### 3. Media 779 art content under ADD (Q2c) — refuted as cause

Decoded from UnPackGameData (mappings/palettes/texels, RAW path): whole-glass
frame 0 is **fully opaque** — 0 of 16384 texels transparent, mean RGB
≈ (60,124,110), 47.7% of texels luminance ≥ 96. The rewrite palette LUT gives
every non-magenta entry alpha 255 (`new_src/render/gl/Texture.cpp:82-89`), so
under `GL_SRC_ALPHA,GL_ONE` the pane adds a solid teal veil to whatever is
behind it — plainly visible against the dark shaft, subtly brightening against
lit walls. "Blended invisible" cannot explain a total absence.

### 4. Ranked remaining causes

* **H1 (top): no pixels land where the user looks — viewing geometry, not
  rasterization.** The pane occupies z = 575–639 mu; reachable floor positions
  put the eye around 40–100 mu, and intervening nearer-leaf room geometry
  (walls/ceiling around the shaft opening) covers that screen band except
  through specific openings. Both engines share this exactly, so the original
  behaves the same during free play; the memorable glass view is crash-cinematic
  camera 9 standing INSIDE the shaft at eye level z=484 (§2b above) — which has
  never fired in captured rewrite sessions (no `STARTCINEMATIC camera=9` line).
  Consistent with the floor slabs (z 544/576 mu) being "missing" from the same
  angles.
* **H2: emitted but overdrawn despite valid NDC** — would require an ordering
  divergence not found statically; decided by the test below.
* Everything else (leaf attach, index space, double-Z, batch state, blend art,
  TWO_SIDED, DECAL, split-sprites) is verified equal or refuted, §2–§3.

### 5. The smoking-gun test (one runtime change)

In `World3D::drawSprite`, gate on an env var (e.g. `DOOM_GLASS_DEBUG=1`) and for
tileNum 178 only: (a) force `renderMode = 0` after `applyBatchState` input is
read, (b) log the four wall-quad corners transformed to NDC
(`uMVP * vec4(pos,1)`) once per second. Outcomes:

* no NDC line while the user stands at the elevator → sprite not emitted
  (contradicts current evidence; would indict collection);
* corners outside NDC [−1,1]³ → wrong geometry/Z (H-geometry);
* corners inside NDC **and the pane appears** with mode 0 → ADD blending made
  it unrecognizable at that angle (blend/art);
* corners inside NDC **and still invisible** → later geometry overdraws it →
  H2/order divergence proven.

Companion check: force event[48]/camera 9 (or teleport the camera to
160,1248,z=484,yaw≈512) and ask the user whether the teal pane and slabs show
there — that is where the original displays them.

### 6. Why spr152 never reached drawSprite (Q4)

Raw record from tmp_map00.bin: `info=0x20F400C6` = FLAT|TILE|SOLID|FLIPV|
TWO_SIDED|NOENTITY — **not hidden**, coords nonzero (96,1056)px = tile
(1.5,16.5), Z=64, anim byte 0. It is dropped at COLLECTION, not by a flag:
its parked position classifies outside every walkable leaf (yesterday's
simulation §1; consistent with today's probe pattern where every in-leaf
sibling logged a valid leaf). In the rewrite only `drawBSP` renders
(`new_src/core/GameContext.cpp:994`; `drawSprites` has no caller — dead code),
and its `spriteLeaf[i] != leaf` filter skips leaf-less sprites
(`new_src/render/World3D.cpp:1263`). Legacy is identical: `relinkSprite` stores
S_NODE=−1 and the sprite joins no nodeSprites list
(`src/Render.cpp:2394-2398`). It will start rendering only after event[48]'s
`LERPSPRITE spr152 dst=(2,19)` (IP 3710) completes and the relink re-classifies
it into the shaft — i.e. after D-B is resolved. (Side note: had the billboard
pass been alive, spr152 would have been mis-drawn there as a billboard, because
`drawSprites:553` excludes TILE-flagged sprites from the wall class while
`drawBSP:1240`'s classify does not — another reason the dead pass should stay
dead or be aligned.)
