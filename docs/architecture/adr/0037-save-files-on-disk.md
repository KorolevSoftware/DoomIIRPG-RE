# ADR 0037 — Save files live on disk in the legacy payload layouts plus a CRC trailer; disk is the only store; no main menu yet

Date: 2026-10-30. Status: accepted. Spec: `../save-load.md`. Amends ADR 0034 §1 (storage),
§3, §4 and §5.
**§6 superseded by ADR 0043 (2026-11-03)**: boot opens the main menu, `--newgame` / capture
runs bypass the front end, BACKTOMAIN and save-exit return to the main menu instead of quitting.

## Context

The original keeps one save slot as loose files in `Doom2rpg.app/` relative to the working
directory: `CONFIGFILENAME`, `FULLPLAYER`/`FWORLD` (manual save), `BRIEFPLAYER` and
`SB_1..SB_10` (level transitions and the level-entry snapshot that Restart Level reads)
(`docs/original-code/save-load.md` §1-§7, `src/Game.cpp:1576-2370,3690-3768`,
`src/JavaStream.cpp:9-24,92-112,214-246`). Its reader has no bounds checks and any I/O
failure is a fatal `Error()`.

The rewrite already serializes the brief world record in the legacy layout, but into ten
in-memory slots plus a value copy of `Player` (ADR 0034). `platform/FileSystem` already has
`savePath() = SDL_GetBasePath() + "Doom2rpg.app"` and an atomic temp+rename write
(`new_src/platform/FileSystem.cpp:18-27,63-82`). There is no main menu in the rewrite: the
game boots straight into a new game (`new_src/core/Main.cpp:359`).

## Decision

1. **Location.** One slot in `FileSystem::savePath()` = `<executable dir>/Doom2rpg.app/`
   (`build_new/new_src/Doom2rpg.app/`). Same folder name as the original; the executable
   directory equals the original's working directory for the documented run command
   (`cd build_new/new_src && ./DoomIIRPG`). File names are the legacy ones.
2. **Payloads keep the legacy layouts.** World records and player records are byte-identical
   to `saveWorldState` / `savePlayerState` (fields the rewrite has no state for are written as
   fixed values and ignored on read, as in ADR 0034 §2). The config payload is the legacy
   core part (version int **11** … `bestGrades`, 129 bytes) followed directly by the
   `0xDEADBEEF` marker — the GEC port block (window/vsync/deadzone/keybinds, 658 bytes) is
   dropped: the rewrite has none of those settings.
3. **Every file ends with a 4-byte little-endian CRC-32 (zlib) of its payload.** It turns
   truncation and bit rot into a cheap "file absent" before any parser runs. Consequence:
   the files are not interchangeable with the legacy C++ port's, which is not a goal.
4. **Disk is the only store.** `WorldState` becomes a pure codec
   (`save(bool brief) -> bytes`, `restore(bytes)`); the ten in-memory slots and the `Player`
   value-copy snapshot are deleted. Every legacy write point writes the file (leave:
   `SB_<map>` + `BRIEFPLAYER`; entry: `saveLevelSnapshot`; first map of a new game: full
   save) and every read point reads it (revisit, Restart Level, Load, Continue). The new
   `core/SaveGame` (port of the save half of `src/Game.cpp`) is the only code that touches
   disk; `LoadingManager` owns it; `domain/game/PlayerState` is the player-record codec.
5. **Trust boundary.** Reads are capped at 1 MiB, CRC-checked, and parsed with a
   `DataReader` that flags overruns; config and player payloads must be consumed exactly;
   indexes from the file (`entityDeathFunctions` count, entity index, `linkIndex`, status
   effect count, map id, weapon) are range-checked. Any failure = "no such file" + one log
   line: no crash, no exit. Player files are parsed into a copy and committed only when
   valid. A world record that passes the CRC but fails parsing (only a serializer bug can do
   that) is logged and left partially applied, which is memory-safe. Write failures keep the
   previous file (atomic rename), are logged, and suppress the "Game Saved" message.
6. **(Superseded by ADR 0043.) No main menu yet — stand-ins.** Boot = `MenuSystem::startGame(!hasSavedState())`:
   Continue (`loadState(getRecentLoadType())`) when a save exists, else New Game
   (`removeState` + new-game resets). The CLI flag `--newgame` replaces "New Game" + confirm
   14 (wipes the slot). `backToMain` (Main Menu → Save Game / Don't Save, dead-menu Main
   Menu) quits the application. Golden-frame capture runs (`DOOM2RPG_CAPTURE_TICKS`) disable
   disk access entirely and start a new game, so they never touch the user's save. The real
   main menu replaces all three stand-ins.
7. **Load runs synchronously at request time.** The legacy extra `ST_LOADING` frame with
   `loadType != 0` (`src/Canvas.cpp:911-915`) is collapsed into
   `LoadingManager::loadState`, which reads the player file and enters `loadMap` directly.

**Data-loss guard (orchestrator amendment 2026-10-30):** if any save file failed validation (size/CRC/parse) since startup, or fails CRC when `removeState` runs, the existing slot files are moved into `Doom2rpg.app/corrupt/` (overwriting older copies there) before the new game wipes the slot; logged `[save] moved N file(s) with a damaged save to …` (`new_src/core/SaveGame.cpp`).

## Consequences

- Progress survives the process; Restart Level, revisits and Continue all use the same files
  the original uses, with the original's quirks (e.g. in-game Load after a level transition
  loads the brief entry snapshot, `getRecentLoadType()` = 2).
- Removing `MD2` (skipping the leave-time `BRIEFPLAYER`) becomes necessary: with files on
  disk, quitting during the stats screen / travel map must continue on the next map.
- A missing or damaged file degrades exactly like a missing file in the original (fresh map,
  "No Saved Game", new game at boot) instead of crashing.
- The full-world variant is now ported (live monsters keep health, effects and goal; corpses
  stay), extending the same `EntityState` functions as ADR 0034 §4 foresaw.

## Rejected alternatives

- **Keep the in-memory slots and mirror each write to disk.** Two sources of truth that must
  be kept in sync and reloaded at boot, for the sole benefit of surviving a failed disk
  write; the atomic write already keeps the previous file.
- **No CRC, structural validation only.** A truncated world record is detected only after it
  has been partially applied to the live map, which needs a "reload the map from the .bin"
  recovery path in the loading tail. Four bytes per file are cheaper.
- **`SDL_GetPrefPath` (per-user application data).** Conventional for shipped games, but
  hides the files from the developer who has to inspect and delete them during this rewrite,
  and differs from the original. Easy to switch later in `FileSystem::initialize`.
- **Full 791-byte config with a zero-filled GEC block.** Writes settings the rewrite does not
  have; zero keybinds would be wrong data, not absent data.
- **Typed or textual (JSON) save format.** A second serializer next to the legacy-layout
  world codec ADR 0034 built precisely to avoid one.
- **Refuse BACKTOMAIN until a main menu exists.** Leaves "Main Menu → Save Game" and the
  dead menu's "Main Menu" dead; quitting is the honest equivalent of "leave the game".
