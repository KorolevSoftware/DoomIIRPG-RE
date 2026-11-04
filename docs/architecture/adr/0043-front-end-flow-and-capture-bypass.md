# ADR 0043 — Front end: main menu in `MenuSession`, character select and prologue as peer sessions, real `backToMain`, capture runs bypass the front end

Date: 2026-11-03. Status: accepted. Spec: `../front-end.md`. Facts:
`docs/original-code/front-end.md`. Supersedes ADR 0037 §6 (the boot / `--newgame` /
BACKTOMAIN-quits stand-ins).

## Context

The rewrite boots straight into Continue-or-new-game (`LoadingManager::boot`,
`new_src/core/LoadingManager.cpp:28-31`), and every "go to the main menu" path quits the
process (`new_src/core/GameContext.cpp:70`, `new_src/core/LoadingManager.cpp:182-185`),
per ADR 0037 §6. The original's chain is `ST_LOGO` → `ST_INTRO_MOVIE` → `MENU_MAIN` →
(confirm 14) → difficulty 16 → `ST_CHARACTER_SELECTION` → `ST_INTRO` → `loadMap(1,false,true)`
(FE §1). The main-menu screens are `menus.bin` rows that `io/MenuData` already parses
(ADR 0013); `MenuSession` already has the `loadMenuItems(beg,count)` slice, `setYesNo` and the
nav stack. The golden-frame capture runs (`DOOM2RPG_CAPTURE_TICKS`) rely on a fixed tick
timeline from boot to map 1 with disk access off.

## Decision

1. **Main-menu screens are `MenuSession` screens.** `MENU_MAIN` 3, `MENU_MAIN_EXIT` 13,
   `MENU_MAIN_CONFIRMNEW` 14, `MENU_MAIN_DIFFICULTY` 16 are built in `initMenu` exactly like
   their legacy cases; actions 7/8/10/22 go into `select()`. The skin is a second stateless
   view, `ui/MainMenuView::drawMainMenu`, over the **same** `MenuViewModel` (new fields
   `mainSkin`, `drawLogo`, `dialKnobY`, per-row `textDy`); `MenuView` keeps the in-game tree
   alone. Screens out of scope (Options, Comic Book, Mini Games, Game Help, About) stay
   visible and refuse on select, the existing rewrite-refusal idiom.
2. **Character select and prologue are peer sessions** (`core/CharacterSelectSession`,
   `core/PrologueSession`) owned by `GameContext`, with `ui/` models and views, like
   `TravelMapSession`. New `StateId`s `CharacterSelect = 22`, `Intro = 9` (later `Logo = 26`,
   `IntroMovie = 2`). Their touch goes through one intent `UiAction::ScreenTouch` (index = the
   legacy button id) → latched id → `Action::ScreenTouch`, the `MiniGameTouch` idiom.
3. **Capture runs and `--newgame` bypass the whole front end** in
   `LoadingManager::boot(skipFrontEnd = true)`: difficulty 1, character 1, the new-game wipe
   and resets, then `loadMap(1, false, true)`. Capture runs additionally keep disk access off
   (unchanged). This is the exact path the goldens were recorded on, so their tick timeline
   and frames stay valid; no env var is added. Normal start: `boot(false)` → logo (once ported)
   → main menu.
4. **`backToMain` is real and lives in `GameContext`**: runtime teardown (`unloadMap_` +
   thread pool reset), `loadMapID = 0`, `player.reset()`, end fade, `ST_MENU`/`MENU_MAIN`
   (`src/Canvas.cpp:1372-1407`). `LoadingManager` (save-exit `0x04`) and `MenuSession`
   (`ACTION_BACKTOMAIN`) call it through `Env` callbacks. The previous `MapData` stays resident
   (ADR 0035); the world pass is skipped while `loadMapID == 0`.
5. **Difficulty is `Game` state** (`difficulty_`, default 1, config round-trip, stamped into
   `vars[12]` at load); the character is `Player::characterChoice` (stamped into `vars[14]` by
   `updateScriptVars`, picks the HUD face sheet and the dialog portrait row). `Player::reset`
   reads the difficulty through an injected `const Game*` for the Difficult DEFENSE-0 rule and
   no longer zeroes `bestGrades` (legacy only zeroes it in the constructor).

## Consequences

- The process no longer quits on "Main Menu → Save Game / Don't Save" or the death menu's
  "Main Menu"; only Exit → Yes, Save & Quit and the window close end it.
- A damaged save no longer silently becomes a new game at boot: Continue reports "No Saved
  Game" (deviation FE-D1) and the user chooses.
- Difficulty 2/4 now reach every existing reader (spawn hp bump, combat, scripts) for the
  first time; the Difficult/Nightmare behaviour of those readers is exercised for real.
- Golden captures need no re-recording; anyone who wants the menu in an automated run must
  drive it with keys.
- `MenuSession` grows by the four main screens and a main-menu geometry branch; `MenuViewModel`
  by four fields.

## Rejected alternatives

- **A separate `FrontEndSession` owning main menu, select and prologue.** Duplicates the menu
  engine (`moveDir`, nav stack, `setYesNo`, refusals) for screens that are plain `menus.bin`
  rows; ADR 0013 exists to avoid exactly that.
- **One `drawMenu` with a `mainSkin` branch everywhere.** The two skins share almost no
  geometry (plates, alpha, dial vs track scrollbar, soft keys, status line, info buttons);
  branching every block of the 400-line in-game view risks regressions in the menu the user
  already signed off. A sibling view over the same model costs one row walk.
- **An env var (`DOOM2RPG_SKIP_FRONTEND`) or auto-pressing through the menu in capture runs.**
  Auto-selection shifts the tick timeline (logo frames, menu ticks), invalidating every golden;
  a new env var duplicates what `--newgame` / capture already mean.
- **Keep the quit stand-ins until the logo/intro exist.** The main menu is the user's
  priority; `backToMain` is independent of the boot presentation.
