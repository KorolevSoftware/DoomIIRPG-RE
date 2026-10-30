# Translucent surfaces render solid white (map00 bridge pit, map01 glass)

Date: 2026-10-30

## Hypothesis

Two places in the rewrite show a white surface where the original shows a dark,
translucent one:

1. map01 (mapID 2): a glass pane that a cutscene later breaks.
2. map00 (mapID 1): the elevator/bridge pit where the NPC bridge cutscene
   moves the player. The original shows a gradient that fades to black.

The guess was one missing blend mode (SUB/ADD/multiply) or one missing
texture/palette rule.

## Verdict

**CONFIRMED, with a corrected root cause.** No blend mode is missing. Every
legacy `RENDER_*` mode is mapped to a sokol pipeline. The cause is the **FADE
wall texture, tile 302** (`TILENUM_FADE`, `src/Enums.h:789`):

* On the GL path the original never uploads the media art of tile 302. It
  swaps in a procedural 16×64 texture with **RGB = 0 and an alpha ramp**
  (`src/GLES.cpp:825-828` → `gles::CreateFadeTexture`, `src/GLES.cpp:234-298`).
  It draws that texture with `RENDER_NORMAL` alpha blend
  (`src/Render.cpp:956-961`).
* The rewrite uploads the real media art (media 882, a **white→black** gray
  ramp meant for the TinyGL SUB span). It draws it opaque in NORMAL
  (`new_src/render/World3D.cpp:170-195`, `:286`). The result is a white
  gradient.

Case 2 (map00) is the bridge pit itself: its walls and floor are tile 302.
Case 1 (map01) is PARTIAL: both map01 glass panes broken by a script sit on
tile-302 shafts. The additive glass (`ADD`, dark blue art) on top of a white
FADE wall still reads as white. I could not prove that the pane the user means
is one of these two (see Open questions).

## Method

* Extracted `map00.bin`, `map01.bin` and `newMappings/Palettes/Texels*.bin`
  from `build_new/new_src/Doom 2 RPG.ipa` into the scratchpad.
* Parsed the sprites (X, Y, info low/high bytes, z-sprite Z and info bytes) and
  the `nodePolys` texture ids. The parse follows the `src/LoadingManager.cpp:463-553`
  layout and reuses the section offsets from `tools/disasm_map_scripts.py`
  (`MapSections`). The wall tile is `u16 >> 7` and the vertex coordinates are
  `byte << 7` (`src/Render.cpp:946-991`).
* Disassembled the map01 scripts with
  `python3 tools/disasm_map_scripts.py map01.bin`.
* Read the media of tiles 178/302/212 with the loaders in
  `tools/extract_textures.py` (palette and index statistics only; no images
  were viewed).
* Read the legacy GL path (`src/GLES.cpp`, `src/Render.cpp`, `src/Image.cpp`)
  and the rewrite path (`new_src/render/World3D.cpp`,
  `new_src/render/api/RenderModes.cpp`, `new_src/render/sokol/SgPipelines.cpp`,
  `SgScene3D.cpp`, `SgTexture.cpp`, `shaders/world.glsl`).

## Evidence

### E1. Original: tile 302 on GL is a procedural black alpha ramp

* `src/Render.cpp:956-961`:
  `else if ((uVar7 == Enums::TILENUM_FADE) || (uVar7 == Enums::TILENUM_SCORCH_MARK)) { iVar10 = Render::RENDER_NORMAL; if (!this->_gles->isInit) { iVar10 = Render::RENDER_SUB; } }`.
  On GL the mode is NORMAL. On TinyGL it is SUB.
* `src/Render.cpp:2057` → `gles::SetupTexture(n=302, …)` → on first use,
  `CreateTextureForMediaID(n, mediaID, true)` (`src/GLES.cpp:594-596`) →
  `if (mediaID == render->mediaMappings[Enums::TILENUM_FADE] && n == Enums::TILENUM_FADE) { this->CreateFadeTexture(mediaID); return; }`
  (`src/GLES.cpp:825-828`).
