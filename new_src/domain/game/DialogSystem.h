#ifndef NEW_DOMAIN_GAME_DIALOGSYSTEM_H
#define NEW_DOMAIN_GAME_DIALOGSYSTEM_H

#include <cstdint>

#include "text/Text.h"
#include "ui/DialogModel.h"

namespace newcore {

class Font;
class Game;
class GameContext;
class Graphics2D;
class Hud;
class Localization;
struct ScriptThread;
class ScriptVM;
class Tables;
enum class Action : int;

// Faithful port of Canvas::dialogSystem (src/DialogSystem.cpp): styled bottom
// dialog boxes, %NN text-arg composition, word wrap + paging, typewriter
// reveal and the style-2 help FIFO. Owns no GL state and, since spec
// 2026-08-27-ui-layer §7, no drawing either: the box is painted by
// ui/DialogView.cpp from the model this class builds.
class DialogSystem {
public:
	static constexpr int kMaxHelpMessages = 16;  // helpMessageTypes[16] (src/DialogSystem.cpp:850)
	static constexpr int kMaxDialogLines = 512;  // dialogIndexes[1024] shorts
	static constexpr int kMaxTextArgs = 50;      // src/Text.h:74-80

	// Wrap budgets in chars: (W-2)/9 and (W-9)/9 on the 480px canvas
	// (src/Canvas.cpp:89-92).
	static constexpr int kDialogMaxChars = (480 - 2) / 9;
	static constexpr int kWithBarMaxChars = (480 - 9) / 9;

	struct Env {
		GameContext* ctx = nullptr;
		ScriptVM* vm = nullptr;
		Game* game = nullptr;
		const Localization* loc = nullptr;
		// Both are drawing leftovers: hud supplies the ui_images sheet to the
		// drawScrollBar forwarder below, and the pair gates buildViewModel the
		// way it gated the old draw call. Spec GROUP 5 removes the forwarder.
		Hud* hud = nullptr;
		const Font* font = nullptr;
		const Tables* tables = nullptr;
	};

	void init(const Env& env);

	// startDialog(thread, mapType, strId, style, flags, resume): composeText
	// into the large buffer -> prepareDialog -> setState(ST_DIALOG)
	// (src/DialogSystem.cpp:737-747).
	void startDialog(ScriptThread* thread, int textType, int strIdx,
		int style, int flags, bool resumeScript);

	// Help FIFO for style-2 popups (src/DialogSystem.cpp:753-908).
	bool enqueueHelpDialog(int textType, int strIdx, int threadIdx);
	void dequeueHelpDialog(bool force = false);

	// ACTION_* dispatch while ST_DIALOG (src/DialogSystem.cpp:29-112).
	void handleInput(Action action);

	// State half of the legacy dialogState (src/DialogSystem.cpp:114-518): the
	// drawing moved to ui/DialogView.cpp (spec 2026-08-27-ui-layer §7) and
	// what stays here is the per-frame resolution of the paging/typewriter
	// state into the view model. Not const and not idempotent: it advances the
	// typewriter cursor exactly like the draw call it replaces, so it must be
	// called once per rendered frame and only while ST_DIALOG.
	// Returns false when there is nothing to draw.
	bool buildViewModel(DialogViewModel& m);

	// Shared Canvas::drawScrollBar port; also used by the loot overlay, like
	// legacy src/LoothingSystem.cpp:146-150.
	void drawScrollBar(Graphics2D& g, int x, int y, int h,
		int topLine, int pageEnd, int numLines, int viewLines) const;

	// Text-arg pool feeding %NN substitution (src/Text.cpp:222-275).
	void resetTextArgs();
	void addTextArg(const std::string& arg);
	void addTextArg(int value);

private:
	void prepareDialog(Text& text, int style, int flags);
	void closeDialog(bool skip);
	void composeText(int type, int idx, Text& out) const;

	Env env_;

	Text buffer_;                                // wrapped dialog text ('|' separators kept)
	int16_t dialogIndexes_[kMaxDialogLines * 2] = { 0 }; // <start,len> per line
	int numDialogLines_ = 0;
	int viewLines_ = 4;
	int currentDialogLine_ = 0;
	int typeLineIdx_ = 0;                        // typewriter cursor line
	int64_t lineStartTimeMs_ = 0;
	int style_ = 0;
	int flags_ = 0;
	int type_ = 0;                               // queue type of the shown dialog (help rule)
	bool closing_ = false;
	bool resumeScriptAfterClosed_ = false;
	ScriptThread* thread_ = nullptr;             // parked thread resumed on close

	std::string dynamicArgs_;                    // flat byte pool of text args
	int argIndex_[kMaxTextArgs] = { 0 };         // cumulative end offsets
	int numTextArgs_ = 0;

	int helpTypes_[kMaxHelpMessages] = { 0 };
	int helpIds_[kMaxHelpMessages] = { 0 };      // STRINGID (map<<10)|idx
	int helpThreads_[kMaxHelpMessages] = { 0 };  // VM pool index or -1
	int numHelpMessages_ = 0;

	Text scratch_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_DIALOGSYSTEM_H
