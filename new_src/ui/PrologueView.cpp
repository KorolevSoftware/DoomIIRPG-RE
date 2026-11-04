#include "ui/PrologueView.h"

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

// Button touch areas (src/IntroSequenceManager.cpp:42-48). Only touchAreaDrawing
// moves (:289-299); GetTouchedButtonID reads touchArea (src/Button.cpp:280-303).
constexpr UiRect kBackRect{ 0, 280, 60, 40 };
constexpr UiRect kMoreRect{ 380, 280, 100, 40 };
constexpr UiRect kSkipRect{ 420, 0, 60, 40 };

// Story origin displayRect + (15, 20), line height 21 (:97-99, :321-322).
constexpr int kBodyX = 15;
constexpr int kBodyY = 20;
constexpr int kBodyLineH = 21;

} // namespace

// fontRenderMode(2) dimming is a no-op in the port (precedent
// minigames/hacking/HackingView.cpp:26-27), so the labels are plain.
UiResult drawPrologue(Ui& ui, const PrologueModel& m) {
	UiResult res;
	ui.image(ui.art().automapBackground, 0, 0, Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft);   // gameHelpBG.bmp
	int fired = -1;
	if (m.back != nullptr) {
		if (ui.softKey(UiId::StoryBack, *m.back, 17, 310,
			Graphics2D::kAnchorLeft | Graphics2D::kAnchorBottom, kBackRect)) fired = 0;
	}
	if (m.bottomRight != nullptr) {
		if (ui.softKey(UiId::StoryMore, *m.bottomRight, 463, 310,
			Graphics2D::kAnchorRight | Graphics2D::kAnchorBottom, kMoreRect)) fired = 1;
	}
	if (m.topRight != nullptr) {                 // button 2 only off the last page (src/TouchController.cpp:270-280)
		if (ui.softKey(UiId::StorySkip, *m.topRight, 463, 10, Graphics2D::kAnchorRight, kSkipRect))
			fired = 2;
	}
	if (m.body != nullptr)
		ui.textRun(*m.body, m.bodyStart, m.bodyLen, kBodyX, kBodyY, Graphics2D::kAnchorNone, kBodyLineH);
	if (fired >= 0) {
		res.action = UiAction::ScreenTouch;
		res.index = fired;
	}
	return res;
}

} // namespace newcore
