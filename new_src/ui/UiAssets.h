#ifndef NEW_UI_UIASSETS_H
#define NEW_UI_UIASSETS_H

#include <cstdint>
#include <functional>
#include <vector>

#include "render/gl/Texture.h"

namespace newcore {

// Owner of every UI sheet. Moved out of Hud::startup so ui/ no longer needs
// core/AppContext.h (spec 2026-08-27-ui-layer §0, §2.1): the resource reader
// is injected by the composition root.
class UiAssets {
public:
	// name -> bytes. Mirrors AppContext::readResource's signature.
	using ResourceReader = std::function<bool(const char*, std::vector<uint8_t>&)>;

	UiAssets() = default;

	// Loads all sheets; returns false if any one failed (each failure logs).
	bool load(const ResourceReader& read);

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
	Texture arrowUp, arrowDown, arrowLeft, arrowRight;
	Texture arrowUpPressed, arrowDownPressed, arrowLeftPressed, arrowRightPressed;
	Texture uiImages, portraitsSmall, pageUp, pageDown, pageOk;
	Texture damageVignette, attackArrows, hudTest;
};

} // namespace newcore

#endif // NEW_UI_UIASSETS_H
