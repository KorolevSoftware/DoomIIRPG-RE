# Sentry bot minigame ("Matrix Skip") and the sentry-bot reward

Source: `src/SentryBotGame.{h,cpp}` (892 + 71 lines). Raw log: `docs/research/2026-10-30-sentry-bot.md`.
Shared minigame pieces (help screen, `evaluateMiniGameResults`, `ST_MINI_GAME` dispatch) are in
`docs/original-code/hacking-minigame.md` §3 and §6. Coordinates assume `screenRect = {0,0,480,320}`
(`docs/original-code/automap.md`), so `n = (screenRect[2]-screenRect[0])/2 = 240`.
Anchor flags: 3 = HCENTER|VCENTER, 20 = TOP|LEFT, 10 = RIGHT|VCENTER.

## 1. Entry

- `EV_MINIGAME` type 0: `initGame(this, b1)`. If `game->skipMinigames`, `forceWin()` is called.
  Then `unpauseTime = -1` and the thread yields (`n = 2`) (`src/ScriptThread.cpp:1290-1303`).
- `initGame(thread, botType)`: `gamePlayedFromMainMenu = (thread == nullptr)`. `botType` is forced to 0
  unless it is 0 or 1 (`src/SentryBotGame.cpp:44-52`).
- The main menu path is `playFromMainMenu()` → `initGame(nullptr, 0)` (`:29-31`, `src/MenuSystem.cpp:3215`).
- `setupGlobalData()`: `setState(ST_MINI_GAME)`, then `stateVars[0]=0` (selects sentry in the dispatch),
  `stateVars[1]=0` (help screen first), `stateVars[2]=0` (sub-screen), `initMiniGameHelpScreen()`,
  `clearSoftKeys()` (`:33-42`). Dispatch: `src/Canvas.cpp:457-459` (draw), `src/InputEventController.cpp:340-342`
  (keys), `src/TouchController.cpp:114,246,328` (touch).

Screen state: `stateVars[1]` 0 = help, 1 = game. With `stateVars[1]==1`, `stateVars[2]` is
0 = board, 1 = success (bot choice), 2 = failure. `stateVars[3] = upTimeMs` is stored on result but never read
by this file (`:219,224`). There is **no confirm-exit screen** (unlike hacking).

### Script call sites (disasm, `tools/disasm_map_scripts.py`)
Each one is guarded by `v16 == 0` (`scriptStateVars[16]` = isFamiliar, `src/Game.cpp:3469`) and by a
per-terminal "done" var. Before the call there is `PLAYSOUND 1065`.

| map | IP | args | result handling |
|---|---|---|---|
| map01 | 6154 | (0,-1,-1) | `v7==1`: disable event 36, `++v57`, hide sprites 199-201 |
| map02 | 2131 / 2219 / 2309 | (0,-1,-1) | `v7==1`: disable an event, UNMARKTILE flag 0x1, `++v23/24/25`, hide 3 sprites |
| map03 | 3228 / 3295 | (0,-1,-1) | `v7==1`: `++v42/43`, hide 3 sprites, disable event, UNMARKTILE 0x4 and 0x1, `++v20/21` |
| map04 | 3427 | (1,-1,-1) | `v7==0`: same unlock, **plus** `ENTITY_BREATHES`/`LERPSPRITE`/`WAKEMONSTER sprite=21` (a hostile sentry bot wakes), `ADVANCETURN`. `v7==1`: unlock only |
| map05 | 3667 | (1,-1,-1) | as map04, monster sprite 103 |
| map06 | 1973 | (1,-1,-1) | as map04, monster sprite 74 |
| map08 | 995 | (1,-1,-1) | as map04, monster sprite 57 |

`v7 == 2` (quit) matches neither branch: nothing changes and the terminal can be retried.
On botType 0 terminals, failure (`v7==0`) also changes nothing.

## 2. Puzzle generation (`src/SentryBotGame.cpp:53-81`)

