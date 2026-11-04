#ifndef NEW_UI_INTROMOVIEVIEW_H
#define NEW_UI_INTROMOVIEVIEW_H

#include "ui/IntroMovieModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view (ADR 0012 point 1): the drawing half of playIntroMovie +
// drawScrollingText (src/IntroSequenceManager.cpp:207-245, :795-847) and the
// Canvas fade (src/Canvas.cpp:499-509). Any release reports
// UiAction::ScreenTouch: every event skips the movie (src/Canvas.cpp:833-841).
UiResult drawIntroMovie(Ui& ui, const IntroMovieModel& m);

} // namespace newcore

#endif // NEW_UI_INTROMOVIEVIEW_H
