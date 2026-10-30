# map01 window shows pale panels instead of space (tile 301 SKY_BOX)

Date: 2026-10-30

## Hypothesis

On map01 (mapID 2) the player stands on tile (8,23), world (544,1504), facing
east (angle 0), and looks through an X-framed window. The original shows space
there. The rewrite shows blocky light-gray/white panels and a dim structure.
The guess was a special sky/space tile, sky flag or texture that the rewrite
does not handle.

## Verdict

**CONFIRMED.** The window is backed by **tile 301 `TILENUM_SKY_BOX`**
(`src/Enums.h:843`) wall quads. The original never draws the media art of 301:

* The loader re-points 301 at media slot 1023 (`src/LoadingManager.cpp:358`).
* `gles::CreateTextureForMediaID` fills that slot from the per-map 256×256 sky
  (`src/GLES.cpp:824`, `:834-842`).
* The polygon gets **screen-space UVs** that match the full-screen sky backdrop
  (`src/GLES.cpp:325-343`, set by `isSkyMap` in `src/Render.cpp:2072-2079`).

The rewrite uploads the raw `newMappings[301]`, which is **media 882**. That is
the same 16×64 white→black gray ramp as FADE tile 302. It draws the ramp as an
ordinary opaque wall with world UVs (`new_src/render/World3D.cpp:193-240`,
`:326-372`). This is the pale "panels": the white top rows, and the dark bottom
rows give the "dim structure".

The sky choice itself is correct in the rewrite. mapID 2 → tables 16/17 in both
(`src/LoadingManager.cpp:571`, `new_src/core/Main.cpp:339-342`). No map flag
picks a "space" variant. There is no animation or scroll over time. The sky
moves only with yaw.

## Method

* Extracted `map01.bin`, `tables.bin`, `newMappings/Palettes/Texels*.bin` from
  `build_new/new_src/Doom 2 RPG.ipa` into the scratchpad.
* Parsed polygons with `tools/map_to_obj.py` (`parse_map`, which follows
  `Render::drawNodeGeometry`: tile = `u16 >> 7`, coords `byte << 7`,
  `src/Render.cpp:944-991`). Listed polys whose bounds touch x 544..1300,
  y 1304..1704 (world = raw/16).
* Read media 882 statistics with `tools/extract_textures.py` loaders (palette
  and index counts only; no images viewed).
* Read the legacy GL sky path and the rewrite sky/poly path.

## Evidence

### E1. map01 geometry at the window (world units)

The leaf for tiles (9..10,23) holds these polys, in this order:
* `455` floor z 192, x 640..704, y 1472..1536
* **`301 sky_box` x = 704, y 1472..1536, z 192..320** (flags 0x5a)
* `202` ceiling z 320
* 8× `128 barred_window` on x = 704 (frame pieces z 192..224, 288..320 and
  diagonals): the X frame.

The neighboring windows repeat this pattern. There are 301 walls at x = 704,
y 1408..1472 and 1536..1600, and at y = 1408 / 1600 for x 640..704, each with
128 frames. map01 has 15 tile-301 polys in total. Nothing else lies behind
x = 704 in the window's view cone at that height. (The next geometry is at
z ≥ 640 or y ≥ 1696.)

### E2. Tile 301 shares media 882 with 302 in newMappings

`newMappings.bin`: `mappings[301] = 882`, `mappings[302] = 882`,
`mappings[303] = 883`. Media 882 is dims 0x46 (16 wide × 64 tall), 16-color
gray palette, mean RGB ≈ 124. Its most frequent colors run from (239,239,239)
down to (8,12,8).

### E3. Original: 301 remap and sky texture

