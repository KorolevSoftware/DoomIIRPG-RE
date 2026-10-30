#ifndef NEW_CORE_LOADINGMANAGER_H
#define NEW_CORE_LOADINGMANAGER_H

#include <cstdint>
#include <functional>

#include "core/GameStates.h"
#include "domain/game/WorldState.h"

namespace newcore {

class EntityDefs;
class Game;
class Hud;
class Localization;
class MapData;
class Player;
class ScriptVM;
class Tables;

// Port of src/LoadingManager.cpp's map-change half (ADR 0035): the loadMap
// gateway, EV_CHANGE_MAP effects, the ST_SAVING tick, the travel direction
// (scriptStateVars[15]) and the per-load hooks GameContext::tickLoading calls.
class LoadingManager {
public:
	static constexpr int kSaveExit = 0x2B;   // 43: player|world|exit|brief (MC §3.1)
	static constexpr int kSaveStats = 0x33;  // 51: player|world|stats|brief
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
		const Tables* tables = nullptr;   // levelNames (loading caption)
		Hud* hud = nullptr;               // "Game Saved" message
		const Localization* loc = nullptr;
		StateHost* host = nullptr;
		const int64_t* gameTime = nullptr;
		std::function<void(int menuId)> openMenu;   // used from G4b
	};
	void init(const Env& env);

	void loadMap(int mapId, bool skipSave, bool newGame);  // src/LoadingManager.cpp:55-79
	void changeMap(int b, int s);                          // src/ScriptThread.cpp:484-517
	void debugChangeMap(int mapId);                        // DEBUG (removable)
	void tickSaving();                                     // src/Canvas.cpp:853-903 subset
	void finishTravelMap();                                // finishAndLoadLevel (src/TravelMapManager.cpp:514-521)
	// loadState(2) (src/Game.cpp:2191-2215): level-entry snapshot back, reload
	// the current map. False = no snapshot yet.
	bool restartLevel();
	WorldState& world() { return world_; }

	// Load-tail hooks, called by GameContext::tickLoading phase 1, in this order.
	void onEntitiesLoaded();   // src/LoadingManager.cpp:641-643, :659-670
	void onPlayerSpawned();    // src/LoadingManager.cpp:673-677 (snapshot from G2a)
	void onLoadFinished();     // src/LoadingManager.cpp:710-713
	bool isLoadedGame() const { return isLoaded_; }   // skips snapshot + entrance event

	int lastMapID() const { return lastMapID_; }
	int nextMap() const { return nextMap_; }
	int travelLoadId() const { return tmLoad_; }    // TM_LoadLevelId (G5)
	int travelLastId() const { return tmLast_; }    // TM_LastLevelId (G5)
	int captionId() const { return caption_; }      // loading-box caption (G3)

private:
	void saveState(int saveType, int captionId);   // src/LoadingManager.cpp:47-53
	void enterTravelMap();                          // TravelMapManager::init var-15 part

	Env env_;
	WorldState world_;
	int lastMapID_ = 0;         // canvas->lastMapID; 0 at boot
	int nextMap_ = 0;            // menuSystem->LEVEL_STATS_nextMap
	int saveType_ = 0;
	int caption_ = -1;           // packed (type<<10)|index, -1 = none
	bool tmNewGame_ = false;     // travelMapManager.TM_NewGame
	bool isLoaded_ = false;      // game->isLoaded
	int activeLoadType_ = 0;     // game->activeLoadType (2 = restart, from G2a)
	short numLevelLoads_[10] = {};   // game->numLevelLoads (config file, src/Game.cpp:1946)
	int tmLoad_ = 1, tmLast_ = 0;
};

} // namespace newcore

#endif // NEW_CORE_LOADINGMANAGER_H
