#ifndef NEW_CORE_GAMECONTEXT_H
#define NEW_CORE_GAMECONTEXT_H

#include <cstdint>
#include <vector>

#include "core/MayaCamera.h"
#include "domain/game/Game.h"
#include "render/Camera3D.h"

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
struct ScriptThread;
class ScriptVM;
class Tables;
class World3D;

// Legacy Canvas state ids (src/Canvas.h:68-93 numbering kept). Menu/Intro/
// Combat/Automap/TravelMap/... are documented out-of-scope stubs (spec
// 2026-08-23-phase5-skeleton §3): not enumerated, unreachable; script
// requests for them are logged by the VM.
enum class StateId : int {
	Playing     = 3,  // legacy ST_PLAYING
	InterCamera = 4,  // legacy ST_INTER_CAMERA (script lerp show; world renders from player view)
	Dialog      = 8,  // legacy ST_DIALOG (real dialogs, spec GROUP 1)
	Loading     = 7,  // legacy ST_LOADING
	Dying       = 13, // legacy ST_DYING (stub)
	Camera      = 18, // legacy ST_CAMERA (scripted cinematic, cutscenes-camera.md §1)
	Looting     = 23, // legacy ST_LOOTING (src/Canvas.h:90; loot-crouch camera,
	                  // docs/research/2026-08-25-camera-pitch-loot.md)
};

// Discrete input actions (legacy getKeyAction subset,
// src/InputEventController.cpp:24-141). The first six are the playing set;
// during ST_DIALOG every queued action routes to the dialog handler, which
// reads Forward/Back/TurnLeft/TurnRight as ACTION_UP/DOWN/LEFT/RIGHT.
enum class Action : int {
	None, Forward, Back, TurnLeft, TurnRight, Use,
	Passturn, Automap, Menu, BackKey,
};

// The rewrite's Canvas analog: owns the game-state machine, clocks, pending
// input actions, per-state ticks and render orchestration. Non-owning
// pointers to all subsystems; constructed after them in main() so it dies
// first (spec §2).
class GameContext {
public:
	static constexpr int kTickMs = 15;   // one fixed-step quantum
	static constexpr int kNumStateVars = 9;
	static constexpr int kLootPhaseMs = 500;  // LOOTING_CROUCH_TIME (src/Canvas.h:46);
	                                          // promoted from tickLooting's local constant

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

	// ---- scripted cinematics (cutscenes-camera.md §1-§3) ----
	// EV_STARTCINEMATIC target: bind map camera camIdx, capture the player
	// pose for -2 sentinel channels, enter ST_CAMERA with the 1 s skip
	// lockout (src/ScriptThread.cpp:183-228, :400-411).
	void startCinematic(int camIdx);

	// EV_ADV_CAMERAKEY target: park the caller via the -1 protocol and queue
	// it on cameraResumeList_; Snap resumes it after `resumeCount` more key
	// completions (src/ScriptThread.cpp:690-702, src/MayaCamera.cpp:359-370).
	void advanceCameraKey(ScriptThread* t, int resumeCount);

	// Camera activity probe ("cinematic bound"): true whenever the maya view
	// is armed or playing, regardless of key index — the isCameraActive
	// analog (src/Game.cpp:3297-3299). "Started" = activeCameraKey_ >= 0.
	bool cameraActive() const { return cameraView_ && cameraCamIdx_ >= 0; }

	// Script-driven movement handshake storage (cutscenes-camera.md §4):
	// animated GOTO/TURN_PLAYER parks its thread in gotoThread_ and
	// tickPlaying's arrival handling resumes it once view==dest and the angle
	// settled (src/MovementController.cpp:164-167, :298-302). gotoTriggered_
	// defers an instant GOTO's destination events to the next playing tick
	// (src/MovementController.cpp:503-509).
	ScriptThread* gotoThread_ = nullptr;
	bool gotoTriggered_ = false;

	// finishRotation(true) analog, also invoked by the VM's instant-GOTO path
	// (src/ScriptThread.cpp:641).
	void finishRotationFired();
	// Terrain height in canvas coords (src/MovementController.cpp getHeight
	// analog); used by spawnPlayer and the VM's GOTO destZ.
	int getHeight(int x, int y) const;

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
	void handlePlayingAction(Action a);
	void updateFacingProbe();            // health-bar feed probe (src/MovementController.cpp:28-93 subset)
	void handleLootingAction(Action a);  // src/LoothingSystem.cpp:85-117
	void closeLootSession();             // grant + stand-up restart (:89-103)
	void drawLootingMenu(Graphics2D& g); // src/LoothingSystem.cpp:121-150

