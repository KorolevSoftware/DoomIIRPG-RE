# Decomposition: GameContext + Game, typed trace result, named legacy encodings

_Spec date: 2026-08-26. Author: architect. Status: approved by the user, ready to
implement group by group._

Related: ADR [0010](../adr/0010-module-decomposition-and-injection.md) (module
boundaries + injection pattern), ADR
[0011](../adr/0011-typed-trace-result-and-named-encodings.md) (TraceHit + named
encodings).

## 0. Scope, ground rules, and what this is NOT

Measured problem (see the task brief and `docs/journal.md` 2026-08-26):
`new_src/core/GameContext.cpp` is 1613 lines (legacy `src/Canvas.cpp` = 1625) and
`new_src/domain/game/Game.cpp` is 1565. Four of six implementation groups in the
last cycle had to edit `GameContext.cpp`, forcing serialization, and both
blocking review items were "three notions of the same predicate drifted apart
inside one file".

**This is a pure refactor. Zero behaviour change.** Every group below is a
*verbatim move* of existing code plus mechanical renames of the access path
(`sys_.game->` → `env_.game->`, `state` → `host_->state()`, …). Rules for
every coder on every group:

1. Move code by cut/paste. Do not re-indent logic, do not reorder statements, do
   not "clean up" a comment, do not fix a bug you notice. Report it instead —
   it becomes its own ticket.
2. Keep every legacy citation comment attached to the code it documents. The
   citations are the ground truth for the reviewer.
3. Keep the *order of operations* in `tick()` and `render()` byte-identical.
   Ordering is behaviour here (state hooks run scripts synchronously).
4. Any numeric literal you move stays numerically identical. Where a group
   introduces a name for it (Phase 3), the name is proven with a
   `static_assert` on the legacy value.
5. `new_src` only. New files ⇒ `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`
   (GLOB) then `cmake --build build_new -j 8`.

**Explicitly out of scope** (cannot be done without touching behaviour; listed
here so nobody smuggles them in):

- Implementing real scissor for `Graphics2D::setClip` (`new_src/render/Graphics2D.cpp:20-24`).
  It is a no-op for the sprite batch; `ViewWeapon` clips its quad and source
  sub-rect by hand instead. Group P1-G5 only *documents* this on the function.
  Making `setClip` real would change what every other 2D caller draws.
- Porting `cullBoundingBox`, the cinematic view weapon (`cinematicWeapon`),
  `Hud::draw` widgets, and the remaining TIER-B opcodes. Unrelated debts.
- Renaming entity `info` bits whose legacy *meaning* is not yet established
  (`0x200000` at `new_src/domain/game/Combat.cpp:118`, `0x20000000` at
  `new_src/domain/game/ScriptVM.cpp:1005-1006`, `0x2000000`/`0x20000000` in
  `World3D.cpp:781,808`). Naming these needs a researcher fact first; see §5.4.

Debts that DO get folded in, because they sit inside a group we already open:

| Debt | Folded into |
|---|---|
| TEMP `[cam] nextKey` stderr print (`GameContext.cpp:797-800`), stale comment placement in `MayaCamera.cpp` | P1-G2 |
| TEMP `[fire] elected …` print (`GameContext.cpp:1128-1133`) | P1-G4 |
| `setClip`-is-a-no-op warning comment | P1-G5 |
| Hard-coded weapon magnification (already fixed to read the live projection) — keep reading `Camera3D::projectionInt()`, now passed as a parameter | P1-G5 |

## 1. Target shape

```
core/
  GameStates.h        NEW  StateId, Action, StateHost (interface)
  GameContext.{h,cpp}      state machine + clocks + tick/render coordination ONLY (~330 lines)
  CinematicCamera.{h,cpp}  NEW  maya clock, key boundaries, ADV_CAMERAKEY parking
  LootSession.{h,cpp}      NEW  ST_LOOTING pose/dwell clock + loot menu overlay
  PlayerActions.{h,cpp}    NEW  playing input, movement commit, use chain, fire commit, arrival hooks
domain/game/
  Targeting.{h,cpp}        NEW  view forward, facing probe, fire-target election, health-bar feed
  TraceSystem.{h,cpp}      NEW  capsule traces + TraceHit (Phase 2)
  DoorSystem.{h,cpp}       NEW  (Phase 2)
  MonsterSystem.{h,cpp}    NEW  (Phase 2)
  SpriteLerps.{h,cpp}      NEW  (Phase 2)
  CorpseLoot.{h,cpp}       NEW  (Phase 2)
  EntityDb.{h,cpp}         NEW  (Phase 2)
  Game.{h,cpp}             container of peer subsystems + cross-cutting turn logic (~250 lines)
  WeaponTable.h            NEW  typed views over tables->weaponData / weaponInfo (Phase 3)
render/
  SceneRenderer.{h,cpp}    NEW  world pass: viewport, camera, sky, BSP, sprite classification
ui/
  ViewWeapon.{h,cpp}       NEW  first-person weapon quad + muzzle flash
domain/world/
  MapData.h                + heightAt() (Phase 1 G1)
```

### 1.1 How a module reaches the systems it needs (no singleton, no back-chain)

Every new module follows the pattern already established by `Combat` (ADR 0008),
`ScriptVM` and `DialogSystem`: a plain class with

- a nested `struct Env` of **non-owning raw pointers to exactly the subsystems it
  uses** (never a `GameContext*`, never a `Game*` for Phase-2 domain modules —
  they take `EntityDb*`/`MapData*`),
- a single `void init(const Env&)` wiring call performed once by the owner,
- clocks injected as `const int64_t*` (the `Combat::Env::gameTime` precedent,
  `new_src/domain/game/Combat.h:57`), so `GameContext` stays the sole owner of
  `upTimeMs`/`gameTime`.

The one thing modules may not get from a data pointer is *the current game
state and the right to change it synchronously*. Deferring a state change would
alter ordering (`finishCinematic` changes state **before** resuming parked
threads, `GameContext.cpp:930-936`), so a mailbox is not allowed. Instead
`core/GameStates.h` declares the narrow interface

```cpp
// The only thing a module may know about the state machine.
class StateHost {
public:
	virtual ~StateHost() = default;
	virtual StateId state() const = 0;
	virtual void requestState(StateId s) = 0;   // == GameContext::setState
};
```

`GameContext` is the only implementation. A module holds `StateHost*` — two
methods, no reach-through to `Systems`, `Game` or `Player`. This is the explicit
antithesis of `CAppContainer::getInstance()->app->…`: the arrow points from the
module to a two-method abstraction, not from the module back into the world.

