#ifndef NEW_MINIGAMES_VENDING_VENDINGMACHINE_H
#define NEW_MINIGAMES_VENDING_VENDINGMACHINE_H

#include <cstdint>

#include "minigames/common/MiniGame.h"
#include "minigames/vending/VendingAssets.h"
#include "minigames/vending/VendingModel.h"
#include "text/Text.h"
#include "ui/UiAssets.h"

namespace newcore {

class MiniGameHost;
class TextureStore;

// The vending machine minigame session (src/VendingMachine.cpp), world
// branch only: no playFromMainMenu, no forceWin (ADR 0033 point 7).
class VendingMachine : public MiniGame {
public:
	void init(MiniGameHost* host) { host_ = host; }
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);

	// initGame(loadMapID, (loadMapID-1)*2 + b1) (src/ScriptThread.cpp:1306,
	// src/VendingMachine.cpp:39-116). b2/b3 are unused (scripts pass -1).
	void begin(int b1, int b2, int b3) override;
	void handleAction(Action a) override;      // handleInput (:172-187)
	void handleTouch(int buttonId) override;   // touchEnd (:879-895)
	UiResult draw(Ui& ui) override;            // updateGame (:393-419)

	void endGame(int n);                       // (:850-864) -> MiniGameHost::finish

	// Menu API for the vending menus 83-87 (src/VendingMachine.cpp:118-130,
	// :805-827).
	bool drinkInThisVendingMachine(int n) const;
	short getDrinkPrice(int n) const;
	int getSnackPrice() const { return machineHasBeenHacked_ ? 8 : 10; }
	// False when the credits cannot pay price * quantity; else per unit
	// credits -= price, give(0, n, 1) (:813-823).
	bool buyDrink(int n);
	int currentItemQuantity = 1;   // written by the menu (action 28, UP/DOWN on 86)
	int currentItemPrice = 0;      // written by initMenu 86
	// Menu 83 MENU/BACK: back to the terminal, or out when it cannot be hacked.
	void returnFromBuying();

private:
	// screen_ values = legacy stateVars[1] (:172-187).
	static constexpr int kScreenHelp = 0, kScreenGame = 1, kScreenTerminal = 2;
	// Pseudo button for the terminal's Exit soft-key image (spec §6.3); not
	// one of the legacy fmButtons 0-10.
	static constexpr int kTerminalExit = 11;
	// ((W - 97) / 4) + 1 at W = 480 (:66-73).
	static constexpr int kWidthOfContainingBox = (480 - 97) / 4 + 1;

	bool machineCanBeHacked() const;           // (:132-134)
	void randomizeGame();                      // (:136-170)
	int numbersCorrect() const;                // (:829-837)
	void updateHighLowState();                 // (:377-391)
	void handleInputForBasicVendingMachine(Action a);   // (:189-225)
	void handleInputForHelpScreen(Action a);            // (:227-260)
	void handleInputForGame(Action a);                  // (:262-375)
	void handleTouchForGame(int buttonId);              // (:906-975)
	void buildTerminal(Ui& ui);                // drawMainScreen's state half (:469-600)
	void buildHelp();                          // drawHelpScreen (:620-632)
	void buildGame(Ui& ui);                    // drawGameScreen's state half (:634-803)
	void buildGameResult();                    // drawGameResults' text (:421-458)
	void compose(int type, int index, Text& out) const;   // appends

	MiniGameHost* host_ = nullptr;
	VendingAssets assets_;
	VendingModel model_;

	int screen_ = kScreenTerminal;   // stateVars[1]
	int result_ = 0;                 // stateVars[2]: 0 editing, 1 won, 2 wrong
	int64_t chevronClock_ = 0;       // stateVars[3]
	int mainTerminalCursor_ = 0;
	int gameCursor_ = 0;             // next digit to submit
	int gameCursor2_ = 0;            // [GEC] selected column for UP/DOWN
	int lastHighlight_ = -1;         // fmButton highlight edge -> click sound
	int solution_[4] = { 0 };
	int sliderPositions_[4] = { 0 };
	int playersGuess_[4] = { 0 };
	int minimums_[4] = { 0 };
	int maximums_[4] = { 0 };
	int chevronAnimationOffset_[4] = { 0 };
	int correctSum_ = 0;
	int triesLeft_ = 0;
	int currentMapNumber_ = 1;
	int currentMachineNumber_ = 1;
	bool machineHasBeenHacked_ = false;
	bool machineJustHacked_ = false;
	int iqBonusAwarded_ = 0;

	Text title_, credits_, prompt_, buttonLabel_[2], exitLabel_, backLabel_, resultText_;
};

} // namespace newcore

#endif // NEW_MINIGAMES_VENDING_VENDINGMACHINE_H
