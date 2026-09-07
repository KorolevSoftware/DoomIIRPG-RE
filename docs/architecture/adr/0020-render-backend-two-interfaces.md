# ADR 0020 — The graphics backend is a library behind two need-shaped interfaces (`Draw2D` / `Scene3D`)

Date: 2026-09-02
Status: accepted
Related: ADR 0009 (world viewport), ADR 0012 (UI layer), ADR 0019 (blend table), ADR 0021, ADR 0022

## Context

The rewrite has one graphics implementation (GL 3.3 core) and one half-abstraction,
`render/RenderBackend`, which failed as a seam for two concrete reasons:

1. It was designed *from below*, i.e. from what the GL sprite batcher already did.
   Its public header includes `render/gl/SpriteBatch.h` (`new_src/render/RenderBackend.h:6`),
   so GL types leak into every translation unit that renders anything.
2. It only ever covered the 2D layer. The 3D world stayed outside it: `World3D.cpp`
   contains 72 direct `gl*` calls, its own shader sources
   (`new_src/render/World3D.cpp:59-96`), its own VAO/VBO and its own blend-state
   machine (`applyBatchState`).

Meanwhile the *content* of the game is strictly two-layered, and the split is the
original's own: `Graphics`/`Image` (2D blits) versus `TinyGL`/`gles` (world). Research
(`docs/original-code/image-formats.md`, `docs/research/2026-09-01-image-formats.md`)
adds the decisive fact: every shipped image is palette-indexed, there is no
true-color content, and a *shader* is needed only by the 3D world (index expansion +
fog). The whole 2D layer is textured quads with a tint, a blend mode and 1-bit
transparency — expressible on any 2D API from 1998 onwards.

## Decision

Split the renderer into a small backend-neutral core plus interchangeable
implementations, with the interface shaped by the *callers*:

* `render/api/Draw2D` — the 2D output device. Five operations: `drawQuad`,
  `fillQuad`, `setRenderMode`, `setClipCanvas`/`clearClip`, `flush`. Derived from the
  actual call inventory of `Graphics2D`, `Ui`, `Font`, `Hud*`, `Menu*`, `Dialog*`,
  `ViewWeapon` — nothing else is used by anybody.
* `render/api/Scene3D` — the world device. Seven operations: `beginScene`/`endScene`,
  `setTexture`, `setRenderMode`, `submitTriangles`, `setFog`, `drawSky`. Derived
  from what `World3D` actually produces: a stream of `{x,y,z,u,v}` triangles in world
  units, in painter's order, grouped by texture and legacy `RENDER_*` mode.
* `render/api/TextureStore` + opaque `TextureId` + move-only `Texture` handle — the
  only way to own pixels. Creation takes *indices + an RGB565 palette*, because that
  is what the data is; whether the implementation keeps it indexed (GL: R8 + LUT) or
  expands it (SDL: RGBA8) is its private business.
* `render/api/RenderBackend` — the abstract frame/device owner (frame begin/end,
  canvas viewport, letterbox mapping, capture) returning the three sub-interfaces.
* `Graphics2D`, `World3D`, `SceneRenderer`, `Camera3D` become **backend-neutral game
  code** that talks only to those interfaces.

Explicitly *outside* the interfaces: shaders and GLSL of any kind, `GLuint`/`GLenum`/
any GL header, `SDL_Texture`/`SDL_Renderer`, palettes as GPU objects, VAO/VBO/batch
sizes, GL blend enums (a neutral `BlendFactor` enum is used by the shared
`RenderModes` table instead), depth buffers, render targets, matrices as uniforms
(the camera hands over `float[16]`).

CMake gets three new targets: `dr_render_core` (interfaces + neutral helpers, no GL,
no SDL), `dr_render_gl`, `dr_render_sdl`.

## Consequences

* The 2D layer stops seeing GL entirely; `ui/`, `text/`, `io/` include only
  `render/api/*`.
* `World3D` becomes a geometry/ordering module — the single largest win, because that
  is where every faithful port detail lives and it must never be duplicated per
  backend.
* Adding a backend means implementing 3 small interfaces (~15 virtuals), not
  re-porting the world renderer.
* Cost: one extra virtual call per quad/triangle batch (not per vertex), one
  indirection for texture lookup, and a `TextureStore&` argument added to every
  texture upload call site.
* `RenderBackend` keeps the exact method names it has today, so the state-machine and
  UI call sites survive the split unchanged.

## Rejected alternatives

* **Keep `RenderBackend` concrete and `#ifdef` the implementation.** Rejected: the
  two implementations must be comparable *at runtime* in the same build (that is the
  entire point — see the acceptance criteria of the spec), and `#ifdef` would keep
  every leak in place.
* **One flat interface for 2D and 3D.** Rejected: the two halves have disjoint state
  (2D: clip + canvas coords; 3D: camera + fog + painter order) and disjoint
  implementation risk; the original itself keeps them apart.
* **Abstract at the "draw a scene graph" level.** Rejected: the port's fidelity lives
  in per-quad, per-mode decisions (`docs/original-code/rendering.md` §7-§8); hiding
  them behind a scene graph would force the faithful logic into each backend.
* **Abstract at the GL-command level (a mini-GL).** Rejected: that is exactly the
  mistake being undone; a mini-GL forces every backend to emulate GL semantics
  (texture wrap, per-pixel shading) instead of doing what it is good at.
