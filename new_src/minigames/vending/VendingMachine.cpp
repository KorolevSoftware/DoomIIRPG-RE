#include "minigames/vending/VendingMachine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/ItemPickup.h"
#include "domain/game/Player.h"
#include "io/Localization.h"
#include "io/MenuData.h"
#include "io/Tables.h"
#include "minigames/common/MiniGameHost.h"
#include "minigames/vending/VendingView.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

// Legacy button container ids 9/10 (src/VendingMachine.cpp:106-110).
constexpr MiniGameButton kButtonBack{ 9, UiRect{ 0, 290, 54, 30 } };
constexpr MiniGameButton kButtonExit{ 10, UiRect{ 428, 290, 54, 30 } };

constexpr int kStrCredits = 197;        // Code "Credits: %01"
constexpr int kStrPromptHack = 198;     // "What would you like to do?"
constexpr int kStrPromptBuy = 199;      // "Would you like to buy?"
constexpr int kStrPricesDropped = 200;
constexpr int kStrBuy = 202, kStrHack = 203, kStrYes = 141, kStrNo = 140;
constexpr int kStrExit = 30;
constexpr int kStrHelpTitle = 195, kStrHelpBody = 196;
constexpr int kStrVendingTitle = 344;   // MenuStrings::VENDING_MACHINE_TITLE
constexpr int kStrBack = 80;            // MenuStrings "Back"
constexpr int kStrHackFailed = 201;     // "Hack failed!|Initiating security protocols. "
constexpr int kStrPressOk = 207;
constexpr int kStrTapToContinue = 208;
constexpr int kStrWrongCode = 209;      // "Wrong code!|Tries left: "
constexpr int kStrHackIqBonus = 236;    // "Hack Successful!|+%01 to I.Q."
constexpr int kStrHackIqMax = 237;

// fmButton-style exclusive containment of drawMainScreen's press checks
// (:516-519, :535-538, :576-579).
bool insideExclusive(int px, int py, int x, int y0, int w, int y1) {
	return px > x && px < x + w && py > y0 && py < y1;
}

} // namespace

bool VendingMachine::loadAssets(TextureStore& store, const UiAssets::ResourceReader& read) {
	return assets_.load(store, read);
}

void VendingMachine::begin(int b1, int /*b2*/, int /*b3*/) {
	const MiniGameHost::Env& e = host_->env();
	const int loadMapID = e.game->combat.loadMapID;
	screen_ = kScreenTerminal;
	result_ = 0;
	chevronClock_ = *e.upTimeMs;
	host_->help().reset();
	machineJustHacked_ = false;
	currentMapNumber_ = std::max(std::min(loadMapID, 9), 1);
	currentMachineNumber_ = std::max(std::min((loadMapID - 1) * 2 + b1, 18), 1);
	triesLeft_ = e.player->getVendingMachineTriesLeft(currentMachineNumber_);
	machineHasBeenHacked_ = e.player->vendingMachineIsHacked(currentMachineNumber_);
	correctSum_ = 0;
	if (machineCanBeHacked()) randomizeGame();
	mainTerminalCursor_ = 0;
	gameCursor_ = 0;
	iqBonusAwarded_ = 0;
	gameCursor2_ = 0;
	lastHighlight_ = -1;
	std::fprintf(stderr, "[vending] begin machine=%d map=%d tries=%d hacked=%d\n",
		currentMachineNumber_, currentMapNumber_, triesLeft_, (int)machineHasBeenHacked_);
}

bool VendingMachine::machineCanBeHacked() const {
	return !machineHasBeenHacked_ && triesLeft_ > 0;
}

