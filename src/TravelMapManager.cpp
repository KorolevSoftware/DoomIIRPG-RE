#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "TravelMapManager.h"
#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Graphics.h"
#include "Text.h"
#include "Image.h"
#include "Enums.h"
#include "Game.h"
#include "Render.h"
#include "TinyGL.h"

TravelMapManager::TravelMapManager() {
	std::memset(this, 0, sizeof(TravelMapManager));
}

TravelMapManager::~TravelMapManager() {
}

void TravelMapManager::init() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	this->TM_LoadLevelId = (short)std::max(1, std::min(canvas->loadMapID, 9));
	this->TM_LastLevelId = ((canvas->getRecentLoadType() == 1 && !this->TM_NewGame) ? this->TM_LoadLevelId : ((short)std::max(0, std::min(canvas->lastMapID, 9))));

	if (this->TM_LastLevelId == 0 && this->TM_LoadLevelId > 1) {
		this->TM_LastLevelId = (short)(this->TM_LoadLevelId - 1);
	}
	short n;
	if (this->TM_LoadLevelId > this->TM_LastLevelId) {
		n = 0;
	}
	else if (this->TM_LoadLevelId == this->TM_LastLevelId) {
		n = 1;
	}
	else {
		n = 2;
	}
	app->game->scriptStateVars[15] = n;

	app->beginImageLoading();
	this->imgNameHighlight = app->loadImage("highlight.bmp", true);
	this->imgMagGlass = app->loadImage("magnifyingGlass.bmp", true);
	this->imgSpaceShip = app->loadImage("spaceShip.bmp", true);

	bool b = false;
	if (onMoon(this->TM_LoadLevelId)) {
		this->imgTierCloseUp = app->loadImage("TM_Levels1.bmp", true);
	}
	else if (onEarth(this->TM_LoadLevelId)) {
		if (onMoon(this->TM_LastLevelId) || inHell(this->TM_LastLevelId)) {
			this->imgEarthCloseUp = app->loadImage("TM_Levels2.bmp", true);
			b = true;
		}
		this->imgTierCloseUp = app->loadImage("TM_Levels4.bmp", true);
	}
	else {
		this->imgTierCloseUp = app->loadImage("TM_Levels3.bmp", true);
	}

	if (onMoon(this->TM_LoadLevelId) && this->TM_LastLevelId == 0) {
		this->imgTravelPath = app->loadImage("toMoon.bmp", true);
	}
	else if ((onEarth(this->TM_LoadLevelId) && onMoon(this->TM_LastLevelId)) || (onMoon(this->TM_LoadLevelId) && onEarth(this->TM_LastLevelId))) {
		this->imgTravelPath = app->loadImage("toEarth.bmp", true);
	}
	else if ((inHell(this->TM_LoadLevelId) && onEarth(this->TM_LastLevelId)) || (onEarth(this->TM_LoadLevelId) && inHell(this->TM_LastLevelId))) {
		this->imgTravelPath = app->loadImage("toHell.bmp", true);
	}

	this->imgTravelBG = app->loadImage("TravelMap.bmp", true);
	this->imgMapHorzGridLines = app->loadImage("travelMapHorzGrid.bmp", true);
	this->imgMapVertGridLines = app->loadImage("travelMapVertGrid.bmp", true);
	app->endImageLoading();

	this->totalTMTimeInPastAnimations = 0;
	this->mapWidth = this->imgTravelBG->width;
	this->mapHeight = this->imgTravelBG->height;
	this->xDiff = std::max(0, (canvas->displayRect[2] - this->imgTravelBG->width) / 2);
	this->yDiff = std::max(0, (canvas->displayRect[3] - this->imgTravelBG->height) / 2);

	int n2;
	int n3;
	if (onMoon(this->TM_LoadLevelId)) {
		n2 = 137;
		n3 = 216;
	}
	else if (onEarth(this->TM_LoadLevelId)) {
		n2 = 123;
		n3 = 150;
	}
	else {
		n2 = 150;
		n3 = 11;
	}

	if (!b) {
		short n4 = (short)(2 * (this->TM_LoadLevelId - 1));
		this->targetX = Canvas::CROSS_HAIR_CORDS[n4] + this->xDiff + n2;
		this->targetY = Canvas::CROSS_HAIR_CORDS[n4 + 1] + this->yDiff + n3;
	}
	else {
		this->targetX = Canvas::UAC_BUILDING_LOCATION_ON_EARTH[0] + this->xDiff + n2;
		this->targetY = Canvas::UAC_BUILDING_LOCATION_ON_EARTH[1] + this->yDiff + n3;
	}

	canvas->stateVars[0] = app->upTimeMs;
	if (b) {
		canvas->stateVars[8] = 1;
	}

	this->imgStarField = app->loadImage("cockpit.bmp", true);
	this->_field_0xf24 = 480;
	this->_field_0xf20 = this->imgStarField->height;
	this->_field_0xf28 = 0;
	this->_field_0xf2c = 1;
}

