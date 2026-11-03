# 2026-10-30 Familiar (sentry-bot control): presentation and gameplay integration

## Hypothesis / questions
How does the original render, HUD, combat, movement, input, scripts and sounds behave while `isFamiliar`?
(Lifecycle is in `docs/original-code/familiar-core.md`, by a parallel researcher.)

## Method
- grep `isFamiliar|familiarType|postProcessMode` across `src/`; read each site: `src/Render.cpp:2259-2290,2592-2705`,
  `src/Graphics.cpp:104-229,308-367`, `src/Hud.cpp:233-245,555-710,1235-1264`, `src/TouchController.cpp:538-548`,
  `src/SoftKeyController.cpp:77-99`, `src/Combat.cpp:195-330,611-850,1015-1040`, `src/CombatEntity.cpp:335-375`,
  `src/MovementController.cpp:197-211,312-358`, `src/PlayingInputHandler.cpp:60-215,395-500`,
  `src/InputEventController.cpp:70-92`, `src/MenuSystem.cpp:1540-1566,3256-3259`, `src/Game.cpp:1215-1235,2725-2767,
  3195-3225,3461-3471`, `src/Entity.cpp:135,152-240,395-415,1370-1395`, `src/Player.cpp:128-145,415-450,625-712,
  962-1063,2080-2518`, `src/ScriptThread.cpp:1744-1750,1925-1946`, `src/GameStateRunner.cpp:163-177,283-322`.
- Image sizes from BMP headers in `UnPackGameData/` (Hud_Sentry 29x34, HUD_sentry_active 194x38,
  HUD_Panel_top_sentrybot 480x20, damage_bot 16x16).
- Strings: `strings00.bin` split on NUL (calibrated 30 Exit, 52 Menu, 96, 164).
- Weapon rows: `tables.bin` table 2 (recipe in combat.md §10.2): w0 (8,10,0,5,1,1,0,2,50), w3 (12,15,0,5,7,0,0,1,35),
  w4 (100,120,0,4,7,0,-1,1,30), w5 (20,30,0,5,7,0,0,1,35), w6 (-116,-101 -> 140..155,0,4,7,0,-1,1,30).
- Sounds: `src/Sounds.h` list, id - 1000 (`src/Sound.cpp:180`).
- Scripts: `tools/disasm_map_scripts.py` on map00-09 (scratchpad), grep v16 / FORCE_BOT_RETURN.
- new_src: grep familiar/sentry/vars[16].

## Verdict
CONFIRMED with details. Curated: `docs/original-code/familiar-presentation.md`.

## Key evidence
- Tint: `src/Render.cpp:2650-2655` `if ((uint16_t)(app->player->familiarType - 1) > 1u) FMGL_fillRect(0,0,480,256,1,0,0,alpha) else (0,1,0,alpha)`;
  `alpha = sin(update) * 0.05 + 0.35`, `update += 0.05f` per frame (`:2647-2649`); scanlines `:2656-2658`
  `fillRect(0, i + (y / 3), 480, 5, 0,0,0, 0.15)` for i in 0..250 step 10, `y = (y+1) % 30`.
- Only called when `postProcessMode != 0` (`src/Canvas.cpp:1351-1352`, `src/MayaCamera.cpp:319-320`).
- Eye bob `src/Render.cpp:2271-2273` `viewZ += (sinTable[time >> 2 & 0x3FF] >> 11) + 128`.
- Bottom HUD `src/Hud.cpp:644-680`: colours 0xFF59A907/0xFF96DA0E/0xFFEDD703/0xFFFF8B00/0xFFFF0C00 at >80/>60/>40/>20;
  face `drawRegion(.., 0, type3/4 ? 17 : 0, 29, 17, SCR_CX - 66, 278, 3)`; bar `fillRect(n5 + 6i, 271, 4, 14)`.
- Soft keys `src/SoftKeyController.cpp:91-92` `setSoftKeys(0,52,0,216)` = "Menu"/"Return"; holding a bot 220 "Discard".
- View weapon `src/Combat.cpp:695-699` exploding returns, shooting `weapon = 0`; `:719-724` idle (1,50); `:790-795` `y -= 35`.
- Bot damage `src/CombatEntity.cpp:361-366` 7/8 or 5/8; defense applied after (`:372`).
- Monster target is ET_PLAYER at destX/destY (`src/Entity.cpp:1378-1381`).
- Movement `src/MovementController.cpp:322-325` message 222 "No Bot Access".
- Door FIRE `src/PlayingInputHandler.cpp:455-457` sound 1111 + message 193 "Sentry Bot cannot open doors.".
- Touch `src/Hud.cpp:1255-1263`: button 1 -> ACTION_BOT_DISCARD, 3 -> ACTION_PASSTURN, 0 -> ACTION_MENU.
- Menu `src/MenuSystem.cpp:1551-1557` insert RETURN_TO_PLAYER (355/356), disable items 0 and 6.
- Scripts: v16 EVAL forms over map00-08: 262 `v16 == 0`, 142 `v16 == 1`, 9 compound; FORCE_BOT_RETURN at map05 2365,
  map07 4755/4832/5439. Refusal helper pattern map00 func_9586: `v16==0 ? (v14==1 ? 1073 : 1072) : 1111`, then MESSAGE.
- Sounds 1108 activate, 1109 death, 1111 pain, 1112 return, 1032 explosion, 1086 pistol (bot shot,
  `src/Combat.cpp:261-265`); 1110 SentryBot_discard has no caller (grep `1110` in src/*.cpp: none).

## Open questions
- Per-map script MESSAGE string ids (e.g. 169 in func_9586) are map-file strings, not resolved here.
- Body copy def 72/68/66 eType (expected NPC 3); talking/firing at the body was not traced.
- Whether the 256-px tint height leaves the 3D view's lower rows uncovered depends on the GL viewport (rendering.md §6.1); not checked.
