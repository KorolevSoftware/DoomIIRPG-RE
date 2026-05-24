#include <cstdio>
#include <algorithm>

#include "SDLGL.h"
#include "ZipFile.h"

#include "CAppContainer.h"
#include "App.h"
#include "Text.h"
#include "Resource.h"
#include "Render.h"
#include "TinyGL.h"
#include "Canvas.h"
#include "Combat.h"
#include "Game.h"
#include "MenuSystem.h"
#include "Player.h"
#include "Sound.h"
#include "Combat.h"
#include "Hud.h"
#include "EntityDef.h"
#include "ParticleSystem.h"
#include "HackingGame.h"
#include "SentryBotGame.h"
#include "VendingMachine.h"
#include "ComicBook.h"
#include "JavaStream.h"
#include "Image.h"
#include "Graphics.h"

Applet::Applet() {
	std::memset(this, 0, sizeof(Applet));
}

Applet::~Applet() {
}

bool Applet::startup() {
	printf("Applet::startup\n");

	this->closeApplet = false;
	this->fontType = 0;
	this->accelerationIndex = 0;
	this->field_0x290 = '\0';
	this->field_0x291 = '\0';

	// Iphone Only
	{
		for (int i = 0; i < 32; i++) {
			accelerationX[i] = 0.0f;
			accelerationY[i] = 0.0f;
			accelerationZ[i] = 0.0f;
		}
	}

	this->field_0x414 = 0;
	this->field_0x418 = 0;
	this->field_0x41c = 0;
	this->field_0x420 = 0;
	this->field_0x424 = 0;
	this->field_0x428 = 0;

	this->backBuffer = new Image;
	this->backBuffer->colorsIndexes =  new uint8_t[480 * 320 *2];
	std::memset(this->backBuffer->colorsIndexes, 0, 480 * 320 * 2);
	this->backBuffer->RGB565Palette = nullptr;
	this->backBuffer->width = CAppContainer::getInstance()->sdlGL->vidWidth;
	this->backBuffer->height = CAppContainer::getInstance()->sdlGL->vidHeight;

	printf("w: %d || h: %d\n", backBuffer->width, backBuffer->height);

	this->initLoadImages = false;
	this->time = 0;
	this->upTimeMs = 0;
	this->field_0x7c = 0;
	this->field_0x80 = 0;
	this->canvas = new Canvas;
	this->resource = new Resource;
	this->localization = new Localization;
	this->render = new Render;
	this->tinyGL = new TinyGL;
	this->game = new Game;
	this->menuSystem = new MenuSystem;
	this->player = new Player;
	this->sound = new Sound;
	this->combat = new Combat;
	this->hud = new Hud;
	this->entityDefManager = new EntityDefManager;
	this->particleSystem = new ParticleSystem;
	this->hackingGame = new HackingGame;
	this->sentryBotGame = new SentryBotGame;
	this->vendingMachine = new VendingMachine;
	this->comicBook = new ComicBook;

	Applet::loadConfig();
	//this->moreGames = getStartupVarBool("More_Games", false);
	//this->systemFont = getStartupVarBool("System_Font", false);
	int time = this->upTimeMs;
	this->gameTime = this->upTimeMs;
	this->startupMemory = Applet::MAXMEMORY;

	if (this->canvas->startup()) {
		this->testImg = Applet::loadImage("cockpit.bmp", true);

		this->canvas->loadMiniGameImages();
		if (this->localization->startup()) {
			if (this->render->startup()) {
				this->resource->initTableLoading();
				this->loadTables();

				if (this->tinyGL->startup(this->render->screenWidth, this->render->screenHeight)) {
					if (this->entityDefManager->startup()) {
						if (this->player->startup()) {
							if (this->menuSystem->startup()) {
								if (this->sound->startup()) {
									if (this->game->startup()) {
										if (this->particleSystem->startup()) {
											if (this->combat->startup()) {

												this->game->loadConfig();
												if (this->canvas->isFlipControls != false) {
													this->canvas->isFlipControls = false;
													this->canvas->flipControls();
												}

												this->canvas->setControlLayout();
												this->canvas->clearEvents(1);
												this->canvas->setState(Canvas::ST_LOGO);
												this->canvas->graphics.backBuffer = this->backBuffer;
												this->canvas->graphics.graphClipRect[0] = 0;
												this->canvas->graphics.graphClipRect[1] = 0;
												this->canvas->graphics.graphClipRect[2] = this->backBuffer->width;
												this->canvas->graphics.graphClipRect[3] = this->backBuffer->height;

												this->accelerationIndex = 0;
												this->field_0x290 = false;
												this->field_0x291 = '\0';
												//this->accelStart();
												printf("**** Startup took %i ms\n", this->upTimeMs - time);
												printf("**** Fragment size %i ms\n", 0);

												return true;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	printf("error faltal:\n");
	return false;
}

void Applet::loadConfig() {

}

Image* Applet::createImage(InputStream* inputStream, bool isTransparentMask) {
// read header
#pragma pack(push, 1)
	struct ImageDesc { // Windows BITMAPINFOHEADER
	    char    BMPIdentifier[2];
		int     fileSize;
		char    reserved[4];
		int32_t offBeg;
		int32_t headerSize;
		int32_t width;
		int32_t height;
		int16_t colorPlanes;      // must be 1
		int16_t bitsPerPixel;
		int32_t compression;      // 0 = BI_RGB (no compression)
		int32_t imageSize;        // raw bitmap size; may be 0 for BI_RGB
		int32_t xPixelsPerMeter;
		int32_t yPixelsPerMeter;
		int32_t colorsUsed;
		int32_t importantColors;  // 0 = all colors required
	};
#pragma pack(pop)
	static_assert(sizeof(ImageDesc) == 54, "ImageDesc binary layout mismatch");
	ImageDesc desc = inputStream->readByDesc<ImageDesc>();

	// read pixels
	Image* newIamge = new Image;
	newIamge->texture = -1;
	newIamge->width = desc.width;
	newIamge->height = desc.height;
	newIamge->depth = desc.bitsPerPixel;
	newIamge->isTransparentMask = isTransparentMask;


	if (desc.bitsPerPixel != 4 && desc.bitsPerPixel != 8) {
        Error("Expected image bpp 4 or 8. Found bpp %d", desc.bitsPerPixel);
	    return nullptr;
	}

	if (desc.colorsUsed == 0) {
		desc.colorsUsed = 1 << (desc.bitsPerPixel & 0xff);
	}

	// load palette
	uint32_t* RGB888Palette = new uint32_t[desc.colorsUsed];
	uint16_t* RGB565Palette = new uint16_t[desc.colorsUsed];

	inputStream->readArray<uint32_t>(RGB888Palette, desc.colorsUsed);

	for (uint32_t i = 0; i < desc.colorsUsed; i++) {
		RGB888Palette[i] = SDL_SwapLE32(RGB888Palette[i]);
	}


	if (isTransparentMask) { // if convert 888 to 565 color maby equval 0 its
    	SDL_ConvertPixels(desc.colorsUsed, 1,
    	    SDL_PIXELFORMAT_RGB888, RGB888Palette, desc.colorsUsed * sizeof(uint32_t),
    		SDL_PIXELFORMAT_RGB565, RGB565Palette, desc.colorsUsed * sizeof(uint16_t)
    	);

    	for (uint32_t i = 0; i < desc.colorsUsed; i++) {
    	    int rgb = RGB888Palette[i];
    		RGB888Palette[i] = std::max(rgb, 8);
    	}
	}

	SDL_ConvertPixels(desc.colorsUsed, 1,
	    SDL_PIXELFORMAT_RGB888, RGB888Palette, desc.colorsUsed * sizeof(uint32_t),
		SDL_PIXELFORMAT_RGB565, RGB565Palette, desc.colorsUsed * sizeof(uint16_t)
	);

	delete [] RGB888Palette;
	newIamge->RGB565Palette = RGB565Palette;

	const int bitsPerRow  = desc.bitsPerPixel * desc.width;
	const int srcStride   = ((bitsPerRow + 31) / 32) * 4;  // BMP row stride in bytes (4-byte aligned)
	const int copyBytes   = (bitsPerRow + 7) / 8;          // useful pixel bytes per row (byte-aligned)
	const int pixelCount  = desc.height * desc.width;

	const size_t pixelDataSizeOf = srcStride * desc.height;
	uint8_t* data = new uint8_t[pixelDataSizeOf];
	inputStream->readArray<uint8_t>(data, pixelDataSizeOf);

	newIamge->colorsIndexes = new uint8_t[pixelCount];

    // Step 2: convert 4bpp → 8bpp
    uint8_t* pixelData = data;
    int pixelStride = srcStride;

    if (desc.bitsPerPixel == 4) {
        uint8_t* unpacked = new uint8_t[pixelCount];
        for (int row = 0; row < desc.height; row++) {
            uint8_t* src = data + srcStride * row;
            uint8_t* dst = unpacked + desc.width * row;
            for (int col = 0; col < desc.width / 2; col++) {
                dst[col * 2]     = src[col] >> 4;
                dst[col * 2 + 1] = src[col] & 0xf;
            }
        }
        pixelData = unpacked;
        pixelStride = desc.width; // нет BMP-паддинга, строка = width байт
    }

    // Step 3: flip
    for (int row = 0; row < newIamge->height; row++) {
        std::memcpy(newIamge->colorsIndexes + newIamge->width * row,
            pixelData + pixelStride * (newIamge->height - 1 - row),
            newIamge->width);
    }

    if (pixelData != data) delete[] pixelData;
    delete[] data;
    return newIamge;
}

Image* Applet::loadImage(char* fileName, bool isTransparentMask) {
	InputStream iStream = InputStream();
	Image* img;

	if (iStream.loadResource(fileName) == 0) {
		Applet::Error("Failed to open image: %s", fileName);
	}

	img = this->createImage(&iStream, isTransparentMask);

	iStream.close();
	iStream.~InputStream();
	return img;
}

#include <stdarg.h> //va_list|va_start|va_end
void Applet::Error(const char* fmt, ...) {

	char errMsg[256];
	va_list ap;
	va_start(ap, fmt);
#ifdef _WIN32
	vsprintf_s(errMsg, sizeof(errMsg), fmt, ap);
#else
	vsnprintf(errMsg, sizeof(errMsg), fmt, ap);
#endif
	va_end(ap);

	SDL_SetWindowFullscreen(CAppContainer::getInstance()->sdlGL->window, 0);

	printf("%s", errMsg);

	const SDL_MessageBoxButtonData buttons[] = {
		{ /* .flags, .buttonid, .text */        0, 0, "Ok" },
	};
	const SDL_MessageBoxColorScheme colorScheme = {
		{ /* .colors (.r, .g, .b) */
			/* [SDL_MESSAGEBOX_COLOR_BACKGROUND] */
			{ 255,   0,   0 },
			/* [SDL_MESSAGEBOX_COLOR_TEXT] */
			{   0, 255,   0 },
			/* [SDL_MESSAGEBOX_COLOR_BUTTON_BORDER] */
			{ 255, 255,   0 },
			/* [SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND] */
			{   0,   0, 255 },
			/* [SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED] */
			{ 255,   0, 255 }
		}
	};
	const SDL_MessageBoxData messageboxdata = {
		SDL_MESSAGEBOX_ERROR, /* .flags */
		NULL, /* .window */
		"Doom II RPG Error", /* .title */
		errMsg, /* .message */
		SDL_arraysize(buttons), /* .numbuttons */
		buttons, /* .buttons */
		&colorScheme /* .colorScheme */
	};

	SDL_ShowMessageBox(&messageboxdata, NULL);

	CAppContainer::getInstance()->~CAppContainer();
	exit(0);
}

void Applet::Error(int id)
{
	std::printf("Error id: %i\n", id);
	this->Error("App Error");
	this->idError = id;
}

void Applet::beginImageLoading() {
}

void Applet::endImageLoading() {
}

void Applet::loadTables() {
	int count01, count02, count03, count04, count05, count06, count07;
	int count08, count09, count10, count11, count12, count13, count14;

	count01 = this->resource->getNumTableShorts(0); // count TBL_COMBAT_MONSTERATTACKS
	count02 = this->resource->getNumTableBytes(1);	// count TBL_COMBAT_WEAPONINFO
	count03 = this->resource->getNumTableBytes(2);	// count TBL_COMBAT_WEAPONDATA
	count04 = this->resource->getNumTableBytes(3);	// count TBL_COMBAT_MONSTERSTATS
	count05 = this->resource->getNumTableInts(4);	// count TBL_COMBAT_COMBATMASKS
	count06 = this->resource->getNumTableBytes(5);	// count TBL_CANVAS_KEYSNUMERIC
	count07 = this->resource->getNumTableBytes(6);	// count TBL_ENUMS_OSC_CYCLE
	count08 = this->resource->getNumTableShorts(7); // count TBL_GAME_LEVELNAMES
	count09 = this->resource->getNumTableBytes(8);	// count TBL_MONSTER_COLORS
	count10 = this->resource->getNumTableInts(9);	// count TBL_RENDER_SINETABLE
	count11 = this->resource->getNumTableShorts(10);// count TBL_ENERGY_DRINK_DATA
	count12 = this->resource->getNumTableBytes(11); // count TBL_MONSTER_WEAKNESS
	count13 = this->resource->getNumTableInts(12);	// count TBL_UNK
	count14 = this->resource->getNumTableBytes(13); // count TBL_MONSTER_SOUNDS

	this->combat->monsterAttacks = new short[count01];
	this->combat->wpinfo = new int8_t[count02];
	this->combat->weapons = new int8_t[count03];
	this->combat->monsterStats = new int8_t[count04];
	this->combat->tableCombatMasks = new int32_t[count05];
	this->canvas->keys_numeric = new int8_t[count06];
	this->canvas->OSC_CYCLE = new int8_t[count07];
	this->game->levelNames = new int16_t[count08];
	this->particleSystem->monsterColors = new uint8_t[count09];
	this->render->sinTable = new int32_t[count10];
	this->vendingMachine->energyDrinkData = new int16_t[count11];
	this->combat->monsterWeakness = new int8_t[count12];
	this->canvas->movieEffects = new int32_t[count13];
	this->game->monsterSounds = new uint8_t[count14];

	this->resource->beginTableLoading();
	this->resource->loadShortTable(this->combat->monsterAttacks, 0);
	this->resource->loadByteTable(this->combat->wpinfo, 1);
	this->resource->loadByteTable(this->combat->weapons, 2);
	this->resource->loadByteTable(this->combat->monsterStats, 3);
	this->resource->loadIntTable(this->combat->tableCombatMasks, 4);
	this->resource->loadByteTable(this->canvas->keys_numeric, 5);
	this->resource->loadByteTable(this->canvas->OSC_CYCLE, 6);
	this->game->levelNamesCount = this->resource->loadShortTable(this->game->levelNames, 7);
	this->resource->loadByteTable((int8_t*)this->particleSystem->monsterColors, 8);
	this->resource->loadIntTable(this->render->sinTable, 9);
	this->resource->loadShortTable(this->vendingMachine->energyDrinkData, 10);
	this->resource->loadByteTable(this->combat->monsterWeakness, 11);
	this->resource->loadIntTable(this->canvas->movieEffects, 12);
	this->resource->loadByteTable((int8_t*)this->game->monsterSounds, 13);
	this->resource->finishTableLoading();

	//for (int i = 0; i < count14; i++) {
	//	printf("data[%d] = %d\n", i, (uint8_t)this->game->monsterSounds[i]);
	//}
}

void Applet::loadRuntimeImages() {
	//printf("Applet::loadRuntimeImages\n");
	//printf("this->initLoadImages %d\n", this->initLoadImages);
	if (this->initLoadImages == false) {
		this->imageMemory = 1000000000;

		this->canvas->imgMapCursor = this->loadImage("Automap_Cursor.bmp", true);
		this->canvas->imgUIImages = this->loadImage("ui_images.bmp", true);
		this->hud->imgCockpitOverlay = this->loadImage("cockpit.bmp", true);
		this->hud->imgDamageVignette = this->loadImage("damage.bmp", true);
		this->hud->imgDamageVignetteBot = this->loadImage("damage_bot.bmp", true);
		this->canvas->imgDialogScroll = this->loadImage("DialogScroll.bmp", true);
		this->hud->imgActions = this->loadImage("Hud_Actions.bmp", true);
		this->hud->imgBottomBarIcons = this->loadImage("Hud_Fill.bmp", true);
		this->hud->imgHudFill = this->loadImage("Hud_Actions.bmp", true);

		delete this->hud->imgPlayerFrameNormal;
		this->hud->imgPlayerFrameNormal = nullptr;
		delete this->hud->imgPlayerFrameActive;
		this->hud->imgPlayerFrameActive = nullptr;

		this->hud->imgPlayerFrameNormal = this->loadImage("HUD_Player_frame_Normal.bmp", true);
		this->hud->imgPlayerFrameActive = this->loadImage("HUD_Player_frame_Active.bmp", true);

		delete this->hud->imgPlayerFaces;
		this->hud->imgPlayerFaces = nullptr;
		delete this->hud->imgPlayerActive;
		this->hud->imgPlayerActive = nullptr;

		if (this->player->characterChoice == 1) {
			this->hud->imgPlayerFaces = this->loadImage("Hud_Player.bmp", true);
			this->hud->imgPlayerActive = this->loadImage("HUD_Player_Active.bmp", true);
		}
		else if (this->player->characterChoice == 2) {
			this->hud->imgPlayerFaces = this->loadImage("Hud_PlayerDoom.bmp", true);
			this->hud->imgPlayerActive = this->loadImage("HUD_PlayerDoom_Active.bmp", true);
		}
		else if (this->player->characterChoice == 3) {
			this->hud->imgPlayerFaces = this->loadImage("Hud_PlayerScientist.bmp", true);
			this->hud->imgPlayerActive = this->loadImage("HUD_PlayerScientist_Active.bmp", true);
		}

		this->hud->imgSentryBotFace = this->loadImage("Hud_Sentry.bmp", true);
		this->hud->imgSentryBotActive = this->loadImage("HUD_sentry_active.bmp", true);

		this->hud->imgHudTest = this->loadImage("Hud_Test.bmp", true);
		this->hud->imgScope = this->loadImage("scope.bmp", true);

		this->canvas->updateLoadingBar(false);
		this->imageMemory = this->imageMemory + -1000000000;
	}
}

void Applet::freeRuntimeImages() {
	delete this->canvas->imgMapCursor;
	this->canvas->imgMapCursor = nullptr;
	delete this->canvas->imgUIImages;
	this->canvas->imgUIImages = nullptr;
	delete this->canvas->imgDialogScroll;
	this->canvas->imgDialogScroll = nullptr;
	delete this->hud->imgScope;
	this->hud->imgScope = nullptr;
	delete this->hud->imgDamageVignette;
	this->hud->imgDamageVignette = nullptr;
	delete this->hud->imgActions;
	this->hud->imgActions = nullptr;
	delete this->hud->imgBottomBarIcons;
	this->hud->imgBottomBarIcons = nullptr;
	delete this->hud->imgHudFill;
	this->hud->imgHudFill = nullptr;
	delete this->hud->imgPlayerFaces;
	this->hud->imgPlayerFaces = nullptr;
	delete this->hud->imgPlayerActive;
	this->hud->imgPlayerActive = nullptr;
	delete this->hud->imgDamageVignetteBot;
	this->hud->imgDamageVignetteBot = nullptr;
	delete this->hud->imgHudTest;
	this->hud->imgHudTest = nullptr;
	delete this->hud->imgSentryBotFace;
	this->hud->imgSentryBotFace = nullptr;
	delete this->hud->imgSentryBotActive;
	this->hud->imgSentryBotActive = nullptr;
	delete this->hud->imgCockpitOverlay;
	this->hud->imgCockpitOverlay = nullptr;
}

void Applet::setFont(int fontType) {
	if (fontType <= 3) {
		this->fontType = fontType;
	}
}

void Applet::shutdown() {
	this->closeApplet = true;
}

uint32_t Applet::nextInt() {
	return std::rand() & INT32_MAX;
}

uint32_t Applet::nextByte() {
	return this->nextInt() & UINT8_MAX;
}

void Applet::setFontRenderMode(int fontRenderMode) {
	this->canvas->fontRenderMode = fontRenderMode;
}


void Applet::AccelerometerUpdated(float x, float y, float z) {

	this->accelerationX[this->accelerationIndex] = x;
	this->accelerationY[this->accelerationIndex] = y;
	this->accelerationZ[this->accelerationIndex] = z;
	this->accelerationIndex = (this->accelerationIndex + 1) % 32;

	int v7 = (uint8_t)this->field_0x291;
	int v8 = v7 == 0;
	if (!v7) {
		v8 = this->accelerationIndex == 0;
	}
	if (v8) {
		this->field_0x291 = v7 + 1;
	}
	//this->comicBook->UpdateAccelerometer(x, y, z);
}

void Applet::StartAccelerometer() {
	this->accelerationIndex = 0;
	this->field_0x290 = false;
	this->field_0x291 = false;
}

void Applet::StopAccelerometer() {
	this->accelerationIndex = 0;
	this->field_0x290 = false;
	this->field_0x291 = false;
}

void Applet::CalcAccelerometerAngles() {
	bool v2; // zf
	float y; // s13
	int v5; // r2
	float x; // s14
	float z; // s12
	float v9; // s11
	float v10; // s14
	int zoomAngle; // r3
	int v14; // r3
	int zoomMaxAngle; // r3
	int zoomPitch; // r1

	v2 = this->field_0x291 == false;
	if (this->field_0x291)
	{
		v2 = !this->canvas->isZoomedIn;
	}
	if (!v2)
	{
		v5 = 0;
		x = 0.0;
		y = 0.0;
		z = 0.0;
		do{
			x += this->accelerationX[v5];
			y += this->accelerationY[v5];
			z += this->accelerationZ[v5];
		} while (++v5 < 32);

		this->field_0x414 = x * 0.03125;
		this->field_0x418 = y * 0.03125;
		this->field_0x41c = z * 0.03125;

		if (!this->field_0x290)
		{
			this->field_0x290 = true;
			this->field_0x420 = x * 0.03125;
			this->field_0x424 = y * 0.03125;
			this->field_0x428 = z * 0.03125;
			return;
		}
		this->canvas->zoomAngle = (int)(float)((float)(this->field_0x414 - this->field_0x420) * 420.0);

		zoomAngle = this->canvas->zoomAngle;
		if (zoomAngle >= -200)
		{
			if (zoomAngle <= 200)
				goto LABEL_13;
			v14 = 200;
		}
		else
		{
			v14 = -200;
		}
		this->canvas->zoomAngle = v14;
		this->canvas = this->canvas;
	LABEL_13:
		this->canvas->zoomPitch = (int)(float)((float)(this->field_0x418 - this->field_0x424) * 420.0);
		zoomMaxAngle = this->canvas->zoomMaxAngle;
		zoomPitch = this->canvas->zoomPitch;
		if (zoomPitch >= -zoomMaxAngle)
		{
			if (zoomPitch > zoomMaxAngle)
				this->canvas->zoomPitch = zoomMaxAngle;
		}
		else
		{
			this->canvas->zoomPitch = -zoomMaxAngle;
		}
	}
}
