#ifndef NEW_UI_LOADINGVIEW_H
#define NEW_UI_LOADINGVIEW_H

namespace newcore {

class Text;
class Ui;

// The ST_LOADING / ST_SAVING box (docs/original-code/ui.md §19.5).
struct LoadingModel {
	const Text* caption = nullptr;     // dehyphenated; never null when drawn
	const Text* subCaption = nullptr;  // (0,58) "Just a moment!"
	int pacifierX = 174;
};

// Stateless view: LoadingScreenController::drawLoadingBar
// (src/LoadingScreenController.cpp:38-100) with the full-redraw branch taken.
void drawLoading(Ui& ui, const LoadingModel& m);

} // namespace newcore

#endif // NEW_UI_LOADINGVIEW_H
