#ifndef NEW_DOMAIN_GAME_GAMESPRITES_H
#define NEW_DOMAIN_GAME_GAMESPRITES_H

#include <cstdint>

namespace newcore {

class MapData;
class Player;
class SpriteLerps;

// Peer subsystem owning the 48 runtime sprites (missiles, impact and claw
// effects) that live in the custom slots appended to the map sprite arrays
// (ADR 0030). Port of Game::gsprite_* (src/Game.cpp:1283-1570) for the flag
// bits the ported missiles and effects use. Positions are kept in legacy
// baked Z space and written to storage through SpriteLerps::spriteZBias.
class GameSprites {
public:
	static constexpr int kMax = 48;                    // MAX_CUSTOM_SPRITES (src/Render.h:53)

	// GameSprite::FLAG_* (src/GameSprite.h:11-22), the ported subset.
	static constexpr int kFlagInUse = 0x1;
	static constexpr int kFlagPropagate = 0x2;
	static constexpr int kFlagAnim = 0x40;
	static constexpr int kFlagNoExpire = 0x200;
	static constexpr int kFlagScale = 0x400;
	static constexpr int kFlagUnlink = 0x800;      // unlink on destroy: a no-op here, hidden is enough
	static constexpr int kFlagFacePlayer = 0x1000; // positions relative to the player view

	struct Sprite {                                    // legacy GameSprite subset
		int slot = -1;                                 // map sprite index (firstCustomSprite + i)
		int flags = 0;
		int16_t pos[6] = {};                           // start xyz, end xyz (legacy baked Z)
		int vel[3] = {};                               // units per 1000 ms
		int duration = 0;
		int64_t time = 0;                              // upTimeMs at alloc
		int numAnimFrames = 0;
		uint8_t startScale = 64, destScale = 64;
		int scaleStep = 0;
	};

	struct Env {
		MapData* map = nullptr;
		const SpriteLerps* lerps = nullptr;            // spriteZBias
		const Player* player = nullptr;                // view position for kFlagFacePlayer
	};

	void init(const Env& env);                         // all free; slots stay hidden

	// gsprite_alloc (src/Game.cpp:1294-1325). nullptr when all 48 are taken
	// (legacy fatal Error 34).
	Sprite* alloc(int tile, int frames, int flags, int64_t now);
	// gsprite_allocAnim (src/Game.cpp:1327-1401): cases 242 and 245/246/247;
	// every other tile takes the default 4 frames x 200 ms.
	Sprite* allocAnim(int tile, int x, int y, int z, int64_t now);
	// gsprite_destroy (src/Game.cpp:1403-1462), non-LERP_ENT/TRUNC path.
	void destroy(Sprite* s);
	void clear(int flagMask);                          // src/Game.cpp:1283-1292
	void update(int64_t now);                          // src/Game.cpp:1464-1570

	int activePropogators() const { return activePropogators_; }
	int activeSprites() const { return activeSprites_; }

	// S_X/S_Y and stored Z = zBaked - spriteZBias (also Combat::allocMissile).
	void write(const Sprite& s, int x, int y, int zBaked);

private:
	Env env_;
	Sprite sprites_[kMax];
	int activeSprites_ = 0;
	int activePropogators_ = 0;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_GAMESPRITES_H
