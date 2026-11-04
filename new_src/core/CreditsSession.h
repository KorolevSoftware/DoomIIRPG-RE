#ifndef NEW_CORE_CREDITSSESSION_H
#define NEW_CORE_CREDITSSESSION_H

#include <cstdint>

#include "text/Text.h"
#include "ui/IntroMovieModel.h"

namespace newcore {

class Localization;

// ST_CREDITS logic (docs/original-code/front-end.md §12.4): initScrollingText(2, 0,
// false, 16, 5, 500) plus drawScrollingText's clock (src/IntroSequenceManager.cpp:163-245).
// No sound anywhere in the session.
class CreditsSession {
public:
	struct Env {
		const Localization* loc = nullptr;
		const int64_t* upTimeMs = nullptr;   // stands in for gameTime (deviation FE-D9)
	};
	void init(const Env& env);
	void begin();                         // setState(ST_CREDITS)
	void buildModel(CreditsModel& m);     // latches done_ like drawScrollingText

private:
	Env env_;
	Text text_, continue_;
	int64_t start_ = 0;
	int end_ = 0;                         // scrollingTextEnd, ms
	bool done_ = false;                   // scrollingTextDone
};

} // namespace newcore

#endif // NEW_CORE_CREDITSSESSION_H