## 2. Phase 1 — split `GameContext` (7 groups, strictly sequential)

Every Phase-1 group edits `core/GameContext.{h,cpp}`, so they **cannot run in
parallel with each other**. They are deliberately small (one module each) so a
flaky window fits one. Phase 2 groups touch `domain/game/Game.{h,cpp}` and new
files only (thanks to the temporary forwarders in §3.1) and therefore **can run
in parallel with any Phase-1 group**.

A researcher-driven cinematic-letterbox fix is landing in `GameContext`/`Hud`
before this work starts. **Move whatever is in the file at the time of your
group, not the snippets quoted in this spec.** Line numbers here are anchors as
of 2026-08-26, pre-fix.

### P1-G1 — prep: state enums, `StateHost`, `MapData::heightAt`

Files: NEW `new_src/core/GameStates.h`; edit `core/GameContext.{h,cpp}`,
`domain/world/MapData.h`, `domain/game/DialogSystem.cpp`,
`domain/game/ScriptVM.cpp`.

1. Move `enum class StateId` (`GameContext.h:27-40`) and `enum class Action`
   (`:47-50`) **verbatim, comments included** into the new
   `core/GameStates.h` (namespace `newcore`, guard `NEW_CORE_GAMESTATES_H`).
   Add `class StateHost` exactly as in §1.1. `GameContext.h` includes
   `core/GameStates.h`. No other file changes: both enums keep their names in
   the same namespace.
2. `MapData` gains the terrain accessor (pure data, removes the reason for
   modules to call back into `GameContext`):
   ```cpp
   // Terrain height in canvas units (legacy MovementController::getHeight;
   // port of new_src/core/GameContext.cpp:299-304).
   int heightAt(int x, int y) const {
   	const int hx = x & 0x7FF, hy = y & 0x7FF;
   	return heightMap[(hy >> 6) * 32 + (hx >> 6)] << 3;
   }
   ```
   `GameContext::getHeight` becomes `return sys_.map->heightAt(x, y);` (keep the
   method: `ScriptVM` calls it 5×; it is deleted in P1-G7).
3. `GameContext` declares `class GameContext : public StateHost`; the public
   field `StateId state` becomes private `StateId state_` with
   `StateId state() const override { return state_; }` and
   `void requestState(StateId s) override { setState(s); }`. `setState` stays
   public and keeps its exact body. Update the internal uses plus the 5 external
   ones: `DialogSystem.cpp:236,242,244,354` and `ScriptVM.cpp:1165`
   (`env_.ctx->state` → `env_.ctx->state()`).
   `oldState`, `stateChanged`, `stateVars` stay public fields (no external users).

Verify: build; run; boot to gameplay; walk, open a door, talk through the intro
dialog, ride the lift cutscene. Everything must look exactly as before.

### P1-G2 — `core/CinematicCamera.{h,cpp}`

Moves out: `MayaCamera maya_` plus every camera-clock member and method.

Owns (moved from `GameContext`): `maya_`, `cameraCamIdx_`, `cameraView_`,
`activeCameraKey_`, `cameraStartTime_`, `cinUnpauseTime_`, `skipCinematic_`,
`cameraResumeList_`, `cameraResumeCounts_`.
Borrows via `Env`: `MapData* map`, `Player* player`, `ScriptVM* vm`,
`StateHost* host`, `const int64_t* gameTime`.
Deleted from `GameContext`: `preCameraState_` (already write-only dead state —
verify with grep before deleting; if it has a reader, move it here instead).

```cpp
class CinematicCamera {
public:
	struct Env { MapData* map; Player* player; ScriptVM* vm;
	             StateHost* host; const int64_t* gameTime; };
	void init(const Env&);

	void startCinematic(int camIdx);                       // GameContext.cpp:761-784
	void advanceCameraKey(ScriptThread* t, int resumeCount);// :802-813

	// "cinematic bound" probe (legacy isCameraActive, src/Game.cpp:3297-3299).
	bool active() const;                                   // was cameraActive()
	bool started() const { return activeCameraKey_ >= 0; }

	void tickClock();                                      // :844-886 tickCinematicClock
	void tickCameraState();                                // :822-843 minus hud->update
	void requestSkip();                                    // sets skipCinematic_
	void clearSkipRequest();                               // exitState_ hook (:56-61)
	bool skipGateOpen() const;                             // gameTime >= cinUnpauseTime_

	// Display-rate resample + pose, nullptr when no cinematic owns the view.
	// Exactly the guarded block of GameContext.cpp:1452-1470.
	const MayaPose* renderPose();
	const MayaPose& pose() const;                          // tick-time feed (:196-198)

private:
	void nextKey(); bool resumeKeyWaits(); void finishCinematic();
	void skipCinematicNow(); void flushParkedThreads(bool force);
	int  keyDuration(int key) const;
	…moved members…
};
```

`GameContext` keeps a `CinematicCamera cinematic_;` member and:
- `tick()`: `cameraActive()` → `cinematic_.active()` (3 sites: gameTime gate at
  `:127-129`, lerp view-angle feed `:196-198`, camera-skip input branch
  `:161-165` → `if (cinematic_.skipGateOpen()) cinematic_.requestSkip();`).
- `tickCamera()` shrinks to `cinematic_.tickCameraState(); sys_.hud->update(kTickMs);`
  (keep this exact order, `:840-842`).
- `tickPlaying()` `:366` → `if (cinematic_.active()) cinematic_.tickClock();`.
- `exitState_()` → `cinematic_.clearSkipRequest()`.
- Public pass-throughs for the VM (the VM already holds `ctx`; a two-line
  accessor `CinematicCamera& cinematic()` is preferred): update
  `ScriptVM.cpp:791,801,803` to `env_.ctx->cinematic().startCinematic(cam)` /
  `.active()` / `.advanceCameraKey(t, count)`.
- `render()`: replace the whole `if (cine && cameraCamIdx_ >= 0 …)` head with
  `const MayaPose* cinePose = cinematic_.renderPose();` and use
  `cinePose != nullptr` where `cine` was used. **This is the structural fix for
  the drift bug**: fov, cockpit overlay and view-weapon suppression now read one
  value produced by one owner.

Fold in: delete the TEMP `[cam] nextKey` print (`:797-800`); fix the misplaced
comment in `new_src/core/MayaCamera.cpp` (journal 2026-08-26) and throttle the
tween out-of-range diagnostic if it is still unthrottled.

