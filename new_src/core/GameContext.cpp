#include "core/GameContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/AppContext.h"
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
#include "render/RenderBackend.h"
#include "text/Font.h"
#include "ui/DialogView.h"
#include "ui/Hud.h"
#include "ui/HudView.h"
#include "ui/LootView.h"
#include "ui/MenuView.h"
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
	menuEnv.host = this;
	menuEnv.upTimeMs = &upTimeMs;
	menu_.init(menuEnv);
	Targeting::Env tgtEnv;
	tgtEnv.game = sys_.game;
	tgtEnv.player = sys_.player;
	tgtEnv.map = sys_.map;
	tgtEnv.tables = sys_.tables;
	tgtEnv.hud = sys_.hud;
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
	actions_.init(actEnv);
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
	case StateId::Dialog:
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
		deathTimeMs_ = upTimeMs; // unused this phase (src/Canvas.cpp:1125-1131 analog)
		break;
	case StateId::Looting:
		// The session itself (pose latch + poolLoot) lives in LootSession;
		// clearing the queued events stays state-machine business.
		pendingActions_.clear();
		loot_.begin();
		break;
	case StateId::Menu:
		// src/Canvas.cpp:1192-1206 analog: the queued events are cleared and the
		// menu system is set to MENU_INGAME with a fresh stack. The legacy
		// pausePlayer() has no rewrite counterpart (no pause flag).
		pendingActions_.clear();
		menu_.begin();
		break;
	case StateId::Loading:
		loadingPhase_ = 0;     // arm the loading-phase counter
		break;
	}
}

// ---- frame ----

