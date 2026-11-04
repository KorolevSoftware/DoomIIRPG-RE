#include "core/ComicBookSession.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "ui/ComicBookModel.h"
#include "ui/ComicBookView.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

constexpr int kLast = ComicBookSession::kFrames - 1;   // 38
constexpr int kFrameW = 480;
constexpr int kHalfW = 240;
constexpr int kMinSpeed = 8;                           // landscape minimum slide speed (:600-627)
constexpr int kTapFrames = 8;                          // short tap toggles Done (:756-758)

int prevFrame(int p) { return p == 0 ? kLast : p - 1; }
int nextFrame(int p) { return p == kLast ? 0 : p + 1; }

} // namespace

ComicBookSession::ComicBookSession() {
	doneLabel_.append("Done");
	doneLabel_.dehyphenate();
}

void ComicBookSession::handleAction(Action a) {
	// Every key hides Done (:993). The fake centre tap legacy sends first changes nothing else.
	if ((a == Action::TurnLeft || a == Action::TurnRight) && !sliding_ && !dragging_) {
		// FE-D7: a real slide at max(|midPoint|, 8) = 8 px/frame instead of the legacy one-frame flip
		// (pixel ints fed to the normalised-float userMoved, src/CAppContainer.cpp:71-89).
		target_ = a == Action::TurnLeft ? prevFrame(page_) : nextFrame(page_);   // LEFT drags right -> previous
		snapping_ = false;
		midPoint_ = 0;
	}
	drawExit_ = false;
}

UiResult ComicBookSession::draw(Ui& ui) {
	const UiInput& in = ui.in();
	const int x = in.cursorValid ? in.cursorX : lastX_;
	const int y = in.cursorValid ? in.cursorY : lastY_;
	if (in.pressed) {
		touch(x, y, true);
	} else if (in.down && (x != lastX_ || y != lastY_)) {
		touchMove(x, y);
	}
	if (in.released) touch(x, y, false);
	lastX_ = x;
	lastY_ = y;
	++pressFrames_;                                    // ++field_0x14c at the top of Draw (:105)
	updateMovement();

	ComicBookModel m;
	m.frame = page_;
	m.x = endPoint_;
	m.neighbours = dragging_ || sliding_;
	m.next = nextFrame(page_);
	m.prev = prevFrame(page_);
	m.doneButton = drawExit_;
	m.doneHighlighted = exitHighlighted_;
	m.doneLabel = &doneLabel_;
	drawComicBook(ui, m);

	if (exitRequested_) {
		exitRequested_ = false;
		return { UiAction::Menu, -1 };                 // -> Action::Menu -> back()
	}
	return {};
}

bool ComicBookSession::buttonTouch(int x, int y) {
	exitHighlighted_ = false;
	if (!drawExit_) return false;
	// Inclusive edges, x <= rx + rw (:870-876).
	if (10 <= x && x <= 70 && 10 <= y && y <= 40) exitHighlighted_ = true;
	return exitHighlighted_;
}

void ComicBookSession::touch(int x, int y, bool press) {
	if (buttonTouch(x, y)) std::fprintf(stderr, "[sound] 1064\n");   // menu_open.wav (:734-736)
	if (press) {
		pressFrames_ = 0;
	} else {
		if (exitHighlighted_) {                        // DeleteImages + back() (:744-753)
			exitHighlighted_ = false;
			exitRequested_ = true;
		}
		if (pressFrames_ <= kTapFrames) drawExit_ = !drawExit_;
	}
	if (sliding_ || (!dragging_ && !press)) return;
	dragging_ = press;
	begPoint_ = x;
	if (press) {                                       // field_0x118 = field_0x110 = 0 here
		snapping_ = false;
		midPoint_ = 0;
		return;
	}
	if (endPoint_ > kHalfW || midPoint_ > 2) {         // previous (:775-818)
		snapping_ = false;
		target_ = page_ - 1;
	} else if (endPoint_ < -kHalfW || midPoint_ < -2) {   // next
		snapping_ = false;
		target_ = page_ + 1;
	} else {                                           // snap back
		target_ = page_;
		if (endPoint_ != 0) snapping_ = true;
	}
	if (target_ < 0) {                                 // (:836-846)
		target_ = kLast;
	} else if (target_ > kLast) {
		target_ = 0;
	}
}

void ComicBookSession::touchMove(int x, int y) {
	buttonTouch(x, y);
	if (!sliding_ && dragging_) {                      // (:880-913)
		midPoint_ = x - begPoint_;
		begPoint_ = x;
		endPoint_ += midPoint_;
	}
}

void ComicBookSession::updateMovement() {
	if (dragging_) return;
	if (page_ != target_) {
		sliding_ = true;
		const int speed = std::max(std::abs(midPoint_), kMinSpeed);
		// Legacy: endPoint += (page >= target ? +speed : -speed), then the 0<->38 wrap pair
		// corrects by 2*speed (:600-627). For a neighbour target (the only kind touch() and
		// handleAction produce) that is exactly:
		const bool toPrev = target_ == prevFrame(page_);
		endPoint_ += toPrev ? speed : -speed;
		if (endPoint_ >= kFrameW || endPoint_ <= -kFrameW) {   // commit (:628-639)
			page_ = target_;
			midPoint_ = 0;
			endPoint_ = 0;
			sliding_ = false;
		}
	}
	if (snapping_) {                                   // snap-back (:641-650)
		sliding_ = true;
		endPoint_ = 4 * endPoint_ / 5;                 // truncates toward 0
		if (endPoint_ == 0) {
			snapping_ = false;
			sliding_ = false;
		}
	}
	assert(page_ >= 0 && page_ < kFrames && target_ >= 0 && target_ < kFrames);
}

} // namespace newcore
