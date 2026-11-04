# Main-menu sub-screens: Game Help + About, Credits, Mini Games, Comic Book (Options stays refused)

Date: 2026-11-04. ADR: `adr/0044-main-menu-subscreens.md`. Parent spec: `front-end.md`
(this spec retires most of its deviation FE-D2). Facts: `docs/original-code/front-end.md` §5, §12
(cited **FE §n**), raw report `docs/research/2026-11-04-main-menu-subscreens.md`.
`src/MenuSystem.cpp` is ISO-8859 + CRLF: use `grep -a` there.

Scope, in delegation order: **G1** Game Help list + 10 help leaves + About; **G2** Credits
(`ST_CREDITS` 15); **G3a/G3b** Mini Games from the main menu; **G4a/G4b** Comic Book.
**Options (17) stays refused** (§6). No new module kinds: menu screens stay `MenuSession`
screens (ADR 0013/0043), the two non-menu screens are a peer session (Credits) and a
`GameContext`-owned delegate of menu id 89 (Comic Book), exactly like the legacy
`app->comicBook` (FE §12.1).

Build: G2 and G4a add files ⇒ `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` once, then
`cmake --build build_new -j 8`. Every group builds and runs on its own.

---------------------------------------------------------------------------------
## 1. Groups

| Group | Content | Files |
|---|---|---|
| **G1** | Game Help (4) list, 10 main help leaves, About (8) | `io/MenuData.h`, `core/MenuSession.cpp` |
| **G2** | Credits state | `core/GameStates.h`, `core/CreditsSession.{h,cpp}` (new), `ui/IntroMovieModel.h`, `ui/IntroMovieView.{h,cpp}`, `core/MenuSession.{h,cpp}`, `core/GameContext.{h,cpp}` |
| **G3a** | `playFromMainMenu` in the host + 3 games (no caller yet) | `minigames/common/MiniGameHost.{h,cpp}`, `minigames/hacking/HackingGame.{h,cpp}`, `minigames/sentrybot/SentryBotGame.{h,cpp}`, `minigames/vending/VendingMachine.{h,cpp}` |
| **G3b** | Mini Games menu (18), `ACTION_MINIGAME`, `back()` empty-stack rule | `core/MenuSession.{h,cpp}`, `core/GameContext.cpp` |
| **G4a** | comic frames, model, view, session (no caller yet) | `ui/UiAssets.{h,cpp}`, `ui/ComicBookModel.h` (new), `ui/ComicBookView.{h,cpp}` (new), `core/ComicBookSession.{h,cpp}` (new) |
| **G4b** | menu id 89 wiring | `core/MenuSession.{h,cpp}`, `core/GameContext.{h,cpp}` |

---------------------------------------------------------------------------------
## 2. G1 — Game Help (4), help leaves, About (8)

Everything reuses the in-game help machinery (`helpResourceFor`, `loadHelpResource`,
type 5 scrolling, `select()` type-5 → `back()`) and the main skin (`MainMenuView`), which
already draws label rows, a cursor-less list, the dial and the Back switch. **`MainMenuView`
is not touched.**

### 2.1 `io/MenuData.h`
Add to `MenuId` (`src/Menus.h:220-241`): `kMenuMainArmorHelp = 5, kMenuMainEffectHelp = 6,
kMenuMainItemHelp = 7, kMenuMainGeneral = 9, kMenuMainMove = 10, kMenuMainAttack = 11,
kMenuMainSniper = 12, kMenuMainHackerHelp = 20, kMenuMainMatrixSkipHelp = 21,
kMenuMainPowerUpHelp = 22`.

### 2.2 `core/MenuSession.cpp`
**Constants** (anonymous namespace, next to `kMainRect`):
```
kMainHelpRect      = {85, 0, 300, 320}   // paint: setMenuDimentions(85,0,300,320) for the 11 leaves (src/MenuSystem.cpp:807-845)
kMainHelpClip      = {0, 0, 480, 320}    // setClipRect(screenRect[0], menuRect[1], screenRect[2], menuRect[3]) (:852)
kMainHelpInitRectH = 281                 // setMenuSettings (77,10,323,281): the height initMenu tests (:4248-4264, :2753)
kMainHelpDialRect  = {408, 81, 45, 183}  // 0x198, 0x51 + Menu_Dial size (:2773-2776)
kMainHelpWrap      = 35                  // wrapText(menu >= MENU_INGAME ? ... : 35) (:3674), a CHARACTER count
```

**`helpResourceFor`**: add the main ids (FE §12.2 table, `src/MenuSystem.cpp:1288-1344,
:1466-1486`): `9→1, 10→2, 11→3, 12→7, 5→4, 7→6, 6→5, 20→8, 21→9, 22→10, 8→11`. This alone makes
them `isCodeBuiltScreen`, so the GOTO rows of menu 4 and the About row stop being refused by the
"built in code" gate.

**Helper**: `bool isMainHelpLeaf(int id) { return isMainMenu(id) && helpResourceFor(id) > 0; }`.

**Refusals (FE-D2 shrinks)**: replace `isUnportedMainScreen` by
```cpp
// Main-menu GOTO targets the rewrite does not open (deviation FE-D2). nullptr = ported.
const char* unportedMainScreenReason(int menuId) {
    if (menuId == kMenuMainOptions)   return "MENU_MAIN_OPTIONS ships 0 rows; src/ fills it with [GEC] menus (front-end.md FE-D2)";
    if (menuId == kMenuMainMoreGames) return "Other Games is removed from MENU_MAIN (front-end.md FE-D2)";
    if (menuId == kMenuComicBook || menuId == kMenuMainMinigame)
        return "main-menu screen not ported yet (main-menu-subscreens.md)";   // G3b / G4b delete these
    return nullptr;
}
```
and in the refusal loop `else if (const char* why = unportedMainScreenReason(param)) disableRow(i, why);`.