void TravelMapManager::dispose() {
	delete this->imgNameHighlight;
	this->imgNameHighlight = nullptr;
	delete this->imgMagGlass;
	this->imgMagGlass = nullptr;
	delete this->imgTravelBG;
	this->imgTravelBG = nullptr;
	delete this->imgTravelPath;
	this->imgTravelPath = nullptr;
	delete this->imgSpaceShip;
	this->imgSpaceShip = nullptr;
	delete this->imgTierCloseUp;
	this->imgTierCloseUp = nullptr;
	delete this->imgEarthCloseUp;
	this->imgEarthCloseUp = nullptr;
	delete this->imgMapHorzGridLines;
	this->imgMapHorzGridLines = nullptr;
	delete this->imgMapVertGridLines;
	this->imgMapVertGridLines = nullptr;
	delete this->imgStarField;
	this->imgStarField = nullptr;
}

void TravelMapManager::drawTravelMap(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (canvas->stateVars[5] == 1) {
		this->drawStarFieldPage(graphics);
		canvas->staleView = true;
		return;
	}

	graphics->drawImage(this->imgTravelBG, canvas->SCR_CX, (canvas->displayRect[3] - this->imgTravelBG->height) / 2, 17, 0, 0);

	if (this->xDiff > 1 || this->yDiff > 1) {
		graphics->fillRegion(canvas->imgFabricBG, 0, 0, canvas->displayRect[2], this->yDiff);
		graphics->fillRegion(canvas->imgFabricBG, 0, this->yDiff, this->xDiff, canvas->displayRect[3] - this->yDiff);
		graphics->fillRegion(canvas->imgFabricBG, this->xDiff, this->yDiff + this->mapHeight, this->mapWidth, this->yDiff);
		graphics->fillRegion(canvas->imgFabricBG, this->xDiff + this->mapWidth, this->yDiff, this->xDiff, canvas->displayRect[3] - this->yDiff);
		graphics->drawRect(this->xDiff + -1, this->yDiff + -1, this->mapWidth + 1, this->mapHeight + 1, 0xFF000000);
		graphics->clipRect(this->xDiff, this->yDiff, this->imgTravelBG->width, this->imgTravelBG->height);
	}

	int time = app->upTimeMs - canvas->stateVars[0];
	this->drawGridLines(graphics, time + this->totalTMTimeInPastAnimations);

	bool levelSamePlanet = this->newLevelSamePlanet();
	if (canvas->stateVars[1] != 1) {
		if (time > (levelSamePlanet ? 1500 : 700)) {
			this->totalTMTimeInPastAnimations += time;
			canvas->stateVars[0] += time;
			canvas->stateVars[1] = 1;
			time = 0;
			if (levelSamePlanet) {
				canvas->stateVars[2] = 1;
				canvas->stateVars[3] = 1;
			}
		}
		else if (levelSamePlanet) {
			this->drawAppropriateCloseup(graphics, this->TM_LastLevelId, false);
			Text* smallBuffer = app->localization->getSmallBuffer();
			smallBuffer->setLength(0);
			app->localization->composeText((short)3, app->game->levelNames[this->TM_LastLevelId - 1], smallBuffer);
			smallBuffer->dehyphenate();
			this->drawLocatorBoxAndName(graphics, (time & 0x200) == 0x0, this->TM_LastLevelId, smallBuffer);
			smallBuffer->dispose();
		}
	}

	if (canvas->stateVars[1] == 1) {
		if (canvas->stateVars[2] != 1) {
			if (this->drawDottedLine(graphics, time)) {
				this->totalTMTimeInPastAnimations += time;
				canvas->stateVars[0] += time;
				canvas->stateVars[2] = 1;
			}
		}
		else if (canvas->stateVars[2] == 1) {
			if (canvas->stateVars[3] != 1) {
				this->drawDottedLine(graphics);
				if (time > 500 || levelSamePlanet) {
					this->totalTMTimeInPastAnimations += time;
					canvas->stateVars[0] += time;
					canvas->stateVars[3] = 1;
				}
			}
			else if (canvas->stateVars[3] == 1) {
				this->drawAppropriateCloseup(graphics, this->TM_LoadLevelId, canvas->stateVars[8] == 1);
				if (canvas->stateVars[6] == 1) {
					Text* smallBuffer2 = app->localization->getSmallBuffer();
					smallBuffer2->setLength(0);
					app->localization->composeText((short)3, app->game->levelNames[this->TM_LoadLevelId - 1], smallBuffer2);
					smallBuffer2->dehyphenate();
					drawLocatorBoxAndName(graphics, (time & 0x200) == 0x0, this->TM_LoadLevelId, smallBuffer2);
					smallBuffer2->setLength(0);
					app->localization->composeText((short)0, (short)96, smallBuffer2);
					smallBuffer2->wrapText(24);
					smallBuffer2->dehyphenate();
					graphics->drawString(smallBuffer2, canvas->SCR_CX, canvas->displayRect[3] - 21, 17);
					smallBuffer2->dispose();
				}
				else if (canvas->stateVars[4] != 1) {
					if (this->drawLocatorLines(graphics, time, levelSamePlanet, canvas->stateVars[8] == 1)) {
						this->totalTMTimeInPastAnimations += time;
						canvas->stateVars[0] += time;
						canvas->stateVars[4] = 1;
					}
				}
				else {
					this->drawLocatorLines(graphics, -1, levelSamePlanet, canvas->stateVars[8] == 1);
					if (time > 800) {
						this->totalTMTimeInPastAnimations += time;
						canvas->stateVars[0] += time;
						if (canvas->stateVars[8] == 0) {
							canvas->stateVars[6] = 1;
						}
						else {
							canvas->stateVars[4] = 0;
							canvas->stateVars[8] = 0;
							int n7 = 123;
							int n8 = 150;
							short n9 = (short)(2 * (this->TM_LoadLevelId - 1));
							this->targetX = Canvas::CROSS_HAIR_CORDS[n9] + this->xDiff + n7;
							this->targetY = Canvas::CROSS_HAIR_CORDS[n9 + 1] + this->yDiff + n8;
						}
					}
				}
			}
		}
	}

	canvas->staleView = true;
}

