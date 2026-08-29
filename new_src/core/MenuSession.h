#ifndef NEW_CORE_MENUSESSION_H
#define NEW_CORE_MENUSESSION_H

#include <cstdint>

#include "core/GameStates.h"
#include "io/MenuData.h"
#include "text/Text.h"
#include "ui/MenuModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class EntityDefs;
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
// GROUP 6 adds the two code-built item screens (MENU_ITEMS 72 and
// MENU_ITEMS_WEAPONS 73), whose bodies the original appends in initMenu.
// GROUP 7 adds the three code-built confirm screens (LOAD 49, RESTARTLVL 52,
// SAVEQUIT 53) through setYesNo, the port of SetYESNO.
// GROUP 6 adds the ten type-5 HELP leaves and the PDA shell (QUESTLOG 46).
// GROUP 8 adds the per-row info buttons and the torn-page help popup.
// The remaining code-built screens (the item-use confirm 77, the drinks list 75,
// the ITEM_SHOWDETAILS details screen 71) have no group yet.
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
		const Tables* tables = nullptr;      // oscCycle (cursor wobble), weaponDef
		const EntityDefs* defs = nullptr;    // item/weapon names on the item screens
		Player* player = nullptr;
		StateHost* host = nullptr;
		const int64_t* upTimeMs = nullptr;   // cursor oscillation clock
	};
	void init(const Env& env);

	void begin();                     // ST_MENU entry hook (setMenu(MENU_INGAME))
	void handleAction(Action a);      // port of handleMenuEvents (src/MenuSystem.cpp:2826-2969)
	void setSelectedIndex(int i);     // mouse latch (:4787-4792)
	// Info-button latch (:4802-4805). Separate from the selection on purpose:
	// the legacy touch path writes selectedHelpIndex and leaves selectedIndex
	// alone, so opening a row's help never moves the cursor.
	void setInfoIndex(int i);
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
		// The rewrite's ARGUMENT1..N label: true when the row's text is a
		// runtime string in literalBuf_ rather than a plain menus.bin id
		// (getLastArgString, :4090-4098). Used by the confirm screens.
		bool hasLiteralLabel = false;
	};

	void setMenu(int menuId);         // (:612-654)
	void initMenu(int menuId);        // (:1227-1573 subset)
	// Row producers. addItem is the legacy appender (:3997-4003); addParsedRows
	// is loadMenuItems (:4005-4035) with the menus.bin span it copies from.
	int  addItem(const MenuItemDef& def);
	int  addItem(int labelId, int flags, int action, int param, int helpId);
	void addParsedRows(int menuId, int beg, int count);
	// The item-row idiom of MENU_ITEMS/MENU_ITEMS_WEAPONS: name + description
	// from find(ET_ITEM, subType, defParm), the count in the value column.
	int  addEntityRow(int subType, int defParm, int flags, int action, int param);
	void buildItemsScreen();          // MENU_ITEMS (:1930-1986)
	void buildWeaponsScreen();        // MENU_ITEMS_WEAPONS (:1988-2021)
	// LoadHelpResource(short) (:3662-3702): the type-5 body of a HELP leaf.
	void loadHelpResource(int resource);
	// LoadHelpItems(Text*, int) (:3811-3838): one item per '|'-separated line.
	void loadHelpItems(const Text& text, int extraFlags);
	// One line of a help page, taken verbatim from [beg, end) of `src`.
	int  addHelpRow(int srcLabelId, const Text& src, int beg, int end, int flags);
	// LoadNotebook (:3777-3809), shell only: the map-name row and the divider.
	void loadNotebook();
	// SetYESNO(short, int, int, int[, int, int]) (:3611-3660): the message rows,
	// the blank row, the Yes row and the No row of a type-6 confirm screen.
	void setYesNo(int strId, int preselect, int yesAction, int yesParam,
		int noAction = kActionBack, int noParam = 0);
	// One message line of setYesNo, taken verbatim from [beg, end) of `src`.
	int  addMessageRow(int srcStrId, const Text& src, int beg, int end);
	// drawHelpText / selectedHelpIndex (:159-160, :4802-4813, :2840-2857): the
	// torn-page modal. showHelp wraps the row's helpField into helpPopupText_.
	void showHelp(int i);
	void closeHelp();
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
	int  maxScrollIndex() const;      // last index the pixel clamp still moves (types 5/7)
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
	// LoadHelpResource's side effect on the canvas rect: `menuRect[2] -= 27`
	// before the narrower re-wrap (:3697). setMenuSettings runs at the top of
	// every initMenu (:1242), so the mutation never outlives one screen — this
	// flag is reset there for the same reason.
	bool helpNarrowed_ = false;
	// The text-type-2 index the current help page was built from, so its rows
	// can carry a real (non-EMPTY_TEXT) source string id the way the original's
	// ARGUMENT1..N ids do (:4090-4098).
	int helpTextIndex_ = 0;
	// selectedHelpIndex (:159): the row whose torn page is up, -1 = no popup.
	// It doubles as the legacy drawHelpText flag, which is only ever set
	// together with it (:4803-4804, :2844-2845).
	int helpIndex_ = -1;
	// The row the last info button carried, latched before Action::MenuInfo
	// reaches handleAction (the setSelectedIndex idiom).
	int infoIndex_ = -1;

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
	Text literalBuf_[kMaxRows];   // the rewrite's ARGUMENT1..N slots (:4090-4098)
	Text yesNoText_;              // setYesNo's message before it is split on '\n'
	Text helpText_;               // localization->getLargeBuffer() of LoadHelpResource
	Text helpPopupText_;          // WrapHelpText's buffer (src/MenuItem.cpp:29-34)
	MenuRow rowBuf_[kMaxRows];
	Text statusText_;
	Text statusTailText_;     // the shield half, built with leading spaces (:751-756)
	Text softLeftText_;
	Text softRightText_;
};

} // namespace newcore

#endif // NEW_CORE_MENUSESSION_H
