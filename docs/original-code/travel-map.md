# Travel map screen (`ST_TRAVELMAP`) in the original

Verified facts about the inter-level "travel map" (`Canvas::ST_TRAVELMAP = 21`,
`src/Canvas.h:88`) implemented in `src/TravelMapManager.{h,cpp}`. How the game gets
here (save step, `loadMap`) is in `map-change.md` §3; the loading bar that follows
(`ST_LOADING`) and the level-stats menu that precedes forward exits are in `ui.md` §19.
Raw log: `../research/2026-10-29-travel-map.md`.

## 1. Entry / exit and frame plumbing

* Entered by `Canvas::setState(ST_TRAVELMAP)` from `LoadingManager::loadMap` (no-save
  branch, after `setLoadingBarText(3, levelNames[loadMapID-1])`,
  `src/LoadingManager.cpp:75-77`) or from `ST_SAVING` with saveType bit 0x08
  (`src/Canvas.cpp:885-886`). `setState` zeroes `stateVars[0..8]`
  (`src/Canvas.cpp:1024-1026`) and then calls `travelMapManager.init()`
  (`src/Canvas.cpp:1062-1063`).
* Draw: `Canvas` paint → `drawTravelMap(graphics)` (`src/Canvas.cpp:438-439`). There is
  no per-frame update logic; the state is excluded from the update chain
  (`src/Canvas.cpp:827`). All timing is computed in the draw function from
  `app->upTimeMs`; each draw sets `canvas->staleView = true` (`src/TravelMapManager.cpp:255`).
* Input: `InputEventController` → `canvas->handleTravelMapInput(key, keyAction)` →
  `TravelMapManager::handleInput` (`src/InputEventController.cpp:429-431`,
  `src/Canvas.cpp:1539`).
* Exit: only `finishAndLoadLevel()` (`src/TravelMapManager.cpp:514-521`):
  `clearSoftKeys()`, `dispose()` (deletes all 10 images, `:123-144`),
  `setLoadingBarText(0, 41)` (common string 41 = `Load-ing\x85` "Loading…"),
  `setState(ST_LOADING)`. **There is no timeout** on the normal path: the screen waits
  for input after the animation ends (§5). (The 5250 ms auto-exit exists only in the
  dead star-field page, §7.)

## 2. `init()` — what it reads from game state (`src/TravelMapManager.cpp:23-121`)

```
TM_LoadLevelId = clamp(canvas->loadMapID, 1, 9)                                  // :27
TM_LastLevelId = (getRecentLoadType()==1 && !TM_NewGame) ? TM_LoadLevelId
                                                        : clamp(canvas->lastMapID, 0, 9)  // :28
if (TM_LastLevelId == 0 && TM_LoadLevelId > 1) TM_LastLevelId = TM_LoadLevelId - 1        // :30-32
```

* `getRecentLoadType()` = 2 if `canvas->recentBriefSave` else 1
  (`src/LoadingManager.cpp:753-759`); `recentBriefSave = (saveType & 0x20) != 0` at
  every save (`src/Game.cpp:2064`) and is persisted in the save (`src/Game.cpp:1943,2020`).
  Level exits save with 0x2B / 0x33 (brief) → type 2 → last = real `lastMapID`.
  After a FULL save (type 1) and not a new game, last := load ("same level").
* `TM_NewGame` is the 3rd arg of `loadMap` (`src/LoadingManager.cpp:71`); true only for
  the intro-end new game (`src/IntroSequenceManager.cpp:135`). With `lastMapID == 0`
  the new game yields last = 0 ("Mars"), load = 1.
* Map 10 ("Test Map") is clamped to 9 (VIOS) for display.
* **Writes `game->scriptStateVars[15]`** (`:33-43`): 0 = forward (load > last),
  1 = same level (load == last), 2 = backward (load < last). `loadMedia` clears every
  script var except 15 (`src/LoadingManager.cpp:631-635`), so this is how map
  scripts learn the arrival direction (see §8).

Planet of a level id (`:446-456`): `onMoon` 1..3, `onEarth` 4..6, `inHell` ≥ 7; id 0 is
none of them (Mars / start).
`newLevelSamePlanet()` = `last != load` and both on the same planet (`:258-263`).

