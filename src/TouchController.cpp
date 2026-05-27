#include "TouchController.h"

#include "App.h"
#include "Button.h"
#include "CAppContainer.h"
#include "Canvas.h"
#include "Game.h"
#include "Graphics.h"
#include "HackingGame.h"
#include "Hud.h"
#include "Input.h"
#include "IntroSequenceManager.h"
#include "MenuSystem.h"
#include "MiniGameManager.h"
#include "Player.h"
#include "SentryBotGame.h"
#include "Text.h"
#include "Utils.h"
#include "VendingMachine.h"

void TouchController::touchStart(int pressX, int pressY) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	canvas->touched = false;

	if (canvas->state == Canvas::ST_MENU) {
		app->menuSystem->handleUserTouch(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_LOOTING) {
		canvas->handleEvent(6);
	}
	else if ((canvas->state == Canvas::ST_PLAYING) || (canvas->state == Canvas::ST_COMBAT)) {
		if (!canvas->isZoomedIn) {
			app->hud->handleUserTouch(pressX, pressY, true);

			if (!app->hud->isInWeaponSelect) {
				canvas->m_controlButtons[canvas->m_controlMode + 0]->HighlightButton(pressX, pressY, true);
				canvas->m_controlButtons[canvas->m_controlMode + 2]->HighlightButton(pressX, pressY, true);
				canvas->m_controlButtons[canvas->m_controlMode + 4]->HighlightButton(pressX, pressY, true);

				canvas->m_controlButton = nullptr;
				fmButton* button;
				if (!canvas->isZoomedIn && (
					(button = canvas->m_controlButtons[canvas->m_controlMode + 0]->GetTouchedButton(pressX, pressY)) ||
					(button = canvas->m_controlButtons[canvas->m_controlMode + 2]->GetTouchedButton(pressX, pressY)) ||
					(button = canvas->m_controlButtons[canvas->m_controlMode + 4]->GetTouchedButton(pressX, pressY))) &&
					(button->buttonID != 6)) {
					canvas->m_controlButton = button;
					canvas->m_controlButtonIsTouched = true;
				}
				else if (canvas->m_swipeArea[canvas->m_controlMode]->rect.ContainsPoint(pressX, pressY))
				{
					canvas->m_swipeArea[canvas->m_controlMode]->touched = true;
					canvas->m_swipeArea[canvas->m_controlMode]->begX = pressX;
					canvas->m_swipeArea[canvas->m_controlMode]->begY = pressY;
					canvas->m_swipeArea[canvas->m_controlMode]->curX = -1;
					canvas->m_swipeArea[canvas->m_controlMode]->curY = -1;
					return;
				}
				canvas->m_swipeArea[canvas->m_controlMode]->touched = false;
			}
		}
		else {
			if (canvas->m_sniperScopeDialScrollButton->field_0x0_ &&
				canvas->m_sniperScopeDialScrollButton->barRect.ContainsPoint(pressX, pressY)) {
				canvas->m_sniperScopeDialScrollButton->SetTouchOffset(pressX, pressY);
				canvas->m_sniperScopeDialScrollButton->field_0x14_ = 1;
			}
			else {
				app->hud->handleUserTouch(pressX, pressY, true);
				if (!app->hud->isInWeaponSelect) {
					canvas->m_sniperScopeButtons->HighlightButton(pressX, pressY, true);
				}
			}
		}
	}
	else if (canvas->state == Canvas::ST_AUTOMAP) {
		//puts("touch in automap!");
		canvas->m_softKeyButtons->HighlightButton(pressX, pressY, true);
		canvas->m_controlButtons[canvas->m_controlMode + 0]->HighlightButton(pressX, pressY, true);
		canvas->m_controlButtons[canvas->m_controlMode + 2]->HighlightButton(pressX, pressY, true);
		canvas->m_controlButton = nullptr;
		fmButton* button;
		if (((button = canvas->m_controlButtons[canvas->m_controlMode + 0]->GetTouchedButton(pressX, pressY)) ||
			(button = canvas->m_controlButtons[canvas->m_controlMode + 2]->GetTouchedButton(pressX, pressY))) &&
			(button->buttonID != 6)) {
			canvas->m_controlButton = button;
			canvas->m_controlButtonIsTouched = true;
		}
	}
	else if (canvas->state == Canvas::ST_MIXING) {
		canvas->m_mixingButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_INTRO) {
		canvas->introSequenceManager.m_storyButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_DIALOG) {
		canvas->m_dialogButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_TREADMILL) {
		canvas->miniGameManager.m_treadmillButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_CHARACTER_SELECTION) {
		canvas->introSequenceManager.m_characterButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_MINI_GAME) {
		if (app->canvas->stateVars[0] == 2) {
			app->hackingGame->touchStart(pressX, pressY);
		}
		else if (app->canvas->stateVars[0] == 4) {
			app->vendingMachine->touchStart(pressX, pressY);
		}
		else if (app->canvas->stateVars[0] == 0) {
			app->sentryBotGame->touchStart(pressX, pressY);
		}
	}
	else if (canvas->state == Canvas::ST_CAMERA) { // [GEC ]Port: New
		canvas->m_softKeyButtons->HighlightButton(pressX, pressY, true);
	}
}

