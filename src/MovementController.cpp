#include <cstdlib>

#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "MayaCamera.h"
#include "Game.h"
#include "Render.h"
#include "Player.h"
#include "Combat.h"
#include "Hud.h"
#include "TinyGL.h"
#include "ScriptThread.h"
#include "Enums.h"
#include "Entity.h"
#include "MovementController.h"

MovementController::MovementController() {
}

void MovementController::setAnimFrames(int animFrames) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;
	canvas->animFrames = animFrames;
	canvas->animPos = (64 + canvas->animFrames - 1) / canvas->animFrames;
	canvas->animAngle = (256 + canvas->animFrames - 1) / canvas->animFrames;
}

void MovementController::checkFacingEntity() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (!canvas->updateFacingEntity) {
		return;
	}
	int destX = canvas->destX;
	int destY = canvas->destY;
	int destZ = canvas->destZ;
	int n = 21741;
	int *view = app->tinyGL->view;
	app->game->trace(destX + (-view[2] * 28 >> 14), destY + (-view[6] * 28 >> 14), destZ + (-view[10] * 28 >> 14), destX + (6 * -view[2] >> 8), destY + (6 * -view[6] >> 8), destZ + (6 * -view[10] >> 8), nullptr, n, 2, canvas->isZoomedIn);
	Entity* traceEntity = app->game->traceEntity;
	if (traceEntity != nullptr && (traceEntity->def->eType == 6 || traceEntity->def->eType == 11 || traceEntity->def->eType == 12 || traceEntity->def->eType == 10 || traceEntity->def->eType == 14)) {
		int i = 0;
		while (i < app->game->numTraceEntities) {
			Entity* entity = app->game->traceEntities[i];
			short linkIndex = entity->linkIndex;
			if (entity->def->eType == 2) {
				if (traceEntity->def->eType != 12) {
					traceEntity = entity;
					break;
				}
				break;
			}
			else {
				if (entity->def->eType == 5 || entity->def->eType == 4) {
					break;
				}
				if (entity->def->eType == 0) {
					break;
				}
				if (entity->def->eType == 12 && (app->render->mapFlags[linkIndex] & 0x2) != 0x0) {
					break;
				}
				if (entity->def->eType == 7) {
					if (traceEntity->def->eType == 12) {
						traceEntity = entity;
						break;
					}
					break;
				}
				else {
					if (entity->def->eType == 14) {
						if (traceEntity->def->eSubType != 6) {
							traceEntity = entity;
							break;
						}
					}
					else if (entity->def->eType == 10 && (entity->def->eSubType == 1 || entity->def->eSubType == 2 || entity->def->eSubType == 3) && traceEntity != nullptr && traceEntity->def->eType != 6) {
						traceEntity = entity;
						break;
					}
					++i;
				}
			}
		}
	}
	app->player->facingEntity = traceEntity;
	if (app->player->facingEntity != nullptr) {
		Entity* facingEntity = app->player->facingEntity;
		int dist = facingEntity->distFrom(canvas->viewX, canvas->viewY);
		if (facingEntity->def->eType != 2 && dist > app->combat->tileDistances[2]) {
			app->player->facingEntity = nullptr;
		}
		else if (facingEntity->def->eType == 3 && dist <= app->combat->tileDistances[0]) {
			app->player->showHelp((short)0, false);
		}
		else if (dist <= app->combat->tileDistances[0]) {
			if (facingEntity->def->eType == 10) {
				if (facingEntity->def->eSubType == 1) {
					app->player->showHelp((short)2, false);
				}
				else if (facingEntity->def->eSubType == 2) {
					app->player->showHelp((short)3, false);
				}
				else if (facingEntity->def->eSubType == 3) {
					app->player->showHelp((short)9, false);
				}
			}
			else if (facingEntity->def->eType == 5) {
				if (facingEntity->def->eSubType == 1) {
					app->player->showHelp((short)1, false);
				}
				app->player->showHelp((short)7, false);
			}
			else if (facingEntity->def->eType == 6 && facingEntity->def->eSubType == 3) {
				app->player->showHelp((short)4, false);
			}
			else if (facingEntity->def->tileIndex == 158) {
				app->player->showHelp((short)18, false);
			}
		}
	}
	Entity* traceEntity2 = app->game->traceEntity;
	int n2 = 4141;
	if (traceEntity2 != nullptr && (1 << traceEntity2->def->eType & n2) == 0x0) {
		for (int j = 1; j < app->game->numTraceEntities; ++j) {
			Entity* entity2 = app->game->traceEntities[j];
			if ((1 << entity2->def->eType & n2) != 0x0) {
				traceEntity2 = entity2;
				break;
			}
		}
	}
	if (traceEntity2 != nullptr) {
		int dist2 = traceEntity2->distFrom(canvas->viewX, canvas->viewY);
		if (dist2 <= app->combat->tileDistances[0] && traceEntity2->def->eType == 0 && app->combat->weaponDown) {
			app->combat->shiftWeapon(true);
		}
		else if ((canvas->state == Canvas::ST_PLAYING || canvas->state == Canvas::ST_DIALOG) && ((0x2 & 1 << app->player->ce->weapon) == 0x0 || dist2 <= app->combat->tileDistances[0])) {
			if (traceEntity2->def->eType == 3) {
				app->combat->shiftWeapon(true);
			}
			else if (app->combat->weaponDown) {
				app->combat->shiftWeapon(false);
			}
		}
		else if (canvas->state == Canvas::ST_DIALOG && canvas->oldState != Canvas::ST_INTER_CAMERA) {
			app->combat->shiftWeapon(true);
		}
		else {
			app->combat->shiftWeapon(false);
		}
	}
	else {
		app->combat->shiftWeapon(false);
	}
	canvas->updateFacingEntity = false;
}

