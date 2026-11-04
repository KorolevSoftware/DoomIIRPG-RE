#include "minigames/sentrybot/SentryBotGame.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

#include "core/Sound.h"
#include "domain/game/DialogSystem.h"
#include "domain/game/Game.h"
#include "domain/game/ItemPickup.h"
#include "domain/game/Player.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "minigames/common/MiniGameHost.h"
#include "minigames/sentrybot/SentryBotView.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

constexpr int kStrExit = 30;
constexpr int kStrHelpTitle = 171, kStrHelpBody = 172;
constexpr int kStrHint = 173, kStrSkipCode = 174, kStrEnterCode = 175, kStrTapHint = 176;
constexpr int kStrAccepted = 177, kStrDenied = 178, kStrSelect = 180;
constexpr int kStrShooting = 181, kStrExploding = 182, kStrShootingInfo = 183, kStrExplodingInfo = 184;
constexpr int kStrWarning = 186, kStrDefensive = 187, kStrLoggedOut = 188;
constexpr int kStrHackSuccessful = 239, kStrHackFailed = 240;

void compose(const Localization* loc, int index, Text& out) {
	if (loc != nullptr) out.append(loc->get(kTextMain, index));
}

// Fresh buffer with one composed Main string, dehyphenated.
void composeLine(const Localization* loc, int index, Text& out) {
	out.setLength(0);
	compose(loc, index, out);
	out.dehyphenate();
}

void literal(const char* s, Text& out) {
	out.setLength(0);
	out.append(s);
}

} // namespace

bool SentryBotGame::loadAssets(TextureStore& store, const UiAssets::ResourceReader& read) {
	const bool ok = assets_.load(store, read);
	// The button size drives the Submit/Delete rects (:107, :114).
	std::fprintf(stderr, "[sentrybot] button %dx%d\n", assets_.button.width(), assets_.button.height());
	return ok;
}

void SentryBotGame::begin(int b1, int, int) {
	botType_ = (b1 == 0 || b1 == 1) ? b1 : 0;
	help_ = true;
	result_ = kBoard;
	host_->help().reset();
	generate();
	answerCursor_ = 0;
	botCursor_ = 0;
	playerCursor_ = -1;
	for (uint8_t& g : guess_) g = 0;
	cursorMoveTime_ = *host_->env().upTimeMs;
	touched_ = false;
	curX_ = curY_ = 0;
	touchCell_ = touchBot_ = -1;
	lastHighlight_ = -1;
	std::fprintf(stderr, "[sentrybot] begin botType=%d iq=%d skip=%d solution=%d%d%d%d\n",
		botType_, host_->fromMainMenu() ? 0 : host_->env().player->ce.getIQPercent(), skipCode_,
		solution_[0], solution_[1], solution_[2], solution_[3]);
}

void SentryBotGame::generate() {
	Player& p = *host_->env().player;
	const int iq = host_->fromMainMenu() ? 0 : p.ce.getIQPercent();   // (:55)
	const int n4 = (10000 * 2 + (6 - 2) * (100 - iq) * (100 - iq)) / 10000;
	do {
		skipCode_ = ItemPickup::nextInt() % n4 + 1;
	} while (skipCode_ == p.lastSkipCode);
	p.lastSkipCode = (uint8_t)skipCode_;
	const bool allowSkip = host_->fromMainMenu() || iq < 50;   // (:61)
	// True when every cell the solution walk can visit is the skip code: the
	// legacy walk (:72) would loop forever.
	auto orbitAllSkip = [this]() {
		for (int k = 1; k <= 16; ++k) {
			if (board_[(k * skipCode_ - 1) % 16] != skipCode_) return false;
		}
		return true;
	};
	// shortcut: regenerate the board instead of hanging; unreachable for iq >= 50.
	do {
		for (int i = 0; i < 16; ++i) {
			board_[i] = (uint8_t)(ItemPickup::nextInt() % 6 + 1);
			if (board_[i] == skipCode_ && !allowSkip) --i;
		}
		board_[0] = (uint8_t)(skipCode_ % 6 + 1);
	} while (orbitAllSkip());
	int cur = -1;
	for (int j = 0; j < 4; ++j) {
		do {
			cur = (cur + skipCode_) % 16;
		} while (board_[cur] == skipCode_);
		solution_[j] = board_[cur];
	}
#ifndef NDEBUG
	assert(skipCode_ >= 1 && skipCode_ <= n4);
	assert(board_[0] != skipCode_);
	for (int j = 0; j < 4; ++j) assert(solution_[j] != skipCode_);
	if (!allowSkip) {
		for (int i = 0; i < 16; ++i) assert(board_[i] != skipCode_);
	}
#endif
}

