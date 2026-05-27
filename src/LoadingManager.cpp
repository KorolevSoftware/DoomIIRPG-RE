#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Game.h"
#include "Render.h"
#include "Player.h"
#include "Sound.h"
#include "HackingGame.h"
#include "SentryBotGame.h"
#include "VendingMachine.h"
#include "ParticleSystem.h"
#include "TinyGL.h"
#include "Hud.h"
#include "Image.h"
#include "Utils.h"
#include "LoadingManager.h"

LoadingManager::LoadingManager() {
}

void LoadingManager::loadRuntimeData() {
    Applet* app = CAppContainer::getInstance()->app;
    app->loadRuntimeImages();
    //app->checkPeakMemory("after loadRuntimeData");
}

void LoadingManager::freeRuntimeData() {
    Applet* app = CAppContainer::getInstance()->app;
    app->freeRuntimeImages();
}

void LoadingManager::loadState(int loadType, short n, short n2) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    canvas->loadType = loadType;
    app->game->saveConfig();
    canvas->setLoadingBarText(n, n2);
    canvas->setState(Canvas::ST_LOADING);
}

void LoadingManager::saveState(int saveType, short n, short n2) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    canvas->saveType = saveType;
    canvas->setLoadingBarText(n, n2);
    canvas->setState(Canvas::ST_SAVING);
}

void LoadingManager::loadMap(int loadMapID, bool b, bool tm_NewGame) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    if (loadMapID > 0 && loadMapID < 11) {
        bool b2 = false;
        int n = loadMapID - 1;
        short n2 = app->game->numLevelLoads[n];
        app->game->numLevelLoads[n] = (short)(n2 + 1);
        if ((b2 ? 1 : 0) == n2) {
            app->player->currentLevelDeaths = 0;
        }
    }
    canvas->lastMapID = canvas->loadMapID;
    canvas->loadMapID = loadMapID;
    app->sound->soundStop();
    canvas->travelMapManager.TM_NewGame = tm_NewGame;
    if (!b && app->game->activeLoadType == 0 && canvas->lastMapID >= 1 && canvas->lastMapID <= 10) {
        this->saveState(43, (short)3, (short)196);
    }
    else {
        canvas->setLoadingBarText((short)3, app->game->levelNames[canvas->loadMapID - 1]);
        canvas->setState(Canvas::ST_TRAVELMAP);
    }
}

void LoadingManager::loadMiniGameImages() {
    Applet* app = CAppContainer::getInstance()->app;

    app->beginImageLoading();
    app->hackingGame->imgEnergyCore = app->loadImage("hackerBG.bmp", true);
    app->hackingGame->imgGameColors = app->loadImage("blockGameColors.bmp", true);
    app->hackingGame->imgHelpScreenAssets = app->loadImage("gameHelpBG.bmp", true);
    app->sentryBotGame->imgMatrixSkip_BG = app->loadImage("matrixSkip_BG.bmp", true);
    app->sentryBotGame->imgGameAssets = app->loadImage("sentryGame.bmp", true);
    app->sentryBotGame->imgButton = app->loadImage("matrixSkip_grid_pressed.bmp", true);
    app->sentryBotGame->imgButton2 = app->loadImage("matrixSkip_grid_pressed_2.bmp", true);
    app->sentryBotGame->imgButton3 = app->loadImage("matrixSkip_grid_pressed_3.bmp", true);
    app->vendingMachine->imgVendingGame = app->loadImage("vendingGame.bmp", true);
    app->vendingMachine->imgVending_BG = app->loadImage("vending_BG.bmp", true);
    app->vendingMachine->imgVendingBG = app->loadImage("vendingBG.bmp", true);
    app->vendingMachine->imgSubmitButton = app->loadImage("vending_submit.bmp", true);
    app->vendingMachine->imgVending_submit_pressed = app->loadImage("vending_submit_pressed.bmp", true);
    app->vendingMachine->imgVending_arrow_up = app->loadImage("vending_arrow_up.bmp", true);
    app->vendingMachine->imgVending_arrow_up_pressed = app->loadImage("vending_arrow_up_pressed.bmp", true);
    app->vendingMachine->imgVending_arrow_down = app->loadImage("vending_arrow_down.bmp", true);
    app->vendingMachine->imgVending_arrow_down_pressed = app->loadImage("vending_arrow_down_pressed.bmp", true);
    app->vendingMachine->imgVending_button_small = app->loadImage("vending_button_small.bmp", true);
    app->endImageLoading();

    app->vendingMachine->imgHelpScreenAssets = app->sentryBotGame->imgHelpScreenAssets = app->hackingGame->imgHelpScreenAssets;

    // [GEC] Fix the image
    fixImage(app->hackingGame->imgGameColors);
    fixImage(app->vendingMachine->imgVending_arrow_down);
}

