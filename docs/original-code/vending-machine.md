# Vending machine minigame (`src/VendingMachine.{h,cpp}`)

Verified facts about the original vending machine: script entry, terminal / help / hack
screens, the 4-digit hack puzzle, prices, how each machine's state is saved, exit back to
the script. The buy menus 83-88 are only described at the hand-off level (§7).
Note: `MenuSystem.cpp` is ISO-8859, so plain `grep` skips it. Use `grep -a`.

## 1. Entry (script `EV_MINIGAME` type 4)

- Opcode `EV_MINIGAME = 53`, `EV_ENDMINIGAME = 54` (`src/Enums.h:455-456`).
- Decode: 4 signed byte args `n111 n112 n113 n114` (`src/ScriptThread.cpp:1292-1295`).
  For type 4: `vendingMachine->initGame(this, loadMapID, (loadMapID - 1) * 2 + n112)`,
  `forceWin()` if `game->skipMinigames`, `unpauseTime = -1; n = 2` (the thread suspends)
  (`src/ScriptThread.cpp:1305-1313`). `n112` is the machine slot within the map (1 or 2).
  `n113`/`n114` are unused for type 4 (scripts pass -1).
- So the global machine id is `(loadMapID-1)*2 + slot`, clamped to 1..18. The map number
  is clamped to 1..9 (`src/VendingMachine.cpp:53-54`). `loadMapID` 1 = `map00.bin`
  (`src/Resource.h:19`, `src/LoadingManager.cpp:362`).
- After the switch, `b` stays true, so `EV_MINIGAME` itself writes `scriptStateVars[7] = 0`.
  It also advances IP before the loop exits on `n == 2` (`src/ScriptThread.cpp:238,2040-2043`).
  The thread therefore resumes at the next opcode.
- `EV_ENDMINIGAME` does nothing ("No implementado") (`src/ScriptThread.cpp:1351-1354`).
- Main-menu entry: `ACTION_MINIGAME` with item param 4 calls `playFromMainMenu()`
  (`src/MenuSystem.cpp:3217-3220`). That function runs `initGame(nullptr,1,1)` and then
  sets `stateVars[1]=0` (the help screen) (`src/VendingMachine.cpp:33-37`).
  `gamePlayedFromMainMenu = (callingThread == nullptr)` (`:43`).

### initGame (`src/VendingMachine.cpp:39-116`)
- `canvas->setState(ST_MINI_GAME)` sets `stateVars[0]=4` (the minigame selector, see
  `src/Canvas.cpp:450-466`, `src/InputEventController.cpp:334-349`). It also sets
  `stateVars[1]=2` (the **main terminal screen** is the first screen), `[2]=0`, `[4]=0`,
  `[3]=app->time` (the chevron timer) (`:44-49`).
- `triesLeft = 4` from the main menu, otherwise `player->getVendingMachineTriesLeft(id)`.
  `machineHasBeenHacked = player->vendingMachineIsHacked(id)` (`:55-56`).
- `randomizeGame()` runs only if `machineCanBeHacked()`, meaning `!hacked && triesLeft>0`
  (`:58-60`, `:132-134`).
- `widthOfContainingBox = ((W - 97) / 4) + 1`, where `W = screenRect[2]-screenRect[0]`.
  W=480 gives 96 (`:66-73`).
- It builds `m_vendingButtons` (`:79-111`):

| id | rect | image / pressed |
|---|---|---|
| 0 (submit) | `(400, 160 - h/2, w, h)` of `vending_submit.bmp` | `vending_submit.bmp` / `vending_submit_pressed.bmp`, normalRenderMode 12, highlight 0 |
| 1..8 | rect set every frame in `drawGameMiddleBar` | odd = `vending_arrow_up(_pressed).bmp`, even = `vending_arrow_down(_pressed).bmp` |
| 9 (back) | `(0, 290, 54, 30)` | none |
| 10 (exit) | `(428, 290, 54, 30)` | none |

## 2. Per-machine saved state (Player)

- `hackedVendingMachines`: a bitmask with bit `id` (`src/Player.cpp:2451-2457`).
- Tries: two packed ints, `vendingMachineHackTriesLeft1` for ids 1..9 and `...2` for ids
  10..18. Each machine has a 3-bit field at bit `3*(k-1)`. Every field starts at **4**
  (`src/Player.cpp:509-518`, `:2459-2483`). `removeOneVendingMachineTry` subtracts
  `1<<(3*(k-1))` if the field is >0.
- All three ints are saved and loaded (`src/Player.cpp:1294-1296`, `:1377-1379`).
  New game: `hackedVendingMachines = 0` (`:506`).
