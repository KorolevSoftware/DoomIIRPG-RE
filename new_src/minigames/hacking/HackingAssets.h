#ifndef NEW_MINIGAMES_HACKING_HACKINGASSETS_H
#define NEW_MINIGAMES_HACKING_HACKINGASSETS_H

#include "render/api/Texture.h"
#include "ui/UiAssets.h"

namespace newcore {

class TextureStore;

// Sheets only the hacking minigame draws (src/LoadingManager.cpp:85-87,105,108).
struct HackingAssets {
	Texture background;   // hackerBG.bmp 480x320 (legacy imgEnergyCore)
	Texture colors;       // blockGameColors.bmp, widened to 108 by fixBlockColors

	bool load(TextureStore& store, const UiAssets::ResourceReader& read);
};

} // namespace newcore

#endif // NEW_MINIGAMES_HACKING_HACKINGASSETS_H
