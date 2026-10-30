# Map change (level transition) in the original

Verified facts about how `src/` moves the player from one map to another and how
per-map state survives a revisit. Complements `game-flow.md` §3.3-3.4 (the full
`loadMedia` pipeline, step by step), `fades.md` (fade math/flags) and
`cutscenes-camera.md` §5. The level-stats screen (`MENU_LEVEL_STATS`) and the
travel-map / loading-bar visuals are covered elsewhere; only their hand-off points
are listed here. Raw log: `../research/2026-10-29-map-change.md`.

Map ids: `mapID` 1..10 ↔ `map00.bin`..`map09.bin`
(`RES_MAP_FILE_ARRAY[mapNameID - 1]`, `src/Resource.h:19`, `src/LoadingManager.cpp:362`).

## 1. Entry points

| Caller | Call | Notes |
|---|---|---|
| `EV_CHANGE_MAP` (opcode 11, `src/Enums.h:413`) | see §2 | the only in-game data-driven exit |
| `Render::fadeScene`, CHANGEMAP flag | `loadMap(LEVEL_STATS_nextMap, false, false)` | `src/Render.cpp:2555-2559` |
| `Render::fadeScene`, SHOWSTATS flag | `canvas->saveState(51, 3, 196)` | `src/Render.cpp:2561-2565` |
| Level-stats menu "continue" | `loadMap(LEVEL_STATS_nextMap, true, false)` (unless `endingGame`) | `src/MenuSystem.cpp:2996-3000` |
| Debug menu `ACTION_CHANGEMAP` (17) | `spawnParam = 0; loadMap(1 + param, false, false)` | `src/MenuSystem.cpp:3130-3133` |
| Cheats 1666 / 123414 | `loadMap(loadMapID, true, false)` (reload current, no save) | `src/MenuSystem.cpp:296-298`, `src/CAppContainer.cpp:145-147` |
| New game (intro end) | `loadMap(startupMap, false, true)` (`TM_NewGame`) | `src/IntroSequenceManager.cpp:135` |
| Skip-intro start | `loadMap(startupMap, true, false)` | `src/Canvas.cpp:1386-1391` |
| Load game / restart level | `Game::loadState(type)` → `loadMap(saveStateMap, false, false)` | `src/Game.cpp:2191-2215`; restart = `loadState(2)` from `ACTION_RESTARTLEVEL`, `src/MenuSystem.cpp:3105-3106` |

There is no automatic "level end" trigger: exits are always map-script
`EV_CHANGE_MAP` instructions (level doors 277/278 are ordinary doors,
`game-flow.md` §3.4). `Game::changeMapParam` (`src/Game.h:88`) is declared but never
referenced in any `.cpp` (grep).

## 2. `EV_CHANGE_MAP` decode (`src/ScriptThread.cpp:484-517`)

Operands: `B = getUByteArg()`, `S = getUShortArg()` (`:486-487`).

| Bits | Meaning |
|---|---|
| `B & 0x0F` | target mapID → `menuSystem->LEVEL_STATS_nextMap` (`:491`) |
| `(B >> 4) & 7` | spawn facing dir (0..7, 45° steps) |
| `B & 0x80` | fade-out 1000 ms before switching (`:496-507`) |
| `S & 0x3FF` | spawn tile `x | y<<5` (x,y 0..31) |

Effects, in order:
1. `player->completedLevels |= 1 << (loadMapID - 1)` — marks the map being LEFT (`:489`).
2. `game->spawnParam = (((B>>4)&7) << 10) | (S & 0x3FF)` (`:490`).
3. `showStats = (nextMap >= loadMapID)` — going back to a lower map never shows stats (`:488,492-494`).
4. `game->snapAllMovers()` — finish all lerp sprites instantly (`:495`, body `src/Game.cpp:2500-2505`).
5. Dispatch:
   * bit7 set: if `ST_AUTOMAP` → `ST_PLAYING`; `startFade(1000, FADEOUT | (showStats ? SHOWSTATS : CHANGEMAP))` (`:496-507`); on completion `fadeScene` performs the same calls as the no-fade branches (§1 table).
   * else if showStats: `canvas->saveState(51, 3, 194)` (`:509-510`).
   * else: `canvas->loadMap(nextMap, false, false)` (`:512-513`).