void VendingMachine::randomizeGame() {
	correctSum_ = 0;
	for (int i = 0; i < 4; ++i) {
		solution_[i] = ItemPickup::nextInt() % 9;
		sliderPositions_[i] = 5;
		correctSum_ += solution_[i];
	}
	for (int j = 0; j < 4; ++j) {
		playersGuess_[j] = sliderPositions_[j];
		minimums_[j] = 0;
		maximums_[j] = 9;
		chevronAnimationOffset_[j] = 0;
	}
	const int n = host_->env().player->ce.getIQPercent();
	int k;
	if (n >= 80) k = 3;
	else if (n >= 50) k = 2;
	else if (n >= 20) k = 1;
	else k = 0;
	while (k > numbersCorrect()) {
		const int n2 = ItemPickup::nextInt() % 4;
		correctSum_ -= solution_[n2] - 5;
		solution_[n2] = 5;
	}
	machineHasBeenHacked_ = false;
}

int VendingMachine::numbersCorrect() const {
	int n = 0;
	for (int i = 0; i < 4; ++i) {
		if (sliderPositions_[i] == solution_[i]) ++n;
	}
	return n;
}

void VendingMachine::updateHighLowState() {
	for (int i = 0; i < 4; ++i) {
		const int n = sliderPositions_[i] - solution_[i];
		if (n > 0) {
			maximums_[i] = std::min(sliderPositions_[i] - 1, maximums_[i]);
		} else if (n < 0) {
			minimums_[i] = std::max(sliderPositions_[i] + 1, minimums_[i]);
		} else {
			maximums_[i] = sliderPositions_[i];
			minimums_[i] = sliderPositions_[i];
		}
	}
}

void VendingMachine::endGame(int n) {
	host_->finish(n);
}

// energyDrinkData = tables.bin table 10: [price, hackedPrice, mapMask] per drink.
bool VendingMachine::drinkInThisVendingMachine(int n) const {
	const Tables* t = host_->env().tables;
	if (t == nullptr || (size_t)(n * 3 + 2) >= t->energyDrinkData.size()) return false;
	return (t->energyDrinkData[(size_t)(n * 3 + 2)] & (1 << currentMapNumber_)) != 0;
}

short VendingMachine::getDrinkPrice(int n) const {
	const Tables* t = host_->env().tables;
	const size_t k = (size_t)(n * 3 + (machineHasBeenHacked_ ? 1 : 0));
	if (t == nullptr || k >= t->energyDrinkData.size()) return 0;
	return t->energyDrinkData[k];
}

bool VendingMachine::buyDrink(int n) {
	Player* p = host_->env().player;
	if (p->inventory[Enums::INV_ONE_UAC_CREDIT] < currentItemPrice * currentItemQuantity) return false;
	for (int i = 0; i < currentItemQuantity; ++i) {
		p->inventory[Enums::INV_ONE_UAC_CREDIT] -= (int16_t)currentItemPrice;
		p->give(0, n, 1, false);
	}
	return true;
}

// The legacy setState zeroes stateVars[3] (the chevron clock) and this does
// not restore it (src/Canvas.cpp:1024-1026).
void VendingMachine::returnFromBuying() {
	host_->env().host->requestState(StateId::MiniGame);
	screen_ = kScreenTerminal;
	result_ = 0;
	machineJustHacked_ = false;
	chevronClock_ = 0;
	if (!machineCanBeHacked()) endGame(2);
}

// ---- input ----

void VendingMachine::handleAction(Action a) {
	switch (screen_) {
	case kScreenGame: handleInputForGame(a); break;
	case kScreenTerminal: handleInputForBasicVendingMachine(a); break;
	case kScreenHelp: handleInputForHelpScreen(a); break;
	}
}

// The hackable and non-hackable branches differ only in FIRE on cursor 1.
void VendingMachine::handleInputForBasicVendingMachine(Action a) {
	if (a == Action::Menu || a == Action::BackKey) {
		endGame(2);
	} else if (a == Action::TurnLeft || a == Action::TurnRight) {
		++mainTerminalCursor_;
		mainTerminalCursor_ %= 2;
	} else if (a == Action::Use) {
		if (mainTerminalCursor_ == 0) {
			host_->env().openMenu(kMenuVending);   // setMenu(MENU_VENDING_MACHINE) (:199-201)
		} else if (mainTerminalCursor_ == 1) {
			if (machineCanBeHacked()) {
				screen_ = kScreenHelp;
				host_->help().reset();
			} else {
				endGame(2);
			}
		}
	}
}

