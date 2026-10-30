# Hacking minigame (`src/HackingGame.{h,cpp}`)

Verified facts about the original "Hacker" minigame: script entry, the column-sort
puzzle, input, drawing, sounds and how the result goes back to the script. The game is
a sorting puzzle. 5 rows by 3..6 columns hold colored blocks, and the player moves one
block at a time until every column holds only its own color.
Raw investigation log: `docs/research/2026-10-30-hacking-minigame.md`.
Shared framework facts (help screen, `ST_MINI_GAME`, `endGame` tail) are also covered in
`docs/architecture/adr/0033-minigames-folder-and-interface.md` and `vending-machine.md`.

## 1. Entry: script `EV_MINIGAME` type 2

- Opcode `EV_MINIGAME` (53) reads 4 signed byte args `n111..n114`
  (`src/ScriptThread.cpp:1292-1295`). Type `n111 == 2` is hacking (`:1314-1347`):
  - `n112 == 1` is a "custom puzzle". It calls `app->Error("Custom Puzzles not supported
    on brew version ...")` (`:1317-1318`). No shipped map uses it.
  - `n112 == 0`: `n113` = column count. `n114 == -1` → `initGame(this, n113)`, which
    defaults turns to 30 (`src/HackingGame.cpp:53-55`). Otherwise
    `initGame(this, n113, n114)`, where `n114` = turns (`src/ScriptThread.cpp:1331-1339`).
  - `forceWin()` if `game->skipMinigames` (`:1341-1343`). This is dead in practice, see ADR 0033.
  - `unpauseTime = -1; n = 2` parks the thread (`:1344-1345`). The VM loop tail still
    writes `scriptStateVars[7] = 0` and advances IP (`src/ScriptThread.cpp:2040-2043`),
    so the thread later resumes at the next opcode.
- Shipped call sites (from `tools/disasm_map_scripts.py`): map00 `MINIGAME type=2
  args=(0,3,15)` @7104, map04 `(0,4,20)` @4313, map05 `(0,5,30)` @4285, map06 `(0,6,40)` @3054.
  So the args are `(custom=0, columns, turns)`.
- Main-menu entry `playFromMainMenu` (`src/HackingGame.cpp:29-34`): columns
  `nextInt()%3+3`, turns `nextInt()%(cols*10-15)+5`. This path is unreachable in the rewrite (ADR 0033).

### map00 call site (event[147], tile (29,27), TRIGGER facing W, entry IP 7074)

```
7074 EVAL v16==0          else CALL func_9586 (familiar: "no access" message 169)
7081 EVAL v63==0          else DIALOG 75 "Secure Storage already unlocked."
7088 PLAYSOUND 1065
7091 DIALOG 71 style 3    "Secure storage terminal locked down."
7094 DIALOG 72 type 1     "Do you want to try to hack this computer?"
7097 EVAL v4==1           (yes/no answer) else skip
7104 MINIGAME type=2 (0,3,15)
7109 EVAL v7==1           win?
7116   NEXTSTATE ++v63 ; DIALOG 73 "Secure storage access granted." ; DOOROP sprite=132 UNLOCK ; WAIT 300
7129   else DIALOG 74 "Secure storage access denied." ; WAIT 300
```
Strings come from map string group 4 (`loadMapStringID = 4 + mapNameID - 1`,
`src/LoadingManager.cpp:310`). Only `v7 == 1` unlocks. Lose (0) and quit (2) behave the
same for the script, and the terminal can be retried because v63 stays 0.

### initGame (`src/HackingGame.cpp:57-100`)
1. `columnCount = clamp(i, 3, 6)` (`:60`). `gamePlayedFromMainMenu = (thread == nullptr)` (`:62`).
2. `iq = getIQPercent()` (0 from the main menu) (`:64`).
   `getIQPercent = clamp(stats[7] - 100, 0, 100)` (`src/CombatEntity.cpp:62-64`).
3. `fillGameBoardRandomly(board, 5, columnCount, 3, scrambles)` with
   `scrambles = (10000*turns + turns*(100-iq)^2) / 20000` (integer) (`:65`).
   IQ 0 → `turns`. IQ 100 → `turns/2`. Example for 15 turns: IQ 0 → 15, IQ 50 → 9, IQ 100 → 7.
