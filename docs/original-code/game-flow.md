# Game flow & state machine (original `src/`) — verified mechanics

How the legacy RE port structures its top-level loop, canvas state machine,
map lifecycle, turn system and per-frame tick. Every claim cites
`src/<file>:<line>`. Cross-references: movement details in
`docs/original-code/player-collision.md`; door mechanics (incl. the
`advanceTurn` auto-close pass) in `docs/original-code/doors.md`.

Key vocabulary: `app` = the `Applet` service locator (`src/App.h:29`);
`app->upTimeMs` = monotonic wall clock in ms; `app->time` = this frame's
timestamp; `app->gameTime` = gameplay clock that pauses in menus.

--------------------------------------------------------------------------------
## 1. Top-level loop

* Native entry point `main()` (`src/Main.cpp:34`) opens the `.ipa` zip, creates
  the SDL/GL window, then constructs the app via
  `CAppContainer::Construct(&sdlGL, &zipFile)` (`src/Main.cpp:53`) which news
  `Applet` and calls `Applet::startup()` (`src/CAppContainer.cpp:181-188`);
  it also force-set `game->hasSeenIntro = true`
  (`src/CAppContainer.cpp:188`), so the intro movie is skippable by any key.
* **Frame loop** (`src/Main.cpp:73-89`): `while (!closeApplet)` polls real
  time; a frame runs only when `currentTimeMillis > UpTime`, then schedules
  the next frame at `now + 15` (~66 FPS ceiling, variable rate, **no fixed
  timestep**): `input.handleEvents()` → `drawView()` → `input.consumeEvents()`.
* `drawView` (`src/Main.cpp:102-142`) computes `passedTime = now - last`,
  **clamps it to 125 ms** (`src/Main.cpp:133-135`), and calls
  `DoLoop(passedTime)`.
* `DoLoop` (`src/CAppContainer.cpp:55-61`): `sound->startFrame()`; sets
  `canvas->staleView = true`; `canvas->run()`; `sound->endFrame()`; then
  `app->upTimeMs += time`. So `upTimeMs` advances by the *clamped* frame
  delta — the whole engine is driven from this one counter.
* **Clock advancement** inside `Canvas::run` (`src/Canvas.cpp:745-771`):
  - `app->lastTime = app->time; app->time = upTimeMs` (`src/Canvas.cpp:750-751`)
    — `app->time` is refreshed exactly once per frame.
  - `if (!game->pauseGameTime && state != ST_MENU) gameTime += time - lastTime`
    (`src/Canvas.cpp:769-771`). Menus freeze `gameTime`; dialogs do *not*
    (they use their own `player->unpause` bookkeeping instead,
    `src/DialogSystem.cpp:527`).
* **Paint/update split**: all simulation happens inside `Canvas::run` before
  painting; the 3D view is rendered *inside the state tick* (via
  `updateView → renderScene`, `src/MovementController.cpp:544-546`), while the
  2D overlay is painted afterwards by `backPaint(&graphics)`
  (`src/Canvas.cpp:978-980`), driven by `repaintFlags` bits
  (`REPAINT_CLEAR/VIEW3D/PARTICLES/HUD/MENU/STARTUP_LOGO/LOADING_BAR`,
  `src/Canvas.h:122-129`). A [GEC] port addition pre-renders the software 3D
  buffer right after input when `REPAINT_VIEW3D` is set and GLES is off
  (`src/Canvas.cpp:783-789`).
* **Runs every frame regardless of state** (unless `ST_MENU` showing
  `MENU_ENABLE_SOUNDS`, `src/Canvas.cpp:791-796`):
  ```cpp
  app->game->numTraceEntities = 0;
  app->game->UpdatePlayerVars();          // canvas view vars -> game mirror
  app->game->gsprite_update(app->time);   // particle/poof game-sprites
  app->game->runScriptThreads(app->gameTime);
  ```
  (`src/Game.cpp:857-872`, `src/Game.cpp:1464`, `src/Game.cpp:3246-3268`;
  script threads only actually *resume* while `ST_PLAYING`/`ST_CAMERA`,
  `src/Game.cpp:3259-3261`).
* Input dispatch also happens every frame before the state tick:
  `runInputEvents()` (`src/Canvas.cpp:781` → `src/InputEventController.cpp:445-479`)
  pops queued key events (queued from SDL by `Input::handleEvents` →
  `canvas->addEvents`, `src/Input.cpp:1002` et al.;
  `addEvents` keeps a single-slot queue at `events[0]`,
  `src/InputEventController.cpp:481-489`) and routes each through
  `handleEvent(key)`.

--------------------------------------------------------------------------------
## 2. Canvas state machine

### 2.1 All states (`src/Canvas.h:68-93`)

| Value | Name | Meaning |
|---|---|---|
| 1 | `ST_MENU` | MenuSystem overlay owns the screen |
| 2 | `ST_INTRO_MOVIE` | Boot studio/intro movie (table camera + scrolling text) |
| 3 | `ST_PLAYING` | Normal grid-roam gameplay |
| 4 | `ST_INTER_CAMERA` | Interactive cinematic camera (comment: "Wolf -> ST_DRIVING") |
| 5 | `ST_COMBAT` | Turn-based combat encounter |
| 6 | `ST_AUTOMAP` | Full-screen automap over the live world |
| 7 | `ST_LOADING` | Two-phase map/state loading |
| 8 | `ST_DIALOG` | Modal dialog/chat |
| 9 | `ST_INTRO` | Prologue story pages (after character select) |
| 10 | `ST_BENCHMARK` | Speed-test flythrough (`startSpeedTest`, `src/Canvas.cpp:1358-1370`) |
| 11 | `ST_BENCHMARKDONE` | Only referenced by input to exit benchmarking (`src/InputEventController.cpp:350`) |
| 12 | `ST_MINI_GAME` | Hacking / sentry-bot / vending minigame; variant in `stateVars[0]` (`src/Canvas.cpp:450-466`, `src/InputEventController.cpp:334-349`) |
| 13 | `ST_DYING` | Player death fall + fade |
| 14 | `ST_EPILOGUE` | End-of-game scrolling epilogue |
| 15 | `ST_CREDITS` | Credits crawl |
| 16 | `ST_SAVING` | Executes an armed `saveState(saveType,…)` then dispatches |
| 17 | `ST_ERROR` | Fatal-error screen (fire = quit) |
| 18 | `ST_CAMERA` | Non-interactive cinematic playback |
| 19 | `ST_TAF` | Dead — never referenced outside the header |
| 20 | `ST_MIXING` | Vestigial — only stubs in touch/input (`src/InputEventController.cpp:426-428`) |
| 21 | `ST_TRAVELMAP` | Between-level solar-system travel map |
| 22 | `ST_CHARACTER_SELECTION` | Character pick at new game |
| 23 | `ST_LOOTING` | Corpse/container looting UI |
| 24 | `ST_TREADMILL` | Treadmill minigame overlay |
| 25 | `ST_BOT_DYING` | Sentry-bot familiar destruction sequence |
| 26 | `ST_LOGO` | Startup logo (`l2.bmp`) |

