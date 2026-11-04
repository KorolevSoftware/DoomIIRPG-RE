#include "core/SaveGame.h"

#include <zlib.h>

#include <cassert>
#include <cstdio>

#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "io/DataReader.h"
#include "io/DataWriter.h"
#include "platform/FileSystem.h"

namespace newcore {

namespace {

constexpr int kNumMaps = 10;
constexpr size_t kConfigBytes = 133;   // save-load.md §2.1

// Game::GetSaveFile (src/Game.cpp:3690-3750); "" = no such file.
std::string fileName(SaveGame::File f, int mapId) {
	switch (f) {
	case SaveGame::File::FullWorld: return "FWORLD";
	case SaveGame::File::Config: return "CONFIGFILENAME";
	case SaveGame::File::BriefPlayer: return "BRIEFPLAYER";
	case SaveGame::File::FullPlayer: return "FULLPLAYER";
	case SaveGame::File::BriefWorld:
		return mapId >= 1 && mapId <= kNumMaps ? "SB_" + std::to_string(mapId) : std::string();
	}
	return std::string();
}

// Every file of the slot.
template <class Fn>
void forEachFile(Fn fn) {
	fn(SaveGame::File::Config, 0);
	fn(SaveGame::File::FullWorld, 0);
	fn(SaveGame::File::FullPlayer, 0);
	fn(SaveGame::File::BriefPlayer, 0);
	for (int m = 1; m <= kNumMaps; ++m) fn(SaveGame::File::BriefWorld, m);
}

uint32_t crcOf(const std::vector<uint8_t>& b, size_t n) {
	return static_cast<uint32_t>(crc32(0L, b.data(), static_cast<uInt>(n)));
}

// ADR 0037 §3: uint32 LE crc32(payload) trailer.
void appendCrc(std::vector<uint8_t>& b) {
	const uint32_t c = crcOf(b, b.size());
	for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(c >> (8 * i)));
}

bool checkAndStripCrc(std::vector<uint8_t>& b) {
	if (b.size() < 4) return false;
	const size_t n = b.size() - 4;
	const uint32_t stored = static_cast<uint32_t>(b[n]) | static_cast<uint32_t>(b[n + 1]) << 8 |
	                        static_cast<uint32_t>(b[n + 2]) << 16 | static_cast<uint32_t>(b[n + 3]) << 24;
	if (crcOf(b, n) != stored) return false;
	b.resize(n);
	return true;
}

#ifndef NDEBUG
void crcSelfCheck() {
	const std::vector<uint8_t> payload = { 11, 0, 0, 0, 0xEF, 0xBE, 0xAD, 0xDE, 7 };
	std::vector<uint8_t> b = payload;
	appendCrc(b);
	assert(b.size() == payload.size() + 4);
	std::vector<uint8_t> ok = b;
	const bool good = checkAndStripCrc(ok);
	assert(good && ok == payload);
	(void)good;
	std::vector<uint8_t> flipped = b;
	flipped[3] ^= 0x10;
	const bool bad = checkAndStripCrc(flipped);
	assert(!bad);
	(void)bad;
	std::vector<uint8_t> tiny = { 1, 2, 3 };
	const bool small = checkAndStripCrc(tiny);
	assert(!small);
	(void)small;
	std::fprintf(stderr, "[save] crc self-check OK\n");
}
#endif

} // namespace

void SaveGame::init(const Env& env) {
	env_ = env;
	world_.init({ env.game, env.player, env.vm, env.map, env.defs, env.gameTime });
#ifndef NDEBUG
	static bool checked = false;
	if (!checked) {
		checked = true;
		crcSelfCheck();
		PlayerState::selfCheck();
	}
#endif
}

std::string SaveGame::path(File f, int mapId) const {
	return FileSystem::instance().savePath() + "/" + fileName(f, mapId);
}

bool SaveGame::exists(File f, int mapId) const {
	return !fileName(f, mapId).empty() && FileSystem::fileExists(path(f, mapId));
}

bool SaveGame::write(File f, int mapId, std::vector<uint8_t> payload) {
	if (!disk_) return true;
	const std::string name = fileName(f, mapId);
	if (name.empty()) {
		std::fprintf(stderr, "[save] no file for kind %d map %d\n", static_cast<int>(f), mapId);
		return false;
	}
	appendCrc(payload);
	if (!FileSystem::instance().ensureSaveDirectory() ||
	    !FileSystem::writeFile(path(f, mapId), payload.data(), payload.size())) {
		std::fprintf(stderr, "[save] %s: write failed\n", name.c_str());
		return false;
	}
	std::fprintf(stderr, "[save] wrote %s (%zu B)\n", name.c_str(), payload.size());
	return true;
}