**`initMenu`** — two new branches before the final `else`:
```cpp
} else if (menuId == kMenuMainHelp) {              // (:1282-1287)
    addParsedRows(menuId, 1, -1);                  // row 0 "Back" dropped
}
```
and in the existing help branch (`helpResourceFor(menuId) > 0`, which now also takes the main ids):
```cpp
type_ = kMenuTypeHelp;
loadHelpResource(helpResourceFor(menuId));
if (menuId == kMenuMainEffectHelp)                 // (:1295-1301); the in-game 44 has none (:1692-1696)
    addItem(kEmptyTextId, kItemNoSelect, kActionNone, 0, kEmptyTextId);
```

**`loadHelpResource`**: `const bool mainLeaf = isMainMenu(menu_);`
* `helpText_.wrapText(mainLeaf ? kMainHelpWrap : kHelpMaxChars);` (:3674)
* the over-height re-wrap block runs only `if (!mainLeaf && kHelpInitRectH < h)` (:3684, second
  half of the legacy condition `menu >= MENU_INGAME`). Fix the two comments that say "never the 35".
* the log line prints the wrap actually used.

**`listRect()`**: `if (isMainMenu(menu_)) return isMainHelpLeaf(menu_) ? kMainHelpRect : kMainRect;`
(so `viewPx()` = 320 and the divider count `300 / kCharSpacing` = 27 = legacy `:909`).

**`scrollPixels()`**: the snapped main branch only `if (isMainMenu(menu_) && !isMainHelpLeaf(menu_))`
— legacy `isMainMenu` is false for type 5 (`:2813`), so the leaves use the generic index→pixel
path clamped to `[0, contentHeight() - 320]` (A2/A3 precedent), and `maxScrollIndex()` already
uses the same `viewPx()`.

**`buildViewModel`**, main branch (replace the body, keep the order):
```cpp
const bool leaf = isMainHelpLeaf(menu_);
m.mainSkin = true;
m.drawLogo = !leaf;                                  // leaves: drawLogo = false (:1288-1344)
m.itemWidth = kMainItemWidth; m.itemPadding = kMainItemPadding;
m.rect = leaf ? kMainHelpRect : kMainRect;
m.clip = leaf ? kMainHelpClip : kMainClip;
m.scrollPx = scrollPixels();
m.rowHits = true;
if (leaf) {
    m.showBar = contentPx > kMainHelpInitRectH;      // isMainMenuScrollBar (:2753, :2773-2776)
    if (m.showBar) {
        m.barRect = kMainHelpDialRect;
        const int maxScroll = contentPx - viewPx();  // A2: the knob follows the index model's clamp
        const int thumb = maxScroll > 0 ? m.scrollPx * (kDialRect.h - kDialThumbLen) / maxScroll : 0;
        const int centre = thumb + (kDialThumbLen >> 1);
        m.dialKnobY = kMainHelpDialRect.y + (centre * 4 / 5 - (kDialKnobH >> 1)) + 16;   // (:5019-5036)
    }
} else {
    /* existing MAIN / MAIN_HELP dial block, unchanged */
}
// Button 11 (:4564-4580): 4, 5-12, 16, 18, 20-22. G3b relies on kMenuMainMinigame being here.
if (menu_ == kMenuMainDifficulty || menu_ == kMenuMainHelp || menu_ == kMenuMainMinigame || leaf) { ...softLeft as today... }
```
Input needs nothing new: FIRE on type 5 → `back()` (`select`, `:3004-3011`), BACK/MENU →
`back()`, which plays 1122 and pops to 4 (leaf) or 3 (About) with the saved cursor (`:591-602`).
UP/DOWN/wheel scroll one line (`moveDir` type-5 branch).

### 2.3 G1 acceptance (user screen test)
1. Main menu → Game Help: logo + logo2 on top, 11 plates (How to Play … Power-Up, Credits),
   dial on the right; Back switch bottom-left. Credits logs `refused` (until G2).
2. How to Play: the title picture **without** the DOOM II RPG logo, text in a column starting at
   x 85, about 35 characters wide, from the very top of the screen; dial at x 408 when the page is
   longer than ~17 lines; UP/DOWN scroll a line; Back switch bottom-left.
3. FIRE, Backspace, Escape, or the Back switch → back on Game Help with the cursor on the same row.
4. Effects List: same; the page ends with one extra blank line (only visible at max scroll).
5. About (from the main menu root): customer-service and copyright text; back lands on About.
6. Log: `[menu] help N: L lines, wrap 35, rect w 300` for each leaf; in-game Help leaves still
   log `wrap 49` / re-wrap 37 (regression).

---------------------------------------------------------------------------------
## 3. G2 — Credits (`ST_CREDITS` 15)

### 3.1 `core/GameStates.h`
`Credits = 15, // legacy ST_CREDITS: 2:0 crawl on black (src/Canvas.cpp:1161-1165, docs/original-code/front-end.md §12.4)`.
Add the case to every `switch (StateId)` in `GameContext` (enter, tick → `break`).

