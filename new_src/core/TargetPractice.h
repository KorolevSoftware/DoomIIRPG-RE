#ifndef NEW_CORE_TARGETPRACTICE_H
#define NEW_CORE_TARGETPRACTICE_H

#include <cstdint>

#include "core/GameStates.h"

namespace newcore {

class CinematicCamera;
class DialogSystem;
class Entity;
class Game;
class Hud;
class Localization;
class MapData;
class MediaLoader;
class Player;
class PlayerActions;
struct ScriptThread;
class ScriptVM;
class ZoomController;

// Virtual target practice session (ADR 0039): Player::enterTargetPractice /
// assessTargetPracticeShot / exitTargetPractice (src/Player.cpp:2521-2634) plus the
// Canvas-side fields save*, *ShotTime and targetPracticeThread. Stays in ST_PLAYING.
class TargetPractice {
public:
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		Hud* hud = nullptr;
		const Localization* loc = nullptr;
		MapData* map = nullptr;
		const MediaLoader* media = nullptr;
		ScriptVM* vm = nullptr;
		DialogSystem* dialogs = nullptr;
		PlayerActions* actions = nullptr;
		ZoomController* zoom = nullptr;
		StateHost* host = nullptr;
		const CinematicCamera* cinematic = nullptr;   // isCameraActive gate of showHelp
		const int64_t* upTimeMs = nullptr;
	};

	void init(const Env& env);
	bool active() const { return active_; }            // Player::inTargetPractice
	int score() const { return score_; }
	void enter(int tileX, int tileY, int dir, ScriptThread* thread);  // src/Player.cpp:2521-2551
	void assessShot(Entity* target);                                  // :2553-2598
	void exit();                                                      // :2600-2634
	// Player::reset's inTargetPractice/targetPracticeScore clear (src/Player.cpp:504-505):
	// no restore, no resume; the parked thread dies with the VM pool.
	void reset();
	// Render.cpp:1672-1713 layer choice: out = {legs, body, head, board}, -1 = skip.
	// Mutates: an expired timer is reset to 0 and its layer skipped this frame (verbatim).
	void layerFrames(int64_t now, bool zoomed, int out[4]);

private:
	Env env_;
	bool active_ = false;
	ScriptThread* thread_ = nullptr;           // canvas->targetPracticeThread
	int score_ = 0;
	int saveX_ = 0, saveY_ = 0, saveZ_ = 0, saveAngle_ = 0;
	int64_t headShotTime_ = 0, bodyShotTime_ = 0, legShotTime_ = 0;
};

} // namespace newcore

#endif // NEW_CORE_TARGETPRACTICE_H
