#include "LoadingScreenController.h"

#include "App.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Graphics.h"
#include "Text.h"

void LoadingScreenController::setLoadingBarText(short loadingStringID, short loadingStringType) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	canvas->loadingStringID = loadingStringID;
	canvas->loadingStringType = loadingStringType;
	canvas->loadingFlags |= 0x3;
}

void LoadingScreenController::updateLoadingBar(bool force) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int uVar2;

	if (force == false) {
		if (app->upTimeMs - canvas->lastPacifierUpdate < 0x96) {
			return;
		}
		canvas->lastPacifierUpdate = app->upTimeMs;
	}
	uVar2 = canvas->loadingFlags;
	canvas->loadingFlags = uVar2 | 3;
	if ((canvas->loadingStringID == -1) || (canvas->loadingStringType == -1)) {
		this->setLoadingBarText((short)0, (short)57);
	}
	canvas->loadingFlags = uVar2 | 3;
	canvas->repaintFlags |= Canvas::REPAINT_LOADING_BAR;
	return;
}

void LoadingScreenController::drawLoadingBar(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	Text* text;
	int iVar1;
	int iVar2;
	int iVar3;
	int uVar4;
	int iVar5;
	int iVar6;

	if ((canvas->loadingFlags & 3) != 0) {
		iVar1 = canvas->SCR_CX;
		iVar6 = canvas->SCR_CY;
		if ((canvas->loadingFlags & 1) != 0) {
			text = app->localization->getSmallBuffer();
			canvas->loadingFlags &= 0xfffffffe;
			graphics->eraseRgn(canvas->displayRect);
			graphics->fillRegion(canvas->imgFabricBG, iVar1 + -0x4b, iVar6 + -0x1d, 0x96, 0x3a);
			graphics->setColor(0xffffffff);
			graphics->drawRect(iVar1 + -0x4b, iVar6 + -0x1d, 0x96, 0x3a);
			app->localization->composeText(canvas->loadingStringID, canvas->loadingStringType, text);
			text->dehyphenate();
			graphics->drawString(text, canvas->SCR_CX, canvas->SCR_CY + -0x16, 0x11);
			text->setLength(0);
			app->localization->composeText(0, 58, text);
			text->dehyphenate();
			graphics->drawString(text, canvas->SCR_CX, canvas->SCR_CY + -6, 0x11);
			text->dispose();
			iVar1 = canvas->SCR_CX;
			iVar6 = canvas->SCR_CY;
		}
		canvas->loadingFlags &= 0xfffffffd;
		iVar2 = iVar1 + 0x3e;
		iVar5 = canvas->pacifierX + 10;
		canvas->pacifierX = iVar5;
		iVar3 = iVar2;
		if (iVar2 <= iVar5) {
			iVar3 = iVar1 + -0x42;
		}
		if ((iVar2 <= iVar5) || (iVar3 = iVar1 + -0x42, iVar5 < iVar3)) {
			canvas->pacifierX = iVar3;
		}
		graphics->setColor(-0x1000000);
		graphics->fillRect(iVar1 + -0x43, iVar6 + 0xb, 0x86, 0xc);
		graphics->setColor(-1);
		graphics->drawRect(iVar1 + -0x43, iVar6 + 0xb, 0x86, 0xc);
		iVar3 = canvas->pacifierX;
		iVar1 = (iVar1 + 0x43) - iVar3;
		if (0x19 < iVar1) {
			iVar1 = 0x1a;
		}
		iVar2 = ((iVar3 / 10) / 6) * 8;
		uVar4 = (iVar3 / 10) % 6;
		if (uVar4 < 3) {
			iVar2 = 6;
		}
		if (2 < uVar4) {
			iVar2 = 0;
		}
		graphics->drawRegion(canvas->imgLoadingFire, 0, 0, iVar1, 9, iVar3, iVar6 + 0xd, 0, iVar2, 0);
	}
}
