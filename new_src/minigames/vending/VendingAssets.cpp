#include "minigames/vending/VendingAssets.h"

#include <cstdint>
#include <vector>

namespace newcore {

namespace {

// fixImage on vending_arrow_down.bmp (src/LoadingManager.cpp:109,
// src/Utils.cpp:164-172): indices 1 and 2 become 0. The legacy gates it on
// the MD5 of the shipped file, the only variant there is, so it is applied
// unconditionally.
void fixArrowDown(std::vector<uint8_t>& indices, int&, int) {
	for (uint8_t& i : indices) {
		if (i == 1 || i == 2) i = 0;
	}
}

} // namespace

// Legacy load order (src/LoadingManager.cpp:93-102); vendingBG.bmp is UiAssets'.
bool VendingAssets::load(TextureStore& store, const UiAssets::ResourceReader& read) {
	if (!read) return false;
	bool ok = true;
	ok &= loadUiTexture(store, read, "vendingGame.bmp", vendingGame);
	ok &= loadUiTexture(store, read, "vending_BG.bmp", gameBackground);
	ok &= loadUiTexture(store, read, "vending_submit.bmp", submit);
	ok &= loadUiTexture(store, read, "vending_submit_pressed.bmp", submitPressed);
	ok &= loadUiTexture(store, read, "vending_arrow_up.bmp", arrowUp);
	ok &= loadUiTexture(store, read, "vending_arrow_up_pressed.bmp", arrowUpPressed);
	ok &= loadUiTexture(store, read, "vending_arrow_down.bmp", arrowDown, fixArrowDown);
	ok &= loadUiTexture(store, read, "vending_arrow_down_pressed.bmp", arrowDownPressed);
	ok &= loadUiTexture(store, read, "vending_button_small.bmp", buttonSmall);
	return ok;
}

} // namespace newcore
