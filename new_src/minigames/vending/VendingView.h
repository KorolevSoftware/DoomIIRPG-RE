#ifndef NEW_MINIGAMES_VENDING_VENDINGVIEW_H
#define NEW_MINIGAMES_VENDING_VENDINGVIEW_H

#include "minigames/vending/VendingModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;
struct VendingAssets;

// Stateless vending screens (src/VendingMachine.cpp:469-686). Returns the
// touch release resolved to a legacy button id (UiAction::MiniGameTouch).
UiResult drawVending(Ui& ui, const VendingModel& m, const VendingAssets& art);

} // namespace newcore

#endif // NEW_MINIGAMES_VENDING_VENDINGVIEW_H
