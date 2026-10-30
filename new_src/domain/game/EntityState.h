#ifndef NEW_DOMAIN_GAME_ENTITYSTATE_H
#define NEW_DOMAIN_GAME_ENTITYSTATE_H

namespace newcore {

class DataReader;
class DataWriter;
class EntityDefs;
class Game;
class MapData;

// Entity records of the world state (ADR 0034 field 7, ADR 0037): port of
// Game::saveEntityStates(OS, b) / loadEntityStates and the Entity
// save-handle, saveState, loadState and restoreBinaryState helpers.
namespace EntityState {

struct Env {
	Game* game = nullptr;
	MapData* map = nullptr;
	const EntityDefs* defs = nullptr;
};

void save(const Env& env, DataWriter& w, bool brief);   // Game::saveEntityStates(OS, b) (src/Game.cpp:3379-3395)
// Game::loadEntityStates (src/Game.cpp:3371-3377). False = corrupt record
// (bad entity index or linkIndex), possibly partially applied.
bool load(const Env& env, DataReader& r);

} // namespace EntityState

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ENTITYSTATE_H
