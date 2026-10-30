# Save / load system in the original

Verified facts about how `src/` writes the game to disk and reads it back.
Per-map world records (`SB_n`) and the level-transition save path are covered in
`map-change.md` §3-§5 (and ADR 0034); this file adds the disk layer, the player
and config files, the full-world variant, the load flow and the menus.
Raw log: `../research/2026-10-30-save-load.md`.

## 1. Files, location, IO helpers

| id (`src/Game.h:28-32`) | file name (`GetSaveFile`, `src/Game.cpp:3690-3750`) | writer | content |
|---|---|---|---|
| 0 `FILE_NAME_FULLWORLD` | `FWORLD` | `saveWorldState(OS, false)` | world record of the map current at the manual save |
| 1 `FILE_NAME_CONFIG` | `CONFIGFILENAME` | `saveConfig` / `saveEmptyConfig` | settings + global counters (§3) |
| 2 `FILE_NAME_BRIEFPLAYER` | `BRIEFPLAYER` | `savePlayerState` | player record, level-transition / level-entry point |
| 3 `FILE_NAME_FULLPLAYER` | `FULLPLAYER` | `savePlayerState` | player record, manual save |
| 4 `FILE_NAME_BRIEFWORLD` | `SB_1` .. `SB_10` (index `mapID-1`) | `saveWorldState(OS, true)` | per-map brief world records |

* Exactly **one slot**. No slot list, no headers besides the config version.
* Path: `"Doom2rpg.app/" + name`, relative to the current working directory
  (`dir` constant `src/JavaStream.h:10`; `OutputStream::openFile` builds
  `dir + "/" + fileName`, `src/JavaStream.cpp:224-226`, and creates `dir` with
  `system("mkdir \"Doom2rpg.app\"")` if `stat` fails, `:214-222`;
  `InputStream::loadFile(name, LT_FILE)` same path, `:92-97`;
  `Game::getProfileSaveFileName` same, used for `fopen`/`remove`, `src/Game.cpp:3752-3768`).
* Open modes: `openFile(name, 1)` = `fopen "wb"` (truncate), `src/JavaStream.cpp:244-246`.
  Writes are buffered in 256-byte chunks and `fwrite`n on flush/close (`:265-313`).
* Encoding: **host-endian raw memcpy** (`readData`/`writeData`, `src/JavaStream.cpp:9-24`),
  i.e. little-endian on all supported targets. `writeInt` 4 B, `writeShort` 2 B
  (int16), `writeByte`/`writeBoolean` 1 B. `readShort` returns a sign-extended
  int16, `readByte` an unsigned byte (`:137-160`). Reads have **no bounds check**
  (whole file is `malloc`ed and read once, `:98-112`).
* Entity records are framed by a 4-byte marker `0xCAFEBABE` written LE
  (`Resource::writeMarker`, `src/Resource.cpp:92-102`); `readMarker` just skips
  4 bytes without checking (`:84-90`).
* Any open failure on save → `app->Error(...)` which shows an SDL message box
  and `exit(0)` (`src/Game.cpp:2093,2111`, `src/App.cpp:292-339`). Missing
  player file on load → same fatal path (`src/Game.cpp:2219-2221`).

## 2. When files are written

`saveType` bits are listed in `map-change.md` §3.1. The callers
(`canvas->saveState(type, 3, 196)` → `ST_SAVING` (16), `src/LoadingManager.cpp:47-53`):

