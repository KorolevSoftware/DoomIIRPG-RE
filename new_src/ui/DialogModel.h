#ifndef NEW_UI_DIALOGMODEL_H
#define NEW_UI_DIALOGMODEL_H

namespace newcore {

class Text;

// Per-frame view model for the styled dialog box (ADR 0012 point 4, spec
// 2026-08-27-ui-layer §7). Built from scratch by
// DialogSystem::buildViewModel() on the frame it is drawn; every pointer is
// borrowed and valid for THAT frame only.
//
// The split: DialogSystem keeps the composition, the wrap-and-retry-narrower
// rule, lines-per-page, paging, chaining and the typewriter clock — so the
// reveal cursor is resolved into per-row character counts here, not recomputed
// by the view. The view owns the box geometry, the colours, the portrait
// inset, the title and the page icons.
struct DialogViewModel {
	// Max lines per page over all styles: 4 (src/DialogSystem.cpp:607-618,
	// ported at new_src/domain/game/DialogSystem.cpp:128-131).
	static constexpr int kMaxViewLines = 4;

	int style = 0;
	int flags = 0;
	int viewLines = 4;

	// The wrapped dialog buffer with its '|' separators kept; rows index into
	// it. Null or empty means "nothing to draw".
	const Text* text = nullptr;
	// Producer-owned scratch buffer, borrowed so the view can recompose a row
	// with the '^2' green colour code (style 9) without allocating: ui/ owns
	// nothing and allocates nothing per frame (ADR 0012 point 2).
	Text* scratch = nullptr;

	struct Row {
		int start = 0;     // offset into `text`
		int visible = 0;   // chars revealed by the typewriter; 0 = not drawn
	};
	// Row i is drawn in slot i of the page, whether or not it is revealed
	// (the legacy loop advances `ty` unconditionally,
	// src/DialogSystem.cpp:333-354).
	Row rows[kMaxViewLines];
	int rowCount = 0;

	// Speaker/title line for the header-strip styles (2/16/9); len <= 0 means
	// no title, exactly like the legacy drawTitle early-out.
	int titleStart = 0;
	int titleLen = 0;

	// Scrollbar (numDialogLines > viewLines). Values are already
	// header-adjusted by the producer.
	bool showScrollBar = false;
	int scrollTop = 0;
	int scrollPageEnd = 0;
	int scrollNumLines = 0;

	// Which page icon the frame shows. At most one of down/ok is ever set
	// (they share the same rect, src/Canvas.cpp:336-346).
	bool showPageUp = false;
	bool showPageDown = false;
	bool showPageOk = false;

	// Whether a click on the OK icon or on the box body reports Activate.
	// The original swallows that touch on the last page of a choice dialog
	// (src/TouchController.cpp:379-392: buttons 6/7/8 return without queueing
	// when `!(currentDialogLine < numDialogLines - dialogViewLines)` and
	// `dialogFlags & 0x7`). The page-up/page-down icons need no such gate:
	// they are only drawn on pages where it cannot bite.
	bool activateFires = true;
};

} // namespace newcore

#endif // NEW_UI_DIALOGMODEL_H
