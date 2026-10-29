#ifndef NEW_MINIGAMES_COMMON_MINIGAMEHELPVIEW_H
#define NEW_MINIGAMES_COMMON_MINIGAMEHELPVIEW_H

#include "minigames/common/MiniGameHelpModel.h"

namespace newcore {

class Ui;

// Stateless help screen (src/MiniGameManager.cpp:254-325). The per-game
// "Exit" label and buttons are drawn by each game's view after this call.
void drawMiniGameHelp(Ui& ui, const MiniGameHelpModel& m);

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAMEHELPVIEW_H
