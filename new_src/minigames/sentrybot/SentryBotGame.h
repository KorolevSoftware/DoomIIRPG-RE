#ifndef NEW_MINIGAMES_SENTRYBOT_SENTRYBOTGAME_H
#define NEW_MINIGAMES_SENTRYBOT_SENTRYBOTGAME_H

#include <cstdint>

#include "minigames/common/MiniGame.h"
#include "minigames/sentrybot/SentryBotAssets.h"
#include "minigames/sentrybot/SentryBotModel.h"
#include "text/Text.h"
#include "ui/UiAssets.h"

namespace newcore {

class MiniGameHost;
class TextureStore;

// The sentry bot "Matrix Skip" session (src/SentryBotGame.cpp): no forceWin,
// no wasTouched (ADR 0033 point 7).
class SentryBotGame : public MiniGame {
public:
	void init(MiniGameHost* host) { host_ = host; }
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);

	// EV_MINIGAME type 0: b1 = botType (src/ScriptThread.cpp:1290-1303); b2/b3 unused.
	void begin(int b1, int b2, int b3) override;   // initGame (:44-120)
	void playFromMainMenu() { begin(0, -1, -1); }   // (:29-31)
	void handleAction(Action a) override;           // -> input(legacy id)
	void handleTouch(int buttonId) override;        // touchEnd (:839-892)
	UiResult draw(Ui& ui) override;                 // updateGame (:296-321)

private:
	// Legacy action ids (src/Enums.h:993-1012); 64/65 come only from touch.
	static constexpr int kUp = 1, kDown = 2, kLeft = 3, kRight = 4, kMenu = 5, kFire = 6,
		kAutomap = 7, kPassTurn = 14, kBack = 15, kMatrixLeft = 64, kMatrixRight = 65;
	static constexpr int kBoard = 0, kWon = 1, kLost = 2;   // stateVars[2]

	void generate();              // :53-81
	void input(int action);       // handleInput (:127-294)
	void commitTouch();           // ADR 0038: apply draw-time selections in the tick
	void award();                 // awardSentryBot stub (ADR 0040)
	void endGame(int n);          // :806-824

	MiniGameHost* host_ = nullptr;
	SentryBotAssets assets_;
	SentryBotModel model_;
	int botType_ = 0;
	int skipCode_ = 1;
	uint8_t board_[16] = {}, solution_[4] = {}, guess_[4] = {};
	int answerCursor_ = 0, playerCursor_ = -1, botCursor_ = 0;
	int64_t cursorMoveTime_ = 0;      // timeSinceLastCursorMove
	bool help_ = true;                // stateVars[1] == 0
	int result_ = kBoard;             // stateVars[2]
	// Touch samples (touchStart/Move/End, :826-843) and the draw-time
	// selections waiting for the tick (-1 = none).
	bool touched_ = false;
	int curX_ = 0, curY_ = 0;
	int touchCell_ = -1, touchBot_ = -1;
	int lastHighlight_ = -1;          // button highlight edge -> sound 1027
	Text exitCode_, back_, exit_, del_, submit_, hint_, enterCode_, tapHint_;
	Text digits_[16], guessText_[4];
	Text title_, subtitle_, select_, shooting_, exploding_;
	Text shootingHead_, shootingBody_, explodingHead_, explodingBody_, warning_, message_;
};

} // namespace newcore

#endif // NEW_MINIGAMES_SENTRYBOT_SENTRYBOTGAME_H