void TouchController::touchMove(int pressX, int pressY) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	fmSwipeArea::SwipeDir swDir;

	//canvas->touched = true; // Old

	// [GEC] Evita falsos toques en la pantalla
	const int begMouseX = (int)(gBegMouseX * Applet::IOS_WIDTH);
	const int begMouseY = (int)(gBegMouseY * Applet::IOS_HEIGHT);
	if (!pointInRectangle(pressX, pressY, begMouseX - 3, begMouseY - 3, 6, 6)) {
		canvas->touched = true;
	}

	if (canvas->state == Canvas::ST_MENU) {
		app->menuSystem->handleUserMoved(pressX, pressY);
	}
	else if ((canvas->state == Canvas::ST_PLAYING) || (canvas->state == Canvas::ST_COMBAT)) {

		if (canvas->isZoomedIn) {
			if (canvas->m_sniperScopeDialScrollButton->field_0x14_) {
				canvas->m_sniperScopeDialScrollButton->Update(pressX, pressY);
				int field_0x44 = canvas->m_sniperScopeDialScrollButton->field_0x44_;
				if (!canvas->m_sniperScopeDialScrollButton->field_0x0_ || (field_0x44 <= 2)) {
					field_0x44 = 3;
				}
				canvas->zoomDestFOV = 110 * field_0x44 / 15 + 80;

				// [GEC] update zoomCurFOVPercent
				{
					float maxScroll = (float)((canvas->m_sniperScopeDialScrollButton->barRect).h - canvas->m_sniperScopeDialScrollButton->field_0x4c_);
					float yScroll = (float)((float)canvas->m_sniperScopeDialScrollButton->field_0x48_ / maxScroll);
					canvas->zoomCurFOVPercent = canvas->zoomMinFOVPercent - (int)((float)canvas->zoomMinFOVPercent * yScroll);
				}
			}
			else {
				if (canvas->m_sniperScopeDialScrollButton->field_0x0_
					&& canvas->m_sniperScopeDialScrollButton->barRect.ContainsPoint(pressX, pressY))
				{
					canvas->m_sniperScopeDialScrollButton->SetTouchOffset(pressX, pressY);
					canvas->m_sniperScopeDialScrollButton->field_0x14_ = 1;
					return;
				}

				app->hud->handleUserMoved(pressX, pressY);
				if (!app->hud->isInWeaponSelect) {
					canvas->m_sniperScopeButtons->HighlightButton(pressX, pressY, true);
				}
			}
		}
		else {
			app->hud->handleUserMoved(pressX, pressY);
			if (!app->hud->isInWeaponSelect) {
				if (canvas->m_swipeArea[canvas->m_controlMode]->touched) {
					if (canvas->m_swipeArea[canvas->m_controlMode]->UpdateSwipe(pressX, pressY, &swDir)) {
						canvas->touched = true;
						this->touchSwipe(swDir);
						return;
					}
				}
				else {
					canvas->m_controlButtons[canvas->m_controlMode + 0]->HighlightButton(pressX, pressY, true);
					canvas->m_controlButtons[canvas->m_controlMode + 2]->HighlightButton(pressX, pressY, true);
					canvas->m_controlButtons[canvas->m_controlMode + 4]->HighlightButton(pressX, pressY, true);

					if (canvas->m_controlButton && !canvas->m_controlButton->highlighted) {
						canvas->m_controlButton = nullptr;
					}
					canvas->m_controlButton = nullptr;

					fmButton* button;
					if (!canvas->isZoomedIn && (
						(button = canvas->m_controlButtons[canvas->m_controlMode + 0]->GetTouchedButton(pressX, pressY)) ||
						(button = canvas->m_controlButtons[canvas->m_controlMode + 2]->GetTouchedButton(pressX, pressY)) ||
						(button = canvas->m_controlButtons[canvas->m_controlMode + 4]->GetTouchedButton(pressX, pressY))) &&
						(button->buttonID != 6)) {
						canvas->m_controlButton = button;
						canvas->m_controlButtonIsTouched = true;
					}
				}
			}
		}
	}
	else if (canvas->state == Canvas::ST_AUTOMAP) {
		canvas->m_softKeyButtons->HighlightButton(pressX, pressY, 1);
		canvas->m_controlButtons[canvas->m_controlMode + 0]->HighlightButton(pressX, pressY, true);
		canvas->m_controlButtons[canvas->m_controlMode + 2]->HighlightButton(pressX, pressY, true);

		if (canvas->m_controlButton && !canvas->m_controlButton->highlighted) {
			canvas->m_controlButton = nullptr;
		}
		canvas->m_controlButton = nullptr;

		fmButton* button;
		if (((button = canvas->m_controlButtons[canvas->m_controlMode + 0]->GetTouchedButton(pressX, pressY)) ||
			(button = canvas->m_controlButtons[canvas->m_controlMode + 2]->GetTouchedButton(pressX, pressY))) &&
			(button->buttonID != 6)) {
			canvas->m_controlButton = button;
			canvas->m_controlButtonIsTouched = true;
		}
	}
	else if (canvas->state == Canvas::ST_MIXING) {
		canvas->m_mixingButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_INTRO) {
		canvas->introSequenceManager.m_storyButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_DIALOG) {
		canvas->m_dialogButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_TREADMILL) {
		canvas->miniGameManager.m_treadmillButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_CHARACTER_SELECTION) {
		canvas->introSequenceManager.m_characterButtons->HighlightButton(pressX, pressY, true);
	}
	else if (canvas->state == Canvas::ST_MINI_GAME) {
		if (app->canvas->stateVars[0] == 2) {
			app->hackingGame->touchMove(pressX, pressY);
		}
		else if (app->canvas->stateVars[0] == 4) {
			app->vendingMachine->touchMove(pressX, pressY);
		}
		else if (app->canvas->stateVars[0] == 0) {
			app->sentryBotGame->touchMove(pressX, pressY);
		}
	}
	else if (canvas->state == Canvas::ST_CAMERA) { // [GEC]: New
		canvas->m_softKeyButtons->HighlightButton(pressX, pressY, true);
	}
}

