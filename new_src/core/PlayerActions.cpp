#include "core/PlayerActions.h"

#include <cstdio>

#include "core/Familiar.h"
#include "core/TargetPractice.h"
#include "core/ZoomController.h"
#include "domain/game/Automap.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/game/Targeting.h"
#include "domain/game/TraceSystem.h"
#include "domain/world/MapBits.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/MenuData.h"
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
	p.finishRotation();
	env_.game->relinkPlayer(p);                            // src/Game.cpp:969
	const Entity* pe = env_.game->db.playerEntity();
	std::fprintf(stderr, "[spawn] player entity def=%p tile=%d,%d\n",
		(const void*)pe->def, p.destX >> 6, p.destY >> 6);
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
	// (src/MovementController.cpp:178). The automap arm resolves the whole
	// monster turn at once and ignores those gates (:185-195).
	if (env_.host->state() == StateId::Automap) {
		Automap::uncover(*env_.map, p.destX, p.destY);
		env_.game->advanceTurn();
		if (env_.game->lerps.animatingEffects() != 0) {
			env_.host->requestState(StateId::Playing);
		} else {
			env_.game->monsters.snapMonsters(true);
			env_.game->lerps.snap(-1);
		}
	} else if (gotoThread == nullptr && env_.game->monstersTurn == 0 && !env_.game->skipAdvanceTurn) {
		Automap::uncover(*env_.map, p.destX, p.destY);     // src/MovementController.cpp:182
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

bool PlayerActions::attemptMove(int tx, int ty) {
	Player& p = *env_.player;
	// Familiar: no-bot tiles refuse the step, except the body's own tile
	// (src/MovementController.cpp:322-325).
	if (p.isFamiliar && (env_.map->mapFlags[(ty >> 6) * 32 + (tx >> 6)] & 0x10) != 0 &&
	    (tx != p.saveX || ty != p.saveY)) {
		if (env_.hud != nullptr && env_.loc != nullptr)
			env_.hud->addMessage(env_.loc->get(kTextMain, 222), 0xAA000000, 3);
		return false;
	}
	// Leave event BEFORE collision/trace; abortMove cancels the move
	// (src/MovementController.cpp:326-331).
	env_.game->eventFlagsForMovement(p.viewX, p.viewY, tx, ty);
	env_.game->abortMove = false;
	env_.vm->executeTile(p.viewX >> 6, p.viewY >> 6, env_.game->eventFlags_[0], true);
	if (env_.game->abortMove) return false;
	TraceSystem& trace = env_.game->trace;
	TraceHit h = trace.trace(p.viewX, p.viewY, tx, ty,
		env_.game->db.playerEntity(), Contents::PLAYERSOLID, 16);
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
			h.entity, Contents::PLAYERSOLID, 16);
	}
	// The familiar walking onto its body returns into it (:333-336).
	const bool intoBody = p.isFamiliar && tx == p.saveX && ty == p.saveY && h.isEntity() &&
		p.playerEntityCopyIndex >= 0 && h.entity == &env_.game->db.entities()[p.playerEntityCopyIndex];
	if (!h.blocks() || intoBody) {
		if (intoBody) env_.familiar->returnsToPlayer(true);
		p.attemptMove(tx, ty);
		p.setDestHeight(env_.map->heightAt(tx, ty));
		p.setZStep(p.destZ - p.viewZ);
		env_.game->relinkPlayer(p);                    // src/MovementController.cpp:348
		return true;
	}
	// TEMP [dbg] move-block audit (remove after blue-door bug fixed)
	std::fprintf(stderr,
		"[dbg] moveBlocked to %d,%d by spr=%d type=%d linked=%d frac=%d\n",
		tx >> 6, ty >> 6,
		h.entity ? (h.entity->getSprite()) : -1,
		h.isEntity() ? h.eType : -1,       // world hit logged as -1, as before
		(h.entity && (h.entity->info & Entity::kInfoLinked)) != 0 ? 1 : 0,
		h.frac);
	// A blocked step in the automap still costs a turn (src/MovementController.cpp:351-353).
	if (env_.host->state() == StateId::Automap) env_.game->advanceTurn();
	return false;
}

// ---- playing action handlers ----