4. `setupGlobalData()` (`:36-51`): `confirmationCursor = 0`, `CORE_WIDTH = 35`,
   `CORE_HEIGHT = 100`, `setState(ST_MINI_GAME)`, `stateVars[0] = 2` (hacking selector),
   `stateVars[1] = 0` (**the help screen comes first**), `stateVars[2] = 0`, help scroll reset,
   `clearSoftKeys`, `selected = false`.
5. `turnsLeft = turns` (`:67`). This is not the scramble count.
6. Initial cursor: scan columns left→right and rows top→bottom (row 0 = top). The first
   non-empty cell becomes `(selectedRow, selectedColumn)`, which is the top block of the
   first non-empty column (`:68-80`).
7. The touch state is zeroed (`:82-86`). The buttons (`:93-99`) are
   `fmButton(0, 420,280, 60,40, snd 1027)` = **Exit** and `fmButton(1, 0,280, 60,40, snd 1027)` = **Back**.

## 2. Rules

### Board
- `short gameBoard[5][6]`, indexed `[row][col]`. Row 0 is the top and row 4 the bottom.
  Value 0 = empty, k = color k (1..6) (`src/HackingGame.h:28`).
- Solved state: **every non-empty cell in column j has value j+1**. All 5x6 cells are
  checked (`gameIsSolved`, `src/HackingGame.cpp:584-593`). Column order equals color order.

### Generation (`fillGameBoardRandomly` 5-arg, `src/HackingGame.cpp:133-180`; the 4-arg overload `:102-131` is unused)
1. Clear the board. Column k gets 3 blocks of color k+1 in rows 4,3,2 (`:140-144`).
2. `b = 0` (last destination). `mostRecentSrc[c] = mostRecentDest[c] = c` (`:145-149`).
3. Repeat until `scrambles` accepted moves are made (a rejected try does `--n5`):
   - `src = nextByte() / ((255+cols)/cols)`, then `dst` the same way, with 2 RNG draws per try
     (`:151-152`). The divisors are 86/64/52/43 for 3/4/5/6 columns, so values stay in 0..cols-1.
   - Reject if `src == dst`, or `src == b` (the source cannot be the previous destination; on the
     first try `b = 0`, so column 0 can never be the first source), or
     `mostRecentDest[src]==dst && mostRecentSrc[dst]==src` (an immediate reverse), or the
     source is empty (`board[4][src]==0`), or the dest is full (`board[0][dst]!=0`) (`:153-164`).
   - Otherwise move the top block of `src` onto the first empty slot (from the bottom) of `dst`.
     Then `mostRecentSrc[src]=dst; mostRecentDest[dst]=src; b=dst` (`:166-174`).
4. If the result is solved, regenerate from scratch (recursive call, `:177-179`).
- RNG: `nextByte() = nextInt() & 0xFF` and `nextInt() = std::rand() & INT32_MAX`
  (`src/App.cpp:506-512`). `srand` is never called in `src/` (grep), so the C runtime's
  default seed is used.

### Cursor movement (`attemptToMove(n)`, `src/HackingGame.cpp:303-344`)
- `c = (n + cols) % cols` (it wraps around). Nothing happens if `c == selectedColumn`.
- `n2` = the lowest empty row of column c: scan rows 4→0 and take the first empty one.
  It is -1 if the column is full.
- **Not carrying** (`!selected`): the cursor goes to `row = (n2 == 4) ? 4 : n2+1`, which is
  the top block, or row 0 if the column is full, or the empty bottom row 4 if the column is empty.
  `selectedColumn = c`.
- **Carrying** (`selected`): if c is full, keep stepping by `d = c - selectedColumn`
  (mod cols) until a column with space turns up or the walk comes back to the source column.
  If one is found, the block moves to `board[n2][thatColumn]`, the source cell is cleared and
  the cursor follows. If none is found, `selected = false` and the block stays.
  With keys `d = ±1`. A touch can give a larger `d`.

### FIRE in play (`stateVars[1]==1, stateVars[2]==0`, `src/HackingGame.cpp:234-255`)
- Not carrying: if the cursor cell is non-empty, `selected = true` and
  `columnBlockCameFrom = selectedColumn`. FIRE on an empty cell does nothing.
- Carrying: drop (`selected = false`). If `selectedColumn != columnBlockCameFrom`, then
  `--turnsLeft` (a drop back into the source column is free). Then **solved → `stateVars[2]=1`
  (won)**, else **`turnsLeft == 0` → `stateVars[2]=2` (lost)**. Both also call `clearSoftKeys`.
  The check runs on every drop.
- There is no real-time limit. The only limit is the turn count.