```
iq  = fromMainMenu ? 0 : player->ce->getIQPercent()
n4  = (10000*2 + (6-2)*(100-iq)*(100-iq)) / 10000          // integer; iq 0 -> 6, iq 100 -> 2
do skipCode = nextInt() % n4 + 1  while skipCode == player->lastSkipCode   // 1..n4
player->lastSkipCode = (uint8)skipCode
allowSkip = fromMainMenu || getIQPercent() < 50
for i in 0..15: board[i] = nextInt() % 6 + 1; if board[i]==skipCode && !allowSkip: --i (re-roll)
board[0] = skipCode % 6 + 1
cur = -1
for j in 0..3: do cur = (cur + skipCode) % 16 while board[cur] == skipCode;  solution[j] = board[cur]
usersGuess[0..3] = 0; answer_cursor = 0; bot_selection_cursor = 0; player_cursor = -1
timeSinceLastCursorMove = app->time; failedEarly = false
```
- `n4` by IQ (integer division): iq 0 → 6, 1-13 → 5, 14-29 → 4, 30-50 → 3, 51-100 → 2. skipCode is 1..n4.
  The `!= lastSkipCode` loop cannot hang because n4 ≥ 2.
- `allowSkip` means low IQ (<50) boards CAN contain skip-code cells. High IQ boards never do (except
  `board[0]`, which is `skipCode%6+1` and so never equals skipCode). Note: `getIQPercent()` is called
  on line 61 even from the main menu, but only after `||` short-circuits.
- The walk starts at index -1, so the first landing is index `skipCode-1` (re-stepped while that cell equals
  skipCode). Wrap is mod 16, so a short skip code can revisit cells.
- RNG: `app->nextInt()` (same stream as the rest of the game).

## 3. Input

### Keys (`handleInput`, `:127-294`). Actions: UP 1, DOWN 2, LEFT 3, RIGHT 4, MENU 5, FIRE 6,
AUTOMAP 7, PASSTURN 14, BACK 15, MATRIX_LEFT 64, MATRIX_RIGHT 65 (`src/Enums.h:993-1012`).
MATRIX_LEFT/RIGHT come only from touch (`:878,881`), no key maps to them (`src/InputEventController.cpp:24-44`).

0. First, global: if `stateVars[2]==2`, or `stateVars[2]==1 && fromMainMenu`, then actions 7, 15, 6 →
   `endGame(0)` (`:130-133`). (So the failure screen always reports 0; the menu-mode success screen too.)
1. Help (`stateVars[1]==0`, `:135-177`):
   - MENU(5) or BACK(15): by `stateVars[2]`: 0 → `endGame(2)`; 1 → award bot by cursor + `endGame(1)`;
     2 → `endGame(0)`. (Only `stateVars[2]==0` is reachable in practice.)
   - FIRE: `stateVars[1]=1` (start), `clearSoftKeys()`.
   - UP/DOWN/RIGHT/LEFT: `handleMiniGameHelpScreenScroll(-1 / +1 / +16 / -16)`.
2. Board (`stateVars[1]==1 && stateVars[2]==0`, `:179-269`):
   - AUTOMAP(7) / BACK(15): back to help (`stateVars[1]=0`, `initMiniGameHelpScreen`, `clearSoftKeys`).
   - MENU(5): `endGame(2)` (quit).
   - FIRE: if `answer_cursor < 4`: if `player_cursor > -1`, `usersGuess[answer_cursor] = board[player_cursor]`,
     `answer_cursor++` (`:205-211`). Else (4 entered): `playerHasWon()` → `stateVars[2]=1`, otherwise
     `stateVars[2]=2`; both `clearSoftKeys()` (`:215-226`). The J2ME early-fail on a wrong digit is `#if 0`
     (`:195-203`), so `failedEarly` is always false in this build.
   - LEFT: only if `player_cursor > -1`: `player_cursor = (player_cursor+15)%16`, reset blink timer, `soundClick()`.
   - RIGHT: `player_cursor = (player_cursor+1)%16` (from -1 goes to 0), reset blink timer, `soundClick()`.
   - PASSTURN(14) / MATRIX_LEFT(64) = Delete: if `answer_cursor > 0`: clear `usersGuess[answer_cursor]` if ≤3,
     `answer_cursor--`, clear `usersGuess[answer_cursor]` (`:245-258`).
   - MATRIX_RIGHT(65) = enter: if `player_cursor > -1 && answer_cursor <= 3`: store and `answer_cursor++` (`:259-268`).
   - `player_cursor` is never reset to -1 after an entry (GEC lines commented out, `:210,253,265`).
