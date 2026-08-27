# 2026-08-27 — In-game menu structure + timed text slots

## Hypotheses / questions

**Part 1 (menu).** (1) Which sections exist under the `Menu` soft key and who
owns them; (2) list geometry, visible count, scroll granularity, selection
rendering, end behaviour, key repeat; (3) whether the menu scrollbar is the
same widget as `Canvas::drawScrollBar` (which our
`DialogSystem::drawScrollBar` clones); (4) how long-text sections wrap/scroll;
(5) soft-key labels per section and Back semantics; (6) menu state absent from
`new_src`.

**Part 2 (text slots).** For centre message, "important" message, speech
bubble, cinematic subtitle, cinematic title, dialog text, loot text: setter +
duration, clear path, geometry/colour, coexistence + draw order, and the dialog
reveal rule.

## Method

* grep/read `src/MenuSystem.{h,cpp}` (5876 lines), `src/Menus.h`,
  `src/MenuItem.{h,cpp}`, `src/Hud.{h,cpp}`, `src/Canvas.cpp`,
  `src/GameStateRunner.cpp`, `src/InputEventController.cpp`, `src/Input.cpp`,
  `src/PlayingInputHandler.cpp`, `src/ScriptThread.cpp`, `src/Button.cpp`,
  `src/DialogSystem.cpp`, `src/LootingSystem.cpp`, `src/Text.cpp`,
  `src/Resource.cpp`.
* Decoded the shipped data to resolve the menu tree instead of guessing:
  `menus.bin` (format from `MenuSystem::startup` + `loadMenuItems`),
  `strings.idx` + `strings0{0,1,2}.bin` (format from
  `Resource::loadFileIndex` + `Localization::loadTextFromIndex`),
  `tables.bin` table 6 (`OSC_CYCLE`), and BMP headers for the menu art.
  Scratch scripts in the session scratchpad (not committed).
* Cross-checks: every clear path was read in code, not inferred from comments
  (menu open clearing `msgCount`; `drawTopBar` shift; `drawBubbleText`
  self-clear; `Hud::draw` cin-id expiry; `ST_CAMERA` entry clears;
  `LootingSystem` 500 ms gate).

## Verdict

**Part 1: CONFIRMED** (with two honest gaps, below).
**Part 2: CONFIRMED**, plus one refutation of an assumption baked into
`new_src`.

Notable results:

1. The menu tree is **data-driven**, not coded: `menus.bin` holds 72 menu rows
   and 426 item ints. `MENU_INGAME` (id 29, type 1 LIST) has exactly 11 items:
   Inventory, PDA, Save Game, Load Game, View Map, Status, Game Help, Options,
   Restart Level, Save & Quit, Main Menu (`src/MenuSystem.cpp:4005-4035`
   decoder + decoded table).
2. **There is no save-slot list.** `ACTION_SAVE` → `canvas->saveState(3|0x80,
   3, 196)`; `ACTION_LOAD` → `canvas->loadState(getRecentLoadType(), 3, 194)`
   (`src/MenuSystem.cpp:3035-3050`). "Load Game" only opens a YES/NO confirm
   (`SetYESNO(136, 1, ACTION_LOAD, 0)`, `:1714-1719`).
3. **Selection wraps** in list menus (`moveDir`, `src/MenuSystem.cpp:417-431`)
   and **clamps** in HELP/NOTEBOOK types 5/7 (`:405-413`).
4. **No key auto-repeat**: `if (!sdlEvent.key.repeat)` at
   `src/Input.cpp:740,794`, and the event ring holds one event
   (`src/InputEventController.cpp:481-489`).
5. **The menu scrollbar is a different widget** from
   `Canvas::drawScrollBar`. It is `fmScrollButton m_scrollBar`
   (`src/MenuSystem.cpp:233`) rendered from `gameMenu_ScrollBar.bmp` (20x220)
   + `gameMenu_{top,mid,bottom}Slider.bmp` (24x13 / 24x6 / 24x13) at
   `barRect = (430, 18, 50, 220)` for `MENU_INGAME`
   (`src/MenuSystem.cpp:2755-2803`, `src/Button.cpp:536-589`). Our
   `DialogSystem::drawScrollBar` is byte-for-byte the `Canvas::drawScrollBar`
   algorithm (`src/Canvas.cpp:1284-1315`) — correct for dialog + loot, wrong
   for the menu.
