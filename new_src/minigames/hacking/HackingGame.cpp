#include "minigames/hacking/HackingGame.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "domain/game/ItemPickup.h"
#include "domain/game/Player.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "minigames/common/MiniGameHost.h"
#include "minigames/hacking/HackingView.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

// Legacy button container (src/HackingGame.cpp:95, :98).
constexpr MiniGameButton kButtonExit{ 0, UiRect{ 420, 280, 60, 40 } };
constexpr MiniGameButton kButtonBack{ 1, UiRect{ 0, 280, 60, 40 } };
// Confirm screen Yes/No boxes (:460-503).
constexpr UiRect kYesRect{ 142, 240, 45, 25 };
constexpr UiRect kNoRect{ 258, 240, 45, 25 };

constexpr int kStrExit = 30;
constexpr int kStrHelpTitle = 164, kStrHelpBody = 165;
constexpr int kStrPromptPlaying = 166, kStrTurns = 167, kStrGoal = 168;
constexpr int kStrPromptWon = 169, kStrPromptLost = 170, kStrPromptConfirm = 189;
constexpr int kStrBack = 80, kStrYes = 197, kStrNo = 198;   // Menu strings

// Column hit rect, legacy pointInRectangle(x, y, 25+79i, 90, CORE_WIDTH, CORE_HEIGHT).
UiRect columnRect(int i) { return UiRect{ 25 + 79 * i, 90, 35, 100 }; }

// Applet::nextByte (src/App.cpp:510-512).
int nextByte() { return ItemPickup::nextInt() & 0xFF; }

} // namespace

bool HackingGame::loadAssets(TextureStore& store, const UiAssets::ResourceReader& read) {
	const bool ok = assets_.load(store, read);
	// 108 means fixBlockColors ran (the shipped file is 106 wide).
	std::fprintf(stderr, "[hacking] blockGameColors %dx%d\n",
		assets_.colors.width(), assets_.colors.height());
	return ok;
}

// (src/HackingGame.cpp:28-33): 3..5 columns, turns 5..19 / 5..29 / 5..39.
void HackingGame::playFromMainMenu() {
	const int rnd = ItemPickup::nextInt() % 3 + 3;
	begin(0, rnd, ItemPickup::nextInt() % (rnd * 10 - 15) + 5);
}

void HackingGame::begin(int b1, int b2, int b3) {
	if (b1 != 0) std::fprintf(stderr, "[hacking] custom puzzle %d not supported\n", b1);
	columns_ = std::max(3, std::min(b2, 6));
	const int turns = (b3 == -1) ? 30 : b3;
	const int iq = host_->fromMainMenu() ? 0 : host_->env().player->ce.getIQPercent();   // (:64)
	const int scrambles = (10000 * turns + turns * (100 - iq) * (100 - iq)) / 20000;
	// shortcut: 0 scrambles would loop forever on a solved board (the legacy
	// recursion); shipped turns 15..40 always give >= 7.
	fillBoard(std::max(scrambles, 1));
	confirmCursor_ = 0;
	screen_ = kScreenHelp;
	result_ = kPlaying;
	host_->help().reset();
	selected_ = false;
	turnsLeft_ = turns;
	bool found = false;
	for (int col = 0; col < columns_ && !found; ++col) {
		for (int row = 0; row < kRows; ++row) {
			if (board_[row][col] != 0) {
				selRow_ = row;
				selCol_ = col;
				found = true;
				break;
			}
		}
	}
	curX_ = curY_ = oldX_ = oldY_ = 0;
	touched_ = false;
	lastHighlight_ = -1;
	done_ = false;
	std::fprintf(stderr, "[hacking] begin cols=%d turns=%d iq=%d scrambles=%d\n",
		columns_, turns, iq, scrambles);
}