void VendingMachine::handleInputForHelpScreen(Action a) {
	switch (a) {
	case Action::Automap: endGame(2); break;
	case Action::Menu:
	case Action::BackKey: screen_ = kScreenTerminal; break;
	case Action::Use: screen_ = kScreenGame; break;
	case Action::Forward: host_->help().scroll(-1); break;
	case Action::Back: host_->help().scroll(1); break;
	case Action::TurnRight: host_->help().scroll(16); break;
	case Action::TurnLeft: host_->help().scroll(-16); break;
	default: break;
	}
}

// Result 1 (won) / 2 (wrong) branches first, then editing (:262-375). The
// click sounds are [GEC] additions kept from the port.
void VendingMachine::handleInputForGame(Action a) {
	Player* player = host_->env().player;
	if (result_ == 2) {
		if (a == Action::Use) {
			if (triesLeft_ == 0) {
				screen_ = kScreenTerminal;
			} else {
				gameCursor_ = 0;
				for (int i = 0; i < 4; ++i) {
					if (playersGuess_[i] != solution_[i]) playersGuess_[i] = -1;
				}
				while (playersGuess_[gameCursor_] != -1) ++gameCursor_;
				result_ = 0;
			}
		} else if (a == Action::Menu) {
			endGame(result_);
		} else if (a == Action::Automap || a == Action::BackKey) {
			if (triesLeft_ != 0) {
				screen_ = kScreenHelp;
				host_->help().reset();
			}
		}
	} else if (result_ == 1) {
		if (a == Action::Use) {
			screen_ = kScreenTerminal;
			machineJustHacked_ = true;
			machineHasBeenHacked_ = true;
			player->setVendingMachineHack(currentMachineNumber_);
		} else if (a == Action::Menu) {
			endGame(result_);   // without persisting the hack bit (:311-313)
		}
	} else if (a == Action::Menu) {
		endGame(2);
	} else if (a == Action::Automap || a == Action::BackKey) {
		screen_ = kScreenHelp;
		host_->help().reset();
	} else if (a == Action::Use) {
		playersGuess_[gameCursor_] = sliderPositions_[gameCursor_];
		++gameCursor_;
		while (gameCursor_ < 4 && playersGuess_[gameCursor_] != -1) ++gameCursor_;
		if (gameCursor_ == 4) {
			updateHighLowState();
			--triesLeft_;
			if (numbersCorrect() == 4) {
				std::fprintf(stderr, "[sound] 1043\n");
				result_ = 1;
				machineHasBeenHacked_ = true;
				iqBonusAwarded_ = player->modifyStat(Enums::STAT_IQ, 2);   // MINIGAME_IQ_BONUS
			} else {
				std::fprintf(stderr, "[sound] 1040\n");
				player->removeOneVendingMachineTry(currentMachineNumber_);
				result_ = 2;
			}
			std::fprintf(stderr, "[vending] submit %d%d%d%d -> %s, tries=%d\n",
				sliderPositions_[0], sliderPositions_[1], sliderPositions_[2],
				sliderPositions_[3], result_ == 1 ? "won" : "wrong", triesLeft_);
		}
	} else if (a == Action::Forward) {
		std::fprintf(stderr, "[sound] 1027\n");
		if (sliderPositions_[gameCursor2_] < 9) {
			++sliderPositions_[gameCursor2_];
			playersGuess_[gameCursor2_] = sliderPositions_[gameCursor2_];
		}
	} else if (a == Action::Back) {
		std::fprintf(stderr, "[sound] 1027\n");
		if (sliderPositions_[gameCursor2_] > 0) {
			--sliderPositions_[gameCursor2_];
			playersGuess_[gameCursor2_] = sliderPositions_[gameCursor2_];
		}
	} else if (a == Action::TurnLeft) {
		std::fprintf(stderr, "[sound] 1027\n");
		gameCursor2_ = (gameCursor2_ + 3) % 4;
	} else if (a == Action::TurnRight) {
		std::fprintf(stderr, "[sound] 1027\n");
		gameCursor2_ = (gameCursor2_ + 1) % 4;
	}
}

