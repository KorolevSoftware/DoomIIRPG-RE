#include "core/PrologueSession.h"

#include <cstdio>
#include <string>

#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "io/Localization.h"

namespace newcore {

namespace {

constexpr int kStrStory = 12;          // 2:12 "Mission Update ... Greetings, %01. ..."
constexpr int kWrapChars = (480 - 30) / 10;    // (:84)
constexpr int kLinesPerPage = (320 - 40) / 21; // (:85)

} // namespace

void PrologueSession::init(const Env& env) {
	env_ = env;
}

void PrologueSession::begin() {
	page_ = 0;
	totalPages_ = 0;
	cursor_ = 1;                                                   // (src/Canvas.cpp:1174-1178)
	const Localization& loc = *env_.loc;
	const int choice = env_.player->characterChoice;
	const std::string arg = loc.get(kTextIngame2, choice == 1 ? 218 : choice == 2 ? 219 : 220);   // (:59-73)
	std::string s = loc.get(kTextHelp, kStrStory);
	composeArgs(s, &arg, 1);
	story_.setLength(0);
	story_.append(s);                                              // not dehyphenated (:80)
	story_.wrapText(kWrapChars);                                   // '|' breaks (:86)

	pageStart_[totalPages_++] = 0;                                 // (:88-96)
	int lines = 0;
	for (int first = 0; (first = story_.findFirstOf('|', first)) != -1;) {
		++first;
		if (++lines % kLinesPerPage == 0) {
			if (totalPages_ >= kMaxPages) {
				std::fprintf(stderr, "[intro] story exceeds %d pages - truncated\n", kMaxPages);
				break;
			}
			pageStart_[totalPages_++] = first;
		}
	}
	pageStart_[totalPages_] = story_.length();
}

void PrologueSession::prev() {
	if (page_ == 0) env_.host->requestState(StateId::CharacterSelect);
	else --page_;
}

void PrologueSession::handleAction(Action a) {
	switch (a) {
	case Action::TurnLeft:
	case Action::TurnRight:
		if (cursor_ != 2) cursor_ ^= 1;
		break;
	case Action::Forward:                                          // ACTION_UP
		if (cursor_ != 2 && page_ < totalPages_ - 1) cursor_ = 2;
		break;
	case Action::Back:                                             // ACTION_DOWN
		if (cursor_ == 2) cursor_ = 1;
		break;
	case Action::Use:                                              // ACTION_FIRE
		if (cursor_ == 0) prev();
		else if (cursor_ == 1) ++page_;
		else page_ = totalPages_;
		break;
	case Action::Automap:
		++page_;
		cursor_ = 1;
		break;
	case Action::BackKey:
	case Action::Menu:
		prev();
		if (env_.host->state() == StateId::Intro) cursor_ = 0;
		break;
	default:
		break;
	}
}

void PrologueSession::handleTouch(int id) {
	if (id < 0 || id > 2 || (id == 2 && page_ >= totalPages_ - 1)) return;
	cursor_ = id;
	handleAction(Action::Use);
}

void PrologueSession::buildModel(PrologueModel& m) {
	m = PrologueModel{};
	const Localization& loc = *env_.loc;
	// Deviation FE-D4: the current page, where legacy always draws page 0.
	m.body = &story_;
	m.bodyStart = pageStart_[page_];
	m.bodyLen = pageStart_[page_ + 1] - pageStart_[page_];
	back_.setLength(0);
	back_.append(loc.get(kTextIngame2, 80));
	back_.dehyphenate();
	m.back = &back_;
	const bool last = page_ >= totalPages_ - 1;
	right_.setLength(0);
	right_.append(loc.get(kTextMain, last ? 43 : 56));
	right_.dehyphenate();
	m.bottomRight = &right_;
	if (!last) {
		skip_.setLength(0);
		skip_.append(loc.get(kTextMain, 40));
		skip_.dehyphenate();
		m.topRight = &skip_;
	}
}

} // namespace newcore
