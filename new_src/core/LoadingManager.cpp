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
	save_.init({ env.game, env.player, env.vm, env.map, env.defs, env.gameTime, env.upTimeMs });
}

void LoadingManager::boot(bool forceNewGame) {
	save_.loadConfig();
	startGame(forceNewGame || !save_.hasSavedState());
}

void LoadingManager::startGame(bool newGame) {
	if (!newGame) {
		if (loadState(recentLoadType())) return;
		std::fprintf(stderr, "[save] Continue failed -> new game\n");
	}
	save_.removeState();                                                  // src/MenuSystem.cpp:3587
	activeLoadType_ = 0;                                                  // :3589-3602
	isLoaded_ = false;
	env_.game->loadMapID = 0;
	lastMapID_ = 0;
	Player& p = *env_.player;
	const int bestGrades = p.bestGrades;                                  // legacy reset() leaves it
	p.reset();
	p.bestGrades = bestGrades;
	p.totalDeaths = 0;
	p.currentLevelDeaths = 0;
	p.currentGrades = 0;
	loadMap(1, false, true);   // character select + intro skipped: startupMap 1 (src/IntroSequenceManager.cpp:135)
}

bool LoadingManager::loadState(int loadType) {
	Game& game = *env_.game;
	Player& player = *env_.player;
	save_.saveConfig();                                                   // src/LoadingManager.cpp:42
	PlayerState::Header h;
	if (!save_.loadPlayer(loadType, h)) return false;                    // legacy: Error() + exit
	if (loadType == 2) player.currentLevelDeaths = 0;                     // src/Game.cpp:2200-2202
	const int w = player.ce.weapon;                                       // selectWeapon(ce->weapon) (src/LoadingManager.cpp:691)
	player.selectWeapon(w, w >= 0 ? env_.defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, w) : nullptr);
	game.spawnParam = -1;                                                 // loadPlayerState (src/Game.cpp:2189)
	if (h.viewX != 0 && h.viewY != 0) {                                  // src/Game.cpp:2205-2208
		game.spawnParam = ((h.viewX >> 6) & 0x1F) | (((h.viewY >> 6) & 0x1F) << 5) |
		                  (((h.viewAngle & 0x3FF) >> 7) << 10);
		if (game.spawnParam == 0) game.spawnParam = -1;                   // 0 = header spawn (src/Game.cpp:946)
	}
	activeLoadType_ = loadType;
	isLoaded_ = true;
	loadMap(h.mapId, false, false);   // no save (activeLoadType != 0) -> TravelMap
	return true;
}

void LoadingManager::requestSave(int saveType) { saveState(saveType, stringId(kTextIngame2, 196)); }

PlayerState::Header LoadingManager::headerAtPlayer(int mapId) const {
	// src/Canvas.cpp:871, src/LoadingManager.cpp:686-688, src/Game.cpp:2124-2137.
	// prevX/saveX are familiar-only canvas fields: written from the view, ignored on read.
	const Player& p = *env_.player;
	PlayerState::Header h;
	h.mapId = mapId;
	h.viewX = p.destX;
	h.viewY = p.destY;
	h.viewAngle = p.destAngle;
	h.viewPitch = p.viewPitch;
	h.prevX = p.destX;
	h.prevY = p.destY;
	h.saveX = p.viewX;
	h.saveY = p.viewY;
	h.saveZ = p.viewZ;
	h.saveAngle = p.viewAngle;
	h.savePitch = p.viewPitch;
	return h;
}

PlayerState::Header LoadingManager::headerAtSpawn(int mapId, bool zeroIfNone) const {
	// src/Canvas.cpp:855-868; spawnParam -1 is the legacy 0 (ADR 0035).
	PlayerState::Header h;
	h.mapId = mapId;
	const int sp = env_.game->spawnParam;
	if (zeroIfNone && sp == -1) return h;
	const int p = std::max(sp, 0);
	const int x = 32 + (p & 0x1F) * 64;
	const int y = 32 + ((p >> 5) & 0x1F) * 64;
	h.viewX = h.prevX = h.saveX = x;
	h.viewY = h.prevY = h.saveY = y;
	h.viewAngle = ((p >> 10) & 0xFF) << 7;
	h.saveZ = 36;
	return h;
}

