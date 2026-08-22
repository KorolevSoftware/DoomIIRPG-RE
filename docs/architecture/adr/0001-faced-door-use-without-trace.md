# ADR 0001 — Faced-door use without a trace system

Date: 2026-08-23
Status: Accepted
Supersedes: —
Related: `docs/architecture/specs/2026-08-23-fix-doors-sprite-placement.md` (item A5), `docs/original-code/doors.md` R2, `docs/research/2026-08-23-doors.md`

## Context

The original selects the door to open by tracing along the view ray
(mask 13997 incl. ET_DOOR, `src/PlayingInputHandler.cpp:200-221`) and accepting
the first hit within Chebyshev distance² ≤ 4096 of the player
(`src/PlayingInputHandler.cpp:445`, `src/Combat.cpp:42`,
`src/Entity.cpp:1155-1158`). The rewrite has no entity trace yet; its
placeholder picked the Euclidean-nearest door within 3 tiles of the faced tile
(`new_src/domain/game/Game.cpp:78-96`), which can open doors the player is not
facing. The full fix — porting `Game::trace`/capsule tests over entities — is
disproportionate to the current milestone.

## Decision

Implement `Game::useDoorFacing(map, px, py, stepX, stepY)` as a documented,
bounded simplification:

* Candidate set = LINKED door entities on the player's tile and on the adjacent
  tile in the facing direction only. Both candidates satisfy the legacy 1-tile
  Chebyshev cap by construction.
* Selection order = own tile first (ray fraction ≈ 0), then facing tile; within
  a tile, tile-list insertion order.
* Linked-only filter mirrors legacy semantics: traces walk `entityDb`, which
  contains only linked entities, so open (unlinked) doors cannot be "used".
* Locked refusal stays inside `performDoorEvent` (`eSubType == 1` → no-op),
  matching `src/Game.cpp:1059-1061`.

Player collision keeps its existing tile-granular model
(`canPlayerStep`: solid ⇔ door linked in `entityDb`), now driven by the same
linked-state timeline as the original (solid through the whole open animation,
passable at open end, solid from close start).

## Consequences

* Positive: correct perceived behavior (E opens what you face); no new trace
  infrastructure; deterministic; ~20 LOC.
* Negative / accepted deviations:
  * Doors diagonally adjacent or >1 tile away but visually ahead are not
    reachable by use (legacy ray could reach them within 6 tiles when standing
    back — but the ≤1-tile rule already excluded those).
  * Coincident doors on one tile resolve by insertion order, not ray geometry.
  * Collision granularity is whole-tile while linked (legacy blocks via a
    sliding ±32 wall segment through the animated position).
* Escape hatch: when the real trace system lands (combat targeting needs it
  anyway), `useDoorFacing` collapses into a thin wrapper around
  `trace(..., CONTENTS_WEAPONSOLID, …)` + the Chebyshev check and the
  simplification is deleted.

## Rejected alternatives

* **Port the full capsule-vs-entity trace now** — rejected: large surface area
  (trace masks, segment tests per oriented entity) for one interaction.
* **Keep nearest-in-3-tiles but add a facing-dot filter** — rejected: still
  opens laterally placed doors at tile corners; harder to reason about than an
  explicit candidate set.
* **Ray-vs-AABB against door tiles only** — rejected: more code than the
  candidate set with no additional fidelity for wall-aligned doors.
