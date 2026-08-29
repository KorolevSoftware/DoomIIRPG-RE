#include "core/MenuSession.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Enums.h"
#include "domain/game/Player.h"
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

// items[i].flags & 0x8001 == ITEM_NOSELECT | ITEM_HIDDEN (:432, :446).
constexpr int kUnselectableMask = kItemNoSelect | kItemHidden;

} // namespace

void MenuSession::init(const Env& env) {
	env_ = env;
}

// ST_MENU entry hook: setMenu(MENU_INGAME), which clears the navigation stack
// (src/MenuSystem.cpp:619-621). The state change itself is the caller's
// business here — the legacy setMenu pushes ST_MENU onto the canvas (:650-653),
// the rewrite arrives from GameContext::enterState_.
void MenuSession::begin() {
	setMenu(kMenuInGame);
}

void MenuSession::setMenu(int menuId) {
	// clearStack() for MENU_INGAME (:619-621). The stack itself (pushMenu /
	// popMenu, spec §4.1) arrives with G5; until then there is nothing to clear.
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
		// Absent from menus.bin: the code-built screens (QUESTLOG 46, LOAD 49,
		// RESTARTLVL 52, SAVEQUIT 53, every HELP leaf; ADR 0013) arrive with
		// G5-G7. Until then such a screen has no rows at all.
		std::fprintf(stderr, "[menu] id %d has no menus.bin row (code-built screen, G5+)\n",
			menuId);
		type_ = kMenuTypeList;
	}

	if (menuId != oldMenu_) {                          // (:1227-1230)
		scrollIndex_ = 0;
		selectedIndex_ = 0;
	}

	// loadMenuItems(menu, 0, -1) (:2724-2727): the whole span, copied because
	// initMenu patches flags per screen (:1549-1573).
	int count = 0;
	const MenuItemDef* src = env_.menus->items(menuId, count);
	if (count > kMaxRows) count = kMaxRows;
	for (int i = 0; i < count; ++i) {
		items_[i].def = src[i];
		items_[i].disabledByRewrite = false;
		items_[i].disabledReason = nullptr;
	}
	numItems_ = count;

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
	}

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

// moveDir (:401-455), verbatim minus the type == 9 (vending) sub-cases.
void MenuSession::moveDir(int n) {
	if (numItems_ <= 0) return;

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
		// gotoMenu + the 3-value navigation stack are G5 (spec §12).
		std::fprintf(stderr, "[menu] goto %d not implemented yet (G5)\n",
			items_[i].def.param);
		break;
	case kActionBack:
		back();
		break;
	default:
		std::fprintf(stderr, "[menu] action %d (param %d) has no rewrite path\n",
			items_[i].def.action, items_[i].def.param);
		break;
	}
}

// back() (:591-607). The stack is always empty in G3, so only the root-like
// branch can run; MENU_ITEMS_DRINKS and MENU_INGAME_SNIPER have no rewrite
// counterpart (spec §4.1).
void MenuSession::back() {
	if (menu_ == kMenuInGame || menu_ == kMenuItems || menu_ == kMenuQuestLog) {
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

	Text& t = labelBuf_[i];
	t.setLength(0);
	if ((d.flags & kItemChecked) != 0) {                             // (:934-938)
		t.append(kCheckGlyph);
		t.append(" ");
	}
	if (env_.loc != nullptr) {
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
		static bool logged = false;
		if (!logged) {
			logged = true;
			std::fprintf(stderr, "[menu] ITEM_DIVIDER row: buildDivider not ported yet (G5)\n");
		}
	}
	row.label = &t;
}

bool MenuSession::buildViewModel(MenuViewModel& m) {
	if (numItems_ <= 0) return false;

	m = MenuViewModel{};
	for (int i = 0; i < numItems_; ++i) composeLabel(i);
	m.rows = rowBuf_;
	m.rowCount = numItems_;
	// Pixel scrolling and the scrollbar are G4 (spec §12): the window pull in
	// moveDir already maintains scrollIndex_, nothing reads it yet.
	m.scrollPx = 0;
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
