#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Game.h"
#include "Render.h"
#include "Player.h"
#include "Combat.h"
#include "Hud.h"
#include "Sound.h"
#include "MenuSystem.h"
#include "Menus.h"
#include "MayaCamera.h"
#include "Entity.h"
#include "ScriptThread.h"
#include "Image.h"
#include "GameStateRunner.h"

GameStateRunner::GameStateRunner() {}

void GameStateRunner::combatState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	app->game->monsterLerp();
	app->game->updateLerpSprites();
	if (canvas->combatDone) {
		if (!app->game->interpolatingMonsters) {
			if (app->combat->curAttacker == nullptr) {
				app->game->advanceTurn();
			}
			if (!app->game->isCameraActive()) {
				canvas->setState(Canvas::ST_PLAYING);
			}
			else {
				canvas->setState(Canvas::ST_CAMERA);
				app->game->activeCamera->cameraThread->run();
			}
		}
	}
	else if (app->combat->runFrame() == 0) {
		if (canvas->state == Canvas::ST_DYING || canvas->state == Canvas::ST_BOT_DYING) {
			while (app->game->combatMonsters != nullptr) {
				app->game->combatMonsters->undoAttack();
			}
			return;
		}
		if (app->combat->curAttacker == nullptr) {
			app->game->touchTile(canvas->destX, canvas->destY, false);
			canvas->combatDone = true;
		}
		else if (canvas->knockbackDist == 0) {
			Entity* curAttacker = app->combat->curAttacker;
			if ((curAttacker->monster->goalFlags & 0x8) != 0x0) {
				curAttacker->monster->resetGoal();
				curAttacker->monster->goalType = 5;
				curAttacker->monster->goalParam = 1;
				curAttacker->aiThink(false);
			}
			Entity* nextAttacker;
			Entity* nextAttacker2;
			for (nextAttacker = curAttacker->monster->nextAttacker; nextAttacker != nullptr && nextAttacker->monster->target == nullptr && !nextAttacker->aiIsAttackValid(); nextAttacker = nextAttacker2) {
				nextAttacker2 = nextAttacker->monster->nextAttacker;
				nextAttacker->undoAttack();
			}
			if (nextAttacker != nullptr) {
				app->combat->performAttack(nextAttacker, nextAttacker->monster->target, 0, 0, false);
			}
			else {
				app->game->combatMonsters = nullptr;
				if (app->game->interpolatingMonsters) {
					canvas->setState(Canvas::ST_PLAYING);
				}
				else {
					app->game->endMonstersTurn();
					canvas->drawPlayingSoftKeys();
					canvas->combatDone = true;
				}
			}
		}
	}
	canvas->updateView();
	canvas->repaintFlags |= Canvas::REPAINT_PARTICLES;
	app->hud->repaintFlags |= 0x2B; // J2ME 0x6B
	if (!app->game->isCameraActive()) {
		canvas->repaintFlags |= Canvas::REPAINT_HUD;
	}
}

