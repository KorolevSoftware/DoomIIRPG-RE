#ifndef NEW_UI_UITYPES_H
#define NEW_UI_UITYPES_H

namespace newcore {

// Widget identity. Explicit ids (ADR 0012 point 3): greppable, collision-free
// and they give UiState's scroll table a fixed size.
enum class UiId : int {
	None = 0,
	// HUD bottom bar (rects: docs/original-code/ui.md §1)
	HudSwitchLeft, HudSwitchRight, HudWeapon, HudPortrait, HudShield,
	HudHealth, HudKeys, HudSoftLeft, HudSoftCenter, HudSoftRight,
	// loot list
	LootBody, LootScroll,
	// dialog box
	DialogBody, DialogPageUp, DialogPageDown, DialogOk,
	// menu (geometry TBD, docs/original-code/ui.md — spec §7)
	MenuList, MenuSoftLeft, MenuSoftRight,
	Count
};

// What the UI reports. Every value must be translatable into the EXISTING
// Action queue (new_src/core/GameStates.h:26-27) — spec §4.2.
// Deliberately no NextWeapon/PrevWeapon: the switch buttons' semantics are not
// established (docs/original-code/ui.md §1 documents only their rects and the
// highlight swap), so they report None for now (Deviation D3).
enum class UiAction : int {
	None = 0,
	Menu, Automap, Back, PassTurn, Activate,
	ScrollUp, ScrollDown, ScrollHome, ScrollEnd,
	ListRow,          // index carries the row
};

// One navigation edge per frame. Moves a selected index owned by the screen's
// session; there is no focus graph (ADR 0012 point 7).
enum class Nav : int { None, Up, Down, Left, Right, Activate, Cancel };

struct UiInput {
	int  cursorX = -1, cursorY = -1;   // canvas coords; <0 = no cursor
	bool cursorValid = false;          // false inside the letterbox bars
	bool pressed  = false;             // press edge THIS frame
	bool released = false;             // release edge THIS frame
	bool down     = false;             // level, for the *_Active highlight
	Nav  nav      = Nav::None;         // one nav edge per frame
	int  wheel    = 0;                 // +up / -down notches
};

struct UiRect {
	int x = 0, y = 0, w = 0, h = 0;

	bool contains(int px, int py) const {
		return px >= x && py >= y && px < x + w && py < y + h;
	}

	// Empty when the rects do not overlap (w or h <= 0).
	UiRect intersect(const UiRect& o) const {
		const int x0 = x > o.x ? x : o.x;
		const int y0 = y > o.y ? y : o.y;
		const int x1 = (x + w) < (o.x + o.w) ? (x + w) : (o.x + o.w);
		const int y1 = (y + h) < (o.y + o.h) ? (y + h) : (o.y + o.h);
		return UiRect{ x0, y0, x1 - x0, y1 - y0 };
	}
};

struct UiResult {
	UiAction action = UiAction::None;
	int index = -1;
};

} // namespace newcore

#endif // NEW_UI_UITYPES_H
