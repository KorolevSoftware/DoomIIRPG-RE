#include "core/MenuSession.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "minigames/vending/VendingMachine.h"

namespace newcore {

namespace {

// Geometry constants of the in-game tree, all from setMenuSettings
// (src/MenuSystem.cpp:4173-4194): menuItem_height 46, menuItem_width =
// inGame_menu_option_button->width = 296, menuItem_paddingBottom 10,
// menuItem_fontPaddingBottom 0. FONT_HEIGHT[0] = 16 (src/App.h:39).
constexpr int kItemHeightPx = 46;
constexpr int kItemWidthPx = 296;
constexpr int kItemPaddingBottom = 10;
constexpr int kItemFontPaddingBottom = 0;
constexpr int kFontHeight = 16;

// setLength((menuItem_width - 10) / FONT_WIDTH[0]) with FONT_WIDTH[0] = 12
// (src/MenuSystem.cpp:948, src/App.h:40) — a literal CHARACTER count, never
// derived from the rewrite's 9 px advance (spec §14 NEW FACT 9).
constexpr int kTruncateChars = (kItemWidthPx - 10) / 12;   // 23

// The vending menus (menu > MENU_ITEMS_HOLY_WATER_MAX, :4208-4211):
// menuItem_width = vending_button_large.bmp width = 231 (shipped file),
// menuItem_paddingBottom = 3; the scrollbar moves to x = 350 (:2789-2790).
constexpr int kVendingItemWidthPx = 231;
constexpr int kVendingPaddingBottom = 3;
constexpr int kVendingTruncateChars = (kVendingItemWidthPx - 10) / 12;   // 18
constexpr int kVendingBarX = 350;

// Main-menu geometry (ids < MENU_INGAME), all src/MenuSystem.cpp.
constexpr int kMainItemWidth = 162;                    // setMenuSettings default (:4173-4178)
constexpr int kMainItemPadding = 0;                    // (:4178)
constexpr int kMainTruncateChars = (kMainItemWidth - 10) / 12;   // (:945-951)
constexpr UiRect kMainRect{ 138, 126, 204, 176 };      // x = (480-204)/2 (:4241-4247); paint forces y/h (:803-806)
constexpr UiRect kMainClip{ 0, 111, 480, 206 };        // setClipRect(0, 126-15, 480, 176+30) (:804)
constexpr int kMainInitRectH = 152;                    // 38 << 2 (:4245, :2753)
constexpr UiRect kDialRect{ 353, 122, 45, 183 };       // x = 138 + 0xd7, y = 137 - 0xf (:2771-2778)
constexpr int kDialThumbLen = 11;                      // 183 >> 4 (:2780-2783, :2800-2802)
constexpr int kDialKnobH = 24;                         // Menu_Dial_Knob.bmp
// The eleven main-menu help leaves (Game Help children + About).
constexpr UiRect kMainHelpRect{ 85, 0, 300, 320 };     // paint: setMenuDimentions(85, 0, 300, 320) (:807-845)
constexpr UiRect kMainHelpClip{ 0, 0, 480, 320 };      // setClipRect(screenRect[0], menuRect[1], screenRect[2], menuRect[3]) (:852)
constexpr int kMainHelpInitRectH = 281;                // setMenuSettings(77, 10, 323, 281), the height initMenu tests (:4248-4264, :2753)
constexpr UiRect kMainHelpDialRect{ 408, 81, 45, 183 };   // 0x198, 0x51 + Menu_Dial size (:2773-2776)
constexpr int kMainHelpWrap = 35;                      // wrapText(menu >= MENU_INGAME ? ... : 35) (:3674), a CHARACTER count

constexpr char kCheckGlyph = '\x87';       // (:934-938)
constexpr char kEllipsisGlyph = '\x85';    // (:948-951)
// The same '\x80' is what a weapon with AMMO_NONE prints in its value column
// (:2015-2016) — one glyph, not a divider run.
constexpr char kNoAmmoGlyph = '\x80';

// menus.bin string ids (type 3) the code-built item rows use verbatim:
// ITEMS_HEALTH_TITLE / ITEMS_ARMOR_TITLE (src/MenuStrings.h:161-162, used at
// src/MenuSystem.cpp:1937,:1959) and the NANO DRINKS row + its help
// (src/MenuStrings.h:227-228, used at :1972).
constexpr int kStrItemsHealthTitle = 112;
constexpr int kStrItemsArmorTitle = 113;
constexpr int kStrNanoDrinksItem = 178;
constexpr int kStrNanoDrinksItemHelp = 179;

// The three confirm-screen questions, exactly the ids the initMenu cases pass to
// SetYESNO: MENU_INGAME_RESTARTLVL (:1731) "Re-start Lev-el?",
// MENU_INGAME_SAVEQUIT (:1738) "Save & Quit?", MENU_INGAME_LOAD (:1717)
// (the strings themselves are verified against the shipped strings01/02 chunk)
// "Load Game?". None of the three carries a %NN argument, so the rewrite can use
// the string as it ships instead of Localization::composeText.
constexpr int kStrRestartLevelQuestion = 134;
constexpr int kStrSaveQuitQuestion = 135;
constexpr int kStrLoadGameQuestion = 136;
// MENU_ITEMS_CONFIRM's question "Use %01?" (:2054-2058), composed with the name.
constexpr int kStrUseItemQuestion = 184;
// MenuStrings::YES_LABEL / NO_LABEL (src/MenuStrings.h:246-247), appended by
// SetYESNO (:3651-3652).
constexpr int kStrYesLabel = 197;
constexpr int kStrNoLabel = 198;

// SetYESNO's literal flag values (:3640-3652): 9 for the message and blank rows
// (they are label rows, 16 px, centred and unselectable), 8 for Yes/No (action
// rows, so 296x32 plates with a centred label).
constexpr int kYesNoMessageFlags = kItemNoSelect | kItemAlignCenter;
constexpr int kYesNoButtonFlags = kItemAlignCenter;

// Canvas::ingameScrollWithBarMaxChars = (displayRect[2] - 34) / 9 = (480-34)/9
// (src/Canvas.cpp:94), the wrap budget LoadHelpResource passes for every
// menu >= MENU_INGAME (:3674). A literal CHARACTER count: the original measured
// it against FONT_WIDTH 12 / CHAR_SPACING 11 while the rewrite's font advances
// 9, so deriving it from a pixel width here would be wrong (NEW FACT 9).
constexpr int kHelpMaxChars = (480 - 34) / 9;   // 49

// The re-wrap budget: menuRect[2] is first reduced by 27 and then divided by
// CHAR_SPACING[0] (:3697-3698) -> 407 / 11. Again a literal, for the same
// reason: 37, not 434/9 and not 407/9.
constexpr int kHelpRectNarrowPx = 27;

// menuRect[3] as setMenuSettings leaves it for a help leaf — 320 minus the
// bottom panel (:4362) — which is the height LoadHelpResource compares the
// content against, BEFORE paint narrows the region to 241 (:848). 256/16 = 16
// lines fit here; 241/16 = 15 are actually drawn.
constexpr int kHelpInitRectH = 320 - 64;

// Canvas::displayRect[3] = backBuffer->height (src/Canvas.cpp:52), i.e. the
// 320 px canvas. LoadNotebook opens the page scrolled to the bottom by
// (displayRect[3] - 26) / 16 lines (:3805).
constexpr int kDisplayH = 320;

// render->mapNameID for the rewrite. LoadNotebook titles the page with
// render->mapNameField = 0xC00 | game->levelNames[mapNameID - 1]
// (src/LoadingManager.cpp:331), i.e. STRINGID(FILE_MENUSTRINGS, levelName).
// The rewrite loads map00 and nothing else (new_src/core/Main.cpp:108), so the
// id is fixed at 1 here rather than invented as a new Env field.
constexpr int kMapNameId = 1;

// CHAR_SPACING[0] = 11 (src/App.h:41). The divider cell count is
// menuRect[2] / CHAR_SPACING[0] (:909), i.e. a CHARACTER budget computed with
// the original's 11 px advance — never with the rewrite's 9 px one (NEW FACT 9).
constexpr int kCharSpacing = 11;

// WrapHelpText (src/MenuItem.cpp:29-34): composeTextField(helpField) then
// wrapText(menuHelpMaxChars, 12, '\n'), with menuHelpMaxChars set for this one
// call to imgGameMenuTornPage->width / FONT_WIDTH[0] = 408 / 12 (:1162). A
// literal CHARACTER budget again: the rewrite's font advances 9, so deriving it
// from the page width here would give 45 (NEW FACT 9).
constexpr int kHelpPopupChars = 408 / 12;   // 34
constexpr int kHelpPopupLines = 12;         // the 0xc argument of WrapHelpText

// items[i].flags & 0x8001 == ITEM_NOSELECT | ITEM_HIDDEN (:432, :446).
constexpr int kUnselectableMask = kItemNoSelect | kItemHidden;

// The default list region as paint() narrows it (src/MenuSystem.cpp:848); it is
// also the scroll widget's boxRect, i.e. the area a touch drag scrolls
// (SetScrollBox from initMenu, :2793-2796). Owned here and written into the
// model; geometryFor() below gives the screens that move it (MENU_INGAME_OPTIONS
// and the confirm screens) their own x/w.
constexpr UiRect kListRect{ 70, 10, 340, 241 };

// The scroll widget's view height. Two values are defensible: 256, because
// SetScrollBox runs from initMenu while menuRect is still (70, 0, 340, 320-64)
// (src/MenuSystem.cpp:4192, :2793-2796), and 241, the height paint() narrows
// the region to right afterwards (:848).
//
// A3 (spec §14) is RESOLVED, and by the user's screen rather than by reasoning.
// With the reference-derived 256 the clamp stops at contentPx - 256 = 350 and
// the last plate lands at 210..256 of a 241 px region — its bottom 15 px cut
// off by the clip, which is exactly the "last row against the bottom border"
// the user reported. With 241 the clamp stops at 365 and the last plate lands
// at 195..241, whole and ending on the region's edge. We use 241.
//
// This changes the SCROLL view height only: barRect() is a separate use of
// menuRect[3] (:2757-2762) and keeps its initMenu-time derivation.
constexpr int kViewPx = 241;
static_assert(kViewPx == kListRect.h, "A3: the scroll view IS the drawn region");

// Drag dead box: the port ignores pointer moves that stay inside a 6x6 box
// around the press point ([GEC], src/MenuSystem.cpp:4842-4847 and
// src/TouchController.cpp:127-131). Applied symmetrically here — a move of more
// than 3 px on either axis turns the press into a drag; anything smaller is
// still a click and still activates the row.
constexpr int kDragDeadBoxPx = 3;

// barRect for the in-game tree (:2757-2762, :2787): x 430, w 50, h =
// imgGameMenuScrollBar->height = 220, y = menuRect[1] + ((menuRect[3]-220)>>1)
// evaluated with the initMenu-time rect, whose y is always 0 here.
constexpr int kBarX = 430;
constexpr int kBarW = 50;
constexpr int kBarH = 220;

// setMenuSettings' per-screen rect, narrowed by paint() to y 10 / h 241 for
// every id of the in-game tree (:847-849). `initRectH` is the height
// setMenuSettings left behind — the one SetScrollBox saw when it placed the bar
// (:2757-2762) — which is NOT the drawn height (A3, see kViewPx).
struct ScreenGeom {
	UiRect rect;
	int initRectH;
};

// buildFraction (:3840-3851).
void buildFraction(Text& t, int i, int i2) {
	t.setLength(0);
	if (i < 0) t.append('-');
	t.append(i);
	t.append("/");
	if (i2 < 0) t.append('-');
	t.append(i2);
}

// Player::formatTime (src/Player.cpp:1127-1146): H:MM:SS, hours unpadded.
void formatTime(Text& t, int ms) {
	const int s = ms / 1000;
	const int m = s / 60;
	t.setLength(0);
	t.append(m / 60);
	t.append(":");
	if (m % 60 < 10) t.append("0");
	t.append(m % 60);
	t.append(":");
	if (s - m * 60 < 10) t.append("0");
	t.append(s - m * 60);
}

// buildModStat (:3853-3862). Verbatim, including the sign quirk: a negative
// modifier prints its own minus after the literal "(-", i.e. "12(--3)".
void buildModStat(Text& t, int i, int i2) {
	t.setLength(0);
	t.append(i);
	if (i2 == 0) return;
	t.append((i2 > 0) ? "(+" : "(-");
	t.append(i2);
	t.append(')');
}

// The menu id -> help resource map of the ten type-5 leaves, read straight off
// their initMenu cases (src/MenuSystem.cpp:1663-1764): each one sets
// `this->type = 5` and calls LoadHelpResource with the number below. -1 = the id
// is not a help leaf.
int helpResourceFor(int menuId) {
	switch (menuId) {
	case kMenuHelpGeneral:    return 1;    // (:1663-1667)
	case kMenuHelpMove:       return 2;    // (:1669-1673)
	case kMenuHelpAttack:     return 3;    // (:1675-1679)
	case kMenuHelpSniper:     return 7;    // (:1681-1685)
	case kMenuHelpArmor:      return 4;    // (:1687-1691)
	case kMenuHelpEffects:    return 5;    // (:1693-1697)
	case kMenuHelpItems:      return 6;    // (:1699-1703)
	case kMenuHelpHacker:     return 8;    // (:1749-1753)
	case kMenuHelpMatrixSkip: return 9;    // (:1755-1759)
	case kMenuHelpPowerUp:    return 10;   // (:1761-1765)
	// The main-menu leaves (:1288-1344, :1466-1486).
	case kMenuMainGeneral:        return 1;
	case kMenuMainMove:           return 2;
	case kMenuMainAttack:         return 3;
	case kMenuMainSniper:         return 7;
	case kMenuMainArmorHelp:      return 4;
	case kMenuMainItemHelp:       return 6;
	case kMenuMainEffectHelp:     return 5;
	case kMenuMainHackerHelp:     return 8;
	case kMenuMainMatrixSkipHelp: return 9;
	case kMenuMainPowerUpHelp:    return 10;
	case kMenuMainAbout:          return 11;
	default:                  return -1;
	}
}

// Screens with no menus.bin row that the rewrite builds in code, mirroring their
// initMenu cases (ADR 0013). G7 added the three confirm screens; G6 adds
// QUESTLOG 46 (LoadNotebook, :1707-1710) and the ten type-5 HELP leaves; the
// main-menu confirms EXIT 13 / CONFIRMNEW 14 (:1345-1357) are SetYESNO-built. Every
// remaining absent id stays refused by the GOTO gate in initMenu.
bool isCodeBuiltScreen(int menuId) {
	return menuId == kMenuLoad || menuId == kMenuRestartLvl || menuId == kMenuSaveQuit ||
		menuId == kMenuQuestLog || menuId == kMenuItemsConfirm || helpResourceFor(menuId) > 0 ||
		menuId == kMenuComicBook || menuId == kMenuMainExit || menuId == kMenuMainConfirmNew;
}

// The screens whose action rows carry an "i" button, verbatim from the paint
// whitelist (:5132-5140): menu <= MENU_INGAME_STATUS, MENU_INGAME_HELP,
// MENU_ITEMS, MENU_ITEMS_WEAPONS, MENU_ITEMS_DRINKS and the three vending ids.
// `menu <= 30` reaches every main-menu id too, but ST_MENU only ever runs the
// in-game tree here, where it means exactly MENU_INGAME (29) and
// MENU_INGAME_STATUS (30).
// updateTouchButtonState' own drawButton whitelist (:4479-4489) resolves to the
// same set of ids for this tree, so the drawn button and the clickable one
// always agree.
bool screenHasInfoButtons(int menuId) {
	return menuId <= kMenuStatus || menuId == kMenuHelp || menuId == kMenuItems ||
		menuId == kMenuItemsWeapons || menuId == kMenuItemsDrinks ||
		menuId == kMenuVendingDrinks || menuId == kMenuVendingSnacks ||
		menuId == kMenuVendingConfirm;
}

bool isVendingMenu(int menuId) {
	return menuId >= kMenuVending && menuId <= kMenuVendingCantBuy;
}

// The `menu < MENU_INGAME` tests of paint / setMenuSettings (src/MenuSystem.cpp:762-771, :803).
bool isMainMenu(int menuId) { return menuId < kMenuInGame && menuId != kMenuLevelStats; }

bool isMainHelpLeaf(int menuId) { return isMainMenu(menuId) && helpResourceFor(menuId) > 0; }

// Main-menu GOTO targets the rewrite does not open (deviation FE-D2). nullptr = ported.
const char* unportedMainScreenReason(int menuId) {
	if (menuId == kMenuMainOptions)
		return "MENU_MAIN_OPTIONS ships 0 rows; src/ fills it with [GEC] menus (front-end.md FE-D2)";
	if (menuId == kMenuMainMoreGames) return "Other Games is removed from MENU_MAIN (front-end.md FE-D2)";
	return nullptr;
}

// fillVendingMachineSnacks' three name sets, menu strings (file 3), by map:
// 1-3, other, 7-9 (src/MenuSystem.cpp:4100-4164).
constexpr int kSnackCount = 12;
constexpr int kSnackSets[3][kSnackCount] = {
	{ 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369 },
	{ 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 368, 369 },
	{ 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 369 },
};
constexpr int kStrCredits = 197;   // Code "Cre-dits: %01" (file 0)
constexpr int kStrCostItem = 350;        // MenuStrings::COST_ITEM "%01 x %02"
constexpr int kStrCostItemEmpty = 351;   // MenuStrings::COST_ITEM_EMPTY "To-tal Cost: %01"

ScreenGeom geometryFor(int menuId) {
	switch (menuId) {
	// One case group in the original (:4330-4349): w = the 296 px option
	// button, x = (480 - w) >> 1 = 92, y = 0, h = 320. Its remaining members
	// (INGAME_CONTROLLER / OPTIONS_SOUND / OPTIONS_INPUT) have no MenuId in the
	// rewrite. LOADNOSAVE 50 and SPECIAL_EXIT 58 are deliberately absent: they
	// are NOT in that group and keep the default rect.
	case kMenuOptions:
	case kMenuExit:
	case kMenuLoad:
	case kMenuInGameDead:
	case kMenuRestartLvl:
	case kMenuSaveQuit:
	case kMenuControls:
	case kMenuItemsConfirm:
	case kMenuItemsHealthMsg:
	case kMenuItemsArmorMsg:
	case kMenuItemsSyringeMsg:
	case kMenuItemsHolyWaterMax:
		return { UiRect{ (480 - kItemWidthPx) / 2, kListRect.y, kItemWidthPx, kListRect.h }, 320 };
	// The text-section group (:4351-4367): x = 23, y = 0, w = 434,
	// h = 320 - panelBottom->height. MENU_INGAME_SNIPER (41) is a member here
	// too — in this build it is the sniper-rifle HELP leaf, not a list.
	case kMenuHelpGeneral:
	case kMenuHelpMove:
	case kMenuHelpAttack:
	case kMenuHelpSniper:
	case kMenuHelpArmor:
	case kMenuHelpEffects:
	case kMenuHelpItems:
	case kMenuQuestLog:
	case kMenuHelpHacker:
	case kMenuHelpMatrixSkip:
	case kMenuHelpPowerUp:
		return { UiRect{ 23, kListRect.y, 434, kListRect.h }, kHelpInitRectH };
	// paint() narrows every menu past MENU_ITEMS_HOLY_WATER_MAX to
	// (70, 11, 203, 250) (:847-851); setMenuSettings left the default
	// in-game rect, so barRect() keeps the 320 - 64 derivation.
	case kMenuVending:
	case kMenuVendingDrinks:
	case kMenuVendingSnacks:
	case kMenuVendingConfirm:
	case kMenuVendingCantBuy:
		return { UiRect{ 70, 11, 203, 250 }, 320 - 64 };
	// MENU_LEVEL_STATS (:4233-4239); paint re-applies y 0 / h 320 (:803-806).
	case kMenuLevelStats:
		return { UiRect{ 23, 0, 407, 320 }, 320 };
	default:
		// menu >= MENU_INGAME: setMenuDimentions(70, 0, 340, 320 - 64) (:4191-4194).
		return { kListRect, 320 - 64 };
	}
}

} // namespace

void MenuSession::init(const Env& env) {
	env_ = env;
}

UiRect MenuSession::listRect() const {
	if (isMainMenu(menu_)) return isMainHelpLeaf(menu_) ? kMainHelpRect : kMainRect;
	UiRect r = geometryFor(menu_).rect;
	// LoadHelpResource re-wraps an over-long page against a 27 px narrower rect
	// and leaves menuRect[2] narrowed (:3697), which is what moves the text off
	// the scrollbar: 23 + 407 = 430 is exactly the bar's left edge. paint() then
	// reads that same width for the divider cell count (:909) and for a centred
	// label (:965), so the whole screen has to see it.
	if (helpNarrowed_) r.w -= kHelpRectNarrowPx;
	return r;
}

UiRect MenuSession::barRect() const {
	const int initRectH = geometryFor(menu_).initRectH;
	return UiRect{ isVendingMenu(menu_) ? kVendingBarX : kBarX, (initRectH - kBarH) >> 1,
		kBarW, kBarH };
}

// ST_MENU entry hook: setMenu(menuId) — MENU_INGAME from gameplay, MENU_INGAME_DEAD
// from the death sequence (src/GameStateRunner.cpp:279). The state change itself
// is the caller's business here — the legacy setMenu pushes ST_MENU onto the
// canvas (:650-653), the rewrite arrives from GameContext::enterState_.
void MenuSession::begin(int menuId) {
	// The popup cannot survive a screen change (it swallows every action while
	// it is up), but the entry hook resets it anyway, exactly as the legacy
	// constructor leaves it (:159-160).
	helpIndex_ = -1;
	infoIndex_ = -1;
	// Legacy clears the stack on every ST_MENU exit (src/Canvas.cpp:1033-1035),
	// so it is empty on entry; setMenu clears it only for MENU_INGAME / MENU_MAIN (:619-621).
	clearStack();
	setMenu(menuId);
}

// The ST_CREDITS BACK/FIRE handler's menu half (src/InputEventController.cpp:398-412).
// menu_ still holds the screen Credits was opened from, so setMenu keeps its cursor (:1226-1229).
void MenuSession::returnFromCredits() {
	helpIndex_ = -1;
	infoIndex_ = -1;
	clearStack();                                      // leaving ST_MENU emptied it (src/Canvas.cpp:1033-1035)
	if (env_.game == nullptr || env_.game->loadMapID == 0) {
		const int idx = 3 + ((env_.hasSavedState && env_.hasSavedState()) ? 1 : 0);
		pushMenu(kMenuMain, idx, 0);                   // verbatim quirk: idx 3/4 is "Mini Games" (FE §12.4)
		setMenu(kMenuMainHelp);
	} else {
		pushMenu(kMenuInGame, 7, 0);
		setMenu(kMenuHelp);
	}
}

void MenuSession::setMenu(int menuId) {
	// clearStack() for MENU_INGAME and MENU_MAIN (:619-621). MENU_MAIN_BEGIN and
	// MENU_INGAME_KICKING, the other two ids of that test, are unreachable here.
	if (menuId == kMenuInGame || menuId == kMenuMain) clearStack();
	oldMenu_ = menu_;
	menu_ = menuId;
	initMenu(menu_);
}

void MenuSession::initMenu(int menuId) {
	if (env_.menus == nullptr) {
		numItems_ = 0;
		return;
	}
	// setMenuSettings runs at the top of every initMenu (:1242), so the rect a
	// previous help page narrowed is restored here and nowhere else.
	helpNarrowed_ = false;

	type_ = env_.menus->type(menuId);
	if (type_ < 0) {
		if (!isCodeBuiltScreen(menuId)) {
			// Absent from menus.bin and not built here either. Reaching one is a
			// bug: initMenu refuses a GOTO to such an id, so no such screen can
			// be entered (see the disable loop below).
			std::fprintf(stderr, "[menu] id %d has no menus.bin row and no code-built body\n",
				menuId);
		}
		// A fallback only. Every code-built body below assigns the real type
		// itself, exactly where its initMenu case does: 5 for the ten HELP
		// leaves (:1664 and its nine siblings), 6 for the three confirms
		// (:1715, :1729, :1736), 7 inside LoadNotebook (:3779).
		type_ = kMenuTypeList;
	}

	if (menuId != oldMenu_) {                          // (:1227-1230)
		scrollIndex_ = 0;
		selectedIndex_ = 0;
		// The drag offset is part of the scroll position, so it is dropped
		// exactly where the index is dropped and kept where the index is kept.
		hasDragScroll_ = false;
		dragScrollPx_ = 0;
	}

	numItems_ = 0;
	if (menuId == kMenuItems) {
		buildItemsScreen();
	} else if (menuId == kMenuItemsWeapons) {
		buildWeaponsScreen();
	} else if (menuId == kMenuItemsDrinks) {
		buildDrinksScreen();
	} else if (menuId == kMenuVending) {               // (:2062-2077)
		addVendingHeader(menuId);
	} else if (menuId == kMenuVendingDrinks) {
		buildVendingDrinksScreen();
	} else if (menuId == kMenuVendingSnacks) {
		buildVendingSnacksScreen();
	} else if (menuId == kMenuVendingConfirm || menuId == kMenuVendingCantBuy) {
		buildVendingOrderScreen(menuId == kMenuVendingConfirm);
	} else if (menuId == kMenuQuestLog) {              // (:1707-1710)
		loadNotebook();
	} else if (helpResourceFor(menuId) > 0) {          // (:1663-1705, :1749-1765)
		type_ = kMenuTypeHelp;
		loadHelpResource(helpResourceFor(menuId));
		if (menuId == kMenuMainEffectHelp)             // (:1295-1301); the in-game 44 has none (:1692-1696)
			addItem(kEmptyTextId, kItemNoSelect, kActionNone, 0, kEmptyTextId);
	} else if (menuId == kMenuLoad) {                  // (:1714-1719)
		type_ = kMenuTypeVCenter;
		scrollIndex_ = 0;
		setYesNo(kStrLoadGameQuestion, 1, kActionLoad, 0);
	} else if (menuId == kMenuRestartLvl) {            // (:1728-1733)
		type_ = kMenuTypeVCenter;
		scrollIndex_ = 0;
		setYesNo(kStrRestartLevelQuestion, 1, kActionRestartLevel, 0);
	} else if (menuId == kMenuSaveQuit) {              // (:1735-1740)
		type_ = kMenuTypeVCenter;
		scrollIndex_ = 0;
		// The one caller that spells the NO branch out; 2/0 is also the default.
		setYesNo(kStrSaveQuitQuestion, 1, kActionSaveQuit, 0, kActionBack, 0);
	} else if (menuId == kMenuItemsConfirm) {          // (:2047-2059)
		type_ = kMenuTypeVCenter;
		scrollIndex_ = 0;
		// find(6, 0, menuParam)->name — the INVENTORY def even for 22 (entities.bin
		// has (6,0,22), name 86 "Holy Water", same id as the ammo def's).
		std::string name;
		const EntityDef* d = env_.defs != nullptr ? env_.defs->find(Enums::ET_ITEM,
			Enums::ITEM_CLASS_INVENTORY, menuParam_) : nullptr;
		if (d != nullptr && env_.loc != nullptr) name = env_.loc->get(kTextIngame, d->name);
		else std::fprintf(stderr, "[menu] confirm: no def (6,0,%d)\n", menuParam_);
		const int peek = stackCount_ > 0 ? menuStack_[stackCount_ - 1] : 0;   // peekMenu()
		setYesNo(kStrUseItemQuestion, 1,
			peek == kMenuItemsDrinks ? kActionUseItemSyring : kActionUseItemOther,
			menuParam_, kActionBack, 0, &name, 1);
	} else if (menuId == kMenuInGame && env_.player != nullptr && env_.player->isFamiliar) {
		// Familiar root (:1551-1557): "Return to player" as row 1, Inventory
		// and Status greyed out.
		addParsedRows(menuId, 0, 1);
		addItem(env_.loc->makeId(kTextIngame2, 355), kItemNormal, kActionReturnToPlayer, 0,
			env_.loc->makeId(kTextIngame2, 356));
		addParsedRows(menuId, 1, -1);
		if (numItems_ > 6) {
			items_[0].def.flags = kItemDisabled;
			items_[6].def.flags = kItemDisabled;
		}
	} else if (menuId == kMenuMain) {                  // (:1256-1272)
		if (env_.hasSavedState && env_.hasSavedState())
			addItem(env_.loc->makeId(kTextIngame2, 70), kItemAlignCenter, kActionContinue, 0, kEmptyTextId);
		addParsedRows(menuId, 0, 2);                   // New Game, Options
		addParsedRows(menuId, 3, -1);                  // skips row 2 "Other Games" [GEC]
	} else if (menuId == kMenuMainDifficulty) {        // (:1274-1281)
		addParsedRows(menuId, 1, -1);                  // title row 0 not shown
	} else if (menuId == kMenuMainHelp) {              // (:1282-1287)
		addParsedRows(menuId, 1, -1);                  // row 0 "Back" dropped
	} else if (menuId == kMenuMainMinigame) {          // (:1275-1281)
		addParsedRows(menuId, 1, -1);                  // "Back" dropped
	} else if (menuId == kMenuMainExit) {              // (:1345-1350)
		setYesNo(106, 0, kActionExit, 0);              // "Exit Game?", No preselected
	} else if (menuId == kMenuMainConfirmNew) {        // (:1352-1357)
		type_ = kMenuTypeVCenter;
		setYesNo(107, 0, kActionGoto, kMenuMainDifficulty);   // "This will erase\n...", No preselected
	} else if (menuId == kMenuComicBook) {
		// empty: ComicBookSession owns the screen (:2276-2278)
	} else {
		// loadMenuItems(menu, 0, -1) (:2724-2727): the whole span, copied because
		// initMenu patches flags per screen (:1549-1573).
		addParsedRows(menuId, 0, -1);
	}

	// Per-screen patches (:1549-1573). The camera-active variant is not
	// ported: the rewrite can only open the menu from Playing (spec §4.2).
	if (menuId == kMenuInGame) {
		selectedIndex_ = 1;
		if (env_.player != nullptr && !env_.player->isFamiliar && numItems_ > 2 &&
		    env_.player->inventory[18] == 0) {
			items_[2].def.flags = env_.player->inventory[18] + kItemDisabled;
			// This is the ONE data-driven ITEM_DISABLED the in-game root can
			// produce, and its on-screen look is unverified (see MenuView.cpp
			// kDisabledOverlay). Logged so a screenshot can be attributed:
			// if this line is absent, row 2 is a plain row.
			std::fprintf(stderr, "[menu] row 2 ITEM_DISABLED by data rule "
				"(inventory[18] == 0) — unverified appearance\n");
		}
	} else if (menuId == kMenuPlayer) {
		selectedIndex_ = 2;                            // (:1575)
	} else if (menuId == kMenuMainDifficulty) {
		selectedIndex_ = 1;                            // (:1276), lands on "Normal"
	} else if (menuId == kMenuMainMinigame) {
		selectedIndex_ = 1;                            // (:1276); the initMenu tail moves it onto Hacker
	}

	// fillStatus' subset: the rows whose value the rewrite can actually source
	// (:1575-1594 marks them with textField2 = ARGUMENT1..17).
	fillValues();

	// Rewrite-only refusals (spec §11.2): keyed on the row's ACTION, not on its
	// index, so the same rule covers the sub-screens when they arrive. At the
	// root this marks exactly `Save Game` (ACTION_SAVE) and `View Map`
	// (ACTION_CHANGESTATE 6 = ST_AUTOMAP).
	//
	// This is NOT ITEM_DISABLED: such a row keeps the normal 296 px plate and
	// stays selectable, exactly as the reference build draws `Save Game`. The
	// only effect is that select() refuses it (see there).
	for (int i = 0; i < numItems_; ++i) {
		switch (items_[i].def.action) {
		case kActionGoto:
			// A GOTO whose target has no menus.bin row would open an empty
			// screen (the code-built bodies are G6/G7), and MENU_INGAME_CONTROLS
			// exists in the file but drives Hud::drawArrowControls, which the
			// rewrite does not have (spec §11.2, §15).
			if (items_[i].def.param == kMenuControls) {
				disableRow(i, "no arrow-controls screen");
			} else if (const char* why = unportedMainScreenReason(items_[i].def.param)) {
				disableRow(i, why);
			} else if (env_.menus->type(items_[i].def.param) < 0 &&
			           !isCodeBuiltScreen(items_[i].def.param)) {
				// G7 widened this: an id absent from menus.bin is now refused
				// only while the rewrite has no code-built body for it either.
				// 49/52/53 have one from here on; 46 and the HELP leaves do not.
				disableRow(i, "screen is built in code, not ported yet (G6)");
			}
			break;
		case kActionChangeState:
			// 6 = ST_AUTOMAP, 15 = ST_CREDITS (src/Canvas.h:68-93)
			if (items_[i].def.param != 6 && items_[i].def.param != 15)
				disableRow(i, "no rewrite state for this ACTION_CHANGESTATE");
			break;
		case kActionShowDetails:
			// showDetailsMenu() -> MENU_SHOWDETAILS (71), built in code from the
			// def's help text (:1907-1926).
			disableRow(i, "details screen is built in code, not ported yet (G8)");
			break;
		default:
			break;
		}
	}

	// Restored-from-J2ME tail (:2732-2736).
	if (selectedIndex_ >= numItems_) {
		moveDir(-1);
	} else if (!selectable(selectedIndex_)) {
		moveDir(1);
	}
}

// ---- row producers ----

// addItem (:3997-4003). The original errors out at 50 rows; the rewrite drops
// the row and logs, which keeps every already-built row consistent.
int MenuSession::addItem(const MenuItemDef& def) {
	if (numItems_ >= kMaxRows) {
		std::fprintf(stderr, "[menu] menu %d exceeds %d rows — row dropped\n",
			menu_, kMaxRows);
		return -1;
	}
	const int i = numItems_++;
	items_[i].def = def;
	items_[i].disabledByRewrite = false;
	items_[i].disabledReason = nullptr;
	items_[i].hasValue = false;
	items_[i].hasLiteralLabel = false;
	return i;
}

int MenuSession::addItem(int labelId, int flags, int action, int param, int helpId) {
	MenuItemDef def;
	def.labelId = labelId;
	def.flags = flags;
	def.action = action;
	def.param = param;
	def.helpId = helpId;
	return addItem(def);
}

// loadMenuItems(menu, begItem, numItems) (:4005-4035): skip `beg` items of the
// menus.bin span, append `count` of them, count < 0 = all that remain.
void MenuSession::addParsedRows(int menuId, int beg, int count) {
	int total = 0;
	const MenuItemDef* src = env_.menus->items(menuId, total);
	if (beg < 0 || beg > total) return;
	int n = (count < 0) ? total - beg : count;
	if (n > total - beg) n = total - beg;
	for (int k = 0; k < n; ++k) addItem(src[beg + k]);
}

// The four-times-repeated item row of MENU_ITEMS and the weapon row of
// MENU_ITEMS_WEAPONS: label = def->name, help = def->description, both in
// FILE_ENTITYSTRINGS = text type 1 (src/MenuStrings.h:21). The count/ammo string
// is the caller's business — this only reserves the row.
int MenuSession::addEntityRow(int subType, int defParm, int flags, int action, int param) {
	if (env_.defs == nullptr || env_.loc == nullptr) return -1;
	const EntityDef* d = env_.defs->find(Enums::ET_ITEM, subType, defParm);
	if (d == nullptr) {
		std::fprintf(stderr, "[menu] no entity def (6, %d, %d) — row skipped\n",
			subType, defParm);
		return -1;
	}
	return addItem(env_.loc->makeId(kTextIngame, d->name), flags, action, param,
		env_.loc->makeId(kTextIngame, d->description));
}

// MENU_ITEMS_DRINKS (:2024-2045): the menus.bin span (blank + "NA-NO DRINKS"
// divider), then one row per owned drink 0..10, or "[None]".
void MenuSession::buildDrinksScreen() {
	addParsedRows(kMenuItemsDrinks, 0, -1);
	Player* p = env_.player;
	if (p == nullptr || env_.loc == nullptr) return;
	bool any = false;
	for (int slot = Enums::INV_DRINK_MIN; slot < Enums::INV_DRINK_MAX; ++slot) {
		const int n = p->inventory[slot];
		if (n <= 0) continue;
		const int i = addEntityRow(Enums::ITEM_CLASS_INVENTORY, slot, kItemShowDetails,
			kActionConfirmUse, slot);                          // flags 32, action 24 (:2038)
		if (i < 0) continue;
		any = true;
		valueBuf_[i].setLength(0);
		valueBuf_[i].append(n);
		items_[i].hasValue = true;
	}
	if (!any) {                                                // "[None]", flags 9 (:2041-2043)
		addItem(env_.loc->makeId(kTextIngame2, 1), kItemNoSelect | kItemAlignCenter,
			kActionNone, 0, kEmptyTextId);
	}
}

int MenuSession::addLiteralRow(int srcLabelId, const std::string& text, int flags,
	int action, int param, int helpId) {
	const int i = addItem(srcLabelId, flags, action, param, helpId);
	if (i < 0) return -1;
	literalBuf_[i].setLength(0);
	literalBuf_[i].append(text);
	items_[i].hasLiteralLabel = true;
	return i;
}

int MenuSession::addVendingCreditsRow() {
	if (env_.loc == nullptr || env_.player == nullptr) return -1;
	std::string s = env_.loc->get(kTextMain, kStrCredits);
	const std::string arg = std::to_string(env_.player->inventory[Enums::INV_ONE_UAC_CREDIT]);
	composeArgs(s, &arg, 1);
	return addLiteralRow(env_.loc->makeId(kTextMain, kStrCredits), s,
		kItemNoSelect | kItemAlignCenter, kActionNone, 0, kEmptyTextId);
}

// loadMenuItems(menu, 0, 1), the credits row, loadMenuItems(menu, 1, -1).
void MenuSession::addVendingHeader(int menuId) {
	addParsedRows(menuId, 0, 1);
	addVendingCreditsRow();
	addParsedRows(menuId, 1, -1);
}

// 86 and 87 share the build: drink iff 0 <= menuParam < 11 (a snack's param is
// its composite string id), "<name> x <qty>" and "To-tal Cost: <price*qty>".
// 86 caches the price on the machine and makes the name row the buy action;
// 87 ends in "You can't af-ford this!" and a Back row.
void MenuSession::buildVendingOrderScreen(bool confirm) {
	const int menuId = confirm ? kMenuVendingConfirm : kMenuVendingCantBuy;
	VendingMachine* vm = env_.vending;
	if (vm == nullptr || env_.loc == nullptr || env_.defs == nullptr) {
		addParsedRows(menuId, 0, -1);
		return;
	}
	const bool drink = menuParam_ >= 0 && menuParam_ < 11;
	const EntityDef* d = env_.defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_INVENTORY,
		drink ? menuParam_ : Enums::INV_HEALTH_MIN);
	const int price = drink ? vm->getDrinkPrice(menuParam_) : vm->getSnackPrice();
	std::string name;
	if (drink) {
		if (d != nullptr) name = env_.loc->get(kTextIngame, d->name);
	} else {
		name = env_.loc->get(env_.loc->typeOf(menuParam_), env_.loc->indexOf(menuParam_));
	}
	const int helpId = d != nullptr ? env_.loc->makeId(kTextIngame, d->description) : kEmptyTextId;
	if (confirm) vm->currentItemPrice = price;