// fillGameBoardRandomly(gameBoard, 5, columnCount, 3, scrambles); the legacy
// tail recursion on a solved board is the do/while.
void HackingGame::fillBoard(int scrambles) {
	const int n = kRows;
	const int n2 = columns_;
	do {
		for (int i = 0; i < kRows; ++i) {
			for (int j = 0; j < kMaxCols; ++j) board_[i][j] = 0;
		}
		for (int k = 0; k < n2; ++k) {
			for (int l = 0; l < 3; ++l) board_[n - l - 1][k] = (short)(k + 1);
		}
		uint8_t b = 0;
		uint8_t mostRecentSrc[kMaxCols], mostRecentDest[kMaxCols];
		for (uint8_t c = 0; c < kMaxCols; ++c) {
			mostRecentSrc[c] = c;
			mostRecentDest[c] = c;
		}
		const int div = (255 + n2) / n2;
		for (int n5 = 0; n5 < scrambles; ++n5) {
			const int n6 = nextByte() / div;
			const int n7 = nextByte() / div;
			if (n6 == n7) {
				--n5;
			} else if (n6 == b) {
				--n5;
			} else if (mostRecentDest[n6] == n7 && mostRecentSrc[n7] == n6) {
				--n5;
			} else if (board_[n - 1][n6] == 0 || board_[0][n7] != 0) {
				--n5;
			} else {
				int n8;
				for (n8 = n - 1; n8 > 0 && board_[n8 - 1][n6] != 0; --n8) {}
				int n9;
				for (n9 = n - 1; board_[n9][n7] != 0; --n9) {}
				board_[n9][n7] = board_[n8][n6];
				board_[n8][n6] = 0;
				mostRecentSrc[n6] = (uint8_t)n7;
				mostRecentDest[n7] = (uint8_t)n6;
				b = (uint8_t)n7;
			}
		}
	} while (solved());
#ifndef NDEBUG
	for (int k = 1; k <= columns_; ++k) {
		int count = 0;
		for (int i = 0; i < kRows; ++i) {
			for (int j = 0; j < kMaxCols; ++j) count += board_[i][j] == k;
		}
		assert(count == 3);
	}
#endif
}

bool HackingGame::solved() const {
	for (int i = 0; i < kRows; ++i) {
		for (int j = 0; j < kMaxCols; ++j) {
			if (board_[i][j] != 0 && board_[i][j] != j + 1) return false;
		}
	}
	return true;
}

void HackingGame::attemptToMove(int n) {
	const int col = (n + columns_) % columns_;
	if (selCol_ == col) return;
	int n2 = -1;
	for (int i = 4; i >= 0; --i) {
		if (board_[i][col] == 0) {
			n2 = i;
			break;
		}
	}
	if (selected_) {
		int n3, n4;
		for (n3 = col - selCol_, n4 = (col + n3 + columns_) % columns_;
			n4 != selCol_ && n2 == -1; n4 = (n4 + n3 + columns_) % columns_) {
			for (int j = 4; j >= 0; --j) {
				if (board_[j][n4] == 0) {
					n2 = j;
					break;
				}
			}
		}
		const int dst = (n4 - n3 + columns_) % columns_;
		if (n2 != -1) {
			board_[n2][dst] = board_[selRow_][selCol_];
			board_[selRow_][selCol_] = 0;
			selRow_ = n2;
			selCol_ = dst;
		} else {
			selected_ = false;
		}
	} else {
		if (n2 != 4) ++n2;
		selRow_ = n2;
		selCol_ = col;
	}
}

