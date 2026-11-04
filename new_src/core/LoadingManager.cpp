#include "core/LoadingManager.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/Sound.h"
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

void LoadingManager::boot(bool skipFrontEnd) {
	save_.loadConfig();
	if (skipFrontEnd) {
		env_.game->setDifficulty(1);
		env_.player->characterChoice = 1;
		resetForNewGame();
		startNewGameMap();
	} else {
		env_.host->requestState(StateId::Logo);   // App.cpp:119-128 -> ST_LOGO
	}
}

bool LoadingManager::startGame(bool newGame) {
	Sound::instance().soundStop();                       // src/MenuSystem.cpp:3583
	if (!newGame) {
		if (loadState(recentLoadType())) return true;
		std::fprintf(stderr, "[save] Continue failed\n");
		return false;
	}
	resetForNewGame();
	env_.host->requestState(StateId::CharacterSelect);   // (src/MenuSystem.cpp:3603)
	return true;
}

void LoadingManager::startNewGameMap() { loadMap(1, false, true); }

void LoadingManager::resetForNewGame() {
	save_.removeState();                                                  // src/MenuSystem.cpp:3587
	activeLoadType_ = 0;                                                  // :3589-3602
	isLoaded_ = false;
	env_.game->loadMapID = 0;
	lastMapID_ = 0;
	Player& p = *env_.player;
	p.reset();
	p.totalDeaths = 0;
	p.currentLevelDeaths = 0;
	p.currentGrades = 0;
}

bool LoadingManager::loadState(int loadType) {
	Game& game = *env_.game;
	Player& player = *env_.player;
	save_.saveConfig();                                                   // src/LoadingManager.cpp:42
	PlayerState::Header h;
	if (!save_.loadPlayer(loadType, h)) return false;                    // legacy: Error() + exit
	player.saveX = h.saveX;                                               // src/Game.cpp:2178-2183
	player.saveY = h.saveY;
	player.saveZ = h.saveZ;
	player.saveAngle = h.saveAngle;
	player.savePitch = h.savePitch;
	if (loadType == 2) player.currentLevelDeaths = 0;                     // src/Game.cpp:2200-2202
	const int w = player.ce.weapon;                                       // selectWeapon(ce->weapon) (src/LoadingManager.cpp:691)
	// Legacy has one weapon field; keep the rewrite's mirror in step even when
	// selectWeapon is a no-op (loaded while familiar, src/Player.cpp:135-137).
	player.weapon = w;
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
	// prevX is written from the view and ignored on read; save* is the familiar
	// body pose (canvas->save*, src/Game.cpp:2161-2165), the view when not deployed.
	const Player& p = *env_.player;
	PlayerState::Header h;
	h.mapId = mapId;
	h.viewX = p.destX;
	h.viewY = p.destY;
	h.viewAngle = p.destAngle;
	h.viewPitch = p.viewPitch;
	h.prevX = p.destX;
	h.prevY = p.destY;
	h.saveX = p.isFamiliar ? p.saveX : p.viewX;
	h.saveY = p.isFamiliar ? p.saveY : p.viewY;
	h.saveZ = p.isFamiliar ? p.saveZ : p.viewZ;
	h.saveAngle = p.isFamiliar ? p.saveAngle : p.viewAngle;
	h.savePitch = p.isFamiliar ? p.savePitch : p.viewPitch;
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
	Sound::instance().soundStop();                                        // :69
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
		env_.backToMain();                             // backToMain(false) (src/Canvas.cpp:879-880)
	} else if (type & 0x40) {
		env_.quit();                                   // app->shutdown() (src/Canvas.cpp:882-883)
	} else if (type & 0x08) {
		enterTravelMap();                              // src/Canvas.cpp:885-886
	} else if (type & 0x10) {                          // src/Canvas.cpp:888-890
		Sound::instance().playSound(1069, 1, 3);       // Music_Level_End
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
	// A deployed player needs its body back as a linked drop entity (the world
	// record restores it); otherwise come back as the player, not the bot.
	Player& p = *env_.player;
	EntityDb& db = env_.game->db;
	if (p.isFamiliar) {
		const int idx = p.playerEntityCopyIndex;
		const bool inRing = idx >= db.firstDropIndex && idx < db.firstDropIndex + EntityDb::kMaxDropEntities &&
		                    idx < EntityDb::kEntities;
		if (!inRing || db.entities()[idx].def == nullptr || (db.entities()[idx].info & Entity::kInfoLinked) == 0) {
			std::fprintf(stderr, "[save] familiar body %d missing, loading as the player\n", idx);
			p.isFamiliar = false;
			p.familiarType = 0;
			p.playerEntityCopyIndex = -1;
			p.swapStatusEffects();    // the copy side holds the player's own effects
			p.translateStatusEffects();
			// Spawn at the hero's pose (save*), not the bot's saved view; the
			// spawnPlayer that follows places and relinks the player from it.
			int& sp = env_.game->spawnParam;
			sp = ((p.saveX >> 6) & 0x1F) | (((p.saveY >> 6) & 0x1F) << 5) | (((p.saveAngle & 0x3FF) >> 7) << 10);
			if (sp == 0) sp = -1;                                         // 0 = header spawn (src/Game.cpp:946)
			p.viewPitch = p.savePitch;
		}
	}
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
