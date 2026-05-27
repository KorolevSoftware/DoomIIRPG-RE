#include "ZoomController.h"

#include <algorithm>

#include "App.h"
#include "Button.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Combat.h"
#include "Enums.h"
#include "Game.h"
#include "Hud.h"
#include "MenuSystem.h"
#include "Menus.h"
#include "Player.h"
#include "Render.h"
#include "SDLGL.h"
#include "Sound.h"
#include "TinyGL.h"

void ZoomController::initZoom() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	canvas->zoomTime = 0;
	canvas->zoomCurFOVPercent = 0;
	canvas->zoomFOV = canvas->zoomDestFOV = 190;
	canvas->zoomAngle = 0;
	canvas->zoomPitch = 0;
	canvas->zoomTurn = 0;
	canvas->viewPitch = canvas->destPitch = 0;
	canvas->zoomStateTime = app->time;
	canvas->isZoomedIn = true;
	app->StartAccelerometer();
	canvas->m_sniperScopeDialScrollButton->Update(0, 320);
	canvas->zoomAccuracy = 2560 * std::max(0, std::min(((256 - app->player->ce->getStatPercent(Enums::STAT_ACCURACY)) << 8) / 26, 256)) >> 8;
	canvas->zoomMinFOVPercent = 256;
	canvas->zoomMaxAngle = 64 - (canvas->zoomAccuracy >> 8);
	app->render->startFade(500, 2);
	canvas->drawPlayingSoftKeys();

	CAppContainer::getInstance()->sdlGL->centerMouse(0, -22); // [GEC]
}

void ZoomController::zoomOut() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	canvas->isZoomedIn = false;
	canvas->viewAngle += canvas->zoomAngle;
	int n = 255;
	canvas->destAngle = canvas->viewAngle = ((canvas->viewAngle + (n >> 1)) & ~n);
	app->render->startFade(500, 2);
	canvas->finishRotation(true);
	app->tinyGL->resetViewPort();
	canvas->drawPlayingSoftKeys();
}

bool ZoomController::handleZoomEvents(int key, int action) {
	return this->handleZoomEvents(key, action, false);
}

bool ZoomController::handleZoomEvents(int key, int action, bool force) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (!force && ((canvas->zoomTime != 0) || app->game->activePropogators != 0 || app->game->animatingEffects != 0 || !app->game->snapMonsters(false))) {
		return true;
	}
	int n3 = 5 + ((20 * (256 - canvas->zoomCurFOVPercent)) >> 8);
	if (action == Enums::ACTION_MENU || action == Enums::ACTION_BACK) {
		this->zoomOut();
		return true;
	}
	if (action == Enums::ACTION_AUTOMAP) {
		if (!app->player->inTargetPractice) {
			app->hud->msgCount = 0;
			app->menuSystem->setMenu(Menus::MENU_INGAME);
			return true;
		}
	}
	else if (action == Enums::ACTION_RIGHT) {
		canvas->zoomAngle -= n3;
		canvas->updateFacingEntity = true;
		++canvas->zoomTurn;
		CAppContainer::getInstance()->sdlGL->centerMouse(0, -22); // [GEC]
		CAppContainer::getInstance()->app->StopAccelerometer(); // [GEC]
	}
	else if (action == Enums::ACTION_LEFT) {
		canvas->zoomAngle += n3;
		canvas->updateFacingEntity = true;
		++canvas->zoomTurn;
		CAppContainer::getInstance()->sdlGL->centerMouse(0, -22); // [GEC]
		CAppContainer::getInstance()->app->StopAccelerometer(); // [GEC]
	}
	else if (action == Enums::ACTION_DOWN) {
		canvas->zoomPitch -= n3;
		++canvas->zoomTurn;
		CAppContainer::getInstance()->sdlGL->centerMouse(0, -22); // [GEC]
		CAppContainer::getInstance()->app->StopAccelerometer(); // [GEC]
	}
	else if (action == Enums::ACTION_UP) {
		canvas->zoomPitch += n3;
		++canvas->zoomTurn;
		CAppContainer::getInstance()->sdlGL->centerMouse(0, -22); // [GEC]
		CAppContainer::getInstance()->app->StopAccelerometer(); // [GEC]
	}
	else if (action == Enums::ACTION_PASSTURN) {
		app->hud->addMessage((short)45);
		app->game->touchTile(canvas->destX, canvas->destY, false);
		app->game->advanceTurn();
		canvas->invalidateRect();
		canvas->zoomTurn = 0;
	}
	else if (action == Enums::ACTION_NEXTWEAPON) {
		if (canvas->zoomCurFOVPercent < canvas->zoomMinFOVPercent) {
			canvas->zoomCurFOVPercent += 64;
			canvas->zoomCurFOVPercent = std::min(canvas->zoomCurFOVPercent, canvas->zoomMinFOVPercent); // [GEC]
			++canvas->zoomTurn;
			canvas->zoomDestFOV = 190 + ((-55 * canvas->zoomCurFOVPercent) >> 7);
			canvas->zoomDestFOV = std::max(canvas->zoomDestFOV, 102);
			canvas->zoomTime = app->time + 360;

			// [GEC] update scroll bar
			{
				float maxScroll = (float)((canvas->m_sniperScopeDialScrollButton->barRect).h - canvas->m_sniperScopeDialScrollButton->field_0x4c_);
				float curFOV = (float)((float)canvas->zoomCurFOVPercent / (float)canvas->zoomMinFOVPercent);
				canvas->m_sniperScopeDialScrollButton->field_0x48_ = (int)(maxScroll - (maxScroll * curFOV));
				app->sound->playSound(1113, 0, 5, false);
			}

		}
	}
	else if (action == Enums::ACTION_PREVWEAPON) {
		if (canvas->zoomCurFOVPercent > 0) {
			canvas->zoomCurFOVPercent -= 64;
			canvas->zoomCurFOVPercent = std::max(canvas->zoomCurFOVPercent, 0); // [GEC]
			++canvas->zoomTurn;
			canvas->zoomDestFOV = 190 + ((-55 * canvas->zoomCurFOVPercent) >> 7);
			canvas->zoomTime = app->time + 360;

			// [GEC] update scroll bar
			{
				float maxScroll = (float)((canvas->m_sniperScopeDialScrollButton->barRect).h - canvas->m_sniperScopeDialScrollButton->field_0x4c_);
				float curFOV = (float)((float)canvas->zoomCurFOVPercent / (float)canvas->zoomMinFOVPercent);
				canvas->m_sniperScopeDialScrollButton->field_0x48_ = (int)(maxScroll - (maxScroll * curFOV));
				app->sound->playSound(1113, 0, 5, false);
			}
		}
		++canvas->zoomTurn;
	}
	else if (action == Enums::ACTION_FIRE) {
		canvas->zoomTurn = 0;
		return canvas->handlePlayingEvents(key, action);
	}

	if (canvas->zoomPitch < -canvas->zoomMaxAngle) {
		canvas->zoomPitch = -canvas->zoomMaxAngle;
	}
	else if (canvas->zoomPitch > canvas->zoomMaxAngle) {
		canvas->zoomPitch = canvas->zoomMaxAngle;
	}
	if ((canvas->zoomTurn & 0x7) == 0x7) {
		app->game->advanceTurn();
	}
	return true;
}