void LoadingManager::loadMap(int mapId, bool skipSave, bool newGame) {
	if (mapId >= 1 && mapId <= 10 && save_.numLevelLoads[mapId - 1]++ == 0)
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
	Game& game = *env_.game;
	if (type & 0x10) game.levelGrade(true);            // before saveConfig: grades are config fields (src/Game.cpp:2069-2071)
	// 0x08 saves the map just left (loadMap already advanced loadMapID; MC §3.1),
	// 0x10 and manual saves the current one (src/Canvas.cpp:854-871).
	const PlayerState::Header h = (type & 0x08) ? headerAtSpawn(game.loadMapID, true)
	                            : (type & 0x10) ? headerAtSpawn(nextMap_, false)
	                            :                 headerAtPlayer(game.loadMapID);
	const int worldMap = (type & 0x08) ? lastMapID_ : game.loadMapID;
	if (save_.saveState(type, worldMap, h))
		env_.hud->addMessage(env_.loc->get(kTextMain, 38), 0xAA000000, 0);   // "Game Saved" (src/Canvas.cpp:873)
	else
		std::fprintf(stderr, "[save] save FAILED (type 0x%X)\n", type);    // legacy Error(48); deviation SL-D2
	// clearEvents(1) (:899): nothing queued in the rewrite.
	if (type & 0x04) {
		env_.quit();                                   // backToMain stand-in (ADR 0037 §6)
	} else if (type & 0x40) {
		env_.quit();                                   // app->shutdown()
	} else if (type & 0x08) {
		enterTravelMap();                              // src/Canvas.cpp:885-886
	} else if (type & 0x10) {                          // src/Canvas.cpp:888-890
		std::fprintf(stderr, "[sound] 1069\n");        // Music_Level_End
		env_.openMenu(kMenuLevelStats);
	} else {
		if (type & 0x100) std::fprintf(stderr, "[save] MENU_END_FINALQUIT not ported\n");
		env_.host->requestState(StateId::Playing);     // 0x80 returnToGame (:894-897)
	}
}

void LoadingManager::enterTravelMap() {
	// src/TravelMapManager.cpp:23-43.
	tmLoad_ = std::clamp(env_.game->loadMapID, 1, 9);
	tmLast_ = (recentLoadType() == 1 && !tmNewGame_) ? tmLoad_ : std::clamp(lastMapID_, 0, 9);
	if (tmLast_ == 0 && tmLoad_ > 1) tmLast_ = tmLoad_ - 1;
	env_.vm->vars[15] = static_cast<short>(tmLoad_ > tmLast_ ? 0 : (tmLoad_ == tmLast_ ? 1 : 2));
	env_.host->requestState(StateId::TravelMap);
}

void LoadingManager::finishTravelMap() {
	// "Game Loaded" / "Loading..." (src/LoadingManager.cpp:645-650, src/TravelMapManager.cpp:519)
	caption_ = stringId(kTextMain, isLoaded_ ? 39 : 41);
	env_.host->requestState(StateId::Loading);
}

void LoadingManager::onEntitiesLoaded() {
	if (env_.game->loadMapID <= 10 && env_.game->loadMapID > env_.player->highestMap)
		env_.player->highestMap = env_.game->loadMapID;                   // :641-643
	env_.game->curLevelTime = *env_.gameTime;                             // playTime = curLevelTime (:663)
	env_.player->levelInit();                                             // :667
	// loadWorldState (:670, src/Game.cpp:1736-1754). A corrupt record is logged
	// by SaveGame and left partially applied (ADR 0037 §5).
	if (!save_.loadWorld(env_.game->loadMapID, activeLoadType_ == 1))
		env_.game->totalLevelTime = 0;
}

void LoadingManager::onPlayerSpawned() {
	// :675-688, before static func 0: the pre-script entry state.
	const int mapId = env_.game->loadMapID;
	if (!isLoaded_) save_.saveLevelSnapshot(mapId, headerAtPlayer(mapId));
	if (!isLoaded_ && !save_.hasSavedState()) save_.saveState(3, mapId, headerAtPlayer(mapId));   // first map of a new game
}

void LoadingManager::onLoadFinished() {
	isLoaded_ = false;                                                    // :710-713
	activeLoadType_ = 0;
}

} // namespace newcore
