#include "domain/game/Automap.h"

#include <cassert>
#include <vector>

#include "domain/game/Entity.h"
#include "domain/game/EntityDb.h"
#include "domain/game/Enums.h"
#include "domain/game/Player.h"
#include "domain/game/ViewVisibility.h"
#include "domain/world/MapBits.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "ui/AutomapModel.h"

namespace newcore {

namespace {

constexpr int kLineSeen = 0x8;      // LINE_FLAG_AUTOMAP (src/Enums.h:1204)
constexpr int kLineTypeMap = 6;     // LINE_TYPE_MAP (src/Render.cpp:740)
constexpr int kTileVisited = 0x80;

constexpr int kTileHidden = 0x8;

// drawAutomap layout (src/AutomapController.cpp:73-76).
constexpr int kCell = 8;
constexpr int kOx = 112;
constexpr int kOy = 32;
constexpr int kDiv = 0x400000 / (kCell << 8);   // 2048

int px(int v) { return ((v << 16) / kDiv + 128) >> 8; }
int px1(int v) { return ((v << 16) / kDiv + 1 + 128) >> 8; }

void markLine(MapData& map, int i) {
	map.lineFlags[i >> 1] |= (uint8_t)(kLineSeen << ((i & 1) << 2));
}

void rect(AutomapModel& m, int x, int y, int w, int h, uint32_t argb) {
	m.prims.push_back({ false, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, argb });
}

void line(AutomapModel& m, int x0, int y0, int x1, int y1, uint32_t argb) {
	m.prims.push_back({ true, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, argb });
}

// Entity markers (src/AutomapController.cpp:131-231).
void buildEntities(AutomapModel& m, const MapData& map, const EntityDb& db, const Player& p) {
	const std::vector<Entity>& ents = db.entities();
	const Entity* world = ents.size() > 0 ? &ents[0] : nullptr;
	const Entity* player = ents.size() > 1 ? &ents[1] : nullptr;
	for (int i = 0; i < 32; ++i) {
		for (int j = 0; j < 32; ++j) {
			const uint8_t b = map.mapFlags[i * 32 + j];
			if (b & kTileHidden) continue;
			EntityDb::TileWalk walk("Automap::buildModel");
			for (const Entity* e = db.tileHead(i * 32 + j); e != nullptr && walk.ok(e); e = e->nextOnTile) {
				if (e == world || e == player || e->def == nullptr) continue;
				const int s = e->getSprite();
				if (s < 0 || s >= map.numSprites) continue;
				const int si = map.mapSpriteInfo[s];
				if (!(si & Enums::SPRITE_FLAG_AUTOMAP_VISIBLE) || (si & 0x10000)) continue;
				const int frame = (si >> 8) & 0xFF;
				const EntityDef& d = *e->def;
				uint32_t color = 0;
				bool isLine = false;
				bool big = false;
				int need = 2;
				if (d.eType == Enums::ET_DOOR) {
					if (d.tileIndex == 271 || d.tileIndex == 272) color = 0xFFFF8400;
					else if (d.tileIndex == 273 || d.tileIndex == 274) color = 0xFF00C0FF;
					else color = 0xFF3D68E3;
					isLine = true;
				} else if (si & 0x400000) {
					color = 0xFF2A3657;
					isLine = true;
				} else if (d.eType == Enums::ET_NONOBSTRUCTING_SPRITEWALL) {
					color = 0xFF8D8068;
					isLine = true;
				} else if (d.eType == Enums::ET_NPC) {
					big = true;
					color = 0xFF0000FF;
					need = 0x80;
				} else if (d.eType == Enums::ET_ATTACK_INTERACTIVE && frame == 0) {
					color = 0xFF8000FF;
				} else if (d.eType == Enums::ET_MONSTER) {
					color = 0xFFFF8000;
					need = 0x80;
				} else if (d.eType == Enums::ET_DECOR) {
					color = (d.tileIndex == 173 || d.tileIndex == 180) ? 0 : 0xFF8D8068;
				} else if (p.god && d.eType == Enums::ET_ITEM && d.eSubType != 3) {
					color = 0xFF00FFEA;
				}
				if (color == 0 || ((b & need) == 0 && (need & 0x80) != 0)) continue;   // :190
				if (si & 0xF000000) {   // directional
					const int sx = map.mapSprites[s];
					const int sy = map.mapSprites[map.numSprites + s];
					int xa = sx, xb = sx, ya = sy, yb = sy;
					if (si & 0x3000000) { xa -= 32; xb += 32; }
					else { ya -= 32; yb += 32; }
					const int x0 = px1(xa), x1 = px(xb), y0 = px(ya), y1 = px(yb);
					if (isLine) line(m, kOx + x0, kOy + y0, kOx + x1, kOy + y1, color);
					else rect(m, kOx + ((x0 + x1) >> 1) - 2, kOy + ((y0 + y1) >> 1) - 2, 4, 4, color);
				} else if (big) {
					rect(m, kOx + kCell * j + 2, kOy + kCell * i + 2, 6, 6, color);
				} else {
					rect(m, kOx + kCell * j + 3, kOy + kCell * i + 3, 4, 4, color);
				}
			}
		}
	}
}

#ifndef NDEBUG
// Synthetic map: the 3x3 uncover skips a sight-blocking neighbour but never
// the centre, and row/column 31 are never uncovered (the `< 31` quirk).
void uncoverSelfCheck() {
	MapData m;
	m.mapFlags.assign(1024, 0);
	m.mapFlags[1 * 32 + 2] = TileFlag::BLOCKS_SIGHT;
	Automap::uncover(m, 1 * 64 + 32, 1 * 64 + 32);
	for (int y = 0; y <= 2; ++y)
		for (int x = 0; x <= 2; ++x)
			assert(((m.mapFlags[y * 32 + x] & kTileVisited) != 0) == !(x == 2 && y == 1));
	Automap::uncover(m, 31 * 64 + 32, 31 * 64 + 32);
	assert(m.mapFlags[30 * 32 + 30] & kTileVisited);
	for (int k = 0; k < 32; ++k) {
		assert(!(m.mapFlags[31 * 32 + k] & kTileVisited));
		assert(!(m.mapFlags[k * 32 + 31] & kTileVisited));
	}
}
#endif

} // namespace

void Automap::markSeen(MapData& map, const ViewVisibility& vis, bool gate) {
	if (!vis.valid) return;
	for (int i : vis.seenLines) {
		const int type = map.lineFlags[i >> 1] >> ((i & 1) << 2) & 7;
		if (gate || type == kLineTypeMap) markLine(map, i);
	}
	if (!gate) return;
	for (int s : vis.drawnSprites) map.mapSpriteInfo[s] |= Enums::SPRITE_FLAG_AUTOMAP_VISIBLE; // src/Render.cpp:877-879
}

void Automap::uncover(MapData& map, int destX, int destY) {
#ifndef NDEBUG
	static bool checked = false;
	if (!checked) {
		checked = true;
		uncoverSelfCheck();
	}
#endif
	const int n = destX >> 6;
	const int n2 = destY >> 6;
	if (n < 0 || n >= 32 || n2 < 0 || n2 >= 32) return;
	for (int i = n2 - 1; i <= n2 + 1; ++i) {
		if (i < 0 || i >= 31) continue;
		for (int j = n - 1; j <= n + 1; ++j) {
			if (j < 0 || j >= 31) continue;
			const uint8_t b = map.mapFlags[i * 32 + j];
			if ((j == n && i == n2) || (b & TileFlag::BLOCKS_SIGHT) == 0)
				map.mapFlags[i * 32 + j] = (uint8_t)(b | kTileVisited);
		}
	}
}

void Automap::giveMap(MapData& map, int x, int y, int w, int h) {
	for (int i = 0; i < map.numLines; ++i) markLine(map, i);
	for (int j = 0; j < map.numMapSprites; ++j) {
		const int tx = map.mapSprites[j] >> 6;
		const int ty = map.mapSprites[map.numSprites + j] >> 6;
		if (tx >= x && tx < x + w && ty >= y && ty < y + h)
			map.mapSpriteInfo[j] |= Enums::SPRITE_FLAG_AUTOMAP_VISIBLE;
	}
	for (int k = y; k < y + h; ++k) {
		for (int l = x; l < x + w; ++l) {
			const uint8_t b = map.mapFlags[k * 32 + l];
			if ((b & TileFlag::SOLID_AT_LOAD) == 0) map.mapFlags[k * 32 + l] = (uint8_t)(b | kTileVisited);
		}
	}
}

void Automap::buildModel(AutomapModel& m, const MapData& map, const EntityDb& db,
                         const Player& p, int64_t timeMs, Blink& blink) {
	m.prims.clear();
	// Tiles (src/AutomapController.cpp:79-101); entrance/exit start one row higher.
	for (int i = 0; i < 32; ++i) {
		for (int j = 0; j < 32; ++j) {
			const int tile = i * 32 + j;
			const uint8_t b = map.mapFlags[tile];
			if (b & kTileHidden) continue;
			if (tile == map.mapEntranceAutomap) {
				rect(m, kOx + kCell * j, kOy - 1 + kCell * i, kCell, kCell, 0xFF00FF00);
			} else if (b & kTileVisited) {
				if (tile == map.mapExitAutomap) rect(m, kOx + kCell * j, kOy - 1 + kCell * i, kCell, kCell, 0xFFFF0000);
				else if ((b & TileFlag::SOLID_AT_LOAD) == 0) rect(m, kOx + kCell * j, kOy + kCell * i, kCell, kCell, 0xFF90B9E7);
			}
		}
	}
	// Ladders (:103-118).
	for (int t : map.mapLadders) {
		if (t < 0 || t >= 1024 || !(map.mapFlags[t] & kTileVisited) || (map.mapFlags[t] & kTileHidden)) continue;
		const int x = kOx + kCell * (t % 32);
		const int y = kOy + kCell * (t / 32);
		rect(m, x, y, kCell, kCell, 0xFFFFFF00);
		for (int l = 0; l < 8; l += 2) line(m, x, y + l, x + kCell, y + l, 0xFF000000);
		line(m, x, y, x, y + kCell, 0xFF000000);
		line(m, x + kCell, y, x + kCell, y + kCell, 0xFF000000);
	}
	// Walls (:120-129): seen lines of type 0 or 6.
	for (int n = 0; n < map.numLines; ++n) {
		const int f = map.lineFlags[n >> 1] >> ((n & 1) << 2) & 0xF;
		if (!(f & kLineSeen) || ((f & 7) != 0 && (f & 7) != kLineTypeMap)) continue;
		line(m, kOx + map.lineXs[2 * n], kOy + map.lineYs[2 * n],
		     kOx + map.lineXs[2 * n + 1], kOy + map.lineYs[2 * n + 1], 0xFF2A3657);
	}
	buildEntities(m, map, db, p);
	// shortcut: quest markers need Player notebook (src/AutomapController.cpp:246-277), add with the journal
	// Cursor (:292-324).
	if (timeMs > blink.nextToggleMs) {
		blink.nextToggleMs = timeMs + 333;
		blink.on = !blink.on;
	}
	int dir = 0;
	switch (p.destAngle & 0x3FF) {   // ANGLE_NORTH/SOUTH/EAST/WEST (src/Enums.h:377-383)
	case 256: dir = 0; break;
	case 768: dir = 1; break;
	case 0:   dir = 2; break;
	case 512: dir = 3; break;
	default:  break;
	}
	m.cursorSrcY = dir * 4 + (blink.on ? 16 : 0);
	m.cursorX = kOx + (kCell * (p.viewX - 32)) / 64 + kCell / 2;
	m.cursorY = kOy + (kCell * (p.viewY - 32)) / 64 + kCell / 2;
	m.cursor = m.cursorY < 320;
}

} // namespace newcore
