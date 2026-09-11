# ADR 0024 — sokol_gfx is the single graphics backend; SDL2 stays the window, input and swapchain provider

Date: 2026-09-11
Status: accepted
Supersedes: ADR 0021, ADR 0022, ADR 0023 (all three become historical)
Scope: `new_src/render/sokol/`, `new_src/platform/Window.*`,
`new_src/core/RenderBackendFactory.*`, `third_party_libs/sokol*`
Spec: `specs/2026-09-11-sokol-gfx-backend.md`

## Context

Two backends live behind `Draw2D`/`Scene3D` (ADR 0020): raw OpenGL 3.3
(`render/gl/`) and SDL_Render (`render/sdl/`, ADR 0021). The split paid for itself —
it proved the abstraction does not leak and produced the shared
`QuadUV`/`PixelConvert`/`CanvasViewport`/`RenderModes` helpers and the F12 capture
diff — but it left two structural problems.

1. **Memory.** SDL2 rejects `INDEX8` textures on every driver, so `SdlTextureStore`
   expands every palette-indexed image to 32 bits: measured 59.1 MB against the GL
   path's 15.1 MB for the same world texture set. Roughly four times the texture
   memory, for a renderer that is strictly worse.
2. **Portability of the good path.** The 15.1 MB path is hand-written GL 3.3. It gives
   us nothing on Metal or D3D11, and a fourth hand-written backend per API is not a
   plan.

The user's decision: rewrite the GL backend on `sokol_gfx` as a cross-platform GPU
wrapper (Metal on Apple, D3D11 on Windows, OpenGL on Linux) and drop the SDL_Render
backend because of its memory cost. SDL2 stays for the window and input, and we write
the missing per-API surface/device creation ourselves.

The feasibility of the whole idea hinged on one unverified fact: whether
`SG_PIXELFORMAT_R8` exists in sokol_gfx and is guaranteed sampleable on Metal, D3D11
and GL. Our entire memory advantage is one-component index textures plus a 256×1
palette; without R8 the "less memory" motivation collapses and the migration would have
to be re-argued.

**Verified on 2026-09-11 against the upstream header (details and citations in the
spec, §0.1): `SG_PIXELFORMAT_R8` exists and is fully supported.** GL/GLES3, Metal,
WebGPU and Vulkan set the full capability set unconditionally at init; D3D11 queries
`DXGI_FORMAT_R8_UNORM`, whose texture2d + shader-sample support is mandatory from
feature level 10.0. We only need nearest sampling. There is no fallback to design.

Three further facts shaped the design (spec §0.2–§0.5): sokol_gfx creates no window,
device or swapchain and wants a fresh `sg_swapchain` every frame; we need no depth
buffer at all, which removes the hardest part of the Metal/D3D11 glue; clip-space depth
conventions differ between GL and Metal/D3D11 and must be fixed in the vertex shader;
and sokol's buffer-update rules forbid the write→draw→write→draw pattern our batching
uses (handled by ADR 0025).

## Decision

1. **One graphics backend: `dr_render_sokol`** in `new_src/render/sokol/`, implementing
   the unchanged `render/api/` interfaces. The GPU API is chosen at build time by the
   CMake variable `DOOM2RPG_SOKOL_BACKEND` (`metal` on Apple, `d3d11` on Windows,
   `glcore` elsewhere), mapping to exactly one of `SOKOL_METAL` / `SOKOL_D3D11` /
   `SOKOL_GLCORE`.
2. **SDL2 keeps the window and input; we write the glue sokol refuses to write.** A
   small `SgEnvironment` interface (create device → describe `sg_environment` → acquire
   an `sg_swapchain` per frame → present → read back) with one implementation per API:
   GL (context already created by `Window`), Metal (`SDL_Metal_CreateView` +
   `CAMetalLayer` + `MTLCreateSystemDefaultDevice`, `nextDrawable` per frame, and
   **nothing** in `present()` because sokol calls `presentDrawable` itself), D3D11
   (device + swap chain on the `HWND`).
3. **No depth buffer anywhere.** `depth_format = SG_PIXELFORMAT_NONE` in the
   environment, the swapchain and every pipeline. The renderer has always been
   painter's-order (`src/GLES.cpp:89`), so this is a faithfulness *and* simplicity win:
   no depth texture to allocate or resize in any environment.
4. **The index + palette scheme is kept verbatim**: `SG_PIXELFORMAT_R8` index image +
   `SG_PIXELFORMAT_RGBA8` 256×1 palette image, nearest filtering, `CLAMP_TO_EDGE` or
   `REPEAT` from `TextureFlags::Tiled`. `SgTextureStore::textureBytes()` must report the
   same number as `GlTextureStore` so the log line stays a cross-backend comparison.
5. **Shaders go through sokol-shdc**, integrated via the upstream
   `sokol_shaders.cmake`. Two hand-written annotated-GLSL files
   (`render/sokol/shaders/quad2d.glsl` with three programs, `world.glsl` with one),
   compiled at build time for `glsl410:glsl300es:metal_macos:metal_ios:metal_sim:hlsl5`
   into `${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/`. **Generated code never enters
   `new_src/`** — it is a build artifact, which keeps the project rule "C++ sources in
   `new_src/` are written by hand" intact. If the host has no shdc binary the configure
   step fails loudly; there is no committed-header fallback.
