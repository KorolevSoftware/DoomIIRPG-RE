# Spec — Save / load to disk (2026-10-30)

ADR: [0037](adr/0037-save-files-on-disk.md) (amends ADR 0034 §1/§3/§5).
Facts: `docs/original-code/save-load.md` (abbrev. **SL**), `docs/original-code/map-change.md`
(**MC**), raw log `docs/research/2026-10-30-save-load.md`.

## 0. Goal and non-goals

Port the original single-slot save system onto disk, reusing the existing world-record
serializer (`domain/game/WorldState`, `EntityState`) and the `core/LoadingManager` flow.

In scope: config file (v11), `FULLPLAYER`+`FWORLD` (manual save, full world record),
`BRIEFPLAYER`+`SB_n` (level transition + level-entry snapshot), load from boot ("Continue"),
in-game Load (confirm 49), dead-menu Load, Restart Level (type 2), the menu actions SAVE (4),
SAVEQUIT (13), SAVEEXIT (25), BACKTOMAIN (5) and the HUD "Game Saved" message; corrupt or
truncated files never crash.

Non-goals (named, not built): the main menu (`MENU_MAIN`, "Continue" row, New Game confirm 14
as a screen; ADR 0037 §6 gives the stand-in), travel-map FIRE fast-forward (needs
`hasSavedState()`, one-line follow-up in `TravelMapSession::handleAction`), `prepareMonsters`
resurrection (deviation MD3 stays), `MENU_END_FINALQUIT` (ACTION_SAVE param 1, no shipped row),
camera-active exit dialog 58, static func 1 on `gameCompleted`, legacy-port file
compatibility, settings that the rewrite does not have (sound, language, keybinds).

## 1. Ownership and data flow

```
GameContext ── owns ── LoadingManager ── owns ── SaveGame ── owns ── WorldState (codec)
                          (flow)            (files, config,      PlayerState (codec, namespace)
                                             CRC, validation)    EntityState (codec, namespace)
MenuSession ──callbacks──► GameContext::saveGame/loadGame/restartLevel/backToMain
```

* `SaveGame` is the only code that touches disk. Path = `FileSystem::instance().savePath()`
  = `SDL_GetBasePath() + "Doom2rpg.app"` (existing, `new_src/platform/FileSystem.cpp:18-27`),
  i.e. `build_new/new_src/Doom2rpg.app/` (ADR 0037 §1).
* `WorldState` loses its in-memory slots and the `Player` value-copy snapshot: disk is the
  only store (ADR 0037 §4). It becomes `save(bool brief) -> bytes` / `restore(bytes)`.
* Single-threaded; all I/O is synchronous inside a tick (`Saving` tick, Loading phase 1, a
  menu select). Files are a few KB.

## 2. On-disk format (all files)

Encoding: little-endian via `io/DataWriter` / `io/DataReader` (legacy host-endian memcpy,
SL §1). Every file = **payload + 4-byte CRC trailer**: `uint32 LE crc32(payload)` (zlib
`crc32(0, payload, size)`), ADR 0037 §3. Payloads:

| File (name exact, `src/Game.cpp:3690-3750`) | Payload |
|---|---|
| `CONFIGFILENAME` | §2.1, 133 bytes |
| `FULLPLAYER`, `BRIEFPLAYER` | §2.2 player record, 305 bytes (+108 with status effects) |
| `FWORLD` | world record `WorldState::save(false)` (layout MC §4 / `src/Game.cpp:1576-1734`) |
| `SB_1` .. `SB_10` | world record `WorldState::save(true)`, index = mapID |

Reader rules (every file): size ≤ `kMaxFileBytes = 1 << 20`; size ≥ 4; CRC must match; then
the payload parser must consume **exactly** the payload (DataReader `overrun()` false and, for
config/player, `remaining() == 0`). Any failure = "file absent" + one `[save]` log line naming
the file and reason. Never `Error()`/exit.

### 2.1 Config payload (rewrite layout = legacy core part + marker; GEC block dropped)

Order = `saveConfig` (`src/Game.cpp:1913-1975`) fields 1-26, then the marker
(SL §3). "R" = read back on load, "-" = written as shown, ignored on load.

