# 2026-10-30 Familiar (sentry-bot control) core lifecycle

Curated result: `docs/original-code/familiar-core.md`.

## Hypotheses / questions
State fields, deploy, return triggers, damage/death, self-destruct, discard/pick-up, save/load, script vars
of the "familiar" system in `src/`. Rendering/HUD/combat AI/input were out of scope (parallel researcher).

## Method
- `grep -rn -a` over `src/` for every familiar identifier (isFamiliar, familiarType, *Copy, playerEntityCopyIndex,
  ST_BOT_DYING, EV_FORCE_BOT_RETURN, ACTION_BOT_DISCARD, spawnPlayerEntityCopy, spawnSentryBotCorpse, ...).
- Read `src/Player.cpp:2079-2518` (all familiar methods), `:125-220,415-460,495-560,620-752,961-1063,1270-1419,1565,
  1655-1700,2660-2760`; `src/Game.cpp:1210-1225,2155-2189,2612-2811,3200-3232,3461-3471`;
  `src/GameStateRunner.cpp:160-180,283-322`; `src/Canvas.cpp:1125-1145`; `src/DialogSystem.cpp:558-570`;
  `src/ScriptThread.cpp:484-514,980-1000,1740-1752,1920-1947`; `src/Entity.cpp:128-160,232-256,1497-1545,1740-1770,
  1787-1800,1997-2011,2298-2307`; `src/CombatEntity.cpp:345-375`; `src/Combat.cpp:400-418,1025-1040`;
  `src/PlayingInputHandler.cpp:72-100,400-500`; `src/MovementController.cpp:312-358`; `src/MenuSystem.cpp:1545-1562,
  3256-3259`; `src/MayaCamera.cpp:380-392`; `src/Render.cpp:1774-1789,2698-2712`.
- Disassembled map00..map09 from the .ipa with `tools/disasm_map_scripts.py` (outputs in session scratchpad);
  counted `FORCE_BOT_RETURN`, `CHANGE_MAP`, `DESTROY_PLAYER`, `v16`.

## Verdict
CONFIRMED with corrections to the scoped summary in `sentry-bot-minigame.md` §8:
- ammo/inventory/weapons/ce are NOT swapped at deploy; only buffs + status effects swap. The *Copy item arrays are
  the bot's bag (zeroed at deploy, routed by `give`), and are shared with target practice/VIOS stripping.
- Deploy has no "tile in front free" test besides `mapFlags & 0x10`; occupancy fails inside `setFamiliar`'s
  `attemptMove` (message 190).
- Additional return triggers: in-game menu action 33 (`src/MenuSystem.cpp:3256`), deferred monster return after
  `ST_CAMERA` (`src/MayaCamera.cpp:386`), `equipForLevel` (`src/Player.cpp:1697`).
- Walk-in return (b=true) keeps the move animation and does not restore fog (`src/Player.cpp:2152-2161`).
- A killed exploding bot explodes as well (explosion depends only on type, `src/Player.cpp:2228-2230`).
- `tookBotsInventory`, `botReturnedDueToMonster`, `goldCopy` are dead state.

## Key evidence (verbatim)
- `src/Player.cpp:2400` `... & 0x10) != 0x0) { app->hud->addMessage((short)0, (short)217, 3);`
- `src/Player.cpp:2094` `if (!app->canvas->attemptMove(app->canvas->viewX + app->canvas->viewStepX, ...))`
- `src/Player.cpp:1036` `short* array2 = (this->isFamiliar && !b2 && n2 != 6) ? this->ammoCopy : this->ammo;`
- `src/Game.cpp:3219` `if (calcPosition[0] - app->canvas->saveX == 0 ^ calcPosition[1] - app->canvas->saveY == 0)`
- `src/CombatEntity.cpp:361-366` `damage = dmgStrMin * 7 / 8;` / `damage = dmgStrMin * 5 / 8;`
- `src/Player.cpp:1327` `this->numStatusEffectsCopy = (this->isFamiliar ? IS->readByte() : 0);`
- `src/ScriptThread.cpp:1746-1747` `if (this->app->player->isFamiliar) { this->app->player->familiarReturnsToPlayer(false);`
- map04 IP 602: `EVAL [ v16 == 1 ] ; iffalse -> loc_615` → `CALL_FUNC func_5560` (PLAYSOUND 1111 / MESSAGE str=87).

## Script opcode counts (listing lines)
| map | FORCE_BOT_RETURN | CHANGE_MAP | DESTROY_PLAYER | v16 |
|---|---|---|---|---|
| 00 | 0 | 1 | 0 | 77 |
| 01 | 0 | 2 | 0 | 56 |
| 02 | 0 | 2 | 4 | 61 |
| 03 | 0 | 2 | 0 | 50 |
| 04 | 0 | 2 | 0 | 57 |
| 05 | 1 (IP 2365) | 3 | 0 | 43 |
| 06 | 0 | 2 | 16 | 29 |
| 07 | 3 (IPs 4755, 4832, 5439) | 2 | 2 | 21 |
| 08 | 0 | 1 | 0 | 19 |
| 09 | 0 | 0 | 0 | 0 |

## Open questions
- Text of strings 87, 193, 222, 223 (file 0) and menu strings 355/356; help 12/13/16 not resolved.
- Not every CHANGE_MAP site was checked for a v16 guard (only map04 event[6] sampled).
- `playerFog*` not saved: effect of returning after a load-while-deployed not traced.
- `getFreeDropEnt` (`src/Game.cpp:2612-2622`) can reuse a linked drop slot when all 16 are linked; whether the body
  can be overwritten that way was not explored.
- `addHealth` while familiar has no upper clamp above 100 (only `stat == 100` refuses), reachable via `give(3,...)`.