bool TravelMapManager::newLevelSamePlanet() {
	return this->TM_LastLevelId != this->TM_LoadLevelId &&
		((onMoon(this->TM_LastLevelId) && onMoon(this->TM_LoadLevelId)) ||
		 (onEarth(this->TM_LastLevelId) && onEarth(this->TM_LoadLevelId)) ||
		 (inHell(this->TM_LastLevelId) && inHell(this->TM_LoadLevelId)));
}

void TravelMapManager::drawAppropriateCloseup(Graphics* graphics, int n, bool b) {
	if (this->onMoon(n)) {
		graphics->drawImage(this->imgTierCloseUp, Canvas::moonCoords[0], Canvas::moonCoords[1], 0, 0, 0);
	}
	else if (this->onEarth(n)) {
		if (b) {
			graphics->drawImage(this->imgEarthCloseUp, Canvas::earthCoords[0], Canvas::earthCoords[1], 0, 0, 0);
		}
		else {
			graphics->drawImage(this->imgTierCloseUp, Canvas::earthCoords[2], Canvas::earthCoords[3], 0, 0, 0);
		}
	}
	else {
		graphics->drawImage(this->imgTierCloseUp, Canvas::hellCoords[0], Canvas::hellCoords[1], 0, 0, 0);
	}
}