### Screens / state machine
`stateVars[1]`: 0 = help, 1 = game, 2 = "Still exit?" confirmation. `stateVars[2]` (inside
game): 0 = playing, 1 = won, 2 = lost.

| Screen | Key actions (`handleInput`, `src/HackingGame.cpp:182-301`) |
|---|---|
| help (0) | MENU or BACK → `endGame` by `stateVars[2]`: 0→`endGame(2)`, 1→`endGame(1)`, 2→`endGame(0)` (only 0 is reachable, so it is always quit). FIRE → game (`stateVars[1]=1`, clear softkeys). UP/DOWN scroll ∓1 line, LEFT/RIGHT ∓16 (`:185-216`) |
| game playing | LEFT/RIGHT → `soundClick` + `attemptToMove(col∓1)`. AUTOMAP or BACK → help (`stateVars[1]=0`, help scroll reset, clear softkeys). FIRE → pick/drop. MENU → confirmation (`stateVars[1]=2`) (`:218-262`) |
| game won | FIRE or AUTOMAP → `endGame(1)` (`:264-273`) |
| game lost | FIRE or AUTOMAP → `endGame(0)` (`:274-283`) |
| confirm (2) | FIRE: `confirmationCursor==1` (Yes) → `endGame(2)`, else back to game. LEFT/RIGHT toggle the cursor 0↔1. BACK → game (`:285-300`). `confirmationCursor` is reset only in `setupGlobalData`, so it persists between visits |

Action ids: `ACTION_MENU = 5`, `ACTION_AUTOMAP = 7`, `ACTION_BACK = 15` (`src/Enums.h:997-1005`).
Dispatch: `InputEventController` → `hackingGame->handleInput` when `stateVars[0]==2`
(`src/InputEventController.cpp:333-337`). UP/DOWN do nothing in the game.

### Touch (`src/HackingGame.cpp:638-688`, dispatched `src/TouchController.cpp:106-108,238-240,318-320`)
- `touchStart(x,y)`: highlight buttons, `touched=true`, `current = old = (x,y)`.
- `touchMove(x,y)`: highlight, `old = previous current`, `current = (x,y)`.
- `touchEnd(x,y)`: unhighlight all, `touched=false`, `current=(x,y)` (`old` is kept), then:
  - help: release on button 0 (Exit) → `handleInput(MENU)` (= quit). Anywhere else (button 1
    included): zero `current`/`old` and `handleInput(FIRE)` (start the game).
  - game: on the won/lost screen any release → `handleInput(FIRE)`. Otherwise button 0 →
    `MENU` (confirm screen) and button 1 → `BACK` (help).
  - confirm: nothing here. It is handled in draw, see below.
- Button hit test: inclusive rect `x..x+w, y..y+h`, list order (`src/Button.cpp:277-287`).
- **Column taps are resolved inside `drawGameScreen`** (`:382-400`), for every
  `stateVars[1] != 0` (the confirm screen included). For each column i < cols, with rect
  `(25+79i, 90, 35, 100)` (exclusive, `pointInRectangle` `src/Utils.cpp:52-54`):
  - if `touched` and `current` is inside: draw a white outline and play `soundClick` when
    `touchedColumn` changes (static, starts at -1);
  - else if `old` is inside: `touchedColumn=-1; attemptToMove(i); handleInput(FIRE); old=(0,0)`.
  The effect: a tap on a column (release, `old` = press point) moves the cursor or the carried
  block there and then fires (pick up / drop). Quirks: a drag that leaves a column fires on the
  first frame after it leaves (`old` = previous sample is still inside). A column tap on the
  confirm screen runs FIRE as the confirmation choice.
- Confirm-screen buttons are also resolved in draw (`:460-503`) from `current` while
  `!touched`: Yes rect `(142,240,45,25)` → `confirmationCursor=1; FIRE` (quit). No rect
  `(258,240,45,25)` → `confirmationCursor=0; FIRE` (back to game). `current` is zeroed after.

## 3. Exit (`endGame(n)`, `src/HackingGame.cpp:619-636`)
1. `playSound(n==1 ? 1043 : 1040, 0, 3, false)`.
2. `evaluateMiniGameResults(n)` (`src/MiniGameManager.cpp:235-248`): if n==1,
   `modifyStat(7, 2)` (IQ +2). If it returned >0, HUD message 236 "Hack Successful!|+%01 to I.Q."
   (arg = delta), else 237 "Hack Successful!|No Bonus.|I.Q. is at Max.", both with flag 3.
