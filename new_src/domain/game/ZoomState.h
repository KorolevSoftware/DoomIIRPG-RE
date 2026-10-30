#ifndef NEW_DOMAIN_GAME_ZOOMSTATE_H
#define NEW_DOMAIN_GAME_ZOOMSTATE_H

#include <cstdint>

namespace newcore {

// Legacy Canvas zoom fields (src/Canvas.h:330-344). Plain data on Game::zoom so domain
// readers (Targeting, CombatEntity, TargetPractice) see it; behaviour is core/ZoomController.
struct ZoomState {
	bool isZoomedIn = false;
	int zoomFOV = 0, zoomDestFOV = 0;
	int zoomMinFOVPercent = 0, zoomCurFOVPercent = 0;
	int64_t zoomTime = 0;          // upTimeMs deadline of the FOV lerp, 0 = idle
	int zoomAngle = 0, zoomPitch = 0;
	int64_t zoomStateTime = 0;     // upTimeMs at initZoom (sway phase)
	int zoomAccuracy = 0, zoomMaxAngle = 0;
	int zoomCollisionX = 0, zoomCollisionY = 0, zoomCollisionZ = 0;
	int zoomTurn = 0;
};

// One frame's zoomed camera (src/MovementController.cpp:541 renderScene arguments).
struct ZoomPose { int yaw = 0, pitch = 0, fov = 290; };

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ZOOMSTATE_H
