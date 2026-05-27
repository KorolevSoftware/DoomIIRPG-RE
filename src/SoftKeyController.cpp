#include "SoftKeyController.h"

#include "App.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Graphics.h"
#include "Player.h"
#include "Text.h"

void SoftKeyController::clearSoftKeys() {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	canvas->softKeyLeftID = -1;
	canvas->softKeyRightID = -1;
	canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
}

void SoftKeyController::clearLeftSoftKey() {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	canvas->softKeyLeftID = -1;
	canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
}

void SoftKeyController::clearRightSoftKey() {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	canvas->softKeyRightID = -1;
	canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
}

void SoftKeyController::setLeftSoftKey(short i, short i2) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	if (!canvas->displaySoftKeys) {
		return;
	}
	canvas->softKeyLeftID = Localization::STRINGID(i, i2);
	canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
}

void SoftKeyController::setRightSoftKey(short i, short i2) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	if (!canvas->displaySoftKeys) {
		return;
	}
	canvas->softKeyRightID = Localization::STRINGID(i, i2);
	canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
}

void SoftKeyController::setSoftKeys(short n, short n2, short n3, short n4) {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	if (!canvas->displaySoftKeys) {
		return;
	}
	canvas->softKeyLeftID = Localization::STRINGID(n, n2);
	canvas->softKeyRightID = Localization::STRINGID(n3, n4);
	canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;
	this->checkHudEvents();
}

void SoftKeyController::checkHudEvents() {
	Applet* app = CAppContainer::getInstance()->app;
	(void)app;
	//app->hud->hudEventsAvailable = (canvas->displaySoftKeys && (canvas->softKeyLeftID == 52 || canvas->softKeyRightID == 52)); // J2ME Only
}

void SoftKeyController::drawSoftKeys(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	(void)app;
	(void)graphics;
	// J2ME Only
}

void SoftKeyController::drawPlayingSoftKeys() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	if (app->player->inTargetPractice) {
		this->setLeftSoftKey((short)0, (short)30);
		this->clearRightSoftKey();
	}
	else if (canvas->isZoomedIn) {
		this->setSoftKeys((short)0, (short)52, (short)0, (short)55);
	}
	else if (canvas->state == Canvas::ST_AUTOMAP) {
		this->setSoftKeys((short)0, (short)52, (short)0, (short)53);
	}
	else if (app->player->isFamiliar) {
		this->setSoftKeys((short)0, (short)52, (short)0, (short)216);
	}
	else if (app->player->weaponIsASentryBot(app->player->ce->weapon)) {
		this->setSoftKeys((short)0, (short)52, (short)0, (short)220);
	}
	else {
		this->setSoftKeys((short)0, (short)52, (short)0, (short)55);
	}
}
