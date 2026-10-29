# ADR 0032 — Item use: `Player` owns the item switch, effect planes and msg 111; the menu owns turn sequencing

Date: 2026-10-28
Status: accepted
Spec: `specs/2026-10-28-use-item.md` (G1a, G1b, G2a)

## Context

Legacy `Player::useItem` (`src/Player.cpp:801-959`) applies the effect, plays the sound,
decrements the slot and ends with `game->advanceTurn()`. Its only caller,
`MenuSystem::select` actions 20/21 (`src/MenuSystem.cpp:3149-3179`), then runs the monster
phase in the menu (`snapMonsters(true)`, `snapLerpSprites(-1)`) and leaves the menu if a monster
attack switched the canvas to `ST_COMBAT`. Status effects (`statusEffects[54]`, `buffs[30]`) and
their helpers live on legacy `Player` and are read by combat, AI, movement and the HUD.
`addHealth` emits HUD message 111 itself (`src/Player.cpp:445-449`).

The rewrite's `Player` has no `Game`, `Hud` or `Localization` reference; `Game` holds a `Player*`
(`Game::setXPSystems`). There is no `ST_COMBAT`: a monster attack is `combat.active` inside
`Playing` (ADR 0028).

## Decision

1. `Player::useItem(n)` returns `UseResult { Refused, Used, UsedNoTurn }`. The caller calls
   `Game::advanceTurn()` on `Used`. Sound + decrement still happen inside `useItem`, so the order
   is identical to legacy (advanceTurn was its last statement).
2. `statusEffects`/`buffs` and `add/remove/update/translateStatusEffects`, `updateStats`,
   `Player::advanceTurn` stay on `Player` with legacy names and layout; consumers read
   `player.buffs[...]` / `player.statusEffects[...]` through the `Player*` they already hold.
   `Game::advanceTurn` calls `player_->advanceTurn()` and does the haste parity.
3. `Player` gets `setMessaging(Hud*, const Localization*)`; `addHealth(amount, showMessage = true)`
   emits msg 111 as legacy does. This also restores the message on food pickup.
4. MenuSession maps "the canvas left ST_MENU" to `game.combat.active || host.state() != Menu`
   and calls `returnToGame()` for the first.

## Consequences

- One new domain→ui dependency (`Player` → `Hud`), the same shape `ItemPickup`, `Combat` and
  `MonsterSystem` already have.
- Any future caller of `useItem` must honour `UseResult::Used` (there is none in legacy).
- Buff consumers need no new plumbing: `Combat::env().player`, `MonsterAI::Env::player`,
  `Game::player_` already exist.

## Rejected alternatives

- **`Player` holds a `Game*` and calls `advanceTurn` itself** — creates a `Player`↔`Game` cycle
  for one call.
- **A separate `BuffSystem` peer on `Game`** — legacy code and every consumer index
  `player->buffs[k]` directly; a separate owner adds indirection with nothing to hide, and the
  planes must die with `Player::reset()` anyway.
- **`addHealth` returns the delta and callers compose msg 111** — four call sites (use, food,
  give kind 3, Energy Blast) would each duplicate the composition, and the quiet regen variant
  still needs the flag.
- **Keep the menu open and run the monster attack in Menu** — Menu does not tick `Combat`; the
  attack would freeze until Resume. Leaving the menu is the legacy behaviour.
