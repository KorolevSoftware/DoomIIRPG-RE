# ADR 0025 — The sokol backend records one frame command list; the GPU is touched only between `sg_begin_pass` and `sg_commit`

Date: 2026-09-11
Status: accepted
Scope: `new_src/render/sokol/SgFrame.*`, `SgDraw2D.*`, `SgScene3D.*`, `SgRenderBackend.*`
ADR context: 0024 (sokol is the backend), 0020 (the two interfaces)
Spec: `specs/2026-09-11-sokol-gfx-backend.md` §0.5, §5.5

## Context

Both devices batch and flush many times per frame. `GlDraw2D::flush` flushes on texture
change, on render-mode change, on clip change and on buffer overflow
(`new_src/render/gl/GlDraw2D.cpp:238-300`); `GlScene3D::flush` flushes on texture
change, render-mode change and at 65536 vertices
(`new_src/render/gl/GlScene3D.cpp:141-215`). Each flush today is a `glBufferData` +
`glDrawArrays` pair — write, draw, write, draw, dozens of times per frame, on the same
buffer object. The flush boundaries are also the *draw order*, and the draw order is the
picture: the renderer has no depth buffer and relies entirely on painter's order.

sokol_gfx does not allow that pattern on one buffer:

* `sg_update_buffer` — "Only one update per frame is allowed" (`sokol_gfx.h:340-342`).
* `sg_write_buffer_transient` — multiple writes per frame are allowed "but only until
  the resource is bound. After a resource has been used in a frame it cannot be written
  to until the next frame" (`sokol_gfx.h:323-326`).
* `sg_append_buffer` — does support interleaved append/draw and is the classic sokol
  dynamic-batching idiom, but it requires `sg_buffer_usage.dynamic_update`, which the
  header marks **deprecated** (`sokol_gfx.h:3358`), and the header states outright that
  the update functions "will be replaced with more flexible 'write-persistent'
  functions in the next resource API update" (`:349-350`).

There is a second, independent pressure: with sokol, all rendering must happen inside
one `sg_begin_pass` / `sg_end_pass` bracket whose `sg_swapchain` is acquired for that
frame, and `sg_begin_pass` resets the viewport and scissor to the full framebuffer
(`sokol_gfx.h:289-290`). The current architecture calls into the GPU from wherever the
game happens to be — `setCanvasViewport` from `SceneRenderer`, `setClipCanvas` from
`Ui`, a mid-frame `glReadPixels` for F12 — with no single point where a pass could be
opened and closed.

## Decision

**Nothing in the renderer talks to the GPU while the frame is being built. Both devices
append to one CPU-side vertex staging vector and record commands into one ordered
`SgFrame` command list. `SgRenderBackend::endFrame` performs a single
`sg_write_buffer_transient`, opens the pass, replays the list, closes the pass and
commits.**

1. **One list, shared by both devices.** `SgFrame` is owned by `SgRenderBackend` and
   handed to `SgDraw2D` and `SgScene3D` by reference. A single list is not a convenience
   — it is *required*: the world is drawn between 2D batches, so 2D and 3D commands must
   interleave in the order they were issued.
2. **Three command kinds only:** `Viewport`, `Scissor`, `Draw`. Viewport and scissor
   rects are recorded in drawable pixels with top-left origin, replayed through
   `sg_apply_viewport`/`sg_apply_scissor_rect` with `origin_top_left = true` — sokol
   does the per-API flip for us (`sokol_gfx.h:12296-12303`, `:14945-14963`).
3. **One vertex buffer for both layouts.** The 2D stride is 32 bytes and the world
   stride is 20; both are multiples of 4, which is all
   `sg_bindings.vertex_buffer_offsets` requires. The buffer is created with
   `.usage = { .vertex_buffer = true, .write_transient = true }`, 4 MB, and is written
   exactly once per frame, before anything binds it — precisely the contract
   write-transient asks for.
4. **Uniform values are captured per command, not latched as state.** Each `Draw`
   carries an index into a per-frame `Uniforms` array holding both blocks (`mvp`,
   `view`, `depth_fix`, `color_mod`, `fog_color`, `fog_params`, `canvas_size`). Replay
   applies them with `sg_apply_uniforms` right before `sg_draw`. This is the same
   guarantee ADR 0019 point 3 argued for the GL path ("one modulation factor per
   batch"), but structural instead of procedural: a command physically cannot be drawn
   under another command's uniforms.
5. **The flush-compare-assign caches stay exactly as they are.** `setTexture` and
   `setRenderMode` keep the identical early-outs and flush points, because those
   boundaries are the draw order and the draw order is the picture. The only change
   inside `flush()` is that it records instead of drawing.
6. **Overflow is loud.** `appendVertices` past 4 MB drops the vertices, returns -1 (the
   caller then records nothing) and logs once per frame. Visibly missing geometry plus
   a log line beats a silent truncation.
7. **`sg_begin_pass` resets the viewport, so `beginFrame` records the letterbox
   `Viewport` command first, every frame.**

## Rejected alternatives

* **`sg_append_buffer` with interleaved draws.** The smallest diff from the current
  code — `glBufferData` → `sg_append_buffer`, `glDrawArrays` → `sg_draw` — and it works
  today. Rejected: it stands on a deprecated usage flag and on functions the header
  says are about to be replaced, and it would leave every GPU call scattered across the
  frame, which does not survive the "one pass per frame" requirement anyway (the pass
  would have to be opened in `beginFrame` before the swapchain image is needed, and
  `setCanvasViewport` would still be a GPU call from game code).
* **One buffer per flush** (a pool of small write-transient buffers). Legal, but the
  buffer count is unbounded by construction and each is a pool slot; and it still leaves
  `sg_draw` calls scattered.
* **Two command lists, one per device, replayed 3D-then-2D.** Rejected: it reorders the
  frame. The world is drawn *inside* a 2D frame, and reordering changes the picture (the
  same reason ADR 0019 rejected a separate additive pass).
* **Keeping uniform values as replay-time state** (a running "current uniforms" struct
  mutated by dedicated commands). Fewer bytes, but it reintroduces exactly the
  state-leak hazard ADR 0019 point 4 had to reason about (the sky inheriting a
  torchiere's `ADD50` modulation). ~176 bytes × a few hundred commands is under 100 KB
  per frame; not worth the risk.

## Consequences

* There is exactly one place in the program that calls `sg_apply_*` and `sg_draw`:
  `SgFrame::replay()`. Every GPU-ordering question has one place to look.
* `flushFrame()` (the loading-screen mid-frame present) becomes a full
  pass/replay/commit/present cycle followed by a fresh `beginFrame` on the list, instead
  of a `glFlush`.
* Per frame the renderer holds 4 MB of GPU vertex memory plus a 4 MB CPU staging vector
  that is reserved once and never reallocated. Against the ≈15.1 MB of texture memory
  and the 44 MB the SDL backend cost, this is an acceptable price for the ordering
  guarantee; it is also the one number to shrink first if memory becomes tight again.
* Debugging gains a natural hook: the command list is a plain vector of PODs, so a
  future `--dump-frame` that prints it is a few lines — a trace-level analogue of the
  F12 pixel diff.
* Blend state is no longer mutable: it lives in immutable pipeline objects selected per
  command (ADR 0024 point 6), so the whole class of "a leftover blend func from the
  previous draw" bugs — which the GL path needed explicit `endScene` restoration for
  (`GlScene3D.cpp:132-139`) and an explicit re-assert in `GlDraw2D::flush` for
  (`GlDraw2D.cpp:283-287`) — cannot happen.