void TouchController::touchEnd(int pressX, int pressY) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	short sVar1;
	int iVar3;
	int state;
	int uVar5;

	state = canvas->state;
	//printf("state %d\n", state);
	if (canvas->state == Canvas::ST_MENU) {
		app->menuSystem->handleUserTouch(pressX, pressY, false);
		return;
	}

	if (canvas->state == Canvas::ST_INTRO) {
		state = canvas->introSequenceManager.m_storyButtons->GetTouchedButtonID(pressX, pressY);
		if (state != 1) {
			if (state == 2) {
				if (canvas->introSequenceManager.storyPage >= canvas->introSequenceManager.storyTotalPages - 1) {
					return;
				}
			}
			else if (state != 0) {
				return;
			}
		}
		canvas->stateVars[0] = state;
	LAB_00022b00:
		state = canvas->numEvents;
		if (state == 4) {
			return;
		}
		iVar3 = 6;
	}
	else {
		if (canvas->state != Canvas::ST_DIALOG) {
			if (canvas->state == Canvas::ST_AUTOMAP) {
				canvas->m_controlButton = nullptr;
				state = canvas->m_softKeyButtons->GetTouchedButtonID(pressX, pressY);
				canvas->m_controlButtonTime = app->gameTime + -1;
			}
			else {
				if (canvas->state == Canvas::ST_COMBAT || canvas->state == Canvas::ST_PLAYING) {
					state = this->touchToKey_Play(pressX, pressY);
				}
				else {
					if (canvas->state == Canvas::ST_CAMERA) {
						if (app->canvas->softKeyRightID != -1) { // [GEC]: New
							canvas->m_softKeyButtons->HighlightButton(pressX, pressY, true);
						}

						if ((app->canvas->softKeyRightID != -1) &&
							(state = canvas->m_softKeyButtons->GetTouchedButtonID(pressX, pressY),
								state == 20)) {
						LAB_00022c60:

							state = 6;
							goto LAB_00022c64;
						}
					}
					else {
						if (canvas->state == Canvas::ST_MINI_GAME) {
							state = app->canvas->stateVars[0];
							if (state == 2) {
								app->hackingGame->touchEnd(pressX, pressY);
							}
							else {
								if (state == 4) {
									app->vendingMachine->touchEnd(pressX, pressY);
								}
								else {
									if (state == 0) {
										app->sentryBotGame->touchEnd(pressX, pressY);
									}
								}
							}
						}
						else {
							if (canvas->state == Canvas::ST_CHARACTER_SELECTION) { // [GEC]
								state = AVK_PASSTURN;
								goto LAB_00022c64;
							}
							if (canvas->state != Canvas::ST_TREADMILL) goto LAB_00022c60;
							state = canvas->miniGameManager.m_treadmillButtons->GetTouchedButtonID(pressX, pressY);
							if (state == 0) {
								state = canvas->numEvents;
								if (state != 4) {
									iVar3 = 2;
								LAB_00022c48:
									canvas->events[state] = iVar3;
									canvas->numEvents = state + 1;
									canvas->keyPressedTime = app->upTimeMs;
								}
							}
							else {
								if (state == 1) {
									state = canvas->numEvents;
									if (state != 4) {
										iVar3 = 4;
										goto LAB_00022c48;
									}
								}
							}
						}
					}
					state = -1;
				}
			}
		LAB_00022c64:
			canvas->m_controlButtonIsTouched = false;
			if (state == -1) {
				return;
			}
			iVar3 = canvas->numEvents;
			if (iVar3 == 4) {
				return;
			}
			canvas->events[iVar3] = state;
			canvas->numEvents = iVar3 + 1;
			state = app->upTimeMs;
			goto LAB_00022ca4;
		}
		state = canvas->m_dialogButtons->GetTouchedButtonID(pressX, pressY);
		if (state - 7U < 2) {
			state = 6;
		LAB_00022944:
			if (canvas->dialogSystem.currentDialogLine < canvas->dialogSystem.numDialogLines - canvas->dialogSystem.dialogViewLines) goto LAB_00022980;
			uVar5 = canvas->dialogSystem.dialogFlags;
			if ((uVar5 & 2) != 0) {
				return;
			}
			if ((uVar5 & 4) != 0) {
				return;
			}
			if ((uVar5 & 1) != 0) {
				return;
			}
		LAB_00022af8:
			//pCVar4 = *Applet;
			goto LAB_00022b00;
		}
		if (state == 6) goto LAB_00022944;
	LAB_00022980:
		if ((1 < state - 5U) || (canvas->dialogSystem.numDialogLines <= canvas->dialogSystem.dialogViewLines)) {
			uVar5 = canvas->dialogSystem.dialogFlags;
			if ((uVar5 & 2) == 0) {
				if (uVar5 == 0) {
					return;
				}
				if (canvas->dialogSystem.currentDialogLine < canvas->dialogSystem.numDialogLines - canvas->dialogSystem.dialogViewLines) {
					return;
				}
				if (((uVar5 & 4) == 0) && ((uVar5 & 1) == 0)) {
					return;
				}
				if (1 < state - 3U) {
					return;
				}
				if (state == 3) {
					app->game->scriptStateVars[4] = 0;
				}
				else {
					app->game->scriptStateVars[4] = 1;
				}
			}
			else {
				iVar3 = canvas->dialogSystem.dialogStyle;
				sVar1 = (short)state;
				if (iVar3 == 11) {
					if (state == 0) {
						app->game->scriptStateVars[4] = sVar1;
					}
					else {
						if (state != 1) {
							return;
						}
						app->game->scriptStateVars[4] = sVar1;
					}
					iVar3 = canvas->numEvents;
					if (iVar3 != 4) {
						canvas->events[iVar3] = 6;
						canvas->numEvents = iVar3 + 1;
						canvas->keyPressedTime = app->upTimeMs;
					}
					iVar3 = canvas->dialogSystem.dialogStyle;
				}
				if (iVar3 != 10) {
					return;
				}
				if (state == 0) {
					app->game->scriptStateVars[4] = sVar1;
				}
				else {
					if (state != 1) {
						return;
					}
					app->game->scriptStateVars[4] = sVar1;
				}
			}
			goto LAB_00022af8;
		}
		if (state != 5) goto LAB_00022af8;
		state = canvas->numEvents;
		if (state == 4) {
			return;
		}
		iVar3 = 19;
	}
	canvas->events[state] = iVar3;
	canvas->numEvents = state + 1;
	state = app->upTimeMs;