	const std::string itemArgs[2] = { name, std::to_string(vm->currentItemQuantity) };
	std::string item = env_.loc->get(kTextIngame2, kStrCostItem);
	composeArgs(item, itemArgs, 2);
	const std::string totalArg = std::to_string(price * vm->currentItemQuantity);
	std::string total = env_.loc->get(kTextIngame2, kStrCostItemEmpty);
	composeArgs(total, &totalArg, 1);

	addParsedRows(menuId, 0, 1);
	addVendingCreditsRow();
	addItem(kEmptyTextId, kItemNoSelect | kItemAlignCenter, kActionNone, 0, kEmptyTextId);
	addParsedRows(menuId, 1, -1);
	if (confirm) {
		addLiteralRow(env_.loc->makeId(kTextIngame2, kStrCostItem), item, kItemNormal,
			drink ? kActionBuyDrink : kActionBuySnack, menuParam_, helpId);
	} else {
		addLiteralRow(env_.loc->makeId(kTextIngame2, kStrCostItem), item, kItemNoSelect,
			kActionNone, 0, kEmptyTextId);
	}
	addLiteralRow(env_.loc->makeId(kTextIngame2, kStrCostItemEmpty), total, kItemNoSelect,
		kActionNone, 0, kEmptyTextId);
	const int note = kItemNoSelect | kItemAlignCenter;   // flags 9
	addItem(kEmptyTextId, note, kActionNone, 0, kEmptyTextId);
	if (confirm) {
		addItem(env_.loc->makeId(kTextIngame2, 346), note, kActionNone, 0, kEmptyTextId);  // "Press UP/DOWN"
		addItem(env_.loc->makeId(kTextIngame2, 347), note, kActionNone, 0, kEmptyTextId);  // "to change quantity"
		addItem(kEmptyTextId, note, kActionNone, 0, kEmptyTextId);
		addItem(env_.loc->makeId(kTextIngame2, 348), note, kActionNone, 0, kEmptyTextId);  // "Tap item to buy"
		addItem(kEmptyTextId, note, kActionNone, 0, kEmptyTextId);
		addItem(env_.loc->makeId(kTextIngame2, 349), note, kActionNone, 0, kEmptyTextId);  // "Press back to go back"
	} else {
		addItem(env_.loc->makeId(kTextIngame2, 354), note, kActionNone, 0, kEmptyTextId);  // "You can't afford this!"
		addItem(env_.loc->makeId(kTextMain, 96), kItemAlignCenter, kActionBack, 0, helpId); // "Tap Screen to Continue"
	}
}