void HackingGame::handleAction(Action a) {
	if (done_) return;
	if (screen_ == kScreenHelp) {
		if (a == Action::Menu || a == Action::BackKey) {
			endGame(result_ == kPlaying ? 2 : result_ == kWon ? 1 : 0);
		} else if (a == Action::Use) {
			screen_ = kScreenGame;
		} else if (a == Action::Forward) {
			host_->help().scroll(-1);
		} else if (a == Action::Back) {
			host_->help().scroll(1);
		} else if (a == Action::TurnRight) {
			host_->help().scroll(16);
		} else if (a == Action::TurnLeft) {
			host_->help().scroll(-16);
		}
	} else if (screen_ == kScreenGame) {
		if (result_ == kPlaying) {
			if (a == Action::TurnLeft || a == Action::TurnRight) {
				std::fprintf(stderr, "[sound] 1027\n");
				attemptToMove(a == Action::TurnLeft ? selCol_ - 1 : selCol_ + 1);
			} else if (a == Action::Automap || a == Action::BackKey) {
				screen_ = kScreenHelp;
				host_->help().reset();
			} else if (a == Action::Menu) {
				screen_ = kScreenConfirm;
			} else if (a == Action::Use) {
				if (selected_) {
					selected_ = false;
					if (cameFrom_ != selCol_) --turnsLeft_;
					if (solved()) result_ = kWon;
					else if (turnsLeft_ == 0) result_ = kLost;
					std::fprintf(stderr, "[hacking] drop col=%d turns=%d result=%d\n",
						selCol_, turnsLeft_, result_);
				} else if (board_[selRow_][selCol_] != 0) {
					selected_ = true;
					cameFrom_ = selCol_;
				}
			}
		} else if (a == Action::Use || a == Action::Automap) {
			endGame(result_ == kWon ? 1 : 0);
		}
	} else {
		if (a == Action::Use) {
			if (confirmCursor_ == 1) endGame(2);
			else screen_ = kScreenGame;
		} else if (a == Action::TurnLeft || a == Action::TurnRight) {
			confirmCursor_ ^= 1;
		} else if (a == Action::BackKey) {
			screen_ = kScreenGame;
		}
	}
}

void HackingGame::handleTouch(int buttonId) {
	if (done_) return;
	if (buttonId >= kColumnTapBase) {   // drag left a column (ADR 0038 point 3)
		attemptToMove(buttonId - kColumnTapBase);
		handleAction(Action::Use);
		return;
	}
	if (screen_ == kScreenHelp) {
		if (buttonId == kButtonExit.id) {
			handleAction(Action::Menu);
		} else {
			curX_ = curY_ = oldX_ = oldY_ = 0;
			handleAction(Action::Use);
		}
		return;
	}
	if (screen_ == kScreenGame) {
		if (result_ != kPlaying) handleAction(Action::Use);
		else if (buttonId == kButtonExit.id) handleAction(Action::Menu);
		else if (buttonId == kButtonBack.id) handleAction(Action::BackKey);
	}
	releasePass();
}

void HackingGame::releasePass() {
	if (done_ || screen_ == kScreenHelp) return;
	for (int i = 0; i < columns_ && !done_; ++i) {
		if (columnRect(i).contains(oldX_, oldY_)) {
			touchedColumn_ = -1;
			attemptToMove(i);
			handleAction(Action::Use);
			oldX_ = oldY_ = 0;
		}
	}
	if (done_ || screen_ != kScreenConfirm) return;
	if (kYesRect.contains(curX_, curY_)) {
		confirmCursor_ = 1;
		handleAction(Action::Use);
		curX_ = curY_ = 0;
	}
	if (done_ || screen_ != kScreenConfirm) return;
	if (kNoRect.contains(curX_, curY_)) {
		confirmCursor_ = 0;
		handleAction(Action::Use);
		curX_ = curY_ = 0;
	}
}

void HackingGame::endGame(int n) {
	done_ = true;
	std::fprintf(stderr, "[sound] %d\n", n == 1 ? 1043 : 1040);
	if (!host_->fromMainMenu()) host_->evaluateResults(n);   // (:623-626)
	host_->finish(n);
}

// ---- frame ----

void HackingGame::compose(int type, int index, Text& out) const {
	const Localization* loc = host_->env().loc;
	if (loc != nullptr) out.append(loc->get(type, index));
}

