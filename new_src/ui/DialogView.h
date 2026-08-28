#ifndef NEW_UI_DIALOGVIEW_H
#define NEW_UI_DIALOGVIEW_H

#include "ui/DialogModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view function (ADR 0012 point 1): draws the styled dialog box from
// the frame's model and reports what was pressed. Reads no game state and
// mutates nothing — the returned intent is translated by
// GameContext::applyUiAction into the existing Action queue, so a click on a
// page icon goes through the very queue the keys go through.
UiResult drawDialog(Ui& ui, const DialogViewModel& m);

} // namespace newcore

#endif // NEW_UI_DIALOGVIEW_H
