# ADR 0021 — SDL_Render is a full second backend, 3D view included (affine texture mapping accepted)

Date: 2026-09-02
Status: HISTORICAL as of 2026-09-11 — superseded by ADR 0024 (the SDL_Render backend
is retired: 59.1 MB of texture memory against the indexed path's 15.1 MB, and nothing
it did was reusable on Metal/D3D11). Kept as the record of what was built and measured.
_Was: accepted (user decision, not up for re-litigation)._
Related: ADR 0020, ADR 0022

## Context

The second implementation exists for portability: SDL_Render runs on Metal, D3D11,
GLES2, OpenGL and (degraded) software, so a machine without usable GL 3.3 core can
still run the game. `SDL_RenderGeometry` (SDL >= 2.0.18; the dev machine has SDL
2.32.10) takes triangles with 2D float positions, per-vertex RGBA and normalized
texture coordinates. It has **no w component**, therefore no perspective-correct
interpolation, and no depth buffer.

Two facts make this acceptable for *this* game:
* the world never needed a depth buffer — the original GL path disables depth test and
  relies on BSP painter order (`new_src/render/World3D.cpp:446`, legacy
  `GLES::SetGLState`), which the rewrite reproduces;
* the game is corridor-shaped: walls are close, mostly frontal, and floors/ceilings
  are small quads generated per BSP leaf.

## Decision

The SDL_Render backend implements `Scene3D` too, with a CPU vertex pipeline inside the
backend:

1. transform `{x,y,z}` by the MVP handed over by `Camera3D` (already the faithful
   14.14-derived matrix);
2. clip each triangle against the near plane (`w > 1/1024`) with Sutherland-Hodgman,
   interpolating `x,y,z,w,u,v`;
3. perspective divide, map NDC to the current canvas viewport rect (the world band
   `1,7,478,248` of ADR 0009);
4. **affine** UVs: `tex_coord` = the interpolated `u,v` with no `w` division;
5. batch by (texture, render mode) and emit `SDL_RenderGeometry`.

Textures whose UVs leave `[0,1]` (world walls/floors/sky, flagged `Tiled` at creation)
are **split in object space** against integer `u`/`v` boundaries before step 1, because
SDL_Render textures are `CLAMP_TO_EDGE` and expose no wrap mode. Splitting is exact:
UVs are affine in object space, so a plain polygon clip against `u = k` / `v = k`
yields sub-triangles whose UVs all fall inside one tile.

Palette-indexed data is expanded at texture creation time, through an `INDEX8`
`SDL_Surface` + palette handed to `SDL_CreateTextureFromSurface`, so SDL performs the
conversion once into whatever format the driver actually wants. No atlas, no eviction.

**Measured on this machine, 2026-09-02** (probes `sdl_fmt_probe.c` for SDL2 and
`sdl3_fmt_probe.c` for SDL3, both compiled and run, output reproducible, kept in the
scratchpad). The two SDL generations behave *differently in kind*:

*SDL 2.32.10 — the version the project currently builds against
(`CMakeLists.txt:18`, `new_src/CMakeLists.txt:35`):*

* `SDL_CreateTexture(SDL_PIXELFORMAT_INDEX8, ...)` is rejected on **every** driver with
  the literal message *"Palettized textures are not supported"*. Indexed textures are
  impossible; expansion is mandatory.
* Trap: `ARGB1555` / `RGB565` textures are *created* "successfully", but the `metal`
  driver advertises only 32-bit formats in `SDL_RendererInfo.texture_formats`
  (ARGB8888/ABGR8888 + YUV), so SDL keeps an intermediate surface and converts on every
  update — *more* memory, not less.
* No driver (`metal`, `opengl`, `software`) advertises a 16-bit format **with alpha**
  (`ARGB1555`/`ARGB4444` absent everywhere), and every sprite needs the transparent key.
  So sprites are 32-bit with no alternative.
* Working path: `INDEX8` `SDL_Surface` + palette -> `SDL_CreateTextureFromSurface`,
  which converts **once**. Observed result: `ARGB8888` on metal, `RGB888` (24-bit) on
  opengl/software.
* Cost: 9.7 MB of indexed texels (39 `newTexels*.bin`) + 328 KB of palettes become
  **~40 MB** of video memory expanded to 32 bits.

*SDL 3.4.0 — installed on the machine, not yet adopted by the project:*

* Indexed textures are supported **natively**: every working driver (`metal`, `opengl`,
  `gpu`, `software`) advertises `SDL_PIXELFORMAT_INDEX8`, `SDL_CreateTexture(INDEX8)`
  returns a real 8-bit texture (*actual format: SDL_PIXELFORMAT_INDEX8, 8 bpp*), and
  `SDL_CreateTextureFromSurface` on an INDEX8 surface **keeps** INDEX8 instead of
  converting.
