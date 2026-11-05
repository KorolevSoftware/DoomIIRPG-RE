# 0049 — Script fog: software 16-level palette fog in `fs_world`, state on `Game::fog`

Date: 2026-11-05. Spec: [`script-fog.md`](../script-fog.md).

## Context

The map00 "flickering light" is the map script snapping the depth fog with
`EV_LERP_FOG` (docs/original-code/light-flicker.md). The reference runs the TinyGL
software rasterizer, which fogs by picking one of 16 prebuilt RGB565 palettes per
span / sprite line: `level = clamp(((0x7FFFFF/(fracZ>>16) - fogMin) << 4) / fogRange, 0, 15)`
(`src/TinyGL.cpp:47-52`), palettes from `buildFogTables` (`src/Render.cpp:1823-1883`).
new_src only had the GLES linear fog (`new_src/render/World3D.cpp:253-278`), never
enabled (`new_src/core/Main.cpp:303-306`), and stubs both fog opcodes
(`new_src/domain/game/ScriptVM.cpp:1413-1423`). Golden frames must not move
outside fog scenes.

## Decision

1. State lives in a plain domain struct `FogState` as `Game::fog` (same pattern as
   `Game::particles`, ADR 0047): `color/min/range` + the lerp fields, written by the
   two opcodes, reset by the map-load sequence, ticked by `SceneRenderer` right after
   the camera is set (`src/Render.cpp:2299-2314`) and forwarded through
   `World3D::setFog` → `Scene3D::setFog(int argb, int fogMin, int fogRange)` as
   verbatim legacy ints.
2. `fs_world` computes the level per fragment with integer-exact arithmetic:
   `w = fog_depth * 16384` (our eye depth is TinyGL units / 16384), `z = 0x7FFFFF / w`,
   `d = 0x7FFFFF / z`, C-truncating division by `fogRange`, clamp 0..15; then applies
   the `buildFogTable` 565 formula to the 565 fields recovered from the
   bit-replicated LUT texel and re-expands. Level 0 / mode 0 leave the texel untouched.
3. Fog is applied before `color_mod` and for every render mode (software order:
   fog palette, then `setupPalette`, `src/Render.cpp:507`). PULSATE gets an explicit
   flat-colour mode (`fog_params.z == 2`) instead of the forced-fog trick of ADR 0048.
4. The sky (quad and tile-301 polygons) is fogged at the fixed legacy depth 2047
   (`fracZ 0x10000000`, `src/Render.cpp:2133`) via a forced-z uniform.
5. No clear-colour emulation: the rewrite always paints the sky backdrop under the world.

## Consequences

- Fog-off output is bit-identical (uniform branch). With map00's live `16384/1`
  fog, geometry beyond 16 tiles darkens exactly as in the original; goldens 200/400/500/750
  may change and are re-captured after user confirmation.
- Per-fragment vs per-span: identical for wall columns and pitch-0 floors and billboards;
  sub-span differences on pitched floors only.
- ADR 0019 point "fog after colour modulation" and the `kRenderModes[].fog` flag
  no longer apply to `SgScene3D` (they described the GLES path).
- Uniform meaning of `fog_color` / `fog_params` changes; no layout change.
- COLD buff fog, view-weapon fog, save/load of fog stay as named hooks (spec §6).

## Rejected alternatives

- **Keep the GLES linear fog** (`GLES.cpp:178-190`): continuous gradient, not the
  16-band look of the reference; formula unrelated to the software one.
- **Precompute 16 palette LUTs per texture on the CPU** (literal port of
  `buildFogTables`): 16x palette memory and a re-upload on every `SET_FOG_COLOR`;
  the shader reproduces the same integers per texel for free.
- **State on `World3D` or `Hud`**: scripts would need a render pointer in
  `ScriptVM::Env`; the domain-owned, renderer-read pattern already exists for particles.
- **Per-span level** (exact span-left-edge emulation): impossible in a fragment shader
  without screen-space span reconstruction; the visible difference is nil for walls.