void SentryBotGame::handleAction(Action a) {
	switch (a) {
	case Action::Forward:   input(kUp); break;
	case Action::Back:      input(kDown); break;
	case Action::TurnLeft:  input(kLeft); break;
	case Action::TurnRight: input(kRight); break;
	case Action::Menu:      input(kMenu); break;
	case Action::Use:       input(kFire); break;
	case Action::Automap:   input(kAutomap); break;
	case Action::Passturn:  input(kPassTurn); break;
	case Action::BackKey:   input(kBack); break;
	default: break;
	}
}

void SentryBotGame::commitTouch() {
	if (touchCell_ != -1) {   // no blink reset (legacy :515)
		playerCursor_ = touchCell_;
		touchCell_ = -1;
	}
	if (touchBot_ != -1) {
		botCursor_ = touchBot_;
		touchBot_ = -1;
	}
}

void SentryBotGame::input(int action) {
	commitTouch();
	const int64_t now = *host_->env().upTimeMs;
	// Verbatim (:130-133): a main-menu WIN also ends with endGame(0), i.e. sound 1040.
	if ((result_ == kLost || (result_ == kWon && host_->fromMainMenu())) &&
	    (action == kAutomap || action == kBack || action == kFire)) {
		endGame(0);
		return;
	}
	if (help_) {
		if (action == kMenu || action == kBack) {
			if (result_ == kBoard) {
				endGame(2);
			} else if (result_ == kWon) {
				award();
				endGame(1);
			} else {
				endGame(0);
			}
		} else if (action == kFire) {
			help_ = false;
		} else if (action == kUp) {
			host_->help().scroll(-1);
		} else if (action == kDown) {
			host_->help().scroll(1);
		} else if (action == kRight) {
			host_->help().scroll(16);
		} else if (action == kLeft) {
			host_->help().scroll(-16);
		}
		return;
	}
	if (result_ == kBoard) {
		switch (action) {
		case kAutomap:
		case kBack:
			help_ = true;
			host_->help().reset();
			break;
		case kMenu:
			endGame(2);
			break;
		case kFire:
			if (answerCursor_ < 4) {
				if (playerCursor_ > -1) {
					guess_[answerCursor_] = board_[playerCursor_];
					++answerCursor_;
				}
			} else {
				bool won = true;   // playerHasWon (:756-763)
				for (int i = 0; i < 4; ++i) won = won && guess_[i] == solution_[i];
				result_ = won ? kWon : kLost;
				std::fprintf(stderr, "[sentrybot] submit %d%d%d%d -> %s\n",
					guess_[0], guess_[1], guess_[2], guess_[3], won ? "accepted" : "denied");
			}
			break;
		case kLeft:
			if (playerCursor_ > -1) {
				playerCursor_ = (playerCursor_ + 15) % 16;
				cursorMoveTime_ = now;
				Sound::instance().soundClick();
			}
			break;
		case kRight:
			playerCursor_ = (playerCursor_ + 1) % 16;
			cursorMoveTime_ = now;
			Sound::instance().soundClick();
			break;
		case kPassTurn:
		case kMatrixLeft:
			if (answerCursor_ > 0) {
				if (answerCursor_ <= 3) guess_[answerCursor_] = 0;
				--answerCursor_;
				guess_[answerCursor_] = 0;
			}
			break;
		case kMatrixRight:
			if (playerCursor_ > -1 && answerCursor_ <= 3) {
				guess_[answerCursor_] = board_[playerCursor_];
				++answerCursor_;
			}
			break;
		default:
			break;
		}
	} else if (result_ == kWon && !host_->fromMainMenu()) {   // (:271-272)
		if (action == kFire) {
			award();
			endGame(1);
		} else if (action == kLeft || action == kRight) {
			botCursor_ = (botCursor_ + 1) % 2;
		}
	}
}

void SentryBotGame::handleTouch(int buttonId) {
	commitTouch();
	if (help_) {
		input(buttonId == 0 ? kMenu : kFire);
	} else if (result_ == kBoard) {
		if (buttonId == 0) input(kMenu);
		else if (buttonId == 1) input(kBack);
		else if (buttonId == 2) { if (answerCursor_ > 3) input(kFire); }
		else if (buttonId == 3) input(kMatrixLeft);
		else input(kMatrixRight);
	} else if (result_ == kWon) {
		input(kFire);
	} else {
		input(kMenu);   // faithful quirk D3: rule 0 ignores MENU
	}
}