* Texture palettes are a first-class, mutable object: `SDL_SetTexturePalette` /
  `SDL_GetTexturePalette` (`/opt/homebrew/include/SDL3/SDL_render.h:1018,1033`) plus the
  creation property `SDL_PROP_TEXTURE_CREATE_PALETTE_POINTER` (`:696,813`).
* 16-bit formats **with alpha** work: `ARGB1555` and `ARGB4444` are created as real
  16-bit textures on all drivers.
* Cost: the indexed pipeline survives end to end — 9.7 MB stays 9.7 MB. The ~40 MB
  figure above is an **SDL2-only** number.

Therefore the pixel format is **not fixed at design time**: `SdlTextureStore` asks the
runtime what it has (`SDL_RendererInfo.texture_formats` on SDL2 / the SDL3 equivalent)
and picks the best available; the probes can be re-run on another platform.

**Open risk (must be measured before anything depends on it):** it is unknown whether
an SDL3 driver expands INDEX8 on the **GPU** (palette LUT in its shader) or on the
**CPU** when the texture is updated. If the latter, `SDL_SetTexturePalette` implies a
full re-upload of the texels, which would make palette-based fog unusable for
performance reasons. See the spec §4.2.3 for the concrete measurement.

Affine texture mapping is unaffected by any of this: `SDL_RenderGeometry` has no `w`
component in either generation.

## Consequences — what this looks like on screen

* **Affine warping.** On a large polygon seen at a steep angle (the floor and ceiling
  right in front of the camera, a long side wall passing the view edge) the texture
  pattern bends/swims and the diagonal seam between the two triangles of a quad
  becomes visible as a crease in the pattern; straight grout lines kink at that
  diagonal. Magnitude grows with the depth ratio between the triangle's nearest and
  farthest vertex, so it is worst on the floor quad under the player's feet and
  invisible on frontal walls. Camera-facing billboards (all sprites, monsters, items)
  are at constant depth and are therefore **pixel-identical** to the GL path apart
  from rounding. The sky quad is drawn in screen space and is unaffected.
* **Ordering.** No depth buffer, exactly like the GL path; any painter-order bug will
  look the same in both backends (a useful property: it isolates backend bugs from
  port bugs).
* **Memory (measured, not estimated).** On **SDL2**: world media 9.7 MB of indexed
  texels + 328 KB of palettes -> **~40 MB** expanded to 32 bits for the complete world
  texture set (a 256x256 blob = 256 KB versus 66 KB as indices + LUT); UI: the 43 sheets
  the rewrite currently loads are 783,269 px -> **3.0 MB** (0.77 MB as indices); all 275
  shipped BMPs would be 13.2 Mpx -> 50 MB but are never all resident. On **SDL3**: the
  indexed pipeline is preserved, so 9.7 MB stays 9.7 MB and the UI stays at 0.77 MB.
  Either way fine on desktop; the store counts bytes and logs the total so a regression
  is visible.
* If a chosen SDL driver refuses `SDL_ComposeCustomBlendMode` (software renderer,
  D3D9), the subtractive mode degrades — see the spec §5 fallback and its on-screen
  description.

## Rejected alternatives

* **2D-only SDL backend with the world band left blank.** Rejected by the user: the
  point is a playable fallback, not a menu viewer. (It *is* kept as an intermediate
  implementation step so the tree never stops being playable — spec group G5.)
* **Subdivide world quads into a grid to fight the warping.** Deferred, not rejected:
  it is a pure win inside `SdlScene3D` (error falls quadratically with subdivision)
  but costs triangles and complexity; do it only if the user dislikes the picture.
* **Our own software rasterizer with perspective correction (a TinyGL revival).**
  Rejected: it re-imports the whole legacy rasterizer, is slow at -O0, and duplicates
  what SDL_Render already does on the GPU.
* **Pre-tiled textures instead of the object-space split.** Rejected: unbounded
  texture-memory blowup keyed by every repeat count that occurs in a map.
* **Asking SDL for 16-bit textures to halve the memory.** Rejected **on SDL2** by
  measurement: no driver there advertises a 16-bit format with alpha, and `metal`
  advertises no 16-bit format at all, so the request silently buys an extra conversion
  surface instead of saving memory. On SDL3 `ARGB1555`/`ARGB4444` do work, but they are
  pointless there: the indexed path is cheaper still.
* **Baking the fog levels into expanded textures (the original's 16 palettes).**
  Rejected **on SDL2**: the expansion result depends on the palette, so a world texture
  would need one expansion per palette *per* fog level — the measured 40 MB becomes
  hundreds of MB. On **SDL3** the same effect is expressible cheaply through
  `SDL_SetTexturePalette` and is *not* rejected — it is gated on the open risk above
  (ADR 0022 and spec §4.2.3).