// Results: any tap is FIRE; 0 FIRE, 9 BACK, 10 MENU; 1..8 move slider
// (id-1)/2 up (odd) or down (even), clamped to 0..9.
void VendingMachine::handleTouchForGame(int buttonId) {
	if (result_ >= 1 && result_ <= 2) {
		handleInputForGame(Action::Use);
		return;
	}
	switch (buttonId) {
	case 0: handleInputForGame(Action::Use); return;
	case 9: handleInputForGame(Action::BackKey); return;
	case 10: handleInputForGame(Action::Menu); return;
	default: break;
	}
	if (buttonId >= 1 && buttonId <= 8) {
		const int col = (buttonId - 1) / 2;
		gameCursor2_ = col;
		if (buttonId % 2 == 1) {
			if (++sliderPositions_[col] >= 9) sliderPositions_[col] = 9;
		} else {
			if (--sliderPositions_[col] < 0) sliderPositions_[col] = 0;
		}
	}
}

void VendingMachine::handleTouch(int buttonId) {
	switch (screen_) {
	case kScreenGame: handleTouchForGame(buttonId); break;
	case kScreenTerminal:   // handleTouchForBasicVendingMachine (:977-992)
		if (buttonId == kButtonBack.id || buttonId == kTerminalExit) handleAction(Action::BackKey);
		else if (buttonId == kButtonExit.id) handleAction(Action::Menu);
		else handleAction(Action::Use);
		break;
	case kScreenHelp:       // handleTouchForHelpScreen (:897-904)
		handleAction(buttonId == kButtonExit.id ? Action::BackKey : Action::Use);
		break;
	}
}

// ---- frame ----

void VendingMachine::compose(int type, int index, Text& out) const {
	const Localization* loc = host_->env().loc;
	if (loc != nullptr) out.append(loc->get(type, index));
}

UiResult VendingMachine::draw(Ui& ui) {
	const int64_t now = *host_->env().upTimeMs;
	if (now - chevronClock_ > 100) {
		chevronClock_ = now;
		for (int i = 0; i < 4; ++i) {
			++chevronAnimationOffset_[i];
			chevronAnimationOffset_[i] %= 11;
		}
	}
	model_ = VendingModel{};
	exitLabel_.setLength(0);
	compose(kTextMain, kStrExit, exitLabel_);
	model_.exitLabel = &exitLabel_;
	switch (screen_) {
	case kScreenGame: buildGame(ui); break;
	case kScreenTerminal: buildTerminal(ui); break;
	case kScreenHelp: buildHelp(); break;
	}
	// fmButton::SetHighlighted plays the button's sound 1027 on the edge into
	// highlighted (src/Button.cpp:145-159); the soft-key pseudo button has none.
	const int hl = highlightedButtonId(model_.buttons, model_.buttonCount, ui.in());
	if (hl != -1 && hl != kTerminalExit && hl != lastHighlight_) {
		std::fprintf(stderr, "[sound] 1027\n");
	}
	lastHighlight_ = hl;
	return drawVending(ui, model_, assets_);
}

