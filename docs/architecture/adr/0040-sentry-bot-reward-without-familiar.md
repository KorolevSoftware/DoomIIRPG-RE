# ADR 0040 — The sentry bot minigame grants a real bot weapon, but the familiar system (deploy/control) is deferred

Date: 2026-10-30
Status: accepted; decisions 2, 3 and 5 superseded by ADR 0041 (2026-11-02, spec `../familiar.md`)
Spec: `../sentry-bot-minigame.md`

## Context

Winning "Matrix Skip" calls `awardSentryBot(w)` (`src/SentryBotGame.cpp:784-804`): it discards an
already owned bot (`attemptToDiscardFamiliar`, drops it as a world item, `showHelp(16)`), then
`give(1, w, 1)` grants bot weapon 3..6. Owning that weapon is the entry to the "familiar" system
(`docs/original-code/sentry-bot-minigame.md` §8): FIRE deploys the bot, the player then controls it
(camera, separate inventory, tinted view, bot HUD, self-destruct, death state, return and loot
transfer, ~400 script checks on `v16`, save fields). That is about 17 legacy files and 700-900
lines. The minigame itself (Part A) is self-contained and every map01-08 terminal needs it.

## Decision

1. Port the minigame faithfully; the reward is `Player::give(1, w, 1)` only (already ported with
   the bot pre-step: ammo slot 7 = 100, bits 0x78 cleared, bit w set, weapon selected).
2. `attemptToDiscardFamiliar` and `showHelp(16)` are skipped with a `[familiar]` log; `give`
   evicts the previous bot bit, so the old bot simply disappears instead of dropping.
3. FIRE with a bot weapon selected is intercepted at the legacy spot
   (`src/PlayingInputHandler.cpp:493-495`, after the wall push, before zoom/attack) and does
   nothing except log `[familiar] deploy deferred`. No turn passes.
4. The bot view sprite (tiles 19/18, frames 2+3 with the bob/mirror, `src/Combat.cpp:797-813`) is
   ported, so the granted weapon looks right.
5. `scriptStateVars[16]` (isFamiliar) stays 0 forever, so every terminal guard `v16 == 0` passes.

## Consequences

- All Matrix Skip terminals work and unlock; botType 1 terminals still wake their hostile bot on a
  loss (script side, unaffected).
- The player can own and select a bot weapon that does nothing. This is visible but harmless and
  is the documented gap until the familiar system is designed (its own spec + ADR).
- Saves store the bot bit and ammo[7] in the legacy layout, so a later familiar port can read them.
- `isFamiliar`/`familiarType` in saves keep being written as 0.

## Rejected alternatives

- **Port the familiar system now:** too large for this cycle and orthogonal to the minigame.
- **Grant nothing (result 1 only):** the scripts would unlock, but the world would diverge from the
  original (no bot weapon bit, no ammo[7]) and a later port would find inconsistent saves.
- **Let FIRE fall through to `fireWeapon`:** weapons 3..6 have no attack art/flash in the original
  and would start a meaningless attack sequence.
