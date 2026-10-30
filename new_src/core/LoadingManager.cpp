#include "core/LoadingManager.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "io/Localization.h"
#include "io/MenuData.h"
#include "io/Tables.h"
#include "ui/Hud.h"

namespace newcore {

namespace {
// Localization::makeId without an instance: packed (type<<10)|index.
int stringId(int type, int index) { return (type << Localization::kStringIdShift) | index; }
} // namespace

void LoadingManager::init(const Env& env) {
	env_ = env;
	caption_ = stringId(kTextMain, 41);   // new game reaches ST_LOADING via finishAndLoadLevel (src/TravelMapManager.cpp:519)
	world_.init({ env.game, env.player, env.vm, env.map, env.defs, env.gameTime });
}

void LoadingManager::loadMap(int mapId, bool skipSave, bool newGame) {
	if (mapId >= 1 && mapId <= 10 && numLevelLoads_[mapId - 1]++ == 0)
		env_.player->currentLevelDeaths = 0;                              // :59-66
	lastMapID_ = env_.game->loadMapID;                                    // :67-68
	env_.game->loadMapID = mapId;
	std::fprintf(stderr, "[sound] stop\n");                               // :69 soundStop
	tmNewGame_ = newGame;                                                 // :70-71
	std::fprintf(stderr, "[map] loadMap %d (last %d)\n", mapId, lastMapID_);
	if (!skipSave && activeLoadType_ == 0 && lastMapID_ >= 1 && lastMapID_ <= 10) {
		saveState(kSaveExit, stringId(kTextIngame2, 196));                // :72-73 "Saving Game..."
	} else {
		const std::vector<int16_t>& names = env_.tables->levelNames;   // :75-77
		if (mapId >= 1 && mapId <= static_cast<int>(names.size())) caption_ = stringId(kTextIngame2, names[mapId - 1]);
		enterTravelMap();
	}
}

void LoadingManager::changeMap(int b, int s) {
	env_.player->completedLevels |= 1 << (env_.game->loadMapID - 1);     // :489
	env_.game->spawnParam = (((b >> 4) & 7) << 10) | (s & 0x3FF);        // :490
	if (env_.game->spawnParam == 0) env_.game->spawnParam = -1;           // 0 = header spawn (src/Game.cpp:946)
	nextMap_ = b & 0xF;                                                   // :491
	const bool showStats = nextMap_ >= env_.game->loadMapID;              // :488, :492-494
	env_.game->snapAllMovers();                                           // :495
	if (b & 0x80)                                                         // :496-507, deviation MD1
		std::fprintf(stderr, "[map] CHANGE_MAP fade bit ignored (no shipping site sets it)\n");
	if (showStats) saveState(kSaveStats, stringId(kTextIngame2, 194));   // :509-510 (194: data quirk, UI19.1)
	else loadMap(nextMap_, false, false);                                 // :512-513
	// changeMapStarted (:515): no flag needed, the state is no longer Playing,
	// so GameContext::tick drops gameplay input.
}

void LoadingManager::debugChangeMap(int mapId) {
	// DEBUG (removable): legacy debug writes spawnParam = 0, its "none"
	// (src/MenuSystem.cpp:3130-3133); the rewrite's "none" is -1.
	changeMap(mapId & 0xF, 0);
	env_.game->spawnParam = -1;
}

void LoadingManager::saveState(int saveType, int captionId) {
	saveType_ = saveType;
	caption_ = captionId;
	env_.host->requestState(StateId::Saving);
}

void LoadingManager::tickSaving() {
	const int type = saveType_;
	saveType_ = 0;
	// 0x10 saves the current map, 0x08 the one just left (loadMap already advanced loadMapID; MC §3.1).
	world_.saveLevel((type & 0x10) ? env_.game->loadMapID : lastMapID_);
	if (type & 0x10) env_.game->levelGrade(true);      // commits before the menu is built (src/Game.cpp:2069-2071)
	env_.hud->addMessage(env_.loc->get(kTextMain, 38), 0xAA000000, 0);   // "Game Saved" (src/Canvas.cpp:873)
	// Deviation MD2: the leave-time BRIEFPLAYER write and clearEvents are
	// skipped (overwritten by the entry snapshot / nothing queued).
	if (type & 0x08) {
		enterTravelMap();                              // src/Canvas.cpp:885-886
	} else if (type & 0x10) {                          // src/Canvas.cpp:888-890
		std::fprintf(stderr, "[sound] 1069\n");        // Music_Level_End
		env_.openMenu(kMenuLevelStats);
	}
}

void LoadingManager::enterTravelMap() {
	// src/TravelMapManager.cpp:23-43. getRecentLoadType() == 1 only after a
	// full save (no save system yet), so the last id is always lastMapID.
	tmLoad_ = std::clamp(env_.game->loadMapID, 1, 9);
	tmLast_ = std::clamp(lastMapID_, 0, 9);
	if (tmLast_ == 0 && tmLoad_ > 1) tmLast_ = tmLoad_ - 1;
	env_.vm->vars[15] = static_cast<short>(tmLoad_ > tmLast_ ? 0 : (tmLoad_ == tmLast_ ? 1 : 2));
	env_.host->requestState(StateId::TravelMap);
}

void LoadingManager::finishTravelMap() {
	caption_ = stringId(kTextMain, 41);   // "Loading..." (src/TravelMapManager.cpp:519)
	env_.host->requestState(StateId::Loading);
}

void LoadingManager::onEntitiesLoaded() {
	if (env_.game->loadMapID <= 10 && env_.game->loadMapID > env_.player->highestMap)
		env_.player->highestMap = env_.game->loadMapID;                   // :641-643
	env_.game->curLevelTime = *env_.gameTime;                             // playTime = curLevelTime (:663)
	env_.player->levelInit();                                             // :667
	if (!world_.restoreLevel(env_.game->loadMapID))                       // loadWorldState (:670)
		env_.game->totalLevelTime = 0;                                    // src/Game.cpp:1751-1753
}

void LoadingManager::onPlayerSpawned() {
	// saveLevelSnapshot (:675-677), before static func 0: the pre-script entry state.
	if (!isLoaded_) {
		world_.savePlayer();
		world_.saveLevel(env_.game->loadMapID);
	}
}

bool LoadingManager::restartLevel() {
	if (!world_.restorePlayer()) return false;
	const Player& p = *env_.player;
	env_.game->spawnParam = -1;                                           // loadPlayerState (src/Game.cpp:2189)
	if (p.viewX != 0 && p.viewY != 0) {                                  // src/Game.cpp:2205-2208
		env_.game->spawnParam = ((p.viewX >> 6) & 0x1F) | (((p.viewY >> 6) & 0x1F) << 5) |
		                        (((p.viewAngle & 0x3FF) >> 7) << 10);
		if (env_.game->spawnParam == 0) env_.game->spawnParam = -1;       // 0 = header spawn (src/Game.cpp:946)
	}
	activeLoadType_ = 2;                                                  // :2209-2212
	isLoaded_ = true;
	loadMap(env_.game->loadMapID, false, false);   // no save (activeLoadType != 0) -> var 15 = 1 -> Loading
	return true;
}

void LoadingManager::onLoadFinished() {
	isLoaded_ = false;                                                    // :710-713
	activeLoadType_ = 0;
}

} // namespace newcore
