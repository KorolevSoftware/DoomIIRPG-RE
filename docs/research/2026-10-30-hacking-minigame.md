# 2026-10-30 — Hacking minigame (original)

## Hypothesis / questions
On map00 a hack terminal opens a loot door through a minigame. Questions: how the script
starts it and reads the result, the full rules, the exact drawing, the sounds, and whether a
sibling game (sentry bot) shares code.

## Method
- Read `src/HackingGame.{h,cpp}` in full, plus `src/ScriptThread.cpp:1290-1347` (EV_MINIGAME),
  `src/MiniGameManager.cpp:235-354` (help screen and IQ reward), `src/LoadingManager.cpp:81-110`,
  `src/Utils.cpp:111-150` (fixImage), `src/Button.cpp`, `src/Graphics.cpp` (drawRect/fillRect/drawCursor),
  `src/App.cpp:506-512` (RNG), `src/Canvas.cpp:448-480`, `src/InputEventController.cpp:333-349`,
  `src/TouchController.cpp`, and `src/SentryBotGame.cpp` for the sibling summary.
- Map scripts: `python3 tools/disasm_map_scripts.py tmp_map00.bin --verify` (0 failures), plus
  a run over map00..map09 extracted from the IPA, grepping `MINIGAME type=[02]`.
- Strings: decoded `strings.idx` + `strings00.bin` read-only (format `src/Resource.cpp:169-207`,
  `src/Text.cpp:179-212`), group 0 (Code), 3 (Menu), 4 (map00 = `loadMapStringID` 4+mapNameID-1,
  `src/LoadingManager.cpp:310`).
- BMP headers: hackerBG 480x320, blockGameColors 106x35 (8 bpp), gameHelpBG 480x320,
  sentryGame 185x124, matrixSkip_BG 480x320.

## Verdict
CONFIRMED. The curated result is in `docs/original-code/hacking-minigame.md`.

## Evidence (key quotes)
- `src/ScriptThread.cpp:1331-1339`: `else if (n115 == 0) { short n118 = n113; if (n116 == -1)
  initGame(this, n118); else initGame(this, n118, n116); }`. So args are (custom, columns, turns).
- `src/HackingGame.cpp:65`: `fillGameBoardRandomly(gameBoard, 5, columnCount, 3,
  (10000 * i2 + i2 * (100 - n2) * (100 - n2)) / 20000)`.
- `src/HackingGame.cpp:587`: `if (array[i][j] != 0 && array[i][j] != j + 1) return false;`.
- `src/HackingGame.cpp:237-247`: turns drop only when `columnBlockCameFrom != selectedColumn`.
  Then the solved check comes before the `turnsLeft == 0` check.
- `src/HackingGame.cpp:622-629`: `playSound((n == 1) ? 1043 : 1040, ...)`,
  `evaluateMiniGameResults(n)`, `scriptStateVars[7] = n`, `setState(ST_PLAYING)`, `callingThread->run()`.
- map00 disasm: event[147] tile (29,27) → `MINIGAME type=2 args=(0,3,15)` @7104 →
  `EVAL v7==1` → `NEXTSTATE ++v63`, `DIALOG 73`, `DOOROP sprite=132 UNLOCK`.
- Other hacking call sites: map04 (0,4,20), map05 (0,5,30), map06 (0,6,40).
  Sentry (type 0): map01, map02 x3, map03 x2 with botType 0, and map04/05/06/08 with botType 1.
- `src/Utils.cpp:116-150`: blockGameColors is widened 106→108 and the 54x18 block at x=54 is
  patched (pieces 4-6).
- No `srand` anywhere in `src/` (grep), so `std::rand` runs with its default seed.

## Open questions
- `v4 == 1` at map00 @7097 is taken to be the "Yes" answer of the type-1 dialog 72 (the hack
  runs only on that branch). The mapping of index to Yes/No label was not re-verified here
  (see `dialog-system.md`).
- The pixel content of the patched blockGameColors pieces was not inspected visually.
