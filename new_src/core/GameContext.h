#ifndef NEW_CORE_GAMECONTEXT_H
#define NEW_CORE_GAMECONTEXT_H

#include <cstdint>
#include <functional>
#include <vector>

#include "core/CinematicCamera.h"
#include "core/Familiar.h"
#include "core/GameStates.h"
#include "core/LoadingManager.h"
#include "core/LootSession.h"
#include "core/TravelMapSession.h"
#include "core/MenuSession.h"
#include "core/PlayerActions.h"
#include "core/TargetPractice.h"
#include "core/ZoomController.h"
#include "domain/game/Automap.h"
#include "domain/game/Game.h"
#include "domain/game/Targeting.h"
#include "minigames/common/MiniGameHost.h"
#include "render/SceneRenderer.h"
#include "text/Text.h"
#include "ui/AutomapModel.h"
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
		// The 2D façade owned by AppContext (ADR 0020); the world pass needs
		// it for its missing-world fallback fill.
		Graphics2D* g2d = nullptr;
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
		// Parsed menus.bin (ADR 0013), built in Main.cpp next to Tables.
		const MenuData* menus = nullptr;
		// Reads map%02d.bin (mapId-1) into *map in place, re-uploads world
		// textures, sky and map strings. False = read/parse failed.
		std::function<bool(int mapId)> loadMapFile;
	};

	void init(const Init& sys);

	// ACTION_RESTARTLEVEL (src/MenuSystem.cpp:3105-3111): loadGame(2); without
	// a usable snapshot file, a fresh start of the current level.
	void restartLevel();
	// Death-effect reset + LoadingManager::loadState. False = no usable player file.
	bool loadGame(int loadType);
	// backToMain / shutdown stand-in (ADR 0037 §6); GameLoop stops on it.
	bool quitRequested() const { return quitRequested_; }

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

	// DEBUG (golden capture runs, set by GameLoop when capture ticks are
	// given): the travel map finishes on its first tick.
	bool skipTravelMap = false;

	// PHASE5 DEBUG (removable): grants both keycards without the loot system.
	void debugGiveKeycards();

	// DEBUG (rewrite-only, removable): flips the player coordinate readout
	// drawn by Hud::drawDebugPosition. Bound to F3 in GameLoop.cpp.
	void debugToggleCoords();

	// ---- scripted cinematics (cutscenes-camera.md §1-§3) ----
	// The cutscene clock lives in its own module (spec
	// 2026-08-26-decomposition §P1-G2); the VM reaches it through here.
	CinematicCamera& cinematic() { return cinematic_; }

	// Player turn actions: input handler, arrival hooks and the script
	// GOTO/TURN_PLAYER handshake fields the VM writes (spec §P1-G7).
	PlayerActions& actions() { return actions_; }

	// Sniper scope zoom behaviour; the data is Game::zoom (ADR 0039).
	ZoomController& zoom() { return zoom_; }

	// Virtual target practice session; the VM enters it from opcode 84 (ADR 0039).
	TargetPractice& targetPractice() { return practice_; }

	// Sentry-bot control behaviour; the state lives on Player (ADR 0041).
	Familiar& familiar() { return familiar_; }

	// ST_MINI_GAME dispatcher; the VM starts minigames through it (ADR 0033).
	MiniGameHost& minigames() { return minigames_; }

	// loadMap gateway, EV_CHANGE_MAP effects, ST_SAVING tick (ADR 0035).
	LoadingManager& loading() { return loading_; }

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

	// Producer half of the view weapon (spec §6): the Player / Game::combat /
	// Tables reads, the wpinfo row lookup, the attack-pose return lerp and the
	// two flash gates that used to run inside ViewWeapon::draw.
	void buildViewWeaponModel(ViewWeaponModel& m) const;

	bool inputBlocked() const;

	void unloadMap_();       // per-map runtime teardown before the next map file is read
	void tickLoading();      // two-phase ordered tail (spec §5)
	void tickPlaying();      // legacy playing tick order (spec §6)
	bool checkDeath_();      // ST_DYING / familiar ST_BOT_DYING entry; true = state left
	void tickCamera();       // ST_CAMERA per-frame order (src/Canvas.cpp:949-958)
	void tickAutomap();      // ST_AUTOMAP (src/AutomapController.cpp:18-33)
	void tickDying();

	Init sys_;
	StateId state_ = StateId::Loading;
	int loadingPhase_ = 0;
	int64_t lastTurnTime_ = 0;
	int64_t deathTimeMs_ = 0;
	bool quitRequested_ = false;
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

	// ST_MENU screen state (spec 2026-08-28-menu §4).
	MenuSession menu_;
	// Screen the next ST_MENU entry opens; set by the caller right before
	// setState(Menu), consumed and reset by enterState_.
	int nextMenuId_ = kMenuInGame;
	int debugBotWeapon_ = 6;   // DEBUG (removable): last F5 bot weapon, cycles 3..6

	// View forward / facing probe / fire election (spec §P1-G4).
	Targeting targeting_;

	// First-person weapon quad, drawn after drawBSP (spec §P1-G5).
	ViewWeapon viewWeapon_;

	// World pass: viewport, camera, sky, BSP, sprite classification (spec §P1-G6).
	SceneRenderer scene_;

	// Playing input, movement commit, use chain, fire commit, arrival hooks
	// (spec §P1-G7).
	PlayerActions actions_;

	// Sniper zoom: initZoom/zoomOut, zoomed input, render pose (ADR 0039).
	ZoomController zoom_;

	// Target practice: enter/assessShot/exit and the canvas-side fields (ADR 0039).
	TargetPractice practice_;
	Familiar familiar_;
	Text practiceScoreText_;   // "Score:N" buffer the score bar borrows

	// ST_MINI_GAME: calling thread, active game, shared endGame tail.
	MiniGameHost minigames_;

	// Map-change gateway (ADR 0035).
	LoadingManager loading_;

	// ST_TRAVELMAP logic and the producer-side strings its model borrows.
	TravelMapSession travel_;
	Text travelLabelText_;
	Text travelNameText_;
	Text travelPromptText_;

	// ST_AUTOMAP per-frame model, cursor blink and the soft-key labels it borrows.
	AutomapModel automapModel_;
	Automap::Blink automapBlink_;
	Text automapLeftText_, automapRightText_;

	// Loading / saving box (ui.md §19.5): pacifier position and its 150 ms
	// repaint throttle on upTimeMs, plus the caption buffers the model borrows.
	static constexpr int kPacifierStart = 174;   // SCR_CX - 66
	static constexpr int kPacifierEnd = 302;     // SCR_CX + 62
	static constexpr int kPacifierMs = 150;
	int pacifierX_ = kPacifierStart;
	int64_t lastPacifierMs_ = 0;
	Text loadingCaption_;
	Text loadingSubCaption_;
	// Presented-frame counter: Saving work and Loading phase 1 wait until a
	// frame was rendered after frameAtStateEntry_ (src/LoadingManager.cpp:610-616).
	uint32_t framesRendered_ = 0;
	uint32_t frameAtStateEntry_ = 0;
};

} // namespace newcore

#endif // NEW_CORE_GAMECONTEXT_H
