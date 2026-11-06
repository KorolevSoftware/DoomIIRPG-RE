# Front end: main menu, difficulty, character select, prologue, logo / intro movie

Date: 2026-11-03. ADR: `adr/0043-front-end-flow-and-capture-bypass.md` (supersedes ADR 0037 §6).
Facts: `docs/original-code/front-end.md` (cited below as **FE §n**), raw report
`docs/research/2026-11-03-front-end.md`. Every `src/` citation is the legacy port;
`src/MenuSystem.cpp` is ISO-8859 + CRLF, so use `grep -a` there.

Scope, in the user's priority: the main menu with its real rows and skin, the character
select, the prologue; then the logo; the intro movie last (research-gated, G6b).
**Deferred** (rows stay visible and refuse on select, deviation FE-D2): Options body,
Other Games, Comic Book, Mini Games, Game Help, About, Credits. **2026-11-04:** all but
Options are specified in `main-menu-subscreens.md` (ADR 0044); FE-D2 now covers Options only.
**2026-11-05:** Options → Sound is specified in `options-sound.md` (ADR 0050); FE-D2 now covers
only the omitted Video/Input rows and Other Games.

---------------------------------------------------------------------------------
## 0. The flow after this spec

```
boot(skipFrontEnd=false)  : loadConfig -> backToMain           [G6a: -> ST_LOGO -> backToMain]
boot(skipFrontEnd=true)   : loadConfig -> difficulty 1, choice 1 -> resetForNewGame -> loadMap(1,false,true)
                            (capture runs + --newgame; ADR 0043 §3)
backToMain                : unload runtime, loadMapID = 0, player.reset, ST_MENU / MENU_MAIN (3)
MENU_MAIN New Game        : hasSavedState ? MENU_MAIN_CONFIRMNEW (14) : MENU_MAIN_DIFFICULTY (16)
ACTION_DIFFICULTY p       : game.setDifficulty(p); startGame(true) -> resetForNewGame -> ST_CHARACTER_SELECTION (22)
char select Yes           : player.characterChoice = c; player.reset(); ST_INTRO (9)
prologue last page passed : loadMap(1, false, true) -> TravelMap -> Loading -> Playing (unchanged)
MENU_MAIN Continue        : startGame(false) = loadState(recentLoadType()); failure -> MENU_INGAME_LOADNOSAVE (50)
in-game Don't Save / dead : backToMain                       (was: quit)
in-game Save Game (0x04)  : save, then backToMain             (was: quit)
Save & Quit (0x40), Exit  : quit                              (unchanged / new ACTION_EXIT)
```

Threading: unchanged, single-threaded fixed-step loop (`GameLoop`). All new state lives in
`core/` sessions; `ui/` gets per-frame models and stateless views (ADR 0012).

---------------------------------------------------------------------------------
## 1. Decisions in one place (details in ADR 0043)

1. **No new front-end module.** The main-menu screens are `MenuSession` screens (they are
   `menus.bin` rows, ADR 0013). The two non-menu screens are peer sessions owned by
   `GameContext`, like `TravelMapSession`: `core/CharacterSelectSession`,
   `core/PrologueSession`. New `StateId`s: `CharacterSelect = 22`, `Intro = 9`
   (G6: `Logo = 26`, `IntroMovie = 2`).
2. **`backToMain` is real** and lives in `GameContext` (it owns the runtime teardown
   `unloadMap_`). `LoadingManager` and `MenuSession` reach it through an `Env` callback.
3. **Main-menu skin = a second view function on the same `MenuViewModel`**
   (`ui/MainMenuView`). `MenuView` (in-game) is not touched beyond what G1 needs. The model
   gains `mainSkin`, `drawLogo`, `dialKnobY` and a per-row `textDy`.
4. **Capture runs and `--newgame` bypass the front end** in `LoadingManager::boot(true)`:
   difficulty 1, character 1, wipe, `loadMap(1,false,true)`. The tick timeline is identical
   to today's, so the golden frames stay valid.
5. **The world pass is skipped while `game.loadMapID == 0`** (deviation FE-D6): legacy
   unloads the map in `backToMain` (`src/Canvas.cpp:1375-1379`); the rewrite keeps the old
   `MapData` resident (ADR 0035), so drawing it under the opaque menu would be wasted work.
6. Character-select and prologue touch use one generic intent, `UiAction::ScreenTouch`
   (index = the legacy button id), latched by `GameContext` and queued as
   `Action::ScreenTouch` — the `MiniGameTouch` idiom.

---------------------------------------------------------------------------------
## 2. Groups (implement in this order, one per delegation)

| Group | Content | Files |
|---|---|---|
| **G1** | difficulty + character plumbing, no UI | `domain/game/Game.{h,cpp}`, `domain/game/Player.{h,cpp}`, `domain/game/ScriptVM.cpp`, `core/SaveGame.cpp`, `core/LoadingManager.cpp`, `core/GameContext.cpp`, `core/Main.cpp`, `ui/UiAssets.{h,cpp}`, `ui/HudModel.h`, `ui/HudView.cpp`, `ui/DialogModel.h`, `ui/DialogView.cpp` |
| **G2** | main-menu logic, real `backToMain`, boot into the menu | `io/MenuData.h`, `core/MenuSession.{h,cpp}`, `core/LoadingManager.{h,cpp}`, `core/GameContext.{h,cpp}`, `core/Main.cpp` (comments) |
| **G3** | main-menu skin + geometry | `ui/UiAssets.{h,cpp}`, `ui/MenuModel.h`, `ui/MainMenuView.{h,cpp}` (new), `core/MenuSession.cpp`, `core/GameContext.cpp` |
| **G4a** | character-select assets, model, view (no caller yet) | `ui/UiAssets.{h,cpp}`, `ui/UiTypes.h`, `ui/CharacterSelectModel.h` (new), `ui/CharacterSelectView.{h,cpp}` (new) |
| **G4b** | character-select session + state | `core/GameStates.h`, `core/CharacterSelectSession.{h,cpp}` (new), `core/GameContext.{h,cpp}`, `core/LoadingManager.cpp` |
| **G5** | prologue | `ui/PrologueModel.h`, `ui/PrologueView.{h,cpp}`, `core/PrologueSession.{h,cpp}` (all new), `ui/UiTypes.h`, `core/GameStates.h`, `core/GameContext.{h,cpp}`, `core/CharacterSelectSession.cpp` |
| **G6a** | logo (`ST_LOGO`) | `ui/UiAssets.{h,cpp}`, `core/GameStates.h`, `core/GameContext.{h,cpp}`, `core/LoadingManager.cpp` |
| **G6b** | intro movie (`ST_INTRO_MOVIE`) — research R1-R3 resolved 2026-11-04 (§9) | implemented |