### 2.2 How state changes happen

There is exactly **one** mutator, `Canvas::setState(int)`
(`src/Canvas.cpp:1020-1217`):

1. `stateChanged = true` (cleared again at the end of the frame,
   `src/Canvas.cpp:987`); zeroes all 9 `stateVars[]` (`src/Canvas.cpp:1024-1026`)
   — sub-state timelines are rebuilt per entry; drops the held virtual button
   (`src/Canvas.cpp:1027-1028`).
2. **Old-state exit hooks** (`src/Canvas.cpp:1030-1049`): leaving `ST_AUTOMAP`
   unpauses the player (`player->unpause(time - automapTime)`); leaving
   `ST_MENU` unpauses + clears the menu stack; leaving `ST_CAMERA` re-enables
   render activation and clears `skippingCinematic`; leaving `ST_COMBAT` to a
   non-combat state runs `combat->cleanUpAttack()`; leaving `ST_CREDITS`
   disposes the scroll buffer.
3. `oldState = state; state = state_` (`src/Canvas.cpp:1052-1053`).
4. **New-state enter hooks** (`src/Canvas.cpp:1056-1216`): e.g.
   `ST_COMBAT` resets HUD repaint flags and `combatDone`;
   `ST_PLAYING` stamps `game->lastTurnTime`, redraws soft keys, sets
   `updateFacingEntity`, invalidates the view; `ST_DIALOG` collapses any
   sniper-zoom, clears soft keys and events; `ST_DYING` records `deathTime`,
   pitches the view down (`destPitch = 64`) and wipes queued help messages;
   `ST_LOADING`/`ST_SAVING` clear `REPAINT_HUD` and arm the pacifier/loading
   bar (`src/Canvas.cpp:1183-1187`); `ST_TRAVELMAP` calls
   `travelMapManager.init()`; `ST_AUTOMAP` records `automapTime`.

### 2.3 Which handler processes input per state

Single dispatcher `InputEventController::handleEvent(int key)`
(`src/InputEventController.cpp:152-443`), fed by `runInputEvents`:

| State | Handler | Citation |
|---|---|---|
| `ST_LOGO` | none (BACK quits) | `src/InputEventController.cpp:170-172` |
| `ST_MENU` | `menuSystem->handleMenuEvents` | `src/InputEventController.cpp:314-316` |
| `ST_CHARACTER_SELECTION` | `introSequenceManager.handleCharacterSelectionInput` | `src/InputEventController.cpp:317-319` |
| `ST_INTRO` | `introSequenceManager.handleStoryInput` | `src/InputEventController.cpp:320-322` |
| `ST_INTRO_MOVIE` | sets `game->skipMovie` on FIRE | `src/InputEventController.cpp:323-325` |
| `ST_DIALOG` | `dialogSystem.handleDialogEvents` | `src/InputEventController.cpp:331-333` |
| `ST_MINI_GAME` | per-minigame `handleInput` | `src/InputEventController.cpp:334-349` |
| `ST_AUTOMAP` | **also `playingInputHandler.handlePlayingEvents`** (movement still works; automap-awareness is internal) | `src/InputEventController.cpp:354-357` |
| `ST_PLAYING` | `zoomController.handleZoomEvents` if `isZoomedIn`, else `playingInputHandler.handlePlayingEvents` | `src/InputEventController.cpp:358-363` |
| `ST_COMBAT` | inline: L/R rotates view around attacker; when `combatDone && !interpolatingMonsters` → `setState(ST_PLAYING)` + `advanceTurn` | `src/InputEventController.cpp:364-386` |
| `ST_CAMERA` | any key after `cinUnpauseTime` → `game->skipCinematic()` | `src/InputEventController.cpp:416-420` |
| `ST_DYING` | FIRE/BACK jumps to dead menu | `src/InputEventController.cpp:421-425` |
| `ST_TRAVELMAP` | `travelMapManager.handleInput` | `src/InputEventController.cpp:429-431` |
| `ST_LOOTING` / `ST_TREADMILL` | respective controllers | `src/InputEventController.cpp:432-437` |

Key → action mapping lives in `getKeyAction`
(`src/InputEventController.cpp:47-141`) producing `ACTION_UP/DOWN/LEFT/RIGHT/
STRAFELEFT/STRAFERIGHT/FIRE/AUTOMAP/MENU/PASSTURN/PREVWEAPON/NEXTWEAPON/
ITEMS/QUESTLOG/BACK` (`src/Enums.h:992-1010`).

### 2.4 Stacked/sub-states (not `state` values)

* **Zoom (sniper scope)**: boolean `isZoomedIn` *inside* `ST_PLAYING`;
  entering dialogs/death force-collapses it (`src/Canvas.cpp:1084-1091`,
  `src/Canvas.cpp:1116-1124`).
* **Help-message queue**: `dialogSystem.numHelpMessages` gates turn
  advancement and monster updates (`src/Canvas.cpp:812`,
  `src/GameStateRunner.cpp:181`).
* **Menus-over-game**: the menu system has its own push/pop stack while
  `state == ST_MENU` (`src/GameStateRunner.cpp:232-235`); soft keys are part
  of `SoftKeyController`.
* **Loading bar**: `ST_LOADING`/`ST_SAVING` paint through
  `REPAINT_LOADING_BAR` + `LoadingScreenController`; loading is deliberately
  split across **two frames** (see §3.3).
* **Benchmark**: `renderOnly` + `st_enabled` flags repurpose `ST_BENCHMARK`
  (`src/Canvas.cpp:1358-1370`).

### 2.5 Legal transitions (observed call sites)

