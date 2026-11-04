#ifndef NEW_UI_CHARACTERSELECTVIEW_H
#define NEW_UI_CHARACTERSELECTVIEW_H

#include "ui/CharacterSelectModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view (ADR 0012 point 1): port of drawCharacterSelection +
// drawCharacterSelectionAvatar + drawCharacterSelectionStats
// (src/IntroSequenceManager.cpp:341-584). A released button reports
// UiAction::ScreenTouch with the legacy button id: 0-2 slot, 3 Yes, 4 Back.
UiResult drawCharacterSelect(Ui& ui, const CharacterSelectModel& m);

} // namespace newcore

#endif // NEW_UI_CHARACTERSELECTVIEW_H