// Drinks 0..10 this machine sells: name/description from (6,0,n), the price in
// the value column, flags 32, action 28 with the slot as param (:2096-2103).
void MenuSession::buildVendingDrinksScreen() {
	addVendingHeader(kMenuVendingDrinks);
	if (env_.vending == nullptr) return;
	for (int n = 0; n < 11; ++n) {
		if (!env_.vending->drinkInThisVendingMachine(n)) continue;
		const int i = addEntityRow(Enums::ITEM_CLASS_INVENTORY, n, kItemShowDetails,
			kActionConfirmBuy, n);
		if (i < 0) continue;
		valueBuf_[i].setLength(0);
		valueBuf_[i].append((int)env_.vending->getDrinkPrice(n));
		items_[i].hasValue = true;
	}
}

// Twelve cosmetic names, all the same snack (6,0,16) at one price; the param
// is the composite string id, which is how 86/87 tell a snack from a drink.
void MenuSession::buildVendingSnacksScreen() {
	addVendingHeader(kMenuVendingSnacks);
	if (env_.vending == nullptr || env_.loc == nullptr || env_.defs == nullptr ||
	    env_.game == nullptr) return;
	const int mapId = env_.game->loadMapID;   // canvas->loadMapID (:2123)
	int set = 1;
	if (mapId >= 1 && mapId <= 3) set = 0;
	else if (mapId >= 7 && mapId <= 9) set = 2;
	const EntityDef* d = env_.defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_INVENTORY,
		Enums::INV_HEALTH_MIN);   // slot 16, the snack
	const int helpId = d != nullptr ? env_.loc->makeId(kTextIngame, d->description) : kEmptyTextId;
	for (int k = 0; k < kSnackCount; ++k) {
		const int id = env_.loc->makeId(kTextIngame2, kSnackSets[set][k]);
		const int i = addItem(id, kItemNormal, kActionConfirmBuy, id, helpId);
		if (i < 0) continue;
		valueBuf_[i].setLength(0);
		valueBuf_[i].append(env_.vending->getSnackPrice());
		items_[i].hasValue = true;
	}
}