bool SaveGame::read(File f, int mapId, std::vector<uint8_t>& payload) const {
	if (!disk_ || !exists(f, mapId)) return false;
	const std::string name = fileName(f, mapId);
	if (!FileSystem::readFile(path(f, mapId), payload, kMaxFileBytes)) {
		std::fprintf(stderr, "[save] %s: unreadable or over %zu B\n", name.c_str(), kMaxFileBytes);
		corrupt_ = true;
		return false;
	}
	if (!checkAndStripCrc(payload)) {
		std::fprintf(stderr, "[save] %s: bad CRC (%zu B)\n", name.c_str(), payload.size());
		corrupt_ = true;
		return false;
	}
	return true;
}

bool SaveGame::writeConfig(bool empty) {
	// saveConfig / saveEmptyConfig (src/Game.cpp:1913-1975, :2320-2370), GEC block dropped.
	const Player& p = *env_.player;
	totalPlayTime += static_cast<int>((*env_.upTimeMs - lastSaveTime_) / 1000);   // :1947
	lastSaveTime_ = *env_.upTimeMs;                                                // :1950
	DataWriter w;
	w.writeInt(kConfigVersion);
	w.writeByte(static_cast<uint8_t>(empty ? 1 : env_.game->difficulty()));
	w.writeBool(true);                                   // allowSounds
	w.writeByte(100);                                    // soundFxVolume
	w.writeByte(100);                                    // musicVolume
	w.writeBool(false);                                  // vibrateEnabled
	w.writeBool(true);                                   // enableHelp
	w.writeInt(0);                                       // defaultLanguage
	w.writeInt(empty ? 0 : p.currentLevelDeaths);
	w.writeInt(empty ? 0 : p.totalDeaths);
	w.writeInt(p.animFrames);
	w.writeInt(0);                                       // controlLayout
	w.writeInt(0);                                       // controlAlpha
	w.writeBool(false);                                  // flipControls
	for (int i = 0; i < 5 + 10; ++i) w.writeInt(0);      // help bitmasks, menu indexes
	w.writeBool(!empty && recentBriefSave);
	w.writeBool(hasSeenIntro);
	for (short n : numLevelLoads) w.writeShort(n);
	w.writeInt(totalPlayTime);
	w.writeInt(empty ? 0 : p.currentGrades);
	w.writeInt(empty ? 0 : p.bestGrades);
	w.writeUInt(kConfigMarker);
	return write(File::Config, 0, w.buffer());
}

bool SaveGame::saveConfig() { return writeConfig(false); }

bool SaveGame::loadConfig() {
	std::vector<uint8_t> payload;
	if (!read(File::Config, 0, payload)) return false;
	DataReader r(payload);
	const bool sizeOk = payload.size() == kConfigBytes;
	if (!sizeOk || r.readInt() != kConfigVersion) {
		std::fprintf(stderr, "[save] CONFIGFILENAME: bad size %zu or version\n", payload.size());
		corrupt_ = true;
		return false;
	}
	const int difficulty = r.readByte();                  // src/Game.cpp:1984
	r.skip(1 + 1 + 1 + 1 + 1 + 4);                       // allowSounds .. defaultLanguage
	const int currentLevelDeaths = r.readInt();
	const int totalDeaths = r.readInt();
	r.skip(4 + 4 + 4 + 1 + (5 + 10) * 4);                // animFrames .. menu indexes
	const bool brief = r.readBool();
	const bool seenIntro = r.readBool();
	short loads[10];
	for (short& n : loads) n = r.readShort();
	const int playTime = r.readInt();
	const int currentGrades = r.readInt();
	const int bestGrades = r.readInt();
	if (r.readUInt() != kConfigMarker) {
		std::fprintf(stderr, "[save] CONFIGFILENAME: bad marker\n");
		corrupt_ = true;
		return false;
	}
	const bool difficultyOk = difficulty == 1 || difficulty == 2 || difficulty == 4;
	if (!difficultyOk) std::fprintf(stderr, "[save] config difficulty %d invalid -> 1\n", difficulty);
	env_.game->setDifficulty(difficultyOk ? difficulty : 1);
	Player& p = *env_.player;
	p.currentLevelDeaths = currentLevelDeaths;
	p.totalDeaths = totalDeaths;
	p.currentGrades = currentGrades;
	p.bestGrades = bestGrades;
	recentBriefSave = brief;
	hasSeenIntro = seenIntro;
	for (int i = 0; i < 10; ++i) numLevelLoads[i] = loads[i];
	totalPlayTime = playTime;
	lastSaveTime_ = *env_.upTimeMs;
	return true;
}

