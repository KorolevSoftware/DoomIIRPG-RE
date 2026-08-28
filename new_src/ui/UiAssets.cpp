#include "ui/UiAssets.h"

#include <cstdio>

#include "io/BmpImageLoader.h"

namespace newcore {

namespace {

bool loadTexture(const UiAssets::ResourceReader& read, const char* name, Texture& out) {
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
	out.uploadIndexed(img->indices(), img->width(), img->height(), img->palette(), true);
	if (!out.valid()) {
		std::fprintf(stderr, "UiAssets: texture upload failed for %s\n", name);
		return false;
	}
	return true;
}

} // namespace

// Verbatim move of the former Hud::startup (order and file names unchanged).
bool UiAssets::load(const ResourceReader& read) {
	if (!read) return false;
	bool ok = true;
	ok &= loadTexture(read, "HUD_Panel_top.bmp", panelTop);
	ok &= loadTexture(read, "gameMenu_Panel_bottom.bmp", panelBottom);
	ok &= loadTexture(read, "Hud_Weapon_Normal.bmp", weaponNormal);
	ok &= loadTexture(read, "HUD_Weapon_Active.bmp", weaponActive);
	ok &= loadTexture(read, "arrow-up.bmp", arrowUp);
	ok &= loadTexture(read, "arrow-down.bmp", arrowDown);
	ok &= loadTexture(read, "arrow-left.bmp", arrowLeft);
	ok &= loadTexture(read, "arrow-right.bmp", arrowRight);
	ok &= loadTexture(read, "arrow-up_pressed.bmp", arrowUpPressed);
	ok &= loadTexture(read, "arrow-down_pressed.bmp", arrowDownPressed);
	ok &= loadTexture(read, "arrow-left_pressed.bmp", arrowLeftPressed);
	ok &= loadTexture(read, "arrow-right_pressed.bmp", arrowRightPressed);
	ok &= loadTexture(read, "ui_images.bmp", uiImages);
	ok &= loadTexture(read, "Hud_Portrait_Small.bmp", portraitsSmall);
	ok &= loadTexture(read, "pageUP_Icon.bmp", pageUp);
	ok &= loadTexture(read, "pageDOWN_Icon.bmp", pageDown);
	ok &= loadTexture(read, "pageOK_Icon.bmp", pageOk);
	ok &= loadTexture(read, "damage.bmp", damageVignette);
	ok &= loadTexture(read, "Hud_Attack_Arrows.bmp", attackArrows);
	ok &= loadTexture(read, "Hud_Test.bmp", hudTest);
	std::fprintf(stdout, "UiAssets: weaponNormal=%dx%d valid=%d, weaponActive valid=%d\n",
		weaponNormal.width(), weaponNormal.height(), (int)weaponNormal.valid(),
		(int)weaponActive.valid());
	ok &= loadTexture(read, "HUD_Shield_Normal.bmp", shieldNormal);
	ok &= loadTexture(read, "Hud_Shield_Button_Active.bmp", shieldActive);
	ok &= loadTexture(read, "Hud_Key_Normal.bmp", keyNormal);
	ok &= loadTexture(read, "Hud_Key_Active.bmp", keyActive);
	ok &= loadTexture(read, "HUD_Health_Normal.bmp", healthNormal);
	ok &= loadTexture(read, "Hud_Health_Button_Active.bmp", healthActive);
	ok &= loadTexture(read, "Hud_Player.bmp", playerFaces);
	ok &= loadTexture(read, "HUD_Player_Active.bmp", playerActive);
	ok &= loadTexture(read, "HUD_Player_frame_Normal.bmp", playerFrameNormal);
	ok &= loadTexture(read, "HUD_Player_frame_Active.bmp", playerFrameActive);
	ok &= loadTexture(read, "Hud_Numbers.bmp", numbers);
	ok &= loadTexture(read, "cockpit.bmp", cockpitOverlay);
	return ok;
}

} // namespace newcore