void VendingMachine::buildGame(Ui& ui) {
	const UiInput& in = ui.in();
	const bool touched = in.down && in.cursorValid;
	model_.screen = VendingModel::Screen::Game;

	for (int i = 0; i < 4; ++i) playersGuess_[i] = sliderPositions_[i];   // (:637-639)
	// The top bar is drawn before the middle bar's drag, so it shows the
	// sliders as they were at the start of the frame.
	for (int i = 0; i < 4; ++i) model_.topValues[i] = playersGuess_[i];
	model_.correctSum = correctSum_;
	model_.boxStep = kWidthOfContainingBox;
	model_.sumBoxX = 32 + 4 * kWidthOfContainingBox;

	const int now = (int)(*host_->env().upTimeMs % 2000);
	const int intensity = 187 + std::abs(1000 - now) * 68 / 1000;
	model_.pulseColor = 0xFF000000u | ((uint32_t)intensity << 16) | ((uint32_t)intensity << 8);

	const int sw = assets_.submit.width();
	const int sh = assets_.submit.height();
	model_.buttons[0] = MiniGameButton{ 0, UiRect{ 400, 160 - sh / 2, sw, sh } };   // (:83)
	int x = 32;   // screenRect[0] + 32
	for (int i = 0; i < 4; ++i) {
		// Arrow touch areas use the submit image's size (:741-748).
		model_.buttons[1 + 2 * i] = MiniGameButton{ 1 + 2 * i,
			UiRect{ x + 16 - sw / 2 - 2, 104 - sh + 5, sw, sh } };
		model_.buttons[2 + 2 * i] = MiniGameButton{ 2 + 2 * i,
			UiRect{ x + 16 - sw / 2 - 2, 213, sw, sh } };

		VendingModel::Column& c = model_.columns[i];
		if (touched && in.cursorX > x + 5 && in.cursorX < x + 27 &&
		    in.cursorY > 104 && in.cursorY < 214) {   // column drag (:750-765)
			c.dragged = true;
			gameCursor_ = i;
			sliderPositions_[i] = std::max(0, std::min((214 - in.cursorY) / 11, 9));
		}
		c.x = x;
		c.minimum = minimums_[i];
		c.maximum = maximums_[i];
		c.chevronOffset = chevronAnimationOffset_[i];
		c.slider = sliderPositions_[i];
		c.selected = gameCursor2_ == i;
		x += kWidthOfContainingBox;
	}
	model_.buttons[9] = kButtonBack;
	model_.buttons[10] = kButtonExit;
	model_.buttonCount = 11;

	prompt_.setLength(0);
	if (result_ != 0) {
		buildGameResult();
		model_.result = &resultText_;
		compose(kTextMain, kStrTapToContinue, prompt_);
	} else {
		compose(kTextMain, kStrPressOk, prompt_);
	}
	prompt_.dehyphenate();
	model_.prompt = &prompt_;
	backLabel_.setLength(0);
	compose(kTextIngame2, kStrBack, backLabel_);
	model_.backLabel = &backLabel_;
}

// drawGameResults' text (:421-458), world branch.
void VendingMachine::buildGameResult() {
	resultText_.setLength(0);
	if (machineHasBeenHacked_) {
		if (iqBonusAwarded_ > 0) {
			const Localization* loc = host_->env().loc;
			std::string s = loc != nullptr ? loc->get(kTextMain, kStrHackIqBonus) : std::string();
			const std::string arg = std::to_string(iqBonusAwarded_);
			composeArgs(s, &arg, 1);
			resultText_.append(s);
		} else {
			compose(kTextMain, kStrHackIqMax, resultText_);
		}
	} else if (triesLeft_ > 0) {
		compose(kTextMain, kStrWrongCode, resultText_);
		resultText_.append(triesLeft_);
	} else {
		compose(kTextMain, kStrHackFailed, resultText_);
	}
	resultText_.wrapText(20, '\n');
}

