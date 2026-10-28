# 2026-09-22 — Monster movement and line of sight

## Questions / hypotheses

1. `Entity::checkLineOfSight` — algorithm, blockers, parameters.
2. Goal movement (`aiGoal_MOVE`, `aiReachedGoal_MOVE`, `aiMoveToGoal`,
   `aiCalcSimpleGoal`): how the target tile is chosen, one tile per turn?,
   blocking rules (monster clip, doors, entities, masks).
3. `aiInitLerp`/`aiFinishLerp` + `Game::monsterLerp`: timing, walk frames,
   when the entityDb link/unlink happens.
4. Sprite facing while moving.

## Method

Read `src/Entity.cpp:535-1153`, `src/Game.cpp:90-330` (link/trace),
`:729-750` (findMapEntity), `:858-939` (monsterAI/monsterLerp),
`:2375-2470` (canSnap/snapMonsters/updateMonsters),
`:2855-3240` (updateLerpSprite, snapLerpSprites, alloc/freeLerpSprite),
`:3399`, `:3596-3666`; `src/Render.cpp:746-824` (cullBoundingBox);
`src/GameStateRunner.cpp:20-80,175-185`; `src/ScriptThread.cpp:1152-1160,
1752-1761`; `src/Enums.h:60-84,293-305,567-580`; `src/Canvas.h:108`;
`src/Combat.cpp:42,1235-1242`. Grep for every `goalFlags` writer.

## Verdicts

1. **CONFIRMED with a twist.** `checkLineOfSight(x0,y0,x1,y1,mask)` in tile
   coords. Returns false for non-axis-aligned pairs; the stepping loop
   `while (n != n3 && n2 != n4)` (`src/Entity.cpp:898`) can never execute
   because one axis delta is already 0, so the entity test at `:901` is dead.
   Net: true iff same row or column. Only used as a score hint in `calcPath`
   (`:917,965`, mask `15535|0x100`). Real hit checks are traces in
   `aiWeaponForTarget` (masks 5295/15791/4131) and `aiIsAttackValid` (5295).
2. **CONFIRMED — depth-limited DFS, one 4-connected step per turn.**
   `calcPath` (`src/Entity.cpp:908-1028`) greedy DFS, depth 8
   (`:1043`), neighbours sorted by squared tile distance to goal (chase
   ascending, flee descending), random start direction (`:946`), global
   visited bitset seeded from `baseVisitedTiles` (`:1049`), ENV_DAMAGE tiles
   skipped (`:955`), edges tested with `trace(..., 15535, 16)` (`:1004`);
   doors are path-transparent (`interactClipMask = 32`, `:1011-1015`).
   Best partial path used as fallback (`:1079-1083`). Only the first step is
   executed (`:1085-1090`). A door on that step → `performDoorEvent(0, door, 2)`
   instead of moving (`:1105-1111`). Monster clip = `baseVisitedTiles` bits
   from `linkWorldEntity` and script `EV_MARKTILE` flag 1
   (`src/Game.cpp:179,3656-3666`, `src/ScriptThread.cpp:1152-1160`).
   Chase goal tile = player's `destX/destY` tile, `lineOfSightWeight = -4`
   (aligned tiles preferred) (`:1051-1055`). Adjacent to player → success at
   depth 0 → no move (`:1084`).
3. **CONFIRMED.** Relink at START (`src/Entity.cpp:1093-1094`), lerp flagged
   `LS_FLAG_ENT_NORELINK|LS_FLAG_ASYNC` (`:866`). Travel 275 ms, 500 ms for
   eSubType 13 or (14 && parm) (`:1099-1104`), 400 ms knockback (`:1262`).
   Linear interpolation `t = elapsed*256/travel` (`src/Game.cpp:2876-2882`).
   While `goalFlags & 1`, `trace` uses the goal tile centre for that monster
   (`src/Game.cpp:227-231`). End → `freeLerpSprite` → `aiFinishLerp`
   (`src/Game.cpp:3213-3216`) → `aiReachedGoal_MOVE` (MOVE2ATTACK monsters
   may attack immediately, `src/Entity.cpp:1121-1136`). `monsterLerp` only
   recomputes `interpolatingMonsters` (`src/Game.cpp:915-939`). On-screen
   steps set `info 0x10000000` (`src/Entity.cpp:1095-1097`) and block
   `canSnapMonsters` (`src/Game.cpp:2375-2389`); off-screen steps snap.
4. **CONFIRMED (limited).** Only front/back anim nibble. 1-tile steps always
   use WALK_FRONT (32) because `WorldDistToTileDist(4096) == 1` is not `> 1`
   (`src/Game.cpp:2907-2915`, `src/Combat.cpp:42,1235-1242`); IDLE_BACK →
   WALK_BACK. Frame `(1 + (t*dist>>12)) & 3` = 4 frames per tile
   (`src/Game.cpp:2932`). Reset to IDLE / IDLE_BACK at end
   (`src/Game.cpp:3104-3111`).

## Side findings

* `goalFlags & 0x10` branch in `aiReachedGoal_MOVE`
  (`src/Entity.cpp:1141-1151`) is dead — no writer in `src/`; also
  `goalFlags` is saved in 4 bits only (`src/EntityMonster.cpp:26,32`).
* Cross-check: `docs/original-code/doors.md` cites the monster door-open at
  `src/Entity.cpp:1109-1111` — verified correct (`if (...eType == 5)` at
  1109, `performDoorEvent(0, traceEntity, 2)` at 1110).
* `aiGoal_MOVE` begins with `snapLerpSprites(sprite)` (`:1035`), so a
  re-think during the same turn finishes the previous step first (and may
  trigger its `aiReachedGoal_MOVE` attack).

## Open questions

* Whether the dead LOS loop is an RE-port bug (original likely `||`) cannot
  be resolved from `src/`; the port's behaviour is "aligned = LOS".
* Exact goal-choice/turn order and haste are in the parallel turn-pipeline
  report.

Curated: `docs/original-code/monster-movement.md`.
