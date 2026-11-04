#ifndef NEW_UI_MAINMENUVIEW_H
#define NEW_UI_MAINMENUVIEW_H

#include "ui/MenuModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view function (ADR 0012 point 1): the main-menu look of the same
// MenuViewModel (m.mainSkin). Presentation half of MenuSystem::paint for ids
// below MENU_INGAME (src/MenuSystem.cpp:764-1134) plus the Back switch soft key
// (:5212-5238) and the row hit areas of drawTouchButtons (:5090-5102).
UiResult drawMainMenu(Ui& ui, const MenuViewModel& m);

} // namespace newcore

#endif // NEW_UI_MAINMENUVIEW_H
