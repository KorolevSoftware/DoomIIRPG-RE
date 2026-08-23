# Research: player collision in the original (`src/`) — 2026-08-23

## Hypothesis / questions

Document end-to-end how the legacy port blocks player movement (walls, doors,
entities), to replace the rewrite's simplified collision that lets the player
clip. Specific asks:

1. Movement flow from input to committed position (PlayingInputHandler →
   MovementController → Game trace helpers).
2. Decompose mask 13501 (and interact mask 13997, others); what a trace returns.
3. Wall-line math (`CapsuleToLineTrace`), oriented-sprite ±32 segments,
   circle radii.
4. Step semantics: point/capsule test? off-grid positions? slide vs reject?
5. Doors: cross-reference `docs/original-code/doors.md` §6.
6. Heights: any step-up logic?
7. Monsters reuse of the trace (mask bits only).
8. Automap/zoom/noclip effects on collision.

## Method

* Grepped masks (`13501`, `13997`, `21741`, `4141`, `4133`), trace helpers
  (`traceWorld`, `CapsuleTo*`), and call sites of `attemptMove`.
* Read full control flow: `src/MovementController.cpp`,
  `src/PlayingInputHandler.cpp`, `src/Game.cpp::trace/loadMapEntities/
  eventFlagsForMovement/performDoorEvent/touchTile/executeTile/spawnPlayerEntityCopy`,
  `src/Render.cpp::CapsuleToCircleTrace/CapsuleToLineTrace/traceWorld/
  nodeClassifyPoint/getHeight/drawNodeLines`, `src/LoadingManager.cpp`
  (map arrays), `src/Entity.cpp` (AI move/knockback), `src/Player.cpp`
  (relink/familiar), `src/InputEventController.cpp`, `src/ZoomController.cpp`,
  `src/AutomapController.cpp`, `src/ScriptThread.cpp` (EV_ABORT_MOVE).
* Bit-decomposed all masks with Python; cross-checked against
  `src/Enums.h:10-40`.

## Verdict

All eight questions answered — CONFIRMED with exact formulas/constants
(see `docs/original-code/player-collision.md`). Headline facts:

* Movement = one swept capsule trace per step:
  `Game::trace(viewX, viewY, destX, destY, playerEnt, 13501, 16)`
  (`src/MovementController.cpp:326-332`). Radius **16**, not 25.
  25 is the *entity circle* radius (passed squared as 625;
  env-damage uses 16²=256) (`src/Game.cpp:270-275`).
* Circle overlap test is `d² < r²+R²` (=881 for walking) — sum-of-squares
  approximation of (r+R)²=1681 (`src/Render.cpp:1122`). Must be ported as-is.
* Hit frac = `(t16.16 >> 2) - 1`; miss = 16384; commit requires zero hits —
  plain reject, never slide/partial (`src/MovementController.cpp:333`,
  `src/Render.cpp:1123,1207`).
* Walls = BSP leaf lines from mapXX.bin (byte coords `<<3`, packed 4-bit
  flags). Collision flag rules: `&7`: 0–3 solid, 4/6 never, 5 only for masks
  with PLAYERCLIP(0x10)/MONSTERBLOCK_ITEM(0x800) bits, 7 one-sided via cross
  product (`src/Render.cpp:1232-1247`).
* Oriented sprites (N/S/E/W bits `0xF000000`): segment ±32 through the
  current sprite position; axis chosen by `info & 0x3000000` (horizontal)
  else vertical (`src/Game.cpp:249-263`).
* No height checks anywhere in the accept path: collision is strictly 2-D;
  `destZ = 36 + getHeight(tile)` applied unconditionally after commit
  (`src/MovementController.cpp:343`, `src/Render.cpp:2444-2451`).
* Doors: consistent with `doors.md` §6 — re-verified unlink-at-open-end
  (`src/Game.cpp:3131`), relink-at-close-start (`src/Game.cpp:1079`).
* Blocked move in ST_AUTOMAP still calls `advanceTurn`
  (`src/MovementController.cpp:351-353`).

## Doc inconsistencies found

* `docs/status.md` Phase 4 row "Player collisions (trace)" mentions rewrite
  helper `canPlayerStep`. The original has **no** canPlayerStep concept and no
  height-based stepping at all — the name implies semantics that don't exist
  upstream. Rewrite's version also traces lines-only with radius 16 and skips
  the masked entityDb pass entirely (status row itself acknowledges that part).
* `docs/original-code/doors.md` §6 is accurate but could add two nuances:
  (a) the "circle radius 25" combines with the capsule radius 16 via the
  sum-of-squares test (effective ≈29.7 units, not 41); (b) "segment slides
  with the panel" applies only to sliding doors (parm bit 0 clear) and secret
  variants — red/blue slip doors stay centered while animating.
* No contradictions found between this investigation, `doors.md`, and
  `status.md`.

## Open questions

* Meaning of line flag values 1..3 vs 0 (all behave identically in collision
  and automap drawing draws only 0 and 6) — likely renderer variants; not
  needed for movement.
* `traceWorld` classifies BSP sides with `P << 4` while rendering classifies
  with raw view coords (`src/Render.cpp:1269` vs `:1086`) — faithful to the
  port as written; flagged so the rewrite reproduces both call sites exactly.
