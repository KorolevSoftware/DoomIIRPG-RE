#include "core/ZoomController.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/PlayerActions.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "ui/Hud.h"

namespace newcore {

void ZoomController::init(const Env& env) {
	env_ = env;
}

bool ZoomController::zoomedIn() const {
	return env_.game->zoom.isZoomedIn;
}

void ZoomController::initZoom() {
	ZoomState& z = env_.game->zoom;
	Player& p = *env_.player;
	const int64_t now = *env_.upTimeMs;
	z.zoomTime = 0;
	z.zoomCurFOVPercent = 0;
	z.zoomFOV = z.zoomDestFOV = 190;
	z.zoomAngle = 0;
	z.zoomPitch = 0;
	z.zoomTurn = 0;
	p.viewPitch = 0;
	z.zoomStateTime = now;
	z.isZoomedIn = true;
	z.zoomAccuracy = 2560 * std::clamp(((256 - p.ce.getStatPercent(Enums::STAT_ACCURACY)) << 8) / 26, 0, 256) >> 8;
	z.zoomMinFOVPercent = 256;
	z.zoomMaxAngle = 64 - (z.zoomAccuracy >> 8);
	env_.hud->startFade(now, 500, Hud::kFadeIn);
}

void ZoomController::zoomOut() {
	ZoomState& z = env_.game->zoom;
	Player& p = *env_.player;
	z.isZoomedIn = false;
	p.viewAngle += z.zoomAngle;
	p.destAngle = p.viewAngle = (p.viewAngle + 127) & ~255;
	env_.hud->startFade(*env_.upTimeMs, 500, Hud::kFadeIn);
	env_.actions->finishRotationFired();         // finishRotation(true)
}

void ZoomController::forceRemove() {
	ZoomState& z = env_.game->zoom;
	if (z.isZoomedIn) {
		z.zoomTurn = 0;
		zoomOut();                               // handleZoomEvents(-6, 15, true): forced ACTION_BACK
	}
}

void ZoomController::drop() {
	ZoomState& z = env_.game->zoom;
	if (!z.isZoomedIn) return;
	Player& p = *env_.player;
	z.isZoomedIn = false;
	p.destAngle = p.viewAngle = (p.viewAngle + z.zoomAngle + 127) & ~255;
}

bool ZoomController::handle(Action a) {
	ZoomState& z = env_.game->zoom;
	Game& game = *env_.game;
	if (z.zoomTime != 0 || game.sprites.activePropogators() != 0 ||
	    game.lerps.animatingEffects() != 0 || !game.monsters.snapMonsters(false)) {
		return true;
	}
	const int n3 = 5 + ((20 * (256 - z.zoomCurFOVPercent)) >> 8);
	switch (a) {
	case Action::Menu:
	case Action::BackKey:
		zoomOut();
		return true;
	case Action::Automap:
		if (!(env_.inTargetPractice && env_.inTargetPractice())) {
			env_.hud->clearMessages();
			env_.host->requestState(StateId::Menu);
			return true;
		}
		break;
	case Action::TurnRight:
		z.zoomAngle -= n3;
		++z.zoomTurn;
		break;
	case Action::TurnLeft:
		z.zoomAngle += n3;
		++z.zoomTurn;
		break;
	case Action::Back:
		z.zoomPitch -= n3;
		++z.zoomTurn;
		break;
	case Action::Forward:
		z.zoomPitch += n3;
		++z.zoomTurn;
		break;
	case Action::Passturn:
		// Same message path as PlayerActions' Passturn arm (touchTile stub not ported).
		if (env_.loc != nullptr) {
			env_.hud->showCenterMessage(env_.loc->get(kTextMain, 45), 0xAA000000, 3500);
		}
		game.advanceTurn();
		z.zoomTurn = 0;
		break;
	case Action::NextWeapon:
		if (z.zoomCurFOVPercent < z.zoomMinFOVPercent) {
			z.zoomCurFOVPercent = std::min(z.zoomCurFOVPercent + 64, z.zoomMinFOVPercent);
			++z.zoomTurn;
			z.zoomDestFOV = std::max(190 + ((-55 * z.zoomCurFOVPercent) >> 7), 102);
			z.zoomTime = *env_.upTimeMs + 360;
			std::fprintf(stderr, "[sound] 1113\n");
		}
		break;
	case Action::PrevWeapon:
		if (z.zoomCurFOVPercent > 0) {
			z.zoomCurFOVPercent = std::max(z.zoomCurFOVPercent - 64, 0);
			++z.zoomTurn;
			z.zoomDestFOV = 190 + ((-55 * z.zoomCurFOVPercent) >> 7);
			z.zoomTime = *env_.upTimeMs + 360;
			std::fprintf(stderr, "[sound] 1113\n");
		}
		++z.zoomTurn;                            // src/ZoomController.cpp:150, unconditional
		break;
	case Action::Use:
		z.zoomTurn = 0;
		return false;
	default:
		break;
	}
	z.zoomPitch = std::clamp(z.zoomPitch, -z.zoomMaxAngle, z.zoomMaxAngle);
	if ((z.zoomTurn & 7) == 7) game.advanceTurn();
	return true;
}

bool ZoomController::renderPose(ZoomPose& out) {
	ZoomState& z = env_.game->zoom;
	if (!z.isZoomedIn) return false;
	const std::vector<int32_t>& sin = env_.tables->sinTable;
	const Player& p = *env_.player;
	const int64_t now = *env_.upTimeMs;
	const int64_t t = now - z.zoomStateTime;
	const int n2 = z.zoomAccuracy * sin[(t / 2) & 0x3FF] >> 24;
	const int n3 = z.zoomAccuracy * sin[(t / 3) & 0x3FF] >> 24;
	int fov;
	if (now < z.zoomTime) {
		fov = z.zoomDestFOV + (int)((z.zoomFOV - z.zoomDestFOV) * (z.zoomTime - now) / 360);
	} else {
		z.zoomTime = 0;
		fov = z.zoomFOV = z.zoomDestFOV;
	}
	int pitch = z.zoomPitch + p.viewPitch;
	// Recoil kick of the player's own shot (ST_COMBAT && curAttacker == nullptr && nextStage == 1).
	const Combat& c = env_.game->combat;
	if (c.active && c.curAttacker == nullptr && c.nextStage == 1 && c.animTime > 0) {
		const int64_t frac = ((*env_.gameTime - c.animStartTime) << 16) / c.animTime;
		pitch += sin[(512 * frac >> 16) & 0x3FF] * 28 >> 16;
	}
	out = ZoomPose{ p.viewAngle + z.zoomAngle + n2, pitch + n3, fov };
	return true;
}

} // namespace newcore
