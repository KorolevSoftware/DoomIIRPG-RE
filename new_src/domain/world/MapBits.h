#ifndef NEW_DOMAIN_WORLD_MAPBITS_H
#define NEW_DOMAIN_WORLD_MAPBITS_H

// Names for the bit packings of the per-tile / per-sprite map arrays that the
// original code reads as bare literals (MapData::mapFlags, mapSpriteInfo).
// Values only: no accessors, no new semantics (ADR 0011 §5).
#include "domain/game/Enums.h"

namespace newcore {

// MapData::mapFlags[tileY * 32 + tileX] — one byte per tile, the low nibble
// read verbatim from the packed table (src/LoadingManager.cpp:558-563), the
// high bits set at runtime. Only the two bits used by the rewrite are named;
// the remaining readers in the original (0x1 door-link, 0x4 wall push, 0x8/0x80
// automap, 0x10/0x20 player) are still literals at their own call sites.
namespace TileFlag {

// Sight stops at this tile. The single reader in the original is the facing /
// health-bar probe's promotion re-scan, and only for an ET_SPRITEWALL hit:
// an opaque spritewall ends the scan (src/MovementController.cpp:61,
// docs/research/2026-08-26-facing-entity-health-bar.md §2).
constexpr int BLOCKS_SIGHT = 0x2;

// Tile carries a tileEvents entry. Set while parsing the event table
// (src/LoadingManager.cpp:548) and tested before running the event
// (src/Game.cpp:3312, src/ScriptThread.cpp:66,101).
constexpr int HAS_EVENT = 0x40;

} // namespace TileFlag

// MapData::mapSpriteInfo[sprite] layout: tile number in bits 0-7, one packed
// animation state byte in bits 8-15, SPRITE_FLAG_* in bits 16-31
// (src/Enums.h:1230-1251).
namespace SpriteInfo {

constexpr int kTileNumMask = 0xFF;
constexpr int kAnimByteMask = 0xFF00;
constexpr int kAnimShift = 8;

// Wall-plane orientation. The direction bits themselves keep their legacy
// names in Enums.h; these are the legacy composites (src/Enums.h:1248-1250).
constexpr int ORIENTED = Enums::SPRITE_FLAG_NORTH | Enums::SPRITE_FLAG_SOUTH |
                         Enums::SPRITE_FLAG_EAST | Enums::SPRITE_FLAG_WEST;
constexpr int HORIZONTAL = Enums::SPRITE_FLAG_NORTH | Enums::SPRITE_FLAG_SOUTH;
constexpr int VERTICAL = Enums::SPRITE_FLAG_EAST | Enums::SPRITE_FLAG_WEST;

static_assert(ORIENTED   == 0xF000000, "src/Enums.h:1248 SPRITE_FLAGS_ORIENTED");
static_assert(HORIZONTAL == 0x3000000, "src/Enums.h:1249 SPRITE_FLAGS_HORIZONTAL");
static_assert(VERTICAL   == 0xC000000, "src/Enums.h:1250 SPRITE_FLAGS_VERTICAL");

} // namespace SpriteInfo
} // namespace newcore

#endif // NEW_DOMAIN_WORLD_MAPBITS_H
