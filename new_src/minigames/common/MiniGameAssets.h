#ifndef NEW_MINIGAMES_COMMON_MINIGAMEASSETS_H
#define NEW_MINIGAMES_COMMON_MINIGAMEASSETS_H

#include "render/api/Texture.h"
#include "ui/UiAssets.h"

namespace newcore {

class TextureStore;

// The sheet all three minigames share (ADR 0033 point 6).
struct MiniGameAssets {
	Texture helpBackground;   // gameHelpBG.bmp (src/LoadingManager.cpp:87,105)

	bool load(TextureStore& store, const UiAssets::ResourceReader& read);
};

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAMEASSETS_H
