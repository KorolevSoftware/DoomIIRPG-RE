#include "core/MenuSession.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "domain/game/Enums.h"
#include "domain/game/Player.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Tables.h"

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

constexpr char kCheckGlyph = '\x87';       // (:934-938)
constexpr char kEllipsisGlyph = '\x85';    // (:948-951)
constexpr char kDividerGlyph = '\x80';     // buildDivider (:267-281)
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
// MenuStrings::YES_LABEL / NO_LABEL (src/MenuStrings.h:246-247), appended by
// SetYESNO (:3651-3652).
constexpr int kStrYesLabel = 197;
constexpr int kStrNoLabel = 198;

// SetYESNO's literal flag values (:3640-3652): 9 for the message and blank rows
// (they are label rows, 16 px, centred and unselectable), 8 for Yes/No (action
// rows, so 296x32 plates with a centred label).
constexpr int kYesNoMessageFlags = kItemNoSelect | kItemAlignCenter;
constexpr int kYesNoButtonFlags = kItemAlignCenter;

// CHAR_SPACING[0] = 11 (src/App.h:41). The divider cell count is
// menuRect[2] / CHAR_SPACING[0] (:909), i.e. a CHARACTER budget computed with
// the original's 11 px advance — never with the rewrite's 9 px one (NEW FACT 9).
constexpr int kCharSpacing = 11;

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

// buildDivider (:267-281), verbatim: the cell count is computed BEFORE the
// padding spaces go in, and an empty label becomes three divider glyphs.
void buildDivider(Text& t, int cells) {
	const int cnt = (cells - (t.length() + 2)) / 2;
	if (t.length() > 0) {
		t.insert(' ', 0);
		t.append(' ');
	} else {
		t.append(kDividerGlyph);
		t.append(kDividerGlyph);
		t.append(kDividerGlyph);
	}
	for (int j = 0; j < cnt; ++j) {
		t.insert(kDividerGlyph, 0);
		t.append(kDividerGlyph);
	}
}

