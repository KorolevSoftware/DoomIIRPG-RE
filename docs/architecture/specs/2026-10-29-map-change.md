# Map change: gateway, per-map world state, loading box, level stats, travel map (G1a-G5b)

Date: 2026-10-29. ADRs: [0034](../adr/0034-per-map-world-state-in-memory-legacy-layout.md)
(world state in memory, legacy layout), [0035](../adr/0035-map-transition-flow-loadingmanager.md)
(`LoadingManager`, resident old map, in-place `MapData` swap).

Facts used (read them, they are the ground truth for every formula below):
`docs/original-code/map-change.md` (MC), `docs/original-code/travel-map.md` (TM),
`docs/original-code/ui.md` §19 (UI19), `docs/original-code/fades.md` (FD),
`docs/original-code/game-flow.md` §3.3 (GF).

Group order (one group = one coder delegation; user test points marked **TEST**):

| Group | Content | Files |
|---|---|---|
| G1a | data moves, no behaviour change | `Game.h/.cpp`, `Combat.h/.cpp`, `CombatEntity.cpp`, `MenuSession.cpp`, `VendingMachine.cpp`, `MiniGameHost.h`, `Player.h/.cpp`, `ScriptVM.cpp` |
| G1b | bare transition + debug key **TEST** | new `core/LoadingManager.h/.cpp`; `GameContext.h/.cpp`, `GameStates.h`, `Main.cpp`, `ScriptVM.cpp`, `UiInputCollector.cpp`, `ui/Hud.cpp` (comment only) |
| G2a | world-state store + non-entity fields + restart via snapshot | new `domain/game/WorldState.h/.cpp`; `LoadingManager`, `GameContext.cpp`, `ScriptVM.h`, `Game.h/.cpp` |
| G2b | entity records **TEST** | new `domain/game/EntityState.h/.cpp`; `WorldState.cpp` |
| G3 | loading / saving box **TEST** | new `ui/LoadingView.h/.cpp`; `UiAssets.h/.cpp`, `GameContext.h/.cpp`, `LoadingManager` |
| G4a | grade + level counters in menus 1 and 32 | `Game.h/.cpp`, `Player.h/.cpp`, `MenuSession.cpp`, `io/MenuData.h` |
| G4b | level-stats screen + flow **TEST** | `MenuSession.h/.cpp`, `MenuModel.h`, `MenuView.cpp`, `UiAssets`, `LoadingManager`, `GameContext.cpp` |
| G5a | travel-map logic → per-frame model | new `core/TravelMapSession.h/.cpp`, new `ui/TravelMapModel.h` |
| G5b | travel-map view, assets, wiring, input **TEST** | new `ui/TravelMapView.h/.cpp`; `UiAssets`, `GameStates.h`, `GameContext.h/.cpp`, `LoadingManager` |

New files ⇒ re-run `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (GLOB) in
G1b, G2a, G2b, G3, G5a, G5b.

Threading: everything runs on the single game/GL loop thread
(`GameLoop` → `GameContext::tick`/`render`).

---

## 0. Current state of the rewrite (what is reused)

- **Map load = Loading tail.** `GameContext::tickLoading` phase 0 clears vars
  except 15, phase 1 resets the VM pool, stamps var 12, `Game::loadEntities`,
  `spawnPlayer`, static func 0, entrance tile event, `ST_PLAYING`
  (`new_src/core/GameContext.cpp:365-399`).
- **Peers are re-wired per load.** `Game::loadEntities` re-inits `db`, `trace`,
  `doors`, `monsters`, `ai`, `lerps`, `sprites`, `loot` and zeroes the per-map
  counters (`new_src/domain/game/Game.cpp:25-55`). `GameSprites::init` re-derives
  the 48 custom slots from `numMapSprites` (`new_src/domain/game/GameSprites.cpp:12-20`);
  `SpriteLerps::init` empties the pool (`new_src/domain/game/SpriteLerps.cpp:17-21`);
  `ScriptVM::resetPool` also drops the event cache (`new_src/domain/game/ScriptVM.cpp:32-43`).
- **Restart reload already swaps `MapData` in place** (move-assign over `g_map`,
  `new_src/core/Main.cpp:329-339`); its pre-load cleanup list is in
  `GameContext::restartLevel` (`new_src/core/GameContext.cpp:409-436`).
- **Render caches are per frame or per upload.** `World3D::uploadMapTextures`
  clears and rebuilds both texture maps (`new_src/render/World3D.cpp:160-219`);
  sprite→leaf classification is recomputed every frame
  (`new_src/render/World3D.cpp:1204-1230`), so legacy `relinkSprite` is a no-op
  here; `SceneRenderer` re-sizes its per-sprite arrays every frame
  (`new_src/render/SceneRenderer.cpp:128-139`).
- **All media are already resident.** `MediaLoader::finalize` loads every
  non-reference entry regardless of registration (`new_src/io/Media.cpp:69-145`).
- **Fade hold exists.** `Hud::drawFade` keeps a finished FADEOUT black
  (`new_src/ui/Hud.cpp:131-143`); legacy `loadMap` never clears it (FD §5).

---

## G1a — data moves (no behaviour change)

### G1a.1 `loadMapID` moves to `Game`

- `domain/game/Game.h`: add next to `spawnParam`
  (`new_src/domain/game/Game.h:131`):
  ```cpp
  int loadMapID = 1;          // canvas->loadMapID, 1..10 (src/LoadingManager.cpp:67-68)
  int totalLevelTime = 0;     // ms on this map before curLevelTime (src/Game.cpp:1585-1587)
  int64_t curLevelTime = 0;   // gameTime at load / last fold (src/LoadingManager.cpp:663)
  // snapAllMovers (src/Game.cpp:2500-2505): force-complete every mover before a map change.
  void snapAllMovers();
  ```
- Delete `Combat::loadMapID` (`new_src/domain/game/Combat.h:92`). Readers:
  - `new_src/domain/game/Combat.cpp:381` → `env_.game->loadMapID`
  - `new_src/domain/game/CombatEntity.cpp:154` → `c.env().game->loadMapID`
    (`Combat::env()`, `new_src/domain/game/Combat.h:161`)
  - `new_src/domain/game/Game.cpp:512` → `loadMapID`
  - `new_src/core/MenuSession.cpp:712` → `env_.game->loadMapID`
  - `new_src/minigames/vending/VendingMachine.cpp:57` → `e.game->loadMapID`
  - comment in `new_src/minigames/common/MiniGameHost.h:40`.
- `Game::snapAllMovers()` body:
  ```cpp
  lerps.snap(-1);           // snapLerpSprites(-1) (src/Game.cpp:2958-2978)
  doors.update(1 << 20);    // door anims are lerps in legacy; any dur (<= 750 ms) completes
  // snapGameSprites (src/Game.cpp:2502) not ported: gsprites are cleared at unload anyway.
  ```

### G1a.2 Player fields

`domain/game/Player.h` (next to `currentLevelDeaths`, `new_src/domain/game/Player.h:180`):
```cpp
int completedLevels = 0;   // bit mapID-1 = map left via EV_CHANGE_MAP (src/ScriptThread.cpp:489)
int highestMap = 1;        // src/Player.cpp:571; raised at load (src/LoadingManager.cpp:641-643)
int moves = 0;             // turns on this map (src/Player.cpp:53, :96)
// src/Player.cpp:94-105 subset: moves = 0, turnTime = now, health 0 -> 1.
// numNotebookIndexes / questComplete / questFailed / inCombat are not ported.
void levelInit();
```
- `Player::reset()` (`new_src/domain/game/Player.cpp:95`) zeroes
  `completedLevels`, `moves` and sets `highestMap = 1`.
- `Player::advanceTurn()` (`new_src/domain/game/Player.cpp:396`): `++moves;`
  first (src/Player.cpp:53); update the "Deferred: moves" comment (totalMoves
  stays deferred).
- `levelInit()`: `moves = 0; if (clock_) turnTime = *clock_; if (getHealth() == 0) ce.setStat(STAT_HEALTH, 1);`

### G1a.3 Per-map string type in the VM

Legacy loads string type `loadMapStringID = 4 + (mapID - 1)` per map
(MC §3.3 step 3, `src/LoadingManager.cpp:309-311`); the VM hardcodes
`kTextMap` (= type 4, map00 only). In `ScriptVM.cpp` add a file-local helper
```cpp
// loadMapStringID (src/LoadingManager.cpp:309-311): one string type per map.
static int mapTextType(const Game& g) { return kTextMap + g.loadMapID - 1; }
```
and use `mapTextType(*env_.game)` instead of `kTextMap` at
`new_src/domain/game/ScriptVM.cpp:365, 486, 948, 979, 983`.
(The `TextType` names 5..13 in `io/Localization.h` are really map string
types 1..9; they are unreferenced — leave them, do not use them.)

**Acceptance G1a:** builds; map00 plays exactly as before (loadMapID is 1 everywhere).

---

## G1b — bare transition (no screens) + debug key

### G1b.1 Data flow (final graph; later groups only fill edges)

```
EV_CHANGE_MAP (VM)  -> LoadingManager::changeMap(B, S)          (src/ScriptThread.cpp:484-517)
   showStats ? saveState(kSaveStats, 194) : loadMap(next, false, false)