New files ⇒ re-run `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (GLOB) in G3, G4a,
G4b, G5. Build: `cmake --build build_new -j 8`.

---------------------------------------------------------------------------------
## 3. G1 — difficulty and character plumbing

No visible change with the defaults (difficulty 1, character 1). It removes every
hardcoded "marine / Normal" so G4 can change them.

### 3.1 `Game` owns the difficulty (`domain/game/Game.{h,cpp}`)
Legacy `game->difficulty`, default 1 (`src/Game.cpp:43`), set only by ACTION_DIFFICULTY
(FE §6). Today `Game::difficulty()` reads `vm_->vars[12]` (`new_src/domain/game/Game.cpp:748-750`).

```cpp
// Game.h, replacing the vars[12] accessor and its comment (:221-223)
int  difficulty() const { return difficulty_; }   // game->difficulty: 1 Normal, 2 Difficult, 4 Nightmare
void setDifficulty(int d) { difficulty_ = d; }
// private:
int difficulty_ = 1;                                // src/Game.cpp:43
```
Delete the `.cpp` definition. All readers keep calling `difficulty()`.

### 3.2 Stamp `vars[12]` from it (`core/GameContext.cpp` `tickLoading`, :526-529)
`sys_.vm->vars[12] = static_cast<short>(sys_.game->difficulty());` (`src/LoadingManager.cpp:692`).
Rewrite the comment: the +25 % spawn hp bump (`new_src/domain/game/Game.cpp:213`) now
applies on difficulties 2/4 exactly like legacy. Add one log line after it:
`[load] difficulty %d character %d`.

### 3.3 Config round-trip (`core/SaveGame.cpp` `loadConfig`, :186)
The writer already writes `game->difficulty()` (:150). The reader skips it. Replace
`r.skip(1 + 1 + 1 + 1 + 1 + 1 + 4)` with `const int difficulty = r.readByte(); r.skip(1 + 1 + 1 + 1 + 1 + 4);`
(`src/Game.cpp:1984`). Commit it with the other fields after the marker check:
`env_.game->setDifficulty((difficulty == 1 || difficulty == 2 || difficulty == 4) ? difficulty : 1);`
Log `[save] config difficulty %d invalid -> 1` when it falls back (trust boundary, ADR 0037 §5).

### 3.4 `vars[14]` = character (`domain/game/ScriptVM.cpp` `updateScriptVars`, :315-320)
`vars[14] = static_cast<short>(env_.player->characterChoice);` (`src/Game.cpp:3468`). Drop
the "hardcode marine" comment.

### 3.5 `Player::reset` (`domain/game/Player.{h,cpp}`)
* Add `void setGame(const Game* g) { game_ = g; }` and `const Game* game_ = nullptr;`
  (forward-declare `Game` in `Player.h`, include `domain/game/Game.h` in `Player.cpp`).
  `core/Main.cpp`: `player.setGame(&game);` right after `Game game;`.
* In `reset()`, replace the "shortcut: the difficulty == 2 DEFENSE 0 override is skipped"
  comment (:148-149) with, between `setStatsAccordingToCharacterChoice()` and `updateStats()`:
  `if (game_ != nullptr && game_->difficulty() == 2) baseCe.setStat(Enums::STAT_DEFENSE, 0);`
  (`src/Player.cpp:560-564`).
* Delete `bestGrades = 0;` from `reset()` (:156). Legacy zeroes it only in the constructor
  (`src/Player.cpp:37`); `Player.h:237` already defaults it to 0. Then delete the
  save/restore pair around `p.reset()` in `LoadingManager::startGame` (`new_src/core/LoadingManager.cpp:44,46`).
  Without this the character-select `reset()` (G4b) would wipe the best grades.

### 3.6 HUD face sheet by character (`ui/UiAssets`, `ui/HudModel.h`, `ui/HudView.cpp`, `core/GameContext.cpp`)
Legacy loads one of three sheet pairs by `characterChoice` (`src/App.cpp:438-449`). The
rewrite keeps all three resident (the travel-map precedent):
* `UiAssets`: `Texture playerFacesDoom, playerActiveDoom, playerFacesScientist, playerActiveScientist;`
  loaded right after `HUD_Player_Active.bmp` from `Hud_PlayerDoom.bmp`, `HUD_PlayerDoom_Active.bmp`,
  `Hud_PlayerScientist.bmp`, `HUD_PlayerScientist_Active.bmp` (exact names, `src/App.cpp:444-449`).
* `HudModel`: `int characterChoice = 1;  // picks the face sheet pair (src/App.cpp:438-449)`.
* `HudView.cpp:157`: choose the pair by `m.characterChoice` (2 → Doom, 3 → Scientist,
  anything else → the existing `playerFaces`/`playerActive`).
* `GameContext::buildHudModel`: `m.characterChoice = sys_.player->characterChoice;`.

### 3.7 Dialog portrait row (`ui/DialogModel.h`, `ui/DialogView.cpp`, `core/GameContext.cpp`)
Legacy picks row `characterChoice - 1` of `Hud_Portrait_Small.bmp` (`src/DialogSystem.cpp:291-308`).
* `DialogViewModel`: `int portraitRow = 0;`.
* `DialogView.cpp:200-204`: source y = `portraitRow * (portraits.height() / 3)`; update the comment.
* `GameContext::render`, right after `sys_.dialogs->buildViewModel(dlg)` succeeds:
  `dlg.portraitRow = std::clamp(sys_.player->characterChoice - 1, 0, 2);`.

**G1 acceptance.** Builds with no warnings in touched files. `./DoomIIRPG --newgame` plays
exactly as before; the log shows `[load] difficulty 1 character 1`. HUD face and dialog
portrait unchanged (marine). Reviewer greps: no remaining `vars[14] = 1`, no `vm_->vars[12]`
in `Game.cpp`, no `bestGrades = 0` in `Player::reset`.

---------------------------------------------------------------------------------
## 4. G2 — main-menu logic, real `backToMain`, boot into the menu

### 4.1 Ids and actions (`io/MenuData.h`)
Add to `MenuId` (`src/Menus.h:216-246`): `kMenuMain = 3, kMenuMainHelp = 4, kMenuMainAbout = 8,
kMenuMainExit = 13, kMenuMainConfirmNew = 14, kMenuMainDifficulty = 16, kMenuMainOptions = 17,
kMenuMainMinigame = 18, kMenuMainMoreGames = 19, kMenuComicBook = 89`.
Add to `MenuAction` (`src/Menus.h:135-167`): `kActionNewGame = 7, kActionExit = 8,
kActionDifficulty = 10, kActionContinue = 22, kActionMinigame = 27`.
Update both enum comments ("only the in-game tree" → "in-game tree + main menu").

### 4.2 `MenuSession` (`core/MenuSession.{h,cpp}`)

**Env additions**
```cpp
std::function<bool(bool newGame)> startGame;   // MenuSystem::startGame; false = Continue found no usable save
std::function<bool()> hasSavedState;           // Game::hasSavedState (Continue row, New Game confirm)
std::function<void()> quit;                    // ACTION_EXIT: app->shutdown() (src/MenuSystem.cpp:3082-3084)
```
`backToMain`'s comment: now the real `Canvas::backToMain(false)`.

**Helper** (anonymous namespace): `bool isMainMenu(int id) { return id < kMenuInGame && id != kMenuLevelStats; }`
(the `menu < MENU_INGAME` tests of `paint`/`setMenuSettings`, `src/MenuSystem.cpp:762-771,803`).