| Trigger | saveType | Files | Then |
|---|---|---|---|
| In-game menu "Save Game" (`ACTION_SAVE` 4, param 0) | `3 | 0x80` = 131 | config, FULLPLAYER, FWORLD | `returnToGame()`, `ST_PLAYING` |
| `ACTION_SAVE` with item param 1 | `3 | 0x100` = 259 | same | `MENU_END_FINALQUIT` (`src/MenuSystem.cpp:3039-3049`, `src/Canvas.cpp:892-893`) |
| "Save & Quit" confirm (`ACTION_SAVEQUIT` 13) | `67` = 0x43 | config, FULLPLAYER, FWORLD | `app->shutdown()` (`src/MenuSystem.cpp:3113-3115`, `src/Canvas.cpp:882-883`) |
| "Main Menu" → "Save Game" (`ACTION_SAVEEXIT` 25) | `7` | config, FULLPLAYER, FWORLD | `backToMain(false)` (`src/MenuSystem.cpp:3199-3201`, `src/Canvas.cpp:879-880`) |
| Level exit backwards / no stats | `43` = 0x2B | config, BRIEFPLAYER, `SB_<lastMapID>` | `ST_TRAVELMAP` (`src/LoadingManager.cpp:72-73`) |
| Level exit forward (stats) | `51` = 0x33 | config, BRIEFPLAYER, `SB_<loadMapID>` | `MENU_LEVEL_STATS` (`src/ScriptThread.cpp:510`, `src/Render.cpp:2564`) |
| Level entry (`loadMedia`, not a loaded game, `loadType != 3`) | `saveLevelSnapshot()` | BRIEFPLAYER, `SB_<loadMapID>` (no config) | — (`src/LoadingManager.cpp:675-677`, `src/Game.cpp:2119-2153`) |
| First map load of a fresh install (no save set) | direct `Game::saveState(..., 3)` | config, FULLPLAYER, FWORLD | — (`src/LoadingManager.cpp:682-688`) |
| Options leave / intro seen / every `loadState` | `saveConfig()` only | config | `leaveOptionsMenu` `src/MenuSystem.cpp:4041-4049`, `src/IntroSequenceManager.cpp:875`, `src/Canvas.cpp:838`, `src/LoadingManager.cpp:42` |

`Game::saveState` (`src/Game.cpp:2059-2117`) order: `recentBriefSave = saveType & 0x20`;
`freeRuntimeData()`; if 0x10 `levelGrade(true)`; **`saveConfig()` always**; if bit 0x01 →
player file (BRIEF if 0x20 else FULL); if 0x02 → world file (`SB_<lastMapID>` if
brief else `FWORLD`); `loadRuntimeData()`. After the call `ST_SAVING` shows HUD
message (0,38) "Game Saved" (`src/Canvas.cpp:873`).

Arguments for a manual save (no 0x08/0x10): `saveState(loadMapID, loadMapID,
destX, destY, destAngle, viewPitch, prevX, prevY, saveX, saveY, saveZ, saveAngle,
savePitch, type)` (`src/Canvas.cpp:871`).

There is **no autosave on a timer**; the "auto-saved game" (Restart Level) is the
`saveLevelSnapshot` taken at every level entry.

## 3. Config file `CONFIGFILENAME` (`saveConfig`, `src/Game.cpp:1913-1975`)

| # | type | field |
|---|---|---|
| 1 | int | version **11** (`getSaveVersion()` returns 11, `src/Game.cpp:3367-3369`; literal used) |
| 2 | byte | `difficulty` |
| 3 | bool | `sound->allowSounds` |
| 4 | byte | `soundFxVolume` |
| 5 | byte | `musicVolume` |
| 6 | bool | `vibrateEnabled` |
| 7 | bool | `player->enableHelp` |
| 8 | int | `localization->defaultLanguage` |
| 9 | int | `player->currentLevelDeaths` |
| 10 | int | `player->totalDeaths` |
| 11 | int | `canvas->animFrames` (clamped 2..64 on load) |
| 12 | int | `m_controlLayout` (>2 → 0 on load) |
| 13 | int | `m_controlAlpha` |
| 14 | bool | `isFlipControls` |
| 15-19 | int ×5 | `helpBitmask`, `invHelpBitmask`, `ammoHelpBitmask`, `weaponHelpBitmask`, `armorHelpBitmask` |
| 20 | int ×10 | `menuSystem->indexes[0..9]` (pairs) |
| 21 | bool | `canvas->recentBriefSave` |
| 22 | bool | `game->hasSeenIntro` |
| 23 | short ×10 | `numLevelLoads[0..9]` |
| 24 | int | `totalPlayTime += (upTimeMs - lastSaveTime)/1000` (seconds) |
| 25 | int | `player->currentGrades` |
| 26 | int | `player->bestGrades` |
| GEC | int, bool, int, int, int, bool | `windowMode`, `vSync`, `resolutionIndex`, `gVibrationIntensity`, `gDeadZone`, `_glesObj->isInit` |
| GEC | int ×(16×10) | `keyMapping[KEY_MAPPIN_MAX=16].keyBinds[KEYBINDS_MAX=10]` (`src/Input.h:86,94`) |
| end | int | `0xDEADBEEF` |