loadMap(id, skipSave, newGame)                                   (src/LoadingManager.cpp:55-79)
   !skipSave && activeLoadType==0 && 1<=lastMapID<=10 ? saveState(kSaveExit, 196)
                                                       : enterTravelMap()
saveState(type, caption) -> StateId::Saving                     (src/LoadingManager.cpp:47-53)
Saving tick -> [G2a world save] -> stats ? [G4b menu 1 | G1b: loadMap(next, true, false)]
                                         : enterTravelMap()      (src/Canvas.cpp:853-903)
enterTravelMap -> vars[15] = direction -> [G5b TravelMap | G1b: Loading]  (src/TravelMapManager.cpp:23-43)
Loading phase 0 (vars cleared except 15) -> phase 1: unload, load file, entities, spawn, scripts
```

### G1b.2 `StateId::Saving` and debug actions (`core/GameStates.h`)

- `Saving = 16,  // legacy ST_SAVING (src/Canvas.h:68-93)`.
- `Action::DebugNextMap, DebugPrevMap` after `DebugVending`, comment
  `// DEBUG (removable, map change): N / Shift+N`.

### G1b.3 `core/LoadingManager.h`

```cpp
#ifndef NEW_CORE_LOADINGMANAGER_H
#define NEW_CORE_LOADINGMANAGER_H
#include <cstdint>
#include <functional>
#include "core/GameStates.h"

namespace newcore {
class EntityDefs; class Game; class MapData; class Player; class ScriptVM;

// Port of src/LoadingManager.cpp's map-change half (ADR 0035): the loadMap
// gateway, EV_CHANGE_MAP effects, the ST_SAVING tick, the travel direction
// (scriptStateVars[15]) and the per-load hooks GameContext::tickLoading calls.
class LoadingManager {
public:
	static constexpr int kSaveExit = 0x2B;   // 43: player|world|exit|brief (MC §3.1)
	static constexpr int kSaveStats = 0x33;  // 51: player|world|stats|brief
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
		StateHost* host = nullptr;
		const int64_t* gameTime = nullptr;
		std::function<void(int menuId)> openMenu;   // used from G4b
	};
	void init(const Env& env);

	void loadMap(int mapId, bool skipSave, bool newGame);  // src/LoadingManager.cpp:55-79
	void changeMap(int b, int s);                          // src/ScriptThread.cpp:484-517
	void debugChangeMap(int mapId);                        // DEBUG (removable)
	void tickSaving();                                     // src/Canvas.cpp:853-903 subset

	// Load-tail hooks, called by GameContext::tickLoading phase 1, in this order.
	void onEntitiesLoaded();   // src/LoadingManager.cpp:641-643, :659-670
	void onPlayerSpawned();    // src/LoadingManager.cpp:673-677 (snapshot from G2a)
	void onLoadFinished();     // src/LoadingManager.cpp:710-713
	bool isLoadedGame() const { return isLoaded_; }   // skips snapshot + entrance event

	int lastMapID() const { return lastMapID_; }
	int nextMap() const { return nextMap_; }
	int travelLoadId() const { return tmLoad_; }    // TM_LoadLevelId (G5)
	int travelLastId() const { return tmLast_; }    // TM_LastLevelId (G5)
	int captionId() const { return caption_; }      // loading-box caption (G3)

private:
	void saveState(int saveType, int captionId);   // src/LoadingManager.cpp:47-53
	void enterTravelMap();                          // TravelMapManager::init var-15 part

	Env env_;
	int lastMapID_ = 0;          // canvas->lastMapID; 0 at boot
	int nextMap_ = 0;            // menuSystem->LEVEL_STATS_nextMap
	int saveType_ = 0;
	int caption_ = -1;           // packed (type<<10)|index, -1 = none
	bool tmNewGame_ = false;     // travelMapManager.TM_NewGame
	bool isLoaded_ = false;      // game->isLoaded
	int activeLoadType_ = 0;     // game->activeLoadType (2 = restart, from G2a)
	short numLevelLoads_[10] = {};   // game->numLevelLoads (config file, src/Game.cpp:1946)
	int tmLoad_ = 1, tmLast_ = 0;
};
} // namespace newcore
#endif
```

### G1b.4 `core/LoadingManager.cpp` — exact bodies

`loadMap(mapId, skipSave, newGame)` (src/LoadingManager.cpp:55-79):
```
if (mapId >= 1 && mapId <= 10 && numLevelLoads_[mapId-1]++ == 0)
    player->currentLevelDeaths = 0;                       // :59-66
lastMapID_ = game->loadMapID; game->loadMapID = mapId;    // :67-68
// sound->soundStop(): log "[sound] stop"                 // :69
tmNewGame_ = newGame;                                     // :70-71
fprintf(stderr, "[map] loadMap %d (last %d)\n", ...);
if (!skipSave && activeLoadType_ == 0 && lastMapID_ >= 1 && lastMapID_ <= 10)
    saveState(kSaveExit, makeId(kTextIngame2, 196));      // :72-73 "Saving Game..."
else { caption_ = makeId(kTextIngame2, levelNames[mapId-1]); enterTravelMap(); }  // :75-77
```
(`levelNames` comes from `Tables`; add `const Tables* tables` to `Env` if you
need it, or store only the caption in G3 — G1b may leave `caption_` unset.)

