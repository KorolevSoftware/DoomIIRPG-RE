#ifndef NEW_CORE_MENUSESSION_H
#define NEW_CORE_MENUSESSION_H

#include <cstdint>

#include "core/GameStates.h"
#include "io/MenuData.h"
#include "text/Text.h"
#include "ui/MenuModel.h"

namespace newcore {

class Localization;
class Player;
class Tables;

// ALL retained state of the ST_MENU screen (spec 2026-08-28-menu §4), the
// LootSession / DialogSystem precedent: the session owns the current menu id,
// the item copies, the selection and the Text buffers the view model borrows;
// the view is stateless and UiState knows nothing about the menu.
//
// GROUP 3 scope: the root screen (MENU_INGAME, id 29). The navigation stack
// (gotoMenu/pushMenu/popMenu), pixel scrolling, the scrollbar, the code-built
// screens and the info buttons arrive with G4+.
class MenuSession {
public:
	// maxItems: [GEC] hard-set to 4 at src/MenuSystem.cpp:1231; what the J2ME
	// build computed there is UNKNOWN (spec §14 A1). Page size and window size.
	static constexpr int kMaxItems = 4;
	// items[50] in the original (src/MenuSystem.h:139); 64 leaves room for the
	// code-built screens without a reallocation.
	static constexpr int kMaxRows = 64;

	struct Env {
		const MenuData* menus = nullptr;
		const Localization* loc = nullptr;
		const Tables* tables = nullptr;      // oscCycle (cursor wobble)
		Player* player = nullptr;
		StateHost* host = nullptr;
		const int64_t* upTimeMs = nullptr;   // cursor oscillation clock
	};
	void init(const Env& env);

	void begin();                     // ST_MENU entry hook (setMenu(MENU_INGAME))
	void handleAction(Action a);      // port of handleMenuEvents (src/MenuSystem.cpp:2826-2969)
	void setSelectedIndex(int i);     // mouse latch (:4787-4792)

	// Rebuilds the whole screen model into the session's own buffers. False =
	// nothing to draw (no items, i.e. the menu is not up).
	bool buildViewModel(MenuViewModel& m);

private:
	// One runtime row: the MenuItemDef copy the original patches in place, plus
	// the rewrite's own refusal flag (spec §11.2).
	struct Row {
		MenuItemDef def;
		bool disabledByRewrite = false;
		const char* disabledReason = nullptr;
	};

	void setMenu(int menuId);         // (:612-654)
	void initMenu(int menuId);        // (:1227-1573 subset)
	void back();                      // (:591-607)
	void returnToGame();              // (:1209-1220)
	void select(int i);               // (:2971-3050 subset)
	void moveDir(int n);              // (:401-455 verbatim, minus type 9)
	void scrollPageUp();              // (:349-363)
	void scrollPageDown();            // (:332-346)
	int  itemHeight(int i) const;     // getMenuItemHeight (:4947-4984)
	bool selectable(int i) const;     // the EMPTY_TEXT / 0x8001 test (:432,446)
	void disableRow(int i, const char* reason);
	void composeLabel(int i);         // one row's label into labelBuf_[i]

	Env env_;

	int menu_ = 0;            // Menus::MENU_NONE
	int oldMenu_ = 0;
	int type_ = 0;
	Row items_[kMaxRows];
	int numItems_ = 0;
	int selectedIndex_ = 0;
	int scrollIndex_ = 0;

	// Producer-side storage the model points at (spec §1: core/ allocates once,
	// ui/ allocates nothing).
	Text labelBuf_[kMaxRows];
	MenuRow rowBuf_[kMaxRows];
	Text statusText_;
	Text statusTailText_;     // the shield half, built with leading spaces (:751-756)
	Text softLeftText_;
	Text softRightText_;
};

} // namespace newcore

#endif // NEW_CORE_MENUSESSION_H