Level names: `composeText(3, levelNames[id-1])`, `levelNames` = tables.bin table 7
(`src/App.cpp:396`); shipped values (strings type 3, parsed from the IPA):
1 Tycho (155), 2 Cichus (156), 3 Kepler (157), 4 UAC Admin (158), 5 UAC Labs (159),
6 UAC R&D (160), 7 Gehenna (161), 8 Abaddon (162), 9 VIOS (163), 10 Test Map (167).
Text is `dehyphenate()`d before drawing (`:187,216`).

### 2.1 Images loaded (`:45-78,116`)

| Member | File (WxH in IPA) | Condition |
|---|---|---|
| `imgNameHighlight` | `highlight.bmp` 100x24 | always |
| `imgMagGlass` | `magnifyingGlass.bmp` 24x15 | always |
| `imgSpaceShip` | `spaceShip.bmp` 15x20 | always |
| `imgTierCloseUp` | `TM_Levels1.bmp` 135x98 | load on Moon |
| `imgTierCloseUp` | `TM_Levels4.bmp` 182x156 | load on Earth |
| `imgEarthCloseUp` | `TM_Levels2.bmp` 158x129 | load on Earth AND last on Moon or in Hell → sets local `b` ("two-step Earth") |
| `imgTierCloseUp` | `TM_Levels3.bmp` 135x98 | load in Hell |
| `imgTravelPath` | `toMoon.bmp` 124x64 | load on Moon and last == 0 |
| `imgTravelPath` | `toEarth.bmp` 37x53 | Moon↔Earth either direction |
| `imgTravelPath` | `toHell.bmp` 68x158 | Earth↔Hell either direction |
| (none) | — | same planet / same level: `imgTravelPath` stays null |
| `imgTravelBG` | `TravelMap.bmp` 480x320 | always |
| `imgMapHorzGridLines` | `travelMapHorzGrid.bmp` 240x1 | always |
| `imgMapVertGridLines` | `travelMapVertGrid.bmp` 1x640 | always |
| `imgStarField` | `cockpit.bmp` 240x234 | always (only used by dead §7) |

### 2.2 Geometry set up in `init`

* `xDiff = max(0, (displayRect[2] - bgW)/2)`, `yDiff` likewise (`:83-84`);
  `displayRect` = backbuffer size (`src/Canvas.cpp:49-52`) = 480x320 = BG size, so
  both are 0 in practice (the FabricBG border branch `:158-165` never runs).
* Planet origin `(n2,n3)`: Moon (137,216), Earth (123,150), Hell (150,11) (`:86-99`).
* Target crosshair (`:101-109`): normally
  `CROSS_HAIR_CORDS[2*(load-1)], [+1]` + diff + origin; two-step Earth:
  `UAC_BUILDING_LOCATION_ON_EARTH` (82,48) + diff + (123,150), and `stateVars[8] = 1`.
* `stateVars[0] = upTimeMs` (phase start), `totalTMTimeInPastAnimations = 0`.

Constants (`src/Canvas.h:95-106`):

```
CROSS_HAIR_CORDS  = {64,33, 60,45, 60,56, 110,52, 38,85, 83,125, 61,45, 72,44, 67,49}  // per level 1..9
LOCATOR_BOX_CORDS = {86,-15, 106,16, 106,48, 136,22, 136,65, 136,109, 90,8, 100,33, 92,57}
UAC_BUILDING_LOCATION_ON_EARTH = {82,48}
moonCoords  = {137,216}         earthCoords = {123,150, 113,140}   hellCoords = {150,11}
moonNameCoords  = {124,255, 165,64}   moonPathCoords  = {200,202}
earthNameCoords = {126,160, 40,226}   earthPathCoords = {167,206}
hellNameCoords  = {184,23}            hellPathCoords  = {137,52}
```

Note: crosshair / locator coordinates on Earth are relative to `earthCoords[0..1]`
(123,150) even though `TM_Levels4` is drawn at `earthCoords[2..3]` (113,140)
(`:93-94,243-247,274,410-412`); `TM_Levels2` is drawn at (123,150) (`:271`).

## 3. Per-frame draw order (`drawTravelMap`, `src/TravelMapManager.cpp:146-256`)

1. If `stateVars[5] == 1` → star-field page and return (`:150-154`; dead, §7).
2. `TravelMap.bmp` at `(SCR_CX, (displayRect[3]-h)/2)` anchor 17 = HCENTER|TOP
   → (240,0) (`:156`).