6. `canvas->changeMapStarted = true` (`:515`) — blocks playing input
   (`src/PlayingInputHandler.cpp:29`, `src/InputEventController.cpp:417`); cleared
   only inside `loadMapData` (`src/LoadingManager.cpp:597`).

Shipping data never sets bit7: all 17 `CHANGE_MAP` sites in map00-map08 (listed in
the research log) print without the fade suffix in `tools/disasm_map_scripts.py`
(the tool labels bit7 "fade+stats", but bit7 only selects the fade; stats depend on
the map comparison). map00's exit is preceded by its own `EV_FADEOP` 800 ms
(`fades.md`). map09 (mapID 10) has no `CHANGE_MAP`. mapID 10 is the debug "Test Map" (treadmill room), reachable only via
`MENU_DEBUG_MAPS`; VIOS (map08) ends the game with `END_GAME` (`travel-map.md` §9).

## 3. `loadMap` → save → travel map → load

`Canvas::loadMap` forwards to `LoadingManager::loadMap(id, b, tm_NewGame)`
(`src/Canvas.cpp:1237`, `src/LoadingManager.cpp:55-79`):

1. If `1 <= id <= 10`: `numLevelLoads[id-1]++`; if it was 0 (first visit),
   `player->currentLevelDeaths = 0` (`:59-66`).
2. `lastMapID = loadMapID; loadMapID = id` (`:67-68`); `sound->soundStop()`;
   `travelMapManager.TM_NewGame = tm_NewGame` (`:69-70`).
3. If `!b && game->activeLoadType == 0 && 1 <= lastMapID <= 10`:
   `saveState(43, 3, 196)` → `ST_SAVING` (`:72-73`, `LoadingManager::saveState` `:47-53`).
   Otherwise: loading-bar text = `levelNames[loadMapID-1]`, `setState(ST_TRAVELMAP)` (`:75-77`).

Nothing is unloaded here: the OLD map stays resident (geometry, entities, script
vars) through `ST_SAVING` and `ST_TRAVELMAP`; that is what lets `ST_SAVING` serialize it.

### 3.1 saveType bits (`src/Canvas.cpp:853-903`, `src/Game.cpp:2059-2117`)

| Bit | Meaning |
|---|---|
| 0x01 | write player file (`savePlayerState`) |
| 0x02 | write world file (`saveWorldState`) |
| 0x04 | → main menu afterwards |
| 0x08 | level exit: player saved at `spawnParam` coords for `loadMapID` (new map), world saved for `lastMapID`; → `ST_TRAVELMAP` |
| 0x10 | stats exit: player saved at `spawnParam` coords for `LEVEL_STATS_nextMap`, world for current `loadMapID`; `levelGrade(true)`; sound 1069; → `MENU_LEVEL_STATS` |
| 0x20 | BRIEF save: `BRIEFPLAYER` + `BRIEFWORLD[lastMapID-1]` (`SB_1..SB_10`) instead of `FULLPLAYER`/`FWORLD` (`src/Game.cpp:2064,2077-2081,2099-2104`, names `src/Game.cpp:3690-3715`) |
| 0x40 | shutdown; 0x80 return to game; 0x100 final-quit menu |

`43 = 0x2B` = player+world+exit+brief; `51 = 0x33` = player+world+stats+brief.
Spawn coords written for 0x08/0x10: `x = 32 + (spawnParam & 0x1F)*64`,
`y = 32 + ((spawnParam>>5) & 0x1F)*64`, `angle = ((spawnParam>>10) & 0xFF) << 7`,
pitch 0, saveZ 36 (`src/Canvas.cpp:857-859,866-868`). With 0x08 and
`spawnParam == 0` all coords are written as 0 (`:861-862`).
After the save `hud->addMessage(0, 38)` and `clearEvents(1)` (`:873,902`).

### 3.2 State sequence

* Forward exit (no fade): `EV_CHANGE_MAP` → `saveState(51)` → `ST_SAVING`
  (writes SB_cur + BRIEFPLAYER) → `MENU_LEVEL_STATS` → continue →
  `loadMap(next, true)` (skips the second save) → `ST_TRAVELMAP`
  (`TravelMapManager::init`, `src/Canvas.cpp:1062-1063`) → user input / timer →
  `finishAndLoadLevel()` → loading text (0,41), `ST_LOADING`
  (`src/TravelMapManager.cpp:514-521`; callers `:464,470,510,593`).
