#ifndef NEW_CORE_SAVEGAME_H
#define NEW_CORE_SAVEGAME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "domain/game/PlayerState.h"
#include "domain/game/WorldState.h"

namespace newcore {

class EntityDefs;
class Game;
class MapData;
class Player;
class ScriptVM;

// The single save slot on disk (ADR 0037): port of the save half of
// src/Game.cpp. Every file = legacy payload + CRC-32 trailer. The only code
// that touches the save directory.
class SaveGame {
public:
	static constexpr int kConfigVersion = 11;      // src/Game.cpp:3367-3369
	static constexpr uint32_t kConfigMarker = 0xDEADBEEF;
	static constexpr size_t kMaxFileBytes = 1u << 20;
	enum class File { FullWorld = 0, Config = 1, BriefPlayer = 2, FullPlayer = 3, BriefWorld = 4 };   // src/Game.h:28-32
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
		const int64_t* gameTime = nullptr;
		const int64_t* upTimeMs = nullptr;
	};
	void init(const Env& env);
	// false: no reads, no writes (golden runs). Payloads are still built and
	// writes report success, but every read fails: hasSavedState() is false,
	// Restart Level falls back to a fresh start, Load shows "No Saved Game"
	// and a revisited map starts fresh.
	void setDiskEnabled(bool on) { disk_ = on; }

	// Config-file state with no other owner (src/Game.cpp:1943-1948).
	short numLevelLoads[10] = {};
	bool recentBriefSave = false;                  // canvas->recentBriefSave
	bool hasSeenIntro = false;
	int totalPlayTime = 0;                         // seconds

	bool loadConfig();                             // src/Game.cpp:1977-2054
	bool saveConfig();                             // :1913-1975
	bool hasSavedState() const;                    // :2249-2268
	// :2271-2318 (saveEmptyConfig + remove 13 files). If any save file failed
	// validation since startup (or fails it now), the whole slot is first moved
	// to Doom2rpg.app/corrupt/ instead of being deleted (ADR 0037 §5); if any
	// file cannot be moved, nothing is wiped or overwritten.
	void removeState();
	// Game::saveState file half (:2059-2117); the caller already ran levelGrade for 0x10.
	// recentBriefSave = type & 0x20; config always; 0x01 player file; 0x02 world file
	// (SB_<worldMapId> if brief else FWORLD). True = every requested file written.
	bool saveState(int saveType, int worldMapId, const PlayerState::Header& h);
	bool saveLevelSnapshot(int mapId, const PlayerState::Header& h);   // :2119-2153 (no config)
	// loadState's file read (:2191-2215): FULLPLAYER (1) / BRIEFPLAYER (2). False = absent/corrupt.
	bool loadPlayer(int loadType, PlayerState::Header& h);
	// loadWorldState (:1736-1754): FWORLD if full else SB_<mapId>. False = no record applied
	// (out of range, absent, CRC, Rejected); Corrupt is logged and returns true (applied).
	bool loadWorld(int mapId, bool full);

private:
	std::string path(File f, int mapId) const;     // savePath + "/" + name; SB_<mapId>
	bool write(File f, int mapId, std::vector<uint8_t> payload);
	bool read(File f, int mapId, std::vector<uint8_t>& payload) const;    // ADR 0037 §5 rules, strips CRC
	bool exists(File f, int mapId) const;
	bool writeConfig(bool empty);
	bool quarantine();   // false = some file is still in place

	Env env_;
	WorldState world_;
	bool disk_ = true;
	mutable bool corrupt_ = false;                 // some file failed validation
	int64_t lastSaveTime_ = 0;                     // game->lastSaveTime
};

} // namespace newcore

#endif // NEW_CORE_SAVEGAME_H
