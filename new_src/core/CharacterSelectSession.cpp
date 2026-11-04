#include "core/CharacterSelectSession.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "io/Localization.h"
#include "io/Tables.h"

namespace newcore {

namespace {

// getCharacterConstantByOrder (src/IntroSequenceManager.cpp:325-339).
constexpr int kSlotChoice[3] = { 1, 3, 2 };

// Menu strings (file 3, src/MenuStrings.h): CHARACTER_SELECT_MAJOR + c - 1 /
// CHARACTER_SELECT_MAJOR_NAME + c - 1 for constant c (:261-266).
constexpr int kStrShortName = 211;
constexpr int kStrFullName = 214;
constexpr int kStrConfirm = 221;   // CHARACTER_SELECT_CONFIRM
constexpr int kStrBack = 80;       // BACK_ITEM
constexpr int kStrYes = 197;       // YES_LABEL
// DEFENSE, STRENGTH, ACCURACY, AGILITY, IQ labels (:54, :56, :272-274).
constexpr int kStrStat[5] = { 13, 15, 224, 223, 225 };

// drawCharacterSelectionStats' table (src/IntroSequenceManager.cpp:493-528), by constant - 1.
constexpr int kStats[3][5] = {
	{ 8, 9, 97, 12, 110 },    // 1 Major
	{ 12, 14, 92, 6, 100 },   // 2 Sarge
	{ 8, 8, 87, 6, 150 },     // 3 Scientist
};

constexpr int kPromptChars = 24;   // wrapText(0x18, '\n') (:436)

} // namespace

void CharacterSelectSession::init(const Env& env) {
	env_ = env;
}

void CharacterSelectSession::begin(int highlighted) {
	highlighted_ = highlighted;
	confirm_ = false;
	onYes_ = true;
}

void CharacterSelectSession::step(int dir) {
	for (int i = 0; i < 3; ++i) {
		if (kSlotChoice[i] == highlighted_) {
			highlighted_ = kSlotChoice[(i + (dir > 0 ? 1 : 2)) % 3];
			std::fprintf(stderr, "[sound] 1027\n");   // soundClick
			return;
		}
	}
}

void CharacterSelectSession::handleAction(Action a) {
	if (!confirm_) {
		if (a == Action::TurnRight) {
			step(1);
		} else if (a == Action::TurnLeft) {
			step(-1);
		} else if (a == Action::Use) {
			confirm_ = true;
			std::fprintf(stderr, "[sound] 1086\n");
		} else if (a == Action::Menu) {
			if (env_.backToMain) env_.backToMain();
		}
		return;
	}
	if (a == Action::TurnLeft || a == Action::TurnRight) {
		onYes_ = !onYes_;
		std::fprintf(stderr, "[sound] 1027\n");
	} else if (a == Action::Use) {
		std::fprintf(stderr, "[sound] 1086\n");
		if (onYes_) choose();
		else if (env_.backToMain) env_.backToMain();
	} else if (a == Action::Menu) {
		confirm_ = false;
		onYes_ = true;
		std::fprintf(stderr, "[sound] 1122\n");
	}
}

// One tap confirms (src/IntroSequenceManager.cpp:612-634).
void CharacterSelectSession::handleTouch(int buttonId) {
	if (buttonId >= 0 && buttonId < 3) {
		highlighted_ = kSlotChoice[buttonId];
	} else if (buttonId == 3) {
		choose();
	} else if (buttonId == 4) {
		if (env_.backToMain) env_.backToMain();
	}
}

void CharacterSelectSession::choose() {
	env_.player->characterChoice = highlighted_;
	env_.player->reset();
	std::fprintf(stderr, "[charselect] character %d\n", highlighted_);
	env_.host->requestState(StateId::Intro);
}

void CharacterSelectSession::buildModel(CharacterSelectModel& m) {
	m = CharacterSelectModel{};
	m.highlighted = highlighted_;
	for (int i = 0; i < 3; ++i) m.slotChoice[i] = kSlotChoice[i];
	const Localization& loc = *env_.loc;

	title_.setLength(0);
	title_.append(loc.get(kTextIngame2, kStrFullName + highlighted_));
	title_.dehyphenate();
	m.title = &title_;

	for (int i = 0; i < 3; ++i) {
		// currentCharColor = 5 resets after each string (src/Graphics.cpp:634).
		shortName_[i].setLength(0);
		shortName_[i].append("^5");
		shortName_[i].append(loc.get(kTextIngame2, kStrShortName + kSlotChoice[i]));
		shortName_[i].dehyphenate();
		m.shortName[i] = &shortName_[i];
	}

	const int* stats = kStats[std::clamp(highlighted_, 1, 3) - 1];
	for (int k = 0; k < 5; ++k) {
		const bool defenseZero = k == 0 && env_.game != nullptr && env_.game->difficulty() == 2;   // (:530-532)
		stat_[k].setLength(0);
		stat_[k].append("^5");
		stat_[k].append(loc.get(kTextIngame2, kStrStat[k]));
		stat_[k].dehyphenate();
		stat_[k].append(defenseZero ? 0 : stats[k]);
		m.statRow[k] = &stat_[k];
	}

	prompt_.setLength(0);
	prompt_.append(loc.get(kTextIngame2, kStrConfirm));
	prompt_.wrapText(kPromptChars, '\n');
	m.prompt = &prompt_;
	back_.setLength(0);
	back_.append(loc.get(kTextIngame2, kStrBack));
	m.back = &back_;
	yes_.setLength(0);
	yes_.append(loc.get(kTextIngame2, kStrYes));
	m.yes = &yes_;

	m.torsoLift = (int)((*env_.upTimeMs / 1000) & 1);
	m.confirmCursor = confirm_;
	m.cursorOnYes = onYes_;
	if (env_.tables != nullptr && env_.tables->oscCycle.size() >= 4)
		m.cursorOffset = env_.tables->oscCycle[(int)((*env_.upTimeMs / 100) % 4)];
}

} // namespace newcore