Verify: lift-ride cutscene at map00 plays at the same speed with the same
framing, cockpit letterbox present, key-0 pose correct, ESC/any key past 1 s
skips it, and gameplay resumes with the player facing the same way.

### P1-G3 — `core/LootSession.{h,cpp}`

Moves out: the whole ST_LOOTING vertical slice.

Owns: `lootPool_` (`Game::LootPool`), `lootCrouch_`, `lootTime_`,
`lootSettleSfx_`, `lootDestX_/Y_/Z_/Pitch_`, `lootStepX_/Y_`,
`kLootPhaseMs`.
Borrows: `MapData* map`, `Game* game`, `Player* player`,
`const Localization* loc`, `const Font* font`, `DialogSystem* dialogs`
(shared `drawScrollBar`), `const Tables* tables`, `StateHost* host`,
`const int64_t* upTimeMs`.

```cpp
class LootSession {
public:
	static constexpr int kLootPhaseMs = 500;   // LOOTING_CROUCH_TIME (src/Canvas.h:46)
	struct Env { … };
	void init(const Env&);
	void begin();                       // enterState_(Looting) body, GameContext.cpp:91-116
	void tick();                        // tickLooting, :480-537
	void handleAction(Action a);        // :538-555
	void draw(Graphics2D& g);          // drawLootingMenu, :574-621
private:
	void close();                       // closeLootSession, :556-561
	…moved members…
};
```

`getHeight` calls inside the moved code become `env_.map->heightAt(...)`. The
file-local `fillArgb`/`rectArgb` helpers (`:562-572`) move with the drawing code.
`GameContext`: `enterState_` case `Looting` → `pendingActions_.clear(); loot_.begin();`
(keep the clear in `GameContext` — it is state-machine business);
`tickLooting()` disappears (switch calls `loot_.tick()`); the input branch calls
`loot_.handleAction(a)`; `render()` calls `loot_.draw(g)` under the same
`state() == StateId::Looting` guard.

Verify: E on the imp corpse → 500 ms crouch → red "Looted Items:" panel, E
pages/closes, TAB/BACK closes, items granted, stand-up, turn consumed.

### P1-G4 — `domain/game/Targeting.{h,cpp}`

Moves out: `viewForward` (`:1176-1189`), `electFireTarget` (`:1190-1305`),
`updateFacingProbe` (`:1306-1376`) and the monster-health-bar feed block of
`render()` (`:1554-1580`) — the probe and its only readout belong together.

Owns: nothing persistent (the probe result keeps living on
`Player::facingEntity`, unchanged — the HUD reads it).
Borrows: `Game* game`, `Player* player`, `MapData* map`, `const Tables* tables`
(sin table), `Hud* hud`.

```cpp
class Targeting {
public:
	struct Env { Game* game; Player* player; MapData* map;
	             const Tables* tables; Hud* hud; };
	void init(const Env&);
	void viewForward(int& fwdX, int& fwdY) const;
	Entity* electFireTarget(int weapon, int* outFrac);   // signature unchanged in G4
	void updateFacingProbe();                            // writes player->facingEntity
	void feedHealthBar() const;                          // render() block :1554-1580
};
```
`GameContext::render()` keeps only
`if (state() == StateId::Playing) { targeting_.updateFacingProbe(); sys_.game->facingDirty = false; }`
followed by `targeting_.feedHealthBar();` — identical order to today.
`electFireTarget` stays called from `handlePlayingAction` (still in
`GameContext` until P1-G7): `targeting_.electFireTarget(...)`.

Fold in: delete the TEMP `[fire] elected …` print (`:1128-1133`).

Verify: health bar appears for a monster up to 6 tiles ahead and hides for
non-monsters past 3 tiles; shooting still elects the same targets (fire at a
monster behind a corpse from the corpse tile; fire at a wall point-blank).

### P1-G5 — `ui/ViewWeapon.{h,cpp}`

Moves out: `kWeaponVpCx/Cy`, `kWeaponCanvasCx/Cy`, `drawWeaponQuad`,
`drawViewWeapon` (`GameContext.cpp:623-758`) with all comments.

Owns: nothing. Borrows: `Player* player`, `Game* game` (→ `combat`),
`const Tables* tables`, `World3D* world`, `MediaLoader* media`, `Hud* hud`
(shake), `const int64_t* gameTime`.

```cpp
class ViewWeapon {
public:
	struct Env { … };
	void init(const Env&);
	// Draws the weapon + muzzle flash. Magnification is derived from the
	// projection actually in use this frame (cam.projectionInt()), so a
	// cinematic fov cannot desync it. NO state gate inside: the caller owns
	// the "may the weapon draw" decision (see §4).
	void draw(Graphics2D& g, const Camera3D& cam);
};
```
The two gates currently *inside* `drawViewWeapon` (`:661-673`: gameplay-state
list, and `if (cameraActive()) return;`) move **up to the single call site** in
`GameContext::render()`:
```cpp
const bool gameplayView = state() == StateId::Playing || state() == StateId::Looting ||
                          state() == StateId::Dialog;
if (cinePose == nullptr && gameplayView) viewWeapon_.draw(g, sceneRenderer_.camera());
```
(pre-G6 the camera is still `camera_`). The weapon-select / `weapons == 0` /
`w < 0` early-outs stay inside `draw` — they are weapon data, not view state.

Fold in: on `drawWeaponQuad`, keep and sharpen the existing note that
`Graphics2D::setClip` does not scissor the sprite batch, and add the same warning
as a header comment on `Graphics2D::setClip` itself (comment only — see §0).

Verify: rifle geometry and position identical (235×236 quad, art touching the
bottom panel), muzzle flash on fire, weapon hidden during cutscenes and during
loot dwell it is visible exactly as before.

### P1-G6 — `render/SceneRenderer.{h,cpp}`

Moves out: `isFloaterTile`/`isSpecialBossTile` (`:1393-1405`), `kWorldRect`, the
shake math, both camera-setup branches, the sprite-bias/char-class
classification loop, `drawSky`/`drawBSP`, the viewport push/pop and the
"world not initialized" fallback fill — i.e. `render()` lines `:1414-1550`.

Owns: `Camera3D camera_` (moved off `GameContext`), the two classification
buffers as members refilled per frame with `assign(numSprites, 0)` (identical
values; avoids a per-frame allocation).
Borrows: `MapData* map`, `MediaLoader* media`, `World3D* world`, `Game* game`,
`Player* player`, `Hud* hud`, `const Tables* tables`, `const int64_t* upTimeMs`.

