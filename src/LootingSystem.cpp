#include <algorithm>
#include "LootingSystem.h"
#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Graphics.h"
#include "Text.h"
#include "Enums.h"
#include "Game.h"
#include "Player.h"
#include "Combat.h"
#include "Render.h"
#include "Sound.h"
#include "Hud.h"
#include "Entity.h"
#include "EntityDef.h"

LootingSystem::LootingSystem() {
	std::memset(this, 0, sizeof(LootingSystem));
	this->specialLootIcon = -1;
}

LootingSystem::~LootingSystem() {
}

void LootingSystem::onEnterLooting(int destPitch) {
	Applet* app = CAppContainer::getInstance()->app;
	app->canvas->clearSoftKeys();
	this->lootingTime = app->time;
	this->crouchingForLoot = true;
	this->lootingCachedPitch = destPitch;
	this->field_0xac5_ = false;
}

void LootingSystem::lootingState() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	app->hud->repaintFlags |= 0x22;
	canvas->repaintFlags |= (Canvas::REPAINT_HUD | Canvas::REPAINT_VIEW3D);
	int height = app->render->getHeight(canvas->destX, canvas->destY);
	int height2 = app->render->getHeight(canvas->destX + canvas->viewStepX, canvas->destY + canvas->viewStepY);
	if (app->time < this->lootingTime + 500) {
		int n = (500 - (app->time - this->lootingTime) << 16) / 500;
		int n2 = 65536 - n;
		if (this->crouchingForLoot) {
			int n3 = (height > height2) ? height : (height * n + height2 * n2 >> 16);
			canvas->viewX = canvas->destX + (48 + (-48 * n >> 16)) * (canvas->viewStepX >> 6);
			canvas->viewY = canvas->destY + (48 + (-48 * n >> 16)) * (canvas->viewStepY >> 6);
			canvas->viewZ = n3 + 26 + (10 * n >> 16);
			canvas->viewPitch = std::max(-(64 - (64 * n >> 16)) + this->lootingCachedPitch, -64);
		}
		else {
			int n4 = (height > height2) ? height : (height * n2 + height2 * n >> 16);
			canvas->viewX = canvas->destX + (48 * n >> 16) * (canvas->viewStepX >> 6);
			canvas->viewY = canvas->destY + (48 * n >> 16) * (canvas->viewStepY >> 6);
			canvas->viewZ = n4 + 36 + (-10 * n >> 16);
			canvas->viewPitch = std::max(-(64 * n >> 16) + this->lootingCachedPitch, -64);
		}
		canvas->updateView();
	}
	else {
		if (!this->field_0xac5_) {
			this->field_0xac5_ = true;
			app->sound->playSound(1055, 0, 3, 0);
		}
		if (this->crouchingForLoot) {
			canvas->viewX = canvas->destX + 48 * (canvas->viewStepX >> 6);
			canvas->viewY = canvas->destY + 48 * (canvas->viewStepY >> 6);
			canvas->viewZ = std::max(height, height2) + 26;
			canvas->viewPitch = std::max(-64 + this->lootingCachedPitch, -64);
			canvas->updateView();
		}
		else {
			canvas->viewX = canvas->destX;
			canvas->viewY = canvas->destY;
			canvas->viewZ = height + 36;
			canvas->viewPitch = this->lootingCachedPitch;
			canvas->updateView();
			canvas->setState(Canvas::ST_PLAYING);
			app->game->advanceTurn();
		}
	}
}

void LootingSystem::handleLootingEvents(int action) {
	Applet* app = CAppContainer::getInstance()->app;
	if (this->crouchingForLoot && app->time > this->lootingTime + 500) {
		int max = std::max(this->numPoolItems + ((this->lootPoolCredits != 0) ? 1 : 0) - 3, 0);
		if (action == Enums::ACTION_FIRE) {
			if (this->lootLineNum >= max) {
				this->lootingTime = app->time;
				this->crouchingForLoot = false;
				this->giveLootPool();
			}
			else {
				this->lootLineNum = std::min(this->lootLineNum + 3, max);
			}
		}
		else if (action == Enums::ACTION_PASSTURN || action == Enums::ACTION_BACK) {
			this->lootingTime = app->time;
			this->crouchingForLoot = false;
			this->giveLootPool();
		}
		else if (action == Enums::ACTION_DOWN) {
			this->lootLineNum = std::min(this->lootLineNum + 1, max);
		}
		else if (action == Enums::ACTION_UP) {
			this->lootLineNum = std::max(this->lootLineNum - 1, 0);
		}
		else if (action == Enums::ACTION_LEFT) {
			this->lootLineNum = 0;
		}
		else if (action == Enums::ACTION_RIGHT) {
			this->lootLineNum = max;
		}
	}
}

