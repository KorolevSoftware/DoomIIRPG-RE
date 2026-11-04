#ifndef NEW_UI_PROLOGUEMODEL_H
#define NEW_UI_PROLOGUEMODEL_H

namespace newcore {

class Text;

// Per-frame model of ST_INTRO (front-end.md §7). Built by
// PrologueSession::buildModel; the Text pointers are borrowed for THIS frame.
struct PrologueModel {
	const Text* body = nullptr; int bodyStart = 0, bodyLen = 0;   // page slice of the composed buffer
	const Text* back = nullptr;          // 3:80, dehyphenated
	const Text* bottomRight = nullptr;   // 0:56 "More", or 0:43 "Continue" on the last page
	const Text* topRight = nullptr;      // 0:40 "Skip"; null on the last page
};

} // namespace newcore

#endif // NEW_UI_PROLOGUEMODEL_H