	void tickLoading();      // two-phase ordered tail (spec §5)
	void tickPlaying();      // legacy playing tick order (spec §6)
	void tickCamera();       // ST_CAMERA per-frame order (src/Canvas.cpp:949-958)
	void tickLooting();      // ST_LOOTING pose driver (src/LootingSystem.cpp:35-83)
	void tickDying();
	void tickCinematicClock(); // key-boundary engine shared by Camera/Playing
	                           // (src/MayaCamera.cpp:46-148, :335-374)

	void finishCinematic();            // end-of-keys Snap (src/MayaCamera.cpp:376-397)
	void skipCinematicNow();           // Game::skipCinematic (src/Game.cpp:2507-2544)
	void nextKey();                    // MayaCamera::NextKey (src/MayaCamera.cpp:36-44)
	bool resumeKeyWaits();             // ADV_CAMERAKEY countdown step (src/MayaCamera.cpp:359-370);
	                                   // true = a thread resumed (no auto-advance then)
	void flushParkedThreads(bool force); // cameraResumeList_ drain, pool-index order
	int cameraKeyDuration(int key) const; // MS channel of the active camera's key

	void spawnPlayer();      // legacy Game::spawnPlayer (src/Game.cpp:941-972)
	void finishMovement();   // legacy MovementController::finishMovement (:160-196)
	int flagForFacingDir(int i) const; // src/MovementController.cpp:214-224

	Init sys_;
	Camera3D camera_;
	int loadingPhase_ = 0;
	int64_t lastTurnTime_ = 0;
	int64_t deathTimeMs_ = 0;
	std::vector<Action> pendingActions_;

	// Cinematic camera runtime (cutscenes-camera.md §1-§3). Two-field model
	// restored from legacy (spec 2026-08-26-camera-key0 §1): cameraView_
	// binds the maya view (legacy activeCameraView, src/ScriptThread.cpp:186;
	// cleared by the Snap tail, src/MayaCamera.cpp:379), while
	// activeCameraKey_ keeps its pure legacy meaning. Phases: idle
	// (false/-1) -> armed post-STARTCINEMATIC (true/-1; static key-0 pose
	// renders, boundary engine off) -> playing (true/>=0) -> finished or
	// skipped (false/-1).
	MayaCamera maya_;
	int cameraCamIdx_ = -1;
	bool cameraView_ = false;           // legacy activeCameraView (src/ScriptThread.cpp:186)
	int activeCameraKey_ = -1;          // -1 = bound-not-started (src/ScriptThread.cpp:188)
	int64_t cameraStartTime_ = 0;      // legacy activeCameraTime (src/Canvas.cpp:1092)
	int64_t cinUnpauseTime_ = 0;       // skip soft-key gate (src/ScriptThread.cpp:227-228)
	bool skipCinematic_ = false;
	StateId preCameraState_ = StateId::Playing;
	StateId dialogPrevState_ = StateId::Playing; // dialog-close restore source (:541-554)
	std::vector<ScriptThread*> cameraResumeList_; // ADV_CAMERAKEY park list (§2)
	std::vector<int> cameraResumeCounts_;         // remaining key completions per entry

	// Loot-crouch camera runtime (docs/research/2026-08-25-camera-pitch-loot.md;
	// LootingSystem field analogs). lootDest* anchors the cached player pose
	// the two 500 ms phases lerp away from and back to.
	// Loot dwell session (legacy LootingSystem field analogs).
	bool lootSettleSfx_ = false;              // field_0xac5_: sound 1055 once per session
	Game::LootPool lootPool_;                 // pooled entries + lootText + lineIndex + lootLineNum
	bool lootCrouch_ = false;             // crouchingForLoot (phase selector)
	int64_t lootTime_ = 0;                // lootingTime (app->time latch)
	int lootDestX_ = 0, lootDestY_ = 0, lootDestZ_ = 0, lootDestPitch_ = 0;
	int lootStepX_ = 0, lootStepY_ = 0;   // facing unit steps (viewStep>>6, ±1/0)
};

} // namespace newcore

#endif // NEW_CORE_GAMECONTEXT_H