void GameContext::tick() {
	upTimeMs += kTickMs;                                   // app->upTimeMs / app->time
	// gameTime advances in PLAYING, CAMERA and LOOTING (scripts + the camera
	// clock share it, src/Game.cpp:3259; the loot crouch runs on the shared
	// clock too). Frozen elsewhere (deviation C5, narrowed by the cinematic
	// group). Freezing during Dialog is what pauses the camera clock over a
	// dialog (legacy activeCameraTime flip, src/Canvas.cpp:1092-1094).
	if (!pauseGameTime && (state_ == StateId::Playing || state_ == StateId::Camera ||
	                       state_ == StateId::Looting)) gameTime += kTickMs;

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
		if (state_ == StateId::Menu) { menu_.handleAction(a); continue; }
		if (blocked || state_ != StateId::Playing) break;
		actions_.handleAction(a);
	}
	pendingActions_.clear();

	// Globals each tick: UpdatePlayerVars + runScriptThreads
	// (src/Canvas.cpp:791-796); gsprite_update has no counterpart.
	// Threads tick in PLAYING and CAMERA only (src/Game.cpp:3259): during a
	// cinematic the scripts keep running — input is what's parked.
	sys_.game->trace.setPlayerPos(sys_.player->viewX, sys_.player->viewY);
	if (state_ == StateId::Playing || state_ == StateId::Camera) sys_.vm->runScriptThreads(gameTime);

	// SpriteLerp pool + door anims tick in every state legacy covers with
	// updateLerpSprites: PLAYING/INTER_CAMERA (src/GameStateRunner.cpp:25,
	// :184), ST_DIALOG (src/Canvas.cpp:921), ST_CAMERA (:953); automap
	// (src/AutomapController.cpp:22) and combat have no rewrite counterpart.
	// Centralized here so no state can regress out of coverage again. Safe
	// under a dialog: legacy resumes blocking door/lerp owners from its
	// ST_DIALOG branch too (updateLerpSprites -> callThreads flush). Playing
	// keeps lerps-before-updateView order (src/GameStateRunner.cpp:184-185);
	// in CAMERA this runs before the camera clock (legacy swaps those two —
	// within-tick difference only).
	if (state_ == StateId::Playing || state_ == StateId::InterCamera ||
	    state_ == StateId::Camera || state_ == StateId::Dialog) {
		// Walk-writer view feed (spec §4): the chooser compares move vectors
		// against the last rendered view — maya pose during a cinematic key,
		// else the player view angle (same two sources render() uses,
		// GameContext.cpp:724-769; legacy read app->render->viewAngle).
		sys_.game->lerps.setLerpViewAngle(cinematic_.active()
		                                      ? cinematic_.pose().yaw
		                                      : sys_.player->viewAngle);
		sys_.game->update(kTickMs);
	}

	switch (state_) {
	case StateId::Loading: tickLoading(); break;
	case StateId::Playing: tickPlaying(); break;
	case StateId::InterCamera:
		// ST_INTER_CAMERA: world keeps rendering from the player view while
		// script lerps animate (src/MovementController.cpp:391, :518); parked
		// threads resume via their owners only (runScriptThreads gates above).
		break;
	case StateId::Camera:  tickCamera(); break;
	case StateId::Looting: loot_.tick(); break;
	case StateId::Dialog:
		// Lerps+doors ticked in the globals section above (legacy ST_DIALOG
		// branch, src/Canvas.cpp:920-926); view/input updates do not.
		break;
	case StateId::Dying:   tickDying(); break;
	case StateId::Menu:
		// Nothing ticks while the menu is up: GameStateRunner::menuState only
		// updates soft keys (src/GameStateRunner.cpp:203-253). No lerps, no
		// doors, no scripts, no view update — and gameTime does not advance
		// either (the clock gate above lists Playing/Camera/Looting only).
		break;
	}

	// Screen shake randomize/expire, every state incl. Playing (legacy
	// updateView block, src/MovementController.cpp:381-388).
	sys_.hud->tickShake(upTimeMs);

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
		// [unload-slot: game.unloadMapData()/render.unloadMap() — no-op on first boot]
		sys_.vm->clearStateVars(15);                       // zero scriptStateVars except vars[15] (src/LoadingManager.cpp:631-635)
		loadingPhase_ = 1;
		return;                                            // bar frame in legacy (src/LoadingManager.cpp:610-616)
	}

	// phase 1 (src/LoadingManager.cpp:617-744)
	sys_.vm->resetPool();                                  // 20 threads freed (src/Game.cpp:332-360)
	// Difficulty stamped BEFORE loadEntities so the spawn-time +25% hp bump
	// sees it (spec §1; Group-2 order fix pulled forward — Group 1's own
	// acceptance checks hp=62 at difficulty 2).
	sys_.vm->vars[12] = 2;                                 // difficulty default (:692)
	sys_.game->loadEntities(*sys_.map, *sys_.defs);        // (:658); loadWorldState slot: fresh entry no-op (:670)
	actions_.spawnPlayer();                                // (:673)
	lastTurnTime_ = upTimeMs;                              // stamp kept at the call site (spec §P1-G7)
	std::fprintf(stderr, "[load] staticFunc(0)\n");
	sys_.vm->executeStaticFunc(Enums::SCR_INIT_MAP);       // SCR_INIT_MAP (:691-693)
	// staticFunc(1): completed-game only — no caller in the subset.
	// prevX/Y = view snap: save-only fields, not ported.
	sys_.vm->executeTile(sys_.player->viewX >> 6, sys_.player->viewY >> 6, 4081, true); // entrance event, ENTER|all-dirs (:700-706)
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
	std::fprintf(stderr, "[load] -> %s (blockInput 200ms)\n",
		state_ == StateId::Camera ? "ST_CAMERA (kept)" : "ST_PLAYING");
}

// ---- Playing (fixed order, spec §6) ----

