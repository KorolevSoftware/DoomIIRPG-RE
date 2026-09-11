# ADR 0026 — F12 frame capture stays OpenGL-only, and `DOOM2RPG_SOKOL_BACKEND=glcore` stays configurable forever

Date: 2026-09-11
Status: accepted
Scope: `new_src/render/sokol/SgEnvironment*`, `SgRenderBackend::writeCapture`
ADR context: 0024 (sokol is the backend)
Spec: `specs/2026-09-11-sokol-gfx-backend.md` §3, §8

## Context

F12 dumps the letterboxed canvas of the finished frame to a 24-bit BMP named
`capture-<backend>-NNN.bmp` (`new_src/render/api/BmpWriter.*`,
`new_src/render/gl/GlRenderBackend.cpp:95-119`). Comparing those files pixel by pixel
between backends is the single most effective verification tool this project has: it
caught the menu-cursor animation difference and a 2D-layer regression that neither the
user nor a reviewer noticed by eye. The project's ground rule is that nobody inspects
rendered images with scripts — the user is the eyes — which makes a byte-level diff
disproportionately valuable, because it is the one automated check on the picture.

`sokol_gfx` has **no framebuffer readback API**. There is no `sg_read_pixels`, and no
way to get an `sg_image`'s content back to the CPU. Readback is, by design, outside its
scope. Worse, on Metal sokol presents the drawable inside `sg_commit`
(`sokol_gfx.h:17207-17209`), so by the time control returns there is nothing left to
read; capturing there would need `CAMetalLayer.framebufferOnly = NO`, a dedicated blit
command buffer and a `waitUntilCompleted` — real work, on the hot path, for a debug
feature. D3D11 would need a staging texture and a `CopyResource` + `Map`.

## Decision

1. **Readback is an `SgEnvironment` responsibility**, declared as
   `bool readPixels(int x, int y, int w, int h, uint8_t* rgbaBottomUp)` returning false
   when the API cannot do it.
2. **`SgEnvironmentGl` implements it** with the same `glPixelStorei(GL_PACK_ALIGNMENT,
   4)` + `glReadPixels(..., GL_RGBA, GL_UNSIGNED_BYTE, ...)` the GL backend already uses,
   returning the bottom row first, which is BMP order — so the BMPs produced by
   `dr_render_gl` and by `dr_render_sokol(glcore)` are directly byte-comparable.
3. **`SgEnvironmentMetal` and `SgEnvironmentD3D11` return false** and log
   `"capture: not supported on the <apiName> environment"` once. F12 stays bound and
   never crashes.
4. **`DOOM2RPG_SOKOL_BACKEND=glcore` remains a supported configuration on every
   platform, permanently** — not a transition crutch. It is how the diff is run, and it
   is the answer to "did that change alter the picture?" for the rest of the project's
   life.
5. Because the diff lives on the GL path, the acceptance criteria of the migration
   groups that must be *exact* (the 2D layer, the world pass) are checked with
   `glcore`, and the Metal/D3D11 groups are checked by the user's eyes against the
   `glcore` build.

## Rejected alternatives

* **Render every frame into an offscreen canvas-sized render target and blit it to the
  swapchain**, so the capture reads a resource we own. It does not help: sokol still
  cannot read an `sg_image` back, so the readback stays API-specific — and it adds a
  full-screen blit to every frame plus a second colour format to keep in sync.
* **Implement Metal capture now** (`framebufferOnly = NO`, blit to a managed
  `MTLTexture`, `waitUntilCompleted`, `getBytes`). Feasible and maybe 60 lines, but it
  costs a GPU stall and a non-framebuffer-only layer on the normal path, and it buys
  nothing the `glcore` diff does not already buy — the comparison that matters is
  "sokol versus the known-good renderer", and that has to run on one API anyway to be
  meaningful. Deliberately deferred; if Metal-specific rendering ever diverges, this is
  the first thing to build.
* **Capture through SDL** (`SDL_GetWindowSurface`, a screenshot API). SDL2 has no
  portable way to read an accelerated window's pixels, and the GL/Metal window is not
  backed by an SDL surface at all.
* **Drop the capture tool.** Rejected outright: it is the project's only automated check
  on the rendered image, and it has already earned its place twice.

## Consequences

* The migration's "byte-identical" acceptance criteria (spec groups G4 and G5) are
  explicitly criteria for the `glcore` build. The Metal build's criterion is "looks like
  the `glcore` build", judged by the user.
* CMake must keep the `glcore` path buildable on macOS and Windows even after the
  respective native defaults land, and CI-equivalent manual checks should use it.
* A regression that exists *only* on Metal or *only* on D3D11 is not detectable by the
  diff. Such a regression would have to come from sokol's own per-API translation or
  from our per-API glue — the clip-space depth remap (spec §0.4) is the known example,
  which is exactly why it is specified as a numbered fact with its own acceptance check
  rather than left to be discovered.
* `capture-gl-NNN.bmp` and `capture-sokol-NNN.bmp` coexist by name for the duration of
  the migration (`RenderBackend::name()` supplies the prefix), so a run of one does not
  overwrite the other's evidence.