// MENU_ITEMS (:1930-1986). Row order is the original's, which is NOT the
// menus.bin order: the health/armor groups come first, then menus.bin row 1
// (the INVENTORY divider), then the drinks entry, then menus.bin row 2 (the
// Weapons plate), then the credits row. menus.bin row 0 — a blank spacer — is
// never loaded here: the legacy calls are loadMenuItems(menu, 1, 1) and
// loadMenuItems(menu, 2, -1) (:1970, :1974).
void MenuSession::buildItemsScreen() {
	Player* p = env_.player;
	if (p == nullptr || env_.loc == nullptr) {
		addParsedRows(kMenuItems, 1, -1);
		return;
	}

	// Health group, inventory 16..17 (:1932-1946). The divider is emitted lazily,
	// so an empty group contributes nothing at all.
	bool needHeader = true;
	for (int slot = Enums::INV_HEALTH_MIN; slot < Enums::INV_HEALTH_MAX; ++slot) {
		const int n = p->inventory[slot];
		if (n <= 0) continue;
		if (needHeader) {
			needHeader = false;
			addItem(env_.loc->makeId(kTextIngame2, kStrItemsHealthTitle),
				kItemDivider | kItemAlignCenter | kItemNoSelect, kActionNone, 0,
				kEmptyTextId);                                     // flags 73 (:1937)
		}
		const int i = addEntityRow(Enums::ITEM_CLASS_INVENTORY, slot, kItemShowDetails,
			kActionConfirmUse, slot);
		if (i < 0) continue;
		valueBuf_[i].setLength(0);
		valueBuf_[i].append(n);
		items_[i].hasValue = true;
	}

	// The holy-water charge, listed as a usable item once the pistol is owned and
	// there are at least 25 units of it (:1947-1953). Its NAME comes from the
	// AMMO def (6, 2, 3) while the row's param is the INVENTORY slot 22.
	if (p->ammo[Enums::AMMO_HOLY_WATER] >= 25 &&
	    (p->weapons & (1 << Enums::WP_HOLY_WATER_PISTOL)) != 0) {
		const int i = addEntityRow(Enums::ITEM_CLASS_AMMO, Enums::AMMO_HOLY_WATER,
			kItemShowDetails, kActionConfirmUse, Enums::INV_OTHER_HOLY_WATER);
		if (i >= 0) {
			valueBuf_[i].setLength(0);
			valueBuf_[i].append(p->ammo[Enums::AMMO_HOLY_WATER]);
			items_[i].hasValue = true;
		}
	}

	// Armor group, inventory 11..12 (:1954-1968).
	needHeader = true;
	for (int slot = Enums::INV_ARMOR_MIN; slot < Enums::INV_ARMOR_MAX; ++slot) {
		const int n = p->inventory[slot];
		if (n <= 0) continue;
		if (needHeader) {
			needHeader = false;
			addItem(env_.loc->makeId(kTextIngame2, kStrItemsArmorTitle),
				kItemDivider | kItemAlignCenter | kItemNoSelect, kActionNone, 0,
				kEmptyTextId);                                     // flags 73 (:1959)
		}
		const int i = addEntityRow(Enums::ITEM_CLASS_INVENTORY, slot, kItemShowDetails,
			kActionConfirmUse, slot);
		if (i < 0) continue;
		valueBuf_[i].setLength(0);
		valueBuf_[i].append(n);
		items_[i].hasValue = true;
	}

	addParsedRows(kMenuItems, 1, 1);                               // (:1970)

	// hasANanoDrink(): any of inventory 0..10 (src/Player.cpp:2655-2662).
	bool hasNanoDrink = false;
	for (int slot = Enums::INV_DRINK_MIN; slot < Enums::INV_DRINK_MAX; ++slot) {
		if (p->inventory[slot] > 0) { hasNanoDrink = true; break; }
	}
	if (hasNanoDrink) {                                            // (:1971-1973)
		addItem(env_.loc->makeId(kTextIngame2, kStrNanoDrinksItem), kItemNormal,
			kActionGoto, kMenuItemsDrinks,
			env_.loc->makeId(kTextIngame2, kStrNanoDrinksItemHelp));
	}

	addParsedRows(kMenuItems, 2, -1);                              // (:1974)

	// UAC credits: always present, always last, and the one row of this screen
	// whose action is ACTION_SHOWDETAILS rather than ACTION_CONFIRMUSE
	// (:1976-1982).
	const int credits = addEntityRow(Enums::ITEM_CLASS_INVENTORY,
		Enums::INV_ONE_UAC_CREDIT, kItemShowDetails, kActionShowDetails,
		Enums::INV_ONE_UAC_CREDIT);
	if (credits >= 0) {
		valueBuf_[credits].setLength(0);
		valueBuf_[credits].append(p->inventory[Enums::INV_ONE_UAC_CREDIT]);
		items_[credits].hasValue = true;
	}
}

// MENU_ITEMS_WEAPONS (:1988-2021): both menus.bin rows (a blank spacer and the
// WEAPONS divider) first, then one row per owned weapon bit.
void MenuSession::buildWeaponsScreen() {
	addParsedRows(kMenuItemsWeapons, 0, -1);                       // (:1989)
	Player* p = env_.player;
	if (p == nullptr || env_.tables == nullptr) return;

	for (int w = 0; w < Enums::WP_PLAYERMAX; ++w) {
		if ((p->weapons & (1 << w)) == 0) continue;
		// Holding the item weapon greys out every OTHER weapon (:1996). This is a
		// data-driven ITEM_DISABLED, so it gets the legacy look; nothing else on
		// this screen does.
		int flags = (p->ce.weapon == Enums::WP_ITEM && w != Enums::WP_ITEM)
			? kItemDisabled : 0;
		if (p->ce.weapon == w) flags |= kItemChecked;              // (:1999-2001)
		const int i = addEntityRow(Enums::ITEM_CLASS_WEAPON, w, flags | kItemShowDetails,
			kActionUseItemWeapon, w);
		if (i < 0) continue;

		// Value column = the weapon's ammo pool (:2003-2018). WEAPON_FIELD_AMMOTYPE
		// is weapons[w * 9 + 4]; AMMO_NONE prints one '\x80' glyph and the soul
		// cube prints "n/5" against AMMO_MAX_SOULS.
		Text& v = valueBuf_[i];
		v.setLength(0);
		const int ammoType = env_.tables->weaponDef(w).ammoType;
		if (ammoType != Enums::AMMO_NONE) {
			if (ammoType == Enums::AMMO_SOUL_CUBE) {
				v.append(p->ammo[Enums::AMMO_SOUL_CUBE]);
				v.append("/");
				v.append(Enums::AMMO_MAX_SOULS);
			} else if (ammoType >= 0 && ammoType < 9) {
				v.append(p->ammo[ammoType]);
			} else {
				std::fprintf(stderr, "[menu] weapon %d has ammoType %d out of range\n",
					w, ammoType);
			}
		} else {
			v.append(kNoAmmoGlyph);
		}
		items_[i].hasValue = true;
	}

	static bool logged = false;
	if (!logged) {
		logged = true;
		std::fprintf(stderr, "[menu] weapons screen: mask 0x%x, %d rows (2 from menus.bin)\n",
			p->weapons, numItems_);
	}
}

// One message line of a confirm screen. The original splits the composed
// question into localization text args and points each row at ARGUMENT1..N
// (addTextArg + getLastArgString, src/Text.cpp:247-254,
// src/MenuSystem.cpp:3638-3647, :4090-4098). The rewrite has no text-arg table,
// so the piece is copied verbatim into literalBuf_ while the row keeps the
// SOURCE string id: that id is never EMPTY_TEXT, exactly like ARGUMENT1..N, so
// the EMPTY_TEXT tests in paint() and moveDir() behave as they did.
int MenuSession::addMessageRow(int srcStrId, const Text& src, int beg, int end) {
	const int labelId = (env_.loc != nullptr) ? env_.loc->makeId(kTextIngame2, srcStrId)
	                                          : kEmptyTextId;
	const int i = addItem(labelId, kYesNoMessageFlags, kActionNone, 0, kEmptyTextId);
	if (i < 0) return -1;
	literalBuf_[i].setLength(0);
	literalBuf_[i].append(src, beg, end - beg);
	items_[i].hasLiteralLabel = true;
	return i;
}

// SetYESNO (:3611-3660). The two short overloads only compose the question and
// default the NO branch to (ACTION_BACK, 0) (:3611-3618), so one function with a
// defaulted no-action covers all three.
//
// composeText(3, strId, text) (:3616) resolves %NN from the current text args;
// the rewrite passes them explicitly (`args`, the ITEMS_CONFIRM name) and
// composes with composeArgs. A '%' left over would mean an unresolved
// argument, which is worth a line in the log rather than a silent wrong string.
void MenuSession::setYesNo(int strId, int preselect, int yesAction, int yesParam,
	int noAction, int noParam, const std::string* args, int numArgs) {
	yesNoText_.setLength(0);
	if (env_.loc != nullptr) {
		std::string s = env_.loc->get(kTextIngame2, strId);
		if (args != nullptr) composeArgs(s, args, numArgs);
		yesNoText_.append(s);
	}
	if (yesNoText_.findFirstOf('%') >= 0) {
		std::fprintf(stderr, "[menu] confirm string %d has an unresolved argument: '%s'\n",
			strId, yesNoText_.c_str());
	}

	// The split, verbatim (:3635-3648): every '\n' ends a line, and the tail
	// after the last one is a line too; with no '\n' at all the whole string is
	// one line.
	if (yesNoText_.findFirstOf('\n', 0) >= 0) {
		int n6 = 0;
		int first;
		for (n6 = 0; (first = yesNoText_.findFirstOf('\n', n6)) >= 0; n6 = first + 1) {
			addMessageRow(strId, yesNoText_, n6, first);
		}
		addMessageRow(strId, yesNoText_, n6, yesNoText_.length());
	} else {
		addMessageRow(strId, yesNoText_, 0, yesNoText_.length());
	}

	addItem(kEmptyTextId, kYesNoMessageFlags, kActionNone, 0, kEmptyTextId);   // (:3650)
	const int yesLabel = (env_.loc != nullptr) ? env_.loc->makeId(kTextIngame2, kStrYesLabel)
	                                           : kEmptyTextId;
	const int noLabel = (env_.loc != nullptr) ? env_.loc->makeId(kTextIngame2, kStrNoLabel)
	                                          : kEmptyTextId;
	addItem(yesLabel, kYesNoButtonFlags, yesAction, yesParam, kEmptyTextId);   // (:3651)
	addItem(noLabel, kYesNoButtonFlags, noAction, noParam, kEmptyTextId);      // (:3652)

	// (:3654-3659) — i == 1 puts the cursor on YES, anything else on NO.
	selectedIndex_ = (preselect == 1) ? numItems_ - 2 : numItems_ - 1;
}

// One line of a help page. LoadHelpItems hands each '|'-separated piece to
// addTextArg and points the row at getLastArgString() (:3811-3838, :4090-4098);
// the rewrite has no text-arg table, so the piece is copied verbatim into
// literalBuf_ while the row keeps the SOURCE string id — never EMPTY_TEXT, just
// like ARGUMENT1..N, so the EMPTY_TEXT tests in paint() and moveDir() behave as
// they did. An empty piece (from a `||` in the shipped string) therefore stays a
// selectable, zero-glyph 16 px row: it is the paragraph spacing.
int MenuSession::addHelpRow(int srcLabelId, const Text& src, int beg, int end, int flags) {
	const int i = addItem(srcLabelId, flags, kActionNone, 0, kEmptyTextId);
	if (i < 0) return -1;
	literalBuf_[i].setLength(0);
	literalBuf_[i].append(src, beg, end - beg);
	items_[i].hasLiteralLabel = true;
	return i;
}