3. `time = upTimeMs - stateVars[0]` (`:167`). Grid lines with
   `t = time + totalTMTimeInPastAnimations` (= ms since init; every phase switch adds
   `time` to both `total` and `stateVars[0]`, so the sum is continuous) (`:168`).
   `drawGridLines` (`:432-444`): vertical 1x640 strip every 44 px starting at
   `x = (t/200) % 44` (scrolls right 1 px / 200 ms, period 44), y = 0, anchor 0x14
   (LEFT|TOP); horizontal 240x1 strip at y = 5, 49, 93, … < 320, drawn twice (x=0 and
   x=240). All with renderMode 2 = `RENDER_BLEND50` (`src/Render.h:20`;
   GL path: modulate alpha 0.5, `src/Image.cpp:177-181`).
4. Phase content (§4).

Anchors (`src/Graphics.cpp:350-368`): bit1 HCENTER, bit2 VCENTER, 4 LEFT, 8 RIGHT,
0x20 BOTTOM; anything else = top-left.

## 4. Animation state machine

`canvas->stateVars` usage: [0] phase start ms, [1] phase A done, [2] path done,
[3] path hold done, [4] locator lines arrived, [6] final prompt, [8] two-step Earth
pending, [5]/[7] star field (dead).
`same` = `newLevelSamePlanet()`.

| Phase | Condition | Draws | Ends when |
|---|---|---|---|
| A | `[1] != 1` | if `same`: closeup of the LAST level + locator box/name of LAST level with blinking magnifier (`:182-190`); else nothing | `time > (same ? 1500 : 700)` → `[1]=1`; if `same` also `[2]=[3]=1` (skip path) (`:171-181`) |
| B | `[1]==1 && [2]!=1` | `drawDottedLine(time)` — progressive path reveal (§4.1) | path fully revealed (returns true) → `[2]=1` (`:194-200`) |
| C | `[2]==1 && [3]!=1` | full path (`drawDottedLine(g)` with `n = 22*max(screenW,screenH)`, `:303-307`) | `time > 500` or `same` → `[3]=1` (`:202-209`) |
| D | `[3]==1 && [6]!=1 && [4]!=1` | closeup of NEW level (`TM_Levels2` if `[8]==1`); animated locator crosshair `drawLocatorLines(time, same, [8]==1)` | both axes reached target → `[4]=1` (`:225-231`) |
| E | `[4]==1 && [6]!=1` | closeup + crosshair fixed at target (`n = -1`) | `time > 800`: if `[8]==0` → `[6]=1`; else two-step: `[4]=0, [8]=0`, retarget to `CROSS_HAIR_CORDS[load]` + (123,150) → phase D again on `TM_Levels4` (`:232-249`) |
| F | `[6]==1` | closeup + locator box & name of NEW level (blinking magnifier) + common string 96 "Tap Screen to Con-tin-ue" `wrapText(24)`, dehyphenated, at `(SCR_CX, displayRect[3]-21)` anchor 17 (`:212-224`) | input only (§5) |

Closeup placement (`drawAppropriateCloseup`, `:265-280`): Moon → `TM_Levels1` at
(137,216); Earth → `TM_Levels2` at (123,150) if two-step flag else `TM_Levels4` at
(113,140); Hell → `TM_Levels3` at (150,11). All anchor 0 (top-left), renderMode 0.

### 4.1 Path reveal (`drawDottedLine`, `:282-301`)

`px = time / 22` (1 pixel per 22 ms). Dispatch by (last, load):

| Route | Function | Reveal | Label (after threshold) | Full duration |
|---|---|---|---|---|
| last 0 → Moon | `drawMarsToMoonLinePlusSpaceShip` (`:309-331`) | `toMoon` columns `[w-px, w)` drawn at `moonPathCoords.x + w - px`, y 202 (right→left) | type3 165 "Earth's moon" at (124,255) anchor 4, when `px > w/3` | 124·22 = 2728 ms |
| Moon → Earth | `drawMoonToEarthLine(.., false)` (`:339-368`) | `toEarth` rows `[h-px, h)` at (167, 206+h-px) (bottom→top) | type3 164 "Earth" at (126,160), `px > h/2` | 53·22 = 1166 ms |
| Earth → Moon | `drawMoonToEarthLine(.., true)` | rows `[0, px)` at (167,206) (top→bottom) | 165 "Earth's moon" at (165,64) | 1166 ms |
| Earth → Hell | `drawEarthToHellLine(.., false)` (`:370-399`) | `toHell` rows `[h-px, h)` at (137, 52+h-px) (bottom→top) | 166 "Hell" at (184,23), `px > 2h/3` | 158·22 = 3476 ms |
| Hell → Earth | `drawEarthToHellLine(.., true)` | rows `[0, px)` at (137,52) (top→bottom) | 164 "Earth" at (40,226) | 3476 ms |
| anything else (same planet, same level, Moon↔Hell) | — | nothing | — | returns true at once (`:297-298`) |

