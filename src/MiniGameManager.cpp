#include <cstdlib>
#include <cstring>
#include "MiniGameManager.h"
#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Graphics.h"
#include "Text.h"
#include "Image.h"
#include "Enums.h"
#include "Hud.h"
#include "Player.h"
#include "Combat.h"
#include "Render.h"
#include "Sound.h"
#include "Button.h"

MiniGameManager::MiniGameManager() {
	std::memset(this, 0, sizeof(MiniGameManager));
}

MiniGameManager::~MiniGameManager() {
}

bool MiniGameManager::startup() {
	Applet* app = CAppContainer::getInstance()->app;
	fmButton* button;

	this->imgBootL = app->loadImage("bootL.bmp", true);
	this->imgBootR = app->loadImage("bootR.bmp", true);
	this->m_treadmillButtons = new fmButtonContainer();
	int w = imgBootL->width;
	int h = imgBootL->height;
	int x = 240 - (2 * w);
	int y = 160 - (h / 2);
	button = new fmButton(0, x, y, w, h, 1027);
	button->SetImage(this->imgBootL, true);
	button->SetHighlightImage(this->imgBootL, true);
	button->normalRenderMode = 12;
	button->highlightRenderMode = 0;
	this->m_treadmillButtons->AddButton(button);
	button = new fmButton(1, x + 3 * w, y, w, h, 1027);
	button->SetImage(this->imgBootR, true);
	button->SetHighlightImage(this->imgBootR, true);
	button->normalRenderMode = 12;
	button->highlightRenderMode = 0;
	this->m_treadmillButtons->AddButton(button);

	return true;
}

void MiniGameManager::onEnterTreadmill() {
	Applet* app = CAppContainer::getInstance()->app;
	this->treadmillNumSteps = 0;
	this->treadmillLastStep = 1;
	this->treadmillLastStepTime = app->time;
	this->treadmillReturnCode = 0;
}

bool MiniGameManager::handleTreadmillEvents(int action) {
	Applet* app = CAppContainer::getInstance()->app;
	if (this->treadmillReturnCode != 0) {
		return true;
	}
	if (this->treadmillLastStepTime + 300 > app->time) {
		if (this->treadmillNumSteps == 4) {
			app->hud->addMessage((short)247, 3);
		}
		return false;
	}
	if (action == Enums::ACTION_DOWN) {
		this->treadmillReturnCode = 2;
		return true;
	}
	if (action == Enums::ACTION_STRAFELEFT || action == Enums::ACTION_STRAFERIGHT) {
		if (action == this->treadmillLastStep) {
			this->treadmillReturnCode = 3;
		}
		else {
			this->treadmillLastStep = action;
			if (++this->treadmillNumSteps * 2 >= 100) {
				this->treadmillReturnCode = 1;
			}
		}
		this->treadmillLastStepTime = app->time;
		return true;
	}
	if (action == Enums::ACTION_AUTOMAP) {
		this->treadmillReturnCode = 2;
		return true;
	}
	return true;
}

void MiniGameManager::treadmillState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	app->hud->repaintFlags |= 0x22;
	canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_VIEW3D);
	app->hud->repaintFlags &= 0xFFFFFFBF;
	bool b = false;
	if (this->treadmillReturnCode != 0) {
		if (this->treadmillReturnCode == 1 && app->time > this->treadmillLastStepTime + 300) {
			app->localization->resetTextArgs();
			app->localization->addTextArg(2);
			if (app->player->modifyStat(Enums::STAT_AGILITY, 2) == 0) {
				app->hud->addMessage((short)244, 3);
			}
			else {
				app->hud->addMessage((short)243, 3);
			}
			b = true;
		}
		else if (this->treadmillReturnCode == 3) {
			if (this->treadmillFall()) {
				app->hud->addMessage((short)245, 3);
				b = true;
			}
		}
		else if (this->treadmillReturnCode == 2) {
			canvas->attemptMove(canvas->viewX - canvas->viewStepX, canvas->viewY - canvas->viewStepY);
			app->hud->addMessage((short)246, 3);
			b = true;
		}
	}
	if (b) {
		app->combat->shiftWeapon(false);
		canvas->setState(Canvas::ST_PLAYING);
		return;
	}
	if (this->treadmillLastStep == 1) {
		canvas->updateView();
		return;
	}
	if (this->treadmillReturnCode == 0 && app->time > 1500 + this->treadmillLastStepTime) {
		this->treadmillReturnCode = 3;
		this->treadmillLastStepTime = app->time;
		return;
	}
	if (app->time <= this->treadmillLastStepTime + 300) {
		bool b2 = this->treadmillLastStep == 9;
		bool b3 = this->treadmillLastStepTime + 150 > app->time;
		bool b4 = b2 ^ !b3;
		int n = 4 + (-4 * ((std::abs(150 - (app->time - this->treadmillLastStepTime)) << 16) / 150) >> 16);
		if (b4 == b3) {
			n = -n;
		}
		int n2 = n * n;
		canvas->viewX = canvas->destX + n * (canvas->viewRightStepX >> 6);
		canvas->viewY = canvas->destY + n * (canvas->viewRightStepY >> 6);
		canvas->viewZ = 36 + n2 + app->render->getHeight(canvas->destX, canvas->destY);
		canvas->invalidateRect();
	}

	canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, canvas->viewRoll, 290);
}