void GameContext::tickPlaying() {
	// 1. [held-button auto-repeat — n/a, keyboard discrete]
	// 2. deferred turn consumer (dialog-close analog, src/Canvas.cpp:812-815).
	if (sys_.game->queueAdvanceTurn) {
		sys_.game->queueAdvanceTurn = false;
		sys_.game->advanceTurn();
	}
	// 3. death check (src/GameStateRunner.cpp:170-173).
	if (sys_.player->getHealth() <= 0) {
		setState(StateId::Dying);
		return;
	}
	// 4. combat seq / monster phase at the legacy position (:181-183). While
	//    a fire seq runs (ST_COMBAT analog, spec §0.C) it ticks and consumes
	//    the turn on completion; otherwise the monster-phase stub closes the
	//    monstersTurn window opened by advanceTurn.
	//    Per-tick shotsFired reset (spec §C step 4): legacy clears the latch
	//    once per rendered frame (src/Render.cpp:2351) so activate()'s
	//    back-turned wake guard is bypassed only during shot frames.
	sys_.game->combat.shotsFired = false;
	if (sys_.game->combat.active) {
		if (!sys_.game->combat.tick()) {          // runFrame analog (spec §5)
			sys_.game->combat.active = false;
			sys_.game->advanceTurn();             // turn consumed AFTER seq
		}
	} else {
		sys_.game->monsters.updateMonsters();      // Stage-1 stub (spec §0.B)
	}
	// 4.5 The facing probe no longer runs here: legacy recomputes it from the
	//     HUD top bar on every rendered frame (src/Hud.cpp:735-742), see
	//     GameContext::render.
	// 5. door/sprite lerps tick in the globals section (legacy updateLerpSprites
	//    here, src/GameStateRunner.cpp:184), still BEFORE updateView (:185).
	// Help-popup dequeue while playing & monsters idle
	// (src/GameStateRunner.cpp:199; guards live in the dialog system).
	if (sys_.game->monstersTurn == 0) sys_.dialogs->dequeueHelpDialog();
	// 6. updateView (src/MovementController.cpp:377-547).
	bool posIdle = (sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY);
	bool angleIdle = (sys_.player->viewAngle == sys_.player->destAngle);
	sys_.player->updateView();                             // interpolate X/Y/Z/angle
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

void GameContext::tickDying() {
	// ST_DYING stub: legacy runs the fall/fade timeline then the dead menu
	// (spec §11 out of scope). Health can only reach 0 via future combat.
}

// ---- cinematic camera (docs/original-code/cutscenes-camera.md §1-§3) ----

void GameContext::tickCamera() {
	// HUD message/subtitle timers run on the shared clock in legacy
	// (gameTime advances in PLAYING + CAMERA, src/Game.cpp:3259). Skipped on a
	// skip frame: tickCameraState() returns false there, as the pre-
	// decomposition early return did.
	if (cinematic_.tickCameraState()) sys_.hud->update(kTickMs);
}

void GameContext::debugGiveKeycards() {
	// PHASE5 DEBUG (removable): grant red/blue keycards so the positive
	// unlock path is testable without the loot system (spec §10 item 8).
	if (sys_.player == nullptr) return;
	sys_.player->give(0, 19, 1);
	sys_.player->give(0, 20, 1);
	std::fprintf(stderr, "[debug] granted keycards inv[19]=%d inv[20]=%d\n",
		sys_.player->inventory[19], sys_.player->inventory[20]);
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

void setSoftKeyText(Text& out, const std::string& s) {
	// composeText + dehyphenate, as the original does for every soft-key label
	// (src/TouchController.cpp:556-561).
	out.setLength(0);
	out.append(s);
	out.dehyphenate();
}

} // namespace

void GameContext::buildHudModel(HudModel& m, bool showBottomBar, bool interactive) {
	m = HudModel{};
	m.showBottomBar = showBottomBar;
	m.interactive = interactive;

	// Producer half of the former Hud::feedPlayerStatus: the RAW live stats the
	// legacy widgets read every draw pass (src/Hud.cpp:683-709).
	const Player& p = *sys_.player;
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

	// Soft keys: gameplay defaults only. The rewrite has no softKeyLeftID/
	// RightID state machine, so the per-screen variants (Exit / Leave /
	// Re-turn / Dis-card, docs/original-code/ui.md §6) are not modelled and the
	// slots stay null outside the bottom-bar states — which is exactly the
	// legacy "== -1" case the view already handles.
	if (showBottomBar && sys_.loc != nullptr) {
		setSoftKeyText(softLeftText_, sys_.loc->get(kTextMain, kSoftKeyMenuId));
		setSoftKeyText(softRightText_, sys_.loc->get(kTextMain, kSoftKeyMapId));
		setSoftKeyText(softCenterText_, "Wait");   // hardcoded ASCII (src/Hud.cpp:715-721)
		m.softLeft = &softLeftText_;
		m.softRight = &softRightText_;
		m.softCenter = &softCenterText_;
		m.softLeftHit = kSoftLeftHit;
		m.softRightHit = kSoftRightHit;
	}
}

// ---- view-weapon view model (spec 2026-08-27-ui-layer §6) ----

void GameContext::buildViewWeaponModel(ViewWeaponModel& m) const {
	m = ViewWeaponModel{};
	if (sys_.player == nullptr || sys_.game == nullptr || sys_.tables == nullptr) return;

	const Player& p = *sys_.player;
	const int w = p.ce.weapon;                                 // (src/Combat.cpp:677)
	if (w < 0 || p.weapons == 0) return;                       // (:706-708)

	// wpinfo table 1: idleX,idleY,atkX,atkY,flashX,flashY signed bytes per
	// weapon (src/Combat.h:53-59). Row missing => draw nothing, same as the
	// former ((size_t)(w * 6 + 5) >= weaponInfo.size()) early return.
	const WeaponPose* pose = sys_.tables->weaponPose(w);
	if (pose == nullptr) return;
	const int idleX = pose->idleX;
	const int idleY = pose->idleY;
	const int atkX  = pose->atkX;
	const int atkY  = pose->atkY;

	// Attack pose: hold (atkX,atkY) until flashDone, then lerp back over
	// animTime in 16.16 (src/Combat.cpp:735-767). b5 reduces to
	// "seq running for this weapon" (curAttacker == nullptr always). The
	// flashDone latch itself lives in Combat::tick now (deviation D2).
	int wpX = idleX, wpY = idleY;
	bool flash = false;
	const Combat& c = sys_.game->combat;
	if (c.active && c.attackerWeaponId == w) {
		wpX = atkX;
		wpY = atkY;
		if (!c.flashDone) {
			flash = ((1 << w) & 0x200) == 0;                   // :741 (weapon 9 excluded)
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

	m.visible = true;
	m.weapon = w;
	m.weaponTile = Combat::getWeaponTileNum(w);
	m.flashTile = Combat::getWeaponTileNum(0);                 // (:826-834)
	m.poseX = wpX;
	m.poseY = wpY;
	// The second legacy flash gate sat at the blit (:826); both are ANDed here
	// so the view only reads a bool. Neither condition is dropped or merged.
	m.muzzleFlash = flash && ((1 << w) & 0x181) != 0;
	m.flashX = pose->flashX;
	m.flashY = pose->flashY;
	// Canvas shake; legacy negates sy first (sy = -|sy|, src/Combat.cpp:709),
	// so the model carries the already-negated value.
	m.shakeX = sys_.hud->shakeX();
	m.shakeY = -std::abs(sys_.hud->shakeY());
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
	Graphics2D& g = renderer.g2d();

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

	scene_.drawWorld(renderer, app.window(), cinePose, underDialog);

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
	if (cinePose == nullptr && !cinematic_.active() && gameplayView) {
		ViewWeaponModel wp;
		buildViewWeaponModel(wp);
		viewWeapon_.draw(g, scene_.camera(), wp);
	}

	// Cockpit overlay while a cinematic renders with the raw toggle set
	// (MayaCamera::Render -> Hud::drawOverlay, src/MayaCamera.cpp:316-318;
	// cinRect = viewRect.x / 42 / viewRect width, src/Canvas.cpp:151-154).
	// The boot intro enables it only around camera 0 (IP 1945-2062).
	if (cinePose != nullptr && sys_.hud->cockpitOverlay()) {
		sys_.hud->drawOverlay(g, 0, 42, 480);
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
		sys_.hud->drawTopBar(g, *sys_.font, 480);
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
	if (ui != nullptr) {
		HudModel hud;
		buildHudModel(hud, gameplayView, hudInteractive);
		const UiResult r = drawHud(*ui, hud);
		applyUiAction(r.action, r.index);
	}

	// Messages overlay while cockpit/HUD stay hidden.
	sys_.hud->drawMessages(g, *sys_.font);

	// Dialog box overlay (legacy backPaint -> dialogState,
	// src/Canvas.cpp:447-449), now split into the producer's model and the
	// view (spec §7). buildViewModel advances the typewriter exactly like the
	// draw call it replaced, so it runs once per rendered frame, here.
	if (state_ == StateId::Dialog && ui != nullptr) {
		DialogViewModel dlg;
		if (sys_.dialogs->buildViewModel(dlg)) {
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

	// In-game menu (spec 2026-08-28-menu §7.1): drawn last because it OWNS the
	// frame — its 480x320 background is opaque, so the world pass above (which
	// keeps running, NEW FACT 1) and the empty message overlay are completely
	// covered. The HUD bars and the view weapon are already excluded by
	// `gameplayView`, which does not list Menu.
	if (state_ == StateId::Menu && ui != nullptr) {
		// Touch drag scrolling, before the model is built: it sets both the
		// frame's scroll offset and whether the rows take hits at all
		// (src/MenuSystem.cpp:4869-4913).
		menu_.updateDrag(ui->in());
		MenuViewModel mv;
		if (menu_.buildViewModel(mv)) {
			const UiResult r = drawMenu(*ui, mv);
			applyUiAction(r.action, r.index);
		}
	}

	if (ui != nullptr) ui->endFrame();

	renderer.endFrame(app.window());
}

} // namespace newcore
