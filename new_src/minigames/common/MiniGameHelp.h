#ifndef NEW_MINIGAMES_COMMON_MINIGAMEHELP_H
#define NEW_MINIGAMES_COMMON_MINIGAMEHELP_H

#include "minigames/common/MiniGameHelpModel.h"
#include "text/Text.h"

namespace newcore {

class Localization;

// The one help scroll state all minigames share (MiniGameManager help,
// src/MiniGameManager.cpp:250-350).
class MiniGameHelp {
public:
	static constexpr int kViewLines = 16;   // 0x10
	static constexpr int kWrapChars = 49;   // wrapText(0x31, '\n')

	void init(const Localization* loc) { loc_ = loc; }
	void reset() { scroll_ = 0; }           // initMiniGameHelpScreen (:250-252)
	void scroll(int delta);                 // handleMiniGameHelpScreenScroll (:327-350)
	// drawMiniGameHelpText's text half (:258-318): composes title/body/footer
	// and cuts the 16-line body window. titleId/bodyId are packed string ids.
	// m.background is left to the caller (the sheet lives in MiniGameAssets).
	void buildModel(int titleId, int bodyId, MiniGameHelpModel& m);

private:
	void compose(int id, Text& out) const;

	const Localization* loc_ = nullptr;
	int scroll_ = 0;
	int numLines_ = 0;   // helpTextNumberOfLines, refreshed by every buildModel
	Text title_, body_, window_, footer_;
};

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAMEHELP_H
