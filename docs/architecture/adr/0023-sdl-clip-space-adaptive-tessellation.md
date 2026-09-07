# ADR 0023 — The affine warp is fought by adaptive tessellation, measured in screen space and cut in clip space

Date: 2026-09-07
Status: accepted
Related: ADR 0021 (affine mapping accepted), ADR 0022 (per-vertex fog), ADR 0020 (two
interfaces); spec `specs/2026-09-07-sdl-tessellation.md`

## Context

`SDL_RenderGeometry` has no `w` per vertex, so the SDL backend interpolates texture
coordinates affinely (ADR 0021). The user accepted the artifact but asked for it to be
reduced. ADR 0021 already listed subdivision as *deferred, not rejected*: "a pure win
inside `SdlScene3D` (error falls quadratically with subdivision) but costs triangles and
complexity; do it only if the user dislikes the picture." That condition is now met
(G1-G6 shipped, commit `20f4807`, the user confirmed the warping).

The design has one genuine tension. The artifact is a **screen-space** phenomenon: its
magnitude is pixels of pattern displacement, and it must be judged after projection,
otherwise distant walls get subdivided for nothing. The remedy is an **object-space**
operation: only a cut made before the perspective divide interpolates `u, v` and the
depth correctly; cutting after the divide would reproduce the very error being fought.
So criterion and operation sit on opposite ends of the pipeline, and the naive
implementation projects the geometry twice — once to decide, once to draw.

## Decision

**1. Tessellation exists only in the SDL implementation.** GL gets perspective
correction from the hardware for free, so subdividing there is cost with zero benefit.
This is the same division of labour the tile split already uses: `Scene3D` says *what*
to draw, the implementation decides *how* (GL gets texture repeat free from
`GL_REPEAT`, SDL does it by clipping geometry against integer `u`/`v`).

**2. Cut in clip space; it is exactly the object-space cut.** `MVP` is a linear map on
homogeneous vectors, and every world vertex enters with `w_obj = 1`, so the clip-space
image of the object-space midpoint of an edge is the **arithmetic average** of the two
clip-space vertices, component by component, `w` included:
`M * ((a + b)/2) = (M*a + M*b)/2`. Eye-space depth (from the view matrix) behaves the
same way, and `u, v` are affine over the polygon in object space, so their average is the
exact UV of that midpoint. Therefore a barycentric grid built by averaging *already
transformed* vertices is bit-for-bit the same geometry as transforming the object-space
grid — up to float rounding, with none of the cost. The triangle is transformed once.

**3. Measure after the near clip.** Every vertex then satisfies `w >= kNearW`, so the
screen positions are finite and the criterion needs no special case for geometry behind
the eye; the clipped-away part is never subdivided. The near clip interpolates linearly
in clip space, so the clipped polygon's vertices are still exact images of object-space
points and decision 2 keeps holding for them.

**4. The criterion is the peak affine displacement in canvas pixels.** For an edge with
clip `w0, w1` and screen length `L`, the perspective-correct object parameter at screen
fraction `s` is `t(s) = s*w0 / (s*w0 + (1-s)*w1)`; affine interpolation uses `t = s`, and
`|t - s|` peaks at `s = 1/2` with `|w1 - w0| / (2*(w0 + w1))`. Multiplied by `L` this is
the pattern displacement in pixels: `M = L * |w1 - w0| / (2*(w0 + w1))`, maximized over
the three edges. `M` scales as `1/n²` under an `n`-way split (both factors shrink like
`1/n`), hence `n = ceil(sqrt(M / tolerance))`, capped at 8; `tolerance = 2` canvas pixels.

**5. Uniform `n x n` split per triangle, no neighbour bookkeeping.** Perspective
projection maps straight lines to straight lines, so the new vertices along an edge land
*exactly* on the screen segment of the original edge. A T-junction against a neighbour
that chose a different `n` therefore produces **no crack** — only the parametrization
differs, never the silhouette. This removes the usual reason for edge-shared
tessellation factors and makes a per-triangle decision safe.