3. `scriptStateVars[7] = n`. **1 = won, 0 = lost (out of turns), 2 = quit**.
4. `setState(ST_PLAYING)`, `callingThread->run()`, `callingThread = nullptr`.

## 4. Drawing (`updateGame`, `src/HackingGame.cpp:346-357`)
`clipRect(0,0,480,320)`. Help when `stateVars[1]==0`, else `drawGameScreen`. Canvas clears
`REPAINT_HUD` for this selector (`src/Canvas.cpp:452-454`). Softkeys are never drawn
(`drawSoftKeys` is commented out, `src/Canvas.cpp:477-480`), so the `setLeft/RightSoftKey` calls
have no visible effect.
Anchors (J2ME): HCENTER 1, VCENTER 2, LEFT 4, RIGHT 8, TOP 16 (`docs/original-code/ui.md` §1).
`drawRect(x,y,w,h)` draws a (w+1)x(h+1) outline (`src/Graphics.cpp:236-241`). `fillRect`
ignores alpha, so it is always opaque (`src/Graphics.cpp:88-95`).

### Assets (`src/LoadingManager.cpp:85-87,105,108`)
- `hackerBG.bmp` 480x320 (`imgEnergyCore`): full-screen background.
- `blockGameColors.bmp` 106x35 shipped. `fixImage` widens it to 108 and overwrites the
  54x18 block at x=54..107, rows 0..17 (pieces 4..6) with hard-coded pixels
  (`src/Utils.cpp:116-150`). Layout: **piece k (1..6) = src (18k-18, 0, 18, 18)**
  (`drawPiece`, `:595-597`). **Column marker j = src (12j, 18, 12, 12)** (`:403`).
- `gameHelpBG.bmp` 480x320, the shared help background.

### Help screen (`drawHelpScreen`, `:359-373` → `src/MiniGameManager.cpp:255-322`)
`gameHelpBG` at (0,0). Title Code 164 "Hacker" dehyphenated at (W/2, top+25) anchor 3. Body
Code 165, wrapped at 49 chars, 16 visible lines from the scroll position at (left+18, top+45)
anchor 20. Scrollbar at (right-18, top+45, h 256). Code 96 "Tap Screen to Continue" at
(W/2, bottom-20) anchor 3. Then "Exit" (Code 30) at (445,302) anchor 3, with `fontRenderMode 2`
(dimmed) unless button 0 is highlighted.

### Game screen (`drawGameScreen`, `:375-510`), in draw order
1. `hackerBG` at (0,0).
2. Column hover outlines / tap resolution (§2 Touch). Outline `drawRect(25+79i, 90, 35, 100, 0xFFFFFFFF)`.
3. Column markers: `drawRegion(colors, 12j,18,12,12 → 37+79j, 195)` for j < cols (`:402-404`).
4. "Turns:" (Code 167) dehyphenated + `turnsLeft` at (148,28) anchor 1 (`:406-411`).
5. `drawGoalTextAndBars` (`:512-551`): "Goal" (Code 168) at (335,38) anchor 1. Bars
   `fillRect(n + 9i, 22, 5, 15, color_i)` with `n = 335 - (5 + 9*(cols-1))/2`. Colors:
   0 `0xFFA9E94E`, 1 `0xFFE77313`, 2 `0xFFFD1100`, 3 `0xFFC450FF`, 4 `0xFFFFF395`, 5 `0xFF101ADE`.
6. If `stateVars[1]==1` (`:416-453`): prompt Code 166 (playing), 169 (won) or 170 (lost),
   wrapped at 47, at (12,215) anchor 4. "Back" (Menu 80) at (35,302) anchor 3 and "Exit"
   (Code 30) at (445,302) anchor 3, each dimmed (`fontRenderMode 2`) unless its button is highlighted.
7. Else, the confirm screen (`:454-506`): Code 189 "Progress will be lost. Still exit?" wrapped at
   47 at (75,215) anchor 4. Yes box `fillRect(142,240,45,25)` gray `0x808080`, or `0x7878FF`
   while touched over it, plus a white `drawRect` outline. Label Menu 197 "Yes" at (150,245)
   anchor 4. No box at (258,240,45,25), label Menu 198 "No " at (270,245) anchor 4. Cursor glyph
   `0x8A` (`drawCursor`, `src/Graphics.cpp:674-686`) at
   `x = (cursor==1 ? 136 : 252) + OSC_CYCLE[time/100 % 4]`, y 245, anchor 8 (RIGHT).
   `OSC_CYCLE = {-1,0,1,0}` (`docs/original-code/ui.md` §12.4).