**`setMenu`**: `clearStack()` also for `kMenuMain` (`src/MenuSystem.cpp:619-621`).

**`initMenu` bodies** — new branches before the final `else` (FE §5.1):
```cpp
} else if (menuId == kMenuMain) {                                   // (:1256-1272)
    if (env_.hasSavedState && env_.hasSavedState())
        addItem(env_.loc->makeId(kTextIngame2, 70), kItemAlignCenter, kActionContinue, 0, kEmptyTextId);
    addParsedRows(menuId, 0, 2);                                    // New Game, Options
    addParsedRows(menuId, 3, -1);                                   // skips row 2 "Other Games" [GEC]
} else if (menuId == kMenuMainDifficulty) {                         // (:1274-1281)
    addParsedRows(menuId, 1, -1);                                   // title row 0 not shown
} else if (menuId == kMenuMainExit) {                               // (:1345-1350)
    setYesNo(106, 0, kActionExit, 0);                               // "Exit Game?", No preselected
} else if (menuId == kMenuMainConfirmNew) {                         // (:1352-1357)
    type_ = kMenuTypeVCenter;
    setYesNo(107, 0, kActionGoto, kMenuMainDifficulty);             // "This will erase\n...", No preselected
}
```
(13 and 14 have `menus.bin` rows, so `type_` comes from the file for 13; 14 is set to 6 as
in legacy `:1353`.) Per-screen patch block: `if (menuId == kMenuMainDifficulty) selectedIndex_ = 1;`
(`:1276`, lands on "Normal").

**Refusals** (deviation FE-D2): in the refusal loop, a `kActionGoto` whose target is one of
`kMenuMainOptions, kMenuMainMoreGames, kMenuComicBook, kMenuMainMinigame, kMenuMainHelp, kMenuMainAbout`
→ `disableRow(i, "main-menu screen not ported (front-end.md FE-D2)")`. Rows stay drawn
normally and log on select, like the in-game refusals.

**`select()`** new cases (`src/MenuSystem.cpp:3072-3099,3180-3182`):
```cpp
case kActionNewGame:                                                 // (:3072-3080)
    gotoMenu(env_.hasSavedState && env_.hasSavedState() ? kMenuMainConfirmNew : kMenuMainDifficulty);
    break;
case kActionExit:       if (env_.quit) env_.quit(); break;           // (:3082-3084)
case kActionDifficulty:                                              // (:3095-3099)
    if (env_.game != nullptr) env_.game->setDifficulty(items_[i].def.param);   // 1 / 2 / 4
    if (env_.startGame) env_.startGame(true);
    break;
case kActionContinue:                                                // (:3180-3182)
    if (!env_.startGame || !env_.startGame(false)) gotoMenu(kMenuLoadNoSave);  // deviation FE-D1
    break;
```

**`handleAction`**: `Action::BackKey` on `kMenuMain` → `gotoMenu(kMenuMainExit)` and stop
(`src/MenuSystem.cpp:2939-2942`); everything else unchanged (`Action::Menu` on the empty
root does nothing, `:2921-2934` + `back()`).

**`back()`**: log `[sound] 1122` only when `menu_ != kMenuMain` (`:577-581`).

**Soft keys already in G2** (the in-game skin is the interim look until G3, and its
"Resume" key must not drop into a game that does not exist): in `buildViewModel`, for
main ids set `m.softRight = nullptr` and keep `m.softLeft` only on `kMenuMainDifficulty` /
`kMenuMainHelp` (no button 15 below MENU_INGAME, button 11 only there,
`src/MenuSystem.cpp:4498-4583`); in `handleAction`, ignore `Action::MenuResume` on main ids.

### 4.3 `LoadingManager` (`core/LoadingManager.{h,cpp}`)
```cpp
// Env addition
std::function<void()> backToMain;   // GameContext::backToMain_ (src/Canvas.cpp:1372-1407)

void boot(bool skipFrontEnd);       // App.cpp:119-128 loadConfig, then the front end or the dev/capture bypass (ADR 0043 §3)
bool startGame(bool newGame);       // MenuSystem::startGame (src/MenuSystem.cpp:3571-3609); false = no usable save (Continue)
void startNewGameMap();             // disposeIntro: loadMap(startupMap = 1, false, true) (src/IntroSequenceManager.cpp:131-136)
// private
void resetForNewGame();             // startGame(true) body minus the state change (:3584-3602)
```
* `boot`: `save_.loadConfig();` then
  `if (skipFrontEnd) { env_.game->setDifficulty(1); env_.player->characterChoice = 1; resetForNewGame(); startNewGameMap(); } else env_.backToMain();`
  (G6a replaces `env_.backToMain()` with `env_.host->requestState(StateId::Logo)`.)
* `resetForNewGame`: today's `startGame` new-game body (`removeState` … `currentGrades = 0`),
  minus the `loadMap` line and minus the `bestGrades` save/restore (G1).
* `startGame(true)`: `resetForNewGame();` then — **G2 interim** — `startNewGameMap();`
  (G4b changes this line to `env_.host->requestState(StateId::CharacterSelect)`). Returns true.
* `startGame(false)`: `if (loadState(recentLoadType())) return true;` log
  `[save] Continue failed`, return false. **No** fallback to a new game any more.
* `startNewGameMap`: `loadMap(1, false, true);`.
* `tickSaving`: `type & 0x04` → `env_.backToMain();` (`src/Canvas.cpp:879-880`); `0x40` keeps
  `env_.quit()`. Fix both comments.

### 4.4 `GameContext` (`core/GameContext.{h,cpp}`)
Private `void backToMain_();` — port of `Canvas::backToMain(false)` (`src/Canvas.cpp:1372-1407`):
```cpp
void GameContext::backToMain_() {
    unloadMap_();                          // unloadMapData + render->unloadMap analog (:1377-1379)
    sys_.vm->resetPool();                  // the map's threads die with it
    sys_.game->loadMapID = 0;              // (:1375)
    sys_.player->reset();                  // (:1376)
    sys_.hud->clearScreenEffects();        // render->endFade (:1380)
    pauseGameTime = true;                  // as at boot
    std::fprintf(stderr, "[sound] 1071\n");   // Music_Title (:1404)
    nextMenuId_ = kMenuMain;
    setState(StateId::Menu);               // setMenu(MENU_MAIN) (:1405, src/MenuSystem.cpp:645-649)
}
```
Wiring in `init`: `menuEnv.backToMain = [this] { backToMain_(); };`
`menuEnv.startGame = [this](bool n) { return loading_.startGame(n); };`
`menuEnv.hasSavedState = [this] { return loading_.save().hasSavedState(); };`
`menuEnv.quit = [this] { quitRequested_ = true; };`
`loadEnv.backToMain = [this] { backToMain_(); };`
Header comment of `quitRequested()`: "ACTION_EXIT / Save & Quit / window close".

