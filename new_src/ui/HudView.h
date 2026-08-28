#ifndef NEW_UI_HUDVIEW_H
#define NEW_UI_HUDVIEW_H

#include "ui/HudModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view function (ADR 0012 point 1): draws the HUD bottom bar from
// the frame's model and reports what was pressed. No game state is read and
// nothing is mutated — the returned intent is translated by
// GameContext::applyUiAction into the existing Action queue.
UiResult drawHud(Ui& ui, const HudModel& m);

} // namespace newcore

#endif // NEW_UI_HUDVIEW_H