```cpp
class SceneRenderer {
public:
	struct Env { … };
	void init(const Env&);
	// One world pass. cinePose != nullptr => cinematic takeover (fov 315, or
	// 290 when a dialog runs inside it: underDialog). Falls back to the flat
	// fill when the world is not initialized.
	void drawWorld(RenderBackend& r, Window& w, const MayaPose* cinePose, bool underDialog);
	const Camera3D& camera() const { return camera_; }
};
```
`camera_.setSinTable(...)` moves from `GameContext::init` into
`SceneRenderer::init`. Layering note: `render/SceneRenderer.h` includes
`core/MayaCamera.h` for the plain-value `MayaPose` (that header only pulls
`domain/world/MapData.h`, so no cycle with `core/` — checked
`new_src/core/MayaCamera.h:1-13`). If a cycle ever appears, move `MayaPose`
into `domain/world/`, not the other way round. `GameContext::render()` becomes the frame composer
described in §4.

Verify: gameplay framing unchanged (world band 1,7,478,248; horizon at y=131),
sky/fog identical, monsters/NPC stacks and corpses render as before, cutscene
framing unchanged, cockpit overlay in the same place.

### P1-G7 — `core/PlayerActions.{h,cpp}`

Moves out: `handlePlayingAction` (`:980-1175`), `finishMovement` (`:418-446`),
`finishRotationFired` (`:447-463`), `flagForFacingDir` (`:464-472`),
`spawnPlayer` (`:277-298`), and the public handshake fields `gotoThread_`,
`gotoTriggered_`.

Owns: `gotoThread`, `gotoTriggered` (now public members of `PlayerActions`).
Borrows: `Game* game`, `Player* player`, `ScriptVM* vm`, `MapData* map`,
`Hud* hud`, `const Localization* loc`, `Targeting* targeting`,
`StateHost* host`, `const int64_t* upTimeMs`.

```cpp
class PlayerActions {
public:
	struct Env { … };
	void init(const Env&);
	void spawnPlayer();                 // needs the turn-clock stamp: see below
	void handleAction(Action a);
	void finishMovement();
	void finishRotationFired();
	int  flagForFacingDir(int i) const;
	ScriptThread* gotoThread = nullptr; // script GOTO/TURN_PLAYER handshake
	bool gotoTriggered = false;
};
```
`spawnPlayer` ends with `lastTurnTime_ = upTimeMs` — `lastTurnTime_` stays on
`GameContext` (state-machine clock). Give `PlayerActions::spawnPlayer` an
`int64_t*` in `Env` pointing at `GameContext::lastTurnTime_`, or keep the stamp
at the `GameContext` call site (`tickLoading` `:264`); pick the call-site stamp —
fewer pointers. Same for `enterState_(Playing)`.

`GameContext` keeps `tickPlaying()`, which now reads
`if (actions_.gotoTriggered) { … }` / `actions_.finishMovement()` /
`actions_.finishRotationFired()` in exactly the current order, and the input loop
calls `actions_.handleAction(a)`.

ScriptVM call sites: `:664` and `:712,754,1132,1277-1278` `getHeight` →
`env_.map->heightAt(...)`; `:666,1162` `finishRotationFired()` →
`env_.ctx->actions().finishRotationFired()`; `:1113,1152` `gotoThread_` and
`:1166` `gotoTriggered_` → `env_.ctx->actions().gotoThread` / `.gotoTriggered`.
Then delete `GameContext::getHeight`.

Verify: 90° turns, 1-tile steps, blocked moves, E on a door (open/locked), E on a
script tile, TAB "Turn Passed", firing (elect/air/wall-push), scripted GOTO/lift
sequences, spawn position/facing after boot.

### Phase-1 exit state

`GameContext.{h,cpp}` then contains: `Init`/`sys_`, the clocks, `setState` +
`enterState_`/`exitState_`, `inputBlocked`, the input dispatch loop, the globals
block, the per-state tick switch (each case one to three lines), `tickLoading`,
`tickDying`, `debugGiveKeycards`, `render()` as a composer, and the module
members. Target ≤ 400 lines. If a coder ends a group above that, say so in the
report — it means a seam was missed, not that the target is wrong.

## 3. Phase 2 — split `Game` (6 groups, parallel with Phase 1)

`Game` becomes what ADR 0008 already started with `Combat combat;`: a container
of peer subsystems plus the cross-cutting turn logic. Public members
(`trace`, `doors`, `monsters`, `lerps`, `loot`, `db`) are addressed directly by
callers — `game->doors.performDoorEvent(...)` — so `Game` never grows back into a
facade.

Domain subsystems must NOT hold a `Game*`. They take `EntityDb*` (§P2-GF) and
`MapData*`. Until `EntityDb` exists (it lands last), each extracted subsystem
takes the two pieces it actually needs — `std::vector<Entity>* entities` and
`Entity** entityDb` (the 1024 tile heads) — passed in its `Env` by `Game`. P2-GF
replaces those two pointers with one `EntityDb*`.

### 3.1 Temporary forwarders (this is what buys the parallelism)

Each Phase-2 group leaves the old `Game` method as a one-line inline forwarder
in `Game.h`, tagged `// FORWARDER (spec 2026-08-26-decomposition §3.1) — delete in P3-F<n>`.
So Phase 2 touches `domain/game/Game.{h,cpp}` + its new files only, never
`GameContext.cpp` or the Phase-1 modules. The forwarders are swept in Phase 3.

### P2-GA — `domain/game/TraceSystem.{h,cpp}` (+ `TraceHit`) — do this first

Moves out of `Game.cpp:446-643` and `:790-806`: `capsuleToLineTrace`,
`capsuleToCircleTrace`, `traceEntityHits`, `traceWorldFrac`, `traceMove`, the
scratch state (`tracePoints_`, `traceBBox_`, `traceHits_`, `traceCollisionX_/Y_`),
`entityDistFrom`, and `setPlayerPos`/`playerX_/playerY_` (only the trace's
ET_PLAYER branch and the door auto-close read them; the door system gets its own
copy in P2-GB — keep ONE owner: `TraceSystem` owns it, `DoorSystem::Env` gets
`const TraceSystem*`).

Public API — this is where part B of the task lands, see §5.1:

```cpp
TraceHit trace(int x0,int y0,int x1,int y1, Entity* skipEnt, int mask, int radius);
const std::vector<TraceHit>& hits() const;    // sorted by frac asc
int collisionX() const; int collisionY() const;
int distFrom(const TraceHit& h, int x, int y) const;   // kind-switched
int distFrom(const Entity* e, int x, int y) const;     // entity form
void setPlayerPos(int x, int y);
```
`Game` keeps forwarders `traceMove(...)` (bool + out-params, implemented on top
of `trace()`), `lastTraceHits()` (adapter is NOT possible — see §5.2 step 2:
`lastTraceHits()` is deleted in the same group and its two consumers are
migrated in P3-B1/P3-B2, so keep the old pair vector alive behind
`hits()` only if a coder needs an intermediate build; prefer migrating).

### P2-GB — `domain/game/DoorSystem.{h,cpp}`

Moves: `useDoorFacing`, `doorFamilyTile`, `performDoorEvent`, `doorRegistered`,
`registerOpenDoor`, `unregisterOpenDoor`, `unlinkDoor`, `canCloseDoor`,
`advanceTurnDoors`, `updateDoors`, `DoorAnim`, `doorAnims_`, `openDoors_`,
`kOpenDoors` (`Game.cpp:241-445`, `:1464-1518`). Borrows: entities/entityDb,
`MapData*`, `const EntityDefs*`, `ScriptVM*` (owner-thread resume),
`const TraceSystem*` (player pos).

### P2-GC — `domain/game/MonsterSystem.{h,cpp}`

Moves: `activate`, `deactivate`, `updateMonsters`, `endMonstersTurn`,
`snapMonsters`, `painMonster`, `diedMonster`, `awardKillXP`, `corpsifyMonster`,
`isBossDef`, the `entityMonsters_[80]` pool + `numMonsters_`, the ring heads
(`activeMonsters`, `inactiveMonsters`, `combatMonsters`,
`interpolatingMonsters`), and the XP wiring (`setXPSystems`, `xpPlayer_`,
`xpLoc_`, `xpHud_`) — `Game.cpp:774-1013` plus `:687-741`.

### P2-GD — `domain/game/SpriteLerps.{h,cpp}`

Moves: `isqrt64`, `SpriteLerp` (+ `calcDist`), `vecToDir`, `dbgLerpAnimAudit`,
`allocLerpSprite`, `spriteZBias`, `updateLerpSprite`, `freeLerpSprite`,
`spriteLerps_[16]`, `lerpClock_`, `lerpViewAngle_`, `sinTable_`,
`setSinTable`, `setLerpViewAngle`, `clockMs` (`Game.cpp:1208-1463`).
`Game::update(dtMs)` keeps calling `lerps.update(dtMs)` then `doors.update(...)`
in the current order (`Game.cpp:1519+`).

### P2-GE — `domain/game/CorpseLoot.{h,cpp}`

Moves: `LootPool`, `itemLongName`, `poolLootCorpse`, `giveLootPool`,
`findLootableCorpseFacing`, `populateDefaultLootSet` (`Game.cpp:32-52`,
`:756-773`, `:1014-1175`). `Game::LootPool` becomes `CorpseLoot::Pool`; keep
`using LootPool = CorpseLoot::Pool;` in `Game.h` as a forwarder so
`LootSession` compiles unchanged until P3-F5.

### P2-GF — `domain/game/EntityDb.{h,cpp}` + `Game` cleanup

Moves: `entities_`, `entityDb_[1024]`, `kEntities`, `linkEntity`,
`unlinkEntity`, `findMapEntity`, `entities()`, `playerEntity`, `worldEntity`,
`findEntityBySprite`, `removeEntity`. `Game` then holds `EntityDb db;` and each
subsystem's `Env` swaps its `(entities, entityDb)` pair for one `EntityDb*`.
`Game` retains: `loadEntities` (spawn orchestration), `advanceTurn`,
`update(dtMs)`, `touchTile`, `eventFlagsForMovement`, `eventFlagForDirection`,
`setLineLocked`, `difficulty`, `composeArgs`, the turn/script flags
(`monstersTurn`, `queueAdvanceTurn`, `skipAdvanceTurn`, `skipDialog`,
`abortMove`, `spawnParam`, `eventFlags_`, `facingDirty`), and the subsystem
members.

Verify (Phase 2, per group): doors open/close/auto-close and the blue-door card
path; lift rides; monster wake/pain/death/corpsify + corpse looting; script
lerps (the hangar lift, door slides); loot list contents and grants; boot with
no crash and identical entity counts in the `[load]` stderr lines.

## 4. `GameContext::render()` after Phase 1 (exact composition order)

```cpp
void GameContext::render(AppContext& app) {
	RenderBackend& renderer = app.renderer();
	renderer.beginFrame(app.window());
	Graphics2D& g = renderer.g2d();

	// ONE source of truth for "a cinematic owns the view this frame".
	const MayaPose* cinePose = cinematic_.renderPose();
	const bool underDialog   = state() == StateId::Dialog;

	scene_.drawWorld(renderer, app.window(), cinePose, underDialog);

	const bool gameplayView = state() == StateId::Playing ||
	                          state() == StateId::Looting || underDialog;
	if (cinePose == nullptr && gameplayView) viewWeapon_.draw(g, scene_.camera());
	if (cinePose != nullptr && sys_.hud->cockpitOverlay()) sys_.hud->drawOverlay(g, 0, 42, 480);

	if (state() == StateId::Playing) { targeting_.updateFacingProbe(); sys_.game->facingDirty = false; }
	targeting_.feedHealthBar();
	if (gameplayView) sys_.hud->drawTopBar(g, *sys_.font, 480);
	sys_.hud->drawMessages(g, *sys_.font);
	if (state() == StateId::Dialog)  sys_.dialogs->draw(g);
	if (state() == StateId::Looting) loot_.draw(g);

	renderer.endFrame(app.window());
}
```
Note the two derived predicates (`cinePose != nullptr`, `gameplayView`) are each
computed once. The class of bug that produced review blocker 2 — fov keyed off
`cameraActive()`, cockpit off `state == Camera`, weapon off a state list — is now
unrepresentable inside this function, and the three consumers cannot drift
because they read the same two locals. `gameplayView` must reproduce today's two
separate lists exactly: `drawViewWeapon`'s gate (`Playing|Looting|Dialog`,
`:661-663`) and `drawTopBar`'s gate (`Playing|Looting|Dialog`, `:1585-1588`) —
they are already identical; if the letterbox fix changed one of them, keep two
named locals instead of merging.

## 5. Part B and C — typed trace result and named encodings (Phase 3)

Phase 3 runs after Phase 1 and P2-GA. Its groups are small and, unlike Phase 1,
several of them touch disjoint files (see §6).