void MovementController::finishMovement() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (canvas->gotoThread != nullptr && canvas->viewAngle == canvas->destAngle) {
		canvas->gotoThread->run();
		canvas->gotoThread = nullptr;
	}
	app->game->executeTile(canvas->destX >> 6, canvas->destY >> 6, this->flagForFacingDir(8), true);
	app->game->executeTile(canvas->destX >> 6, canvas->destY >> 6, app->game->eventFlags[1], true);
	app->game->touchTile(canvas->destX, canvas->destY, true);
	if (canvas->knockbackDist > 0) {
		--canvas->knockbackDist;
		canvas->destZ += 12;
		if (canvas->knockbackDist == 0) {
			canvas->destZ = 36 + app->render->getHeight(canvas->destX, canvas->destY);
		}
	}
	else if (canvas->gotoThread == nullptr && canvas->state == Canvas::ST_PLAYING && app->game->monstersTurn == 0) {
		//if (canvas->state != Canvas::ST_AUTOMAP) {
			canvas->updateFacingEntity = true;
		//}
		canvas->uncoverAutomap();
		app->game->advanceTurn();
	}
	else if (canvas->state == Canvas::ST_AUTOMAP) {
		canvas->uncoverAutomap();
		app->game->advanceTurn();
		if (app->game->animatingEffects != 0) {
			canvas->setState(Canvas::ST_PLAYING);
		}
		else {
			app->game->snapMonsters(true);
			app->game->snapLerpSprites(-1);
		}
	}
}

int MovementController::flagForWeapon(int i) {
	Applet* app = CAppContainer::getInstance()->app;
	bool weaponIsASentryBot = app->player->weaponIsASentryBot(i);
	i = 1 << i;
	if (weaponIsASentryBot && (!app->player->isFamiliar || (app->player->familiarType != 1 && app->player->familiarType != 3))) {
		return 0;
	}
	if ((i & 0x2) != 0x0) {
		return 4096;
	}
	if ((i & 0x800) != 0x0) {
		return 16384;
	}
	return 8192;
}

int MovementController::flagForFacingDir(int i) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;
	int destAngle = canvas->destAngle;
	if (i == 4) {
		destAngle += 512;
	}
	if (i == 8 || i == 4) {
		return i | 1 << ((destAngle & 0x3FF) >> 7) + 4;
	}
	return 0;
}