Size: 129 bytes original part + 18 + 640 + 4 = **791 bytes**.

`loadConfig` (`src/Game.cpp:1977-2054`) reads the same order only when the
version int is 11 (else silently ignored); marker mismatch → fatal
`Error("Failed marker check in loadConfig()")`. Called once at startup after all
subsystems (`src/App.cpp:120`).

`saveEmptyConfig` (`src/Game.cpp:2320-2370`, called by `removeState`) writes
version 11, difficulty **1**, current sound/vibrate/help/language/anim/layout/
alpha/flip, zeros for deaths, help masks, indexes, `recentBriefSave = false`,
the real `hasSeenIntro`, `numLevelLoads`, `totalPlayTime`, zero grades, then only
`windowMode`, `vSync`, `resolutionIndex` and the marker (142 bytes).
**Port bug**: it omits vibration/deadzone/isInit/keybinds, so a `loadConfig` of
an empty config would read the marker as `gVibrationIntensity`, run past the
buffer and fail the marker check. In practice every new game rewrites the full
config at the first map load (§2, last-but-one row) before any `loadConfig`.

## 4. Player file (`FULLPLAYER` / `BRIEFPLAYER`)

`Game::savePlayerState` (`src/Game.cpp:2155-2171`) header, then `Player::saveState`
(`src/Player.cpp:1351-1420`):

| type | field |
|---|---|
| short | `loadMapID` (map to load; for 0x10 saves = `LEVEL_STATS_nextMap`) |
| short ×4 | `viewX`, `viewY`, `viewAngle`, `viewPitch` |
| int ×5 | `prevX`, `prevY`, `saveX`, `saveY`, `saveZ` |
| short ×2 | `saveAngle`, `savePitch` |
| 11 B | `baseCe->saveState(OS, true)`: short `stats[0]` (health), short `stats[1]`, bytes `stats[2..7]`, byte `weapon` (`src/CombatEntity.cpp:113-129`) |
| 11 B | `ce->saveState(OS, true)` (same layout) |
| short | `characterChoice` |
| int, int | `weapons`, `weaponsCopy` (bitmasks) |
| byte | `level` |
| int | `currentXP` (`nextLevelXP` recomputed on load) |
| int | `totalTime` (updated `+= gameTime - playTime` before write) |
| int ×5 | `totalMoves`, `completedLevels`, `killedMonstersLevels`, `foundSecretsLevels`, `disabledWeapons` |
| byte | `prevWeapon` |
| int, int | `gamePlayedMask`, `lastCombatTurn` |
| bool | `inCombat` |
| short | `highestMap` |
| bool | `isFamiliar` |
| short | `familiarType` |
| int ×5 | `playerEntityCopyIndex`, `hackedVendingMachines`, `vendingMachineHackTriesLeft1`, `…2`, `chainsawStrengthBonusCount` |
| byte | `lastSkipCode` |
| short ×9, ×9 | `ammo[9]`, `ammoCopy[9]` |
| short ×26, ×26 | `inventory[26]`, `inventoryCopy[26]` |
| byte | `numStatusEffects`; if ≠ 0: 18 × (short `statusEffects[36+i]` stack count, short `[0+i]` turns, short `[18+i]` amount) |
| only if `isFamiliar`: byte | `numStatusEffectsCopy`; if ≠ 0 the same 18 × 3 shorts from `statusEffectsCopy` |
| int ×8 | `counters[8]` |
| bool | `gameCompleted` |