`changeMap(b, s)` (src/ScriptThread.cpp:484-517, MC §2):
```
player->completedLevels |= 1 << (game->loadMapID - 1);           // :489
game->spawnParam = (((b >> 4) & 7) << 10) | (s & 0x3FF);          // :490
nextMap_ = b & 0xF;                                                // :491
const bool showStats = nextMap_ >= game->loadMapID;                // :488,492-494
game->snapAllMovers();                                             // :495
if (b & 0x80) fprintf(stderr, "[map] CHANGE_MAP fade bit ignored (no shipping site sets it)\n");  // :496-507, deviation MD1
if (showStats) saveState(kSaveStats, makeId(kTextIngame2, 194));   // :509-510 (194 is the data quirk, UI19.1)
else           loadMap(nextMap_, false, false);                    // :512-513
// changeMapStarted (:515): no flag needed — the state is no longer Playing,
// so GameContext::tick drops gameplay input (new_src/core/GameContext.cpp:265).
```

`debugChangeMap(mapId)`: `changeMap(mapId & 0xF, 0); game->spawnParam = -1;`
(header spawn; legacy debug sets `spawnParam = 0`, its "none" value,
`src/MenuSystem.cpp:3130-3133`; the rewrite's "none" is -1,
`new_src/domain/game/Game.h:131`). Going through `changeMap` on purpose: it
exercises the real exit path, including stats on forward moves (G4b). It also
sets the `completedLevels` bit, which the legacy debug menu does not — fine for
a debug key.

`saveState(type, caption)`: `saveType_ = type; caption_ = caption; host->requestState(StateId::Saving);`

`tickSaving()` (src/Canvas.cpp:853-903):
```
const int type = saveType_; saveType_ = 0;
// G2a: world save goes here (see G2a.4)
if (type & 0x10) loadMap(nextMap_, true, false);   // G1b stand-in for MENU_LEVEL_STATS "continue" (src/MenuSystem.cpp:2996-3000); G4b replaces
else if (type & 0x08) enterTravelMap();            // :885-886
```
Skipped on purpose (deviation MD2): the leave-time `BRIEFPLAYER` write (it is
overwritten by the entry snapshot before anything can read it), HUD message
(0,38) "Game Saved" (cleared by the load before it is ever drawn,
`src/LoadingManager.cpp:659`), `clearEvents`.

`enterTravelMap()` (src/TravelMapManager.cpp:23-43, TM §2):
```
tmLoad_ = clamp(game->loadMapID, 1, 9);
// getRecentLoadType() == 1 only after a FULL save (save system, later): always 2 today.
tmLast_ = clamp(lastMapID_, 0, 9);
if (tmLast_ == 0 && tmLoad_ > 1) tmLast_ = tmLoad_ - 1;
vm->vars[15] = tmLoad_ > tmLast_ ? 0 : (tmLoad_ == tmLast_ ? 1 : 2);
host->requestState(StateId::Loading);          // G5b: TravelMap instead
```
(Faithful order: legacy writes var 15 at travel-map init, which every load
passes through, before `loadMedia` clears all other vars.)

`onEntitiesLoaded()`:
```
if (game->loadMapID <= 10 && game->loadMapID > player->highestMap) player->highestMap = game->loadMapID;  // :641-643
game->curLevelTime = *gameTime;              // playTime = curLevelTime = gameTime (:663)
player->levelInit();                         // :667
// G2a: loadWorldState (:670)
```
`onPlayerSpawned()`: empty in G1b (G2a: `saveLevelSnapshot`).
`onLoadFinished()`: `isLoaded_ = false; activeLoadType_ = 0;` (src/LoadingManager.cpp:710-713).

### G1b.5 `GameContext` changes

- Member `LoadingManager loading_;` + `LoadingManager& loading() { return loading_; }`.
  `init()` wires `Env{ game, player, vm, map, defs, this, &gameTime, openMenu }`
  where `openMenu = [this](int id) { nextMenuId_ = id; setState(StateId::Menu); }`
  (same lambda as `actEnv.openMenu`, `new_src/core/GameContext.cpp:94`).
