#ifndef NEW_MINIGAMES_COMMON_MINIGAMEHELPMODEL_H
#define NEW_MINIGAMES_COMMON_MINIGAMEHELPMODEL_H

namespace newcore {

class Text;
class Texture;

// Per-frame view model of the shared minigame help screen, filled by
// MiniGameHelp::buildModel. Every pointer is borrowed for that frame only.
struct MiniGameHelpModel {
	const Texture* background = nullptr;
	const Text* title = nullptr;    // dehyphenated
	const Text* body = nullptr;     // the visible window of the wrapped body, '\n' separated
	const Text* footer = nullptr;   // dehyphenated
	// Scrollbar inputs: first visible line, min(firstLine+16, numLines), total.
	int firstLine = 0;
	int pageEnd = 0;
	int numLines = 0;
};

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAMEHELPMODEL_H