* `CreateFadeTexture` (`src/GLES.cpp:234-298`): 16×64 RGBA8, `memset 0`
  (RGB = 0). Alpha loop at `:279-287`: `pixels[i*4+3] = alpha; if (i == 15*(i/15) && i > 512) alpha -= 10; if (alpha < 0) alpha = 0;`.
  Simulated: A = 255 for i ≤ 525. The steps are 245, 235, …, 5. The first
  A = 0 is at i = 901 (row 56). Rows 57-63 are fully transparent.
  `GL_NEAREST` (`:291-292`).
* NORMAL state: `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`,
  `glColor4f(1,1,1,1)`, `fogMode = 2` (fog on) (`src/GLES.cpp:625-635`,
  `:709-715`). Net effect: `dst' = A·fog(black) + (1−A)·dst`, which darkens
  toward t = 0.
* Texture coordinates: `vert->t = (int8)nodePolys << 6` (`src/Render.cpp:994`)
  and `TEXT_COORDS_TO_FLOAT(x) = x/1024` (`src/GLES.cpp:20`). FADE walls use
  t 0..16, so they span the texture exactly once.

### E2. The media art of 302 is a white ramp (TinyGL SUB art)

Media 882 (`mappings[302]`), 16×64, raw, 16-entry palette. The row colors at
column 0 are: row 0 (255,255,255), row 8 239, row 16 198, row 24 173,
row 32 140, row 40 107, row 48 49, row 56 24 (gray). TinyGL subtracts this art
through `_spanTexture[RENDER_SUB]` with a 50 % palette
(`src/Render.cpp:1986-1991`, `src/Span.cpp:230-244`), so white means "darken".

### E3. Where tile 302 sits

* **map00**: bridge pit, leaves 424/565. There are vertical walls on the
  x = 18, x = 21, y = 18 and y = 21 sides, z 128→448 (448 = floor height at
  (19,19)/(19,20)), with t = 0 at z 128. There is also a floor quad at z 128.
  The NPC bridge tile (19,20) and the bridge pieces are in this pit
  (`docs/research/2026-10-29-bridge-npc-sequence.md`). FADE is also used in
  30+ other places on map00 (leaf list in the scratch dump).
* **map01**: the 4-wall shaft at x 12..13, y 17..18, z 704..832 (leaf 452), and
  the 4-wall shaft at x 11..12, y 10..11, z 704..832 (leaf 336). Both are at
  the same tiles as the glass panes below.

### E4. map01 glass panes (tile 178 `TILENUM_GLASS`, `src/Enums.h:792`)

| spr | tile | info | flags | Z byte | script |
|---|---|---|---|---|---|
| 178 | (12,17) | 0x203200B2 | FLAT, NOENTITY, TWO_SIDED, FLIP_H | 62 | broken by the cutscene at IP 2128-2133 (`SPAWN_PARTICLES tile(12,17)`, `ENTITY_FRAME sprite=178 frame=1`). The restorer at IP 558 is gated on v31==1. `LERPSCALE 128` at IP 829. |
| 179 | (11,10) | 0x203201B2 | FLAT, NOENTITY, frame 1 (already broken) | 192 | particles at IP 2176, `LERPSCALE 128` at IP 835 |
| 226 | (22,11) | 0x043000B2 | E wall | 72 | broken at IP 6369-6374, restorer at IP 739. **No FADE nearby.** |
| 152/153/154 | (22,12)/(22,12)/(22,11) | 0x04x… | E wall | 72 | `LERPSCALE 32` at IP 94-106 |
| 227 | (23,7) | 0x083001B2 | W wall, frame 1 | 64 | none |

Glass gets `RENDER_ADD` from `postProcessSprites` (`src/Render.cpp:2478-2480`),
and the rewrite does the same (`new_src/domain/game/Game.cpp:97`). Media 779
(whole glass) is dark blue (most common palette entries are (24,44,66) and
(24,48,74)) and has no magenta key. With `(SRC_ALPHA, ONE)` it adds only a faint
blue tint, so the visible color is whatever lies behind it. Behind spr178 and
spr179 there are the FADE shafts, which the rewrite draws white.

