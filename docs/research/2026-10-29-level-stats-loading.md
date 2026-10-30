# 2026-10-29 — Level-stats screen and loading screen (original)

## Questions

1. What does the `SHOWSTATS` fade flag open? Which state or menu, what does it
   show, where are the counters tracked, the layout, the input to continue,
   and what happens next?
2. What is drawn in `ST_LOADING` / `ST_SAVING` between maps? Assets, timing,
   and can it be skipped?

## Method

- grep `saveState`, `MENU_LEVEL_STATS`, `ST_LOADING`, `drawLoadingBar`,
  `currentLevelDeaths` across `src/`. Read `src/ScriptThread.cpp:484-517`,
  `src/Render.cpp:2553-2566`, `src/LoadingManager.cpp:39-79,605-744`,
  `src/Canvas.cpp:853-975,1183-1187`, `src/LoadingScreenController.cpp`,
  `src/MenuSystem.cpp` (initMenu, fillStatus, paint, handleMenuEvents, select,
  setMenuSettings, getMenuItemHeight), `src/Player.cpp` (counters, grade,
  formatTime), `src/Game.cpp` (time/secrets persistence, awardSecret).
- Decoded the shipped `menus.bin` row 1 and `strings.idx`/`strings0X.bin`
  with a throwaway scratchpad script that implements the formats from
  `src/MenuSystem.cpp:4005-4035` and `src/Resource.cpp:169-200` +
  `src/Text.cpp:179-210`. Not committed.
- Read the BMP headers of `endOfLevelStatsBG.bmp` (480x320, 8bpp),
  `FabricBG.bmp` (32x32, 8bpp), `loadingFire.bmp` (26x9, 4bpp).

## Verdict

1. **CONFIRMED.** The stats screen is the menu `MENU_LEVEL_STATS = 1`
   (`src/Menus.h:46`) in `ST_MENU`, not a Canvas state. It opens after an
   `ST_SAVING` pass with `saveType = 51` (`src/Render.cpp:2564`,
   `src/Canvas.cpp:865-890`). Content: map-name divider + Time, Secrets,
   Enemies, Turns, Deaths, Grade + "Tap Screen to / Continue". There is no
   "items" counter. Level-end music 1069 = `Music_Level_End.wav`. Any of FIRE,
   AUTOMAP, BACK, or a tap continues; it then calls
   `loadMap(nextMap, true)` → `ST_TRAVELMAP`, or `MENU_END_RANKING` when
   `endingGame` is set (`src/MenuSystem.cpp:2996-3002`).
2. **CONFIRMED.** One box on a cleared screen: tiled `FabricBG` panel
   150x58 with a white border, a caption, "Just a moment!", and a black bar
   134x12 with a 26x9 `loadingFire` sprite that steps 10 px each paint and is
   throttled to 150 ms. There is no art, no percentage and no tips. It is not
   skippable (no input branch, synchronous load). In this port it presents
   about one frame (two-phase `loadMedia`).

## Key evidence (short quotes)

- `src/ScriptThread.cpp:492-494`: `if (LEVEL_STATS_nextMap < canvas->loadMapID) showStats = false;`
- `src/Render.cpp:2564`: `app->canvas->saveState(51, (short)3, (short)196);`
- `src/Canvas.cpp:887-890`: `app->sound->playSound(1069, 1u, 3, saveType & 8); app->menuSystem->setMenu(Menus::MENU_LEVEL_STATS);`
- `src/Game.cpp:2069-2071`: `if ((saveType & 0x10) != 0x0) { app->player->levelGrade(true); }`
- `src/MenuSystem.cpp:1244-1255`: `items[3].textField = ARGUMENT1 … items[10].textField2 = ARGUMENT7; fillStatus(true, false, false);`
- `src/MenuSystem.cpp:3951-3967`: the argument order is map name, time, secrets, kills, moves, deaths, grade.
- `src/Player.cpp:2033`: `max(((6*((k<<8)/t)>>8) + (6*((s<<8)/ts)>>8) >> 1) - currentLevelDeaths, 1)`
- `src/MenuSystem.cpp:4233-4239`: `w = 407; h = 320; x = 23; y = 0;`
- `src/MenuSystem.cpp:2996-2999`: `if (!app->canvas->endingGame) { app->canvas->loadMap(this->LEVEL_STATS_nextMap, true, false); return; }`
- `src/LoadingScreenController.cpp:56-58`: `fillRegion(imgFabricBG, cx-0x4b, cy-0x1d, 0x96, 0x3a); … drawRect(...)`
- `src/LoadingScreenController.cpp:22-24`: `if (app->upTimeMs - canvas->lastPacifierUpdate < 0x96) return;`
- `src/LoadingManager.cpp:610-616`: on the first call it draws the bar, sets `isLoaded = true` and returns false.

Decoded data (`menus.bin`, menu 1): type 5, 15 items. Rows 5-10 use string
ids 28/30/32/34/56/36 with help ids 29/31/33/35/57/37. Rows 13/14 use 140
"Tap Screen to" and 70 "Continue". The `%01..%07` strings are ids 241-247.
The grade strings 38-44 are "A+","A","B","C","D","F","--". The loading
strings are (0,41) "Loading…", (0,57) "Processing…", (0,58) "Just a moment!",
(3,194) "Loading Game…" and (3,196) "Saving Game…".

The full curated write-up is in `docs/original-code/ui.md` §19.

## Open questions

- I did not verify whether `gameTime` keeps running while the stats menu is
  open. The time value is computed once, when the menu is built.
- The help popup (`ACTION_MENU_ITEM_INFO`) needs the selected row to have a
  help id. I did not trace whether up/down can move the selection onto rows
  5-10 in a type-5 menu.
- The no-fade `EV_CHANGE_MAP` path uses caption 194 "Loading Game…" while the
  fade path uses 196 "Saving Game…". The code is as quoted. It may be an
  original quirk.
- `Graphics::fade` is absent from `src/`, so I could not confirm the exact
  black colour of `eraseRgn` behind the loading box.