LAB_00022ca4:
	canvas->keyPressedTime = state;
	return;
}

void TouchController::touchEndUnhighlight() {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	if (canvas->state == Canvas::ST_MIXING) {
		canvas->m_mixingButtons->HighlightButton(0, 0, false);
	}
	else if (canvas->state == Canvas::ST_INTRO) {
		canvas->introSequenceManager.m_storyButtons->HighlightButton(0, 0, false);
	}

	if (canvas->state == Canvas::ST_PLAYING || canvas->state == Canvas::ST_COMBAT || canvas->state == Canvas::ST_AUTOMAP || canvas->state == Canvas::ST_DIALOG || canvas->state == Canvas::ST_CAMERA) {
		canvas->m_controlButtons[canvas->m_controlMode]->HighlightButton(0, 0, false);
		canvas->m_controlButtons[canvas->m_controlMode + 2]->HighlightButton(0, 0, false);
		canvas->m_controlButtons[canvas->m_controlMode + 4]->HighlightButton(0, 0, false);
		canvas->m_sniperScopeButtons->HighlightButton(0, 0, false);
		canvas->m_softKeyButtons->HighlightButton(0, 0, false);
		canvas->m_dialogButtons->HighlightButton(0, 0, false);
		canvas->introSequenceManager.m_characterButtons->HighlightButton(0, 0, false);
	}
	else if (canvas->state == Canvas::ST_TREADMILL) {
		canvas->miniGameManager.m_treadmillButtons->HighlightButton(0, 0, false);
	}
}

