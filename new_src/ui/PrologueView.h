#ifndef NEW_UI_PROLOGUEVIEW_H
#define NEW_UI_PROLOGUEVIEW_H

#include "ui/PrologueModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view (ADR 0012 point 1): port of drawStory
// (src/IntroSequenceManager.cpp:260-323). A released button reports
// UiAction::ScreenTouch with the legacy id: 0 Back, 1 More/Continue, 2 Skip.
UiResult drawPrologue(Ui& ui, const PrologueModel& m);

} // namespace newcore

#endif // NEW_UI_PROLOGUEVIEW_H
