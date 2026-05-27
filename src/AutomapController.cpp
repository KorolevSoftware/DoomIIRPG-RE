#include "AutomapController.h"

#include <cstdint>

#include "App.h"
#include "Button.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Entity.h"
#include "EntityDef.h"
#include "Enums.h"
#include "Game.h"
#include "Graphics.h"
#include "Player.h"
#include "Render.h"
#include "Text.h"

void AutomapController::automapState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	app->game->updateLerpSprites();
	if (!canvas->automapDrawn && app->game->animatingEffects == 0) {
		canvas->updateView();
		canvas->repaintFlags &= ~Canvas::REPAINT_VIEW3D;
		if (canvas->state != Canvas::ST_AUTOMAP) {
			canvas->updateView();
		}
	}
	if (canvas->state == Canvas::ST_AUTOMAP || canvas->state == Canvas::ST_PLAYING) {
		canvas->drawPlayingSoftKeys();
	}
}

void AutomapController::uncoverAutomap() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (!app->game->updateAutomap) {
		return;
	}
	int n = canvas->destX >> 6;
	int n2 = canvas->destY >> 6;
	if (n < 0 || n >= 32 || n2 < 0 || n2 >= 32) {
		return;
	}
	for (int i = n2 - 1; i <= n2 + 1; ++i) {
		if (i >= 0) {
			if (i < 31) {
				for (int j = n - 1; j <= n + 1; ++j) {
					if (j >= 0) {
						if (j < 31) {
							uint8_t b = app->render->mapFlags[i * 32 + j];
							if ((j == n && i == n2) || (b & 0x2) == 0x0) {
								app->render->mapFlags[i * 32 + j] |= (uint8_t)128;
							}
						}
					}
				}
			}
		}
	}
}