bool LoadingManager::loadMedia() {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    printf("Canvas::loadMedia\n");

    //printf("Canvas::isLoaded %d\n", Canvas::isLoaded);
    if (Canvas::isLoaded == false) {
        canvas->updateLoadingBar(Canvas::isLoaded);
        canvas->drawLoadingBar(&canvas->graphics);
        Canvas::isLoaded = true;

        return false;
    }
    Canvas::isLoaded = false;

    bool allowSounds = app->sound->allowSounds;
    canvas->inInitMap = true;
    app->sound->allowSounds = false;
    canvas->mediaLoading = true;
    bool displaySoftKeys = canvas->displaySoftKeys;
    canvas->updateLoadingBar(false);
    this->unloadMedia();
    canvas->displaySoftKeys = false;
    canvas->isZoomedIn = false;
    app->StopAccelerometer();
    app->tinyGL->resetViewPort();

    for (int i = 0; i < 128; ++i) {
        if (i != 15) {
            app->game->scriptStateVars[i] = 0;
        }
    }

    if (!app->render->beginLoadMap(canvas->loadMapID)) {
        return false;
    }

    if (canvas->loadMapID <= 10 && canvas->loadMapID > app->player->highestMap) {
        app->player->highestMap = canvas->loadMapID;
    }

    if (app->game->isSaved) {
        canvas->setLoadingBarText((short)0, (short)38);
    }
    else if (app->game->isLoaded) {
        canvas->setLoadingBarText((short)0, (short)39);
    }
    else if (canvas->loadType != 3) {
        canvas->setLoadingBarText((short)3, (short)(app->render->mapNameField & 0x3FF));
    }

    canvas->updateLoadingBar(false);
    app->render->mapMemoryUsage -= 1000000000;
    //app->checkPeakMemory("after map loaded");
    app->game->loadMapEntities();
    app->hud->msgCount = 0;

    canvas->updateLoadingBar(false);
    app->player->playTime = app->gameTime;
    app->game->curLevelTime = app->gameTime;
    canvas->clearEvents(1);
    app->particleSystem->freeAllParticles();
    canvas->displaySoftKeys = displaySoftKeys;
    app->player->levelInit();

    canvas->updateLoadingBar(false);
    app->game->loadWorldState();

    canvas->updateLoadingBar(false);
    app->game->spawnPlayer();
    canvas->knockbackDist = 0;
    if (!app->game->isLoaded && canvas->loadType != 3) {
        app->game->saveLevelSnapshot();
    }

    canvas->updateLoadingBar(false);
    //printf("app->game->isLoaded %d\n", app->game->isLoaded);
    //printf("this->loadType %d\n", this->loadType);
    if (app->game->isLoaded || app->game->hasSavedState() || canvas->loadType == 3) {
        this->loadRuntimeData();
        //app->checkPeakMemory("after loadRuntimeData");
    }
    else {
        app->game->saveState(canvas->loadMapID, canvas->loadMapID, canvas->destX, canvas->destY, canvas->destAngle, canvas->destPitch, canvas->destX, canvas->destY, canvas->saveX, canvas->saveY, canvas->saveZ, canvas->saveAngle, canvas->savePitch, 3);
    }

    canvas->updateLoadingBar(false);
    app->player->selectWeapon(app->player->ce->weapon);
    app->game->scriptStateVars[12] = app->game->difficulty;
    app->game->executeStaticFunc(0);

    canvas->updateLoadingBar(false);
    if (app->player->gameCompleted) {
        app->game->executeStaticFunc(1);
    }

    if (!app->game->isLoaded) {
        canvas->prevX = canvas->destX;
        canvas->prevY = canvas->destY;
        app->game->executeTile(canvas->viewX >> 6, canvas->viewY >> 6, 4081, 1);
        canvas->finishRotation(false);
        canvas->dequeueHelpDialog(true);
    }
    canvas->finishRotation(false);

    app->game->endMonstersTurn();
    canvas->uncoverAutomap();
    canvas->updateLoadingBar(false);
    app->game->isSaved = (app->game->isLoaded = false);
    app->game->activeLoadType = 0;
    canvas->dequeueHelpDialog(true);
    if (canvas->state == 0) {
        canvas->setState(Canvas::ST_PLAYING);
    }
    app->game->pauseGameTime = false;
    app->lastTime = (app->time = app->upTimeMs);
    canvas->blockInputTime = app->gameTime + 200;
    app->sound->allowSounds = allowSounds;
    canvas->inInitMap = false;
    canvas->mediaLoading = false;
    canvas->renderScene(canvas->viewX, canvas->viewY, canvas->viewZ, canvas->viewAngle, canvas->viewPitch, 0, 290);

    if (canvas->state != Canvas::ST_CAMERA) {
        canvas->repaintFlags |= Canvas::REPAINT_HUD;
    }
    canvas->repaintFlags |= Canvas::REPAINT_SOFTKEYS;

    if (canvas->state == Canvas::ST_PLAYING) {
        canvas->drawPlayingSoftKeys();
    }

    for (int i = 0; i < 18; i++) {
        app->render->monsterIdleTime[i] = ((app->nextByte() % 10) * 1000) + app->time + 12000;
    }

    if (canvas->state == Canvas::ST_LOADING && !canvas->loadType) {
        canvas->setState(Canvas::ST_PLAYING);
    }

    return true;
}

void LoadingManager::unloadMedia() {
    Applet* app = CAppContainer::getInstance()->app;
    this->freeRuntimeData();
    app->game->unloadMapData();
    app->render->unloadMap();
}

int LoadingManager::getRecentLoadType() {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    if (canvas->recentBriefSave) {
        return 2;
    }
    return 1;
}
