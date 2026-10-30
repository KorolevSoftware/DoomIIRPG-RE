#ifndef NEW_UI_AUTOMAPMODEL_H
#define NEW_UI_AUTOMAPMODEL_H

#include <cstdint>
#include <vector>

namespace newcore {

class Text;

// One fill or one 1-px line, canvas pixels, opaque ARGB (Graphics::setColor ignores
// alpha, src/Graphics.cpp:88-95). Rect: (x0,y0) top-left, (x1,y1) = (w,h).
// Line: endpoints, inclusive.
struct AutomapPrim { bool line; int16_t x0, y0, x1, y1; uint32_t argb; };

// Per-frame model of ST_AUTOMAP (docs/original-code/automap.md §3). prims are in
// legacy draw order (tiles, ladders, walls, entities). Owned by GameContext,
// rebuilt every frame (clear() keeps capacity).
struct AutomapModel {
	std::vector<AutomapPrim> prims;
	bool cursor = false; int cursorSrcY = 0, cursorX = 0, cursorY = 0;  // 4x4 region, anchor 3
	const Text* softLeft = nullptr;   // "Menu"  (text 0/52)
	const Text* softRight = nullptr;  // "Leave" (text 0/53)
};

} // namespace newcore

#endif // NEW_UI_AUTOMAPMODEL_H