World pass gate (deviation FE-D6) in `render()`: wrap `scene_.drawWorld(...)`, the two
`sys_.game->visibility` writes and `Automap::markSeen` in
`if (sys_.game->loadMapID != 0) { ... }` with the comment
"backToMain zeroes loadMapID (src/Canvas.cpp:1375); the old MapData stays resident (ADR 0035)".

### 4.5 `core/Main.cpp`
Code unchanged (`boot(forceNewGame)` now means "skip the front end"). Update the two
comments: `--newgame` = dev shortcut "wipe + start map 1 as Major on Normal, no menu";
`DOOM2RPG_CAPTURE_TICKS` = disk off + the same bypass (ADR 0043 §3).

**G2 acceptance / user screen test** (in-game skin for now — the art arrives in G3):
1. Run with a save present: the game opens on a menu (in-game look) with
   Continue, New Game, Options, Comic Book, Mini Games, Game Help, About, Exit; cursor on Continue.
2. Continue → travel map → the saved game, as before.
3. In game: Menu → Main Menu → Don't Save → back on the main menu (no quit). Same with
   Save Game (shows "Game Saved" then the main menu; Continue resumes there).
4. New Game (save present) → "This will erase your saved game. Are you sure?" with No
   selected; Yes → Normal / Difficult / Nightmare with the cursor on Normal; pick one →
   travel map → map 1 (G2 interim: no character select yet).
5. Exit → "Exit Game?" → Yes quits; No / Escape returns. Backspace on the root opens it.
6. Options / Comic Book / Mini Games / Game Help / About log `refused` and do nothing.
7. `DOOM2RPG_CAPTURE_TICKS=...` golden runs and `--newgame` still start map 1 directly.

---------------------------------------------------------------------------------
## 5. G3 — main-menu skin and geometry

### 5.1 Assets (`ui/UiAssets.{h,cpp}`)
`Texture mainBackground, mainLogo, mainButton, mainButtonOn, mainDial, mainDialKnob;` from
`logo.bmp` (480×320, `src/Canvas.cpp:1382-1383`), `logo2.bmp` (170×120),
`menu_button_background.bmp` / `menu_button_background_on.bmp` (204×38),
`Menu_Dial.bmp` (45×183), `Menu_Dial_Knob.bmp` (22×24) (`src/MenuSystem.cpp:101-138`).
The Back soft key reuses `switchLeftNormal/Active` (32×32).

### 5.2 Model (`ui/MenuModel.h`)
* `MenuRow`: `int textDy = 0;  // label y nudge: +8 on label rows of 13/16/18, +7 on row 3 of 14 (src/MenuSystem.cpp:968-977, :1053-1057)`.
* `MenuViewModel`: `bool mainSkin = false; bool drawLogo = false; int dialKnobY = 0;`
  (`barRect` doubles as the dial rect when `mainSkin`).

### 5.3 Producer (`core/MenuSession.cpp`)
Constants (anonymous namespace, all `src/MenuSystem.cpp`):
```
kMainItemWidth   = 162   // setMenuSettings default, never overridden below MENU_INGAME (:4173-4178)
kMainItemPadding = 0     // (:4178)
kMainRect        = {138, 126, 204, 176}  // x = (480-204)/2 (:4241-4247); paint forces y 126 h 176 (:803-806)
kMainClip        = {0, 111, 480, 206}    // setClipRect(0, 126-15, 480, 176+30) (:804)
kMainInitRectH   = 152   // 38 << 2: menuRect[3] when initMenu tests for a scrollbar (:4245, :2753)
kDialRect        = {353, 122, 45, 183}   // x = 138 + 0xd7, y = 137 - 0xf (:2771-2778); dial size
kDialThumbLen    = 11    // 183 >> 4 (:2780-2783) -> SetScrollBox(..., C - 50, 11) (:2800-2802)
kDialKnobH       = 24
```
* `itemHeight(i)`: action rows add `kMainItemPadding` (0) when `isMainMenu(menu_)`.
* `listRect()`: `kMainRect` for main ids.
* `composeLabel`: truncation width/count use `kMainItemWidth` / `(162 - 10) / 12` on main
  ids (`:945-951`). Set `row.textDy`: label rows (`action == 0`) on `kMenuMainExit`,
  `kMenuMainDifficulty`, `kMenuMainMinigame` → 8 (`:968-977`); row index 3 on
  `kMenuMainConfirmNew` → 7 (`:1053-1057`).
* Scroll (A2 precedent, `scrollPixels()` header comment). For main ids:
  ```
  C2     = contentHeight() - 50                       // SetScrollBox content arg (:2800-2802)
  maxRaw = max(0, C2 - kMainInitRectH)                // field_0x40_ - field_0x3c_ (src/Button.cpp:417-440)
  raw    = clamp(sum of itemHeight(k) for non-hidden k < scrollIndex_, 0, maxRaw)
  drawPx = getScrollPos snap: walk non-hidden rows with posY; first row with raw < posY + h/2 -> posY;
           none -> raw                                 // isMainMenu branch (:4927-4945)
  ```
  `scrollPixels()` returns `drawPx` for main ids. Worked example (8 rows, scrollIndex 4):
  raw 184 → clamp 166 → snaps to 184 (rows 4-7 visible).
* `updateDrag`: return immediately on main ids (deviation FE-D3, `// shortcut: no touch
  drag / dial drag on the main menu; port handleUserMoved's dial branch if wanted`).
* `buildViewModel`: after `cursorOffset` is computed and before the vending branch, a main
  branch that returns:
  ```
  m.mainSkin = true;  m.drawLogo = true;                 // every main list screen (:1263,1278,1284,1348,1355)
  m.itemWidth = kMainItemWidth; m.itemPadding = kMainItemPadding;
  m.rect = kMainRect; m.clip = kMainClip; m.scrollPx = drawPx; m.rowHits = true;
  m.showBar = (menu_ == kMenuMain || menu_ == kMenuMainHelp) && contentHeight() > kMainInitRectH;
              // isMainMenuScrollBar (:2753-2785); drawScrollbar skips 13/14/16/17/18/19 (:869-879)
  if (m.showBar) {
      m.barRect = kDialRect;
      thumb  = maxRaw > 0 ? raw * (183 - kDialThumbLen) / maxRaw : 0;     // field_0x48_
      centre = thumb + (kDialThumbLen >> 1);
      m.dialKnobY = kDialRect.y + (centre * 4 / 5 - (kDialKnobH >> 1)) + 16;   // drawScrollbar (:5019-5036)
  }
  m.softLeft = (menu_ == kMenuMainDifficulty || menu_ == kMenuMainHelp) ? &softLeftText_ : nullptr;
              // button 11 only there (src/MenuSystem.cpp:4564-4580); label 3:80 dehyphenated
  m.softRight = nullptr; m.statusLine = nullptr; m.helpPopup = nullptr;    // no button 15 below MENU_INGAME
  ```

