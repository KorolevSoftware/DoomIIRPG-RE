#include "InputEventController.h"

#include "App.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Combat.h"
#include "Enums.h"
#include "Game.h"
#include "HackingGame.h"
#include "Input.h"
#include "MenuSystem.h"
#include "Menus.h"
#include "Player.h"
#include "Render.h"
#include "Resource.h"
#include "SentryBotGame.h"
#include "Sound.h"
#include "VendingMachine.h"

namespace
{
	static constexpr int NUM_CODES = 36;

	int keys_codeActions[NUM_CODES] = {
		AVK_CLR,		Enums::ACTION_BACK,
		AVK_SOFT2,		Enums::ACTION_AUTOMAP,
		AVK_SOFT1,		Enums::ACTION_MENU,
		// New items Only Port
		AVK_STAR,		Enums::ACTION_PREVWEAPON,
		AVK_POUND,		Enums::ACTION_AUTOMAP,
		AVK_NEXTWEAPON, Enums::ACTION_NEXTWEAPON,
		AVK_PREVWEAPON, Enums::ACTION_PREVWEAPON,
		AVK_AUTOMAP,	Enums::ACTION_AUTOMAP,
		AVK_UP,			Enums::ACTION_UP,
		AVK_DOWN,		Enums::ACTION_DOWN,
		AVK_LEFT,		Enums::ACTION_LEFT,
		AVK_RIGHT,		Enums::ACTION_RIGHT,
		AVK_MOVELEFT,	Enums::ACTION_STRAFELEFT,
		AVK_MOVERIGHT,	Enums::ACTION_STRAFERIGHT,
		AVK_SELECT,		Enums::ACTION_FIRE,
		AVK_MENUOPEN,	Enums::ACTION_MENU,
		AVK_PASSTURN,	Enums::ACTION_PASSTURN,
		AVK_BOTDISCARD,	Enums::ACTION_BOT_DISCARD
	};
}

int InputEventController::getKeyAction(int key) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int iVar1;

	//printf("getKeyAction i %d\n", key);

	if (canvas->state == Canvas::ST_MENU) {
		if (key & AVK_MENU_UP) {
			return Enums::ACTION_UP;
		}
		if (key & AVK_MENU_DOWN) {
			return Enums::ACTION_DOWN;
		}
		if (key & AVK_MENU_PAGE_UP) {
			return Enums::ACTION_LEFT;
		}
		if (key & AVK_MENU_PAGE_DOWN) {
			return Enums::ACTION_RIGHT;
		}
		if (key & AVK_MENU_SELECT) {
			return Enums::ACTION_FIRE;
		}
		if (key & AVK_ITEMS_INFO) {
			return Enums::ACTION_MENU_ITEM_INFO;
		}
	}

	if (key & AVK_MENU_OPEN) {
		return Enums::ACTION_MENU;
	}

	if (!app->player->isFamiliar) {
		if (key & AVK_ITEMS_INFO) {
			return Enums::ACTION_ITEMS;
		}

		if (key & AVK_DRINKS) {
			return Enums::ACTION_ITEMS_DRINKS;
		}

		if (key & AVK_PDA) {
			return Enums::ACTION_QUESTLOG;
		}
	}

	key &= ~(AVK_MENU_UP | AVK_MENU_DOWN | AVK_MENU_PAGE_UP | AVK_MENU_PAGE_DOWN | AVK_MENU_SELECT | AVK_MENU_OPEN | AVK_ITEMS_INFO | AVK_DRINKS | AVK_PDA);

	for (int j = 0; j < (NUM_CODES / 2); j++)
	{
		if (keys_codeActions[(j * 2) + 0] == key) {
			//printf("rtn %d\n", keys_codeActions[(j * 2) + 1]);
			return keys_codeActions[(j * 2) + 1];
		}
	}

	if (key - 1U < 10) { // KEY_1 to KEY_9 ... KEY_0
		return canvas->keys_numeric[key - 1U];
	}

	if (key == 12) { // KEY_STAR
		return Enums::ACTION_PREVWEAPON;
	}
	if (key == 11) { // KEY_POUND
		return Enums::ACTION_AUTOMAP;
	}
	if (key == 14) { // KEY_ARROWLEFT
		return Enums::ACTION_LEFT;
	}
	if (key == 15) { // KEY_ARROWRIGHT
		return Enums::ACTION_RIGHT;
	}
	if (key == 16) { // KEY_ARROWUP
		return Enums::ACTION_UP;
	}
	if (key == 17) { // KEY_ARROWDOWN
		return Enums::ACTION_DOWN;
	}
	if (key == 13) { // KEY_OK
		return Enums::ACTION_FIRE;
	}

	iVar1 = (key ^ key >> 0x1f) - (key >> 0x1f);
	if (iVar1 == 19) { // KEY_LEFTSOFT
		return Enums::ACTION_MENU;
	}
	if (iVar1 == 20) { // KEY_RIGHTSOFT
		return Enums::ACTION_AUTOMAP; // ACTION_AUTOMAP
	}
	if (iVar1 == 18) { // KEY_CLR, KEY_BACK
		return Enums::ACTION_BACK;
	}

	return Enums::ACTION_NONE;
}