int TouchController::touchToKey_Play(int pressX, int pressY) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int result;

	canvas->m_controlButton = 0;
	if (canvas->m_sniperScopeDialScrollButton->field_0x14_)
	{
		canvas->m_sniperScopeDialScrollButton->field_0x14_ = 0;
		return -1;
	}
	canvas->m_swipeArea[canvas->m_controlMode]->touched = false;
	if (((pressY > 256)) || (app->hud->isInWeaponSelect))
	{
		app->hud->handleUserTouch(pressX, pressY, false);
		return -1;
	}
	if (canvas->touched) {
		return -1;
	}
	if (canvas->isZoomedIn) {
		return canvas->m_sniperScopeButtons->GetTouchedButtonID(pressX, pressY);
	}
	if (canvas->m_controlButtonIsTouched) {
		return -1;
	}

	result = canvas->m_controlButtons[canvas->m_controlMode + 0]->GetTouchedButtonID(pressX, pressY);
	if (result == -1)
	{
		result = canvas->m_controlButtons[canvas->m_controlMode + 2]->GetTouchedButtonID(pressX, pressY);
		if (result == -1) {
			result = canvas->m_controlButtons[canvas->m_controlMode + 4]->GetTouchedButtonID(pressX, pressY);
		}
	}

	if (result != 6) {
		return -1;
	}

	return result;
}

