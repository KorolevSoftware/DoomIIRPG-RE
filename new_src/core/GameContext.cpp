#include "core/GameContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/AppContext.h"
#include "core/Sound.h"
#include "domain/game/Automap.h"
#include "domain/game/DialogSystem.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "render/Graphics2D.h"
#include "render/ParticleRenderer.h"
#include "render/World3D.h"
#include "render/api/RenderBackend.h"
#include "text/Font.h"
#include "ui/AutomapView.h"
#include "ui/CharacterSelectView.h"
#include "ui/DialogView.h"
#include "ui/Hud.h"
#include "ui/HudView.h"
#include "ui/IntroMovieView.h"
#include "ui/LoadingView.h"
#include "ui/LootView.h"
#include "ui/TravelMapView.h"
#include "ui/MainMenuView.h"
#include "ui/MenuView.h"
#include "ui/PrologueView.h"
#include "ui/Ui.h"

namespace newcore {

// ---- setup ----

void GameContext::init(const Init& sys) {
	sys_ = sys;
	CinematicCamera::Env cinEnv;
	cinEnv.map = sys_.map;
	cinEnv.player = sys_.player;
	cinEnv.vm = sys_.vm;
	cinEnv.host = this;
	cinEnv.gameTime = &gameTime;
	cinEnv.particles = &sys_.game->particles;
	cinematic_.init(cinEnv);
	LootSession::Env lootEnv;
	lootEnv.map = sys_.map;
	lootEnv.game = sys_.game;
	lootEnv.player = sys_.player;
	lootEnv.loc = sys_.loc;
	lootEnv.font = sys_.font;
	lootEnv.tables = sys_.tables;
	lootEnv.host = this;
	lootEnv.upTimeMs = &upTimeMs;
	loot_.init(lootEnv);
	MenuSession::Env menuEnv;
	menuEnv.menus = sys_.menus;
	menuEnv.loc = sys_.loc;
	menuEnv.tables = sys_.tables;
	menuEnv.defs = sys_.defs;
	menuEnv.player = sys_.player;
	menuEnv.game = sys_.game;
	menuEnv.host = this;
	menuEnv.upTimeMs = &upTimeMs;
	menuEnv.map = sys_.map;
	menuEnv.gameTime = &gameTime;
	menuEnv.restartLevel = [this] { restartLevel(); };
	menuEnv.saveGame = [this](int type) { loading_.requestSave(type); };
	menuEnv.loadGame = [this] { return loadGame(loading_.recentLoadType()); };
	menuEnv.backToMain = [this] { backToMain_(); };
	menuEnv.startGame = [this](bool n) { return loading_.startGame(n); };
	menuEnv.hasSavedState = [this] { return loading_.save().hasSavedState(); };
	menuEnv.quit = [this] { quitRequested_ = true; };
	menuEnv.continueLevelStats = [this] { loading_.loadMap(loading_.nextMap(), true, false); };
	menuEnv.vending = &minigames_.vending();
	menuEnv.returnToPlayer = [this] { familiar_.returnsToPlayer(false); };
	menuEnv.playMiniGame = [this](int type) { minigames_.playFromMainMenu(type); };
	menu_.init(menuEnv);
	Targeting::Env tgtEnv;
	tgtEnv.game = sys_.game;
	tgtEnv.player = sys_.player;
	tgtEnv.map = sys_.map;
	tgtEnv.tables = sys_.tables;
	tgtEnv.hud = sys_.hud;
	tgtEnv.view = scene_.camera().viewInt();   // stable member array, refreshed by each world pass
	targeting_.init(tgtEnv);
	ViewWeapon::Env wpEnv;
	wpEnv.world = sys_.world;
	wpEnv.media = sys_.media;
	viewWeapon_.init(wpEnv);
	SceneRenderer::Env sceneEnv;
	sceneEnv.map = sys_.map;
	sceneEnv.media = sys_.media;
	sceneEnv.world = sys_.world;
	sceneEnv.game = sys_.game;
	sceneEnv.player = sys_.player;
	sceneEnv.hud = sys_.hud;
	sceneEnv.g2d = sys_.g2d;
	sceneEnv.tables = sys_.tables;
	sceneEnv.upTimeMs = &upTimeMs;
	scene_.init(sceneEnv);                     // takes over camera_.setSinTable (spec §P1-G6)
	PlayerActions::Env actEnv;
	actEnv.game = sys_.game;
	actEnv.player = sys_.player;
	actEnv.vm = sys_.vm;
	actEnv.map = sys_.map;
	actEnv.hud = sys_.hud;
	actEnv.loc = sys_.loc;
	actEnv.targeting = &targeting_;
	actEnv.host = this;
	actEnv.zoom = &zoom_;
	actEnv.practice = &practice_;
	actEnv.familiar = &familiar_;
	actEnv.openMenu = [this](int id) { nextMenuId_ = id; setState(StateId::Menu); };
	actEnv.weaponSelectOpen = [this] { return hudButtons_.weaponSelectOpen(); };
	actions_.init(actEnv);
	ZoomController::Env zoomEnv;
	zoomEnv.game = sys_.game;
	zoomEnv.player = sys_.player;
	zoomEnv.hud = sys_.hud;
	zoomEnv.loc = sys_.loc;
	zoomEnv.tables = sys_.tables;
	zoomEnv.actions = &actions_;
	zoomEnv.host = this;
	zoomEnv.upTimeMs = &upTimeMs;
	zoomEnv.gameTime = &gameTime;
	zoomEnv.inTargetPractice = [this] { return practice_.active(); };
	zoom_.init(zoomEnv);
	TargetPractice::Env tpEnv;
	tpEnv.game = sys_.game;
	tpEnv.player = sys_.player;
	tpEnv.hud = sys_.hud;
	tpEnv.loc = sys_.loc;
	tpEnv.map = sys_.map;
	tpEnv.media = sys_.media;
	tpEnv.vm = sys_.vm;
	tpEnv.dialogs = sys_.dialogs;
	tpEnv.actions = &actions_;
	tpEnv.zoom = &zoom_;
	tpEnv.host = this;
	tpEnv.cinematic = &cinematic_;
	tpEnv.upTimeMs = &upTimeMs;
	practice_.init(tpEnv);
	Familiar::Env famEnv;
	famEnv.game = sys_.game;
	famEnv.player = sys_.player;
	famEnv.vm = sys_.vm;
	famEnv.map = sys_.map;
	famEnv.hud = sys_.hud;
	famEnv.loc = sys_.loc;
	famEnv.defs = sys_.defs;
	famEnv.dialogs = sys_.dialogs;
	famEnv.actions = &actions_;
	famEnv.host = this;
	famEnv.cinematic = &cinematic_;
	famEnv.upTimeMs = &upTimeMs;
	familiar_.init(famEnv);
	MiniGameHost::Env mgEnv;
	mgEnv.vm = sys_.vm;
	mgEnv.host = this;
	mgEnv.game = sys_.game;
	mgEnv.player = sys_.player;
	mgEnv.tables = sys_.tables;
	mgEnv.loc = sys_.loc;
	mgEnv.defs = sys_.defs;
	mgEnv.upTimeMs = &upTimeMs;
	mgEnv.hud = sys_.hud;
	mgEnv.dialogs = sys_.dialogs;
	mgEnv.openMenu = [this](int id) { nextMenuId_ = id; setState(StateId::Menu); };
	minigames_.init(mgEnv);
	LoadingManager::Env loadEnv;
	loadEnv.game = sys_.game;
	loadEnv.player = sys_.player;
	loadEnv.vm = sys_.vm;
	loadEnv.map = sys_.map;
	loadEnv.defs = sys_.defs;
	loadEnv.tables = sys_.tables;
	loadEnv.hud = sys_.hud;
	loadEnv.loc = sys_.loc;
	loadEnv.host = this;
	loadEnv.gameTime = &gameTime;
	loadEnv.upTimeMs = &upTimeMs;
	loadEnv.openMenu = [this](int id) { nextMenuId_ = id; setState(StateId::Menu); };
	loadEnv.quit = [this] { quitRequested_ = true; };
	loadEnv.backToMain = [this] { backToMain_(); };
	loading_.init(loadEnv);
	TravelMapSession::Env travelEnv;
	travelEnv.upTimeMs = &upTimeMs;
	travelEnv.art = sys_.ui != nullptr ? &sys_.ui->art() : nullptr;
	travelEnv.tables = sys_.tables;
	travel_.init(travelEnv);
	CharacterSelectSession::Env charEnv;
	charEnv.player = sys_.player;
	charEnv.game = sys_.game;
	charEnv.loc = sys_.loc;
	charEnv.tables = sys_.tables;
	charEnv.host = this;
	charEnv.upTimeMs = &upTimeMs;
	charEnv.backToMain = [this] { backToMain_(); };
	charSelect_.init(charEnv);
	PrologueSession::Env introEnv;
	introEnv.player = sys_.player;
	introEnv.loc = sys_.loc;
	introEnv.host = this;
	prologue_.init(introEnv);
	IntroMovieSession::Env movieEnv;
	movieEnv.tables = sys_.tables;
	movieEnv.loc = sys_.loc;
	movieEnv.upTimeMs = &upTimeMs;
	movieEnv.exit = [this] { exitIntroMovie_(); };
	introMovie_.init(movieEnv);
	credits_.init({ sys_.loc, &upTimeMs });
	if (sys_.tables) {
		sys_.game->lerps.setSinTable(&sys_.tables->sinTable);   // parabola lerp arc (SpriteLerps.h)
	}
}

// ---- state machine ----

void GameContext::setState(StateId s) {
	// Dialog-close prior-state restore (src/DialogSystem.cpp:541-554):
	// closeDialog always asks back for ST_PLAYING; redirect while an
	// inter-cinematic/cinematic context survives, INTER_CAMERA > CAMERA
	// priority. dialogPrevState_ latches the pre-dialog state at entry.
	if (s == StateId::Playing && state_ == StateId::Dialog) {
		if (dialogPrevState_ == StateId::InterCamera) s = StateId::InterCamera;
		else if (cinematic_.active()) s = StateId::Camera;
	}
	stateChanged = true;                                   // src/Canvas.cpp:1024
	for (int& v : stateVars) v = 0;                        // sub-state timelines rebuilt per entry (:1024-1026)
	exitState_();
	oldState = state_;
	state_ = s;                                             // :1052-1053
	enterState_(s);
}

void GameContext::exitState_() {
	// Legacy exit hooks live here: ST_AUTOMAP unpauses the player,
	// ST_MENU unpauses + clears the menu stack, ST_CAMERA re-enables render
	// activation + clears skippingCinematic (src/Canvas.cpp:1030-1049).
	if (state_ == StateId::Camera) cinematic_.clearSkipRequest();  // (:1037-1040)
}

void GameContext::enterState_(StateId s) {
	switch (s) {
	case StateId::Playing:
		// Clear pending input actions + stamp the turn clock
		// (src/Canvas.cpp:1101-1106 analog).
		pendingActions_.clear();
		lastTurnTime_ = upTimeMs;
		sys_.game->facingDirty = true;         // ST_PLAYING entry latch (src/Canvas.cpp:1075)
		break;
	case StateId::InterCamera:
		break; // no entry side effects (no dedicated ST_INTER_CAMERA handler exists)
	case StateId::Automap:
		// No queue clear, no side effects: entry only resets automapDrawn /
		// automapTime (src/Canvas.cpp:1188-1190).
		break;
	case StateId::Dialog:
		zoom_.drop();                          // src/Canvas.cpp:1085-1091
		// ST_DIALOG entry: clear soft keys + events (src/Canvas.cpp:1110-1111);
		// soft keys are implicit in the rewrite, events are the queued actions.
		pendingActions_.clear();
		dialogPrevState_ = oldState;
		break;
	case StateId::Camera:
		// Entering ST_CAMERA clears HUD messages/subtitles + soft keys and
		// letterboxes the viewport (src/Canvas.cpp:1207-1216); the queued
		// events analog is cleared here, viewport swap is a render concern.
		pendingActions_.clear();
		sys_.hud->clearMessages();       // msgCount = 0 (src/Canvas.cpp:1208)
		sys_.hud->clearCinematicText();
		break;
	case StateId::Dying:
		zoom_.drop();                          // src/Canvas.cpp:1116-1124
		// Player::died (src/Player.cpp:711-733); death sound by character (:720-726).
		++sys_.player->currentLevelDeaths;
		++sys_.player->totalDeaths;
		{
			const int cc = sys_.player->characterChoice;
			if (cc >= 1 && cc <= 3) Sound::instance().playSound(cc == 1 ? 1092 : cc == 2 ? 1091 : 1090, 0, 3);
		}
		sys_.hud->startShake(upTimeMs, 350, 10);   // startShake(350, 5, 500): intensity 2*5 (:729)
		sys_.player->ce.setStat(Enums::STAT_HEALTH, 0);
		sys_.game->monsters.combatMonsters = nullptr;
		sys_.game->combat.active = false;
		// ST_DYING entry (src/Canvas.cpp:1113-1129): destPitch has no analog,
		// tickDying writes the pitch directly.
		deathTimeMs_ = upTimeMs;
		sys_.dialogs->clearHelpQueue();
		std::fprintf(stderr, "[player] died\n");
		break;
	case StateId::BotDying:
		familiar_.onEnterBotDying();           // src/Canvas.cpp:1130-1141
		break;
	case StateId::Looting:
		// The session itself (pose latch + poolLoot) lives in LootSession;
		// clearing the queued events stays state-machine business.
		pendingActions_.clear();
		loot_.begin();
		break;
	case StateId::Menu:
		// src/Canvas.cpp:1192-1206 analog: the queued events are cleared and the
		// menu system is set to nextMenuId_ with a fresh stack. The legacy
		// pausePlayer() has no rewrite counterpart (no pause flag).
		pendingActions_.clear();
		if (oldState == StateId::Credits) menu_.returnFromCredits();
		else menu_.begin(nextMenuId_);
		nextMenuId_ = kMenuInGame;
		break;
	case StateId::MiniGame:
		// initGame clears the soft keys only; clearing the queue is the
		// Dialog/Menu events analog, so world keys never leak into the game.
		pendingActions_.clear();
		break;
	case StateId::CharacterSelect:
		// src/Canvas.cpp:1166-1173; prologue Back restores the previous
		// choice (src/IntroSequenceManager.cpp:723-726).
		pendingActions_.clear();
		charSelect_.begin(oldState == StateId::Intro ? sys_.player->characterChoice : 1);
		break;
	case StateId::Intro:
		pendingActions_.clear();                       // (src/Canvas.cpp:1174-1178)
		prologue_.begin();
		break;
	case StateId::Logo:
		pendingActions_.clear();
		break;
	case StateId::IntroMovie:
		pendingActions_.clear();                       // numEvents = 0 (src/GameStateRunner.cpp:343-347)
		introMovie_.begin();
		break;
	case StateId::Credits:
		pendingActions_.clear();
		credits_.begin();
		break;
	case StateId::TravelMap:
		// setState zeroes stateVars, then travelMapManager.init (src/Canvas.cpp:1062-1063).
		pendingActions_.clear();
		travel_.begin(loading_.travelLoadId(), loading_.travelLastId());
		break;
	case StateId::Loading:
	case StateId::Saving:
		// Save type/caption were latched by LoadingManager; the box restarts
		// its pacifier (src/Canvas.cpp:1183-1187).
		if (s == StateId::Loading) loadingPhase_ = 0;     // arm the loading-phase counter
		pacifierX_ = kPacifierStart;
		frameAtStateEntry_ = framesRendered_;
		break;
	}
}

// ---- frame ----

void GameContext::tick() {
	upTimeMs += kTickMs;                                   // app->upTimeMs / app->time
	// gameTime advances in PLAYING, CAMERA, LOOTING and AUTOMAP (scripts + the camera
	// clock share it, src/Game.cpp:3259; the loot crouch runs on the shared
	// clock too; the automap never pauses, Player::unpause is a stub,
	// src/Player.cpp:1419-1423). Frozen elsewhere (deviation C5, narrowed by the cinematic
	// group). Freezing during Dialog is what pauses the camera clock over a
	// dialog (legacy activeCameraTime flip, src/Canvas.cpp:1092-1094).
	if (!pauseGameTime && (state_ == StateId::Playing || state_ == StateId::Camera ||
	                       state_ == StateId::Looting || state_ == StateId::Automap)) gameTime += kTickMs;

	// Expired-latch sweep: legacy runInputEvents zeroes blockInputTime once
	// gameTime passes it, independent of thread state
	// (src/InputEventController.cpp:472-476). Silent like the original.
	if (blockInputTime != 0 && gameTime > blockInputTime) blockInputTime = 0;

	// TEMP [dbg] auto-dialog dismiss (remove after silent-tile-events bug
	// fixed): D2R_AUTODIALOG=1 presses Use every ~600 ms while a dialog box
	// is open, so headless runs get through story dialogs.
	{
		static int dialogCooldown = 0;
		static int enabled = -1;
		if (enabled < 0) {
			const char* env = std::getenv("D2R_AUTODIALOG");
			enabled = env != nullptr && std::atoi(env) != 0 ? 1 : 0;
		}
		if (enabled != 0 && state_ == StateId::Dialog && --dialogCooldown <= 0) {
			dialogCooldown = 40;
			pendingActions_.push_back(Action::Use);
		}
	}

	// Input gate: state is checked PER EVENT like legacy handleEvent
	// (src/InputEventController.cpp:156); ST_DIALOG routes every key to the
	// dialog handler (:331-333) so movement keys never leak to gameplay.
	// ST_CAMERA accepts only the skip gesture (:416-420). Blocked-drop
	// mirrors :449-478. Indexed loop: a dialog opening/closing mid-batch
	// clears the queue inside setState (entry hooks).
	bool blocked = inputBlocked();
	for (size_t i = 0; i < pendingActions_.size(); ++i) {
		Action a = pendingActions_[i];
		if (state_ == StateId::Dialog) {
			sys_.dialogs->handleInput(a);
			continue;
		}
		if (state_ == StateId::Camera) {
			// Any action past the cinUnpauseTime lockout skips the cinematic
			// (Passturn/Automap/Fire/18 in legacy; the subset set is smaller).
			if (cinematic_.skipGateOpen()) cinematic_.requestSkip();
			continue;
		}
		if (state_ == StateId::Looting) { loot_.handleAction(a); continue; }
		// ST_MENU: every queued action goes to the menu and nowhere else
		// (src/InputEventController.cpp:314-316). Deliberately BEFORE the
		// blocked-drop check: the legacy ST_MENU dispatch happens before it, and
		// a script lockout must not freeze an open menu (spec §2.4).
		if (state_ == StateId::Menu) {
			if (menu_.menuId() == kMenuComicBook) {
				comic_.handleAction(a);                // handleComicBookEvents first (src/MenuSystem.cpp:2834-2836)
				// FIRE/arrows also fall through in legacy; with 0 rows they do nothing.
				if (a == Action::BackKey || a == Action::Menu) menu_.handleAction(a);   // -> back()
			} else {
				menu_.handleAction(a);
			}
			continue;
		}
		// ST_MINI_GAME dispatch also precedes the block test
		// (src/InputEventController.cpp:334-349 vs :449).
		if (state_ == StateId::MiniGame) { minigames_.handleAction(a); continue; }
		// ST_TRAVELMAP has its own handler, also ahead of the block test
		// (src/InputEventController.cpp:429-431).
		if (state_ == StateId::TravelMap) {
			if (travel_.handleAction(a)) loading_.finishTravelMap();
			continue;
		}
		if (state_ == StateId::CharacterSelect) {
			if (a == Action::ScreenTouch) charSelect_.handleTouch(screenTouchId_);
			else charSelect_.handleAction(a);
			continue;
		}
		if (state_ == StateId::Intro) {
			if (a == Action::ScreenTouch) prologue_.handleTouch(screenTouchId_);
			else prologue_.handleAction(a);
			continue;
		}
		// ST_LOGO: only raw key 18 (shutdown) acts (src/InputEventController.cpp:167-172).
		if (state_ == StateId::Logo) continue;
		// ST_INTRO_MOVIE: any event skips, hasSeenIntro being forced true in the
		// port (src/Canvas.cpp:833-841, src/CAppContainer.cpp:186-188).
		if (state_ == StateId::IntroMovie) {
			exitIntroMovie_();
			continue;
		}
		// ST_CREDITS (not ending the game): BACK or FIRE at any time (src/InputEventController.cpp:387-414).
		if (state_ == StateId::Credits) {
			if (a == Action::BackKey || a == Action::Use) {
				if (sys_.game->loadMapID != 0) Sound::instance().soundStop();   // in-game branch only (:408)
				setState(StateId::Menu);
			}
			continue;
		}
		// ST_AUTOMAP runs the playing handler unchanged (src/InputEventController.cpp:354-356).
		if (blocked || (state_ != StateId::Playing && state_ != StateId::Automap)) break;
		if (a == Action::DebugVending) {
			// DEBUG (removable, minigames): no calling thread, so finish()
			// only returns to Playing.
			minigames_.start(MiniGameHost::kVending, 1, -1, -1, nullptr);
			continue;
		}
		if (a == Action::DebugHacking) {
			// DEBUG (removable, minigames): map00 terminal args (3 columns, 15 turns).
			minigames_.start(MiniGameHost::kHacking, 0, 3, 15, nullptr);
			continue;
		}
		if (a == Action::DebugSentryBot || a == Action::DebugSentryBotRed) {
			// DEBUG (removable, minigames): J = botType 0, Shift+J = botType 1.
			minigames_.start(MiniGameHost::kSentryBot, a == Action::DebugSentryBotRed ? 1 : 0, -1, -1, nullptr);
			continue;
		}
		if (a == Action::DebugGiveBot) {
			// DEBUG (removable, familiar): F5 = the next bot weapon of 3, 4, 5, 6.
			debugBotWeapon_ = debugBotWeapon_ == 6 ? 3 : debugBotWeapon_ + 1;
			sys_.player->give(Enums::ITEM_CLASS_WEAPON, debugBotWeapon_, 1);
			std::fprintf(stderr, "[debug] bot weapon %d, ammo[7]=%d\n", debugBotWeapon_,
				sys_.player->ammo[Enums::AMMO_SENTRY_BOT]);
			continue;
		}
		if (a == Action::DebugNextMap || a == Action::DebugPrevMap) {
			// DEBUG (removable, map change): N = next mapID, Shift+N = previous; header spawn.
			const int id = sys_.game->loadMapID;
			loading_.debugChangeMap(a == Action::DebugNextMap ? id % 10 + 1 : (id + 8) % 10 + 1);
			continue;
		}
		actions_.handleAction(a);
	}
	pendingActions_.clear();

	// Globals each tick: UpdatePlayerVars + gsprite_update + runScriptThreads
	// (src/Canvas.cpp:791-796); ST_COMBAT's second gsprite_update is in step 6
	// of tickPlaying.
	// Threads tick in PLAYING and CAMERA only (src/Game.cpp:3259): during a
	// cinematic the scripts keep running — input is what's parked. Playing
	// with combat.active is ST_COMBAT: runScriptThreads is still called
	// (src/Canvas.cpp:791-796) but resumes nothing, only sweeps finished
	// threads (src/Game.cpp:3259-3265). Lerp/door owners still resume their
	// threads via updateLerpSprites (src/GameStateRunner.cpp:25), ticked below.
	sys_.game->trace.setPlayerPos(sys_.player->viewX, sys_.player->viewY);
	sys_.game->trace.setPlayerDest(sys_.player->destX, sys_.player->destY);
	sys_.game->sprites.update(upTimeMs);
	if (state_ == StateId::Playing || state_ == StateId::Camera) {
		const bool resume = state_ == StateId::Camera || !sys_.game->combat.active;
		sys_.vm->runScriptThreads(gameTime, resume);
	}

	// SpriteLerp pool + door anims tick in every state legacy covers with
	// updateLerpSprites: PLAYING/INTER_CAMERA (src/GameStateRunner.cpp:25,
	// :184), ST_DIALOG (src/Canvas.cpp:921), ST_CAMERA (:953); ST_COMBAT
	// (:25) is Playing with combat.active here, ST_AUTOMAP
	// (src/AutomapController.cpp:22) is Automap.
	// Centralized here so no state can regress out of coverage again. Safe
	// under a dialog: legacy resumes blocking door/lerp owners from its
	// ST_DIALOG branch too (updateLerpSprites -> callThreads flush). Playing
	// keeps lerps-before-updateView order (src/GameStateRunner.cpp:184-185);
	// in CAMERA this runs before the camera clock (legacy swaps those two —
	// within-tick difference only).
	if (state_ == StateId::Playing || state_ == StateId::InterCamera ||
	    state_ == StateId::Camera || state_ == StateId::Dialog || state_ == StateId::Automap) {
		// Walk-writer view feed (spec §4): the chooser compares move vectors
		// against the last rendered view — maya pose during a cinematic key,
		// else the player view angle (same two sources render() uses,
		// GameContext.cpp:724-769; legacy read app->render->viewAngle).
		sys_.game->lerps.setLerpViewAngle(cinematic_.active()
		                                      ? cinematic_.pose().yaw
		                                      : sys_.player->viewAngle);
		// ST_COMBAT refreshes interpolatingMonsters BEFORE updateLerpSprites
		// (src/GameStateRunner.cpp:24-25): a step finishing this tick still
		// counts as interpolating for this tick's runFrame.
		if (state_ == StateId::Playing && sys_.game->combat.active) sys_.game->monsters.monsterLerp();
		sys_.game->update(kTickMs);
	}

	// DEBUG (golden capture runs): skip the travel map before the state
	// switch, so Loading runs in this same tick and the tick timeline matches
	// a run without the screen.
	if (state_ == StateId::TravelMap && skipTravelMap) loading_.finishTravelMap();

	switch (state_) {
	case StateId::Loading: tickLoading(); break;
	case StateId::Playing: tickPlaying(); break;
	case StateId::InterCamera:
		// ST_INTER_CAMERA: world keeps rendering from the player view while
		// script lerps animate (src/MovementController.cpp:391, :518); parked
		// threads resume via their owners only (runScriptThreads gates above).
		break;
	case StateId::Camera:  tickCamera(); break;
	case StateId::Automap: tickAutomap(); break;
	case StateId::Looting: loot_.tick(); break;
	case StateId::Dialog:
		// Lerps+doors ticked in the globals section above (legacy ST_DIALOG
		// branch, src/Canvas.cpp:920-926); view/input updates do not.
		break;
	case StateId::Dying:   tickDying(); break;
	case StateId::BotDying: familiar_.tickDying(); break;
	case StateId::Saving:                             // nothing else ticks (src/Canvas.cpp:853-903)
		if (framesRendered_ != frameAtStateEntry_) loading_.tickSaving();   // box presented first
		break;
	case StateId::Menu:
		// Nothing ticks while the menu is up: GameStateRunner::menuState only
		// updates soft keys (src/GameStateRunner.cpp:203-253). No lerps, no
		// doors, no scripts, no view update — and gameTime does not advance
		// either (the clock gate above lists Playing/Camera/Looting only).
		break;
	case StateId::MiniGame:
		// Nothing ticks (src/Canvas.cpp:827-828 excludes ST_MINI_GAME), no
		// scripts, gameTime frozen; animations run on upTimeMs inside draw.
		break;
	case StateId::TravelMap:
		// Excluded from the update chain (src/Canvas.cpp:827); all timing is
		// computed while drawing.
		break;
	case StateId::CharacterSelect:
		// No map is loaded (loadMapID 0); the screen's only clock is upTimeMs, read while drawing.
		break;
	case StateId::Intro:
		if (prologue_.done()) loading_.startNewGameMap();   // disposeIntro (src/Canvas.cpp:848-851)
		break;
	case StateId::Logo:                                     // pacLogoTime -1 .. 120 (src/GameStateRunner.cpp:332-347)
		if (stateVars[0] <= 121) ++stateVars[0];
		else setState(StateId::IntroMovie);
		break;
	case StateId::IntroMovie:
		introMovie_.tick();
		break;
	case StateId::Credits:
		// Excluded from the update chain (src/Canvas.cpp:827); the crawl clock is read while drawing.
		break;
	}
	// Idle-only pass for the other states that render the world (sound.md §5.4 callers).
	if (state_ == StateId::Dialog || state_ == StateId::Camera || state_ == StateId::Dying ||
	    state_ == StateId::BotDying || state_ == StateId::Looting) {
		sys_.game->monsters.renderActivate(sys_.game->visibility, false);
	}

	// Screen shake randomize/expire, every state incl. Playing (legacy
	// updateView block, src/MovementController.cpp:381-388).
	sys_.hud->tickShake(upTimeMs);

	// Familiar monster-at-body return, at most one tick late (ADR 0041 §4).
	familiar_.consumeMonsterLatch(state_);

	stateChanged = false;                                  // src/Canvas.cpp:987
}

bool GameContext::inputBlocked() const {
	// blockInputTime == 0 is the legacy "no lockout" sentinel
	// (src/InputEventController.cpp:449) — without the guard, the 0 <= 0
	// comparison blocks input at boot before gameTime starts.
	return sys_.vm != nullptr &&
		(sys_.vm->isInputBlockedByScript() ||
		 (blockInputTime != 0 && gameTime <= blockInputTime));
}

// ---- Loading (two-phase ordered tail, spec §5) ----

void GameContext::tickLoading() {
	if (loadingPhase_ == 0) {
		sys_.vm->clearStateVars(15);                       // zero scriptStateVars except vars[15] (src/LoadingManager.cpp:631-635)
		loadingPhase_ = 1;
		frameAtStateEntry_ = framesRendered_;
		return;                                            // bar frame in legacy (src/LoadingManager.cpp:610-616)
	}
	if (framesRendered_ == frameAtStateEntry_) return;     // the box must be presented before the load

	// phase 1 (src/LoadingManager.cpp:617-744)
	Sound& snd = Sound::instance();
	const bool savedAllow = snd.allowSounds;               // mute init-map effects (:619-621)
	snd.inInitMap = true;
	snd.allowSounds = false;
	unloadMap_();
	if (!sys_.loadMapFile || !sys_.loadMapFile(sys_.game->loadMapID))
		std::fprintf(stderr, "[load] map %d FAILED to load\n", sys_.game->loadMapID);
	sys_.vm->resetPool();                                  // 20 threads freed (src/Game.cpp:332-360)
	// Stamped BEFORE loadEntities; the spawn-time +25% hp bump
	// (domain/game/Game.cpp:213) applies on difficulties 2/4 as in legacy.
	sys_.vm->vars[12] = static_cast<short>(sys_.game->difficulty());   // (:692)
	std::fprintf(stderr, "[load] difficulty %d character %d\n",
		sys_.game->difficulty(), sys_.player->characterChoice);
	sys_.game->loadEntities(*sys_.map, *sys_.defs);        // (:658); loadWorldState slot: fresh entry no-op (:670)
	loading_.onEntitiesLoaded();
	actions_.spawnPlayer();                                // (:673)
	loading_.onPlayerSpawned();
	lastTurnTime_ = upTimeMs;                             // stamp kept at the call site (spec §P1-G7)
	std::fprintf(stderr, "[load] staticFunc(0)\n");
	sys_.vm->executeStaticFunc(Enums::SCR_INIT_MAP);       // SCR_INIT_MAP (:691-693)
	// staticFunc(1): completed-game only — no caller in the subset.
	// prevX/Y = view snap: save-only fields, not ported.
	if (!loading_.isLoadedGame())                          // entrance event, ENTER|all-dirs (:700-706)
		sys_.vm->executeTile(sys_.player->viewX >> 6, sys_.player->viewY >> 6, 4081, true);
	sys_.player->finishRotation();                         // finishRotation(false) analog (:707)
	sys_.game->monsters.endMonstersTurn();                 // monstersTurn = 0 (:708)
	// uncoverAutomap stub (:709).
	// Enter ST_PLAYING only when no cinematic took over during staticFunc(0)
	// (src/LoadingManager.cpp:715-717 gates on canvas->state == ST_LOADING);
	// stomping a live ST_CAMERA here kept the cockpit overlay gate in
	// render() false forever during the boot intro.
	if (state_ == StateId::Loading) setState(StateId::Playing);
	pauseGameTime = false;
	blockInputTime = gameTime + 200;
	snd.allowSounds = savedAllow;                          // (:721)
	snd.inInitMap = false;
	sys_.game->monsters.armIdleSounds(upTimeMs);           // (:734-736)
	std::fprintf(stderr, "[load] -> %s (blockInput 200ms)\n",
		state_ == StateId::Camera ? "ST_CAMERA (kept)" : "ST_PLAYING");
	loading_.onLoadFinished();
}

// Game::unloadMapData + Render::unloadMap analog (src/LoadingManager.cpp:619-625,
// src/Game.cpp:642-685). The fade is kept: legacy carries it across a map change.
void GameContext::unloadMap_() {
	actions_.gotoThread = nullptr;                       // VM pool is freed right after
	actions_.gotoTriggered = false;
	cinematic_.reset();                                  // activeCameraView = false (src/Game.cpp:649)
	Combat& c = sys_.game->combat;
	c.active = false;
	c.curAttacker = nullptr;                             // (src/Game.cpp:652-653)
	c.attackerMonster = nullptr;
	c.curTarget = nullptr;
	c.targetMonster = nullptr;
	c.numActiveMissiles = 0;
	for (GameSprites::Sprite*& m : c.activeMissiles) m = nullptr;
	c.totalDamage = 0;
	sys_.game->sprites.clear(GameSprites::kFlagInUse);   // (src/Game.cpp:655-659), before the map is replaced
	sys_.game->visibility = ViewVisibility{};            // last frame was drawn on the old map
	sys_.hud->clearMessages();                           // msgCount = 0 (src/LoadingManager.cpp:659)
	sys_.hud->feedMonsterHealth(-1, 0, 0);               // hud->lastTarget = nullptr (src/Game.cpp:654)
	sys_.dialogs->clearHelpQueue();
	sys_.player->facingEntity = nullptr;                // entity array is rebuilt
	sys_.player->viewPitch = 0;                          // death tilt (tickDying)
	sys_.player->viewRoll = 0;
	sys_.game->zoom.isZoomedIn = false;                  // (src/LoadingManager.cpp:627)
	// Player::reset clears the session (src/Player.cpp:504-505). Every reset path here
	// (new game, load, restart) reloads the map, and the parked thread dies with resetPool.
	practice_.reset();
}

// Canvas::backToMain(false) (src/Canvas.cpp:1372-1407).
void GameContext::backToMain_() {
	unloadMap_();                                        // unloadMapData + render->unloadMap analog (:1377-1379)
	sys_.vm->resetPool();                                // the map's threads die with it
	sys_.game->loadMapID = 0;                            // (:1375)
	sys_.player->reset();                                // (:1376)
	sys_.hud->clearScreenEffects();                      // render->endFade (:1380)
	pauseGameTime = true;                                // as at boot
	Sound::instance().playSound(1071, 1, 3);             // Music_Title (:1404)
	nextMenuId_ = kMenuMain;
	setState(StateId::Menu);                             // setMenu(MENU_MAIN) (:1405, src/MenuSystem.cpp:645-649)
}

void GameContext::exitIntroMovie_() {
	Sound::instance().soundStop();                       // (src/IntroSequenceManager.cpp:872)
	loading_.save().hasSeenIntro = true;
	loading_.save().saveConfig();
	backToMain_();
}

// ---- Restart Level (spec 2026-09-22-monster-attacks §10) ----

bool GameContext::loadGame(int loadType) {
	Player& p = *sys_.player;
	p.viewPitch = 0;                                     // death tilt (tickDying)
	p.viewRoll = 0;
	sys_.hud->clearScreenEffects();                      // endFade (src/Canvas.cpp:1380)
	return loading_.loadState(loadType);
}

// loadState(2) (src/MenuSystem.cpp:3106, src/Game.cpp:2191-2215): the
// level-entry BRIEFPLAYER + SB_<map> come back from disk and the map reloads
// via the gateway.
void GameContext::restartLevel() {
	std::fprintf(stderr, "[restart] level restart -> loadState(2)\n");
	if (loadGame(2)) return;
	// No usable snapshot file: fresh start of the current map.
	Player& p = *sys_.player;
	p.reset();
	p.currentLevelDeaths = 0;                            // src/Game.cpp:2200-2202; totalDeaths kept
	p.viewZ = p.destZ;                                   // death drop; spawnPlayer re-derives both
	setState(StateId::Loading);
}

// ---- Playing (fixed order, spec §6) ----

// Player death (ce health) or the familiar's death (ammo[7]), src/GameStateRunner.cpp:170-177.
bool GameContext::checkDeath_() {
	Player& p = *sys_.player;
	if (p.ce.getStat(Enums::STAT_HEALTH) <= 0) {
		setState(StateId::Dying);
		return true;
	}
	if (p.isFamiliar && p.ammo[Enums::AMMO_SENTRY_BOT] <= 0) {
		p.ammo[Enums::AMMO_SENTRY_BOT] = 0;
		familiar_.dying(false);
		return true;
	}
	return false;
}

void GameContext::tickPlaying() {
	// 1. [held-button auto-repeat — n/a, keyboard discrete]
	// 2. deferred turn consumer (dialog-close analog, src/Canvas.cpp:812-815).
	//    ST_PLAYING only: not while an attack seq runs (ST_COMBAT analog).
	if (sys_.game->queueAdvanceTurn && !sys_.game->combat.active) {
		sys_.game->queueAdvanceTurn = false;
		sys_.game->advanceTurn();
	}
	// 2.5 wall-push raise (src/GameStateRunner.cpp:166-169); playingState
	//     only, so not while an attack seq runs (ST_COMBAT analog).
	Combat& c = sys_.game->combat;
	if (!c.active && c.pushedWall && c.pushedTime <= gameTime) {
		c.shiftWeapon(false);
		c.pushedWall = false;
	}
	// 3. death check (src/GameStateRunner.cpp:170-177).
	if (checkDeath_()) return;
	// 4. combat seq / monster phase at the legacy position (:181-183). While
	//    an attack seq runs (ST_COMBAT analog, ADR 0028) it ticks; a finished
	//    player attack consumes the turn, a finished monster attack hands
	//    over to the next queued attacker (src/GameStateRunner.cpp:41-78).
	//    Otherwise the monster phase runs under the legacy playingState gate
	//    (src/GameStateRunner.cpp:181; knockbackDist is always 0 here).
	//    Per-tick shotsFired reset (spec §C step 4): legacy clears the latch
	//    once per rendered frame (src/Render.cpp:2351) so activate()'s
	//    back-turned wake guard is bypassed only during shot frames.
	sys_.game->combat.shotsFired = false;
	// Sight wake of the last rendered frame, seen with shotsFired == false
	// like the legacy render pass (ADR 0029, src/Render.cpp:2351, :1576-1583).
	sys_.game->monsters.renderActivate(sys_.game->visibility, true);
	const bool inCombat = c.active;               // ST_COMBAT for the whole tick
	if (c.active) {
		// monsterLerp + the lerp tick already ran in the globals section in
		// legacy combatState order (src/GameStateRunner.cpp:24-25).
		if (!c.tick()) {                          // runFrame analog
			if (c.curAttacker == nullptr) {
				c.active = false;
				sys_.game->advanceTurn();         // turn consumed AFTER seq
			} else {
				sys_.game->monsters.onMonsterAttackDone();
			}
		}
	} else if (sys_.game->sprites.activePropogators() == 0 &&
	           sys_.game->lerps.animatingEffects() == 0 &&
	           sys_.game->monstersTurn != 0 &&
	           sys_.dialogs->numHelpMessages() == 0) {
		sys_.game->monsters.updateMonsters();
	}
	// Legacy enters ST_DYING inside Player::pain (src/Player.cpp:706-708 ->
	// :711-733) and the bot's death at the end of a monster attack
	// (src/Combat.cpp:1035-1036); step 3 above stays as the second line of defence.
	if (checkDeath_()) return;
	// 4.5 The facing probe no longer runs here: legacy recomputes it from the
	//     HUD top bar on every rendered frame (src/Hud.cpp:735-742), see
	//     GameContext::render.
	// 5. door/sprite lerps tick in the globals section (legacy updateLerpSprites
	//    here, src/GameStateRunner.cpp:184), still BEFORE updateView (:185).
	// Help-popup dequeue while playing and no camera is active
	// (src/GameStateRunner.cpp:197-199; state guards live in the dialog
	// system). No monstersTurn term: the monster phase waits for an empty help
	// queue, so gating on the turn would deadlock it. Not in ST_COMBAT: the
	// legacy dequeue lives in playingState only and re-checks the state after
	// updateMonsters, so neither the tick a seq starts nor the tick it ends
	// (combatState) dequeues.
	if (!inCombat && !c.active && !cinematic_.active()) sys_.dialogs->dequeueHelpDialog();
	// 6. updateView (src/MovementController.cpp:377-547).
	bool posIdle = (sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY);
	bool angleIdle = (sys_.player->viewAngle == sys_.player->destAngle);
	sys_.player->updateView();                             // interpolate X/Y/Z/angle
	// Second gsprite_update of ST_COMBAT (src/MovementController.cpp:500-502):
	// recounts after runFrame destroyed/allocated sprites this tick.
	if (inCombat) sys_.game->sprites.update(upTimeMs);
	if (actions_.gotoTriggered) {
		// Instant-GOTO deferred destination events: fresh default direction
		// mask, ENTER then FACE (src/MovementController.cpp:503-509). Runs
		// INSTEAD of finishMovement this frame, like the legacy else-if.
		actions_.gotoTriggered = false;
		sys_.game->eventFlagsForMovement(-1, -1, -1, -1);
		sys_.vm->executeTile(sys_.player->destX >> 6, sys_.player->destY >> 6,
			sys_.game->eventFlags_[1], true);
		sys_.vm->executeTile(sys_.player->destX >> 6, sys_.player->destY >> 6,
			actions_.flagForFacingDir(8), true);
	} else if (!posIdle && sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY) {
		actions_.finishMovement();                         // (:510-512)
	}
	if (!angleIdle && sys_.player->viewAngle == sys_.player->destAngle) {
		actions_.finishRotationFired();                    // (:514-516)
	}
	// A cinematic survives into ST_PLAYING here: legacy updateView keeps
	// driving the active camera whenever isCameraActive() regardless of the
	// canvas state (src/MovementController.cpp:518-520) — e.g. a staticFunc
	// STARTCINEMATIC that boot's ST_PLAYING transition overwrites.
	if (cinematic_.active()) cinematic_.tickClock();
	// 7. camera pull-back + scene draw happen in render().
	// HUD message timers tick with the playing state (legacy hud->update,
	// src/Hud.cpp:1365-1378 analog).
	sys_.hud->update(kTickMs);

	// TEMP [dbg] auto-walk driver (remove after silent-tile-events bug
	// fixed): D2R_AUTOTEST=N queues N Forward steps while fully idle and
	// unblocked, so tile-event arrival can be verified headlessly;
	// D2R_AUTOUSE=N likewise queues N Use presses afterwards;
	// D2R_AUTOPASS=N queues N Passturn presses (fire-path acceptance);
	// D2R_AUTOKEYS=1 grants the debug keycards first.
	{
		static int stepsLeft = -1;
		static int usesLeft = -1;
		static int passesLeft = -1;
		static int cooldown = 0;
		if (stepsLeft < 0) {
			const char* env = std::getenv("D2R_AUTOTEST");
			stepsLeft = env != nullptr ? std::atoi(env) : 0;
			env = std::getenv("D2R_AUTOUSE");
			usesLeft = env != nullptr ? std::atoi(env) : 0;
			env = std::getenv("D2R_AUTOPASS");
			passesLeft = env != nullptr ? std::atoi(env) : 0;
			env = std::getenv("D2R_AUTOKEYS");
			if (env != nullptr && std::atoi(env) != 0) debugGiveKeycards();
		}
		bool idle = sys_.player->viewX == sys_.player->destX &&
		            sys_.player->viewY == sys_.player->destY &&
		            sys_.player->viewAngle == sys_.player->destAngle;
		if (stepsLeft > 0 && idle && !cinematic_.active() && !inputBlocked() &&
		    --cooldown <= 0) {
			--stepsLeft;
			cooldown = 40; // ~600 ms between steps
			std::fprintf(stderr, "[dbg] autotest queue Forward (%d left)\n", stepsLeft);
			pendingActions_.push_back(Action::Forward);
		} else if (usesLeft > 0 && !cinematic_.active() && !inputBlocked() &&
		           --cooldown <= 0) {
			--usesLeft;
			cooldown = 200; // ~3 s between uses
			std::fprintf(stderr, "[dbg] autotest queue Use (%d left)\n", usesLeft);
			pendingActions_.push_back(Action::Use);
		} else if (passesLeft > 0 && idle && !cinematic_.active() && !inputBlocked() &&
		           --cooldown <= 0) {
			--passesLeft;
			cooldown = 100; // ~1.5 s between passes
			std::fprintf(stderr, "[dbg] autotest queue Passturn (%d left)\n", passesLeft);
			pendingActions_.push_back(Action::Passturn);
		}
	}
}

// GameStateRunner::dyingState (src/GameStateRunner.cpp:257-281). The pitch is
// ported unnegated: the rewrite camera uses the legacy view matrix and sign
// (positive = up, cf. the loot crouch's -64 looking down).
void GameContext::tickDying() {
	Player& p = *sys_.player;
	const int t = (int)(upTimeMs - deathTimeMs_);
	if (t < 750) {
		const int n = ((750 - t) << 16) / 750;
		p.viewZ = sys_.map->heightAt(p.destX, p.destY) + 18 + ((20 * n) >> 16);
		p.viewPitch = 96 + ((-96 * n) >> 16);
		p.viewRoll = 16 + ((-16 * n) >> 16);
		// updateView (:266). Legacy steps viewZ by a zero zStep, i.e. leaves
		// it; Player::updateView snaps viewZ = destZ instead, so keep the drop.
		const int z = p.viewZ;
		p.updateView();
		if (p.zStep == 0) p.viewZ = z;
	} else if (t < 2750) {
		if (!sys_.hud->isFading()) sys_.hud->startFade(upTimeMs, 2000, Hud::kFadeOut);
		p.viewRoll = 16;
	} else {
		// baseDizzy/destDizzy (:278) have no rewrite counterpart.
		nextMenuId_ = kMenuInGameDead;
		setState(StateId::Menu);
	}
}

// ---- cinematic camera (docs/original-code/cutscenes-camera.md §1-§3) ----

void GameContext::tickCamera() {
	// HUD message/subtitle timers run on the shared clock in legacy
	// (gameTime advances in PLAYING + CAMERA, src/Game.cpp:3259). Skipped on a
	// skip frame: tickCameraState() returns false there, as the pre-
	// decomposition early return did.
	if (cinematic_.tickCameraState()) {
		sys_.hud->update(kTickMs);
	} else if (!sys_.hud->isFading() || (sys_.hud->fadeFlags() & 0xC) == 0) {
		sys_.hud->startFade(upTimeMs, 500, Hud::kFadeIn);   // end of skipCinematic (src/Game.cpp:2530-2532)
	}
}

void GameContext::tickAutomap() {
	// Leaves exactly like the original: an attack seq means ST_COMBAT
	// (src/Combat.cpp:165), a paused blocking script forces ST_PLAYING
	// (src/Game.cpp:3256-3258).
	if (sys_.game->combat.active || sys_.vm->isInputBlockedByScript()) {
		setState(StateId::Playing);
		return;
	}
	sys_.game->doors.snapAll();                               // src/Game.cpp:1153
	// Sight wake of the last rendered frame (ADR 0029); the original's automap
	// updateView renders too (src/AutomapController.cpp:23-24).
	sys_.game->monsters.renderActivate(sys_.game->visibility, true);
	Player& p = *sys_.player;
	const bool moved = p.viewX != p.destX || p.viewY != p.destY;
	const bool turned = p.viewAngle != p.destAngle;
	// updateView in ST_AUTOMAP snaps view to dest (src/MovementController.cpp:492-498).
	p.viewX = p.destX; p.viewY = p.destY; p.viewZ = p.destZ; p.viewAngle = p.destAngle;
	if (moved) actions_.finishMovement();                     // (:510-512)
	if (turned && state_ == StateId::Automap) actions_.finishRotationFired();   // (:514-516)
}

void GameContext::debugGiveKeycards() {
	// PHASE5 DEBUG (removable): grant red/blue keycards so the positive
	// unlock path is testable without the loot system (spec §10 item 8).
	if (sys_.player == nullptr) return;
	sys_.player->give(0, 19, 1);
	sys_.player->give(0, 20, 1);
	std::fprintf(stderr, "[debug] granted keycards inv[19]=%d inv[20]=%d\n",
		sys_.player->inventory[19], sys_.player->inventory[20]);
	// DEBUG (removable, use-item G2): map00 has no drinks; one of each plus
	// the holy-water pistol and 50 water so every item-use arm is testable.
	for (int s = 0; s < 11; ++s) sys_.player->give(0, s, 1, true);
	sys_.player->give(1, Enums::WP_HOLY_WATER_PISTOL, 1, true);
	sys_.player->give(2, Enums::AMMO_HOLY_WATER, 50, true);
	std::fprintf(stderr, "[debug] granted drinks 0..10 + holy water\n");
	// DEBUG (removable, minigames): credits for the vending buy menus.
	sys_.player->give(0, 24, 1000, true);
	std::fprintf(stderr, "[debug] credits inv[24]=%d\n", sys_.player->inventory[24]);
	// DEBUG (removable): full health and armor cap (Player::addArmor clamps to 200).
	sys_.player->addHealth(sys_.player->getMaxHealth(), false);
	sys_.player->addArmor(200);
	std::fprintf(stderr, "[debug] health=%d armor=%d\n",
		sys_.player->getHealth(), sys_.player->ce.getStat(Enums::STAT_ARMOR));
}

void GameContext::debugToggleCoords() {
	// DEBUG (rewrite-only, removable): the coordinate readout is ours, no
	// legacy counterpart exists.
	if (sys_.hud == nullptr) return;
	sys_.hud->toggleDebugPosition();
}

// ---- HUD view model (spec 2026-08-27-ui-layer §4.1) ----

namespace {

// Soft-key string ids: the gameplay default is setSoftKeys(0,52, 0,55)
// (src/SoftKeyController.cpp:97-99) — text type 0, 52 = "Menu", 55 = "Map"
// (docs/original-code/ui.md §6).
constexpr int kSoftKeyMenuId = 52;
constexpr int kSoftKeyMapId = 55;

// Hit boxes for the two side soft keys. DELIBERATE DEVIATION from the original,
// at the user's request: the legacy touch areas registered in Hud::startup are
// id 0 = left (0,256,52,64) -> ACTION_MENU and id 1 = right (428,256,52,64) ->
// ACTION_AUTOMAP (src/Hud.cpp:67-71 for the rects, src/Hud.cpp:1300-1320 for the
// actions), and those 52x64 boxes also cover the "Menu" / "Map" labels drawn at
// y=320 (src/TouchController.cpp:550-577) — in the original clicking the text IS
// clicking the button. Here the rects are narrowed to the 32x32 arrow icons at
// (9,268) / (438,268) so only the icon reacts and the text is a pure label, like
// the centre "Wait" literal. Both new rects are strict subsets of the legacy
// ones, so nothing became clickable that was not before. Do NOT "restore" the
// 52x64 rects thinking this is a bug — see the CORRECTION section of
// docs/architecture/specs/2026-08-27-ui-layer.md.
constexpr UiRect kSoftLeftHit{ 9, 268, 32, 32 };
constexpr UiRect kSoftRightHit{ 438, 268, 32, 32 };
// ACTION_PASSTURN is the portrait: hud button 3, (219, 264, imgPlayerFaces->
// width + 10 = 32 + 10, 36) (src/Hud.cpp:76 for the rect,
// src/Hud.cpp:1343-1345 for the action). The centre "Wait" literal has no touch
// area at all in the original (src/Hud.cpp:712-720 only draws it).
constexpr UiRect kPortraitHit{ 219, 264, 42, 36 };
// Hud buttons 2/4/5/6 (docs/architecture/hud-bar-buttons.md §2).
constexpr UiRect kWeaponHit{ 268, 258, 102, 44 };   // src/Hud.cpp:73
constexpr UiRect kShieldHit{ 49, 258, 79, 44 };     // src/Hud.cpp:79
constexpr UiRect kHealthHit{ 133, 258, 79, 44 };    // src/Hud.cpp:82
constexpr UiRect kKeysHit{ 375, 258, 55, 44 };      // src/Hud.cpp:85
// While familiar a press strictly inside the 194x38 HUD_sentry_active plate at
// (143, 262) is button 3 (src/Hud.cpp:1245-1248). DEVIATION: legacy also needs
// the press to start on some HUD button; the gaps between buttons count here.
constexpr UiRect kBotHit{ 144, 263, 193, 37 };

void setSoftKeyText(Text& out, const std::string& s) {
	// composeText + dehyphenate, as the original does for every soft-key label
	// (src/TouchController.cpp:556-561).
	out.setLength(0);
	out.append(s);
	out.dehyphenate();
}

// fmButton soundResID per widget (sound.md §5.7); -1 = silent.
// hudBlocked: Hud::handleUserTouch returns before SetHighlighted (src/Hud.cpp:1238-1239).
int pressSound(UiId id, bool hudBlocked) {
	const int i = (int)id;
	if (i >= (int)UiId::HudSwitchLeft && i <= (int)UiId::HudWeaponSel14 && hudBlocked) return -1;
	if (i >= (int)UiId::HudSwitchLeft && i <= (int)UiId::HudSoftRight && id != UiId::HudSoftCenter)
		return 1121;   // HUD buttons 0-6, src/Hud.cpp:67-85
	if (i >= (int)UiId::HudWeaponSel0 && i <= (int)UiId::HudWeaponSel14) return 1027;   // src/Hud.cpp:90
	if (i >= (int)UiId::DialogBody && i <= (int)UiId::DialogChoiceRight) return 1027;   // src/Canvas.cpp:327-348
	if (i >= (int)UiId::MenuRow0 && i <= (int)UiId::MenuInfo8) return 1027;   // src/MenuSystem.cpp:173-256
	if (id == UiId::MenuSoftLeft || id == UiId::MenuSoftRight) return 1122;   // src/MenuSystem.cpp:193,219
	if (id == UiId::AutomapSoftLeft || id == UiId::AutomapSoftRight) return 1027;   // buttons 19/20, src/Canvas.cpp:355-357
	if (i >= (int)UiId::CharSlot0 && i <= (int)UiId::StorySkip) return 1027;   // src/IntroSequenceManager.cpp:37-47
	return -1;
}

// SetHighlighted plays on the rising edge unless already playing (src/Button.cpp:145-158).
void playPressSound(UiId id, bool hudBlocked) {
	const int snd = pressSound(id, hudBlocked);
	Sound& S = Sound::instance();
	if (snd != -1 && !S.isSoundPlaying(snd)) S.playSound(snd, 0, 5);
}

} // namespace

void GameContext::buildHudModel(HudModel& m, bool showBottomBar, bool interactive) {
	m = HudModel{};
	m.showBottomBar = showBottomBar;
	m.interactive = interactive;

	// Producer half of the former Hud::feedPlayerStatus: the RAW live stats the
	// legacy widgets read every draw pass (src/Hud.cpp:683-709).
	const Player& p = *sys_.player;
	m.characterChoice = p.characterChoice;
	m.health = p.getHealth();
	m.maxHealth = p.getMaxHealth();
	m.shield = p.ce.getStat(Enums::STAT_ARMOR);
	m.weapon = p.weapon;
	if (sys_.tables != nullptr) {
		const int ammoType = sys_.tables->weaponDef(p.weapon).ammoType;
		if (ammoType >= 0 && ammoType < 9) m.ammo = p.ammo[ammoType];
	}
	// drawWeapon's drawNumbers flag (src/Hud.cpp:1053-1112). NOTE: the
	// original's `default:` branch also covers weapon < 0, so a player with no
	// weapon shows no digits there; the formula below is the spec's
	// (weapon != 1 && weapon < 15) and keeps the current rewrite's "000".
	m.showAmmo = m.weapon != 1 && m.weapon < 15;
	m.slashAmmo = m.weapon == 13;
	// Nested-if fold of src/Hud.cpp:1158-1172: slot 19 = red keycard,
	// slot 20 = blue keycard.
	m.keysRow = (p.inventory[19] > 0 ? 1 : 0) | (p.inventory[20] > 0 ? 2 : 0);

	// The portrait's touch area does not depend on the soft-key labels, unlike
	// the two side rects below; the view gates it on m.interactive.
	m.portraitHit = kPortraitHit;
	m.weaponHit = kWeaponHit;
	m.shieldHit = kShieldHit;
	m.healthHit = kHealthHit;
	m.keysHit = kKeysHit;
	m.weaponSelect = hudButtons_.weaponSelectOpen() && !p.isFamiliar;
	m.weaponSelectMask = p.weapons & ~p.disabledWeapons & 0x7FFF;   // src/Hud.cpp:1183
	if (sys_.tables != nullptr) {
		for (int i = 0; i < 15; ++i) {
			const int ammoType = sys_.tables->weaponDef(i).ammoType;
			if (ammoType >= 0 && ammoType < 9) m.weaponSelectAmmo[i] = p.ammo[ammoType];
		}
	}
	m.familiar = p.isFamiliar;
	if (m.familiar) {
		m.botHealth = p.ammo[Enums::AMMO_SENTRY_BOT];
		m.botRed = p.familiarType == 3 || p.familiarType == 4;
		m.portraitHit = kBotHit;
	}

	// Soft keys: the playing variants of drawPlayingSoftKeys
	// (src/SoftKeyController.cpp:77-100); the rewrite has no softKeyLeftID/
	// RightID state machine, so the slots stay null outside the bottom-bar
	// states — which is exactly the legacy "== -1" case the view already
	// handles. The automap variant lives in automapModel_.
	if (showBottomBar && sys_.loc != nullptr) {
		const bool botHeld = p.ce.weapon >= 0 && ((1 << p.ce.weapon) & Enums::WP_SENTRY_BOT_MASK) != 0;
		int rightId = kSoftKeyMapId;
		if (!sys_.game->zoom.isZoomedIn) {
			if (p.isFamiliar) rightId = 216;      // "Return"
			else if (botHeld) rightId = 220;      // "Discard"
		}
		m.softRightAction = botHeld && state_ != StateId::Automap ? UiAction::BotDiscard : UiAction::Automap;
		setSoftKeyText(softLeftText_, sys_.loc->get(kTextMain, kSoftKeyMenuId));
		setSoftKeyText(softRightText_, sys_.loc->get(kTextMain, rightId));
		setSoftKeyText(softCenterText_, "Wait");   // hardcoded ASCII (src/Hud.cpp:715-721)
		m.softLeft = &softLeftText_;
		m.softRight = &softRightText_;
		m.softCenter = &softCenterText_;
		m.softLeftHit = kSoftLeftHit;
		m.softRightHit = kSoftRightHit;
		// In the range: "Exit" on the left, no right key (src/SoftKeyController.cpp:81-83).
		if (practice_.active()) {
			setSoftKeyText(softLeftText_, sys_.loc->get(kTextMain, 30));
			m.softRight = nullptr;
			m.softRightHit = UiRect{};
		}
	}
}

// ---- view-weapon view model (spec 2026-08-27-ui-layer §6) ----

void GameContext::buildViewWeaponModel(ViewWeaponModel& m) const {
	m = ViewWeaponModel{};
	if (sys_.player == nullptr || sys_.game == nullptr || sys_.tables == nullptr) return;

	const Player& p = *sys_.player;
	const int w = p.ce.weapon;                                 // (src/Combat.cpp:677)
	if (w < 0 || p.weapons == 0) return;                       // (:706-708)
	// A deployed bot shows the rifle (weapon 0 art and pose row) for the
	// shooting types and nothing for the exploding ones (:695-700); m.weapon
	// keeps ce.weapon for the +12 bot scrY bias applied before the swap.
	if (p.isFamiliar && p.familiarType != 1 && p.familiarType != 3) return;
	const int pw = p.isFamiliar ? 0 : w;

	// wpinfo table 1: idleX,idleY,atkX,atkY,flashX,flashY signed bytes per
	// weapon (src/Combat.h:53-59). Row missing => draw nothing, same as the
	// former ((size_t)(w * 6 + 5) >= weaponInfo.size()) early return.
	const WeaponPose* pose = sys_.tables->weaponPose(pw);
	if (pose == nullptr) return;
	int idleX = pose->idleX;
	int idleY = pose->idleY;
	int atkX  = pose->atkX;
	int atkY  = pose->atkY;
	if (p.isFamiliar) {                                        // (:719-724)
		atkX = 1 + (atkX - idleX);
		atkY = 50 + (atkY - idleY);
		idleX = 1;
		idleY = 50;
	}

	// Attack pose: hold (atkX,atkY) until flashDone, then lerp back over
	// animTime in 16.16 (src/Combat.cpp:735-767). b5 = "the player's own
	// seq is running for this weapon". The flashDone latch itself lives in
	// Combat's player sequence now (deviation D2).
	int wpX = idleX, wpY = idleY;
	bool flash = false;
	const Combat& c = sys_.game->combat;
	const bool attackPose = c.playerAttackPose(w);
	if (attackPose) {
		wpX = atkX;
		wpY = atkY;
		if (!c.flashDone) {
			flash = ((1 << pw) & 0x200) == 0;                  // :741 (weapon 9 excluded)
		} else {
			// SHOTHOLD return lerp; chainsaw jitter branch (:752-761) omitted.
			const int elapsed = (int)(gameTime - c.animStartTime);
			// Lerp starts at animStartTime; legacy does not subtract
			// flashTime (src/Combat.cpp:747).
			const int t = std::clamp(elapsed, 0, c.animTime) *
				65536 / std::max(c.animTime, 1);
			wpX = atkX + (((idleX - atkX) * t) >> 16);
			wpY = atkY + (((idleY - atkY) * t) >> 16);
		}
	}
	// Lowered weapon: steady when down (src/Combat.cpp:672-674); the lerp
	// only outside the attack pose (:769-784), its finished branch reading
	// the same value the latch then leaves for the steady term.
	int lowerY = 0;
	if (!c.lerpingWeapon && c.weaponDown) lowerY = Combat::kLoweredWeaponY;
	if (!attackPose && c.lerpingWeapon) {
		const int gt = (int)gameTime;
		if (c.lerpWpStartTime + c.lerpWpDur > gt) {
			const int f = ((gt - c.lerpWpStartTime) << 16) / c.lerpWpDur;
			lowerY = (c.lerpWpDown ? f : 65536 - f) * Combat::kLoweredWeaponY >> 16;
		} else if (c.lerpWpDown) {
			lowerY = Combat::kLoweredWeaponY;
		}
	}

	m.visible = true;
	m.weapon = w;
	m.weaponTile = Combat::getWeaponTileNum(pw);
	m.flashTile = Combat::getWeaponTileNum(0);                 // (:826-834)
	if (pw == 9) m.underlayTile = Combat::getWeaponTileNum(0); // (:835-837)
	if (p.isFamiliar) m.extraY = -35;                          // (:790-791, GL path)
	if (pw >= 3 && pw <= 6) {                                  // weaponIsASentryBot (:797-813)
		m.botTile = (w == 3 || w == 4) ? 19 : 18;
		const int t = (int)((upTimeMs + m.botTile * 1337) / 1024) & 0xF;   // app->time
		m.botMirror = t == 0 || (t >= 4 && t <= 7) || (t >= 10 && t <= 12);  // flags ^= 0x20000
		m.botDy = (t & 0x2) ? 1 : 2;
	}
	m.poseX = wpX;
	m.poseY = wpY;
	m.lowerY = lowerY;
	// The second legacy flash gate sat at the blit (:826); both are ANDed here
	// so the view only reads a bool. Neither condition is dropped or merged.
	m.muzzleFlash = flash && ((1 << pw) & 0x181) != 0;
	m.flashX = pose->flashX;
	m.flashY = pose->flashY;
	// Canvas shake; legacy negates sy first (sy = -|sy|, src/Combat.cpp:709),
	// so the model carries the already-negated value.
	m.shakeX = sys_.hud->shakeX();
	m.shakeY = -std::abs(sys_.hud->shakeY());
	// src/Combat.cpp:710-712 (the cinematicWeapon `b2` term is false here).
	m.brightRed = upTimeMs < sys_.hud->damageTime() && sys_.hud->damageCount() >= 0 &&
		c.totalDamage > 0;
}

void GameContext::applyUiAction(UiAction a, int index) {
	// One switch, feeding the queue the keyboard already feeds (spec §4.2).
	Action queued = Action::None;
	switch (a) {
	case UiAction::None: return;
	case UiAction::Menu:       queued = Action::Menu; break;
	case UiAction::Automap:    queued = Action::Automap; break;
	case UiAction::Back:       queued = Action::BackKey; break;
	case UiAction::PassTurn:   queued = Action::Passturn; break;
	case UiAction::Activate:   queued = Action::Use; break;
	case UiAction::BotDiscard: queued = Action::BotDiscard; break;
	case UiAction::ItemsDrinks: queued = Action::ItemsDrinks; break;
	case UiAction::Items:      queued = Action::Items; break;
	case UiAction::QuestLog:   queued = Action::QuestLog; break;
	case UiAction::WeaponTap:  queued = Action::HudNextWeapon; break;
	case UiAction::WeaponSelect:   // src/Hud.cpp:1281-1288: no ammo check, no message, no turn
		sys_.player->selectWeapon(index, sys_.defs != nullptr
			? sys_.defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, index) : nullptr);
		return;
	case UiAction::DialogChoice:
		sys_.vm->vars[4] = static_cast<short>(index);
		queued = Action::Use;
		break;
	// The scroll mapping is chosen so a wheel behaves exactly like the arrow
	// keys already do in the loot list (new_src/core/LootSession.cpp:104-120).
	case UiAction::ScrollUp:   queued = Action::Forward; break;
	case UiAction::ScrollDown: queued = Action::Back; break;
	case UiAction::ScrollHome: queued = Action::TurnLeft; break;
	case UiAction::ScrollEnd:  queued = Action::TurnRight; break;
	case UiAction::ListRow:
		// NEW FACT 6 (spec 2026-08-28-menu §9): the original's touch handler
		// writes selectedIndex and then calls select(); the rewrite writes the
		// index and queues the SAME action the FIRE key queues, so mouse and
		// keyboard cannot diverge.
		if (state_ == StateId::Menu) {
			menu_.setSelectedIndex(index);
			queued = Action::Use;
		} else {
			std::fprintf(stderr, "[ui] list row %d (no mapping this phase)\n", index);
			return;
		}
		break;
	case UiAction::Info:
		// Same two-step as ListRow, and for the same reason: the legacy handler
		// latches the index the info button carries and then acts on it
		// (src/MenuSystem.cpp:4802-4805). Unlike ListRow this index is NOT the
		// selection — the cursor stays where it is.
		if (state_ == StateId::Menu) {
			menu_.setInfoIndex(index);
			queued = Action::MenuInfo;
		} else {
			std::fprintf(stderr, "[ui] info button %d outside the menu\n", index);
			return;
		}
		break;
	case UiAction::InfoClose:
		queued = Action::MenuInfoClose;
		break;
	case UiAction::MiniGameTouch:
		minigames_.latchTouch(index);
		queued = Action::MiniGameTouch;
		break;
	case UiAction::ScreenTouch:
		screenTouchId_ = index;
		queued = Action::ScreenTouch;
		break;
	case UiAction::Resume:
		// Touch-only in the original (src/MenuSystem.cpp:4787-4789): no key
		// produces this action.
		queued = Action::MenuResume;
		break;
	}
	std::fprintf(stderr, "[ui] intent %d -> queue Action %d\n", (int)a, (int)queued);
	queueAction(queued);
}