bool TravelMapManager::drawDottedLine(Graphics* graphics, int n) {
	int n2 = n / 22;
	bool b;
	if (this->TM_LastLevelId == 0 && onMoon(this->TM_LoadLevelId)) {
		b = this->drawMarsToMoonLinePlusSpaceShip(graphics, n2);
	}
	else if (this->onMoon(this->TM_LastLevelId) && this->onEarth(this->TM_LoadLevelId)) {
		b = this->drawMoonToEarthLine(graphics, n2, false);
	}
	else if (this->onEarth(this->TM_LastLevelId) && this->onMoon(this->TM_LoadLevelId)) {
		b = this->drawMoonToEarthLine(graphics, n2, true);
	}
	else if (this->onEarth(this->TM_LastLevelId) && inHell(this->TM_LoadLevelId)) {
		b = this->drawEarthToHellLine(graphics, n2, false);
	}
	else {
		b = (!inHell(this->TM_LastLevelId) || !onEarth(this->TM_LoadLevelId) || this->drawEarthToHellLine(graphics, n2, true));
	}
	return b;
}

bool TravelMapManager::drawDottedLine(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	return drawDottedLine(graphics, 22 * std::max(canvas->screenRect[2], canvas->screenRect[3]));
}

bool TravelMapManager::drawMarsToMoonLinePlusSpaceShip(Graphics* graphics, int n) {
	Applet* app = CAppContainer::getInstance()->app;

	bool b = false;
	int width = this->imgTravelPath->width;
	int n2 = n;
	if (n2 > width) {
		b = true;
		n2 = width;
	}
	if (n > width / 3) {
		Text* smallBuffer = app->localization->getSmallBuffer();
		smallBuffer->setLength(0);
		app->localization->composeText((short)3, (short)165, smallBuffer);
		smallBuffer->dehyphenate();
		graphics->drawString(smallBuffer, Canvas::moonNameCoords[0], Canvas::moonNameCoords[1], 4);
		smallBuffer->dispose();
	}
	graphics->drawRegion(this->imgTravelPath, width - n2, 0, n2, this->imgTravelPath->height, Canvas::moonPathCoords[0] + width - n2, Canvas::moonPathCoords[1], 0, 0, 0);
	int n3 = width - n2;
	graphics->drawImage(this->imgSpaceShip, Canvas::moonPathCoords[0] + n3 - this->imgSpaceShip->width, this->yCoordOfSpaceShip(n3) + Canvas::moonPathCoords[1] - (this->imgSpaceShip->height >> 1), 0, 0, 0);
	return b;
}

int TravelMapManager::yCoordOfSpaceShip(int n) {
	int iVar1;
	iVar1 = (n * 11) / 15;
	return ((-851 * (iVar1 * iVar1) / 110880 + 6115 * iVar1 / 22176 + 40) * 15) / 11;
}

bool TravelMapManager::drawMoonToEarthLine(Graphics* graphics, int n, bool b) {
	Applet* app = CAppContainer::getInstance()->app;

	bool b2 = false;
	int height = this->imgTravelPath->height;
	int n2 = n;
	if (n2 > height) {
		b2 = true;
		n2 = height;
	}
	int n3 = b ? 0 : (height - n2);
	int width = this->imgTravelPath->width;
	if (n > height / 2) {
		Text* smallBuffer = app->localization->getSmallBuffer();
		smallBuffer->setLength(0);
		if (!b) {
			app->localization->composeText((short)3, (short)164, smallBuffer);
			smallBuffer->dehyphenate();
			graphics->drawString(smallBuffer, Canvas::earthNameCoords[0], Canvas::earthNameCoords[1], 4);
		}
		else {
			app->localization->composeText((short)3, (short)165, smallBuffer);
			smallBuffer->dehyphenate();
			graphics->drawString(smallBuffer, Canvas::moonNameCoords[2], Canvas::moonNameCoords[3], 4);
		}
		smallBuffer->dispose();
	}
	graphics->drawRegion(this->imgTravelPath, 0, n3, width, n2, Canvas::earthPathCoords[0], Canvas::earthPathCoords[1] + n3, 0, 0, 0);
	return b2;
}