// buildFraction (:3840-3851).
void buildFraction(Text& t, int i, int i2) {
	t.setLength(0);
	if (i < 0) t.append('-');
	t.append(i);
	t.append("/");
	if (i2 < 0) t.append('-');
	t.append(i2);
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

// Screens with no menus.bin row that the rewrite builds in code, mirroring their
// initMenu cases (ADR 0013). G7 covers the three confirm screens; QUESTLOG 46 and
// the type-5 HELP leaves are still missing and stay refused until G6.
bool isCodeBuiltScreen(int menuId) {
	return menuId == kMenuLoad || menuId == kMenuRestartLvl || menuId == kMenuSaveQuit;
}

ScreenGeom geometryFor(int menuId) {
	switch (menuId) {
	// One case group in the original (:4330-4349): w = the 296 px option
	// button, x = (480 - w) >> 1 = 92, y = 0, h = 320. Its remaining members
	// (INGAME_CONTROLLER / OPTIONS_SOUND / OPTIONS_INPUT / INGAME_DEAD /
	// ITEMS_CONFIRM / the three ITEMS_*MSG screens) have no MenuId in the
	// rewrite. LOADNOSAVE 50 and SPECIAL_EXIT 58 are deliberately absent: they
	// are NOT in that group and keep the default rect.
	case kMenuOptions:
	case kMenuExit:
	case kMenuLoad:
	case kMenuRestartLvl:
	case kMenuSaveQuit:
	case kMenuControls:
		return { UiRect{ (480 - kItemWidthPx) / 2, kListRect.y, kItemWidthPx, kListRect.h }, 320 };
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
	return geometryFor(menu_).rect;
}

UiRect MenuSession::barRect() const {
	const int initRectH = geometryFor(menu_).initRectH;
	return UiRect{ kBarX, (initRectH - kBarH) >> 1, kBarW, kBarH };
}

// ST_MENU entry hook: setMenu(MENU_INGAME), which clears the navigation stack
// (src/MenuSystem.cpp:619-621). The state change itself is the caller's
// business here — the legacy setMenu pushes ST_MENU onto the canvas (:650-653),
// the rewrite arrives from GameContext::enterState_.
void MenuSession::begin() {
	setMenu(kMenuInGame);
}

void MenuSession::setMenu(int menuId) {
	// clearStack() for MENU_INGAME (:619-621). MENU_MAIN_BEGIN and
	// MENU_INGAME_KICKING, the other two ids of that test, are unreachable here.
	if (menuId == kMenuInGame) clearStack();
	oldMenu_ = menu_;
	menu_ = menuId;
	initMenu(menu_);
}

void MenuSession::initMenu(int menuId) {
	if (env_.menus == nullptr) {
		numItems_ = 0;
		return;
	}
	type_ = env_.menus->type(menuId);
	if (type_ < 0) {
		if (isCodeBuiltScreen(menuId)) {
			// The three confirm cases set `this->type = 6` themselves before they
			// call SetYESNO (:1715, :1729, :1736).
			type_ = kMenuTypeVCenter;
		} else {
			// Absent from menus.bin and not built here either: QUESTLOG 46 and
			// every HELP leaf (ADR 0013) arrive with G6. Reaching one is a bug:
			// initMenu refuses a GOTO to such an id, so no such screen can be
			// entered (see the disable loop below).
			std::fprintf(stderr, "[menu] id %d has no menus.bin row (code-built screen, G6)\n",
				menuId);
			type_ = kMenuTypeList;
		}
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
	} else if (menuId == kMenuLoad) {                  // (:1714-1719)
		scrollIndex_ = 0;
		// The original passes SetYESNO(136, 1, ACTION_LOAD = 3, 0), and its
		// ACTION_LOAD calls canvas->loadState (:3035-3038). The rewrite has no
		// save system, so YES is retargeted to a GOTO of the shipped
		// MENU_INGAME_LOADNOSAVE (50), the "No Saved Game" + Back screen that
		// ships in menus.bin — the designed refusal, not an invented one
		// (spec §11.2 row 3). Nothing else about the screen changes.
		setYesNo(kStrLoadGameQuestion, 1, kActionGoto, kMenuLoadNoSave);
	} else if (menuId == kMenuRestartLvl) {            // (:1728-1733)
		scrollIndex_ = 0;
		setYesNo(kStrRestartLevelQuestion, 1, kActionRestartLevel, 0);
	} else if (menuId == kMenuSaveQuit) {              // (:1735-1740)
		scrollIndex_ = 0;
		// The one caller that spells the NO branch out; 2/0 is also the default.
		setYesNo(kStrSaveQuitQuestion, 1, kActionSaveQuit, 0, kActionBack, 0);
	} else {
		// loadMenuItems(menu, 0, -1) (:2724-2727): the whole span, copied because
		// initMenu patches flags per screen (:1549-1573).
		addParsedRows(menuId, 0, -1);
	}

	// Per-screen patches (:1549-1573). The isFamiliar rebuild and the
	// camera-active variant are not ported: there is no isFamiliar, and the
	// rewrite can only open the menu from Playing (spec §4.2).
	if (menuId == kMenuInGame) {
		selectedIndex_ = 1;
		if (env_.player != nullptr && numItems_ > 2 && env_.player->inventory[18] == 0) {
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
			} else if (items_[i].def.param == kMenuItemsDrinks) {
				// 75 DOES have a menus.bin row (a blank line + the NANO DRINKS
				// divider), so the type() test below would let it through; its
				// list is appended in code like this screen's (:2024-2046).
				disableRow(i, "drinks list is built in code, not ported yet (G8)");
			} else if (env_.menus->type(items_[i].def.param) < 0 &&
			           !isCodeBuiltScreen(items_[i].def.param)) {
				// G7 widened this: an id absent from menus.bin is now refused
				// only while the rewrite has no code-built body for it either.
				// 49/52/53 have one from here on; 46 and the HELP leaves do not.
				disableRow(i, "screen is built in code, not ported yet (G6)");
			}
			break;
		case kActionSave:
		case kActionSaveQuit:
		case kActionSaveExit:
			disableRow(i, "no save system");
			break;
		// ACTION_LOAD is deliberately NOT disabled: the LOAD confirm screen's
		// YES row retargets to the shipped "No Saved Game" screen (id 50)
		// instead, which is G7's business (spec §11.2 row 3).
		case kActionChangeState:
			disableRow(i, "no ST_AUTOMAP / ST_CREDITS state");
			break;
		case kActionRestartLevel:
			disableRow(i, "no map reload");
			break;
		case kActionBackToMain:
			disableRow(i, "no main menu");
			break;
		case kActionConfirmUse:
			// ACTION_CONFIRMUSE hands the slot to MENU_ITEMS_CONFIRM (77), a
			// SetYESNO screen built in code (:2047-2062), and the YES branch runs
			// Player::useItem, which the rewrite does not have either.
			disableRow(i, "use-item confirm screen is built in code, not ported yet (G7)");
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
// composeText(3, strId, text) (:3616) is not ported: none of the three questions
// this screen family uses in the in-game tree carries a %NN argument, so the
// shipped string IS the composed text. A '%' in it would mean an unresolved
// argument, which is worth a line in the log rather than a silent wrong string.
void MenuSession::setYesNo(int strId, int preselect, int yesAction, int yesParam,
	int noAction, int noParam) {
	yesNoText_.setLength(0);
	if (env_.loc != nullptr) yesNoText_.append(env_.loc->get(kTextIngame2, strId));
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

// fillStatus (:3928-3979) reduced to the values the rewrite can actually
// source. The original stores them as text arguments and paint resolves each
// row's textField2, which initMenu assigned as ARGUMENT1..17 (:1575-1594); the
// per-row mapping is the same here, the composed string just goes straight into
// valueBuf_.
//
// MENU_INGAME_PLAYER rows 2..11 (fillStatus' b2 block, :3931-3950) are all
// sourceable. Rows 12..18 (the b3 block, :3968-3977) need player->totalTime,
// totalMoves, totalDeaths and counters[1,2,6,7]; MENU_INGAME_LEVEL (:3951-3967)
// needs the level timer, mapSecretsFound/totalSecrets, monsterStats, moves,
// currentLevelDeaths and levelGrade. None of those exist in the rewrite, so
// those rows keep no value at all rather than a made-up number (spec §15).
void MenuSession::fillValues() {
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
		return kItemHeightPx + kItemPaddingBottom;
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
	const int maxScroll = contentHeight() - kViewPx;
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

// thumbLen L = V*H/C (SetScrollBox, src/Button.cpp:397-405). Shared by the bar
// drag (which needs the free track length) and the view model.
int MenuSession::barThumbLen() const {
	const int contentPx = contentHeight();
	if (contentPx <= 0) return 0;
	return kViewPx * kBarH / contentPx;
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
	const int maxScroll = contentHeight() - kViewPx;

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
		} else if (n > 0 && scrollIndex_ < numItems_ - kMaxItems) {
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
	if (n < 0) {
		if (selectedIndex_ - kMaxItems + 1 > scrollIndex_) {
			scrollIndex_ = selectedIndex_ - kMaxItems + 1;
		} else if (selectedIndex_ < scrollIndex_) {
			scrollIndex_ = selectedIndex_;
		}
	} else {
		if (selectedIndex_ > scrollIndex_ + kMaxItems - 1) {
			scrollIndex_ = selectedIndex_ - kMaxItems + 1;
		} else if (scrollIndex_ > selectedIndex_) {
			scrollIndex_ = selectedIndex_;
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
// ACTION_AUTOMAP reaches only screens the in-game tree does not have
// (LEVEL_STATS / SHOWDETAILS / MORE_GAMES), and ACTION_PASSTURN is explicitly
// ignored (:2891), so both do nothing here.
void MenuSession::handleAction(Action a) {
	switch (a) {
	case Action::Forward:    moveDir(-1); break;          // ACTION_UP -> scrollUp (:322-330)
	case Action::Back:       moveDir(1); break;           // ACTION_DOWN -> scrollDown
	case Action::TurnLeft:   scrollPageUp(); break;       // ACTION_LEFT
	case Action::TurnRight:  scrollPageDown(); break;     // ACTION_RIGHT
	case Action::Use:        select(selectedIndex_); break; // ACTION_FIRE
	case Action::Menu:
	case Action::BackKey:    back(); break;               // ACTION_MENU / ACTION_BACK
	case Action::MenuResume: returnToGame(); break;       // right soft key (:4787-4789)
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
	if (type_ == kMenuTypeHelp || type_ == kMenuTypeNotebook) {       // (:3004-3011)
		back();
		return;
	}
	switch (items_[i].def.action) {
	case kActionNone:
		break;
	case kActionGoto:
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
	// only fires if the data or a future body changes.
	if (numItems_ == 0) {
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

// back() (:591-607). MENU_ITEMS_DRINKS and MENU_INGAME_SNIPER have no rewrite
// counterpart (spec §4.1), and MENU_MAIN_MINIGAME / MENU_COMIC_BOOK are
// main-menu ids that cannot be reached from ST_MENU.
void MenuSession::back() {
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
	} else if (menu_ == kMenuInGame || menu_ == kMenuItems || menu_ == kMenuQuestLog) {
		returnToGame();
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
	// save system, no automap state, no map reload, no main menu) still looks
	// like a normal row — that is what the reference build shows for
	// `Save Game` — and refuses in select() instead, so `disabledByRewrite` is
	// deliberately NOT ORed in here.
	row.disabled = (d.flags & kItemDisabled) != 0;

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
	if (row.action && t.getStringWidth() + 10 > kItemWidthPx) {      // (:945-951)
		// NOTE: the trigger width is measured with the rewrite's 9 px advance
		// while the original measured 12 px, so this fires later than it did in
		// the original; the truncation length itself stays the literal 23.
		t.setLength(kTruncateChars);
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
	m.showBar = contentPx > kViewPx;
	if (m.showBar) {
		m.barRect = barRect();
		// thumbLen L = V*H/C, thumbOffset = scrollPx * (H-L) / (C-V)
		// (SetScrollBox src/Button.cpp:397-405, UpdateContent :479-481).
		m.barThumbLen = barThumbLen();
		m.barThumbOffset = m.scrollPx * (m.barRect.h - m.barThumbLen) / (contentPx - kViewPx);
	}
	// No cursor at all on the text-section types (:1064).
	m.selectedRow = (type_ == kMenuTypeHelp || type_ == kMenuTypeNotebook)
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

	// Health / shield readout (:742-758).
	if (env_.player != nullptr) {
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

	// Soft keys (:5238-5240 left label, :5286-5289 right label).
	softLeftText_.setLength(0);
	if (env_.loc != nullptr) softLeftText_.append(env_.loc->get(kTextIngame2, 80));
	softLeftText_.dehyphenate();
	m.softLeft = &softLeftText_;
	softRightText_.setLength(0);
	softRightText_.append("Resume");      // ASCII literal in the original
	softRightText_.dehyphenate();
	m.softRight = &softRightText_;

	return true;
}

} // namespace newcore