- `Init::reloadMap` → `std::function<bool(int mapId)> loadMapFile;`
  (comment: "Reads map%02d.bin (mapId-1) into *map in place, re-uploads world
  textures, sky and map strings. False = read/parse failed.").
- New private `void unloadMap_();` = the pre-load cleanup now inside
  `restartLevel` (`new_src/core/GameContext.cpp:416-433`) minus the player
  lines: `actions_.gotoThread = nullptr; actions_.gotoTriggered = false;
  cinematic_.reset();` combat pointer/missile/damage resets,
  `sprites.clear(kFlagInUse)`, `visibility = {}`, `hud->clearMessages()`,
  `dialogs->clearHelpQueue()`, plus `sys_.player->facingEntity = nullptr`
  (entity array is rebuilt) and `sys_.player->viewPitch = sys_.player->viewRoll = 0`.
  It does **not** touch the fade (legacy keeps it across a map change, FD §2/§5;
  this is what carries map00's IP 9671 black into map02).
  Comment: `// Game::unloadMapData + Render::unloadMap analog (src/LoadingManager.cpp:619-625, src/Game.cpp:642-685)`.
- `tickLoading()` phase 1 becomes (new lines marked +):
  ```
  + unloadMap_();
  + if (!sys_.loadMapFile || !sys_.loadMapFile(sys_.game->loadMapID))
  +     fprintf(stderr, "[load] map %d FAILED to load\n", sys_.game->loadMapID);
    vm->resetPool(); vm->vars[12] = 1; game->loadEntities(...);
  + loading_.onEntitiesLoaded();
    actions_.spawnPlayer();
  + loading_.onPlayerSpawned();
    lastTurnTime_ = upTimeMs; staticFunc(0);
  ~ if (!loading_.isLoadedGame()) vm->executeTile(... 4081, true);   // :700-706 "if not loaded"
    finishRotation; endMonstersTurn; setState(Playing) if still Loading;
    pauseGameTime = false; blockInputTime = gameTime + 200;
  + loading_.onLoadFinished();
  ```
- `restartLevel()` (G1b only): delete the cleanup lines moved into `unloadMap_`
  and the `sys_.reloadMap()` call (phase 1 now always reloads the file); keep
  `p.reset()`, `currentLevelDeaths = 0`, `viewZ = destZ`,
  `hud->clearScreenEffects()`, `setState(Loading)`. Known G1b limit: on maps
  other than map00 restart gives new-game stats (fixed by G2a).
- `tick()` state switch: `case StateId::Saving: loading_.tickSaving(); break;`
  (nothing else ticks: no gameTime, no scripts, no lerps — Saving is not in the
  lists at `new_src/core/GameContext.cpp:213`, `:288`, `:304`).
- Debug actions, next to `DebugVending` (`new_src/core/GameContext.cpp:266-271`),
  Playing only:
  ```cpp
  if (a == Action::DebugNextMap || a == Action::DebugPrevMap) {
      // DEBUG (removable, map change): N = next mapID, Shift+N = previous; header spawn.
      const int id = sys_.game->loadMapID;
      loading_.debugChangeMap(a == Action::DebugNextMap ? id % 10 + 1 : (id + 8) % 10 + 1);
      continue;
  }
  ```

### G1b.6 `ScriptVM.cpp` `EV_CHANGE_MAP`

Replace the parse-only body (`new_src/domain/game/ScriptVM.cpp:1034-1045`):
```cpp
int b = readUByte(t);
int s = readUShort(t);
std::fprintf(stderr, "[script] CHANGE_MAP map=%d dir=%d tile=%d fade=%d\n",
	b & 0xF, (b >> 4) & 7, s & 0x3FF, (b & 0x80) != 0);
env_.ctx->loading().changeMap(b, s);
break;   // the thread keeps running on the still-resident old map (ADR 0035 §2)
```

### G1b.7 `Main.cpp`

Replace the `reloadMap` lambda (`new_src/core/Main.cpp:329-339`) with:
```cpp
// Map file + per-map resources for mapId 1..10 (ADR 0035): parse into a fresh
// MapData and move it over g_map so every borrowed &g_map stays valid.
const auto loadMapFile = [&app, &g_map, &g_media, &world, &tables, &loc](int mapId) {
	char name[16];
	std::snprintf(name, sizeof name, "map%02d.bin", mapId - 1);   // RES_MAP_FILE_ARRAY[id-1] (src/Resource.h:19)
	std::vector<uint8_t> bytes;
	MapData fresh;
	if (!app.readResource(name, bytes) || !MapParser::parse(bytes, fresh)) return false;
	g_map = std::move(fresh);
	world.uploadMapTextures(g_map, g_media);
	// skyIndex = ((mapNameID-1)/5%2)*2 -> tables 16/17 (A) or 18/19 (B) (GF §3.3)
	const bool skyA = ((mapId - 1) / 5) % 2 == 0;
	world.uploadSky(skyA ? tables.skyTexelA : tables.skyTexelB,
	                skyA ? tables.skyPaletteA : tables.skyPaletteB);
	const int strType = kTextMap + mapId - 1;                       // src/LoadingManager.cpp:309-311
	if (!loc.isLoaded(strType)) loc.loadTextType(0, strType);
	return true;
};
```
Pass it in `ctx.init({... , loadMapFile})`. After `ctx.init` and the
minigame assets: `ctx.loading().loadMap(1, false, true);  // new game, startupMap 1 (src/IntroSequenceManager.cpp:135)`.
In the same group change the `Game::loadMapID` default from 1 (G1a) to **0**
(legacy `canvas->loadMapID` before the first load): the boot call must see
`lastMapID = 0`, otherwise the gateway takes the save branch for a map that
was never loaded. Every reader runs after this first `loadMap`.
Verify `Texture::uploadIndexed` on an already-valid `Texture` (sky) releases
the old handle (`new_src/render/api/Texture.h:29`); if it does not, release
first. Keep the boot-time map00 parse/upload block as is (smoke logs).

### G1b.8 Input + misc

- `UiInputCollector::keyAction` (`new_src/core/UiInputCollector.cpp:105`):
  `case SDL_SCANCODE_N: a = (ev.key.keysym.mod & KMOD_SHIFT) ? Action::DebugPrevMap : Action::DebugNextMap; break; // DEBUG (removable, map change)`.
- `new_src/ui/Hud.cpp:128-129`: drop the `TODO(CHANGE_MAP)` (now reachable).
- `docs/status.md` controls table gains N / Shift+N (orchestrator).

**Acceptance G1b (TEST):** on map00 press **N** → after one or two frames map01
(Cichus) appears, player at its header spawn, health/armor/ammo/inventory/XP
unchanged, HUD normal; map01's init script runs (stderr `[load] staticFunc(0)`,
`[map] loadMap 2 (last 1)`); its arrival cinematic plays (var 15 = 0, TM §8).
**Shift+N** → back to map00 (var 15 = 2). Playing map00 to its real exit
(camera 3, `CHANGE_MAP map=2`) now lands in map01 under a held black screen
until map01's script fades in. No crash on N repeated through all ten ids
(mapID 10 = test treadmill room). Revisits are fresh copies in G1b (state
comes in G2). Vending on map01 uses machine ids 3/4 (`(loadMapID-1)*2+b1`).

---

## G2a — `WorldState`: store, non-entity fields, entry snapshot, restart

### G2a.1 `domain/game/WorldState.h`

```cpp
// Per-map world state, ADR 0034: one byte blob per mapID in the legacy brief
// saveWorldState layout, plus the level-entry player snapshot.
class WorldState {
public:
	static constexpr int kNumMaps = 10;
	struct Env {
		Game* game = nullptr; Player* player = nullptr; ScriptVM* vm = nullptr;
		MapData* map = nullptr; const EntityDefs* defs = nullptr;
		const int64_t* gameTime = nullptr;
	};
	void init(const Env& env);

	// saveWorldState(OS, true) (src/Game.cpp:1576-1734) of the LOADED map into slot mapId-1.
	void saveLevel(int mapId);
	// loadWorldState (src/Game.cpp:1736-1909). False = empty slot / mismatch (fresh map).
	bool restoreLevel(int mapId);
	// saveLevelSnapshot's player half (src/Game.cpp:2119-2141) / loadState(2)'s read.
	// shortcut: value copy of Player, the save system replaces it with savePlayerState.
	void savePlayer();
	bool restorePlayer();          // false = no snapshot
	void clear();                  // removeState(true) (src/Game.cpp:2271-2318), new game
	// For the future save system (SB_<mapId>, src/Game.cpp:3706-3715).
	const std::vector<uint8_t>& levelBlob(int mapId) const { return levels_[mapId - 1]; }
private:
	Env env_;
	std::vector<uint8_t> levels_[kNumMaps];
	Player player_;
	bool hasPlayer_ = false;
};
```

`restorePlayer()`: keep the config-file fields (ADR 0034 §5) —
```
const int deaths = player->totalDeaths;   // + currentGrades/bestGrades once G4a adds them
*player = player_;
player->totalDeaths = deaths;
player->currentLevelDeaths = 0;           // src/Game.cpp:2200-2202
player->facingEntity = nullptr;           // entity array is rebuilt by the load
```

### G2a.2 Record layout (write = `saveLevel`, read = `restoreLevel`)

Writer: `DataWriter w;` … `levels_[mapId-1] = w.buffer();`. Reader:
`DataReader r(levels_[mapId-1])`; empty slot → `return false`.
Order and encoding exactly as `src/Game.cpp:1583-1733` / `:1760-1900`:

| # | Field | Write (rewrite source) | Read |
|---|---|---|---|
| 1 | int compileDate | `map->compileDate` | `!=` current → `if (stored) game->spawnParam = -1; game->totalLevelTime = 0; return false` (`:1902-1909`) |
| 2 | int totalLevelTime | first `game->totalLevelTime += gameTime - curLevelTime; game->curLevelTime = gameTime` (`:1585-1587`) | assign |
| 3 | byte mapSecretsFound | `game->mapSecretsFound` | assign |
| 4 | 6 × int fog | 0 (no fog state; `new_src/core/Main.cpp:298-301`) | discard |
| 5 | byte pick-up weapon tile, **only if** `player->ce.weapon == 14` | `player->activeWeaponDef ? tileIndex : 0` | discard if `ce.weapon == 14` (setPickUpWeapon not ported) |
| 6 | 4 × short placedBombs | 0 (brief writes 0, `:1599-1606`) | discard |
| 7 | entity states | G2a: `w.writeShort(0)`; G2b: `EntityState::save` | G2a: read count, must be 0; G2b: `EntityState::load` |
| 8 | line bits | bit `j%8` of byte `j/8` = `map->lineFlags[j>>1] & (8 << ((j&1)<<2))`, flush every 8, trailing partial byte (`:1609-1624`) | OR the bit back; never clears (`:1784-1795`) |
| 9 | byte deathFunc count + pairs | 0 (SETDEATHFUNC not ported, `new_src/domain/game/ScriptVM.cpp:1054-1058`) | read n, consume 2·n shorts |
| 10 | sprite bits | for sprite k, 4 per byte: bit 2·(k%4) = hidden (`mapSpriteInfo[k] & 0x10000`, or the brief corpse rule below), bit 2·(k%4)+1 = `mapSpriteInfo[k] & 0x200000`; loop `while (k < numMapSprites)` over whole bytes, so the last byte may cover slots ≥ numMapSprites — read them like legacy (`:1637-1662`) | set/clear `0x10000` and `0x200000` per bit, same loop (`:1800-1820`) |
| 11 | tile bits | bit i%8 = `map->mapFlags[i] & 0x80`, i = 0..1023 | OR `0x80` |
| 12 | 127 × short vars | `vm->vars[i]`, i ≠ 15 | assign, i ≠ 15 |
| 13 | tile-event disabled bits | per event i: bit `i%32` = `(tileEvents[2i+1] & 0x80000) >> 19`; write int when `i%32 == 31`; after the loop write once more if last `i%32 != 31` (0 events → one int 0) (`:1679-1692`) | first int read up front; per event set/clear bit 19; read next int when `i%32 == 31 && i < n-1` (`:1838-1847`) |
| 14 | byte numLerpSprites | 0 (pool empty at both save points, ADR 0034) | read; if nonzero log + `return false` |
| 15 | byte numScriptThreads + 20 thread records | `vm->numThreads()`; per pool slot: int `unpauseTime` (relative: `-1`/`0` verbatim else `unpauseTime - gameTime`), byte state, int IP, int FP, FP × int stack (`src/ScriptThread.cpp:30-43`) | read all, change nothing: legacy never restores `stackPtr`, so all end `inuse = false` (`src/ScriptThread.cpp:45-57`, `src/Game.cpp:1858-1868`) |
| 16 | int dropIndex | 0 (no drops, `new_src/domain/game/ScriptVM.cpp:1369-1373`) | discard |
| 17 | int player moves | `player->moves` | assign |
| 18 | byte, byte, byte, 8 × (short, short) notebook/quest | zeros | discard |
| 19 | byte mapFlagsBitmask | `map->flagsBitmask` | assign |
| 20 | byte, byte mastermind/caldex hacks | 0 | discard |
| 21 | short watchLine | -1 | discard |
| 22 | int VIOS mallocs | 0 | discard |
| 23 | short lootFound, short destroyedObj | `game->lootFound`, `game->destroyedObj` | assign |

Brief corpse rule for field 10 (`:1643-1650`): the sprite's entity
(`game->db.findEntityBySprite(k)`) has `def->eType == ET_CORPSE`,
`def->eSubType != 17` (CORPSE_SKELETON) and `!(monster->flags & 0x80)`.
Legacy dereferences `monster` unguarded; apply the rule only when
`monster != nullptr` (never null for corpses in shipping data).

After a successful read: `prepareMonsters()` (`src/Game.cpp:701-727`,
resurrection of up to 8 corpses on a completed, cleared map) is **deferred**
(deviation MD3, `Entity::resurrect` not ported) — log once
`[world] prepareMonsters deferred`. HUD health-anim fields (`:1897-1899`) have
no rewrite counterpart.

`ScriptVM.h` gains `int numThreads() const { return numThreads_; }`.
`saveLevel`/`restoreLevel` skip `mapId > 10` (`src/Game.cpp:1741-1743`).
Add a self-check (debug, once per save) — `restoreLevel` must consume exactly
`buffer.size()` bytes; log `[world] size mismatch` otherwise.

### G2a.3 `Game` additions

None beyond G1a (fields already there).

### G2a.4 `LoadingManager` integration

- Member `WorldState world_;` + `WorldState& world()`. `init` also calls
  `world_.init({game, player, vm, map, defs, gameTime})`.
- `tickSaving()` first line after reading `type`:
  `world_.saveLevel((type & 0x10) ? game->loadMapID : lastMapID_);`
  (0x10 saves the current map, 0x08 the one just left — `loadMap` already
  advanced `loadMapID`; MC §3.1).
- `onEntitiesLoaded()` tail: `if (!world_.restoreLevel(game->loadMapID)) game->totalLevelTime = 0;` (`src/Game.cpp:1751-1753`).
- `onPlayerSpawned()`: `if (!isLoaded_) { world_.savePlayer(); world_.saveLevel(game->loadMapID); }`
  (`saveLevelSnapshot`, `src/LoadingManager.cpp:675-677`; runs before static
  func 0, so the snapshot is the pre-script entry state). Note `saveLevel`
  folds the level time; at entry `gameTime - curLevelTime == 0`.
- New public `bool restartLevel();` (`loadState(2)`, `src/Game.cpp:2191-2215`):
  ```
  if (!world_.restorePlayer()) return false;
  const Player& p = *player;
  if (p.viewX != 0 && p.viewY != 0)
      game->spawnParam = ((p.viewX >> 6) & 0x1F) | (((p.viewY >> 6) & 0x1F) << 5)
                       | (((p.viewAngle & 0x3FF) >> 7) << 10);        // :2205-2208
  activeLoadType_ = 2; isLoaded_ = true;                              // :2209-2212
  loadMap(game->loadMapID, false, false);    // no save (activeLoadType != 0) -> var15 = 1 -> Loading
  return true;
  ```

### G2a.5 `GameContext::restartLevel`

```
p.viewPitch = 0; p.viewRoll = 0;                  // death tilt (tickDying)
sys_.hud->clearScreenEffects();                   // endFade (src/Canvas.cpp:1380)
if (loading_.restartLevel()) return;
// No snapshot (cannot happen after the first load): G1b fallback.
p.reset(); p.currentLevelDeaths = 0; p.viewZ = p.destZ; setState(StateId::Loading);
```
Update the deviation-D6 comment: restart is now `loadState(2)` faithful except
the player snapshot being a value copy (ADR 0034).

**Acceptance G2a:** builds; Shift+N/N round trip keeps script-var-driven state
(e.g. a door a script opened, a disabled tile event does not fire again,
secrets counter), and hidden sprites stay hidden; entities are still fresh
(dead monsters may stand again — fixed in G2b). Restart Level on map01 returns
the player to the map01 entry state (inventory/health as when entering), not
new-game stats; stderr shows `[map] loadMap 2 (last 2)` and no entrance event.

---

## G2b — entity records (`domain/game/EntityState.h/.cpp`)

```cpp
namespace EntityState {
struct Env { Game* game = nullptr; MapData* map = nullptr; const EntityDefs* defs = nullptr; };
void save(const Env& env, DataWriter& w);   // Game::saveEntityStates(OS, true) (src/Game.cpp:3379-3395)
bool load(const Env& env, DataReader& r);   // Game::loadEntityStates (src/Game.cpp:3371-3377)
}
```
Wire into field 7 of G2a.2. Port verbatim, brief variant (`b == true`), these
legacy functions as file-local helpers on rewrite `Entity`
(`new_src/domain/game/Entity.h`; its `info` bits are the legacy ones):

| Legacy | Lines | Rewrite notes |
|---|---|---|
| `saveEntityStates` | `src/Game.cpp:3379-3395` | iterate `game->db.entities()`; per record `writeInt(0xCAFEBABE)` marker (`src/Resource.cpp:92-102`), `writeInt(handle)`, `saveState` |
| `loadEntityStates` | `src/Game.cpp:3371-3377` | `readShort` count; per record `readInt` marker (ignored), `readInt` handle, `entities[handle & 0xFFFF].loadState`; bad index → `return false` |
| `getSaveHandle(b)` | `src/Entity.cpp:1787-1857` | |
| `isBinaryEntity` | `src/Entity.cpp:1412-1482` | note: ET_NONOBSTRUCTING_SPRITEWALL with tile ≠ 168 returns *true* with b = false — keep |
| `saveState(n)` | `src/Entity.cpp:1497-1572` | brief monsters take the short record (x>>3, y>>3, frame byte, flags) |
| `loadState(n)` | `src/Entity.cpp:1574-1786` | |
| `restoreBinaryState(n)` | `src/Entity.cpp:1859-1934` | |

Rewrite substitutions:
- `isDroppedEntity()` → always false (no drop slots; `EV_DROPITEM` not ported,
  `new_src/domain/game/ScriptVM.cpp:1369-1373`). Drop every dropped-only branch
  with a one-line comment.
- `isNamedEntity()` → always false (`Entity::name` not ported, `EV_NAMEENTITY`
  skipped, `new_src/domain/game/ScriptVM.cpp:1047-1052`).
- `linkEntity/unlinkEntity` → `game->db`. `relinkSprite` → nothing
  (per-frame classification, §0). `turnEntityIntoWaterSpout` → `game->`.
  `performDoorEvent(n, e, 0)` → `game->doors.performDoorEvent(n, e, 0)`
  (same signature, `new_src/domain/game/DoorSystem.h:60`). `setLineLocked` →
  `game->setLineLocked`. `activate(e,false,false,false,true)` / `deactivate` →
  `game->monsters`. `entityDefManager->find(9, sub, parm)` →
  `defs->find(Enums::ET_CORPSE, sub, parm)`. `lootSet = nullptr` → `hasLootSet = false`.
  `++destroyedObj` → `++game->destroyedObj`. `Error(ERR_CLEANCORPSE)` → log.
- **S_Z conversion.** The rewrite stores sprite Z raw-relative; legacy stores
  baked Z (`new_src/domain/game/SpriteLerps.h` `spriteZBias` comment). Every
  legacy write of a *computed* height
  (`S_Z = getHeight(x, y) + 32`, `src/Entity.cpp:1679, 1689-1691`) becomes
  `S_Z = (map->heightAt(x, y) + 32) - game->lerps.spriteZBias(sprite, x, y)`.
  Raw `S_Z` save/restore (`:1569-1571` / `:1767-1769`) copies the stored value
  untouched (same representation both ways).
- Sprite field offsets: `mapSprites[sprite + F * numSprites]`, F = 0 X, 1 Y,
  2 Z, 8 SCALEFACTOR (`new_src/domain/world/MapData.h:72-75`).
- Constants: `INTERACT_PICKUP 3`, `INTERACT_BARRICADE 1`, `INTERACT_CRATE 2`,
  `DOOR_LOCKED 1`, `CORPSE_SKELETON 17`, `DECOR_STATUE 3`,
  `DECOR_WATER_SPOUT 7`, `TILENUM_NONOBSTRUCTING_SPRITEWALL2 168`,
  `TILENUM_DUMMY_START/END 140/143` (`src/Enums.h:46-781`) — reuse
  `domain/game/Enums.h` names where they exist, add the missing ones there.

Entity indices are stable across save/load: the same map file and defs always
spawn the same array (`Game::loadEntities` is deterministic), so legacy's
index-keyed records work unchanged.

**Acceptance G2b (TEST):** on map01 kill a monster, open a crate, pick up an
item, open a door; Shift+N to map00, N back to map01 → an ordinary corpse is
gone on return (brief handle: hidden, no payload — src/Entity.cpp:1848-1850, src/Game.cpp:1643-1650;
only NOTRACK and skeleton corpses keep their state; corrected 2026-10-29 after G2b review), crate open, item gone, door state as left,
no monster stands up. Restart Level on map01 restores the entry state (monster
alive again, crate closed). map00 restart still works (now without the
entrance event, ADR 0034 consequences).

---

## G3 — loading / saving box (UI19.5)

### Assets
`UiAssets` gains `Texture fabricBG; // FabricBG.bmp 32x32` and
`Texture loadingFire; // loadingFire.bmp 26x9` (loaded like the other sheets).

### `ui/LoadingView.h/.cpp`
```cpp
struct LoadingModel {
	const Text* caption = nullptr;     // dehyphenated; never null when drawn
	const Text* subCaption = nullptr;  // (0,58) "Just a moment!"
	int pacifierX = 174;
};
void drawLoading(Ui& ui, const LoadingModel& m);
```
Draw order (`src/LoadingScreenController.cpp:38-100`): black fill
(0,0,480,320) (eraseRgn); `fabricBG` tiled into rect (165,131,150,58), clipped
to it; white 1-px frame (165,131,150,58) (inherits the known `drawRect` 1-px
deviation, `docs/status.md`); caption at (240,138) HCENTER|TOP; subCaption at
(240,154) HCENTER|TOP; bar: black fill (173,171,134,12) + white frame same
rect; `loadingFire` region (0,0,w,9), `w = min(26, 307 - pacifierX)`, at
(pacifierX,173), vertically flipped when `(pacifierX/10) % 6 < 3`
(legacy rotateMode 6 = mirror+180° = vertical flip, `src/Image.cpp:139-142`;
use the `Graphics2D` vertical-flip mode 8, `new_src/render/Graphics2D.h:50-51`;
add a thin `Ui` pass-through for the rotate argument if `Ui::imageRegion`
lacks one).

### Producer (`GameContext`)
- Caption: `Saving` → `loading_.captionId()` (196 "Saving Game…" for exits,
  194 for the no-fade stats path); `Loading` → `(0,41)` "Loading…" (the travel
  map's caption, `src/TravelMapManager.cpp:519`, which every load passes);
  missing → `(0,57)` "Processing…" (`src/LoadingScreenController.cpp:30-32`).
  Resolve with `Localization::get(typeOf(id), indexOf(id))` + `dehyphenate()`.
- Pacifier (`updateLoadingBar`, `src/LoadingScreenController.cpp:17-36`):
  member `int pacifierX_`, set to `SCR_CX - 66 = 174` on entering Saving or
  Loading (`src/Canvas.cpp:1183-1187`); in `render()`, at most once per 150 ms
  of `upTimeMs`: `pacifierX_ += 10; if (pacifierX_ >= 302 || pacifierX_ < 174) pacifierX_ = 174;`.
- Draw `drawLoading` in `render()` when `state_ == Saving || state_ == Loading`,
  after the world/HUD passes and before the menu block (it paints the whole
  screen black first, so the world under it does not show).
- **One presented frame before the work** (legacy draws the box, flushes, then
  does the work on the next call, `src/LoadingManager.cpp:610-616`,
  `src/Canvas.cpp:904-910`): member `uint32_t framesRendered_` incremented at
  the end of `render()`. `tickSaving` is invoked only when
  `framesRendered_ != frameAtStateEntry_`; Loading phase 0 → 1 likewise
  (record `frameAtStateEntry_` in `enterState_` for Saving/Loading and again
  when phase 0 finishes). Without this gate several 15 ms quanta can run inside
  one rendered frame and the box would never be seen.

**Acceptance G3 (TEST):** at every transition (N, Shift+N, restart) the screen
shows, for at least one frame each, a black screen with the fabric box,
"Saving Game…" (or "Loading Game…") / "Just a moment!", the bar with the fire
glyph; then the same box with "Loading…"; then the new map. At boot the
"Loading…" box shows before map00. Expect a flash, not a long screen — the
load itself is synchronous, as in the original.

---

## G4a — grade and level counters (menus 1 and 32)

- `Player.h`: `int currentGrades = 0, bestGrades = 0;` (3 bits per map,
  `src/Player.cpp:2058-2074`), helpers `getCurrentGrade(n)`,
  `setCurrentGrade(n, g)` (OR only, as legacy `:2063-2065`), `getBestGrade(n)`,
  `setBestGrade(n, g)` (`:2071-2074`); `static int gradeToString(int g)`:
  6→38, 5→39, 4→40, 3→41, 2→42, 1→43, else 44 (`src/Player.cpp:1991-2020`).
  `reset()` zeroes both. `WorldState::restorePlayer` keeps both (config file).
- `Game.h/.cpp`:
  ```cpp
  // Player::fillMonsterStats (src/Player.cpp:107-126): total = entities with a
  // monster payload and !(flags & 0x80 NOTRACK); killed = those with info & 0x1010000.
  void fillMonsterStats(int& killed, int& total) const;
  // Player::levelGrade (src/Player.cpp:2022-2041); commit = legacy b.
  int levelGrade(bool commit);
  ```
  `levelGrade`: `g = player->getCurrentGrade(loadMapID); if (g) return g;`
  `fillMonsterStats(k, t); if (t == 0 || map->totalSecrets == 0) return 0;`
  `g = std::max(((((6 * ((k << 8) / t)) >> 8) + ((6 * ((mapSecretsFound << 8) / totalSecrets)) >> 8)) >> 1) - player->currentLevelDeaths, 1);`
  `if (commit) { if (g > getBestGrade(loadMapID)) setBestGrade(loadMapID, g); setCurrentGrade(loadMapID, g); } return g;`
- `io/MenuData.h`: `kMenuLevelStats = 1` in `MenuId`.
- `MenuSession::fillValues` (`new_src/core/MenuSession.cpp:1105-1125`): add the
  `fillStatus(true,false,false)` block (`src/MenuSystem.cpp:3951-3967`, UI19.2)
  for `kMenuLevelStats` (rows: label of row 3 = map name, values rows 5..10)
  and `kMenuLevel` 32 (label of row 1 = map name, values rows 2..7 — `src/MenuSystem.cpp:1599-1607`,
shipped menus.bin has 8 rows; corrected 2026-10-29 after G4a review):
  1. map name: `loc.get(kTextIngame2, tables.levelNames[loadMapID-1])`
     (`mapNameField = 0xC00 | levelNames[id-1]`, `src/LoadingManager.cpp:331`)
  2. time: `formatTime(totalLevelTime + (gameTime - curLevelTime))` =
     `s = ms/1000; m = s/60; h = m/60;` → `h ":" mm ":" ss` with `m%60`,
     `s - m*60` zero-padded to 2, hours unpadded (`src/Player.cpp:1127-1146`)
  3. secrets `buildFraction(mapSecretsFound, map->totalSecrets)`
  4. enemies `buildFraction(killed, total)` from `fillMonsterStats`
  5. `player->moves`
  6. `player->currentLevelDeaths`
  7. `loc.get(kTextIngame2, Player::gradeToString(game->levelGrade(false)))`
  `MenuSession::Env` needs `gameTime` (`const int64_t*`) and `map`/`tables`
  if absent. Update the "None of those exist" comment.

**Acceptance G4a:** Menu → Status → Level shows real time/secrets/enemies/
turns/deaths/grade on map00 (grade "--" while map00 has 0 secrets or monsters).

---

## G4b — level-stats screen and flow

- `MenuSession`: menu 1 is a `menus.bin` row (type 5, 15 items, UI19.2);
  `initMenu` builds it from the file like other parsed menus and overrides row
  3's label with the map name argument (row 3 flags 0x49 → centred divider via
  the existing divider path). Settings for this id (UI19.3): rect x=23, y=0,
  w=407, h=320, row height 16 (`FONT_HEIGHT[0]`, padding 0), no cursor (type 5),
  no scrollbar, no clip, labels at x=23 TOP|LEFT, values right-aligned at
  x=430 RIGHT|TOP, empty-text non-divider rows not drawn but keep height.
- `MenuViewModel` gains `bool statsSkin` (like `vendingSkin`,
  `new_src/ui/MenuModel.h:94-97`): `MenuView` draws `art.endOfLevelStatsBG`
  (new `UiAssets` texture, `endOfLevelStatsBG.bmp` 480x320) at (0,0) instead of
  the menu background (`src/MenuSystem.cpp:762-763`), no info buttons, no row
  plates. Right soft key label `(0,43)` "Continue" if the menu draws soft keys
  (`src/GameStateRunner.cpp:213-214`).
- Input (UI19.4): FIRE (Use), AUTOMAP and BACK → `select`; MENU → `back()`,
  which with an empty stack does nothing for this menu; touch release anywhere →
  same as FIRE. `select` for `kMenuLevelStats` ignores the row and calls a new
  `Env::continueLevelStats` callback (`src/MenuSystem.cpp:2996-3000`;
  `endingGame` branch to `MENU_END_RANKING` out of scope).
- `GameContext`: `menuEnv.continueLevelStats = [this] { loading_.loadMap(loading_.nextMap(), true, false); };`
- `LoadingManager::tickSaving` stats branch (replaces the G1b stand-in):
  ```
  game->levelGrade(true);                      // commits before the menu is built (src/Game.cpp:2069-2071)
  fprintf(stderr, "[sound] 1069\n");           // Music_Level_End (src/Canvas.cpp:888-890)
  env_.openMenu(kMenuLevelStats);
  ```
- The held black fade (map00 IP 9671) stays under the opaque stats background;
  nothing to do.

**Acceptance G4b (TEST):** press **N** (forward) → "Saving Game…"/"Loading Game…"
box → full-screen end-of-level panel: map name divider, Time H:MM:SS, Secrets
a/b, Enemies a/b, Turns, Deaths, Grade (letter or "--"), "Tap Screen to /
Continue" at the bottom; Enter/click/Tab/Backspace continue to "Loading…" and
the next map; Esc does nothing. **Shift+N** (backward) shows no stats. Playing
map00 to its exit shows stats for Tycho.

---

## G5a — travel-map logic (`core/TravelMapSession`, `ui/TravelMapModel.h`)

Dead star-field page (TM §7) is **not** ported.

### `ui/TravelMapModel.h` (plain data, no logic, ADR 0012)
```cpp
enum class TmImage { None, Moon1, Earth2, Earth4, Hell3, ToMoon, ToEarth, ToHell };
struct TravelMapModel {
	int gridT = 0;                        // ms since init (grid scroll)
	TmImage closeup = TmImage::None; int closeupX = 0, closeupY = 0;
	TmImage path = TmImage::None;         // revealed part of the path image
	int pathSrcX = 0, pathSrcY = 0, pathSrcW = 0, pathSrcH = 0, pathX = 0, pathY = 0;
	int pathLabelId = -1; int pathLabelX = 0, pathLabelY = 0, pathLabelAnchor = 0;
	bool ship = false; int shipX = 0, shipY = 0;
	bool crosshair = false; int crossX = 0, crossY = 0;
	bool box = false;                      // locator box + name (TM §4.3)
	bool magnifier = false; int magX = 0, magY = 0;
	int highlightX = 0, highlightY = 0;
	int nameId = -1;                       // type-3 string id of the level name
	int nameX = 0, nameY = 0;
	bool prompt = false;                   // (0,96) "Tap Screen to Continue" at (240, 299) anchor 17
};
```

### `core/TravelMapSession.h`
```cpp
class TravelMapSession {
public:
	struct Env { const int64_t* upTimeMs = nullptr; const UiAssets* art = nullptr; };
	void init(const Env& env);
	void begin(int loadId, int lastId);   // TravelMapManager::init (src/TravelMapManager.cpp:23-121) minus var 15
	// Advances the phase machine (TM §4) and fills the model. Called once per
	// rendered frame, like drawTravelMap (src/TravelMapManager.cpp:146-256).
	void buildModel(TravelMapModel& m);
	// handleInput (TM §5). Returns true when the screen is finished (finishAndLoadLevel).
	bool handleAction(Action a);
private:
	int sv_[9] = {};                      // canvas->stateVars usage per TM §4
	int load_ = 1, last_ = 0;
	int totalPast_ = 0;                   // totalTMTimeInPastAnimations
	int crossTX_ = 0, crossTY_ = 0;       // target crosshair (TM §2.2)
	bool twoStepEarth_ = false;           // local `b` of init
	Env env_;
};
```
All formulas, constants and phase transitions are in TM §2.2, §3, §4,
§4.1-§4.3, §5 — port them verbatim from `src/TravelMapManager.cpp` lines cited
there (constants table TM §2.2 = `src/Canvas.h:95-106`). Image sizes come from
the textures (`Texture::width()/height()`), not hard-coded. `xDiff = yDiff = 0`
(TM §2.2). Time base `upTimeMs`.

Input (TM §5): `Action::Menu` → finished; `Action::Use` → finished if
`sv_[6] == 1`, else the fast-forward branches only when `hasSavedState()` —
which is **false** until the save system exists (it needs FULLPLAYER,
`src/Game.cpp:2250-2268`), so before the prompt Use does nothing (faithful to a
fresh install); everything else ignored.

Earth offset (TM §2.2 note): crosshair/locator coordinates on Earth use origin
(123,150) while `TM_Levels4` is drawn at (113,140) — a 10 px mismatch. **Port it
faithfully**; it is deviation candidate MD5, to be decided by the user after
seeing it.

**Acceptance G5a:** builds; add an assert self-check in the .cpp (run once
from `init`) on the TM §4.1 integer formula (C++ truncating division,
left-to-right): `yCoordOfSpaceShip(0) == 54` (k 0: 40·15/11),
`yCoordOfSpaceShip(62) == 50` (k 45: -15 + 12 + 40 = 37 → 555/11),
`yCoordOfSpaceShip(124) == 2` (k 90: -62 + 24 + 40 = 2 → 30/11), so a sign or
precedence slip fails loudly.

---

## G5b — travel-map view, assets, wiring

- `UiAssets`: `highlight.bmp`, `magnifyingGlass.bmp`, `spaceShip.bmp`,
  `TM_Levels1..4.bmp`, `toMoon.bmp`, `toEarth.bmp`, `toHell.bmp`,
  `TravelMap.bmp`, `travelMapHorzGrid.bmp`, `travelMapVertGrid.bmp` (TM §2.1).
  Loaded once at startup (deviation MD4: legacy loads per entry and disposes in
  `finishAndLoadLevel`, invisible difference). `cockpit.bmp` not loaded.
- `ui/TravelMapView.h/.cpp`: `UiResult drawTravelMap(Ui& ui, const TravelMapModel& m);`
  Order (TM §3): `TravelMap.bmp` at (240,0) HCENTER|TOP; grid with
  RENDER_BLEND50 (vertical strip every 44 px from `x = (t/200) % 44`, y 0;
  horizontal strip at y = 5 + 44k < 320, at x 0 and 240), restore blend; closeup;
  path region + label (type 3, `dehyphenate`); ship; crosshair: two full-screen
  lines black (0xFF000000) at x+1 / y+1, then 0xFFBDFD80 at x / y; locator:
  magnifier (anchor 3) if `magnifier`, `highlight` (anchor 6), name with char
  color 3 (anchor 3); prompt `(0,96)` wrapped at 24, dehyphenated, at (240,299)
  anchor 17. A mouse release anywhere returns `UiAction::Activate` (touch-up in
  this state synthesizes FIRE, TM §5).
- `GameStates.h`: `TravelMap = 21,  // legacy ST_TRAVELMAP (src/Canvas.h:88)`.
- `GameContext`: member `TravelMapSession travel_;` init with `{&upTimeMs, uiAssets}`
  (`Init` gains `const UiAssets* uiAssets` if not reachable through `Ui`).
  `enterState_(TravelMap)`: `pendingActions_.clear(); travel_.begin(loading_.travelLoadId(), loading_.travelLastId());`
  Input loop: `if (state_ == StateId::TravelMap) { if (travel_.handleAction(a)) finishTravel_(); continue; }`
  placed with the Menu/MiniGame dispatch, before the blocked-drop check.
  `finishTravel_()` = `setState(StateId::Loading)` (caption (0,41), G3).
  `render()`: when `TravelMap`, build the model and draw it last (opaque
  480x320). No tick work (excluded from the update chain, TM §1).
- `LoadingManager::enterTravelMap`: last line becomes
  `host->requestState(StateId::TravelMap);`.

**Acceptance G5b (TEST):** at boot the travel map shows Mars→Moon (path
revealed right→left with the spaceship on a parabola, "Earth's moon" label),
crosshair glides from the bottom-right corner, Tycho's box with a blinking
magnifier and "Tap Screen to Continue"; Enter/click continue, Esc skips at any
moment. N from Tycho (same planet) → stats → travel map: Tycho's box 1.5 s,
crosshair glides to Cichus. N from Kepler (3→4) → Moon→Earth path, two-step
Earth closeup (TM_Levels2, then TM_Levels4). Restart Level → travel map of the
same level (no path, crosshair from the corner). Watch for the 10 px Earth
offset (MD5) and report.

---

## Deviations introduced (to log in `docs/status.md` / PLAN)

| Id | Deviation | Why / when to fix |
|---|---|---|
| MD1 | `EV_CHANGE_MAP` fade bit (0x80) ignored, logged | no shipping site sets it (MC §2); port with `fadeScene` CHANGEMAP/SHOWSTATS completion if ever needed |
| MD2 | leave-time BRIEFPLAYER write, "Game Saved" msg, `clearEvents` skipped in Saving | unobservable (overwritten / cleared before use) |
| MD3 | `prepareMonsters` corpse resurrection on revisits deferred | needs `Entity::resurrect` |
| MD4 | travel-map images loaded once at startup | memory only |
| MD5 | (candidate) Earth 10 px crosshair/closeup offset kept faithful | user decides after seeing it |
| MD6 | player restart snapshot is a value copy of `Player` | save system ports `savePlayerState` (ADR 0034) |
| — | revisit entities: no dropped items, no entity names, no death functions in the record | those subsystems are not ported; their record fields are written empty |

## Out of scope

Save/load files, `FWORLD`/full variant, `EV_END_GAME` + epilogue + `MENU_END_RANKING`,
automap visited flags (only round-tripped as bits), sound, fog values per map,
`staticFunc(1)` (game completed), `loadType == 3` spawn, debug maps menu.