### 3.2 `ui/IntroMovieModel.h` + `ui/IntroMovieView.{h,cpp}` (extend, no new ui files)
```cpp
// Per-frame model of ST_CREDITS (front-end.md §12.4). Text borrowed for THIS frame.
struct CreditsModel {
    const Text* text = nullptr;            // 2:0 wrapped at 42
    int textY = 250;                       // cinRect[3] - offset
    const Text* continueLabel = nullptr;   // 0:43 dehyphenated; non-null once scrollingTextDone
};
UiResult drawCredits(Ui& ui, const CreditsModel& m);
```
`drawCredits` (drawCredits + drawScrollingText for ST_CREDITS, `src/IntroSequenceManager.cpp:190-245`):
1. `ui.panel({0,0,480,320}, 0xFF000000)` — `eraseRgn(displayRect)`; no prolog, fade, band or clip (`:229-238`).
2. `ui.label(*m.text, 240, m.textY, Graphics2D::kAnchorHCenter, 16)` — `drawString(buf, SCR_CX, y, 16, 1)` (`:243`).
3. `if (m.continueLabel) ui.label(*m.continueLabel, 216, 288, Graphics2D::kAnchorVCenter)` —
   `(SCR_CX - 24, screenRect[3] - 32)`, anchor 2 = VCENTER (`:195-203`, `src/Graphics.h:13-19`).
4. `if (ui.in().released) res.action = UiAction::Activate;` — a touch release in ST_CREDITS falls
   to key 6 = FIRE (`src/TouchController.cpp:336-338` → `:311-313`).

### 3.3 `core/CreditsSession.{h,cpp}` (new)
```cpp
class CreditsSession {
public:
    struct Env { const Localization* loc = nullptr; const int64_t* upTimeMs = nullptr; };
    void init(const Env& env);
    void begin();                         // setState(ST_CREDITS): initScrollingText(2, 0, false, 16, 5, 500)
    void buildModel(CreditsModel& m);     // drawCredits; latches done_ like drawScrollingText
private:
    Env env_;
    Text text_, continue_;
    int64_t start_ = 0;
    int end_ = 0;                         // scrollingTextEnd, ms
    bool done_ = false;                   // scrollingTextDone
};
```
Constants and formulas (`src/IntroSequenceManager.cpp:163-245`):
```
kSpacing   = 16
kMsPerLine = 500 * ((16 << 16) >> 4) >> 16 = 500            // n (:211)
begin():  text_ = loc->get(kTextHelp, 0); text_.wrapText((480 - 8) / 11);   // 42, (:180); dehyphenate=false
          lines = text_.getNumLines() + 5                    // (:182)
          end_  = kMsPerLine * lines                          // ST_CREDITS: NO + (320/16 - 2) term (:214-215)
          start_ = *upTimeMs; done_ = false                   // deviation FE-D9: upTimeMs, legacy gameTime
          continue_ = loc->get(kTextMain, 43), dehyphenate()  // (:197-199)
buildModel(): t = *upTimeMs - start_; if (t > end_) { t = end_; done_ = true; }   // hold the end (:222-226)
          off = (((t << 8) / kMsPerLine) * (kSpacing << 8)) >> 16           // ≈ t / 31.25, 32 px/s (:243)
          m.text = &text_; m.textY = 250 - off; m.continueLabel = done_ ? &continue_ : nullptr
```
(`cinRect[3] = 250`, `src/Canvas.cpp:113-154`.) No sound anywhere in the session.

### 3.4 `core/MenuSession.{h,cpp}`
* Refusal loop: `case kActionChangeState: if (param != 6 && param != 15) disableRow(i, "no rewrite state for this ACTION_CHANGESTATE");`
* `select()`:
  ```cpp
  case kActionChangeState:                           // (src/MenuSystem.cpp:3086-3093)
      if (env_.host == nullptr) break;
      if (items_[i].def.param == 6) env_.host->requestState(StateId::Automap);
      else if (items_[i].def.param == 15) {
          if (env_.game != nullptr && env_.game->loadMapID != 0) std::fprintf(stderr, "[sound] 1071\n");   // in-game only
          env_.host->requestState(StateId::Credits);
      }
      break;
  ```
* New public `void returnFromCredits();` — the ST_CREDITS BACK/FIRE handler's menu half
  (`src/InputEventController.cpp:398-412`):
  ```cpp
  void MenuSession::returnFromCredits() {
      helpIndex_ = -1; infoIndex_ = -1;
      clearStack();                                  // leaving ST_MENU emptied it (src/Canvas.cpp:1033-1035)
      if (env_.game == nullptr || env_.game->loadMapID == 0) {
          const int idx = 3 + ((env_.hasSavedState && env_.hasSavedState()) ? 1 : 0);
          pushMenu(kMenuMain, idx, 0);               // verbatim quirk: idx 3/4 is "Mini Games" (FE §12.4)
          setMenu(kMenuMainHelp);                    // menu_ is still 4 -> selectedIndex/scrollIndex kept (:1226-1229)
      } else {
          pushMenu(kMenuInGame, 7, 0);
          setMenu(kMenuHelp);                        // menu_ is still 37 -> cursor kept
      }
  }
  ```

### 3.5 `core/GameContext.{h,cpp}`
* Member `CreditsSession credits_;`, init `{ sys_.loc, &upTimeMs }` next to `introMovie_`.
* `enterState_`: `case StateId::Credits: pendingActions_.clear(); credits_.begin(); break;`
  `case StateId::Menu:` → `if (oldState == StateId::Credits) menu_.returnFromCredits(); else menu_.begin(nextMenuId_);`
  then `nextMenuId_ = kMenuInGame;` as today.
* `tick()` input loop, next to the IntroMovie branch (before the blocked-drop break):
  ```cpp
  // ST_CREDITS (not ending the game): BACK or FIRE at any time (src/InputEventController.cpp:387-414).
  if (state_ == StateId::Credits) {
      if (a == Action::BackKey || a == Action::Use) {
          if (sys_.game->loadMapID != 0) std::fprintf(stderr, "[sound] stop\n");   // in-game branch only (:408)
          setState(StateId::Menu);
      }
      continue;
  }
  ```
  `tick()` state switch: `case StateId::Credits: break;` (excluded from the update chain, `src/Canvas.cpp:827`).