bool MiniGameManager::treadmillFall() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	if (app->time > this->treadmillLastStepTime + 1000 + 500 + 500) {
		canvas->viewX = canvas->destX;
		canvas->viewY = canvas->destY;
		canvas->viewZ = canvas->destZ;
		canvas->viewPitch = canvas->destPitch;
		return true;
	}
	int n = (canvas->viewStepX >> 6) * 32;
	int n2 = (canvas->viewStepY >> 6) * 32;
	if (this->treadmillLastStepTime + 1000 > app->time) {
		int n3 = 1000 - (app->time - this->treadmillLastStepTime);
		int n4 = (n3 << 16) / 1000;
		canvas->viewX = canvas->destX + (-n + (n * n4 >> 16));
		canvas->viewY = canvas->destY + (-n2 + (n2 * n4 >> 16));
		canvas->viewZ = 36;
		if (n3 < 250) {
			canvas->viewZ -= 12 - (12 * n4 >> 16);
		}
		if (n3 > 500) {
			canvas->viewPitch = 128 - (128 * n4 >> 16);
		}
		else {
			canvas->viewPitch = 128 * n4 >> 16;
		}
	}
	else if (this->treadmillLastStepTime + 1000 + 500 > app->time) {
		canvas->viewX = canvas->destX - n;
		canvas->viewY = canvas->destY - n2;
		canvas->viewZ = 24;
		canvas->viewPitch = 0;
	}
	else {
		int n5 = 500 - (app->time - 1000 - 500 - this->treadmillLastStepTime);
		int n6 = (n5 << 16) / 500;
		canvas->viewX = canvas->destX - (n * n6 >> 16);
		canvas->viewY = canvas->destY - (n2 * n6 >> 16);
		canvas->viewZ = 36 - (12 * n6 >> 16);
		if (n5 > 250) {
			canvas->viewPitch = -128 + (128 * n6 >> 16);
		}
		else {
			canvas->viewPitch = -(128 * n6 >> 16);
		}
	}
	canvas->viewZ += app->render->getHeight(canvas->destX, canvas->destY);
	canvas->invalidateRect();
	canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, canvas->viewRoll, 290);
	return false;
}

void MiniGameManager::drawTreadmillReadout(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	Text* smallBuffer = app->localization->getSmallBuffer();
	app->localization->resetTextArgs();
	app->localization->addTextArg(this->treadmillNumSteps * 2);
	app->localization->composeText((short)0, (short)242, smallBuffer);
	app->hud->drawImportantMessage(graphics, smallBuffer, 0xFF666666);
	smallBuffer->dispose();
	this->m_treadmillButtons->Render(graphics);
}

void MiniGameManager::drawTargetPracticeScore(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	if (app->hud->msgCount != 0 && (app->hud->messageFlags[0] & 0x4) != 0x0) {
		return;
	}
	Text* smallBuffer = app->localization->getSmallBuffer();
	app->localization->resetTextArgs();
	app->localization->composeText((short)0, (short)229, smallBuffer);
	smallBuffer->append(app->player->targetPracticeScore);
	app->hud->drawImportantMessage(graphics, smallBuffer, 0xFF7F0000);
	smallBuffer->dispose();
}