void LootingSystem::drawLootingMenu(Graphics* graphics) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	if (this->crouchingForLoot && app->time > this->lootingTime + 500) {
		int* dialogRect = canvas->dialogRect;
		dialogRect[0] = canvas->viewRect[0] + 0;
		dialogRect[1] = canvas->viewRect[1] + 0 + 16;
		dialogRect[2] = canvas->viewRect[2] - dialogRect[0] - 0 - 1;
		dialogRect[3] = 48;
		graphics->setColor(0xFF660000);
		graphics->fillRect(dialogRect[0], dialogRect[1], dialogRect[2], dialogRect[3]);
		graphics->setColor(0xFF000000);
		graphics->fillRect(dialogRect[0], dialogRect[1] - 18, dialogRect[2], 18);
		graphics->setColor(0xFFFFFFFF);
		graphics->drawRect(dialogRect[0], dialogRect[1] - 18, dialogRect[2], 18);
		graphics->drawRect(dialogRect[0], dialogRect[1], dialogRect[2], dialogRect[3]);
		Text* smallBuffer = app->localization->getSmallBuffer();
		app->localization->composeText((short)0, (short)227, smallBuffer);
		smallBuffer->dehyphenate();
		graphics->drawString(smallBuffer, canvas->SCR_CX, dialogRect[1] - 16, 1);
		smallBuffer->dispose();
		for (int i = 0; i < 3; ++i) {
			graphics->drawString(this->lootText, dialogRect[0] + 5, dialogRect[1] + 1 + i * 16, 20,
				this->lootPoolIndices[2 * (i + this->lootLineNum)],
				this->lootPoolIndices[2 * (i + this->lootLineNum) + 1]);
		}
		int n = this->numPoolItems + ((this->lootPoolCredits != 0) ? 1 : 0);
		canvas->drawScrollBar(graphics,
			dialogRect[0] + dialogRect[2], dialogRect[1] + 1, dialogRect[3] - 1,
			this->lootLineNum,
			(this->lootLineNum + 3 > n) ? n : (this->lootLineNum + 3),
			n, 3);
	}
}

