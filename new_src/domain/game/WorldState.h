#ifndef NEW_DOMAIN_GAME_WORLDSTATE_H
#define NEW_DOMAIN_GAME_WORLDSTATE_H

#include <cstdint>
#include <vector>

#include "domain/game/Player.h"

namespace newcore {

class EntityDefs;
class Game;
class MapData;
class ScriptVM;

// Per-map world state, ADR 0034: one byte blob per mapID in the legacy brief
// saveWorldState layout, plus the level-entry player snapshot.
class WorldState {
public:
	static constexpr int kNumMaps = 10;
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
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

} // namespace newcore

#endif // NEW_DOMAIN_GAME_WORLDSTATE_H