* `render()`, after the intro-movie block: `if (state_ == StateId::Credits && ui) { CreditsModel cm; credits_.buildModel(cm); applyUiAction(drawCredits(*ui, cm).action, -1); }`.

### 3.6 G2 acceptance
1. Game Help → Credits: black screen, centred credit lines rising from y≈250 at about 32 px/s
   ("Id Software LLC, a division of ZeniMax Media Inc.", "•••• CREDITS ••••", "Producer" …).
   No music log.
2. At the end ("Thanks for Playing!" has stopped around mid-screen) "Continue" appears at the
   bottom, slightly left of centre.
3. Enter / Backspace / a click — at any time, also mid-scroll — returns to Game Help with the
   cursor still on Credits. Backspace there lands on the main menu with the cursor on
   **Mini Games** (verbatim quirk, not a bug).
4. If the in-game Help list has a Credits row: in game it logs `[sound] 1071`, shows the same
   crawl, and BACK returns to the in-game Help list (cursor kept) with `[sound] stop`.

---------------------------------------------------------------------------------
## 4. G3 — Mini Games (18) from the main menu

### 4.1 Why an explicit flag, not `thread == nullptr`
Legacy derives `gamePlayedFromMainMenu = (callingThread == nullptr)` (`src/HackingGame.cpp:61-62`,
`src/SentryBotGame.cpp:46-47`, `src/VendingMachine.cpp:42-43`). The rewrite's debug keys
(V/H/J, `new_src/core/GameContext.cpp:437-451`) already start games with `nullptr` and expect
the world tail. So the host carries `fromMainMenu_`, set only by `playFromMainMenu`.

### 4.2 G3a — `minigames/common/MiniGameHost.{h,cpp}`
```cpp
// ACTION_MINIGAME (src/MenuSystem.cpp:3208-3221): the game's playFromMainMenu with no thread.
// False = unknown type, nothing changed.
bool playFromMainMenu(int type);
bool fromMainMenu() const { return fromMainMenu_; }   // legacy gamePlayedFromMainMenu
private: bool fromMainMenu_ = false;
```
* `start()`: add `fromMainMenu_ = false;` next to `callingThread_ = t;`.
* `playFromMainMenu(type)`: pick the game exactly like `start()`; `callingThread_ = nullptr;
  active_ = g; touchButton_ = -1; fromMainMenu_ = true; env_.host->requestState(StateId::MiniGame);`
  then `hacking_.playFromMainMenu()` / `sentry_.playFromMainMenu()` / `vending_.playFromMainMenu()`.
* `finish(result)`, new head (`src/HackingGame.cpp:631-634`, `src/SentryBotGame.cpp:820-823`,
  `src/VendingMachine.cpp:859-862`):
  ```cpp
  if (fromMainMenu_) {          // no scriptStateVars[7], no thread, no ST_PLAYING
      fromMainMenu_ = false; active_ = nullptr; callingThread_ = nullptr;
      std::fprintf(stderr, "[sound] 1071\n");        // Music_Title, looped
      env_.openMenu(kMenuMainMinigame);              // setMenu(18) with an empty stack
      return;
  }
  ```
  (`#include "io/MenuData.h"` if not already reachable.)
* Fix the class comment ("world branch only").

**What assumed a world, and what this group does about it** (loadMapID 0, no thread, map data
stale but resident, ADR 0035): `vm->vars[7]` + `resumeThread` → skipped by the head above;
`evaluateResults` (Hud message, IQ) → not called on the main path; `game->loadMapID` (vending
machine number) → bypassed by `VendingMachine::initGame(1, 1)`; sentry `award()`
(`game->attemptToDiscardFamiliar`, `dialogs`) → unreachable on the main path; `player` → always
resident (reset by `backToMain_`). Rendering needs nothing: every minigame screen opens with an
opaque 480×320 blit and the world pass is already skipped at `loadMapID == 0` (FE-D6).

### 4.3 G3a — `HackingGame`
* `void playFromMainMenu();` (`src/HackingGame.cpp:28-33`):
  `const int rnd = ItemPickup::nextInt() % 3 + 3; begin(0, rnd, ItemPickup::nextInt() % (rnd * 10 - 15) + 5);`
  (3..5 columns; turns 5..19 / 5..29 / 5..39).
* `begin`: `const int iq = host_->fromMainMenu() ? 0 : host_->env().player->ce.getIQPercent();` (`:64`).
  With IQ 0 the scramble count equals the turns (`(10000t + t·100·100)/20000`).
* `endGame`: `if (!host_->fromMainMenu()) host_->evaluateResults(n);` (`:623-626`); sound + `finish` unchanged.
* Header comment: drop "no playFromMainMenu".

### 4.4 G3a — `SentryBotGame`
* `void playFromMainMenu() { begin(0, -1, -1); }` (`src/SentryBotGame.cpp:29-31`).
* `generate()`: `iq = host_->fromMainMenu() ? 0 : p.ce.getIQPercent();`
  `allowSkip = host_->fromMainMenu() || iq < 50;` (`:55`, `:61`). The begin log prints `iq`.
* `input()`, rule 0 (`:130-133`):
  `if ((result_ == kLost || (result_ == kWon && host_->fromMainMenu())) && (action == kAutomap || action == kBack || action == kFire)) { endGame(0); return; }`
  — verbatim: a main-menu **win** ends with `endGame(0)`, i.e. sound 1040.
  The won branch (bot choice, `award()`) becomes `} else if (result_ == kWon && !host_->fromMainMenu()) {` (`:271-272`).