void TouchController::drawTouchSoftkeyBar(Graphics* graphics, bool highlighted_Left, bool highlighted_Right) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	Text* smallBuffer = app->localization->getSmallBuffer();

	graphics->drawImage(app->menuSystem->imgGameMenuPanelbottom,
		0, 320 - app->menuSystem->imgGameMenuPanelbottom->height, 0, 0, 0);
	if (app->player->isFamiliar) {
		graphics->drawImage(app->menuSystem->imgGameMenuPanelBottomSentrybot,
			0, 320 - app->menuSystem->imgGameMenuPanelBottomSentrybot->height, 0, 0, 0);
	}

	if (!highlighted_Left || (highlighted_Left && canvas->softKeyLeftID == -1)) {
		graphics->drawImage(app->hud->imgSwitchLeftNormal, 9, 268, 0, 0, 0);
	}
	else {
		graphics->drawImage(app->hud->imgSwitchLeftActive, 9, 268, 0, 0, 0);
	}

	if (canvas->softKeyLeftID != -1) {
		smallBuffer->setLength(0);
		app->localization->composeText(canvas->softKeyLeftID, smallBuffer);
		smallBuffer->dehyphenate();
		graphics->drawString(smallBuffer, 2, 320, 36);
	}

	if (!highlighted_Right || (highlighted_Right && canvas->softKeyRightID == -1)) {
		graphics->drawImage(app->hud->imgSwitchRightNormal, 438, 268, 0, 0, 0);
	}
	else {
		graphics->drawImage(app->hud->imgSwitchRightActive, 438, 268, 0, 0, 0);
	}

	if (canvas->softKeyRightID != -1) {
		smallBuffer->setLength(0);
		app->localization->composeText(canvas->softKeyRightID, smallBuffer);
		smallBuffer->dehyphenate();
		graphics->drawString(smallBuffer, 478, 320, 40);
	}
	smallBuffer->dispose();
}