void MiniGameManager::evaluateMiniGameResults(int n) {
	Applet* app = CAppContainer::getInstance()->app;
	if (n == 1) {
		int modifyStat = app->player->modifyStat(7, 2);
		app->localization->resetTextArgs();
		if (modifyStat > 0) {
			app->localization->addTextArg(modifyStat);
			app->hud->addMessage((short)236, 3);
		}
		else {
			app->hud->addMessage((short)237, 3);
		}
	}
}

void MiniGameManager::initMiniGameHelpScreen() {
	this->miniGameHelpScrollPosition = 0;
}

void MiniGameManager::drawMiniGameHelpScreen(Graphics* graphics, int i, int i2, Image* image) {
	graphics->drawImage(image, 0, 0, 0, 0, 0);
	this->drawMiniGameHelpText(graphics, i, i2);
}

void MiniGameManager::drawMiniGameHelpText(Graphics* graphics, int i, int i2) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	int x;
	Text* textBuff1;
	int iVar1;
	Text* textBuff2;
	int iVar2;
	Text* textBuff3;
	int iVar3;

	textBuff1 = app->localization->getSmallBuffer();
	x = (canvas->screenRect[2] - canvas->screenRect[0]) / 2;
	textBuff1->setLength(0);
	app->localization->composeText(i, textBuff1);
	textBuff1->dehyphenate();
	graphics->drawString(textBuff1, x, canvas->screenRect[1] + 0x19, 3);

	textBuff1->setLength(0);
	app->localization->composeText(i2, textBuff1);
	textBuff1->wrapText(0x31, '\n');
	iVar1 = textBuff1->getNumLines();
	this->helpTextNumberOfLines = iVar1;

	textBuff2 = app->localization->getSmallBuffer();
	textBuff2->setLength(0);
	for (iVar1 = 0; iVar1 < this->miniGameHelpScrollPosition; iVar1 = iVar1 + 1) {
		iVar3 = textBuff1->findFirstOf('\n');
		textBuff3 = textBuff1;
		if (iVar3 != -1) {
			iVar2 = textBuff1->length();
			textBuff1->substring(textBuff2, iVar3 + 1, (iVar2 - iVar3) + -1);
			textBuff1->setLength(0);
			textBuff3 = textBuff2;
			textBuff2 = textBuff1;
		}
		textBuff1 = textBuff3;
	}
	for (iVar1 = this->miniGameHelpScrollPosition + 0x10; iVar1 < this->helpTextNumberOfLines;
		iVar1 = iVar1 + 1) {
		iVar3 = textBuff1->findLastOf('\n');
		textBuff3 = textBuff1;
		if (iVar3 != -1) {
			textBuff1->substring(textBuff2, 0, iVar3);
			textBuff1->setLength(0);
			textBuff3 = textBuff2;
			textBuff2 = textBuff1;
		}
		textBuff1 = textBuff3;
	}
	textBuff2->dispose();

	graphics->drawString(textBuff1, canvas->screenRect[0] + 0x12, canvas->screenRect[1] + 0x2d, 0x14);
	iVar3 = this->helpTextNumberOfLines;
	iVar1 = this->miniGameHelpScrollPosition + 0x10;
	if (iVar3 < iVar1) {
		iVar1 = iVar3;
	}
	canvas->drawScrollBar(graphics, canvas->screenRect[2] + -0x12, canvas->screenRect[1] + 0x2d, 0x100,
		this->miniGameHelpScrollPosition, iVar1, iVar3, 0x10);
	textBuff1->setLength(0);
	app->localization->composeText(0, 0x60, textBuff1);
	textBuff1->dehyphenate();
	graphics->drawString(textBuff1, x, canvas->screenRect[3] + -0x14, 3);
	textBuff1->dispose();
}

void MiniGameManager::handleMiniGameHelpScreenScroll(int i) {
	int iVar1;
	int iVar2;

	if (i < 0) {
		iVar1 = i + this->miniGameHelpScrollPosition;
		if (iVar1 < 0) {
			iVar1 = 0;
		}
		this->miniGameHelpScrollPosition = iVar1;
		return;
	}
	if (0 < i) {
		iVar1 = this->helpTextNumberOfLines + -0x10;
		if (iVar1 < 0) {
			iVar1 = 0;
		}
		iVar2 = i + this->miniGameHelpScrollPosition;
		if (iVar1 < iVar2) {
			this->miniGameHelpScrollPosition = iVar1;
		}
		else {
			this->miniGameHelpScrollPosition = iVar2;
		}
		return;
	}
	return;
}