* `endGame`: `if (!host_->fromMainMenu()) host_->evaluateResults(n);` (`:811-814`).
* `draw()`: the Success screen branch becomes `result_ == kWon && !host_->fromMainMenu()`; add
  before the failure `else`:
  ```cpp
  } else if (result_ == kWon) {            // main-menu win (drawSuccessScreen, :380-395)
      // Same layout as the botType-0 failure screen: 177 at y 15, 239 at y 71, 188 wrapped 22 at y 139.
      model_.screen = SentryBotModel::Screen::Failure;
      composeLine(loc, kStrAccepted, title_);           model_.title = &title_;
      composeLine(loc, kStrHackSuccessful, subtitle_);  model_.subtitle = &subtitle_;   // 239, new constant
      model_.warning = false;
      message_.setLength(0); compose(loc, kStrLoggedOut, message_); message_.wrapText(22, '\n');
      model_.message = &message_; model_.messageY = 139;
      model_.guessY = 26; model_.answerCursor = -1;
  }
  ```
  (legacy: `n2 = 59` → 239 at `59+12`, 188 at `59+80`; failure botType 0: 240 at `59+12`, 188 at
  `n4 + 80` with `n4 = 59`, `:330-367` — identical positions, only the two strings differ.)
  Touch on that screen: `handleTouch` already maps `result_ == kWon` → `input(kFire)` → rule 0,
  which is the legacy `wasTouched` release → FIRE (`:397-407`).
* Header comment: drop "no playFromMainMenu".

### 4.5 G3a — `VendingMachine`
* Split `begin(b1)` into
  ```cpp
  void VendingMachine::begin(int b1, int, int) {           // initGame(loadMapID, (loadMapID-1)*2 + b1)
      const int loadMapID = host_->env().game->loadMapID;
      initGame(loadMapID, (loadMapID - 1) * 2 + b1);
  }
  void VendingMachine::playFromMainMenu() {                // (src/VendingMachine.cpp:33-37)
      initGame(1, 1);
      screen_ = kScreenHelp;                               // stateVars[1] = 0: help first, no buy terminal
      result_ = 0;
  }
  ```
  `initGame(int map, int machine)` (private) = today's `begin` body with the clamps applied to
  its arguments, plus (`:55-56`):
  `triesLeft_ = host_->fromMainMenu() ? 4 : e.player->getVendingMachineTriesLeft(currentMachineNumber_);`
  `machineHasBeenHacked_ = !host_->fromMainMenu() && e.player->vendingMachineIsHacked(currentMachineNumber_);`
* `randomizeGame`: `n = host_->fromMainMenu() ? 0 : getIQPercent()` (`:150`).
* `handleInputForHelpScreen`: `Menu`/`BackKey` → `if (host_->fromMainMenu()) endGame(2); else screen_ = kScreenTerminal;` (`:231-241`).
* `handleInputForGame` (`:262-345`):
  - result 2, `Use`, `triesLeft_ == 0` → `fromMainMenu ? endGame(result_) : screen_ = kScreenTerminal`;
  - result 1, `Use` → `fromMainMenu ? endGame(result_) : (terminal + justHacked + hacked + setVendingMachineHack)`;
  - submit: win → IQ bonus only `if (!fromMainMenu)`; loss → `removeOneVendingMachineTry` only `if (!fromMainMenu)`.
* `buildGameResult` (`:421-447`): hacked → `fromMainMenu ? 239 : (236 / 237)`; `triesLeft_ > 0` → 209 + n;
  else `fromMainMenu ? 240 : 201`. New constants `kStrHackSuccessful = 239`, `kStrHackFailedMain = 240`.
* `endGame` unchanged (it only calls `finish`; no 1043/1040 here, `:850-864`).
* Header comment: drop "no playFromMainMenu".

**G3a acceptance**: builds; the V/H/J debug keys in game behave exactly as before (they go
through `start()`, `fromMainMenu_ == false`).

### 4.6 G3b — `core/MenuSession.{h,cpp}` + `core/GameContext.cpp`
* `Env`: `std::function<void(int type)> playMiniGame;   // ACTION_MINIGAME -> MiniGameHost::playFromMainMenu`
* `unportedMainScreenReason`: delete `kMenuMainMinigame`.
* `initMenu`, before the final `else`:
  ```cpp
  } else if (menuId == kMenuMainMinigame) {          // (:1275-1281)
      addParsedRows(menuId, 1, -1);                  // "Back" dropped
  }
  ```
  per-screen patch block: `else if (menuId == kMenuMainMinigame) selectedIndex_ = 1;` (`:1276`; the
  initMenu tail moves it onto the first selectable row, Hacker).
* `select()`:
  ```cpp
  case kActionMinigame:                              // (src/MenuSystem.cpp:3208-3221)
      if (items_[i].def.param == 0 || items_[i].def.param == 2 || items_[i].def.param == 4) {
          std::fprintf(stderr, "[sound] stop\n");    // soundStop()
          if (env_.playMiniGame) env_.playMiniGame(items_[i].def.param);
      }
      break;
  ```
* `back()`, after the return-to-game branch:
  `else if (menu_ == kMenuMainMinigame || menu_ == kMenuComicBook) setMenu(kMenuMain);` (`:608-610`).
  Update the function comment.
* Back switch on 18 is already in the G1 soft-key set.
* `GameContext::init`: `menuEnv.playMiniGame = [this](int type) { minigames_.playFromMainMenu(type); };`

