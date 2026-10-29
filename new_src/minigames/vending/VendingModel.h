#ifndef NEW_MINIGAMES_VENDING_VENDINGMODEL_H
#define NEW_MINIGAMES_VENDING_VENDINGMODEL_H

#include <cstdint>

#include "minigames/common/MiniGameButtons.h"
#include "minigames/common/MiniGameHelpModel.h"

namespace newcore {

class Text;

// Per-frame view model of the vending minigame, filled by VendingMachine::draw.
// Positions are the session's: it needs them itself for the press and drag
// checks the legacy makes while drawing (src/VendingMachine.cpp:469-600,
// :733-803).
struct VendingModel {
	enum class Screen { Help, Game, Terminal };
	Screen screen = Screen::Terminal;

	// ---- terminal ----
	const Text* title = nullptr;     // Menu 344 + divider, at (SCR_CX+1, 14) anchor 1
	const Text* credits = nullptr;   // Code 197, at (SCR_CX+1, 30) anchor 17
	const Text* prompt = nullptr;    // terminal: wrapped at 26; game: 207/208 at (240, 308)
	const Text* buttonLabel[2] = { nullptr, nullptr };
	int buttonX[2] = { 0, 0 };       // vending_button_small left edges
	int buttonTop = 0;               // posY - h/2
	int buttonLabelY = 0;            // posY
	bool buttonHeld[2] = { false, false };   // renderMode 0 instead of 1 (BLEND25)
	int cursorX = 0, cursorY = 0;    // OSC_CYCLE wobble already applied
	bool softkeyPressed = false;
	int softkeyX = 0, softkeyY = 0;  // top-left of the soft-key image
	int softkeyLabelX = 0, softkeyLabelY = 0;

	// ---- help ----
	MiniGameHelpModel help;

	// ---- hack game (drawGameScreen, :634-686) ----
	struct Column {
		int x = 0;                   // left edge; the top-bar box i shares it
		int minimum = 0, maximum = 9;
		int chevronOffset = 0;       // chevronAnimationOffset
		int slider = 0;
		bool selected = false;       // gameCursor2 == i: pulsing double rect
		bool dragged = false;        // yellow column outline (:752)
	};
	Column columns[4];
	int sumBoxX = 0;                 // top-bar box 4 (correctSum)
	int boxStep = 0;                 // widthOfContainingBox
	int topValues[4] = { 0, 0, 0, 0 };   // playersGuess, -1 = blank
	int correctSum = 0;
	uint32_t pulseColor = 0;         // 0xFF000000 | I<<16 | I<<8
	const Text* result = nullptr;    // drawGameResults text, null while editing
	const Text* backLabel = nullptr; // Menu 80 at (35, 302)

	const Text* exitLabel = nullptr; // Code 30: terminal soft key, help/game (445, 302)

	MiniGameButton buttons[12];
	int buttonCount = 0;
};

} // namespace newcore

#endif // NEW_MINIGAMES_VENDING_VENDINGMODEL_H
