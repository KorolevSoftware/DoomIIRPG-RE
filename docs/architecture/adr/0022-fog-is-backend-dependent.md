# ADR 0022 — Fog is a backend-dependent effect (per-pixel on GL, per-vertex on SDL)

Date: 2026-09-02
Status: HISTORICAL as of 2026-09-11 — superseded by ADR 0024. With sokol_gfx the fog is
per-pixel on every backend again, so this ADR has no subject left.
_Was: accepted (user decision)._
Related: ADR 0019, ADR 0020, ADR 0021

## Context

The original has *two* different fogs already:

* Software path: for every span it picks one of **16 pre-baked palettes** by the
  interpolated inverse depth — `src/TinyGL.cpp:47-52` (the `fogTables` / shade-level
  selection), used at `src/TinyGL.cpp:654` and `:662`. Fog is thus quantized both in
  depth (16 steps) and in color (a re-baked palette per step).
* GL path: fixed-function `GL_FOG` / `GL_LINEAR` with `glFogf(GL_FOG_START/END)`,
  `src/GLES.cpp:100-103`. Per-pixel, continuous — and *not available at all* in GL 3.3
  core, which has no fixed-function pipeline.

The rewrite therefore already deviates from both: it computes fog per pixel in the
world fragment shader from eye-space depth (`new_src/render/World3D.cpp:59-96`), which
is the closest thing to the GL path. `SDL_RenderGeometry` has no programmable stage at
all, so per-pixel fog is impossible on the SDL backend.

Measurements of 2026-09-02 (probes described in ADR 0021) show the SDL side splits by
generation:

* **SDL 2.32.10 — what the project builds against today.** Indexed textures do not
  exist (`SDL_PIXELFORMAT_INDEX8` is rejected by every driver with *"Palettized textures
  are not supported"*), so every world texture is expanded once **against its palette**.
  Reproducing the original's 16 baked fog palettes would mean one expanded copy per
  palette *per fog level*: the measured 9.7 MB of indexed texels (+328 KB of palettes),
  which already becomes ~40 MB expanded to 32 bits, would grow into hundreds of MB. Fog
  therefore *has* to be vertex colour, which `SDL_RenderGeometry` carries natively.
* **SDL 3.4.0 — installed but not adopted.** Indexed textures are native and a texture's
  palette is a mutable object (`SDL_SetTexturePalette`,
  `/opt/homebrew/include/SDL3/SDL_render.h:1018`). The original's approach becomes
  *expressible*: pick one of 16 pre-baked palettes and swap it per draw — per texture /
  per draw call rather than per span as the software original does
  (`src/TinyGL.cpp:47-52`, calls at `:654,662`). The same mechanism would also make the
  `Render::setupPalette` palette effects (greyshift, pulsate, multiply-shift)
  expressible the way the original does them.
  **Unresolved risk:** whether a driver expands INDEX8 on the GPU (palette LUT in its
  shader) or on the CPU at update time. If the latter, a palette swap implies a full
  texel re-upload and palette fog is unusable for performance reasons. This must be
  measured before anything depends on it (spec §4.2.3).

## Decision

Fog is declared a **backend-dependent** effect. `Scene3D::setFog(enabled, start, end,
rgba)` states the *intent* (a linear eye-space fade to a color, in the units the port
already computes from `fogMin`/`fogRange`); each backend renders it as well as it can:

* **GL**: unchanged — per-pixel in the fragment shader, RGB only (alpha untouched, so
  keyed billboard texels stay transparent).
* **SDL**: **per-vertex**, computed by the same formula during the CPU vertex pass:
  * fog factor `f = clamp((end - depth) / (end - start), 0, 1)` per vertex;
  * for **opaque geometry** (texture created without `TransparentKey` — i.e. walls,
    floors, ceilings, sky), the exact `mix(fogColor, texel, f)` is reproduced with a
    second, untextured pass over the same triangles: color = `fogColor`, per-vertex
    alpha = `1 - f`, standard alpha blend. Two `SDL_RenderGeometry` calls per fogged
    geometry batch.
  * for **keyed content** (sprites, billboards, decals) the haze pass is skipped and
    fog is approximated by multiplying the vertex color by `f`. Reason: an untextured
    haze quad would paint a rectangle over the sprite's transparent texels. All four
    corners of a camera-facing billboard sit at nearly the same depth, so the
    multiplication is uniform over the sprite; the visible difference from GL is that
    a sprite fades toward **black** instead of toward the fog color.
  * a fast path: when `fogColor` is (near) black the haze pass is skipped for geometry
    too, because `mix(black, c, f) == c * f` exactly.
* The original's 16-step quantization is reproduced by **neither** backend as
  implemented; we keep continuous fog, as the GL path does.
* **Not decided here (user's call):** if the project ever moves to SDL3, palette-swap
  fog becomes available on the SDL path and would be *closer to the original* than our
  own GL path is. That is a migration decision with its own cost, and it is gated on the
  GPU-vs-CPU expansion measurement above. Until such a decision exists, the SDL backend
  ships per-vertex fog and this ADR stands as written.

## Consequences — what this looks like on screen

* GL: unchanged from today.
* SDL: fog gradients become linear between polygon vertices. On big floor/ceiling
  quads a faint faceting can appear along the triangle diagonal (the fade is correct
  at the three corners, slightly off in the middle); wall corners and doorway frames
  fade correctly. Sprites keep one fog value each, and in a *colored* fog (e.g. the
  greenish `0xFF1A2A1A` used in the Main.cpp test line) a distant monster darkens
  instead of taking the fog tint — most noticeable on light-colored sprites deep in a
  fogged corridor.
* Fog is off for the additive/subtractive render modes in both backends, because the
  legacy table says so (ADR 0019, `src/GLES.cpp:709-715`) — muzzle flashes and glows
  never get fogged in either implementation.

## Rejected alternatives

* **Port the original's 16 pre-baked palettes for the SDL path.** Rejected **for the
  SDL2 baseline** by measurement: SDL2 has no indexed textures, so each fog level needs
  its own expanded copy of every texture — the measured ~40 MB expanded world set turns
  into hundreds of MB (16x), or into a per-frame CPU expansion. **Not rejected for
  SDL3**, where `SDL_SetTexturePalette` makes it cheap in memory; there it is deferred
  behind (a) the SDL3 migration decision, which is the user's, and (b) the GPU-vs-CPU
  INDEX8 expansion measurement.
* **No fog at all on SDL.** Rejected: fog is load-bearing for several maps' mood and
  for hiding the far clip; a missing fade would read as "the SDL backend is broken".
* **A full-screen fog overlay by depth bucket (draw the world in N depth slices, tint
  each).** Rejected: it needs the world split by depth, which fights the BSP painter
  order the port depends on.