3. Success (`stateVars[2]==1`, not from menu, `:271-292`):
   - FIRE: `awardSentryBot(botType==0 ? (cursor==0 ? 3 : 4) : (cursor==0 ? 5 : 6))`, `endGame(1)`.
   - LEFT/RIGHT: `bot_selection_cursor = (cursor+1)%2`.
   - MENU(5)/BACK/AUTOMAP: nothing here (the help branch is not active). So in script mode the player
     cannot leave the success screen without picking a bot.
4. Failure: only rule 0 (actions 6/7/15 → `endGame(0)`).

`playerHasWon()`: all 4 `usersGuess[i] == solution[i]` (`:756-763`). Order matters.
`forceWin()`: copy solution into guesses, `stateVars[1]=1`, `stateVars[2]=1`, `clearSoftKeys()` (`:774-782`).

### Buttons (`m_sentryBotButtons`, created in `initGame`, `:94-119`), sound 1027 each
| id | rect | use |
|---|---|---|
| 0 Exit | (428,290,54,30) | help & board: MENU |
| 1 Back | (0,290,54,30) | board: BACK |
| 2 Submit (right) | (368, 142 - h/2, w, h) of `matrixSkip_button.bmp`; normalRenderMode 12, highlight 0 | board: FIRE if `answer_cursor>3`; success: "Exploding" selector |
| 3 Delete (left) | (8, 142 - h/2, w, h), same image and modes | board: MATRIX_LEFT; success: "Shooting" selector |

`matrixSkip_button.bmp` is loaded three times into `imgSubmit`, `imgUnk1`, `imgDelete` (`:90-92`).

### Touch (`:826-892`)
- `touchStart/Move`: `HighlightButton(x,y,true)`, `touched=true`, store `currentPress_x/y`.
- `touchEnd`: un-highlight all, `touched=false`, store press, then help → `handleTouchForHelpScreen`, else
  `handleTouchForGame`.
- Help: button 0 → MENU (quit `endGame(2)`), anywhere else → FIRE (start).
- Board: button 0 → MENU, 1 → BACK, 2 → FIRE only if `answer_cursor>3`, 3 → MATRIX_LEFT (delete),
  anything else → MATRIX_RIGHT (enter the value under `player_cursor`).
- Cell selection by touch happens **inside `drawGameScreen`**: while `touched` and the press is in the cell rect
  `(n3-84, n4+6, 42, 42)`, `player_cursor = i` (with `soundClick()` when it changes) (`:509-515`).
  So the release over a cell enters that cell (MATRIX_RIGHT).
- Success: FIRE if `bot_selection_cursor != -1` (always true). The choice is made during drawing: while
  button 3 is highlighted → cursor 0, button 2 highlighted → cursor 1 (`:424-426,438-440`).
- Failure: touch sends ACTION_MENU (5) (`:889-891`). Rule 0 accepts only 6/7/15 and there is no `stateVars[2]==2`
  branch, so a touch on the failure screen does **nothing**; keys FIRE/AUTOMAP/BACK are needed. (Literal reading;
  likely a port bug from the MENU/AUTOMAP swap noted in the comments.)

## 4. Drawing (`updateGame`, `:296-321`)
`clipRect(0,0,menuRect[2],menuRect[3])`, `setClipRect(0,0,480,320)`, draw screen, `staleView=1`,
`resetScreenSpace()`.

Assets (`src/LoadingManager.cpp:88-92,105`): `imgMatrixSkip_BG` = `matrixSkip_BG.bmp`,
`imgGameAssets` = `sentryGame.bmp`, `imgButton` / `imgButton2` / `imgButton3` =
`matrixSkip_grid_pressed.bmp` / `_2` / `_3`, `imgHelpScreenAssets` = hacking's `gameHelpBG.bmp`.

`sentryGame.bmp` regions used (texX,texY,w,h): grid quarter (53,0,91,91); start marker (144,0,41,37);
guess-bar half (0,91,95,33); cursor (95,91,22,22).

