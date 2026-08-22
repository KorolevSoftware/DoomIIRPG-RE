# Research log — 2026-08-23 — Sprite placement math & "slight shift" bug hunt

## Hypothesis

User report in the rewrite (`new_src/`): "sprites render slightly shifted/offset".
Working hypothesis: a rounding or half-texel/half-tile offset difference between
the legacy sprite renderer (`src/Render.cpp`) and `new_src/render/World3D.cpp`
(missing +32 centering, `>>` vs `/`, heightMap snap, bottom-vs-center anchor,
RLE crop off-by-N, wall-surface offset).

## Method

1. Located the implementation via grep over `src/`:
   `renderSpriteObject` (`src/Render.cpp:1498`), `renderSprite`
   (`src/Render.cpp:426`), `postProcessSprites` (`src/Render.cpp:2459`),
   `relinkSprite`/`getNodeForPoint` (`src/Render.cpp:2376-2442`),
   `addSprite` sort key (`src/Render.cpp:827-894`), `viewStepValues`
   (`src/Canvas.h:108`), `getHeight` (`src/Render.cpp:2444`).
2. Read the actual control flow of both billboard and wall-decal branches,
   including `TinyGL::viewMtxMove` (`src/TinyGL.cpp:203-221`) to pin down the
   right/up axis conventions (column 0 = camera right, `-column 1` = up),
   and `gles::DrawWorldSpaceSpriteLine` (`src/GLES.cpp:483-547`) for the
   GL-path UV override + 176 crop.
3. Verified units from loaders: `shiftCoord` byte*8 (`src/Resource.cpp:154-156`),
   Z default 32 / scale 64 (`src/LoadingManager.cpp:497-498`), z-sprite bytes
   (`src/LoadingManager.cpp:527`), postProcess call site
   (`src/LoadingManager.cpp:569`), camera input `(v<<4)+8`
   (`src/Canvas.cpp:1344`).
4. Cross-checked usage sites for units (tile = coord>>6: `src/Game.cpp:1023`,
   `src/MovementController.cpp:329`; spawn centers tile<<6+32:
   `src/ScriptThread.cpp:1220`; entity floor snap `src/Entity.cpp:70`).
5. Read `new_src/render/World3D.cpp` (all), `new_src/render/Camera3D.cpp`,
   `new_src/core/Main.cpp` render loop, `new_src/domain/world/MapParser.cpp`,
   `new_src/io/DataReader.cpp`, `new_src/render/gl/Texture.cpp`.

## Verdict

PARTIAL. The per-sprite placement math in the rewrite is a faithful match
(no missing +32, no >> vs / bug). Thirteen discrepancies documented (see
`docs/original-code/sprite-placement.md`); the ones that can plausibly produce
a *uniform slight displacement* are:

1. Missing camera pull-back nudge `viewX -= 160*viewCos>>16;
   viewY += 160*viewSin>>16` (`src/Render.cpp:2279-2282`) — absent from
   `new_src` entirely (camera set at raw player position,
   `new_src/core/Main.cpp:380-382`). ~10 map units of global framing offset.
2. Viewport/aspect source differs (legacy shrinks viewport by 2 px and uses
   `viewRect[1]=20`: `src/TinyGL.cpp:149-166`, `src/Canvas.cpp:124-127`,
   `src/Render.cpp:2223`; rewrite uses full 480×320,
   `new_src/core/Main.cpp:225,382`).
3. Portal sockets included in the `z -= 128` range in the rewrite
   (155..157 at `new_src/render/World3D.cpp:717` vs legacy 155..156,
   `src/Render.cpp:521-523`) — sprite-specific 8-unit drop.
4. Height snap re-derived per frame from current X/Y
   (`new_src/render/World3D.cpp:535-542`) instead of baked at load
   (`src/Render.cpp:2459-2467`) — diverges after runtime movement on
   non-flat terrain; sort key also uses raw Z there.

## Key evidence (abridged)

* Billboard anchor: `z -= 512` (`src/Render.cpp:456`); sizes
  `(((n11>>2)<<4)+7)*scale/0x10000` / `(((n12>>1)<<4)+7)*scale/0x10000` or
  fixed `518`/`1036` with full-frame UVs (`src/Render.cpp:461-472`);
  nudge `n17 += 10` → 12 (RLE) / 10 (bounds)
  (`src/Render.cpp:473-475`); corners at `(x<<4, y<<4, z-84)` ± halfW along
  camera-right, +height along -column-1 (=up) (`src/Render.cpp:479-489`,
  `src/TinyGL.cpp:203-221`). Bottom-edge-centered anchor confirmed.
* Wall decals: orientation index from flag bits
  (`src/Render.cpp:526-537`, flags `src/Enums.h:1240-1248`), axis pair
  `viewStepValues[((orient+2)&7)<<1]` (`src/Canvas.h:108`,
  `src/Render.cpp:559-561,624-638`), no wall-surface standoff, z lift
  `(tHeight-bounds3)<<4` minus `16*(scaleFactor/2048)`
  (`src/Render.cpp:552-555`).
* GL billboard UV override + `s*176/sWidth` crop
  (`src/GLES.cpp:515-542`); TinyGL RLE sampler uses the same 176 logical size
  (`src/TinyGL.cpp:747-780`).
* Sort key `(x*mvp[2]+y*mvp[6]+z*mvp[10]>>14)+mvp[14]` on raw coords with
  biases (+6 TILE, water INT_MIN, oriented +5, crate +5, …)
  (`src/Render.cpp:839-874`); list head = farthest, ties newest-first
  (`src/Render.cpp:880-893`). Rewrite order-equivalent except omitted biases
  and reversed tie-break (`new_src/render/World3D.cpp:861-877`).

## Open questions

* Whether the original GLES output was globally y-flipped (negated
  `projectionMatrix[5]`, `src/GLES.cpp:168-172`) — determines whether the legacy
  GL billboard t-convention displayed upright or flipped; does not affect
  world-space placement.
* Whether any shipped map stores media bounds with bits above 0xFFF set
  (would make the rewrite's int16 cast diverge from the legacy mask).
