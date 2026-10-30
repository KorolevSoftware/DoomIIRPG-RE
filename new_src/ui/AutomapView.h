#ifndef NEW_UI_AUTOMAPVIEW_H
#define NEW_UI_AUTOMAPVIEW_H

#include "ui/AutomapModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view of ST_AUTOMAP (drawAutomap, src/AutomapController.cpp:65-348).
// Left soft key reports UiAction::Menu, right soft key UiAction::Automap
// (soft-key buttons 19/20, src/InputEventController.cpp:110-111,133-134).
UiResult drawAutomap(Ui& ui, const AutomapModel& m);

} // namespace newcore

#endif // NEW_UI_AUTOMAPVIEW_H
