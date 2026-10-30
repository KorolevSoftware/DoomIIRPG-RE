#ifndef NEW_DOMAIN_GAME_ENTITYSTATE_H
#define NEW_DOMAIN_GAME_ENTITYSTATE_H

namespace newcore {

class DataReader;
class DataWriter;
class EntityDefs;
class Game;
class MapData;

// Entity records of the brief world state (ADR 0034, field 7): port of
// Game::saveEntityStates(OS, true) / loadEntityStates and the Entity
// save-handle, saveState, loadState and restoreBinaryState helpers.
namespace EntityState {

struct Env {
	Game* game = nullptr;
	MapData* map = nullptr;
	const EntityDefs* defs = nullptr;
};

void save(const Env& env, DataWriter& w);   // Game::saveEntityStates(OS, true) (src/Game.cpp:3379-3395)
bool load(const Env& env, DataReader& r);   // Game::loadEntityStates (src/Game.cpp:3371-3377)

} // namespace EntityState

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ENTITYSTATE_H