Size: 34-byte header + 271 bytes = **305 bytes** with no status effects and not
familiar; +108 when status effects exist. Status-effect index meaning
(`[0+n]` turns, `[18+n]` amount, `[36+n]` stack count): `addStatusEffect`,
`src/Player.cpp:1574-1592`.

Load (`Game::loadPlayerState`, `src/Game.cpp:2173-2189`; `Player::loadState`,
`src/Player.cpp:1270-1349`) reads the same order: `saveStateMap = short`;
`viewX/Y/Angle/Pitch` copied into both `view*` and `dest*`; `viewZ = destZ = 36`;
`prev*/save*` restored; `spawnParam = 0`; `setCharacterChoice` (also writes
`scriptStateVars[14]`); `setFamiliarType`; status arrays zeroed when count 0;
finally `translateStatusEffects()` and `updateStats()`.

Not in the player file: `currentLevelDeaths`, `totalDeaths`, grades, help
bitmasks (config); `moves`, notebook, `questComplete/Failed` (world record).

## 5. World files (`SB_n` brief, `FWORLD` full)

Record layout: `map-change.md` §4 (`saveWorldState`, `src/Game.cpp:1576-1734`).
The map id is **not** stored; it comes from the player file. Differences of the
full variant (`b = false`):

* `placedBombs[4]` written for real (brief writes 0s) (`src/Game.cpp:1599-1606`).
* `getSaveHandle(false)` (`src/Entity.cpp:1787-1857`): dropped dynamite is kept
  (brief drops it, `:1797-1799`); live monsters do **not** get the compact
  `0x200000` flag and non-gibbed corpses are not forced to `0x80000` (brief
  rule `:1849-1855`). So `Entity::saveState` (`src/Entity.cpp:1497-1572`) writes
  the full monster record: `info>>16` byte, sprite info bytes, x/y>>3 or
  `linkIndex`, `monster->flags`, scale byte if flag 0x200, and unless
  `0x100000`: short `monsterEffects`, `ce.saveState(OS,false)` (short health) and
  `saveGoalState` int = `type | flags<<4 | turns<<8 | x<<12 | y<<17 | param<<22`
  (`src/EntityMonster.cpp:25-37`).
* Map-sprite hidden bits: brief additionally marks non-gibbed `ET_CORPSE`
  (`src/Game.cpp:1643-1650`).
* Lerp sprites (`src/LerpSprite.cpp:17-43`) and script threads
  (`src/ScriptThread.cpp:30-43`) are written in both variants. A manual save
  happens from `ST_MENU`, so normally no movers are active, but nothing forces it.
* Both variants write the byte `activeWeaponDef->tileIndex` iff the player's
  current weapon is 14 (pick-up weapon); the reader keys on the already
  loaded player weapon (`src/Game.cpp:1596-1598,1774-1776`).

Validation: first int must equal the loaded map's `mapCompileDate`, otherwise
the record is ignored (`totalLevelTime = 0`, and `spawnParam = 0` if the stored
date is non-zero) (`src/Game.cpp:1759-1760,1902-1909`). No other version field.

## 6. Save detection

`Game::hasSavedState()` (`src/Game.cpp:2249-2268`) is true iff
`hasConfig()` (file opens and first int == 11, `:2225-2235`) **and**
`BRIEFPLAYER` exists **and** `FULLPLAYER` exists **and** at least one of
`SB_1..SB_10` exists (`hasElement` = `fopen "rb"` succeeds, `:2237-2247`).
FWORLD is not checked.

