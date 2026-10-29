#include "minigames/common/MiniGameAssets.h"

namespace newcore {

bool MiniGameAssets::load(TextureStore& store, const UiAssets::ResourceReader& read) {
	if (!read) return false;
	return loadUiTexture(store, read, "gameHelpBG.bmp", helpBackground);
}

} // namespace newcore