void VendingMachine::buildTerminal(Ui& ui) {
	const MiniGameHost::Env& e = host_->env();
	const UiInput& in = ui.in();
	const bool touched = in.down && in.cursorValid;
	const bool hackable = machineCanBeHacked();
	model_.screen = VendingModel::Screen::Terminal;

	title_.setLength(0);
	compose(kTextIngame2, kStrVendingTitle, title_);
	title_.dehyphenate();
	buildDivider(title_, title_.length() + 10);
	model_.title = &title_;

	std::string credits = e.loc != nullptr ? e.loc->get(kTextMain, kStrCredits) : std::string();
	const std::string arg = std::to_string(e.player->inventory[24]);   // INV_ONE_UAC_CREDIT
	composeArgs(credits, &arg, 1);
	credits_.setLength(0);
	credits_.append(credits);
	credits_.dehyphenate();
	model_.credits = &credits_;

	prompt_.setLength(0);
	if (hackable) {
		compose(kTextMain, kStrPromptHack, prompt_);
	} else if (machineJustHacked_) {
		compose(kTextMain, kStrPricesDropped, prompt_);
		compose(kTextMain, kStrPromptBuy, prompt_);
	} else {
		compose(kTextMain, kStrPromptBuy, prompt_);
	}
	prompt_.wrapText(26, '\n');
	model_.prompt = &prompt_;

	const int oldCursor = mainTerminalCursor_;
	const int posY = 16 * prompt_.getNumLines() + 95;
	const int width = assets_.buttonSmall.width();
	const int height = assets_.buttonSmall.height() / 2;
	model_.buttonTop = posY - height;
	model_.buttonLabelY = posY;
	const int labelIds[2] = { hackable ? kStrBuy : kStrYes, hackable ? kStrHack : kStrNo };
	int labelCenterX[2] = { 0, 0 };
	for (int i = 0; i < 2; ++i) {
		const int posX = i == 0 ? 112 : width + 135;
		model_.buttonX[i] = posX;
		if (touched && insideExclusive(in.cursorX, in.cursorY, posX, posY - height, width, posY + height)) {
			mainTerminalCursor_ = i;
			model_.buttonHeld[i] = true;
		}
		buttonLabel_[i].setLength(0);
		compose(kTextMain, labelIds[i], buttonLabel_[i]);
		buttonLabel_[i].dehyphenate();
		model_.buttonLabel[i] = &buttonLabel_[i];
		labelCenterX[i] = posX + width / 2;
	}
	// The cursor sits left of the Buy/Yes label unless cursor 1 (:543, :560-562).
	const int c = mainTerminalCursor_ == 1 ? 1 : 0;
	const int cursorX = labelCenterX[c] - buttonLabel_[c].getStringWidth() / 2 - 5;
	int osc = 0;
	if (e.tables != nullptr && e.tables->oscCycle.size() >= 4) {
		osc = e.tables->oscCycle[(size_t)((*e.upTimeMs / 100) % 4)];
	}
	model_.cursorX = cursorX + osc;
	model_.cursorY = posY - 8;

	// Exit soft key (:571-593). The legacy exits one frame after a release
	// inside the image; here the release hits pseudo button kTerminalExit.
	const Texture& softkey = ui.art().vendingSoftkeyNormal;
	const int sw = softkey.width();
	const int sh = softkey.height() / 2;
	if (touched && insideExclusive(in.cursorX, in.cursorY, 362, 297 - sh, sw, 297 + sh)) {
		model_.softkeyPressed = true;
		mainTerminalCursor_ = 2;
	}
	model_.softkeyX = 362;
	model_.softkeyY = 297 - sh;
	model_.softkeyLabelX = 362 + sw / 2;
	model_.softkeyLabelY = 297;

	if (oldCursor != mainTerminalCursor_ && mainTerminalCursor_ != -1) {
		std::fprintf(stderr, "[sound] 1027\n");   // MenuSystem::soundClick (src/MenuSystem.cpp:5874-5876)
	}

	model_.buttons[0] = kButtonBack;
	model_.buttons[1] = kButtonExit;
	model_.buttons[2] = MiniGameButton{ kTerminalExit, UiRect{ 362, 297 - sh, sw, 2 * sh } };
	model_.buttonCount = 3;
}

void VendingMachine::buildHelp() {
	model_.screen = VendingModel::Screen::Help;
	host_->help().buildModel(kStrHelpTitle, kStrHelpBody, model_.help);
	model_.help.background = &host_->assets().helpBackground;
	model_.buttons[0] = kButtonBack;
	model_.buttons[1] = kButtonExit;
	model_.buttonCount = 2;
}

} // namespace newcore