| off | type | field | written value | R |
|---|---|---|---|---|
| 0 | int | version | `11` (`src/Game.cpp:3367-3369`) | check |
| 4 | byte | difficulty | `game.difficulty()` | - |
| 5 | bool | allowSounds | 1 | - |
| 6 | byte | soundFxVolume | 100 | - |
| 7 | byte | musicVolume | 100 | - |
| 8 | bool | vibrateEnabled | 0 | - |
| 9 | bool | enableHelp | 1 | - |
| 10 | int | defaultLanguage | 0 | - |
| 14 | int | currentLevelDeaths | `player.currentLevelDeaths` | R |
| 18 | int | totalDeaths | `player.totalDeaths` | R |
| 22 | int | animFrames | `player.animFrames` | - |
| 26 | int | controlLayout | 0 | - |
| 30 | int | controlAlpha | 0 | - |
| 34 | bool | flipControls | 0 | - |
| 35 | int×5 | help / inv / ammo / weapon / armor help bitmasks | 0 | - |
| 55 | int×10 | menu `indexes[0..9]` | 0 | - |
| 95 | bool | recentBriefSave | `SaveGame::recentBriefSave` | R |
| 96 | bool | hasSeenIntro | `SaveGame::hasSeenIntro` | R |
| 97 | short×10 | numLevelLoads | `SaveGame::numLevelLoads` | R |
| 117 | int | totalPlayTime (s) | `totalPlayTime += (upTimeMs - lastSaveTime_) / 1000; lastSaveTime_ = upTimeMs` (`:1947,:1950`) | R |
| 121 | int | currentGrades | `player.currentGrades` | R |
| 125 | int | bestGrades | `player.bestGrades` | R |
| 129 | int | marker | `0xDEADBEEF` | check |