- Persisted tries drop **only on a failed submit** (`src/VendingMachine.cpp:344-346`).
  The hack bit is set only on FIRE from the win-result screen (`:300-310`).

## 3. State machine

`stateVars[1]` selects the screen: 0 = help, 1 = hack game, 2 = main terminal
(`src/VendingMachine.cpp:172-187`, `:403-416`). Inside the game, `stateVars[2]` is
0 = editing, 1 = won result, 2 = wrong / failed result.

### Puzzle setup `randomizeGame` (`:136-170`)
- `solution[i] = app->nextInt() % 9`, which gives **0..8** (`nextInt = std::rand() & INT32_MAX`,
  `src/App.cpp:506-508`). `sliderPositions[i] = 5`. `correctSum = Σ solution`.
- `playersGuess = 5`, `minimums = 0`, `maximums = 9`, `chevronAnimationOffset = 0`.
- IQ assist: `n = ce->getIQPercent()` (0 from the main menu). `k = 3` if n>=80, 2 if >=50,
  1 if >=20, else 0. `while (k > numbersCorrect()) { i = nextInt()%4; correctSum -=
  solution[i]-5; solution[i]=5; }`. This pre-solves k digits at the starting slider value 5.

### Submit (game, `stateVars[2]==0`, FIRE) (`:324-349`)
- `playersGuess[gameCursor] = slider`, then the cursor moves past the non-(-1) guesses.
  `drawGameScreen` copies `sliders → playersGuess` every frame (`:637-639`), so no guess is
  -1 and **one FIRE submits all 4 digits**.
- Then: `updateHighLowState()` (for each column, slider>sol: `max=min(slider-1,max)`;
  slider<sol: `min=max(slider+1,min)`; equal: `min=max=slider`) (`:377-391`), `--triesLeft`.
- Win (`numbersCorrect()==4`): sound 1043, `stateVars[2]=1`, `machineHasBeenHacked=true`.
  If not from the main menu, `iqBonusAwarded = player->modifyStat(7, 2)` (stat 7 = IQ,
  capped at 200, `src/Player.cpp:236-238`; `MINIGAME_IQ_BONUS = 2`, `src/Enums.h:1350`).
- Fail: sound 1040, `removeOneVendingMachineTry(id)` (world only), `stateVars[2]=2`.
- `forceWin()` (used for skipMinigames) only sets `slider = solution` and runs
  `updateHighLowState`. The player still has to submit (`:843-848`).

### Input per state
| state | action → effect |
|---|---|
| main (2), hackable (`:191-208`) | MENU/BACK → `endGame(2)`. LEFT/RIGHT → cursor ^=1. FIRE: cursor 0 → `setMenu(MENU_VENDING_MACHINE)` (83). Cursor 1 → help screen (`stateVars[1]=0`, init help scroll, clear softkeys) |
| main (2), not hackable (`:210-224`) | same, except FIRE with cursor 1 → `endGame(2)` ("No") |
| help (0) (`:227-260`) | AUTOMAP → `endGame(2)`. MENU/BACK → main screen, or `endGame(2)` from the main menu. FIRE → game (`stateVars[1]=1`). UP/DOWN scroll ∓1 line, LEFT/RIGHT ∓16 |
| game editing (`:316-374`) | MENU → `endGame(2)`. AUTOMAP/BACK → help. FIRE → submit. UP/DOWN → slider of `gameCursor2` ±1 in 0..9 (+click). LEFT/RIGHT → `gameCursor2` ∓1 mod 4 (+click) |
| game won (2==1) (`:299-315`) | FIRE → main screen with `machineJustHacked=true`, `setVendingMachineHack(id)` (main menu: `endGame(1)`). MENU → `endGame(1)` **without** persisting the hack bit |
| game wrong (2==2) (`:264-298`) | FIRE: tries==0 → main screen (main menu: `endGame(2)`). Otherwise wrong guesses become -1, the cursor goes to the first wrong one, `stateVars[2]=0`. MENU → `endGame(2)`. AUTOMAP/BACK → help (if tries>0) |

Action values: UP 1, DOWN 2, LEFT 3, RIGHT 4, MENU 5, FIRE 6, AUTOMAP 7, BACK 15
(`src/Enums.h:993-1005`).
Touch (`:866-992`): help: button 10 → BACK, otherwise FIRE. Game: on a result screen any
tap → FIRE. Otherwise 0 → FIRE, 9 → BACK, 10 → MENU, 1..8 → slider (id+1)/2-1 up (odd) or
down (even). Dragging inside a column (`x+5..x+27`, `y+104..y+214`) sets
`slider = (y+214-py)/11` (`:754-768`). Main: 9 → BACK, 10 → MENU, otherwise FIRE.

