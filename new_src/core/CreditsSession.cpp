#include "core/CreditsSession.h"

#include "io/Localization.h"

namespace newcore {

namespace {

constexpr int kSpacing = 16;
constexpr int kMsPerLine = 500 * ((kSpacing << 16) >> 4) >> 16;   // 500 (:211)
constexpr int kWrap = (480 - 8) / 11;                              // 42, a character count (:180)
constexpr int kExtraLines = 5;                                     // (:182)
constexpr int kStrContinue = 43;                                   // 0:43 "Continue" (:197-199)
constexpr int kTextTopY = 250;                                     // cinRect[3] (src/Canvas.cpp:113-154)

} // namespace

void CreditsSession::init(const Env& env) {
	env_ = env;
}

void CreditsSession::begin() {
	text_.setLength(0);
	text_.append(env_.loc->get(kTextHelp, 0));
	text_.wrapText(kWrap);
	// ST_CREDITS: no (320 / spacing - 2) term (:214-215).
	end_ = kMsPerLine * (text_.getNumLines() + kExtraLines);
	start_ = *env_.upTimeMs;
	done_ = false;
	continue_.setLength(0);
	continue_.append(env_.loc->get(kTextMain, kStrContinue));
	continue_.dehyphenate();
}

void CreditsSession::buildModel(CreditsModel& m) {
	m = CreditsModel{};
	int t = static_cast<int>(*env_.upTimeMs - start_);
	if (t > end_) {                                   // hold the end (:222-226)
		t = end_;
		done_ = true;
	}
	const int off = (((t << 8) / kMsPerLine) * (kSpacing << 8)) >> 16;   // 32 px/s (:243)
	m.text = &text_;
	m.textY = kTextTopY - off;
	m.continueLabel = done_ ? &continue_ : nullptr;
}

} // namespace newcore