void GameStateRunner::renderOnlyState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (canvas->st_enabled) {
		canvas->viewAngle = (canvas->viewAngle + canvas->animAngle & 0x3FF);
		canvas->viewPitch = 0;
	}
	else {
		if (canvas->viewX == canvas->destX && canvas->viewY == canvas->destY && canvas->viewAngle == canvas->destAngle) {
			return;
		}
		if (canvas->viewX < canvas->destX) {
			canvas->viewX += canvas->animPos;
			if (canvas->viewX > canvas->destX) {
				canvas->viewX = canvas->destX;
			}
		}
		else if (canvas->viewX > canvas->destX) {
			canvas->viewX -= canvas->animPos;
			if (canvas->viewX < canvas->destX) {
				canvas->viewX = canvas->destX;
			}
		}
		if (canvas->viewY < canvas->destY) {
			canvas->viewY += canvas->animPos;
			if (canvas->viewY > canvas->destY) {
				canvas->viewY = canvas->destY;
			}
		}
		else if (canvas->viewY > canvas->destY) {
			canvas->viewY -= canvas->animPos;
			if (canvas->viewY < canvas->destY) {
				canvas->viewY = canvas->destY;
			}
		}
		if (canvas->viewZ < canvas->destZ) {
			++canvas->viewZ;
		}
		else if (canvas->viewZ > canvas->destZ) {
			--canvas->viewZ;
		}
		if (canvas->viewAngle < canvas->destAngle) {
			canvas->viewAngle += canvas->animAngle;
			if (canvas->viewAngle > canvas->destAngle) {
				canvas->viewAngle = canvas->destAngle;
			}
		}
		else if (canvas->viewAngle > canvas->destAngle) {
			canvas->viewAngle -= canvas->animAngle;
			if (canvas->viewAngle < canvas->destAngle) {
				canvas->viewAngle = canvas->destAngle;
			}
		}
		if (canvas->viewPitch < canvas->destPitch) {
			canvas->viewPitch += canvas->pitchStep;
			if (canvas->viewPitch > canvas->destPitch) {
				canvas->viewPitch = canvas->destPitch;
			}
		}
		else if (canvas->viewPitch > canvas->destPitch) {
			canvas->viewPitch -= canvas->pitchStep;
			if (canvas->viewPitch < canvas->destPitch) {
				canvas->viewPitch = canvas->destPitch;
			}
		}
	}
	canvas->lastFrameTime = app->time;
	app->render->render((canvas->viewX << 4) + 8, (canvas->viewY << 4) + 8, (canvas->viewZ << 4) + 8, canvas->viewAngle, 0, 0, 290);
	app->combat->drawWeapon(0, 0);
	canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_VIEW3D);
}

void GameStateRunner::playingState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (canvas->pushedWall && canvas->pushedTime <= app->gameTime) {
		app->combat->shiftWeapon(false);
		canvas->pushedWall = false;
	}
	if (app->player->ce->getStat(0) <= 0) {
		app->player->died();
		return;
	}
	if (app->player->isFamiliar && app->player->ammo[7] <= 0) {
		app->player->ammo[7] = 0;
		app->player->familiarDying(false);
	}
	if (app->hud->isShiftingCenterMsg()) {
		canvas->staleView = true;
	}
	if (canvas->knockbackDist == 0 && app->game->activePropogators == 0 && app->game->animatingEffects == 0 && app->game->monstersTurn != 0 && canvas->dialogSystem.numHelpMessages == 0) {
		app->game->updateMonsters();
	}
	app->game->updateLerpSprites();
	canvas->updateView();
	if (canvas->state == Canvas::ST_LOADING || canvas->state == Canvas::ST_SAVING) {
		return;
	}
	if (canvas->state != Canvas::ST_PLAYING && canvas->state != Canvas::ST_INTER_CAMERA) {
		return;
	}
	canvas->repaintFlags |= Canvas::REPAINT_PARTICLES;
	if (!app->game->isCameraActive() || canvas->state == Canvas::ST_INTER_CAMERA) {
		canvas->repaintFlags |= Canvas::REPAINT_HUD;
		app->hud->repaintFlags |= 0x2B; // J2ME 0x6B
		app->hud->update();
	}
	if (canvas->state == Canvas::ST_INTER_CAMERA || (!app->game->isCameraActive() && canvas->state == Canvas::ST_PLAYING) || canvas->state == Canvas::ST_AUTOMAP) {
		canvas->dequeueHelpDialog();
	}
}

void GameStateRunner::menuState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	short n = -1;
	int menu = app->menuSystem->menu;
	if ((app->menuSystem->items[app->menuSystem->selectedIndex].flags & 0x20)) {
		n = 49;
	}
	else {
		if (menu == Menus::MENU_END_RANKING || menu == Menus::MENU_LEVEL_STATS) {
			n = 43;
		}
		else if (app->menuSystem->type != 5) {
			if (menu != Menus::MENU_SHOWDETAILS) {
				if (menu == Menus::MENU_VENDING_MACHINE_DETAILS || menu == Menus::MENU_VENDING_MACHINE_CONFIRM) {
					n = 202;
				}
				else if (menu != Menus::MENU_VENDING_MACHINE_CANT_BUY) {
					if (app->menuSystem->items[app->menuSystem->selectedIndex].action != 0) {
						n = 121;
					}
				}
			}
		}
	}

	if (menu != Menus::MENU_MAIN_MORE_GAMES) {
		canvas->clearSoftKeys();
		if (app->menuSystem->getStackCount() != 0 || menu == Menus::MENU_MAIN_MINIGAME) {
			if (app->menuSystem->peekMenu() != 25) {
				canvas->setLeftSoftKey((short)3, (short)80);
			}
		}
		else if (menu == Menus::MENU_INGAME || menu == Menus::MENU_INGAME_KICKING || menu == Menus::MENU_INGAME_SNIPER) {
			canvas->setLeftSoftKey((short)0, (short)30);
		}
		else if (menu == Menus::MENU_VENDING_MACHINE) {
			canvas->setLeftSoftKey((short)3, (short)80);
		}
		if (n != -1) {
			if (!app->menuSystem->changeValues) { // Old changeSfxVolume
				canvas->setRightSoftKey((short)0, n);
			}
		}
		else if (app->menuSystem->menu == Menus::MENU_SHOWDETAILS) {
			canvas->setRightSoftKey((short)0, (short)40);
		}
	}

	canvas->repaintFlags |= Canvas::REPAINT_MENU;
}

