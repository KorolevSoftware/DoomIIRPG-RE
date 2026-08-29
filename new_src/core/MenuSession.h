#ifndef NEW_CORE_MENUSESSION_H
#define NEW_CORE_MENUSESSION_H

#include <cstdint>

#include "core/GameStates.h"
#include "io/MenuData.h"
#include "text/Text.h"
#include "ui/MenuModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Localization;
class Player;
class Tables;

// ALL retained state of the ST_MENU screen (spec 2026-08-28-menu §4), the
// LootSession / DialogSystem precedent: the session owns the current menu id,
// the item copies, the selection and the Text buffers the view model borrows;
// the view is stateless and UiState knows nothing about the menu.
//
// GROUP 3+4+5 scope: the root screen (MENU_INGAME, id 29), pixel scrolling, the
// scrollbar, the navigation stack and every sub-screen that has a menus.bin row.
// The code-built screens (PDA 46, the confirm screens 49/52/53, the help leaves)
// and the info buttons arrive with G6-G8.
class MenuSession {
public:
	// maxItems: [GEC] hard-set to 4 at src/MenuSystem.cpp:1231; what the J2ME
	// build computed there is UNKNOWN (spec §14 A1). Page size and window size.
	static constexpr int kMaxItems = 4;
	// items[50] in the original (src/MenuSystem.h:139); 64 leaves room for the
	// code-built screens without a reallocation.
	static constexpr int kMaxRows = 64;
	// Stack depth; the original errors out on overflow (src/MenuSystem.cpp:4059-4081).
	static constexpr int kMaxStack = 10;

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
	// Touch drag scrolling, from the pointer state of THIS frame. Must run
	// before buildViewModel(): it decides both the frame's scroll offset and
	// whether the rows take hits at all (handleUserMoved, :4869-4913).
	void updateDrag(const UiInput& in);

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
		// The rewrite's textField2: true when fillValues() produced a value
		// string for this row (:1575-1594 assigns ARGUMENT1..17 there).
		bool hasValue = false;
	};

	void setMenu(int menuId);         // (:612-654)
	void initMenu(int menuId);        // (:1227-1573 subset)
	void gotoMenu(int menuId);        // (:2818-2824)
	void pushMenu(int menuId, int selectedIndex, int scrollIndex);  // (:4059-4069)
	int  popMenu(int& selectedIndex, int& scrollIndex);             // (:4071-4081)
	void clearStack() { stackCount_ = 0; }                          // (:4055-4057)
	void fillValues();                // fillStatus subset (:3928-3979)
	void back();                      // (:591-607)
	void returnToGame();              // (:1209-1220)
	void select(int i);               // (:2971-3050 subset)
	void moveDir(int n);              // (:401-455 verbatim, minus type 9)
	void scrollPageUp();              // (:349-363)
	void scrollPageDown();            // (:332-346)
	UiRect listRect() const;          // setMenuSettings + paint's narrowing (:848)
	UiRect barRect() const;           // (:2757-2762)
	int  itemHeight(int i) const;     // getMenuItemHeight (:4947-4984)
	int  contentHeight() const;       // sum over the non-hidden rows (:2742-2751)
	int  scrollPixels() const;        // scrollIndex_ -> pixels (spec §6.3, A2)
	int  barThumbLen() const;         // L = V*H/C (SetScrollBox, src/Button.cpp:397-405)
	void barDragTo(int cursorY, int maxScroll);  // fmScrollButton::Update (:497-534)
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

	// The navigation stack (:4045-4081). The original keeps five parallel
	// stacks; the two scroll-pixel ones (scrollY1Stack / scrollY2Stack) are
	// [GEC] widget state the rewrite derives from scrollIndex, so three remain.
	int menuStack_[kMaxStack] = { 0 };
	int idxStack_[kMaxStack] = { 0 };
	int scrollStack_[kMaxStack] = { 0 };
	int stackCount_ = 0;

	// Touch drag state, the rewrite's copy of the two fmScrollButton latches:
	// field_0x38_ (content drag) and field_0x14_ (bar drag), plus the pending
	// phase the [GEC] dead box creates (src/MenuSystem.cpp:4842-4847).
	enum class DragMode { None, Pending, Content, Bar };
	DragMode drag_ = DragMode::None;
	int  pressX_ = 0;               // press origin, for the dead box only
	int  pressY_ = 0;
	int  dragLatchY_ = 0;           // field_0x58_ (SetContentTouchOffset)
	int  dragLatchScrollPx_ = 0;    // field_0x5c_
	int  dragScrollPx_ = 0;         // field_0x44_ while a drag owns the scroll
	bool hasDragScroll_ = false;    // dragScrollPx_ overrides the index model
	bool gestureConsumed_ = false;  // this frame's release ended a drag

	// Producer-side storage the model points at (spec §1: core/ allocates once,
	// ui/ allocates nothing).
	Text labelBuf_[kMaxRows];
	Text valueBuf_[kMaxRows];     // the textField2 column (:1038-1044)
	MenuRow rowBuf_[kMaxRows];
	Text statusText_;
	Text statusTailText_;     // the shield half, built with leading spaces (:751-756)
	Text softLeftText_;
	Text softRightText_;
};

} // namespace newcore

#endif // NEW_CORE_MENUSESSION_H