### 4.7 G3 acceptance
1. Main menu → Mini Games: logo, three plates Hacker / Matrix Skip / Power-Up, cursor on Hacker,
   Back switch bottom-left, no dial. Backspace → main menu, cursor on Mini Games.
2. Hacker: log `[sound] stop`, `[hacking] begin cols=3..5 turns=… iq=0 scrambles=<turns>`, the help
   screen first. Win or lose: `[sound] 1043`/`1040`, then `[sound] 1071`, back on Mini Games with
   the cursor on Hacker; Backspace from there → main menu (empty stack, cursor on Continue/New Game).
   No IQ message, no `vars[7]`.
3. Matrix Skip: help first; after a correct code the result screen shows the title 0:177,
   "Hack Successful!" (0:239) and the logged-out text — **no** bot choice; FIRE/
   Backspace/click → `[sound] 1040`, `[sound] 1071`, menu 18.
4. Power-Up: opens on the **help** screen (not the buy terminal); 4 tries; Backspace on help → menu
   18; result text "Hack Successful!" (0:239) or after 4 misses "Hack Failed!" (0:240); FIRE →
   `[sound] 1071`, menu 18.
5. In game, V/H/J debug games and EV_MINIGAME terminals behave as before.

---------------------------------------------------------------------------------
## 5. G4 — Comic Book (menu id 89)

### 5.1 Shape (ADR 0044 §4)
89 stays a `MenuSession` menu id with **0 rows** (legacy `initMenu` case 89 is empty,
`src/MenuSystem.cpp:2276-2278`), so `gotoMenu` / the stack / `back()` keep their legacy
semantics for free. `GameContext` owns a `ComicBookSession comic_` and, while the menu id is 89,
routes rendering and keys to it before the menu — the legacy `paint`/`handleMenuEvents`/touch
hooks (`:721-724`, `:2834-2836`, `:4662-4665`, `:4832-4833`). Only the 39 landscape frames exist
for the port (the portrait mode is dead, FE §12.1); the 17 portrait pages are not loaded.

### 5.2 G4a — `ui/UiAssets.{h,cpp}`
`Texture comicFrames[39];   // ComicBook/frames/*.bmp, 480x320 4-bit (src/ComicBook.cpp:323-440)`,
loaded at the end of `UiAssets::load` from a static name table in this exact order
(index = legacy `imgiPhoneComicBook` slot):
`"iPhone cover"`, then `"iPhone page "` + `1a 1b 2a 2b 2c 3a 3b 4a 4b 4c 5a 5b 5c 6a 6b 6c 7a 7b 8a
8b 8c 9 10a 10b 10c 11a 11b 12a 12b 13a 13b 14a 14b 15a 15b 15c 16a 16b`, each as
`"ComicBook/frames/<name>.bmp"` via `loadUiTexture` (the resource reader prefixes
`Payload/Doom2rpg.app/Packages/`). **Loaded once at startup, resident** (deviation FE-D8):
39 × 480 × 320 R8 index texels = 5.99 MB GPU (+ 39 KB LUTs), no CPU copy kept; the legacy
one-image-per-frame "Loading" screen (`:103-121`, `:216-258`) existed only to spread the iPhone
decode and is not reproduced.

### 5.3 G4a — `ui/ComicBookModel.h` (new)
```cpp
// Per-frame model of the comic viewer (menu id 89, src/ComicBook.cpp:88-214). Text borrowed for THIS frame.
struct ComicBookModel {
    int frame = 0;                 // iPhonePage, 0..38
    int x = 0;                     // endPoint: the current frame's left edge
    bool neighbours = false;       // dragging || sliding (field_0x10 || field_0x110)
    int next = 1, prev = 38;       // wrapped neighbour indices
    bool doneButton = false;       // drawExitButton
    bool doneHighlighted = false;  // exitBtnHighlighted
    const Text* doneLabel = nullptr;
};
```

### 5.4 G4a — `ui/ComicBookView.{h,cpp}` (new)
`UiResult drawComicBook(Ui& ui, const ComicBookModel& m);` — stateless, returns `{}` (all comic
input is read by the session, the `MiniGame::draw` idiom). Draw order (`Draw`, `:137-214`;
`DrawExitButton`, `:930-970`):
1. `ui.panel({0,0,480,320}, 0xFFFFFFFF)` — `glClearColor(1,1,1,1)` (`:137-138`).
2. `ui.image(art.comicFrames[m.frame], m.x, 0, Top|Left)`.
3. `if (m.neighbours)`: `comicFrames[m.next]` at `m.x + 480`, then `comicFrames[m.prev]` at `m.x - 480` (`:164-209`).
4. `if (m.doneButton)`: `ui.panel({9, 9, 62, 32}, 0xFF000000)`; `ui.panel({10, 10, 60, 30}, m.doneHighlighted ? 0xFF9696FF : 0xFF3232FF)`;
   `ui.label(*m.doneLabel, 40, 25, HCenter|VCenter)`.
   Colour note: `fillRect(..., (v << 8) | (v << 16) | 0xFF)` with v = 50 / 150 and `fillRect`
   reading the int as ARGB with alpha forced to 1 (`src/ComicBook.cpp:954-961`,
   `src/Graphics.cpp:88-101`) = RGB(v, v, 255), a **blue** plate — not grey as FE §12.1 says.

