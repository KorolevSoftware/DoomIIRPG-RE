#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Game.h"
#include "Render.h"
#include "Player.h"
#include "Combat.h"
#include "Hud.h"
#include "TinyGL.h"
#include "Entity.h"
#include "Enums.h"
#include "MenuSystem.h"
#include "Menus.h"
#include "Sound.h"
#include "PlayingInputHandler.h"

PlayingInputHandler::PlayingInputHandler() {}

bool PlayingInputHandler::handlePlayingEvents(int key, int action) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	//printf("handlePlayingEvents %d, %d\n", key, action);

	bool b = false;
	if (!canvas->isZoomedIn && (canvas->viewX != canvas->destX || canvas->viewY != canvas->destY || canvas->viewAngle != canvas->destAngle)) {
		return false;
	}

	if (canvas->knockbackDist != 0 || canvas->changeMapStarted) {
		return false;
	}

	if (canvas->renderOnly) {
		canvas->viewX = canvas->destX;
		canvas->viewY = canvas->destY;
		canvas->viewZ = canvas->destZ;
		canvas->viewAngle = canvas->destAngle;
		canvas->viewAngle = canvas->destPitch;
		canvas->viewSin = app->render->sinTable[canvas->destAngle & 0x3FF];
		canvas->viewCos = app->render->sinTable[canvas->destAngle + 256 & 0x3FF];
		canvas->viewStepX = canvas->viewCos * 64 >> 16;
		canvas->viewStepY = -canvas->viewSin * 64 >> 16;
		canvas->invalidateRect();
	}
	else {
		if (!canvas->isZoomedIn) {
			if (canvas->viewX != canvas->destX || canvas->viewY != canvas->destY) {
				b = true;
				canvas->viewX = canvas->destX;
				canvas->viewY = canvas->destY;
				canvas->viewZ = canvas->destZ;
				canvas->finishMovement();
				canvas->invalidateRect();
			}
			else if (canvas->viewAngle != canvas->destAngle) {
				b = true;
				canvas->viewAngle = canvas->destAngle;
				canvas->viewAngle = canvas->destPitch;
				canvas->finishRotation(true);
				canvas->invalidateRect();
			}
		}
		if (canvas->blockInputTime != 0) {
			return true;
		}

		if (app->hud->isInWeaponSelect != 0) { // [GEC]
			return true;
		}

		bool b2 = canvas->state == Canvas::ST_AUTOMAP;
		if (action != Enums::ACTION_PREVWEAPON && action != Enums::ACTION_NEXTWEAPON && action != Enums::ACTION_LEFT && action != Enums::ACTION_RIGHT && (app->game->activePropogators != 0 || !app->game->snapMonsters(b2) || app->game->animatingEffects != 0)) {
			return true;
		}
	}

	if (action == Enums::ACTION_BOT_DISCARD) { // [GEC]
		if (app->player->isFamiliar && canvas->state != Canvas::ST_AUTOMAP) {
			app->player->familiarReturnsToPlayer(false);
		}
		else if (app->player->weaponIsASentryBot(app->player->ce->weapon) && canvas->state != Canvas::ST_AUTOMAP) {
			app->player->attemptToDiscardFamiliar(app->player->ce->weapon);
		}
	}

	if (action == Enums::ACTION_AUTOMAP) {
		if (!app->player->inTargetPractice) {
			// OLD -> Original
			/*if (app->player->isFamiliar && canvas->state != Canvas::ST_AUTOMAP) {
				app->player->familiarReturnsToPlayer(false);
			}
			else if (app->player->weaponIsASentryBot(app->player->ce->weapon) && canvas->state != Canvas::ST_AUTOMAP) {
				app->player->attemptToDiscardFamiliar(app->player->ce->weapon);
			}
			else */{
				canvas->setState((canvas->state != Canvas::ST_AUTOMAP) ? Canvas::ST_AUTOMAP : Canvas::ST_PLAYING);
			}
		}
	}
	else if (action == Enums::ACTION_UP) {
		canvas->attemptMove(canvas->viewX + canvas->viewStepX, canvas->viewY + canvas->viewStepY);
	}
	else if (action == Enums::ACTION_DOWN) {
		canvas->attemptMove(canvas->viewX - canvas->viewStepX, canvas->viewY - canvas->viewStepY);
	}
	else if (action == Enums::ACTION_STRAFELEFT) {
		canvas->attemptMove(canvas->viewX + canvas->viewStepY, canvas->viewY - canvas->viewStepX);
	}
	else if (action == Enums::ACTION_STRAFERIGHT) {
		canvas->attemptMove(canvas->viewX - canvas->viewStepY, canvas->viewY + canvas->viewStepX);
	}
	else if (action == Enums::ACTION_LEFT || action == Enums::ACTION_RIGHT) {
		int n3 = 256;
		app->hud->damageTime = 0;
		if (action == Enums::ACTION_RIGHT) {
			n3 = -256;
		}
		canvas->destAngle += n3;
		canvas->startRotation(false);
		canvas->automapDrawn = false;
	}
	else if (action == Enums::ACTION_PREVWEAPON || action == Enums::ACTION_NEXTWEAPON) {
		int weapon = app->player->ce->weapon;
		if (weapon == 14) {
			return true;
		}
		if (action == Enums::ACTION_PREVWEAPON) {
			app->player->selectPrevWeapon();
		}
		else {
			app->player->selectNextWeapon();
		}
		if (weapon != app->player->ce->weapon) {
			app->hud->addMessage((short)1, (short)app->player->activeWeaponDef->longName, 1);
		}
		app->player->helpBitmask |= 0x100;
	}
	else if (action == Enums::ACTION_BACK) {
		if (canvas->state == Canvas::ST_AUTOMAP) {
			canvas->setState(Canvas::ST_PLAYING);
		}
		else if (app->player->inTargetPractice) {
			app->player->exitTargetPractice();
		}
		else {
			app->hud->msgCount = 0;
			app->menuSystem->setMenu(Menus::MENU_INGAME);
		}
	}
	else if (action == Enums::ACTION_MENU) {
		if (app->player->inTargetPractice) {
			app->player->exitTargetPractice();
		}
		else {
			app->hud->msgCount = 0;
			app->menuSystem->setMenu(Menus::MENU_INGAME);
		}
	}
	else if (action == Enums::ACTION_ITEMS) {
		if (app->player->inTargetPractice) {
			app->player->exitTargetPractice();
		}
		else {
			app->hud->msgCount = 0;
			app->menuSystem->setMenu(Menus::MENU_ITEMS);
			app->menuSystem->oldMenu = Menus::MENU_ITEMS; // [GEC]
		}
	}
	else if (action == Enums::ACTION_ITEMS_DRINKS) {
		if (app->player->inTargetPractice) {
			app->player->exitTargetPractice();
		}
		else {
			app->hud->msgCount = 0;
			app->menuSystem->setMenu(Menus::MENU_ITEMS_DRINKS);
			app->menuSystem->oldMenu = Menus::MENU_ITEMS_DRINKS; // [GEC]
		}
	}
	else if (action == Enums::ACTION_QUESTLOG) {
		if (app->player->inTargetPractice) {
			app->player->exitTargetPractice();
		}
		else {
			app->hud->msgCount = 0;
			app->menuSystem->setMenu(Menus::MENU_INGAME_QUESTLOG);
			app->menuSystem->oldMenu = Menus::MENU_INGAME_QUESTLOG; // [GEC]
		}
	}
	else if (action == Enums::ACTION_FIRE) {
		if (app->player->facingEntity != nullptr && app->player->facingEntity->def->eType == 10) {
			canvas->lootingSystem.lootSource = app->player->facingEntity->name;
		}
		else {
			canvas->lootingSystem.lootSource = -1;
		}

		int weapon2 = app->player->ce->weapon;

		int n4 = 16384;
		int n5 = 13997;

		if (Entity::CheckWeaponMask(weapon2, 2) != 0x0) {
			//n5 |= 0x2000; // J2ME only?
		}
		if (weapon2 == 2) {
			n5 |= 0x4100;
		}
		int n6 = 0;
		int n7 = 6;
		if (Entity::CheckWeaponMask(weapon2, 2) != 0x0) {
			n7 = 1;
			n5 |= 0x10;
		}

		Entity* entity = nullptr;
		Entity* entity2 = nullptr;

		int n8 = canvas->viewX + (n7 * -app->tinyGL->view[2] >> 8);
		int n9 = canvas->viewY + (n7 * -app->tinyGL->view[6] >> 8);
		int n10 = canvas->viewZ + (n7 * -app->tinyGL->view[10] >> 8);
		app->game->trace(canvas->viewX, canvas->viewY, canvas->viewZ, n8, n9, n10, nullptr, n5, 2, canvas->isZoomedIn);

		int i = 0;
		while (i < app->game->numTraceEntities) {
			Entity* entity3 = app->game->traceEntities[i];
			int n11 = app->game->traceFracs[i];
			int dist = entity3->distFrom(canvas->viewX, canvas->viewY);
			uint8_t eType = entity3->def->eType;
			if (eType == 0 || eType == 12 || eType == 4) {
				if (entity == nullptr) {
					entity = entity3;
					n4 = n11;
					break;
				}
				break;
			}
			else {
				if (eType == 10) {
					if ((1 << entity3->def->eSubType & 0x1) == 0x0 || app->player->ce->weapon == 1) {
						if (n6 == 0) {
							entity = entity3;
							n4 = n11;
							break;
						}
						break;
					}
				}
				else if (eType == 13) {
					if (Entity::CheckWeaponMask(weapon2, 2) != 0x0) {
						entity2 = entity3;
					}
				}
				else if (eType == 3) {
					if (dist >= 8192) {
						if (n6 == 0) {
							entity = entity3;
							n4 = n11;
							break;
						}
						break;
					}
				}
				else {
					if (eType == 2) {
						entity = entity3;
						n4 = n11;
						n6 = 0;
						break;
					}
					if (eType == 5) {
						if (entity == nullptr) {
							entity = entity3;
							n4 = n11;
							n6 = 0;
							break;
						}
						break;
					}
					else if (eType == 9) {
						if (dist == app->combat->tileDistances[0]) {
							if (weapon2 == 1) {
#if 0 // J2ME
								if (entity != nullptr && entity->def->eType == 9) {
									if (entity->linkIndex < entity3->linkIndex) {
										if (entity3->monster == nullptr && entity3->def->eSubType != 11) {
											if (entity3->param == 0 && entity3->lootSet != nullptr) {
												entity = entity3;
												n4 = n11;
												n6 = 1;
											}
											else {
												app->hud->addMessage((short)0, (short)250, 2);
											}
										}
										else {
											entity = entity3;
											n4 = n11;
										}
									}
								}
								else if (entity3->monster == nullptr && entity3->def->eSubType != 11) {
									if (entity3->param == 0 && entity3->lootSet != nullptr) {
										entity = entity3;
										n4 = n11;
										n6 = 1;
									}
									else {
										app->hud->addMessage((short)0, (short)250, 2);
									}
								}
								else {
									entity = entity3;
									n4 = n11;
								}
#else
								if (((entity == nullptr) || (entity->def->eType != 9))
									|| (entity->linkIndex < entity3->linkIndex)) {
									entity = entity3;
									n4 = n11;
								}
#endif
							}
							else if (!canvas->isZoomedIn) {
								if (entity3->monster == nullptr) {
									if (entity3->param == 0 && entity3->lootSet != nullptr) {
										entity = entity3;
										n4 = n11;
										n6 = 1;
									}
								}
								else if ((entity3->monster->flags & 0x800) == 0x0 && entity3->lootSet != nullptr) {
									entity = entity3;
									n4 = n11;
									n6 = 1;
								}
							}
						}
					}
					else if (eType == 8) {
						if (entity3->def->eSubType == 1 && weapon2 == 2 && app->player->ammo[3] >= 2) {
							entity = entity3;
							n4 = n11;
							n6 = 0;
							break;
						}
					}
					else if (eType == 7) {
						if ((app->render->mapSpriteInfo[entity3->getSprite()] & 0xFF) == 0x95) {
							entity = entity3;
							n4 = n11;
							break;
						}
					}
					else if (eType == 14) {
						if (entity3->def->eSubType == 7 && dist == app->combat->tileDistances[0]) {
							entity = entity3;
							n4 = n11;
							break;
						}
					}
					else if (eType != 7 && eType != 6 && entity == nullptr) {
						entity = entity3;
						n4 = n11;
					}
				}
				++i;
			}
		}

		int dist2 = app->combat->tileDistances[9];
		if (entity != nullptr) {
			dist2 = entity->distFrom(canvas->viewX, canvas->viewY);
		}
		if (n6 != 0) {
			canvas->setState(Canvas::ST_LOOTING);
			canvas->poolLoot(entity->calcPosition());
			return true;
		}
		int n12 = weapon2 * 9;
		if (entity != nullptr && entity->def->eType == 10 && (1 << entity->def->eSubType & 0x1) == 0x0 && app->combat->WorldDistToTileDist(dist2) > app->combat->weapons[n12 + 3]) {
			entity = nullptr;
		}
		if (entity2 != nullptr && (entity == nullptr || (1 << weapon2 & 0x0) != 0x0 || (entity->def->eType != 2 && entity->def->eType != 9))) {
			entity = entity2;
		}

		if (entity != nullptr && entity->def->eType == 10 && entity->def->eSubType == 2) {
			if (dist2 <= app->combat->tileDistances[0]) {
				entity->param = app->upTimeMs + 200;
				app->game->unlinkEntity(entity);
			}
			else if (weapon2 == 11) {}
		}

		int flagForFacingDir = canvas->flagForFacingDir(4);
		int n13 = canvas->destX + canvas->viewStepX >> 6;
		int n14 = canvas->destY + canvas->viewStepY >> 6;
		if (app->game->executeTile(n13, n14, flagForFacingDir, true)) {
			if (!app->game->skipAdvanceTurn && canvas->state == Canvas::ST_PLAYING) {
				app->game->touchTile(canvas->destX, canvas->destY, false);
				app->game->snapMonsters(true);
				app->game->advanceTurn();
			}
		}
		else if (entity != nullptr && entity->def->eType == 10 && entity->def->eSubType == 3 && dist2 <= app->combat->tileDistances[0] && app->player->ammo[8] == 0) {
			if (!app->player->isFamiliar) {
				if (app->player->ce->weapon == 2 && app->player->ammo[3] < 100) {
					app->hud->addMessage((short)248);
					app->player->ammo[3] = 100;
					app->player->showHelp((short)14, false);
					app->sound->playSound(1046, 0, 3, 0);
				}
				else if (app->player->ce->getStat(4) < 11) {
					app->hud->addMessage((short)238, 3);
				}
				else {
					app->player->currentWeaponCopy = app->player->ce->weapon;
					app->player->setPickUpWeapon(entity->def->tileIndex);
					app->player->give(2, 8, 1, true);
					app->player->giveAmmoWeapon(14, true);
					canvas->turnEntityIntoWaterSpout(entity);
					int* calcPosition = entity->calcPosition();
					if (this->shouldFakeCombat(calcPosition[0] >> 6, calcPosition[1] >> 6, flagForFacingDir) && app->combat->explodeThread != nullptr) {
						app->combat->explodeThread->run();
						app->combat->explodeThread = nullptr;
					}
					app->sound->playSound(1134, 0, 3, 0);
				}
				return true;
			}
		}
		else {
			if (entity != nullptr && entity->def->eType == 14 && entity->def->eSubType == 7 && dist2 <= app->combat->tileDistances[0] && dist2 > 0 && app->player->ce->weapon == 2) {
				if (app->player->ammo[3] < 100) {
					app->hud->addMessage((short)248);
					app->player->showHelp((short)14, false);
					app->sound->playSound(1046, 0, 3, 0);
				}
				else {
					app->hud->addMessage((short)249);
				}
				app->player->ammo[3] = 100;
				return true;
			}
			if (entity != nullptr && entity->def->eType == 5 && dist2 <= app->combat->tileDistances[0] && weapon2 != 14) {
				if (!app->player->isFamiliar) {
					if (entity->def->eSubType == 1) {
						app->hud->addMessage((short)44, 2);
					}
					else {
						app->game->performDoorEvent(0, entity, 1);
						app->game->advanceTurn();
					}
				}
				else {
					app->sound->playSound(1111, 0, 3, 0);
					app->hud->addMessage((short)193, 2);
				}
				return true;
			}
			if (entity != nullptr && entity->def->eType == 12 && dist2 <= app->combat->tileDistances[0] && (app->render->mapFlags[n14 * 32 + n13] & 0x4) != 0x0) {
				if (app->game->performDoorEvent(0, entity, 1, true)) {
					app->game->awardSecret(true);
				}
				return true;
			}
			if (entity != nullptr && (entity->def->eType == 0 || entity->def->eType == 12) && dist2 <= app->combat->tileDistances[0]) {
				if (canvas->isZoomedIn) {
					return true;
				}

				if (app->player->isFamiliar) {
					app->sound->playSound(1111, 0, 3, 0);
				}
				else if (app->player->characterChoice == 1) {
					app->sound->playSound(1073, 0, 3, 0);
				}
				else if (app->player->characterChoice >= 1 && app->player->characterChoice <= 3) {
					app->sound->playSound(1072, 0, 3, 0);
				}

				app->combat->shiftWeapon(true);
				canvas->pushedWall = true;
				canvas->pushedTime = app->gameTime + 500;
				app->render->rockView(1000, canvas->viewX + (canvas->viewStepX >> 6) * 2, canvas->viewY + (canvas->viewStepY >> 6) * 2, canvas->viewZ);
				return true;
			}
			else {
				if (app->player->isFamiliar && (app->player->familiarType == 2 || app->player->familiarType == 4)) {
					app->player->startSelfDestructDialog();
					return true;
				}
				if (!app->player->isFamiliar && app->player->weaponIsASentryBot(weapon2)) {
					app->player->attemptToDeploySentryBot();
					return true;
				}
				if (entity != nullptr && entity->def->eType == Enums::ET_ENV_DAMAGE) {
					this->shouldFakeCombat(app->game->traceCollisionX >> 6, app->game->traceCollisionY >> 6, flagForFacingDir);
					app->player->fireWeapon(entity, app->game->traceCollisionX, app->game->traceCollisionY);
					app->game->removeEntity(entity);
					app->player->addXP(5);
				}
				else {
					if (!canvas->isZoomedIn && Entity::CheckWeaponMask(weapon2, 512) != 0x0 && app->player->ammo[app->combat->weapons[n12 + 4]] > 0) {
						canvas->initZoom();
						return true;
					}
					if (entity != nullptr && entity->def->eType != 0) {
						int* calcPosition2 = entity->calcPosition();
						bool shouldFakeCombat = this->shouldFakeCombat(calcPosition2[0] >> 6, calcPosition2[1] >> 6, flagForFacingDir);
						int n15 = weapon2 * 9;
						if (shouldFakeCombat || (app->combat->weapons[n15 + 3] == 1 && app->combat->weapons[n15 + 2] == 1 && dist2 > app->combat->tileDistances[0])) {
							app->game->traceCollisionX = calcPosition2[0];
							app->game->traceCollisionY = calcPosition2[1];
							app->player->fireWeapon(&app->game->entities[0], calcPosition2[0], calcPosition2[1]);
						}
						else {
							if (canvas->isZoomedIn) {
								canvas->zoomCollisionX = canvas->viewX + (n4 * (n8 - canvas->viewX) >> 14);
								canvas->zoomCollisionY = canvas->viewY + (n4 * (n9 - canvas->viewY) >> 14);
								canvas->zoomCollisionZ = canvas->viewZ + (n4 * (n10 - canvas->viewZ) >> 14);
							}
							app->player->fireWeapon(entity, calcPosition2[0], calcPosition2[1]);
							if (app->player->inTargetPractice) {
								if (app->player->ammo[1] == 0) {
									app->player->exitTargetPractice();
								}
								else {
									app->player->assessTargetPracticeShot(entity);
								}
							}
						}
					}
					else {
						this->shouldFakeCombat(app->game->traceCollisionX >> 6, app->game->traceCollisionY >> 6, flagForFacingDir);
						app->player->fireWeapon(&app->game->entities[0], app->game->traceCollisionX, app->game->traceCollisionY);
						if (app->player->inTargetPractice) {
							if (app->player->ammo[1] == 0) {
								app->player->exitTargetPractice();
							}
							else {
								app->hud->addMessage((short)68);
							}
						}
					}
				}
			}
		}
	}
	else if (action == Enums::ACTION_PASSTURN) {
		app->hud->addMessage((short)45);
		app->game->touchTile(canvas->destX, canvas->destY, false);
		app->game->advanceTurn();
		canvas->invalidateRect();
	}

	return this->endOfHandlePlayingEvent(action, b);
}

