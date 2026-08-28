#ifndef NEW_UI_LOOTVIEW_H
#define NEW_UI_LOOTVIEW_H

#include "ui/LootModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view function (ADR 0012 point 1): draws the corpse-loot list from
// the frame's model and reports what was pressed. Reads no game state and
// mutates nothing — the returned intent is translated by
// GameContext::applyUiAction into the existing Action queue, so a click goes
// through the very queue the keys go through.
UiResult drawLootList(Ui& ui, const LootListModel& m);

} // namespace newcore

#endif // NEW_UI_LOOTVIEW_H
