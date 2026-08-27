#include "core/PlayerActions.h"

#include <cstdio>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/game/Targeting.h"
#include "domain/game/TraceSystem.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "ui/Hud.h"

namespace newcore {

void PlayerActions::init(const Env& env) {
	env_ = env;
}

void PlayerActions::spawnPlayer() {
	Player& p = *env_.player;
	int x, y, dir;
	if (env_.game->spawnParam != -1) {                     // encoded by a level exit / save
		x = env_.game->spawnParam & 0x1F;
		y = (env_.game->spawnParam >> 5) & 0x1F;
		dir = (env_.game->spawnParam >> 10) & 7;
		env_.game->spawnParam = -1;
	} else {                                               // map header spawn
		x = env_.map->spawnIndex % 32;
		y = env_.map->spawnIndex / 32;
		dir = env_.map->spawnDir;
	}
	p.viewX = p.destX = x * 64 + 32;
	p.viewY = p.destY = y * 64 + 32;
	p.viewZ = p.destZ = env_.map->heightAt(p.viewX, p.viewY) + 36;
	p.viewAngle = p.destAngle = (dir << 7) & 0x3FF;
	p.startRotation();
	p.finishRotation(); // player->relink() omitted: player entity not linked in the rewrite
}

void PlayerActions::finishMovement() {
	Player& p = *env_.player;
	// A parked scripted-walk thread resumes FIRST, once its angle settled
	// (src/MovementController.cpp:164-167). Capture-clear-run order (the
	// legacy run()-then-clear would clobber a re-park issued by the resumed
	// script; finishRotation's capture form at :298-302 is the safe shape).
	if (gotoThread != nullptr && p.viewAngle == p.destAngle) {
		ScriptThread* t = gotoThread;
		gotoThread = nullptr;
		env_.vm->resumeThread(t);
	}
	// FACE then ENTER on the destination tile (src/MovementController.cpp:168-169).
	int faceMask = flagForFacingDir(8);
	int faceRes = env_.vm->executeTile(p.destX >> 6, p.destY >> 6, faceMask, true);
	int enterMask = env_.game->eventFlags_[1];
	int enterRes = env_.vm->executeTile(p.destX >> 6, p.destY >> 6, enterMask, true);
	// TEMP [dbg] arrival audit (remove after silent-tile-events bug fixed)
	std::fprintf(stderr, "[dbg] finishMovement destTile=%d,%d face=0x%X->%d enter=0x%X->%d\n",
		p.destX >> 6, p.destY >> 6, faceMask, faceRes, enterMask, enterRes);
	env_.game->touchTile(p.destX, p.destY, true);          // canvas units, like legacy (:170)
	// advanceTurn unless a script claimed/skipped the turn this arrival; a
	// still-parked gotoThread (angle not settled yet, or re-parked by the
	// resumed script) means a scripted walk — no turn consumed
	// (src/MovementController.cpp:178).
	if (gotoThread == nullptr && env_.game->monstersTurn == 0 && !env_.game->skipAdvanceTurn) {
		env_.game->advanceTurn();
	}
}

void PlayerActions::finishRotationFired() {
	// Recompute step vectors, then fire the rotation-arrival FACE event
	// (src/MovementController.cpp:284-310, :307). Safe to snap here: input is
	// gated on full idle, so a turn never overlaps a move in the subset.
	env_.player->finishRotation();
	env_.game->facingDirty = true;             // rotation arrival re-probe (src/MovementController.cpp:304-308)
	// Same handshake as finishMovement: the rotation was the last leg of a
	// scripted GOTO/TURN_PLAYER (src/MovementController.cpp:298-302).
	if (gotoThread != nullptr &&
		env_.player->viewX == env_.player->destX && env_.player->viewY == env_.player->destY) {
		ScriptThread* t = gotoThread;
		gotoThread = nullptr;
		env_.vm->resumeThread(t);
	}
	env_.vm->executeTile(env_.player->destX >> 6, env_.player->destY >> 6, flagForFacingDir(8), true);
}

int PlayerActions::flagForFacingDir(int i) const {
	int destAngle = env_.player->destAngle;
	if (i == 4) destAngle += 512;                          // look backward for use/triggers
	if (i == 4 || i == 8) {
		return i | (1 << (((destAngle & 0x3FF) >> 7) + 4));
	}
	return 0;
}

// ---- playing action handlers ----

void PlayerActions::handleAction(Action a) {
	Player& p = *env_.player;
	// Input drops entirely while a combat seq runs — legacy is in ST_COMBAT
	// so no playing input matches (spec §0.C.3, src/GameStateRunner state gate).
	if (env_.game->combat.active) return;
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
		env_.game->eventFlagsForMovement(p.viewX, p.viewY, tx, ty);
		env_.game->abortMove = false;
		env_.vm->executeTile(p.viewX >> 6, p.viewY >> 6, env_.game->eventFlags_[0], true);
		if (env_.game->abortMove) break;
		TraceSystem& trace = env_.game->trace;
		TraceHit h = trace.trace(p.viewX, p.viewY, tx, ty,
			env_.game->db.playerEntity(), Enums::CONTENTS_PLAYERSOLID, 16);
		// DEVIATION (blue-door block bug): an ET_NPC whose circle contains
		// the trace START (startsInside) is the scripted-greeter overlap state;
		// legacy dissolves it through the per-turn NPC AI that ADR 0005 has
		// not ported yet, so until then a start-inside NPC hit never blocks.
		// Re-trace past that entity so real blockers BEHIND it (a shut door)
		// still apply.
		for (int pass = 0; h.blocks() && pass < 4 &&
		     h.startsInside() && h.isEntity() &&
		     h.eType == Enums::ET_NPC; ++pass) {
			std::fprintf(stderr, "[dbg] start-inside NPC spr=%d stepped past\n",
				h.entity->getSprite()); // TEMP [dbg]
			h = trace.trace(p.viewX, p.viewY, tx, ty,
				h.entity, Enums::CONTENTS_PLAYERSOLID, 16);
		}
		if (!h.blocks()) {
			p.attemptMove(tx, ty);
			p.setDestHeight(env_.map->heightAt(tx, ty));
			p.setZStep(p.destZ - p.viewZ);
		} else {
			// TEMP [dbg] move-block audit (remove after blue-door bug fixed)
			std::fprintf(stderr,
				"[dbg] moveBlocked to %d,%d by spr=%d type=%d linked=%d frac=%d\n",
				tx >> 6, ty >> 6,
				h.entity ? (h.entity->getSprite()) : -1,
				h.isEntity() ? h.eType : -1,       // world hit logged as -1, as before
				(h.entity && (h.entity->info & Entity::kInfoLinked)) != 0 ? 1 : 0,
				h.frac);
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
			: env_.game->findLootableCorpseFacing(
				p.viewX, p.viewY, p.viewStepX, p.viewStepY);
		if (corpse != nullptr) {
			env_.host->requestState(StateId::Looting);
			break;
		}
		// Faced-tile TRIGGER event FIRST, before door use; a script that ran
		// consumes the turn unless it set skipAdvanceTurn
		// (src/PlayingInputHandler.cpp:395-404,445-453).
		int mask = flagForFacingDir(4);
		int tx = (p.destX + p.viewStepX) >> 6;
		int ty = (p.destY + p.viewStepY) >> 6;
		int ran = env_.vm->executeTile(tx, ty, mask, true);
		bool consumed = false;
		if (ran != 0) {
			consumed = true;                           // script ran -> legacy return true (:395-404)
			if (!env_.game->skipAdvanceTurn) env_.game->advanceTurn();
		} else {
			Game::DoorUseResult dr = env_.game->useDoorFacing(*env_.map, p.viewX, p.viewY, p.viewStepX, p.viewStepY);
			if (dr == Game::DoorUseResult::Opened) {
				env_.game->advanceTurn();                  // opened doors consume the turn (:451-452)
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
		if (weapon2 >= 0 && !env_.game->combat.active) {
			TraceSystem& trace = env_.game->trace;
			const TraceHit elected = env_.targeting->electFireTarget(weapon2);
			const int dist2 = elected.blocks()
				? trace.distFrom(elected, p.viewX, p.viewY) : 0;
			// Outcome mapping of the legacy shot commit (:496-540): attackable
			// types fire at the entity, a wall within one tile is a push, and
			// everything else (nothing elected, far geometry) is an air shot
			// into the WORLD slot. eType is the one resolved by TraceSystem
			// (ADR 0011): -1 when nothing was elected, ET_WORLD for a world hit.
			enum Outcome { kAirShot, kElected, kWallPush };
			Outcome outcome = kAirShot;
			if (elected.eType == Enums::ET_MONSTER || elected.eType == Enums::ET_NPC ||
			    elected.eType == Enums::ET_DECOR || elected.eType == Enums::ET_ENV_DAMAGE ||
			    elected.eType == Enums::ET_CORPSE ||
			    elected.eType == Enums::ET_ATTACK_INTERACTIVE ||
			    elected.eType == Enums::ET_NONOBSTRUCTING_SPRITEWALL) {
				outcome = kElected;
			} else if ((elected.eType == Enums::ET_WORLD || elected.eType == Enums::ET_SPRITEWALL) &&
			           dist2 <= env_.game->combat.tileDistances[0]) {      // :467 gate
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
				Entity* target = outcome == kElected ? elected.entity : env_.game->db.worldEntity();
				int ax = trace.collisionX();
				int ay = trace.collisionY();
				if (outcome == kElected) {
					const int s = target->getSprite();
					if (s >= 0) {
						ax = env_.map->mapSprites[s];
						ay = env_.map->mapSprites[env_.map->numSprites + s];
					}
				}
				p.fireWeapon(env_.game->combat, target, ax, ay);
				// NO advanceTurn here — the seq completion in tickPlaying
				// consumes the turn (spec §0.C).
			}
		}
		break;
	}
	case Action::Passturn:
		// src/PlayingInputHandler.cpp:550-555: msg 45 (+ touchTile stub) +
		// advanceTurn. showCenterMessage is the rewrite's message-log stand-in.
		if (env_.loc != nullptr && env_.hud != nullptr) {
			std::string pass = env_.loc->get(kTextMain, 45);
			// TEMP [dbg] passturn audit (remove with the fire-path acceptance)
			std::fprintf(stderr, "[turn] passturn msg45=\"%s\"\n", pass.c_str());
			env_.hud->showCenterMessage(pass, 0xAA000000, 3500);
		}
		env_.game->advanceTurn();
		break;
	default:
		break;
	}
}

} // namespace newcore
