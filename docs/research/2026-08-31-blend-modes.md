# Sprite blend modes (`RENDER_*`) and color modulation — full audit

Date: 2026-08-31
Question: what is the complete `RENDER_*` blend-mode system in the original
(`src/`)? Exact GL state per mode, the color-modulation factor and *how* it is
applied, the TinyGL software equivalent, every assignment site of
`S_RENDERMODE`, the torchiere glow branch, tile 240 in brief, and the effect of
mode on draw order.

Verdict: **CONFIRMED** — modes form a closed 14-value enum; modulation in the GL
path is a single `glColor4f` per batch under `GL_MODULATE`; the software
rasterizer reproduces the same factors by pre-scaling the *palette* and using a
shared additive span. `ADD50` really means "add half the texel value".

## Method

grep for `RENDER_`, `S_RENDERMODE`, `glBlendFunc`, `glDepth*`, `setupPalette`,
`span*` over `src/`; read `gles::SetupTexture`, `Render::setupPalette`,
`Render::setupTexture`, `Render::renderSpriteObject`, `Render::addSprite`,
`Render::renderBSP`, `Span.cpp`, plus every write site of `S_RENDERMODE`.

## Evidence highlights

* Enum: `src/Render.h:18-31` (`NORMAL 0 … RENDER_MAX 14`, with `UNK = 8`,
  `PERF = 9`, `NONE = 10`, `BLEND75 = 12`, `BLENDSPECIALALPHA = 13`; 11 unused).
* GL state machine: `src/GLES.cpp:615-706` inside `gles::SetupTexture`, guarded
  by `if (renderMode != this->renderMode || flags != this->flags)`
  (`src/GLES.cpp:615`) — state is cached per `(renderMode, flags)` pair.
* Modulation is a *global* color, not vertex colors:
  `glDisableClientState(GL_COLOR_ARRAY)` (`src/GLES.cpp:98`) and the vertex
  struct fed to `glDrawElements` carries only `xyzw`/`st`
  (`src/GLES.cpp:559-571`). Texture env is forced back to `GL_MODULATE` before
  the switch (`src/GLES.cpp:620`), so `Cfinal = Ctex * Cprimary`.
  `RENDER_ADD50` → `glColor4f(0.50f, 0.50f, 0.50f, 1.0f)` (`src/GLES.cpp:660-665`).
* No depth buffer at all: `glDisable(GL_DEPTH_TEST)` in `gles::SetGLState`
  (`src/GLES.cpp:89`) and in `Main.cpp:116`; there is no `glDepthMask`/
  `glDepthFunc`/`glEnable(GL_DEPTH_TEST)` anywhere in `src/`. Ordering is 100 %
  painter's algorithm.
* Software path: span-function tables per mode (`src/Render.cpp:50-110`) plus
  palette pre-scaling per mode (`src/Render.cpp:1981-2021`). `ADD/ADD75/ADD50/
  ADD25` share one additive span (`src/Render.cpp:66-73`) and differ only by the
  palette scale — mode 5 uses `(c & 0xE79C) >> 1` (`src/Render.cpp:2002-2006`),
  i.e. exactly "half the texel", confirming the GL `glColor4f(0.5,…)` semantics.
* Assignment sites: `Render::postProcessSprites` (`src/Render.cpp:2472-2494`),
  pool resets (`src/Render.cpp:2505,2519`), `Game.cpp:392,1315,1346,1350,1361`,
  `Entity.cpp:76`, `Combat.cpp:1164,1384,1415,1683`.
* Torchiere glow: `src/Render.cpp:1642-1647` — tile 136 emits an extra tile 193
  (`TILENUM_SFX_LIGHTGLOW1`) sprite at `z + ((10*scaleFactor)/65536)<<4` with a
  hard-coded `Render::RENDER_ADD50`, drawn *before* the body
  (`src/Render.cpp:1728`).
* Tile 240 `TILENUM_WATER_STREAM`: `src/Render.cpp:1548-1551` routes to
  `Render::renderStreamSprite` (`src/Render.cpp:1412-1495`), a stretched quad
  between two points with T scrolled by `app->time * 3 & 0x3FF`
  (`src/Render.cpp:1486`) and 3× vertical tiling (`src/Render.cpp:1473`).
* Draw order: back-to-front node walk (`src/Render.cpp:1751-1760`) plus a
  per-node insertion sort of sprites by view-space depth with per-tile biases
  (`src/Render.cpp:837-892`). Sort key never looks at `S_RENDERMODE`.

Curated result: `docs/original-code/rendering.md` §8.

## Open questions

* `RENDER_UNK = 8` hits `default: assert(0)` in `gles::SetupTexture`
  (`src/GLES.cpp:702-705`); no call site was found that can produce 8, 11, 12 or
  13 for a 3D sprite — 12/13 are only reachable through `Image::setRenderMode`
  (`src/Image.cpp:195-204`).
* `RENDER_NONE` (10) does not call `glColor4f` (`src/GLES.cpp:684-688`), so it
  inherits whatever color the previous mode left. Harmless because the blend
  func is `(GL_ZERO, GL_ONE)` (draw nothing), but it is a real state leak.
* `fogMode == 1` (`fogBlack`) is read (`src/GLES.cpp:711`) but never written —
  dead branch.
