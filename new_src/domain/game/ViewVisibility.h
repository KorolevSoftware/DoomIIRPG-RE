#ifndef NEW_DOMAIN_GAME_VIEWVISIBILITY_H
#define NEW_DOMAIN_GAME_VIEWVISIBILITY_H

#include <cstdint>
#include <vector>

namespace newcore {

// What the last world frame drew (ADR 0029). Filled by World3D::drawBSP,
// copied into Game::visibility by GameContext::render (the only writer) and
// consumed by the simulation on the next tick.
struct ViewVisibility {
	bool valid = false;        // a world frame was rendered since the last consume
	bool cinematic = false;    // that frame was rendered in ST_CAMERA (src/Canvas.cpp:1211)
	std::vector<int> drawnSprites;   // sprites the legacy renderBSP would submit (visited leaves + split rescue), ADR 0031
	std::vector<int> seenLines;      // drawNodeLines candidates for line bit 8 (ADR 0036)
	uint32_t tiles[32] = {};        // bit x of tiles[y]: checkTileVisibilty per tile (ADR 0031)
	bool tileVisible(int tx, int ty) const {
		return tx >= 0 && ty >= 0 && tx < 32 && ty < 32 && ((tiles[ty] >> tx) & 1u) != 0;
	}
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_VIEWVISIBILITY_H
