#ifndef NEW_UI_INTROMOVIEMODEL_H
#define NEW_UI_INTROMOVIEMODEL_H

namespace newcore {

class Text;

// Per-frame model of ST_INTRO_MOVIE (docs/original-code/front-end.md §3.4).
// Built by IntroMovieSession::buildModel; `text` is borrowed for THIS frame.
struct IntroMovieModel {
	bool started = false;        // false = phase 0: nothing but black
	bool crawl = false;          // false = the camera pan, true = the text crawl
	int camX = 0, camY = 0;      // pan: prolog.bmp pixel at the screen centre
	const Text* text = nullptr;  // crawl: 0:133 wrapped at 42
	int textOffset = 0;          // crawl: px scrolled, 20 px/s
	int blackAlpha = 0;          // fade-in overlay over the whole screen, 0 = none
};

} // namespace newcore

#endif // NEW_UI_INTROMOVIEMODEL_H
