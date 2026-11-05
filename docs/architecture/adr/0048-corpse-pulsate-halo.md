# ADR 0048 — Corpse pulsate halo: per-sprite halo byte from `SceneRenderer`, `Scene3D::setPulsate` device state

Date: 2026-11-05. Status: accepted. Spec: `../corpse-halo.md`.
Facts: `docs/original-code/corpse-outline.md`, `docs/research/2026-11-05-corpse-outline.md`.

## Context

The original marks a lootable corpse by drawing an enlarged (18/16; 17/16 for sentinels, Boss
Pinky, Mastermind, Arachnotron; no halo for lost souls, cacodemons, VIOS) copy of its sprite before the body, with `RENDER_FLAG_PULSATE`. The user's legacy build runs the TinyGL
software path (`isInit` is read from the config file, `src/Game.cpp:2036`). There, every
non-transparent halo texel is written opaque as flat grey `(g,g,g)`, where `g` is a 2048 ms
triangle wave in 127..254, with no blend and no fog (`src/Render.cpp:1968-1979`,
`src/Span.cpp:59-63`). The GL path (additive `(a,a,a,a)`, `src/GLES.cpp:719-733`) is what
non-default configs show. The visibility test needs entity state (`param`,
`monster->flags & 0x800`, loot set, drop-slot index) and the canvas state (`!= ST_CAMERA`).

In the rewrite, `World3D` draws by sprite index and sees no entities. `SceneRenderer` already
derives per-sprite bytes from the entity list each frame (`spriteCharClass_`,
`spriteHasEntity_`). The `Scene3D` device only knows the 14 fixed `RENDER_*` rows, whose colour
modulation is constant.

## Decision

1. **Visibility is decided in `SceneRenderer`.** It computes one byte per sprite in the
   existing classification loop: 0 means no halo, otherwise the byte is the scale numerator
   (18 or 17; 0 for lost souls, cacodemons and VIOS, `src/Render.cpp:3568-3571`, `:3619-3659`). `drawBSP` receives it as a new trailing optional parameter, exactly like
   `spriteHasEntity`. `World3D` stays entity-free. The per-tile rule (`deadHaloScale`: 17 for sentinels, Pinky,
   Mastermind, Arachnotron; 0 for lost souls, cacodemons, VIOS) sits next to the `isFloaterTile` / `isSpecialBossTile` helpers that already live in
   `SceneRenderer`. The camera-state test comes in through a new `drawWorld(..., bool cameraState)`
   argument (`state_ == StateId::Camera`).
2. **The halo is a new sticky device state, `Scene3D::setPulsate(float a)`.** It runs the
   AlphaBlend pipeline with colour modulation 1. Fog is forced to `f = 0` with fog colour
   `(g,g,g)`, so the existing shader writes the flat grey on opaque texels: a colour replace
   with no shader or uniform change. The next
   `setRenderMode` always leaves it, with a flush. `World3D` computes the grey (`pulsateGrey`) from `timeMs_` using
   the legacy integer formula (`pulsateGrey`) (constexpr + `static_assert`). There is no shader change.
3. **Two hook sites.** One is the billboard branch of `drawSprite` (props 137-139 and the
   dropped sentry-bot corpse). The other is the `kManimDead` case of `drawCharacter` (all dead
   monsters). Dead floaters and special bosses already reach `kManimDead` through the corpse
   clause of the classification, so the four legacy MANIM_DEAD call sites collapse into one,
   and no floater or boss renderer is required.
4. **Looted marker.** For monster corpses the test is `MFLAG_LOOTED == 0 && param == 0`,
   because `CorpseLoot` marks monster corpses with `++param` (its deviation #1) while save
   restore sets `MFLAG_LOOTED`.

## Consequences

- Golden frames: the cinematic frames t0200/t0400/t0500 must stay identical. t0750/t0850 were
  expected to change around the map00 corpse prop, but the post-G1 run was ALL IDENTICAL; open
  question pending the on-screen check (spec §6).
- Each visible lootable corpse adds one draw command (the halo batch) to the frame.
- `Scene3D` grows one pure virtual. Every future backend must implement it, which is a
  one-line uniform/pipeline choice.
- Imp-family corpses show no halo until the joke-item loot table is ported, because their loot
  set stays empty. This is inherited, not introduced here.

## Rejected alternatives

- **A 15th `kRenderModes` row "PULSATE".** The modulation varies with time, while the rows are
  constants. `kRenderModeCount` is also documented as `RENDER_MAX` and sizes other tables.
- **A modulation override on `setRenderMode(mode, const float* mod)`.** It changes every caller's
  signature to serve one use.
- **Passing `Game`/entities into `World3D`.** It breaks the existing render/domain split. The
  per-sprite byte array is the established channel.
- **The GL-path additive `(a,a,a,a)` halo** (the first version of this ADR). It does not match
  what the user's legacy build shows, which is the software path.
- **A new `pulsate` flag uniform plus a shader branch.** It does the same job as the forced fog,
  but needs a shader edit and a uniform-layout change on every backend.
- **Deriving the camera gate from `cinePose != nullptr`.** It is wrong for a dialog inside a
  cinematic (`StateId::Dialog`), where the original shows the halo.
