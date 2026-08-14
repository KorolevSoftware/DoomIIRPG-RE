#include <stdexcept>
#include <assert.h>
#include <algorithm>

#include "SDLGL.h"
#include "App.h"
#include "Image.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Graphics.h"
#include "MayaCamera.h"
#include "Game.h"
#include "GLES.h"
#include "TinyGL.h"
#include "Hud.h"
#include "Render.h"
#include "Combat.h"
#include "Player.h"
#include "MenuSystem.h"
#include "HackingGame.h"
#include "SentryBotGame.h"
#include "VendingMachine.h"
#include "ParticleSystem.h"
#include "Text.h"
#include "Button.h"
#include "Sound.h"
#include "Resource.h"
#include "Enums.h"
#include "Utils.h"
#include "Menus.h"
#include "Input.h"

Canvas::Canvas() {
	std::memset(this, 0, sizeof(Canvas));
}

Canvas::~Canvas() {
}

bool Canvas::isLoaded;

bool Canvas::startup() {
	Applet* app = CAppContainer::getInstance()->app;
	int viewWidth, viewHeight;
	fmButton* button;

	printf("Canvas::startup\n");

	this->displayRect[0] = 0;
	this->displayRect[1] = 0;
	this->displayRect[2] = app->backBuffer->width;
	this->displayRect[3] = app->backBuffer->height;

	//printf("this->displayRect[0] %d\n", this->displayRect[0]);
	//printf("this->displayRect[1] %d\n", this->displayRect[1]);
	//printf("this->displayRect[2] %d\n", this->displayRect[2]);
	//printf("this->displayRect[3] %d\n", this->displayRect[3]);

	this->graphics.setGraphics();
	this->pacLogoTime = -1;
	this->vibrateEnabled = true;
	this->loadMapStringID = -1;
	this->startupMap = 1;
	this->loadType = 0;
	this->saveType = 0;
	this->st_count = 0;
	this->knockbackDist = 0;
	this->dialogSystem.numHelpMessages = 0;
	this->destZ = 36;
	this->viewZ = 36;
	this->screenRect[2] = 0;
	this->screenRect[3] = 0;
	this->dialogSystem.dialogThread = nullptr;
	this->ignoreFrameInput = false;
	this->blockInputTime = 0;
	this->showLocation = false;
	this->lastPacifierUpdate = 0;
	this->numEvents = 0;
	this->dialogSystem.dialogItem = nullptr;
	this->dialogSystem.dialogViewLines = 0;
	this->lastMapID = 0;
	this->loadMapID = 0;
	this->automapDrawn = false;

	this->displayRect[2] &= 0xFFFFFFFE;
	this->screenRect[2] = this->displayRect[2];
	this->screenRect[3] = this->displayRect[3];

	this->dialogMaxChars = (this->displayRect[2] - 2) / 9;
	this->scrollMaxChars = (this->displayRect[2] - 2) / 9;
	this->dialogWithBarMaxChars		= (this->displayRect[2] - 9) / 9;
	this->scrollWithBarMaxChars		= (this->displayRect[2] - 9) / 9;
	this->menuScrollWithBarMaxChars = (this->displayRect[2] - 9) / 9;
	this->ingameScrollWithBarMaxChars = (this->displayRect[2] - 34) / 9;
	this->menuHelpMaxChars = (this->displayRect[2] - 32) / 9;
	this->subtitleMaxChars = this->displayRect[2] / 9;

	if (app->hud->startup()) {

		int n2 = this->screenRect[3] - 35 - 35;
		if (this->displayRect[3] >= 128) {
			this->displaySoftKeys = true;
			this->softKeyY = this->displayRect[3] - 0;
			if (this->displayRect[3] - this->screenRect[3] < 0) {
				n2 -= 0 - (this->displayRect[3] - this->screenRect[3]);
			}
		}
		else {
			this->softKeyY = this->displayRect[3];
		}

		int n3 = n2 & 0xFFFFFFFE;
		this->screenRect[3] = n3 + 35 + 35;
		this->screenRect[0] = (this->displayRect[2] - this->screenRect[2]) / 2;
		if (this->displaySoftKeys) {
			this->screenRect[1] = (this->softKeyY - this->screenRect[3]) / 2;
		}
		else {
			this->screenRect[1] = (this->displayRect[3] - this->screenRect[3]) / 2;
		}

		this->SCR_CX = this->screenRect[2] / 2;
		this->SCR_CY = this->screenRect[3] / 2;
		this->viewRect[0] = this->screenRect[0];
		this->viewRect[1] = 20;//this->screenRect[1] + 35;
		this->viewRect[2] = this->screenRect[2];
		this->viewRect[3] = n3;

		// custom
		viewWidth = 0;
		viewHeight = 0;
		if (viewWidth != 0 && viewHeight != 0) {
			this->viewRect[0] += ((this->viewRect[2] - viewWidth) >> 1);
			this->viewRect[1] += ((this->viewRect[3] - viewHeight) >> 1);
			this->viewRect[2] = viewWidth;
			this->viewRect[3] = viewHeight;
		}

		this->hudRect[0] = this->displayRect[0];
		this->hudRect[1] = this->screenRect[1];
		this->hudRect[2] = this->displayRect[2];
		this->hudRect[3] = this->screenRect[3];

		this->menuRect[0] = this->displayRect[0];
		this->menuRect[1] = this->displayRect[1];
		this->menuRect[2] = this->displayRect[2];
		this->menuRect[3] = this->screenRect[3];

		this->CAMERAVIEW_BAR_HEIGHT = 20;

		this->cinRect[0] = this->viewRect[0];
		this->cinRect[1] = 42;
		this->cinRect[2] = this->viewRect[2];
		this->cinRect[3] = this->viewRect[3];

		if (this->screenRect[1] + this->screenRect[3] == this->softKeyY + -1) {
			this->softKeyY = this->screenRect[1] + this->screenRect[3];
		}

		this->setAnimFrames(10);

		this->startupMap = 1;
		this->skipIntro = false;
		this->tellAFriend = false;

		app->beginImageLoading();
		this->imgDialogScroll = app->loadImage("DialogScroll.bmp", true);
		this->imgFabricBG = app->loadImage("FabricBG.bmp", true);
		this->imgFont = app->loadImage("Font.bmp", true);
		this->imgEndOfLevelStatsBG = app->loadImage("endOfLevelStatsBG.bmp", true);
		this->imgGameHelpBG = app->loadImage("gameHelpBG.bmp", true);
		this->imgIcons_Buffs = app->loadImage("Icons_Buffs.bmp", true);
		this->imgInventoryBG = app->loadImage("inventoryBG.bmp", true);
		this->imgLoadingFire = app->loadImage("loadingFire.bmp", true);
		this->imgFont_16p_Light = app->loadImage("Font_16p_Light.bmp", true);
		this->imgFont_16p_Dark = app->loadImage("Font_16p_Dark.bmp", true);
		this->imgFont_18p_Light = app->loadImage("Font_18p_Light.bmp", true);
		this->imgWarFont = app->loadImage("WarFont.bmp", true);
		this->fontRenderMode = 0;
		app->endImageLoading();

		this->lootingSystem.lootSource = -1;
		this->m_controlLayout = 2;
		this->isFlipControls = false;
		this->m_controlMode = 1;
		this->lastBacklightRefresh = 0;
		this->vibrateTime = 0;
		this->areSoundsAllowed = false;
		this->m_controlAlpha = 50;
		this->m_controlGraphic = 0;

		char* arrowsFiles[] = {
			"arrow-up.bmp",
			"greenArrow_up.bmp",
			"arrow-up_pressed.bmp",
			"greenArrow_up-pressed.bmp",
			"arrow-down.bmp",
			"greenArrow_down.bmp",
			"arrow-down_pressed.bmp",
			"greenArrow_down-pressed.bmp",
			"arrow-left.bmp",
			"greenArrow_left.bmp",
			"arrow-left_pressed.bmp",
			"greenArrow_left-pressed.bmp",
			"arrow-right.bmp",
			"greenArrow_right.bmp",
			"arrow-right_pressed.bmp",
			"greenArrow_right-pressed.bmp"
		};

		char** files = arrowsFiles;
		Image **imgArrows = this->imgArrows;
		for (int i = 0; i < 2; i++) {
			imgArrows[0] = app->loadImage(files[0], true);
			imgArrows[2] = app->loadImage(files[2], true);
			imgArrows[4] = app->loadImage(files[4], true);
			imgArrows[6] = app->loadImage(files[6], true);
			imgArrows[8] = app->loadImage(files[8], true);
			imgArrows[10] = app->loadImage(files[10], true);
			imgArrows[12] = app->loadImage(files[12], true);
			imgArrows[14] = app->loadImage(files[14], true);
			imgArrows++;
			files++;
		}

		this->imgDpad_default = app->loadImage("dpad_default.bmp", true);
		this->imgDpad_up_press = app->loadImage("dpad_up-press.bmp", true);
		this->imgDpad_down_press = app->loadImage("dpad_down-press.bmp", true);
		this->imgDpad_left_press = app->loadImage("dpad_left-press.bmp", true);
		this->imgDpad_right_press = app->loadImage("dpad_right-press.bmp", true);
		this->imgPageUP_Icon = app->loadImage("pageUP_Icon.bmp", true);
		this->imgPageDOWN_Icon = app->loadImage("pageDOWN_Icon.bmp", true);
		this->imgPageOK_Icon = app->loadImage("pageOK_Icon.bmp", true);
		this->imgSniperScope_Dial = app->loadImage("SniperScope_Dial.bmp", true);
		this->imgSniperScope_Knob = app->loadImage("SniperScope_Knob.bmp", true);

		this->touched = false;

		// Setup Sniper Scope Dial Scroll Button
		{
			this->m_sniperScopeDialScrollButton = new fmScrollButton(400, 67, this->imgSniperScope_Dial->width, this->imgSniperScope_Dial->height, true, 1113);
			this->m_sniperScopeDialScrollButton->SetScrollBox(0, 0, 1, 1, 16);
			this->m_sniperScopeDialScrollButton->field_0x0_ = 1;
		}

		// Setup Sniper Scope Buttons
		{
			this->m_sniperScopeButtons = new fmButtonContainer();
			button = new fmButton(6, 122, 20, 236, 236, -1);
			this->m_sniperScopeButtons->AddButton(button);
		}

		// Setup Control Buttons
		{
			fmButtonContainer** m_controlButtons = this->m_controlButtons;
			for (int i = 0; i < 2; i++) {
				m_controlButtons[0] = new fmButtonContainer();
				m_controlButtons[2] = new fmButtonContainer();
				m_controlButtons[4] = new fmButtonContainer();

				if (i == 1) {
					int v53 = 5;
					int v49 = 117;
					while (1)
					{
						button = new fmButton(5, v53, v53 + 116, 13, v49, -1);
						//button->drawTouchArea = true; // Test
						m_controlButtons[2]->AddButton(button);
						button = new fmButton(7, 140 - v53, v53 + 116, 13, v49, -1);
						//button->drawTouchArea = true; // Test
						m_controlButtons[2]->AddButton(button);
						button = new fmButton(3, v53 + 13, v53 + 103, v49, 13, -1);
						//button->drawTouchArea = true; // Test
						m_controlButtons[0]->AddButton(button);
						button = new fmButton(9, v53 + 13, 243 - v53, v49, 13, -1);
						//button->drawTouchArea = true; // Test
						m_controlButtons[0]->AddButton(button);
						v49 -= 26;
						if (v53 == 44)
							break;
						v53 += 13;
					}
					button = new fmButton(6, 153, 25, 322, 226, -1);
					m_controlButtons[2]->AddButton(button);
					this->m_swipeArea[1] = new fmSwipeArea(0, 0, this->screenRect[2], this->screenRect[3] - 64, 50, 50);
				}
				else {
					button = new fmButton(3, 42, 31, 70, 70, -1);
					button->SetImage(&this->imgArrows[0], 0, true);
					button->SetHighlightImage(&this->imgArrows[2], 0, true);
					button->normalRenderMode = 13;
					button->highlightRenderMode = 13;
					m_controlButtons[0]->AddButton(button);
					button = new fmButton(9, 42, 181, 70, 70, -1);
					button->SetImage(&this->imgArrows[4], 0, true);
					button->SetHighlightImage(&this->imgArrows[6], 0, true);
					button->normalRenderMode = 13;
					button->highlightRenderMode = 13;
					m_controlButtons[0]->AddButton(button);
					button = new fmButton(5, 5, 106, 70, 70, -1);
					button->SetImage(&this->imgArrows[8], 0, true);
					button->SetHighlightImage(&this->imgArrows[10], 0, true);
					button->normalRenderMode = 13;
					button->highlightRenderMode = 13;
					m_controlButtons[2]->AddButton(button);
					button = new fmButton(7, 80, 106, 70, 70, -1);
					button->SetImage(&this->imgArrows[12], 0, true);
					button->SetHighlightImage(&this->imgArrows[14], 0, true);
					button->normalRenderMode = 13;
					button->highlightRenderMode = 13;
					m_controlButtons[2]->AddButton(button);
					button = new fmButton(6, 155, 25, 320, 226, -1);
					m_controlButtons[2]->AddButton(button);
					button = new fmButton(6, 475, 172, 0, 79, -1);
					m_controlButtons[2]->AddButton(button);
					this->m_swipeArea[0] = new fmSwipeArea(0, 0, this->screenRect[2], this->screenRect[3] - 64, 50, 50);
				}

				m_controlButtons++;
			}
		}

		// Setup Dialog Buttons
		{
			this->m_dialogButtons = new fmButtonContainer();
			for (int i = 0; i < 5; i++) {
				button = new fmButton(i, 0, 0, 0, 0, 1027);
				this->m_dialogButtons->AddButton(button);
			}
			button = new fmButton(5, 390, 20, 90, 90, 1027);
			button->SetImage(this->imgPageUP_Icon, true);
			button->SetHighlightImage(this->imgPageUP_Icon, true);
			button->normalRenderMode = 12;
			button->highlightRenderMode = 0;
			this->m_dialogButtons->AddButton(button);
			button = new fmButton(6, 390, 110, 90, 90, 1027);
			button->SetImage(this->imgPageDOWN_Icon, true);
			button->SetHighlightImage(this->imgPageDOWN_Icon, true);
			button->normalRenderMode = 12;
			button->highlightRenderMode = 0;
			this->m_dialogButtons->AddButton(button);
			button = new fmButton(7, 390, 110, 90, 90, 1027);
			button->SetImage(this->imgPageOK_Icon, true);
			button->SetHighlightImage(this->imgPageOK_Icon, true);
			button->normalRenderMode = 12;
			button->highlightRenderMode = 0;
			this->m_dialogButtons->AddButton(button);
			button = new fmButton(8, 0, 0, 0, 0, 1027);
			this->m_dialogButtons->AddButton(button);
		}

		// Setup SoftKey Buttons
		{
			this->m_softKeyButtons = new fmButtonContainer();
			button = new fmButton(19, 0, 250, 100, 70, 1027);
			this->m_softKeyButtons->AddButton(button);
			button = new fmButton(20, 380, 250, 100, 70, 1027);
			this->m_softKeyButtons->AddButton(button);
		}

		// Setup Mixing Buttons
		{
			this->m_mixingButtons = new fmButtonContainer();
		}

		// Setup TreadMill Buttons
		this->introSequenceManager.startup();
		this->miniGameManager.startup();

		return true;
	}

	return false;
}

