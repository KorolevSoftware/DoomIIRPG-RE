#ifndef NEW_UI_UIASSETS_H
#define NEW_UI_UIASSETS_H

#include <cstdint>
#include <functional>
#include <vector>

#include "render/api/Texture.h"

namespace newcore {

class TextureStore;

// Owner of every UI sheet. Moved out of Hud::startup so ui/ no longer needs
// core/AppContext.h (spec 2026-08-27-ui-layer §0, §2.1): the resource reader
// is injected by the composition root.
class UiAssets {
public:
	// name -> bytes. Mirrors AppContext::readResource's signature.
	using ResourceReader = std::function<bool(const char*, std::vector<uint8_t>&)>;

	UiAssets() = default;

	// Loads all sheets; returns false if any one failed (each failure logs).
	bool load(TextureStore& store, const ResourceReader& read);

	Texture panelTop, panelBottom;
	// HUD bottom-bar switch arrows, 32x32 (docs/original-code/ui.md §1);
	// loaded by GROUP 4, which is the first group that draws them.
	Texture switchLeftNormal, switchLeftActive;
	Texture switchRightNormal, switchRightActive;
	Texture weaponNormal, weaponActive;
	Texture shieldNormal, shieldActive;
	Texture healthNormal, healthActive;
	Texture keyNormal, keyActive;
	Texture playerFaces, playerActive, playerFrameNormal, playerFrameActive;
	Texture numbers;
	Texture cockpitOverlay;
	// Sniper scope mask (src/App.cpp:456) and zoom dial (src/Canvas.cpp:234-235).
	Texture scope, sniperDial, sniperKnob;
	Texture arrowUp, arrowDown, arrowLeft, arrowRight;
	Texture arrowUpPressed, arrowDownPressed, arrowLeftPressed, arrowRightPressed;
	Texture uiImages, portraitsSmall, pageUp, pageDown, pageOk;
	Texture damageVignette, attackArrows, hudTest;
	// In-game menu sheets (spec 2026-08-28-menu §8.5). menuInfo*/menuTornPage
	// are for GROUP 8 and have no caller yet.
	Texture menuHealth, menuShield;
	Texture menuInfoNormal, menuInfoPressed, menuTornPage;
	Texture menuBackground;
	Texture menuScrollBar, menuSliderTop, menuSliderMid, menuSliderBottom;
	Texture menuSoftKey, menuOptionButton;
	// HUD buff box icons, 30x30 cells stacked vertically (src/Canvas.cpp:172).
	Texture iconsBuffs;
	// Vending terminal background and soft keys, also drawn by the vending
	// menus 83-87 (ADR 0033 point 6; src/LoadingManager.cpp:95, src/Hud.cpp:55-56).
	Texture vendingBackground;
	Texture vendingSoftkeyNormal, vendingSoftkeyPressed;
	// Vending menu art (src/MenuSystem.cpp:93, :99-100, :124, :134-137).
	Texture vendingButtonLarge, vendingButtonHelp;
	Texture vendingArrowUpGlow, vendingArrowDownGlow;
	Texture vendingScrollBar, vendingScrollTop, vendingScrollMid, vendingScrollBottom;
	// Loading / saving box (src/Canvas.cpp:168, :174).
	Texture fabricBG;      // FabricBG.bmp 32x32, tiled
	Texture loadingFire;   // loadingFire.bmp 26x9, bar pacifier
	Texture endOfLevelStatsBG;   // endOfLevelStatsBG.bmp 480x320, menu 1 (src/Canvas.cpp:170)
	// Travel map (src/TravelMapManager.cpp:45-78). Resident instead of loaded
	// per visit; the session reads their sizes, the view draws them.
	Texture tmHighlight, tmMagGlass, tmSpaceShip;
	Texture tmLevels1, tmLevels2, tmLevels3, tmLevels4;
	Texture tmToMoon, tmToEarth, tmToHell;
	Texture tmBackground, tmGridHorz, tmGridVert;
	// Automap (src/AutomapController.cpp:71, :322).
	Texture automapBackground;   // gameHelpBG.bmp 480x320 (src/Canvas.cpp:171)
	Texture automapCursor;       // Automap_Cursor.bmp (src/App.cpp:416), 4 px wide frames
};

// The one BMP -> indexed texture loader (ADR 0033 point 6); each failure logs.
// fixIndices, when set, patches the palette indices before the upload (the
// legacy fixImage, src/Utils.cpp:111-172).
// May rewrite the indices and the width (rows stay `height`).
using FixIndicesFn = void (*)(std::vector<uint8_t>& indices, int& width, int height);
bool loadUiTexture(TextureStore& store, const UiAssets::ResourceReader& read,
	const char* name, Texture& out, FixIndicesFn fixIndices = nullptr);

} // namespace newcore

#endif // NEW_UI_UIASSETS_H