### Help (`:475-487`)
`drawMiniGameHelpScreen(g, STRINGID(0,171) "Matrix Skip", STRINGID(0,172) <rules>, imgHelpScreenAssets)`,
then "Exit" (Code 30) at (445,302) anchor 3, `fontRenderMode 2` unless button 0 is highlighted.

### Board (`:489-727`), `n = 240`, `n2 = screenRect[1] + 50 = 50`
1. `matrixSkip_BG` at (0,0).
2. Grid: region (53,0,91,91) at (n-91,n2) rot 0, (n,n2) rot 4 (mirror X), (n-91,n2+91) rot 6 (180°+mirror = flip Y),
   (n,n2+91) rot 2 (180°). Anchor 20. (Rotate modes: `src/Image.cpp:120-150`.)
3. If `player_cursor == -1`: start marker (144,0,41,37) at (n-120, n2+8).
4. For i in 0..15: `n3 = n + (i%4)*42`, `n4 = n2 + (i/4)*44`. If touched over `(n3-84, n4+6, 42, 42)`: draw the
   pressed-cell image with a per-cell image/rotation/offset table (`:518-633`; corners use `_2`, edges `_3`,
   interior `matrixSkip_grid_pressed`). Then the number at `(n3-63, n4+27)` anchor 3. If `i == player_cursor`:
   `drawCursor(n3-63, n4+26, true)`. Cell centres: x = 177, 219, 261, 303; y = 77, 121, 165, 209.
5. If `player_cursor == -1`: `drawCursor(n-102, n2+26, true)` = (138, 76).
6. `drawPlayersGuess(n, n2+199 = 249, true)`.
7. Text (anchor 3): Code 173 "Hint:" + Code 174 "Skip Code:" + skipCode at (n, 30); Code 175 "Enter your code"
   at (n, 241); Code 176 "Tap number to pick a number" at (n, 294).
8. "Back" (literal) at (35,310) and "Exit" (literal) at (445,310), each `fontRenderMode 2` unless its button
   is highlighted.
9. Button render modes: Submit (2) normal = 1 if `answer_cursor > 3` else 0; Delete (3) normal = 0 if
   `answer_cursor <= 0` else 1; highlight 0 for both (`:685-701`). Render mode 0 = opaque, 1 = 25% alpha
   modulate, 12 = 75% (`src/Image.cpp:162-205`). (Literal: the "active" state is the 25% one.)
10. Render buttons 2 and 3, then literal "Delete" at the centre of button 3 and "Submit" at the centre of button 2
    (anchor 3).

`drawCursor(x, y, blink)` (`:749-754`): draws region (95,91,22,22) at (x+1, y) anchor 3, unless `blink` and
`((time - timeSinceLastCursorMove) & 0x200) != 0` (so it blinks with a 1024 ms period, visible first 512 ms).

`drawPlayersGuess(n, y, showCursor)` (`:729-747`): region (0,91,95,33) at (n-95,y) rot 0 and (n,y) rot 4,
anchor 20. For i in 0..3: `x = n - 67 + 45i` (173, 218, 263, 308), text row `ty = y + 16`; non-zero guess drawn at
(x, ty) anchor 3; underline `drawLine(x-6, ty+8, x+6, ty+8, 0xFF6C834A)`; if `showCursor && answer_cursor == i`:
`drawCursor(x, ty, false)` (no blink).

### Success (`:373-473`)
`matrixSkip_BG`, `setLeftSoftKey(0,30)`, `drawPlayersGuess(n, 26, false)`, Code 177 "Code Accepted!" at (n,15).
- Main-menu mode: Code 239 "Hack Successful!" at (n,71); Code 188 "You are being logged out. Good day." wrapped 22
  at (n,139). Auto-FIRE on touch release through the static `wasTouched` latch (`:398-408`).
- Script mode (`n2 = 95`): Code 180 "Select a bot type" at (n,95). Render buttons 2 and 3. Code 181 "Shooting" at
  (n-180=60, 140), Code 182 "Exploding" at (n+180=420, 140), anchor 3. Cursor: `graphics->drawCursor(xPos +
  OSC_CYCLE[(time/100)%4], 140, 10)` with `xPos = label_x - w/2 - 5` of the selected label (glyph 0x84,
  `src/Graphics.cpp:674-686`). Info panel: `fillRect(12, 320-10-140=170, 456, 140, 0xFF464C43)`; literal
  "Shooting Bot:" at (22,178) anchor 20; Code 183 wrapped 50 at (22,197); literal "Exploding Bot:" at (22,243);
  Code 184 wrapped 50 at (22,262).

