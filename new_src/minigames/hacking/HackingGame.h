#ifndef NEW_MINIGAMES_HACKING_HACKINGGAME_H
#define NEW_MINIGAMES_HACKING_HACKINGGAME_H

#include "minigames/common/MiniGame.h"
#include "minigames/hacking/HackingAssets.h"
#include "minigames/hacking/HackingModel.h"
#include "text/Text.h"
#include "ui/UiAssets.h"

namespace newcore {

class MiniGameHost;
class TextureStore;

// The hacking minigame session (src/HackingGame.cpp), world branch only:
// no playFromMainMenu, no forceWin (ADR 0033 point 7).
class HackingGame : public MiniGame {
public:
	void init(MiniGameHost* host) { host_ = host; }
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);

	// EV_MINIGAME type 2: b1 = custom flag, b2 = columns, b3 = turns (-1 = 30)
	// (src/ScriptThread.cpp:1314-1347, src/HackingGame.cpp:53-100).
	void begin(int b1, int b2, int b3) override;
	void handleAction(Action a) override;      // handleInput (:182-301)
	void handleTouch(int buttonId) override;   // touchEnd (:661-688) + release pass (ADR 0038)
	UiResult draw(Ui& ui) override;            // updateGame (:346-357)

private:
	static constexpr int kRows = 5, kMaxCols = 6;
	static constexpr int kScreenHelp = 0, kScreenGame = 1, kScreenConfirm = 2;   // stateVars[1]
	static constexpr int kPlaying = 0, kWon = 1, kLost = 2;                      // stateVars[2]
	static constexpr int kColumnTapBase = 100;   // pseudo button ids, ADR 0038

	void fillBoard(int scrambles);             // fillGameBoardRandomly 5-arg (:133-180)
	bool solved() const;                       // gameIsSolved (:584-593)
	void attemptToMove(int n);                 // (:303-344)
	void releasePass();                        // first draw after touchEnd (:382-400, :460-503)
	void endGame(int n);                       // (:619-636)
	void compose(int type, int index, Text& out) const;   // appends, as vending

	MiniGameHost* host_ = nullptr;
	HackingAssets assets_;
	HackingModel model_;

	short board_[kRows][kMaxCols] = {};
	int columns_ = 3;
	int turnsLeft_ = 0;
	int selRow_ = 0, selCol_ = 0;
	bool selected_ = false;                    // carrying a block
	int cameFrom_ = 0;                         // columnBlockCameFrom
	int screen_ = kScreenHelp;
	int result_ = kPlaying;
	int confirmCursor_ = 0;                    // persists between confirm visits
	bool done_ = false;                        // endGame ran; stop the rest of a touch
	// Raw press samples (touchStart/Move/End, :638-659); (0,0) = cleared.
	int curX_ = 0, curY_ = 0, oldX_ = 0, oldY_ = 0;
	bool touched_ = false;
	int touchedColumn_ = -1;                   // legacy static: NOT reset by begin
	int lastHighlight_ = -1;                   // button highlight edge -> sound 1027

	Text exitLabel_, backLabel_, turns_, goal_, prompt_, yes_, no_;
};

} // namespace newcore

#endif // NEW_MINIGAMES_HACKING_HACKINGGAME_H