bool PlayingInputHandler::handleCinematicInput(int action) { // J2ME
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	if (action == Enums::ACTION_FIRE) {
		app->game->executeTile(canvas->destX >> 6, canvas->destY >> 6, app->game->eventFlags[1], true);
	}
	return false;
}

bool PlayingInputHandler::shouldFakeCombat(int n, int n2, int n3) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	bool b = false;
	int n4 = app->player->ce->weapon * 9;
	if (app->combat->weapons[n4 + 4] != 0) {
		short n5 = app->player->ammo[app->combat->weapons[n4 + 4]];
		if (app->combat->weapons[n4 + 5] > 0 && n5 - app->combat->weapons[n4 + 5] < 0) {
			return false;
		}
	}
	n3 |= canvas->flagForWeapon(app->player->ce->weapon);
	if (app->game->doesScriptExist(n, n2, n3)) {
		(app->combat->explodeThread = app->game->allocScriptThread())->queueTile(n, n2, n3);
		b = true;
	}
	return b;
}

bool PlayingInputHandler::endOfHandlePlayingEvent(int action, bool b) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	if ((action == Enums::ACTION_STRAFELEFT || action == Enums::ACTION_STRAFERIGHT || action == Enums::ACTION_LEFT || action == Enums::ACTION_RIGHT) && (canvas->viewX != canvas->destX || canvas->viewY != canvas->destY || canvas->viewAngle != canvas->destAngle)) {
		app->player->facingEntity = nullptr;
	}
	return true;
}
