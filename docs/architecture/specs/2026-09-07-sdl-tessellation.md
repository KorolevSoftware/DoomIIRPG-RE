# Spec — SDL path: adaptive triangle tessellation against the affine warp (G7.1-G7.3)

Date: 2026-09-07
Status: HISTORICAL as of 2026-09-11 — implemented (G7.1) and then deleted together with
the SDL_Render backend, see ADR 0024 and `specs/2026-09-11-sokol-gfx-backend.md` §7.
_Was: ready to implement._
Delta on: `specs/2026-09-02-render-backend-split.md` §G7 ("whichever specific artifacts the
user names in G6 — candidate: quad subdivision against the affine warping")
ADRs: **0023** (this work), 0021 (affine mapping accepted), 0022 (per-vertex fog), 0020
(two interfaces), 0009 (world band `1,7,478,248`)
Scope: `new_src/render/sdl/SdlScene3D.{h,cpp}` (G7.1, G7.2) and
`new_src/core/GameLoop.cpp` (G7.3, measurement only). **No other file changes.**

---

## 1. Goal and the one-line idea

`SDL_Vertex` has no `w`, so `SDL_RenderGeometry` interpolates texture coordinates
affinely: the pattern bends and kinks along the diagonal of every quad seen at a steep
angle (ADR 0021, deviation D1). The user accepted the artifact and asked for it to be
*reduced*.

The affine error over a triangle edge falls as **1/n²** when the edge is cut into `n`
object-space pieces (derivation in ADR 0023 §Decision). So: measure the error in canvas
pixels, cut the triangle into `n x n` pieces with `n = ceil(sqrt(error / tolerance))`,
and let everything whose error is already below the tolerance through untouched.

Two properties make this cheap and exact, and they are the whole design:

1. **The measurement needs the projection, the cut needs object space — but a cut in
   clip space is provably the same cut.** `MVP` is a linear map on homogeneous vectors,
   so the clip-space image of an object-space midpoint is exactly the *average* of the
   two clip-space endpoint vectors (`x, y, w` and the eye depth alike); `u, v` are affine
   in object space, so their average is the exact UV of that midpoint. Therefore the
   triangle is transformed **once**, measured after projection, and subdivided by plain
   averaging of already-transformed vertices. No second transform, no double projection,
   no object-space bookkeeping. (ADR 0023.)
2. **Projection maps straight lines to straight lines.** A subdivided edge's interior
   vertices land *exactly* on the screen segment of the un-subdivided edge. Two adjacent
   triangles that pick different `n` therefore share a T-junction with **no crack**: only
   the texture parametrization differs, never the silhouette. This is what allows a
   per-triangle `n` with no neighbour bookkeeping.

Consequence for verification: subdividing may **not** move a single polygon boundary
pixel. Any change on a silhouette is a bug, not an improvement (§6).

## 2. Place in the pipeline (and why after the tile split)

Current `SdlScene3D::submitTriangles` (`new_src/render/sdl/SdlScene3D.cpp:338-350`):

```
submitTriangles -> [splitTiles: object space, per integer u/v cell] -> projectTriangle
projectTriangle -> MVP transform -> near clip (w > kNearW) -> fan -> appendVertex
```

New pipeline (the added stage is marked `+`):

```
submitTriangles -> [splitTiles: unchanged] -> projectTriangle
projectTriangle -> MVP transform -> near clip -> fan
              +  -> emitClipTriangle: warpMetric -> subdivisionSteps -> emitSubdivided
                 -> appendVertex (unchanged projection + fog + haze)
```

**Order: the tile split stays first, tessellation runs strictly after it, in clip space.**
Reasons, in order of weight:

* *Numbers.* The tile split is already a subdivision, and the criterion evaluated on its
  output sees pieces that are `1/cells` of the original on screen, so it asks for far
  fewer extra cuts. Worked example, a floor polygon spanning 4x4 texture cells with its
  near edge 32 world units away: tiles-first gives 16 cells x 2 triangles, of which only
  the ~4 nearest cells exceed the tolerance (`n = 3..5`) -> `32 - 8 + 8*~16 ≈ 152`
  triangles. Tessellation-first measures the *whole* polygon (error 16x larger ->
  `n = 8`, the cap) -> 128 sub-triangles, each of which still spans texture cells and
  must go through `splitTiles` again -> ~4 cells each -> ~500+ triangles for a strictly
  worse picture (the far half of the polygon got cut for nothing).
* *Adaptivity is only available in this order.* The far cells of a long corridor wall
  land below the tolerance and cost nothing; the near cells of the same wall subdivide.
  With tessellation first the whole wall shares one `n`.
* *Exactness and code size.* After `splitTiles` every UV is already tile-local in
  `[0,1]`. Averages of values in `[0,1]` stay in `[0,1]`, so the new stage cannot
  produce an out-of-range UV and cannot trip SDL2's "reject the whole
  `SDL_RenderGeometry` call" rule; `clampUv` (`SdlScene3D.cpp:31-35`) stays the no-op
  safety net it is today. `splitTiles` and the `kMaxTileCells` derivation are **not
  touched**.
* Tessellation-first would also have to run in *object* space (the tile split is object
  space), which throws away property 1 of §1 and doubles the transform work.

**After the near clip, not before.** The criterion divides by `w`; before the clip a
vertex may sit behind the eye (`w <= kNearW`), where the projected position is
meaningless or infinite. Measuring the clipped triangle is well defined (`w >= kNearW`
for every vertex by construction), needs no special case, and never spends sub-triangles
on the part that was clipped away. The clip is linear in clip space, so the clipped
polygon's vertices are exact images of object-space points and property 1 still holds
for them.

**Untouched paths:** `drawSky` / `emitScreenTriangle` (screen-space vertices, no
projection, nothing to correct — ADR 0021 step 4 note), `SdlDraw2D`, and the whole GL
device. Perspective correction is free in hardware on the GL path, so subdividing there
would be pure overhead: the interface says *what* to draw, the implementation decides
*how* — the same split of responsibility the tile split already uses (GL gets it free
from `GL_REPEAT`, SDL does it in geometry).

**Billboards need no special case.** Sprites, monsters and items are camera-facing, so
all their vertices share one `w`; the metric evaluates to 0 and `n = 1`. The criterion
selects the geometry that needs it by itself.

## 3. The criterion (exact formulas)

Notation: a clipped, projected triangle with vertices `(x_i, y_i, w_i)` in clip space,
`w_i >= kNearW`; viewport size `vpW_, vpH_` (canvas pixels of the current band, ADR
0009: 478 x 248 for the world).

**Screen positions** (only differences are used, so the `+0.5` offset and the y flip of
`appendVertex` cancel and are omitted):

```
sx_i = (x_i / w_i) * 0.5f * vpW_
sy_i = (y_i / w_i) * 0.5f * vpH_
```

**Per edge (i, j)** of the three edges:

```
L_ij = max(|sx_i - sx_j|, |sy_i - sy_j|)          // Chebyshev length, no sqrt
e_ij = |w_i - w_j| / (w_i + w_j)                  // 2x the parametric error
M_ij = 0.5f * L_ij * e_ij                         // peak displacement, canvas pixels
```

**Triangle metric** `M = max(M_01, M_12, M_20)`.

`M` is the peak displacement, in canvas pixels along the edge, between where the affine
interpolation puts a texture feature and where perspective would put it. Derivation
(ADR 0023): with screen fraction `s`, the perspective-correct object parameter is
`t(s) = s*w0 / (s*w0 + (1-s)*w1)`; affine uses `t = s`; `|t - s|` peaks at `s = 1/2`
with the value `|w1 - w0| / (2*(w0 + w1))`, and multiplying by the edge's screen length
converts the parametric error into pixels of pattern displacement.

**Step count:**

```
kWarpTolerancePx = 2.0f      // accepted residual displacement, canvas pixels
n = (M <= kWarpTolerancePx) ? 1
                            : min(maxSubdivN_, (int)ceilf(sqrtf(M / kWarpTolerancePx)))
```

`n = 1` means "emit as today". `maxSubdivN_` defaults to `kMaxSubdivN = 8` (64
sub-triangles, the hard cap) and is overridable at runtime for A/B measurement (§5).

Constants, and why:

* `kWarpTolerancePx = 2.0f` — a 2-canvas-pixel displacement at 480x320 is roughly 4-6
  device pixels in a typical window, i.e. right at the edge of visibility for a straight
  grout line. It is a **tuning knob for the user's eye**, not a derived number. Halving
  it doubles `n` on the affected triangles, i.e. costs ~4x their triangle count.
* `kMaxSubdivN = 8` — 64 sub-triangles. `n = 8` covers `M` up to `64 * tol = 128 px`.
  Above that the triangle straddles the eye plane; its error is concentrated in the few
  rows just in front of the camera and a *uniform* split cannot fix it anyway (§7), so
  spending 4x more triangles there buys nothing.

## 4. G7.1 — implementation (`SdlScene3D.h` / `SdlScene3D.cpp`)

### 4.1 Header additions (`new_src/render/sdl/SdlScene3D.h`)

Next to `kMaxTileCells` / `kNearW`:

```cpp
	// Accepted residual affine texture displacement, in canvas pixels: a
	// triangle whose peak displacement is below this is emitted whole
	// (spec 2026-09-07-sdl-tessellation §3, ADR 0023). Tuned by eye, not
	// derived; halving it costs ~4x the triangles on the affected geometry.
	static constexpr float kWarpTolerancePx = 2.f;
	// Hard cap on the n of the n x n split: 64 sub-triangles. n = 8 clears a
	// metric of 128 px; beyond that the triangle straddles the eye plane and a
	// uniform split cannot help (spec §7).
	static constexpr int kMaxSubdivN = 8;
```

Private methods (after `projectTriangle`):

```cpp
	// Peak affine texture displacement over the three edges of an already
	// transformed, near-clipped triangle, in canvas pixels (spec §3).
	float warpMetric(const ClipVertex tri[3]) const;
	// n for the n x n barycentric split: 1 when the triangle is already inside
	// the tolerance, else ceil(sqrt(M / kWarpTolerancePx)) clamped to
	// maxSubdivN_.
	int subdivisionSteps(const ClipVertex tri[3]) const;
	// One near-clipped triangle: measures, subdivides in clip space when it
	// pays, emits through appendVertex.
	void emitClipTriangle(const ClipVertex tri[3]);
	// Regular n x n barycentric split of a clip triangle (n >= 2). Splitting in
	// clip space is exactly the object-space split, because MVP is linear in
	// homogeneous coordinates (ADR 0023).
	void emitSubdivided(const ClipVertex tri[3], int n);
```

Private data:

```cpp
	// Runtime override of kMaxSubdivN, from DOOM2RPG_SDL_TESS (spec §5).
	// 1 disables tessellation entirely.
	int maxSubdivN_ = kMaxSubdivN;
```

### 4.2 `SdlScene3D.cpp`

**Interpolation helper.** `ClipVertex` is a private nested type, so the clip-space
counterpart of `lerpWorld` (`SdlScene3D.cpp:37-45`) cannot live in the anonymous
namespace. Declare it in the header, with the other private methods, as
`static ClipVertex lerpClip(const ClipVertex& a, const ClipVertex& b, float t);`:

```cpp
SdlScene3D::ClipVertex SdlScene3D::lerpClip(const ClipVertex& a, const ClipVertex& b,
	float t) {
	ClipVertex r;
	r.x = a.x + (b.x - a.x) * t;
	r.y = a.y + (b.y - a.y) * t;
	r.w = a.w + (b.w - a.w) * t;
	r.u = a.u + (b.u - a.u) * t;
	r.v = a.v + (b.v - a.v) * t;
	r.depth = a.depth + (b.depth - a.depth) * t;
	return r;
}
```

All six components interpolate linearly — that is the exactness argument of ADR 0023,
and it is why `depth` (hence the per-vertex fog of ADR 0022) also becomes correct at the
new vertices instead of merely interpolated.

**`warpMetric`:**

```cpp
float SdlScene3D::warpMetric(const ClipVertex tri[3]) const {
	float sx[3], sy[3];
	for (int i = 0; i < 3; ++i) {
		const float w = tri[i].w > kNearW ? tri[i].w : kNearW; // clip guarantees it
		const float inv = 1.f / w;
		sx[i] = tri[i].x * inv * 0.5f * vpW_;
		sy[i] = tri[i].y * inv * 0.5f * vpH_;
	}
	float worst = 0.f;
	for (int i = 0; i < 3; ++i) {
		const int j = (i + 1) % 3;
		const float dx = std::fabs(sx[i] - sx[j]);
		const float dy = std::fabs(sy[i] - sy[j]);
		const float len = dx > dy ? dx : dy;
		const float sum = tri[i].w + tri[j].w;
		if (sum <= 0.f) continue;
		const float m = 0.5f * len * std::fabs(tri[i].w - tri[j].w) / sum;
		if (m > worst) worst = m;
	}
	return worst;
}
```

**`subdivisionSteps`:**

```cpp
int SdlScene3D::subdivisionSteps(const ClipVertex tri[3]) const {
	if (maxSubdivN_ <= 1) return 1;
	const float m = warpMetric(tri);
	if (m <= kWarpTolerancePx) return 1;
	const int n = (int)std::ceil(std::sqrt(m / kWarpTolerancePx));
	if (n >= maxSubdivN_) return maxSubdivN_;
	return n < 2 ? 2 : n;
}
```

**`emitClipTriangle`:**

```cpp
void SdlScene3D::emitClipTriangle(const ClipVertex tri[3]) {
	const int n = subdivisionSteps(tri);
	if (n <= 1) {
		reserveFor(3);
		appendVertex(tri[0]);
		appendVertex(tri[1]);
		appendVertex(tri[2]);
		return;
	}
	emitSubdivided(tri, n);
}
```

**`emitSubdivided`** — regular barycentric grid, row by row, two `ClipVertex` rows on
the stack (`kMaxSubdivN + 1` entries each). Row `i` (`i = 0..n`) holds `i + 1` vertices
between `A + (B-A)*i/n` and `A + (C-A)*i/n`:

```cpp
void SdlScene3D::emitSubdivided(const ClipVertex tri[3], int n) {
	ClipVertex rowA[kMaxSubdivN + 1];
	ClipVertex rowB[kMaxSubdivN + 1];
	const float invN = 1.f / (float)n;
	rowA[0] = tri[0];                                  // row 0 = the apex
	for (int i = 0; i < n; ++i) {
		const float t = (float)(i + 1) * invN;
		const ClipVertex left = lerpClip(tri[0], tri[1], t);
		const ClipVertex right = lerpClip(tri[0], tri[2], t);
		const int count = i + 2;                        // vertices in row i+1
		for (int k = 0; k < count; ++k) {
			rowB[k] = lerpClip(left, right, (float)k / (float)(count - 1));
		}
		// Up-triangles: (rowA[k], rowB[k], rowB[k+1]) keep the input winding.
		for (int k = 0; k <= i; ++k) {
			reserveFor(3);
			appendVertex(rowA[k]);
			appendVertex(rowB[k]);
			appendVertex(rowB[k + 1]);
		}
		// Down-triangles: (rowA[k], rowB[k+1], rowA[k+1]).
		for (int k = 0; k < i; ++k) {
			reserveFor(3);
			appendVertex(rowA[k]);
			appendVertex(rowB[k + 1]);
			appendVertex(rowA[k + 1]);
		}
		for (int k = 0; k < count; ++k) rowA[k] = rowB[k];
	}
}
```

Total emitted triangles: `sum over i of (2i + 1) = n²`. Winding matches the input
(`SDL_RenderGeometry` does no culling, but consistency keeps captures comparable).
`reserveFor(3)` is called **per leaf, before its three vertices** — `reserveFor` may
flush, and a flush is only safe between complete triangles (it clears both `vertices_`
and `haze_`); a mid-batch flush is invisible because the whole run shares one texture
and one render mode, exactly as today.

**`projectTriangle`** (`SdlScene3D.cpp:215-259`): replace the tail

```cpp
	reserveFor((n - 2) * 3);
	for (int i = 1; i + 1 < n; ++i) {
		appendVertex(out[0]);
		appendVertex(out[i]);
		appendVertex(out[i + 1]);
	}
```

with a fan through the new stage:

```cpp
	for (int i = 1; i + 1 < n; ++i) {
		const ClipVertex fan[3] = { out[0], out[i], out[i + 1] };
		emitClipTriangle(fan);
	}
```

Also raise `vertices_.reserve(1024)` (`SdlScene3D.cpp:74`) to `8192` and add
`haze_.reserve(8192)` — the leaf count per frame grows by ~1k triangles (§5) and the
vector should not re-grow every frame.

`#include <cmath>` is already there (`SdlScene3D.cpp:3`).

**Env knob** in `initialize`, after the members are set:

```cpp
	// Debug/measurement knob (spec §5): DOOM2RPG_SDL_TESS=1 disables the
	// affine-warp tessellation, 2..8 caps it lower than kMaxSubdivN.
	if (const char* env = std::getenv("DOOM2RPG_SDL_TESS")) { ... clamp to [1, kMaxSubdivN] ... }
```

with a one-line `stderr` log of the value in effect (`#include <cstdlib>`).

### 4.3 What G7.1 must NOT change

* `splitTiles`, `kMaxTileCells` and their derivation (see §7 point 4).
* `clampUv`, `appendVertex`, `fogFactor`, `flush`, batching, `drawSky`,
  `emitScreenTriangle`.
* Any file outside `new_src/render/sdl/SdlScene3D.{h,cpp}`. No CMake change (no new
  files, so no reconfigure needed; `cmake --build build_new -j 8` is enough).

## 5. G7.2 — counters, and how to price this honestly

The existing "median 63 FPS on both backends" number **cannot** price this work: the
loop paces itself to ~66 fps (`SDL_Delay(kTickMs - spent)`, `new_src/core/GameLoop.cpp:105-110`)
and the renderer is created with `SDL_RENDERER_PRESENTVSYNC`
(`new_src/platform/Window.cpp:80`). Both backends are sitting in `SDL_Delay` / vsync, so
63 FPS measures the monitor, not the pipeline. Measure **time and triangles**, not FPS.

**G7.2 — inside `SdlScene3D` (same file pair), gated on `DOOM2RPG_GFX_STATS=1`:**

```cpp
	struct Stats {
		int submitted;   // triangles handed to submitTriangles
		int tilePieces;  // triangles out of splitTiles (1 when it declines)
		int leaves;      // triangles actually emitted (post-tessellation)
		int vertices;    // SDL_Vertex written, incl. haze twins
		int geomCalls;   // SDL_RenderGeometry calls
		double cpuMs;    // time in submitTriangles + drawSky, EXCLUDING flush
		double drawMs;   // time inside flush (the SDL calls themselves)
	};
```

`beginScene` zeroes the per-frame block; `endScene` accumulates it into a 1-second
window and prints one line per second (`SDL_GetTicks`), reporting for the window: frame
count, mean and p95 of `cpuMs` and `drawMs`, and mean/max of the four counters. Timing
via `SDL_GetPerformanceCounter` / `SDL_GetPerformanceFrequency`. The split matters:
**`cpuMs` is the price of tessellation**; `drawMs` is what the extra triangles cost SDL.

Procedure (the honest measurement):

1. Park the camera at a fixed pose in map00 and confirm it with the coordinate overlay
   (`B`, `GameLoop.cpp:50`). Two poses: **worst** — standing in an open room, looking
   slightly down so the near floor fills the lower band; **typical** — a corridor.
2. `DOOM2RPG_GFX_STATS=1 DOOM2RPG_SDL_TESS=1 ./DoomIIRPG --backend=sdl` -> baseline
   (tessellation off), record the stats line.
3. Same with `DOOM2RPG_SDL_TESS=8` -> record. The interesting deltas: `leaves`,
   `cpuMs`, `drawMs`.
4. Decision rule: if `cpuMs + drawMs` grows by less than ~2 ms at the worst pose, ship
   `kWarpTolerancePx = 2` unchanged. Between 2 and 5 ms, raise the tolerance to 3-4 px.
   Above 5 ms, lower `kMaxSubdivN` to 4 first (the eye-straddling triangles are the bulk
   of the cost) and re-measure.

**G7.3 (optional, only if the above is inconclusive) — uncapped frame time**, in
`new_src/core/GameLoop.cpp`: gate on `DOOM2RPG_BENCH=1` (read once before the loop),
and when set (a) call `context.window().setVSync(false)` once before the loop and
(b) skip the `SDL_Delay` pacing at `:109-110`. Then the printed FPS line becomes a real
throughput number for both backends. This changes the simulation cadence (one tick per
uncapped frame, i.e. the game runs fast) — it is a measurement mode, it must be
off by default, and the log must say `BENCH: vsync off, pacing off` so nobody reports a
bug from it.

## 6. Acceptance criteria

### 6.1 What the user should see (the user is the eyes)

Compare three captures of the **same pose** (coordinate overlay confirms x/y/yaw):
`A` = `--backend=sdl DOOM2RPG_SDL_TESS=1`, `B` = `--backend=sdl` (default 8),
`C` = `--backend=gl`.

Where to look, in order:

1. **The floor 1-2 tiles in front of the feet, and the ceiling above it.** In `A` the
   grout/panel lines bow into an S and *kink* where the quad's diagonal runs; in `B`
   they must be visibly straighter and the diagonal crease must be gone or reduced to a
   faint break. This is the primary criterion.
2. **A side wall of a corridor passing the edge of the view.** In `A` the texture
   compresses unevenly along the wall and its horizontal lines kink at every triangle
   diagonal; in `B` the nearest 1-2 tiles of that wall should read like `C`.
3. **Beyond ~2 tiles of depth: nothing may change at all.** `A` and `B` must be
   identical there (the metric is below tolerance -> `n = 1`). If distant walls changed,
   something else changed too.
4. **Sprites, monsters, items, the view weapon, the sky band, all 2D/HUD: byte-identical
   between `A` and `B`.** Billboards are constant-depth (`M = 0`) and the sky is drawn in
   screen space.
5. **Fog on large floors** (where fog is on) should look smoother in `B` — a side effect
   of ADR 0022 getting more vertices, not a goal.

"Better" vs "merely different" — the distinguishing test: **no polygon boundary may
move.** Subdivision cannot move an edge (projection maps lines to lines), so the
`A`/`B` difference must be confined to *texture interiors*. Report immediately if any of
these appears — each is a bug, not a tuning matter:

* a **hairline crack or dotted seam** along a subdivision boundary (would mean the
  rasterizer is not closing a T-junction — report; the fix is a uniform `n` per polygon,
  not a smaller tolerance);
* a **grid of thin light/dark lines** on floors following the new sub-triangles (would
  mean a UV or clamp bug: sub-triangle UVs must be exact averages and stay inside the
  tile);
* a silhouette shift, a polygon disappearing, geometry flickering with camera motion,
  or a sprite changing size/position.

### 6.2 What the orchestrator checks

* `cmake --build build_new -j 8` clean; `--backend=gl` output unchanged (the GL device is
  untouched — a byte-identical capture is expected).
* BMP diff `A` vs `B`: differing pixels form **interior** patches on near floors/walls;
  the 2D areas, the sky band and every sprite bounding box diff to 0.
* BMP diff `B` vs `C` (GL) has a strictly smaller mean per-channel delta over the world
  band than `A` vs `C`. This is the numeric statement of "the warp got smaller".
* Stats line (§5) at the worst pose: `leaves / submitted` ratio and the `cpuMs` delta
  recorded in `docs/journal.md`, since the estimate in §7 is an estimate.

## 7. Numbers: how many triangles this actually adds

Projection scale, from the live camera setup: `fov = 315` of 1024,
`aspect = (fov << 14) / ((478 << 14) / 248) = 163` (`new_src/render/SceneRenderer.cpp:87-89`),
`projection[5] = cot(163/2 * 360/1024 deg) = cot(28.5 deg) = 1.845`
(`new_src/render/Camera3D.cpp:45-62`), and NDC maps onto a 248-pixel-high band, so a
world span of `T` units at depth `d` covers **`229 * T / d` canvas pixels**
(`1.845 * 124 = 228.8`; the horizontal axis matches — `projection[0] * 239 = 228`).

One texture cell of a floor is 64 world units (1 tile, `docs/original-code/doors.md:8`).
For a floor cell whose near/far edges sit at depths `d1`, `d2 = d1 + 64`, with eye height
`h ≈ 32`: `L = 229 * h * (1/d1 - 1/d2)`, `e = |w1 - w0| / (w1 + w0) = 64 / (d1 + d2)`,
`M = 0.5 * L * e` — the same three lines as §3:

| `d1` (units) | `L` (px) | `e` | `M` (px) | `n` | leaves / triangle |
|---|---|---|---|---|---|
| ~0 (cell under the feet, clipped) | huge | ->1.0 | >128 | 8 (cap) | 64 |
| 32 | 153 | 0.500 | 38.2 | 5 | 25 |
| 64 | 57 | 0.333 | 9.5 | 3 | 9 |
| 128 | 19 | 0.200 | 1.9 | **1** | 1 |
| 192 | 9.5 | 0.143 | 0.7 | **1** | 1 |

**The tolerance switches tessellation off past ~128 world units = 2 tiles.** A frontal
wall has `e = 0` on both its vertical and horizontal edges -> `n = 1` at any distance; a
side wall behaves like the floor table above.

Per-frame estimate for map00: within 2 tiles of the eye a frame sees on the order of
30 tile cells (floor + ceiling + the near cells of the side walls) = ~60 triangles, at an
average ~12 leaves each -> **~700 leaves in place of 60**, plus the 2-6 eye-straddling
floor/ceiling triangles at the cap -> **+150..400**. Total **added ≈ +600..1100 triangles
per frame**, concentrated in one or two batches, with **no extra draw calls and no extra
state changes**.

For scale: the whole map is 1787 faces -> 4123 fan triangles (a frame draws a subset of
that, further multiplied by the tile split), and the blind 4x4 subdivision the user
priced would put the map at ~66k. This design pays ~1k triangles a frame instead of a
16x blanket multiplier, and it pays them exactly where the artifact is.

**`kMaxTileCells = 289` is unaffected, and the two limits do not compound.** The cell cap
is a statement about the object-space UV range of a *submitted* triangle
(`SdlScene3D.h:29-45`); tessellation runs downstream, in clip space, and never touches
`u`/`v` ranges (averages of `[0,1]` values stay in `[0,1]`) nor the submitted geometry.
The naive worst case `289 cells x 5 tri x 64 = 92k` is unreachable *by construction*, not
by luck: the criterion is evaluated **per cell piece**, and a triangle spanning 289 cells
has each piece at ~1/289 of its screen area, so every piece measures far below the
tolerance and takes `n = 1`. The tile split feeds the criterion, it does not multiply
against it. The only new limit needed is `kMaxSubdivN` on the metric itself, and no
recursion-depth limit exists because there is no recursion.

## 8. Explicitly NOT done

* **No subdivision on the GL path.** Hardware does perspective correction for free;
  subdividing there is cost without benefit.
* **No perspective-correct rasterization.** ADR 0021 stands: `SDL_Vertex` has no `w`.
  This work reduces the artifact; it does not remove it.
* **No depth-adaptive (non-uniform) split.** On a triangle that straddles the eye plane
  the error is concentrated in the few screen rows nearest the camera, and a uniform
  `n x n` grid over-splits the far part while still under-splitting the near part.
  Residual warp at the very bottom of the world band, in the cell under the player's
  feet, is **expected and accepted**. A recursive per-child re-measurement would fix it
  and is the natural follow-up if the user still sees it.
* **No viewport clipping of world triangles** (side planes) and no screen-space bounding
  box reject. A triangle whose visible part is small but whose projected extent is huge
  still gets the cap's 64 sub-triangles, most of them off screen. Cheap to add later; not
  needed to hit the budget in §7.
* **No neighbour-aware / edge-shared tessellation factors.** Unnecessary: T-junctions are
  crack-free here (§1 property 2).
* **No change to the tile split, the cell cap, batching, fog, the sky, or any interface
  in `render/api/`.** `Scene3D` keeps saying *what* to draw.
* **No new files, no CMake change, no ADR-0022 rework** (per-vertex fog stays per-vertex;
  it merely gets more vertices).
* `--gfx-stats` as a *command-line* flag is dropped in favour of `DOOM2RPG_GFX_STATS`,
  following the `DOOM2RPG_BACKEND` precedent (`new_src/core/Main.cpp:50`): it keeps the
  debug knob out of the argument parser and out of the neutral `RenderBackend` interface.

## 9. Implementation groups (one delegation each)

* **G7.1 — the tessellation.** `new_src/render/sdl/SdlScene3D.{h,cpp}` per §4:
  `kWarpTolerancePx`, `kMaxSubdivN`, `maxSubdivN_` + the `DOOM2RPG_SDL_TESS` knob,
  `lerpClip`, `warpMetric`, `subdivisionSteps`, `emitClipTriangle`, `emitSubdivided`,
  the `projectTriangle` tail rewired, reserves raised. Verify: builds; `--backend=sdl`
  renders the world; `DOOM2RPG_SDL_TESS=1` reproduces today's picture exactly.
* **G7.2 — the counters.** Same file pair, §5: `Stats`, the per-frame block, the
  1-second summary line gated on `DOOM2RPG_GFX_STATS=1`. Verify: the line appears once
  per second and `leaves >= tilePieces >= submitted`, with `leaves == tilePieces` when
  `DOOM2RPG_SDL_TESS=1`.
* **G7.3 — bench mode (optional, only on demand).** `new_src/core/GameLoop.cpp`, §5:
  `DOOM2RPG_BENCH=1` disables vsync and the pacing delay, logs that it did.
