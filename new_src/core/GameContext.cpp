#include "core/GameContext.h"

#include <cstdio>

#include "core/AppContext.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
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
	}
}

// ---- state machine ----

void GameContext::setState(StateId s) {
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
	// activation (src/Canvas.cpp:1030-1049). None apply to the subset.
}

void GameContext::enterState_(StateId s) {
	switch (s) {
	case StateId::Playing:
		// Clear pending input actions + stamp the turn clock
		// (src/Canvas.cpp:1101-1106 analog).
		pendingActions_.clear();
		lastTurnTime_ = upTimeMs;
		break;
	case StateId::Dying:
		deathTimeMs_ = upTimeMs; // unused this phase (src/Canvas.cpp:1125-1131 analog)
		break;
	case StateId::Loading:
		loadingPhase_ = 0;     // arm the loading-phase counter
		break;
	}
}

// ---- frame ----

void GameContext::tick() {
	upTimeMs += kTickMs;                                   // app->upTimeMs / app->time
	if (!pauseGameTime && state == StateId::Playing) gameTime += kTickMs; // frozen outside Playing (deviation C5)

	// Expired-latch sweep: legacy runInputEvents zeroes blockInputTime once
	// gameTime passes it, independent of thread state
	// (src/InputEventController.cpp:472-476). Silent like the original.
	if (blockInputTime != 0 && gameTime > blockInputTime) blockInputTime = 0;

	// Input gate: dialog-modal first (legacy ST_DIALOG routes keys to the
	// dialog handler, src/InputEventController.cpp:331-333), then the
	// blocked-drop / normal dispatch (src/InputEventController.cpp:449-478).
	if (scriptDialogActive_) {
		dismissDialogStep();
	} else {
		bool blocked = inputBlocked();
		if (blocked) {
			pendingActions_.clear();
		} else {
			runInputEvents();
		}
	}

	// Globals each tick: UpdatePlayerVars + runScriptThreads
	// (src/Canvas.cpp:791-796); gsprite_update has no counterpart.
	sys_.game->setPlayerPos(sys_.player->viewX, sys_.player->viewY);
	if (state == StateId::Playing) sys_.vm->runScriptThreads(gameTime);

	switch (state) {
	case StateId::Loading: tickLoading(); break;
	case StateId::Playing: tickPlaying(); break;
	case StateId::Dying:   tickDying(); break;
	}

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

bool GameContext::enterScriptDialog(ScriptThread* t) {
	if (scriptDialogActive_) {
		// Nesting guard: legacy cannot reach a second startDialog because
		// ST_DIALOG freezes background threads outright (src/Game.cpp:3259);
		// refusing keeps the modal owner intact and changes nothing else.
		return false;
	}
	scriptDialogThread_ = t;
	scriptDialogActive_ = true;
	return true;
}

void GameContext::dismissDialogStep() {
	// ST_DIALOG input analog: ACTION_FIRE pages/closes one step per press
	// (src/DialogSystem.cpp:34-48); every other key is swallowed by the modal.
	bool dismiss = false;
	for (Action a : pendingActions_) {
		if (a == Action::Use) {                            // E == ACTION_FIRE analog
			dismiss = true;
			break;
		}
	}
	pendingActions_.clear();
	if (!dismiss) return;

	sys_.hud->clearDialogMessage();
	scriptDialogActive_ = false;
	ScriptThread* t = scriptDialogThread_;
	scriptDialogThread_ = nullptr;
	if (t != nullptr && sys_.vm != nullptr) {
		int code = sys_.vm->resumeThread(t);               // legacy dialogThread->run()
		                                                   // (src/DialogSystem.cpp:559-562)
		// A chained EV_DIALOG re-raised scriptDialogActive_ inside run();
		// otherwise make sure no finished/parked thread keeps the latch.
		if (!scriptDialogActive_ && code != 2) {
			// Usually self-blocked: the finishing thread is still inuse with
			// flags&1 at call time; the real clear happens next tick via
			// runScriptThreads sweep -> freeThread (reviewer note).
			sys_.vm->releaseBlockIfUnheld("dialog-chain-end"); // FIX A
		}
	}
}

void GameContext::runInputEvents() {
	for (Action a : pendingActions_) {
		if (state == StateId::Playing) handlePlayingAction(a);
	}
	pendingActions_.clear();
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
	setState(StateId::Playing);                            // (:712-720)
	pauseGameTime = false;
	blockInputTime = gameTime + 200;
	std::fprintf(stderr, "[load] -> ST_PLAYING (blockInput 200ms)\n");
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
	// 5. door lerps ~= updateLerpSprites (:184); resumes blocking door scripts.
	sys_.game->update(kTickMs);
	// 6. updateView (src/MovementController.cpp:377-547).
	bool posIdle = (sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY);
	bool angleIdle = (sys_.player->viewAngle == sys_.player->destAngle);
	sys_.player->updateView();                             // interpolate X/Y/Z/angle
	if (!posIdle && sys_.player->viewX == sys_.player->destX && sys_.player->viewY == sys_.player->destY) {
		finishMovement();                                  // (:510-512)
	}
	if (!angleIdle && sys_.player->viewAngle == sys_.player->destAngle) {
		finishRotationFired();                             // (:514-516)
	}
	// 7. camera pull-back + scene draw happen in render().
	// HUD message timers tick with the playing state (legacy hud->update,
	// src/Hud.cpp:1365-1378 analog).
	sys_.hud->update(kTickMs);
}

void GameContext::finishMovement() {
	Player& p = *sys_.player;
	// FACE then ENTER on the destination tile (src/MovementController.cpp:168-169).
	sys_.vm->executeTile(p.destX >> 6, p.destY >> 6, flagForFacingDir(8), true);
	sys_.vm->executeTile(p.destX >> 6, p.destY >> 6, sys_.game->eventFlags_[1], true);
	sys_.game->touchTile(p.destX, p.destY, true);          // canvas units, like legacy (:170)
	// advanceTurn unless a script claimed/skipped the turn this arrival
	// (knockback/gotoThread guards absent — neither exists in the subset).
	if (sys_.game->monstersTurn == 0 && !sys_.game->skipAdvanceTurn) {
		sys_.game->advanceTurn();
	}
}

void GameContext::finishRotationFired() {
	// Recompute step vectors, then fire the rotation-arrival FACE event
	// (src/MovementController.cpp:284-310, :307). Safe to snap here: input is
	// gated on full idle, so a turn never overlaps a move in the subset.
	sys_.player->finishRotation();
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
		if (sys_.game->traceMove(*sys_.map, p.viewX, p.viewY, tx, ty,
			sys_.game->playerEntity(), Enums::CONTENTS_PLAYERSOLID, 16)) {
			p.attemptMove(tx, ty);
			p.setDestHeight(getHeight(tx, ty));
			p.setZStep(p.destZ - p.viewZ);
		}
		break;
	}
	case Action::Use: {
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

	// Camera from the player view + render pull-back (src/Render.cpp:2279-
	// 2282; magnitude <= 2.5 map units). Gameplay keeps using player view coords.
	const std::vector<int32_t>& sinTable = sys_.tables->sinTable;
	int yaw = sys_.player->viewAngle & 0x3FF;
	int viewSin = sinTable[yaw];
	int viewCos = sinTable[(yaw + 256) & 0x3FF];
	int rvx = (sys_.player->viewX << 4) + 8 - (160 * viewCos >> 16);
	int rvy = (sys_.player->viewY << 4) + 8 + (160 * viewSin >> 16);
	camera_.setView(rvx, rvy, (sys_.player->viewZ << 4) + 8, sys_.player->viewAngle, 0, 0, 290,
		(290 << 14) / ((480 << 14) / 320));

	if (sys_.world->initialized() && sys_.map->numNodes > 0) {
		sys_.world->drawSky(camera_);
		// Per-sprite sort-bias hooks (src/Render.cpp:856-862): corpse/linked
		// entities draw nearer (+1), monsters (-1; none exist yet).
		std::vector<int> spriteSortBias(sys_.map->numSprites, 0);
		for (const Entity& ent : sys_.game->entities()) {
			int si = ent.getSprite();
			if (!ent.def || si < 0 || si >= sys_.map->numSprites) continue;
			if (ent.info & 0x1010000) spriteSortBias[si] = +1;
			else if (ent.def->eType == Enums::ET_MONSTER) spriteSortBias[si] = -1;
		}
		sys_.world->drawBSP(*sys_.map, *sys_.media, camera_, spriteSortBias.data());
	} else {
		g.fillRect(0, 0, 480, 320, 32, 32, 64);
	}

	// Messages overlay while cockpit/HUD stay hidden.
	sys_.hud->drawMessages(g, *sys_.font);

	renderer.endFrame(app.window());
}

} // namespace newcore
