# 2026-10-30 Sentry bot minigame (EV_MINIGAME type 0) and the bot reward

## Hypothesis / questions
1. How does the original "Matrix Skip" minigame work: entry, generation, input, screens, drawing, sounds, result?
2. What does the win grant, and what does owning a sentry-bot weapon do in normal play (familiar system)? Port or stub?

## Method
- Read `src/SentryBotGame.cpp` (all 892 lines) and `.h`; call sites in `src/ScriptThread.cpp:1290-1303`,
  `src/LoadingManager.cpp:80-106`, `src/Canvas.cpp:450-460`, `src/InputEventController.cpp:20-44,330-345`.
- Reward: `src/Player.cpp` (give 962-1063, familiar functions 2080-2518), grep for `isFamiliar|familiarType|ST_BOT_DYING|postProcessMode`
  over `src/` (17 files).
- Scripts: extracted map00-09.bin from the IPA into the scratchpad and ran `tools/disasm_map_scripts.py` on each;
  grepped `MINIGAME type=0` and read the following EVAL on v7.
- Strings: split file 0 of `strings00.bin` on NUL; calibrated with 30 "Exit", 96 "Tap Screen to Continue", 164 "Hacker"
  (all match the hacking doc).
- Checked `new_src/` with grep for familiar/sentry/minigame.

## Verdict
CONFIRMED (with corrections to hacking-minigame.md §6): see `docs/original-code/sentry-bot-minigame.md`.

## Evidence (key quotes)
- `src/SentryBotGame.cpp:56` `int n4 = (10000 * n + (n2 - n) * (100 - n3) * (100 - n3)) / 10000;` with n=2, n2=6.
- `:58-60` skipCode re-rolled while equal to `player->lastSkipCode`; stored back as uint8.
- `:61-67` `b = fromMenu || IQ < 50`; when `!b`, cells equal to skipCode are re-rolled.
- `:68` `gameBoard[0] = skipCode % 6 + 1`; `:71-74` solution walk from -1 by skipCode mod 16, skipping skipCode cells.
- `:130` failure screen (and menu-mode success) exit on actions 7/15/6 → `endGame(0)`.
- `:195-212` J2ME early fail is `#if 0`; FIRE appends `gameBoard[player_cursor]`.
- `:276-282` win FIRE: `awardSentryBot(botType==0 ? 3 : 5)` for cursor 0, else 4/6, then `endGame(1)`.
- `:784-804` award discards an owned bot (bits 0x8,0x10,0x20,0x40) via `attemptToDiscardFamiliar` + `showHelp(16,true)`,
  then `give(1, n, 1)`.
- `src/Player.cpp:978-983` bot give: refused when `isFamiliar`; `give(2, 7, 100, true)`; clears bits 8|16|32|64.
- `:809` sound `(n == 1) ? 1043 : 1040`; `:812-813` `evaluateMiniGameResults(n)`, `scriptStateVars[7] = n`.
- `:889-891` failure-screen touch sends ACTION_MENU (5), which no failure branch handles → touch does nothing there.
- `:685-701` button render modes (Submit 1 when `answer_cursor>3`, Delete 1 when `answer_cursor>0`); `src/Image.cpp:172-175`
  mode 1 = alpha 0.25.

### Script call sites (disasm IPs)
map01 6154 (0); map02 2131, 2219, 2309 (0); map03 3228, 3295 (0); map04 3427 (1); map05 3667 (1); map06 1973 (1);
map08 995 (1). Type-0 terminals act only on `v7==1`. Type-1 terminals unlock on both `v7==0` and `v7==1`, but on 0 they
also run `ENTITY_BREATHES` + `LERPSPRITE` + `WAKEMONSTER` (sprites 21 / 103 / 74 / 57): a hostile sentry bot comes out.
v7==2 (quit) does nothing. All are guarded by `v16 == 0` (not controlling a bot).

### Strings (file 0)
171 Matrix Skip, 172 rules, 173 Hint:, 174 Skip Code:, 175 Enter your code, 176 Tap number to pick a number,
177 Code Accepted!, 178 Code Denied!, 179 Incorrect Input!, 180 Select a bot type, 181 Shooting, 182 Exploding,
183/184 bot descriptions, 186 WARNING!, 187 Sentry Bot initiating defensive action., 188 You are being logged out. Good day.,
190 No room to deploy, step back., 192 Your Sentry Bot has perished!, 194 Self-destruct?, 217 Cannot deploy sentry bot here.,
219 You are under attack!|Sentry bot cannot be deployed., 221 Cannot discard sentry bot here., 239 Hack Successful!, 240 Hack Failed!

### Familiar system scope
Files touching it: Player (44 refs, ~450 lines), Combat 5, Hud 6, PlayingInputHandler 9, Render 4, MovementController 4,
ScriptThread 4, Game 3, Canvas/GameStateRunner (ST_BOT_DYING), CombatEntity, Entity, DialogSystem, MenuSystem,
SoftKeyController, TouchController, InputEventController, MayaCamera, ParticleSystem. Deploy = the player takes control of
the bot (camera moves, a body copy stays behind), bot health in `ammo[7]`, separate pickup inventory, tinted view, bot HUD,
self-destruct/explode, death state, return-and-loot transfer, save fields.

### new_src state
`new_src/minigames/common/MiniGameHost.cpp:31` `default: return false;   // sentry bot: later`. The VM then logs
"MINIGAME type=0 not implemented" and continues (`new_src/domain/game/ScriptVM.cpp:1380-1385`), so the next
`EVAL v7 == 1` reads a STALE v7 (for example from an earlier hacking win). No familiar/sentry code exists in new_src
(`new_src/ui/ViewWeapon.cpp:131-133` defers the bot view sprite; `new_src/core/MenuSession.cpp:458-460` notes no isFamiliar).

## Open questions
- The text for `showHelp(16)` and help 12/13 (help-message table, not file 0) was not resolved.
- Whether the Submit/Delete 25%-alpha "active" state is intended (literal reading of `:685-701`).