UiResult HackingGame::draw(Ui& ui) {
	const UiInput& in = ui.in();
	const MiniGameHost::Env& e = host_->env();
	const int64_t now = *e.upTimeMs;

	// touchStart / touchMove / touchEnd samples (:638-659).
	if (in.cursorValid) {
		if (in.pressed) {
			touched_ = true;
			curX_ = oldX_ = in.cursorX;
			curY_ = oldY_ = in.cursorY;
		} else if (touched_ && in.down && (in.cursorX != curX_ || in.cursorY != curY_)) {
			oldX_ = curX_;
			oldY_ = curY_;
			curX_ = in.cursorX;
			curY_ = in.cursorY;
		}
		if (in.released) {
			touched_ = false;
			curX_ = in.cursorX;
			curY_ = in.cursorY;
		}
	} else if (in.released) {
		touched_ = false;
	}

	model_ = HackingModel{};
	exitLabel_.setLength(0);
	compose(kTextMain, kStrExit, exitLabel_);
	model_.exitLabel = &exitLabel_;
	model_.buttons[0] = kButtonExit;
	model_.buttons[1] = kButtonBack;
	model_.buttonCount = 2;

	// fmButton::SetHighlighted plays sound 1027 on the highlight edge
	// (src/Button.cpp:145-155).
	const int hl = highlightedButtonId(model_.buttons, model_.buttonCount, in);
	if (hl != -1 && hl != lastHighlight_) std::fprintf(stderr, "[sound] 1027\n");
	lastHighlight_ = hl;

	if (screen_ == kScreenHelp) {
		model_.screen = HackingModel::Screen::Help;
		host_->help().buildModel(kStrHelpTitle, kStrHelpBody, model_.help);
		model_.help.background = &host_->assets().helpBackground;
	} else {
		model_.screen = screen_ == kScreenGame ? HackingModel::Screen::Game
			: HackingModel::Screen::Confirm;
		model_.columns = columns_;
		for (int i = 0; i < kRows; ++i) {
			for (int j = 0; j < kMaxCols; ++j) model_.board[i][j] = board_[i][j];
		}
		model_.selRow = selRow_;
		model_.selCol = selCol_;
		model_.carrying = selected_;
		turns_.setLength(0);
		compose(kTextMain, kStrTurns, turns_);
		turns_.dehyphenate();
		turns_.append(turnsLeft_);
		model_.turns = &turns_;
		goal_.setLength(0);
		compose(kTextMain, kStrGoal, goal_);
		goal_.dehyphenate();
		model_.goal = &goal_;
		// drawGamePieces pulse (:566-572); the second branch is the portable
		// spelling of the legacy flooring (-95*(t-511) >> 9) + 255.
		const int t = (int)(now & 0x3FF);
		const int g = (t < 511) ? 160 + ((95 * t) >> 9) : 255 - ((95 * (t - 511) + 511) >> 9);
		model_.selColor = 0xFF000000u | (uint32_t)g * 0x010101u;
		prompt_.setLength(0);
		if (screen_ == kScreenGame) {
			compose(kTextMain, result_ == kWon ? kStrPromptWon
				: result_ == kLost ? kStrPromptLost : kStrPromptPlaying, prompt_);
			backLabel_.setLength(0);
			compose(kTextIngame2, kStrBack, backLabel_);
			model_.backLabel = &backLabel_;
		} else {
			compose(kTextMain, kStrPromptConfirm, prompt_);
			yes_.setLength(0);
			compose(kTextIngame2, kStrYes, yes_);
			no_.setLength(0);
			compose(kTextIngame2, kStrNo, no_);
			model_.yesLabel = &yes_;
			model_.noLabel = &no_;
			model_.yesHeld = touched_ && kYesRect.contains(curX_, curY_);
			model_.noHeld = touched_ && kNoRect.contains(curX_, curY_);
			int osc = 0;
			if (e.tables != nullptr && e.tables->oscCycle.size() >= 4) {
				osc = e.tables->oscCycle[(size_t)((now / 100) % 4)];
			}
			model_.cursorX = (confirmCursor_ == 1 ? 136 : 252) + osc;
		}
		prompt_.wrapText(47, '\n');
		model_.prompt = &prompt_;
	}

	// Hover and drag-leave (:382-400); the release half is releasePass.
	UiResult columnTap;
	if (touched_ && screen_ != kScreenHelp) {
		for (int i = 0; i < columns_; ++i) {
			if (columnRect(i).contains(curX_, curY_)) {
				model_.hoverColumn = i;
				if (i != touchedColumn_) {
					touchedColumn_ = i;
					std::fprintf(stderr, "[sound] 1027\n");   // soundClick
				}
			} else if (columnRect(i).contains(oldX_, oldY_)) {
				touchedColumn_ = -1;
				oldX_ = oldY_ = 0;
				columnTap = UiResult{ UiAction::MiniGameTouch, kColumnTapBase + i };
			}
		}
	}

	const UiResult r = drawHacking(ui, model_, assets_);
	return columnTap.action != UiAction::None ? columnTap : r;
}

} // namespace newcore