void LootingSystem::poolLoot(int* array) {
	Applet* app = CAppContainer::getInstance()->app;
	Entity* entity = app->game->findMapEntity(array[0], array[1], 512);
	this->lootText = app->localization->getLargeBuffer();
	this->lootText->setLength(0);
	this->numPoolItems = 0;
	this->numLootItems = 0;
	this->lootLineNum = 0;
	this->lootPoolCredits = 0;
	while (entity != nullptr) {
		if (entity->def->eType == 9) {
			if (entity->monster == nullptr) {
				if (entity->param != 0) {
					entity = entity->nextOnTile;
					continue;
				}
				++entity->param;
			}
			else {
				if ((entity->monster->flags & 0x800) != 0x0) {
					entity = entity->nextOnTile;
					continue;
				}
				entity->monster->flags |= 0x800;
			}
			entity->info |= 0x400000;
			for (int i = 0; i < 3; ++i) {
				if (entity->lootSet[i] == 0) {
					break;
				}
				bool b = true;
				int n = entity->lootSet[i] >> 12 & 0xF;
				if (n == 6) {
					int n2 = entity->lootSet[i] & 0xFFF;
					for (int j = 0; j < this->numPoolItems; ++j) {
						if ((entity->lootSet[j] >> 12 & 0xF) == 0x6 && n2 == (this->lootPool[j] & 0xFFF)) {
							b = false;
							break;
						}
					}
				}
				else {
					int n3 = entity->lootSet[i] & 0x3F;
					++this->numLootItems;
					int n4 = (entity->lootSet[i] & 0xFC0) >> 6;
					if (n == 0) {
						if (n4 == 24) {
							this->lootPoolCredits += n3;
							continue;
						}
						if (n4 == 25) {
							this->lootPoolCredits += n3 * 100;
							continue;
						}
					}
					int n5 = entity->lootSet[i] >> 6;
					for (int k = 0; k < this->numPoolItems; ++k) {
						if (n5 == this->lootPool[k] >> 6) {
							b = false;
							this->lootPool[k] = ((this->lootPool[k] & 0xFFFFFFC0) | (n3 + (this->lootPool[k] & 0x3F) & 0x3F));
							break;
						}
					}
				}
				if (b) {
					this->lootPool[this->numPoolItems++] = entity->lootSet[i];
				}
			}
		}
		entity = entity->nextOnTile;
	}
	Canvas* canvas = app->canvas;
	for (int l = 0; l < this->numPoolItems; ++l) {
		int n6 = this->lootPool[l];
		int n7 = n6 >> 12 & 0xF;
		if (n7 == 6) {
			short n8 = (short)(n6 & 0xFFF);
			this->lootText->append('\x88');
			app->localization->composeText(canvas->loadMapStringID, n8, this->lootText);
			this->lootText->append("|");
		}
		else {
			int n9 = (n6 & 0xFC0) >> 6;
			int n10 = n6 & 0x3F;
			app->localization->resetTextArgs();
			app->localization->addTextArg('\x88');
			EntityDef* find = app->entityDefManager->find(6, n7, n9);
			if (n7 == 1) {
				app->localization->addTextArg((short)1, find->longName);
				app->localization->composeText((short)0, (short)91, this->lootText);
			}
			else {
				app->localization->addTextArg(n10);
				app->localization->addTextArg((short)1, find->longName);
				app->localization->composeText((short)0, (short)90, this->lootText);
			}
		}
	}
	if (this->lootPoolCredits != 0) {
		app->localization->resetTextArgs();
		app->localization->addTextArg('\x88');
		app->localization->addTextArg(this->lootPoolCredits);
		app->localization->addTextArg((short)1, (short)157);
		app->localization->composeText((short)0, (short)90, this->lootText);
	}
	if (this->numPoolItems == 0 && this->lootPoolCredits == 0) {
		app->localization->composeText((short)0, (short)228, this->lootText);
	}
	this->lootText->dehyphenate();
	for (int n13 = 0; n13 < 18; ++n13) {
		this->lootPoolIndices[n13] = (short)0;
	}
	int length = this->lootText->length();
	int n11 = 0;
	int n12 = 0;
	for (int n13 = 0; n13 < length; ++n13) {
		if (this->lootText->charAt(n13) == '|') {
			this->lootPoolIndices[n12 * 2] = (short)n11;
			this->lootPoolIndices[n12 * 2 + 1] = (short)(n13 - n11);
			++n12;
			n11 = n13 + 1;
		}
	}
	this->lootPoolIndices[n12 * 2] = (short)n11;
	this->lootPoolIndices[n12 * 2 + 1] = (short)(length - n11);
}

void LootingSystem::giveLootPool() {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;
	for (int i = 0; i < this->numPoolItems; ++i) {
		int n = this->lootPool[i];
		int n2 = n >> 12 & 0xF;
		if (n2 != 6) {
			int n3 = (n & 0xFC0) >> 6;
			app->player->give(n2, n3, n & 0x3F, false);
			if (n2 == 1) {
				int n4 = n3 * 9;
				uint8_t a = app->combat->weapons[n4 + 5];
				if (a != 0) {
					app->player->give(2, app->combat->weapons[n4 + 4], std::max((int)a, 10), false);
				}
			}
		}
	}
	if (this->lootPoolCredits != 0) {
		app->player->give(0, 24, this->lootPoolCredits, false);
		this->lootPoolCredits = 0;
	}
	app->game->foundLoot(canvas->viewX + canvas->viewStepX, canvas->viewY + canvas->viewStepY, canvas->viewZ, this->numLootItems);
	this->numPoolItems = 0;
	this->numLootItems = 0;
	this->lootText->dispose();
}