## 4. Rendering (`updateGame`, `:393-419`)
Every frame it clips to the screen. Every >100 ms, `chevronAnimationOffset[i] = (+1) % 11`.
It then draws by state and sets `staleView = true`.
Assets (`src/LoadingManager.cpp:93-105`): `vendingGame.bmp` (atlas), `vending_BG.bmp`
(game bg), `vendingBG.bmp` (terminal bg, also used by menus 83-88), `vending_submit(_pressed).bmp`,
`vending_arrow_{up,down}(_pressed).bmp`, `vending_button_small.bmp`. The help bg is
`gameHelpBG.bmp`, shared with the other minigames (`:87,105`). Softkey:
`vending_softkey_{normal,pressed}.bmp` (`src/Hud.cpp:55-56`). `fixImage` patches
`vending_arrow_down` (`src/LoadingManager.cpp:109`).

Strings (file 0 = CodeStrings, file 3 = MenuStrings, `src/MenuStrings.h:20,23`; English
text read from `strings00.bin`): 30 "Exit", 96 "Tap Screen to Continue", 140 "No",
141 "Yes", 195 "Power-Up", 196 help body, 197 "Credits: %01", 198 "What would you like to do?",
199 "Would you like to buy?", 200 "Code accepted! Prices have dropped! ",
201 "Hack failed!|Initiating security protocols. ", 202 "Buy", 203 "Hack",
207 "Press "OK" to Submit", 208 "Tap screen to Continue.", 209 "Wrong code!|Tries left: ",
236 "Hack Successful!|+%01 to I.Q.", 237 "...No Bonus.|I.Q. is at Max.", 239 "Hack Successful!",
240 "Hack Failed!"; MenuStrings 80 "Back", 344 "Vending Machine".

### Main terminal (`drawMainScreen`, `:469-600`)
1. `vendingBG.bmp` at (0,0) (`:605`). The rest of `drawVendingMachineBackground` is empty loops.
2. Title Menu 344, dehyphenated, `buildDivider(len+10)`, at (SCR_CX+1, 14) with anchor 1 (`:477-480`).
3. Credits Code 197 with arg `inventory[24]`, at (SCR_CX+1, 30) with anchor 17 (`:482-489`).
4. Prompt: 198 if hackable, else 200+199 if just hacked, else 199. Wrapped at 26, drawn at (240,60) with anchor 17 (`:490-503`).
5. Two `vending_button_small.bmp` (h2 = img.h/2) at x=112 and x=w+135, top `posY-h2`,
   where `posY = 16*promptLines + 95`. renderMode 0 while touched, otherwise 1. Labels: hackable 202/203,
   otherwise 141/140, at the button centre with anchor 3 (`:505-563`).
6. Cursor: `drawCursor(labelLeft-5 + OSC_CYCLE[(time/100)%4], posY-8, 24)` (`:543,565-569`).
7. Exit softkey image at (362, 297-h/2), label Code 30 at its centre (`:571-593`).
   Quirk: if `currentPress` lies inside it while untouched, it calls `handleInput(BACK)` (`:583-586`).
8. Click sound when the cursor changed (`:597-599`).

### Help (`drawHelpScreen`, `:620-632`, `src/MiniGameManager.cpp:254-325`)
`gameHelpBG.bmp` at (0,0). Title 195 at (W/2, top+25) with anchor 3. Body 196 wrapped at 49,
16 visible lines, at (left+18, top+45) with anchor 20, plus a scrollbar at (right-18, top+45, 256).
Code 96 at (W/2, bottom-20). "Exit" (Code 30) at (445,302) with anchor 3, fontRenderMode 2 unless
button 10 is highlighted.

### Hack game (`drawGameScreen`, `:634-686`)
1. `vending_BG.bmp` at (0,0).
2. Top bar (`:688-731`): literal "Current Sum" at (240,9). Then 5 boxes from x=left+32 with step
   `widthOfContainingBox` (96): `drawRegion(vendingGame, 38,37,33,29, x, top+17)`. The value
   sits at (x+14, top+31): boxes 0..3 show `playersGuess` (blank if -1) and box 4 shows `correctSum`.
   "+" (i=1..3) / "=" (i=4) at `x - (W96-33)/2 - 1`. Literals "Hack Goal Sum =" at (200,280) and
   `correctSum` at (300,280).