**6. Order: after the existing tile split.** The tile split is itself a subdivision; the
criterion applied to its output sees pieces that are a fraction of the original on
screen and asks for far fewer extra cuts, and it can decide *per cell*, so the far cells
of a long wall cost nothing. The reverse order measures whole polygons (one `n` for near
and far alike) and then multiplies the tile split over every sub-triangle — more
triangles for a worse picture. It would also have to run in object space, throwing away
decision 2.

**7. Consequence used as the acceptance test:** because subdivision cannot move a
boundary pixel, a capture with tessellation on must differ from one with it off **only in
texture interiors**. Anything moving on a silhouette is a bug.

## Consequences

* Near floors, ceilings and steeply-seen wall tiles lose most of the bend and the
  diagonal crease; anything past ~128 world units (2 tiles) is untouched because it is
  already inside the tolerance. Billboards (all sprites/monsters/items) are constant
  depth, measure 0 and are bit-identical; the sky is screen space and is not in the path.
* Estimated cost ~+600..1100 triangles per frame on map00 (spec §7), in the same batches,
  with no extra draw calls — versus a 16x blanket multiplier for a blind 4x4 grid.
* Per-vertex fog (ADR 0022) gets more vertices, so banding on large floors improves for
  free; its `depth` at the new vertices is exact, not approximated.
* `kMaxTileCells = 289` and its derivation are untouched, and the two limits do not
  compound: the criterion runs *per tile cell*, so a triangle spanning many cells has
  tiny pieces that all take `n = 1`.
* The residual warp on triangles straddling the eye plane (the cell under the player's
  feet) survives: a uniform grid cannot concentrate detail where `w` is smallest. Named
  and accepted; a recursive per-child re-measurement is the follow-up if the user still
  sees it.
* Two runtime knobs (`DOOM2RPG_SDL_TESS`, `DOOM2RPG_GFX_STATS`) exist so the picture and
  the price can be A/B'd without a rebuild. The old "63 FPS on both backends" figure is
  worthless for pricing — the loop is capped by vsync plus a 15 ms pacing delay — so the
  backend times its own submit phase in microseconds instead (spec §5).

## Rejected alternatives

* **Subdivide in object space and transform the pieces.** Correct but pays the MVP
  transform for every generated vertex, and needs the screen criterion computed from a
  separate throw-away projection of the corners: two projections instead of one, for the
  identical result (decision 2).
* **Measure in object/world space** (e.g. polygon area or edge length in world units).
  Cheap, but blind to distance: it subdivides far walls that need nothing and misses the
  near-floor case, which is exactly where the artifact lives.
* **Measure the depth ratio alone** (`max w / min w`), no screen length. Over-splits
  small distant polygons with a large depth ratio (a far floor cell seen edge-on) whose
  on-screen error is a fraction of a pixel.
* **Blind uniform 4x4 subdivision of every world quad.** ~16x triangles everywhere
  (~66k for map00's faces) for a picture no better on the near floor than the adaptive
  `n = 5..8`, and worse everywhere else per triangle spent.
* **Longest-edge bisection (binary, recursive).** Same error-per-triangle as the grid but
  the level granularity is powers of two (4, 16, 64 leaves — no 9 or 25), it produces
  slivers, and it needs recursion state. The closed-form `n x n` grid is a double loop
  over two stack rows.
* **Edge-shared tessellation factors to avoid T-junctions.** Unnecessary here (decision
  5) and it would force per-edge patterns into the emitter.
* **Tessellate before the tile split.** More triangles, less adaptivity, object-space
  implementation forced (decision 6).
* **Perspective-correct software rasterization** (a TinyGL revival). Already rejected in
  ADR 0021; unchanged.
* **A `--gfx-stats` command-line flag and a `setStatsEnabled` on `RenderBackend`.**
  Rejected: it pushes a debug concern into the neutral interface. An env var read inside
  `SdlScene3D::initialize` follows the existing `DOOM2RPG_BACKEND` precedent.