### 5.1 `TraceHit` (new file `domain/game/TraceHit.h`, used by `TraceSystem`)

Today two conventions are carried by naked integers and a null pointer:

- a world hit is `entities_[0]` with `def == nullptr`
  (`Game.cpp:611`, decoded ad hoc at `GameContext.cpp:1101-1102,1229-1230,1334-1335`
  and `Game.cpp:794-800`) — this convention being *undiscoverable* is why the
  wall-push branch was dead code (journal 2026-08-26, blocker 1);
- `frac == -1` means "the closest point on the sweep is the ray start", i.e. the
  entity overlaps the player's own position, and therefore sorts first
  (`Game.cpp:494,516` produce it; `Game.cpp:625-626` sorts on it) — the corpse
  defect.

```cpp
// docs/original-code/player-collision.md §2, src/Game.cpp:299-326,
// src/Render.cpp:1119-1125,1195-1209.
enum class TraceHitKind : uint8_t {
	None,   // nothing hit; entity == nullptr, frac == kFracMiss
	World,  // BSP line hit; entity == the world slot (def == nullptr by design)
	Ent,    // entityDb entity; entity->def != nullptr
};

struct TraceHit {
	static constexpr int kFracMiss    = 16384; // 1.0 in 14.14 = miss sentinel
	static constexpr int kFracAtStart = -1;    // closest point == ray start

	TraceHitKind kind = TraceHitKind::None;
	Entity* entity = nullptr;
	int frac  = kFracMiss;     // 14.14 along the sweep; kFracAtStart == start-inside
	int eType = -1;            // resolved ONCE by TraceSystem: ET_WORLD for World, -1 for None
	int eSubType = 0;          // 0 for World/None

	bool blocks() const      { return kind != TraceHitKind::None; }
	bool isWorld() const     { return kind == TraceHitKind::World; }
	bool isEntity() const    { return kind == TraceHitKind::Ent; }
	// The hit entity overlaps the sweep START (own tile / point-blank): the
	// legacy formula returns (0 >> 2) - 1, so it sorts before every real hit.
	bool startsInside() const { return frac == kFracAtStart; }
};
```
`eType` is stored, not computed, precisely so that `def == nullptr` is decoded in
exactly one place (`TraceSystem::pushHit`). **No consumer may test
`hit.entity->def == nullptr` again**; the reviewer greps for that.

`dist` is deliberately NOT a field: for a world hit the distance is measured
against `traceCollisionX/Y`, which only exists after the world pass, and for
entities it depends on the query point. It is a method on the owner instead:
`TraceSystem::distFrom(const TraceHit&, int x, int y)` — the port of
`Game::entityDistFrom` (`Game.cpp:790-806`) with `if (kind == World)` replacing
`if (def == nullptr || eType == ET_WORLD)`. Same arithmetic, same result.

### 5.2 Consumer migration (every current consumer, no behaviour change)

| Consumer | Today | After |
|---|---|---|
| `PlayerActions` move branch (`GameContext.cpp:1011-1029`) | `bool clear = traceMove(..., &hitEnt, &hitFrac)`; NPC step-past loop reads `hitFrac < 0` and `hitEnt->def->eType == ET_NPC` | `TraceHit h = trace(...)`; `if (!h.blocks())` commit; loop condition `h.startsInside() && h.isEntity() && h.eType == ET_NPC` |
| `PlayerActions` fire commit (`:1093-1174`) | `hitType = hit == nullptr ? -1 : (def ? eType : ET_WORLD)`; wall-push branch | `TraceHit elected`; `if (!elected.blocks())` → air shot; `elected.eType == …` for the attackable set; wall push `if (elected.isWorld() \|\| elected.eType == ET_SPRITEWALL) && dist <= combat.tileDistSq(1)`. The `Outcome` enum stays. |
| `Targeting::electFireTarget` (`:1190-1305`) | walks `pair<int,Entity*>`, recomputes `et` per hit | walks `const std::vector<TraceHit>&`; `const int et = h.eType;` — drop the `ent == nullptr` skip (unrepresentable) and the local `def ? … : ET_WORLD`; returns `TraceHit` instead of `Entity* + int* outFrac` |
| `Targeting::updateFacingProbe` (`:1306-1376`) | `Entity* hit` out-param + `def != nullptr` guard + per-hit `et` decode | `TraceHit hit = trace(...)`; the promotion re-scan walks `hits()`; the outer guard becomes `hit.isEntity()`; the final distance gate uses `distFrom(hit, …)`. `player->facingEntity` keeps receiving an `Entity*` (`hit.entity`) — the HUD contract does not change. |
| `Game::lastTraceHits()` | public accessor returning the pair vector | deleted; `TraceSystem::hits()` returns `const std::vector<TraceHit>&` |
| `Game::traceMove` | bool + 2 out-params | `TraceSystem::trace` returning `TraceHit`; the bool becomes `!hit.blocks()` at each call site |
| `Game::entityDistFrom` | `def == nullptr` world branch | `TraceSystem::distFrom` (two overloads; the `Entity*` overload keeps serving `MonsterSystem`/`CorpseLoot`/`Combat`) |
| `Combat::calcHitEntity` (`Combat.cpp:330-345`) | `env_.game->entityDistFrom(e, …)` | `env_.game->trace.distFrom(e, …)` |

Bit-exactness proof for the reviewer: the sort key, the `-1`, the `16384`
sentinel, the push order (entities then the world hit) and the "empty ⇒ commit"
rule are unchanged; only the carrier type changed.

### 5.3 Named content masks (`domain/game/Enums.h`)

Decode table: `docs/original-code/player-collision.md` §2.2 (with the ET_* ids at
`src/Enums.h:10-25`); named legacy constants at `src/Enums.h:26-40`.