* Boot: `startup()` ends with `setState(ST_LOGO)` (`src/App.cpp:128`);
  LOGO → `ST_INTRO_MOVIE` (`src/GameStateRunner.cpp:343`); movie done/skipped →
  `backToMain(false)` → `MENU_MAIN` (`src/Canvas.cpp:833-841`, exit path
  `src/Canvas.cpp:1372-1407`).
* New game: menu item → `ST_CHARACTER_SELECTION`
  (`src/MenuSystem.cpp:3604`); confirm → `ST_INTRO`
  (`src/IntroSequenceManager.cpp:674-680`); last story page → `disposeIntro()`
  → `loadMap(startupMap = 1, …)` (`src/Canvas.cpp:848-851`,
  `src/IntroSequenceManager.cpp:131-136`); BACK on page 0 returns to
  character selection (`src/IntroSequenceManager.cpp:249-255`).
* `loadMap` → `ST_TRAVELMAP` or `ST_SAVING` (`src/LoadingManager.cpp:72-77`);
  travel map → `ST_LOADING` (`src/TravelMapManager.cpp:514-521`); loading →
  `ST_PLAYING` (`src/LoadingManager.cpp:739-741`).
* In-game: `PLAYING ↔ AUTOMAP` toggle (`src/PlayingInputHandler.cpp:86-99`,
  `139-141`); `PLAYING → COMBAT` (via `setState(ST_COMBAT)` in the combat
  system; exit conditions `src/GameStateRunner.cpp:26-38`);
  `PLAYING → DIALOG` (scripts, `src/ScriptThread.cpp:545-585`); `DIALOG →`
  `PLAYING`/`CAMERA`/`INTER_CAMERA`/`COMBAT` depending on context
  (`src/DialogSystem.cpp:541-557`); `PLAYING → DYING`
  (`src/Player.cpp:711-733`); `PLAYING → BOT_DYING`
  (`src/Player.cpp:735-752`); `PLAYING → LOOTING`
  (`src/PlayingInputHandler.cpp:374-377`); menus ↔ game via
  `setMenu`/`returnToGame` (`src/MenuSystem.cpp:649-658`, `1209-1220`).
* Ending: fade with `FADE_FLAG_EPILOGUE` → `ST_EPILOGUE`
  (`src/Render.cpp:2567-2571`); `MENU_END_RANKING` → `ST_CREDITS`
  (`src/MenuSystem.cpp:2990-2993`); `ST_SAVING` dispatch table can go to
  travel map, stats menu, main menu or shut down the app
  (`src/Canvas.cpp:879-900`).

The per-state *tick* dispatcher is the big if/else chain in `Canvas::run`
(`src/Canvas.cpp:805-968`); unhandled states raise `app->Error(51)`
(`src/Canvas.cpp:966-968`).

--------------------------------------------------------------------------------
## 3. Game / world / map lifecycle

### 3.1 Boot → playing map00 (new game)

1. `Applet::startup()` constructs subsystems in a strict order — canvas,
   resource, localization, render, tinyGL, entityDefManager, player,
   menuSystem, sound, game, particleSystem, combat — then `game->loadConfig()`,
   `clearEvents(1)`, `setState(ST_LOGO)` (`src/App.cpp:77-128`).
2. `ST_LOGO`: caches sounds, shows `l2.bmp` for ~121 frames, then
   `setState(ST_INTRO_MOVIE)` with input ignored for one frame
   (`src/GameStateRunner.cpp:324-348`).
3. `ST_INTRO_MOVIE`: plays the table-camera movie; ends when
   `(hasSeenIntro && any key pressed) || scrollingTextDone` → `saveConfig()` +
   `backToMain(false)` → main menu (`src/Canvas.cpp:833-841`).
4. Character selection → prologue pages → `disposeIntro()` calls
   `canvas->loadMap(canvas->startupMap /* = 1 */, false, tm_NewGame = true)`
   (`src/IntroSequenceManager.cpp:131-136`; `startupMap = 1` defaulted at
   `src/Canvas.cpp:63,162`).

### 3.2 `loadMap(mapID, b, tm_NewGame)` — the gateway (`src/LoadingManager.cpp:55-79`)

* Increments `game->numLevelLoads[mapID-1]`; because the comparison constant
  is hard-false (`b2 = false`), `player->currentLevelDeaths` resets on the
  **first** entry to a map (`src/LoadingManager.cpp:59-67`).
* Remembers `lastMapID`, stops sounds, stores `TM_NewGame`
  (`src/LoadingManager.cpp:68-71`).
* If we came from another playable map (`b == false && activeLoadType == 0 &&
  1 <= lastMapID <= 10`) it arms a **brief auto-save of the old map**
  (`saveState(43, 3, 196)` → `ST_SAVING`, `src/LoadingManager.cpp:72-74`);
  otherwise it sets the loading-bar caption from
  `game->levelNames[mapID-1]` and goes straight to `ST_TRAVELMAP`
  (`src/LoadingManager.cpp:76-77`).

### 3.3 Travel map → loading → playing

* `ST_TRAVELMAP` (`TravelMapManager::init`, `src/TravelMapManager.cpp:23-121`)
  derives `TM_LoadLevelId`/`TM_LastLevelId`, encodes the travel direction
  into `scriptStateVars[15]` (`src/TravelMapManager.cpp:43`), and animates a
  starfield/dotted-line/closeup timeline built on `stateVars[]`
  (`src/TravelMapManager.cpp:146-256`). FIRE/MENU (or the starfield page
  timing out) → `finishAndLoadLevel()`: dispose art, set bar text, 
  `setState(ST_LOADING)` (`src/TravelMapManager.cpp:458-521`, `587-594`).
* `ST_LOADING` with `loadType == 0` calls `canvas->loadMedia()` which is
  **two-phase**: the first invocation draws the loading bar once and returns
  `false`, making `Canvas::run` repaint and bail for that frame
  (`src/LoadingManager.cpp:610-616`, `src/Canvas.cpp:904-910`); the second
  invocation performs the real load (`src/LoadingManager.cpp:617-744`).
  With `loadType != 0` (continue-from-save) it instead calls
  `game->loadState(loadType)` and shows message 39
  (`src/Canvas.cpp:911-915`).

Ordered `loadMedia()` sequence (`src/LoadingManager.cpp:604-744`):