Uses: `MENU_MAIN` prepends "Continue" (`MenuStrings::CONTINUE_ITEM`, flags 8,
action `ACTION_CONTINUE` 22) (`src/MenuSystem.cpp:1256-1261`); `ACTION_NEWGAME`
goes to `MENU_MAIN_CONFIRMNEW` instead of difficulty (`:3072-3079`); help menu
return index (`src/InputEventController.cpp:401-406`); travel-map skip input
(`src/TravelMapManager.cpp:461,472`); `loadMedia` full-save fallback (§2).

Wipe: `MenuSystem::startGame(true)` (new game after difficulty) calls
`removeState(true)` → `saveEmptyConfig()`, `remove` FWORLD, FULLPLAYER,
BRIEFPLAYER, SB_1..SB_10 (`src/MenuSystem.cpp:3580-3604`, `src/Game.cpp:2271-2318`).

## 7. Load flow

Entry points all call `canvas->loadState(type, 3, 194)` →
`LoadingManager::loadState`: `canvas->loadType = type`, `saveConfig()`,
loading-bar text (3,194) "Loading Game…", `setState(ST_LOADING)` (7)
(`src/LoadingManager.cpp:38-45`):

| Entry | type |
|---|---|
| Main menu "Continue" (`ACTION_CONTINUE` → `startGame(false)`, `src/MenuSystem.cpp:3180-3182,3605-3608`) | `getRecentLoadType()` |
| In-game "Load Game" → `MENU_INGAME_LOAD` confirm → `ACTION_LOAD` 3 (`:3035-3037`) | `getRecentLoadType()` |
| Dead menu "Load Saved Game" (`ACTION_LOAD`) | `getRecentLoadType()` |
| "Restart Level" confirm / dead menu (`ACTION_RESTARTLEVEL` 12, `:3105-3111`) | **2**; HUD (3,195) "Game Loaded" unless from `MENU_INGAME_DEAD` |

`getRecentLoadType()` = `recentBriefSave ? 2 : 1` (`src/LoadingManager.cpp:753-760`).
`recentBriefSave` is set by the last `Game::saveState` and persisted in config:
after a manual save it is false (type 1 = FULLPLAYER + FWORLD); after a level
transition it is true (type 2 = BRIEFPLAYER + `SB_<map>`, i.e. the entry
snapshot of the level the player was in, because `saveLevelSnapshot` overwrites
BRIEFPLAYER on entry).

Sequence:

1. `ST_LOADING`, `loadType != 0` → `Game::loadState(loadType)`, then HUD (0,39)
   "Game Loaded", `loadType = 0` (`src/Canvas.cpp:904-915`).
2. `Game::loadState` (`src/Game.cpp:2191-2223`): type 2 first zeroes
   `currentLevelDeaths`; opens FULLPLAYER (1) / BRIEFPLAYER (2); `loadPlayerState`;
   `activeLoadType = type`; if `viewX != 0 && viewY != 0`:
   `spawnParam = (viewX>>6 & 31) | (viewY>>6 & 31)<<5 | ((viewAngle & 0x3FF)>>7)<<10`
   (position is restored to the **tile centre and 45° direction**, not the exact
   coords); `canvas->loadMap(saveStateMap, false, false)`; `isSaved = false`,
   `isLoaded = true`.
3. `loadMap` (`src/LoadingManager.cpp:55-79`): bumps `numLevelLoads`; the save
   branch is skipped because `activeLoadType != 0`; text = level name;
   `ST_TRAVELMAP`. For type 1 the travel map starts from `TM_LoadLevelId`
   (`src/TravelMapManager.cpp:28`).