"Done" = `px > extent` (strictly greater). Labels (and the Mars→Moon spaceship, parked at u=0) are drawn in phases B–C
(not D–F): phase C calls `drawDottedLine` with `n = 22*max(screenRect[2],screenRect[3])` = 22·480
(`:303-307`, `src/Canvas.cpp:85-86`), past every threshold (corrected 2026-10-29 after G5 review). Spaceship (Mars→Moon only, `:328-329`): with `u = w - px` (unrevealed width),
`x = 200 + u - shipW`, `y = 202 + yCoordOfSpaceShip(u) - shipH/2`, anchor 0, where
(`:333-337`, integer math):

```
k = (u*11)/15
yCoordOfSpaceShip(u) = ((-851*(k*k)/110880 + 6115*k/22176 + 40) * 15) / 11
```

So the ship flies right→left from x = 309 to x = 185 along a parabola.

### 4.2 Locator crosshair (`drawLocatorLines`, `:523-585`)

* `n >= 0` (animating): `step = n / (same ? 15 : 7)` px (1 px / 15 ms on same planet,
  1 px / 7 ms otherwise). Start point: `same` → crosshair of the LAST level
  (`CROSS_HAIR_CORDS[last]` + diff + planet origin of last; Earth origin uses
  `earthCoords[2..3]` if the two-step flag is set, else [0..1]) (`:532-556`);
  otherwise the bottom-right corner `(displayRect[2]-xDiff, displayRect[3]-yDiff)`
  = (480,320) (`:557-560`). Each axis moves `start + sign(target-start)*step`
  independently (`:561-564`).
* `n < 0`: drawn at target (`:566-569`).
* Each axis clamps to target when it reaches/overshoots (`:572-579`); returns true when
  both clamped.
* Draws two full-screen lines: black (0xFF000000) at `x+1` / `y+1` as shadow, then
  0xFFBDFD80 (light green) at `x` / `y` (`:580-583`).

### 4.3 Locator box and name (`drawLocatorBoxAndName`, `:401-430`)

For level `n` with planet origin (Moon/Earth/Hell `[0..1]`):
* magnifier (`magnifyingGlass.bmp`) at `CROSS_HAIR_CORDS[n]` + diff + origin, anchor 3
  (centered), only when `(time & 0x200) == 0` → blink 512 ms on / 512 ms off;
* `highlight.bmp` at `x = LOCATOR_BOX_CORDS[n].x + diff + ox`,
  `y = LOCATOR_BOX_CORDS[n].y + diff + oy + highlightH/2`, anchor 6 (VCENTER|LEFT);
* `graphics.currentCharColor = 3`; level name at `(x + highlightW/2, y)` anchor 3.

## 5. Input (`handleInput`, `src/TravelMapManager.cpp:458-512`)

* `ACTION_MENU` (5): `finishAndLoadLevel()` immediately, in any phase (`:463-466`).
  Mapped from left soft key / `AVK_SOFT1` / `AVK_MENUOPEN` / `AVK_MENU_OPEN`
  (`src/InputEventController.cpp:27,41,75-76,130-131`).