void MovementController::startRotation(bool b) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	int8_t b2 = Canvas::viewStepValues[((canvas->destAngle & 0x3FF) >> 7 << 1) + 0];
	int8_t b3 = Canvas::viewStepValues[((canvas->destAngle & 0x3FF) >> 7 << 1) + 1];
	int n = 384;
	app->game->trace(canvas->destX, canvas->destY, canvas->destX + (b2 * n >> 6), canvas->destY + (b3 * n >> 6), nullptr, 4133, 2);
	Entity* traceEntity = app->game->traceEntity;
	int n2 = app->game->traceFracs[0] * n >> 14;
	int n3;
	int n4;
	if (traceEntity != nullptr && (traceEntity->def->eType == 0 || traceEntity->def->eType == 12) && n2 <= 36) {
		n3 = canvas->destZ;
		n4 = (b ? 0 : 1);
	}
	else {
		bool b4 = !this->pitchIsControlled(canvas->destX >> 6, canvas->destY >> 6, app->game->VecToDir(b2 * 32, b3 * 32, false));
		if (traceEntity != nullptr && traceEntity->def->eType == 2) {
			if (b4) {
				int* calcPosition = traceEntity->calcPosition();
				n3 = app->render->getHeight(calcPosition[0], calcPosition[1]) + 36;
			}
			else {
				n3 = canvas->destZ;
			}
			n4 = 1;
		}
		else {
			n2 = 64;
			if (b4) {
				n3 = app->render->getHeight(canvas->destX + b2, canvas->destY + b3) + 36;
			}
			else {
				n3 = canvas->destZ;
			}
			n4 = (b ? 0 : 1);
		}
		n4 = 1;
	}
	if (n4 == 0) {
		return;
	}
	if (n2 == 0) {
		canvas->destPitch = 0;
	}
	else {
		canvas->destPitch = ((n3 - canvas->destZ) << 7) / n2;
	}
	if (canvas->destPitch < -64) {
		canvas->destPitch = -64;
	}
	else if (canvas->destPitch > 64) {
		canvas->destPitch = 64;
	}
	canvas->pitchStep = std::abs((canvas->destPitch - canvas->viewPitch) / canvas->animFrames);
}

void MovementController::finishRotation(bool b) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	canvas->viewSin = app->render->sinTable[canvas->destAngle & 0x3FF];
	canvas->viewCos = app->render->sinTable[canvas->destAngle + 256 & 0x3FF];
	canvas->viewStepX = Canvas::viewStepValues[(((canvas->destAngle & 0x3FF) >> 7) << 1) + 0];
	canvas->viewStepY = Canvas::viewStepValues[(((canvas->destAngle & 0x3FF) >> 7) << 1) + 1];
	int n = canvas->destAngle - 256 & 0x3FF;
	canvas->viewRightStepX = Canvas::viewStepValues[((n >> 7) << 1) + 0];
	canvas->viewRightStepY = Canvas::viewStepValues[((n >> 7) << 1) + 1];
	if (b && app->hud->msgCount > 0 && (app->hud->messageFlags[0] & 0x2) != 0x0) {
		app->hud->msgTime = 0;
	}
	if (canvas->gotoThread != nullptr && canvas->viewX == canvas->destX && canvas->viewY == canvas->destY) {
		ScriptThread* gotoThread = canvas->gotoThread;
		canvas->gotoThread = nullptr;
		gotoThread->run();
	}
	if (canvas->state == Canvas::ST_COMBAT) {
		canvas->updateFacingEntity = true;
	}
	else {
		app->game->executeTile(canvas->destX >> 6, canvas->destY >> 6, this->flagForFacingDir(8), true);
		canvas->updateFacingEntity = true;
	}
}

bool MovementController::attemptMove(int n, int n2) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (canvas->renderOnly) {
		canvas->destX = n;
		canvas->destY = n2;
		return true;
	}

	if (app->player->isFamiliar && (app->render->mapFlags[(n2 >> 6) * 32 + (n >> 6)] & 0x10) != 0x0 && (n != canvas->saveX || n2 != canvas->saveY)) {
		app->hud->addMessage((short)0, (short)222, 3);
		return false;
	}
	int n3 = app->player->noclip ? 0 : 13501;
	app->game->eventFlagsForMovement(canvas->viewX, canvas->viewY, n, n2);
	canvas->abortMove = false;
	app->game->executeTile(canvas->viewX >> 6, canvas->viewY >> 6, app->game->eventFlags[0], true);
	bool b = false;
	if (!canvas->abortMove) {
		app->game->trace(canvas->viewX, canvas->viewY, n, n2, app->player->getPlayerEnt(), n3, 16);
		if (app->game->traceEntity == nullptr || (app->player->isFamiliar && n == canvas->saveX && n2 == canvas->saveY && app->game->traceEntity == &app->game->entities[app->player->playerEntityCopyIndex])) {
			if (app->player->isFamiliar && n == canvas->saveX && n2 == canvas->saveY && app->game->traceEntity == &app->game->entities[app->player->playerEntityCopyIndex]) {
				app->player->familiarReturnsToPlayer(true);
			}
			if (app->hud->msgCount > 0 && (app->hud->messageFlags[0] & 0x2) != 0x0) {
				app->hud->msgTime = 0;
			}
			canvas->automapDrawn = false;
			canvas->destX = n;
			canvas->destY = n2;
			canvas->destZ = 36 + app->render->getHeight(canvas->destX, canvas->destY);
			canvas->zStep = (std::abs(canvas->destZ - canvas->viewZ) + canvas->animFrames - 1) / canvas->animFrames;
			canvas->prevX = canvas->viewX;
			canvas->prevY = canvas->viewY;
			this->startRotation(false);
			app->player->relink();
			b = true;
		}
		else if (canvas->knockbackDist == 0 && canvas->state == Canvas::ST_AUTOMAP) {
			app->game->advanceTurn();
		}
	}
	else if (canvas->knockbackDist != 0) {
		canvas->knockbackDist = 0;
	}
	return b;
}

