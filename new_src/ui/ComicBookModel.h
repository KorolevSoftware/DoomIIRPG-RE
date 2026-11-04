#ifndef NEW_UI_COMICBOOKMODEL_H
#define NEW_UI_COMICBOOKMODEL_H

namespace newcore {

class Text;

// Per-frame model of the comic viewer (menu id 89, src/ComicBook.cpp:88-214).
// Built by ComicBookSession::draw; `doneLabel` is borrowed for THIS frame.
struct ComicBookModel {
	int frame = 0;                    // iPhonePage, 0..38
	int x = 0;                        // endPoint: the current frame's left edge
	bool neighbours = false;          // dragging || sliding (field_0x10 || field_0x110)
	int next = 1, prev = 38;          // wrapped neighbour indices
	bool doneButton = false;          // drawExitButton
	bool doneHighlighted = false;     // exitBtnHighlighted
	const Text* doneLabel = nullptr;
};

} // namespace newcore

#endif // NEW_UI_COMICBOOKMODEL_H
