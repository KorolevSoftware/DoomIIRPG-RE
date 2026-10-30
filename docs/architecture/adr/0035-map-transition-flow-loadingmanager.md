# ADR 0035 — Map transitions run through a `LoadingManager` port; the old map stays resident until Loading phase 1; `MapData` is swapped in place

Date: 2026-10-29. Status: accepted. Spec: `../specs/2026-10-29-map-change.md`.

## Context

`EV_CHANGE_MAP` is parse-only (`new_src/domain/game/ScriptVM.cpp:1034-1045`), the
rewrite boots one map (`new_src/core/Main.cpp:210-229`) and the only reload path
is Restart Level, which re-reads `map00.bin` into the same `MapData` object
(`new_src/core/Main.cpp:329-339`, `new_src/core/GameContext.cpp:409-436`). The
original routes every map change through one gateway,
`LoadingManager::loadMap` (`src/LoadingManager.cpp:55-79`), then
`ST_SAVING` → (`MENU_LEVEL_STATS`) → `ST_TRAVELMAP` → `ST_LOADING`, and unloads
the old map only inside `loadMedia` (`src/LoadingManager.cpp:604-744`;
`docs/original-code/map-change.md` §3). `canvas->loadMapID` is read by domain
code (combat miss gate, damage scaling, secrets, vending), today from a field
that is never assigned (`new_src/domain/game/Combat.h:92`).

## Decision

1. **`core/LoadingManager.{h,cpp}`** (name and responsibilities of
   `src/LoadingManager.cpp`) owned by `GameContext`, reached by the VM through
   `GameContext::loading()` (same idiom as `cinematic()` / `minigames()`):
   the `loadMap(id, skipSave, newGame)` gateway, the `EV_CHANGE_MAP` effects,
   the `ST_SAVING` tick, the var-15 travel direction, Restart Level
   (`loadState(2)`), the per-load hooks the Loading tail calls, and ownership of
   `WorldState` (ADR 0034). `GameContext::tickLoading` stays the load tail; it
   only gains hook calls.
2. **The old map stays resident until Loading phase 1.** `EV_CHANGE_MAP` only
   records the request and changes state; the running thread keeps executing
   bytecode that is still loaded, exactly like legacy. The world save of the
   map being left happens in the new `StateId::Saving` (16) tick, before
   Loading phase 0 clears the script vars.
3. **`MapData` is swapped in place.** `GameContext::Init::reloadMap` becomes
   `std::function<bool(int mapId)> loadMapFile`, built in `Main.cpp`: read
   `map%02d.bin` (`mapId - 1`), parse into a fresh `MapData`, move-assign over
   the one `g_map` every module borrows, re-upload world/sprite textures, pick
   the sky table pair, load the per-map string type. Every load (boot, change,
   restart) re-reads the file; per-map game state is rebuilt by
   `Game::loadEntities`, which already re-wires all peers
   (`new_src/domain/game/Game.cpp:25-55`).
4. **`loadMapID` moves from `Combat` to `Game`** (`Game::loadMapID`), next to
   `spawnParam`, `totalLevelTime`, `curLevelTime`; the five readers switch.
   Flow-only state (`lastMapID`, `nextMap`, `numLevelLoads[10]`,
   `activeLoadType`, `isLoaded`, `TM_NewGame`) lives on `LoadingManager`;
   player-file state (`completedLevels`, `highestMap`, `moves`, grades) on
   `Player`, as in legacy.
5. **States added only as the screens arrive:** `Saving` (G1), the loading box
   drawn in `Saving`/`Loading` (G3), `MENU_LEVEL_STATS` reuses `ST_MENU` (G4),
   `TravelMap` (21) (G5). Before a screen exists its exit edge goes straight to
   the next state, so the flow graph is final from G1.

## Consequences

- One code path for boot, script exits, the debug key and restart; G2-G5 only
  add work inside existing edges.
- Boot now goes through the gateway as a new game (`loadMap(1, false, true)`,
  `src/IntroSequenceManager.cpp:135`), so from G5 on the travel map (Mars →
  Moon) shows before map00, as in the original.
- The map file is parsed twice at boot (Main smoke logs + Loading phase 1);
  accepted, it is milliseconds.
- `MediaLoader::finalize` already loads every media entry, not only the
  registered ones (`new_src/io/Media.cpp:69-145`), so no media reload is needed
  per map.

## Rejected alternatives

- **Unload/load synchronously inside `EV_CHANGE_MAP`**: the calling thread's
  IP points into the old bytecode and would run on the new map's bytes.
- **Gateway inside `GameContext`**: works, but `GameContext.cpp` is already
  >1000 lines and the legacy split (Canvas vs LoadingManager) gives a natural,
  citable seam.
- **Keep `loadMapID` on `Combat`**: zero-diff today, but it is a level fact read
  by menus and minigames; leaving it on a combat peer invites a second copy.