4. Travel map ends → `ST_LOADING` with `loadType 0` → `loadMedia()`
   (`map-change.md` §3.3). Load-specific branches:
   * bar text (0,39) when `isLoaded` (`src/LoadingManager.cpp:645-650`);
   * `loadMapEntities` resets, `player->levelInit()`, then `loadWorldState()`
     reads **FWORLD** if `activeLoadType == 1` else `SB_<loadMapID>`
     (`src/Game.cpp:1736-1754`); `prepareMonsters` resurrection is skipped when
     `isLoaded` (`src/Game.cpp:706`);
   * `spawnPlayer()` uses `spawnParam` from step 2;
   * no `saveLevelSnapshot` (`:675`), `loadRuntimeData()` instead of first save (`:682-684`);
   * static func 0 (and 1 if `gameCompleted`) still run; the entrance tile event,
     `prevX/Y` reset and help dequeue are skipped (`:700-706`);
   * finally `isSaved = isLoaded = false`, `activeLoadType = 0`, `ST_PLAYING` (`:712-741`).

The old map (in-game load) stays resident until `loadMedia`'s `unloadMedia()`.
From the main menu the map was already unloaded by `backToMain`
(`src/Canvas.cpp:1372-1380`).

## 8. Menus and strings

Menu ids `src/Menus.h:45-124`; root `MENU_INGAME` (29) items decoded in
`ui.md` §11.1. Strings verified from shipped `strings.idx`/`strings0x.bin`
(table 0 = common, 3 = menu).

| Menu / item | Behaviour | Text |
|---|---|---|
| `MENU_INGAME` #2 "Save Game" | `ACTION_SAVE` direct, no confirm; **disabled (flag 4) while `inventory[18]` (`INV_OTHER_JOURNAL`, `src/Enums.h:221`) == 0** (`src/MenuSystem.cpp:1560-1562`) | — |
| `MENU_INGAME` #3 "Load Game" → `MENU_INGAME_LOAD` 49 | `SetYESNO(136, 1, ACTION_LOAD, 0)` (`:1714-1719`) | 3:136 "Load Game?" |
| `MENU_INGAME_LOADNOSAVE` 50 | "No Saved Game" + Back; no code path reaches it (grep) | — |
| `MENU_INGAME` #8 "Restart Level" → 52 | `SetYESNO(134, 1, ACTION_RESTARTLEVEL, 0)` (`:1728-1733`) | 3:134 "Restart Level?" |
| `MENU_INGAME` #9 "Save & Quit" → 53 | `SetYESNO(135, 1, ACTION_SAVEQUIT, 0, 2, 0)` (`:1735-1740`) | 3:135 "Save & Quit?" |
| `MENU_INGAME` #10 "Main Menu" → 42 | data menu: Save Game (`ACTION_SAVEEXIT`) / Don't Save (`ACTION_BACKTOMAIN`) / Cancel | — |
| camera active: `MENU_INGAME_SPECIAL_EXIT` 58 | `SetYESNO(137, 0, ACTION_MAIN_SPECIAL, 0)` | 3:137 "You will lose all unsaved progress. Are you sure?" |
| `MENU_INGAME_DEAD` 51 | Load Saved Game / Restart Level / Main Menu (`player-damage.md` §3.4) | — |
| `MENU_MAIN` "Continue" | only if `hasSavedState()` | `CONTINUE_ITEM` |
| `MENU_MAIN_CONFIRMNEW` 14 | `SetYESNO(107, 0, ACTION_GOTO, MENU_MAIN_DIFFICULTY)` (`:1352-1357`) | 3:107 "This will erase your saved game. Are you sure?" |
| loading bar | save (3,196) "Saving Game…", load (3,194) "Loading Game…", map load (0,41) "Loading…" | |
| HUD | (0,38) "Game Saved", (0,39) / (3,195) "Game Loaded" | |

`SetYESNO(short id, defSel, yesAction, yesParam)` → No = `ACTION_BACK` (2);
`defSel == 1` preselects "Yes", else "No" (`src/MenuSystem.cpp:3611-3660`).
