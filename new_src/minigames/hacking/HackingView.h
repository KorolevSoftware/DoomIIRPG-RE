#ifndef NEW_MINIGAMES_HACKING_HACKINGVIEW_H
#define NEW_MINIGAMES_HACKING_HACKINGVIEW_H

#include "minigames/hacking/HackingModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;
struct HackingAssets;

// Stateless hacking screens (src/HackingGame.cpp:346-582). Returns the touch
// release resolved to a legacy button id (UiAction::MiniGameTouch).
UiResult drawHacking(Ui& ui, const HackingModel& m, const HackingAssets& art);

} // namespace newcore

#endif // NEW_MINIGAMES_HACKING_HACKINGVIEW_H