6. **All 14 legacy `RENDER_*` modes are carried over 1:1**, still from the single
   `render/api/RenderModes.cpp` table (ADR 0019). They collapse into **four** distinct
   `sg_blend_state`s — alpha (`SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA`), additive
   (`SRC_ALPHA`/`ONE`), subtractive (`ZERO`/`ONE_MINUS_SRC_COLOR`) and write-nothing
   (`ZERO`/`ONE`) — because the per-mode `mod[4]` and `fog` flags are uniforms, not
   pipeline state. Blend state moves from mutable GL state into immutable pipeline
   objects, which structurally kills the "a leftover `ADD50` darkened the next draw"
   family of bugs.
7. **Fog is per-pixel again on every backend.** ADR 0022 has no subject left.
8. **The SDL_Render backend is removed in full**, first thing (spec group G1):
   `render/sdl/`, the `dr_render_sdl` target, `BackendKind::SdlRender`,
   `GraphicsApi::SdlRender`, `Window::sdlRenderer()`, the CPU vertex pipeline, the
   object-space UV tile split, the per-vertex fog and haze pass, and the G7.1 adaptive
   tessellation with its `DOOM2RPG_SDL_TESS`/`DOOM2RPG_GFX_STATS` knobs. sokol gives
   hardware perspective correction on every API, so there is nothing left to tessellate
   against.
9. **The raw GL backend survives the whole migration and dies last** (spec group G8).
   It is the pixel reference: `--backend=gl` versus `--backend=sokol` built with
   `DOOM2RPG_SOKOL_BACKEND=glcore`, compared with F12 captures, gives a byte-level diff
   on the same driver and the same API. That technique caught two regressions nobody
   saw by eye; giving it up before the sokol path is proven would be trading the only
   tool that works for a week of guessing. `glcore` stays a configurable option
   permanently, so the diff never becomes unavailable.

## Rejected alternatives

* **Keep SDL_Render as a low-end fallback.** It costs 44 MB of texture memory, needs
  CPU clipping and tessellation nobody else needs, and its only remaining advantage —
  running without a GL driver — is covered better by `SOKOL_DUMMY_BACKEND` for tests.
  Rejected; this is the user's decision and the measurement supports it.
* **`sokol_app.h` instead of SDL2.** It would hand us window + swapchain + present for
  free on all three APIs. Rejected: SDL2 already carries input, game controllers, audio
  init and the whole video-mode list (`Window.cpp:14-21`), and the user decided SDL2
  stays. The missing piece is ~150 lines of per-API glue, which is cheaper than porting
  input and audio.
* **Write per-API backends by hand (a `render/metal/`, a `render/d3d11/`).** That is the
  problem sokol_gfx exists to solve, and it would multiply the 14-mode table, the
  texture store and the batching logic by three.
* **A shared palette atlas (one 256×N image, a row index per texture)** to cut the
  per-texture object count from 2 images + 2 views down to 1 + 1. Genuinely cheaper on
  pool slots and bindings. Rejected *for now*: it changes the texture data layout, which
  would break the byte-identical F12 diff that the entire migration plan is verified
  with. Revisit after G8 as a pure optimisation, with the diff re-established against
  the sokol backend itself.
* **`sg_append_buffer` for mid-frame batching** instead of a command list. Works today,
  but the header announces the update functions are being replaced and marks
  `dynamic_update` deprecated. See ADR 0025.
* **A pre-generated shader header committed under `new_src/`** so the build works
  without shdc. Rejected: it puts machine-generated C into the hand-written source tree
  and creates a second source of truth that will silently rot. A loud configure error on
  an unsupported host is the better failure.

## Consequences

* Texture memory stays at ≈15.1 MB and becomes the *only* number — the 59.1 MB path is
  gone. This was the user's stated reason for the change.
* The window must ask for **OpenGL 4.1 core** on every platform: sokol-shdc's lowest
  desktop-GL target is `glsl410`. macOS caps at exactly 4.1 core, so 4.1 is the one
  version that works everywhere. The raw-GL reference keeps its `#version 330` shaders,
  which compile fine in a 4.1 core context. The Linux/Windows compatibility-profile
  request (`Window.cpp:48-51`) goes away.
* Presentation ownership becomes API-dependent: GL swaps in `SgEnvironmentGl::present`,
  Metal is presented by sokol inside `sg_commit`, D3D11 presents its swap chain. A
  `Window::present()` that "just knows" what to do is no longer possible, and
  `Window::present()` degrades to a GL-only shim that disappears with ADR 0020's GL
  backend in G8.
* Metal and D3D11 cannot read the framebuffer back, so F12 becomes GL-only — see
  ADR 0026.
* vsync control for Metal/D3D11 is deferred (Metal: `CAMetalLayer.displaySyncEnabled`;
  D3D11: the `Present` interval). Until then those builds are vsync-on.
* Two new vendored third-party trees, `third_party_libs/sokol` and
  `third_party_libs/sokol-tools-bin`, whose commit hashes must be bumped **together**
  (the generated headers carry a `#version:5#` generator tag and static bind-slot
  defines tied to the header's resource model).
* ADR 0021/0022/0023 and the spec `2026-09-07-sdl-tessellation.md` become historical.
  The files stay: they are the record of *why* we know the SDL path was the wrong one,
  and the tessellation measurement is the evidence.
* G5/G6/G7.1 were not wasted work. They are the reason this is a transliteration and
  not a redesign: they proved `Draw2D`/`Scene3D` do not leak, they factored out every
  shared helper the sokol backend now reuses, and they built the F12 diff tool this
  migration is verified with.
