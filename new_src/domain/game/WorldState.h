#ifndef NEW_DOMAIN_GAME_WORLDSTATE_H
#define NEW_DOMAIN_GAME_WORLDSTATE_H

#include <cstdint>
#include <vector>

namespace newcore {

class EntityDefs;
class Game;
class MapData;
class Player;
class ScriptVM;

// World-record codec (ADR 0034, ADR 0037 §4): the legacy saveWorldState /
// loadWorldState layout of the loaded map. Storage is SaveGame's.
class WorldState {
public:
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
		const int64_t* gameTime = nullptr;
	};
	void init(const Env& env);

	// saveWorldState(OS, brief) of the LOADED map (src/Game.cpp:1576-1734).
	// Accumulates totalLevelTime.
	std::vector<uint8_t> save(bool brief);
	enum class Restore { Applied, Rejected, Corrupt };
	// loadWorldState(IS) (src/Game.cpp:1756-1909). Rejected = empty blob or
	// compile-date mismatch (nothing applied). Corrupt = overrun / bad index /
	// lerp count != 0, possibly partially applied.
	Restore restore(const std::vector<uint8_t>& blob);

private:
	Env env_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_WORLDSTATE_H
