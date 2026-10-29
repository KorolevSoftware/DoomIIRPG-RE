#ifndef NEW_MINIGAMES_COMMON_MINIGAME_H
#define NEW_MINIGAMES_COMMON_MINIGAME_H

#include "core/GameStates.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// The lifecycle all three legacy minigames share (ADR 0033 point 2). Legacy
// action -> Action mapping: UP=Forward, DOWN=Back, LEFT=TurnLeft,
// RIGHT=TurnRight, FIRE=Use, MENU=Menu, AUTOMAP=Automap, BACK=BackKey
// (src/Enums.h:993-1005); every other action is ignored.
class MiniGame {
public:
	virtual ~MiniGame() = default;
	// initGame. Called by MiniGameHost::start AFTER it latched the calling
	// thread and switched to StateId::MiniGame. b1..b3 are the raw signed
	// EV_MINIGAME byte args 2..4 (src/ScriptThread.cpp:1292-1295); each game
	// derives its own initGame parameters from them.
	virtual void begin(int b1, int b2, int b3) = 0;
	// handleInput(action): every queued key action while StateId::MiniGame.
	virtual void handleAction(Action a) = 0;
	// touchEnd after GetTouchedButtonID (src/Button.cpp:277-287): the legacy
	// button id released on, -1 = released on no button.
	virtual void handleTouch(int buttonId) = 0;
	// updateGame. Session side: may advance draw-time clocks and read ui.in()
	// (drags), then builds its model and calls its stateless view. Returns the
	// view's intent (UiAction::MiniGameTouch or None).
	virtual UiResult draw(Ui& ui) = 0;
};

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAME_H