void Canvas::flushGraphics() {
	this->graphics.resetScreenSpace();
	this->backPaint(&this->graphics);
}

void Canvas::backPaint(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;

	graphics->clearClipRect();

	if (this->repaintFlags & Canvas::REPAINT_CLEAR) {
		this->repaintFlags &= ~Canvas::REPAINT_CLEAR;
		graphics->eraseRgn(this->displayRect);
	}

	if (this->repaintFlags & Canvas::REPAINT_VIEW3D) {
		if (app->render->_gles->isInit) {
			this->repaintFlags &= ~Canvas::REPAINT_VIEW3D;
			if (app->render->isFading()) {
				app->render->fadeScene(graphics);
			}
		}
		else {
			//this->repaintFlags &= ~Canvas::REPAINT_VIEW3D;
			//app->render->Render3dScene();
			app->render->drawRGB(graphics);
		}
	}

	if (this->repaintFlags & Canvas::REPAINT_PARTICLES) {
		this->repaintFlags &= ~Canvas::REPAINT_PARTICLES;
		app->particleSystem->renderSystems(graphics);
	}

	if (this->state == Canvas::ST_COMBAT || this->state == Canvas::ST_PLAYING) {
		this->m_swipeArea[this->m_controlMode]->Render(graphics);
	}

	if (((this->repaintFlags & Canvas::REPAINT_HUD) != 0) && (this->state != Canvas::ST_MENU)) { // REPAINT_HUD
		//this->repaintFlags &= ~Canvas::REPAINT_HUD; java, brew only
		app->hud->draw(graphics);
	}

	if (app->player->inTargetPractice) {
		this->drawTargetPracticeScore(graphics);
	}

	if (this->state == Canvas::ST_INTRO_MOVIE) {
		this->playIntroMovie(graphics);
	}
	else if (this->state == Canvas::ST_CHARACTER_SELECTION) {
		this->drawCharacterSelection(graphics);
	}
	else if (this->state == Canvas::ST_INTRO) {
		this->drawStory(graphics);
	}
	else if (this->state == Canvas::ST_EPILOGUE) {
		this->drawScrollingText(graphics);
	}
	else if (this->state == Canvas::ST_CREDITS) {
		this->drawCredits(graphics);
	}
	else if (this->state == Canvas::ST_TRAVELMAP) {
		this->drawTravelMap(graphics);
	}
	else if (this->state == Canvas::ST_AUTOMAP) {
		this->drawAutomap(graphics, !this->automapDrawn);
		this->m_softKeyButtons->Render(graphics);
		app->hud->drawArrowControls(graphics);
		this->automapDrawn = true;
	}
	else if (this->state == Canvas::ST_DIALOG) {
		this->dialogState(graphics);
	}
	else if (this->state == Canvas::ST_MINI_GAME) {
		switch (this->stateVars[0]) {
			case 2: {
				this->repaintFlags &= ~Canvas::REPAINT_HUD;
				app->hackingGame->updateGame(graphics);
				break;
			}
			case 0: {
				app->sentryBotGame->updateGame(graphics);
				break;
			}
			case 4: {
				app->vendingMachine->updateGame(graphics);
				break;
			}
		}
	}
	else if (this->state == Canvas::ST_ERROR) {
		//this->errorState(graphics);
	}
	else if (this->state == Canvas::ST_LOOTING) {
		this->drawLootingMenu(graphics);
	}
	else if (this->state == Canvas::ST_TREADMILL) {
		this->drawTreadmillReadout(graphics);
	}

	/*if ((this->repaintFlags & Canvas::REPAINT_SOFTKEYS) != 0x0) { // REPAINT_SOFTKEYS
		this->repaintFlags &= ~Canvas::REPAINT_SOFTKEYS;
		this->drawSoftKeys(graphics);
	}*/

	if (this->repaintFlags & Canvas::REPAINT_MENU) {
		this->repaintFlags &= ~Canvas::REPAINT_MENU;
		app->menuSystem->paint(graphics);
	}

	if (this->repaintFlags & Canvas::REPAINT_STARTUP_LOGO) {
		this->repaintFlags &= ~Canvas::REPAINT_STARTUP_LOGO;
		graphics->fillRect(0, 0, this->displayRect[2], this->displayRect[3], -0x1000000);
		graphics->drawImage(this->imgStartupLogo, this->displayRect[2] / 2, this->displayRect[3] / 2, 3, 0, 0);
	}

	//printf("this->repaintFlags %d\n", this->repaintFlags);
	if (this->repaintFlags & Canvas::REPAINT_LOADING_BAR) { // REPAINT_LOADING_BAR
		this->repaintFlags &= ~Canvas::REPAINT_LOADING_BAR;
		this->drawLoadingBar(graphics);
	}

	if (this->fadeFlags && (app->time < this->fadeTime + this->fadeDuration)) {
		int alpha = ((app->time - this->fadeTime) << 8) / this->fadeDuration;

		if ((this->fadeFlags & Canvas::FADE_FLAG_FADEOUT) != 0) {
			alpha = 256 - alpha;
		}

		graphics->fade(this->fadeRect, alpha, this->fadeColor);
	}
	else {
		this->fadeFlags = Canvas::FADE_FLAG_NONE;
	}

	if (this->state == Canvas::ST_BENCHMARK) {
		if (this->st_enabled) {
			int n = this->viewRect[1];
			this->debugTime = app->upTimeMs;
			Text* largeBuffer = app->localization->getLargeBuffer();
			largeBuffer->setLength(0);
			largeBuffer->append("Rndr ms: ");
			largeBuffer->append(this->st_fields[0] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[0] * 100 / this->st_count - this->st_fields[0] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Bsp ms: ");
			largeBuffer->append(this->st_fields[1] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[1] * 100 / this->st_count - this->st_fields[1] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Hud ms: ");
			largeBuffer->append(this->st_fields[2] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[2] * 100 / this->st_count - this->st_fields[2] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			int n2 = this->st_fields[4] + this->st_fields[5];
			largeBuffer->setLength(0);
			largeBuffer->append("Blit ms: ");
			largeBuffer->append(n2 / this->st_count)->append('.');
			largeBuffer->append(n2 * 100 / this->st_count - n2 / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Paus ms: ");
			largeBuffer->append(this->st_fields[6] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[6] * 100 / this->st_count - this->st_fields[6] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Dbg ms: ");
			largeBuffer->append(this->st_fields[9] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[9] * 100 / this->st_count - this->st_fields[9] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Loop ms: ");
			largeBuffer->append(this->st_fields[7] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[7] * 100 / this->st_count - this->st_fields[7] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Key ms: ");
			largeBuffer->append(this->st_fields[11] - this->st_fields[10]);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("State ms: ");
			largeBuffer->append(this->st_fields[12] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[12] * 100 / this->st_count - this->st_fields[12] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append("Totl ms: ");
			largeBuffer->append(this->st_fields[8] / this->st_count)->append('.');
			largeBuffer->append(this->st_fields[8] * 100 / this->st_count - this->st_fields[8] / this->st_count * 100);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			largeBuffer->setLength(0);
			largeBuffer->append(this->st_count);
			graphics->drawString(largeBuffer, this->viewRect[0], n, 0);
			n += 16;
			this->debugTime = app->upTimeMs - this->debugTime;
			largeBuffer->dispose();
		}
	}
	else if (this->state == Canvas::ST_CAMERA || this->state == Canvas::ST_PLAYING || this->state == Canvas::ST_COMBAT || this->state == Canvas::ST_INTER_CAMERA) {
		int n3 = this->viewRect[1];
		if (this->showSpeeds) {
			int lastRenderTime = this->afterRender - this->beforeRender;
			if (this->lastFrameTime == app->time) {
				this->afterRender = (this->beforeRender = 0);
				this->lastRenderTime = lastRenderTime;
			}
			int n4 = app->time - this->totalFrameTime;
			this->totalFrameTime = app->time;
			Text* largeBuffer2 = app->localization->getLargeBuffer();
			largeBuffer2->setLength(0);
			largeBuffer2->append("ms: ");
			largeBuffer2->append(this->lastRenderTime)->append('/');
			largeBuffer2->append(app->render->clearColorBuffer)->append('/');
			largeBuffer2->append(app->render->bltTime)->append('/');
			largeBuffer2->append(n4);
			graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
			n3 += 16;
			largeBuffer2->setLength(0);
			largeBuffer2->append("li: ");
			largeBuffer2->append(app->render->lineRasterCount)->append('/');
			largeBuffer2->append(app->render->lineCount);
			graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
			n3 += Applet::FONT_HEIGHT[app->fontType];
			largeBuffer2->setLength(0);
			largeBuffer2->append("sp: ");
			largeBuffer2->append(app->render->spriteRasterCount)->append('/');
			largeBuffer2->append(app->render->spriteCount)->append('/');
			largeBuffer2->append(app->render->numMapSprites);
			graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
			n3 += Applet::FONT_HEIGHT[app->fontType];
			if (app->render->renderMode == 63) {
				largeBuffer2->setLength(0);
				largeBuffer2->append("cnt: ");
				largeBuffer2->append(app->tinyGL->spanCalls)->append('/');
				largeBuffer2->append(app->tinyGL->spanPixels);
				graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
				n3 += Applet::FONT_HEIGHT[app->fontType];
				largeBuffer2->setLength(0);
				largeBuffer2->append("tris: ");
				largeBuffer2->append(app->tinyGL->countBackFace)->append('/');
				largeBuffer2->append(app->tinyGL->countDrawn);
				graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
				n3 += Applet::FONT_HEIGHT[app->fontType];
			}
			largeBuffer2->setLength(0);
			largeBuffer2->append("OSTime: ");
			int v30 = 0;
			for (int i = 0; i < 8; i++) {
				v30 += app->osTime[i];
			}
			largeBuffer2->append(v30 / 8);
			graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
			n3 += Applet::FONT_HEIGHT[app->fontType];
			largeBuffer2->setLength(0);
			largeBuffer2->append("Code: ");
			int v31 = 0;
			for (int i = 0; i < 8; i++) {
				v31 += app->codeTime[i];
			}
			largeBuffer2->append(v31 / 8);
			graphics->drawString(largeBuffer2, this->viewRect[0], n3, 0);
			largeBuffer2->dispose();
			n3 += Applet::FONT_HEIGHT[app->fontType];
		}

		if (this->showLocation) {
			Text* smallBuffer = app->localization->getSmallBuffer();
			smallBuffer->setLength(0);
			smallBuffer->append(this->viewX >> 6);
			smallBuffer->append(' ');
			smallBuffer->append(this->viewY >> 6);
			smallBuffer->append(' ');
			int angle = this->viewAngle & 0x3FF;
			if (angle == Enums::ANGLE_NORTH) {
				smallBuffer->append('N');
			}
			else if (angle == Enums::ANGLE_EAST) {
				smallBuffer->append('E');
			}
			else if (angle == Enums::ANGLE_SOUTH) {
				smallBuffer->append('S');
			}
			else if (angle == Enums::ANGLE_WEST) {
				smallBuffer->append('W');
			}
			else if (angle == Enums::ANGLE_NORTHEAST) {
				smallBuffer->append("NE");
			}
			else if (angle == Enums::ANGLE_NORTHWEST) {
				smallBuffer->append("NW");
			}
			else if (angle == Enums::ANGLE_SOUTHEAST) {
				smallBuffer->append("SE");
			}
			else if (angle == Enums::ANGLE_SOUTHWEST) {
				smallBuffer->append("SW");
			}
			graphics->drawString(smallBuffer, this->viewRect[0], n3, 0);
			smallBuffer->dispose();
		}
	}

	graphics->resetScreenSpace();
	if (this->showFreeHeap) {
		graphics->setColor(0xFF000000);
		graphics->fillRect(this->viewRect[0], this->viewRect[1] + this->viewRect[3] - 18, this->viewRect[2], 16);
		switch (this->updateChar) {
		case '*': {
			this->updateChar = '+';
			break;
		}
		case '+': {
			this->updateChar = '%';
			break;
		}
		case '%': {
			this->updateChar = '#';
			break;
		}
		case '#': {
			this->updateChar = '*';
			break;
		}
		}
		Text *largeBuffer3 = app->localization->getLargeBuffer();
		if (largeBuffer3 != nullptr) {
			largeBuffer3->append(this->updateChar);
			largeBuffer3->append(" ");
			largeBuffer3->append(1000000000/*App.getFreeMemory()*/);
			graphics->drawString(largeBuffer3, this->SCR_CX, this->viewRect[1] + this->viewRect[3] - 16, 17);
			largeBuffer3->setLength(0);
			for (int i = 0; i < 10; ++i) {
				largeBuffer3->append(app->game->numLevelLoads[i]);
				largeBuffer3->append('|');
			}
			graphics->drawString(largeBuffer3, this->viewRect[0], this->viewRect[1], 4);
			largeBuffer3->setLength(0);
			int t = app->game->totalPlayTime + (app->upTimeMs - app->game->lastSaveTime) / 1000; //[GEC] hace que el tiempo avance
			int n6 = t / 3600;
			int n7 = (t - n6 * 3600) / 60;
			int n8 = (t - n6 * 3600) - n7 * 60;
			largeBuffer3->append(n6);
			largeBuffer3->append(':');
			if (n7 < 10) {
				largeBuffer3->append(0);
			}
			largeBuffer3->append(n7);
			largeBuffer3->append(':');
			if (n8 < 10) {
				largeBuffer3->append(0);
			}
			largeBuffer3->append(n8);
			graphics->drawString(largeBuffer3, this->viewRect[0] + this->viewRect[2] - 3, this->viewRect[1], 8);
			largeBuffer3->dispose();
		}
	}
}

void Canvas::run() {
	Applet* app = CAppContainer::getInstance()->app;
	app->CalcAccelerometerAngles();

	int upTimeMs = app->upTimeMs;
	app->lastTime = app->time;
	app->time = upTimeMs;

	if (this->st_enabled != false) {
		this->st_count = this->st_count + 1;
		this->st_fields[0] = this->st_fields[0] + app->render->frameTime;
		this->st_fields[1] = this->st_fields[1] + app->render->bspTime;
		this->st_fields[2] = this->st_fields[2] + app->hud->drawTime;
		this->st_fields[4] = this->st_fields[4] + app->render->bltTime;
		this->st_fields[5] = (this->pauseTime - this->flushTime) + this->st_fields[5];
		this->st_fields[6] = (this->loopEnd - this->pauseTime) + this->st_fields[6];
		this->st_fields[8] = (app->time - app->lastTime) + this->st_fields[8];
		this->st_fields[3] = this->st_fields[3] + app->combat->renderTime;
		this->st_fields[9] = this->st_fields[9] + this->debugTime;
		this->st_fields[7] = (this->loopEnd - this->loopStart) + this->st_fields[7];
		upTimeMs = app->upTimeMs;
	}
	this->loopStart = upTimeMs;

	if (!app->game->pauseGameTime && this->state != Canvas::ST_MENU) {
		app->gameTime += app->time - app->lastTime;
	}

	if (this->vibrateTime && this->vibrateTime < app->time) {
		this->vibrateTime = 0;
	}

	if (this->state != Canvas::ST_DIALOG && this->state != Canvas::ST_MENU && app->game->isInputBlockedByScript()) {
		this->clearEvents(1);
	}

	this->runInputEvents();

	// [GEC]
	if (this->repaintFlags & Canvas::REPAINT_VIEW3D) { // REPAINT_VIEW3D
		if (!app->render->_gles->isInit) {
			app->render->Render3dScene();
			app->tinyGL->applyClearColorBuffer();
		}
	}

	if ((this->state != Canvas::ST_MENU) || (app->menuSystem->menu != Menus::MENU_ENABLE_SOUNDS)) {
		app->game->numTraceEntities = 0;
		app->game->UpdatePlayerVars();
		app->game->gsprite_update(app->time);
		app->game->runScriptThreads(app->gameTime);
	}

	//printf("lastTime %d\n", app->lastTime);
	//printf("time %d\n", app->time);
	//printf("this->loopStart %d\n", this->loopStart);
	int time = app->upTimeMs;
	app->game->updateAutomap = false;
	//printf("this->state %d\n", this->state);

	if (this->state == Canvas::ST_PLAYING) {

		if (this->m_controlButton) {
			this->handleEvent(this->m_controlButton->buttonID);
		}

		app->game->updateAutomap = true;
		if (this->dialogSystem.numHelpMessages == 0 && app->game->queueAdvanceTurn) {
			app->game->snapMonsters(true);
			app->game->advanceTurn();
		}

		this->playingState();
		if (this->state == Canvas::ST_PLAYING) {
			this->repaintFlags |= Canvas::REPAINT_HUD;
			app->hud->repaintFlags |= 0x2f;
		}
	}
	else if (this->state == Canvas::ST_INTER_CAMERA) {
		this->repaintFlags |= Canvas::REPAINT_HUD;
		app->hud->repaintFlags |= 0x2B;
	}
	else if (this->state != Canvas::ST_CREDITS && this->state != Canvas::ST_TRAVELMAP && this->state != Canvas::ST_MIXING) {
		if (this->state != Canvas::ST_MINI_GAME) {
			if (this->state == Canvas::ST_COMBAT) {
				app->game->updateAutomap = true;
				this->combatState();
			}
			else if (this->state == Canvas::ST_INTRO_MOVIE) {
				if ((app->game->hasSeenIntro && this->numEvents != 0) || this->introSequenceManager.scrollingTextDone) {
					this->dialogBuffer->dispose();
					this->dialogBuffer = nullptr;
					app->game->hasSeenIntro = true;
					app->game->saveConfig();
					this->backToMain(false);
				}
			}
			else if (this->state == Canvas::ST_EPILOGUE) {
				if (this->introSequenceManager.scrollingTextDone) {
					this->disposeEpilogue();
				}
			}
			else if (this->state != Canvas::ST_CHARACTER_SELECTION) {
				if (this->state == Canvas::ST_INTRO) {
					if (this->introSequenceManager.storyPage >= this->introSequenceManager.storyTotalPages) {
						this->disposeIntro();
					}
				}
				else if (this->state == Canvas::ST_SAVING) {
					if ((this->saveType & 0x2) != 0x0 || (this->saveType & 0x1) != 0x0) {
						if ((this->saveType & 0x8) != 0x0) {
							if (app->game->spawnParam != 0) {
								int n11 = 32 + ((app->game->spawnParam & 0x1F) << 6);
								int n12 = 32 + ((app->game->spawnParam >> 5 & 0x1F) << 6);
								app->game->saveState(this->lastMapID, this->loadMapID, n11, n12, (app->game->spawnParam >> 10 & 0xFF) << 7, 0, n11, n12, n11, n12, 36, 0, 0, this->saveType);
							}
							else {
								app->game->saveState(this->lastMapID, this->loadMapID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, this->saveType);
							}
						}
						else if ((this->saveType & 0x10) != 0x0) {
							int n13 = 32 + ((app->game->spawnParam & 0x1F) << 6);
							int n14 = 32 + ((app->game->spawnParam >> 5 & 0x1F) << 6);
							app->game->saveState(this->loadMapID, app->menuSystem->LEVEL_STATS_nextMap, n13, n14, (app->game->spawnParam >> 10 & 0xFF) << 7, 0, n13, n14, n13, n14, 36, 0, 0, this->saveType);
						}
						else {
							app->game->saveState(this->loadMapID, this->loadMapID, this->destX, this->destY, this->destAngle, this->viewPitch, this->prevX, this->prevY, this->saveX, this->saveY, this->saveZ, this->saveAngle, this->savePitch, this->saveType);
						}
						app->hud->addMessage((short)0, (short)38);
					}
					else {
						app->Error(48); // ERR_SAVESTATE
					}

					if ((this->saveType & 0x4) != 0x0) {
						this->backToMain(false);
					}
					else if ((this->saveType & 0x40) != 0x0) {
						app->shutdown();
					}
					else if ((this->saveType & 0x8) != 0x0) {
						this->setState(Canvas::ST_TRAVELMAP);
					}
					else if ((this->saveType & 0x10) != 0x0) {
						app->sound->playSound(1069, 1u, 3, saveType & 8);
						app->menuSystem->setMenu(Menus::MENU_LEVEL_STATS);
					}
					else if ((this->saveType & 0x100) != 0x0) {
						app->menuSystem->setMenu(Menus::MENU_END_FINALQUIT);
					}
					else {
						if ((this->saveType & 0x80) != 0x0) {
							app->menuSystem->returnToGame();
						}
						this->setState(Canvas::ST_PLAYING);
					}
					this->saveType = 0;
					this->clearEvents(1);
				}
				else if (this->state == Canvas::ST_LOADING) {
					if (this->loadType == 0) {
						if (!this->loadMedia()) {
							this->flushGraphics();
							return;
						}
					}
					else {
						app->game->loadState(this->loadType);
						app->hud->addMessage((short)0, (short)39);
						this->loadType = 0;
					}
				}
				else if (this->state == Canvas::ST_MENU) {
					this->menuState();
				}
				else if (this->state == Canvas::ST_DIALOG) {
					app->game->updateLerpSprites();
					this->updateView();
					this->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_PARTICLES);
					app->hud->repaintFlags |= 0x2B;
					//app->hud->repaintFlags &= 0xFFFFFFBF;
				}
				else if (this->state == Canvas::ST_AUTOMAP) {
					if (this->m_controlButton) {
						if (app->gameTime > this->m_controlButtonTime) {
							this->m_controlButtonTime = app->gameTime + 250;
							this->handleEvent(this->m_controlButton->buttonID);
						}
					}
					app->game->updateAutomap = true;
					this->automapState();
				}
				else if (this->state == Canvas::ST_DYING) {
					this->dyingState();
				}
				else if (this->state == Canvas::ST_LOOTING) {
					this->lootingState();
				}
				else if (this->state == Canvas::ST_TREADMILL) {
					this->treadmillState();
				}
				else if (this->state == Canvas::ST_BOT_DYING) {
					this->familiarDyingState();
				}
				else if (this->state == Canvas::ST_CAMERA) {
					if (app->game->activeCameraKey != -1) {
						app->game->activeCamera->Update(app->game->activeCameraKey, app->gameTime - app->game->activeCameraTime);
					}
					app->game->updateLerpSprites();
					this->updateView();
					if (this->state == Canvas::ST_CAMERA && app->gameTime > app->game->cinUnpauseTime && this->softKeyRightID == -1) {
						this->clearLeftSoftKey();
						this->setRightSoftKey((short)0, (short)40);
					}
				}
				else if (this->state == Canvas::ST_LOGO) {
					this->logoState();
				}
				else if (this->state == Canvas::ST_BENCHMARK) {
					this->renderOnlyState();
				}
				else {
					app->Error(51); // ERR_STATE
				}
			}
		}
	}

	if (this->state == Canvas::ST_SAVING || this->state == Canvas::ST_LOADING) {
		this->repaintFlags &= ~Canvas::REPAINT_VIEW3D;
	}
	this->st_fields[12] = app->upTimeMs - time;

	this->flushTime = app->upTimeMs;
	this->graphics.resetScreenSpace();
	this->backPaint(&this->graphics);
	if (this->keyPressedTime != 0) {
		this->lastKeyPressedTime = app->upTimeMs - this->keyPressedTime;
		this->keyPressedTime = 0;
	}
	this->pauseTime = app->upTimeMs;
	this->loopEnd = app->upTimeMs;
	this->stateChanged = false;

	if (this->sysSoundDelayTime > 0 && this->sysSoundDelayTime < app->upTimeMs - this->sysSoundTime) {
		app->sound->playSound((app->nextByte() & 0xF) + 1000, 0, 3, 0);
		this->sysSoundTime = app->upTimeMs;
	}
}

void Canvas::clearEvents(int ignoreFrameInput) { this->inputEventController.clearEvents(ignoreFrameInput); }

void Canvas::loadRuntimeData() { this->loadingManager.loadRuntimeData(); }
void Canvas::freeRuntimeData() { this->loadingManager.freeRuntimeData(); }

void Canvas::startShake(int i, int i2, int i3) {
	Applet* app = CAppContainer::getInstance()->app;
	SDLGL* sdlGL = CAppContainer::getInstance()->sdlGL;

	if (app->game->skippingCinematic) {
		return;
	}

	if (i2 != 0) {
		this->shakeTime = app->time + i;
		this->shakeIntensity = 2 * i2;
		this->staleTime += 1;
	}

	if (i3 != 0 && this->vibrateEnabled) {
		controllerVibrate(i3); // [GEC]
		this->vibrateTime = i3 + app->upTimeMs;
	}
}

void Canvas::setState(int state) {
	Applet* app = CAppContainer::getInstance()->app;

	this->stateChanged = true;
	for (int i = 0; i < 9; ++i) {
		this->stateVars[i] = 0;
	}
	this->m_controlButtonIsTouched = false;
	this->m_controlButton = 0;

	if (this->state == Canvas::ST_AUTOMAP) {
		app->player->unpause(app->time - this->automapTime);
	}
	else if (this->state == Canvas::ST_MENU) {
		app->player->unpause(app->time - app->menuSystem->startTime);
		app->menuSystem->clearStack();
	}
	else if (this->state == Canvas::ST_CAMERA) {
		app->render->disableRenderActivate = false;
		app->game->skippingCinematic = false;
	}
	else if (this->state == Canvas::ST_COMBAT && state != Canvas::ST_COMBAT && app->combat->stage != Canvas::ST_MENU) {
		app->combat->cleanUpAttack();
	}
	else if (this->state == Canvas::ST_CREDITS) {
		this->dialogBuffer->dispose();
		this->dialogBuffer = nullptr;
	}
	else if (this->state == Canvas::ST_INTER_CAMERA) {
	}


	this->oldState = this->state;
	this->state = state;

	//printf("state %d\n", state);
	if (state == Canvas::ST_COMBAT) {
		app->hud->repaintFlags = 47;
		this->repaintFlags |= Canvas::REPAINT_HUD;
		this->clearSoftKeys();
		this->combatDone = false;
	}
	else if (state == Canvas::ST_TRAVELMAP) {
		this->travelMapManager.init();
	}
	else if (state == Canvas::ST_PLAYING) {
		app->hud->repaintFlags = 0x2f;
		this->repaintFlags |= Canvas::REPAINT_HUD;
		app->game->lastTurnTime = app->time;
		if (app->game->monstersTurn == 0 || this->oldState == Canvas::ST_CAMERA) {
			this->drawPlayingSoftKeys();
		}
		if (this->oldState == Canvas::ST_COMBAT && app->combat->curTarget != nullptr && app->combat->curTarget->def->eType == 3) {
			app->game->executeStaticFunc(7);
		}
		this->updateFacingEntity = true;
		if (this->oldState != Canvas::ST_COMBAT && this->oldState != Canvas::ST_DIALOG) {
			this->invalidateRect();
		}
	}
	else if (state == Canvas::ST_INTER_CAMERA) {
		app->hud->repaintFlags = 43;
		this->repaintFlags |= Canvas::REPAINT_HUD;
	}
	else if (state == Canvas::ST_DIALOG) {
		if (app->canvas->isZoomedIn) {
			this->isZoomedIn = 0;
			app->StopAccelerometer();
			this->destAngle = this->viewAngle = (this->viewAngle + this->zoomAngle + 127) & 0xFFFFFF00;
			app->tinyGL->resetViewPort();
			this->drawPlayingSoftKeys();
		}
		if (app->game->isCameraActive()) {
			app->game->activeCameraTime = app->gameTime - app->game->activeCameraTime;
		}
		app->hud->repaintFlags = 47;
		this->repaintFlags |= Canvas::REPAINT_HUD;
		app->tinyGL->resetViewPort();
		//this->clearLeftSoftKey();
		/*this->setRightSoftKey((short)0, (short)40);
		int n2 = 0;
		if (this->dialogStyle == 2 || this->dialogStyle == 16 || this->dialogStyle == 9 || (this->dialogStyle == 4 && this->dialogItem != nullptr)) {
			n2 = 1;
		}
		if (this->numDialogLines - n2 > this->dialogViewLines) {
			this->setLeftSoftKey((short)0, (short)125);
		}
		else {
			//this->clearLeftSoftKey();
		}*/
		this->clearSoftKeys();
		this->clearEvents(1);
	}
	else if (state == Canvas::ST_DYING) {
		app->hud->repaintFlags = 47;
		this->repaintFlags |= Canvas::REPAINT_HUD;
		if (this->isZoomedIn) {
			this->isZoomedIn = false;
			app->StopAccelerometer();
			this->viewAngle += this->zoomAngle;
			int n3 = 255;
			this->destAngle = (this->viewAngle = (this->viewAngle + (n3 >> 1) & ~n3));
			app->tinyGL->resetViewPort();
			this->drawPlayingSoftKeys();
		}
		this->clearSoftKeys();
		this->deathTime = app->time;
		this->destPitch = 64;
		this->dialogSystem.numHelpMessages = 0;
	}
	else if (state == Canvas::ST_BOT_DYING) {
		app->hud->repaintFlags = 47;
		this->repaintFlags |= Canvas::REPAINT_HUD;
		this->clearSoftKeys();
		this->familiarDeathTime = app->time;
		this->selfDestructScreenShakeStarted = false;
		app->hud->brightenScreen(100, 0);
		if (!this->familiarSelfDestructed) {
			app->hud->smackScreen(100);
		}
		this->destPitch = 64;
	}
	else if (state == Canvas::ST_LOOTING) {
		this->lootingSystem.onEnterLooting(this->destPitch);
	}
	else if (state == Canvas::ST_TREADMILL) {
		this->clearSoftKeys();
		app->combat->shiftWeapon(true);
		app->hud->repaintFlags |= 0x20;
		this->repaintFlags |= Canvas::REPAINT_HUD;
		this->miniGameManager.onEnterTreadmill();
	}
	else if (state == Canvas::ST_EPILOGUE) {
		app->player->levelGrade(true);
		bool soundEnabled = app->sound->allowSounds;
		app->sound->allowSounds = app->canvas->areSoundsAllowed;
		app->sound->allowSounds = soundEnabled;
		this->clearSoftKeys();
		this->loadEpilogueText();
		this->stateVars[0] = 1;
	}
	else if (state == Canvas::ST_CREDITS) {
		app->localization->loadText(2);
		this->initScrollingText(2, 0, false, 16, 5, 500);
		app->localization->unloadText(2);
	}
	else if (state == Canvas::ST_CHARACTER_SELECTION) {
		this->clearSoftKeys();
		this->setupCharacterSelection();
		this->stateVars[0] = 1;
		this->stateVars[1] = 0;
		this->stateVars[2] = 1;
		this->stateVars[8] = 0; // [GEC]
	}
	else if (state == Canvas::ST_INTRO) {
		this->clearSoftKeys();
		this->loadPrologueText();
		this->stateVars[0] = 1;
	}
	else if (state == Canvas::ST_INTRO_MOVIE) {
		this->initScrollingText((short)0, (short)133, 0, 32, 1, 800);
		this->stateVars[0] = 1;
	}
	else if (state == Canvas::ST_LOADING || state == Canvas::ST_SAVING) {
		this->repaintFlags &= ~Canvas::REPAINT_HUD;
		this->pacifierX = this->SCR_CX - 66;
		this->updateLoadingBar(false);
	}
	else if (state == Canvas::ST_AUTOMAP) {
		this->automapDrawn = false;
		this->automapTime = app->time;
	}
	else if (state == Canvas::ST_MENU) {
		app->menuSystem->startTime = app->time;
		/*if (Canvas.oldState == Canvas::ST_PLAYING) {
			Sound.playSound(3);
		}
		else if (Canvas.oldState == Canvas::ST_MIXING) {
			MenuSystem.goBackToStation = true;
		}*/

		if (this->oldState != Canvas::ST_MENU) {
			this->clearEvents(1);
		}
		app->beginImageLoading();
		app->endImageLoading();
	}
	else if (state == Canvas::ST_CAMERA) {
		app->hud->msgCount = 0;
		app->hud->subTitleID = -1;
		app->hud->cinTitleID = -1;
		app->render->disableRenderActivate = true;
		this->repaintFlags |= Canvas::REPAINT_HUD; // j2me 0x11;
		app->hud->repaintFlags = 24;
		this->clearSoftKeys();
		app->tinyGL->setViewport(this->cinRect[0], this->cinRect[1], this->cinRect[2], this->cinRect[3]);
	}
}

void Canvas::setAnimFrames(int animFrames) { this->movementController.setAnimFrames(animFrames); }

void Canvas::checkFacingEntity() { this->movementController.checkFacingEntity(); }

void Canvas::finishMovement() { this->movementController.finishMovement(); }

int Canvas::flagForWeapon(int i) { return this->movementController.flagForWeapon(i); }
int Canvas::flagForFacingDir(int i) { return this->movementController.flagForFacingDir(i); }

void Canvas::startRotation(bool b) { this->movementController.startRotation(b); }
void Canvas::finishRotation(bool b) { this->movementController.finishRotation(b); }

int Canvas::getKeyAction(int i) { return this->inputEventController.getKeyAction(i); }

bool Canvas::attemptMove(int n, int n2) { return this->movementController.attemptMove(n, n2); }

void Canvas::loadState(int loadType, short n, short n2) { this->loadingManager.loadState(loadType, n, n2); }
void Canvas::saveState(int saveType, short n, short n2) { this->loadingManager.saveState(saveType, n, n2); }
void Canvas::loadMap(int loadMapID, bool b, bool tm_NewGame) { this->loadingManager.loadMap(loadMapID, b, tm_NewGame); }

void Canvas::loadPrologueText() { this->introSequenceManager.loadPrologueText(); }
void Canvas::loadEpilogueText() { this->introSequenceManager.loadEpilogueText(); }
void Canvas::setupCharacterSelection() { this->introSequenceManager.setupCharacterSelection(); }
void Canvas::disposeIntro() { this->introSequenceManager.disposeIntro(); }
void Canvas::disposeEpilogue() { this->introSequenceManager.disposeEpilogue(); }

void Canvas::loadMiniGameImages() { this->loadingManager.loadMiniGameImages(); }

void Canvas::drawScroll(Graphics* graphics, int n, int n2, int n3, int n4) { this->introSequenceManager.drawScroll(graphics, n, n2, n3, n4); }
void Canvas::initScrollingText(short i, short i2, bool dehyphenate, int spacingHeight, int numLines, int textMSLine) { this->introSequenceManager.initScrollingText(i, i2, dehyphenate, spacingHeight, numLines, textMSLine); }
void Canvas::drawCredits(Graphics* graphics) { this->introSequenceManager.drawCredits(graphics); }
void Canvas::drawScrollingText(Graphics* graphics) { this->introSequenceManager.drawScrollingText(graphics); }

void Canvas::handleDialogEvents(int key) { this->dialogSystem.handleDialogEvents(key); }

bool Canvas::handlePlayingEvents(int key, int action) { return this->playingInputHandler.handlePlayingEvents(key, action); }

bool Canvas::handleCinematicInput(int action) { return this->playingInputHandler.handleCinematicInput(action); }
bool Canvas::shouldFakeCombat(int n, int n2, int n3) { return this->playingInputHandler.shouldFakeCombat(n, n2, n3); }
bool Canvas::endOfHandlePlayingEvent(int action, bool b) { return this->playingInputHandler.endOfHandlePlayingEvent(action, b); }

bool Canvas::handleEvent(int key) { return this->inputEventController.handleEvent(key); }

void Canvas::runInputEvents() { this->inputEventController.runInputEvents(); }

bool Canvas::loadMedia() { return this->loadingManager.loadMedia(); }

void Canvas::combatState() { this->gameStateRunner.combatState(); }

void Canvas::dialogState(Graphics* graphics) { this->dialogSystem.dialogState(graphics); }

void Canvas::automapState() { this->automapController.automapState(); }

void Canvas::renderOnlyState() { this->gameStateRunner.renderOnlyState(); }

void Canvas::playingState() { this->gameStateRunner.playingState(); }

void Canvas::menuState() { this->gameStateRunner.menuState(); }

void Canvas::dyingState() { this->gameStateRunner.dyingState(); }

void Canvas::familiarDyingState() { this->gameStateRunner.familiarDyingState(); }

void Canvas::logoState() { this->gameStateRunner.logoState(); }

void Canvas::drawScrollBar(Graphics* graphics, int i, int i2, int i3, int i4, int i5, int i6, int i7)
{
	bool v9; // cc
	int v12; // r1
	Canvas* v14; // [sp+20h] [bp-24h]
	int v15; // [sp+24h] [bp-20h]
	int v16; // [sp+28h] [bp-1Ch]

	v14 = this;
	v9 = i7 < 0;
	if (i7)
		v9 = i7 < i6;
	if (v9)
	{
		v12 = i6 - i7;
		if (i6 - i7 < i4)
			v12 = i4;
		v15 = 3 * i3 / (4 * ((i7 + i6 - 1) / i7));
		v16 = ((i4 << 16) / (v12 << 8) * ((i3 - v15 - 14) << 8)) >> 16;
		if (i6 == i5)
			v16 = i3 - 3 * i3 / (4 * ((i7 + i6 - 1) / i7)) - 14;
		graphics->drawRegion(this->imgUIImages, 60, 0, 7, 7, i, i2, 24, 0, 0);
		graphics->drawRegion(v14->imgUIImages, 60, 7, 7, 7, i, i3 + i2, 40, 0, 0);
		graphics->setColor(-5002605);
		graphics->fillRect(i - 7, i2 + 7, 7, i3 - 14);
		graphics->setColor(-1585235);
		graphics->fillRect(i - 7, v16 + 7 + i2, 7, v15);
		graphics->setColor(-16777216);
		graphics->drawRect(i - 7, v16 + 7 + i2, 6, v15 - 1);
		graphics->drawRect(i - 7, i2, 6, i3 - 1);
	}
}

void Canvas::uncoverAutomap() { this->automapController.uncoverAutomap(); }

void Canvas::drawAutomap(Graphics* graphics, bool b) { this->automapController.drawAutomap(graphics, b); }

void Canvas::closeDialog(bool skipDialog) { this->dialogSystem.closeDialog(skipDialog); }

void Canvas::prepareDialog(Text* text, int dialogStyle, int dialogFlags) { this->dialogSystem.prepareDialog(text, dialogStyle, dialogFlags); }

void Canvas::startDialog(ScriptThread* scriptThread, short n, int n2, int n3) { this->dialogSystem.startDialog(scriptThread, n, n2, n3); }
void Canvas::startDialog(ScriptThread* scriptThread, short n, short n2, int n3, int n4, bool b) { this->dialogSystem.startDialog(scriptThread, n, n2, n3, n4, b); }
void Canvas::startDialog(ScriptThread* scriptThread, Text* text, int n, int n2) { this->dialogSystem.startDialog(scriptThread, text, n, n2); }
void Canvas::startDialog(ScriptThread* dialogThread, Text* text, int n, int n2, bool dialogResumeScriptAfterClosed) { this->dialogSystem.startDialog(dialogThread, text, n, n2, dialogResumeScriptAfterClosed); }

void Canvas::renderScene(int viewX, int viewY, int viewZ, int viewAngle, int viewPitch, int viewRoll, int viewFov) {
	Applet* app = CAppContainer::getInstance()->app;

	{ // J2ME
		//this->staleView = true;
		//if (!this->staleView && (this->staleTime == 0 || app->time < this->staleTime)) {
			//return;
		//}
	}

	this->staleView = false;
	this->staleTime = 0;
	this->lastFrameTime = app->time;
	this->beforeRender = app->upTimeMs;
	app->render->render((viewX << 4) + 8, (viewY << 4) + 8, (viewZ << 4) + 8, viewAngle, viewPitch, viewRoll, viewFov);
	this->afterRender = app->upTimeMs;
	++this->renderSceneCount;
	app->render->renderPortal();
	if (!this->isZoomedIn) {
		app->combat->drawWeapon(this->shakeX, this->shakeY);
	}
	if (app->render->postProcessMode != 0) {
		app->render->postProcessView(&this->graphics);
	}

	this->repaintFlags |= Canvas::REPAINT_VIEW3D;
}

void Canvas::startSpeedTest(bool b) {
	this->renderOnly = true;
	this->st_enabled = true;
	this->st_count = 1;
	for (int i = 0; i < Canvas::SPD_NUM_FIELDS; ++i) {
		this->st_fields[i] = 0;
	}
	if (!b) {
		this->animAngle = 4;
		this->destAngle = this->viewAngle;
		this->setState(Canvas::ST_BENCHMARK);
	}
}

void Canvas::backToMain(bool b) {
	Applet* app = CAppContainer::getInstance()->app;

	this->loadMapID = 0;
	app->freeRuntimeImages();
	app->player->reset();
	app->game->unloadMapData();
	app->render->unloadMap();
	app->render->endFade();

	delete app->menuSystem->imgMainBG;
	app->menuSystem->imgMainBG = app->loadImage("logo.bmp", true);

	if (b) {
		this->clearEvents(1);
		if (this->skipIntro) {
			app->player->reset();
			app->render->unloadMap();
			app->game->unloadMapData();
			this->loadMap(this->startupMap, true, false);
		}
		else {
			if (app->localization->selectLanguage) {
				app->menuSystem->clearStack();
				app->menuSystem->setMenu(Menus::MENU_SELECT_LANGUAGE);
			}
			else {
				app->menuSystem->setMenu(Menus::MENU_MAIN);
			}
		}
	}
	else {
		app->sound->playSound(1071, 1, 3, false);
		app->menuSystem->setMenu(Menus::MENU_MAIN);
	}
}

void Canvas::drawPlayingSoftKeys() { this->softKeyController.drawPlayingSoftKeys(); }

void Canvas::changeStoryPage(int i) { this->introSequenceManager.changeStoryPage(i); }
void Canvas::drawStory(Graphics* graphics) { this->introSequenceManager.drawStory(graphics); }
int Canvas::getCharacterConstantByOrder(int i) { return this->introSequenceManager.getCharacterConstantByOrder(i); }

void Canvas::drawCharacterSelection(Graphics* graphics) { this->introSequenceManager.drawCharacterSelection(graphics); }
void Canvas::drawCharacterSelectionAvatar(int i, int x, int y, Graphics* graphics) { this->introSequenceManager.drawCharacterSelectionAvatar(i, x, y, graphics); }
void Canvas::drawCharacterSelectionStats(int i, Text* text, int x, int y, Graphics* graphics) { this->introSequenceManager.drawCharacterSelectionStats(i, text, x, y, graphics); }

void Canvas::dequeueHelpDialog() { this->dialogSystem.dequeueHelpDialog(); }
void Canvas::dequeueHelpDialog(bool b) { this->dialogSystem.dequeueHelpDialog(b); }

void Canvas::enqueueHelpDialog(short n) { this->dialogSystem.enqueueHelpDialog(n); }
bool Canvas::enqueueHelpDialog(short n, short n2, uint8_t b) { return this->dialogSystem.enqueueHelpDialog(n, n2, b); }
bool Canvas::enqueueHelpDialog(Text* text) { return this->dialogSystem.enqueueHelpDialog(text); }
bool Canvas::enqueueHelpDialog(Text* text, int n) { return this->dialogSystem.enqueueHelpDialog(text, n); }
void Canvas::enqueueHelpDialog(EntityDef* entityDef) { this->dialogSystem.enqueueHelpDialog(entityDef); }

void Canvas::updateView() { this->movementController.updateView(); }

void Canvas::clearSoftKeys() { this->softKeyController.clearSoftKeys(); }

void Canvas::clearLeftSoftKey() { this->softKeyController.clearLeftSoftKey(); }

void Canvas::clearRightSoftKey() { this->softKeyController.clearRightSoftKey(); }

void Canvas::setLeftSoftKey(short i, short i2) { this->softKeyController.setLeftSoftKey(i, i2); }

void Canvas::setRightSoftKey(short i, short i2) { this->softKeyController.setRightSoftKey(i, i2); }

void Canvas::setSoftKeys(short n, short n2, short n3, short n4) { this->softKeyController.setSoftKeys(n, n2, n3, n4); }

void Canvas::checkHudEvents() { this->softKeyController.checkHudEvents(); }

void Canvas::drawSoftKeys(Graphics* graphics) { this->softKeyController.drawSoftKeys(graphics); }

void Canvas::setLoadingBarText(short loadingStringID, short loadingStringType) { this->loadingScreenController.setLoadingBarText(loadingStringID, loadingStringType); }

void Canvas::updateLoadingBar(bool b) { this->loadingScreenController.updateLoadingBar(b); }

void Canvas::drawLoadingBar(Graphics* graphics) { this->loadingScreenController.drawLoadingBar(graphics); }

void Canvas::unloadMedia() { this->loadingManager.unloadMedia(); }

void Canvas::invalidateRect() {
	this->staleView = true;
}

int Canvas::getRecentLoadType() { return this->loadingManager.getRecentLoadType(); }

void Canvas::initZoom() { this->zoomController.initZoom(); }

void Canvas::zoomOut() { this->zoomController.zoomOut(); }

bool Canvas::handleZoomEvents(int key, int action) { return this->zoomController.handleZoomEvents(key, action); }

bool Canvas::handleZoomEvents(int key, int action, bool b) { return this->zoomController.handleZoomEvents(key, action, b); }

void Canvas::handleCharacterSelectionInput(int key, int action) { this->introSequenceManager.handleCharacterSelectionInput(key, action); }
void Canvas::handleStoryInput(int key, int action) { this->introSequenceManager.handleStoryInput(key, action); }

void Canvas::lootingState() {
	this->lootingSystem.lootingState();
}

void Canvas::handleLootingEvents(int action) {
	this->lootingSystem.handleLootingEvents(action);
}

void Canvas::drawLootingMenu(Graphics* graphics) {
	this->lootingSystem.drawLootingMenu(graphics);
}

void Canvas::poolLoot(int* array) {
	this->lootingSystem.poolLoot(array);
}

void Canvas::giveLootPool() {
	this->lootingSystem.giveLootPool();
}

bool Canvas::handleTreadmillEvents(int action) {
	return this->miniGameManager.handleTreadmillEvents(action);
}

void Canvas::treadmillState() {
	this->miniGameManager.treadmillState();
}

bool Canvas::treadmillFall() {
	return this->miniGameManager.treadmillFall();
}

void Canvas::drawTreadmillReadout(Graphics* graphics) {
	this->miniGameManager.drawTreadmillReadout(graphics);
}

void Canvas::drawTargetPracticeScore(Graphics* graphics) {
	this->miniGameManager.drawTargetPracticeScore(graphics);
}

void Canvas::drawTravelMap(Graphics* graphics) { this->travelMapManager.drawTravelMap(graphics); }

bool Canvas::newLevelSamePlanet() { return this->travelMapManager.newLevelSamePlanet(); }

void Canvas::drawAppropriateCloseup(Graphics* graphics, int n, bool b) { this->travelMapManager.drawAppropriateCloseup(graphics, n, b); }

bool Canvas::drawDottedLine(Graphics* graphics, int n) { return this->travelMapManager.drawDottedLine(graphics, n); }

bool Canvas::drawDottedLine(Graphics* graphics) { return this->travelMapManager.drawDottedLine(graphics); }

bool Canvas::drawMarsToMoonLinePlusSpaceShip(Graphics* graphics, int n) { return this->travelMapManager.drawMarsToMoonLinePlusSpaceShip(graphics, n); }

int Canvas::yCoordOfSpaceShip(int n) { return this->travelMapManager.yCoordOfSpaceShip(n); }

bool Canvas::drawMoonToEarthLine(Graphics* graphics, int n, bool b) { return this->travelMapManager.drawMoonToEarthLine(graphics, n, b); }

bool Canvas::drawEarthToHellLine(Graphics* graphics, int n, bool b) { return this->travelMapManager.drawEarthToHellLine(graphics, n, b); }

void Canvas::drawLocatorBoxAndName(Graphics* graphics, bool b, int n, Text* text) { this->travelMapManager.drawLocatorBoxAndName(graphics, b, n, text); }

void Canvas::drawGridLines(Graphics* graphics, int i) { this->travelMapManager.drawGridLines(graphics, i); }

bool Canvas::onMoon(int n) { return this->travelMapManager.onMoon(n); }

bool Canvas::onEarth(int n) { return this->travelMapManager.onEarth(n); }

bool Canvas::inHell(int n) { return this->travelMapManager.inHell(n); }

void Canvas::handleTravelMapInput(int key, int action) { this->travelMapManager.handleInput(key, action); }

void Canvas::finishTravelMapAndLoadLevel() { this->travelMapManager.finishAndLoadLevel(); }

bool Canvas::drawLocatorLines(Graphics* graphics, int n, bool b, bool b2) { return this->travelMapManager.drawLocatorLines(graphics, n, b, b2); }

void Canvas::initTravelMap() { this->travelMapManager.init(); }

void Canvas::disposeTravelMap() { this->travelMapManager.dispose(); }

void Canvas::drawStarFieldPage(Graphics* graphics) { this->travelMapManager.drawStarFieldPage(graphics); }

void Canvas::drawStarField(Graphics* graphics, int x, int y) { this->travelMapManager.drawStarField(graphics, x, y); }

void Canvas::runStarFieldFrame() { this->travelMapManager.runStarFieldFrame(); }

void Canvas::playIntroMovie(Graphics* graphics) { this->introSequenceManager.playIntroMovie(graphics); }
void Canvas::exitIntroMovie(bool b) { this->introSequenceManager.exitIntroMovie(b); }

void Canvas::setMenuDimentions(int x, int y, int w, int h)
{
	this->menuRect[0] = x;
	this->menuRect[1] = y;
	this->menuRect[2] = w;
	this->menuRect[3] = h;
}

void Canvas::setBlendSpecialAlpha(float alpha) {

	if (alpha < 0.0f) {
		alpha = 0.0f;
	}
	else if (alpha > 1.0f) {
		alpha = 1.0f;
	}

	this->blendSpecialAlpha = alpha;
}

void Canvas::touchStart(int pressX, int pressY) { this->touchController.touchStart(pressX, pressY); }

void Canvas::touchMove(int pressX, int pressY) { this->touchController.touchMove(pressX, pressY); }

void Canvas::touchEnd(int pressX, int pressY) { this->touchController.touchEnd(pressX, pressY); }

void Canvas::touchEndUnhighlight() { this->touchController.touchEndUnhighlight(); }

void Canvas::initMiniGameHelpScreen() {
	this->miniGameManager.initMiniGameHelpScreen();
}

void Canvas::drawMiniGameHelpScreen(Graphics* graphics, int i, int i2, Image* image) {
	this->miniGameManager.drawMiniGameHelpScreen(graphics, i, i2, image);
}

void Canvas::drawMiniGameHelpText(Graphics* graphics, int i, int i2) {
	this->miniGameManager.drawMiniGameHelpText(graphics, i, i2);
}

void Canvas::handleMiniGameHelpScreenScroll(int i) {
	this->miniGameManager.handleMiniGameHelpScreenScroll(i);
}

void Canvas::disposeCharacterSelection() { this->introSequenceManager.disposeCharacterSelection(); }

bool Canvas::pitchIsControlled(int n, int n2, int n3) { return this->movementController.pitchIsControlled(n, n2, n3); }

int Canvas::touchToKey_Play(int pressX, int pressY) { return this->touchController.touchToKey_Play(pressX, pressY); }

bool Canvas::startArmorRepair(ScriptThread* armorRepairThread) { return this->armorRepairSystem.startArmorRepair(armorRepairThread); }
void Canvas::endArmorRepair() { this->armorRepairSystem.endArmorRepair(); }

void Canvas::drawTouchSoftkeyBar(Graphics* graphics, bool highlighted_Left, bool highlighted_Right) { this->touchController.drawTouchSoftkeyBar(graphics, highlighted_Left, highlighted_Right); }

void Canvas::touchSwipe(int swDir) { this->touchController.touchSwipe(swDir); }

void Canvas::turnEntityIntoWaterSpout(Entity* entity) { this->armorRepairSystem.turnEntityIntoWaterSpout(entity); }


void Canvas::flipControls() { this->touchController.flipControls(); }

void Canvas::setControlLayout() { this->touchController.setControlLayout(); }

void Canvas::evaluateMiniGameResults(int n) {
	this->miniGameManager.evaluateMiniGameResults(n);
}
void Canvas::addEvents(int event) { this->inputEventController.addEvents(event); }
