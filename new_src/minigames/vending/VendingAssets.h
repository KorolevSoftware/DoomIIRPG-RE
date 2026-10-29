#ifndef NEW_MINIGAMES_VENDING_VENDINGASSETS_H
#define NEW_MINIGAMES_VENDING_VENDINGASSETS_H

#include "render/api/Texture.h"
#include "ui/UiAssets.h"

namespace newcore {

class TextureStore;

// Sheets only the vending minigame draws (src/LoadingManager.cpp:93-102).
// The terminal background and soft keys are in UiAssets (ADR 0033 point 6).
struct VendingAssets {
	Texture vendingGame;      // vendingGame.bmp atlas (top-bar box, chevrons)
	Texture gameBackground;   // vending_BG.bmp
	Texture submit, submitPressed;
	Texture arrowUp, arrowUpPressed;
	Texture arrowDown, arrowDownPressed;
	Texture buttonSmall;      // vending_button_small.bmp

	bool load(TextureStore& store, const UiAssets::ResourceReader& read);
};

} // namespace newcore

#endif // NEW_MINIGAMES_VENDING_VENDINGASSETS_H