// LoadHelpItems(text, i) (:3811-3838), verbatim. n2 = 2 = ITEM_NODEHYPHENATE:
// the text has already been wrapped (and dehyphenated by wrapText), so paint
// must not dehyphenate it a second time.
void MenuSession::loadHelpItems(const Text& text, int extraFlags) {
	const int srcLabelId = (env_.loc != nullptr)
		? env_.loc->makeId(kTextHelp, helpTextIndex_) : kEmptyTextId;
	const int base = kItemNoDehyphenate | extraFlags;

	int n3 = 0;
	int first = 0;
	for (n3 = 0; (first = text.findFirstOf('|', n3)) >= 0; n3 = first + 1) {
		if (first > n3 && text.charAt(n3) == '#') {
			// A '#' line is a centred divider and LOSES the '#': the arg starts
			// at n3 + 1 (:3816-3818).
			addHelpRow(srcLabelId, text, n3 + 1, first,
				base | kItemAlignCenter | kItemDivider);
		} else {
			addHelpRow(srcLabelId, text, n3, first, base);
		}
	}
	const int length = text.length();
	if (n3 < length) {
		if (text.charAt(n3) == '#') {
			// VERBATIM quirk of the tail branch (:3830-3833): unlike the loop
			// above it starts the arg at n3, so the '#' is KEPT in the drawn
			// text, and it ORs 0x40 twice (0x8 | 0x40 | 0x40), which is the same
			// single ITEM_DIVIDER bit. Neither is a typo we may fix: the shipped
			// strings all put their '#' heading first, so this branch only fires
			// for a page whose LAST line starts with '#'.
			addHelpRow(srcLabelId, text, n3, length,
				base | kItemAlignCenter | kItemDivider | kItemDivider);
		} else {
			addHelpRow(srcLabelId, text, n3, length, base);
		}
	}
}

// LoadHelpResource(short i) (:3662-3702). The original pulls the body out of
// text type 2 through composeText, wraps it to the in-game budget, and — if the
// result is taller than the rect setMenuSettings left behind — throws the rows
// away and re-wraps against a 27 px narrower rect.
//
// composeText(2, i, text) (:3671) is not ported: none of the ten shipped help
// bodies carries a %NN argument (verified against the archive's strings chunk),
// so the shipped string IS the composed text. A '%' would mean an unresolved
// argument, which is worth a log line rather than a silently wrong page.
void MenuSession::loadHelpResource(int resource) {
	scrollIndex_ = 0;                                  // (:3666-3667)
	selectedIndex_ = 0;
	helpTextIndex_ = resource;

	helpText_.setLength(0);
	if (env_.loc != nullptr) helpText_.append(env_.loc->get(kTextHelp, resource));
	if (helpText_.length() == 0) {
		// No text substituted, no placeholder prose: the page is drawn empty and
		// the gap is stated here (the caller still gets rows only if the string
		// had any).
		std::fprintf(stderr, "[menu] help resource %d (text type 2) is empty — page left blank\n",
			resource);
	}
	if (helpText_.findFirstOf('%') >= 0) {
		std::fprintf(stderr, "[menu] help resource %d has an unresolved argument\n", resource);
	}

	// menu >= MENU_INGAME: ingameScrollWithBarMaxChars = 49; the main-menu
	// leaves: 35 (:3674).
	const bool mainLeaf = isMainMenu(menu_);
	const int wrap = mainLeaf ? kMainHelpWrap : kHelpMaxChars;
	helpText_.wrapText(wrap);
	loadHelpItems(helpText_, 0);

	int h = 0;
	for (int j = 0; j < numItems_; ++j) {
		// VERBATIM (:3678-3682): the original tests items[0].flags, not
		// items[j].flags. No help row ever carries ITEM_HIDDEN, so the two read
		// the same here — kept as written rather than "fixed".
		if ((items_[0].def.flags & kItemHidden) == 0) h += itemHeight(j);
	}

	// menuRect[3] is still the setMenuSettings height (256) at this point, NOT
	// the 241 paint narrows it to (:848); the second half of the original's
	// condition, `menu >= MENU_INGAME`, excludes the main-menu leaves.
	if (!mainLeaf && kHelpInitRectH < h) {             // (:3684-3699)
		numItems_ = 0;
		helpText_.setLength(0);
		if (env_.loc != nullptr) helpText_.append(env_.loc->get(kTextHelp, resource));
		helpNarrowed_ = true;                          // menuRect[2] -= 27 (:3697)
		// menuRect[2] / CHAR_SPACING[0] = 407 / 11 = 37 (:3698). listRect() is
		// already narrowed, so this is the original's own expression; the
		// division is by 11, never by the rewrite's 9 px advance (NEW FACT 9).
		helpText_.wrapText(listRect().w / kCharSpacing);
		loadHelpItems(helpText_, 0);
	}

	std::fprintf(stderr, "[menu] help %d: %d lines, wrap %d, rect w %d\n", resource,
		numItems_, helpNarrowed_ ? listRect().w / kCharSpacing : wrap,
		listRect().w);
}

// LoadNotebook (:3777-3809) — the SHELL only. The quest loop needs
// player->notebookIndexes / numNotebookIndexes / isQuestDone / isQuestFailed,
// filled by Player::updateQuests from EV_UPDATEJOURNAL, none of which the
// rewrite has (spec §10.2, §15). So the page carries its two header rows and
// nothing else: a visible, explainable gap rather than invented quest text.
void MenuSession::loadNotebook() {
	type_ = kMenuTypeNotebook;                         // (:3779)

	// items[n++].Set(render->mapNameField, EMPTY_TEXT, 9) (:3780): flags 9 =
	// ITEM_NOSELECT | ITEM_ALIGN_CENTER, a centred 16 px label row.
	int titleId = kEmptyTextId;
	if (env_.loc != nullptr && env_.tables != nullptr &&
	    (int)env_.tables->levelNames.size() >= kMapNameId) {
		titleId = env_.loc->makeId(kTextIngame2, env_.tables->levelNames[kMapNameId - 1]);
	} else {
		std::fprintf(stderr, "[menu] PDA: no level name for map %d — title row left blank\n",
			kMapNameId);
	}
	addItem(titleId, kItemNoSelect | kItemAlignCenter, kActionNone, 0, kEmptyTextId);
	// items[n++].Set(EMPTY_TEXT, EMPTY_TEXT, 73) (:3781): flags 73 =
	// ITEM_DIVIDER | ITEM_ALIGN_CENTER | ITEM_NOSELECT. An EMPTY_TEXT divider is
	// the full-width rule buildDivider draws from three seed glyphs.
	addItem(kEmptyTextId, kItemDivider | kItemAlignCenter | kItemNoSelect,
		kActionNone, 0, kEmptyTextId);

	// The page opens scrolled to the BOTTOM (:3805-3808): index = numItems minus
	// the number of lines that fit, clamped to 0. With the two header rows and
	// (320 - 26) / 16 = 18 lines that is 0 — the empty log has nothing to scroll.
	const int fitLines = (kDisplayH - 26) / kFontHeight;
	const int index = numItems_ - fitLines;
	scrollIndex_ = index;
	selectedIndex_ = index;
	if (index < 0) {
		selectedIndex_ = 0;
		scrollIndex_ = 0;
	}

	static bool logged = false;
	if (!logged) {
		logged = true;
		std::fprintf(stderr, "[menu] PDA: shell only (%d rows) — EV_UPDATEJOURNAL / "
			"Player::updateQuests are not ported, so the quest log is empty\n", numItems_);
	}
}

// fillStatus (:3928-3979) reduced to the values the rewrite can actually
// source. The original stores them as text arguments and paint resolves each
// row's textField2, which initMenu assigned as ARGUMENT1..17 (:1575-1594); the
// per-row mapping is the same here, the composed string just goes straight into
// valueBuf_.
//
// MENU_INGAME_PLAYER rows 2..11 (fillStatus' b2 block, :3931-3950) are all
// sourceable. Rows 12..18 (the b3 block, :3968-3977) need player->totalTime,
// totalMoves, totalDeaths and counters[1,2,6,7], which do not exist in the
// rewrite, so those rows keep no value at all rather than a made-up number
// (spec §15). The level block (b, :3951-3967) is fillLevelValues.
void MenuSession::fillValues() {
	if (menu_ == kMenuLevelStats) fillLevelValues(3, 5);   // initMenu (:1244-1253)
	else if (menu_ == kMenuLevel) fillLevelValues(1, 2);   // initMenu (:1599-1607)
	if (menu_ != kMenuPlayer || env_.player == nullptr) return;
	const Player& p = *env_.player;
	if (numItems_ < 12) return;

	buildFraction(valueBuf_[2], p.ce.getStat(Enums::STAT_HEALTH),
		p.ce.getStat(Enums::STAT_MAX_HEALTH));
	buildFraction(valueBuf_[3], p.ce.getStat(Enums::STAT_ARMOR), 200);
	valueBuf_[4].setLength(0);
	valueBuf_[4].append(p.level);
	valueBuf_[5].setLength(0);
	valueBuf_[5].append(p.currentXP);
	valueBuf_[6].setLength(0);
	valueBuf_[6].append(p.nextLevelXP);
	for (int stat = Enums::STAT_DEFENSE; stat <= Enums::STAT_IQ; ++stat) {
		buildModStat(valueBuf_[7 + stat - Enums::STAT_DEFENSE],
			p.baseCe.getStat(stat), p.ce.getStat(stat) - p.baseCe.getStat(stat));
	}
	for (int i = 2; i <= 11; ++i) items_[i].hasValue = true;

	static bool logged = false;
	if (!logged) {
		logged = true;
		std::fprintf(stderr, "[menu] status values: rows 2-11 filled; rows 12-18 "
			"(time/turns/deaths/shots/best shot/total damage) have no source in "
			"the rewrite and stay blank\n");
	}
}

// fillStatus(true, false, false) (:3951-3967): %01 map name becomes the label
// of nameRow (its textField is ARGUMENT1), %02..%07 the values of six rows.
void MenuSession::fillLevelValues(int nameRow, int firstValueRow) {
	if (env_.game == nullptr || env_.player == nullptr || env_.map == nullptr ||
	    env_.loc == nullptr || env_.tables == nullptr || env_.gameTime == nullptr) return;
	if (numItems_ < firstValueRow + 6) return;
	Game& g = *env_.game;
	const Player& p = *env_.player;
	const Localization& loc = *env_.loc;
	constexpr int kArgument1 = 241;                                // MenuStrings::ARGUMENT1 (src/MenuStrings.h:290)

	// levelNames[loadMapID-1] stands in for render->mapNameID (mapNameField,
	// src/LoadingManager.cpp:331); the two are equal on the shipped maps.
	const std::vector<int16_t>& names = env_.tables->levelNames;
	items_[nameRow].def.labelId = loc.makeId(kTextIngame2, kArgument1);
	literalBuf_[nameRow].setLength(0);
	if (g.loadMapID >= 1 && g.loadMapID <= static_cast<int>(names.size()))
		literalBuf_[nameRow].append(loc.get(kTextIngame2, names[g.loadMapID - 1]));
	items_[nameRow].hasLiteralLabel = true;

	Text* v = &valueBuf_[firstValueRow];
	formatTime(v[0], g.totalLevelTime + static_cast<int>(*env_.gameTime - g.curLevelTime));
	buildFraction(v[1], g.mapSecretsFound, env_.map->totalSecrets);
	int killed = 0, total = 0;
	g.fillMonsterStats(killed, total);
	buildFraction(v[2], killed, total);
	v[3].setLength(0);
	v[3].append(p.moves);
	v[4].setLength(0);
	v[4].append(p.currentLevelDeaths);
	v[5].setLength(0);
	v[5].append(loc.get(kTextIngame2, Player::gradeToString(g.levelGrade(false))));
	for (int i = 0; i < 6; ++i) items_[firstValueRow + i].hasValue = true;
}

void MenuSession::disableRow(int i, const char* reason) {
	if (i < 0 || i >= numItems_) return;
	items_[i].disabledByRewrite = true;
	items_[i].disabledReason = reason;
}

bool MenuSession::selectable(int i) const {
	if (i < 0 || i >= numItems_) return false;
	const MenuItemDef& d = items_[i].def;
	return d.labelId != kEmptyTextId && (d.flags & kUnselectableMask) == 0;
}

// getMenuItemHeight (:4947-4984) for the in-game tree; the [GEC] ITEM_PADDING /
// ITEM_SCROLLBAR sheights have no row in this subtree.
int MenuSession::itemHeight(int i) const {
	if (items_[i].def.action != 0) {
		if (i == numItems_ - 1) return kItemHeightPx;
		if (isMainMenu(menu_)) return kItemHeightPx + kMainItemPadding;
		return kItemHeightPx + (isVendingMenu(menu_) ? kVendingPaddingBottom : kItemPaddingBottom);
	}
	if (i == numItems_ - 1) return kFontHeight;
	return kFontHeight + kItemFontPaddingBottom;
}

// Total pixel height of the list: the legacy sum that decides whether a
// scrollbar exists at all and feeds SetScrollBox as the content size
// (:2742-2751). ITEM_HIDDEN rows contribute nothing.
int MenuSession::contentHeight() const {
	int total = 0;
	for (int i = 0; i < numItems_; ++i) {
		if ((items_[i].def.flags & kItemHidden) != 0) continue;
		total += itemHeight(i);
	}
	return total;
}

