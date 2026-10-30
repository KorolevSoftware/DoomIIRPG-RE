#include "core/TargetPractice.h"

#include <cstdio>
#include <string>

#include "core/CinematicCamera.h"
#include "core/PlayerActions.h"
#include "core/ZoomController.h"
#include "domain/game/DialogSystem.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/game/ShotZones.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Media.h"
#include "ui/Hud.h"

namespace newcore {

void TargetPractice::init(const Env& env) {
	env_ = env;
}

void TargetPractice::enter(int tileX, int tileY, int dir, ScriptThread* thread) {
	Player& p = *env_.player;
	active_ = true;
	thread_ = thread;
	saveX_ = p.viewX;
	saveY_ = p.viewY;
	saveZ_ = p.viewZ;
	saveAngle_ = p.viewAngle;
	// {4:W, 0:E, 2:N, else:S} (src/Player.cpp:2531-2542, src/Enums.h:377-383).
	const int angle = dir == 4 ? 512 : dir == 0 ? 0 : dir == 2 ? 256 : 768;
	p.destAngle = p.viewAngle = angle;
	p.destX = p.viewX = (tileX << 6) + 32;
	p.destY = p.viewY = (tileY << 6) + 32;
	p.destZ = p.viewZ = env_.map->heightAt(p.viewX, p.viewY) + 36;
	// Deviation D1 (spec 2026-09-22-monster-attacks §2.1): legacy does not
	// relink here and leaves a stale player link on the old tile.
	env_.game->relinkPlayer(p);
	env_.actions->finishRotationFired();       // step-vector refresh + FACE event
	// showHelp(17, false) (src/Player.cpp:1175-1194); enableHelp is not ported (always on).
	if (!env_.cinematic->active() && (p.helpBitmask & (1 << 17)) == 0) {
		p.helpBitmask |= 1 << 17;
		env_.dialogs->enqueueHelpDialog(kTextMain, 17, -1);
	}
	p.stripInventoryForTargetPractice();
	score_ = 0;
	env_.hud->startFade(*env_.upTimeMs, 750, Hud::kFadeIn);
	std::fprintf(stderr, "[targetpractice] enter tile=%d,%d dir=%d\n", tileX, tileY, dir);
}

void TargetPractice::assessShot(Entity* target) {
	const MapData& map = *env_.map;
	const ZoomState& z = env_.game->zoom;
	const int s = target->getSprite();
	const int n = map.numSprites;
	const int dx = z.zoomCollisionX - map.mapSprites[s];
	const int dy = z.zoomCollisionY - map.mapSprites[n + s];
	const int dz = z.zoomCollisionZ - map.spriteZ(s);
	const int anim = (map.mapSpriteInfo[s] >> 8) & 0xF0;
	const int zone = shotZone(env_.media->mappings(), target->def->tileIndex, dx, dy, dz);
	const int64_t now = *env_.upTimeMs;
	int msg;
	headShotTime_ = bodyShotTime_ = legShotTime_ = 0;
	if (zone == 0 || (zone != -1 && anim == Enums::MANIM_IDLE_BACK)) {
		msg = 230;
		score_ += 30;
		headShotTime_ = now + 500;
	} else if (zone == 1) {
		msg = 231;
		score_ += 20;
		bodyShotTime_ = now + 500;
	} else if (zone == 2) {
		msg = 232;
		score_ += 10;
		legShotTime_ = now + 500;
	} else {
		msg = 68;
	}
	env_.hud->addMessage(env_.loc->get(kTextMain, msg), 0xFF7F0000, Hud::kMsgFlagImportant);
	std::fprintf(stderr, "[targetpractice] shot zone=%d d=(%d,%d,%d) score=%d\n", zone, dx, dy, dz, score_);
}

void TargetPractice::exit() {
	Player& p = *env_.player;
	active_ = false;
	p.destAngle = p.viewAngle = saveAngle_;
	p.destX = p.viewX = saveX_;
	p.destY = p.viewY = saveY_;
	p.destZ = p.viewZ = saveZ_;
	env_.game->relinkPlayer(p);                // D1 counterpart of enter
	env_.actions->finishRotationFired();       // finishRotation(false)
	// Legacy calls forceRemoveFromScopeZoom inside restoreInventory (src/Player.cpp:2755);
	// hoisted, restoreInventory reads no view state (spec G4.2).
	env_.zoom->forceRemove();
	p.restoreInventory();
	env_.hud->startFade(*env_.upTimeMs, 750, Hud::kFadeIn);
	std::string args[2] = { std::to_string(score_), std::string() };
	int msg = 234;
	int numArgs = 1;
	if (score_ > 240 / 2) {
		const int gain = p.modifyStat(Enums::STAT_ACCURACY, 1);
		if (gain > 0) {
			args[1] = std::to_string(gain);
			numArgs = 2;
			msg = 233;
		} else {
			msg = 235;
		}
	}
	std::string text = env_.loc->get(kTextMain, msg);
	composeArgs(text, args, numArgs);
	env_.hud->addMessage(text, 0xAA000000, Hud::kMsgFlagForce | Hud::kMsgFlagCenter);
	std::fprintf(stderr, "[targetpractice] exit score=%d msg=%d\n", score_, msg);
	if (thread_ != nullptr) {
		ScriptThread* t = thread_;
		thread_ = nullptr;
		env_.host->requestState(StateId::Playing);
		env_.vm->resumeThread(t);
	}
}

void TargetPractice::reset() {
	active_ = false;
	score_ = 0;
	thread_ = nullptr;
}

void TargetPractice::layerFrames(int64_t now, bool zoomed, int out[4]) {
	int64_t* timers[3] = { &legShotTime_, &bodyShotTime_, &headShotTime_ };
	static constexpr int kFlash[3] = { 0, 2, 3 };
	static constexpr int kIdle[3] = { 5, 6, 7 };
	for (int k = 0; k < 3; ++k) {
		int64_t& t = *timers[k];
		if (t == 0) {
			out[k] = kIdle[k];
		} else if (t > now) {
			out[k] = kFlash[k];
		} else {
			t = 0;
			out[k] = -1;
		}
	}
	out[3] = zoomed ? 1 : 4;
}

} // namespace newcore