1. `unloadMedia()`: free runtime HUD images + `game->unloadMapData()` +
   `render->unloadMap()` (`src/LoadingManager.cpp:746-751`).
2. Zero `scriptStateVars` except `[15]` (`src/LoadingManager.cpp:631-635`).
3. `loadMapData(loadMapID)` (`src/LoadingManager.cpp:304-602`):
   * `render->mapNameID = mapID`; `loadMapStringID = 4 + (mapID-1)`;
     load that text file (`src/LoadingManager.cpp:309-311`).
   * Clear `mapFlags`, automap entrance/exit, ladder and keep-pitch tables,
     portal state (`src/LoadingManager.cpp:313-330`).
   * Allocate media arrays and read `NEWMAPPINGS` (media id → texture range)
     (`src/LoadingManager.cpp:333-356`).
   * **42-byte map header** (`src/LoadingManager.cpp:362-397`): version byte
     must be 3; `mapCompileDate` i32; **`mapSpawnIndex` u16, `mapSpawnDir`
     u8**; `mapFlagsBitmask` u8; `totalSecrets` u8; `totalLoot` u8;
     `numNodes` u16; poly-data size u16; `numLines`, `numNormals`,
     `numNormalSprites`, `numZSprites` u16;
     `numMapSprites = normal + z`; `numSprites = numMapSprites +
     MAX_CUSTOM_SPRITES + MAX_DROP_SPRITES`; `numTileEvents` i16;
     `mapByteCodeSize` i16; Maya camera counts; per-kind Maya tween offsets
     (`src/LoadingManager.cpp:389-397`).
   * The map embeds its own **media list**: count + ids, each
     `registerMapMedia(id)` marks palettes/texels REGISTERED
     (`src/LoadingManager.cpp:400-407`, `112-131`); then
     `finalizeMapMedia()` streams only the registered palettes/texels from
     `NEWPALETTES`/`tex*.bin` and uploads GL textures
     (`src/LoadingManager.cpp:133-302`).
   * Allocate BSP/sprite arrays; sprite-field strides `S_X…S_ENT`,
     `S_SCALEFACTOR` are `numSprites * k`
     (`src/LoadingManager.cpp:429-448`).
   * Stream BSP nodes/polys/lines, `heightMap[1024]`, sprite coordinates;
     per-sprite defaults `S_NODE/S_NODENEXT/S_VIEWNEXT/S_ENT = -1`,
     `S_SCALEFACTOR = 64` (full size), `S_Z = 32`
     (`src/LoadingManager.cpp:488-499`); `mapSpriteInfo` arrives as a low-byte
     array then a high-half u16 array (`src/LoadingManager.cpp:501-524`).
   * `staticFuncs[12]`, `tileEvents` (each marks its tile
     `mapFlags |= 0x40`), `mapByteCode`, Maya cameras, packed tile flag
     nibbles (`src/LoadingManager.cpp:542-564`).
   * **Sky selection**: `int skyIndex = ((render->mapNameID - 1) / 5 % 2) * 2;`
     then sky palette table `[16 + skyIndex]` and texel table `[17 + skyIndex]`
     (`src/LoadingManager.cpp:571-584`).
   * Clone every registered palette into 16 light-modulation copies
     (`src/LoadingManager.cpp:587-595`); reset `changeMapStarted`, dizzy
     (`src/LoadingManager.cpp:597-599`).
4. Track `player->highestMap` (`src/LoadingManager.cpp:641-643`).
5. **`game->loadMapEntities()`** (`src/LoadingManager.cpp:658`; body
   `src/Game.cpp:329-508`) — entity creation order:
   1. Reset per-level state: `interpolatingMonsters`, `monstersTurn = 0`,
      `openDoors[0..3] = null`, `watchLine`, bombs, all 275 `entities`,
      `levelVars`, visited tiles, `secretActive`, `numMonsters`, lerp slots,
      20 script threads (`src/Game.cpp:332-360`).
   2. `entities[0]` = world entity (`def find(0,0)`);
      `entities[1]` = **player** entity (`def find(1,0)`), hidden
      (`info = 0x20000`) (`src/Game.cpp:361-369`).
   3. Iterate **every map sprite in index order**
      (`src/Game.cpp:374-486`): effective tileNum `(+257` if
      `SPRITE_FLAG_TILE)`; fix-ups for animated tiles 156/234/236/136/130;
      `entityDefManager->lookup(tileNum)`; if a def exists create
      `Entity{info = spriteIndex+1, def}`; monsters (`eType == 2`) get the
      next `entityMonsters[80]` slot + `deactivate` + random hidden-spawn bit
      + cached attack sounds; `initspawn()`; `S_ENT = index++`; `linkEntity`
      onto its tile unless sprite is HIDDEN (`src/Game.cpp:422-458`).
      Def-less sprites carrying `0x800000` become generic defs 12/13
      (`src/Game.cpp:461-481`).
   4. Append 16 drop-item entities (`firstDropIndex`,
      `src/Game.cpp:487-496`); link the world entity onto tiles flagged
      `mapFlags & 1` (`src/Game.cpp:497-508`).
6. Reset HUD messages, stamp `playTime`/`curLevelTime`, clear events, free
   particles, `player->levelInit()` (`src/LoadingManager.cpp:659-667`).
7. **`game->loadWorldState()`** (`src/LoadingManager.cpp:670`; body
   `src/Game.cpp:1736-1911`): reads `BRIEFWORLD[mapID-1]` (or FULLWORLD for
   full loads), validates against `mapCompileDate`, and restores level
   time/secrets/fog/placed bombs/entity states/automap bits/script vars/tile
   event bits/lerp sprites/script threads/watch line; ends with
   `prepareMonsters()` which may resurrect corpses on replay
   (`src/Game.cpp:701-727`, `1900`).
8. **`spawnPlayer()`** (`src/LoadingManager.cpp:673`; body
   `src/Game.cpp:941-972`): coordinate source priority —
   `game->spawnParam` (encoded by a level exit or a save:
   `x | y<<5 | dir<<10`, `src/Game.cpp:958-963`) →
   `loadType == 3` fixed spawn (3,15 facing 6) → **map header**
   `n = mapSpawnIndex % 32`, `n2 = mapSpawnIndex / 32`,
   `dir = mapSpawnDir` (`src/Game.cpp:953-956`). Then
   `angle = dir << 7 & 0x3FF`, `view = dest = tile*64 + 32`,
   `z = getHeight(x,y) + 36`, `player->relink()`, `lastTurnTime = now`,
   invalidate (`src/Game.cpp:964-971`).
