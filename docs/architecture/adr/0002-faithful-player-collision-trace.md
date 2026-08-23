# ADR 0002 — Faithful swept-capsule collision trace (walls + entityDb)

Date: 2026-08-23
Status: Accepted
Amends: ADR 0001 (door-collision granularity consequence superseded)
Related: `docs/architecture/specs/2026-08-23-faithful-player-collision.md`,
`docs/original-code/player-collision.md`, `docs/research/2026-08-23-player-collision.md`

## Context

Walls were non-solid: the rewrite's `CapsuleToLineTrace` had a sign-flipped
s/t solve plus int32 overflow (`new_src/domain/game/Game.cpp:271-272` vs
`src/Render.cpp:1151-1152`), its line-flag filter only skipped flag 6
(`:306-307` vs `src/Render.cpp:1238-1247`), and doors blocked tile-granularly
instead of as animated ±32 segments (`src/Game.cpp:249-268`). There was no
masked `entityDb` pass at all.

## Decision

1. Port the legacy collision 1:1 as `Game::traceMove` (one entry point,
   bool = clear): swept capsule radius 16, mask `CONTENTS_PLAYERSOLID`
   (13501), over (a) world lines with exact nibble/low-3-bit flag rules and
   one-sided cross test vs trace start, (b) `entityDb` candidates filtered by
   `mask & (1<<eType)` — oriented sprites as ±32 segments through the CURRENT
   animated sprite position, everything else as circles with the legacy
   sum-of-squares overlap `d² < r² + R²` (881 walking).
2. Keep the integer solve bit-faithful (int64-widened products, sequential
   clamp order, representation switch of the t-denominator, zero-guarded
   quotients, strict `<`, `-1` frac bias) instead of a textbook re-derivation.
   Research finding: the legacy "parallel projection" branch
   (`src/Render.cpp:1144-1148`) is dead code (`denom = a·e − b² ≥ 0` always);
   live parallel behavior is s=t=0 via vanishing numerators — ported as such.
3. World pass walks all lines flat (O(numLines)) with the legacy per-line
   flag rules + AABB rejects instead of BSP leaf descent; equivalent because
   leaf-bounds pruning only removes lines that fail the identical per-line
   rejects, and the result is a minimum frac.
4. `canPlayerStep` is deleted (name implies nonexistent upstream semantics);
   door solidity stays driven purely by linked-state × current sprite
   position, so the existing timeline (solid through open animation, passable
   at open end, solid at close start) is preserved unchanged.

## Consequences

* Positive: walls, doors and (future) entities all collide through one
  faithful primitive; flag-4 fake-blockers disappear; one-sided flag-7 walls
  behave directionally; door blocking tracks the sliding panel.
* Accepted deviations (all documented in the spec): flat line walk; omitted
  unreachable branches (monster goal-lerp position, Z filter, world contact
  point); `sprite < 0` candidates fall back to a `(0,0)` circle instead of
  legacy's benign out-of-bounds read; ET_PLAYER candidate position uses view
  instead of dest coords (unreachable for walking).
* Escape hatch from ADR 0001 partially realized: the trace exists; when
  monsters/combat land, `useDoorFacing` can collapse onto
  `traceMove(..., CONTENTS_WEAPONSOLID, ...)` + Chebyshev check.

## Rejected alternatives

* **Textbook clamped-segment rewrite (float or rational)** — rejected: the
  legacy interleaves fraction representations and clamp side effects; a clean
  rewrite silently changes edge behavior (exact-16-unit touches, parallel
  segments, start-inside fracs).
* **Keep `canPlayerStep`, add an entity pass inside it** — rejected: the name
  and tile-based contract mislead (research report flagged it); only two call
  sites, rename now while cheap.
* **Port BSP `traceWorld` descent** — rejected: descent exists solely to
  prune; with per-line rejects already faithful, the flat walk is provably
  equivalent and much less code. Revisit only if profiling demands it.