bool TravelMapManager::drawEarthToHellLine(Graphics* graphics, int n, bool b) {
	Applet* app = CAppContainer::getInstance()->app;

	bool b2 = false;
	int height = this->imgTravelPath->height;
	int n2 = n;
	if (n2 > height) {
		b2 = true;
		n2 = height;
	}
	int n3 = b ? 0 : (height - n2);
	int width = this->imgTravelPath->width;
	if (n > 2 * height / 3) {
		Text* smallBuffer = app->localization->getSmallBuffer();
		smallBuffer->setLength(0);
		if (!b) {
			app->localization->composeText((short)3, (short)166, smallBuffer);
			smallBuffer->dehyphenate();
			graphics->drawString(smallBuffer, Canvas::hellNameCoords[0], Canvas::hellNameCoords[1], 4);
		}
		else {
			app->localization->composeText((short)3, (short)164, smallBuffer);
			smallBuffer->dehyphenate();
			graphics->drawString(smallBuffer, Canvas::earthNameCoords[2], Canvas::earthNameCoords[3], 4);
		}
		smallBuffer->dispose();
	}
	graphics->drawRegion(this->imgTravelPath, 0, n3, width, n2, Canvas::hellPathCoords[0], Canvas::hellPathCoords[1] + n3, 0, 0, 0);
	return b2;
}

void TravelMapManager::drawLocatorBoxAndName(Graphics* graphics, bool b, int n, Text* text) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int n2;
	int n3;
	if (this->onMoon(n)) {
		n2 = Canvas::moonCoords[0];
		n3 = Canvas::moonCoords[1];
	}
	else if (this->onEarth(n)) {
		n2 = Canvas::earthCoords[0];
		n3 = Canvas::earthCoords[1];
	}
	else {
		n2 = Canvas::hellCoords[0];
		n3 = Canvas::hellCoords[1];
	}
	short n4 = (short)(2 * (n - 1));
	int n5 = Canvas::CROSS_HAIR_CORDS[n4] + this->xDiff + n2;
	int n6 = Canvas::CROSS_HAIR_CORDS[n4 + 1] + this->yDiff + n3;
	if (b) {
		graphics->drawImage(this->imgMagGlass, n5, n6, 3, 0, 0);
	}

	int x = Canvas::LOCATOR_BOX_CORDS[n4] + this->xDiff + n2;
	int y = Canvas::LOCATOR_BOX_CORDS[n4 + 1] + this->yDiff + n3 + (this->imgNameHighlight->height >> 1);
	graphics->drawImage(this->imgNameHighlight, x, y, 6, 0, 0);
	canvas->graphics.currentCharColor = 3;
	graphics->drawString(text, x + (this->imgNameHighlight->width >> 1), y, 3);
}

void TravelMapManager::drawGridLines(Graphics* graphics, int i) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int iVar1;

	for (iVar1 = (i / 200) % 44 + canvas->displayRect[0]; iVar1 < canvas->displayRect[2]; iVar1 += 44) {
		graphics->drawImage(this->imgMapVertGridLines, iVar1, canvas->displayRect[1], 0x14, 0, 2);
	}
	for (iVar1 = canvas->displayRect[1] + 5; iVar1 < canvas->displayRect[3]; iVar1 += 44) {
		graphics->drawImage(this->imgMapHorzGridLines, canvas->displayRect[0], iVar1, 20, 0, 2);
		graphics->drawImage(this->imgMapHorzGridLines, 240, iVar1, 20, 0, 2);
	}
}

bool TravelMapManager::onMoon(int n) {
	return n >= 1 && n <= 3;
}

bool TravelMapManager::onEarth(int n) {
	return n >= 4 && n <= 6;
}

bool TravelMapManager::inHell(int n) {
	return n >= 7;
}