9. Fresh entries take a **brief snapshot** immediately:
   `saveLevelSnapshot()` writes BRIEFPLAYER + BRIEFWORLD[mapID-1]
   (`src/LoadingManager.cpp:675-677`, `src/Game.cpp:2119-2153`); continuing a
   save instead loads runtime HUD art (`src/LoadingManager.cpp:682-688`).
10. `player->selectWeapon(current)`; `scriptStateVars[12] = difficulty`;
    **`executeStaticFunc(0)`** = map-init script; `executeStaticFunc(1)` if
    the game was already completed (`src/LoadingManager.cpp:691-698`).
11. Fresh entries: `prevX/Y = destX/Y` and run the **entrance tile event**
    `executeTile(viewX>>6, viewY>>6, 4081, 1)`
    (`src/LoadingManager.cpp:700-706`).
12. `finishRotation(false)` (compute view step vectors),
    `endMonstersTurn()` (`monstersTurn = 0`), `uncoverAutomap()`
    (`src/LoadingManager.cpp:707-710`,
    `src/Game.cpp:2452-2456`, `src/AutomapController.cpp:35-63`).
13. Clear `isSaved/isLoaded/activeLoadType`; **`if (state == 0)
    setState(ST_PLAYING)`** (direct-load guard);
    `pauseGameTime = false`; resync clocks; **input lockout**
    `blockInputTime = gameTime + 200` (`src/LoadingManager.cpp:712-720`).
14. Pre-render one scene, set HUD/soft-key repaint flags, randomize monster
    idle timers (`src/LoadingManager.cpp:724-737`).
15. Finally `if (state == ST_LOADING && !loadType) setState(ST_PLAYING)`
    (`src/LoadingManager.cpp:739-741`).

### 3.4 Entering ANOTHER map (level doors 277/278, portals)

* Tiles 277/278 (`TILENUM_LEVEL_DOOR_LOCKED/UNLOCKED`, `src/Enums.h:840-841`)
  behave like ordinary doors (def `parm = 2`): using them just opens them and
  burns a turn (`src/PlayingInputHandler.cpp:445-453`). They are **not** the
  transition mechanism themselves.
* The transition is data-driven by **tile scripts**. An interact (`ACTION_FIRE`)
  first executes the faced tile's event with the facing-direction flag and,
  if a script ran, consumes the turn
  (`src/PlayingInputHandler.cpp:395-404`) — this happens *before* the door
  check.
* `EV_CHANGE_MAP` (= 11, `src/Enums.h:413`) handler
  (`src/ScriptThread.cpp:484-517`):
  ```cpp
  player->completedLevels |= 1 << (loadMapID - 1);
  game->spawnParam  = (((dir3 << 10) | targetTile10bit));   // args
  menuSystem->LEVEL_STATS_nextMap = arg & 0xF;
  game->snapAllMovers();
  if (arg & 0x80) render->startFade(1000,
        FADE_FLAG_FADEOUT | (showStats ? FADE_FLAG_SHOWSTATS : FADE_FLAG_CHANGEMAP));
  else if (showStats) canvas->saveState(51, 3, 194);
  else                canvas->loadMap(nextMap, false, false);
  canvas->changeMapStarted = true;                          // blocks input
  ```
  (`src/ScriptThread.cpp:489-515`; the latch blocks playing input at
  `src/PlayingInputHandler.cpp:29-31`).
* Fade completion is polled in `Render::fadeScene`
  (`src/Render.cpp:2548-2590`): `FADE_FLAG_CHANGEMAP` →
  `canvas->loadMap(LEVEL_STATS_nextMap, false, false)`
  (`src/Render.cpp:2555-2560`); `FADE_FLAG_SHOWSTATS` →
  `canvas->saveState(51, 3, 196)` → `ST_SAVING`
  (`src/Render.cpp:2561-2566`); `FADE_FLAG_EPILOGUE` → `ST_EPILOGUE`
  (`src/Render.cpp:2567-2571`).
* `ST_SAVING` executes the armed save and dispatches on `saveType` bits
  (`src/Canvas.cpp:853-903`): `0x8` = save with `spawnParam` coords then
  **`setState(ST_TRAVELMAP)`** (the level-exit path); `0x10` = save with
  `LEVEL_STATS_nextMap` as destination, then sound 1069 +
  `setMenu(MENU_LEVEL_STATS)`; `0x4` → main menu; `0x40` →
  `app->shutdown()`; `0x100` → final-quit menu; `0x80` → return to game.
  The stats screen continues via
  `canvas->loadMap(LEVEL_STATS_nextMap, true, false)` (the `true` skips the
  redundant re-save) (`src/MenuSystem.cpp:2996-3000`).
* **What crosses the boundary**: `spawnParam` (target tile + facing),
  `completedLevels`, and the armed save type; the destination map then goes
  through the standard §3.3 pipeline where `loadWorldState()` restores its
  own per-map snapshot and `spawnPlayer()` honors `spawnParam`.

--------------------------------------------------------------------------------
## 4. Worlds vs locations

There is **no explicit world/hub object** — "world" is implied by `mapID`
ranges checked ad hoc, in two different groupings:

* Narrative grouping (travel map): Moon = maps 1–3, Earth = 4–6,
  Hell = ≥ 7 (`onMoon/onEarth/inHell`, `src/TravelMapManager.cpp:446-456`);
  these predicates pick close-up art, travel-path images and locator
  coordinates (`src/TravelMapManager.cpp:50-109`, `265-301`).
* Media/sky grouping (alternates every 5 maps):
  `skyIndex = ((mapNameID - 1) / 5 % 2) * 2` selects sky tables
  `{16,17}` for maps 1–5 and `{18,19}` for maps 6–10
  (`src/LoadingManager.cpp:571-584`).
* Per-map theme/media is fully data-driven: each `mapNN.bin` embeds its media
  id list which registers exactly the palettes/texels that map needs
  (`src/Resource.h:19` names `map00.bin…map09.bin`;
  registration `src/LoadingManager.cpp:400-409`); display names come from
  `game->levelNames[mapID-1]` string ids (`src/Game.h:96`,
  `src/App.cpp:365,380`, `src/LoadingManager.cpp:76,331`) and the per-map
  text file is `4 + (mapID-1)` (`src/LoadingManager.cpp:310`).