void SentryBotGame::award() {
	const int w = botType_ == 0 ? (botCursor_ == 0 ? 3 : 4) : (botCursor_ == 0 ? 5 : 6);   // :276-282
	const MiniGameHost::Env& e = host_->env();
	Player& p = *e.player;
	for (int k = 3; k <= 6; ++k) {                       // awardSentryBot (src/SentryBotGame.cpp:784-802)
		if ((p.weapons & (1 << k)) == 0) continue;
		e.game->attemptToDiscardFamiliar(k);
		// showHelp(16, true); state is MiniGame, so the help is not dequeued here.
		p.helpBitmask |= 1 << 16;
		e.dialogs->enqueueHelpDialog(kTextMain, 16, -1);
		break;
	}
	p.give(1, w, 1);   // ammo[7] = 100, bits 0x78 cleared, bit w set, selectWeapon(w)
	std::fprintf(stderr, "[sentrybot] awarded bot weapon %d\n", w);
}

void SentryBotGame::endGame(int n) {
	Sound::instance().playSound(n == 1 ? 1043 : 1040, 0, 3);
	if (!host_->fromMainMenu()) host_->evaluateResults(n);   // (:811-814)
	host_->finish(n);
}

// ---- frame ----

UiResult SentryBotGame::draw(Ui& ui) {
	const UiInput& in = ui.in();
	const MiniGameHost::Env& e = host_->env();
	const Localization* loc = e.loc;
	const int64_t now = *e.upTimeMs;

	// touchStart / touchMove samples (:826-837); the release is applied last.
	if (in.cursorValid && in.pressed) {
		touched_ = true;
		curX_ = in.cursorX;
		curY_ = in.cursorY;
	} else if (touched_ && in.down && in.cursorValid) {
		curX_ = in.cursorX;
		curY_ = in.cursorY;
	}

	model_ = SentryBotModel{};
	const int bw = assets_.button.width(), bh = assets_.button.height();
	model_.buttons[0] = MiniGameButton{ 0, UiRect{ 428, 290, 54, 30 } };   // :101-119
	model_.buttons[1] = MiniGameButton{ 1, UiRect{ 0, 290, 54, 30 } };
	model_.buttons[2] = MiniGameButton{ 2, UiRect{ 368, 142 - bh / 2, bw, bh } };
	model_.buttons[3] = MiniGameButton{ 3, UiRect{ 8, 142 - bh / 2, bw, bh } };
	model_.buttonCount = 4;

	// fmButton::SetHighlighted plays sound 1027 on the highlight edge unless it
	// is already playing (src/Button.cpp:145-155).
	const int hl = highlightedButtonId(model_.buttons, model_.buttonCount, in);
	Sound& snd = Sound::instance();
	if (hl != -1 && hl != lastHighlight_ && !snd.isSoundPlaying(1027)) snd.playSound(1027, 0, 5);
	lastHighlight_ = hl;

	// Render mode 1 = 25 % alpha, the highlight mode 0 opaque (:685-701,
	// src/Image.cpp:172-175). Faithful quirk D4: the active button is faded.
	model_.submitAlpha = (hl == 2) ? 255 : (answerCursor_ > 3 ? 64 : 255);
	model_.deleteAlpha = (hl == 3) ? 255 : (answerCursor_ > 0 ? 64 : 255);

	if (help_) {
		model_.screen = SentryBotModel::Screen::Help;
		host_->help().buildModel(kStrHelpTitle, kStrHelpBody, model_.help);
		model_.help.background = &host_->assets().helpBackground;
		exitCode_.setLength(0);
		compose(loc, kStrExit, exitCode_);
		model_.exitCode = &exitCode_;
	} else {
		for (int i = 0; i < 4; ++i) {
			if (guess_[i] != 0) {
				guessText_[i].setLength(0);
				guessText_[i].append((int)guess_[i]);
				model_.guess[i] = &guessText_[i];
			}
		}
		if (result_ == kBoard) {
			model_.screen = SentryBotModel::Screen::Board;
			if (touched_) {
				for (int i = 0; i < 16; ++i) {
					const int n3 = 240 + (i % 4) * 42;
					const int n4 = 50 + (i / 4) * 44;
					if (UiRect{ n3 - 84, n4 + 6, 42, 42 }.contains(curX_, curY_)) {
						model_.pressedCell = i;
						const int shown = touchCell_ != -1 ? touchCell_ : playerCursor_;
						if (i != shown) Sound::instance().soundClick();   // (:512)
						touchCell_ = i;
						break;
					}
				}
			}
			model_.cursor = touchCell_ != -1 ? touchCell_ : playerCursor_;
			model_.cursorVisible = ((now - cursorMoveTime_) & 0x200) == 0;   // :751
			for (int i = 0; i < 16; ++i) {
				model_.board[i] = board_[i];
				digits_[i].setLength(0);
				digits_[i].append((int)board_[i]);
				model_.digits[i] = &digits_[i];
			}
			hint_.setLength(0);
			compose(loc, kStrHint, hint_);
			compose(loc, kStrSkipCode, hint_);
			hint_.append(skipCode_);
			hint_.dehyphenate();
			model_.hint = &hint_;
			composeLine(loc, kStrEnterCode, enterCode_);
			model_.enterCode = &enterCode_;
			composeLine(loc, kStrTapHint, tapHint_);
			model_.tapHint = &tapHint_;
			literal("Back", back_);
			model_.back = &back_;
			literal("Exit", exit_);
			model_.exit = &exit_;
			literal("Delete", del_);
			model_.del = &del_;
			literal("Submit", submit_);
			model_.submit = &submit_;
			model_.guessY = 249;
			model_.answerCursor = answerCursor_;
		} else if (result_ == kWon && !host_->fromMainMenu()) {
			model_.screen = SentryBotModel::Screen::Success;
			if (hl == 3) touchBot_ = 0;        // :424-426
			else if (hl == 2) touchBot_ = 1;   // :438-440
			const int shownBot = touchBot_ != -1 ? touchBot_ : botCursor_;
			composeLine(loc, kStrAccepted, title_);
			model_.title = &title_;
			composeLine(loc, kStrSelect, select_);
			model_.select = &select_;
			composeLine(loc, kStrShooting, shooting_);
			model_.shooting = &shooting_;
			composeLine(loc, kStrExploding, exploding_);
			model_.exploding = &exploding_;
			const int w = (shownBot == 0 ? shooting_ : exploding_).getStringWidth();
			const int xPos = (shownBot == 0 ? 60 : 420) - w / 2 - 5;
			int osc = 0;
			if (e.tables != nullptr && e.tables->oscCycle.size() >= 4) {
				osc = e.tables->oscCycle[(size_t)((now / 100) % 4)];
			}
			model_.selCursorX = xPos + osc;
			literal("Shooting Bot:", shootingHead_);
			model_.shootingHead = &shootingHead_;
			literal("Exploding Bot:", explodingHead_);
			model_.explodingHead = &explodingHead_;
			composeLine(loc, kStrShootingInfo, shootingBody_);
			shootingBody_.wrapText(50, '\n');
			model_.shootingBody = &shootingBody_;
			composeLine(loc, kStrExplodingInfo, explodingBody_);
			explodingBody_.wrapText(50, '\n');
			model_.explodingBody = &explodingBody_;
			model_.guessY = 26;
			model_.answerCursor = -1;
		} else if (result_ == kWon) {                 // main-menu win (drawSuccessScreen, :380-395)
			// Same layout as the botType-0 failure screen: 177 at y 15, 239 at y 71, 188 wrapped 22 at y 139.
			model_.screen = SentryBotModel::Screen::Failure;
			composeLine(loc, kStrAccepted, title_);
			model_.title = &title_;
			composeLine(loc, kStrHackSuccessful, subtitle_);
			model_.subtitle = &subtitle_;
			model_.warning = false;
			message_.setLength(0);
			compose(loc, kStrLoggedOut, message_);
			message_.wrapText(22, '\n');
			model_.message = &message_;
			model_.messageY = 139;
			model_.guessY = 26;
			model_.answerCursor = -1;
		} else {
			model_.screen = SentryBotModel::Screen::Failure;
			composeLine(loc, kStrDenied, title_);   // failedEarly (179) is J2ME-only, always false
			model_.title = &title_;
			composeLine(loc, kStrHackFailed, subtitle_);
			model_.subtitle = &subtitle_;
			model_.warning = botType_ == 1;
			if (model_.warning) {
				composeLine(loc, kStrWarning, warning_);
				model_.warningLabel = &warning_;
			}
			message_.setLength(0);
			compose(loc, botType_ == 1 ? kStrDefensive : kStrLoggedOut, message_);
			message_.wrapText(22, '\n');
			model_.message = &message_;
			model_.messageY = botType_ == 1 ? 209 : 139;   // :340-367
			model_.guessY = 26;
			model_.answerCursor = -1;
		}
	}

	if (in.released) touched_ = false;   // touchEnd (:841)
	return drawSentryBot(ui, model_, assets_);
}

} // namespace newcore