// ASSUMPTION A2 (spec §6.3, §14): the original's authoritative scroll model is
// the item-index pair scrollIndex/maxItems; the port's pixel sync block
// (src/MenuSystem.cpp:463-546) is [GEC] and the original's exact pixel
// behaviour is UNKNOWN. So the rewrite derives the pixel offset from the item
// index — the top of the first windowed row — and then applies the widget's own
// clamp to [0, contentPx - viewPx] (UpdateContent, src/Button.cpp:470-478).
// Visible consequence of this choice: the first visible row is always flush
// with the top of the region except at maximum scroll, where the clamp bites.
int MenuSession::scrollPixels() const {
	// Legacy isMainMenu is false for type 5 (:2813): the leaves take the generic path.
	if (isMainMenu(menu_) && !isMainHelpLeaf(menu_)) {
		// getScrollPos' isMainMenu snap (:4927-4945): the first row whose half
		// height lies past the widget offset is drawn flush with the top.
		int raw = 0;
		int maxRaw = 0;
		mainScroll(raw, maxRaw);
		int posY = 0;
		for (int i = 0; i < numItems_; ++i) {
			if ((items_[i].def.flags & kItemHidden) != 0) continue;
			const int h = itemHeight(i);
			if (raw < posY + (h >> 1)) return posY;
			posY += h;
		}
		return raw;
	}
	const int maxScroll = contentHeight() - viewPx();
	if (maxScroll <= 0) return 0;
	// A touch drag writes the pixel offset directly and leaves scrollIndex
	// alone, exactly as UpdateContent writes field_0x44_ (src/Button.cpp:456-478)
	// while the item model stays where it was; the next key move recomputes the
	// pixels from the index again ([GEC] sync block, src/MenuSystem.cpp:463-546,
	// which runs at the end of every scroll move), which is why moveDir() drops
	// this override.
	if (hasDragScroll_) return std::clamp(dragScrollPx_, 0, maxScroll);
	int px = 0;
	for (int i = 0; i < scrollIndex_ && i < numItems_; ++i) {
		if ((items_[i].def.flags & kItemHidden) != 0) continue;
		px += itemHeight(i);
	}
	return std::clamp(px, 0, maxScroll);
}

// The main-menu scroll widget offset (A2 precedent): the index model's pixels,
// clamped to field_0x40_ - field_0x3c_ with the SetScrollBox content C - 50 and
// the initMenu-time view 152 (src/MenuSystem.cpp:2800-2802, src/Button.cpp:417-440).
void MenuSession::mainScroll(int& raw, int& maxRaw) const {
	maxRaw = std::max(0, contentHeight() - 50 - kMainInitRectH);
	int px = 0;
	for (int i = 0; i < scrollIndex_ && i < numItems_; ++i) {
		if ((items_[i].def.flags & kItemHidden) != 0) continue;
		px += itemHeight(i);
	}
	raw = std::clamp(px, 0, maxRaw);
}

// DELIBERATE DEVIATION (spec 2026-08-28-menu, DEVIATION section) — the last
// scrollIndex_ that still changes anything on screen, i.e. the FIRST index whose
// pixel offset already reaches the clamp maxScroll = contentPx - kViewPx.
// Every larger index maps to the same clamped offset in scrollPixels(), so it
// would be a dead press that has to be undone before the page moves again.
// Neither side of this is original behaviour: moveDir's bound `scrollIndex <
// numItems - maxItems` (src/MenuSystem.cpp:401-455) works in ITEMS with the
// [GEC] constant maxItems = 4 (:1231), while the type 5/7 pixel mapping it is
// paired with is the [GEC] sync block (src/MenuSystem.cpp:502-507). There is no
// J2ME behaviour to be faithful to, so this is a product decision: the port's own
// mapping was rejected because it still leaves ~9 dead presses AND makes the step
// uneven (6-14 px); clamping the index removes the dead range entirely and keeps
// one line per press. Do NOT "restore" the verbatim item-count bound.
int MenuSession::maxScrollIndex() const {
	const int maxScroll = contentHeight() - viewPx();
	if (maxScroll <= 0) return 0;
	int px = 0;
	for (int i = 0; i < numItems_; ++i) {
		if (px >= maxScroll) return i;
		if ((items_[i].def.flags & kItemHidden) != 0) continue;
		px += itemHeight(i);
	}
	// Unreachable with real content: the loop can only fall through if the last
	// row alone is taller than the whole view (h > kViewPx). numItems_ > 0 here,
	// because maxScroll > 0 means contentHeight() > kViewPx.
	return numItems_ - 1;
}

// thumbLen L = V*H/C (SetScrollBox, src/Button.cpp:397-405). Shared by the bar
// drag (which needs the free track length) and the view model.
int MenuSession::barThumbLen() const {
	const int contentPx = contentHeight();
	if (contentPx <= 0) return 0;
	return viewPx() * kBarH / contentPx;
}

// fmScrollButton::Update for a vertical bar whose touch offset was zeroed
// (src/Button.cpp:497-534, reached with field_0x54_ = 0 because the in-game
// tree has isMainMenuScrollBar == false, src/MenuSystem.cpp:4175, :4691-4699):
// the thumb centres on the touch, is clamped to the track, and the content
// offset follows through field_0x50_ = (C - V) / (H - L).
void MenuSession::barDragTo(int cursorY, int maxScroll) {
	const int track = kBarH - barThumbLen();
	if (track <= 0) return;
	const int thumb = std::clamp(cursorY - barRect().y - (barThumbLen() >> 1), 0, track);
	dragScrollPx_ = std::clamp(thumb * maxScroll / track, 0, maxScroll);
	hasDragScroll_ = true;
}

// handleUserMoved's two drag branches (src/MenuSystem.cpp:4869-4913) plus the
// press-on-the-bar branch of handleUserTouch (:4691-4699). This is the
// original's own touch behaviour, not a rewrite invention: a move whose current
// point is inside the scroll box latches SetContentTouchOffset and sets
// field_0x38_, and every later move runs UpdateContent.
void MenuSession::updateDrag(const UiInput& in) {
	gestureConsumed_ = false;
	// shortcut: no touch drag / dial drag on the main menu (deviation FE-D3);
	// port handleUserMoved's dial branch if wanted.
	if (isMainMenu(menu_)) return;
	const int maxScroll = contentHeight() - viewPx();

	if (!in.down) {
		// Release: handleUserTouch(b == false) clears whichever drag latch is
		// set and RETURNS before it can reach the button hit test
		// (:4676-4689) — the row the drag started on is never selected.
		if (drag_ == DragMode::Content || drag_ == DragMode::Bar) gestureConsumed_ = true;
		drag_ = DragMode::None;
		return;
	}
	if (!in.cursorValid || numItems_ <= 0 || maxScroll <= 0) return;

	if (in.pressed) {
		pressX_ = in.cursorX;
		pressY_ = in.cursorY;
		if (barRect().contains(in.cursorX, in.cursorY)) {
			// A press on the bar grabs it immediately, with no dead box.
			drag_ = DragMode::Bar;
			barDragTo(in.cursorY, maxScroll);
		} else {
			drag_ = DragMode::Pending;
		}
		return;
	}

	switch (drag_) {
	case DragMode::Bar:
		barDragTo(in.cursorY, maxScroll);
		break;
	case DragMode::Content:
		// UpdateContent: field_0x44_ = field_0x5c_ + (field_0x58_ - y), then the
		// widget's own clamp to [0, C - V] (src/Button.cpp:456-478). Pulling the
		// pointer up scrolls the content up, 1:1 in pixels.
		dragScrollPx_ = std::clamp(dragLatchScrollPx_ + (dragLatchY_ - in.cursorY),
			0, maxScroll);
		break;
	case DragMode::Pending:
		// handleUserMoved gates the CONTENT drag on drawHelpText (:4853) but not
		// the bar drag: the bar branch (:4905-4913) sits before that test, and so
		// does the press that latches it (:4691-4702). Both halves are kept.
		if (helpIndex_ >= 0) break;
		if (std::abs(in.cursorX - pressX_) <= kDragDeadBoxPx &&
		    std::abs(in.cursorY - pressY_) <= kDragDeadBoxPx) {
			break;
		}
		if (!listRect().contains(in.cursorX, in.cursorY)) break;
		// SetContentTouchOffset latches the CURRENT point (:4896,
		// src/Button.cpp:443-452), so the content does not jump by the dead-box
		// distance when the drag starts.
		drag_ = DragMode::Content;
		dragLatchY_ = in.cursorY;
		dragLatchScrollPx_ = scrollPixels();
		dragScrollPx_ = dragLatchScrollPx_;
		hasDragScroll_ = true;
		break;
	case DragMode::None:
		break;
	}
}

// moveDir (:401-455), verbatim minus the type == 9 (vending) sub-cases.
void MenuSession::moveDir(int n) {
	if (numItems_ <= 0) return;

	// Every key move ends with the [GEC] block that rewrites the widget's pixel
	// offset from the item model (:463-546), so a drag offset does not survive
	// one: the key model takes the scroll back.
	hasDragScroll_ = false;

	if (type_ == kMenuTypeHelp || type_ == kMenuTypeNotebook) {
		if (n < 0 && scrollIndex_ > 0) {
			scrollIndex_ += n;
		} else if (n > 0 && scrollIndex_ < maxScrollIndex()) {
			// DELIBERATE DEVIATION: the verbatim bound here is
			// `scrollIndex < numItems - maxItems` (src/MenuSystem.cpp:401-455).
			// See maxScrollIndex() for why the item-count bound is replaced by
			// the pixel clamp: with the rewrite's mapping it lets Down walk past
			// the last offset the clamp allows, so the page stops moving and the
			// presses have to be undone one by one before it moves again.
			// Only this cursor-less branch (types 5/7) changes; the selection
			// path below keeps the legacy window pull unchanged.
			scrollIndex_ += n;
		}
		selectedIndex_ = scrollIndex_;
		return;
	}

	// The legacy skip loops have no bound: a screen with no selectable row makes
	// them index out of the item array forever. `guard` is a rewrite safety net,
	// nothing else: with at least one selectable row it never fires.
	int guard = numItems_ + 1;
	bool bail = false;
	do {
		selectedIndex_ += n;
		if (selectedIndex_ >= numItems_ || selectedIndex_ < 0) {
			if (selectedIndex_ < 0) {
				selectedIndex_ = numItems_ - 1;
			} else {
				selectedIndex_ = 0;
				if (type_ == kMenuTypeVending) scrollIndex_ = 0;
			}
			while (!selectable(selectedIndex_)) {
				if (--guard < 0) { bail = true; break; }
				selectedIndex_ += n;
				if (selectedIndex_ < 0 || selectedIndex_ >= numItems_) { bail = true; break; }
			}
			break;
		}
		if (--guard < 0) { bail = true; break; }
	} while (!selectable(selectedIndex_));

	if (bail) {
		static bool logged = false;
		if (!logged) {
			logged = true;
			std::fprintf(stderr, "[menu] no selectable row on menu %d — selection clamped\n",
				menu_);
		}
		selectedIndex_ = std::clamp(selectedIndex_, 0, numItems_ - 1);
	}

	// Window pull (:441-454). The legacy `maxItems != 0` guard is always true
	// here: maxItems is the [GEC] constant 4 (:1231).
	// Type 9 keeps a 3-row look-ahead (:437-439, :450-452).
	if (n < 0) {
		if (selectedIndex_ - kMaxItems + 1 > scrollIndex_) {
			scrollIndex_ = selectedIndex_ - kMaxItems + 1;
		} else if (selectedIndex_ < scrollIndex_) {
			scrollIndex_ = selectedIndex_;
		} else if (type_ == kMenuTypeVending && selectedIndex_ - 3 < scrollIndex_ &&
		           scrollIndex_ > 0) {
			--scrollIndex_;
		}
	} else {
		if (selectedIndex_ > scrollIndex_ + kMaxItems - 1) {
			scrollIndex_ = selectedIndex_ - kMaxItems + 1;
		} else if (scrollIndex_ > selectedIndex_) {
			scrollIndex_ = selectedIndex_;
		} else if (type_ == kMenuTypeVending &&
		           selectedIndex_ + 3 > scrollIndex_ + kMaxItems - 1 &&
		           scrollIndex_ + kMaxItems < numItems_) {
			++scrollIndex_;
		}
	}
}

void MenuSession::scrollPageUp() {                       // (:349-363)
	const int before = selectedIndex_;
	for (int n = 0; n < kMaxItems && selectedIndex_ != 0; ++n) {
		moveDir(-1);
		if (selectedIndex_ >= before) {
			moveDir(1);
			break;
		}
	}
}

void MenuSession::scrollPageDown() {                     // (:332-346)
	const int before = selectedIndex_;
	for (int n = 0; n < kMaxItems && selectedIndex_ != numItems_ - 1; ++n) {
		moveDir(1);
		if (selectedIndex_ < before) {
			moveDir(-1);
			break;
		}
	}
}

