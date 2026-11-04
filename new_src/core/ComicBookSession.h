#ifndef NEW_CORE_COMICBOOKSESSION_H
#define NEW_CORE_COMICBOOKSESSION_H

#include "core/GameStates.h"
#include "text/Text.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// The comic viewer behind menu id 89 (src/ComicBook.cpp, landscape mode only,
// docs/original-code/front-end.md §12.1). State survives leaving and re-entering
// 89 like the legacy app->comicBook (the page persists).
class ComicBookSession {
public:
	static constexpr int kFrames = 39;
	ComicBookSession();
	void handleAction(Action a);   // handleComicBookEvents (:972-994) + deviation FE-D7
	UiResult draw(Ui& ui);         // touch samples from ui.in(), then Draw (:88-214); {UiAction::Menu} = Done released

private:
	void touch(int x, int y, bool press);   // Touch (:714-860)
	void touchMove(int x, int y);           // TouchMove (:880-913)
	bool buttonTouch(int x, int y);         // ButtonTouch (:862-878)
	void updateMovement();                  // UpdateMovement, landscape branch (:584-650)

	int page_ = 0;           // iPhonePage
	int target_ = 0;         // cur_iPhonePage
	int endPoint_ = 0, midPoint_ = 0, begPoint_ = 0;
	bool dragging_ = false;  // field_0x10
	bool sliding_ = false;   // field_0x110
	bool snapping_ = false;  // field_0x118
	int pressFrames_ = 0;    // field_0x14c
	bool drawExit_ = false, exitHighlighted_ = false;
	bool exitRequested_ = false;
	int lastX_ = 240, lastY_ = 160;
	Text doneLabel_;         // literal "Done" (:963-966)
};

} // namespace newcore

#endif // NEW_CORE_COMICBOOKSESSION_H