* Backward exit (no fade): `EV_CHANGE_MAP` → `loadMap(prev, false)` →
  `saveState(43)` → `ST_SAVING` (SB_cur + BRIEFPLAYER) → `ST_TRAVELMAP` → `ST_LOADING`.
* `ST_LOADING` with `loadType == 0` calls `loadMedia()` each frame
  (`src/Canvas.cpp:904-910`). The first call only draws the loading bar and
  returns false (`Canvas::isLoaded` toggle, `src/LoadingManager.cpp:610-616`);
  the second call does the whole load synchronously (`:617-743`).

### 3.3 `loadMedia` order relevant to map change (`src/LoadingManager.cpp:604-744`)

Detailed per-step notes are in `game-flow.md` §3.3; summary:

1. `inInitMap = true`, sounds muted, `unloadMedia()` = `freeRuntimeData()`,
   `Game::unloadMapData()`, `Render::unloadMap()` (`:619-625`, `:746-751`).
   * `Game::unloadMapData` (`src/Game.cpp:642-685`): clears camera/cinematic weapon,
     facing/target pointers, gsprites, monster lists, `entityDb[1024]`, all entities,
     `entityDeathFunctions[128] = -1`, Maya cameras, weapon-down flags,
     `numDestroyableObj/destroyedObj/lootFound = 0`, `angryVIOS = false`, monster sounds.
   * `Render::unloadMap` (`src/Render.cpp:211+`): `startFogLerp(32752, 32752, 0)`
     (fog reset), unload sky, free all media arrays.
2. Zoom off, viewport reset; `scriptStateVars[i] = 0` for all `i != 15`
   (`:626-635`) — var 15 is the only global script variable. It holds the travel direction written by
   `TravelMapManager::init` (0 fwd / 1 same / 2 back; `travel-map.md` §2).
3. `loadMapData(loadMapID)` (`:637`): string table `loadMapStringID = 4 + (mapID-1)`
   (`:309-311`), header incl. `mapSpawnIndex`/`mapSpawnDir`, per-map media,
   geometry, events, bytecode, sky pair; resets `changeMapStarted`, dizzy (`:597-599`).
4. `highestMap = max(highestMap, loadMapID)` for ids ≤ 10 (`:641-643`).
5. `loadMapEntities()` (`:658`) — per-map reset (`src/Game.cpp:332-360`): open doors,
   watchLine, `setPickUpWeapon(15)`, placedBombs, 275 entities, `levelVars[8]`,
   visited tiles, `secretActive`, lerp sprites (16), script threads (20); then
   spawn from map sprites.
6. HUD msgs cleared, `playTime = curLevelTime = gameTime`, `clearEvents(1)`,
   particles freed, `player->levelInit()` (`:659-667`): `moves`, notebook count,
   `questComplete/Failed` = 0, `inCombat = false`, health 0 → 1
   (`src/Player.cpp:94-105`).
7. `loadWorldState()` (`:670`) — §4.
8. `spawnPlayer()` (`:673`, `src/Game.cpp:941-972`): if `spawnParam != 0`:
   `x = p & 0x1F`, `y = (p>>5) & 0x1F`, `dir = (p>>10) & 0xFF`, then `spawnParam = 0`;
   else `loadType == 3` → (3,15) dir 6; else header `mapSpawnIndex % 32`,
   `/ 32`, `mapSpawnDir`. `angle = dir << 7 & 0x3FF`; `view = dest = tile*64+32`;
   `z = getHeight + 36`; `relink()`. `knockbackDist = 0` (`:674`).
   So the change-map args win over the header; the header is the fallback.
9. If not a loaded game and `loadType != 3`: `saveLevelSnapshot()` (`:675-677`,
   `src/Game.cpp:2119-2153`) writes BRIEFPLAYER + `BRIEFWORLD[loadMapID-1]` = state
   at entry (the "restart level" point).
10. Runtime images reloaded if a save exists (`:682-684`); otherwise (very first
    game, no saves) a full `saveState(...,3)` (`:686-688`).
11. `selectWeapon(current)`, `scriptStateVars[12] = difficulty`,
    `executeStaticFunc(0)` (map init), `executeStaticFunc(1)` if `gameCompleted`
    (`:691-698`).
12. If not loaded: `prevX/Y = destX/Y`, **entrance tile event**
    `executeTile(viewX>>6, viewY>>6, 4081, 1)`, `finishRotation`,
    `dequeueHelpDialog(true)` (`:700-706`).