// handleMenuEvents (:2826-2969), reduced to the actions the rewrite queues.
// ACTION_AUTOMAP selects on LEVEL_STATS (:2903-2904); its other targets
// (SHOWDETAILS / MORE_GAMES) are not in the rewrite, so elsewhere it does
// nothing. ACTION_PASSTURN is explicitly ignored (:2891).
void MenuSession::handleAction(Action a) {
	// The torn page swallows everything (:2840-2848): only ACTION_MENU,
	// ACTION_FIRE and ACTION_MENU_ITEM_INFO close it, and every other action
	// returns without reaching the list. ACTION_BACK is deliberately NOT in that
	// list, so Action::BackKey stays swallowed here too.
	if (helpIndex_ >= 0) {
		if (a == Action::Menu || a == Action::Use || a == Action::MenuInfo ||
		    a == Action::MenuInfoClose) {
			closeHelp();
		}
		return;
	}
	// On the vending confirm UP/DOWN change the quantity, never past what the
	// credits pay for, and rebuild the screen (:2859-2875).
	if (menu_ == kMenuVendingConfirm && env_.vending != nullptr && env_.player != nullptr &&
	    (a == Action::Forward || a == Action::Back)) {
		VendingMachine& vm = *env_.vending;
		if (a == Action::Forward) {
			if (env_.player->inventory[Enums::INV_ONE_UAC_CREDIT] >=
			    vm.currentItemPrice * (vm.currentItemQuantity + 1)) {
				++vm.currentItemQuantity;
				setMenu(menu_);
			}
		} else if (vm.currentItemQuantity > 1) {
			--vm.currentItemQuantity;
			setMenu(menu_);
		}
		return;
	}
	switch (a) {
	case Action::Forward:    moveDir(-1); break;          // ACTION_UP -> scrollUp (:322-330)
	case Action::Back:       moveDir(1); break;           // ACTION_DOWN -> scrollDown
	case Action::TurnLeft:   scrollPageUp(); break;       // ACTION_LEFT
	case Action::TurnRight:  scrollPageDown(); break;     // ACTION_RIGHT
	case Action::Use:        select(selectedIndex_); break; // ACTION_FIRE
	case Action::Automap:                                 // ACTION_AUTOMAP (:2903-2904)
		if (menu_ == kMenuLevelStats) select(selectedIndex_);
		break;
	case Action::BackKey:                                 // ACTION_BACK (:2943-2944)
		if (menu_ == kMenuLevelStats) {
			select(selectedIndex_);
			break;
		}
		if (menu_ == kMenuMain) {                         // (:2939-2942), FE-D10
			if (env_.quit) env_.quit();
			break;
		}
		[[fallthrough]];
	case Action::Menu:                                    // ACTION_MENU
		// Menu 83 returns to the vending terminal instead (:2927-2928, :2947-2948).
		if (menu_ == kMenuVending && env_.vending != nullptr) env_.vending->returnFromBuying();
		else back();
		break;
	case Action::MenuResume:                              // right soft key (:4787-4789)
		// No Resume key below MENU_INGAME (:4498-4583), nor on menu 50 after a failed
		// Continue: there is no game to resume (deviation FE-D1).
		if (isMainMenu(menu_) || (env_.game != nullptr && env_.game->loadMapID == 0)) break;
		// Deviation (ADR 0033): legacy returnToGame() leaves the EV_MINIGAME
		// thread parked forever on the vending menus; endGame(2) resumes it.
		if (isVendingMenu(menu_) && env_.vending != nullptr) env_.vending->endGame(2);
		else returnToGame();
		break;
	case Action::MenuInfo:   showHelp(infoIndex_); break; // info button (:4802-4805)
	default: break;
	}
}

// NEW FACT 6: the legacy touch release assigns selectedIndex = button->
// selectedIndex and then calls select() (:4787-4792). Here the latch and the
// queued Action::Use are split so mouse and keyboard share one code path.
void MenuSession::setSelectedIndex(int i) {
	if (i < 0 || i >= numItems_) return;
	selectedIndex_ = i;
}

void MenuSession::setInfoIndex(int i) {
	if (i < 0 || i >= numItems_) return;
	infoIndex_ = i;
}

// The touch path of the popup (:4802-4805). Note what it does NOT do: unlike
// the key path (:2849-2856) it never tests helpField against EMPTY_TEXT, so an
// info button on a row without help text opens an empty page. Kept verbatim —
// every row of the whitelisted screens ships a help id, and inventing a refusal
// here would be a rewrite of the original's own behaviour.
void MenuSession::showHelp(int i) {
	if (i < 0 || i >= numItems_) return;
	helpIndex_ = i;
	helpPopupText_.setLength(0);
	const int id = items_[i].def.helpId;
	if (env_.loc != nullptr) {
		// composeTextField(helpField, text) (src/MenuItem.cpp:31). The rewrite has
		// no text-argument table, so a %NN argument would survive into the page —
		// none of the in-game help strings carries one.
		helpPopupText_.append(env_.loc->get(env_.loc->typeOf(id), env_.loc->indexOf(id)));
	}
	helpPopupText_.wrapText(kHelpPopupChars, kHelpPopupLines, '\n');
	if (helpPopupText_.length() == 0) {
		std::fprintf(stderr, "[menu] row %d has no help text (id %d) — empty page\n", i, id);
	}
}

void MenuSession::closeHelp() {                          // (:2844-2845, :4811-4812)
	helpIndex_ = -1;
	infoIndex_ = -1;
}

void MenuSession::select(int i) {
	if (i < 0 || i >= numItems_) return;   // legacy reads items[i] unguarded (:2978)
	if ((items_[i].def.flags & kItemDisabled) != 0) return;          // (:2978-2981)
	if (items_[i].disabledByRewrite) {
		// The row looks normal, so the refusal has to be audible somewhere. A
		// Hud center message is NOT used: Hud::drawMessages runs in the
		// gameplay view only and the menu paints an opaque background over it
		// (new_src/core/GameContext.cpp:794), and Hud::update does not tick in
		// Menu — the line would surface seconds later, after Resume, attached
		// to nothing. The log is the honest signal until a menu-local message
		// slot exists.
		std::fprintf(stderr, "[menu] row %d refused: %s\n", i,
			items_[i].disabledReason != nullptr ? items_[i].disabledReason : "?");
		return;
	}
	if (menu_ == kMenuVendingConfirm) std::fprintf(stderr, "[sound] 1125\n");   // (:2983-2985)
	if (menu_ == kMenuLevelStats) {                                  // (:2996-3000); endingGame not ported
		if (env_.continueLevelStats) env_.continueLevelStats();
		return;
	}
	if (type_ == kMenuTypeHelp || type_ == kMenuTypeNotebook) {       // (:3004-3011)
		back();
		return;
	}
	switch (items_[i].def.action) {
	case kActionNone:
		break;
	case kActionGoto:
		if (items_[i].def.param == kMenuMainExit) {
			if (env_.quit) env_.quit();               // FE-D10: no "Exit Game?" confirm
			break;
		}
		gotoMenu(items_[i].def.param);
		break;
	case kActionBack:
		back();
		break;
	case kActionUseItemWeapon:
		// (:3136-3143). The legacy saveIndexes(1) caches this screen's cursor for
		// the next visit; the rewrite's nav stack replaces that mechanism. `i > 0`
		// is the original's own guard and never bites here — the weapon rows start
		// at index 2, after the two menus.bin rows.
		if (i > 0 && env_.player != nullptr && env_.defs != nullptr) {
			env_.player->selectWeapon(items_[i].def.param,
				env_.defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON,
					items_[i].def.param));
			returnToGame();
		}
		break;
	case kActionRestartLevel:
		// (:3105-3111) loadState(2) = the level-entry BRIEFPLAYER + SB_<map>. The
		// "level restarted" message legacy adds outside MENU_INGAME_DEAD is not
		// ported: loadMedia zeroes msgCount before it can show
		// (src/LoadingManager.cpp:659).
		if (env_.restartLevel) env_.restartLevel();
		break;
	case kActionLoad:                                             // (:3035-3037)
		// A missing or damaged save opens the shipped "No Saved Game" screen
		// (deviation SL-D2; legacy Error()s).
		if (!env_.loadGame || !env_.loadGame()) gotoMenu(kMenuLoadNoSave);
		break;
	case kActionSave:                                             // (:3039-3049)
		if (env_.saveGame) env_.saveGame(3 | (items_[i].def.param == 1 ? 0x100 : 0x80));
		break;
	case kActionSaveQuit:                                         // (:3113-3115)
		if (env_.saveGame) env_.saveGame(67);
		break;
	case kActionSaveExit:                                         // (:3199-3201)
		if (env_.saveGame) env_.saveGame(7);
		break;
	case kActionBackToMain:                                       // (:3051-3053)
		if (env_.backToMain) env_.backToMain();
		break;
	case kActionNewGame:                                          // (:3072-3080)
		gotoMenu(env_.hasSavedState && env_.hasSavedState() ? kMenuMainConfirmNew : kMenuMainDifficulty);
		break;
	case kActionExit:                                             // (:3082-3084)
		if (env_.quit) env_.quit();
		break;
	case kActionDifficulty:                                       // (:3095-3099)
		if (env_.game != nullptr) env_.game->setDifficulty(items_[i].def.param);   // 1 / 2 / 4
		if (env_.startGame) env_.startGame(true);
		break;
	case kActionContinue:                                         // (:3180-3182)
		if (!env_.startGame || !env_.startGame(false)) gotoMenu(kMenuLoadNoSave);  // deviation FE-D1
		break;
	case kActionConfirmUse:                                       // (:3188-3197)
		// saveIndexes(2|4) is replaced by the nav stack (same note as kActionUseItemWeapon).
		menuParam_ = items_[i].def.param;
		gotoMenu(kMenuItemsConfirm);
		break;
	case kActionUseItemSyring:
	case kActionUseItemOther: {                                   // (:3149-3179), identical bodies
		if (env_.player == nullptr || env_.game == nullptr || env_.host == nullptr) break;
		Game& game = *env_.game;
		const UseResult r = env_.player->useItem(menuParam_);
		if (r == UseResult::Used) game.advanceTurn();             // useItem's own tail (src/Player.cpp:956-958)
		if (game.lerps.animatingEffects() != 0) { returnToGame(); break; }   // :3152-3155
		game.monsters.snapMonsters(true);                         // :3156
		game.lerps.snap(-1);                                      // :3157
		// :3158-3160 `canvas->state != ST_MENU`. Two rewrite spellings of it:
		// a started monster attack (legacy setState(ST_COMBAT), src/Combat.cpp:165 ==
		// combat.active here, ADR 0028) and anything the PER_TURN script requested.
		if (game.combat.active) { returnToGame(); break; }
		if (env_.host->state() != StateId::Menu) break;
		if (r != UseResult::Refused) { back(); break; }           // :3161-3164, to the rebuilt list
		if (menuParam_ == Enums::INV_OTHER_HOLY_WATER) gotoMenu(kMenuItemsHolyWaterMax);
		else if (menuParam_ >= 16 && menuParam_ < 18) gotoMenu(kMenuItemsHealthMsg);
		else if (menuParam_ >= 11 && menuParam_ < 13) gotoMenu(kMenuItemsArmorMsg);
		else gotoMenu(kMenuItemsSyringeMsg);
		break;
	}
	case kActionConfirmBuy:                                       // (:3223-3232)
		// MENU_VENDING_MACHINE_LAST (88, setMenu instead) is unreachable.
		if (env_.vending == nullptr) break;
		menuParam_ = items_[i].def.param;
		env_.vending->currentItemQuantity = 1;
		gotoMenu(kMenuVendingConfirm);
		break;
	case kActionBuyDrink:                                         // (:3234-3241)
		if (env_.vending == nullptr) break;
		if (env_.vending->buyDrink(menuParam_)) back();
		else setMenu(kMenuVendingCantBuy);
		break;
	case kActionBuySnack: {                                       // (:3243-3254)
		VendingMachine* vm = env_.vending;
		Player* p = env_.player;
		if (vm == nullptr || p == nullptr) break;
		if (p->inventory[Enums::INV_ONE_UAC_CREDIT] < vm->currentItemPrice * vm->currentItemQuantity) {
			setMenu(kMenuVendingCantBuy);
		} else {
			for (int k = 0; k < vm->currentItemQuantity; ++k) {
				p->inventory[Enums::INV_ONE_UAC_CREDIT] -= (int16_t)vm->getSnackPrice();
				p->give(0, Enums::INV_HEALTH_MIN, 1, false);              // the snack, slot 16
			}
			back();
		}
		break;
	}
	case kActionBackTwo: {                                        // (:3203-3207)
		int idx = 0, scroll = 0;
		if (stackCount_ != 0) popMenu(idx, scroll);               // discard the confirm's entry (legacy pops unguarded)
		back();
		break;
	}
	case kActionReturnToPlayer:                                   // (:3256-3259)
		if (env_.returnToPlayer) env_.returnToPlayer();
		returnToGame();
		break;
	case kActionChangeState:                 // ACTION_CHANGESTATE (src/MenuSystem.cpp:3086-3092)
		if (env_.host == nullptr) break;
		if (items_[i].def.param == 6) {
			env_.host->requestState(StateId::Automap);
		} else if (items_[i].def.param == 15) {
			if (env_.game != nullptr && env_.game->loadMapID != 0) std::fprintf(stderr, "[sound] 1071\n");   // in-game only
			env_.host->requestState(StateId::Credits);
		}
		break;
	case kActionMinigame:                    // (src/MenuSystem.cpp:3208-3221)
		if (items_[i].def.param == 0 || items_[i].def.param == 2 || items_[i].def.param == 4) {
			std::fprintf(stderr, "[sound] stop\n");   // soundStop()
			if (env_.playMiniGame) env_.playMiniGame(items_[i].def.param);
		}
		break;
	default:
		std::fprintf(stderr, "[menu] action %d (param %d) has no rewrite path\n",
			items_[i].def.action, items_[i].def.param);
		break;
	}
}

// gotoMenu (:2818-2824): push the CURRENT screen, then switch. The original
// pushes two extra scroll-pixel fields ([GEC] widget state); the rewrite derives
// the pixels from scrollIndex, so the pair (selectedIndex, scrollIndex) is the
// whole restored state.
void MenuSession::gotoMenu(int menuId) {
	const int fromMenu = menu_;
	const int fromIndex = selectedIndex_;
	const int fromScroll = scrollIndex_;
	const int fromDepth = stackCount_;
	if (menuId != menu_) {
		pushMenu(menu_, selectedIndex_, scrollIndex_);
	}
	setMenu(menuId);

	// Safety net with no counterpart in the original: buildViewModel returns
	// false for a screen with no rows, which would make the whole menu vanish
	// for as long as it is up while ST_MENU still swallows the input. No id the
	// in-game tree can reach produces one (every code-built body appends at
	// least four rows, every menus.bin row of the tree is non-empty), so this
	// only fires if the data or a future body changes. MENU_COMIC_BOOK is the
	// one intended 0-row screen: GameContext hands it to ComicBookSession.
	if (numItems_ == 0 && menuId != kMenuComicBook) {
		std::fprintf(stderr, "[menu] menu %d produced 0 rows — staying on menu %d\n",
			menuId, fromMenu);
		stackCount_ = fromDepth;
		setMenu(fromMenu);
		selectedIndex_ = fromIndex;
		scrollIndex_ = fromScroll;
	}
}

// pushMenu (:4059-4069). The original calls Error("Menu stack is full.") on
// overflow; the rewrite logs and drops the push, which leaves the stack
// consistent (one screen loses its way back instead of the process dying). The
// in-game tree is two levels deep, so this cannot fire.
void MenuSession::pushMenu(int menuId, int selectedIndex, int scrollIndex) {
	if (stackCount_ + 1 >= kMaxStack) {
		std::fprintf(stderr, "[menu] stack full at depth %d — push of menu %d dropped\n",
			stackCount_, menuId);
		return;
	}
	idxStack_[stackCount_] = selectedIndex;
	scrollStack_[stackCount_] = scrollIndex;
	menuStack_[stackCount_++] = menuId;
}

