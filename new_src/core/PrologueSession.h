#ifndef NEW_CORE_PROLOGUESESSION_H
#define NEW_CORE_PROLOGUESESSION_H

#include "core/GameStates.h"
#include "text/Text.h"
#include "ui/PrologueModel.h"

namespace newcore {

class Localization;
class Player;

// ST_INTRO logic (front-end.md §7): loadPrologueText + handleStoryInput
// (src/IntroSequenceManager.cpp:50-100, :698-750); drawing goes through
// PrologueModel / drawPrologue.
class PrologueSession {
public:
	struct Env {
		const Player* player = nullptr;
		const Localization* loc = nullptr;
		StateHost* host = nullptr;
	};
	void init(const Env& env);
	void begin();                       // loadPrologueText (src/IntroSequenceManager.cpp:50-100)
	void handleAction(Action a);        // handleStoryInput (:698-750)
	void handleTouch(int id);           // stateVars[0] = id, then FIRE (src/TouchController.cpp:270-288)
	bool done() const { return page_ >= totalPages_; }   // Canvas::run (src/Canvas.cpp:848-851)
	void buildModel(PrologueModel& m);  // call only while !done()

private:
	static constexpr int kMaxPages = 16;
	void prev();                        // changeStoryPage(-1) (:247-258)

	Env env_;
	Text story_, back_, right_, skip_;
	int pageStart_[kMaxPages + 1] = {};
	int page_ = 0, totalPages_ = 0;
	int cursor_ = 1;                    // stateVars[0]: 0 Back, 1 More/Continue, 2 Skip
};

} // namespace newcore

#endif // NEW_CORE_PROLOGUESESSION_H
