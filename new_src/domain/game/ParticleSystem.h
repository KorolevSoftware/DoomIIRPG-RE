#ifndef NEW_DOMAIN_GAME_PARTICLESYSTEM_H
#define NEW_DOMAIN_GAME_PARTICLESYSTEM_H

#include <cstdint>

namespace newcore {

class Entity;
class MapData;
class MediaLoader;
struct Tables;

// Port of src/ParticleSystem.cpp minus rendering (render/ParticleRenderer, ADR 0047).
// There is no tick: the renderer ages particles from gameTime - startTime.
class ParticleSystem {
public:
	struct Env {
		const MapData* map = nullptr;        // &g_map (stable address, ADR 0035)
		const MediaLoader* media = nullptr;  // &g_media, monster palettes
		const Tables* tables = nullptr;      // monsterColors (table 8)
		const int64_t* gameTime = nullptr;   // &ctx.gameTime
	};
	void init(const Env& env);               // freeAllParticles() + reads DOOM2RPG_NO_PARTICLES

	static constexpr int kNumEmitters = 4;   // src/ParticleSystem.h:52
	static constexpr int kPoolSize = 64;     // heads 0..3, particles 4..62, free head 63
	static constexpr int kFreeHead = 63;
	static constexpr uint8_t kEnd = 0xFF;
	static constexpr int kGibBoneMask = 0x619;                 // src/ParticleSystem.cpp:244
	static constexpr uint32_t kLevelColors[8] = {              // src/ParticleSystem.h:43
		0xFFFFFFFF, 0xFF3A332E, 0xFF626262, 0xFF7F7F7F,
		0xFFD1CF11, 0xFFFD8918, 0xFF20B00D,
		0xFFFFFFFF };  // [7]: out of bounds in src (7 entries, index & 7); defined white (ADR 0047 §5)

	void freeAllParticles();                                            // src/ParticleSystem.cpp:183-193
	void spawnParticles(int type, uint32_t color, int sprite);          // :252-255
	void spawnParticles(int type, uint32_t color, int x, int y, int z); // :257-414, world units
	void spawnMonsterBlood(const Entity* e, bool gib);                  // :228-250

	struct Emitter {
		int16_t pos[3] = {0, 0, 0};   // world << 4 (:389-391)
		int gravity = 0;
		uint8_t gibType = 0;          // 0 wood, 1 stone, 2 bone, 255 none
		uint32_t color = 0;           // ARGB, never mutated by the renderer
		int64_t startTime = 0;        // gameTime at spawn
	};
	Emitter emitters[kNumEmitters];
	// Pool (public: the renderer walks and reaps it). size: bit 3 = gib,
	// bits 0..2 = dot radius factor or gib row (src nibble packing dropped).
	uint8_t next[kPoolSize] = {};
	int8_t startX[kPoolSize] = {}, startY[kPoolSize] = {};
	int16_t velX[kPoolSize] = {}, velY[kPoolSize] = {};
	uint8_t size[kPoolSize] = {};

	// Renderer reap: moves the particle right after `prev` to the free list
	// (src/ParticleSystem.cpp:110-111: linkParticle(63, unlinkParticle(n2))).
	void freeAfter(int prev) { link(kFreeHead, unlink(prev)); }

private:
	int unlink(int head);          // :169-176
	void link(int head, int i);    // :178-181
	void freeSystem(int n);        // :195-199
	Env env_;
	int systemIdx_ = 0;
	bool disabled_ = false;        // DOOM2RPG_NO_PARTICLES set
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_PARTICLESYSTEM_H
