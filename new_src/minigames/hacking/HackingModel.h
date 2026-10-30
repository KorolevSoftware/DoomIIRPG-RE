#ifndef NEW_MINIGAMES_HACKING_HACKINGMODEL_H
#define NEW_MINIGAMES_HACKING_HACKINGMODEL_H

#include <cstdint>

#include "minigames/common/MiniGameButtons.h"
#include "minigames/common/MiniGameHelpModel.h"

namespace newcore {

class Text;

// Per-frame view model of the hacking minigame, filled by HackingGame::draw.
// Every pointer is borrowed for that frame only.
struct HackingModel {
	enum class Screen { Help, Game, Confirm };   // legacy stateVars[1] 0 / 1 / 2
	Screen screen = Screen::Help;

	MiniGameHelpModel help;            // Help only
	const Text* exitLabel = nullptr;   // Code 30: Help and Game at (445, 302) anchor 3
	const Text* backLabel = nullptr;   // Menu 80: Game only at (35, 302) anchor 3

	// ---- Game and Confirm (drawGameScreen, src/HackingGame.cpp:375-510) ----
	int columns = 3;
	short board[5][6] = {};            // [row][col], row 0 = top, 0 = empty, k = piece k
	int hoverColumn = -1;              // white outline while touched over it
	const Text* turns = nullptr;       // "Turns:" + turnsLeft
	const Text* goal = nullptr;        // "Goal"
	const Text* prompt = nullptr;      // Game: 166/169/170, Confirm: 189; wrapped at 47
	int selRow = 0, selCol = 0;
	bool carrying = false;             // inner second outline
	uint32_t selColor = 0;             // pulsing gray, 0xFF000000 | g*0x010101
	// ---- Confirm only ----
	const Text* yesLabel = nullptr;    // Menu 197
	const Text* noLabel = nullptr;     // Menu 198
	bool yesHeld = false, noHeld = false;   // touched over the box: 0x7878FF fill
	int cursorX = 0;                   // OSC wobble applied, y 245, anchor 8

	MiniGameButton buttons[2];         // id 0 Exit (420,280,60,40), id 1 Back (0,280,60,40)
	int buttonCount = 2;
};

} // namespace newcore

#endif // NEW_MINIGAMES_HACKING_HACKINGMODEL_H