### 5.4 View (`ui/MainMenuView.{h,cpp}`, new)
`UiResult drawMainMenu(Ui& ui, const MenuViewModel& m);` — presentation half of
`MenuSystem::paint` for ids < `MENU_INGAME`. Draw order (`src/MenuSystem.cpp:764-1134`):
1. `ui.image(art.mainBackground, 240, 160, HCenter|VCenter)` (`:766`).
2. `if (m.drawLogo) ui.image(art.mainLogo, 155, 0, Top|Left)` (`:767-769`, `(480-170)>>1`).
3. Back soft key if `m.softLeft`: image `switchLeftActive` when `ui.isActive(UiId::MenuSoftLeft)`
   else `switchLeftNormal` at (10, 262); `ui.softKey(UiId::MenuSoftLeft, *m.softLeft, 8, 314,
   Left|Bottom, UiRect{10, 262, 32, 32})` (`:5212-5217, :5238`).
4. `ui.pushClip(m.clip)`.
5. Plates: walk rows from `y = -m.scrollPx` while `y < m.rect.h`; for each visible action
   row (slot ≤ 8, `UiId::MenuRow0 + slot`), not `disabled`: held → `ui.image(art.mainButtonOn, m.rect.x, y0, Top|Left)`,
   idle → `ui.imageAlpha(art.mainButton, m.rect.x, y0, 64)` (normalRenderMode 1 = BLEND25,
   highlight mode 0, `src/MenuSystem.cpp:4451-4455`, `MENU_INHERIT_BACKMENU` = 28, `src/Menus.h:75`); hit rect
   `{m.rect.x, y0, m.itemWidth (162), m.itemHeight (46)}` — no `y > 210` rule below
   MENU_INGAME (`:5090-5102`). No info buttons on main screens (`:4475-4489`).
6. Dial if `m.showBar`: `ui.image(art.mainDial, barRect.x, barRect.y, Top|Left)`,
   `ui.image(art.mainDialKnob, barRect.x + 12, m.dialKnobY, Top|Left)`.
7. Labels: same rules as `MenuView` pass 2 (centred action rows at
   `rect.x + itemWidth/2 - w/2`, i.e. centre x 219 — verbatim `:961-966`; centred label rows at
   `rect.x + rect.w/2 - w/2`; cursor `'\x8A'` at `labelX + cursorOffset + 3`, action-row y
   `y0 + 23 - 8`, anchor Right, label shifted +8; action label at `y0 + 23` anchor VCenter;
   label rows at `y0 + textDy` anchor None). No value column, no disabled overlay.
8. `ui.popClip()`; `if (m.drawLogo)` draw `mainLogo` again at (155, 0) (`:1131-1134`).
Result, one per frame: soft key → `UiAction::Back`; row hit → `ListRow` + index; wheel →
`ScrollUp/ScrollDown`.

### 5.5 `GameContext::render`
In the Menu block: `const UiResult r = mv.mainSkin ? drawMainMenu(*ui, mv) : drawMenu(*ui, mv);`
plus `#include "ui/MainMenuView.h"`.

**G3 acceptance / user screen test**
1. Main menu: the full-screen title picture (`logo.bmp`), the DOOM II RPG logo centred at the
   top, faint (25 %) button plates in a column at x 138-342 starting at y 126, labels
   centred ~21 px left of the plate centre (verbatim legacy), the `>` cursor wobbling on
   the selected row, and the dial on the right (x 353) with its knob moving down as you
   scroll past row 4. Rows that scroll above y 126 are hidden under the logo.
2. Clicking a row: the plate turns into the bright `_on` art while held, the action fires on release.
3. Difficulty screen: logo, three plates, Back switch button bottom-left with "Back"; it
   returns to the main menu with the cursor on New Game.
4. Exit / erase-save confirms: message lines centred, Yes/No plates, no dial, no Back key.
5. In-game menu looks exactly as before (regression check).

---------------------------------------------------------------------------------
## 6. G4 — character select (`ST_CHARACTER_SELECTION` 22)

### 6.1 G4a — assets, intent, model, view
**Assets** (`UiAssets`, all from `setupCharacterSelection`, `src/IntroSequenceManager.cpp:108-127`):
`charSelectBG` (`charSelectionBG.bmp` 480×320), `charSelectTopBar` (`Character_select_top_bar.bmp` 163×22),
`charSelectStatHeader` (`Character_select_stat_header.bmp` 110×16), `charSelectStatBar`
(`Character_select_stat_bar.bmp` 110×18), `charSelectAtlas` (`charSelect.bmp` 59×92),
`majorLegs, majorTorso, rileyLegs, rileyTorso, sargeLegs, sargeTorso`
(`Major_legs.bmp` … `Sarge_torso.bmp`, 176×176). Faces reuse `playerFaces`,
`playerFacesDoom`, `playerFacesScientist` (G1). `character_upperbar.bmp` is never drawn — skip.

**Intent** (`ui/UiTypes.h`): ids `CharSlot0, CharSlot1, CharSlot2, CharYes, CharBack`;
`UiAction::ScreenTouch  // index = legacy button id of a front-end screen (char select 0-4, prologue 0-2)`.

**Model** (`ui/CharacterSelectModel.h`, new): plain per-frame data, strings borrowed.
```cpp
struct CharacterSelectModel {
    int highlighted = 1;                 // stateVars[0]: character constant 1 Major / 2 Sarge / 3 Scientist
    int slotChoice[3] = { 1, 3, 2 };     // getCharacterConstantByOrder (src/IntroSequenceManager.cpp:325-339)
    const Text* title = nullptr;         // full name of `highlighted`
    const Text* shortName[3] = {};       // per slot, already "^5"-prefixed
    const Text* statRow[5] = {};         // "Defense:8" ..., already "^5"-prefixed
    const Text* prompt = nullptr;        // 3:221 wrapped at 24 on '\n'
    const Text* back = nullptr;          // 3:80
    const Text* yes = nullptr;           // 3:197
    int torsoLift = 0;                   // (upTimeMs / 1000) & 1
    bool confirmCursor = false;          // stateVars[8] == 1
    bool cursorOnYes = true;             // stateVars[2] == 1
    int cursorOffset = 0;                // OSC_CYCLE[upTimeMs / 100 % 4]
};
```

**View** (`ui/CharacterSelectView.{h,cpp}`, new): `UiResult drawCharacterSelect(Ui&, const CharacterSelectModel&)`,
port of `drawCharacterSelection` + `drawCharacterSelectionAvatar` + `drawCharacterSelectionStats`
(`src/IntroSequenceManager.cpp:341-584`). Constants, all with that file's line:
* `charSelectBG` at (0,0); top bar at (240 - 163/2 = 159, 0) (`:351-352`).
* Title at (240, 3), anchor HCenter (1) (`:373`).
* Slots `i = 0..2`, `j = 152 + 75*i` (`:375-427`):
  frame = atlas (0,0,31,83) at (j-15, 55) + same region rotateMode 4 at (j+16, 55);
  if `ui.isActive(CharSlot_i)`: `ui.panel({j-15,55,62,83}, 0xFF2896FF)` then atlas
  (31,0,27,60) at (j-11, 64) + mirrored at (j+16, 64) (`:383-389`);
  hit `ui.buttonRect(CharSlot_i, {j-15, 55, 62, 83})` (`:391`);
  links for `i < 2`: atlas (51,60,6,19) at x = j+46, j+52, j+58, y 81 (`:393-397`);
  marker when `slotChoice[i] == highlighted`: atlas (31,60,20,32) at (j-3,161) + atlas
  (31,60,19,32) mirrored at (j+17,161) (`:399-402`);
  face: region (0,0,32,32) of the slot's sheet (1 `playerFaces`, 2 `playerFacesDoom`,
  3 `playerFacesScientist`) at (j, 75); short name at (j+16, 142) anchor HCenter (`:404-430`).
