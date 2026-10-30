#include "domain/game/ShotZones.h"

#include <bitset>
#include <cstdio>

#include "io/Media.h"

namespace newcore {

int shotZone(const MediaMappings& m, int tileIndex, int dx, int dy, int dz,
             int headZLo, int headZHi) {
	if (tileIndex < 0 || tileIndex >= (int)m.mappings.size()) return -1;
	const int base = m.mappings[tileIndex];
	static constexpr int kFrames[3] = { 3, 2, 0 };
	int box[3][4];
	for (int i = 0; i < 3; ++i) {
		const int k = (kFrames[i] + base) << 2;
		if (base < 0 || k + 3 >= (int)m.bounds.size()) return -1;
		auto b = [&](int j) { return m.bounds[k + j] & 0xFF; };
		box[i][0] = b(0) * 64 / 176 - 32;
		box[i][1] = b(1) * 64 / 176 - 32;
		box[i][2] = (176 - b(3)) * 64 / 176 - 32;
		box[i][3] = (176 - b(2)) * 64 / 176 - 32;
	}
	box[0][2] += headZLo;
	box[0][3] += headZHi;
	// Self-check: one line per tile the first time it is evaluated.
	static std::bitset<MediaMappings::kMaxMappings> logged;
	if (tileIndex < MediaMappings::kMaxMappings && !logged[tileIndex]) {
		logged[tileIndex] = true;
		std::fprintf(stderr, "[zones] tile=%d head=(%d..%d z%d..%d) body=(%d..%d z%d..%d) legs=(%d..%d z%d..%d)\n",
			tileIndex, box[0][0], box[0][1], box[0][2], box[0][3], box[1][0], box[1][1], box[1][2], box[1][3],
			box[2][0], box[2][1], box[2][2], box[2][3]);
	}
	for (int i = 0; i < 3; ++i) {
		// dy is tested against the horizontal range, verbatim (src/Player.cpp:2564).
		if (dx > box[i][0] && dx < box[i][1] && dy > box[i][0] && dy < box[i][1] &&
		    dz > box[i][2] && dz < box[i][3]) {
			return i;
		}
	}
	return -1;
}

} // namespace newcore
