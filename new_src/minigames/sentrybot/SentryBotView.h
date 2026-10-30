#ifndef NEW_MINIGAMES_SENTRYBOT_SENTRYBOTVIEW_H
#define NEW_MINIGAMES_SENTRYBOT_SENTRYBOTVIEW_H

#include "minigames/sentrybot/SentryBotModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;
struct SentryBotAssets;

// Stateless sentry bot screens (src/SentryBotGame.cpp:296-754). Returns the
// touch release resolved to a legacy button id (UiAction::MiniGameTouch).
UiResult drawSentryBot(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art);

} // namespace newcore

#endif // NEW_MINIGAMES_SENTRYBOT_SENTRYBOTVIEW_H
