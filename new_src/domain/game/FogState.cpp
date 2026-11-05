#include "domain/game/FogState.h"

#include <cassert>

namespace newcore {

void FogState::buildFogTables(int argb) {
	color = argb;
	if (((uint32_t)argb >> 24) == 0) {
		min = 32752;
		range = 1;
	}
}

void FogState::startLerp(int n, int n2, int ms, int64_t now) {
	baseMin = min;
	baseRange = range;
	destMin = n << 4;
	destRange = (n2 - n) << 4;
	if (destRange == 0) destRange = 1;
	if (ms != 0) {
		lerpStart = now;
		lerpTime = ms;
	} else {
		lerpStart = 0;
		lerpTime = 0;
		min = destMin;
		range = destRange;
	}
}

// int64 products: identical to the int32 original wherever that one does not overflow.
void FogState::tick(int64_t now) {
	if (lerpTime == 0) return;
	if (now < lerpStart + lerpTime) {
		int f = (int)(((now - lerpStart) << 12) / lerpTime);
		min = baseMin + (int)(((int64_t)(destMin - baseMin) * f) >> 12);
		range = baseRange + (int)(((int64_t)(destRange - baseRange) * f) >> 12);
		if (range == 0) range = 1;
	} else {
		lerpTime = 0;
		min = destMin;
		range = destRange;
	}
}

#ifndef NDEBUG
// Values from docs/original-code/light-flicker.md (map00 LERP_FOG table).
void FogState::selfCheck() {
	FogState s;
	s.startLerp(0, 32, 0, 0);
	assert(s.min == 0 && s.range == 512);
	s.startLerp(0, 128, 0, 0);
	assert(s.min == 0 && s.range == 2048);
	s.startLerp(32, 128, 0, 0);
	assert(s.min == 512 && s.range == 1536);
	s.startLerp(1024, 1024, 0, 0);
	assert(s.min == 16384 && s.range == 1);

	FogState off;
	off.min = 0;
	off.range = 512;
	off.buildFogTables(0);
	assert(off.color == 0 && off.min == 32752 && off.range == 1);

	s.startLerp(0, 32, 1000, 0);
	assert(s.min == 16384 && s.range == 1 && s.lerpTime == 1000);
	s.tick(500);
	assert(s.min == 8192 && s.range == 256);
	s.tick(1000);
	assert(s.min == 0 && s.range == 512 && s.lerpTime == 0);
}
#endif

} // namespace newcore