* `render->mediaMappings[Enums::TILENUM_SKY_BOX] = (gles::MAX_MEDIA-1);`:
  `src/LoadingManager.cpp:358` (comment: "so it doesn't interfere with the fade
  texture").
* `src/GLES.cpp:824` `v5 = render->mediaMappings[Enums::TILENUM_SKY_BOX];`,
  then `:834-842` `if (v5 == mediaID) { ... __src = render->skyMapTexels; v9 = render->skyMapPalette[0]; v8 = 8; v10 = 8; }`
  (256×256).
* Sky source: `skyIndex = ((render->mapNameID - 1) / 5 % 2) * 2;`, palette
  table `skyIndex+16`, texels `skyIndex+17`: `src/LoadingManager.cpp:571-583`.

### E4. Original: how a 301 polygon is drawn

* `drawNodeGeometry`: 301 gets `RENDER_NORMAL`, `faceCull = CULL_CCW`
  (`src/Render.cpp:950-966`).
* `Render::setupTexture`: `if (tileNum == Enums::TILENUM_SKY_BOX) { ... this->isSkyMap = true; }`
  (`src/Render.cpp:2072-2079`). Every other tile sets it false (`:2091`).
* `gles::DrawModelVerts`: `if (!this->render->isSkyMap) { ...draw... }`,
  otherwise it returns false (`src/GLES.cpp:553`, `:577`). So TinyGL culls,
  transforms and clips (`src/TinyGL.cpp:230-258`). Then it calls
  `gles::RasterizeConvexPolygon(n, TGLVert*)` (`src/TinyGL.cpp:534`).
* `src/GLES.cpp:325-343` (sky branch):
  `st[0] = (x/w) * 0.5f; st[1] = (y'/w) * -0.5f + 0.5f;` with `y' = -y`.
  `xyzw = (x/w, y'/w, 1, 1)`, then `st[0] += -YAW_TO_FLOAT(this->tinyGL->viewYaw);`.
  `YAW_TO_FLOAT(x) = x/256` (`src/GLES.cpp:22`), `viewYaw = viewYaw & 0x3ff`
  (`src/TinyGL.cpp:186`). The positions are already divided by w, so the UVs
  are linear in screen space.
* The backdrop `gles::DrawSkyMap` (`src/GLES.cpp:1125-1157`) uses the same
  formula on an NDC ±1 quad, with fog off. `Render::render` calls it
  unconditionally before `renderBSP` (`src/Render.cpp:2344`). So a 301 wall
  is a window onto the backdrop.
* No depth test (`src/GLES.cpp:89`). Leaves are drawn far to near
  (`src/Render.cpp:1752`). The 301 quad paints over anything farther. The
  later 128 frame meshes paint over it.
* No time term anywhere in the sky UVs. Only lava tiles 479/480 animate in
  `drawNodeGeometry` (`src/Render.cpp:967-984`).

### E5. Rewrite

* `World3D::uploadMapTextures` special-cases only 302
  (`new_src/render/World3D.cpp:207-216`). For 301 it takes
  `media.mappings()[301]` = 882 and uploads the ramp
  (`new_src/render/World3D.cpp:218-240`). Nothing in `new_src/` refers to
  `TILENUM_SKY_BOX`/301. (A grep for `sky` finds only the backdrop code.)
* `World3D::drawPoly` draws every poly with world UVs `s/1024, t/1024` in
  NORMAL (`new_src/render/World3D.cpp:326-372`).
* The backdrop is already a faithful port: `SceneRenderer.cpp:125` →
  `World3D::drawSky` (`World3D.cpp:391-397`) → `SgScene3D::drawSky`
  (`new_src/render/sokol/SgScene3D.cpp:169-226`). It uses an NDC quad,
  u = ±0.5 − viewYaw/256, v = 1 at the bottom and 0 at the top, identity MVP,
  and fog off.
* There is no depth buffer (`new_src/render/sokol/SgPipelines.cpp:132-136`),
  so the painter's order matches the original.

## Root cause

Tile 301 is drawn with its raw shipped media (882, the gray ramp) and world
UVs. The original swaps in the sky texture with screen-space UVs.

## Minimal fix (for the architect/coder)

1. `uploadMapTextures`: skip tile 301. Do not upload media 882 for it.
2. `drawPoly`: when `p.textureId == 301`, draw the polygon as **sky**:
   * Transform each vertex by `camera.mvp()` (the same matrix and vertex
     scale `v/16384` as the world shader).
   * Clip the polygon against the near plane (w > ε, Sutherland–Hodgman on
     the convex fan).
   * Divide by w, and emit NDC vertices with `u = ndc.x*0.5 − viewYaw/256`,
     `v = −ndc.y*0.5 + 0.5`, z = 0.
   * Draw with the sky texture, identity MVP, fog off, alpha blend. This is a
     polygon version of `SgScene3D::drawSky`, for example
     `Scene3D::drawSkyPolygon(TextureId, const float* ndcXY, int n, float uOffset)`.
   Because the UVs must be screen-linear, per-vertex NDC UVs pushed through
   the normal perspective-correct world pipeline would warp. They must be
   pre-divided like the original.
3. Keep the draw at the poly's place in the BSP order, so the frame (tile 128)
   drawn after it still covers it.

A shortcut is to just skip 301 polys, so the backdrop drawn first shows
through. It looks right here, because nothing is drawn behind x = 704 in
view. But it breaks wherever farther geometry was painted before the sky wall.
Use it only as a stop-gap.

## Open questions

* Fog on 301 polys in the original: GL_FOG is left on by `SetupTexture(NORMAL)`
  and the sky branch does not disable it (`src/GLES.cpp:325-343` vs
  `:345-347`). The eye depth is 1.0. On map01 fog is forced off when
  `fogEnd > 0.499` (`src/GLES.cpp:192-195`). Fogged maps with sky walls may
  tint them, which is unverified. Side note: the rewrite's `setFog` applies the
  `> 0.499` cut-off on every map (`new_src/render/World3D.cpp:191`). The
  original gates it on `loadMapID == 2`. This needs a separate check.
* `fogAffectsSkyMap` (`EV_FOG_AFFECTS_SKYMAP`, `src/ScriptThread.cpp:1716-1718`)
  is only ever written (no reader in `src/*.cpp`), so it has no effect.
