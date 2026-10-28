# ADR 0029 — The renderer reports what it saw (`ViewVisibility`); the simulation consumes it on the next tick

Date: 2026-09-22
Status: accepted; decision 1 amended by ADR 0031 (drawnSprites/tiles now follow the legacy leaf cull)
Spec: `specs/2026-09-22-monster-attacks.md` groups G5-G6

## Context

Two monster rules in the original depend on the picture being drawn:

* **Sight wake.** While `Render::renderSpriteObject` draws a monster sprite it traces from the
  view to the sprite (mask 5293) and, on a hit, calls `Game::activate(e, true, true, true,
  false)` — i.e. monsters wake when they are *drawn* and within 4 tiles
  (`src/Render.cpp:1576-1583`; disabled in `ST_CAMERA`, `src/Canvas.cpp:1211`, `:1038`).
* **Snap vs animate.** A monster step lerp is animated only if its bounding box is not culled
  (`!cullBoundingBox(...)` → `info |= 0x10000000`, `src/Entity.cpp:1095-1097`); off-screen
  steps are snapped by the input gate (`src/Game.cpp:2374-2450`).

The rewrite's renderer already recomputes BSP leaf membership and the visible-leaf list every
frame (`new_src/render/World3D.cpp:1151-1190`), but nothing flows back to `domain/`. The domain
must not include `render/`, and ADR 0012 moved state mutation out of draw paths.

## Decision

1. A plain data struct `domain/game/ViewVisibility.h` — `valid`, `cinematic`, the list of sprite
   indices submitted to `drawSprite` by the BSP pass, and a 32×32 tile bitset of tiles
   overlapped by the bounds of the visible BSP leaves.
2. `World3D::drawBSP` fills it (no other behaviour change); `SceneRenderer` exposes it;
   `GameContext::render` copies it into `Game::visibility` right after the world pass. That copy
   is the **only** write.
3. The simulation reads `Game::visibility` on the next tick: `MonsterSystem::renderActivate`
   (sight wake, skipped for cinematic frames) at the head of the `tickPlaying` monster block,
   and `MonsterAI` for the on-screen test of a step (either end tile visible).

## Consequences

* One frame of latency versus the original (which mutates during the draw). `GameLoop` runs
  exactly one tick per rendered frame, so this is one 15 ms quantum.
* The on-screen step test is "a visible BSP leaf overlaps the source or the goal tile" rather
  than a frustum-vs-bbox cull. It errs toward *visible* (leaf bounds are coarse), i.e. a step
  may animate that the original would have snapped — never the reverse, so monsters do not
  pop into view.
* The sight-wake trace is the rewrite's 2D trace; the legacy call passes a Z range and
  `zCheck = true` (`src/Game.cpp:277-283`), which the rewrite's trace does not implement.

## Rejected alternatives

* **Activate inside the draw call** (legacy shape): domain mutation from `render/`,
  reintroducing what ADR 0012 removed.
* **Compute visibility in the domain** (own frustum test from the player angle): a second,
  diverging notion of "visible" — the BSP walk is the only one that knows what was drawn.