### E5. The rewrite's blend modes are complete

* `new_src/render/api/RenderModes.cpp:21-36` maps every mode 0..13 to the
  `src/GLES.cpp:625-701` factors. `sgBlendVariant`
  (`new_src/render/sokol/SgPipelines.cpp:78-98`) maps them to four pipelines:
  AlphaBlend, Additive `(SRC_ALPHA, ONE)`, Subtractive
  `(ZERO, ONE_MINUS_SRC_COLOR)` and WriteNothing (`:18-51`). Nothing is
  unmapped. SUB is not drawn as ADD.

### E6. Why the rewrite shows white

* `World3D::uploadMapTextures` uploads every polygon tile's
  `mappings[tile]` media via `uploadIndexed(..., transparent=true, tiled=true)`
  (`new_src/render/World3D.cpp:170-195`). Tile 302 has no special case
  (`grep 302` finds nothing in `new_src/render/`), so the white ramp of media
  882 is uploaded with A = 255 everywhere. It has no magenta key.
* `World3D::drawPoly` forces `kRenderNormal` on all geometry
  (`new_src/render/World3D.cpp:286`). The result is an opaque white→dark-gray
  wall/floor, which matches what the user reports.

## Minimal fix (for the architect/coder; the rewrite is not touched here)

The exact equivalent of the original is to give tile 302 a texture with RGB = 0
and the `CreateFadeTexture` alpha, and to draw it in NORMAL. Two ways to do it:

**A. Exact. One small backend addition, no shader or pipeline change.**
The world program already samples an RGBA8 256×1 LUT
(`SgTexture::uploadIndexed`, `new_src/render/sokol/SgTexture.cpp:114-120`).
Only `expandPalette565` limits alpha to 0/255. Steps:

1. Add `TextureStore::createIndexedLut(indices, w, h, const uint8_t lut8888[1024], flags)`
   and its `SgTexture` overload, which skips `expandPalette565`.
2. In `uploadMapTextures`, for `tile == 302`: build the indices
   `idx[i] = k_i` (k_i = number of decrements applied before texel i in the
   `src/GLES.cpp:279-287` loop, 0..26). Set `lut[k] = {0,0,0,max(0,255-10k)}`,
   `w = 16`, `h = 64`, tiled, no transparent key.
3. `drawPoly` keeps NORMAL. Fog on for NORMAL matches `fogMode = 2`.

**B. Zero backend changes, near-exact.**
Draw tile 302 with `kRenderSub` (`(ZERO, ONE_MINUS_SRC_COLOR)`, which gives
`dst·(1−C)`), using a synthetic indexed 16×64 texture whose palette entry k is
the gray `a_k = max(0,255−10k)` in RGB565 (`r5 = a>>3, g6 = a>>2, b5 = a>>3`).
Since `dst·(1−a)` equals the NORMAL blend of black at alpha a, colors match
except for two small differences: (i) 565 quantization, ≤ 8/255 per channel;
(ii) no fog on FADE, because SUB has fog off while the original NORMAL row has
fog on. The fogged black differs only when the fog color is not black.
`drawPoly` must then call `setRenderMode(kRenderSub)` for tile 302 instead of
the forced `kRenderNormal` at `World3D.cpp:286`.

Do **not** draw media 882 with SUB. That would be the TinyGL look (a different
curve, `src/Render.cpp:958-960`), not the GL look the rewrite ports (ADR 0019).

Adjacent gap (not the reported bug): `drawPoly` also ignores the geometry
mode for tile 161 `HELL_HANDS` → `BLEND50` (`src/Render.cpp:953-955`). Map00
and map01 have no tile-161 polys.

## Open questions

* Which map01 pane does the user mean? If it is spr226 at (22,11) (E-facing
  wall pane, broken at IP 6369-6374), FADE does not explain it, because there
  is no tile 302 near (22,11). Coordinates or "what is behind the pane" would
  settle it. Player spawn on map01 is (5,23).
* No in-game confirmation yet. After the fix, the user should check the map00
  bridge pit (walls black at the bottom, fading out upward) and the map01
  (12,17) and (11,10) shafts.