// ---- render orchestration ----

void GameContext::render(AppContext& app) {
	RenderBackend& renderer = app.renderer();
	renderer.beginFrame(app.window());
	Graphics2D& g = app.g2d();

	// UI pass frame window (spec §8): every view runs on the render thread
	// between the backend's beginFrame and endFrame. beginFrame latches the
	// frame's input, endFrame drops a press that ended off-canvas.
	Ui* ui = sys_.ui;
	if (ui != nullptr) ui->beginFrame(uiInput_);

	// Single source of truth for "a cinematic owns the world this frame":
	// one value, produced by one owner, feeding fov, cockpit overlay and
	// view-weapon suppression (spec 2026-08-26-decomposition §P1-G2). Also
	// performs the display-rate pose resample.
	const MayaPose* cinePose = cinematic_.renderPose();
	// fov 290 while a dialog runs inside the cinematic, 315 otherwise
	// (src/MayaCamera.cpp:305-310 canvas->state == ST_DIALOG).
	const bool underDialog = state_ == StateId::Dialog;

	// Tile-149 layers every frame, also outside the range: unconditional in legacy.
	int tpFrames[4];
	practice_.layerFrames(upTimeMs, sys_.game->zoom.isZoomedIn, tpFrames);
	sys_.world->setPracticeTargetFrames(tpFrames[0], tpFrames[1], tpFrames[2], tpFrames[3]);
	ZoomPose zp;
	const ZoomPose* zpp = (cinePose == nullptr && zoom_.renderPose(zp)) ? &zp : nullptr;
	// backToMain zeroes loadMapID (src/Canvas.cpp:1375); the old MapData stays resident (ADR 0035).
	if (sys_.game->loadMapID != 0) {
		scene_.drawWorld(renderer, app.window(), cinePose, underDialog, zpp, state_ == StateId::Camera);
		// The only writer of Game::visibility (ADR 0029). disableRenderActivate is
		// tied to ST_CAMERA, not to the camera pose (src/Canvas.cpp:1038, :1211).
		sys_.game->visibility = scene_.visibility();
		sys_.game->visibility.cinematic = state_ == StateId::Camera;
		sys_.game->visibility.timeMs = upTimeMs;           // app->time inside the render (src/Render.cpp:3772)
		// Seen marks of this frame (ADR 0036); gate = legacy updateAutomap.
		Automap::markSeen(*sys_.map, sys_.game->visibility,
		                  state_ == StateId::Playing || state_ == StateId::Automap);
	}

	// View weapon paints over the world in full canvas space; its legacy
	// anchors already include the world viewport origin (ADR 0009). The two
	// gates that used to live inside drawViewWeapon are here now (spec
	// §P1-G5): the gameplay-state list (src/Combat.cpp:706-708 state check)
	// and the cinematic suppression — while a camera is active legacy renders
	// the world from MayaCamera::Render and Canvas::renderScene's drawWeapon
	// call is unreachable (src/MovementController.cpp:518-523); the only view
	// weapon is the cinematicWeapon != -1 branch (src/MayaCamera.cpp:311-314),
	// deliberately deferred in the rewrite.
	const bool gameplayView = state_ == StateId::Playing ||
	                          state_ == StateId::Looting || underDialog;
	// No view weapon while zoomed (src/Canvas.cpp:1348).
	// ST_BOT_DYING keeps it too: drawWeapon only skips ST_DYING (src/Combat.cpp:706).
	if (cinePose == nullptr && !cinematic_.active() && (gameplayView || state_ == StateId::BotDying) &&
	    !sys_.game->zoom.isZoomedIn) {
		ViewWeaponModel wp;
		buildViewWeaponModel(wp);
		// Legacy latches the finished lower/raise inside drawWeapon
		// (src/Combat.cpp:776-782); latching right after the const build, under
		// the same draw gates, keeps both the frame and the tick ordering.
		if (wp.visible) sys_.game->combat.latchWeaponLerp(wp.weapon);
		viewWeapon_.draw(g, scene_.camera(), wp);
	}

	// Cockpit overlay while a cinematic renders with the raw toggle set
	// (MayaCamera::Render -> Hud::drawOverlay, src/MayaCamera.cpp:316-318;
	// cinRect = viewRect.x / 42 / viewRect width, src/Canvas.cpp:151-154).
	// The boot intro enables it only around camera 0 (IP 1945-2062).
	if (cinePose != nullptr && sys_.hud->cockpitOverlay()) {
		sys_.hud->drawOverlay(g, 0, 42, 480);
	}

	// Familiar tint/scanlines/brighten, the end of the 3D pass before the fade
	// (src/Canvas.cpp:1344-1353, src/Render.cpp:2642-2659).
	if (sys_.player->isFamiliar) sys_.hud->drawFamiliarOverlay(g, upTimeMs, sys_.player->familiarType);

	// Screen fade at the end of the legacy REPAINT_VIEW3D slot: over the world,
	// view/cinematic weapon and cockpit overlay, under particles, HUD,
	// letterbox, subtitles, dialog, loot and menu (src/Canvas.cpp:391-417,
	// src/Canvas.cpp:1346-1355, src/MayaCamera.cpp:311-323).
	sys_.hud->drawFade(g, upTimeMs);

	// REPAINT_PARTICLES is set only by the playing/dialog/camera/dying runners
	// (src/GameStateRunner.cpp:82,192,268,275,291,309,316, src/Canvas.cpp:923,
	// src/MovementController.cpp:393,521); drawn before the letterbox fills.
	if (sys_.game->loadMapID != 0 && sys_.world->initialized() && sys_.ui != nullptr &&
	    (cinePose != nullptr || state_ == StateId::Playing || state_ == StateId::Dialog ||
	     state_ == StateId::Camera || state_ == StateId::Dying || state_ == StateId::BotDying)) {
		drawParticles(g, sys_.game->particles, scene_.camera(), sys_.world->occlusion(), gameTime,
		              sys_.player->isFamiliar,   // postProcessMode != 0 (src/Player.cpp:2500-2516), same gate as drawFamiliarOverlay
		              sys_.ui->art().gibs);
	}

	// Cinematic letterbox: two opaque black fills painted after the world
	// pass and the cockpit art, so the top one overpaints rows 0..41 of the
	// world band (eraseRgn = setColor(0) + fillRect, src/Hud.cpp:455-456;
	// rects from cinRect[1]=42, displayRect[2]=480, softKeyY=320).
	// Deliberately keyed on `state`, NOT on the cinematic pose: with a dialog box up over
	// an active camera the legacy repaintFlags = 47 skips the bars
	// (src/Canvas.cpp:1095) so the picture grows 35 rows upward and
	// re-letterboxes on close (src/DialogSystem.cpp:541-554); ST_INTER_CAMERA
	// has no bars either (src/Canvas.cpp:1213, src/MovementController.cpp:394).
	if (state_ == StateId::Camera) {
		g.fillRect(0, 0, 480, 42, 0, 0, 0);
		g.fillRect(0, 292, 480, 28, 0, 0, 0);
	}

	// Health-bar feed (spec §0.F): resolve the facing probe into LIVE
	// ET_MONSTER stats every frame; -1 hides the bar (legacy gates
	// src/Hud.cpp:825-835). Then the top bar gains its real caller for the
	// gameplay states — messages stay solely in drawMessages.
	if (state_ == StateId::Playing) {
		// Legacy call site: Hud::draw forces the probe right before drawTopBar
		// while ST_PLAYING (src/Hud.cpp:735-742). facingDirty stays as an
		// advisory latch (many sites set it) but no longer gates the probe.
		targeting_.updateFacingProbe();
		sys_.game->facingDirty = false;
	}
	targeting_.feedHealthBar();
	if (gameplayView) {
		// Scope mask first in repaint bit 0x1 (src/Hud.cpp:728-733).
		if (sys_.game->zoom.isZoomedIn) sys_.hud->drawScope(g);
		// Buff box, before the damage arrow inside drawEffects (src/Hud.cpp:505-507);
		// skipped in ST_DIALOG (src/Player.cpp:1211). ST_DYING is not a gameplayView.
		if (state_ != StateId::Dialog && sys_.player->numbuffs != 0) {
			sys_.hud->drawBuffs(g, *sys_.font, sys_.player->buffs, sys_.player->numbuffs,
				upTimeMs - sys_.player->turnTime < 600);
		}
		// Repaint bit 0x1 (drawEffects) before 0x2 (drawTopBar), src/Hud.cpp:725-742.
		sys_.hud->drawDamageEffects(g, upTimeMs, sys_.game->combat.totalDamage > 0, sys_.player->isFamiliar);
		sys_.hud->drawTopBar(g, *sys_.font, 480, sys_.player->isFamiliar);
	}

	// Bottom bar through the UI layer (spec §4.2): legacy repaint bit 0x4,
	// drawn right after the 0x2 group (src/Hud.cpp:747-782) and on top of the
	// panel background that drawTopBar paints. The per-state mask is 47 for
	// Playing/Combat/Dialog/Dying and excludes ST_CAMERA (24) and
	// ST_INTER_CAMERA (43), which is exactly `gameplayView` here — the gate
	// stays here and travels into the model as showBottomBar.
	// Drawn but not touchable while a modal overlay owns input: legacy
	// touchStart dispatches ST_DIALOG to m_dialogButtons only
	// (src/TouchController.cpp:96-98) and ST_LOOTING to a bare handleEvent(6)
	// (src/TouchController.cpp:29-31), so Hud::handleUserTouch — the owner of
	// the two 52x64 corner buttons — is never reached in either state.
	const bool hudInteractive = gameplayView && state_ != StateId::Dialog &&
	                            state_ != StateId::Looting;
	// ST_BOT_DYING paints only the bottom bar (hud->repaintFlags = 36,
	// src/GameStateRunner.cpp:287) with cleared soft keys (src/Canvas.cpp:1133),
	// over the sentry-bot panel; nothing is touchable.
	const bool botDying = state_ == StateId::BotDying;
	if (botDying) sys_.hud->drawBottomPanel(g, true);
	if (ui != nullptr) {
		hudButtons_.trackPointer(ui->in());
		HudModel hud;
		buildHudModel(hud, gameplayView || botDying, hudInteractive);
		if (botDying) {
			hud.softLeft = hud.softCenter = hud.softRight = nullptr;
			hud.softLeftHit = hud.softRightHit = UiRect{};
		}
		HudButtons::Frame frame;
		frame.upTimeMs = upTimeMs;
		frame.blockInput = blockInputTime != 0;   // the raw latch, src/Hud.cpp:1238
		frame.combat = sys_.game->combat.active;
		frame.zoomed = sys_.game->zoom.isZoomedIn;
		const UiResult r = hudButtons_.resolve(drawHud(*ui, hud), *ui, hud, frame);
		applyUiAction(r.action, r.index);
	}
	// Zoom dial in repaint bit 0x4, outside ST_DIALOG (src/Hud.cpp:747-768).
	const ZoomState& zs = sys_.game->zoom;
	if (gameplayView && zs.isZoomedIn && state_ != StateId::Dialog) {
		sys_.hud->drawScopeDial(g, zs.zoomCurFOVPercent, zs.zoomMinFOVPercent);
	}
	// Range score bar whenever the session runs, any state (src/Canvas.cpp:419-421).
	if (practice_.active() && sys_.loc != nullptr) {
		practiceScoreText_.setLength(0);
		practiceScoreText_.append(sys_.loc->get(kTextMain, 229) + std::to_string(practice_.score()));
		sys_.hud->drawTargetPracticeScore(g, *sys_.font, practiceScoreText_);
	}

	// Messages overlay while cockpit/HUD stay hidden. Not while dying:
	// hud->repaintFlags = 32 there (src/GameStateRunner.cpp:259), so Hud::draw
	// paints nothing over the fade (src/Hud.cpp:723-819).
	// Same for ST_BOT_DYING (repaintFlags = 36, src/GameStateRunner.cpp:287).
	if (state_ != StateId::Dying && !botDying) sys_.hud->drawMessages(g, *sys_.font);

	// DEBUG (rewrite-only, not a port): player coordinate readout, B toggles
	// it (GameLoop.cpp). view* is the interpolated actual position, so the
	// numbers track the walk animation instead of snapping to the step target.
	if (gameplayView) {
		sys_.hud->drawDebugPosition(g, *sys_.font, sys_.player->viewX,
			sys_.player->viewY, sys_.player->viewAngle);
	}

	// Dialog box overlay (legacy backPaint -> dialogState,
	// src/Canvas.cpp:447-449), now split into the producer's model and the
	// view (spec §7). buildViewModel advances the typewriter exactly like the
	// draw call it replaced, so it runs once per rendered frame, here.
	if (state_ == StateId::Dialog && ui != nullptr) {
		DialogViewModel dlg;
		if (sys_.dialogs->buildViewModel(dlg)) {
			dlg.portraitRow = std::clamp(sys_.player->characterChoice - 1, 0, 2);
			const UiResult r = drawDialog(*ui, dlg);
			applyUiAction(r.action, r.index);
		}
	}

	// Loot list overlay during the dwell window — paints OVER world+HUD
	// (src/Canvas.cpp:414-417,469-472), now split into the producer's model and
	// the view (spec §5). buildViewModel carries the dwell gate the old draw
	// call opened with, so it stays the thing that decides nothing is drawn.
	if (state_ == StateId::Looting && ui != nullptr) {
		LootListModel loot;
		if (loot_.buildViewModel(loot)) {
			const UiResult r = drawLootList(*ui, loot);
			applyUiAction(r.action, r.index);
		}
	}

	// Loading / saving box (src/LoadingScreenController.cpp:17-100): it erases
	// the whole screen first, so the world drawn above never shows.
	if ((state_ == StateId::Loading || state_ == StateId::Saving) && ui != nullptr) {
		if (upTimeMs - lastPacifierMs_ >= kPacifierMs) {          // updateLoadingBar throttle (:22-27)
			lastPacifierMs_ = upTimeMs;
			pacifierX_ += 10;
			if (pacifierX_ >= kPacifierEnd || pacifierX_ < kPacifierStart) pacifierX_ = kPacifierStart;
		}
		int id = loading_.captionId();
		if (id == -1) id = sys_.loc->makeId(kTextMain, 57);      // "Processing..." (:30-32)
		loadingCaption_.setLength(0);
		loadingCaption_.append(sys_.loc->get(sys_.loc->typeOf(id), sys_.loc->indexOf(id)));
		loadingCaption_.dehyphenate();
		loadingSubCaption_.setLength(0);
		loadingSubCaption_.append(sys_.loc->get(kTextMain, 58));  // "Just a moment!"
		loadingSubCaption_.dehyphenate();
		LoadingModel lm;
		lm.caption = &loadingCaption_;
		lm.subCaption = &loadingSubCaption_;
		lm.pacifierX = pacifierX_;
		drawLoading(*ui, lm);
	}

	// In-game menu (spec 2026-08-28-menu §7.1): drawn last because it OWNS the
	// frame — its 480x320 background is opaque, so the world pass above (which
	// keeps running, NEW FACT 1) and the empty message overlay are completely
	// covered. The HUD bars and the view weapon are already excluded by
	// `gameplayView`, which does not list Menu.
	if (state_ == StateId::Menu && ui != nullptr) {
		if (menu_.menuId() == kMenuComicBook) {
			// paint hands the frame to comicBook->Draw and returns: no menu
			// background, soft keys or scrollbar (src/MenuSystem.cpp:721-724).
			const UiResult r = comic_.draw(*ui);
			applyUiAction(r.action, r.index);
		} else {
			// Touch drag scrolling, before the model is built: it sets both the
			// frame's scroll offset and whether the rows take hits at all
			// (src/MenuSystem.cpp:4869-4913).
			menu_.updateDrag(ui->in());
			MenuViewModel mv;
			if (menu_.buildViewModel(mv)) {
				const UiResult r = mv.mainSkin ? drawMainMenu(*ui, mv) : drawMenu(*ui, mv);
				applyUiAction(r.action, r.index);
			}
		}
	}

	// Startup logo (src/Canvas.cpp:487-491): l2.bmp centred on black.
	if (state_ == StateId::Logo && ui != nullptr) {
		ui->panel(UiRect{ 0, 0, 480, 320 }, 0xFF000000);
		ui->image(ui->art().logoL2, 240, 160, Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter);
	}

	// Intro movie (playIntroMovie, src/IntroSequenceManager.cpp:752-859): opaque 480x320.
	if (state_ == StateId::IntroMovie && ui != nullptr) {
		IntroMovieModel im;
		introMovie_.buildModel(im);
		const UiResult r = drawIntroMovie(*ui, im);
		applyUiAction(r.action, r.index);
	}

	// Credits (drawCredits, src/IntroSequenceManager.cpp:190-245): opaque 480x320.
	if (state_ == StateId::Credits && ui != nullptr) {
		CreditsModel cm;
		credits_.buildModel(cm);
		applyUiAction(drawCredits(*ui, cm).action, -1);
	}

	// Character select (drawCharacterSelection, src/IntroSequenceManager.cpp:341-462): opaque 480x320.
	if (state_ == StateId::CharacterSelect && ui != nullptr) {
		CharacterSelectModel cm;
		charSelect_.buildModel(cm);
		const UiResult r = drawCharacterSelect(*ui, cm);
		applyUiAction(r.action, r.index);
	}

	// Prologue (drawStory, src/IntroSequenceManager.cpp:260-323): opaque 480x320,
	// drawn only while a page is left (:268), like the legacy guard.
	if (state_ == StateId::Intro && ui != nullptr && !prologue_.done()) {
		PrologueModel pm;
		prologue_.buildModel(pm);
		const UiResult r = drawPrologue(*ui, pm);
		applyUiAction(r.action, r.index);
	}

	// Travel map (drawTravelMap, src/TravelMapManager.cpp:146-256): opaque
	// 480x320, drawn last. Its strings are resolved here, ui/ has no Localization.
	if (state_ == StateId::TravelMap && ui != nullptr) {
		TravelMapModel tm;
		travel_.buildModel(tm);
		const Localization& loc = *sys_.loc;
		if (tm.pathLabelId >= 0) {
			travelLabelText_.setLength(0);
			travelLabelText_.append(loc.get(kTextIngame2, tm.pathLabelId));
			travelLabelText_.dehyphenate();
			tm.pathLabelText = &travelLabelText_;
		}
		if (tm.nameId >= 0) {
			travelNameText_.setLength(0);
			travelNameText_.append("^3");                       // currentCharColor = 3 (:427)
			travelNameText_.append(loc.get(kTextIngame2, tm.nameId));
			travelNameText_.dehyphenate();
			tm.nameText = &travelNameText_;
		}
		if (tm.prompt) {
			travelPromptText_.setLength(0);
			travelPromptText_.append(loc.get(kTextMain, 96));   // "Tap Screen to Continue" (:218-221)
			travelPromptText_.wrapText(24);
			travelPromptText_.dehyphenate();
			tm.promptText = &travelPromptText_;
		}
		const UiResult r = drawTravelMap(*ui, tm);
		applyUiAction(r.action, r.index);
	}

	// Minigame screens open with an opaque 480x320 blit (spec §4.3), so the
	// world pass above is covered; `gameplayView` excludes HUD and weapon.
	if (state_ == StateId::MiniGame && ui != nullptr) {
		const UiResult r = minigames_.draw(*ui);
		applyUiAction(r.action, r.index);
	}

	// Automap (drawAutomap, src/AutomapController.cpp:65-348): opaque 480x320
	// over the world pass, which keeps running for the seen marks (ADR 0036).
	if (state_ == StateId::Automap && ui != nullptr) {
		Automap::buildModel(automapModel_, *sys_.map, sys_.game->db, *sys_.player, upTimeMs, automapBlink_);
		setSoftKeyText(automapLeftText_, sys_.loc->get(kTextMain, 52));    // "Menu"  (src/SoftKeyController.cpp:88-89)
		setSoftKeyText(automapRightText_, sys_.loc->get(kTextMain, 53));   // "Leave"
		automapModel_.softLeft = &automapLeftText_;
		automapModel_.softRight = &automapRightText_;
		const UiResult r = drawAutomap(*ui, automapModel_);
		applyUiAction(r.action, r.index);
	}

	if (ui != nullptr) {
		playPressSound(ui->state().pressedId, blockInputTime != 0);
		ui->endFrame();
	}

	renderer.endFrame(app.window());
	++framesRendered_;
}

} // namespace newcore