6. **Selection is a wobbling cursor glyph, not a highlight bar**:
   `drawCursor(x + OSC_CYCLE[time/100%4] + 3, y + 23, anchor RIGHT)` with
   `OSC_CYCLE = {-1,0,1,0}` (tables.bin table 6), then the label shifts +8 px
   (`src/MenuSystem.cpp:1064-1076`, `src/Graphics.cpp:670-686`). The
   *background* button art does have a highlighted variant, but that tracks
   touch, not the keyboard cursor (`src/MenuSystem.cpp:5107-5118`).
7. Long text (help topics, PDA) is **the same list machinery**: one wrapped
   line per `MenuItem`, `LoadHelpItems` splitting on `'|'`
   (`src/MenuSystem.cpp:3811-3838`). In-game wrap width 49 chars
   (`ingameScrollWithBarMaxChars`, `src/Canvas.cpp:94`), re-wrapped to 37 if
   the page overflows (`:3685-3698`); 15 lines fit the 241 px region.
8. Menu soft keys are drawn by the menu itself (the HUD is not painted in
   `ST_MENU`): left `inGame_menu_softkey.bmp` at (9,268) labelled
   `composeText(3,80)` = "Back" at (42,295); right at (372,268) labelled the
   **literal ASCII "Resume"** at (448,295)
   (`src/MenuSystem.cpp:5181-5294`). The `softKeyLeftID/RightID` values
   computed each tick in `GameStateRunner::menuState`
   (`src/GameStateRunner.cpp:203-253`) are never rendered in this port.
9. **REFUTED assumption in `new_src`**: `Hud::kImportantDurationMs = 3500`
   (`new_src/ui/Hud.h:193`) has no basis in `src/`. An "important" message is
   just a queue entry with `MSG_FLAG_IMPORTANT`; its lifetime is the same
   `msgDuration` as any other message — 700 ms for ≤49 chars, else `len*50` ms,
   expiring at `msgDuration + 100` (`src/Hud.cpp:122-136,249-251`).
   The speech bubble is likewise 1500 ms
   (`BUBBLE_TEXT_TIME`, `src/Hud.h:38`, `src/Hud.cpp:910`), not the 3000 ms
   default in `new_src/ui/Hud.h:219`, and `new_src` never expires it
   (`new_src/ui/Hud.cpp:507` only accumulates).
10. Dialog reveal is **25 ms/char, per line within the page**
    (`src/DialogSystem.cpp:338-345`), verified against the existing
    `docs/original-code/dialog-system.md` §2.4.

## Gaps (stated as UNKNOWN, not inferred)

* `maxItems` — the logical page/window size — is hard-set to **4** at the top
  of `initMenu` with the porter's comment `// [GEC] 4 por defecto`
  (`src/MenuSystem.cpp:1231`). What the original J2ME build computed there is
  **UNKNOWN**; the port's own replacement
  (`maxItems = maxItemsMain - maxItemsGame`) is commented out at `:5169`.
  4 is consistent with the pixel region (241 px / 56 px per row).
* The `moveDir` block that converts `selectedIndex`/`scrollIndex` into the
  scrollbar's **pixel** offsets (`src/MenuSystem.cpp:463-546`) is explicitly
  marked `// [GEC]`, so the original's item-index scrolling is the authoritative
  model; the exact original pixel behaviour is **UNKNOWN**.
* Every options *value* screen (`MENU_*_OPTIONS_SOUND/VIDEO/INPUT`,
  `*_BINDINGS`, `*_CONTROLLER`, volume/alpha/vsync/resolution/deadzone/
  vibration sliders) is `// [GEC]` port code. The shipped `menus.bin` row for
  `MENU_INGAME_OPTIONS` (id 35) contains only two items: "Controls" → 62 and
  "Back". What the original iOS options screen showed beyond `Controls` is
  **UNKNOWN** from `src/` alone.
* `MENU_INGAME_RECIPES` (47) has an empty `initMenu` case
  (`src/MenuSystem.cpp:1710-1712`) and no `menus.bin` row — dead in this build.

## Curated output

`docs/original-code/ui.md` gained §10-17 (menu ownership, tree table decoded
from `menus.bin`, geometry, item heights, selection, movement, scrollbar,
long-text sections, torn-page popup, soft keys, missing `new_src` state) and
§18 (the seven text slots table, coexistence + draw order, reveal rule,
`new_src` fidelity column).