* Avatar of `highlighted`: legs at (-10, 140), torso at (-10, 140 - torsoLift), anchor 0 (`:432, :464-488`).
* Stats panel (x 362, y 115): header at (360, 111); bars at (360, 129/149/169/189/209);
  rows at (362, 131/151/171/191/211), anchor Top|Left (20) (`:538-584`, `yFix = -2`).
* Prompt at (240, 235), anchor HCenter (`:435-438`; `screenRect[3] - 85`).
* Back: `ui.panel({155,260,70,30}, held ? 0xFF2896FF : 0xFF646464)`,
  `ui.frame({155,260,70,30}, 0xFF000000)` (deviation FE-D5), label at (190, 270) HCenter;
  hit `CharBack` (`:440-446`). Yes: same at (255,260,70,30), label at (290,270), hit `CharYes`
  (`:454-460`). Fill alpha is forced opaque by `Graphics::fillRect` (`src/Graphics.cpp:88-95`).
* Confirm cursor when `confirmCursor`: `ui.glyph('\x8A', x, 270, Right|Top)` with
  `x = 184 - w/2 + cursorOffset` (Back, `w` = width of `back`) or `284 - w/2 + cursorOffset`
  (Yes) (`:448-463`).
* Result: the released button → `UiAction::ScreenTouch`, index 0-2 slot, 3 Yes, 4 Back.

### 6.2 G4b — session and state
**`core/GameStates.h`**: `CharacterSelect = 22` (`src/Canvas.h:68-93`); `Action::ScreenTouch`
(comment: touch-only, latched id, the `MiniGameTouch` idiom).

**`core/CharacterSelectSession.{h,cpp}`** (new):
```cpp
class CharacterSelectSession {
public:
    struct Env {
        Player* player = nullptr;
        const Game* game = nullptr;           // difficulty 2 shows DEFENSE 0 (:530-532)
        const Localization* loc = nullptr;
        const Tables* tables = nullptr;       // oscCycle
        StateHost* host = nullptr;
        const int64_t* upTimeMs = nullptr;
        std::function<void()> backToMain;
    };
    void init(const Env& env);
    void begin(int highlighted);              // entry (src/Canvas.cpp:1166-1173)
    void handleAction(Action a);              // keys (src/IntroSequenceManager.cpp:636-694)
    void handleTouch(int buttonId);           // release (:612-634)
    void buildModel(CharacterSelectModel& m); // fills the Text buffers it owns
private:
    void choose();                            // setCharacterChoice + reset + ST_INTRO (:625-630)
    Env env_;
    int highlighted_ = 1;    // stateVars[0]
    bool confirm_ = false;   // stateVars[8] ([GEC] keyboard sub-mode)
    bool onYes_ = true;      // stateVars[2]
    Text title_, shortName_[3], stat_[5], prompt_, back_, yes_;
};
```
* `begin(h)`: `highlighted_ = h; confirm_ = false; onYes_ = true;`.
* Order step (`:644-659`): RIGHT → next of 1→3→2→1, LEFT → previous; log `[sound] 1027`.
* `handleAction` (`Action::TurnLeft/TurnRight/Use/Menu` = ACTION_LEFT/RIGHT/FIRE/MENU):
  list mode: LEFT/RIGHT step; FIRE → `confirm_ = true` (`[sound] 1086`); MENU → `env_.backToMain()`.
  Confirm mode: LEFT/RIGHT → `onYes_ = !onYes_` (`[sound] 1027`); FIRE → `onYes_ ? choose() : env_.backToMain()`
  (`[sound] 1086`); MENU → `confirm_ = false; onYes_ = true;` (`[sound] 1122`). Other actions ignored.
* `handleTouch(id)`: 0/1/2 → `highlighted_ = slotChoice[id]`; 3 → `choose()`; 4 → `env_.backToMain()`.
  One tap confirms (`:612-634`).
* `choose()`: `env_.player->characterChoice = highlighted_; env_.player->reset(); env_.host->requestState(StateId::Intro);`
  — **G4b interim** (no `Intro` state yet): instead call a `std::function<void()> finish`
  Env member wired to `loading_.startNewGameMap()`; G5 replaces it with the Intro request
  and removes `finish`.
* `buildModel`: title = 3:215 / 3:216 / 3:217 for constant 1 / 2 / 3, dehyphenated
  (`src/MenuStrings.h:264-266`, `:357-373`); short names "^5" + 3:212 / 3:213 / 3:214
  dehyphenated (`:261-263`, colour 5 resets after each string, `src/Graphics.cpp:634`);
  stats "^5" + label dehyphenated + number, labels 3:13, 3:15, 3:224, 3:223, 3:225
  (`src/MenuStrings.h:54,56,272-274`) with the table of FE §7 (1: 8/9/97/12/110,
  2: 12/14/92/6/100, 3: 8/8/87/6/150) and DEFENSE 0 when `game->difficulty() == 2`
  (`:530-532`); prompt 3:221 `wrapText(24, '\n')`, **not** dehyphenated (`:435-437`);
  back 3:80, yes 3:197, not dehyphenated (`:440-454`); `torsoLift = (*upTimeMs / 1000) & 1`;
  cursor wobble from `tables->oscCycle` like `MenuSession::buildViewModel`.

**`core/GameContext`**: member `CharacterSelectSession charSelect_;` + its Text-free wiring
in `init`; `int screenTouchId_ = -1;`.
* `enterState_(CharacterSelect)`: `pendingActions_.clear(); charSelect_.begin(oldState == StateId::Intro ? sys_.player->characterChoice : 1);`
  (prologue Back restores the previous choice, `src/IntroSequenceManager.cpp:723-726`).
* `applyUiAction`: `case UiAction::ScreenTouch: screenTouchId_ = index; queued = Action::ScreenTouch; break;`
* `tick()` dispatch, next to the TravelMap branch (before the blocked test):
  `if (state_ == StateId::CharacterSelect) { a == Action::ScreenTouch ? charSelect_.handleTouch(screenTouchId_) : charSelect_.handleAction(a); continue; }`
* `render()`: `if (state_ == StateId::CharacterSelect && ui) { CharacterSelectModel cm; charSelect_.buildModel(cm); applyUiAction(drawCharacterSelect(*ui, cm)...); }`
  (opaque 480×320, like the travel map block).

**`core/LoadingManager::startGame(true)`**: replace the G2 interim `startNewGameMap()` with
`env_.host->requestState(StateId::CharacterSelect);` (`src/MenuSystem.cpp:3603`).