```cpp
namespace Contents {
constexpr int bit(int eType) { return 1 << eType; }

constexpr int WORLD = bit(ET_WORLD);            constexpr int PLAYER = bit(ET_PLAYER);
constexpr int MONSTER = bit(ET_MONSTER);        constexpr int NPC = bit(ET_NPC);
constexpr int PLAYERCLIP = bit(ET_PLAYERCLIP);  constexpr int DOOR = bit(ET_DOOR);
constexpr int ITEM = bit(ET_ITEM);              constexpr int DECOR = bit(ET_DECOR);
constexpr int ENV_DAMAGE = bit(ET_ENV_DAMAGE);  constexpr int CORPSE = bit(ET_CORPSE);
constexpr int ATTACK_INTERACTIVE = bit(ET_ATTACK_INTERACTIVE);
constexpr int MONSTERBLOCK_ITEM = bit(ET_MONSTERBLOCK_ITEM);
constexpr int SPRITEWALL = bit(ET_SPRITEWALL);
constexpr int NONOBSTRUCTING_SPRITEWALL = bit(ET_NONOBSTRUCTING_SPRITEWALL);
constexpr int DECOR_NOCLIP = bit(ET_DECOR_NOCLIP);

// src/MovementController.cpp:326 — NONOBSTRUCTING_SPRITEWALL does block moves.
constexpr int PLAYERSOLID = WORLD | MONSTER | NPC | PLAYERCLIP | DOOR | DECOR |
                            ATTACK_INTERACTIVE | SPRITEWALL | NONOBSTRUCTING_SPRITEWALL;
// src/PlayingInputHandler.cpp:200 = playersolid - PLAYERCLIP + CORPSE.
constexpr int WEAPONSOLID = (PLAYERSOLID & ~PLAYERCLIP) | CORPSE;
// src/MovementController.cpp:38 — the facing/health-bar probe.
constexpr int FACING_PROBE = WORLD | MONSTER | NPC | DOOR | ITEM | DECOR |
                             ATTACK_INTERACTIVE | SPRITEWALL | DECOR_NOCLIP;
// src/Entity.cpp:1078 / src/Combat.cpp:1070.
constexpr int MONSTERSOLID = WORLD | PLAYER | MONSTER | NPC | DOOR | DECOR |
                             ATTACK_INTERACTIVE | MONSTERBLOCK_ITEM | SPRITEWALL |
                             NONOBSTRUCTING_SPRITEWALL;
constexpr int SPLASH_SOLID = WORLD | DOOR | SPRITEWALL;

// Per-weapon modifiers of the fire ray (src/PlayingInputHandler.cpp:203-215).
constexpr int HOLY_WATER_EXTRA = ENV_DAMAGE | DECOR_NOCLIP;   // was | 0x4100
constexpr int MELEE_EXTRA      = PLAYERCLIP;                  // was | 0x10

static_assert(PLAYERSOLID   == 13501, "src/Enums.h CONTENTS_PLAYERSOLID");
static_assert(WEAPONSOLID   == 13997, "src/Enums.h CONTENTS_WEAPONSOLID");
static_assert(FACING_PROBE  == 21741, "src/MovementController.cpp:38");
static_assert(MONSTERSOLID  == 15535, "src/Enums.h CONTENTS_MONSTERSOLID");
static_assert(SPLASH_SOLID  == 4129,  "src/Combat.cpp:1070");
static_assert(HOLY_WATER_EXTRA == 0x4100 && MELEE_EXTRA == 0x10, "src/PlayingInputHandler.cpp:203-215");
} // namespace Contents
```
The existing `CONTENTS_*` constants (`Enums.h:28-37`) are redefined as aliases of
the composed values (`static constexpr int CONTENTS_PLAYERSOLID = Contents::PLAYERSOLID;`)
so no call site is forced to change in this group; new/moved code uses
`Contents::`. Call sites to convert in this group: `PlayerActions` (2×
`CONTENTS_PLAYERSOLID`), `Targeting::electFireTarget` (`13997`, `0x4100`,
`0x10`), `Targeting::updateFacingProbe` (`21741`), and
`TraceSystem::traceWorldFrac` (`mask & 0x10` → `mask & Contents::PLAYERCLIP`,
`mask & 0x800` → `mask & Contents::MONSTERBLOCK_ITEM`, `Game.cpp:584`).

### 5.4 Weapon tables (`domain/game/WeaponTable.h`, header-only views)

Two stride tables are indexed by hand today: `weaponData` stride 9
(`Combat.cpp:81`, `:339-340`, `CombatEntity.cpp:116-190`, `Game.cpp:1159-1160`)
and `weaponInfo` stride 6 (`GameContext.cpp:690-695`).

```cpp
// src/Combat.h:26-35 (stride 9) and :53-59 (wpinfo table 1, stride 6).
enum class WeaponField : int { StrMin=0, StrMax=1, RangeMin=2, RangeMax=3,
	AmmoType=4, AmmoUsage=5, ProjType=6, NumShots=7, Shothold=8 };
constexpr int kWeaponDataStride = 9;
constexpr int kWeaponInfoStride = 6;

// Row view over tables->weaponData. Out-of-range rows read as 0 — the exact
// clamp semantics of Combat::weaponField (new_src/domain/game/Combat.cpp:80-84).
class WeaponRow {
public:
	WeaponRow() = default;
	WeaponRow(const std::vector<int8_t>& t, int weaponId);
	static WeaponRow fromOffset(const std::vector<int8_t>& t, int offset); // pre-scaled weaponId*9
	bool valid() const;
	int8_t field(WeaponField f) const;
	int8_t strMin() const, strMax() const, rangeMin() const, rangeMax() const,
	       ammoType() const, ammoUsage() const, projType() const,
	       numShots() const, shothold() const;
};

// Row view over tables->weaponInfo: idle/attack/flash screen offsets.
struct WeaponPose {
	int idleX, idleY, atkX, atkY, flashX, flashY;
	static bool load(const std::vector<int8_t>& t, int weaponId, WeaponPose& out); // false = row missing
};
```
`Combat::weaponField` stays as a thin forwarder (`WeaponRow(weaponTable(), id).field(...)`)
so `Player.cpp:231-242` keeps compiling; `Combat::weapon(int id)` returns a
`WeaponRow`. Accessors return `int8_t`; call sites that today mask with `& 0xFF`
(`CombatEntity.cpp:188-189`) keep the mask **with its comment** — the
unsigned reinterpretation is legacy behaviour, not a bug to fix here.

### 5.5 Tile-distance indices

`Combat::tileDistances[j] == (64*(j+1))²` (`Combat.cpp:22-24`), so index `j`
means `j+1` tiles — which is why `tileDistances[0]` reads as "one tile" and
`[2]` as "three tiles". Add:
```cpp
// Chebyshev² distance of n tiles (n>=1). tileDistances[n-1]; src/Combat.cpp:41-44.
int tileDistSq(int tiles) const;   // clamps tiles to [1, kMaxTileDistances]
```
Convert: `tileDistances[0]` → `tileDistSq(1)` (`GameContext.cpp:1114,1250,1273`,
`Game.h:159,221`, `Game.cpp:755,1397` comments), `[2]` → `tileDistSq(3)`
(`:1371`), `[3]` → `tileDistSq(4)` (`Game.cpp:818`), `[9]` → `tileDistSq(10)`
(`:1281`). The raw array stays public for `Combat`'s own math.