// popMenu (:4071-4081). Callers check stackCount_ first, exactly like back().
int MenuSession::popMenu(int& selectedIndex, int& scrollIndex) {
	selectedIndex = idxStack_[stackCount_ - 1];
	scrollIndex = scrollStack_[stackCount_ - 1];
	return menuStack_[--stackCount_];
}

// back() (:591-607). MENU_INGAME_SNIPER has no rewrite counterpart (spec
// §4.1). MENU_MAIN_MINIGAME / MENU_COMIC_BOOK with an empty stack (after a
// main-menu minigame) go to MENU_MAIN (:608-610). On MENU_INGAME_DEAD the
// stack is empty and 51 is in neither list, so only the sound remains.
void MenuSession::back() {
	if (menu_ != kMenuMain) std::fprintf(stderr, "[sound] 1122\n");   // (:577-581)
	if (stackCount_ != 0) {
		int idx = 0;
		int scroll = 0;
		const int target = popMenu(idx, scroll);
		// The restore lands AFTER setMenu, so it overwrites both the
		// oldMenu_-guarded reset in initMenu (:1227-1230) and the per-screen
		// selectedIndex patches — which is what keeps the cursor where it was.
		setMenu(target);
		selectedIndex_ = idx;
		scrollIndex_ = scroll;
		// Deviation U1: legacy leaves the cursor past the end / on a divider when
		// a used-up row vanished (:599); re-run the initMenu tail. No-op otherwise.
		if (selectedIndex_ >= numItems_) {
			moveDir(-1);
		} else if (!selectable(selectedIndex_)) {
			moveDir(1);
		}
	} else if (menu_ == kMenuInGame || menu_ == kMenuItems || menu_ == kMenuItemsDrinks ||
	           menu_ == kMenuQuestLog) {
		returnToGame();
	} else if (menu_ == kMenuMainMinigame || menu_ == kMenuComicBook) {
		setMenu(kMenuMain);
	}
}

void MenuSession::returnToGame() {                       // (:1209-1220)
	numItems_ = 0;
	// The legacy `app->time = lastTime = upTimeMs` re-stamp has no counterpart:
	// gameTime simply does not advance in Menu (spec §2.4). The
	// ST_INTER_CAMERA branch is unreachable — the menu can only be opened from
	// Playing (spec §15).
	if (env_.host != nullptr) env_.host->requestState(StateId::Playing);
}

// ---- view model (spec §6.1) ----

void MenuSession::composeLabel(int i) {
	MenuRow& row = rowBuf_[i];
	row = MenuRow{};
	const MenuItemDef& d = items_[i].def;
	if ((d.flags & kItemHidden) != 0) {
		// The legacy loop advances by nothing and draws nothing (:911-916).
		row.height = 0;
		return;
	}
	row.height = itemHeight(i);
	row.action = d.action != 0;
	row.centered = (d.flags & kItemAlignCenter) != 0;
	// ONLY the data flag reaches the view. A row the REWRITE cannot honour (no
	// credits state, no arrow controls, no details screen) still looks like a
	// normal row and refuses in select() instead, so `disabledByRewrite` is
	// deliberately NOT ORed in here.
	row.disabled = (d.flags & kItemDisabled) != 0;

	// The "i" button, on action rows of the whitelisted screens only
	// (:5132-5140). ITEM_DISABLED does not remove it: the legacy info block is
	// outside that test.
	row.info = row.action && screenHasInfoButtons(menu_);
	if (!row.action && (menu_ == kMenuMainExit || menu_ == kMenuMainDifficulty ||
	                    menu_ == kMenuMainMinigame)) {
		row.textDy = 8;                                               // (:968-977)
	} else if (menu_ == kMenuMainConfirmNew && i == 3) {
		row.textDy = 7;                                               // (:1053-1057)
	}

	// EMPTY_TEXT without ITEM_DIVIDER draws nothing but keeps its height (:930).
	if (d.labelId == kEmptyTextId && (d.flags & kItemDivider) == 0) return;

	// The value column lives inside that same branch in the legacy paint
	// (:930 opens it, :1010 draws textField2), so a row with neither label nor
	// divider shows no value either.
	if (items_[i].hasValue) row.value = &valueBuf_[i];

	Text& t = labelBuf_[i];
	t.setLength(0);
	if ((d.flags & kItemChecked) != 0) {                             // (:934-938)
		t.append(kCheckGlyph);
		t.append(" ");
	}
	if (items_[i].hasLiteralLabel) {
		// composeTextField resolves ARGUMENT1..N out of the text-arg table
		// (:939); the rewrite's equivalent slot is literalBuf_[i].
		t.append(literalBuf_[i]);
	} else if (env_.loc != nullptr) {
		t.append(env_.loc->get(env_.loc->typeOf(d.labelId), env_.loc->indexOf(d.labelId)));
	}
	if ((d.flags & kItemNoDehyphenate) == 0) t.dehyphenate();        // (:943-944)
	const bool vending = isVendingMenu(menu_);
	const bool mainMenu = isMainMenu(menu_);
	if (row.action && t.getStringWidth() + 10 >
	                  (mainMenu ? kMainItemWidth : vending ? kVendingItemWidthPx : kItemWidthPx)) {   // (:945-951)
		// NOTE: the trigger width is measured with the rewrite's 9 px advance
		// while the original measured 12 px, so this fires later than it did in
		// the original; the truncation length itself stays the literal 23 (18
		// on the vending plates, 12 on the main menu).
		t.setLength(mainMenu ? kMainTruncateChars : vending ? kVendingTruncateChars : kTruncateChars);
		t.append(kEllipsisGlyph);
	}
	if ((d.flags & kItemDivider) != 0) {                             // (:954-957)
		// v78 = menuRect[2] / CHAR_SPACING[app->fontType], computed once before
		// the row loop from the PAINT-narrowed rect (:909).
		buildDivider(t, listRect().w / kCharSpacing);
	}
	row.label = &t;
}

bool MenuSession::buildViewModel(MenuViewModel& m) {
	if (numItems_ <= 0) return false;

	m = MenuViewModel{};
	for (int i = 0; i < numItems_; ++i) composeLabel(i);
	m.rows = rowBuf_;
	m.rowCount = numItems_;
	// Same numbers as the model's own defaults; assigned so that the region the
	// drag hit test uses and the region the view draws are one constant.
	m.rect = listRect();
	// setClipRect(screenRect[0], menuRect[1], screenRect[2], menuRect[3]) (:853).
	m.clip = UiRect{ 0, m.rect.y, 480, m.rect.h };
	m.scrollPx = scrollPixels();
	// A content or bar drag owns the gesture, and so does the release that ends
	// one: the rows take no hits on those frames (:4676-4689, :4855-4870).
	m.rowHits = !gestureConsumed_ &&
		drag_ != DragMode::Content && drag_ != DragMode::Bar;

	// The bar exists only when the list is taller than the view (:2753). The
	// thumb is computed here, not in the primitive: the producer already owns
	// scrollPx, contentPx and viewPx, so splitting the formula across the ui/
	// boundary would duplicate all three (spec §6.4).
	const int contentPx = contentHeight();
	m.showBar = contentPx > viewPx();
	if (m.showBar) {
		m.barRect = barRect();
		// thumbLen L = V*H/C, thumbOffset = scrollPx * (H-L) / (C-V)
		// (SetScrollBox src/Button.cpp:397-405, UpdateContent :479-481).
		m.barThumbLen = barThumbLen();
		m.barThumbOffset = m.scrollPx * (m.barRect.h - m.barThumbLen) / (contentPx - viewPx());
	}
	// No cursor at all on the text-section types and the vending order
	// screens (:1064).
	m.selectedRow = (type_ == kMenuTypeHelp || type_ == kMenuTypeNotebook ||
	                 menu_ == kMenuVendingConfirm || menu_ == kMenuVendingCantBuy)
		? -1 : selectedIndex_;

	// Cursor wobble: OSC_CYCLE[app->time / 100 % 4] (:1065), table 6 == {-1,0,1,0}.
	m.cursorOffset = 0;
	if (env_.tables != nullptr && env_.upTimeMs != nullptr &&
	    env_.tables->oscCycle.size() >= 4) {
		const int phase = (int)((*env_.upTimeMs / 100) % 4);
		m.cursorOffset = env_.tables->oscCycle[phase];
	} else {
		static bool logged = false;
		if (!logged) {
			logged = true;
			std::fprintf(stderr, "[menu] oscCycle unavailable — cursor wobble disabled\n");
		}
	}

	if (isMainMenu(menu_)) {
		const bool leaf = isMainHelpLeaf(menu_);
		m.mainSkin = true;
		// Every main list screen (:1263, :1278, :1284, :1348, :1355); the leaves set false (:1288-1344).
		m.drawLogo = !leaf;
		m.itemWidth = kMainItemWidth;
		m.itemPadding = kMainItemPadding;
		m.rect = leaf ? kMainHelpRect : kMainRect;
		m.clip = leaf ? kMainHelpClip : kMainClip;
		m.scrollPx = scrollPixels();
		m.rowHits = true;
		if (leaf) {
			m.showBar = contentPx > kMainHelpInitRectH;   // isMainMenuScrollBar (:2753, :2773-2776)
			if (m.showBar) {
				m.barRect = kMainHelpDialRect;
				const int maxScroll = contentPx - viewPx();   // A2: the knob follows the index model's clamp
				const int thumb = maxScroll > 0 ? m.scrollPx * (kDialRect.h - kDialThumbLen) / maxScroll : 0;
				const int centre = thumb + (kDialThumbLen >> 1);
				m.dialKnobY = kMainHelpDialRect.y + (centre * 4 / 5 - (kDialKnobH >> 1)) + 16;   // (:5019-5036)
			}
		} else {
			// isMainMenuScrollBar (:2753-2785); drawScrollbar skips 13/14/16/17/18/19 (:869-879).
			m.showBar = (menu_ == kMenuMain || menu_ == kMenuMainHelp) && contentPx > kMainInitRectH;
			if (m.showBar) {
				int raw = 0;
				int maxRaw = 0;
				mainScroll(raw, maxRaw);
				m.barRect = kDialRect;
				const int thumb = maxRaw > 0 ? raw * (kDialRect.h - kDialThumbLen) / maxRaw : 0;   // field_0x48_
				const int centre = thumb + (kDialThumbLen >> 1);
				m.dialKnobY = kDialRect.y + (centre * 4 / 5 - (kDialKnobH >> 1)) + 16;   // drawScrollbar (:5019-5036)
			}
		}
		// Button 11 (:4564-4580): 4, 5-12, 16, 18, 20-22; no button 15 below MENU_INGAME (:4498-4583).
		// G3b relies on kMenuMainMinigame being here.
		if (menu_ == kMenuMainDifficulty || menu_ == kMenuMainHelp || menu_ == kMenuMainMinigame || leaf) {
			softLeftText_.setLength(0);
			if (env_.loc != nullptr) softLeftText_.append(env_.loc->get(kTextIngame2, 80));
			softLeftText_.dehyphenate();
			m.softLeft = &softLeftText_;
		}
		return true;
	}

	const bool vending = isVendingMenu(menu_);
	if (vending) {
		m.vendingSkin = true;
		m.itemWidth = kVendingItemWidthPx;
		m.itemPadding = kVendingPaddingBottom;
		m.showQtyArrows = menu_ == kMenuVendingConfirm;
	}

	// MENU_LEVEL_STATS is below MENU_INGAME: only its own background, no bottom
	// panel, no health/shield line (:734-763) and no menu soft keys
	// (updateTouchButtonState shows none for it, :4498-4627).
	if (menu_ == kMenuLevelStats) {
		m.statsSkin = true;
		m.drawBottomPanel = false;
		return true;
	}

	// Health / shield readout (:742-758); the vending branch has none (:735-737).
	if (env_.player != nullptr && !vending) {
		statusText_.setLength(0);
		statusText_.append(env_.player->getHealth());
		statusText_.append("/");
		statusText_.append(env_.player->getMaxHealth());
		while (statusText_.length() <= 6) statusText_.append(' ');
		statusText_.append("  ");
		statusTailText_.setLength(0);
		statusTailText_.append(env_.player->ce.getStat(Enums::STAT_ARMOR));
		statusTailText_.append("/");
		statusTailText_.append(200);       // literal in the original (:750)
		while (statusTailText_.length() <= 6) statusTailText_.insert(' ', 0);
		statusText_.append(statusTailText_);
		m.statusLine = &statusText_;
	}

	// Soft keys (:5238-5240 left label, :5286-5289 right label). MENU_INGAME_DEAD
	// shows neither: updateTouchButtonState leaves buttons 11 and 15 hidden for
	// it (:4499-4509), so there is no Back and no Resume to tap.
	// Vending: 83 shows only the right key, 84-87 both (:4617-4627), and the
	// right label is the literal "Exit" (:5284).
	if (menu_ != kMenuInGameDead) {
		if (menu_ != kMenuVending) {
			softLeftText_.setLength(0);
			if (env_.loc != nullptr) softLeftText_.append(env_.loc->get(kTextIngame2, 80));
			softLeftText_.dehyphenate();
			m.softLeft = &softLeftText_;
		}
		if (env_.game == nullptr || env_.game->loadMapID != 0) {   // no game to resume (FE-D1)
			softRightText_.setLength(0);
			softRightText_.append(vending ? "Exit" : "Resume");   // ASCII literals in the original
			softRightText_.dehyphenate();
			m.softRight = &softRightText_;
		}
	}

	// The torn page, drawn over everything else while selectedHelpIndex is set
	// (:1135-1173). The text is wrapped once, in showHelp(), because nothing can
	// change it while the modal owns the input.
	if (helpIndex_ >= 0) m.helpPopup = &helpPopupText_;

	return true;
}

} // namespace newcore
