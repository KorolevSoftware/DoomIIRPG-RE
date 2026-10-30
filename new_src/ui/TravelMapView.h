#ifndef NEW_UI_TRAVELMAPVIEW_H
#define NEW_UI_TRAVELMAPVIEW_H

#include "ui/TravelMapModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view of ST_TRAVELMAP (drawTravelMap, src/TravelMapManager.cpp:146-256).
// A release anywhere is FIRE (touch-up synthesizes it in this state,
// docs/original-code/travel-map.md §5), reported as UiAction::Activate.
UiResult drawTravelMap(Ui& ui, const TravelMapModel& m);

} // namespace newcore

#endif // NEW_UI_TRAVELMAPVIEW_H
