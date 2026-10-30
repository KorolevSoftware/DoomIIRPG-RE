# ADR 0034 — Per-map world state is an in-memory byte blob in the legacy brief world-record layout

Date: 2026-10-29. Status: accepted; storage (§1 slots), §3, §4 and §5 amended by [ADR 0037](0037-save-files-on-disk.md) (files on disk, full variant, player file). Spec: `../specs/2026-10-29-map-change.md` (G2a/G2b).

## Context

The original keeps the state of every visited map on disk, one file per map
(`BRIEFWORLD[mapID-1]` = `SB_1..SB_10`): written when the map is left
(`saveState(43|51)`, `src/Game.cpp:2097-2112`), overwritten at entry right after
restore + spawn (`saveLevelSnapshot`, `src/Game.cpp:2119-2153`) and read back at
the next entry (`loadWorldState`, `src/Game.cpp:1736-1909`). The entry copy is
also the "Restart Level" point: `loadState(2)` reads `BRIEFPLAYER` +
`BRIEFWORLD[cur]` (`src/Game.cpp:2191-2215`). Facts:
`docs/original-code/map-change.md` §3-§5.

The rewrite has no save system yet (separate later PLAN item), and the current
restart path resets the player to new-game stats (`new_src/core/GameContext.cpp:409-436`),
which is only correct on map00. A map change makes both problems real.

## Decision

1. **In memory, legacy layout.** A new domain module `domain/game/WorldState`
   owns ten `std::vector<uint8_t>` slots (one per mapID 1..10). Each slot holds
   exactly the byte stream the legacy `saveWorldState(OS, brief = true)` writes,
   field by field in the same order and encoding, produced with the existing
   `io/DataWriter` and read with `io/DataReader` (both little-endian,
   `new_src/io/DataWriter.h:11-12`). Entity records are a port of
   `Game::saveEntityStates` / `Entity::getSaveHandle` / `saveState` /
   `loadState` / `restoreBinaryState`, brief variant only
   (`src/Entity.cpp:1406-1786, 1787-1934`, `src/Game.cpp:3371-3395`), in a
   sibling file `domain/game/EntityState`.
2. **Fields the rewrite has no state for are written as the legacy empty value**
   (fog 0s, bombs 0s, death-function count 0, dropIndex 0, notebook 0s,
   watchLine -1, VIOS 0, …) and consumed-and-ignored on read. When the owning
   subsystem is ported, it fills its field in place; the layout does not move.
3. **The save system later writes these blobs verbatim** as `SB_<mapID>` files
   (and reads them back into the slots). No format conversion layer is planned;
   file-level versioning, if any, belongs to the save system. Until then a blob
   never outlives the process, so there is no compatibility constraint between
   builds.
4. **Only the brief variant is ported.** The full-world variant (`FWORLD`,
   `brief = false`: dropped dynamite, monster ce/goal state, real placed bombs)
   is the save system's job and extends the same functions.
5. **Restart Level uses the slot + a player snapshot.** The level-entry
   snapshot (`saveLevelSnapshot`) writes the slot of the entered map and keeps a
   value copy of `Player`; restart restores both and reloads the map through
   the normal gateway with `activeLoadType = 2`. The player copy is a
   documented shortcut: the save system replaces it with a port of
   `savePlayerState` (`src/Game.cpp:2155-2171`, `src/Player.cpp:1351+`). Fields
   that live in the legacy *config* file, not the player file
   (`currentLevelDeaths`, `totalDeaths`, `currentGrades`, `bestGrades`,
   `src/Game.cpp:1928-1950`), are kept across the restore, and
   `currentLevelDeaths` is zeroed as `loadState(2)` does (`src/Game.cpp:2200-2202`).

## Consequences

- Revisits are faithful for everything the rewrite simulates (dead monsters,
  opened crates/doors/secret walls, picked items, script vars, disabled tile
  events, secrets, loot counters, turn count, level time).
- Restart Level becomes correct on every map, and on map00 it now also skips the
  entrance tile event and keeps vars restored from the entry snapshot, exactly
  as `loadMedia` does for a loaded game (`src/LoadingManager.cpp:675-706`).
- A future save system gets its SB files for free; it must still port the
  player file and the full-world variant.
- Script threads are written (legacy record) but never restored: the legacy
  `ScriptThread::loadState` never restores `stackPtr`, so every restored thread
  ends `inuse = false` (`src/ScriptThread.cpp:45-57, 2048-2056`,
  `src/Game.cpp:1858-1868`). The rewrite reproduces that by reading and
  discarding the records.
- The lerp-sprite section is always written as count 0: both save points have
  an empty pool (leave: `snapAllMovers`, `src/ScriptThread.cpp:495`; entry:
  pool reset by `loadMapEntities`, `src/Game.cpp:332-360`).

## Rejected alternatives

- **Typed C++ snapshot struct per map** (copy entity array, vars, flags):
  simplest to write, but the save system would need a second serializer and a
  mapping between the struct and the legacy record — the rework this ADR exists
  to avoid. It also silently snapshots fields legacy deliberately resets
  (`levelVars`, open-door slots, visited tiles, `secretActive`,
  `src/Game.cpp:332-360`).
- **Write SB files to disk now**: drags in save-slot paths, file I/O failure
  handling and new-game wiping (`removeState`, `src/Game.cpp:2271-2318`) before
  the save system is designed; no visible benefit today.
- **Port the full variant now**: needs monster ce/goal serialization and drops,
  neither of which a level change needs.
