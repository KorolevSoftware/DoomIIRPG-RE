#include "domain/game/GameSprites.h"

#include <cstdio>

#include "domain/game/Enums.h"
#include "domain/game/Player.h"
#include "domain/game/SpriteLerps.h"
#include "domain/world/MapData.h"

namespace newcore {

void GameSprites::init(const Env& env) {
	env_ = env;
	for (int i = 0; i < kMax; ++i) {
		sprites_[i] = Sprite{};
		sprites_[i].slot = env_.map->firstCustomSprite() + i;   // Render::customSprites (src/Render.cpp:2497-2498)
	}
	activeSprites_ = 0;
	activePropogators_ = 0;
}

void GameSprites::write(const Sprite& s, int x, int y, int zBaked) {
	MapData& m = *env_.map;
	const int n = m.numSprites;
	const int16_t sx = (int16_t)x;
	const int16_t sy = (int16_t)y;
	m.mapSprites[s.slot + 0 * n] = sx;
	m.mapSprites[s.slot + 1 * n] = sy;
	m.mapSprites[s.slot + 2 * n] = (int16_t)(zBaked - env_.lerps->spriteZBias(s.slot, sx, sy));
}

GameSprites::Sprite* GameSprites::alloc(int tile, int frames, int flags, int64_t now) {
	int i = 0;
	while (i < kMax && (sprites_[i].flags & kFlagInUse) != 0) ++i;   // :1297-1298
	if (i == kMax) {
		std::fprintf(stderr, "[gsprite] ERR_MAX_CUSTOMSPRITES(34): all %d slots in use\n", kMax);
		return nullptr;                                              // legacy fatal Error 34 (:1299-1302)
	}
	Sprite& s = sprites_[i];
	for (int k = 0; k < 6; ++k) s.pos[k] = 0;                        // :1305-1307
	s.vel[0] = s.vel[1] = s.vel[2] = 0;
	s.duration = 0;
	s.time = now;
	MapData& m = *env_.map;
	const int n = m.numSprites;
	m.mapSprites[s.slot + 7 * n] = -1;                               // S_ENT (:1314)
	m.mapSprites[s.slot + 3 * n] = 0;                                // S_RENDERMODE
	m.mapSprites[s.slot + 8 * n] = 64;                               // S_SCALEFACTOR
	s.flags = flags | kFlagInUse;                                    // :1317
	m.mapSpriteInfo[s.slot] = tile | (frames << 8);                  // clears the hidden bit (:1318)
	if ((s.flags & kFlagPropagate) != 0) ++activePropogators_;       // :1319-1321
	++activeSprites_;
	return &s;
}

GameSprites::Sprite* GameSprites::allocAnim(int tile, int x, int y, int z, int64_t now) {
	Sprite* s = alloc(tile, 0, kFlagAnim | kFlagPropagate, now);    // gsprite_alloc(n, 0, 66) (:1330)
	if (s == nullptr) return nullptr;
	s->numAnimFrames = 4;
	s->pos[0] = s->pos[3] = (int16_t)x;
	s->pos[1] = s->pos[4] = (int16_t)y;
	s->pos[2] = s->pos[5] = (int16_t)z;
	s->startScale = s->destScale = 64;
	s->duration = 200 * s->numAnimFrames;
	const int n = env_.map->numSprites;
	switch (tile) {
	case Enums::TILENUM_FIRE_BALL:                                   // :1360-1363
		env_.map->mapSprites[s->slot + 3 * n] = 3;
		write(*s, x, y, z);
		break;
	case Enums::TILENUM_MONSTER_CLAW:                                // :1369-1387
	case Enums::TILENUM_MONSTER_BITE:
	case Enums::TILENUM_MONSTER_BLUNT_TRAUMA: {
		const Player& p = *env_.player;
		s->duration = 200;
		s->flags |= kFlagFacePlayer | kFlagPropagate;
		s->numAnimFrames = 1;
		s->pos[0] = s->pos[3] = -6;
		s->pos[1] = s->pos[4] = 0;
		s->vel[0] = s->vel[1] = s->vel[2] = 0;
		s->pos[5] -= (int16_t)p.viewZ;
		s->pos[2] = s->pos[5];
		write(*s,
			p.viewX + p.viewStepX + (p.viewStepX >> 6) * s->pos[0] + (p.viewRightStepX >> 6) * s->pos[1],
			p.viewY + p.viewStepY + (p.viewStepY >> 6) * s->pos[0] + (p.viewRightStepY >> 6) * s->pos[1],
			p.viewZ + s->pos[2]);
		break;
	}
	default:                                                         // other tiles: not ported, 4 x 200 ms
		write(*s, x, y, z);
		break;
	}
	// relinkSprite (:1399): the renderer recomputes BSP leaves every frame.
	return s;
}

void GameSprites::destroy(Sprite* s) {
	// FLAG_LERP_ENT (0x2000) and the parabola/trunc bounce (0x4000/0x8000)
	// are not ported (:1406-1420, :1422-1452); FLAG_UNLINK's unlinkSprite
	// (:1453-1455) is covered by the hidden bit.
	env_.map->mapSpriteInfo[s->slot] |= Enums::SPRITE_FLAG_HIDDEN;
	s->flags &= ~kFlagInUse;
}

void GameSprites::clear(int flagMask) {
	for (Sprite& s : sprites_) {
		if ((s.flags & kFlagInUse) != 0 && (s.flags & flagMask) != 0) destroy(&s);
	}
}

void GameSprites::update(int64_t now) {
	if (activeSprites_ == 0) return;                                 // :1467-1469
	activeSprites_ = 0;
	activePropogators_ = 0;
	const Player& p = *env_.player;
	MapData& m = *env_.map;
	const int n = m.numSprites;
	for (Sprite& s : sprites_) {
		if ((s.flags & kFlagInUse) == 0) continue;
		const int n2 = (int)(now - s.time);
		if (n2 < 0) {                                                // :1478-1483
			if ((s.flags & kFlagPropagate) != 0) ++activePropogators_;
			++activeSprites_;
		} else if ((s.flags & kFlagNoExpire) == 0 && n2 >= s.duration) {   // :1484-1489
			destroy(&s);
		} else {
			++activeSprites_;
			if ((s.flags & kFlagAnim) != 0) {                        // :1493-1495
				m.mapSpriteInfo[s.slot] = (m.mapSpriteInfo[s.slot] & 0xFFFF00FF) |
				                          ((n2 / 200 % s.numAnimFrames) << 8);
			}
			if ((s.flags & kFlagFacePlayer) != 0) {                  // :1496-1530
				const int16_t n5 = (int16_t)(p.viewX + p.viewStepX);
				const int16_t n6 = (int16_t)(p.viewY + p.viewStepY);
				if ((s.flags & kFlagPropagate) != 0) {
					if (n2 > s.duration) {
						write(s, n5 + (p.viewStepX >> 6) * s.pos[3] + (p.viewRightStepX >> 6) * s.pos[4],
						         n6 + (p.viewStepY >> 6) * s.pos[3] + (p.viewRightStepY >> 6) * s.pos[4],
						         p.viewZ + s.pos[5]);
						s.flags &= ~kFlagPropagate;
					} else {
						const int n7 = s.pos[0] + s.vel[0] * n2 / 1000;
						const int n8 = s.pos[1] + s.vel[1] * n2 / 1000;
						write(s, n5 + (p.viewStepX >> 6) * n7 + (p.viewRightStepX >> 6) * n8,
						         n6 + (p.viewStepY >> 6) * n7 + (p.viewRightStepY >> 6) * n8,
						         p.viewZ + s.pos[2] + s.vel[2] * n2 / 1000);
					}
					++activePropogators_;
				} else {
					write(s, n5, n6, p.viewZ);
				}
				// relinkSprite (:1522-1529): leaves are recomputed every frame.
			} else if ((s.flags & kFlagPropagate) != 0) {            // :1531-1557 (0x4000/0x8000 not ported)
				++activePropogators_;
				if (n2 >= s.duration) {
					write(s, s.pos[3], s.pos[4], s.pos[5]);
					s.flags &= ~kFlagPropagate;
				} else {
					write(s, s.pos[0] + s.vel[0] * n2 / 1000, s.pos[1] + s.vel[1] * n2 / 1000,
					         s.pos[2] + s.vel[2] * n2 / 1000);
				}
			}
			if ((s.flags & kFlagScale) != 0) {                       // :1558-1566
				if (n2 > s.duration) {
					m.mapSprites[s.slot + 8 * n] = s.destScale;
					s.flags &= ~kFlagScale;
				} else {
					m.mapSprites[s.slot + 8 * n] = (uint8_t)(s.startScale + s.scaleStep * n2 / 1000);
				}
			}
		}
	}
}

} // namespace newcore
