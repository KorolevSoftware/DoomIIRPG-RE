# ADR 0028 — Monster behaviour is a peer module `MonsterAI`; monster attacks run on `Combat` inside `ST_PLAYING` (no `ST_COMBAT` state); death is detected from health

Date: 2026-09-22
Status: accepted
Spec: `specs/2026-09-22-monster-attacks.md` groups G2-G3, G6
Research: `docs/original-code/monster-turn.md`, `docs/original-code/monster-attack.md`,
`docs/original-code/monster-movement.md`, `docs/original-code/player-damage.md`

## Context

The original spreads monster behaviour over `Entity` methods (`aiThink`, `aiWeaponForTarget`,
`aiGoal_MOVE`, `calcPath`, `attack`, `undoAttack`, `aiFinishLerp`, `src/Entity.cpp:535-1191`),
`Game` (`monsterAI`, `monsterLerp`, `updateMonsters`, `snapMonsters`,
`src/Game.cpp:874-939, 2374-2474`), `Combat` (`performAttack`, `monsterSeq`,
`explodeOnPlayer`, `src/Combat.cpp:54-166, 427-610, 948-1056`) and the state runner
(`GameStateRunner::combatState`, `src/GameStateRunner.cpp:20-88`, which runs in a dedicated
`ST_COMBAT` canvas state).

The rewrite already decided that entities are passive records and behaviour lives in peer
subsystems (ADR 0010, ADR 0014), that `MonsterSystem` owns the monster pool, rings, pain and
death (spec `2026-08-26-decomposition` P2-GC), and that the player's own attack runs as
`Combat::active` inside `ST_PLAYING` with no `ST_COMBAT` state (spec `2026-08-26-combat-stage1`
§0.C, `new_src/core/GameContext.cpp:361-365`). `MonsterSystem.cpp` is ~290 lines; the AI and
pathing port alone is ~600 lines.

## Decision

1. **New peer `domain/game/MonsterAI`** owns the per-monster brain and legs: goal validity and
   choice, weapon choice, attack re-validation, the one-step path search, the step lerp start
   and its completion (`aiFinishLerp`/`aiReachedGoal_MOVE`), and the static monster-clip bitset
   `baseVisitedTiles`. It is stateless per monster (all per-monster state stays in
   `EntityMonster`); its only own state is the path-search scratch and the clip bitset.
2. **`MonsterSystem` keeps the collective side**: the rings, the pending-attack queue
   (`attack`/`undoAttack`), the per-turn driver (`monsterAI` loop, `monsterLerp`,
   `updateMonsters`, `canSnapMonsters`/`snapMonsters`, `endMonstersTurn`) and the
   "attack finished, start the next one" step that `combatState` performs
   (`onMonsterAttackDone`). It calls `MonsterAI` for every per-monster decision; `MonsterAI`
   calls `MonsterSystem::attack` to enqueue. The two hold non-owning pointers to each other
   (forward declarations only, no header cycle).
3. **Monster attacks run on the one `Combat` instance**, exactly like the original's single
   `app->combat`: `Combat::curAttacker != nullptr` selects `monsterSeq`, `nullptr` selects the
   existing player sequence (`runFrame`, `src/Combat.cpp:857-862`). There is still **no
   `StateId::Combat`**: `Combat::active` inside `ST_PLAYING` is the `ST_COMBAT` analog for both
   directions. `GameContext::tickPlaying` dispatches the "sequence finished" edge: player
   attack → `advanceTurn`, monster attack → `MonsterSystem::onMonsterAttackDone`.
4. **Player death is detected from health, not signalled.** `Player::pain` is the state half
   only (like `addXP`, spec combat-stage1 deviation 14); when health reaches 0 the monster
   sequence returns "finished", `onMonsterAttackDone` empties the queue, and `GameContext`
   enters `StateId::Dying` in the same tick. The `Dying` entry hook does what
   `Player::died` does to the rest of the game (`src/Player.cpp:711-733`).
5. **RNG policy.** Every new roll uses `Combat::nextByte()` (the one `std::rand()` stream the
   rewrite already uses for combat and spawn flips) at the exact legacy call sites. The
   `dodgeDir = nextInt()` roll of `launchProjectile` (`src/Combat.cpp:1444`) is consumed **only
   on the missile path** (positive `PROJTYPE`), not on the default arm, so the player's hitscan
   roll sequence stays what it is today.

## Consequences

* The hand-off chain is one readable path: `advanceTurn` → `updateMonsters` →
  `performMonsterAttack` → `monsterSeq` → `onMonsterAttackDone` → next attacker or
  `endMonstersTurn`; every step is a port of one legacy function.
* Input in `ST_COMBAT` differs from `ST_PLAYING` only by the L/R rotation rule
  (`src/InputEventController.cpp:364-372`); `PlayerActions::handleAction` implements it under
  `Combat::active && curAttacker != nullptr`.
* Code that tests "is the game in combat" keeps using `Combat::active`; code that needs the
  direction reads `Combat::curAttacker`.
* Monster AI adds `std::rand()` consumption (weapon choice ties, path-search tie order), so
  any run in which a monster is awake no longer reproduces an older run's player rolls. The
  intro/golden-frame runs are unaffected unless a script wakes a monster and a turn passes —
  the golden captures are re-checked after G2, G3 and G6.

## Rejected alternatives

* **Everything into `MonsterSystem`** — a ~900-line class mixing the collective turn driver
  with per-monster pathing; the decomposition spec split `Game` for exactly this reason.
* **Methods on `Entity`/`EntityMonster`** — contradicts ADR 0014 (entities stay passive) and
  would pull `TraceSystem`/`Combat` into the entity header.
* **A real `StateId::Combat`** — would have to be retrofitted onto the already-accepted player
  attack path, and every state-gated site (`gameTime` advance, lerp ticking, HUD, view weapon,
  dialog restore) would need a new branch. Nothing in the original's `ST_COMBAT` behaviour
  needs a state of its own beyond the L/R input rule.
* **A `Player::died()` that sets the state itself** — `Player` would need the `StateHost`;
  the health check already exists at the legacy position (`src/GameStateRunner.cpp:170-173`,
  `new_src/core/GameContext.cpp:349-352`).
