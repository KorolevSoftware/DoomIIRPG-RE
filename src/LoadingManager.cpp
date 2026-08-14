#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "JavaStream.h"
#include "Resource.h"
#include "MayaCamera.h"
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
#include "Text.h"
#include "GLES.h"
#include "Enums.h"
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

void LoadingManager::registerMapMedia(int mediaID) {
    Applet* app = CAppContainer::getInstance()->app;
    Render* render = app->render;

    short mappingsBeg = render->mediaMappings[mediaID];
    short mappingsEnd = render->mediaMappings[mediaID + 1];
    for (; mappingsBeg < mappingsEnd; mappingsBeg++) {
        int palIndex = mappingsBeg;
        if ((render->mediaPalColors[palIndex] & Render::MEDIA_FLAG_REFERENCE) != 0x0) {
            palIndex = (render->mediaPalColors[palIndex] & 0x3FF);
        }
        render->mediaPalColors[palIndex] |= Render::MEDIA_PALETTE_REGISTERED;

        int texelIndex = mappingsBeg;
        if ((render->mediaTexelSizes[texelIndex] & Render::MEDIA_FLAG_REFERENCE) != 0x0) {
            texelIndex = (render->mediaTexelSizes[texelIndex] & 0x3FF);
        }
        render->mediaTexelSizes[texelIndex] |= Render::MEDIA_TEXELS_REGISTERED;
    }
}

