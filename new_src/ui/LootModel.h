#ifndef NEW_UI_LOOTMODEL_H
#define NEW_UI_LOOTMODEL_H

namespace newcore {

class Text;

// Per-frame view model for the corpse-loot list (ADR 0012 point 4, spec
// 2026-08-27-ui-layer §5). Built by LootSession::buildViewModel() on the frame
// it is drawn; every pointer is borrowed and valid for THAT frame only.
//
// The split: LootSession keeps the 500 ms crouch/stand dwell, the paging rules,
// the turn consumed on close and the grants. The view owns the box geometry,
// the colours, the title placement and the scrollbar.
struct LootListModel {
	// Loc string (0,227), dehyphenated, composed by the producer.
	const Text* title = nullptr;
	// The pooled lines, '|' separated; rows index into it.
	const Text* text = nullptr;
	// <start,len> pairs, one per line (CorpseLoot::Pool::lineIndex).
	const short* lineIndex = nullptr;
	// Capacity of `lineIndex` in pairs, i.e. the bound the row loop skips
	// against. NOT the same as lineCount: the original indexes the pair table
	// with no bound at all (src/LootingSystem.cpp:140-144), the rewrite's loop
	// guards against the table size (new_src/core/LootSession.cpp:155-160), and
	// the trailing pairs are zeroed so they draw nothing.
	int indexCapacity = 0;
	// Total lines = numPoolItems + (credits != 0). Scrollbar input only.
	int lineCount = 0;
	int topLine = 0;
	// Legacy constant 3, NOT derived from the box height
	// (src/LootingSystem.cpp:140).
	int visibleRows = 3;
};

} // namespace newcore

#endif // NEW_UI_LOOTMODEL_H