* `ACTION_FIRE` (6) (`AVK_SELECT`, KEY_OK; `src/InputEventController.cpp:40,125-126`;
  touch-up anywhere in this state synthesizes event 6 → `keys_numeric[5]` = FIRE,
  `src/TouchController.cpp:310-313,364-376`, see `loot-inventory.md`):
  * `[6]==1` (prompt shown) → `finishAndLoadLevel()` (`:469-470`).
  * else only if `game->hasSavedState()` (config + BRIEFPLAYER + FULLPLAYER + any
    BRIEFWORLD exist, `src/Game.cpp:2250-2268`) — fast-forward:
    * `[2]!=1` (before/in path): add elapsed to total, `stateVars[0] = now`,
      `[1]=[2]=1`, and `[3]=1` if same planet (`:498-506`) → next is phase C/D.
    * `[2]==1`, `[8]==0`: `[3]=[4]=[6]=1` → jump straight to the prompt (`:474-478`).
    * `[2]==1`, `[8]==1` (two-step): add elapsed; if `[4]==1` → do the step-2
      retarget (`[4]=[8]=0`, target = `CROSS_HAIR_CORDS[load]` + diff + (123,150))
      (`:483-491`); else `[3]=[4]=1` (finish step 1) (`:492-495`).
  * with no saves at all (first new game on a fresh install) FIRE does nothing until
    the prompt; MENU still skips.
* `ACTION_AUTOMAP` (7) finishes only when `[5]==1` (dead star field) (`:509-511`).
* All other actions ignored.

## 6. Forward vs back vs same level

* Direction changes: `scriptStateVars[15]` (0/1/2), the path image and its reveal
  direction + label (§4.1), and the closeups (two-step Earth only when arriving on
  Earth from Moon or Hell). The state machine is otherwise identical.
* Same planet (e.g. 1→2 or 5→4): no path; phase A shows the old level's box 1500 ms,
  then the crosshair glides from the old level's crosshair to the new one at
  1 px/15 ms.
* Same level (load game after a full save, or `lastMapID == loadMapID`): phase A
  700 ms of empty map, no path, 500 ms hold (`same` is false), crosshair from (480,320).
* Forward exits are preceded by `MENU_LEVEL_STATS`; back exits are not (`map-change.md` §2/§3.2).

## 7. Dead code: star-field "cockpit" page

`drawStarFieldPage` (`:587-609`) runs only when `stateVars[5] == 1`, but no code ever
writes `stateVars[5]` except the zeroing in `setState` (`src/Canvas.cpp:1024-1026`;
grep for `stateVars[5]` finds only the two readers `:150,509`). It would draw
`cockpit.bmp` twice (left, and mirrored via rotateMode 4 on the right) over a particle
star field simulated in `tinyGL->pixels` (`drawStarField`/`runStarFieldFrame`,
`:611-876`, ticks every 60 ms via `stateVars[7]`), set right softkey 40 and auto-finish
after 5250 ms (`:591-594`). Not reachable in shipping code; a rewrite can omit it.

## 8. Map scripts reading the travel direction (var 15)

Disassembled with `tools/disasm_map_scripts.py`: `SCR_INIT_MAP` of map00, map01 and
map02 test `v15 == 1` → only `FADEOP in 500ms` (re-entering the same level: no arrival
cinematic); otherwise call the arrival cinematic. map01 `func_1140` additionally
branches `v15 == 0` (camera 12, door sprite 5) vs `v15 == 2` (camera 13, door sprite 47),
i.e. different entry cinematics for arriving forward vs backward. Raw listing lines in
the research log.

## 9. mapID 10 ("Test Map", map09.bin)

Not reachable from shipping game data:
* map08 (VIOS, mapID 9) bytecode fully covered by the disassembler once the 36
  `SETDEATHFUNC` targets (IPs 4045..4605) are passed via `--funcs`; it contains exactly
  one `CHANGE_MAP` (IP 793, back to map 8) and `END_GAME` at IP 4040. `EV_END_GAME`
  sets `gameCompleted`, `endingGame` and `ST_EPILOGUE` (`src/ScriptThread.cpp:1645-1651`)
  — the game ends after VIOS; there is no travel to 10.
* map09.bin is a 2-event treadmill test room (`START_TREADMILL`), level name
  "Test Map" (type3 167).
* The only entry is the debug maps menu `MENU_DEBUG_MAPS`, which lists all 10
  `levelNames` (`src/MenuSystem.cpp:1860-1868`) → `ACTION_CHANGEMAP` →
  `loadMap(1+param)` (`src/MenuSystem.cpp:3130-3133`). The level-stats list stops at
  `levelNamesCount - 1` = 9 (`src/MenuSystem.cpp:3888`). The travel map clamps 10 → 9
  (`src/TravelMapManager.cpp:27`).