3. Middle bar (`:733-803`), per column i (x from left+32, step 96): touch rects for the arrow buttons
   (up at y=top+104-h+5, down at top+213, using the submit image's size). Then `minimums[i]`
   chevrons from (x+5, top+203) going up (`drawRegion(vendingGame, 22, 97-anim, 22,11, ..., 20, 6, 0)`),
   and `9-maximums[i]` chevrons from top+104 going down (rotate 0). Slider box 21x10 at
   (x+5, Y-5) with `Y = top+109 + (9-slider)*99/9`. On `gameCursor2` it is a double rect in a pulsing
   yellow, `I = 187 + |1000 - time%2000|*68/1000`, colour `0xFF000000|I<<16|I<<8`. Otherwise a white
   rect. The digit is at (x+15, Y+1).
4. If `stateVars[2]!=0`: `drawGameResults` (`:421-467`) builds the text (hacked: 239 from the main
   menu, else 236 with arg / 237; tries>0: 209 + tries; else 240 from the main menu / 201), wraps it
   at 20, and draws a black box with a white border at SCR_CX/CY (pad 6/4, 16 px per line).
   The prompt is 208 and the left softkey (Menu 80) appears only if tries>0. Otherwise the prompt is 207
   and the left softkey is set. The right softkey is always Code 30 (`:647-658`).
   The prompt goes at (W/2, bottom-12).
5. "Exit" at (445,302) and "Back" at (35,302), dimmed (fontRenderMode 2) unless the button is
   highlighted. Then `m_vendingButtons->Render`, then the literal "OK" at (submit.w/2+398, 160) (`:663-685`).

## 5. Exit (`endGame(n)`, `:850-864`)
World: `game->scriptStateVars[7] = n`, `setState(ST_PLAYING)`, `callingThread->run()`
(resumes after `EV_MINIGAME`), `callingThread = nullptr`. Main menu: sound 1071,
`setMenu(MENU_MAIN_MINIGAME)` (18). Result codes: **2** for every normal exit, **1** only for MENU
on the win screen. map00's script ignores the result (`MINIGAME` → `JUMP` → `RETURN`).

## 6. Prices and credits
- Credits = `player->inventory[24]` (`INV_ONE_UAC_CREDIT = 24`, `src/Enums.h:228`).
- `energyDrinkData` = tables.bin table 10, `int16` (`src/App.cpp:383,399`). For each of the
  11 drinks there are 3 shorts: `[price, hackedPrice, mapMask]`.
  `getDrinkPrice(n) = data[n*3 + hacked]` and `drinkInThisVendingMachine(n) = data[n*3+2] has bit currentMapNumber`
  (`src/VendingMachine.cpp:805-811`). Shipped values (normal/hacked) for drinks 0..10:
  20/15, 25/20, 30/20, 50/40, 70/60, 80/70, 100/80, 100/80, 200/150, 400/300, 666/400.
  mapMask is 0x3FE (maps 1..9) for every drink.
- Snack price: 10, or 8 when hacked (`:825-827`).
- `buyDrink(n)`: fails if `credits < price*qty`. Otherwise for each unit it does
  `credits -= price; give(0, n, 1, false)` (`:813-823`).

## 7. Hand-off to buy menus 83-88 (internals: see menu research)
- Opener: main-terminal FIRE on "Buy" calls `setMenu(83)` (`src/VendingMachine.cpp:201,219`).
- In menu 83, MENU/BACK calls `returnFromBuying()` (`src/MenuSystem.cpp:2927-2928,2947-2948`).
  That re-enters `ST_MINI_GAME` main screen. If the machine can no longer be hacked it calls
  `endGame(2)` at once, so the player leaves the machine (`src/VendingMachine.cpp:118-130`).
- 84 drinks list (`MenuSystem.cpp:2080-2106`), 85 snacks (`fillVendingMachineSnacks(loadMapID)`,
  `:2108-2125`). `ACTION_CONFIRMBUY`(28) opens 86 with qty=1 (`:3223-3232`). `ACTION_BUYDRINK`(29)
  calls `buyDrink` or opens 87 (`:3234-3241`). `ACTION_BUYSNACK`(30) gives item 16 per unit at the
  snack price (`:3243-3254`). 88 = details. Menus 83..88 draw `vendingBG.bmp` (`:736-737`).
  Softkeys: `src/GameStateRunner.cpp:218-242`.

## 8. Where to test
`EV_MINIGAME type=4` sites (disassembled with `tools/disasm_map_scripts.py`):
map00 slot1 (IP 9557); map01 slots 1,2; map02 1,2; map03 1,2; map04 1; map05 1;
map06 1,2; map07 1; map08 1; map09 none.
- **map00 (level 1)**: EVT 166 at tile (27,29), TRIGGER any direction. It is guarded by
  `v16 == 0` (`scriptStateVars[16]` = player is the familiar/sentry-bot, `src/Game.cpp:3468`),
  otherwise it calls func_9586. This is far from the spawn at tile idx 612 = (4,19).
- **map01**: EVT 63 at tile (7,18), TRIGGER facing S, slot 1. It is very close to the
  map01 spawn idx 741 = (5,23).
