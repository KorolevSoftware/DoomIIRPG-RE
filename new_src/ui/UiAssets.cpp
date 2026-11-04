#include "ui/UiAssets.h"

#include <cstdio>

#include "io/BmpImageLoader.h"

namespace newcore {

bool loadUiTexture(TextureStore& store, const UiAssets::ResourceReader& read,
	const char* name, Texture& out, FixIndicesFn fixIndices) {
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
	if (fixIndices == nullptr) {
		out.uploadIndexed(store, img->indices(), img->width(), img->height(), img->palette(), true);
	} else {
		std::vector<uint8_t> fixed = img->indices();
		int w = img->width();
		fixIndices(fixed, w, img->height());
		out.uploadIndexed(store, fixed, w, img->height(), img->palette(), true);
	}
	if (!out.valid()) {
		std::fprintf(stderr, "UiAssets: texture upload failed for %s\n", name);
		return false;
	}
	return true;
}

// Verbatim move of the former Hud::startup (order and file names unchanged).
bool UiAssets::load(TextureStore& store, const ResourceReader& read) {
	if (!read) return false;
	bool ok = true;
	ok &= loadUiTexture(store, read, "HUD_Panel_top.bmp", panelTop);
	ok &= loadUiTexture(store, read, "gameMenu_Panel_bottom.bmp", panelBottom);
	// Same file names and relative order as the original (src/Hud.cpp:51-54).
	ok &= loadUiTexture(store, read, "Switch_Right_Normal.bmp", switchRightNormal);
	ok &= loadUiTexture(store, read, "Switch_Right_Active.bmp", switchRightActive);
	ok &= loadUiTexture(store, read, "Switch_Left_Normal.bmp", switchLeftNormal);
	ok &= loadUiTexture(store, read, "Switch_Left_Active.bmp", switchLeftActive);
	ok &= loadUiTexture(store, read, "Hud_Weapon_Normal.bmp", weaponNormal);
	ok &= loadUiTexture(store, read, "HUD_Weapon_Active.bmp", weaponActive);
	ok &= loadUiTexture(store, read, "arrow-up.bmp", arrowUp);
	ok &= loadUiTexture(store, read, "arrow-down.bmp", arrowDown);
	ok &= loadUiTexture(store, read, "arrow-left.bmp", arrowLeft);
	ok &= loadUiTexture(store, read, "arrow-right.bmp", arrowRight);
	ok &= loadUiTexture(store, read, "arrow-up_pressed.bmp", arrowUpPressed);
	ok &= loadUiTexture(store, read, "arrow-down_pressed.bmp", arrowDownPressed);
	ok &= loadUiTexture(store, read, "arrow-left_pressed.bmp", arrowLeftPressed);
	ok &= loadUiTexture(store, read, "arrow-right_pressed.bmp", arrowRightPressed);
	ok &= loadUiTexture(store, read, "ui_images.bmp", uiImages);
	ok &= loadUiTexture(store, read, "Hud_Portrait_Small.bmp", portraitsSmall);
	ok &= loadUiTexture(store, read, "pageUP_Icon.bmp", pageUp);
	ok &= loadUiTexture(store, read, "pageDOWN_Icon.bmp", pageDown);
	ok &= loadUiTexture(store, read, "pageOK_Icon.bmp", pageOk);
	ok &= loadUiTexture(store, read, "damage.bmp", damageVignette);
	ok &= loadUiTexture(store, read, "Hud_Attack_Arrows.bmp", attackArrows);
	ok &= loadUiTexture(store, read, "Hud_Test.bmp", hudTest);
	ok &= loadUiTexture(store, read, "HUD_Panel_top_sentrybot.bmp", panelTopSentrybot);
	ok &= loadUiTexture(store, read, "gameMenu_Panel_bottom_sentrybot.bmp", panelBottomSentrybot);
	ok &= loadUiTexture(store, read, "damage_bot.bmp", damageVignetteBot);
	ok &= loadUiTexture(store, read, "Hud_Sentry.bmp", sentryFace);
	ok &= loadUiTexture(store, read, "HUD_sentry_active.bmp", sentryActive);
	std::fprintf(stdout, "UiAssets: weaponNormal=%dx%d valid=%d, weaponActive valid=%d\n",
		weaponNormal.width(), weaponNormal.height(), (int)weaponNormal.valid(),
		(int)weaponActive.valid());
	ok &= loadUiTexture(store, read, "HUD_Shield_Normal.bmp", shieldNormal);
	ok &= loadUiTexture(store, read, "Hud_Shield_Button_Active.bmp", shieldActive);
	ok &= loadUiTexture(store, read, "Hud_Key_Normal.bmp", keyNormal);
	ok &= loadUiTexture(store, read, "Hud_Key_Active.bmp", keyActive);
	ok &= loadUiTexture(store, read, "HUD_Health_Normal.bmp", healthNormal);
	ok &= loadUiTexture(store, read, "Hud_Health_Button_Active.bmp", healthActive);
	ok &= loadUiTexture(store, read, "Hud_Player.bmp", playerFaces);
	ok &= loadUiTexture(store, read, "HUD_Player_Active.bmp", playerActive);
	ok &= loadUiTexture(store, read, "Hud_PlayerDoom.bmp", playerFacesDoom);   // src/App.cpp:444-449
	ok &= loadUiTexture(store, read, "HUD_PlayerDoom_Active.bmp", playerActiveDoom);
	ok &= loadUiTexture(store, read, "Hud_PlayerScientist.bmp", playerFacesScientist);
	ok &= loadUiTexture(store, read, "HUD_PlayerScientist_Active.bmp", playerActiveScientist);
	ok &= loadUiTexture(store, read, "HUD_Player_frame_Normal.bmp", playerFrameNormal);
	ok &= loadUiTexture(store, read, "HUD_Player_frame_Active.bmp", playerFrameActive);
	ok &= loadUiTexture(store, read, "Hud_Numbers.bmp", numbers);
	ok &= loadUiTexture(store, read, "cockpit.bmp", cockpitOverlay);
	ok &= loadUiTexture(store, read, "scope.bmp", scope);
	ok &= loadUiTexture(store, read, "SniperScope_Dial.bmp", sniperDial);
	ok &= loadUiTexture(store, read, "SniperScope_Knob.bmp", sniperKnob);
	// In-game menu sheets, in the original's load order
	// (src/MenuSystem.cpp:120-131 for the gameMenu_* block; the softkey comes
	// from src/Hud.cpp:57). inGame_menu_option_button.bmp is declared but never
	// loaded in the port (src/MenuSystem.h:171 has no assignment), so it goes
	// last; the file ships in the archive.
	ok &= loadUiTexture(store, read, "gameMenu_Health.bmp", menuHealth);
	ok &= loadUiTexture(store, read, "gameMenu_Shield.bmp", menuShield);
	ok &= loadUiTexture(store, read, "gameMenu_infoButton_Pressed.bmp", menuInfoPressed);
	ok &= loadUiTexture(store, read, "gameMenu_infoButton_Normal.bmp", menuInfoNormal);
	ok &= loadUiTexture(store, read, "gameMenu_TornPage.bmp", menuTornPage);
	ok &= loadUiTexture(store, read, "gameMenu_Background.bmp", menuBackground);
	ok &= loadUiTexture(store, read, "gameMenu_ScrollBar.bmp", menuScrollBar);
	ok &= loadUiTexture(store, read, "gameMenu_topSlider.bmp", menuSliderTop);
	ok &= loadUiTexture(store, read, "gameMenu_midSlider.bmp", menuSliderMid);
	ok &= loadUiTexture(store, read, "gameMenu_bottomSlider.bmp", menuSliderBottom);
	ok &= loadUiTexture(store, read, "inGame_menu_softkey.bmp", menuSoftKey);
	ok &= loadUiTexture(store, read, "inGame_menu_option_button.bmp", menuOptionButton);
	ok &= loadUiTexture(store, read, "logo.bmp", mainBackground);
	ok &= loadUiTexture(store, read, "logo2.bmp", mainLogo);
	ok &= loadUiTexture(store, read, "menu_button_background.bmp", mainButton);
	ok &= loadUiTexture(store, read, "menu_button_background_on.bmp", mainButtonOn);
	ok &= loadUiTexture(store, read, "Menu_Dial.bmp", mainDial);
	ok &= loadUiTexture(store, read, "Menu_Dial_Knob.bmp", mainDialKnob);
	ok &= loadUiTexture(store, read, "l2.bmp", logoL2);
	ok &= loadUiTexture(store, read, "prolog.bmp", prolog);
	ok &= loadUiTexture(store, read, "charSelectionBG.bmp", charSelectBG);
	ok &= loadUiTexture(store, read, "Character_select_top_bar.bmp", charSelectTopBar);
	ok &= loadUiTexture(store, read, "Character_select_stat_header.bmp", charSelectStatHeader);
	ok &= loadUiTexture(store, read, "Character_select_stat_bar.bmp", charSelectStatBar);
	ok &= loadUiTexture(store, read, "charSelect.bmp", charSelectAtlas);
	ok &= loadUiTexture(store, read, "Major_legs.bmp", majorLegs);
	ok &= loadUiTexture(store, read, "Major_torso.bmp", majorTorso);
	ok &= loadUiTexture(store, read, "Riley_legs.bmp", rileyLegs);
	ok &= loadUiTexture(store, read, "Riley_torso.bmp", rileyTorso);
	ok &= loadUiTexture(store, read, "Sarge_legs.bmp", sargeLegs);
	ok &= loadUiTexture(store, read, "Sarge_torso.bmp", sargeTorso);
	ok &= loadUiTexture(store, read, "Icons_Buffs.bmp", iconsBuffs);   // src/Canvas.cpp:172
	ok &= loadUiTexture(store, read, "vending_softkey_pressed.bmp", vendingSoftkeyPressed);
	ok &= loadUiTexture(store, read, "vending_softkey_normal.bmp", vendingSoftkeyNormal);
	ok &= loadUiTexture(store, read, "vendingBG.bmp", vendingBackground);
	ok &= loadUiTexture(store, read, "vending_button_large.bmp", vendingButtonLarge);
	ok &= loadUiTexture(store, read, "vending_arrow_up_glow.bmp", vendingArrowUpGlow);
	ok &= loadUiTexture(store, read, "vending_arrow_down_glow.bmp", vendingArrowDownGlow);
	ok &= loadUiTexture(store, read, "vending_button_help.bmp", vendingButtonHelp);
	ok &= loadUiTexture(store, read, "vending_scroll_bar.bmp", vendingScrollBar);
	ok &= loadUiTexture(store, read, "vending_scroll_button_top.bmp", vendingScrollTop);
	ok &= loadUiTexture(store, read, "vending_scroll_button_middle.bmp", vendingScrollMid);
	ok &= loadUiTexture(store, read, "vending_scroll_button_bottom.bmp", vendingScrollBottom);
	ok &= loadUiTexture(store, read, "FabricBG.bmp", fabricBG);
	ok &= loadUiTexture(store, read, "loadingFire.bmp", loadingFire);
	ok &= loadUiTexture(store, read, "endOfLevelStatsBG.bmp", endOfLevelStatsBG);
	ok &= loadUiTexture(store, read, "highlight.bmp", tmHighlight);
	ok &= loadUiTexture(store, read, "magnifyingGlass.bmp", tmMagGlass);
	ok &= loadUiTexture(store, read, "spaceShip.bmp", tmSpaceShip);
	ok &= loadUiTexture(store, read, "TM_Levels1.bmp", tmLevels1);
	ok &= loadUiTexture(store, read, "TM_Levels2.bmp", tmLevels2);
	ok &= loadUiTexture(store, read, "TM_Levels3.bmp", tmLevels3);
	ok &= loadUiTexture(store, read, "TM_Levels4.bmp", tmLevels4);
	ok &= loadUiTexture(store, read, "toMoon.bmp", tmToMoon);
	ok &= loadUiTexture(store, read, "toEarth.bmp", tmToEarth);
	ok &= loadUiTexture(store, read, "toHell.bmp", tmToHell);
	ok &= loadUiTexture(store, read, "TravelMap.bmp", tmBackground);
	ok &= loadUiTexture(store, read, "travelMapHorzGrid.bmp", tmGridHorz);
	ok &= loadUiTexture(store, read, "travelMapVertGrid.bmp", tmGridVert);
	ok &= loadUiTexture(store, read, "gameHelpBG.bmp", automapBackground);
	ok &= loadUiTexture(store, read, "Automap_Cursor.bmp", automapCursor);
	// Frame layout of the cursor sheet is unverified (AM open question).
	std::fprintf(stderr, "[automap] cursor %dx%d\n", automapCursor.width(), automapCursor.height());
	return ok;
}

} // namespace newcore
