#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Player.h"
#include "Hud.h"
#include "Sound.h"
#include "Render.h"
#include "Entity.h"
#include "Enums.h"
#include "Text.h"
#include "ScriptThread.h"
#include "ArmorRepairSystem.h"

ArmorRepairSystem::ArmorRepairSystem() {}

bool ArmorRepairSystem::startArmorRepair(ScriptThread* armorRepairThread) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    if (app->player->showHelp((short)15, false)) {
        return false;
    }
    if (app->player->inventory[12] >= 5) {
        canvas->armorRepairThread = armorRepairThread;
        canvas->repairingArmor = true;
        Text* smallBuffer = app->localization->getSmallBuffer();
        app->localization->composeText((short)0, (short)211, smallBuffer);
        canvas->startDialog(nullptr, smallBuffer, 13, 1, false);
        smallBuffer->dispose();
        return true;
    }

    if (app->player->characterChoice == 1) {
        app->sound->playSound(1073, 0, 3, 0);
    }
    else if (app->player->characterChoice >= 1 && app->player->characterChoice <= 3){
        app->sound->playSound(1072, 0, 3, 0);
    }

    app->hud->addMessage((short)0, (short)210, 3);
    return false;
}

void ArmorRepairSystem::endArmorRepair() {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    if (canvas->armorRepairThread != nullptr) {
        canvas->setState(Canvas::ST_PLAYING);
        canvas->armorRepairThread->run();
        canvas->armorRepairThread = nullptr;
        canvas->repairingArmor = false;
    }
}

void ArmorRepairSystem::turnEntityIntoWaterSpout(Entity* entity) {
    Applet* app = CAppContainer::getInstance()->app;
    int sprite = entity->getSprite();
    entity->def = app->entityDefManager->lookup(Enums::TILENUM_WATER_SPOUT);
    entity->name = (short)(entity->def->name | 0x400);
    app->render->mapSpriteInfo[sprite] = ((app->render->mapSpriteInfo[sprite] & 0xFFFFFF00) | Enums::TILENUM_WATER_SPOUT);
    entity->info |= 0x400000;
}
