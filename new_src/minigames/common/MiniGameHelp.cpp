#include "minigames/common/MiniGameHelp.h"

#include <algorithm>

#include "io/Localization.h"

namespace newcore {

namespace {

constexpr int kFooterId = 96;   // composeText(0, 0x60) (src/MiniGameManager.cpp:321)

} // namespace

void MiniGameHelp::scroll(int delta) {
	if (delta < 0) {
		scroll_ = std::max(0, scroll_ + delta);
	} else if (delta > 0) {
		scroll_ = std::min(std::max(0, numLines_ - kViewLines), scroll_ + delta);
	}
}

// composeText resolves %NN args too; the help strings carry none, so the raw
// string is used (same shortcut as MenuSession's help text).
void MiniGameHelp::compose(int id, Text& out) const {
	out.setLength(0);
	if (loc_ != nullptr) out.append(loc_->get(loc_->typeOf(id), loc_->indexOf(id)));
}

void MiniGameHelp::buildModel(int titleId, int bodyId, MiniGameHelpModel& m) {
	compose(titleId, title_);
	title_.dehyphenate();

	compose(bodyId, body_);
	body_.wrapText(kWrapChars, '\n');
	numLines_ = body_.getNumLines();

	// The legacy cuts the buffer down to the window: drop the first scroll_
	// lines at the first '\n', then one trailing line at the last '\n' for
	// every line past scroll_+16 (:276-304). Same cuts, as offsets.
	int start = 0;
	for (int i = 0; i < scroll_; ++i) {
		const int p = body_.findFirstOf('\n', start);
		if (p != -1) start = p + 1;
	}
	int end = body_.length();
	for (int i = scroll_ + kViewLines; i < numLines_; ++i) {
		const int p = body_.findLastOf('\n', end);
		if (p >= start) end = p;
	}
	window_.setLength(0);
	body_.substring(window_, start, end - start);

	compose(kFooterId, footer_);
	footer_.dehyphenate();

	m.title = &title_;
	m.body = &window_;
	m.footer = &footer_;
	m.firstLine = scroll_;
	m.pageEnd = std::min(scroll_ + kViewLines, numLines_);
	m.numLines = numLines_;
}

} // namespace newcore