### 5.5 G4a — `core/ComicBookSession.{h,cpp}` (new)
```cpp
// The comic viewer behind menu id 89 (src/ComicBook.cpp, landscape mode only, FE §12.1).
// State survives leaving and re-entering 89 like the legacy app->comicBook (the page persists).
class ComicBookSession {
public:
    static constexpr int kFrames = 39;
    void handleAction(Action a);   // handleComicBookEvents (:972-994) + deviation FE-D7
    UiResult draw(Ui& ui);         // touch samples from ui.in(), then Draw (:88-214); {UiAction::Menu} = Done released
private:
    void touch(int x, int y, bool press);   // Touch (:714-860)
    void touchMove(int x, int y);           // TouchMove (:880-913)
    bool buttonTouch(int x, int y);         // ButtonTouch (:862-878)
    void updateMovement();                  // UpdateMovement, landscape branch (:584-650)
    int page_ = 0;           // iPhonePage
    int target_ = 0;         // cur_iPhonePage
    int endPoint_ = 0, midPoint_ = 0, begPoint_ = 0;
    bool dragging_ = false;  // field_0x10
    bool sliding_ = false;   // field_0x110
    bool snapping_ = false;  // field_0x118
    int pressFrames_ = 0;    // field_0x14c
    bool drawExit_ = false, exitHighlighted_ = false;
    bool exitRequested_ = false;
    int lastX_ = 240, lastY_ = 160;
    Text doneLabel_;         // literal "Done" (:963-966)
};
```
`UpdateTransition` is a no-op in landscape (`field_0x135/0x13c` are never set, `:652-712`) and is
not ported; `field_0x174` is never set non-zero and is dropped.

**`draw(ui)`** — legacy events arrive before the next `Draw`, so per frame:
```
x, y = ui.in().cursorValid ? cursor : (lastX_, lastY_)
if pressed:                                  touch(x, y, true)
else if down && (x, y) != (lastX_, lastY_):  touchMove(x, y)
if released:                                 touch(x, y, false)
lastX_, lastY_ = x, y
++pressFrames_                               // ++field_0x14c at the top of Draw (:105)
updateMovement()
build ComicBookModel { page_, endPoint_, dragging_ || sliding_, page_ == 38 ? 0 : page_ + 1,
                       page_ == 0 ? 38 : page_ - 1, drawExit_, exitHighlighted_, &doneLabel_ }
drawComicBook(ui, m)
if (exitRequested_) { exitRequested_ = false; return { UiAction::Menu, -1 }; }   // -> Action::Menu -> back()
return {}
```
Movement runs per rendered frame, like the legacy per-`Draw` update.

**`buttonTouch(x, y)`**: `exitHighlighted_ = false; if (!drawExit_) return false;`
hit = `10 <= x && x <= 70 && 10 <= y && y <= 40` (inclusive, `x <= rx + rw`, `:870-876`) →
`exitHighlighted_ = true`.

**`touch(x, y, press)`** (`:714-860`):
```
if (buttonTouch(x, y)) log "[sound] 1064"                  // menu_open.wav (:734-736)
if (press) pressFrames_ = 0;
else {
    if (exitHighlighted_) { exitHighlighted_ = false; exitRequested_ = true; }   // DeleteImages + back() (:744-753)
    if (pressFrames_ <= 8) drawExit_ = !drawExit_;          // short tap toggles Done (:756-758)
}
if (sliding_ || (!dragging_ && !press)) return;
dragging_ = press; begPoint_ = x;
if (press) { snapping_ = false; midPoint_ = 0; return; }   // field_0x118 = field_0x110 = 0 here
if (endPoint_ > 240 || midPoint_ > 2)        { snapping_ = false; target_ = page_ - 1; }   // previous
else if (endPoint_ < -240 || midPoint_ < -2) { snapping_ = false; target_ = page_ + 1; }   // next
else { target_ = page_; if (endPoint_ != 0) snapping_ = true; }                           // snap back
if (target_ < 0) target_ = 38; else if (target_ > 38) target_ = 0;                        // (:836-846)
```
**`touchMove(x, y)`** (`:880-913`): `buttonTouch(x, y);`
`if (!sliding_ && dragging_) { midPoint_ = x - begPoint_; begPoint_ = x; endPoint_ += midPoint_; }`

**`updateMovement()`** (`:584-650`, landscape: `v1 = 39`, min speed 8, `v8` negated):
```
if (dragging_) return;
if (page_ != target_) {
    sliding_ = true;
    const int speed = std::max(std::abs(midPoint_), 8);
    // Legacy: endPoint += (page >= target ? +speed : -speed), then the 0<->38 wrap pair
    // corrects by 2*speed (:600-627). For a neighbour target (the only kind touch()/handleAction
    // produce) that is exactly:
    const bool toPrev = target_ == (page_ == 0 ? 38 : page_ - 1);
    endPoint_ += toPrev ? speed : -speed;
    if (endPoint_ >= 480 || endPoint_ <= -480) {        // commit (:628-639)
        page_ = target_; midPoint_ = 0; endPoint_ = 0; sliding_ = false;
    }
}
if (snapping_) {                                        // snap-back (:641-650)
    sliding_ = true;
    endPoint_ = 4 * endPoint_ / 5;                      // C++ int division, truncates toward 0
    if (endPoint_ == 0) { snapping_ = false; sliding_ = false; }
}
```
Self-check (debug build, cheap): `assert(page_ >= 0 && page_ < kFrames && target_ >= 0 && target_ < kFrames);` at the end.

**`handleAction(a)`** (`handleComicBookEvents`, deviation FE-D7):
```
// Every key hides Done (:993). The fake centre tap legacy sends first changes nothing else.
if ((a == Action::TurnLeft || a == Action::TurnRight) && !sliding_ && !dragging_) {
    // FE-D7: a real slide at max(|midPoint|, 8) = 8 px/frame instead of the legacy one-frame flip
    // (pixel ints fed to the normalised-float userMoved, src/CAppContainer.cpp:71-89).
    target_ = a == Action::TurnLeft ? (page_ == 0 ? 38 : page_ - 1)   // LEFT drags right -> previous
                                    : (page_ == 38 ? 0 : page_ + 1);  // RIGHT -> next
    snapping_ = false; midPoint_ = 0;
}
drawExit_ = false;
```
`doneLabel_` is filled once (first `draw` or a ctor): `"Done"`, dehyphenated.

