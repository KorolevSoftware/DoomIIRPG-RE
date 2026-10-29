#ifndef NEW_MINIGAMES_COMMON_MINIGAMEBUTTONS_H
#define NEW_MINIGAMES_COMMON_MINIGAMEBUTTONS_H

#include "ui/UiTypes.h"

namespace newcore {

// One fmButton of a minigame's button container (drawButton rect).
struct MiniGameButton {
	int id = -1;
	UiRect touch;
	bool enabled = true;
};

// fmButtonContainer::GetTouchedButtonID (src/Button.cpp:277-287): the first
// enabled button in list order whose rect contains (x, y) with INCLUSIVE
// right/bottom edges, else -1.
inline int touchedButtonId(const MiniGameButton* b, int n, int x, int y) {
	for (int i = 0; i < n; ++i) {
		const UiRect& r = b[i].touch;
		if (b[i].enabled && x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h) {
			return b[i].id;
		}
	}
	return -1;
}

// HighlightButton(x, y, true) analog: the button a release would hit while
// the pointer is down.
inline int highlightedButtonId(const MiniGameButton* b, int n, const UiInput& in) {
	if (!in.down || !in.cursorValid) return -1;
	return touchedButtonId(b, n, in.cursorX, in.cursorY);
}

// The release edge on the canvas resolves to the button id under it (-1 =
// none); a release in the letterbox bars is dropped.
inline UiResult touchRelease(const MiniGameButton* b, int n, const UiInput& in) {
	if (!in.released || !in.cursorValid) return {};
	return { UiAction::MiniGameTouch, touchedButtonId(b, n, in.cursorX, in.cursorY) };
}

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAMEBUTTONS_H