void LoadingManager::finalizeMapMedia() {
    Applet* app = CAppContainer::getInstance()->app;
    Render* render = app->render;
    InputStream IS;

    app->canvas->updateLoadingBar(false);
    render->texelMemoryUsage = 0;
    render->paletteMemoryUsage = 0;

    int n = 0;
    int n2 = 0;
    for (int i = 0; i < 1024; ++i) {
        bool b = (render->mediaTexelSizes[i] & Render::MEDIA_TEXELS_REGISTERED) != 0x0;
        bool b2 = (render->mediaPalColors[i] & Render::MEDIA_PALETTE_REGISTERED) != 0x0;

        if (b) {
            int n3 = (render->mediaTexelSizes[i] & 0x3FFFFFFF) + 1;
            render->texelMemoryUsage += n3;
            render->mediaTexelSizes2[n2] = n3;
            render->mediaTexels[n2] = new uint8_t[n3];
            n2++;
        }
        if (b2) {
            int n4 = render->mediaPalColors[i] & 0x3FFFFFFF;
            render->paletteMemoryUsage += 4 * n4;
            render->mediaPalettesSizes[n] = n4;
            render->mediaPalettes[n][0] = new uint16_t[n4];
            n++;
        }
    }

    app->canvas->updateLoadingBar(false);

    if (!IS.loadFile(Resources::RES_NEWPALETTES_BIN_GZ, InputStream::LOADTYPE_RESOURCE)) {
        app->Error("getResource(%s) failed\n", Resources::RES_NEWPALETTES_BIN_GZ);
    }

    // [GEC]: Verifica los datos del las palletas y se corrigen datos si es necesario
    if (checkFileMD5Hash(IS.getData(), IS.getFileSize(), 0xFFE7C84C143EA906, 0xF93B1383F6B2510E)) {
        // WATER STREAM Palette
        IS.data[266852] = 0;
        IS.data[266853] = 0;
        IS.data[266854] = 0;
        IS.data[266855] = 0;
    }

    //app->checkPeakMemory("Loading Palettes");
    int n5 = 0;
    int n6 = 0;
    for (int j = 0; j < 1024; ++j) {
        bool b3 = (render->mediaPalColors[j] & Render::MEDIA_PALETTE_REGISTERED) != 0x0;
        bool b4 = (render->mediaPalColors[j] & Render::MEDIA_FLAG_REFERENCE) != 0x0;
        int n7 = render->mediaPalColors[j] & 0x3FFFFFFF;
        if (b3 && !b4) {
            app->resource->read(&IS, n7 * 2);
            for (int k = 0; k < n7; ++k) {
                render->mediaPalettes[n5][0][k] = app->resource->shiftUShort(); // j2me only -> upSamplePixel(app->resource->shiftUShort());
            }
            int n8 = (j | Render::MEDIA_FLAG_REFERENCE);
            for (int l = j + 1; l < 1024; ++l) {
                if (render->mediaPalColors[l] == n8) {
                    render->mediaPalColors[l] = (0xC0000000 | n5);
                }
            }
            render->mediaPalColors[j] = (0x40000000 | n5);
            ++n5;
            app->resource->readMarker(&IS, n6);
            n6 += (2 * n7) + 4;
        }
        else if (!b4) {
            app->resource->bufSkip(&IS, n7 * 2, true);
            app->resource->readMarker(&IS, n6);
            n6 += (2 * n7) + sizeof(uint32_t);
        }
        if ((j & 0x1E) == 0x1E) {
            app->canvas->updateLoadingBar(false);
        }
    }

    int n9 = 0;
    int n10 = -1;
    int n11 = 0;
    int n12 = 0;
    int m = 0;

    IS.close();
    app->canvas->updateLoadingBar(false);
    //app->checkPeakMemory("Loading Texels");

    for (int n13 = 0; n13 < 1024; ++n13) {
        bool b5 = (render->mediaTexelSizes[n13] & Render::MEDIA_TEXELS_REGISTERED) != 0x0;
        bool b6 = (render->mediaTexelSizes[n13] & Render::MEDIA_FLAG_REFERENCE) != 0x0;
        int n14 = (render->mediaTexelSizes[n13] & 0x3FFFFFFF) + 1;
        if (b5 && !b6) {
            if (m != n10) {
                IS.close();
                /*String str = "/tex0";
                if (m >= 10) {
                    str = "/tex";
                }
                resourceAsStream2 = App.getResourceAsStream(str + m + ".bin");*/
                if (!IS.loadFile(Resources::RES_NEWTEXEL_FILE_ARRAY[m], InputStream::LOADTYPE_RESOURCE)) {
                    app->Error("getResource(%s) failed\n", Resources::RES_NEWTEXEL_FILE_ARRAY[m]);
                }

                n10 = m;
                n11 = 0;
                //Canvas.updateLoadingBar(false);
            }

            if (n11 != n12) {
                app->resource->bufSkip(&IS, n12 - n11, true);
            }
            //printf("n14 %d\n", n14);
            app->resource->readByteArray(&IS, render->mediaTexels[n9], 0, n14);

            // [GEC]: Verifica los datos del sprite de agua animada
            {
                if (n13 == 814) {
                    if (checkFileMD5Hash(render->mediaTexels[n9], n14, 0x2AFCC8EDC9EA8610, 0xEA5458CBEBF345EC)) {
                        render->fixWaterAnim1 = true;
                    }
                }
                else if (n13 == 815) {
                    if (checkFileMD5Hash(render->mediaTexels[n9], n14, 0x14F8BC466131E9AB, 0xFBEC94B21422E569)) {
                        render->fixWaterAnim2 = true;
                    }
                }
                else if (n13 == 816) {
                    if (checkFileMD5Hash(render->mediaTexels[n9], n14, 0xB784065E177D596E, 0x23729441F971FEE7)) {
                        render->fixWaterAnim3 = true;
                    }
                }
                else if (n13 == 817) {
                    if (checkFileMD5Hash(render->mediaTexels[n9], n14, 0x8A53E5E7CD062F73, 0xFAA5B42239006DC8)) {
                        render->fixWaterAnim4 = true;
                    }
                }
            }

            int n15 = (n13 | Render::MEDIA_FLAG_REFERENCE);
            for (int n16 = n13 + 1; n16 < 1024; ++n16) {
                if (render->mediaTexelSizes[n16] == n15) {
                    render->mediaTexelSizes[n16] = (0xC0000000 | n9);
                }
            }
            render->mediaTexelSizes[n13] = (0x40000000 | n9);
            //printf("n9 %d\n", n9);
            //printf("render->mediaTexelSizes[%d] %d\n", n13, render->mediaTexelSizes[n13]);
            ++n9;
            app->resource->readMarker(&IS, n12);
            n12 = (n11 = n12 + (n14 + sizeof(uint32_t)));
        }
        else if (!b6) {
            n12 += n14 + sizeof(uint32_t);
        }

        if (n12 > 0x40000) {
            ++m;
            n12 = 0;
        }
        if ((n13 & 0xF) == 0xF) {
            app->canvas->updateLoadingBar(false);
        }
    }

    IS.close();
    render->_gles->CreateAllActiveTextures();
    app->canvas->updateLoadingBar(false);
}

