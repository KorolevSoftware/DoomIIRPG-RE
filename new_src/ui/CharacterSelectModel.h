#ifndef NEW_UI_CHARACTERSELECTMODEL_H
#define NEW_UI_CHARACTERSELECTMODEL_H

namespace newcore {

class Text;

// Per-frame model of ST_CHARACTER_SELECTION (front-end.md §6.1). Built by
// CharacterSelectSession::buildModel; the Text pointers are borrowed for THIS
// frame only.
struct CharacterSelectModel {
	int highlighted = 1;                 // stateVars[0]: character constant 1 Major / 2 Sarge / 3 Scientist
	int slotChoice[3] = { 1, 3, 2 };     // getCharacterConstantByOrder (src/IntroSequenceManager.cpp:325-339)
	const Text* title = nullptr;         // full name of `highlighted`
	const Text* shortName[3] = {};       // per slot, already "^5"-prefixed
	const Text* statRow[5] = {};         // "Defense:8" ..., already "^5"-prefixed
	const Text* prompt = nullptr;        // 3:221 wrapped at 24 on '\n'
	const Text* back = nullptr;          // 3:80
	const Text* yes = nullptr;           // 3:197
	int torsoLift = 0;                   // (upTimeMs / 1000) & 1
	bool confirmCursor = false;          // stateVars[8] == 1
	bool cursorOnYes = true;             // stateVars[2] == 1
	int cursorOffset = 0;                // OSC_CYCLE[upTimeMs / 100 % 4]
};

} // namespace newcore

#endif // NEW_UI_CHARACTERSELECTMODEL_H
