# map01 fire drawn inside a black rectangle

Date: 2026-10-30

## Report

User, rewrite (sokol), map01 (mapID 2, `map01.bin`). The player stands at tile
(11,16), xy (736,1056), dir 0 (facing +x). To the front-left there is a flame
next to a round door with a green ring. The flame sits inside an opaque black
rectangle. In the original the black is invisible.

## Hypothesis

The flame sprite should use additive blending (RENDER_ADD), but the rewrite
draws it NORMAL.

## Verdict

**REFUTED as stated. Root cause found (CONFIRMED): the black rectangle is not
the fire sprite. It is a SCORCH_MARK (tile 212) wall-and-floor decal made of
BSP polygons, directly behind the fire.** The original GL path forces every
tile-212 texture to `RENDER_SUB`. The rewrite's `World3D::drawPoly` draws every
polygon NORMAL, so the scorch art (a dark square with a lighter burn in the
middle, built for subtractive blending) shows up as an opaque dark rectangle.
The fire billboard itself is additive in both programs.

## Method

* Parsed the `map01.bin` sprites and BSP `nodePolys` with throw-away scripts in
  the scratchpad. They follow the layout in `src/LoadingManager.cpp:361-539`:
  the header, the coord arrays (`shiftCoord` = byte*8, `src/Resource.cpp:154-156`),
  the info low byte, the high word, z-sprite Z, the z-sprite info byte
  (`:509-532`), and the polygon records (`src/Render.cpp:943-991`: texture =
  `u16 >> 7`, vertices `byte << 7`).
* Disassembled the map01 scripts with
  `python3 tools/disasm_map_scripts.py UnPackGameData/map01.bin --ipa ... --verify`.
* Read palette and index statistics for the media of tiles 212 and 130 with
  the `tools/extract_textures.py` loaders. No images were viewed.
* Read the original paths (`src/Render.cpp` postProcessSprites,
  renderSpriteObject, drawNodeGeometry, setupTexture; `src/GLES.cpp`
  SetupTexture) and the rewrite paths (`new_src/domain/game/Game.cpp`
  loadEntities, `new_src/render/World3D.cpp`, `new_src/render/api/RenderModes.cpp`,
  `new_src/render/sokol/SgScene3D.cpp`, `SgPipelines.cpp`, `shaders/world.glsl`).

## Evidence

### E1. What is in front-left of the player

dir 0 = (+64, 0) and dir 2 = DIR_NORTH = (0, -64) (`src/Canvas.h:108`,
`src/Enums.h:362`). So when the player faces +x, "left" means -y.

| sprite | xy | tile | tileNum | info (raw) | note |
|---|---|---|---|---|---|
| 194 | (896,968) | (14,15) | 130 `OBJ_FIRE` | 0x00200082, z-sprite, Z byte 64 | the flame: 160 ahead, 88 to the left |
| 33 | (928,992) | (14,15) | 276 `DOOR_UNLOCKED` (19+257) | 0x0C500013 (TILE, TWO_SIDED, E/W) | the door next to it |
| 46 | (1120,1120) | (17,17) | 130 | 0x00000082 | front-right |
| 182 | (800,1120) | (12,17) | 130 | 0x00200082, Z 192 | front-right, above SEPTIC_STATION spr181 and glass spr178 |

The INIT_MAP chain sets sprite 194 to `LERPSCALE scale=32 t=0` (IP 859, half size).
No script op changes its tile, frame or render mode.

### E2. The polygons behind sprite 194

BSP leaves 437 and 439 contain tile-212 polygons centered on x = 896:

```
leaf 437 tex 212  x 864..896  y 960        z 640..672   (wall strip)
leaf 437 tex 212  x 864..896  y 960..992   z 640        (floor patch)
leaf 439 tex 212  x 896..928  y 960        z 640..672
leaf 439 tex 212  x 896..928  y 960..992   z 640
```

The floor height there is 80*8 = 640. So this is a 64x32 scorch decal on the
wall at y = 960 plus a matching floor patch. Fire 194 stands at y = 968, 8
units in front of that wall. The same layout appears at fire spr193
(832,520): leaves 252/260 have tile-212 polygons at x 800..864, y 512. map01
has 28 tile-212 polygons in total. Counts in other maps: map00 8, map02 29,
map03 4, map04 10, map05 17, map07 15, map06/08/09 0.

### E3. Original: tile 212 polygons are subtractive on GL

* `src/Render.cpp:956-961` (drawNodeGeometry):
  `else if ((uVar7 == Enums::TILENUM_FADE) || (uVar7 == Enums::TILENUM_SCORCH_MARK)) { iVar10 = Render::RENDER_NORMAL; if (!this->_gles->isInit) { iVar10 = Render::RENDER_SUB; } }`
  The call is then `this->setupTexture(uVar7, 0, iVar10, 0)` (`:967`).
* `Render::setupTexture` passes the tile number to the GL back-end:
  `this->_gles->SetupTexture(tileNum, frame, renderMode, renderFlags)`
  (`src/Render.cpp:2056-2058`, call at `:2057`).
* `gles::SetupTexture` overrides the mode before its blend switch:
  `if (n == Enums::TILENUM_SCORCH_MARK) { renderMode = Render::RENDER_SUB; }`
  (`src/GLES.cpp:609-611`).
