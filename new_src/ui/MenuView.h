#ifndef NEW_UI_MENUVIEW_H
#define NEW_UI_MENUVIEW_H

#include "ui/MenuModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view function (ADR 0012 point 1): draws the in-game menu screen
// from the frame's model and reports what was pressed. Port of the presentation
// half of MenuSystem::paint (src/MenuSystem.cpp:735-1130) plus
// drawSoftkeyButtons (:5181-5292) and the row hit areas of drawTouchButtons
// (:5062-5160). Knows nothing about menu ids, states or localization.
UiResult drawMenu(Ui& ui, const MenuViewModel& m);

} // namespace newcore

#endif // NEW_UI_MENUVIEW_H