### Failure (`:323-370`)
`matrixSkip_BG`, `setLeftSoftKey(0,30)`, `drawPlayersGuess(n, 26, false)`, Code 178 "Code Denied!" (179
"Incorrect Input!" if `failedEarly`) at (n,15); Code 240 "Hack Failed!" at (n,71).
- botType 1: warning box centred at (n,139): `fillRect(n-53,113,107,52,0xFF5C000B)`,
  `fillRect(n-50,116,101,46,0xFF343A33)`, `fillRect(n-40,126,80,26,0xFF5C000B)`, `fillRect(n-33,133,66,12,0xFF343A33)`;
  Code 186 "WARNING!" at (n,111); then Code 187 "Sentry Bot initiating defensive action." wrapped 22 at (n,209).
- botType 0: Code 188 "You are being logged out. Good day." wrapped 22 at (n,139).

Strings: file 0 (Code) of `strings00.bin`, calibrated by 30 "Exit" / 96 / 164 "Hacker" from the hacking doc.

## 5. Sounds
- `soundClick()` (1027) on LEFT/RIGHT and on touch selecting a new cell (`:234,242,513`); buttons carry 1027.
- `endGame`: 1043 if n==1, else 1040 (`playSound(id,0,3,0)`, `:809`). Main menu adds 1071 loop (`:821`).
- Scripts play 1065 before the call.

## 6. endGame(n) (`:806-824`)
`n`: **1 = won (bot chosen), 0 = failed, 2 = quit**. Script mode: `evaluateMiniGameResults(n)` (IQ +2 and HUD
message 236/237 on n==1, `src/MiniGameManager.cpp:235-248`), `scriptStateVars[7] = n`, `setState(ST_PLAYING)`,
`callingThread->run()`, `callingThread = nullptr`. Main menu: sound 1071 and `setMenu(MENU_MAIN_MINIGAME)`.
Note `awardSentryBot` runs before `endGame(1)`, so the weapon help/selection happen first.

## 7. awardSentryBot(n) (`:784-804`) and give
- If the player already owns a bot (bit 3, 4, 5 or 6 checked in that order: masks 0x8, 0x10, 0x20, 0x40):
  `attemptToDiscardFamiliar(thatWeapon)` + `showHelp(16, true)`.
  `attemptToDiscardFamiliar(w)` (`src/Player.cpp:2427-2437`): if the tile has `mapFlags & 0x20`, message Code 221
  "Cannot discard sentry bot here."; else `spawnDropItem(viewX, viewY, getWeaponTileNum(w), 6, 1, w, ammo[7], false)`
  (the old bot is dropped as a world item that keeps its health) and `give(1, w, -1)`.
- Then `give(1, n, 1)` (`src/Player.cpp:970-1006`): for a bot weapon (`WP_SENTRY_BOT_MASK = 120` = bits 3-6,
  `src/Enums.h:157`, `src/Player.cpp:2485-2487`): fails if `isFamiliar`; else `give(2, 7, 100, true)` (ammo slot 7 =
  bot health, capped at 100) and clears all bot bits. Then `weapons |= 1<<n`, `showWeaponHelp(n)`, and
  `selectWeapon(n)` because it is new. No inventory items are granted.
- Weapons: 3 = shooting bot, 4 = exploding bot (botType 0); 5 = shooting, 6 = exploding (botType 1, red variant).
  Cursor 0 ("Shooting", left) → 3/5; cursor 1 ("Exploding", right) → 4/6.

## 8. The reward in normal play: the "familiar" system (scoped summary)

Owning bot weapon w makes it a selectable weapon. Its view sprite is tile 19 (w 3/4) or 18 (w 5/6), frames 2 and 3
with a bob (`src/Combat.cpp:797-813`). FIRE with it does not attack: it calls `attemptToDeploySentryBot()`
(`src/PlayingInputHandler.cpp:493-495`).

