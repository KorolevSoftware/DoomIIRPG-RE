#ifndef NEW_CORE_GAMECONTEXT_H
#define NEW_CORE_GAMECONTEXT_H

#include <cstdint>
#include <vector>

#include "core/CinematicCamera.h"
#include "core/GameStates.h"
#include "core/LootSession.h"
#include "core/PlayerActions.h"
#include "domain/game/Game.h"
#include "domain/game/Targeting.h"
#include "render/SceneRenderer.h"
#include "text/Text.h"
#include "ui/HudModel.h"
#include "ui/UiTypes.h"
#include "ui/ViewWeapon.h"

namespace newcore {

class AppContext;
class DialogSystem;
class Entity;
class EntityDefs;
class Font;
class Graphics2D;
class Hud;
class Localization;
class MapData;
class MediaLoader;
class Player;
class ScriptVM;
class Tables;
class Ui;
class World3D;

// The rewrite's Canvas analog: owns the game-state machine, clocks, pending
// input actions, per-state ticks and render orchestration. Non-owning
// pointers to all subsystems; constructed after them in main() so it dies
// first (spec §2).
class GameContext : public StateHost {
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
		DialogSystem* dialogs = nullptr;
		// Immediate-mode UI façade, built in Main.cpp next to UiAssets/UiState
		// (spec 2026-08-27-ui-layer §8). Null = no UI pass this run.
		Ui* ui = nullptr;
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

	// The frame's normalized input, handed over by GameLoop right before
	// render() (spec §3). Copied because UiInputCollector::input() is only
	// valid until the next beginFrame().
	void setUiInput(const UiInput& in) { uiInput_ = in; }

	// The ONE translation from UI intent to the existing action queue (ADR
	// 0012 point 6): mouse and keys share pendingActions_, so they cannot
	// diverge on turn semantics.
	void applyUiAction(UiAction a, int index);

	// PHASE5 DEBUG (removable): grants both keycards without the loot system.
	void debugGiveKeycards();

	// ---- scripted cinematics (cutscenes-camera.md §1-§3) ----
	// The cutscene clock lives in its own module (spec
	// 2026-08-26-decomposition §P1-G2); the VM reaches it through here.
	CinematicCamera& cinematic() { return cinematic_; }

	// Player turn actions: input handler, arrival hooks and the script
	// GOTO/TURN_PLAYER handshake fields the VM writes (spec §P1-G7).
	PlayerActions& actions() { return actions_; }

	// Clocks in ms. upTimeMs is the monotonic app clock; gameTime pauses
	// outside Playing this phase (spec deviation C5).
	int64_t upTimeMs = 0;
	int64_t gameTime = 0;
	// Input lockout latch shared with ScriptVM::evWait (src/Canvas.h:138);
	// 0 = no lockout (legacy sentinel).
	int64_t blockInputTime = 0;
	bool pauseGameTime = true;

	// StateHost (spec 2026-08-26-decomposition §1.1): the only view modules
	// get on the state machine.
	StateId state() const override { return state_; }
	void requestState(StateId s) override { setState(s); }

	StateId oldState = StateId::Loading;
	bool stateChanged = false; // cleared at the end of the same tick (src/Canvas.cpp:987)
	int stateVars[kNumStateVars] = { 0 };

private:
	void exitState_();       // hook slot: legacy AUTOMAP/MENU/CAMERA exits (src/Canvas.cpp:1030-1049)
	void enterState_(StateId s);

	// Rebuilds the HUD view model from scratch every frame (spec §4.1). Not
	// const: it refills the soft-key Text buffers the model borrows.
	void buildHudModel(HudModel& m, bool showBottomBar, bool interactive);

	bool inputBlocked() const;

	void tickLoading();      // two-phase ordered tail (spec §5)
	void tickPlaying();      // legacy playing tick order (spec §6)
	void tickCamera();       // ST_CAMERA per-frame order (src/Canvas.cpp:949-958)
	void tickDying();

	Init sys_;
	StateId state_ = StateId::Loading;
	int loadingPhase_ = 0;
	int64_t lastTurnTime_ = 0;
	int64_t deathTimeMs_ = 0;
	std::vector<Action> pendingActions_;

	// Frame input for the UI pass (levels survive across frames, edges do not).
	UiInput uiInput_;
	// Producer-side storage for the soft-key labels the HudModel borrows
	// (spec §4.1 text-slot ownership): the model only holds pointers.
	Text softLeftText_;
	Text softCenterText_;
	Text softRightText_;

	// Cinematic camera runtime (cutscenes-camera.md §1-§3).
	CinematicCamera cinematic_;
	StateId dialogPrevState_ = StateId::Playing; // dialog-close restore source (:541-554)

	// ST_LOOTING vertical slice (spec 2026-08-26-decomposition §P1-G3).
	LootSession loot_;

	// View forward / facing probe / fire election (spec §P1-G4).
	Targeting targeting_;

	// First-person weapon quad, drawn after drawBSP (spec §P1-G5).
	ViewWeapon viewWeapon_;

	// World pass: viewport, camera, sky, BSP, sprite classification (spec §P1-G6).
	SceneRenderer scene_;

	// Playing input, movement commit, use chain, fire commit, arrival hooks
	// (spec §P1-G7).
	PlayerActions actions_;
};

} // namespace newcore

#endif // NEW_CORE_GAMECONTEXT_H