So the rewrite should treat a "world" as: a `mapID` range predicate (for the
travel map UI) + the map-embedded media manifest + the sky-table pair.

--------------------------------------------------------------------------------
## 5. Turn system — `Game::advanceTurn`

`void Game::advanceTurn()` (`src/Game.cpp:1238-1281`), full responsibility
list in execution order:

1. `queueAdvanceTurn = false` (`src/Game.cpp:1240`); fatal error if monsters
   are still interpolating (`src/Game.cpp:1241-1243`).
2. Haste bookkeeping: if `statusEffects[2] > 0`, every *second* turn is
   skipped for monsters — otherwise the turn is "normal"
   (`src/Game.cpp:1245-1253`).
3. `pushedWall = false`; **`player->advanceTurn()`**: `moves++/totalMoves++`,
   status-effect ticking, buff regen, poison/radiation damage, `inCombat`
   expires after 4 idle turns, `turnTime` stamped
   (`src/Game.cpp:1254-1255`; `src/Player.cpp:51-92`).
4. **`updateBombs()`**: decrements placed-dynamite fuses; explodes at 0
   (`src/Game.cpp:1256`, `2555-2571`).
5. Arm the monster phase: `monstersTurn = 1` (normal) or `2` (slow/hasted
   round); `monstersUpdated = false`; `lastTurnTime = now`
   (`src/Game.cpp:1257-1264`).
6. On normal turns only: every entity gets `updateMonsterFX()` and
   `updateFacingEntity = true` (`src/Game.cpp:1265-1270`).
7. **Door auto-close sweep**: for `openDoors[0..5]`, `CanCloseDoor` →
   `performDoorEvent(1, door, 2 /*snap if offscreen*/)`
   (`src/Game.cpp:1271-1278`; occupancy rules in
   `docs/original-code/doors.md` §5).
8. **`executeStaticFunc(6)`** — the map's per-turn script hook
   (`src/Game.cpp:1279`; runner `src/Game.cpp:3342-3351`).
9. `canvas->startRotation(true)` — recompute view pitch toward the floor
   ahead (`src/Game.cpp:1280`; `src/MovementController.cpp:226-282`).

### When it fires exactly

* After a **move finishes** interpolating — `finishMovement` runs the
  destination-tile exit events + `touchTile`, then (when not in knockback,
  not in a goto thread, `ST_PLAYING`, `monstersTurn == 0`) calls
  `advanceTurn()` (`src/MovementController.cpp:160-196`, dispatched from
  `updateView` at `src/MovementController.cpp:510-512`). The automap variant
  snaps monsters/lerps instead of letting them animate
  (`src/MovementController.cpp:185-195`).
* After **use/interact**: faced-tile script ran (`src/PlayingInputHandler.cpp:398-403`)
  or a door opened (`src/PlayingInputHandler.cpp:451-452`).
* Explicit **pass-turn** action (`src/PlayingInputHandler.cpp:550-554`).
* At **combat end**, once the last attacker finished
  (`src/GameStateRunner.cpp:28-30`; `src/InputEventController.cpp:374-377`).
* **Deferred after dialogs**: `closeDialog` sets
  `game->queueAdvanceTurn = true` for turn-consuming dialog styles
  (`src/DialogSystem.cpp:530-532`); the next `ST_PLAYING` frame consumes it —
  `snapMonsters(true)` + `advanceTurn()` — but only once the help-message
  queue has drained (`src/Canvas.cpp:812-815`).
* Blocked entirely while `knockbackDist != 0` or `changeMapStarted`
  (`src/PlayingInputHandler.cpp:29-31`), and all input is dropped while a
  script with the input-blocking flag is live
  (`src/Canvas.cpp:777-779`; `src/Game.cpp:3447-3459`).

### Monster phase (the other half of a turn)

While `monstersTurn != 0`, `playingState` calls `updateMonsters()` once the
view is idle (see §6): `monsterAI()` lets each active monster `aiThink`
(haste-resistant ones only on round 2), `monsterLerp()` tracks walk
animations, and when nothing interpolates either `combatMonsters` starts an
attack (`performAttack`) or `endMonstersTurn()` closes the phase
(`monstersTurn = 0` + pitch refresh) (`src/GameStateRunner.cpp:181-183`;
`src/Game.cpp:874-939`, `2452-2475`).

--------------------------------------------------------------------------------
## 6. Per-frame tick while `ST_PLAYING` (ordered)

Preamble shared by all states (`src/Canvas.cpp:745-796`):

1. `CalcAccelerometerAngles()` (`src/Canvas.cpp:747`).
2. Clock rollover `lastTime/time` (`src/Canvas.cpp:750-751`).
3. `gameTime += dt` unless paused/menu (`src/Canvas.cpp:769-771`).
4. Vibration timeout (`src/Canvas.cpp:773-775`).
5. Drop queued input if a script blocks input (`src/Canvas.cpp:777-779`).
6. **`runInputEvents()`** — key actions execute here, *before* the state tick
   (moves/uses therefore start mid-frame) (`src/Canvas.cpp:781`).
7. Global sim (except the sounds-enable menu): trace-list reset,
   `UpdatePlayerVars` (mirror canvas↔game view vectors,
   `src/Game.cpp:857-872`), `gsprite_update(app->time)`
   (`src/Game.cpp:1464-1567`), `runScriptThreads(gameTime)` (resumes only in
   `ST_PLAYING`/`ST_CAMERA`) (`src/Canvas.cpp:791-796`).

`ST_PLAYING` branch (`src/Canvas.cpp:805-822`) → `GameStateRunner::playingState`
(`src/GameStateRunner.cpp:162-201`):

8. Held virtual-button auto-repeat → `handleEvent(buttonID)`
   (`src/Canvas.cpp:807-809`).
9. `game->updateAutomap = true` for this frame
   (`src/Canvas.cpp:811`; consumed by `uncoverAutomap`,
   `src/AutomapController.cpp:39-41`).
10. Deferred dialog turn: `numHelpMessages == 0 && queueAdvanceTurn` →
    `snapMonsters(true)` + `advanceTurn()` (`src/Canvas.cpp:812-815`).
11. Pushed-wall timer expiry raises the weapon
    (`src/GameStateRunner.cpp:166-169`).
