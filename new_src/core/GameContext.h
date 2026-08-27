#ifndef NEW_CORE_GAMECONTEXT_H
#define NEW_CORE_GAMECONTEXT_H

#include <cstdint>
#include <vector>

#include "core/CinematicCamera.h"
#include "core/GameStates.h"
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

// The rewrite's Canvas analog: owns the game-state machine, clocks, pending
// input actions, per-state ticks and render orchestration. Non-owning
// pointers to all subsystems; constructed after them in main() so it dies
// first (spec §2).
class GameContext : public StateHost {
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
	// The cutscene clock lives in its own module (spec
	// 2026-08-26-decomposition §P1-G2); the VM reaches it through here.
	CinematicCamera& cinematic() { return cinematic_; }

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

	bool inputBlocked() const;
	void handlePlayingAction(Action a);
	// Player view forward vector in 16.16 (legacy -view[2]/-view[6],
	// src/MovementController.cpp:38).
	void viewForward(int& fwdX, int& fwdY) const;
	void updateFacingProbe();            // health-bar feed probe (src/MovementController.cpp:28-93)
	// Ordered target election over the sorted fire-trace hit list — port of
	// src/PlayingInputHandler.cpp:218-385 (docs/original-code/combat.md §8).
	// Returns the elected entity (nullptr = air shot) and writes its hit
	// fraction to outFrac.
	Entity* electFireTarget(int weapon, int* outFrac);
	void handleLootingAction(Action a);  // src/LoothingSystem.cpp:85-117
	void closeLootSession();             // grant + stand-up restart (:89-103)
	void drawLootingMenu(Graphics2D& g); // src/LoothingSystem.cpp:121-150
	// First-person weapon quad, drawn after drawBSP (spec combat-stage1 §6.2;
	// legacy Combat::drawWeapon GL-path anchors, docs/research/
	// 2026-08-26-hero-choice-and-weapon.md Part B).
	void drawViewWeapon(Graphics2D& g);

	void tickLoading();      // two-phase ordered tail (spec §5)
	void tickPlaying();      // legacy playing tick order (spec §6)
	void tickCamera();       // ST_CAMERA per-frame order (src/Canvas.cpp:949-958)
	void tickLooting();      // ST_LOOTING pose driver (src/LootingSystem.cpp:35-83)
	void tickDying();

	void spawnPlayer();      // legacy Game::spawnPlayer (src/Game.cpp:941-972)
	void finishMovement();   // legacy MovementController::finishMovement (:160-196)
	int flagForFacingDir(int i) const; // src/MovementController.cpp:214-224

	Init sys_;
	StateId state_ = StateId::Loading;
	Camera3D camera_;
	int loadingPhase_ = 0;
	int64_t lastTurnTime_ = 0;
	int64_t deathTimeMs_ = 0;
	std::vector<Action> pendingActions_;

	// Cinematic camera runtime (cutscenes-camera.md §1-§3).
	CinematicCamera cinematic_;
	StateId dialogPrevState_ = StateId::Playing; // dialog-close restore source (:541-554)

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