* RENDER_SUB on GL is `glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR)`, color
  1,1,1,1, fog off (`src/GLES.cpp:672-677`). So `dst' = dst * (1 - Csrc)`: a
  black texel leaves the framebuffer unchanged.
* Net result: tile 212 is SUB on **both** back-ends. TinyGL gets it from
  drawNodeGeometry, GL from the SetupTexture override. Tile 302 FADE is the
  exception: it stays NORMAL on GL, where it uses the procedural ramp
  (`docs/original-code/rendering.md` §8.10).

### E4. Scorch art is built for subtraction

Media 804 (`mappings[212]`) is 128x128 raw, with no magenta entry, and palette
entry 0 is (0,0,0). Distribution of the brightest channel per texel (bucket
size 32): 6711 texels in 0-31, 2285 in 32-63, 1804, 1447, 1307, 1450 and 1380
in the higher buckets. The brightest border texel has a max channel of 44.
Under SUB, the dark border subtracts almost nothing and the bright center
darkens the most, which gives a burn mark. Under NORMAL it is an opaque dark
square with a gray blotch.

### E5. Rewrite: polygons are always NORMAL

`new_src/render/World3D.cpp:415-417` (`World3D::drawPoly`):
"Geometry always composites with standard alpha + global fog" →
`scene_->setRenderMode(kRenderNormal);`. Nothing depends on the tile. Tile 212
polygons are therefore drawn opaque, alpha 1, with fog. The world texture
upload (`uploadMapTextures`, `:312`) only keys magenta, and media 804 has
none.

### E6. The fire sprite path is correct in the rewrite

* Original: `postProcessSprites` sets tiles {208,234,130,242} → 3, 178 → 3,
  236 → 3, 212 → 7, 161 → 2, 244 → 4 (`src/Render.cpp:2459-2494`).
  `renderSpriteObject` reads `S_RENDERMODE` (`src/Render.cpp:1511`) and
  passes it to `renderSprite` (`:1728`).
* Rewrite: `Game::loadEntities` writes the same table
  (`new_src/domain/game/Game.cpp:88-102`). `drawSprite` reads row 3 and
  calls `setRenderMode` before any emission (`new_src/render/World3D.cpp:589-591`).
  `kRenderModes[3]` = (SrcAlpha, One), fog off
  (`new_src/render/api/RenderModes.cpp:24`). `sgBlendVariant` maps it to
  `SgBlend::Additive` (`new_src/render/sokol/SgPipelines.cpp:78-84`), which
  is `SRC_ALPHA, ONE` (`:30-35`). `SgScene3D::setRenderMode` flushes before it
  switches modes (`new_src/render/sokol/SgScene3D.cpp:151-162`).
* Media 726-729 (tile 130 frames) have palette entry 0 = 0xF81F magenta under
  the `extract_textures.py` palette resolution, so the RLE background is fully
  transparent anyway. **This contradicts** `fire-animation-transparency.md`
  §2.2 ("zero magenta entries, entry 0 dark gray"). Not re-verified against
  `src/LoadingManager.cpp:110-200`. Either way the fire cannot produce an
  opaque box under ADD.

### E7. Other additive sprites

Sprite render modes go through the same row-3 path, so they are fine: glass
178 (3), air vent 236 (3), fog 208 (3), BFG 244 (4), missiles and impact anims
(the caller writes row 3: `new_src/domain/game/Combat.cpp:838,895`), melee
sparks (5, `:926`), torchiere glow (5, `World3D.cpp:1029`).

Minor gap, no visible effect today: `GameSprites::allocAnim`
(`new_src/domain/game/GameSprites.cpp:56-95`) implements only the FIRE_BALL
and claw/bite/blunt cases. It lacks POOF 241 → 4, ANIM_FIRE 234 → 3 (+ scale
48, Z = height+32, random H-flip), FOG_GRAY 208 (1 frame, 400 ms), ACID_SPIT
252 (2 frames) and SENTINEL_SPIKES 170 (`src/Game.cpp:1344-1394`). Today's
only caller (Combat) overwrites row 3 right after the call, and the non-combat
callers (`src/Entity.cpp:492,515`, `src/Game.cpp:3572`, POOF) are not ported.

Polygon gap, second case: HELL_HANDS 161 polygons should be BLEND50
(`src/Render.cpp:953-955`), but no shipped map (00-09) has a tile-161 polygon.

## Minimal fix (for the architect/coder)

In `World3D::drawPoly`, replace the fixed NORMAL with the legacy per-tile
choice:

```cpp
// src/Render.cpp:953-961 + src/GLES.cpp:609-611 (GL forces 212 -> SUB)
const int mode = p.textureId == 212 ? kRenderSub
               : p.textureId == 161 ? kRenderBlend50 : kRenderNormal;
scene_->setRenderMode(mode);
```

The SUB row already exists (`RenderModes.cpp:28`, Zero/OneMinusSrcColor, fog
off) together with its sokol variant (`SgPipelines.cpp:36-41`). Tile 302 must
stay NORMAL (§8.10).

## Open questions

* The palette-entry-0 discrepancy for tile 130 (E6) versus the older doc.
* The user called the door "round, with a green ring". The tile-160 polygons
  at x 912..944, y 960/1024 (leaf 439) frame door 276. That is consistent,
  but I did not check it visually.