**G4a acceptance**: builds; startup log has no `UiAssets: missing resource ComicBook/...` lines.

### 5.6 G4b — `core/MenuSession.{h,cpp}` + `core/GameContext.{h,cpp}`
MenuSession:
* `int menuId() const { return menu_; }` (public).
* `isCodeBuiltScreen`: also `menuId == kMenuComicBook`.
* `unportedMainScreenReason`: delete `kMenuComicBook` (the function now only names 17 and 19).
* `initMenu`: `} else if (menuId == kMenuComicBook) { // empty: ComicBookSession owns the screen (:2276-2278)` before the final `else`.
* `gotoMenu` safety net: `if (numItems_ == 0 && menuId != kMenuComicBook)`; extend its comment.
* `back()` already returns 89 → MAIN on an empty stack (G3b); with the stack it pops to MAIN
  (cursor on Comic Book). Sound 1122 as for every non-MAIN screen (`:577-581`).

GameContext:
* Member `ComicBookSession comic_;` (no Env).
* `tick()` input loop, the Menu line becomes:
  ```cpp
  if (state_ == StateId::Menu) {
      if (menu_.menuId() == kMenuComicBook) {
          comic_.handleAction(a);                                  // handleComicBookEvents first (:2834-2836)
          if (a == Action::BackKey || a == Action::Menu) menu_.handleAction(a);   // -> back()
      } else {
          menu_.handleAction(a);
      }
      continue;
  }
  ```
  (Legacy also lets FIRE/UP/DOWN/LEFT/RIGHT fall through; with 0 rows they do nothing.)
* `render()` Menu block: `if (menu_.menuId() == kMenuComicBook) { const UiResult r = comic_.draw(*ui); applyUiAction(r.action, r.index); } else { /* updateDrag + buildViewModel + views, unchanged */ }`
  — no menu background, soft keys or scrollbar on 89 (`paint` returns early, `:721-724`).

### 5.7 G4 acceptance
1. Main menu → Comic Book: white screen with the cover frame (480×320). No "Loading" screen.
2. Drag the mouse left/right: the frame follows the pointer with the neighbour frame visible at
   the edge; release past half the screen or with a flick → it slides on to the next/previous
   frame; a short slow drag released near the start snaps back smoothly.
3. RIGHT arrow: the next frame slides in from the right over ~1 s; LEFT: the previous one from the
   left; from the cover LEFT goes to "16b" (wraps), from 16b RIGHT to the cover. Keys during a slide
   are ignored.
4. A short click toggles a small blue "Done" box top-left (lighter while pressed over it, log
   `[sound] 1064`); releasing on it → `[sound] 1122`, main menu with the cursor on Comic Book.
   Any key hides the box.
5. Backspace / Escape → main menu, cursor on Comic Book. Re-entering shows the page you left on.

---------------------------------------------------------------------------------
## 6. Options (17) — stays refused (decision)
The shipped `menus.bin` row 17 has **0 items** (FE §12.6); everything `src/` shows there is
`[GEC]` (Sound/Video/Input, `src/MenuSystem.cpp:1365-1458`, `:2304-2700`). There is no original
content to port, so the row stays visible and refuses with the G1 reason string. Other Games
(19) is unreachable (row removed from MENU_MAIN, FE §5.1).

---------------------------------------------------------------------------------
## 7. Deviations (added to `front-end.md` §10)

| Id | What | Why |
|---|---|---|
| FE-D2 (narrowed) | Only Options (17) refuses on select; Other Games is unreachable | shipped row 17 has 0 items, its `src/` sub-menus are `[GEC]` (FE §12.6) |
| FE-D7 | Comic Book LEFT/RIGHT slide at 8 px/frame | legacy flips in one frame only because pixel ints reach the normalised-float `userMoved` (`src/CAppContainer.cpp:71-89`, `src/ComicBook.cpp:972-994`); unscaled, the same fake drag would snap back |
| FE-D8 | Comic frames resident from startup (5.99 MB R8), no "Loading" screen, portrait pages never loaded | loading screen only amortised the iPhone decode; portrait mode is dead code (FE §12.1) |
| FE-D9 | Credits crawl clock = `upTimeMs` | legacy `gameTime`; same choice as the G6b intro crawl |

Verbatim quirks kept on purpose (not deviations): BACK after Credits lands MAIN on Mini Games;
a main-menu Matrix Skip win ends with `endGame(0)` / sound 1040; the comic page persists between
visits.

---------------------------------------------------------------------------------
## 8. Reviewer checklist
* `grep -n "fromMainMenu" new_src/minigames` — set only in `playFromMainMenu`, cleared in `start()`
  and in the `finish()` head; every IQ / tries / hack-bit / `evaluateResults` / `award` write guarded.
* The debug V/H/J paths and EV_MINIGAME still reach `finish()`'s world tail (`vars[7]`, Playing, resume).
* `MainMenuView.cpp` unchanged; `MenuView.cpp` unchanged.
* In-game help leaves 38-45/59-61 still wrap at 49 and re-wrap at 37 (log line).
* No `[sound] 1071` on entering Credits from the main menu; `[sound] 1071` once per main-menu
  minigame end.
* `menu_.menuId() == kMenuComicBook` is the only comic routing test; `buildViewModel` is never
  called on 89.
