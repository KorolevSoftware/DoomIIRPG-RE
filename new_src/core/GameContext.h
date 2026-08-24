#ifndef NEW_CORE_GAMECONTEXT_H
#define NEW_CORE_GAMECONTEXT_H

#include <cstdint>
#include <vector>

#include "render/Camera3D.h"

namespace newcore {

class AppContext;
class EntityDefs;
class Font;
class Game;
class Hud;
class Localization;
class MapData;
class MediaLoader;
class Player;
class ScriptVM;
struct ScriptThread;
class Tables;
class World3D;

// Legacy Canvas state ids (subset used this phase; src/Canvas.h:68-93
// numbering kept). Menu/Intro/Combat/Dialog/Automap/Camera/TravelMap/...
// are documented out-of-scope stubs (spec 2026-08-23-phase5-skeleton §3):
// not enumerated, unreachable; script requests for them are logged by the VM.
enum class StateId : int {
	Playing = 3,  // legacy ST_PLAYING
	Loading = 7,  // legacy ST_LOADING
	Dying   = 13, // legacy ST_DYING (stub)
};

// Discrete playing actions (legacy getKeyAction subset,
// src/InputEventController.cpp:47-141).
enum class Action : int { None, Forward, Back, TurnLeft, TurnRight, Use };

// The rewrite's Canvas analog: owns the game-state machine, clocks, pending
// input actions, per-state ticks and render orchestration. Non-owning
// pointers to all subsystems; constructed after them in main() so it dies
// first (spec §2).
class GameContext {
public:
	static constexpr int kTickMs = 15;   // one fixed-step quantum
	static constexpr int kNumStateVars = 9;

	struct Init {
		MapData* map = nullptr;
		const EntityDefs* defs = nullptr;
		const Tables* tables = nullptr;
		const Localization* loc = nullptr;
		const Font* font = nullptr;
		MediaLoader* media = nullptr;
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		Hud* hud = nullptr;
		World3D* world = nullptr;
	};

	void init(const Init& sys);

	// Single mutator, mirrors Canvas::setState (src/Canvas.cpp:1020-1217):
	// latch stateChanged, zero stateVars[9], exit hooks, swap, enter hooks.
	void setState(StateId s);

	// One fixed-step tick (GameLoop drives quanta) and per-frame render.
	void tick();
	void render(AppContext& app);

	// Queued key actions (legacy canvas->events queue); consumed each tick.
	void queueAction(Action a) { pendingActions_.push_back(a); }

	// PHASE5 DEBUG (removable): grants both keycards without the loot system.
	void debugGiveKeycards();

	// Dialogs-lite modal (task FIX B): the ST_DIALOG analog. ScriptVM raises
	// it when EV_DIALOG parks a thread at unpauseTime == -1
	// (src/ScriptThread.cpp:582); while up, input routes to the dismiss
	// handler instead of gameplay (src/InputEventController.cpp:331-333) and
	// one Action::Use press = one dismissal step (ACTION_FIRE,
	// src/DialogSystem.cpp:34-48). Returns false if a dialog is already
	// modal (nesting guard): state stays untouched and the caller must leave
	// its thread parked at unpauseTime == -1.
	bool enterScriptDialog(ScriptThread* t);

	// Clocks in ms. upTimeMs is the monotonic app clock; gameTime pauses
	// outside Playing this phase (spec deviation C5).
	int64_t upTimeMs = 0;
	int64_t gameTime = 0;
	// Input lockout latch shared with ScriptVM::evWait (src/Canvas.h:138);
	// 0 = no lockout (legacy sentinel).
	int64_t blockInputTime = 0;
	bool pauseGameTime = true;

	StateId state = StateId::Loading;
	StateId oldState = StateId::Loading;
	bool stateChanged = false; // cleared at the end of the same tick (src/Canvas.cpp:987)
	int stateVars[kNumStateVars] = { 0 };

private:
	void exitState_();       // hook slot: legacy AUTOMAP/MENU/CAMERA exits (src/Canvas.cpp:1030-1049)
	void enterState_(StateId s);

	bool inputBlocked() const;
	void runInputEvents();
	void dismissDialogStep();   // dialog-modal input tick (see enterScriptDialog)
	void handlePlayingAction(Action a);

	void tickLoading();      // two-phase ordered tail (spec §5)
	void tickPlaying();      // legacy playing tick order (spec §6)
	void tickDying();

	void spawnPlayer();      // legacy Game::spawnPlayer (src/Game.cpp:941-972)
	void finishMovement();   // legacy MovementController::finishMovement (:160-196)
	void finishRotationFired(); // legacy finishRotation(true) FACE-event part (:284-310)
	int flagForFacingDir(int i) const; // src/MovementController.cpp:214-224
	int getHeight(int x, int y) const;

	Init sys_;
	Camera3D camera_;
	int loadingPhase_ = 0;
	int64_t lastTurnTime_ = 0;
	int64_t deathTimeMs_ = 0;
	std::vector<Action> pendingActions_;
	// Dialogs-lite modal (task FIX B): thread parked at unpauseTime == -1 on
	// EV_DIALOG; kept alive until dismissed, which is what holds the input
	// block (legacy blocks via the ST_DIALOG state itself).
	bool scriptDialogActive_ = false;
	ScriptThread* scriptDialogThread_ = nullptr;
};

} // namespace newcore

#endif // NEW_CORE_GAMECONTEXT_H