12. **Death check**: `health <= 0` → `player->died()` → `setState(ST_DYING)`
    and abort the frame (`src/GameStateRunner.cpp:170-173`;
    `src/Player.cpp:711-733`); familiar fuel-out check likewise
    (`src/GameStateRunner.cpp:174-177`).
13. Center-message shifting marks the view stale
    (`src/GameStateRunner.cpp:178-180`).
14. **Monster turn step** — only if `knockbackDist == 0 &&
    activePropogators == 0 && animatingEffects == 0 && monstersTurn != 0 &&
    numHelpMessages == 0` → `updateMonsters()`
    (`src/GameStateRunner.cpp:181-183`).
15. **`updateLerpSprites()`** — advance door/mover/effect lerps; completed
    lerps run their owning script threads (`src/GameStateRunner.cpp:184`;
    `src/Game.cpp:2985-3021`).
16. **`updateView()`** (`src/MovementController.cpp:377-547`) — the heart of
    the frame:
    1. Screen-shake jitter (`src/MovementController.cpp:381-389`).
    2. Maya-camera override renders cinematics instead
       (`src/MovementController.cpp:391-396`).
    3. Knockback chaining (`src/MovementController.cpp:398-400`).
    4. Interpolate `viewX/viewY` by `animPos`, `viewZ` by `zStep`, `viewAngle`
       by `animAngle`, `viewPitch` by `pitchStep` (×1.5 under haste/knockback)
       (`src/MovementController.cpp:402-481`; step sizes from
       `setAnimFrames`, `src/MovementController.cpp:21-26`).
    5. Automap mode snaps view to dest (`src/MovementController.cpp:492-498`).
    6. Scripted `gotoTriggered` tile events
       (`src/MovementController.cpp:503-509`).
    7. Position arrived → **`finishMovement()`** (tile-exit events +
       `touchTile` + `advanceTurn`, §5)
       (`src/MovementController.cpp:510-512`).
    8. Rotation arrived → **`finishRotation(true)`** (recompute
       sin/cos/step vectors + facing tile events)
       (`src/MovementController.cpp:514-516`, `284-310`).
    9. **`renderScene(...)` with FOV 290** — the actual 3D render + weapon
       draw + portal pass (`src/MovementController.cpp:544-546`;
       `src/Canvas.cpp:1330-1356`).
17. Abort if a nested state-change happened (loading/saving)
    (`src/GameStateRunner.cpp:186-191`).
18. Set `REPAINT_PARTICLES`; HUD repaint flags `0x2B` + `hud->update()`
    (`src/GameStateRunner.cpp:192-197`; `hud->update` is tiny — weapon-select
    long-press, `src/Hud.cpp:1365-1378`); `dequeueHelpDialog()`
    (`src/GameStateRunner.cpp:198-200`).
19. Back in `run()`: `repaintFlags |= REPAINT_HUD`, `hud->repaintFlags |= 0x2F`
    (`src/Canvas.cpp:818-821`).
20. **Paint phase**: `backPaint` draws per flags — 3D blit/fade, particles,
    swipe area, HUD (`hud->draw`), state-specific screens, menus, loading bar,
    fade overlay (`src/Canvas.cpp:978-980`, `381-510`); then
    `stateChanged = false` and the ambient sys-sound timer
    (`src/Canvas.cpp:985-992`).

--------------------------------------------------------------------------------
## 7. Save / restore hooks (summary)

Three binary layers, all little-endian streams written under a profile dir
(`GetSaveFile`, names `FILE_NAME_CONFIG/FULLPLAYER/BRIEFPLAYER/FULLWORLD/
BRIEFWORLD`, `src/Game.h:28-32`):

* **Config** (`saveConfig`/`loadConfig`, `src/Game.cpp:1913-2057`): version
  tag 11, difficulty, audio options, control layout/keybinds, help bitmasks,
  `hasSeenIntro`, `numLevelLoads[10]`, play-time, grades; terminated by a
  `0xDEADBEEF` marker. Loaded once at boot (`src/App.cpp:120`).
* **Player file** (FULL/BRIEF): `savePlayerState` writes i16
  `loadMapID`, i16 `viewX/viewY/viewAngle/viewPitch`, i32
  `prevX/prevY/saveX/saveY/saveZ`, i16 `saveAngle/savePitch`, then the whole
  `Player::saveState` blob (`src/Game.cpp:2155-2170`). `loadPlayerState`
  restores them, derives `spawnParam` from the saved tile/angle
  (`viewX>>6 | viewY>>6<<5 | angle>>7<<10`), and sets `isLoaded = true`
  (`src/Game.cpp:2172-2189`, `2208-2213`).
* **World file** (FULL = single global, BRIEF = per map index):
  `saveWorldState` covers compile-date-guarded level metadata (time, secrets,
  fog), placed bombs, per-entity states (incl. door open/locked bits and
  monster HP), automap discovered bits, `scriptStateVars` (except 15),
  tile-event fired bits, the 16 lerp sprites, the 20 script threads, drop
  indices, notebook/quests, and the `watchLine` blocked-door reference
  (`src/Game.cpp:1576-1734` write; `src/Game.cpp:1757-1901` read).

Plug-in points into the state machine:

* Saving is **deferred**: `LoadingManager::saveState(saveType,…)` only arms
  `canvas->saveType` and enters `ST_SAVING`; the work happens in next frame's
  `run()` which then dispatches onward (travel map/stats menu/quit)
  (`src/LoadingManager.cpp:47-53`; `src/Canvas.cpp:853-903`).
* Loading mirrors it: `loadState(loadType)` enters `ST_LOADING`; the frame
  handler calls `game->loadState(loadType)` which itself chains
  `canvas->loadMap(saveStateMap, false, false)` — i.e. a continue flows
  through the same travel-map/loading pipeline
  (`src/Canvas.cpp:904-916`; `src/Game.cpp:2191-2223`).
* Observed `saveType` bit patterns: `3` fresh-entry full snapshot
  (`src/LoadingManager.cpp:687`), `43` level-exit brief save
  (`src/LoadingManager.cpp:73`), `51` end-of-level stats save
  (`src/ScriptThread.cpp:510`, `src/Render.cpp:2564`).
* `hasSavedState()` (config + BRIEFWORLD for any map + BRIEFPLAYER +
  FULLPLAYER) gates the CONTINUE menu item
  (`src/Game.cpp:2250-2269`; `src/MenuSystem.cpp:1258-1261`).

