#ifndef NEW_CORE_CHARACTERSELECTSESSION_H
#define NEW_CORE_CHARACTERSELECTSESSION_H

#include <cstdint>
#include <functional>

#include "core/GameStates.h"
#include "text/Text.h"
#include "ui/CharacterSelectModel.h"

namespace newcore {

class Game;
class Localization;
class Player;
class Tables;

// ST_CHARACTER_SELECTION logic (front-end.md §6.2): port of
// handleCharacterSelectionInput (src/IntroSequenceManager.cpp:587-696); the
// drawing goes through CharacterSelectModel / drawCharacterSelect.
class CharacterSelectSession {
public:
	struct Env {
		Player* player = nullptr;
		const Game* game = nullptr;           // difficulty 2 shows DEFENSE 0 (:530-532)
		const Localization* loc = nullptr;
		const Tables* tables = nullptr;       // oscCycle
		StateHost* host = nullptr;
		const int64_t* upTimeMs = nullptr;
		std::function<void()> backToMain;
	};
	void init(const Env& env);
	void begin(int highlighted);              // entry (src/Canvas.cpp:1166-1173)
	void handleAction(Action a);              // keys (src/IntroSequenceManager.cpp:636-694)
	void handleTouch(int buttonId);           // release (:612-634)
	void buildModel(CharacterSelectModel& m); // fills the Text buffers it owns

private:
	void step(int dir);                       // order 1 -> 3 -> 2 (:644-659)
	void choose();                            // setCharacterChoice + reset + ST_INTRO (:625-630)

	Env env_;
	int highlighted_ = 1;    // stateVars[0]
	bool confirm_ = false;   // stateVars[8] ([GEC] keyboard sub-mode)
	bool onYes_ = true;      // stateVars[2]
	Text title_, shortName_[3], stat_[5], prompt_, back_, yes_;
};

} // namespace newcore

#endif // NEW_CORE_CHARACTERSELECTSESSION_H
