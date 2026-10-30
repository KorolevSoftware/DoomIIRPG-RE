#ifndef NEW_MINIGAMES_SENTRYBOT_SENTRYBOTASSETS_H
#define NEW_MINIGAMES_SENTRYBOT_SENTRYBOTASSETS_H

#include "render/api/Texture.h"
#include "ui/UiAssets.h"

namespace newcore {

class TextureStore;

// Sheets only the sentry bot minigame draws (src/LoadingManager.cpp:88-92;
// matrixSkip_button.bmp is loaded in initGame, src/SentryBotGame.cpp:90-92).
struct SentryBotAssets {
	Texture background;   // matrixSkip_BG.bmp (imgMatrixSkip_BG)
	Texture game;         // sentryGame.bmp (imgGameAssets)
	Texture pressed[3];   // matrixSkip_grid_pressed.bmp, _2.bmp, _3.bmp (imgButton, imgButton2, imgButton3)
	Texture button;       // matrixSkip_button.bmp (legacy loads it 3x as imgSubmit/imgUnk1/imgDelete: one copy suffices)

	bool load(TextureStore& store, const UiAssets::ResourceReader& read);
};

} // namespace newcore

#endif // NEW_MINIGAMES_SENTRYBOT_SENTRYBOTASSETS_H
