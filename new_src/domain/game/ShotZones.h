#ifndef NEW_DOMAIN_GAME_SHOTZONES_H
#define NEW_DOMAIN_GAME_SHOTZONES_H

namespace newcore {

struct MediaMappings;

// Render::getImageFrameBounds(tile, 3, 2, 0) (src/Render.cpp:2025-2036) + the zone loop of
// Player::assessTargetPracticeShot (src/Player.cpp:2561-2568) / CombatEntity::calcHit
// (src/CombatEntity.cpp:180-202). Returns 0 = head (frame 3), 1 = body (frame 2),
// 2 = legs (frame 0), -1 = no zone. headZLo/headZHi: additive zone-0 Z tweak (revenant).
int shotZone(const MediaMappings& m, int tileIndex, int dx, int dy, int dz,
             int headZLo = 0, int headZHi = 0);

} // namespace newcore

#endif // NEW_DOMAIN_GAME_SHOTZONES_H
