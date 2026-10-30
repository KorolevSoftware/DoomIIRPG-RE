#ifndef NEW_UI_TRAVELMAPMODEL_H
#define NEW_UI_TRAVELMAPMODEL_H

namespace newcore {

class Text;

// Travel-map images the model can name (src/TravelMapManager.cpp:50-72).
enum class TmImage { None, Moon1, Earth2, Earth4, Hell3, ToMoon, ToEarth, ToHell };

// Per-frame view model of ST_TRAVELMAP (docs/original-code/travel-map.md §3-§4),
// built by TravelMapSession::buildModel. Plain data: every coordinate is final
// canvas space, every string is an index into strings file 3 (kTextIngame2).
struct TravelMapModel {
	int gridT = 0;                        // ms since init (grid scroll, :168)
	TmImage closeup = TmImage::None; int closeupX = 0, closeupY = 0;   // anchor top-left
	TmImage path = TmImage::None;         // revealed part of the path image
	int pathSrcX = 0, pathSrcY = 0, pathSrcW = 0, pathSrcH = 0, pathX = 0, pathY = 0;
	int pathLabelId = -1; int pathLabelX = 0, pathLabelY = 0, pathLabelAnchor = 0;
	bool ship = false; int shipX = 0, shipY = 0;   // spaceShip.bmp, anchor top-left
	bool crosshair = false; int crossX = 0, crossY = 0;   // two full-screen lines + shadow
	bool box = false;                     // locator box + name (§4.3)
	bool magnifier = false; int magX = 0, magY = 0;       // anchor 3 (centred)
	int highlightX = 0, highlightY = 0;   // highlight.bmp, anchor 6 (VCENTER|LEFT)
	int nameId = -1;                      // level name
	int nameX = 0, nameY = 0;             // anchor 3, char colour 3
	bool prompt = false;                  // (0,96) "Tap Screen to Continue" at (240, 299) anchor 17
	// The three strings, resolved and dehyphenated by the producer (ui/ has no
	// Localization); borrowed for this frame.
	const Text* pathLabelText = nullptr;
	const Text* nameText = nullptr;       // already prefixed with the '^3' colour code
	const Text* promptText = nullptr;     // already wrapped at 24
};

} // namespace newcore

#endif // NEW_UI_TRAVELMAPMODEL_H