bool LoadingManager::loadMapData(int mapNameID) {
    Applet* app = CAppContainer::getInstance()->app;
    Render* render = app->render;
    InputStream IS;

    render->mapNameID = mapNameID;
    app->canvas->loadMapStringID = (short)(4 + (render->mapNameID - 1));
    app->localization->loadText(app->canvas->loadMapStringID);

    for (int i = 0; i < 1024; ++i) {
        render->mapFlags[i] = 0;
    }

    render->mapEntranceAutomap = -1;
    render->mapExitAutomap = -1;

    for (int i = 0; i < Render::MAX_LADDERS_PER_MAP; ++i) {
        render->mapLadders[i] = -1;
    }

    for (int i = 0; i < Render::MAX_KEEP_PITCH_LEVEL_TILES; ++i) {
        render->mapKeepPitchLevelTiles[i] = -1;
    }

    render->portalState = Render::PORTAL_DNE;
    render->previousPortalState = 0;
    render->portalScripted = false;
    render->mapNameField = (0xC00 | app->game->levelNames[render->mapNameID - 1]);

    render->mediaMappings = new short[Render::MEDIA_MAX_MAPPINGS];
    render->mediaDimensions = new uint8_t[Render::MEDIA_MAX_IMAGES];
    render->mediaBounds = new short[(Render::MEDIA_MAX_IMAGES * 4)];
    render->mediaPalColors = new int[Render::MEDIA_MAX_IMAGES];
    render->mediaPalettesSizes = new int[Render::MEDIA_MAX_IMAGES];
    render->mediaTexelSizes = new int[Render::MEDIA_MAX_IMAGES];
    render->mediaTexelSizes2 = new int[Render::MEDIA_MAX_IMAGES];
    render->mediaTexels = new uint8_t*[1024]();
    render->mediaPalettes = new uint16_t **[1024]();
    for (int i = 0; i < 1024; ++i) {
        render->mediaPalettes[i] = new uint16_t *[16]();
        for (int j = 0; j < 16; ++j) {
            render->mediaPalettes[i][j] = nullptr;
        }
    }

    app->canvas->updateLoadingBar(false);
    IS.loadResource(Resources::RES_NEWMAPPINGS_BIN_GZ);
    app->resource->readShortArray(&IS, render->mediaMappings, 0, Render::MEDIA_MAX_MAPPINGS);
    app->resource->readByteArray(&IS, (uint8_t*)render->mediaDimensions, 0, Render::MEDIA_MAX_IMAGES);
    app->resource->readShortArray(&IS, render->mediaBounds, 0, (Render::MEDIA_MAX_IMAGES * 4));
    app->resource->readIntArray(&IS, render->mediaPalColors, 0, Render::MEDIA_MAX_IMAGES);
    app->resource->readIntArray(&IS, render->mediaTexelSizes, 0, Render::MEDIA_MAX_IMAGES);
    IS.close();

    render->mediaMappings[Enums::TILENUM_SKY_BOX] = (gles::MAX_MEDIA-1); // Readjust the index so it doesn't interfere with the fade texture.

    app->canvas->updateLoadingBar(false);

    IS.loadResource(Resources::RES_MAP_FILE_ARRAY[(mapNameID - 1)]);
    app->resource->read(&IS, 42);
    if (app->resource->shiftUByte() != 3) {
        app->Error(68); // ERR_BADMAPVERSION
        return false;
    }

    render->mapCompileDate = app->resource->shiftInt();
    render->mapSpawnIndex = app->resource->shiftUShort();
    render->mapSpawnDir = app->resource->shiftUByte();
    render->mapFlagsBitmask = app->resource->shiftByte();
    app->game->totalSecrets = app->resource->shiftByte();
    app->game->totalLoot = app->resource->shiftUByte();
    render->numNodes = app->resource->shiftUShort();
    int dataSizePolys = app->resource->shiftUShort();
    render->numLines = app->resource->shiftUShort();
    render->numNormals = app->resource->shiftUShort();
    render->numNormalSprites = app->resource->shiftUShort();
    render->numZSprites = app->resource->shiftShort();
    render->numMapSprites = render->numNormalSprites + render->numZSprites;
    render->numSprites = render->numMapSprites + Render::MAX_CUSTOM_SPRITES + Render::MAX_DROP_SPRITES;
    render->numTileEvents = (int)app->resource->shiftShort();
    render->mapByteCodeSize = (int)app->resource->shiftShort();
    app->game->totalMayaCameras = app->resource->shiftByte();
    app->game->totalMayaCameraKeys = app->resource->shiftShort();
    app->canvas->updateLoadingBar(false);

    short totalMayaTweens = 0;
    for (int l = 0; l < 6; ++l) {
        app->game->ofsMayaTween[l] = totalMayaTweens;
        short shiftShort = app->resource->shiftShort();
        if (shiftShort != -1) {
            totalMayaTweens += shiftShort;
        }
    }
    app->game->totalMayaTweens = totalMayaTweens;

    // Load Media data
    app->resource->readMarker(&IS, 0xDEADBEEF);
    app->resource->read(&IS, 2);
    int mediaCount = app->resource->shiftUShort();
    app->resource->read(&IS, mediaCount * 2);
    for (int i = 0; i < mediaCount; i++) {
        this->registerMapMedia(app->resource->shiftUShort());
    }
    app->resource->readMarker(&IS, 0xDEADBEEF);
    IS.close();
    this->finalizeMapMedia();

    //-----------------------------
    render->nodeNormalIdxs = new uint8_t[render->numNodes];
    render->nodeOffsets = new short[render->numNodes];
    render->nodeChildOffset1 = new short[render->numNodes];
    render->nodeChildOffset2 = new short[render->numNodes];
    render->nodeSprites = new short[render->numNodes];
    render->nodeBounds = new uint8_t[render->numNodes * 4];
    render->nodePolys = new uint8_t[dataSizePolys];
    render->lineFlags = new uint8_t[(render->numLines + 1) / 2];
    render->lineXs = new uint8_t[render->numLines * 2];
    render->lineYs = new uint8_t[render->numLines * 2];
    render->normals = new short[render->numNormals * 3];
    render->heightMap = new uint8_t[1024];

    for (int i = 0; i < render->numNodes; ++i) {
        render->nodeSprites[i] = -1;
    }

    render->mapSprites = new short[render->numSprites * 10];
    for (int i = 0; i < render->numSprites * 10; ++i) {
        render->mapSprites[i] = 0;
    }

    render->mapSpriteInfo = new int[render->numSprites * 2];
    for (int i = 0; i < render->numSprites * 2; ++i) {
        render->mapSpriteInfo[i] = 0;
    }

    render->S_X = render->numSprites * 0;
    render->S_Y = render->numSprites * 1;
    render->S_Z = render->numSprites * 2;
    render->S_RENDERMODE = render->numSprites * 3;
    render->S_NODE = render->numSprites * 4;
    render->S_NODENEXT = render->numSprites * 5;
    render->S_VIEWNEXT = render->numSprites * 6;
    render->S_ENT = render->numSprites * 7;
    render->S_SCALEFACTOR = render->numSprites * 8;
    render->SINFO_SORTZ = render->numSprites;

    render->tileEvents = new int[render->numTileEvents * 2];
    render->mapByteCode = new uint8_t[render->mapByteCodeSize];
    app->game->mayaCameras = new MayaCamera[app->game->totalMayaCameras];
    app->game->mayaCameraKeys = new short[app->game->totalMayaCameraKeys * 7];
    app->game->mayaCameraTweens = new int8_t[app->game->totalMayaTweens];
    app->game->mayaTweenIndices = new short[app->game->totalMayaCameraKeys * 6];
    app->game->setKeyOffsets();

    //app->checkPeakMemory("Allocated memory for the map");
    IS.loadResource(Resources::RES_MAP_FILE_ARRAY[(mapNameID - 1)]);
    app->canvas->updateLoadingBar(false);
    //app->checkPeakMemory(, "Reading in final map data");

    app->resource->read(&IS, 42);
    app->resource->readMarker(&IS, 0xDEADBEEF);
    app->resource->read(&IS, mediaCount * 2 + 2);
    app->resource->readMarker(&IS, 0xDEADBEEF);
    app->resource->readShortArray(&IS, render->normals, 0, render->numNormals * 3);
    app->resource->readMarker(&IS);
    app->resource->readShortArray(&IS, render->nodeOffsets, 0, render->numNodes);
    app->resource->readMarker(&IS);
    app->resource->readByteArray(&IS, render->nodeNormalIdxs, 0, render->numNodes);
    app->resource->readMarker(&IS);
    app->resource->readShortArray(&IS, render->nodeChildOffset1, 0, render->numNodes);
    app->resource->readShortArray(&IS, render->nodeChildOffset2, 0, render->numNodes);
    app->resource->readMarker(&IS);
    app->resource->readByteArray(&IS, render->nodeBounds, 0, render->numNodes * 4);
    app->resource->readMarker(&IS);
    app->canvas->updateLoadingBar(false);
    app->resource->readByteArray(&IS, render->nodePolys, 0, dataSizePolys);
    app->resource->readMarker(&IS);
    app->resource->readByteArray(&IS, render->lineFlags, 0, (render->numLines + 1) / 2);
    app->resource->readByteArray(&IS, render->lineXs, 0, render->numLines * 2);
    app->resource->readByteArray(&IS, render->lineYs, 0, render->numLines * 2);
    app->resource->readMarker(&IS);
    app->resource->readByteArray(&IS, render->heightMap, 0, 1024);
    app->resource->readMarker(&IS);
    app->canvas->updateLoadingBar(false);
    app->resource->readCoordArray(&IS, render->mapSprites, render->S_X, render->numMapSprites);
    app->resource->readCoordArray(&IS, render->mapSprites, render->S_Y, render->numMapSprites);
    app->canvas->updateLoadingBar(false);

    for (int i = 0; i < render->numMapSprites; i++) {
        render->mapSprites[i + render->S_NODE] = -1;
        render->mapSprites[i + render->S_NODENEXT] = -1;
        render->mapSprites[i + render->S_VIEWNEXT] = -1;
        render->mapSprites[i + render->S_ENT] = -1;
        render->mapSprites[i + render->S_SCALEFACTOR] = 64;
        render->mapSprites[i + render->S_Z] = 32;
    }

    int n5 = 0;
    int numMapSprites = render->numMapSprites;
    while (numMapSprites > 0) {
        int n6 = (Resource::IO_SIZE > numMapSprites) ? numMapSprites : Resource::IO_SIZE;
        numMapSprites -= n6;
        app->resource->read(&IS, n6);
        while (--n6 >= 0) {
            render->mapSpriteInfo[n5++] = app->resource->shiftUByte();
        }
    }

    app->resource->readMarker(&IS);
    app->canvas->updateLoadingBar(false);

    int n7 = 0;
    int numMapSprites2 = render->numMapSprites;
    while (numMapSprites2 > 0) {
        int n8 = ((Resource::IO_SIZE / 2) > numMapSprites2) ? numMapSprites2 : (Resource::IO_SIZE / 2);
        numMapSprites2 -= n8;
        app->resource->read(&IS, n8 * 2);
        while (--n8 >= 0) {
            render->mapSpriteInfo[n7++] |= (app->resource->shiftUShort() & 0xFFFF) << 16;
        }
    }

    app->resource->readMarker(&IS);
    app->resource->readUByteArray(&IS, render->mapSprites, render->S_Z + render->numNormalSprites, render->numZSprites);
    app->resource->readMarker(&IS);

    int numNormalSprites = render->numNormalSprites;
    int numZSprites = render->numZSprites;
    while (numZSprites > 0) {
        int n10 = (Resource::IO_SIZE > numZSprites) ? numZSprites : Resource::IO_SIZE;
        numZSprites -= n10;
        app->resource->read(&IS, n10);
        while (--n10 >= 0) {
            render->mapSpriteInfo[numNormalSprites++] |= app->resource->shiftUByte() << 8;
        }
    }
    app->canvas->updateLoadingBar(false);
    app->resource->readMarker(&IS);
    app->resource->readUShortArray(&IS, render->staticFuncs, 0, 12);
    app->resource->readMarker(&IS);
    app->resource->readIntArray(&IS, render->tileEvents, 0, render->numTileEvents * 2);

    for (int i = 0; i < render->numTileEvents; i++) {
        int index = render->tileEvents[i << 1] & 0x3FF;
        render->mapFlags[index] |= 0x40;
    }

    app->resource->readMarker(&IS);
    app->resource->readByteArray(&IS, render->mapByteCode, 0, render->mapByteCodeSize);
    app->resource->readMarker(&IS);
    app->canvas->updateLoadingBar(false);
    app->game->loadMayaCameras(&IS);
    app->resource->readMarker(&IS);
    app->resource->read(&IS, 512);

    int cnt = 0;
    for (int i = 0; i < 512; i++) {
        short flags = app->resource->shiftUByte();
        render->mapFlags[cnt++] |= (uint8_t)(flags & 0xF);
        render->mapFlags[cnt++] |= (uint8_t)((flags >> 4) & 0xF);
    }
    app->resource->readMarker(&IS);
    IS.close();

    app->canvas->updateLoadingBar(false);
    render->postProcessSprites();

    int skyIndex = ((render->mapNameID - 1) / 5 % 2) * 2;
    int skyPal = app->resource->getNumTableShorts(skyIndex + 16);
    int skyTexel = app->resource->getNumTableBytes(skyIndex + 17);

    render->skyMapPalette = new uint16_t*[16];
    for (int i = 0; i < 16; i++) {
        render->skyMapPalette[i] = new uint16_t[skyPal];
    }
    render->skyMapTexels = new uint8_t[skyTexel];

    app->resource->beginTableLoading();
    app->resource->loadUShortTable(render->skyMapPalette[0], skyIndex + 16);
    app->resource->loadUByteTable(render->skyMapTexels, skyIndex + 17);
    app->resource->finishTableLoading();
    app->canvas->updateLoadingBar(false);

    for (int n19 = 0; n19 < 1024; n19++) {
        if (render->mediaPalettes[n19][0] != nullptr) {
            int length = render->mediaPalettesSizes[n19];
            for (int n20 = 1; n20 < 16; n20++) {
                render->paletteMemoryUsage += 4 * length;
                render->mediaPalettes[n19][n20] = new uint16_t[length];
            }
        }
    }

    app->canvas->changeMapStarted = false;
    render->destDizzy = 0;
    render->baseDizzy = 0;

    return true;
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

    if (!this->loadMapData(canvas->loadMapID)) {
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
