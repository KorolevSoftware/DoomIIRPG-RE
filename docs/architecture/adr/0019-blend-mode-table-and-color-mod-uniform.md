# ADR 0019 — Blend modes are one table; color modulation is a per-draw `uColorMod` uniform

Date: 2026-09-01
Status: accepted
Scope: `new_src/render/World3D.{h,cpp}` (world/sprite pass only)

## Context

The original has a closed 14-value `RENDER_*` space (`src/Render.h:18-31`) whose GL
state is decided in exactly one switch, `gles::SetupTexture`
(`src/GLES.cpp:615-706`), documented in `docs/original-code/rendering.md` §8.
Each mode is a triple:

1. `glBlendFunc(src, dst)`,
2. a **primary color** `glColor4f(r,g,b,a)` combined with the texel by the
   *fixed* `GL_MODULATE` texture env re-asserted at `src/GLES.cpp:620`, i.e.
   `Cout = Ctex·Ccolor`, `Aout = Atex·Acolor`,
3. a fog toggle (`fogMode = 2` for the alpha modes, `0` for `ADD*`/`SUB`/`NONE`,
   `src/GLES.cpp:709-715`).

Vertex colors are **not** involved: `glDisableClientState(GL_COLOR_ARRAY)`
(`src/GLES.cpp:98`) and the vertex fed to `glDrawElements` carries only `xyzw`
plus `st` (`src/GLES.cpp:559-571`). The modulation factor is therefore a single
piece of *state*, latched once per batch, cached together with the mode at
`src/GLES.cpp:615`.

We target GL 3.3 core, where neither `glColor4f` nor `glTexEnv` exists. So this
is a translation, not a transcription, and the translation has one real hazard:
our world pass **batches**. `World3D` accumulates triangles into `vertices_` and
issues one `glDrawArrays` per flush (`new_src/render/World3D.cpp:414-421`); the
batch is flushed on texture change (`:475-484`, `:1008-1017`) and on render-mode
change (`applyBatchState`, `:426-446`). If a modulation factor were latched as
uniform state while vertices with *different* factors sat in one batch, the port
would silently draw them all with the last factor.

Current state of the port: `applyBatchState` handles only modes 3 and 7 and has
**no modulation at all** (`new_src/render/World3D.cpp:426-446`), so `BLEND25/50`
draw opaque and `ADD75/50/25` draw as plain alpha blending. The world fragment
shader has no color term (`:76-96`).

## Decision

1. **One data table, no switch.** A file-local
   `struct BlendMode { GLenum src, dst; float mod[4]; bool fog; }` with 14 rows
   indexed by the legacy mode value reproduces §8.2 verbatim. `applyBatchState`
   becomes a table lookup plus the existing flush-compare-assign.
2. **Modulation becomes a `uniform vec4 uColorMod`** in the world program,
   multiplied into the sampled palette color *before* the fog mix
   (`col *= uColorMod;` then the existing `mix(uFogColor.rgb, col.rgb, f)`) —
   that is the fixed-pipeline order: texture env first, fog applied to the
   post-texture fragment.
3. **The uniform is written only from `applyBatchState`, which already flushes.**
   The mode is part of the batch key exactly as `(renderMode, flags)` is the
   cache key in the original, so "one factor per batch" is structurally
   guaranteed: a mode change flushes the pending vertices under the *old*
   uniform, then writes the new one. No new batching rule is introduced.
4. Any program user that does **not** go through `begin()` must write
   `uColorMod` itself. Today that is exactly `World3D::drawSky`
   (`new_src/render/World3D.cpp:530-560`): uniforms live in the program object
   and survive across frames, so a leftover `0.5` from a torchiere glow would
   darken the sky on the next frame.

## Rejected alternatives

* **Per-vertex color attribute.** Would add 16 bytes to every world vertex
  (`Vertex` is 20 bytes today, `World3D.h:26-29`, and the VBO is preallocated
  for 65536 of them), require a third `glVertexAttribPointer`, and let vertices
  with different factors share a batch — a *broader* capability than the
  original has, at a permanent memory/bandwidth cost, to model state that is
  per-draw by construction. Rejected.
* **Baking the factor into the palette LUT** (what the software rasterizer does,
  `src/Render.cpp:1981-2021`). Would multiply the texture cache by the number of
  modes and quantize to 5/6/5 twice. Rejected: we are the GL path.
* **A separate additive pass with sorted sprites.** The original never reorders
  by mode: the sort key is position/tile only (`src/Render.cpp:837-873`) and
  there is no depth buffer at all (`src/GLES.cpp:89`). A second pass would
  change draw order and therefore the picture. Rejected.
* **`glBlendColor` / `GL_CONSTANT_COLOR`.** Only reaches the blend stage, cannot
  modulate the texel before the fog mix, and cannot express `Aout = Atex·Acolor`.
  Rejected.

## Consequences

* Modes 1/2/4/5/6/9/12 start to look different from mode 0 for the first time in
  the rewrite; anything that already looked "right" while silently drawing as
  mode 0 will change (that is the point — see spec group G3).
* `RENDER_SUB` keeps the GL semantics `dst·(1 − Csrc)` (`src/GLES.cpp:672-677`),
  not the software rasterizer's true clamped subtract (`src/Span.cpp:230-244`).
  The divergence exists in the original; we are porting the GL path.
* `RENDER_NONE`'s primary-color leak (`src/GLES.cpp:684-688` sets no color) is
  **not** reproduced: mode 10 gets `mod = (1,1,1,1)` in the table. The leak is
  unobservable (`glBlendFunc(GL_ZERO, GL_ONE)` writes nothing) and reproducing it
  would make the uniform's value depend on draw history.
* The `flags` half of the legacy cache key (`RENDER_FLAG_PULSATE`/`RED`/`GREEN`/
  `BLUE_SHIFT`, `src/GLES.cpp:717-757`) is out of scope. When it lands it plugs
  into the same uniform (PULSATE also overrides the blend func), and the cache
  key must become the pair — `currentRenderMode_` gains a `currentFlags_`
  sibling. Nothing in this ADR blocks that.