**Deploy** (`src/Player.cpp:2387-2425`): refused with Code 219 if an active monster within `tileDistances[0]` can attack,
or Code 217 if the tile in front has `mapFlags & 0x10`. familiarType = 1 (w3), 2 (w4), 3 (w5), 4 (w6).
`setFamiliar(type)` (`:2080-2126`): `scriptStateVars[16]=1`, saves the view, `isFamiliar=true`, moves one tile forward
(failing → Code 190 "No room to deploy" and revert). Then fog save, fade, `spawnPlayerEntityCopy` at the old spot (a
stand-in body), zeroes `weaponsCopy/ammoCopy/inventoryCopy`, swaps status effects, help 12/13, sound 1108.

**The player now controls the bot.** The camera is the bot. Bot health is `ammo[7]` (`src/Player.cpp:220,445`).
Pickups go into the `*Copy` arrays (`give`, `:995,1010,1036`).
- View tint: `postProcessMode` 1 (green, type 1/2) or 2 (red, type 3/4) (`src/Player.cpp:2500-2518`), drawn as GL
  overlay with scanlines (`src/Render.cpp:2642-2659`).
- HUD: the bottom bar is replaced by a bot face (`Hud_Sentry.bmp`) and a 20-segment health bar coloured by `ammo[7]`
  (`src/Hud.cpp:640-680`). `damage_bot.bmp` vignette (`src/Hud.cpp:568`).
- Shooting bots (type 1/3) fire with weapon 0 art at a special offset (`src/Combat.cpp:695-724`). Exploding bots (2/4)
  draw no weapon. FIRE opens the "Self-destruct?" dialog (Code 194) (`src/PlayingInputHandler.cpp:489-491`,
  `src/Player.cpp:2439-2449`, `src/DialogSystem.cpp:564`).
- Damage to the bot: 7/8 (type 1/2) or 5/8 (type 3/4) of the raw damage, no armor (`src/CombatEntity.cpp:357-366`).
- Movement: blocked on `mapFlags & 0x10` tiles (Code 222) (`src/MovementController.cpp:322-325`). Walking into the
  stand-in body = `familiarReturnsToPlayer(true)` (`:333-336`). Doors don't close (`src/Game.cpp:1218`), portals off
  (`src/Render.cpp:2702`). A monster next to the real body forces the bot back (`src/Game.cpp:3217-3224`).
- Return (`familiarReturnsToPlayer`, `src/Player.cpp:2278-2311`): sound 1112, `unsetFamiliar` (restore view and fog,
  remove the copy entity), then the player gets everything the bot picked up. Key B (`AVK_BOTDISCARD` →
  `ACTION_BOT_DISCARD`) or the HUD bot button: as bot → return; as player holding a bot weapon → discard it
  (`src/PlayingInputHandler.cpp:77-84`, `src/Hud.cpp:1241-1255`).
- Death (`ammo[7] <= 0`, `src/GameStateRunner.cpp:174-177`, `src/Combat.cpp:1035`) or self-destruct:
  `familiarDying` → `ST_BOT_DYING` (shake, brighten, 1500 ms, `src/GameStateRunner.cpp:285-300`, `src/Canvas.cpp:1130-1142`)
  → `familiarDied()` (`src/Player.cpp:2208-2262`): Code 192 "Your Sentry Bot has perished!", exploding types do
  `radiusHurtEntities` with weapon 4/6 damage, the bot's loot is dropped plus a `spawnSentryBotCorpse`
  (`src/Game.cpp:2770`), and the bot weapon is removed.
- Scripts: `v16` gates ~400 checks across map00-08 (terminals refuse the bot); `EV_FORCE_BOT_RETURN`
  (`src/ScriptThread.cpp:1744-1750`); saves store `isFamiliar`/`familiarType` and copies (`src/Player.cpp:1291-1402`).
- World pickup of a bot item: refused if `isFamiliar || hasASentryBot()`; a dropped bot keeps its stored health
  (`src/Entity.cpp:238-252`).

Scope: about 17 legacy files, roughly 700-900 lines of familiar-specific logic (Player ~450, the rest spread across
HUD, render, combat, movement, input, game state, scripts, save).
