#ifndef NEW_CORE_LOADINGMANAGER_H
#define NEW_CORE_LOADINGMANAGER_H

#include <cstdint>
#include <functional>

#include "core/GameStates.h"
#include "core/SaveGame.h"

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
		const int64_t* upTimeMs = nullptr;
		std::function<void(int menuId)> openMenu;
		std::function<void()> quit;       // app->shutdown() (Save & Quit, src/Canvas.cpp:882-883)
		std::function<void()> backToMain; // GameContext::backToMain_ (src/Canvas.cpp:1372-1407)
	};
	void init(const Env& env);

	void boot(bool skipFrontEnd);       // App.cpp:119-128 loadConfig, then the front end or the dev/capture bypass (ADR 0043 §3)
	bool startGame(bool newGame);       // MenuSystem::startGame (src/MenuSystem.cpp:3571-3609); false = no usable save (Continue)
	void startNewGameMap();             // disposeIntro: loadMap(startupMap = 1, false, true) (src/IntroSequenceManager.cpp:131-136)
	// LoadingManager::loadState + Game::loadState collapsed (src/LoadingManager.cpp:38-45,
	// src/Game.cpp:2191-2215, deviation SL-D1). False = no usable player file, nothing changed.
	bool loadState(int loadType);
	int recentLoadType() const { return save_.recentBriefSave ? 2 : 1; }   // src/LoadingManager.cpp:753-760
	void requestSave(int saveType);                        // canvas->saveState(type, 3, 196) from the menu
	SaveGame& save() { return save_; }

	void loadMap(int mapId, bool skipSave, bool newGame);  // src/LoadingManager.cpp:55-79
	void changeMap(int b, int s);                          // src/ScriptThread.cpp:484-517
	void debugChangeMap(int mapId);                        // DEBUG (removable)
	void tickSaving();                                     // src/Canvas.cpp:853-903
	void finishTravelMap();                                // finishAndLoadLevel (src/TravelMapManager.cpp:514-521)

	// Load-tail hooks, called by GameContext::tickLoading phase 1, in this order.
	void onEntitiesLoaded();   // src/LoadingManager.cpp:641-643, :659-670
	void onPlayerSpawned();    // src/LoadingManager.cpp:675-688
	void onLoadFinished();     // src/LoadingManager.cpp:710-713
	bool isLoadedGame() const { return isLoaded_; }   // skips snapshot + entrance event

	int lastMapID() const { return lastMapID_; }
	int nextMap() const { return nextMap_; }
	int travelLoadId() const { return tmLoad_; }    // TM_LoadLevelId (G5)
	int travelLastId() const { return tmLast_; }    // TM_LastLevelId (G5)
	int captionId() const { return caption_; }      // loading-box caption (G3)

private:
	void resetForNewGame();                         // startGame(true) body minus the state change (src/MenuSystem.cpp:3584-3602)
	void saveState(int saveType, int captionId);   // src/LoadingManager.cpp:47-53
	void enterTravelMap();                          // TravelMapManager::init var-15 part
	PlayerState::Header headerAtPlayer(int mapId) const;
	PlayerState::Header headerAtSpawn(int mapId, bool zeroIfNone) const;

	Env env_;
	SaveGame save_;
	int lastMapID_ = 0;         // canvas->lastMapID; 0 at boot
	int nextMap_ = 0;            // menuSystem->LEVEL_STATS_nextMap
	int saveType_ = 0;
	int caption_ = -1;           // packed (type<<10)|index, -1 = none
	bool tmNewGame_ = false;     // travelMapManager.TM_NewGame
	bool isLoaded_ = false;      // game->isLoaded
	int activeLoadType_ = 0;     // game->activeLoadType: 1 full, 2 brief
	int tmLoad_ = 1, tmLast_ = 0;
};

} // namespace newcore

#endif // NEW_CORE_LOADINGMANAGER_H
