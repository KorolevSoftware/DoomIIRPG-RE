# ADR 0031 — Visibility bookkeeping runs a CPU port of TinyGL's column-occlusion cull; the GPU draw list is untouched

Date: 2026-10-28
Status: accepted (amends ADR 0029 decision 1: meaning of `drawnSprites` and of the tile bitset)
Spec: `specs/2026-09-22-monster-attacks.md` §6.5 (fixes deviation D12)

## Context

`ViewVisibility` (ADR 0029) is filled from the rewrite's BSP walk, which visits every node
(`new_src/render/World3D.cpp` `walkNode`, no cull). Legacy culls every node with
`Render::cullBoundingBox` (`src/Render.cpp:1053-1055`, `:750-823`): the box's silhouette edge is
transformed by `mvp2D`, clipped, projected, and tested against `TinyGL::columnScale`, a
per-screen-column nearest-occluder depth (`src/TinyGL.cpp:689-717`). That buffer is reset each
frame (`src/Render.cpp:2315-2317`) and written in front-to-back walk order by the visited
leaves' solid map lines (`drawNodeLines`, types 0/4, `src/Render.cpp:717-744`) and closed
entity wall-sprites (`occludeSpriteLine`, `src/Render.cpp:666-715`, called from
`:1063-1080`). It is pure 2D integer math: no pixels, no GPU state. The same cull answers
`checkTileVisibilty` (`src/Render.cpp:2453-2457`, used by `src/Game.cpp:928`, `:2952`) and
the monster step test (`src/Entity.cpp:1095`).

Without it every sprite is "drawn" and every tile is "visible": monsters behind the player
wake by sight, and the G6 on-screen step test is always true.

## Decision

1. New `render/ColumnOcclusion.h/.cpp`: a verbatim integer port of the 2D half of TinyGL used
   by the cull (`transform2DVerts`, `clipLine`, `projectVerts` x/z only, `occludeClippedLine`,
   `clippedLineVisCheck` with `b = true`) plus `Render::cullBoundingBox`, over a 480-column
   buffer at the legacy viewport constants.
2. `Camera3D::setView` additionally builds the legacy `mvp2D` (`src/TinyGL.cpp:189`, `:195-200`).
3. `World3D::walkNode` carries a `legacyVis` flag: it still recurses into every node and fills
   `nodeIdxs_` / `splitPairs_` exactly as now (the GPU draw list does not change, golden frames
   stay byte-identical), and in parallel reproduces the legacy visited set, the legacy
   occluder writes and the legacy split rescue in separate lists.
4. `ViewVisibility::drawnSprites` = sprites of the legacy-visible leaves (+ legacy split
   rescue); `ViewVisibility::tiles` bit = `checkTileVisibilty(tx, ty)` evaluated with the
   frame's final column buffer.

## Consequences

* Sight wake and tile visibility follow the original's cull exactly, with ADR 0029's one-frame
  latency.
* About 150 lines of integer code; per frame, at most 1024 extra tile culls (most reject in
  `clipLine` or exit on the first visible column).
* Within one leaf, `drawnSprites` is in ascending sprite index, not legacy's depth order. That
  only matters when two monsters of the same leaf wake in the same frame and the first one's
  activation starts an animating effect (the loop then stops). Recorded as D12 residual.
* The draw path keeps visiting culled leaves; the visibility path is not used to cull drawing.

## Rejected alternatives

* **Frustum-only cull** (box silhouette vs the screen, no columns). For sight wake it is almost
  enough, because `renderActivate` also needs a clear 2D trace to the sprite (mask 5293), and
  that trace already rejects most occluded monsters. For the tile bits it is wrong: every tile
  in the view cone counts as visible, including rooms behind solid walls. So
  `checkTileVisibilty`-driven behaviour (step snap vs animate, `src/Entity.cpp:1095`,
  `src/Game.cpp:928`, `:2952`) would animate steps the original snaps. The occlusion buffer
  adds about 60 lines, so the saving is not worth the error.
* **Cull the draw walk too** (port the cull into the draw path): changes what the GPU draws,
  and golden frames would no longer be byte-identical. That needs its own visual review.
* **Read occlusion back from the GPU depth buffer**: the rewrite has no depth buffer, and the
  frame is a recorded command list (ADR 0025).
