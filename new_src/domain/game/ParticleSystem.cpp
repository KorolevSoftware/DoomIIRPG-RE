#include "domain/game/ParticleSystem.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "domain/game/Entity.h"
#include "domain/game/Enums.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Media.h"
#include "io/Tables.h"

namespace newcore {

namespace {

int nextByte() {
	return std::rand() & 0xFF;   // src/App.cpp:506-512, same stream as Combat::nextByte
}

} // namespace

void ParticleSystem::init(const Env& env) {
	env_ = env;
	disabled_ = std::getenv("DOOM2RPG_NO_PARTICLES") != nullptr;
	freeAllParticles();
}

int ParticleSystem::unlink(int head) {
	const int i2 = next[head];
	if (i2 != kEnd) {
		next[head] = next[i2];
		next[i2] = kEnd;
	}
	return i2;
}

void ParticleSystem::link(int head, int i) {
	next[i] = next[head];
	next[head] = (uint8_t)i;
}

void ParticleSystem::freeAllParticles() {
	next[kFreeHead] = kEnd;
	for (int i = 0; i < kFreeHead; ++i) {
		if (i < kNumEmitters)
			next[i] = kEnd;
		else
			link(kFreeHead, i);
	}
#ifndef NDEBUG
	int n = 0;
	for (int i = next[kFreeHead]; i != kEnd; i = next[i]) ++n;
	assert(n == kFreeHead - kNumEmitters);
#endif
}

void ParticleSystem::freeSystem(int n) {
	for (int i = unlink(n); i != kEnd; i = unlink(n))
		link(kFreeHead, i);
}

void ParticleSystem::spawnMonsterBlood(const Entity* e, bool gib) {
	if (e == nullptr || e->def == nullptr) return;
	const MapData& map = *env_.map;
	const int s = e->getSprite();
	const int tile = map.mapSpriteInfo[s] & 0xFF;
	// getPalette(tile, 0, 0) (src/Render.cpp:2039-2041)
	const int pal = env_.media->paletteIndexFor(env_.media->mappings().mappings[tile]);
	const int ci = e->def->eSubType;
	uint32_t color = 0;   // unresolved palette / table index -> black (never in src)
	if (pal >= 0 && ci < (int)env_.tables->monsterColors.size()) {
		const uint32_t p = env_.media->palette(pal).colors[env_.tables->monsterColors[ci]];
		color = ((p & 0x7E0) << 5) | ((p & 0xF800) << 8) | ((p & 0x1F) << 3);
	}
	const int dz = e->def->eType == Enums::ET_CORPSE ? -26 : 0;
	const int type = (gib && ci < 32 && (kGibBoneMask & (1 << ci)) != 0) ? 4 : 1;
	spawnParticles(type, color, map.mapSprites[s], map.mapSprites[map.numSprites + s],
		map.spriteZ(s) + dz);
}

void ParticleSystem::spawnParticles(int type, uint32_t color, int sprite) {
	const MapData& map = *env_.map;
	if (sprite < 0 || sprite >= map.numSprites) return;
	spawnParticles(type, color, map.mapSprites[sprite], map.mapSprites[map.numSprites + sprite],
		map.spriteZ(sprite));
}

void ParticleSystem::spawnParticles(int type, uint32_t color, int x, int y, int z) {
	if (disabled_) return;   // verification switch (ADR 0047 §8): no RNG, no state change
	int sxLo = -64, sxHi = 64, syLo = -64, syHi = 64;
	int vxLo = -128, vxHi = 128, vyLo = -32, vyHi = 32;
	const int sizeLo = 4, sizeHi = 7;
	int gravity = -196;
	int dots = 15, gibs = 0;
	uint8_t gibType = 0xFF;
	switch (type) {   // src/ParticleSystem.cpp:273-379
	case 0:
		vxLo = 0; vxHi = 0; vyLo = 0; vyHi = 0;
		gravity = 0; gibType = 0; gibs = 5;
		break;
	case 1:
		sxLo >>= 2; sxHi >>= 2; syLo >>= 2; syHi >>= 2;
		vyLo = 96; vyHi = 110;
		break;
	case 7:
		sxLo >>= 3; sxHi >>= 3; syLo >>= 3; syHi >>= 3;
		vxLo = -128; vxHi = 128; vyLo = -128; vyHi = 128;
		break;
	case 5:
		sxLo >>= 3; sxHi >>= 3; syLo >>= 3; syHi >>= 3;
		dots = 8;
		vxLo = -128; vxHi = 128; vyLo = -128; vyHi = 128;
		break;
	case 3:
		sxLo >>= 2; sxHi >>= 2; syLo >>= 2; syHi >>= 2;
		vxLo = -32; vxHi = 32; vyLo = 118; vyHi = 128;
		gibs = 5; gibType = 1;
		break;
	case 2:
		sxLo >>= 2; sxHi >>= 2; syLo >>= 2; syHi >>= 2;
		vxLo = -32; vxHi = 32; vyLo = 64; vyHi = 78;
		gibs = 0; gibType = 0xFF;
		break;
	case 4:
		sxLo >>= 2; sxHi >>= 2; syLo >>= 2; syHi >>= 2;
		vxLo = -32; vxHi = 32; vyLo = 64; vyHi = 78;
		gibs = 5; gibType = 2;
		break;
	case 6:
		sxLo >>= 1; sxHi >>= 1; syLo = -64; syHi = -40;
		gibs = 0;
		vxLo = 0; vxHi = 0; vyLo = 0; vyHi = -128;
		break;
	case 8:
		sxLo >>= 2; sxHi >>= 2; syLo = -15; syHi = 15;
		dots = 8; gibs = 0;
		vxLo = 0; vxHi = -128; vyLo = 0; vyHi = -128;
		break;
	}
	const int idx = systemIdx_;
	systemIdx_ = (systemIdx_ + 1) % kNumEmitters;
	freeSystem(idx);
	Emitter& em = emitters[idx];
	em.color = color;
	em.gravity = gravity;
	em.gibType = gibType;
	em.startTime = *env_.gameTime;
	em.pos[0] = (int16_t)(x << 4);
	em.pos[1] = (int16_t)(y << 4);
	em.pos[2] = (int16_t)(z << 4);
	std::fprintf(stderr, "[particles] type=%d color=%08X at (%d,%d,%d) emitter=%d\n",
		type, color, x, y, z, idx);
	while (gibs > 0 || dots > 0) {
		const int p = unlink(kFreeHead);
		if (p == kEnd) return;
		int s;
		if (dots > 0) {
			s = (sizeLo + ((sizeHi - sizeLo) * nextByte() >> 8)) & 7;
			--dots;
		} else {
			s = (nextByte() % 4) | 8;
			--gibs;
		}
		startX[p] = (int8_t)(sxLo + ((sxHi - sxLo) * nextByte() >> 8));
		startY[p] = (int8_t)(syLo + ((syHi - syLo) * nextByte() >> 8));
		velX[p] = (int16_t)(vxLo + ((vxHi - vxLo) * nextByte() >> 8));
		velY[p] = (int16_t)(vyLo + ((vyHi - vyLo) * nextByte() >> 8));
		size[p] = (uint8_t)s;
		link(idx, p);
	}
}

} // namespace newcore
