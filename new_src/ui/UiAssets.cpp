#include "ui/UiAssets.h"

#include <cstdio>

#include "io/BmpImageLoader.h"

namespace newcore {

namespace {

bool loadTexture(TextureStore& store, const UiAssets::ResourceReader& read,
	const char* name, Texture& out) {
	std::vector<uint8_t> bmp;
	if (!read(name, bmp)) {
		std::fprintf(stderr, "UiAssets: missing resource %s\n", name);
		return false;
	}
	BmpImageLoader loader;
	ImagePtr img = loader.load(bmp, true);
	if (!img) {
		std::fprintf(stderr, "UiAssets: failed to decode %s\n", name);
		return false;
	}
	out.uploadIndexed(store, img->indices(), img->width(), img->height(), img->palette(), true);
	if (!out.valid()) {
		std::fprintf(stderr, "UiAssets: texture upload failed for %s\n", name);
		return false;
	}
	return true;
}

} // namespace

// Verbatim move of the former Hud::startup (order and file names unchanged).
bool UiAssets::load(TextureStore& store, const ResourceReader& read) {
	if (!read) return false;
	bool ok = true;
	ok &= loadTexture(store, read, "HUD_Panel_top.bmp", panelTop);
	ok &= loadTexture(store, read, "gameMenu_Panel_bottom.bmp", panelBottom);
	// Same file names and relative order as the original (src/Hud.cpp:51-54).
	ok &= loadTexture(store, read, "Switch_Right_Normal.bmp", switchRightNormal);
	ok &= loadTexture(store, read, "Switch_Right_Active.bmp", switchRightActive);
	ok &= loadTexture(store, read, "Switch_Left_Normal.bmp", switchLeftNormal);
	ok &= loadTexture(store, read, "Switch_Left_Active.bmp", switchLeftActive);
	ok &= loadTexture(store, read, "Hud_Weapon_Normal.bmp", weaponNormal);
	ok &= loadTexture(store, read, "HUD_Weapon_Active.bmp", weaponActive);
	ok &= loadTexture(store, read, "arrow-up.bmp", arrowUp);
	ok &= loadTexture(store, read, "arrow-down.bmp", arrowDown);
	ok &= loadTexture(store, read, "arrow-left.bmp", arrowLeft);
	ok &= loadTexture(store, read, "arrow-right.bmp", arrowRight);
	ok &= loadTexture(store, read, "arrow-up_pressed.bmp", arrowUpPressed);
	ok &= loadTexture(store, read, "arrow-down_pressed.bmp", arrowDownPressed);
	ok &= loadTexture(store, read, "arrow-left_pressed.bmp", arrowLeftPressed);
	ok &= loadTexture(store, read, "arrow-right_pressed.bmp", arrowRightPressed);
	ok &= loadTexture(store, read, "ui_images.bmp", uiImages);
	ok &= loadTexture(store, read, "Hud_Portrait_Small.bmp", portraitsSmall);
	ok &= loadTexture(store, read, "pageUP_Icon.bmp", pageUp);
	ok &= loadTexture(store, read, "pageDOWN_Icon.bmp", pageDown);
	ok &= loadTexture(store, read, "pageOK_Icon.bmp", pageOk);
	ok &= loadTexture(store, read, "damage.bmp", damageVignette);
	ok &= loadTexture(store, read, "Hud_Attack_Arrows.bmp", attackArrows);
	ok &= loadTexture(store, read, "Hud_Test.bmp", hudTest);
	std::fprintf(stdout, "UiAssets: weaponNormal=%dx%d valid=%d, weaponActive valid=%d\n",
		weaponNormal.width(), weaponNormal.height(), (int)weaponNormal.valid(),
		(int)weaponActive.valid());
	ok &= loadTexture(store, read, "HUD_Shield_Normal.bmp", shieldNormal);
	ok &= loadTexture(store, read, "Hud_Shield_Button_Active.bmp", shieldActive);
	ok &= loadTexture(store, read, "Hud_Key_Normal.bmp", keyNormal);
	ok &= loadTexture(store, read, "Hud_Key_Active.bmp", keyActive);
	ok &= loadTexture(store, read, "HUD_Health_Normal.bmp", healthNormal);
	ok &= loadTexture(store, read, "Hud_Health_Button_Active.bmp", healthActive);
	ok &= loadTexture(store, read, "Hud_Player.bmp", playerFaces);
	ok &= loadTexture(store, read, "HUD_Player_Active.bmp", playerActive);
	ok &= loadTexture(store, read, "HUD_Player_frame_Normal.bmp", playerFrameNormal);
	ok &= loadTexture(store, read, "HUD_Player_frame_Active.bmp", playerFrameActive);
	ok &= loadTexture(store, read, "Hud_Numbers.bmp", numbers);
	ok &= loadTexture(store, read, "cockpit.bmp", cockpitOverlay);
	// In-game menu sheets, in the original's load order
	// (src/MenuSystem.cpp:120-131 for the gameMenu_* block; the softkey comes
	// from src/Hud.cpp:57). inGame_menu_option_button.bmp is declared but never
	// loaded in the port (src/MenuSystem.h:171 has no assignment), so it goes
	// last; the file ships in the archive.
	ok &= loadTexture(store, read, "gameMenu_Health.bmp", menuHealth);
	ok &= loadTexture(store, read, "gameMenu_Shield.bmp", menuShield);
	ok &= loadTexture(store, read, "gameMenu_infoButton_Pressed.bmp", menuInfoPressed);
	ok &= loadTexture(store, read, "gameMenu_infoButton_Normal.bmp", menuInfoNormal);
	ok &= loadTexture(store, read, "gameMenu_TornPage.bmp", menuTornPage);
	ok &= loadTexture(store, read, "gameMenu_Background.bmp", menuBackground);
	ok &= loadTexture(store, read, "gameMenu_ScrollBar.bmp", menuScrollBar);
	ok &= loadTexture(store, read, "gameMenu_topSlider.bmp", menuSliderTop);
	ok &= loadTexture(store, read, "gameMenu_midSlider.bmp", menuSliderMid);
	ok &= loadTexture(store, read, "gameMenu_bottomSlider.bmp", menuSliderBottom);
	ok &= loadTexture(store, read, "inGame_menu_softkey.bmp", menuSoftKey);
	ok &= loadTexture(store, read, "inGame_menu_option_button.bmp", menuOptionButton);
	return ok;
}

} // namespace newcore