bool MovementController::pitchIsControlled(int n, int n2, int n3) {
	Applet* app = CAppContainer::getInstance()->app;

	bool b = false;
	for (int i = 0; i < Render::MAX_KEEP_PITCH_LEVEL_TILES; ++i) {
		if (app->render->mapKeepPitchLevelTiles[i] != -1) {
			short n4 = app->render->mapKeepPitchLevelTiles[i];
			if ((n2 << 5) + n == (n4 & 0x3FF) && n3 == (n4 & 0xFFFFFC00) >> 10) {
				b = true;
				break;
			}
		}
	}
	return b;
}

void MovementController::updateView() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (app->time < canvas->shakeTime) {
		canvas->shakeX = app->nextByte() % (canvas->shakeIntensity * 2) - canvas->shakeIntensity;
		canvas->shakeY = app->nextByte() % (canvas->shakeIntensity * 2) - canvas->shakeIntensity;
		canvas->staleView = true;
	}
	else if (canvas->shakeX != 0 || canvas->shakeY != 0) {
		canvas->staleView = true;
		canvas->shakeX = (canvas->shakeY = 0);
	}

	if (app->game->isCameraActive() && canvas->state != Canvas::ST_INTER_CAMERA) {
		app->game->activeCamera->Render();
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
		app->hud->repaintFlags &= 0x18;
		return;
	}

	if (canvas->knockbackDist > 0 && canvas->viewX == canvas->destX && canvas->viewY == canvas->destY) {
		this->attemptMove(canvas->viewX + canvas->knockbackX * 64, canvas->viewY + canvas->knockbackY * 64);
	}

	bool b = canvas->viewX == canvas->destX && canvas->viewY == canvas->destY;
	bool b2 = canvas->viewAngle == canvas->destAngle;
	int animPos = canvas->animPos;
	int animAngle = canvas->animAngle;

	if (app->player->statusEffects[2] > 0 || canvas->knockbackDist > 0) {
		animPos += animPos / 2;
		animAngle += animAngle / 2;
	}

	if (canvas->viewX != canvas->destX || canvas->viewY != canvas->destY || canvas->viewZ != canvas->destZ || canvas->viewAngle != canvas->destAngle) {
		canvas->invalidateRect();
	}

	if (canvas->viewX < canvas->destX) {
		canvas->viewX += animPos;
		if (canvas->viewX > canvas->destX) {
			canvas->viewX = canvas->destX;
		}
	}
	else if (canvas->viewX > canvas->destX) {
		canvas->viewX -= animPos;
		if (canvas->viewX < canvas->destX) {
			canvas->viewX = canvas->destX;
		}
	}

	if (canvas->viewY < canvas->destY) {
		canvas->viewY += animPos;
		if (canvas->viewY > canvas->destY) {
			canvas->viewY = canvas->destY;
		}
	}
	else if (canvas->viewY > canvas->destY) {
		canvas->viewY -= animPos;
		if (canvas->viewY < canvas->destY) {
			canvas->viewY = canvas->destY;
		}
	}

	if (canvas->viewZ < canvas->destZ) {
		canvas->viewZ += canvas->zStep;
		if (canvas->viewZ > canvas->destZ) {
			canvas->viewZ = canvas->destZ;
		}
	}
	else if (canvas->viewZ > canvas->destZ) {
		canvas->viewZ -= canvas->zStep;
		if (canvas->viewZ < canvas->destZ) {
			canvas->viewZ = canvas->destZ;
		}
	}

	if (canvas->viewAngle < canvas->destAngle) {
		canvas->viewAngle += animAngle;
		if (canvas->viewAngle > canvas->destAngle) {
			canvas->viewAngle = canvas->destAngle;
		}
	}
	else if (canvas->viewAngle > canvas->destAngle) {
		canvas->viewAngle -= animAngle;
		if (canvas->viewAngle < canvas->destAngle) {
			canvas->viewAngle = canvas->destAngle;
		}
	}

	if (canvas->viewPitch < canvas->destPitch) {
		canvas->viewPitch += canvas->pitchStep;
		if (canvas->viewPitch > canvas->destPitch) {
			canvas->updateFacingEntity = true;
			canvas->viewPitch = canvas->destPitch;
		}
	}
	else if (canvas->viewPitch > canvas->destPitch) {
		canvas->viewPitch -= canvas->pitchStep;
		if (canvas->viewPitch < canvas->destPitch) {
			canvas->updateFacingEntity = true;
			canvas->viewPitch = canvas->destPitch;
		}
	}

	int viewZ = canvas->viewZ;
	if (canvas->knockbackDist != 0) {
		int n = canvas->viewX;
		if (canvas->knockbackX == 0) {
			n = canvas->viewY;
		}
		viewZ = canvas->viewZ + (10 * app->render->sinTable[(std::abs(n - canvas->knockbackStart) << 9) / canvas->knockbackWorldDist & 0x3FF] >> 16);
	}

	if (canvas->state == Canvas::ST_AUTOMAP) {
		canvas->viewX = canvas->destX;
		canvas->viewY = canvas->destY;
		canvas->viewZ = canvas->destZ;
		canvas->viewAngle = canvas->destAngle;
		canvas->viewPitch = canvas->destPitch;
	}

	if (canvas->state == Canvas::ST_COMBAT) {
		app->game->gsprite_update(app->time);
	}
	if (app->game->gotoTriggered) {
		app->game->gotoTriggered = false;
		int flagForFacingDir = this->flagForFacingDir(8);
		app->game->eventFlagsForMovement(-1, -1, -1, -1);
		app->game->executeTile(canvas->destX >> 6, canvas->destY >> 6, app->game->eventFlags[1], true);
		app->game->executeTile(canvas->destX >> 6, canvas->destY >> 6, flagForFacingDir, true);
	}
	else if (!b && canvas->viewX == canvas->destX && canvas->viewY == canvas->destY) {
		this->finishMovement();
	}

	if (!b2 && canvas->viewAngle == canvas->destAngle) {
		this->finishRotation(true);
	}

	if (app->game->isCameraActive() && canvas->state != Canvas::ST_INTER_CAMERA) {
		app->game->activeCamera->Update(app->game->activeCameraKey, app->gameTime - app->game->activeCameraTime);
		app->game->activeCamera->Render();
		canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
		return;
	}

	if (canvas->isZoomedIn) {
		int n2 = canvas->zoomAccuracy * app->render->sinTable[(app->time - canvas->zoomStateTime) / 2 & 0x3FF] >> 24;
		int n3 = canvas->zoomAccuracy * app->render->sinTable[(app->time - canvas->zoomStateTime) / 3 & 0x3FF] >> 24;
		int zoomFOV = canvas->zoomFOV;
		int n4;
		if (app->time < canvas->zoomTime) {
			n4 = canvas->zoomDestFOV + (canvas->zoomFOV - canvas->zoomDestFOV) * (canvas->zoomTime - app->time) / 360;
		}
		else {
			canvas->zoomTime = 0;
			n4 = (canvas->zoomFOV = canvas->zoomDestFOV);
		}
		int n5 = canvas->zoomPitch + canvas->viewPitch;
		if (app->combat->curAttacker == nullptr && canvas->state == Canvas::ST_COMBAT && app->combat->nextStage == 1) {
			n5 += app->render->sinTable[512 * ((app->gameTime - app->combat->animStartTime << 16) / app->combat->animTime) >> 16 & 0x3FF] * 28 >> 16;
		}
		canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle + canvas->zoomAngle + n2, n5 + n3, canvas->viewRoll, n4);
		canvas->updateFacingEntity = true;
	}
	else if (canvas->loadMapID != 0) {
		canvas->renderScene(canvas->viewX, canvas->viewY, viewZ, canvas->viewAngle, canvas->viewPitch, canvas->viewRoll, 290);
	}
}
