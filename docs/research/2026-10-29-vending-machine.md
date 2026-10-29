# 2026-10-29 — Vending machine minigame

## Question
How does the original vending machine minigame work? Cover the script entry (EV_MINIGAME
type 4), the state machine and puzzle, prices and credits, rendering, input, the exit back to
the script, the interplay with menus 83-88, and a test location.

## Method
- Read all of `src/VendingMachine.cpp` (992 lines) and `.h`. Read the call sites:
  `src/ScriptThread.cpp:1290-1354`, `src/Canvas.cpp:450-466`, `src/InputEventController.cpp:334-349`,
  `src/TouchController.cpp:106-117,316-326`, `src/Player.cpp:506-518,1294-1296,1377-1379,2451-2483`,
  `src/LoadingManager.cpp:85-110`, `src/MiniGameManager.cpp:250-345`, and the menu hooks found with
  `grep -a` in `src/MenuSystem.cpp` (an ISO-8859 file that plain grep skips).
- Decoded tables.bin table 10 (energyDrinkData) with a scratch script that follows
  `src/Resource.cpp:209-222,236-248,295-310`: 20 LE int offsets, the table at 80+offs[9],
  an int count, then int16s.
- Read the English strings from `strings.idx`/`strings00.bin` (group 0 at 0x0..0x24FE, group 3 at
  0x4DA2..0x628B, NUL-separated, `src/Text.cpp:179-212`).
- Ran `tools/disasm_map_scripts.py` on map00..map09 extracted from the IPA and grepped for `MINIGAME type=4`.

## Verdict
CONFIRMED / fully mapped. Curated result: `docs/original-code/vending-machine.md`.

## Key findings
- The machine id is `(loadMapID-1)*2 + arg1`, clamped to 1..18 (`src/ScriptThread.cpp:1306`,
  `src/VendingMachine.cpp:53-54`). It is saved as a hack bitmask plus 3-bit try counters
  (init 4) in two ints (`src/Player.cpp:509-518,2451-2483`).
- The first screen is the main terminal (`stateVars[1]=2`), not the help screen (`src/VendingMachine.cpp:46`).
  From the main menu the first screen is help (`:35`).
- Puzzle: 4 digits, each `rand()%9` (0..8), sliders 0..9 starting at 5, goal sum shown. The IQ
  percent pre-solves 0..3 digits (thresholds 20/50/80) by forcing them to 5 (`:136-170`).
  One FIRE submits all 4 digits, because `drawGameScreen` refreshes `playersGuess` every frame (`:637-639`).
  Higher/lower hints come from min/max chevrons (`:377-391,774-785`).
- Win: +2 IQ (`modifyStat(7,2)`, cap 200) and hacked prices. Fail: one persisted try is lost
  (`:334-348`).
- Prices (normal/hacked): 20/15, 25/20, 30/20, 50/40, 70/60, 80/70, 100/80, 100/80, 200/150,
  400/300, 666/400. Snacks cost 10/8. Credits = `inventory[24]`.
- Exit: `scriptStateVars[7] = 2` normally (1 only for MENU on the win screen), then
  `setState(ST_PLAYING)` and `callingThread->run()` (`:850-859`). `EV_ENDMINIGAME` is a no-op.
- Test location: map00 EVT 166 tile (27,29), or map01 EVT 63 tile (7,18) next to the map01 spawn (5,23).

## Quirks worth preserving or noting
- MENU on the win screen exits with result 1 but never calls `setVendingMachineHack`, so the hack
  is not persisted. The IQ bonus has already been granted (`:312-314` vs `:300-310`).
- The top bar is labelled "Current Sum", but box 5 shows the target `correctSum` (`:695,707-708`).
- The help text promises "3 additional chances", which is consistent with 4 total tries.
- `drawMainScreen` fires `handleInput(BACK)` from the draw path when the last press lay on the
  Exit softkey and the finger is up (`:583-586`).
- `handleInputForBasicVendingMachine` has two near-identical branches. They differ only in what
  cursor 1 does: Hack vs No (`:191-224`).

## Open questions
- `fmButton` render details for arrow buttons with `SetImage(...,false)` were not traced
  (`src/Button.cpp:161+`).
- Drink names come from `entityDefManager->find(6,0,n)`. The snack list is in
  `fillVendingMachineSnacks`. Both are left to the menu researcher.