**G4 acceptance / user screen test**
1. New Game → difficulty → a screen with the blue background, the top bar with
   "Kira Morgan", three framed faces (Major, Scientist, Sarge, left to right) joined by
   links, a marker under the Major, short names in colour 5 under the faces, the big
   Major figure on the left whose torso bobs 1 px every second, the stats panel on the
   right (Defense 8, Strength 9, Accuracy 97, Agility 12, IQ 110), "Choose this
   character?" and grey Back / Yes buttons.
2. Left/Right cycles Major → Riley → Sarge, updating title, figure and stats (Sarge
   Defense 12 …). On Difficult every Defense shows 0.
3. Enter → a `>` cursor appears beside Yes; Left/Right moves it to Back; Escape leaves
   confirm mode; Enter on Back → main menu.
4. Clicking a face selects it (the frame flashes blue while held); clicking Yes starts
   the game (G4b interim: straight to the travel map); the HUD face and the dialog
   portrait in the intro dialogs are the chosen character's; the log shows
   `[load] difficulty N character C`.

---------------------------------------------------------------------------------
## 7. G5 — prologue (`ST_INTRO` 9)

**`core/GameStates.h`**: `Intro = 9` (`src/Canvas.h:68-93`).
**`ui/UiTypes.h`**: ids `StoryBack, StoryMore, StorySkip`.

**`ui/PrologueModel.h`** (new):
```cpp
struct PrologueModel {
    const Text* body = nullptr; int bodyStart = 0, bodyLen = 0;   // page slice of the composed buffer
    const Text* back = nullptr;          // 3:80, dehyphenated
    const Text* bottomRight = nullptr;   // 0:56 "More", or 0:43 "Continue" on the last page
    const Text* topRight = nullptr;      // 0:40 "Skip"; null on the last page
};
```
**`ui/PrologueView.{h,cpp}`** (new): `UiResult drawPrologue(Ui&, const PrologueModel&)`,
port of `drawStory` (`src/IntroSequenceManager.cpp:260-323`):
`gameHelpBG.bmp` (reuse `art.automapBackground`, same file, `src/LoadingManager.cpp:87`) at
(0,0); back at (17, 310) anchor Left|Bottom (36); bottomRight at (463, 310) Right|Bottom (40);
topRight at (463, 10) anchor Right (8); body `ui.textRun(*body, bodyStart, bodyLen, 15, 20, 0, 21)`.
Hit rects (`src/IntroSequenceManager.cpp:42-48`; `touchArea` never moves — only
`touchAreaDrawing` does, `:289-299`, and `GetTouchedButtonID` reads `touchArea`,
`src/Button.cpp:280-303`): StoryBack {0,280,60,40} → id 0, StoryMore {380,280,100,40} → id 1,
StorySkip {420,0,60,40} → id 2 only when `topRight` (`src/TouchController.cpp:270-280`).
Release → `UiAction::ScreenTouch` + id. The `fontRenderMode(2)` dimming is a no-op in the
port (precedent `new_src/minigames/hacking/HackingView.cpp:26-27`): labels are plain.

**`core/PrologueSession.{h,cpp}`** (new):
```cpp
class PrologueSession {
public:
    struct Env { const Player* player; const Localization* loc; StateHost* host; };
    void init(const Env& env);
    void begin();                       // loadPrologueText (src/IntroSequenceManager.cpp:50-100)
    void handleAction(Action a);        // handleStoryInput (:698-750)
    void handleTouch(int id);           // stateVars[0] = id, then FIRE (src/TouchController.cpp:270-288)
    bool done() const { return page_ >= totalPages_; }   // Canvas::run (src/Canvas.cpp:848-851)
    void buildModel(PrologueModel& m);
private:
    static constexpr int kMaxPages = 16;
    Env env_;
    Text story_, back_, right_, skip_;
    int pageStart_[kMaxPages + 1] = {};
    int page_ = 0, totalPages_ = 0;
    int cursor_ = 1;                    // stateVars[0]: 0 Back, 1 More/Continue, 2 Skip
};
```
* `begin()`: `page_ = 0; cursor_ = 1;` arg = 3:218 / 3:219 / 3:220 for choice 1 / 2 / other
  (`:59-73`); `s = loc->get(kTextHelp, 12)`, `composeArgs(s, &arg, 1)` (`domain/game/Game.h:37`),
  `story_` = s verbatim (no dehyphenate, `:80`); `story_.wrapText(45)` with `'|'`
  (`(480 - 30) / 10`, `:84-86`); pages: `pageStart_[0] = 0`, then every 13th `'|'`
  (`(320 - 40) / 21`) starts a page at the char after it (`:88-96`); `pageStart_[totalPages_] = length`.
  Guard: stop adding pages at `kMaxPages` and log.
* `handleAction`: TurnLeft/TurnRight → `if (cursor_ != 2) cursor_ ^= 1`; Forward (UP) →
  `if (cursor_ != 2 && page_ < totalPages_ - 1) cursor_ = 2`; Back (DOWN) → `if (cursor_ == 2) cursor_ = 1`;
  Use (FIRE) → 0: `prev()`, 1: `++page_`, 2: `page_ = totalPages_`; Automap → `++page_; cursor_ = 1`;
  BackKey / Menu → `prev()` and, if still in Intro, `cursor_ = 0`.
  `prev()`: on page 0 → `env_.host->requestState(StateId::CharacterSelect)` (`changeStoryPage`,
  `:247-258`), else `--page_`.
* `handleTouch(id)`: ignore 2 on the last page; `cursor_ = id; handleAction(Action::Use);`.
* `buildModel`: body = current page slice `[pageStart_[page_], pageStart_[page_+1])`
  (deviation FE-D4: legacy always draws page 0, identical for the shipped one-page text);
  back = 3:80 dehyphenated; not last page → bottomRight 0:56, topRight 0:40; last page →
  bottomRight 0:43, topRight null.

**`core/GameContext`**: member `PrologueSession prologue_;` + wiring; `enterState_(Intro)`:
`pendingActions_.clear(); prologue_.begin();` (`src/Canvas.cpp:1174-1178`); tick dispatch
like CharacterSelect; state switch `case StateId::Intro: if (prologue_.done()) loading_.startNewGameMap(); break;`
(`src/Canvas.cpp:848-851` → `disposeIntro`); render block like CharacterSelect.
**`CharacterSelectSession::choose()`**: `env_.host->requestState(StateId::Intro)`; delete the
G4b `finish` interim.

**G5 acceptance / user screen test**
1. Yes on character select → a full-screen text page on the help background:
   "Mission Update … Greetings, Major Morgan. …" (Sergeant Blazkowicz / Dr. O'Connor for the
   others), "Back" bottom-left, "Continue" bottom-right (one page ⇒ no "Skip").
2. Enter or clicking Continue → travel map → map 1. Escape / Backspace / clicking Back →
   character select with the previously chosen character still highlighted.

---------------------------------------------------------------------------------
## 8. G6a — logo (`ST_LOGO` 26)