void TravelMapManager::handleInput(int key, int action) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	bool hasSavedState = app->game->hasSavedState();

	if (action == Enums::ACTION_MENU) {
		this->finishAndLoadLevel();
		return;
	}

	if (action == Enums::ACTION_FIRE) {
		if (canvas->stateVars[6] == 1) {
			this->finishAndLoadLevel();
		}
		else if (hasSavedState) {
			if (canvas->stateVars[2] == 1) {
				if (canvas->stateVars[8] == 0) {
					canvas->stateVars[3] = 1;
					canvas->stateVars[4] = 1;
					canvas->stateVars[6] = 1;
				}
				else {
					int n3 = app->upTimeMs - canvas->stateVars[0];
					this->totalTMTimeInPastAnimations += n3;
					canvas->stateVars[0] += n3;
					if (canvas->stateVars[4] == 1) {
						canvas->stateVars[4] = 0;
						canvas->stateVars[8] = 0;
						int n5 = Canvas::earthCoords[0];
						int n6 = Canvas::earthCoords[1];
						short n7 = (short)(2 * (this->TM_LoadLevelId - 1));
						this->targetX = Canvas::CROSS_HAIR_CORDS[n7] + this->xDiff + n5;
						this->targetY = Canvas::CROSS_HAIR_CORDS[n7 + 1] + this->yDiff + n6;
					}
					else {
						canvas->stateVars[3] = 1;
						canvas->stateVars[4] = 1;
					}
				}
			}
			else {
				this->totalTMTimeInPastAnimations += app->upTimeMs - canvas->stateVars[0];
				canvas->stateVars[0] = app->upTimeMs;
				canvas->stateVars[1] = 1;
				canvas->stateVars[2] = 1;
				if (this->newLevelSamePlanet()) {
					canvas->stateVars[3] = 1;
				}
			}
		}
	}
	else if (action == Enums::ACTION_AUTOMAP && canvas->stateVars[5] == 1) {
		this->finishAndLoadLevel();
	}
}

void TravelMapManager::finishAndLoadLevel() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	canvas->clearSoftKeys();
	this->dispose();
	canvas->setLoadingBarText((short)0, (short)41);
	canvas->setState(Canvas::ST_LOADING);
}

bool TravelMapManager::drawLocatorLines(Graphics* graphics, int n, bool b, bool b2) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int targetX;
	int targetY;
	int targetX2;
	int targetY2;
	if (n >= 0) {
		int n2 = n / (b ? 15 : 7);
		if (b) {
			int n3;
			int n4;
			if (this->onMoon(this->TM_LastLevelId)) {
				n3 = Canvas::moonCoords[0];
				n4 = Canvas::moonCoords[1];
			}
			else if (this->onEarth(this->TM_LastLevelId)) {
				if (b2) {
					n3 = Canvas::earthCoords[2];
					n4 = Canvas::earthCoords[3];
				}
				else {
					n3 = Canvas::earthCoords[0];
					n4 = Canvas::earthCoords[1];
				}
			}
			else {
				n3 = Canvas::hellCoords[0];
				n4 = Canvas::hellCoords[1];
			}
			short n5 = (short)(2 * (this->TM_LastLevelId - 1));
			targetX = Canvas::CROSS_HAIR_CORDS[n5] + this->xDiff + n3;
			targetY = Canvas::CROSS_HAIR_CORDS[n5 + 1] + this->yDiff + n4;
		}
		else {
			targetX = canvas->displayRect[2] - this->xDiff;
			targetY = canvas->displayRect[3] - this->yDiff;
		}
		int n6 = (this->targetX > targetX) ? 1 : ((this->targetX == targetX) ? 0 : -1);
		int n7 = (this->targetY > targetY) ? 1 : ((this->targetY == targetY) ? 0 : -1);
		targetX2 = targetX + n6 * n2;
		targetY2 = targetY + n7 * n2;
	}
	else {
		targetX2 = (targetX = this->targetX);
		targetY2 = (targetY = this->targetY);
	}
	bool b3 = false;
	bool b4 = false;
	if ((this->targetX - targetX) * (this->targetX - targetX2) < 0 || this->targetX == targetX2) {
		targetX2 = this->targetX;
		b3 = true;
	}
	if ((this->targetY - targetY) * (this->targetY - targetY2) < 0 || this->targetY == targetY2) {
		targetY2 = this->targetY;
		b4 = true;
	}
	graphics->drawLine(targetX2 + 1, canvas->displayRect[1], targetX2 + 1, canvas->displayRect[3], 0xFF000000);
	graphics->drawLine(canvas->displayRect[0], targetY2 + 1, canvas->displayRect[2], targetY2 + 1, 0xFF000000);
	graphics->drawLine(targetX2, canvas->displayRect[1], targetX2, canvas->displayRect[3], 0xFFBDFD80);
	graphics->drawLine(canvas->displayRect[0], targetY2, canvas->displayRect[2], targetY2, 0xFFBDFD80);
	return b3 && b4;
}

