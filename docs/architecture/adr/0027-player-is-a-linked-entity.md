# ADR 0027 — The player is a real, linked entity (`entities[1]`) positioned at its destination tile

Date: 2026-09-22
Status: accepted
Spec: `specs/2026-09-22-monster-attacks.md` group G1
Research: `docs/original-code/monster-attack.md` §2, `docs/original-code/monster-movement.md` §2.1

## Context

Every monster decision in the original is a trace that must *hit the player entity*:
`aiWeaponForTarget` requires `traceEntity == &entities[1]` (`src/Entity.cpp:768-771`),
`aiIsAttackValid` requires `traceEntity->def->eType == 1` (`src/Entity.cpp:655-668`), and
`calcPath` ends a search when the trace reaches `findEnt == &entities[1]`
(`src/Entity.cpp:1005-1010`). The monster move mask 15535 contains `ET_PLAYER`, so the player
also blocks monster steps.

In the original, `entities[1]` has `def = find(ET_PLAYER, 0)` and `info = 0x20000`
(`src/Game.cpp:366-369`), and `Player::relink()` links it at `canvas->destX/destY`
(`src/Player.cpp:1425-1445`) at spawn (`src/Game.cpp:969`), at every move commit
(`src/MovementController.cpp:348`) and after a scripted GOTO (`src/ScriptThread.cpp:656`).
The trace resolves an `ET_PLAYER` hit at `canvas->destX/Y` (`src/Game.cpp:238-241`), and
`Entity::calcPosition` returns the same point (`src/Entity.cpp:1379-1382`).

The rewrite reserves slot 1 but leaves it def-less and never links it
(`new_src/core/PlayerActions.cpp:39` "player entity not linked"), the trace's `ET_PLAYER`
branch reads the *view* position (`new_src/domain/game/TraceSystem.cpp:132-137`), and
`TraceSystem::distFrom(Entity*)` reads a sprite the player does not have (sprite −1 → (0,0)).

## Decision

1. `Game::loadEntities` gives `entities[1]` the `find(ET_PLAYER, 0)` def and `kInfoActive`,
   exactly like `src/Game.cpp:366-369`. The world slot keeps `def == nullptr` (ADR 0011).
2. One call, `Game::relinkPlayer(const Player&)`, unlinks and re-links the player entity at
   `(destX>>6, destY>>6)` when the destination is inside `[0,2047]`, and pushes the destination
   into `TraceSystem`. It is called at the three legacy sites plus the target-practice
   teleport (deviation, see the spec).
3. `TraceSystem` keeps two player points: the view point (`setPlayerPos`, unchanged readers) and
   the destination point (`setPlayerDest`). The `ET_PLAYER` trace branch and
   `distFrom(ET_PLAYER entity)` use the destination.
4. `EntityDb::findEntityBySprite` returns `nullptr` for `sprite < 0` (the player now has a def
   and sprite −1), and `EntityDb::resetEntities` also clears the 1024 tile heads.

## Consequences

* Monster traces work without any special case: the player is "just an entity" in the
  broadphase, as in the original.
* Tile-list walkers now see the player. Audited: door use (`isDoor` filter), door occupancy
  (monster filter + explicit player test), corpse loot (`isCorpse`), item touch (`ET_ITEM`
  filter), trace callers (all pass the player as `skipEnt` or use masks without `PLAYER`).
  `EV_TILE_EMPTY` on the player's tile now answers "not empty" — this is the legacy answer
  (`src/ScriptThread.cpp:456-475`, eType 1 is not in `0x6240`).
* The tile-head clear makes a second `loadEntities` (level restart, spec G8b) safe.

## Rejected alternatives

* **Special-case the player inside every monster trace** ("hit test against the player point
  after the trace"): duplicates the capsule/circle math and diverges from the legacy sort order
  of hits (a monster in front of the player must win).
* **Keep the trace on the view point**: monsters would target the interpolated position while
  the player walks, which the original never does (all AI reads `destX/destY` or reads the view
  only when the view equals the destination).