Empty config (`saveEmptyConfig`, `src/Game.cpp:2320-2370`): same 133-byte layout with
difficulty 1, deaths 0/0, help masks 0, indexes 0, recentBriefSave **false**, grades 0/0;
`hasSeenIntro`, `numLevelLoads`, `totalPlayTime` (updated) as in memory. Does not change any
in-memory field. (This also fixes the legacy port's short empty config, SL §3.)

`loadConfig` validation: payload size == 133, int@0 == 11, int@129 == 0xDEADBEEF; else
nothing is assigned. On success `lastSaveTime_ = upTimeMs`.

### 2.2 Player payload (`domain/game/PlayerState`)

Header (34 B, `Game::savePlayerState`, `src/Game.cpp:2155-2171`) + body (271 B,
`Player::saveState`, `src/Player.cpp:1351-1420`). Field order is exactly SL §4.

| field | type | written from | on load |
|---|---|---|---|
| mapId | short | `Header::mapId` | must be 1..10 else reject |
| viewX, viewY, viewAngle, viewPitch | short×4 | Header | into Header |
| prevX, prevY, saveX, saveY, saveZ | int×5 | Header | into Header (unused) |
| saveAngle, savePitch | short×2 | Header | into Header (unused) |
| baseCe | 11 B | `CombatEntity::saveState(true)` layout, `src/CombatEntity.cpp:118-129`: short stats[0], short stats[1], byte stats[2..7], byte weapon | stats; weapon byte ≥ `WP_PLAYERMAX` (15) → -1 |
| ce | 11 B | same | same |
| characterChoice | short | 1 (marine, `new_src/domain/game/ScriptVM.cpp:313-318`) | ignored |
| weapons, weaponsCopy | int×2 | Player | Player |
| level | byte | Player | Player; `nextLevelXP = calcLevelXP(level)` (`src/Player.cpp:1279`) |
| currentXP | int | Player | Player |
| totalTime | int | 0 (not tracked) | ignored |
| totalMoves | int | 0 | ignored |
| completedLevels | int | Player | Player |
| killedMonstersLevels | int | 0 | ignored |
| foundSecretsLevels | int | Player | Player |
| disabledWeapons | int | Player | Player |
| prevWeapon | byte | 0 | ignored |
| gamePlayedMask, lastCombatTurn | int×2 | 0 | ignored |
| inCombat | bool | 0 | ignored |
| highestMap | short | Player | Player, clamped 1..10 |
| isFamiliar | bool | 0 | drives the copy block below |
| familiarType | short | 0 | ignored |
| playerEntityCopyIndex | int | 0 | ignored |
| hackedVendingMachines, vendingMachineHackTriesLeft1, …2, chainsawStrengthBonusCount | int×4 | Player | Player |
| lastSkipCode | byte | Player (`sentry-bot-minigame.md` G1) | Player |
| ammo[9], ammoCopy[9] | short×18 | Player | Player |
| inventory[26] | short×26 | Player | Player |
| inventoryCopy[26] | short×26 | 0 (no field, `new_src/domain/game/Player.cpp:202-203`) | ignored |
| numStatusEffects | byte | Player | must be ≤ 18 else reject |
| status block, iff count ≠ 0 | 18 × (short `[36+i]`, short `[0+i]`, short `[18+i]`) | `statusEffects` | `statusEffects`; count 0 → all 54 zeroed (`src/Player.cpp:1311-1317`) |
| copy block, iff isFamiliar | byte n; iff n ≠ 0: 18×3 shorts | never written | read and discarded (n ≤ 18 else reject) |
| counters[8] | int×8 | 0 | ignored |
| gameCompleted | bool | Player | Player |

After a successful parse: `translateStatusEffects(); updateStats();` (`src/Player.cpp:1345-1346`).
The parse goes into a local `Player tmp = player;`, committed with `player = tmp` only when
valid, so a rejected file leaves the player untouched.

## 3. Interfaces

### 3.1 `io/DataReader` (modify)

Add `bool overrun() const { return overrun_; }` and `bool overrun_ = false;`. Set it to true
whenever `readByte/readUShort/readUInt/skip/readBytes` is asked for more bytes than
`remaining()` (the existing clamp-and-return-0 behaviour stays). No other change.

### 3.2 `platform/FileSystem` (modify)

* `readFile(path, out, size_t maxSize = SIZE_MAX)`: return false when `ftell` < 0 or
  size > maxSize (today a failed `ftell` resizes to `SIZE_MAX`, `FileSystem.cpp:49-52`).
* `static bool fileExists(const std::string& path)` (`stat` == 0).
* `static bool removeFile(const std::string& path)` (`std::remove`, true if removed or absent).

### 3.3 `domain/game/PlayerState.{h,cpp}` (new)

```cpp
namespace newcore {
class DataReader; class DataWriter; class Player;
namespace PlayerState {
// Game::savePlayerState header (src/Game.cpp:2155-2171).
struct Header {
	int mapId = 0;
	int viewX = 0, viewY = 0, viewAngle = 0, viewPitch = 0;
	int prevX = 0, prevY = 0, saveX = 0, saveY = 0, saveZ = 0;
	int saveAngle = 0, savePitch = 0;
};
void save(const Player& p, const Header& h, DataWriter& w);   // §2.2
// False = truncated / trailing bytes / out-of-range field; p and h untouched then.
bool load(DataReader& r, Player& p, Header& h);
#ifndef NDEBUG
void selfCheck();   // called once from SaveGame::init (G4a)
#endif
}
}
```
Debug self-check (`#ifndef NDEBUG`, run once, the `WorldState` idiom
`new_src/domain/game/WorldState.cpp:24-170`): round trip of a Player with weapons, ammo,
inventory, two status effects and `ce.weapon = -1`; size == 305+108; truncated by one byte →
false and target unchanged; one trailing byte → false.

### 3.4 `domain/game/EntityState` (modify) — full variant

* `void save(const Env& env, DataWriter& w, bool brief);` (`Game::saveEntityStates(OS, b)`,
  `src/Game.cpp:3379-3395`). `load` unchanged in signature: the record shape is keyed on the
  handle bits, not on brief/full (`src/Entity.cpp:1574-1786`).
* `getSaveHandle(env, e, index, brief)`: the final block (corpse rule / ET_MONSTER `kHShort`)
  runs only when `brief` (`src/Entity.cpp:1849-1856`). Dropped dynamite rule is moot (no drop
  slots).
* `saveState`: replace the shortcut comment with the full monster block, inside
  `if ((e.info & kInfoHidden) == 0)` after the scale byte (`src/Entity.cpp:1537-1545`):
  ```
  if ((n & kHCorpse) == 0) {
      w.writeShort((int16_t)e.monster->monsterEffects);
      w.writeShort((int16_t)e.monster->ce.stats[Enums::STAT_HEALTH]);   // ce.saveState(OS,false) (src/CombatEntity.cpp:127-128)
      w.writeInt(goalType | goalFlags << 4 | goalTurns << 8 | goalX << 12 | goalY << 17 | goalParam << 22);   // src/EntityMonster.cpp:25-27
  }
  ```
* `loadState`: the matching `else` of `if ((n & kHCorpse) != 0) e.info |= kInfoCorpse;`
  (`src/Entity.cpp:1718-1727`):
  `monsterEffects = readShort(); ce.stats[STAT_HEALTH] = readShort(); g = readInt();`
  `goalType = g & 0xF; goalFlags = g>>4 & 0xF; goalTurns = g>>8 & 0xF; goalX = g>>12 & 0x1F;
  goalY = g>>17 & 0x1F; goalParam = g>>22 & 0x3FF` (`src/EntityMonster.cpp:29-37`).
* Trust-boundary guards in `load` (return false = corrupt): `e.linkIndex` read from the
  record must be in `[0, 1024)`; the existing bad-index check stays.

### 3.5 `domain/game/WorldState` (modify)

```cpp
class WorldState {
public:
	struct Env { ... unchanged ... };
	void init(const Env& env);
	// saveWorldState(OS, brief) of the LOADED map (src/Game.cpp:1576-1734). Accumulates
	// totalLevelTime exactly as today.
	std::vector<uint8_t> save(bool brief);
	enum class Restore { Applied, Rejected, Corrupt };
	// loadWorldState(IS) (src/Game.cpp:1756-1909). Rejected = empty blob or compile-date
	// mismatch (nothing applied; totalLevelTime = 0, spawnParam = -1 if stored date != 0, as
	// today). Corrupt = overrun / bad index / lerp count != 0, possibly partially applied.
	Restore restore(const std::vector<uint8_t>& blob);
};
```
Body = today's `saveLevel`/`restoreLevel` with: (a) `brief` passed to `EntityState::save`;
(b) the sprite-bit corpse rule (`:1643-1650`) only when `brief`; (c) `placedBombs` stay 0s in
both variants (no bombs in the rewrite); (d) in `restore`, death-function count > 64 →
`Corrupt` before writing the array (today it can overflow `entityDeathFunctions[128]`);
(e) `r.overrun()` checked at the end → `Corrupt`; (f) trailing bytes → log only, `Applied`.
**Delete** `levels_`, `player_`, `hasPlayer_`, `saveLevel`, `restoreLevel`, `savePlayer`,
`restorePlayer`, `clear`, `levelBlob` (done in G4b, see §5). The self-check switches to
`save(true)` / `restore(blob)` and adds: a truncated blob (size-1) → `Corrupt`; one live
monster saved with `save(false)` round-trips `ce.stats[0]`, `monsterEffects`, goal fields.

### 3.6 `core/SaveGame.{h,cpp}` (new) — port of the save half of `src/Game.cpp`

```cpp
namespace newcore {
class EntityDefs; class Game; class MapData; class Player; class ScriptVM;

class SaveGame {
public:
	static constexpr int kConfigVersion = 11;      // src/Game.cpp:3367-3369
	static constexpr uint32_t kConfigMarker = 0xDEADBEEF;
	static constexpr size_t kMaxFileBytes = 1u << 20;
	enum class File { FullWorld = 0, Config = 1, BriefPlayer = 2, FullPlayer = 3, BriefWorld = 4 };   // src/Game.h:28-32
	struct Env {
		Game* game = nullptr; Player* player = nullptr; ScriptVM* vm = nullptr;
		MapData* map = nullptr; const EntityDefs* defs = nullptr;
		const int64_t* gameTime = nullptr; const int64_t* upTimeMs = nullptr;
	};
	void init(const Env& env);                     // inits world_ with the same pointers
	void setDiskEnabled(bool on) { disk_ = on; }   // false: no reads, no writes (golden runs)

	// Config-file state with no other owner (src/Game.cpp:1943-1948).
	short numLevelLoads[10] = {};
	bool recentBriefSave = false;                  // canvas->recentBriefSave
	bool hasSeenIntro = false;
	int totalPlayTime = 0;                         // seconds

	bool loadConfig();                             // src/Game.cpp:1977-2054
	bool saveConfig();                             // :1913-1975
	bool hasSavedState() const;                    // :2249-2268
	void removeState();                            // :2271-2318 (saveEmptyConfig + remove 13 files)
	// Game::saveState file half (:2059-2117); the caller already ran levelGrade for 0x10.
	// recentBriefSave = type & 0x20; config always; 0x01 player file; 0x02 world file
	// (SB_<worldMapId> if brief else FWORLD). True = every requested file written.
	bool saveState(int saveType, int worldMapId, const PlayerState::Header& h);
	bool saveLevelSnapshot(int mapId, const PlayerState::Header& h);   // :2119-2153 (no config)
	// loadState's file read (:2191-2189): FULLPLAYER (1) / BRIEFPLAYER (2). False = absent/corrupt.
	bool loadPlayer(int loadType, PlayerState::Header& h);
	// loadWorldState (:1736-1754): FWORLD if full else SB_<mapId>. False = no record applied
	// (out of range, absent, CRC, Rejected); Corrupt is logged and returns true (applied).
	bool loadWorld(int mapId, bool full);

private:
	std::string path(File f, int mapId) const;     // savePath + "/" + name; SB_<mapId>
	bool write(File f, int mapId, std::vector<uint8_t> payload);          // ensureSaveDirectory, + CRC, FileSystem::writeFile (atomic)
	bool read(File f, int mapId, std::vector<uint8_t>& payload) const;    // §2 reader rules, strips CRC
	bool exists(File f, int mapId) const;
	bool writeConfig(bool empty);
	Env env_;
	WorldState world_;
	bool disk_ = true;
	int64_t lastSaveTime_ = 0;                     // game->lastSaveTime
};
}
```
Algorithms:
* `hasSavedState`: config readable (`read` ok, payload ≥ 4, int@0 == 11) **and**
  `exists(BriefPlayer)` **and** `exists(FullPlayer)` **and** any `exists(BriefWorld, 1..10)`.
  FWORLD not checked (SL §6). `disk_ == false` → false.
* `removeState`: `writeConfig(true)`, then `removeFile` FWORLD, FULLPLAYER, BRIEFPLAYER,
  SB_1..SB_10. Logs `[save] removeState`.
* `write`/`read` log `[save] wrote <name> (<n> B)` / `[save] <name>: <reason>`.
* CRC helpers are two file-local functions (`appendCrc`, `checkAndStripCrc`) with a debug
  self-check: append+check ok; flipped byte fails; payload < 4 fails.

### 3.7 `core/LoadingManager` (modify)

Header changes (everything else stays):
```cpp
	struct Env { ... + const int64_t* upTimeMs = nullptr; std::function<void()> quit; };
	void boot(bool forceNewGame);          // App.cpp:120 loadConfig + main-menu stand-in (ADR 0037 §6)
	void startGame(bool newGame);          // MenuSystem::startGame (src/MenuSystem.cpp:3580-3608)
	// LoadingManager::loadState + Game::loadState collapsed (src/LoadingManager.cpp:38-45,
	// src/Game.cpp:2191-2215, deviation SL-D1). False = no usable player file, nothing changed.
	bool loadState(int loadType);
	int recentLoadType() const { return save_.recentBriefSave ? 2 : 1; }   // src/LoadingManager.cpp:753-760
	void requestSave(int saveType);        // canvas->saveState(type, 3, 196) from the menu
	SaveGame& save() { return save_; }
	// removed: restartLevel(), world(), numLevelLoads_, world_
private:
	PlayerState::Header headerAtPlayer(int mapId) const;
	PlayerState::Header headerAtSpawn(int mapId, bool zeroIfNone) const;
	SaveGame save_;
```

Bodies:
* `init`: `save_.init({game, player, vm, map, defs, gameTime, upTimeMs})` instead of `world_.init`.
* `loadMap`: `numLevelLoads_` → `save_.numLevelLoads` (no other change).
* `headerAtPlayer(mapId)` (manual save `src/Canvas.cpp:871`, first-load full save
  `src/LoadingManager.cpp:686-688`, snapshot `src/Game.cpp:2124-2137`):
  `{mapId, destX, destY, destAngle, viewPitch, destX, destY, viewX, viewY, viewZ, viewAngle, viewPitch}`.
  The rewrite has no `prevX/saveX` (familiar-only canvas fields); they are written from the
  view and ignored on read.
* `headerAtSpawn(mapId, zeroIfNone)` (`src/Canvas.cpp:855-868`): if `zeroIfNone && spawnParam == -1`
  → all zeros except `mapId`. Else `p = max(spawnParam, 0)`, `x = 32 + (p & 0x1F)*64`,
  `y = 32 + ((p>>5) & 0x1F)*64`, `angle = ((p>>10) & 0xFF) << 7`;
  `{mapId, x, y, angle, 0, x, y, x, y, 36, 0, 0}`. (-1 is the rewrite's legacy 0, ADR 0035.)
* `tickSaving` (`src/Canvas.cpp:853-903`, replaces the body; deviation MD2 is withdrawn):
  ```
  type = saveType_; saveType_ = 0;
  if (type & 0x10) game.levelGrade(true);                        // before saveConfig (src/Game.cpp:2069-2071)
  Header h = (type & 0x08) ? headerAtSpawn(loadMapID, true)
           : (type & 0x10) ? headerAtSpawn(nextMap_, false)
           :                 headerAtPlayer(loadMapID);
  worldMap = (type & 0x08) ? lastMapID_ : loadMapID;             // (type & 0x10) and manual: current map
  if (save_.saveState(type, worldMap, h)) hud "Game Saved" (0,38) (src/Canvas.cpp:873)
  else log "[save] save FAILED (type 0x%X)"
  if (type & 0x04)      env_.quit();          // backToMain stand-in (ADR 0037 §6)
  else if (type & 0x40) env_.quit();          // app->shutdown()
  else if (type & 0x08) enterTravelMap();
  else if (type & 0x10) { "[sound] 1069"; openMenu(kMenuLevelStats); }
  else { if (type & 0x100) log "MENU_END_FINALQUIT not ported"; host->requestState(Playing); }   // 0x80 returnToGame
  ```
* `requestSave(type)`: `saveState(type, stringId(kTextIngame2, 196))`.
* `enterTravelMap` (`src/TravelMapManager.cpp:27-28`):
  `tmLast_ = (recentLoadType() == 1 && !tmNewGame_) ? tmLoad_ : clamp(lastMapID_, 0, 9)`;
  rest unchanged; drop the "no save system yet" comment.
* `finishTravelMap`: caption `isLoaded_ ? (0,39) "Game Loaded" : (0,41)`
  (`src/LoadingManager.cpp:645-650`).
* `onEntitiesLoaded`: replace the restore line with
  `if (!save_.loadWorld(loadMapID, activeLoadType_ == 1)) game.totalLevelTime = 0;`
  (`src/Game.cpp:1736-1754`). A corrupt record (CRC passed, parser failed) is logged by
  `SaveGame` and left partially applied — memory-safe, and only reachable through a
  serializer bug since disk damage is caught by the CRC (ADR 0037 §5).
* `onPlayerSpawned` (`src/LoadingManager.cpp:675-688`):
  ```
  if (!isLoaded_) save_.saveLevelSnapshot(loadMapID, headerAtPlayer(loadMapID));
  if (!isLoaded_ && !save_.hasSavedState()) save_.saveState(3, loadMapID, headerAtPlayer(loadMapID));   // first map of a new game
  ```
  (`saveState(3)` sets `recentBriefSave = false`, as legacy.)
* `loadState(type)`:
  ```
  save_.saveConfig();                                            // src/LoadingManager.cpp:42
  Header h;
  if (!save_.loadPlayer(type, h)) return false;                  // legacy: Error()+exit
  if (type == 2) player.currentLevelDeaths = 0;                  // src/Game.cpp:2200-2202
  w = player.ce.weapon;                                          // selectWeapon(ce->weapon) (src/LoadingManager.cpp:691)
  player.selectWeapon(w, w >= 0 ? defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, w) : nullptr);
  game.spawnParam = -1;                                          // loadPlayerState (src/Game.cpp:2189)
  if (h.viewX != 0 && h.viewY != 0) {                            // src/Game.cpp:2205-2208
      spawnParam = ((h.viewX>>6)&0x1F) | (((h.viewY>>6)&0x1F)<<5) | (((h.viewAngle&0x3FF)>>7)<<10);
      if (spawnParam == 0) spawnParam = -1;
  }
  activeLoadType_ = type; isLoaded_ = true;
  loadMap(h.mapId, false, false);                                // no save (activeLoadType != 0) -> TravelMap
  return true;
  ```
  The player's view fields are left to `spawnPlayer`, which consumes `spawnParam`.
* `startGame(newGame)` (`src/MenuSystem.cpp:3585-3608`):
  ```
  if (!newGame) { if (loadState(recentLoadType())) return; log "[save] Continue failed -> new game"; }
  save_.removeState();
  activeLoadType_ = 0; isLoaded_ = false; game.loadMapID = 0; lastMapID_ = 0;
  player.reset(); player.totalDeaths = 0; player.currentLevelDeaths = 0; player.currentGrades = 0;
  loadMap(1, false, true);                                       // character select + intro skipped (startupMap 1, src/IntroSequenceManager.cpp:135)
  ```
  (`bestGrades` and `numLevelLoads` are not reset — legacy does not either.)
* `boot(forceNewGame)`: `save_.loadConfig(); startGame(forceNewGame || !save_.hasSavedState());`.

### 3.8 `core/GameContext` (modify)

* LoadingManager env: `upTimeMs = &upTimeMs`, `quit = [this]{ quitRequested_ = true; }`.
* `bool quitRequested() const { return quitRequested_; }` (public) + `bool quitRequested_ = false;`.
* New public `bool loadGame(int loadType)`: the prelude of today's `restartLevel`
  (`viewPitch = viewRoll = 0`, `hud->clearScreenEffects()`), then `return loading_.loadState(loadType);`.
* `restartLevel()`: `if (loadGame(2)) return;` then today's fallback (fresh start of the
  current map) unchanged.
* MenuSession env (G5): `saveGame = [this](int t){ loading_.requestSave(t); }`,
  `loadGame = [this]{ return loadGame(loading_.recentLoadType()); }`,
  `backToMain = [this]{ quitRequested_ = true; }`.

### 3.9 `core/GameLoop.cpp` (modify)

In the frame loop, after the tick(s): `if (ctx.quitRequested()) running = false;`.

### 3.10 `core/Main.cpp` (modify)

* `--newgame` argv flag → `forceNewGame` (New Game + confirm 14 stand-in, ADR 0037 §6).
* Golden runs: `if (std::getenv("DOOM2RPG_CAPTURE_TICKS")) { ctx.loading().save().setDiskEnabled(false); forceNewGame = true; }`
  — never read, write or wipe the user's save during a capture.
* Replace `ctx.loading().loadMap(1, false, true);` with `ctx.loading().boot(forceNewGame);`.

### 3.11 `core/MenuSession` (modify, G5)

* `Env`: `std::function<void(int saveType)> saveGame; std::function<bool()> loadGame;
  std::function<void()> backToMain;`.
* `initMenu(kMenuLoad)`: `setYesNo(kStrLoadGameQuestion, 1, kActionLoad, 0)` (`src/MenuSystem.cpp:1714-1719`);
  drop the retarget comment.
* Refusal loop: delete the `kActionSave/kActionSaveQuit/kActionSaveExit` and
  `kActionBackToMain` cases (ChangeState / ShowDetails / Controls refusals stay).
* `select`:
  * `kActionLoad` (`:3035-3037`): `if (!env_.loadGame || !env_.loadGame()) gotoMenu(kMenuLoadNoSave);`
    — the shipped "No Saved Game" screen is the rewrite's answer to a missing/corrupt file.
  * `kActionSave` (`:3039-3049`): `env_.saveGame(3 | (param == 1 ? 0x100 : 0x80))`.
  * `kActionSaveQuit` (`:3113-3115`): `env_.saveGame(67)`.
  * `kActionSaveExit` (`:3199-3201`): `env_.saveGame(7)`.
  * `kActionBackToMain` (`:3051-3053`): `env_.backToMain()`.
  * `kActionRestartLevel`: comment update only (deviation D6 is gone; behaviour = `loadState(2)`).
* The `inventory[18] == 0` disable of the Save row (`src/MenuSystem.cpp:1560-1562`) is already
  ported and stays.

## 4. Deviations (all recorded in ADR 0037)

| id | original | rewrite |
|---|---|---|
| SL-D1 | `loadState` sets `ST_LOADING`; the next tick reads the player file, then `loadMap` | done synchronously in `LoadingManager::loadState`; the one-frame "Loading Game…" box before the travel map is skipped |
| SL-D2 | open/parse failure → `Error()` + exit | log; save: HUD "Game Saved" omitted; load: "No Saved Game" screen (menu) / fresh fallback (restart) / new game (boot) |
| SL-D3 | `backToMain` → `MENU_MAIN` | app quit (no main menu yet) |
| SL-D4 | main menu Continue / New Game + confirm 14 | boot auto-Continue if `hasSavedState()`, `--newgame` flag wipes |
| SL-D5 | files are raw payload | + CRC-32 trailer; config is 133 B (GEC block dropped) |
| SL-D6 | lerp sprites are serialized | count always 0 (unchanged from ADR 0034); a manual save mid-mover loses the mover |

## 5. Implementation groups

Each group builds on its own (`cmake --build build_new -j 8`); new files ⇒ re-run
`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (GLOB).

**G1 — reader hardening** (`io/DataReader.{h,cpp}`, `platform/FileSystem.{h,cpp}`): §3.1, §3.2.
Accept: builds; game boots and plays exactly as before (map00 loads, map change works).

**G2 — world codec: full variant + guards** (`domain/game/EntityState.{h,cpp}`,
`domain/game/WorldState.{h,cpp}`): §3.4, §3.5 **except the deletions** — keep
`saveLevel/restoreLevel/savePlayer/restorePlayer/clear` as thin wrappers over the new
`save(true)` / `restore()` and the slots so `LoadingManager` compiles unchanged.
Accept: debug log prints `[world] round-trip self-check OK` (with the new truncation and
full-monster asserts); map change / revisit / restart behave as before.

**G3 — player codec** (new `domain/game/PlayerState.{h,cpp}`): §2.2, §3.3. Not wired yet.
Accept: reconfigure + build (the self-check is first called in G4a).

**G4a — SaveGame** (new `core/SaveGame.{h,cpp}`): §2, §2.1, §3.6. Not wired yet.
`SaveGame::init` runs, once per process (the `WorldState::init` static-flag idiom), the CRC
self-check and `PlayerState::selfCheck()`.
Accept: builds. `SaveGame` is first constructed in G4b, so the log lines
`[save] crc self-check OK` and `[player] state self-check OK` are verified there.

**G4b — wire the flow** (`core/LoadingManager.{h,cpp}`, `domain/game/WorldState.{h,cpp}`
deletions, `core/GameContext.{h,cpp}`, `core/GameLoop.cpp`, `core/Main.cpp`): §3.5 deletions,
§3.7, §3.8 (without the MenuSession env), §3.9, §3.10.
Accept (user, `cd build_new/new_src && rm -rf Doom2rpg.app && ./DoomIIRPG`):
0. Debug log shows `[save] crc self-check OK` and `[player] state self-check OK` once.
1. Travel map, then map00. `ls Doom2rpg.app` → `BRIEFPLAYER CONFIGFILENAME FULLPLAYER FWORLD SB_1`
   (no `.tmp`). Log: `[save] wrote …` lines.
2. Pick up an item, kill a monster, open a crate, walk through the map00 exit: stats, travel
   map, map01. `SB_1` and `SB_2` exist; HUD "Game Saved" appeared at the exit.
3. Close the window, run again: travel map (map00→map01 track not needed — var 15 per
   §3.7), then map01 at its entry tile (type 2: `recentBriefSave` true). Log
   `[save] … BRIEFPLAYER` read. Inventory/health/XP as at the exit.
4. Go back to map00 through its door: item still gone, monster still dead, crate open
   (revisit from disk).
5. Die: dead menu → Restart Level → map01 entry state; deaths counter in the status screen
   kept, `currentLevelDeaths` reset.
6. `./DoomIIRPG --newgame` → files wiped (log `[save] removeState`), fresh map00.
7. `printf 'x' >> Doom2rpg.app/BRIEFPLAYER` (CRC now wrong), run → log names the file,
   boot falls back to a new game, no crash.
8. `DOOM2RPG_CAPTURE_TICKS=…` golden run: no file in `Doom2rpg.app` is created or modified.

**G5 — menu actions** (`core/MenuSession.{h,cpp}`, MenuSession env wiring in
`core/GameContext.cpp`): §3.8 last bullet, §3.11.
Accept (user):
1. Menu → Save Game: "Saving Game…" box for a frame, back in game, HUD "Game Saved";
   `FULLPLAYER`/`FWORLD` mtime updated.
2. Move/fight, Menu → Load Game → Yes: travel map (same planet), then the saved tile (tile
   centre, 45° facing), saved inventory, killed monsters stay dead **with corpses** (full
   record), a monster that was hurt keeps its reduced health.
3. Delete `FULLPLAYER`, save never made since → Load Game → Yes shows "No Saved Game"; Back returns.
4. Dead menu → Load Saved Game restores the last save.
5. Menu → Save & Quit → Yes: window closes; next run continues from that save.
6. Menu → Main Menu → Save Game: saves and closes; → Don't Save: closes, `FULLPLAYER` mtime unchanged.
7. Restart Level confirm from the in-game menu → level-entry state.

## 6. Review checklist

* No `Error`/`exit`/assert on any path fed by file bytes; every file read goes through
  `SaveGame::read` (size cap, CRC) and then a parser that checks `overrun()`.
* `entityDeathFunctions` and `linkIndex` writes are range-guarded.
* Byte layouts match §2.1/§2.2 and `src/Player.cpp:1351-1420` field-for-field.
* `tickSaving` calls `levelGrade` before `saveConfig` (grades are config fields).
* Golden capture runs never touch the save directory.