--------------------------------------------------------------------------------
## 8. Where HUD / DIALOG / COMBAT plug in later

**HUD.** The HUD is a passive overlay: it never owns state. It paints in
`backPaint` under `REPAINT_HUD` whenever `state != ST_MENU`
(`src/Canvas.cpp:414-417`), receives per-frame flag updates from whichever
state is active (`0x2B`/`0x2F` in playing/combat/dialog:
`src/Canvas.cpp:818-821`, `src/GameStateRunner.cpp:83-86,194-195`), exposes
one tiny `Hud::update()` hook called from the playing tick
(`src/Hud.cpp:1365-1378`), and owns messages/toasts (its message timers drive
`staleView`, `src/GameStateRunner.cpp:178-180`). A rewrite needs exactly:
a `hud.tick()` call site in the playing tick and a `hud.paint()` stage in the
overlay pass, plus the repaint-flag plumbing.

**DIALOG.** Dialogs are a genuine state (`ST_DIALOG`) entered from script
execution (`EV_DIALOG` → `canvas->startDialog` → `setState`, 
`src/ScriptThread.cpp:545-585`), with enter-hooks that collapse zoom, park
camera timing and clear soft keys/input (`src/Canvas.cpp:1084-1112`). While
open the world keeps breathing — lerps advance and the view re-renders behind
the scroll (`src/Canvas.cpp:920-926`) — input goes to
`dialogSystem.handleDialogEvents`, and `closeDialog` restores the previous
context (`oldState`, camera, combat) and may arm a deferred turn via
`queueAdvanceTurn` (`src/DialogSystem.cpp:520-557`). Port shape: modal state
with a resume token + the two flags `skipAdvanceTurn`/`queueAdvanceTurn`.

**COMBAT.** Combat is a state (`ST_COMBAT`) plus an `Combat` controller that
owns an attacker queue. Enter-hooks reset repaint/softkeys/`combatDone`
(`src/Canvas.cpp:1056-1061`); its tick (`combatState`) advances the attack
animation chain, waits for monster interpolation, fires `advanceTurn()` once
the player's action resolved, and returns to `ST_PLAYING` (or hands off to a
cinematic camera) (`src/GameStateRunner.cpp:20-87`); input is a small inline
branch (rotate around attacker, exit when done,
`src/InputEventController.cpp:364-386`). Port shape: state whose tick owns
`curAttacker`/`combatMonsters` progression and the single turn handoff at the
end.

--------------------------------------------------------------------------------
## Port checklist — minimal faithful skeleton (boot → map00 → die/exit)

Smallest set that preserves the original's contracts and leaves room for
dialog/combat:

1. **Clock**: accumulate `upTimeMs` from a clamped delta (≤ 125 ms; original
   throttles at ~15 ms, `src/Main.cpp:83-88`, `133-135`); derive `app->time`
   once per frame; keep a separate `gameTime` that freezes in menus
   (`src/Canvas.cpp:769-771`).
2. **`setState` semantics**: single entry point; zero `stateVars[9]`, record
   `oldState`, latch `stateChanged` until end of frame; old/new hook slots
   (`src/Canvas.cpp:1020-1217`, `987`).
3. **Frame order** (fixed): input pump/dispatch → globals
   (`UpdatePlayerVars`, `gsprite_update`, `runScriptThreads`) → state tick →
   overlay paint (`src/Canvas.cpp:781-796, 978-980`).
4. **States to implement first**: `ST_LOGO` (can be collapsed), `ST_MENU`,
   `ST_LOADING` (two-phase!), `ST_PLAYING`, `ST_DYING`; add `ST_TRAVELMAP`
   (skippable) and `ST_AUTOMAP` next; leave `ST_DIALOG`/`ST_COMBAT` stubs.
5. **Playing tick order**: input-repeat → deferred-turn consumer → death
   check → monster-turn stepper → `updateLerpSprites` → `updateView`
   (interp → `finishMovement`/`finishRotation` → render) → HUD flags → help
   dequeue (`src/Canvas.cpp:805-822`; `src/GameStateRunner.cpp:162-201`).
6. **Movement contract**: moves are commit-then-interpolate
   (`attemptMove` traces and sets `dest*`, `src/MovementController.cpp:312-359`);
   arriving triggers tile-exit scripts + `touchTile` + `advanceTurn`
   (`src/MovementController.cpp:160-196`) — see
   `docs/original-code/player-collision.md`.
7. **`advanceTurn` contract**: `player->advanceTurn` (stats/effects) +
   `updateBombs` + `monstersTurn = 1|2` + door auto-close sweep +
   `staticFunc(6)` + `startRotation` (`src/Game.cpp:1238-1281`).
8. **Load pipeline**: `loadMap` → travel-map gate → `ST_LOADING` two-frame
   `loadMedia` with the exact §3.3 ordering (unload → header/media/BSP →
   `loadMapEntities` creation order → `loadWorldState` → `spawnPlayer`
   (`spawnParam` > header spawn) → snapshot/static-func(0) → entrance tile
   event → `endMonstersTurn` → `uncoverAutomap` → `ST_PLAYING` +
   `blockInputTime ≈ 200 ms`) (`src/LoadingManager.cpp:604-744`).
9. **Map exit**: implement `EV_CHANGE_MAP` semantics — `spawnParam =
   dir<<10 | tile10`, `changeMapStarted` latch, fade flags
   `CHANGEMAP`/`SHOWSTATS`, fade-end dispatch to `loadMap`/`ST_SAVING`, and
   the `ST_SAVING` bit-table dispatch (`src/ScriptThread.cpp:484-517`;
   `src/Render.cpp:2548-2590`; `src/Canvas.cpp:853-903`).
10. **Death/exit**: health check in the playing tick → `ST_DYING` with the
    750 ms fall / 2750 ms fade timings → dead menu
    (`src/Canvas.h:40-42`; `src/GameStateRunner.cpp:256-281`); app exit paths
    already exist via `saveType & 0x40` and `AVK_CLR` handlers.
11. **Persistence minimum**: config file + BRIEFPLAYER/BRIEFWORLD pair with
    the `mapCompileDate` guard, armed via `ST_SAVING`/`ST_LOADING` rather
    than executed synchronously (`src/Game.cpp:2059-2223`).