void TouchController::touchSwipe(int swDir) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	int iVar1;

	if (canvas->state == Canvas::ST_PLAYING || canvas->state == Canvas::ST_COMBAT) {
#if 0 // IOS
		switch (swDir) {
			case fmSwipeArea::SwipeDir::Left:
				iVar1 = 2;
				break;
			case fmSwipeArea::SwipeDir::Right:
				iVar1 = 4;
				break;
			case fmSwipeArea::SwipeDir::Down:
				iVar1 = AVK_RIGHT;
				break;
			case fmSwipeArea::SwipeDir::Up:
				iVar1 = AVK_LEFT;
				break;
			default:
				return;
		}

		if (canvas->isZoomedIn != false) {
			bool bVar4 = iVar1 == 2;
			if (bVar4) {
				iVar1 = 5;
			}
			if ((!bVar4) && (iVar1 == 4)) {
				iVar1 = 7;
			}
		}
#else
		switch (swDir) {
			case fmSwipeArea::SwipeDir::Left:
				iVar1 = AVK_MOVELEFT;
				break;
			case fmSwipeArea::SwipeDir::Right:
				iVar1 = AVK_MOVERIGHT;
				break;
			case fmSwipeArea::SwipeDir::Down:
				iVar1 = AVK_DOWN;
				break;
			case fmSwipeArea::SwipeDir::Up:
				iVar1 = AVK_UP;
				break;
			default:
				return;
		}
#endif

		if (canvas->numEvents != 4) {
			canvas->events[canvas->numEvents++] = iVar1;
			canvas->keyPressedTime = app->upTimeMs;
		}
	}
}

void TouchController::flipControls() {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;
	int v1; // r10
	fmButtonContainer** v2; // r6
	int x; // r8
	fmButton* Button; // r5
	fmButton* v5; // r0
	bool v6; // zf
	fmButton* v7; // r4
	fmButton* v8; // r4
	fmButton* v9; // r0
	bool v10; // zf
	fmButton* v11; // r4
	fmButton* v12; // r0
	bool v13; // zf
	fmButton* v14; // r5
	int v15; // r8

	v1 = 0;
	v2 = &canvas->m_controlButtons[4];
	canvas->isFlipControls ^= 1u;
	do
	{
		v2[-4]->FlipButtons();
		v2[-2]->FlipButtons();
		v2[0]->FlipButtons();
		while (1)
		{
			Button = v2[-2]->GetButton(5);
			v5 = v2[-2]->GetButton(7);
			v6 = Button == 0;
			if (Button)
				v6 = v5 == 0;
			v7 = v5;
			if (v6)
				break;
			x = Button->touchArea.x;
			Button->SetTouchArea(v5->touchArea.x, Button->touchArea.y, Button->touchArea.w, Button->touchArea.h);
			v7->SetTouchArea(x, v7->touchArea.y, v7->touchArea.w, v7->touchArea.h);
			Button->buttonID = -2;
			v7->buttonID = -3;
		}
		while (1)
		{
			v8 = v2[-2]->GetButton(-2);
			v9 = v2[-2]->GetButton(-3);
			v10 = v8 == 0;
			if (v8)
				v10 = v9 == 0;
			if (v10)
				break;
			v8->buttonID = 5;
			v9->buttonID = 7;
		}
		v11 = v2[0]->GetButton(2);
		v12 = v2[0]->GetButton(4);
		v13 = v11 == 0;
		if (v11)
			v13 = v12 == 0;
		v14 = v12;
		if (!v13)
		{
			v15 = v11->touchArea.x;
			v11->SetTouchArea(v12->touchArea.x, v11->touchArea.y, v11->touchArea.w, v11->touchArea.h);
			v14->SetTouchArea(v15, v14->touchArea.y, v14->touchArea.w, v14->touchArea.h);
		}
		++v1;
		++v2;
	} while (v1 != 2);
}

void TouchController::setControlLayout() {
	Canvas* canvas = CAppContainer::getInstance()->app->canvas;

	if (canvas->m_controlLayout == 2) {
		canvas->m_controlGraphic = 0;
		canvas->m_controlMode = 1;
	}
	else {
		canvas->m_controlGraphic = canvas->m_controlLayout;
		canvas->m_controlMode = 0;
	}

	for (int i = 0; i < 2; i++) {
		canvas->m_controlButtons[i + 0]->SetGraphic(canvas->m_controlGraphic);
		canvas->m_controlButtons[i + 2]->SetGraphic(canvas->m_controlGraphic);
	}
}
