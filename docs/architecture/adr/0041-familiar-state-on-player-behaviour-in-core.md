# ADR 0041 — Familiar: state on `Player`, behaviour in `core/Familiar`, `ST_BOT_DYING` as a real state, deferred monster return through one latch

Date: 2026-11-02
Status: accepted (supersedes ADR 0040 decisions 2, 3 and 5)
Spec: `../familiar.md`

## Context

The familiar (sentry-bot control) touches ~20 legacy sites: `Player::give`/`getHealth`/`addHealth`/
`pain`/`selectWeapon`/`addStatusEffect`, `CombatEntity::calcDamage`, `Combat`, the playing input
handler, `MovementController::attemptMove`, `Game::freeLerpSprite`, `MenuSystem`, `DialogSystem`,
`ScriptThread`, `Hud`, `Render` and save/load (`docs/original-code/familiar-core.md`,
`familiar-presentation.md`). The state-machine parts (deploy runs a real move, death is its own
canvas state `ST_BOT_DYING = 25`, return teleports/relinks/re-fires FACE events) need
`StateHost`, `PlayerActions`, `DialogSystem` and `Hud`, which `domain/` must not own.

## Decision

1. **State lives on `Player`**, named as in legacy (`isFamiliar`, `familiarType`, `inventoryCopy`,
   status/buff copies, `playerEntityCopyIndex`, the three bookkeeping bools, `saveX/Y/Z/Angle/Pitch`).
   Every domain reader (`give`, `getHealth`, `pain`, `calcDamage`, save codec, `SpriteLerps`) reads
   it there with no new interface. `postProcessMode` is not stored: draw code derives it from
   `familiarType`.
2. **Behaviour lives in one new peer `core/Familiar`** (like `TargetPractice`, ADR 0039): deploy,
   set/unset, return, discard entry, self-destruct dialog, dying entry/tick, death, bot remains,
   explosion. Domain callers that legacy lets call it directly (`ScriptVM`, `DialogSystem`) reach it
   through `GameContext::familiar()`, the path they already use for `targetPractice()`/`loading()`.
3. **`ST_BOT_DYING` is `StateId::BotDying = 25`**, ticked by `Familiar::tickDying`, entered through
   `GameContext::setState` (entry hook → `Familiar::onEnterBotDying`). Input is dropped there (it is
   neither Playing nor Automap), `gameTime` is frozen, the world, view weapon, tint and bottom bar
   are drawn.
4. **Monster-at-body return is a latch**: `SpriteLerps::freeLerpSprite` sets the legacy field
   `Player::unsetFamiliarOnceOutOfCinematic` instead of calling into `core/`. `GameContext::tick`
   consumes it at the end of every tick whose state is not `Camera`/`BotDying`. Legacy returns
   inside `freeLerpSprite` (outside `ST_CAMERA`); the rewrite returns at most one 15 ms tick later.
   The camera-end path keeps its legacy meaning (pending while in `Camera`).
5. **Death detection follows legacy fields**: player death reads `ce.getStat(STAT_HEALTH)`
   (`src/GameStateRunner.cpp:170`, `src/Combat.cpp:1002`), bot death reads `ammo[7]`;
   `getHealth()` keeps its legacy dual meaning, so the existing `getHealth() <= 0` guards in
   `Combat::tick` / `MonsterSystem::onMonsterAttackDone` already mean "ST_DYING or ST_BOT_DYING".
6. Not ported, documented as deviations: `smackScreen` (its only effect is the `#if 0` J2ME pixel
   scroll, `src/Render.cpp:2598-2617`), `savePlayerFog`/`loadPlayerFog` (the rewrite has no
   per-player fog), particle greyscale (no particles), `goldCopy`/`tookBotsInventory`
   (write-only), `equipForLevel` (no caller in the rewrite), `flagForWeapon` (attack tile events
   unported).

## Consequences

- Save files carry the real familiar bytes; `playerEntityCopyIndex` is -1 when not deployed, as in
  legacy. Old rewrite saves (written with 0) load fine because `isFamiliar` was false.
- `ScriptVM`/`DialogSystem` gain one more `GameContext` call each; no new interface type.
- The adjacent-monster return can be one tick late; no script observes the difference (scripts only
  see `v16`, refreshed at every script entry).

## Rejected alternatives

- **A `FamiliarState` struct separate from `Player`:** every `give`/`pain`/save line would take a
  second object; legacy keeps it on `Player` and so do the readers.
- **Behaviour in `domain/game/Player`:** needs `StateHost`, `PlayerActions` (a real move),
  `DialogSystem` and `Hud` fades — exactly what ADR 0010 keeps out of `domain/`.
- **Callback (`std::function`) from `SpriteLerps` into `Familiar`:** re-entrant
  return/teleport/relink in the middle of the lerp pool loop; the latch is the shape legacy already
  uses for the cinematic case.
- **Bot death inside `ST_DYING` with a flag:** two unrelated timelines, render rules and exits in one
  state; legacy keeps them apart.