* `GameStates.h`: `Logo = 26`. `UiAssets`: `Texture logoL2;` from `l2.bmp` (136×154).
* `GameContext`: `enterState_(Logo)`: `pendingActions_.clear();`. Tick dispatch: `if (state_ == StateId::Logo) continue;`
  (only raw key 18 = shutdown does anything, `src/InputEventController.cpp:167-172`; the window close already quits).
  State switch: `case StateId::Logo: if (stateVars[0] <= 121) ++stateVars[0]; else backToMain_(); break;`
  — 122 frames of logo (`pacLogoTime` −1 … 120, `src/GameStateRunner.cpp:332-347`, `src/Canvas.cpp:60`);
  G6b changes `backToMain_()` to `setState(StateId::IntroMovie)`.
  Render: `ui->panel({0,0,480,320}, 0xFF000000); ui->image(art.logoL2, 240, 160, HCenter|VCenter);` (`src/Canvas.cpp:487-491`).
* `LoadingManager::boot(false)`: `env_.host->requestState(StateId::Logo)` instead of `env_.backToMain()`.
  The bypass (`boot(true)`) never shows it.

**G6a acceptance**: normal start shows the small logo centred on black for ~1.8 s
(122 × 15 ms), then the main menu. Capture runs and `--newgame` skip it.

---------------------------------------------------------------------------------
## 9. G6b — intro movie (`ST_INTRO_MOVIE` 2): research first

Known (FE §3): `prolog.bmp` 720×372 panned by table camera 14/15 inside a 220-px band, a
1500 ms black fade-in, then the 0:133 crawl at 20 px/s over a 192-black-faded band, music
1068, exit on crawl end or **any key** (the port forces `hasSeenIntro = true`,
`src/CAppContainer.cpp:186-188`), exit → `backToMain`. Open questions for a researcher
before a delta spec is written:
* **R1** `Game::loadTableCamera(14, 15)` (`src/Game.cpp:511-535`): full layout of tables 14
  (shorts) and 15 (bytes) after the header (keys ×7, then the ×6 block, tweens), and whether it
  can be fed to the existing `core/MayaCamera` (`setup` takes a `MapData::MayaCamera`) by
  building one camera record, or what `isTableCam` changes in `src/MayaCamera.cpp`.
* **R2** `playIntroMovie` phase 1 source/dest math with `posShift` (`src/IntroSequenceManager.cpp:795-835`)
  and the clip band; what camera x/y range the table produces.
* **R3** `initScrollingText` / `drawScrollingText` (`:173-245`) exact wrap width, line
  spacing and end time for 0:133 with the rewrite's 9-px font (the CHARACTER-count
  precedent, `new_src/core/MenuSession.cpp` NEW FACT 9).
`Tables` (`new_src/io/Tables.h`) does not load tables 14/15 yet. Do not implement G6b
before the delta.

---------------------------------------------------------------------------------
**G6b resolved (R1-R3, `docs/original-code/front-end.md` §3.4), implemented 2026-11-04.** `io/Tables` loads table 14 (`tableCamera`, 75 int16) and table 15 (`tableCameraTweens`, 252 bytes). `core/IntroMovieSession` builds a synthetic one-camera `MapData` (keys channel-major, tween indices key-major, tweenCounts from `t[4+13n..]`, posShift `4 - t[2]`) and plays it through `core/MayaCamera`, advancing keys itself: first frame snaps key 0; when `elapsed >= ms[k]` snap key `k+1` and restart the clock; last key = pan complete. Phases on `upTimeMs` (deviation: legacy uses gameTime/app->time): 0 — log `[sound] 1068`, 1500 ms black fade-in (alpha `255 - (t<<8)/1500`), plain black (what legacy draws here is open); 1 — blit `prolog.bmp` at `(-max(0, camX-240), 160-camY)` clipped to (0,50,480,220); 2 — stamp crawl start; 3 — crawl: `prolog.bmp` at (0,-65) in the band, black wash alpha 63, 0:133 wrapped at 42 chars, `label(240, 300-off, HCenter, lineH 32)`, `off = ((t<<8)/1600*(32<<8))>>16`, ends when `t > 1600*(lines+1+8)` (28800 ms). Any key/click skips. Exit: `[sound] stop`, `hasSeenIntro = true`, `saveConfig()`, `backToMain`.

## 10. Deviations

| Id | What | Why |
|---|---|---|
| FE-D1 | Continue with an unreadable save opens `MENU_INGAME_LOADNOSAVE` (50) instead of legacy `Error()` | same as SL-D2 for in-game Load (ADR 0037 §5); the screen uses the in-game skin |
| FE-D2 | Options (17/35) lists only Sound (`options-sound.md`, ADR 0050); the `[GEC]` Video and Input rows are not ported (OPT-D1); Other Games is unreachable. Formerly Options refused on select. Game Help/About (G1), Credits (G2), Mini Games (G3), Comic Book (G4) are ported by `main-menu-subscreens.md`, each group deleting its refusal | shipped row 17 has 0 items and every `src/` Options sub-menu is `[GEC]` (FE §12.6), so there is nothing original to port (ADR 0044 §5) |
| FE-D3 | No touch drag / dial drag on main screens | key, wheel and click cover the 7-8 rows |
| FE-D4 | Prologue draws the current page slice, legacy always page 0 | identical output for the shipped text |
| FE-D5 | Char-select button outline colour black | legacy uses `Graphics::curColor`, not traceable statically (`src/Graphics.cpp:236-241`) — **unverified** |
| FE-D6 | World pass skipped while `loadMapID == 0` | legacy has no map there at all |
| FE-D7 | Comic Book LEFT/RIGHT slide at 8 px/frame (`max(|midPoint|, 8)`) | legacy flips in one frame only because pixel ints reach the normalised-float `userMoved` (`src/CAppContainer.cpp:71-89`, `src/ComicBook.cpp:972-994`); spec `main-menu-subscreens.md` §5.5 |
| FE-D8 | Comic frames resident from startup (39 × 480×320 R8 = 5.99 MB), no "Loading" screen, 17 portrait pages never loaded | the loading screen only amortised the iPhone decode; portrait mode is dead (FE §12.1); ADR 0044 §4 |
| FE-D9 | Credits crawl clock = `upTimeMs` | legacy `gameTime`; same choice as the G6b intro crawl |
| FE-D10 | main-menu Exit / Backspace quit immediately, no Exit Game? confirm | user decision 2026-11-04 |
| (port) | Main-menu labels centre on x 219, not on the 204-px plate | verbatim `menuItem_width` 162 (`src/MenuSystem.cpp:961-966`) |

---------------------------------------------------------------------------------
## 11. Test matrix for the reviewer
* `--newgame` and `DOOM2RPG_CAPTURE_TICKS=200,400 ./DoomIIRPG` reach map 1 with no front-end
  state in the log, same tick timeline as before G2 (goldens unchanged).
* No `quitRequested_ = true` left on any BACKTOMAIN / save-exit path; still set by ACTION_EXIT,
  `0x40` and window close.
* `grep -n "Action::ScreenTouch"`: produced only by `applyUiAction`, consumed only in the two
  front-end dispatch branches.