void GameStateRunner::dyingState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	app->hud->repaintFlags = 32;
	if (app->time < canvas->deathTime + 750) {
		int n = (750 - (app->time - canvas->deathTime) << 16) / 750;
		canvas->viewZ = app->render->getHeight(canvas->destX, canvas->destY) + 18 + (20 * n >> 16);
		canvas->viewPitch = 96 + (-96 * n >> 16);
		int n2 = 16 + (-16 * n >> 16);
		canvas->updateView();
		canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, n2, 290);
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
	}
	else if (app->time < canvas->deathTime + 2750) {
		if (!app->render->isFading()) {
			app->render->startFade(2000, 1);
		}
		canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, 16, 290);
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
	}
	else {
		app->render->baseDizzy = (app->render->destDizzy = 0);
		app->menuSystem->setMenu(Menus::MENU_INGAME_DEAD);
	}
}

void GameStateRunner::familiarDyingState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	app->hud->repaintFlags = 36;
	// app->hud->repaintFlags |= 0x40; // J2ME
	if (canvas->familiarSelfDestructed) {
		canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, canvas->viewRoll, 290);
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
		if (app->time >= canvas->familiarDeathTime + 1500) {
			app->sound->playSound(1032, 0, 3, 0);
			canvas->setState(Canvas::ST_PLAYING);
			app->player->familiarDied();
		}
		else if (!canvas->selfDestructScreenShakeStarted && app->time >= canvas->familiarDeathTime + 750) {
			canvas->selfDestructScreenShakeStarted = true;
			canvas->startShake(canvas->familiarDeathTime + 1500 - app->time, 5, 500);
		}
	}
	else if (app->time < canvas->familiarDeathTime + 750) {
		int n = (750 - (app->time - canvas->familiarDeathTime) << 16) / 750;
		canvas->viewZ = app->render->getHeight(canvas->destX, canvas->destY) + 18 + (20 * n >> 16);
		canvas->viewPitch = 96 + (-96 * n >> 16);
		int n2 = 16 + (-16 * n >> 16);
		canvas->updateView();
		canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, n2, 290);
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
	}
	else if (app->time < canvas->familiarDeathTime + 1500) {
		if (!app->render->isFading()) {
			app->render->startFade(750, 1);
		}
		canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, 16, 290);
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
	}
	else {
		canvas->setState(Canvas::ST_PLAYING);
		app->player->familiarDied();
	}
}

void GameStateRunner::logoState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (!app->sound->soundsLoaded) {
		app->sound->cacheSounds();
	}

	if (canvas->pacLogoTime <= 120) {
		canvas->pacLogoTime++;
		if (canvas->imgStartupLogo == nullptr) {
			canvas->imgStartupLogo = app->loadImage("l2.bmp", true);
		}
		canvas->repaintFlags |= Canvas::REPAINT_STARTUP_LOGO;
	}
	else {
		delete canvas->imgStartupLogo;
		canvas->imgStartupLogo = nullptr;

		canvas->setState(Canvas::ST_INTRO_MOVIE);
		canvas->numEvents = 0;
		canvas->keyDown = false;
		canvas->keyDownCausedMove = false;
		canvas->ignoreFrameInput = 1;
	}
}
