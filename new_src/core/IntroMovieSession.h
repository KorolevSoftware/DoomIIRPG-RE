#ifndef NEW_CORE_INTROMOVIESESSION_H
#define NEW_CORE_INTROMOVIESESSION_H

#include <cstdint>
#include <functional>

#include "core/MayaCamera.h"
#include "domain/world/MapData.h"
#include "text/Text.h"
#include "ui/IntroMovieModel.h"

namespace newcore {

class Localization;
class Tables;

// ST_INTRO_MOVIE logic (docs/original-code/front-end.md §3, §3.4): port of
// playIntroMovie's phases (src/IntroSequenceManager.cpp:752-859) with the
// table camera 14/15 played through core/MayaCamera on a synthetic one-camera
// MapData; the session advances keys itself with the table-cam rule.
class IntroMovieSession {
public:
	struct Env {
		const Tables* tables = nullptr;
		const Localization* loc = nullptr;
		const int64_t* upTimeMs = nullptr;     // stands in for gameTime / app->time
		std::function<void()> exit;            // exitIntroMovie(false) (src/IntroSequenceManager.cpp:861-878)
	};
	void init(const Env& env);              // builds the table camera (src/Game.cpp:511-574)
	void begin();                           // first frame (:766-774)
	void tick();                            // one phase step per frame (:779-847)
	void buildModel(IntroMovieModel& m) const;

private:
	int keyMs(int k) const;

	Env env_;
	MapData camMap_;     // only totalMayaCameraKeys + mayaCameras[0] are used
	MayaCamera cam_;
	bool camOk_ = false;
	int posShift_ = 4;   // 4 - table14[2] (src/Game.cpp:533)
	Text crawl_;
	int phase_ = 0;      // stateVars[3]: 0 fade start, 1 pan, 2 crawl init, 3 crawl
	int key_ = -1;       // activeCameraKey
	int64_t keyStart_ = 0;     // activeCameraTime
	int64_t fadeStart_ = -1;   // canvas->fadeTime; -1 = no fade
	int64_t crawlStart_ = 0;   // scrollingTextStart
};

} // namespace newcore

#endif // NEW_CORE_INTROMOVIESESSION_H
