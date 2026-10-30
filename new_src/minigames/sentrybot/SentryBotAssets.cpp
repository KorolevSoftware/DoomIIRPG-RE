#include "minigames/sentrybot/SentryBotAssets.h"

namespace newcore {

// Legacy load order (src/LoadingManager.cpp:88-92, src/SentryBotGame.cpp:90).
bool SentryBotAssets::load(TextureStore& store, const UiAssets::ResourceReader& read) {
	if (!read) return false;
	bool ok = true;
	ok &= loadUiTexture(store, read, "matrixSkip_BG.bmp", background);
	ok &= loadUiTexture(store, read, "sentryGame.bmp", game);
	ok &= loadUiTexture(store, read, "matrixSkip_grid_pressed.bmp", pressed[0]);
	ok &= loadUiTexture(store, read, "matrixSkip_grid_pressed_2.bmp", pressed[1]);
	ok &= loadUiTexture(store, read, "matrixSkip_grid_pressed_3.bmp", pressed[2]);
	ok &= loadUiTexture(store, read, "matrixSkip_button.bmp", button);
	return ok;
}

} // namespace newcore