void AutomapController::drawAutomap(Graphics* graphics, bool fullRefresh) {
	(void)fullRefresh;

	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	graphics->drawRegion(canvas->imgGameHelpBG, 0, 0, 480, 320, 0, 0, 0, 0, 0);

	int n3 = 8;
	int n6 = 0x400000 / (n3 << 8);
	int n4 = 112;
	int n5 = 32;

	int n7 = 0;
	for (int i = 0; i < 32; ++i) {
		for (int j = 0; j < 32; ++j) {
			uint8_t b2 = app->render->mapFlags[n7 + j];
			if ((b2 & 0x8) == 0x0) {
				if (app->render->mapEntranceAutomap == (i * 32) + j) {
					graphics->setColor(0xFF00FF00);
					graphics->fillRect(n4 + (n3 * j) + canvas->screenRect[0], (n5 - 1) + (n3 * i) + canvas->screenRect[1], n3, n3);
				}
				else if ((b2 & 0x80) != 0x0) {
					if (app->render->mapExitAutomap == (i * 32) + j) {
						graphics->setColor(0xFFFF0000);
						graphics->fillRect(n4 + (n3 * j) + canvas->screenRect[0], (n5 - 1) + (n3 * i) + canvas->screenRect[1], n3, n3);
					}
					else if ((b2 & 0x1) == 0x0) {
						graphics->setColor(0xFF90B9E7);
						graphics->fillRect(n4 + (n3 * j) + canvas->screenRect[0], (n5 - 1) + (n3 * i) + canvas->screenRect[1] + 1, n3, n3);
						graphics->setColor(0xFF2A3657);
					}
				}
			}
		}
		n7 += 32;
	}

	for (int k = 0; k < Render::MAX_LADDERS_PER_MAP; ++k) {
		if (app->render->mapLadders[k] >= 0 && app->render->mapLadders[k] < 1024 &&
			(app->render->mapFlags[app->render->mapLadders[k]] & 0x80) != 0x0 &&
			(app->render->mapFlags[app->render->mapLadders[k]] & 0x8) == 0x0) {
			int n8 = app->render->mapLadders[k] % 32;
			int n9 = app->render->mapLadders[k] / 32;
			graphics->setColor(0xFFFFFF00);
			graphics->fillRect(n4 + (n3 * n8), n5 + (n3 * n9), n3, n3);
			graphics->setColor(0xFF000000);
			for (int l = 0; l < 8; l += 2) {
				graphics->drawLine(n4 + n3 * n8, n5 + n3 * n9 + l, n4 + n3 * n8 + n3, n5 + n3 * n9 + l);
			}
			graphics->drawLine(n4 + n3 * n8, n5 + n3 * n9, n4 + n3 * n8, n5 + n3 * n9 + n3);
			graphics->drawLine(n4 + n3 * n8 + n3, n5 + n3 * n9, n4 + n3 * n8 + n3, n5 + n3 * n9 + n3);
		}
	}

	graphics->setColor(0xFF2A3657);
	for (int n10 = 0; n10 < app->render->numLines; ++n10) {
		int n11 = app->render->lineFlags[n10 >> 1] >> ((n10 & 0x1) << 2) & 0xF;
		if ((n11 & 0x8) != 0x0) {
			int n12 = n11 & 0x7;
			if (n12 == 6 || n12 == 0) {
				graphics->drawLine(n4 + app->render->lineXs[n10 << 1], n5 + app->render->lineYs[n10 << 1], n4 + app->render->lineXs[(n10 << 1) + 1], n5 + app->render->lineYs[(n10 << 1) + 1]);
			}
		}
	}

	int n13 = 0;
	for (int n14 = 0; n14 < 32; ++n14) {
		for (int n15 = 0; n15 < 32; ++n15) {
			uint8_t b3 = app->render->mapFlags[n13 + n15];
			for (Entity* nextOnTile = app->game->entityDb[n13 + n15]; nextOnTile != nullptr; nextOnTile = nextOnTile->nextOnTile) {
				if (nextOnTile != &app->game->entities[1]) {
					if (nextOnTile != &app->game->entities[0]) {
						if ((b3 & 0x8) == 0x0) {
							int sprite = nextOnTile->getSprite();
							int n16 = app->render->mapSpriteInfo[sprite];
							int n17 = (n16 & 0xFF00) >> 8;
							if (0x0 != (n16 & 0x200000)) {
								if (0x0 == (n16 & 0x10000)) {
									int color = 0;
									bool b4 = false;
									bool b5 = false;
									int n18 = 2;
									if (nextOnTile->def->eType == 5) {
										short tileIndex = nextOnTile->def->tileIndex;
										if (tileIndex == 271 || tileIndex == 272) {
											color = 0xFFFF8400;
										}
										else if (tileIndex == 273 || tileIndex == 274) {
											color = 0xFF00C0FF;
										}
										else {
											color = 0xFF3D68E3;
										}
										b5 = true;
									}
									else if ((n16 & 0x400000) != 0x0) {
										color = 0xFF2A3657;
										b5 = true;
									}
									else if (nextOnTile->def->eType == 13) {
										color = 0xFF8D8068;
										b5 = true;
									}
									else if (nextOnTile->def->eType == 3) {
										b4 = true;
										color = 0xFF0000FF;
										n18 = 128;
									}
									else if (nextOnTile->def->eType == 10 && n17 == 0) {
										color = 0xFF8000FF;
									}
									else if (nextOnTile->def->eType == 2) {
										color = 0xFFFF8000;
										n18 = 128;
									}
									else if (nextOnTile->def->eType == 7) {
										color = 0xFF8D8068;
										if (nextOnTile->def->tileIndex == 173 || nextOnTile->def->tileIndex == 180) {
											color = 0;
										}
									}
									else if (app->player->god && nextOnTile->def->eType == 6 && nextOnTile->def->eSubType != 3) {
										color = 0xFF00FFEA;
									}
									if (color != 0 && ((b3 & n18) != 0x0 || (n18 & 0x80) == 0x0)) {
										graphics->setColor(color);
										if ((n16 & 0xF000000) != 0x0) {
											int n20;
											int n19 = n20 = app->render->mapSprites[app->render->S_X + sprite];
											int n22;
											int n21 = n22 = app->render->mapSprites[app->render->S_Y + sprite];
											if ((n16 & 0x3000000) != 0x0) {
												n20 -= 32;
												n19 += 32;
											}
											else {
												n22 -= 32;
												n21 += 32;
											}
											int n23 = (((n20 << 16) / n6) + 1 + 128) >> 8;
											int n24 = (((n19 << 16) / n6) + 128) >> 8;
											int n25 = (((n22 << 16) / n6) + 128) >> 8;
											int n26 = (((n21 << 16) / n6) + 128) >> 8;
											if (b5) {
												graphics->drawLine(n4 + n23, n5 + n25, n4 + n24, n5 + n26);
											}
											else {
												graphics->fillRect(n4 + ((n23 + n24) >> 1) - (n3 / 4), n5 + ((n25 + n26) >> 1) - (n3 / 4), n3 / 2, n3 / 2);
											}
										}
										else if (b4) {
											graphics->fillRect(n4 + (n3 * n15) + (n3 / 4), n5 + (n3 * n14) + (n3 / 4), (n3 / 2) + 2, (n3 / 2) + 2);
										}
										else {
											graphics->fillRect(n4 + (n3 * n15) + (n3 / 4) + 1, n5 + (n3 * n14) + (n3 / 4) + 1, (n3 / 2), (n3 / 2));
										}
									}
								}
							}
						}
					}
				}
			}
		}
		n13 += 32;
	}

#if 0
	int n27 = 0;
	for (int n28 = 0; n28 < 32; ++n28) {
		for (int n29 = 0; n29 < 32; ++n29) {
			if ((app->render->mapFlags[n27 + n29] & 0x8) != 0x0) {
				graphics->setColor(0xFF526F8B);
				graphics->fillRect(n4 + 8 * n29 + canvas->screenRect[0], n5 + 8 * n28 + canvas->screenRect[1] + 1, 8, 8);
			}
		}
		n27 += 32;
	}
#endif

	for (int n30 = 0; n30 < app->player->numNotebookIndexes; ++n30) {
		if (!app->player->isQuestDone(n30)) {
			if (!app->player->isQuestFailed(n30)) {
				int n31 = app->player->notebookPositions[n30] >> 5 & 0x1F;
				int n32 = app->player->notebookPositions[n30] & 0x1F;
				if (n31 + n32 != 0 && (0x80 & app->render->mapFlags[n32 * 32 + n31]) != 0x0) {
					graphics->setColor(((app->time / 1024 & 0x1) == 0x0) ? 0xFFFF0000 : 0xFF00FF00);
					Entity* mapEntity = app->game->findMapEntity(n31 << 6, n32 << 6, 32);
					if (nullptr != mapEntity) {
						int sprite2 = mapEntity->getSprite();
						int n33 = app->render->mapSpriteInfo[sprite2];
						int n35;
						int n34 = n35 = app->render->mapSprites[app->render->S_X + sprite2];
						int n37;
						int n36 = n37 = app->render->mapSprites[app->render->S_Y + sprite2];
						if ((n33 & 0x3000000) != 0x0) {
							n35 -= 32;
							n34 += 32;
						}
						else {
							n37 -= 32;
							n36 += 32;
						}
						graphics->drawLine(n4 + ((((n35 << 16) / n6) + 1 + 128) >> 8), n5 + ((((n37 << 16) / n6) + 128) >> 8), n4 + ((((n34 << 16) / n6) + 128) >> 8), n5 + ((((n36 << 16) / n6) + 128) >> 8));
					}
					else {
						graphics->fillRect(n4 + n3 * n31 + n3 / 4, n5 + n3 * n32 + n3 / 4, n3 / 2 + 2, n3 / 2 + 2);
					}
				}
			}
		}
	}

	// Test Only
#if 0
	for (int node = 0; node < app->render->numNodes; node++) {
		int x1 = n4 + (app->render->nodeBounds[(node << 2) + 0] & 0xFF) << 3;
		int y1 = n5 + (app->render->nodeBounds[(node << 2) + 1] & 0xFF) << 3;
		int x2 = n4 + (app->render->nodeBounds[(node << 2) + 2] & 0xFF) << 3;
		int y2 = n5 + (app->render->nodeBounds[(node << 2) + 3] & 0xFF) << 3;

		graphics->setColor(0xff00ff00);
		graphics->drawRect( (x1 / 8), (y1 / 8), ((x2 - x1 + 1) / 8), ((y2 - y1 + 1) / 8));
	}
#endif

	if (app->time > canvas->automapBlinkTime) {
		canvas->automapBlinkTime = app->time + 333;
		canvas->automapBlinkState = !canvas->automapBlinkState;
	}

	int n38 = 0;
	switch (canvas->destAngle & 0x3FF) {
	case Enums::ANGLE_NORTH: {
		n38 = 0;
		break;
	}
	case Enums::ANGLE_SOUTH: {
		n38 = 1;
		break;
	}
	case Enums::ANGLE_EAST: {
		n38 = 2;
		break;
	}
	case Enums::ANGLE_WEST: {
		n38 = 3;
		break;
	}
	}
	int n39 = n38 * 4;
	if (canvas->automapBlinkState) {
		n39 += 16;
	}
	int n40 = n4 + ((n3 * (canvas->viewX - 32)) / 64) + (n3 / 2);
	int n41 = n5 + ((n3 * (canvas->viewY - 32)) / 64) + (n3 / 2);
	if (n41 < canvas->screenRect[1] + canvas->screenRect[3]) {
		graphics->drawRegion(canvas->imgMapCursor, 0, n39, 4, 4, n40, n41, 3, 0, 0);
	}

	Text* largeBuffer = app->localization->getLargeBuffer();

	largeBuffer->setLength(0);
	app->localization->composeText(canvas->softKeyLeftID, largeBuffer);
	largeBuffer->dehyphenate();
	app->setFontRenderMode(2);
	if (canvas->m_softKeyButtons->GetButton(19)->highlighted) {
		app->setFontRenderMode(0);
	}
	graphics->drawString(largeBuffer, 15, 310, 36); // old 20, 316, 36
	app->setFontRenderMode(0);

	largeBuffer->setLength(0);
	app->localization->composeText(canvas->softKeyRightID, largeBuffer);
	largeBuffer->dehyphenate();
	app->setFontRenderMode(2);
	if (canvas->m_softKeyButtons->GetButton(20)->highlighted) {
		app->setFontRenderMode(0);
	}
	graphics->drawString(largeBuffer, 465, 310, 40);// old 470, 316, 40
	app->setFontRenderMode(0);
	largeBuffer->dispose();
}
