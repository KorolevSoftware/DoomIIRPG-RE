# 2026-10-29 — Travel map screen (ST_TRAVELMAP) + how mapID 10 is reached

## Questions
1. What `ST_TRAVELMAP` draws, its timing/animation, input, exit, forward vs back
   differences, and what it reads from game state.
2. Open question from `2026-10-29-map-change.md`: map08 (mapID 9) has only a back
   exit — how is mapID 10 reached?

## Method
* Read `src/TravelMapManager.{h,cpp}` fully (876 lines), plus call sites
  (`src/Canvas.cpp:438,827,885,1020-1064`, `src/LoadingManager.cpp:55-79,631-635,753-759`,
  `src/InputEventController.cpp:24-140,429-431`, `src/TouchController.cpp:254-376`),
  constants `src/Canvas.h:95-106`, anchors `src/Graphics.cpp:350-368`, render modes
  `src/Render.h:18-30`, `src/Image.cpp:158-181`.
* Grep: `stateVars[5]` (no writer), `recentBriefSave`, `levelNames`, `scriptStateVars[15]`.
* Scratch Python (scratchpad only) parsing the IPA: `strings.idx`/`stringsNN.bin`
  (format `src/Resource.cpp:169-207`), tables.bin table 7 (`src/Resource.cpp:209-311`),
  BMP header sizes.
* `tools/disasm_map_scripts.py` on map00..map09 from the IPA; map08 rerun with
  `--funcs` = all 36 `SETDEATHFUNC` targets.

## Verdicts
1. CONFIRMED / documented in `docs/original-code/travel-map.md`.
2. RESOLVED: mapID 10 is "Test Map" (debug treadmill room), reachable only via the
   debug maps menu; VIOS (9) ends the game with `END_GAME`.

## Evidence (key points)
* init: `TM_LoadLevelId = max(1, min(loadMapID, 9))`; last = load when
  `getRecentLoadType()==1 && !TM_NewGame` else `max(0,min(lastMapID,9))`; last 0 with
  load > 1 → load-1 (`src/TravelMapManager.cpp:27-32`). `scriptStateVars[15]` = 0/1/2
  for load >/==/< last (`:33-43`); preserved by `loadMedia` (`src/LoadingManager.cpp:631-635`).
* Planets: Moon 1-3, Earth 4-6, Hell ≥7 (`:446-456`).
* Phase timings: A 700 ms (1500 ms same planet) (`:172`); path 1 px/22 ms (`:283`);
  hold 500 ms (`:204`); crosshair 1 px/7 ms (1 px/15 ms same planet) (`:531`);
  settle 800 ms (`:234`); then prompt with common str 96 until input (`:212-224`).
  Grid scroll `(t/200)%44` (`:437`). Magnifier blink `(time & 0x200)==0` (`:188,217`).
* Input: `ACTION_MENU` → finish anytime (`:463-466`); `ACTION_FIRE` → finish at prompt,
  else fast-forward only if `hasSavedState()` (`:468-507`); `ACTION_AUTOMAP` only in dead
  star page (`:509-511`). Touch-up → event 6 → FIRE (`src/TouchController.cpp:310-313`).
* Exit: `clearSoftKeys`, `dispose`, `setLoadingBarText(0,41)`, `ST_LOADING` (`:514-521`).
* Star field page dead: `stateVars[5]` only read (`:150,509`), only zeroed by `setState`
  (`src/Canvas.cpp:1024-1026`).

### Strings (IPA, language 0)
* type0[41] `Load-ing\x85`, type0[96] `Tap Screen to Con-tin-ue`, type0[38] `Game Saved`.
* type3[164] `Earth`, [165] `Earth's moon`, [166] `Hell`.
* tables.bin table 7 (`levelNames`) = 155,156,157,158,159,160,161,162,163,167 →
  Ty-cho, Ci-chus, Kep-ler, UAC Ad-min, UAC Labs, UAC R&D, Ge-hen-na, Abad-don, VIOS,
  Test Map.

### Image sizes (BMP headers)
TravelMap 480x320, TM_Levels1 135x98, TM_Levels2 158x129, TM_Levels3 135x98,
TM_Levels4 182x156, toMoon 124x64, toEarth 37x53, toHell 68x158, highlight 100x24,
magnifyingGlass 24x15, spaceShip 15x20, cockpit 240x234, travelMapHorzGrid 240x1,
travelMapVertGrid 1x640.

### var 15 in scripts (disassembler)
```
map00 SCR_INIT_MAP  236: EVAL [ v15 == 1 ] ; iffalse -> loc_249   243: FADEOP in 500ms   249: CALL_FUNC func_1889
map01 SCR_INIT_MAP  127: EVAL [ v15 == 1 ] ; iffalse -> loc_140   134: FADEOP in 500ms   140: CALL_FUNC func_1140
map01 func_1140    1155: EVAL [ v15 == 0 ] -> 1162 STARTCINEMATIC camera=12, DOOROP sprite=5 OPEN ...
                   1175: EVAL [ v15 == 2 ] -> 1182 STARTCINEMATIC camera=13, DOOROP sprite=47 OPEN ...
map02 SCR_INIT_MAP  105: EVAL [ v15 == 1 ] ; iffalse -> loc_118   112: FADEOP in 500ms
```

### mapID 10
* map08 default CFG leaves a gap 4045..4915; rerun with
  `--funcs 4045,4061,...,4605` (36 SETDEATHFUNC targets) → no gaps > 12 bytes; still one
  `CHANGE_MAP` (`793: CHANGE_MAP map=8 dir=6 tile=168`) and `4040: END_GAME`.
* `EV_END_GAME` → `gameCompleted`, `endingGame`, `ST_EPILOGUE` (`src/ScriptThread.cpp:1645-1651`).
* map09: `SCR_INIT_MAP` = ASSIGN_LOOTSET; events 0/1 = `START_TREADMILL` — a test room.
* Debug menu `MENU_DEBUG_MAPS` lists 10 levels (`src/MenuSystem.cpp:1866-1868`) →
  `ACTION_CHANGEMAP` `loadMap(1+param)` (`src/MenuSystem.cpp:3130-3133`).

## Open questions
* Visual check of the TM_Levels images vs. coordinate offsets (the Earth crosshair
  origin (123,150) vs. `TM_Levels4` draw origin (113,140) looks like a 10 px skew in the
  original data; not verified on screen).
* `hasSavedState()` gating of FIRE skip on the very first playthrough was not tested
  at runtime.
