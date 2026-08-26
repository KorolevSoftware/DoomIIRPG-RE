#include "core/GameContext.h"

#include <algorithm>
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
#include "io/Media.h"
#include "io/Tables.h"
#include "render/Graphics2D.h"
#include "render/RenderBackend.h"
#include "render/World3D.h"
#include "text/Font.h"
#include "ui/Hud.h"

namespace newcore {

// ---- setup ----

void GameContext::init(const Init& sys) {
	sys_ = sys;
	if (sys_.tables) {
		camera_.setSinTable(sys_.tables->sinTable.data());
		sys_.game->setSinTable(&sys_.tables->sinTable);   // parabola lerp arc (Game.h)
	}
}

// ---- state machine ----

void GameContext::setState(StateId s) {
	// Dialog-close prior-state restore (src/DialogSystem.cpp:541-554):
	// closeDialog always asks back for ST_PLAYING; redirect while an
	// inter-cinematic/cinematic context survives, INTER_CAMERA > CAMERA
	// priority. dialogPrevState_ latches the pre-dialog state at entry.
	if (s == StateId::Playing && state == StateId::Dialog) {
		if (dialogPrevState_ == StateId::InterCamera) s = StateId::InterCamera;
		else if (activeCameraKey_ >= 0) s = StateId::Camera;
	}
	stateChanged = true;                                   // src/Canvas.cpp:1024
	for (int& v : stateVars) v = 0;                        // sub-state timelines rebuilt per entry (:1024-1026)
	exitState_();
	oldState = state;
	state = s;                                             // :1052-1053
	enterState_(s);
}

void GameContext::exitState_() {
	// Legacy exit hooks live here: ST_AUTOMAP unpauses the player,
	// ST_MENU unpauses + clears the menu stack, ST_CAMERA re-enables render
	// activation + clears skippingCinematic (src/Canvas.cpp:1030-1049).
	if (state == StateId::Camera) skipCinematic_ = false;  // (:1037-1040)
}

void GameContext::enterState_(StateId s) {
	switch (s) {
	case StateId::Playing:
		// Clear pending input actions + stamp the turn clock
		// (src/Canvas.cpp:1101-1106 analog).
		pendingActions_.clear();
		lastTurnTime_ = upTimeMs;
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
		sys_.hud->clearCinematicText();
		break;
	case StateId::Dying:
		deathTimeMs_ = upTimeMs; // unused this phase (src/Canvas.cpp:1125-1131 analog)
		break;
	case StateId::Looting:
		// Legacy setState hook -> LootingSystem::onEnterLooting
		// (src/Canvas.cpp:1142-1143, src/LootingSystem.cpp:26-33): cache the
		// pose + facing and restart the 500 ms clock, crouch phase first.
		// The rewrite caches viewPitch (no destPitch slope machinery yet;
		// identical value here). poolLoot runs immediately after setState
		// (src/PlayingInputHandler.cpp:374-378): every eType-9 entity on the
		// faced tile is marked looted and the list is built NOW, not at close.
		pendingActions_.clear();
		lootDestX_ = sys_.player->viewX;
		lootDestY_ = sys_.player->viewY;
		lootDestZ_ = sys_.player->viewZ;
		lootDestPitch_ = sys_.player->viewPitch;
		lootStepX_ = sys_.player->viewStepX >> 6;
		lootStepY_ = sys_.player->viewStepY >> 6;
		lootTime_ = upTimeMs;
		lootCrouch_ = true;
		lootSettleSfx_ = false;                            // field_0xac5_ (:32)
		{
			int tx = (lootDestX_ + lootStepX_ * 64) >> 6;
			int ty = (lootDestY_ + lootStepY_ * 64) >> 6;
			sys_.game->poolLootCorpse(tx, ty, *sys_.loc, lootPool_);
		}
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
	if (!pauseGameTime && (state == StateId::Playing || state == StateId::Camera ||
	                       state == StateId::Looting)) gameTime += kTickMs;

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
		if (enabled != 0 && state == StateId::Dialog && --dialogCooldown <= 0) {
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
		if (state == StateId::Dialog) {
			sys_.dialogs->handleInput(a);
			continue;
		}
		if (state == StateId::Camera) {
			// Any action past the cinUnpauseTime lockout skips the cinematic
			// (Passturn/Automap/Fire/18 in legacy; the subset set is smaller).
			if (gameTime >= cinUnpauseTime_) skipCinematic_ = true;
			continue;
		}
		if (state == StateId::Looting) { handleLootingAction(a); continue; }
		if (blocked || state != StateId::Playing) break;
		handlePlayingAction(a);
	}
	pendingActions_.clear();

	// Globals each tick: UpdatePlayerVars + runScriptThreads
	// (src/Canvas.cpp:791-796); gsprite_update has no counterpart.
	// Threads tick in PLAYING and CAMERA only (src/Game.cpp:3259): during a
	// cinematic the scripts keep running — input is what's parked.
	sys_.game->setPlayerPos(sys_.player->viewX, sys_.player->viewY);
	if (state == StateId::Playing || state == StateId::Camera) sys_.vm->runScriptThreads(gameTime);

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
	if (state == StateId::Playing || state == StateId::InterCamera ||
	    state == StateId::Camera || state == StateId::Dialog) {
		// Walk-writer view feed (spec §4): the chooser compares move vectors
		// against the last rendered view — maya pose during a cinematic key,
		// else the player view angle (same two sources render() uses,
		// GameContext.cpp:724-769; legacy read app->render->viewAngle).
		sys_.game->setLerpViewAngle(activeCameraKey_ >= 0
		                                ? maya_.pose().yaw
		                                : sys_.player->viewAngle);
		sys_.game->update(kTickMs);
	}

	switch (state) {
	case StateId::Loading: tickLoading(); break;
	case StateId::Playing: tickPlaying(); break;
	case StateId::InterCamera:
		// ST_INTER_CAMERA: world keeps rendering from the player view while
		// script lerps animate (src/MovementController.cpp:391, :518); parked
		// threads resume via their owners only (runScriptThreads gates above).
		break;
	case StateId::Camera:  tickCamera(); break;
	case StateId::Looting: tickLooting(); break;
	case StateId::Dialog:
		// Lerps+doors ticked in the globals section above (legacy ST_DIALOG
		// branch, src/Canvas.cpp:920-926); view/input updates do not.
		break;
	case StateId::Dying:   tickDying(); break;
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
	sys_.game->loadEntities(*sys_.map, *sys_.defs);        // (:658); loadWorldState slot: fresh entry no-op (:670)
	spawnPlayer();                                         // (:673)
	sys_.vm->vars[12] = 2;                                 // difficulty default (:692)
	std::fprintf(stderr, "[load] staticFunc(0)\n");
	sys_.vm->executeStaticFunc(Enums::SCR_INIT_MAP);       // SCR_INIT_MAP (:691-693)
	// staticFunc(1): completed-game only — no caller in the subset.
	// prevX/Y = view snap: save-only fields, not ported.
	sys_.vm->executeTile(sys_.player->viewX >> 6, sys_.player->viewY >> 6, 4081, true); // entrance event, ENTER|all-dirs (:700-706)
	sys_.player->finishRotation();                         // finishRotation(false) analog (:707)
	sys_.game->monstersTurn = 0;                           // endMonstersTurn (:708)
	// uncoverAutomap stub (:709).
	// Enter ST_PLAYING only when no cinematic took over during staticFunc(0)
	// (src/LoadingManager.cpp:715-717 gates on canvas->state == ST_LOADING);
	// stomping a live ST_CAMERA here kept the cockpit overlay gate in
	// render() false forever during the boot intro.
	if (state == StateId::Loading) setState(StateId::Playing);
	pauseGameTime = false;
	blockInputTime = gameTime + 200;
	std::fprintf(stderr, "[load] -> %s (blockInput 200ms)\n",
		state == StateId::Camera ? "ST_CAMERA (kept)" : "ST_PLAYING");
}

void GameContext::spawnPlayer() {
	Player& p = *sys_.player;
	int x, y, dir;
	if (sys_.game->spawnParam != -1) {                     // encoded by a level exit / save
		x = sys_.game->spawnParam & 0x1F;
		y = (sys_.game->spawnParam >> 5) & 0x1F;
		dir = (sys_.game->spawnParam >> 10) & 7;
		sys_.game->spawnParam = -1;
	} else {                                               // map header spawn
		x = sys_.map->spawnIndex % 32;
		y = sys_.map->spawnIndex / 32;
		dir = sys_.map->spawnDir;
	}
	p.viewX = p.destX = x * 64 + 32;
	p.viewY = p.destY = y * 64 + 32;
	p.viewZ = p.destZ = getHeight(p.viewX, p.viewY) + 36;
	p.viewAngle = p.destAngle = (dir << 7) & 0x3FF;
	p.startRotation();
	p.finishRotation(); // player->relink() omitted: player entity not linked in the rewrite
	lastTurnTime_ = upTimeMs;
}

int GameContext::getHeight(int x, int y) const {
	int hx = x & 0x7FF, hy = y & 0x7FF;
	return sys_.map->heightMap[(hy >> 6) * 32 + (hx >> 6)] << 3;
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
	// 4. monster phase placeholder at the legacy position (:181-183) —
	//    updateMonsters has no rewrite counterpart yet (empty world).
	if (sys_.game->monstersTurn != 0) sys_.game->monstersTurn = 0;
	// 5. door/sprite lerps tick in the globals section (legacy updateLerpSprites
	//    here, src/GameStateRunner.cpp:184), still BEFORE updateView (:185).
	// Help-popup dequeue while playing & monsters idle
	// (src/GameStateRunner.cpp:199; guards live in the dialog system).
	if (sys_.game->monstersTurn == 0) sys_.dialogs->dequeueHelpDialog();
	// 6. updateView (src/MovementController.cpp:377-547).
	bool posIdle = (sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY);
	bool angleIdle = (sys_.player->viewAngle == sys_.player->destAngle);
	sys_.player->updateView();                             // interpolate X/Y/Z/angle
	if (gotoTriggered_) {
		// Instant-GOTO deferred destination events: fresh default direction
		// mask, ENTER then FACE (src/MovementController.cpp:503-509). Runs
		// INSTEAD of finishMovement this frame, like the legacy else-if.
		gotoTriggered_ = false;
		sys_.game->eventFlagsForMovement(-1, -1, -1, -1);
		sys_.vm->executeTile(sys_.player->destX >> 6, sys_.player->destY >> 6,
			sys_.game->eventFlags_[1], true);
		sys_.vm->executeTile(sys_.player->destX >> 6, sys_.player->destY >> 6,
			flagForFacingDir(8), true);
	} else if (!posIdle && sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY) {
		finishMovement();                                  // (:510-512)
	}
	if (!angleIdle && sys_.player->viewAngle == sys_.player->destAngle) {
		finishRotationFired();                             // (:514-516)
	}
	// A cinematic survives into ST_PLAYING here: legacy updateView keeps
	// driving the active camera whenever isCameraActive() regardless of the
	// canvas state (src/MovementController.cpp:518-520) — e.g. a staticFunc
	// STARTCINEMATIC that boot's ST_PLAYING transition overwrites.
	if (activeCameraKey_ >= 0) tickCinematicClock();
	// 7. camera pull-back + scene draw happen in render().
	// HUD message timers tick with the playing state (legacy hud->update,
	// src/Hud.cpp:1365-1378 analog).
	sys_.hud->update(kTickMs);

	// TEMP [dbg] auto-walk driver (remove after silent-tile-events bug
	// fixed): D2R_AUTOTEST=N queues N Forward steps while fully idle and
	// unblocked, so tile-event arrival can be verified headlessly;
	// D2R_AUTOUSE=N likewise queues N Use presses afterwards;
	// D2R_AUTOKEYS=1 grants the debug keycards first.
	{
		static int stepsLeft = -1;
		static int usesLeft = -1;
		static int cooldown = 0;
		if (stepsLeft < 0) {
			const char* env = std::getenv("D2R_AUTOTEST");
			stepsLeft = env != nullptr ? std::atoi(env) : 0;
			env = std::getenv("D2R_AUTOUSE");
			usesLeft = env != nullptr ? std::atoi(env) : 0;
			env = std::getenv("D2R_AUTOKEYS");
			if (env != nullptr && std::atoi(env) != 0) debugGiveKeycards();
		}
		bool idle = sys_.player->viewX == sys_.player->destX &&
		            sys_.player->viewY == sys_.player->destY &&
		            sys_.player->viewAngle == sys_.player->destAngle;
		if (stepsLeft > 0 && idle && activeCameraKey_ < 0 && !inputBlocked() &&
		    --cooldown <= 0) {
			--stepsLeft;
			cooldown = 40; // ~600 ms between steps
			std::fprintf(stderr, "[dbg] autotest queue Forward (%d left)\n", stepsLeft);
			pendingActions_.push_back(Action::Forward);
		} else if (usesLeft > 0 && activeCameraKey_ < 0 && !inputBlocked() &&
		           --cooldown <= 0) {
			--usesLeft;
			cooldown = 200; // ~3 s between uses
			std::fprintf(stderr, "[dbg] autotest queue Use (%d left)\n", usesLeft);
			pendingActions_.push_back(Action::Use);
		}
	}
}

void GameContext::finishMovement() {
	Player& p = *sys_.player;
	// A parked scripted-walk thread resumes FIRST, once its angle settled
	// (src/MovementController.cpp:164-167). Capture-clear-run order (the
	// legacy run()-then-clear would clobber a re-park issued by the resumed
	// script; finishRotation's capture form at :298-302 is the safe shape).
	if (gotoThread_ != nullptr && p.viewAngle == p.destAngle) {
		ScriptThread* t = gotoThread_;
		gotoThread_ = nullptr;
		sys_.vm->resumeThread(t);
	}
	// FACE then ENTER on the destination tile (src/MovementController.cpp:168-169).
	int faceMask = flagForFacingDir(8);
	int faceRes = sys_.vm->executeTile(p.destX >> 6, p.destY >> 6, faceMask, true);
	int enterMask = sys_.game->eventFlags_[1];
	int enterRes = sys_.vm->executeTile(p.destX >> 6, p.destY >> 6, enterMask, true);
	// TEMP [dbg] arrival audit (remove after silent-tile-events bug fixed)
	std::fprintf(stderr, "[dbg] finishMovement destTile=%d,%d face=0x%X->%d enter=0x%X->%d\n",
		p.destX >> 6, p.destY >> 6, faceMask, faceRes, enterMask, enterRes);
	sys_.game->touchTile(p.destX, p.destY, true);          // canvas units, like legacy (:170)
	// advanceTurn unless a script claimed/skipped the turn this arrival; a
	// still-parked gotoThread (angle not settled yet, or re-parked by the
	// resumed script) means a scripted walk — no turn consumed
	// (src/MovementController.cpp:178).
	if (gotoThread_ == nullptr && sys_.game->monstersTurn == 0 && !sys_.game->skipAdvanceTurn) {
		sys_.game->advanceTurn();
	}
}

void GameContext::finishRotationFired() {
	// Recompute step vectors, then fire the rotation-arrival FACE event
	// (src/MovementController.cpp:284-310, :307). Safe to snap here: input is
	// gated on full idle, so a turn never overlaps a move in the subset.
	sys_.player->finishRotation();
	// Same handshake as finishMovement: the rotation was the last leg of a
	// scripted GOTO/TURN_PLAYER (src/MovementController.cpp:298-302).
	if (gotoThread_ != nullptr &&
		sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY) {
		ScriptThread* t = gotoThread_;
		gotoThread_ = nullptr;
		sys_.vm->resumeThread(t);
	}
	sys_.vm->executeTile(sys_.player->destX >> 6, sys_.player->destY >> 6, flagForFacingDir(8), true);
}

int GameContext::flagForFacingDir(int i) const {
	int destAngle = sys_.player->destAngle;
	if (i == 4) destAngle += 512;                          // look backward for use/triggers
	if (i == 4 || i == 8) {
		return i | (1 << (((destAngle & 0x3FF) >> 7) + 4));
	}
	return 0;
}

void GameContext::tickDying() {
	// ST_DYING stub: legacy runs the fall/fade timeline then the dead menu
	// (spec §11 out of scope). Health can only reach 0 via future combat.
}

// ---- loot-crouch camera (docs/research/2026-08-25-camera-pitch-loot.md) ----

void GameContext::tickLooting() {
	// LootingSystem::lootingState (src/LootingSystem.cpp:35-83): crouch lerp
	// 500 ms -> dwell (settled crouch pose held every tick, sound 1055 once,
	// loot menu drawn + input live) -> stand-up lerp 500 ms on close ->
	// snap + advanceTurn. The pose is recomputed from scratch every tick,
	// formulas verbatim (16.16 fraction n: remaining, n2: elapsed). Lerps and
	// doors do NOT tick — legacy ST_LOOTING calls lootingState() only
	// (src/Canvas.cpp:940-942).
	Player& p = *sys_.player;
	int t = (int)(upTimeMs - lootTime_);
	if (t < kLootPhaseMs) {
		int n = ((kLootPhaseMs - t) << 16) / kLootPhaseMs; // (:43)
		int n2 = 65536 - n;
		int h0 = getHeight(lootDestX_, lootDestY_);
		int h1 = getHeight(lootDestX_ + lootStepX_ * 64, lootDestY_ + lootStepY_ * 64);
		if (lootCrouch_) {                              // crouch down (:46-50)
			int hb = (h0 > h1) ? h0 : ((h0 * n + h1 * n2) >> 16);
			p.viewX = lootDestX_ + (48 + ((-48 * n) >> 16)) * lootStepX_;
			p.viewY = lootDestY_ + (48 + ((-48 * n) >> 16)) * lootStepY_;
			p.viewZ = hb + 26 + ((10 * n) >> 16);
			p.viewPitch = std::max(-(64 - ((64 * n) >> 16)) + lootDestPitch_, -64);
		} else {                                        // stand up (:53-57)
			int hb = (h0 > h1) ? h0 : ((h0 * n2 + h1 * n) >> 16);
			p.viewX = lootDestX_ + ((48 * n) >> 16) * lootStepX_;
			p.viewY = lootDestY_ + ((48 * n) >> 16) * lootStepY_;
			p.viewZ = hb + 36 + ((-10 * n) >> 16);
			p.viewPitch = std::max(-((64 * n) >> 16) + lootDestPitch_, -64);
		}
		return;
	}
	if (lootCrouch_) {
		// Crouch settled -> DWELL (:66-71): hold the end pose every tick and
		// play sound 1055 once per session (field_0xac5_ latch :62-65). The
		// clock is NOT restarted; closeLootSession (input) starts stand-up.
		int h0 = getHeight(lootDestX_, lootDestY_);
		int h1 = getHeight(lootDestX_ + lootStepX_ * 64, lootDestY_ + lootStepY_ * 64);
		p.viewX = lootDestX_ + 48 * lootStepX_;
		p.viewY = lootDestY_ + 48 * lootStepY_;
		p.viewZ = std::max(h0, h1) + 26;
		p.viewPitch = std::max(lootDestPitch_ - 64, -64);
		if (!lootSettleSfx_) {
			lootSettleSfx_ = true;
			std::fprintf(stderr, "[loot] sound 1055\n");
		}
	} else {
		// Stand-up expiry: snap home and close the session (:74-80); the turn
		// is consumed only now.
		p.viewX = lootDestX_;
		p.viewY = lootDestY_;
		p.viewZ = getHeight(lootDestX_, lootDestY_) + 36;
		p.viewPitch = lootDestPitch_;
		setState(StateId::Playing);
		sys_.game->advanceTurn();
	}
}

// ---- loot dwell session (src/LoothingSystem.cpp:85-150) ----

void GameContext::handleLootingAction(Action a) {
	if (!lootCrouch_ || upTimeMs <= lootTime_ + kLootPhaseMs) return;  // (:87)
	int maxLine = std::max(Game::LootPool::lineCount(lootPool_) - 3, 0);
	switch (a) {
	case Action::Use:                                   // ACTION_FIRE
		if (lootPool_.topLine >= maxLine) closeLootSession();
		else lootPool_.topLine = std::min(lootPool_.topLine + 3, maxLine);
		break;
	case Action::Passturn:
	case Action::BackKey:                closeLootSession(); break;
	case Action::Forward:  lootPool_.topLine = std::max(lootPool_.topLine - 1, 0); break;
	case Action::Back:     lootPool_.topLine = std::min(lootPool_.topLine + 1, maxLine); break;
	case Action::TurnLeft:  lootPool_.topLine = 0; break;
	case Action::TurnRight: lootPool_.topLine = maxLine; break;
	default: break;                                     // other ids ignored
	}
}

void GameContext::closeLootSession() {
	sys_.game->giveLootPool(lootPool_, *sys_.player, sys_.tables);
	lootCrouch_ = false;
	lootTime_ = upTimeMs;                 // stand-up starts now, zero extra delay
}

namespace {

void fillArgb(Graphics2D& g, int x, int y, int w, int h, uint32_t argb) {
	g.fillRect(x, y, w, h, (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb);
}

void rectArgb(Graphics2D& g, int x, int y, int w, int h, uint32_t argb) {
	g.drawRect(x, y, w, h, (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb);
}

} // namespace

void GameContext::drawLootingMenu(Graphics2D& g) {
	if (!(lootCrouch_ && upTimeMs > lootTime_ + kLootPhaseMs)) return; // (:121-122)
	if (lootPool_.text.length() == 0 || sys_.font == nullptr) return;
	constexpr int kViewY = 20;            // viewRect[1] (src/Canvas.cpp:124-127)
	constexpr int kScrCx = 240;           // Canvas::SCR_CX
	const int dx = 0, dy = kViewY + 16, dw = 480 - 1, dh = 48;   // dialogRect (:123-127)
	fillArgb(g, dx, dy, dw, dh, 0xFF660000u);                    // body (:128-129)
	fillArgb(g, dx, dy - 18, dw, 18, 0xFF000000u);               // title bar (:130-131)
	rectArgb(g, dx, dy - 18, dw, 18, 0xFFFFFFFFu);               // (:132-133)
	rectArgb(g, dx, dy, dw, dh, 0xFFFFFFFFu);                    // (:134)
	Text title;                                                  // (:135-139)
	title.append(sys_.loc->get(kTextMain, 227));
	title.dehyphenate();
	g.drawString(*sys_.font, title, kScrCx, dy - 16, Graphics2D::kAnchorHCenter, 16);
	for (int i = 0; i < 3; ++i) {                                // (:140-144)
		int line = i + lootPool_.topLine;
		if (line < 0 || line >= Game::LootPool::kMaxLines) continue;
		g.drawString(*sys_.font, lootPool_.text, dx + 5, dy + 1 + i * 16,
		    Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft, 16,
		    lootPool_.lineIndex[line * 2], lootPool_.lineIndex[line * 2 + 1]);
	}
	int total = Game::LootPool::lineCount(lootPool_);
	if (total > 3)                                               // (:145-150)
		sys_.dialogs->drawScrollBar(g, dx + dw, dy + 1, dh - 1, lootPool_.topLine,
		    std::min(lootPool_.topLine + 3, total), total, 3);
}

// ---- cinematic camera (docs/original-code/cutscenes-camera.md §1-§3) ----

void GameContext::startCinematic(int camIdx) {
	Player& p = *sys_.player;
	MayaPose pose;                     // map units; <<4 applied post-inherit
	pose.x = p.viewX;
	pose.y = p.viewY;
	pose.z = p.viewZ;
	pose.yaw = p.viewAngle & 0x3FF;    // viewPitch/viewRoll have no rewrite counterpart yet
	if (!maya_.setup(*sys_.map, camIdx, pose)) {
		std::fprintf(stderr, "[camera] bad camIdx %d\n", camIdx);
		return;
	}
	cameraCamIdx_ = camIdx;
	activeCameraKey_ = 0;
	cameraStartTime_ = gameTime;       // legacy activeCameraTime (src/Canvas.cpp:1092)
	cinUnpauseTime_ = gameTime + 1000; // skip lockout (src/ScriptThread.cpp:227-228)
	skipCinematic_ = false;
	setState(StateId::Camera);         // src/ScriptThread.cpp:400-411
}

void GameContext::nextKey() {
	// MayaCamera::NextKey (src/MayaCamera.cpp:36-44): restart the clock at
	// now and move to the next key. The ONLY place the key index advances
	// besides Snap's counting tail (:365-368). NOTE: no end guard here —
	// legacy happily parks PAST the last key; completion is the boundary
	// clock's job (tickCinematicClock). An early finish inside the parking
	// opcode would flush-resume the very thread still being parked
	// mid-dispatch (its IP still on the argument byte), desyncing the VM.
	cameraStartTime_ = gameTime;
	++activeCameraKey_;
}

void GameContext::advanceCameraKey(ScriptThread* t, int resumeCount) {
	// EV_ADV_CAMERAKEY park half (src/ScriptThread.cpp:690-702): unpauseTime=-1
	// parks the thread; each completed key ticks the countdown and the flush
	// resumes it (resumeKeyWaits/flushParkedThreads).
	t->unpauseTime = -1;
	cameraResumeList_.push_back(t);
	cameraResumeCounts_.push_back(resumeCount);
	// The opcode tail calls NextKey() immediately: the remainder of the
	// current key is truncated and the next one starts now
	// (src/ScriptThread.cpp:695, src/MayaCamera.cpp:36-44).
	nextKey();
}

int GameContext::cameraKeyDuration(int key) const {
	// MS channel, channel-major keys[numKeys*CH_MS + k] (src/Game.cpp:606-609),
	// masked &0xFFFF like MayaCamera::Update (src/MayaCamera.cpp:59).
	const MapData::MayaCamera& cam = sys_.map->mayaCameras[cameraCamIdx_];
	return cam.keys[cam.numKeys * 6 + key] & 0xFFFF;
}

void GameContext::tickCamera() {
	// ST_CAMERA per-frame order (src/Canvas.cpp:949-958): camera Update ->
	// updateLerpSprites -> updateView. Lerps tick in the globals section
	// (before the clock — legacy runs them after; within-tick difference
	// only). Door lerps keep animating and parked threads keep ticking —
	// input is what's parked.
	if (skipCinematic_) {
		skipCinematicNow();
		return;
	}
	// TEMP [dbg] auto-skip (headless verification only; remove with the
	// D2R_AUTOTEST driver): past the skip lockout, end the cinematic like a
	// user key press. Inactive unless the env var is set.
	static int autoSkip = -1;
	if (autoSkip < 0) autoSkip = std::getenv("D2R_AUTOTEST") != nullptr ? 1 : 0;
	if (autoSkip != 0 && gameTime >= cinUnpauseTime_ + 1000) skipCinematic_ = true;
	tickCinematicClock();
	// HUD message/subtitle timers run on the shared clock in legacy
	// (gameTime advances in PLAYING + CAMERA, src/Game.cpp:3259).
	sys_.hud->update(kTickMs);
}

void GameContext::tickCinematicClock() {
	if (activeCameraKey_ < 0 || cameraCamIdx_ < 0 ||
	    cameraCamIdx_ >= (int)sys_.map->mayaCameras.size()) return;
	const MapData::MayaCamera& cam = sys_.map->mayaCameras[cameraCamIdx_];

	// Key boundaries (src/MayaCamera.cpp:72-77): once a key's duration
	// elapsed, Update does NOT advance anything on its own — with an
	// outstanding ADV_CAMERAKEY park it calls Snap, else it returns and the
	// pose holds at the boundary. Snap (:335-374) snaps the pose to the NEXT
	// key's static value WITHOUT charging its duration or advancing the key,
	// ticks the countdown, and either resumes the expired thread (whose own
	// next ADV_CAMERAKEY then starts the following key fresh from now via
	// NextKey) or, still counting, auto-advances one key (:365-368). The
	// clock is only ever restarted by a NextKey, never by boundary
	// accumulation — that distinction is what keeps chained handshakes from
	// eating keys.
	if (activeCameraKey_ >= (int)cam.numKeys) {
		// Parked PAST the last key (final ADV_CAMERAKEY; legacy NextKey has
		// no end guard, src/MayaCamera.cpp:36-44): hold the end pose until
		// the last key's duration elapses from the restart, then complete —
		// Snap tail (:376-397) resumes the parked thread via flush.
		if (gameTime - cameraStartTime_ >= cameraKeyDuration((int)cam.numKeys - 1))
			finishCinematic();
		return;
	}
	if (!cameraResumeList_.empty() &&
	    gameTime - cameraStartTime_ >= cameraKeyDuration(activeCameraKey_)) {
		if (activeCameraKey_ + 1 >= cam.numKeys) {
			finishCinematic();         // park on the last boundary -> complete (:358)
			return;
		}
		maya_.snap(activeCameraKey_ + 1);      // Snap pose half (:338-357)
		if (!resumeKeyWaits() && !cameraResumeList_.empty()) {
			nextKey();                         // counting tail NextKey (:365-368)
		}
	}
	int keyMs = cameraKeyDuration(activeCameraKey_);
	int elapsed = (int)(gameTime - cameraStartTime_);
	if (elapsed < keyMs) maya_.update(activeCameraKey_, elapsed);
}

bool GameContext::resumeKeyWaits() {
	// One completed key ticks every parked ADV_CAMERAKEY count; expired ones
	// resume in legacy callThreads[] pool order (src/MayaCamera.cpp:359-370).
	// Returns true if any thread was resumed — the caller then must NOT also
	// take Snap's auto-advance branch (legacy single keyThread slot returns
	// right after run(), :359-364).
	std::vector<size_t> done;
	for (size_t i = 0; i < cameraResumeCounts_.size(); ++i) {
		if (--cameraResumeCounts_[i] <= 0) done.push_back(i);
	}
	if (done.empty()) return false;
	std::sort(done.begin(), done.end(), [this](size_t a, size_t b) {
		return sys_.vm->indexOf(cameraResumeList_[a]) < sys_.vm->indexOf(cameraResumeList_[b]);
	});
	// Remove expired entries FIRST, collecting the threads: resumeThread()
	// runs scripts synchronously and a resumed script may immediately hit the
	// next ADV_CAMERAKEY, re-parking and reallocating these very vectors —
	// erasing afterwards used stale indices against the reallocated buffers
	// (user-visible SIGSEGV in vector::erase).
	std::vector<ScriptThread*> toResume;
	for (size_t i : done) toResume.push_back(cameraResumeList_[i]);
	for (size_t i = done.size(); i-- > 0;) {
		cameraResumeList_.erase(cameraResumeList_.begin() + done[i]);
		cameraResumeCounts_.erase(cameraResumeCounts_.begin() + done[i]);
	}
	for (ScriptThread* t : toResume) sys_.vm->resumeThread(t);
	return true;
}

void GameContext::finishCinematic() {
	// End-of-keys Snap (src/MayaCamera.cpp:376-397). The state restore only
	// applies while still inside ST_CAMERA — legacy returns early when the
	// canvas moved on (:380-382), e.g. a cinematic started mid-load whose
	// ST_PLAYING tail overwrite must not be re-restored.
	const MapData::MayaCamera& cam = sys_.map->mayaCameras[cameraCamIdx_];
	maya_.snap(cam.numKeys - 1);   // hold the final-key pose
	activeCameraKey_ = -1;
	// Snap tail: ST_CAMERA -> ST_PLAYING, never a pre-camera restore
	// (src/MayaCamera.cpp:380-384).
	if (state == StateId::Camera) setState(StateId::Playing);
	flushParkedThreads(false);     // single run() per thread, like Snap's resume (:390-394)
}

void GameContext::skipCinematicNow() {
	// Game::skipCinematic analog (src/Game.cpp:2507-2544): snap the remaining
	// keys, fast-forward the parked threads with the huge-timestamp analog,
	// immediate state restore. Subtitles/particles/fade have no rewrite
	// counterpart yet.
	const MapData::MayaCamera& cam = sys_.map->mayaCameras[cameraCamIdx_];
	maya_.snap(cam.numKeys - 1);   // Snap the remaining keys' end pose
	activeCameraKey_ = -1;
	if (state == StateId::Camera) setState(StateId::Playing);   // Snap tail (:380-384)
	flushParkedThreads(true);
}

void GameContext::flushParkedThreads(bool force) {
	// Drain cameraResumeList_ in legacy callThreads[] pool order. force=false:
	// one run() per parked thread (Snap resume). force=true: skip fast-forward
	// — legacy attemptResume(gameTime + 0x40000000) falls through every WAIT
	// (src/Game.cpp:2507-2544); the rewrite reuses the public run()-based
	// resume path and expires whatever re-parks the thread, capped as a
	// runaway guard.
	std::vector<ScriptThread*> list;
	list.swap(cameraResumeList_);
	cameraResumeCounts_.clear();
	std::sort(list.begin(), list.end(), [this](ScriptThread* a, ScriptThread* b) {
		return sys_.vm->indexOf(a) < sys_.vm->indexOf(b);
	});
	for (ScriptThread* t : list) {
		if (!force) {
			sys_.vm->resumeThread(t);
			continue;
		}
		int r = 2;
		for (int guard = 0; guard < 64 && r == 2; ++guard) {
			r = sys_.vm->resumeThread(t);
			if (r == 2) t->unpauseTime = 0;    // huge-timestamp analog: expire any re-park
		}
		if (r == 2) std::fprintf(stderr, "[camera] fast-forward cap hit, thread left parked\n");
	}
}

// ---- playing action handlers ----

void GameContext::handlePlayingAction(Action a) {
	Player& p = *sys_.player;
	// Legacy gate: all playing input is ignored while any animation runs
	// (src/PlayingInputHandler.cpp:25-27).
	bool gateBlocked = (p.viewX != p.destX || p.viewY != p.destY || p.viewAngle != p.destAngle);
	if (gateBlocked) return;

	switch (a) {
	case Action::TurnLeft:
		p.destAngle += 256;
		p.startRotation();
		break;
	case Action::TurnRight:
		p.destAngle -= 256;
		p.startRotation();
		break;
	case Action::Forward:
	case Action::Back: {
		int stepX = p.viewStepX, stepY = p.viewStepY;
		if (a == Action::Back) { stepX = -stepX; stepY = -stepY; }
		int tx = p.viewX + stepX;
		int ty = p.viewY + stepY;
		// Leave event BEFORE collision/trace; abortMove cancels the move
		// (src/MovementController.cpp:326-331).
		sys_.game->eventFlagsForMovement(p.viewX, p.viewY, tx, ty);
		sys_.game->abortMove = false;
		sys_.vm->executeTile(p.viewX >> 6, p.viewY >> 6, sys_.game->eventFlags_[0], true);
		if (sys_.game->abortMove) break;
		Entity* hitEnt = nullptr; int hitFrac = 0;
		bool clear = sys_.game->traceMove(*sys_.map, p.viewX, p.viewY, tx, ty,
			sys_.game->playerEntity(), Enums::CONTENTS_PLAYERSOLID, 16,
			&hitEnt, &hitFrac);
		// DEVIATION (blue-door block bug): an ET_NPC whose circle contains
		// the trace START (frac < 0) is the scripted-greeter overlap state;
		// legacy dissolves it through the per-turn NPC AI that ADR 0005 has
		// not ported yet, so until then a start-inside NPC hit never blocks.
		// Re-trace past that entity so real blockers BEHIND it (a shut door)
		// still apply.
		for (int pass = 0; !clear && pass < 4 &&
		     hitFrac < 0 && hitEnt != nullptr &&
		     hitEnt->def != nullptr && hitEnt->def->eType == Enums::ET_NPC; ++pass) {
			std::fprintf(stderr, "[dbg] start-inside NPC spr=%d stepped past\n",
				hitEnt->getSprite()); // TEMP [dbg]
			clear = sys_.game->traceMove(*sys_.map, p.viewX, p.viewY, tx, ty,
				hitEnt, Enums::CONTENTS_PLAYERSOLID, 16, &hitEnt, &hitFrac);
		}
		if (clear) {
			p.attemptMove(tx, ty);
			p.setDestHeight(getHeight(tx, ty));
			p.setZStep(p.destZ - p.viewZ);
		} else {
			// TEMP [dbg] move-block audit (remove after blue-door bug fixed)
			std::fprintf(stderr,
				"[dbg] moveBlocked to %d,%d by spr=%d type=%d linked=%d frac=%d\n",
				tx >> 6, ty >> 6,
				hitEnt ? (hitEnt->getSprite()) : -1,
				(hitEnt && hitEnt->def) ? hitEnt->def->eType : -1,
				(hitEnt && (hitEnt->info & Entity::kInfoLinked)) != 0 ? 1 : 0,
				hitFrac);
		}
		break;
	}
	case Action::Use: {
		// Corpse loot FIRST: the legacy ACTION_FIRE trace selects lootable
		// corpses before tile TRIGGER events and door use (ST_LOOTING return
		// preempts executeTile :398 and the door branch :445,
		// src/PlayingInputHandler.cpp:274-378). Entering ST_LOOTING pools and
		// marks the faced tile in enterState_ (legacy setState -> poolLoot
		// order, src/PlayingInputHandler.cpp:374-378); the grant fires on UI
		// close (giveLootPool ran before stand-up,
		// src/LootingSystem.cpp:89-103) and advanceTurn at stand-up expiry
		// (:79-80) — looting costs its turn.
		Entity* corpse = sys_.game->findLootableCorpseFacing(
			p.viewX, p.viewY, p.viewStepX, p.viewStepY);
		if (corpse != nullptr) {
			setState(StateId::Looting);
			break;
		}
		// Faced-tile TRIGGER event FIRST, before door use; a script that ran
		// consumes the turn unless it set skipAdvanceTurn
		// (src/PlayingInputHandler.cpp:395-404,445-453).
		int mask = flagForFacingDir(4);
		int tx = (p.destX + p.viewStepX) >> 6;
		int ty = (p.destY + p.viewStepY) >> 6;
		int ran = sys_.vm->executeTile(tx, ty, mask, true);
		if (ran != 0) {
			if (!sys_.game->skipAdvanceTurn) sys_.game->advanceTurn();
		} else {
			Game::DoorUseResult dr = sys_.game->useDoorFacing(*sys_.map, p.viewX, p.viewY, p.viewStepX, p.viewStepY);
			if (dr == Game::DoorUseResult::Opened) {
				sys_.game->advanceTurn();                  // opened doors consume the turn (:451-452)
			} else if (dr == Game::DoorUseResult::Locked) {
				std::fprintf(stderr, "[use] door locked\n"); // hud->addMessage(44) analog (:447-449)
			}
		}
		break;
	}
	default:
		break;
	}
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

// ---- render orchestration ----

void GameContext::render(AppContext& app) {
	RenderBackend& renderer = app.renderer();
	renderer.beginFrame(app.window());
	Graphics2D& g = renderer.g2d();

	sys_.world->setTime((int)upTimeMs);

	// Cinematic letterbox (legacy ST_CAMERA entry swaps the raster viewport
	// to cinRect, src/Canvas.cpp:1207-1216). cinRect = viewRect with y=42
	// (src/Canvas.cpp:151-154); viewRect = {0, 20, 480, 250} on the 480x320
	// canvas (src/Canvas.cpp:124-127). The full-buffer black clear in
	// beginFrame() provides the bars.
	static constexpr int kCinRect[4] = { 0, 42, 480, 250 };
	bool cinematicView = (state == StateId::Camera);
	if (cinematicView) {
		renderer.setCanvasViewport(kCinRect[0], kCinRect[1], kCinRect[2], kCinRect[3]);
	}

	// Screen shake offsets (src/Render.cpp:2265-2270): lateral offset along
	// the view right vector + vertical offset, canvas units -> <<4 render
	// units. Applied to whichever view renders this frame — player OR maya
	// camera, both go through legacy Render::render (:2208).
	const std::vector<int32_t>& sinTable = sys_.tables->sinTable;
	int shakeX = sys_.hud->shakeX();
	int shakeY = sys_.hud->shakeY();

	// Camera from the player view + render pull-back (src/Render.cpp:2279-
	// 2282; magnitude <= 2.5 map units). Gameplay keeps using player view coords.
	if (activeCameraKey_ >= 0 && cameraCamIdx_ >= 0 &&
	    cameraCamIdx_ < (int)sys_.map->mayaCameras.size()) {
		// Cinematic takeover (legacy MayaCamera::Render, src/MayaCamera.cpp:
		// 302-310): the maya pose IS the view; FOV 315 (290 under a dialog).
		// Legacy re-evaluates the pose EVERY rendered frame from absolute
		// elapsed (src/Canvas.cpp:951, src/MovementController.cpp:519);
		// sampling only in the 15 ms tick beats against the display refresh
		// and reads as periodic slow-motion waves.
		// Ticks own the key state (tickCinematicClock: boundaries, Snap
		// holds, resume handshake); render samples the interpolation of the
		// current key at display rate. Past a key's duration the pose HOLDS
		// (no update) exactly like the tick path.
		int64_t camElapsed = gameTime - cameraStartTime_;
		if (activeCameraKey_ < (int)sys_.map->mayaCameras[cameraCamIdx_].numKeys &&
		    camElapsed < cameraKeyDuration(activeCameraKey_))
			maya_.update(activeCameraKey_, (int)camElapsed);
		const MayaPose& mp = maya_.pose();
		int cyaw = mp.yaw & 0x3FF;
		int msin = sinTable[cyaw];
		int mcos = sinTable[(cyaw + 256) & 0x3FF];
		int mx = mp.x + 8 - (160 * mcos >> 16);
		int my = mp.y + 8 + (160 * msin >> 16);
		int mz = mp.z + 8;
		if (shakeX != 0 || shakeY != 0) {
			mx += (shakeX << 4) * sinTable[(cyaw + 512) & 0x3FF] >> 16;
			my += (shakeX << 4) * -msin >> 16;
			mz += shakeY << 4;
		}
		int fov = 315;
		camera_.setView(mx, my, mz, cyaw, mp.pitch, mp.roll, fov,
			(fov << 14) / ((480 << 14) / 320));
	} else {
	int yaw = sys_.player->viewAngle & 0x3FF;
	int viewSin = sinTable[yaw];
	int viewCos = sinTable[(yaw + 256) & 0x3FF];
	int rvx = (sys_.player->viewX << 4) + 8 - (160 * viewCos >> 16);
	int rvy = (sys_.player->viewY << 4) + 8 + (160 * viewSin >> 16);
	int rvz = (sys_.player->viewZ << 4) + 8;
	if (shakeX != 0 || shakeY != 0) {
		rvx += (shakeX << 4) * sinTable[(yaw + 512) & 0x3FF] >> 16;
		rvy += (shakeX << 4) * -viewCos >> 16;
		rvz += shakeY << 4;
	}
	camera_.setView(rvx, rvy, rvz, sys_.player->viewAngle, 0, 0, 290,
		(290 << 14) / ((480 << 14) / 320));
	}

	if (sys_.world->initialized() && sys_.map->numNodes > 0) {
		sys_.world->drawSky(camera_);
		// Per-sprite sort-bias hooks (src/Render.cpp:856-862): corpse/linked
		// entities draw nearer (+1), monsters (-1; none exist yet).
		std::vector<int> spriteSortBias(sys_.map->numSprites, 0);
		// Stacked-character classification (ADR 0005, spec §1): entity-def
		// driven — live NPCs, plus corpsified NPCs whose art tile stayed in
		// the NPC range after the def swap (src/Game.cpp:567-569).
		std::vector<uint8_t> spriteCharClass(sys_.map->numSprites, 0);
		for (const Entity& ent : sys_.game->entities()) {
			int si = ent.getSprite();
			if (!ent.def || si < 0 || si >= sys_.map->numSprites) continue;
			if (ent.info & 0x1010000) spriteSortBias[si] = +1;
			else if (ent.def->eType == Enums::ET_MONSTER) spriteSortBias[si] = -1;
			if (ent.def->eType == Enums::ET_NPC ||
			    (ent.def->eType == Enums::ET_CORPSE &&
			     (sys_.map->mapSpriteInfo[si] & 0xFF) >= Enums::TILENUM_FIRST_NPC &&
			     (sys_.map->mapSpriteInfo[si] & 0xFF) <= Enums::TILENUM_LAST_NPC)) {
				spriteCharClass[si] = 1;
			}
			// Corpsified monsters keep their character-sheet art tile, so the
			// death pose must render through the stacked path's MANIM_DEAD
			// single-corpse-quad branch (src/Render.cpp:3466-3475); the
			// billboard fallback would clamp frame 0x70 back onto the
			// standing base frame (bug: "standing imp remains"). Gated on
			// the died-marker so placed corpse props stay on the billboard
			// path.
			else if ((ent.info & Entity::kInfoCorpse) != 0 &&
			         ((sys_.map->mapSpriteInfo[si] >> 8) & Enums::MANIM_MASK) == Enums::MANIM_DEAD) {
				spriteCharClass[si] = 1;
			}
		}
		sys_.world->drawBSP(*sys_.map, *sys_.media, camera_, spriteSortBias.data(),
		                    spriteCharClass.data());
	} else {
		g.fillRect(0, 0, 480, 320, 32, 32, 64);
	}

	// Overlays draw in full canvas space again; the cockpit overlay anchors
	// at the cinRect top edge y=42 (src/Hud.cpp:623-624 draws both copies at
	// cinRect positions in screen space).
	if (cinematicView) renderer.restoreCanvasViewport(app.window());

	// Cockpit overlay while a cinematic renders with the raw toggle set
	// (MayaCamera::Render -> Hud::drawOverlay, src/MayaCamera.cpp:316-318;
	// cinRect = viewRect.x / 42 / viewRect width, src/Canvas.cpp:151-154).
	// The boot intro enables it only around camera 0 (IP 1945-2062).
	if (state == StateId::Camera && sys_.hud->cockpitOverlay()) {
		sys_.hud->drawOverlay(g, 0, 42, 480);
	}

	// Messages overlay while cockpit/HUD stay hidden.
	sys_.hud->drawMessages(g, *sys_.font);

	// Dialog box overlay (legacy backPaint -> dialogState,
	// src/Canvas.cpp:447-449).
	if (state == StateId::Dialog) sys_.dialogs->draw(g);

	// Loot list overlay during the dwell window — paints OVER world+HUD
	// (src/Canvas.cpp:414-417,469-472).
	if (state == StateId::Looting) drawLootingMenu(g);

	renderer.endFrame(app.window());
}

} // namespace newcore