void PlayerActions::handleAction(Action a) {
	Player& p = *env_.player;
	// The bot has no inventory screens (src/InputEventController.cpp:79-91;
	// the PDA has no rewrite key).
	if (p.isFamiliar && (a == Action::Items || a == Action::ItemsDrinks)) return;
	// ST_COMBAT handler (src/InputEventController.cpp:364-372): during a
	// monster attack only turning acts, startRotation runs for every key; the
	// player's own attack swallows all input. No idle gate here (legacy has
	// none), so quick presses stack.
	if (env_.game->combat.active) {
		if (env_.game->combat.curAttacker != nullptr && !env_.game->zoom.isZoomedIn) {
			if (a == Action::TurnRight) p.destAngle -= 256;
			else if (a == Action::TurnLeft) p.destAngle += 256;
			p.startRotation();
		}
		return;
	}
	// Zoomed ST_PLAYING input goes to the zoom handler first; only FIRE comes
	// back (src/InputEventController.cpp:358-361).
	const bool zoomed = env_.game->zoom.isZoomedIn && env_.host->state() == StateId::Playing;
	if (zoomed && env_.zoom->handle(a)) return;
	const bool inTP = env_.practice != nullptr && env_.practice->active();
	// Legacy gate: all playing input is ignored while any animation runs,
	// skipped while zoomed (src/PlayingInputHandler.cpp:25-27).
	bool gateBlocked = (p.viewX != p.destX || p.viewY != p.destY || p.viewAngle != p.destAngle);
	if (!zoomed && gateBlocked) return;
	// Monster gate (src/PlayingInputHandler.cpp:71-74): everything but turning
	// waits for the monsters to be snapped and animating effects to end.
	// snapMonsters may start a queued monster attack, which swallows the
	// action. activePropogators is tested first: it short-circuits snapMonsters.
	if (a != Action::TurnLeft && a != Action::TurnRight &&
	    (env_.game->sprites.activePropogators() != 0 ||
	     !env_.game->monsters.snapMonsters(env_.host->state() == StateId::Automap) || env_.game->lerps.animatingEffects() != 0)) {
		return;
	}

	switch (a) {
	case Action::TurnLeft:
		env_.hud->setDamageTime(0);              // src/PlayingInputHandler.cpp:112-114
		p.destAngle += 256;
		p.startRotation();
		break;
	case Action::TurnRight:
		env_.hud->setDamageTime(0);
		p.destAngle -= 256;
		p.startRotation();
		break;
	case Action::Forward:
		attemptMove(p.viewX + p.viewStepX, p.viewY + p.viewStepY);
		break;
	case Action::Back:
		attemptMove(p.viewX - p.viewStepX, p.viewY - p.viewStepY);
		break;
	case Action::Use: {
		// Loot source naming, the very first thing the legacy ACTION_FIRE
		// branch does (src/PlayingInputHandler.cpp:189-195): a faced
		// ET_ATTACK_INTERACTIVE names the next GIVELOOT header, anything else
		// clears it. Legacy reads Entity::name (= def->name | 0x400, i.e. the
		// same string in the ingame text type); the rewrite has no per-entity
		// name override, so def->name is used directly.
		env_.game->lootSource =
			(p.facingEntity != nullptr && p.facingEntity->def != nullptr &&
			 p.facingEntity->def->eType == Enums::ET_ATTACK_INTERACTIVE)
				? p.facingEntity->def->name : -1;
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
		// No corpse loot while zoomed (:323, `else if (!canvas->isZoomedIn)`).
		Entity* corpse = (p.ce.weapon == 1 || zoomed) ? nullptr
			: env_.game->loot.findLootableCorpseFacing(
				p.viewX, p.viewY, p.viewStepX, p.viewStepY);
		if (corpse != nullptr) {
			env_.host->requestState(StateId::Looting);
			break;
		}
		// Fire-target election, hoisted here out of the fire block below so
		// the crate branch can read it: legacy elects once (:197-385) and the
		// crate open (:387-393) sits between the corpse loot and the
		// front-tile script. electedDist must be captured now — a World hit's
		// distFrom reads the trace scratch collision point
		// (TraceSystem.cpp:233-239), which a later trace would overwrite;
		// entity hits are position-derived and stable.
		const int weapon2 = p.ce.weapon;                   // legacy ce->weapon (:197)
		TraceHit elected;                                  // legacy `entity`
		int electedDist = 0;
		if (weapon2 >= 0) {
			elected = env_.targeting->electFireTarget(weapon2);
			electedDist = elected.blocks()
				? env_.game->trace.distFrom(elected, p.viewX, p.viewY) : 0;
		}
		// Crate open (:387-393): eType 10 / eSubType 2 within one tile.
		// tileDistSq(1) is legacy tileDistances[0] = 4096 (Combat.cpp:69-72).
		// The weapon 11 else-arm of the legacy is empty.
		if (elected.isEntity() &&
		    elected.eType == Enums::ET_ATTACK_INTERACTIVE && elected.eSubType == 2 &&
		    electedDist <= env_.game->combat.tileDistSq(1)) {
			env_.game->openCrate(elected.entity);
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
			DoorSystem::DoorUseResult dr = env_.game->doors.useDoorFacing(
				*env_.map, p.viewX, p.viewY, p.viewStepX, p.viewStepY, p.isFamiliar);
			if (dr == DoorSystem::DoorUseResult::BotRefused) {
				std::fprintf(stderr, "[sound] 1111\n");    // :455-458, no turn
				if (env_.loc != nullptr) env_.hud->addMessage(env_.loc->get(kTextMain, 193), 0xAA000000, 2);
				consumed = true;
			} else if (dr == DoorSystem::DoorUseResult::Opened) {
				env_.game->advanceTurn();                  // opened doors consume the turn (:451-452)
				consumed = true;
			} else if (dr == DoorSystem::DoorUseResult::Locked) {
				std::fprintf(stderr, "[use] door locked\n"); // hud->addMessage(44) analog (:447-449)
				consumed = true;                       // legacy door branch return true consumes the press (:445-457)
			}
		}
		if (consumed) break;   // fire election only when nothing consumed the press
		// Secret push wall (src/PlayingInputHandler.cpp:461-466): elected
		// ET_SPRITEWALL within one tile AND the FACING tile carries 0x4. No turn.
		// shortcut: `elected` is only filled when weapon2 >= 0, so with no
		// weapon the secret cannot be opened; upgrade if an unarmed state
		// becomes reachable in play.
		if (elected.isEntity() && elected.eType == Enums::ET_SPRITEWALL &&
		    electedDist <= env_.game->combat.tileDistSq(1) &&
		    (env_.map->mapFlags[ty * 32 + tx] & TileFlag::SECRET_DOOR) != 0) {
			if (env_.game->lerps.startSecretWall(elected.entity)) env_.game->awardSecret(true);
			break;
		}
		// ---- fire (legacy probe src/PlayingInputHandler.cpp:189-548) ----
		// Order preserved: loot -> tile event -> door -> fire; reached ONLY
		// when nothing above consumed the press (legacy return-true chain).
		// weapon2 / elected / electedDist come from the hoisted election above
		// (legacy elects once at :197-385 and reuses it here).
		if (weapon2 >= 0 && !env_.game->combat.active) {
			TraceSystem& trace = env_.game->trace;
			const int dist2 = electedDist;
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
			           dist2 <= env_.game->combat.tileDistSq(1)) {         // :467 gate
				outcome = kWallPush;
			}
			if (outcome == kWallPush) {
				// Within 1 tile of a wall: lower the weapon, jolt the view 2
				// units toward the wall, NO turn consumed (:467-487); while
				// zoomed the press is swallowed (:468-470). Hero is forced to
				// characterChoice 1 -> no_use2; the bot bumps with 1111 (:472-480).
				if (zoomed) break;
				Combat& c = env_.game->combat;
				std::fprintf(stderr, "[sound] %d\n", p.isFamiliar ? 1111 : 1073);
				c.shiftWeapon(true);                                   // :482
				c.pushedWall = true;                                   // :483-484
				c.pushedTime = (int)*c.env().gameTime + 500;
				env_.hud->startRockView(*c.env().upTimeMs, 1000,       // :485
					p.viewX + (p.viewStepX >> 6) * 2, p.viewY + (p.viewStepY >> 6) * 2, p.viewZ);
			} else {
				// An exploding bot asks to self-destruct, a held bot deploys
				// (:489-495); a shooting bot falls through to the normal shot.
				if (p.isFamiliar && (p.familiarType == 2 || p.familiarType == 4)) {
					env_.familiar->startSelfDestructDialog();
					break;
				}
				if (!p.isFamiliar && ((1 << weapon2) & Enums::WP_SENTRY_BOT_MASK) != 0) {
					env_.familiar->attemptToDeploy();
					break;
				}
				// Zoom entry (mask 512 -> initZoom) consumes the input (:504-507).
				if (!zoomed && Combat::checkWeaponMask(weapon2, 512) &&
				    p.ammo[env_.game->combat.weaponDef(weapon2).ammoType] > 0) {
					env_.zoom->initZoom();
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
					if (zoomed) {                              // :518-522
						const int* e = env_.targeting->fireEnd();
						ZoomState& z = env_.game->zoom;
						z.zoomCollisionX = p.viewX + (elected.frac * (e[0] - p.viewX) >> 14);
						z.zoomCollisionY = p.viewY + (elected.frac * (e[1] - p.viewY) >> 14);
						z.zoomCollisionZ = p.viewZ + (elected.frac * (e[2] - p.viewZ) >> 14);
					}
				}
				p.fireWeapon(env_.game->combat, target, ax, ay);
				// NO advanceTurn here — the seq completion in tickPlaying
				// consumes the turn (spec §0.C).
				// Range scoring, even when fireWeapon refused (:524-531, :537-543).
				if (inTP) {
					if (p.ammo[1] == 0) {
						env_.practice->exit();
					} else if (outcome == kElected) {
						env_.practice->assessShot(target);
					} else if (env_.loc != nullptr) {
						env_.hud->addMessage(env_.loc->get(kTextMain, 68), 0xAA000000, 0);
					}
				}
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
	case Action::BotDiscard:   // src/PlayingInputHandler.cpp:77-84
		if (env_.host->state() == StateId::Automap) break;
		if (p.isFamiliar) {
			env_.familiar->returnsToPlayer(false);
		} else if (p.ce.weapon >= 0 && ((1 << p.ce.weapon) & Enums::WP_SENTRY_BOT_MASK) != 0) {
			env_.game->attemptToDiscardFamiliar(p.ce.weapon);
		}
		break;
	case Action::Automap:   // src/PlayingInputHandler.cpp:86-98
		if (inTP) break;
		env_.host->requestState(env_.host->state() != StateId::Automap ? StateId::Automap
		                                                               : StateId::Playing);
		break;
	case Action::BackKey:
		if (env_.host->state() == StateId::Automap) {   // src/PlayingInputHandler.cpp:138-141
			env_.host->requestState(StateId::Playing);
			break;
		}
		[[fallthrough]];
	case Action::Menu:
		// In the range both leave it instead (:142-143, :150-151).
		if (inTP) { env_.practice->exit(); break; }
		// Both open the in-game menu, from the playing input handler and
		// therefore behind the same animation gate as every other playing
		// action (src/PlayingInputHandler.cpp:137-158). The message queue is
		// emptied first (hud->msgCount = 0, :146 and :155).
		env_.hud->clearMessages();
		env_.host->requestState(StateId::Menu);
		break;
	case Action::Items:                       // src/PlayingInputHandler.cpp:159-168
		if (inTP) { env_.practice->exit(); break; }
		env_.hud->clearMessages();            // msgCount = 0 (:165)
		if (env_.openMenu) env_.openMenu(kMenuItems);
		break;
	case Action::ItemsDrinks:                 // src/PlayingInputHandler.cpp:169-178
		if (inTP) { env_.practice->exit(); break; }
		env_.hud->clearMessages();            // msgCount = 0
		if (env_.openMenu) env_.openMenu(kMenuItemsDrinks);
		break;
	default:
		// shortcut: NextWeapon/PrevWeapon only act while zoomed (ZoomController::handle);
		// Player::selectNext/PrevWeapon is not ported, port it if weapon cycling keys are wanted.
		break;
	}
}

} // namespace newcore
