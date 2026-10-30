#ifndef NEW_MINIGAMES_SENTRYBOT_SENTRYBOTMODEL_H
#define NEW_MINIGAMES_SENTRYBOT_SENTRYBOTMODEL_H

#include <cstdint>

#include "minigames/common/MiniGameButtons.h"
#include "minigames/common/MiniGameHelpModel.h"

namespace newcore {

class Text;

// Per-frame view model of the sentry bot minigame, filled by
// SentryBotGame::draw. Every pointer is borrowed for that frame only.
struct SentryBotModel {
	enum class Screen { Help, Board, Success, Failure };   // stateVars[1]==0 / [2] 0,1,2
	Screen screen = Screen::Help;

	MiniGameHelpModel help;            // Help
	const Text* exitCode = nullptr;    // Help: Code 30 at (445,302) anchor 3

	// ---- Board (drawGameScreen, src/SentryBotGame.cpp:489-727) ----
	uint8_t board[16] = {};            // 1..6
	int cursor = -1;                   // displayed player_cursor; -1 = start marker
	bool cursorVisible = true;         // blink applied (drawCursor b=true)
	int pressedCell = -1;              // touched over this cell: pressed-cell image
	const Text* digits[16] = {};       // number text per cell
	const Text* hint = nullptr;        // Code 173 + 174 + skipCode
	const Text* enterCode = nullptr;   // Code 175
	const Text* tapHint = nullptr;     // Code 176
	const Text* back = nullptr;        // literal "Back"
	const Text* exit = nullptr;        // literal "Exit"
	const Text* del = nullptr;         // literal "Delete"
	const Text* submit = nullptr;      // literal "Submit"
	uint8_t submitAlpha = 255, deleteAlpha = 255;   // Board and Success

	// ---- Guess bar (drawPlayersGuess, :729-747): Board, Success, Failure ----
	const Text* guess[4] = {};         // nullptr = slot empty (usersGuess 0)
	int guessY = 249;                  // 249 board, 26 result screens
	int answerCursor = -1;             // -1 = no guess cursor (result screens)

	// ---- Success (script mode, :410-471) and Failure (:323-370) ----
	const Text* title = nullptr;       // 177 / 178 at (240,15)
	const Text* subtitle = nullptr;    // Failure: 240 at (240,71)
	const Text* select = nullptr;      // Success: 180 at (240,95)
	const Text* shooting = nullptr;    // 181 at (60,140)
	const Text* exploding = nullptr;   // 182 at (420,140)
	int selCursorX = 0;                // xPos + OSC wobble, y 140, anchor 10
	const Text* shootingHead = nullptr, *shootingBody = nullptr;     // "Shooting Bot:", 183 wrapped 50
	const Text* explodingHead = nullptr, *explodingBody = nullptr;   // "Exploding Bot:", 184 wrapped 50
	bool warning = false;              // Failure, botType 1: red box + Code 186
	const Text* warningLabel = nullptr;
	const Text* message = nullptr;     // Failure: 187 or 188 wrapped 22
	int messageY = 139;                // 139 (botType 0) / 209 (botType 1)

	MiniGameButton buttons[4];         // ids 0 Exit, 1 Back, 2 Submit, 3 Delete (list order)
	int buttonCount = 4;
};

} // namespace newcore

#endif // NEW_MINIGAMES_SENTRYBOT_SENTRYBOTMODEL_H