13. `endMonstersTurn()`, `uncoverAutomap()`, clear `isSaved/isLoaded/activeLoadType`,
    `ST_PLAYING` if state is 0, unpause, `blockInputTime = gameTime + 200`,
    restore sounds, `inInitMap = false`, render one frame, randomize
    `monsterIdleTime[18]`, and `ST_LOADING && !loadType` → `ST_PLAYING`
    (`:707-741`).

So the first scripts of the new map run inside the same `loadMedia` call:
static func 0, (static func 1), then the spawn tile's event — all before the first
`ST_PLAYING` frame.

## 4. Per-map persistence on revisit (BRIEFWORLD)

Yes, the original keeps the state of every visited map, **on disk**, one file per
map: `BRIEFWORLD[mapID-1]` = `SB_<mapID>` (`src/Game.cpp:3706-3715`).

* Write on leave: `saveState(43|51)` writes the still-loaded old map via
  `saveWorldState(OS, brief=true)` (`src/Game.cpp:2097-2112`).
* Write on entry: `saveLevelSnapshot()` overwrites the new map's file right after
  restore + spawn (`src/Game.cpp:2143-2148`).
* Read on entry: `loadWorldState()` (`src/Game.cpp:1736-1754`): skipped for
  `loadMapID > 10`; reads `FWORLD` when `activeLoadType == 1` (full load), else
  `BRIEFWORLD[loadMapID-1]`. Missing file or failed parse → `totalLevelTime = 0`
  (fresh map from `.bin`).
* Validation: first int must equal the map's `mapCompileDate`; else
  `totalLevelTime = 0` and, if the stored date is non-zero, `spawnParam = 0`
  (fall back to header spawn) (`src/Game.cpp:1756-1758,1902-1909`).
* Wiped on new game: `removeState(true)` deletes FWORLD, FULLPLAYER, BRIEFPLAYER and
  all 10 SB files (`src/Game.cpp:2271-2318`, called `src/MenuSystem.cpp:3587`).

World record contents (`saveWorldState`, `src/Game.cpp:1576-1734`; same order read
back `src/Game.cpp:1756-1901`): compile date; `totalLevelTime` (accumulated);
`mapSecretsFound`; fog color/min/range + player fog (after `snapFogLerp`);
pick-up weapon tile if weapon 14; 4 `placedBombs` (brief writes 0s); entity states
(`saveEntityStates`, brief variant skips dropped dynamite, `src/Entity.cpp:1787-1827`);
line flag bit 8 per line (bit-packed); `entityDeathFunctions` pairs; 2 bits per map
sprite (`0x10000` hidden / brief also marks non-gibbed `ET_CORPSE`, and `0x200000`);
tile `mapFlags & 0x80` for 1024 tiles; `scriptStateVars[0..127]` except 15;
tile-event bit 19 (disabled) per event; lerp sprites; 20 script threads;
`dropIndex`; `player->moves`; notebook indexes/positions, `questComplete/Failed`;
`mapFlagsBitmask`; Mastermind/Caldex hacks; watchLine; VIOS counters; `lootFound`;
`destroyedObj`. On load it ends with `prepareMonsters()` (`src/Game.cpp:701-727`):
for a revisited, already-completed map whose monsters are all dead, up to 8 corpses
(at most half of the total) may resurrect, each with a `nextByte() <= 100` roll.

Not persisted per map (reset every load): `levelVars[8]`, open-door slots,
visited tiles, `secretActive` (`src/Game.cpp:332-360`).

## 5. What persists across a map change

The `Player` object is not reloaded on a normal exit (`isLoaded` false; BRIEFPLAYER
is written but not read). Only `Player::levelInit` touches it (§3.3 step 6), so
inventory, weapons/ammo, stats, buffs and `statusEffects` carry over unchanged
(no clearing call on the load path: `removeStatusEffect` callers are
`src/CombatEntity.cpp:274`, `src/Player.cpp:873,1465`, `src/ScriptThread.cpp:1580`).
Also global: `completedLevels`, `highestMap`, `numLevelLoads`, `scriptStateVars[15]`,
`difficulty` (re-copied to var 12). There are no keycard items in the player
state: `TILENUM_KEY_RED/BLUE` (`src/Enums.h:727-728`) are never referenced in a
`.cpp`, so lock state lives in the per-map world data/scripts.