bool SaveGame::hasSavedState() const {
	if (!disk_) return false;
	std::vector<uint8_t> payload;
	if (!read(File::Config, 0, payload) || payload.size() < 4 ||
	    DataReader(payload).readInt() != kConfigVersion) return false;   // hasConfig (:2219-2229)
	if (!exists(File::BriefPlayer, 0) || !exists(File::FullPlayer, 0)) return false;
	for (int m = 1; m <= kNumMaps; ++m)
		if (exists(File::BriefWorld, m)) return true;
	return false;
}

bool SaveGame::quarantine() {
	const std::string dir = FileSystem::instance().savePath() + "/corrupt";
	if (!FileSystem::ensureDirectory(dir)) {
		std::fprintf(stderr, "[save] cannot create %s\n", dir.c_str());
		return false;
	}
	int moved = 0, failed = 0;
	forEachFile([&](File f, int m) {
		if (!exists(f, m)) return;
		if (FileSystem::moveFile(path(f, m), dir + "/" + fileName(f, m))) ++moved;
		else ++failed;
	});
	std::fprintf(stderr, "[save] moved %d file(s) with a damaged save to %s\n", moved, dir.c_str());
	if (failed != 0) {
		std::fprintf(stderr, "[save] %d file(s) could not be moved\n", failed);
		return false;
	}
	corrupt_ = false;
	return true;
}

void SaveGame::removeState() {
	if (disk_) {
		forEachFile([&](File f, int m) {
			std::vector<uint8_t> payload;
			read(f, m, payload);                     // sets corrupt_ on a bad file
		});
		// Only files that failed to move are left: keep them, wipe nothing.
		if (corrupt_ && !quarantine()) {
			std::fprintf(stderr, "[save] removeState skipped: damaged save kept in place\n");
			return;
		}
	}
	writeConfig(true);
	if (disk_) {
		forEachFile([&](File f, int m) {
			if (f != File::Config) FileSystem::removeFile(path(f, m));
		});
	}
	std::fprintf(stderr, "[save] removeState\n");
}

bool SaveGame::saveState(int saveType, int worldMapId, const PlayerState::Header& h) {
	recentBriefSave = (saveType & 0x20) != 0;            // src/Game.cpp:2064
	const bool brief = recentBriefSave;
	bool ok = saveConfig();
	if ((saveType & 0x1) != 0) {
		DataWriter w;
		PlayerState::save(*env_.player, h, w);
		ok = write(brief ? File::BriefPlayer : File::FullPlayer, 0, w.buffer()) && ok;
	}
	if ((saveType & 0x2) != 0) {
		if (brief && (worldMapId < 1 || worldMapId > kNumMaps)) {
			std::fprintf(stderr, "[save] no world file for map %d\n", worldMapId);
			ok = false;
		} else {
			ok = write(brief ? File::BriefWorld : File::FullWorld, worldMapId, world_.save(brief)) && ok;
		}
	}
	return ok;
}

bool SaveGame::saveLevelSnapshot(int mapId, const PlayerState::Header& h) {
	DataWriter w;
	PlayerState::save(*env_.player, h, w);
	if (!write(File::BriefPlayer, 0, w.buffer())) return false;
	if (mapId < 1 || mapId > kNumMaps) return false;
	return write(File::BriefWorld, mapId, world_.save(true));
}

bool SaveGame::loadPlayer(int loadType, PlayerState::Header& h) {
	const File f = loadType == 1 ? File::FullPlayer : File::BriefPlayer;
	std::vector<uint8_t> payload;
	if (!read(f, 0, payload)) return false;
	DataReader r(payload);
	if (!PlayerState::load(r, *env_.player, h)) {
		std::fprintf(stderr, "[save] %s: bad player record\n", fileName(f, 0).c_str());
		corrupt_ = true;
		return false;
	}
	std::fprintf(stderr, "[save] read %s (map %d)\n", fileName(f, 0).c_str(), h.mapId);
	return true;
}

bool SaveGame::loadWorld(int mapId, bool full) {
	if (mapId < 1 || mapId > kNumMaps) return false;     // src/Game.cpp:1741-1743
	const File f = full ? File::FullWorld : File::BriefWorld;
	std::vector<uint8_t> payload;
	if (!read(f, mapId, payload)) return false;
	switch (world_.restore(payload)) {
	case WorldState::Restore::Applied:
		return true;
	case WorldState::Restore::Rejected:
		return false;
	case WorldState::Restore::Corrupt:
		break;
	}
	std::fprintf(stderr, "[save] %s: corrupt world record, partially applied\n", fileName(f, mapId).c_str());
	corrupt_ = true;
	return true;
}

} // namespace newcore
