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
	lootEnv.dialogs = sys_.dialogs;
	lootEnv.tables = sys_.tables;
	lootEnv.host = this;
	lootEnv.upTimeMs = &upTimeMs;
	loot_.init(lootEnv);
	Targeting::Env tgtEnv;
	tgtEnv.game = sys_.game;
	tgtEnv.player = sys_.player;
	tgtEnv.map = sys_.map;
	tgtEnv.tables = sys_.tables;
	tgtEnv.hud = sys_.hud;
	targeting_.init(tgtEnv);
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
		if (blocked || state_ != StateId::Playing) break;
		handlePlayingAction(a);
	}
	pendingActions_.clear();

	// Globals each tick: UpdatePlayerVars + runScriptThreads
	// (src/Canvas.cpp:791-796); gsprite_update has no counterpart.
	// Threads tick in PLAYING and CAMERA only (src/Game.cpp:3259): during a
	// cinematic the scripts keep running — input is what's parked.
	sys_.game->setPlayerPos(sys_.player->viewX, sys_.player->viewY);
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
		sys_.game->setLerpViewAngle(cinematic_.active()
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
	spawnPlayer();                                         // (:673)
	std::fprintf(stderr, "[load] staticFunc(0)\n");
	sys_.vm->executeStaticFunc(Enums::SCR_INIT_MAP);       // SCR_INIT_MAP (:691-693)
	// staticFunc(1): completed-game only — no caller in the subset.
	// prevX/Y = view snap: save-only fields, not ported.
	sys_.vm->executeTile(sys_.player->viewX >> 6, sys_.player->viewY >> 6, 4081, true); // entrance event, ENTER|all-dirs (:700-706)
	sys_.player->finishRotation();                         // finishRotation(false) analog (:707)
	sys_.game->endMonstersTurn();                          // monstersTurn = 0 (:708)
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
	return sys_.map->heightAt(x, y);
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
		sys_.game->updateMonsters();              // Stage-1 stub (spec §0.B)
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
	sys_.game->facingDirty = true;             // rotation arrival re-probe (src/MovementController.cpp:304-308)
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

// First-person view weapon (spec combat-stage1 §6.2; legacy Combat::drawWeapon
// GL path src/Combat.cpp:621-844). The legacy anchors (196 + wpX + shakeX,
// 131 - (wpY + shakeY)) and the v12 box are VIEWPORT-relative inputs
// (src/Render.cpp:358-373), not final canvas pixels: draw2DSprite is no 1:1
// blit. On the GL path it builds a world-space billboard 400 units in front of
// the eye (offset 5*((view[k]&~31)+8*(view[k]>>5))>>8, src/Render.cpp:343-421)
// and lets the world projection magnify it (gles::DrawWorldSpaceSpriteLine,
// src/GLES.cpp:483-547); the software fallback at src/Render.cpp:419 is dead
// here, its scaleFactor *= 1.35f being an approximation of the same factor.
//
// Screen offsets from the billboard math (src/GLES.cpp:483-547):
//   dpx = (x - vpW/2) * m[0] / 12800
//   dpy = (y + v12 - vpH/2) * m[5] * vpH / (vpW * 12800)
// so the quad is scaled about the viewport centre (239,124) = canvas (240,131)
// by Kx = m[0]/12800, Ky = m[5]*vpH/2 / ((vpW/2) * 12800), read from the LIVE
// projection (Camera3D::projectionInt) instead of being hard-coded: the
// gameplay projection is buildProjectionMatrix(290,150) -> m[0]=17098,
// m[5]=-33053 (Kx=1.33578, Ky=1.33973, the 0.3% anisotropy is real integer
// aspect truncation 150.46 -> 150), but a cinematic renders at fov 315/290 and
// would otherwise mismatch. m[5] is stored negated by the GLES BeginFrame
// adjustment (new_src/render/Camera3D.cpp:88), hence the magnitude.
// Viewport centre in viewport space and the canvas point it maps to.
constexpr int kWeaponVpCx = 239, kWeaponVpCy = 124;
constexpr int kWeaponCanvasCx = 240, kWeaponCanvasCy = 131;

// Projects one legacy view-weapon quad (viewport-space top-left x, top edge
// y, box size v12) onto the canvas and blits it clipped to the world band
// (1,7,478,248) — the viewport that clips the billboard on the GL path.
// Source texels are always the top-left 176x176 of the 256x256 weapon media
// (src/GLES.cpp:539-542).
static void drawWeaponQuad(Graphics2D& g, const Texture& tex, int x, int y, int v12,
	uint8_t tint, float magX, float magY) {
	constexpr int kSrc = 176;
	const float fxl = kWeaponCanvasCx + (x - kWeaponVpCx) * magX;
	const float fxr = kWeaponCanvasCx + (x + v12 - kWeaponVpCx) * magX;
	const float fyb = kWeaponCanvasCy + (y + v12 - kWeaponVpCy) * magY;
	const float fyt = fyb - v12 * magY;
	const int xl = (int)std::floor(fxl), xr = (int)std::floor(fxr);
	const int yt = (int)std::floor(fyt), yb = (int)std::floor(fyb);
	const int dw = xr - xl, dh = yb - yt;
	if (dw <= 0 || dh <= 0) return;
	// Band clip; Graphics2D::setClip is not honoured by the sprite batch, so
	// the quad and its source rect are trimmed by hand.
	const int cx0 = std::max(xl, 1), cx1 = std::min(xr, 1 + 478);
	const int cy0 = std::max(yt, 7), cy1 = std::min(yb, 7 + 248);
	if (cx1 <= cx0 || cy1 <= cy0) return;
	// Source edges in float + rounding: integer division here squeezed the
	// cropped art by ~0.5% vertically.
	const float su = (float)kSrc / (float)dw, sv = (float)kSrc / (float)dh;
	const int sx0 = (int)std::lround((cx0 - xl) * su);
	const int sx1 = (int)std::lround((cx1 - xl) * su);
	const int sy0 = (int)std::lround((cy0 - yt) * sv);
	const int sy1 = (int)std::lround((cy1 - yt) * sv);
	if (sx1 <= sx0 || sy1 <= sy0) return;
	g.drawImage(tex, sx0, sy0, sx1 - sx0, sy1 - sy0,
		cx0, cy0, cx1 - cx0, cy1 - cy0, 0, tint, tint, tint, 255);
}

void GameContext::drawViewWeapon(Graphics2D& g) {
	// Gate (src/Combat.cpp:706-708 state check): gameplay states only. Zoom
	// skip (src/Canvas.cpp:1348 isZoomedIn) is implicit — no zoom system yet.
	if (state_ != StateId::Playing && state_ != StateId::Looting &&
	    state_ != StateId::Dialog) return;
	// While a camera is active legacy renders the world from
	// MayaCamera::Render and Canvas::renderScene's drawWeapon call is
	// unreachable (src/MovementController.cpp:518-523); the only view weapon
	// is the cinematicWeapon != -1 branch (src/MayaCamera.cpp:311-314), which
	// is deliberately deferred in the rewrite. The call site also gates on
	// the cinematic pose; this keeps the invariant local should another
	// caller appear.
	if (cinematic_.active()) return;
	Player& p = *sys_.player;
	const int w = p.ce.weapon;                                 // (:677)
	if (w < 0 || p.weapons == 0) return;                       // (:706-708)

	// Legacy anchors (196,131) stay VIEWPORT-relative (draw2DSprite,
	// rendering.md §6.2); the viewport origin (1,7) enters through the
	// centre mapping in drawWeaponQuad, together with the projection
	// magnification.
	int scrX = 196;                        // 480/2 - 44                (:627)
	int scrY = 131;                        // 320/2 - 29                (:628)
	// weaponDown lower/raise lerp absent -> skip scrY += LOWEREDWEAPON_Y(38)
	// (:672-674); shiftWeapon/LOWERWEAPON_TIME=200 stays unported.
	// Per-weapon scrY bias (:679-693).
	scrY += (w == 1) ? 3 : (w == 2) ? 10 : (w >= 3 && w <= 6) ? 12 : 0;

	// wpinfo table 1: idleX,idleY,atkX,atkY,flashX,flashY signed bytes per
	// weapon (src/Combat.h:53-59).
	const Tables& tables = *sys_.tables;
	if ((size_t)(w * 6 + 5) >= tables.weaponInfo.size()) return;
	const int idleX = tables.weaponInfo[w * 6 + 0];
	const int idleY = tables.weaponInfo[w * 6 + 1];
	const int atkX  = tables.weaponInfo[w * 6 + 2];
	const int atkY  = tables.weaponInfo[w * 6 + 3];
	const int flashX = tables.weaponInfo[w * 6 + 4];
	const int flashY = tables.weaponInfo[w * 6 + 5];

	// Attack pose: hold (atkX,atkY) until flashDone, then lerp back over
	// animTime in 16.16 (src/Combat.cpp:735-767). b5 reduces to
	// "seq running for this weapon" (curAttacker == nullptr always).
	int wpX = idleX, wpY = idleY;
	bool flash = false;
	Combat& c = sys_.game->combat;
	if (c.active && c.attackerWeaponId == w) {
		wpX = atkX;
		wpY = atkY;
		if (!c.flashDone) {
			flash = ((1 << w) & 0x200) == 0;                   // :741 (weapon 9 excluded)
			// Render-side flip exactly like legacy drawWeapon (:742-744).
			if (gameTime >= c.flashDoneTime) c.flashDone = true;
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

	// Canvas shake; legacy negates sy first (sy = -|sy|, src/Combat.cpp:709).
	const int sx = sys_.hud->shakeX();
	const int sy = -std::abs(sys_.hud->shakeY());
	const int x = scrX + wpX + sx;                             // (:786)
	const int y = scrY - (wpY + sy);                           // (:787)

	// Muzzle flash FIRST so the gun art draws on top (:826-834): tile 1
	// frame 3 at (+flashX+40, +flashY+40), 88x88 (scaleFactor 0x8000).
	// renderMode 5 = RENDER_ADD50: additive blend with colour (.5,.5,.5,1)
	// (src/GLES.cpp:660-664, src/Combat.cpp:833).
	// Magnification from the projection actually in use this frame.
	const int* proj = camera_.projectionInt();
	const float magX = (float)proj[0] / 12800.f;
	const float magY = (float)std::abs(proj[5]) * (float)kWeaponVpCy /
		((float)kWeaponVpCx * 12800.f);

	if (flash && ((1 << w) & 0x181) != 0) {  // legacy flash gate (src/Combat.cpp:826)
		const Texture* ftex = sys_.world->spriteTexture(*sys_.media,
			Combat::getWeaponTileNum(0), 3);
		if (ftex != nullptr) {
			g.setBlendMode(1);
			drawWeaponQuad(g, *ftex, x + flashX + 40, y + flashY + 40, 88, 128, magX, magY);
			g.setBlendMode(0);
		}
	}

	// Weapon art frame 0; the chainsaw-return/weapons 8+13 frame-1 rule
	// (:822-825) is dead on the map00 rifle route. Sentry-bot stack
	// (:797-813), weapon 14 (:814-820) and weapon 9 underlay (:835-837)
	// deferred (hero-choice doc §B.3 exclusions).
	const int tileNum = Combat::getWeaponTileNum(w);
	const Texture* tex = sys_.world->spriteTexture(*sys_.media, tileNum, 0);
	if (tex != nullptr) {
		drawWeaponQuad(g, *tex, x, y, 176, 255, magX, magY);
	}
}

// ---- cinematic camera (docs/original-code/cutscenes-camera.md §1-§3) ----

void GameContext::tickCamera() {
	// HUD message/subtitle timers run on the shared clock in legacy
	// (gameTime advances in PLAYING + CAMERA, src/Game.cpp:3259). Skipped on a
	// skip frame: tickCameraState() returns false there, as the pre-
	// decomposition early return did.
	if (cinematic_.tickCameraState()) sys_.hud->update(kTickMs);
}

// ---- playing action handlers ----

void GameContext::handlePlayingAction(Action a) {
	Player& p = *sys_.player;
	// Input drops entirely while a combat seq runs — legacy is in ST_COMBAT
	// so no playing input matches (spec §0.C.3, src/GameStateRunner state gate).
	if (sys_.game->combat.active) return;
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
		// The chainsaw is the exception: for melee the corpse branch elects an
		// ATTACK target (gib) instead of a loot session
		// (src/PlayingInputHandler.cpp:280-317 vs :318-334).
		Entity* corpse = (p.ce.weapon == 1) ? nullptr
			: sys_.game->findLootableCorpseFacing(
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
		bool consumed = false;
		if (ran != 0) {
			consumed = true;                           // script ran -> legacy return true (:395-404)
			if (!sys_.game->skipAdvanceTurn) sys_.game->advanceTurn();
		} else {
			Game::DoorUseResult dr = sys_.game->useDoorFacing(*sys_.map, p.viewX, p.viewY, p.viewStepX, p.viewStepY);
			if (dr == Game::DoorUseResult::Opened) {
				sys_.game->advanceTurn();                  // opened doors consume the turn (:451-452)
				consumed = true;
			} else if (dr == Game::DoorUseResult::Locked) {
				std::fprintf(stderr, "[use] door locked\n"); // hud->addMessage(44) analog (:447-449)
				consumed = true;                       // legacy door branch return true consumes the press (:445-457)
			}
		}
		if (consumed) break;   // fire election only when nothing consumed the press
		// ---- fire (legacy probe src/PlayingInputHandler.cpp:189-548) ----
		// Order preserved: loot -> tile event -> door -> fire; reached ONLY
		// when nothing above consumed the press (legacy return-true chain).
		const int weapon2 = p.ce.weapon;   // legacy reads ce->weapon (:197)
		if (weapon2 >= 0 && !sys_.game->combat.active) {
			int frac = 16384;
			Entity* hit = targeting_.electFireTarget(weapon2, &frac);
			const int dist2 = (hit != nullptr)
				? sys_.game->entityDistFrom(hit, p.viewX, p.viewY) : 0;
			// World slot carries no def and reads as eType 0 - same convention
			// as electFireTarget (Targeting.cpp); a plain nullptr means nothing was
			// elected at all.
			const int hitType = (hit == nullptr) ? -1
				: (hit->def != nullptr ? hit->def->eType : Enums::ET_WORLD);
			// Outcome mapping of the legacy shot commit (:496-540): attackable
			// types fire at the entity, a wall within one tile is a push, and
			// everything else (nothing elected, far geometry) is an air shot
			// into the WORLD slot.
			enum Outcome { kAirShot, kElected, kWallPush };
			Outcome outcome = kAirShot;
			if (hitType == Enums::ET_MONSTER || hitType == Enums::ET_NPC ||
			    hitType == Enums::ET_DECOR || hitType == Enums::ET_ENV_DAMAGE ||
			    hitType == Enums::ET_CORPSE ||
			    hitType == Enums::ET_ATTACK_INTERACTIVE ||
			    hitType == Enums::ET_NONOBSTRUCTING_SPRITEWALL) {
				outcome = kElected;
			} else if ((hitType == Enums::ET_WORLD || hitType == Enums::ET_SPRITEWALL) &&
			           dist2 <= sys_.game->combat.tileDistances[0]) {      // :467 gate
				outcome = kWallPush;
			}
			if (outcome == kWallPush) {
				// Within 1 tile of a wall: legacy shiftWeapon(true)+rockView
				// and NO turn consumed (:467-487). The lower/raise lerp system
				// is absent -> log only.
				std::fprintf(stderr, "[combat] wall push (shiftWeapon/rockView deferred)\n");
			} else {
				// Zoom entry (mask 512 -> initZoom): consumes the input like
				// legacy (:504-507); deferred here, log once.
				static bool zoomLogged = false;
				if (((1 << weapon2) & 512) != 0 && !zoomLogged) {
					zoomLogged = true;
					std::fprintf(stderr, "[combat] zoom-entry weapons deferred (initZoom)\n");
					break;
				}
				// Air/world shots target the WORLD slot; elected targets pass
				// their sprite coords (legacy passes calcPosition/
				// traceCollision coords, :509-536). The air-shot impact point is
				// the trace contact point, not the player (:532-536).
				Entity* target = outcome == kElected ? hit : sys_.game->worldEntity();
				int ax = sys_.game->traceCollisionX();
				int ay = sys_.game->traceCollisionY();
				if (outcome == kElected) {
					const int s = target->getSprite();
					if (s >= 0) {
						ax = sys_.map->mapSprites[s];
						ay = sys_.map->mapSprites[sys_.map->numSprites + s];
					}
				}
				p.fireWeapon(sys_.game->combat, target, ax, ay);
				// NO advanceTurn here — the seq completion in tickPlaying
				// consumes the turn (spec §0.C).
			}
		}
		break;
	}
	case Action::Passturn:
		// src/PlayingInputHandler.cpp:550-555: msg 45 (+ touchTile stub) +
		// advanceTurn. showCenterMessage is the rewrite's message-log stand-in.
		if (sys_.loc != nullptr && sys_.hud != nullptr) {
			std::string pass = sys_.loc->get(kTextMain, 45);
			// TEMP [dbg] passturn audit (remove with the fire-path acceptance)
			std::fprintf(stderr, "[turn] passturn msg45=\"%s\"\n", pass.c_str());
			sys_.hud->showCenterMessage(pass, 0xAA000000, 3500);
		}
		sys_.game->advanceTurn();
		break;
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

// Floater family (src/Render.cpp:3023-3025): Sentinel/Lost Soul/Cacodemon.
// Legacy diverts these to renderFloaterAnim before the shared anim switch
// (src/Render.cpp:3157-3161); drawCharacter has no counterpart, so they stay
// on the billboard path (ADR 0007).
static bool isFloaterTile(int n) {
	return (n >= Enums::TILENUM_MONSTER_SENTINEL && n <= Enums::TILENUM_MONSTER_SENTINEL3) ||
	       (n >= Enums::TILENUM_MONSTER_LOST_SOUL && n <= Enums::TILENUM_MONSTER_LOST_SOUL3) ||
	       (n >= Enums::TILENUM_MONSTER_CACODEMON && n <= Enums::TILENUM_MONSTER_CACODEMON3);
}

// Special-boss family (src/Render.cpp:3027-3029): Mastermind/Arachnotron/
// Boss Pinky/VIOS, diverted to renderSpecialBossAnim (src/Render.cpp:3162-3166).
static bool isSpecialBossTile(int n) {
	return n == Enums::TILENUM_BOSS_MASTERMIND || n == Enums::TILENUM_MONSTER_ARACHNOTRON ||
	       n == Enums::TILENUM_BOSS_PINKY ||
	       (n >= Enums::TILENUM_BOSS_VIOS && n <= Enums::TILENUM_BOSS_VIOS5);
}

void GameContext::render(AppContext& app) {
	RenderBackend& renderer = app.renderer();
	renderer.beginFrame(app.window());
	Graphics2D& g = renderer.g2d();

	sys_.world->setTime((int)upTimeMs);

	// World band, used by BOTH gameplay and cinematics: the GL path snaps the
	// viewport to glViewport(1, 65, 478, 248) = canvas rect (1, 7, 478, 248),
	// centre (240,131), whatever y the raster rect carried
	// (src/GLES.cpp:119-127 discards it, src/TinyGL.cpp:149-167;
	// rendering.md §6.1, ADR 0009). The cinematic letterbox is not a viewport
	// change either: it is two opaque black fills painted over the finished
	// world band (src/Hud.cpp:455-456, see the StateId::Camera block below).
	static constexpr int kWorldRect[4] = { 1, 7, 478, 248 };

	// Single source of truth for "a cinematic owns the world this frame":
	// one value, produced by one owner, feeding fov, cockpit overlay and
	// view-weapon suppression (spec 2026-08-26-decomposition §P1-G2). Also
	// performs the display-rate pose resample.
	const MayaPose* cinePose = cinematic_.renderPose();

	// Screen shake offsets (src/Render.cpp:2265-2270): lateral offset along
	// the view right vector + vertical offset, canvas units -> <<4 render
	// units. Applied to whichever view renders this frame — player OR maya
	// camera, both go through legacy Render::render (:2208).
	const std::vector<int32_t>& sinTable = sys_.tables->sinTable;
	int shakeX = sys_.hud->shakeX();
	int shakeY = sys_.hud->shakeY();

	// Camera from the player view + render pull-back (src/Render.cpp:2279-
	// 2282; magnitude <= 2.5 map units). Gameplay keeps using player view coords.
	if (cinePose != nullptr) {
		// Cinematic takeover (legacy MayaCamera::Render, src/MayaCamera.cpp:
		// 302-310): the maya pose IS the view; FOV 315 (290 under a dialog).
		const MayaPose& mp = *cinePose;
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
		// viewAspect over the same 478x248 viewport as gameplay
		// (src/Render.cpp:2223).
		// fov 290 while a dialog runs inside the cinematic, 315 otherwise
		// (src/MayaCamera.cpp:305-310 canvas->state == ST_DIALOG).
		int fov = (state_ == StateId::Dialog) ? 290 : 315;
		camera_.setView(mx, my, mz, cyaw, mp.pitch, mp.roll, fov,
			(fov << 14) / ((478 << 14) / 248));
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
	// Pitch feeds the view matrix (positive = up; the loot crouch writes
	// player->viewPitch). FOV stays at the documented 290: legacy widened by
	// |pitch| only on the mvp2D billboard path (src/TinyGL.cpp:195-200), the
	// world/GL projection kept viewFov — the rewrite has a single projection.
	// viewAspect over the 478x248 world viewport (src/Render.cpp:2223).
	camera_.setView(rvx, rvy, rvz, sys_.player->viewAngle, sys_.player->viewPitch, 0, 290,
		(290 << 14) / ((478 << 14) / 248));
	}

	if (sys_.world->initialized() && sys_.map->numNodes > 0) {
		renderer.setCanvasViewport(kWorldRect[0], kWorldRect[1], kWorldRect[2], kWorldRect[3]);
		sys_.world->drawSky(camera_);
		// Per-sprite sort-bias hooks (src/Render.cpp:856-862): corpse/linked
		// entities draw nearer (+1), monsters (-1).
		std::vector<int> spriteSortBias(sys_.map->numSprites, 0);
		// Stacked-character classification (ADR 0005/0007, spec
		// 2026-08-26 §1): entity-def driven — live NPCs, monsters whose tile
		// is outside the diverted floater/special-boss families (their
		// renderers are not ported), corpsified NPCs whose art tile stayed in
		// the NPC range after the def swap (src/Game.cpp:567-569), and
		// corpsified monsters via the kInfoCorpse clause below. Legacy gate:
		// renderSpriteAnim runs for every entity with monster != nullptr
		// (src/Render.cpp:1622-1626); ET_MONSTER is its exact proxy
		// (allocated iff eType == 2, src/Game.cpp:430-436).
		std::vector<uint8_t> spriteCharClass(sys_.map->numSprites, 0);
		for (const Entity& ent : sys_.game->entities()) {
			int si = ent.getSprite();
			if (!ent.def || si < 0 || si >= sys_.map->numSprites) continue;
			if (ent.info & 0x1010000) spriteSortBias[si] = +1;
			else if (ent.def->eType == Enums::ET_MONSTER) spriteSortBias[si] = -1;
			const int tile = sys_.map->mapSpriteInfo[si] & 0xFF; // monsters never carry SPRITE_FLAG_TILE (+257)
			if (ent.def->eType == Enums::ET_NPC ||
			    (ent.def->eType == Enums::ET_CORPSE &&
			     tile >= Enums::TILENUM_FIRST_NPC &&
			     tile <= Enums::TILENUM_LAST_NPC)) {
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
			else if (ent.def->eType == Enums::ET_MONSTER &&
			         !isFloaterTile(tile) && !isSpecialBossTile(tile)) {
				spriteCharClass[si] = 1;
			}
		}
		sys_.world->drawBSP(*sys_.map, *sys_.media, camera_, spriteSortBias.data(),
		                    spriteCharClass.data());
		renderer.restoreCanvasViewport(app.window());
	} else {
		g.fillRect(0, 0, 480, 320, 32, 32, 64);
	}

	// View weapon paints over the world in full canvas space; its legacy
	// anchors already include the world viewport origin (ADR 0009).
	if (cinePose == nullptr) drawViewWeapon(g);

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
	if (state_ == StateId::Playing || state_ == StateId::Looting ||
	    state_ == StateId::Dialog) {
		sys_.hud->drawTopBar(g, *sys_.font, 480);
	}

	// Messages overlay while cockpit/HUD stay hidden.
	sys_.hud->drawMessages(g, *sys_.font);

	// Dialog box overlay (legacy backPaint -> dialogState,
	// src/Canvas.cpp:447-449).
	if (state_ == StateId::Dialog) sys_.dialogs->draw(g);

	// Loot list overlay during the dwell window — paints OVER world+HUD
	// (src/Canvas.cpp:414-417,469-472).
	if (state_ == StateId::Looting) loot_.draw(g);

	renderer.endFrame(app.window());
}

} // namespace newcore