void TravelMapManager::drawStarFieldPage(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (app->upTimeMs - canvas->stateVars[0] > 5250) {
		this->_field_0xf2c = 2u;
		this->finishAndLoadLevel();
	}
	else {
		int yPos = canvas->screenRect[3] - canvas->screenRect[1] - this->imgStarField->height;
		graphics->fillRect(0, 0, canvas->menuRect[2], canvas->menuRect[3], 0xFF000000);
		this->drawStarField(graphics, 1, yPos / 2);
		graphics->drawImage(this->imgStarField, 1, yPos / 2, 0, 0, 0);
		graphics->drawImage(this->imgStarField, canvas->screenRect[2] - 1, yPos / 2, 24, 4, 0);
		canvas->softKeyRightID = -1;
		canvas->softKeyLeftID = -1;
		canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
		if (canvas->displaySoftKeys) {
			canvas->softKeyRightID = 40;
			canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
		}
	}
}

void TravelMapManager::drawStarField(Graphics* graphics, int x, int y) {
	Applet* app = CAppContainer::getInstance()->app;
	int upTimeMs;
	int result;
	int field_0xf20;
	unsigned int v8;
	unsigned int v9;
	int v10;
	int v11;
	signed int v12;
	int v13;
	int v14;
	int v15;
	int i;
	int v17;
	int v18;
	int v23;
	int v24;
	int v25;
	int v26;
	int v27;

	graphics->fillRect(x, y, this->_field_0xf24, this->_field_0xf20, 0xFF000000);
	if (app->upTimeMs - app->canvas->stateVars[7] > 59) {
		app->canvas->stateVars[7] = app->upTimeMs;
		this->runStarFieldFrame();
	}

	v23 = this->_field_0xf24 / 2;
	field_0xf20 = this->_field_0xf20;
	v26 = y;
	v25 = 0;
	v24 = field_0xf20 / 2;
	v27 = field_0xf20 / -2;
LABEL_20:
	if (v25 < field_0xf20 - this->_field_0xf28)
	{
		v17 = x;
		v18 = -v23;
		for (i = 0; ; ++i)
		{
			if (i >= this->_field_0xf24)
			{
				++v25;
				++v26;
				++v27;
				field_0xf20 = this->_field_0xf20;
				goto LABEL_20;
			}
			v8 = app->tinyGL->pixels[i + this->_field_0xf24 * v25];
			v9 = v8 >> 10;
			if ((v8 & 0x3FF) != 0)
				break;
		LABEL_17:
			++v17;
			++v18;
		}
		v10 = (int)(abs(v18) << 10) / v23;
		v11 = (int)(abs(v27) << 10) / v24;
		v12 = v10 + v11 + ((unsigned int)(v10 + v11) >> 31);
		v13 = (v10 + v11) / 2;
		graphics->setColor(65793 * ((v13 << 7 >> 10) + 128) - 0x1000000);
		if (v9 == 1)
		{
			v15 = 8 * v13;
		}
		else
		{
			if (!v9 || v9 != 2)
			{
				v14 = v12 >> 11;
			LABEL_9:
				if (v14 <= 0 || v14 == 1)
				{
					graphics->fillRect(v17, v26, 1, 1);
				}
				else
				{
					graphics->fillCircle(v17, v26, v14);
				}
				goto LABEL_17;
			}
			v15 = 10 * v13;
		}
		v14 = v15 >> 10;
		goto LABEL_9;
	}
}

