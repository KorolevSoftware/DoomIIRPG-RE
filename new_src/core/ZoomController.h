#ifndef NEW_CORE_ZOOMCONTROLLER_H
#define NEW_CORE_ZOOMCONTROLLER_H

#include <cstdint>
#include <functional>

#include "core/GameStates.h"
#include "domain/game/ZoomState.h"

namespace newcore {

class Game;
class Hud;
class Localization;
class Player;
class PlayerActions;
class Tables;

// Sniper scope zoom behaviour (ADR 0039): port of src/ZoomController.cpp plus the
// zoomed render pose (src/MovementController.cpp:525-541). The data lives on Game::zoom.
// Out of scope: touch dial drag, the scope fire button, accelerometer and centerMouse.
class ZoomController {
public:
	struct Env {
		Game* game = nullptr;            // zoom data, combat, monsters/lerps/sprites gates, advanceTurn
		Player* player = nullptr;
		Hud* hud = nullptr;
		const Localization* loc = nullptr;   // msg 45
		const Tables* tables = nullptr;      // sinTable
		PlayerActions* actions = nullptr;    // finishRotationFired
		StateHost* host = nullptr;
		std::function<bool()> inTargetPractice;   // Player::inTargetPractice; wired in G4, empty = false
		const int64_t* upTimeMs = nullptr;
		const int64_t* gameTime = nullptr;
	};

	void init(const Env& env);
	bool zoomedIn() const;
	void initZoom();                     // src/ZoomController.cpp:21-43
	void zoomOut();                      // :45-57
	void forceRemove();                  // Player::forceRemoveFromScopeZoom, src/Player.cpp:2762-2769
	void drop();                         // setState(DIALOG/DYING) inline zoom drop, src/Canvas.cpp:1085-1091, :1116-1124
	bool handle(Action a);               // handleZoomEvents(key, a, false), :63-167; false = FIRE, caller continues
	bool renderPose(ZoomPose& out);      // src/MovementController.cpp:525-541; false = not zoomed

private:
	Env env_;
};

} // namespace newcore

#endif // NEW_CORE_ZOOMCONTROLLER_H
