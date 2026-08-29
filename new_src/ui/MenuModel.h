#ifndef NEW_UI_MENUMODEL_H
#define NEW_UI_MENUMODEL_H

#include "ui/UiTypes.h"

namespace newcore {

class Text;

// One list row of the in-game menu. Built by MenuSession::buildViewModel() on
// the frame it is drawn (spec 2026-08-28-menu §5); the Text is borrowed and
// valid for THAT frame only.
//
// A hidden item (ITEM_HIDDEN) arrives as `label == nullptr`, `action == false`
// and `height == 0`, which is exactly what the legacy loop does with it: draw
// nothing, advance the cursor by nothing (src/MenuSystem.cpp:911-916).
// An item whose textField is EMPTY_TEXT arrives as `label == nullptr` with its
// real height — drawn as blank, still occupying its row (:930).
struct MenuRow {
	const Text* label = nullptr;
	int  height = 16;      // ROW PITCH: 46+10 for action rows, 16 for label rows
	                       // (src/MenuSystem.cpp:4967-4983); the last row loses
	                       // its padding
	bool action = false;   // legacy `items->action != 0` — drives ALL row geometry
	bool centered = false; // ITEM_ALIGN_CENTER
	bool disabled = false; // ITEM_DISABLED as it comes from the DATA (menus.bin
	                       // flags, or a per-screen patch of the original such as
	                       // items[2].flags = inventory[18] + 4). A row the
	                       // rewrite merely cannot honour is NOT flagged here:
	                       // it is drawn normally and refused on activation.
};

// Per-frame view model of a whole menu screen — the original has one paint()
// for every menu type, so there is one model (spec §5). Deliberately carries no
// menu id, no game-state id and no "which screen is this" flag: everything the
// view branches on is a geometry field or a bool.
struct MenuViewModel {
	// menuRect as paint() narrows it for the in-game tree
	// (src/MenuSystem.cpp:848, 4192).
	UiRect rect{ 70, 10, 340, 241 };
	// setClipRect(screenRect[0], menuRect[1], screenRect[2], menuRect[3])
	// (src/MenuSystem.cpp:852) — the WIDTH comes from screenRect, so the
	// scrollbar and the info buttons sit inside the clip.
	UiRect clip{ 0, 10, 480, 241 };
	int itemWidth  = 296;  // menuItem_width = inGame_menu_option_button.width (:4193)
	int itemHeight = 46;   // menuItem_height: the art/label box, NOT the row pitch (:4173)

	// Every item, in item order: the row index IS the item index, so a row hit
	// needs no mapping. The view walks from row 0 at y = -scrollPx and stops
	// when it leaves `rect`, like the legacy loop (:911).
	const MenuRow* rows = nullptr;
	int rowCount = 0;
	int scrollPx = 0;
	int selectedRow = -1;   // -1 = no cursor at all (types 5 and 7, :1064)
	// false = a touch drag owns the current gesture, so the rows register no hit
	// areas and the release that ends the drag activates nothing. The legacy
	// release handler returns early while a drag latch is set
	// (src/MenuSystem.cpp:4676-4689) and its move handler clears every button
	// highlight on the frame the drag starts (:4855-4870). The drag itself lives
	// entirely in the producer; the view only obeys this flag.
	bool rowHits = true;
	int cursorOffset = 0;   // OSC_CYCLE[time/100%4], resolved by the producer

	// Scrollbar. The producer owns every number here: it already holds scrollPx,
	// the content height and the view height, so the thumb is computed once in
	// MenuSession and Ui::scrollBarMenu only blits the four sheets
	// (fmScrollButton::SetScrollBox src/Button.cpp:380-406 + UpdateContent
	// :456-482). showBar == false hides the bar entirely (:2753).
	bool showBar = false;
	UiRect barRect{ 430, 18, 50, 220 };  // (430, menuRect[1] + ((menuRect[3]-220)>>1),
	                                     // 50, imgGameMenuScrollBar->height) with the
	                                     // initMenu-time rect (:2757-2762, :2787)
	int barThumbLen = 0;                 // px, viewPx * barRect.h / contentPx
	int barThumbOffset = 0;              // px from barRect.y

	// Chrome
	bool drawBackground = true;        // gameMenu_Background, opaque 480x320 (:739)
	bool drawBottomPanel = true;       // gameMenu_Panel_bottom (:734)
	const Text* statusLine = nullptr;  // health/shield readout (:742-758); null = skip
	const Text* softLeft = nullptr;    // loc(3,80) "Back"
	const Text* softRight = nullptr;   // ASCII literal "Resume"
};

} // namespace newcore

#endif // NEW_UI_MENUMODEL_H
