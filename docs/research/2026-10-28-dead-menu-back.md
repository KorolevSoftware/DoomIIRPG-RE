# 2026-10-28 — BACK / cancel inputs in MENU_INGAME_DEAD (51)

Question R1 (`docs/architecture/specs/2026-09-22-monster-attacks.md` §9.1):
what do BACK and other cancel inputs do while the dead menu is open?

## Verdict
CONFIRMED: **nothing** (except a click sound). The player can only leave via
the three rows.

## Evidence
1. Entry: `dyingState` → `setMenu(MENU_INGAME_DEAD)` (`src/GameStateRunner.cpp:279`);
   `setMenu` does not touch the stack for 51 (`src/MenuSystem.cpp:619-621` clears
   only for BEGIN/INGAME/KICKING) and switches to ST_MENU (`:651-654`).
2. Stack is empty: leaving ST_MENU always calls `clearStack()`
   (`src/Canvas.cpp:1033-1035`, inside `Canvas::setState` `:1020`); death is
   entered from gameplay, so `stackCount == 0` when 51 opens.
3. Input routing: ST_MENU → `handleMenuEvents` (`src/InputEventController.cpp:314-315`).
   Key mapping: `AVK_CLR`→ACTION_BACK, `AVK_SOFT1`/`AVK_MENUOPEN`→ACTION_MENU,
   `AVK_SOFT2`→ACTION_AUTOMAP (`:25-27,41`); keys 19/20/18 → MENU/AUTOMAP/BACK (`:130-137`).
4. `handleMenuEvents`: ACTION_MENU → `back()` unless vending/more-games
   (`src/MenuSystem.cpp:2921-2936`); ACTION_BACK → `back()` unless ENABLE_SOUNDS,
   MAIN, LEVEL_STATS/END_*, MORE_GAMES, VENDING (`:2937-2957`). 51 is in none.
   ACTION_AUTOMAP → `back()` only if `moreGamesPage == 3` (`:2918-2920`);
   `moreGamesPage` is never assigned in `src/` (declared `src/MenuSystem.h:150`).
5. `back()` (`src/MenuSystem.cpp:568-611`): sound 1122 if not playing (`:577-581`);
   `stackCount != 0` branch skipped (`:591-602`); 51 not in the
   `returnToGame()` list INGAME/ITEMS/ITEMS_DRINKS/QUESTLOG/SNIPER (`:603-606`) nor
   MINIGAME/COMIC_BOOK (`:607-609`) → no state change.
6. Touch: `updateTouchButtonState` hides buttons 11 (back) and 15 (return)
   first (`:4499,4503`); 51 falls to `default` (`:4508-4509`), so neither is drawn
   nor tappable (`:4781-4789`).
7. `select()` for type 5/7 menus calls `back()` (`:3004-3011`); 51 is type 6
   (decoded `menus.bin`, see `docs/original-code/player-damage.md` §3.4), so FIRE
   executes the row action.

## Rows (already decoded earlier, `player-damage.md` §3.4)
Type 6: "Try Again?" (header, flags 9), blank, "Load Saved Game" → ACTION_LOAD 3,
"Restart Level" → ACTION_RESTARTLEVEL 12, "Main Menu" → ACTION_BACKTOMAIN 5.
Items come from `loadMenuItems` (default branch `src/MenuSystem.cpp:2724-2726`,
decoder `:4005-4034`). RESTARTLEVEL suppresses the HUD message for 51
(`:3105-3111`).

## Presentation
Panel geometry shared with INGAME_OPTIONS/EXIT/LOAD/RESTARTLVL/... (centred
option button, `setMenuSettings` `src/MenuSystem.cpp:4329-4348`). No back arrow,
no return-to-game button (item 6).

## Open
- `moreGamesPage` is uninitialised in the port; assumed 0 (value is indeterminate).