void TravelMapManager::runStarFieldFrame() {
	Applet* app = CAppContainer::getInstance()->app;

	int v1;
	int v2;
	TinyGL* tinyGL;
	unsigned int v4;
	unsigned int v5;
	int v6;
	signed int v7;
	int* v8;
	int v9;
	int v10;
	int v11;
	unsigned int v12;
	TinyGL* v13;
	unsigned int v14;
	unsigned int v15;
	int v16;
	signed int v17;
	int* v18;
	int v19;
	int v20;
	int v21;
	int v22;
	unsigned int v23;
	int v24;
	int v25;
	int v26;
	int v27;
	int v28;
	int result;
	int v30;
	int v31;
	int v32;
	int v33;
	int v34;
	int v35;
	int v38;
	int v39;
	int v40;
	int v41;
	int i;
	int v43;
	unsigned short* v44;
	unsigned short* pixels;
	int v46;
	int v47;
	int v48;
	int v49;

	v1 = this->_field_0xf24;
	v38 = v1 / 2;
	v2 = this->_field_0xf20;
	v39 = v2 / 2;
	for (i = 0; i < v2 / 2; ++i)
	{
		v10 = 0;
		v48 = v39 - i;
		v12 = abs(v39 - i);
		v35 = 60 * v12;
		v34 = 20 * v12;
		v11 = -v38;
		while (v10 < v1)
		{
			tinyGL = app->tinyGL;
			pixels = tinyGL->pixels;
			v4 = pixels[v10 + v1 * i];
			v47 = v4 & 0x3FF;
			if ((v4 & 0x3FF) != 0)
			{
				v5 = v4 >> 10;
				if (v5 == 1)
				{
					v6 = v34;
					v7 = 20 * abs(v11);
				}
				else if (v5)
				{
					v6 = v35;
					v7 = 60 * abs(v11);
				}
				else
				{
					v6 = 300;
					v7 = 300;
				}
				v31 = this->_field_0xf2c;
				v8 = app->render->sinTable;
				v46 = (v8[(v47 + 256) & 0x3FF] * (v7 / v31) / v38) >> 16;
				v9 = (v8[v47] * (v6 / v31) / v39) >> 16;
				if (v48 + v9 >= -v39 && v39 > v48 + v9 && -v38 <= v11 + v46 && v38 > v11 + v46)
				{
					pixels[v10 + v1 * (i - v9) + v46] = v47 | ((short)v5 << 10);
					v1 = this->_field_0xf24;
					tinyGL = app->tinyGL;
				}
				tinyGL->pixels[v10 + v1 * i] = 0;
				v1 = this->_field_0xf24;
			}
			++v10;
			++v11;
		}
		v2 = this->_field_0xf20;
	}
	v49 = v2 - 1;
	v43 = v39 - (v2 - 1);
	while (v49 >= v2 / 2)
	{
		v20 = 0;
		v23 = abs(v43);
		v21 = -v38;
		v33 = 60 * v23;
		v32 = 20 * v23;
		while (v20 < v1)
		{
			v13 = app->tinyGL;
			v44 = v13->pixels;
			v14 = v44[v20 + v1 * v49];
			v40 = v14 & 0x3FF;
			if ((v14 & 0x3FF) != 0)
			{
				v15 = v14 >> 10;
				if (v15 == 1)
				{
					v16 = v32;
					v17 = 20 * abs(v21);
				}
				else if (v15)
				{
					v16 = v33;
					v17 = 60 * abs(v21);
				}
				else
				{
					v16 = 300;
					v17 = 300;
				}
				v30 = this->_field_0xf2c;
				v18 = app->render->sinTable;
				v41 = (v18[(v40 + 256) & 0x3FF] * (v17 / v30) / v38) >> 16;
				v19 = (v18[v40] * (v16 / v30) / v39) >> 16;
				if (v43 + v19 >= -v39 && v39 > v43 + v19 && v21 + v41 >= -v38 && v38 > v21 + v41)
				{
					v44[v20 + v1 * (v49 - v19) + v41] = v40 | ((short)v15 << 10);
					v1 = this->_field_0xf24;
					v13 = app->tinyGL;
				}
				v13->pixels[v20 + v1 * v49] = 0;
				v1 = this->_field_0xf24;
			}
			++v20;
			++v21;
		}
		--v49;
		++v43;
		v2 = this->_field_0xf20;
	}
	v22 = 0;
	while (1)
	{
		result = 4 / this->_field_0xf2c;
		if (v22 >= result)
			break;
		++v22;
		v24 = app->nextInt() % 1023;
		v25 = v24 + 1;
		v26 = app->nextInt() % 3;
		v27 = app->nextInt() % 15 + 1;
		if ((unsigned int)(v24 - 256) <= 0x1FE)
			v27 = -v27;
		v28 = app->nextInt() % 15 + 1;
		if (v25 >= 512)
			v28 = -v28;
		app->tinyGL->pixels[v27 + v38 + this->_field_0xf24 * (v39 - v28)] = v25 | ((short)v26 << 10);
	}
}