### 5.6 Remaining bare literals — inventory and verdict

Named in this refactor (Phase 3 group P3-C3, `Enums.h` + `domain/world/MapBits.h`):

| Literal | Site(s) | Name |
|---|---|---|
| `0x95` | `GameContext.cpp:1266` | `Enums::TILENUM_PRACTICE_TARGET` (`src/PlayingInputHandler.cpp:339-343`) |
| `mapFlags & 0x2` | `GameContext.cpp:1344` | `TileFlag::BLOCKS_SIGHT` (opaque tile, `src/MovementController.cpp:63-65`) |
| `mapFlags & 0x40` | `MapParser.cpp:146`, `ScriptVM.cpp:146` | `TileFlag::HAS_EVENT` |
| `mapSpriteInfo & 0xFF` | many | `SpriteInfo::kTileNumMask` |
| `& 0xFF00 >> 8` / `MANIM_MASK` | `Game.cpp:814,928,959`, `World3D` | `SpriteInfo::kAnimByteMask`, `kAnimShift` |
| `& 0xF000000` / `0x3000000` / `0xC000000` | `Game.cpp:325-332,420-423,546-552` | `SpriteInfo::kOrientMask`, `kOrientNS`, `kOrientEW` |
| `& 0xFFFF` sprite index, `+1` bias | `Entity.h:49-50` | already encapsulated in `getSprite/setSprite` — comment only |
| `0x20000` entity info | `Game.cpp:200` (already `kInfoActive`) | comment-only cleanup: drop the redundant `(0x20000)` note |

Left unnamed on purpose (needs a researcher fact first — do NOT invent a name):
`info & 0x200000` (`Combat.cpp:118`), `info & 0x20000000` (`ScriptVM.cpp:999-1006`,
`World3D.cpp:808,885,1107`), `info & 0x2000000` (`World3D.cpp:781`),
`0x420000` / `0x17000` composite writes (`Game.cpp:203,962`), the lootSet packing
constants `0x600/0x2100/0x2140/0x2040` (`Game.cpp:41-45`) and the
`0xFC0/0xFFF/<<6/<<12` loot-entry packing (`Game.cpp:1049-1097`) — the last two
are documented in `docs/original-code/loot-inventory.md`; naming them is a
follow-up ticket, not this refactor.

## 6. Sequencing, parallelism, and verification

### 6.1 Order

```
P1-G1 → P1-G2 → P1-G3 → P1-G4 → P1-G5 → P1-G6 → P1-G7      (sequential: all edit GameContext.{h,cpp})
P2-GA → P2-GB → P2-GC → P2-GD → P2-GE → P2-GF              (sequential: all edit Game.{h,cpp})
```
The two chains are **independent** and run in parallel: Phase 1 touches
`core/*`, `render/SceneRenderer`, `ui/ViewWeapon`, `domain/game/Targeting`;
Phase 2 touches `domain/game/Game.*` and its new subsystem files. The only
shared file risk is `domain/game/Game.h` (Phase 1 modules include it) — Phase 2
keeps the forwarders of §3.1 so Phase 1's call sites never break. Two coders
max; do not run two groups of the same chain at once.

Phase 3 starts when Phase 1 and P2-GA are both landed:

| Group | Files | Parallel with |
|---|---|---|
| P3-B1 | `TraceSystem.{h,cpp}`, `TraceHit.h`, `Game.h` forwarders | P3-C2 |
| P3-B2 | `Targeting.{h,cpp}`, `PlayerActions.cpp` (consumer migration §5.2) | P3-C2 |
| P3-C1 | `Enums.h` masks + `TraceSystem.cpp`, `Targeting.cpp`, `PlayerActions.cpp` call sites | — (after P3-B2) |
| P3-C2 | `WeaponTable.h` + `Combat.{h,cpp}`, `CombatEntity.cpp`, `Player.cpp`, `ViewWeapon.cpp`, `CorpseLoot.cpp` | P3-B1/B2 |
| P3-C3 | `Enums.h` tile names + `MapBits.h` + `Game.cpp`/`World3D.cpp`/`ScriptVM.cpp` call sites | P3-B*, P3-C1 |
| P3-C4 | `tileDistSq` in `Combat.{h,cpp}` + call sites | after P3-C1 |
| P3-F1..F5 | forwarder sweep: delete each `Game.h` forwarder and fix its callers, one subsystem per group | one at a time |

### 6.2 Verification, every group

1. `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug && cmake --build build_new -j 8`
   — zero new warnings, zero errors.
2. `git diff` self-check by the coder: the moved block must be identical to the
   original modulo the access-path rename. `git diff -M --stat` should show a
   near-1:1 line transfer, and the coder reports any line they had to change and
   why.
3. Run `cd build_new/new_src && ./DoomIIRPG` and ask the user to re-check the
   behaviours listed for that group (§2/§3). No group is "done" before the eye
   check for its own list.
4. Full regression list once per chain completion (the list the user already
   confirmed on 2026-08-26): lift-ride cutscene timing and framing, health bar at
   range, shooting while standing on a corpse, point-blank wall shot consumes no
   ammo and no turn, rifle size/position + muzzle flash, cutscene entry without a
   vertical viewport step, doors (incl. locked/card), loot dwell UI + grants,
   dialogs, TAB "Turn Passed", monster wake/pain/death/corpse looting.
5. Reviewer greps for regressions of the two bug classes:
   `grep -rn "def == nullptr" new_src/core new_src/domain/game/Targeting.cpp` must
   be empty after P3-B2, and `grep -rn "cameraActive\|state == StateId::Camera"`
   must show at most the `CinematicCamera` internals.

### 6.3 Definition of done

- `GameContext.cpp` ≤ 400 lines, `Game.cpp` ≤ 300 lines, no new file above ~350.
- No module holds a `GameContext*`; only `StateHost*` (grep the new headers).
- `TraceHit` is the only carrier of trace results; `lastTraceHits`/`traceMove`
  out-params are gone.
- `13997`, `21741`, `13501`, `0x4100`, `0x10`, `weaponId * 9`, `w * 6`,
  `tileDistances[0/2/3/9]`, `0x95` and `mapFlags & 0x2` no longer appear as bare
  literals outside their defining header (each proven by `static_assert` where a
  value is recomposed).
- The user sees exactly the same game.
