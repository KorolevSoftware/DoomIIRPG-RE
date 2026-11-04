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

// drawCredits + drawScrollingText for ST_CREDITS (src/IntroSequenceManager.cpp:190-245).
// A release reports UiAction::Activate: touch falls to key 6 = FIRE
// (src/TouchController.cpp:336-338 -> :311-313).
UiResult drawCredits(Ui& ui, const CreditsModel& m);

} // namespace newcore

#endif // NEW_UI_INTROMOVIEVIEW_H