8. `drawGamePieces(34, 94)` (`:553-582`): piece at `(34 + 79j, 94 + 19i)`, 18x18. The selection
   box is at the cursor cell: `drawRect(x, y, 18, 18, c)`, plus an inner
   `drawRect(x+1, y+1, 16, 16, c)` when carrying. The gray level pulses with
   `t = time & 0x3FF`: `g = t<511 ? 160 + (95t>>9) : 255 - (95(t-511)>>9)`, `c = 0xFF000000|g*0x010101`.
   That is a 1024 ms period between gray 160 and 255.
   The pieces are drawn on top of everything, the confirm screen included.

Text strings (strings00.bin, file 0 = Code, file 3 = Menu): 30 "Exit", 96 "Tap Screen to
Continue", 164 "Hacker", 165 "Sort the squares according to their color onto the
corresponding color column.||Tap a column to pick up or drop a square.", 166 "To
select/deselect a square, tap the square. Tap column to move square to that column.",
167 "Turns:", 168 "Goal", 169 "Memory columns restored! Tap screen to continue.", 170 "Memory
column restore failed! Tap screen to continue.", 189 "Progress will be lost. Still exit?",
236/237 (IQ messages above). Menu 80 "Back", 197 "Yes", 198 "No ".

## 5. Sounds (ids only)
| id | when | call |
|---|---|---|
| 1027 | `soundClick` on LEFT/RIGHT in play, and on touch-hover of a new column | `playSound(1027,0,5,false)` (`src/MenuSystem.cpp:5874-5876`; `src/HackingGame.cpp:221,225,389`) |
| 1027 | button 0/1 highlight transition (if not already playing) | `src/Button.cpp:145-155`, `src/HackingGame.cpp:95,98` |
| 1043 / 1040 | `endGame` win / other | `src/HackingGame.cpp:622` |
| 1065 | map00 script, before dialog 71 | disasm @7088 |
There is no sound for pick-up or drop.

## 6. Sibling: sentry bot "matrix skip" (`src/SentryBotGame.{h,cpp}`), brief
Full details (supersede this summary): `docs/original-code/sentry-bot-minigame.md`.
- Entry: `EV_MINIGAME` type 0, `n112` = botType (0 or 1, otherwise 0) (`src/ScriptThread.cpp:1296-1303`,
  `src/SentryBotGame.cpp:44-52`). Used on map01-03 `(0,-1,-1)` and map04/05/06/08 `(1,-1,-1)`.
- Same skeleton: `setupGlobalData` sets `stateVars[0]=0`, `[1]=0` (help first) and `[2]=0`
  (`:33-42`). Help → game → result. `endGame` is identical, with 1043/1040, IQ +2,
  `vars[7]=n` and thread run (`:806-824`).
- Puzzle: `skipCode = nextInt() % n4 + 1`, with `n4 = (20000 + 4*(100-iq)^2)/10000` (2..6), never
  equal to `player->lastSkipCode`. A 16-cell board of values 1..6 (cells equal to skipCode are
  re-rolled if IQ >= 50). `board[0] = skipCode%6+1`. The solution is the 4 values found by
  stepping by skipCode mod 16 from cursor -1, skipping cells equal to skipCode (`:53-81`). The player
  moves a cursor over the 16 cells and enters 4 values, then submits. All 4 must match (`:193-226`, `:756-763`).
- Win screen: choose 1 of 2 bots (`bot_selection_cursor`). `awardSentryBot(botType==0 ? 3|4 :
  5|6)` discards the current familiar and gives weapon n (`:784-804`).
- Assets: `matrixSkip_BG.bmp` 480x320, `sentryGame.bmp` 185x124, `matrixSkip_grid_pressed{,_2,_3}.bmp`,
  `matrixSkip_button.bmp` (`src/LoadingManager.cpp:88-92`, `src/SentryBotGame.cpp:89-91`).
  Buttons: Exit (428,290,54,30), Back (0,290,54,30), submit/delete image buttons at x 368 / 8 (`:96-115`).
- Shared code with hacking: only the `MiniGameManager` help screen/scroll and
  `evaluateMiniGameResults`, plus the `ST_MINI_GAME` dispatch. There are no shared rule or draw helpers.
