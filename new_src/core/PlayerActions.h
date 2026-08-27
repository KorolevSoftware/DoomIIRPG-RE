#ifndef NEW_CORE_PLAYERACTIONS_H
#define NEW_CORE_PLAYERACTIONS_H

#include "core/GameStates.h"

namespace newcore {

class Game;
class Hud;
class Localization;
class MapData;
class Player;
struct ScriptThread;
class ScriptVM;
class Targeting;

// Everything the player does on his turn (spec 2026-08-26-decomposition
// §P1-G7): the ST_PLAYING input handler (src/PlayingInputHandler.cpp), the
// movement/rotation arrival hooks (src/MovementController.cpp:160-224) and the
// spawn placement (src/Game.cpp:941-972). Moved verbatim out of GameContext.
class PlayerActions {
public:
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		Hud* hud = nullptr;
		const Localization* loc = nullptr;
		Targeting* targeting = nullptr;
		StateHost* host = nullptr;
	};

	void init(const Env& env);

	// legacy Game::spawnPlayer (src/Game.cpp:941-972). The turn-clock stamp
	// stays at the GameContext call site (spec §P1-G7).
	void spawnPlayer();

	void handleAction(Action a);

	// legacy MovementController::finishMovement (:160-196).
	void finishMovement();
	// finishRotation(true) analog, also invoked by the VM's instant-GOTO path
	// (src/ScriptThread.cpp:641).
	void finishRotationFired();
	int flagForFacingDir(int i) const; // src/MovementController.cpp:214-224

	// Script-driven movement handshake storage (cutscenes-camera.md §4):
	// animated GOTO/TURN_PLAYER parks its thread in gotoThread and
	// tickPlaying's arrival handling resumes it once view==dest and the angle
	// settled (src/MovementController.cpp:164-167, :298-302). gotoTriggered
	// defers an instant GOTO's destination events to the next playing tick
	// (src/MovementController.cpp:503-509).
	ScriptThread* gotoThread = nullptr;
	bool gotoTriggered = false;

private:
	Env env_;
};

} // namespace newcore

#endif // NEW_CORE_PLAYERACTIONS_H