void InputEventController::clearEvents(int ignoreFrameInput) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	canvas->numEvents = 0;
	canvas->keyDown = false;
	canvas->keyDownCausedMove = false;
	canvas->ignoreFrameInput = ignoreFrameInput;
}

bool InputEventController::handleEvent(int key) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	int state = canvas->state;
	int keyAction = this->getKeyAction(key);
	int fadeFlags = app->render->getFadeFlags();


	//printf("handleEvent key: %d keyAction: %d\n", key, keyAction);
	//printf("this->state %d\n", state);

	if (key == 26)
		return true;

	if (key == 18) {
		switch (state)
		{
		case Canvas::ST_LOGO:
			app->shutdown();
			break;
		case Canvas::ST_MENU:
			if (app->menuSystem->menu == Menus::MENU_ENABLE_SOUNDS) {
				app->shutdown();
			}
			break;
		case Canvas::ST_INTRO_MOVIE:
			canvas->exitIntroMovie(true);
			app->shutdown();
			break;
		}
		return true;
	}

	if (canvas->state == Canvas::ST_MENU && app->menuSystem->changeValues) {
		if (app->menuSystem->changeSfxVolume) { // [GEC]
			if (keyAction == Enums::ACTION_RIGHT) {
				app->sound->volumeUp(10);
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
			else if (keyAction == Enums::ACTION_LEFT) {
				app->sound->volumeDown(10);
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
		}
		else if (app->menuSystem->changeMusicVolume) { // [GEC]
			if (keyAction == Enums::ACTION_RIGHT) {
				app->sound->musicVolumeUp(10);
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
			else if (keyAction == Enums::ACTION_LEFT) {
				app->sound->musicVolumeDown(10);
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
		}
		else if (app->menuSystem->changeButtonsAlpha) { // [GEC]
			if (keyAction == Enums::ACTION_RIGHT) {
				canvas->m_controlAlpha += 10;
				if (canvas->m_controlAlpha > 100) {
					canvas->m_controlAlpha = 100;
				}
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
			else if (keyAction == Enums::ACTION_LEFT) {
				canvas->m_controlAlpha -= 10;
				if (canvas->m_controlAlpha < 0) {
					canvas->m_controlAlpha = 0;
				}
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
		}
		else if (app->menuSystem->changeVibrationIntensity) { // [GEC]
			if (keyAction == Enums::ACTION_RIGHT) {
				gVibrationIntensity += 10;
				if (gVibrationIntensity > 100) {
					gVibrationIntensity = 100;
				}
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
			else if (keyAction == Enums::ACTION_LEFT) {
				gVibrationIntensity -= 10;
				if (gVibrationIntensity < 0) {
					gVibrationIntensity = 0;
				}
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
		}
		else if (app->menuSystem->changeDeadzone) { // [GEC]
			if (keyAction == Enums::ACTION_RIGHT) {
				gDeadZone += 5;
				if (gDeadZone > 100) {
					gDeadZone = 100;
				}
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
			else if (keyAction == Enums::ACTION_LEFT) {
				gDeadZone -= 5;
				if (gDeadZone < 0) {
					gDeadZone = 0;
				}
				app->menuSystem->soundClick();
				app->menuSystem->refresh();
				return true;
			}
		}
	}

#if 0 // IOS
	if (app->sound->allowSounds) {
		bool refresh = false;
		if (canvas->state == Canvas::ST_MENU && app->menuSystem->changeSfxVolume) {
			refresh = true;
		}
		if (key == 27) {
			app->sound->volumeUp(10);
			if (refresh) {
				app->menuSystem->refresh();
			}
			return true;
		}
		if (key == 28) {
			app->sound->volumeDown(10);
			if (refresh) {
				app->menuSystem->refresh();
			}
			return true;
		}
	}
#endif

	if (canvas->st_enabled) {
		canvas->st_enabled = false;
		canvas->renderOnly = false;
	}

	if (fadeFlags != 0 && (fadeFlags & 0x10) != 0x0) {
		return true;
	}

	if (state == Canvas::ST_ERROR) {
		if (keyAction == Enums::ACTION_FIRE || key == 18) {
			app->shutdown();
		}
	}
	else if (state == Canvas::ST_MENU) {
		app->menuSystem->handleMenuEvents(key, keyAction);
	}
	else if (state == Canvas::ST_CHARACTER_SELECTION) {
		canvas->handleCharacterSelectionInput(key, keyAction);
	}
	else if (state == Canvas::ST_INTRO) {
		canvas->handleStoryInput(key, keyAction);
	}
	else if (state == Canvas::ST_INTRO_MOVIE) {
		app->game->skipMovie = (keyAction == Enums::ACTION_FIRE);
	}
	else if (state == Canvas::ST_EPILOGUE) {
		if (key == 18) {
			canvas->disposeEpilogue();
		}
	}
	else if (state == Canvas::ST_DIALOG) {
		canvas->handleDialogEvents(key);
	}
	else if (state == Canvas::ST_MINI_GAME) { // [GEC] Restored from J2ME/BREW
		switch (canvas->stateVars[0]) {
			case 2: {
				app->hackingGame->handleInput(keyAction);
				break;
			}
			case 0: {
				app->sentryBotGame->handleInput(keyAction);
				break;
			}
			case 4: {
				app->vendingMachine->handleInput(keyAction);
				break;
			}
		}
	}
	else if (state == Canvas::ST_BENCHMARK || state == Canvas::ST_BENCHMARKDONE) { // [GEC] Restored from J2ME/BREW
		canvas->setState(Canvas::ST_PLAYING);
		canvas->setAnimFrames(canvas->animFrames);
	}
	else if (state == Canvas::ST_AUTOMAP) {
		canvas->automapDrawn = false;
		return canvas->handlePlayingEvents(key, keyAction);
	}
	else if (state == Canvas::ST_PLAYING) {
		if (canvas->isZoomedIn) {
			return canvas->handleZoomEvents(key, keyAction);
		}
		return canvas->handlePlayingEvents(key, keyAction);
	}
	else if (state == Canvas::ST_COMBAT) {
		if (app->combat->curAttacker != nullptr && !canvas->isZoomedIn) {
			if (keyAction == Enums::ACTION_RIGHT) {
				canvas->destAngle -= 256;
			}
			else if (keyAction == Enums::ACTION_LEFT) {
				canvas->destAngle += 256;
			}
			canvas->startRotation(false);
		}
		if (canvas->combatDone && !app->game->interpolatingMonsters) {
			canvas->setState(Canvas::ST_PLAYING);
			if (app->combat->curAttacker == nullptr) {
				app->game->advanceTurn();
				if (state == Canvas::ST_PLAYING) {
					if (canvas->isZoomedIn) {
						return canvas->handleZoomEvents(key, keyAction);
					}
					return canvas->handlePlayingEvents(key, keyAction);
				}
			}
		}
	}
	else if (state == Canvas::ST_CREDITS) {
		if (canvas->endingGame) {
			if ((keyAction == Enums::ACTION_FIRE && canvas->introSequenceManager.scrollingTextDone) || key == 18) {
				canvas->endingGame = false;
				app->sound->soundStop();
				delete app->menuSystem->imgMainBG;
				delete app->menuSystem->imgLogo;
				app->menuSystem->imgMainBG = app->loadImage(Resources::RES_LOGO_BMP_GZ, true);
				app->menuSystem->imgLogo = app->loadImage(Resources::RES_LOGO2_BMP_GZ, true);
				app->menuSystem->background = app->menuSystem->imgMainBG;
				app->menuSystem->setMenu(Menus::MENU_END_);
			}
		}
		else if (keyAction == Enums::ACTION_BACK || keyAction == Enums::ACTION_FIRE) {
			if (canvas->loadMapID == 0) {
				int n2 = 3;
				if (app->game->hasSavedState()) {
					++n2;
				}
				app->menuSystem->pushMenu(3, n2, 0, 0, 0);
				app->menuSystem->setMenu(Menus::MENU_MAIN_HELP);
			}
			else {
				app->sound->soundStop();
				app->menuSystem->pushMenu(29, 7, 0, 0, 0);
				app->menuSystem->setMenu(Menus::MENU_INGAME_HELP);
			}
		}
	}
	else if (state == Canvas::ST_CAMERA) {
		if (!canvas->changeMapStarted && app->gameTime > app->game->cinUnpauseTime && (keyAction == Enums::ACTION_PASSTURN || keyAction == Enums::ACTION_AUTOMAP || keyAction == Enums::ACTION_FIRE || key == 18)) {
			app->game->skipCinematic();
		}
	}
	else if (state == Canvas::ST_DYING) {
		if (canvas->stateVars[0] > 0 && (keyAction == Enums::ACTION_FIRE || key == 18)) {
			app->menuSystem->setMenu(Menus::MENU_INGAME_DEAD);
		}
	}
	else if (state == Canvas::ST_MIXING) {
		// No implementado
	}
	else if (state == Canvas::ST_TRAVELMAP) {
		canvas->handleTravelMapInput(key, keyAction);
	}
	else if (state == Canvas::ST_LOOTING) {
		canvas->handleLootingEvents(keyAction);
	}
	else if (state == Canvas::ST_TREADMILL) {
		canvas->handleTreadmillEvents(keyAction);
	}
	else {
		return false;
	}

	return true;
}

void InputEventController::runInputEvents() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	while (canvas->blockInputTime == 0) {
		if (canvas->ignoreFrameInput > 0) {
			canvas->numEvents = 0;
			canvas->ignoreFrameInput--;
			return;
		}
		if (canvas->numEvents == 0) {
			return;
		}


		int ev = canvas->events[0];
		//printf("this->events[0] %d\n", ev);
		canvas->st_fields[11] = app->upTimeMs;
		if (!this->handleEvent(ev)) {
			return;
		}

		if (canvas->numEvents > 0) {
			canvas->numEvents--;
		}
	}

	if (app->gameTime > canvas->blockInputTime) {
		if (canvas->state == Canvas::ST_PLAYING) {
			canvas->drawPlayingSoftKeys();
		}
		canvas->blockInputTime = 0;
	}
	this->clearEvents(1);
}

void InputEventController::addEvents(int event) { // [GEC]
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	if (canvas->numEvents < Canvas::MAX_EVENTS) {
		//printf("numEvents %d Code %d\n", canvas->numEvents, event);
		canvas->events[0] = event;
		canvas->numEvents = 1;
	}
}
