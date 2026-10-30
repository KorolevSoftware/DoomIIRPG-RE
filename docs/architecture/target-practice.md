# Target practice + sniper scope zoom — spec

Date: 2026-10-30
ADR: [0039](adr/0039-zoom-state-in-game-target-practice-in-playing.md)
Facts: `docs/original-code/target-practice.md` (cited below as **TP §n**),
raw log `docs/research/2026-10-30-target-practice.md`.

## 0. Scope and what exists

The rewrite has **no sniper zoom at all**: `new_src/core/PlayerActions.cpp:330-337` only logs
"zoom-entry weapons deferred", `new_src/domain/game/CombatEntity.cpp:110-114` logs "zoom bbox
path unsupported", `new_src/render/SceneRenderer.cpp:119` hard-codes FOV 290. Target practice
cannot work without the zoom (every scored shot is fired zoomed, TP §2.1), and the zoom is the
scoped rifle's normal-play behaviour too. So this spec ports both.

Already present and reused as-is:

| Piece | Where | Verdict |
|---|---|---|
| `stripInventoryForTargetPractice` | `new_src/domain/game/Player.cpp:297-305` | matches `src/Player.cpp:2694-2705` (rifle + 8 bullets, `give(1,9,1)` auto-selects via `isNew`, `Player.cpp:217,231-235`). |
| `restoreInventory` | `new_src/domain/game/Player.cpp:308-339` | matches `src/Player.cpp:2707-2759` **except** the missing `forceRemoveFromScopeZoom()` (`:2755`, comment `Player.cpp:307`). Added by the callers (§5.3, §5.6), not inside `Player`. |
| Decor tile-149 election filter | `new_src/domain/game/Targeting.cpp:112-121` | matches `src/PlayingInputHandler.cpp:346-352`. |
| Combat "Missed!" | `new_src/domain/game/Combat.cpp:595-604` | gated on `targetType == ET_MONSTER`, like `src/Combat.cpp:333-343`. A decor target (eType 7) never prints Combat's 68, so the TP 68/230-232 messages cannot double up. |
| Weapon 9 row | `docs/research/2026-08-31-projtype-inventory.md:117` | projType 0 (instant), ammoType 1, usage 1, numShots 1, SHOTHOLD 30: `fireWeapon` accepts it (`Player.cpp:727-731`). |
| Ammo deduction timing | `new_src/domain/game/Combat.cpp:613-614` | at the combat stage, after `fireWeapon` returned, like `src/Combat.cpp:355-357`. So 8 scored shots, the 9th FIRE exits (TP §2.1). |
| Help FIFO | `DialogSystem::enqueueHelpDialog(type, id, threadIdx)` (`DialogSystem.cpp:205-219`) | `showHelp(17)` = `enqueueHelpDialog(0, 17, -1)` (`src/DialogSystem.cpp:839-841`). |

Out of scope (each one is named in a code comment where it applies):

- Touch scope controls: dial drag (`src/TouchController.cpp:64-67,141-168`), the
  `m_sniperScopeButtons` fire button (`src/Canvas.cpp:248-250`), the accelerometer and
  `centerMouse` (`src/ZoomController.cpp:32,38,...`, `src/App.cpp:557-617`).
- The zoomed facing-probe Z test (`src/MovementController.cpp:40`, last arg) and the zoomed
  +20 health-bar offset (`src/Hud.cpp:869-871`).
- `Player::selectWeapon` → `zoomOut` (`src/Player.cpp:142-143`). In the rewrite no weapon
  switch can happen while zoomed: menus are unreachable, and Z/X mean zoom in/out.
- `helpBitmask` persistence in the config file. `new_src/core/SaveGame.cpp:162` writes zeros.
  The bit lives for the session only.

## 1. Module layout (ADR 0039)

| New / changed | Kind | Role |
|---|---|---|
| `domain/game/ZoomState.h` | new, header only | Plain zoom data: the 15 legacy `Canvas` zoom fields (`src/Canvas.h:330-344`) + `ZoomPose`. Lives on `Game::zoom`. |
| `core/ZoomController.h/.cpp` | new | Port of `src/ZoomController.cpp`: `initZoom`, `zoomOut`, `forceRemove`, `drop`, `handle(Action)`, `renderPose`. Owned by `GameContext`. |
| `domain/game/ShotZones.h/.cpp` | new | `Render::getImageFrameBounds(tile,3,2,0)` + the zone loop. Shared by target practice and the monster scope crit. |
| `core/TargetPractice.h/.cpp` | new | Port of `Player::enterTargetPractice / assessTargetPracticeShot / exitTargetPractice` + the canvas fields `save*`, `*ShotTime`, `targetPracticeThread`. Owned by `GameContext`. |
| `domain/world/MapData.h` | edit | `spriteZ(s)`: the legacy baked `S_Z`. |
| `domain/game/TraceSystem.h/.cpp` | edit | `trace3D(...)`: the zoomed fire trace with the entity Z band. |
| `domain/game/Targeting.h/.cpp` | edit | Zoomed election from the rendered view matrix; `fireEnd()`. |
| `render/SceneRenderer.h/.cpp` | edit | `drawWorld(..., const ZoomPose*)`. |
| `render/World3D.h/.cpp` | edit | Layered tile-149 branch + `setPracticeTargetFrames`. |
| `ui/UiAssets.h/.cpp`, `ui/Hud.h/.cpp` | edit | Scope overlay, dial + knob, score bar. |
| `core/PlayerActions.*`, `core/GameContext.*`, `core/GameStates.h`, `core/UiInputCollector.cpp`, `core/LoadingManager.cpp`, `domain/game/ScriptVM.cpp`, `domain/game/Player.h/.cpp`, `domain/game/Game.h`, `domain/game/Combat.h`, `domain/game/CombatEntity.cpp`, `core/Main.cpp` | edit | Wiring and hooks. |

Single-threaded, as everywhere else. Clocks: legacy `app->time` = `GameContext::upTimeMs`
(`new_src/core/GameContext.cpp:252`); legacy `app->gameTime` = `GameContext::gameTime`.
**New files ⇒ re-run `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`** (GLOB) in
G1, G3 and G4.

## 2. Groups (order = delegation order)

| Group | Content | Depends on |
|---|---|---|
| **G1** | Zoom core: `ZoomState`, `ZoomController`, input Z/X, render pose, view weapon hide, state drops | — |
| **G2** | Scope HUD: `scope.bmp` overlay, dial + knob | G1 |
| **G3** | Zoomed fire: `trace3D`, zoomed election, `zoomCollision`, `MapData::spriteZ`, `ShotZones` | G1 |
| **G4** | `TargetPractice` module + opcode 84 + playing-input hooks + `helpBitmask` | G3 |
| **G5** | Target sprite layers (World3D tile 149) | G4 |
| **G6** | Score bar + "Exit" soft key | G4 |
| **G7** | Monster scope crit (`CombatEntity` zoom block) | G3 |

---

## G1 — Zoom core

### G1.1 `new_src/domain/game/ZoomState.h` (new)

```cpp
#ifndef NEW_DOMAIN_GAME_ZOOMSTATE_H
#define NEW_DOMAIN_GAME_ZOOMSTATE_H
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
}
#endif
```
(`#include <cstdint>`.) Add `ZoomState zoom;` to `Game` next to the peer subsystems
(`new_src/domain/game/Game.h:182-191`), commented `// sniper zoom data (ADR 0039)`.

### G1.2 `new_src/core/ZoomController.h/.cpp` (new)

```cpp
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
	bool zoomedIn() const;               // env_.game->zoom.isZoomedIn
	void initZoom();                     // src/ZoomController.cpp:21-39
	void zoomOut();                      // :41-53
	void forceRemove();                  // Player::forceRemoveFromScopeZoom, src/Player.cpp:2762-2769
	void drop();                         // setState(DIALOG/DYING) inline zoom drop, src/Canvas.cpp:1085-1091, :1116-1124
	bool handle(Action a);               // handleZoomEvents(key, a, false), :59-166; false = FIRE, caller continues
	bool renderPose(ZoomPose& out);      // src/MovementController.cpp:525-541; false = not zoomed
private:
	Env env_;
};
```

Algorithms (`Z` = `env_.game->zoom`, `p` = player, `now` = `*upTimeMs`, sin =
`tables->sinTable`, 16.16):

- **initZoom** (`:21-39`): `zoomTime = 0; zoomCurFOVPercent = 0; zoomFOV = zoomDestFOV = 190;
  zoomAngle = 0; zoomPitch = 0; zoomTurn = 0; p.viewPitch = 0; zoomStateTime = now;
  isZoomedIn = true;`
  `zoomAccuracy = 2560 * clamp(((256 - p.ce.getStatPercent(STAT_ACCURACY)) << 8) / 26, 0, 256) >> 8;`
  (`:33`, `getStatPercent = stat<<8/100`, `src/CombatEntity.cpp:58-60`)
  `zoomMinFOVPercent = 256; zoomMaxAngle = 64 - (zoomAccuracy >> 8);`
  `hud->startFade(now, 500, Hud::kFadeIn);` Skipped: accelerometer, the dial `Update(0,320)`
  (the dial is derived from `zoomCurFOVPercent` in G2), soft keys (rebuilt per frame), `centerMouse`.
- **zoomOut** (`:41-53`): `isZoomedIn = false; p.viewAngle += zoomAngle;
  p.destAngle = p.viewAngle = (p.viewAngle + 127) & ~255;` then
  `hud->startFade(now, 500, Hud::kFadeIn); actions->finishRotationFired();`
  (`finishRotation(true)`). `resetViewPort` has no counterpart (one viewport, ADR 0009).
- **forceRemove** (`src/Player.cpp:2762-2769`): `if (Z.isZoomedIn) { Z.zoomTurn = 0; zoomOut(); }`.
  `handleZoomEvents(-6, 15, true)` = forced ACTION_BACK = `zoomOut()` + return
  (`:63-70`).
- **drop**: `if (!Z.isZoomedIn) return; Z.isZoomedIn = false;
  p.destAngle = p.viewAngle = (p.viewAngle + Z.zoomAngle + 127) & ~255;` No fade, no rotation hook
  (`src/Canvas.cpp:1088`, `:1119-1121`).
- **handle(a)**, port of `:59-166`:
  1. Gate (`:63-65`): `if (Z.zoomTime != 0 || game->sprites.activePropogators() != 0 ||
     game->lerps.animatingEffects() != 0 || !game->monsters.snapMonsters(false)) return true;`
  2. `n3 = 5 + ((20 * (256 - Z.zoomCurFOVPercent)) >> 8);` (`:66`)
  3. Branch on `a` (rewrite action → legacy action):
     - `Menu`, `BackKey` (ACTION_MENU/BACK, `:67-70`) → `zoomOut(); return true;`
     - `Automap` (`:71-77`): if not in target practice → `hud->clearMessages();
       host->requestState(StateId::Menu); return true;` (MENU_INGAME = the default menu id).
       In target practice (`env_.inTargetPractice && env_.inTargetPractice()`) → no branch
       body (fall through to step 4).
     - `TurnRight` → `zoomAngle -= n3; ++zoomTurn;` `TurnLeft` → `zoomAngle += n3; ++zoomTurn;` (`:78-91`)
     - `Back` (ACTION_DOWN) → `zoomPitch -= n3; ++zoomTurn;` `Forward` (ACTION_UP) → `zoomPitch += n3; ++zoomTurn;` (`:92-104`)
     - `Passturn` (`:105-111`): show msg 45 exactly as `PlayerActions.cpp:378-385` does;
       `game->advanceTurn(); zoomTurn = 0;`
     - `NextWeapon` (`:112-129`): `if (cur < min) { cur = min(cur + 64, min); ++zoomTurn;
       zoomDestFOV = max(190 + ((-55 * cur) >> 7), 102); zoomTime = now + 360;
       fprintf(stderr, "[sound] 1113\n"); }`
     - `PrevWeapon` (`:130-147`): `if (cur > 0) { cur = max(cur - 64, 0); ++zoomTurn;
       zoomDestFOV = 190 + ((-55 * cur) >> 7); zoomTime = now + 360; [sound] 1113 }`
       then **always** `++zoomTurn;` (`:146`)
     - `Use` (ACTION_FIRE, `:148-151`) → `zoomTurn = 0; return false;`
     - anything else → no branch body.
  4. Clamp (`:153-158`): `zoomPitch = clamp(zoomPitch, -zoomMaxAngle, zoomMaxAngle);`
  5. `if ((zoomTurn & 7) == 7) game->advanceTurn();` (`:159-161`). `return true;`
- **renderPose(out)** (`src/MovementController.cpp:525-541`): `if (!Z.isZoomedIn) return false;`
  ```
  t  = now - zoomStateTime;
  n2 = zoomAccuracy * sin[(t / 2) & 0x3FF] >> 24;
  n3 = zoomAccuracy * sin[(t / 3) & 0x3FF] >> 24;
  if (now < zoomTime) fov = zoomDestFOV + (zoomFOV - zoomDestFOV) * (zoomTime - now) / 360;
  else { zoomTime = 0; fov = zoomFOV = zoomDestFOV; }
  pitch = zoomPitch + p.viewPitch;
  Combat& c = game->combat;   // recoil kick, :537-539 (ST_COMBAT && player attack && nextStage == 1)
  if (c.active && c.curAttacker == nullptr && c.nextStage == 1 && c.animTime > 0)
      pitch += sin[(512 * (((gameTime - c.animStartTime) << 16) / c.animTime) >> 16) & 0x3FF] * 28 >> 16;
  out = { p.viewAngle + zoomAngle + n2, pitch + n3, fov };  return true;
  ```
  Use 64-bit intermediates for the `<< 16` term.

### G1.3 Input actions

- `new_src/core/GameStates.h`: add `NextWeapon, PrevWeapon` to `Action` after `ItemsDrinks`,
  with the comment `// ACTION_NEXTWEAPON / ACTION_PREVWEAPON (src/Enums.h:1002-1003)`.
- `new_src/core/UiInputCollector.cpp:111-113`: map `SDL_SCANCODE_Z → Action::NextWeapon`
  (`src/Input.cpp:86`) and `SDL_SCANCODE_X → Action::PrevWeapon` (`:87`). Update the
  "Reserved, deliberately unmapped" comment (P stays reserved).
- Outside zoom, `PlayerActions::handleAction` ignores both. `selectNext/PrevWeapon` is not
  ported (`Player.cpp:334`); write the shortcut comment in the `default:` arm.

### G1.4 `PlayerActions` dispatch

`PlayerActions::Env` += `ZoomController* zoom = nullptr;`. In `handleAction`
(`new_src/core/PlayerActions.cpp:125-166`):

1. In the `combat.active` block: turn only when `curAttacker != nullptr &&
   !env_.game->zoom.isZoomedIn` (`src/InputEventController.cpp:365`).
2. Right after the combat block, before the view gate:
   ```cpp
   const bool zoomed = env_.game->zoom.isZoomedIn && env_.host->state() == StateId::Playing;
   if (zoomed && env_.zoom->handle(a)) return;     // src/InputEventController.cpp:358-361
   ```
3. View gate (`:139-141`) only `if (!zoomed)` (`src/PlayingInputHandler.cpp:25`). The
   monster gate stays as it is. A zoomed press that reaches it is always `Use`.
4. Zoom entry: replace the block at `PlayerActions.cpp:330-337` (static `zoomLogged`) with
   ```cpp
   if (!zoomed && Combat::checkWeaponMask(weapon2, 512) &&
       p.ammo[env_.game->combat.weaponDef(weapon2).ammoType] > 0) {   // :504-507
       env_.zoom->initZoom();
       break;
   }
   ```
5. `kWallPush` branch: `if (zoomed) break;` first (`src/PlayingInputHandler.cpp:468-470`).
   Update the stale "no zoom in the rewrite" comment.
6. Corpse loot election: call `findLootableCorpseFacing` only when `!zoomed`
   (`src/PlayingInputHandler.cpp:323`, `else if (!canvas->isZoomedIn)`).

### G1.5 Rendering hooks

- `SceneRenderer::drawWorld(RenderBackend&, Window&, const MayaPose* cinePose, bool underDialog,
  const ZoomPose* zoomPose)`. In the player branch (`SceneRenderer.cpp:91-121`):
  `yaw = zoomPose ? zoomPose->yaw : viewAngle`, likewise `pitch` (else `viewPitch`) and
  `fov` (else 290). Use that `yaw` for `viewSin/viewCos`, the shake and the pull-back:
  legacy `Render::render` receives the zoomed angle (`src/MovementController.cpp:541`).
  `camera_.setView(rvx, rvy, rvz, yaw, pitch, viewRoll, fov, (fov << 14) / ((478 << 14) / 248))`
  (`src/Render.cpp:2223`).
- `GameContext::render` (`GameContext.cpp:1023`): `ZoomPose zp; const ZoomPose* zpp =
  (cinePose == nullptr && zoom_.renderPose(zp)) ? &zp : nullptr;` and pass it on.
- View weapon gate (`GameContext.cpp:1039`): add `&& !sys_.game->zoom.isZoomedIn`
  (`src/Canvas.cpp:1348`). Remove the "no zoom system yet" notes at `ViewWeapon.cpp:81-82`.

### G1.6 Ownership and state hooks

- `GameContext.h`: member `ZoomController zoom_;`, accessor `ZoomController& zoom()`.
  Wire it in `GameContext::init` next to `actions_` (both point at each other; init order does
  not matter, they only store pointers). Fill `PlayerActions::Env::zoom`.
- `enterState_` (`GameContext.cpp:184`, `:198`): `case Dialog` and `case Dying` call
  `zoom_.drop()` first (`src/Canvas.cpp:1085-1091`, `:1116-1124`).
- `LoadingManager`: where the map load tears down (the `unloadMedia` analog, legacy
  `src/LoadingManager.cpp:627`), set `env_.game->zoom.isZoomedIn = false;`.

### G1 acceptance

Build clean. G1 is gated on the build alone; nothing can grant the rifle before G4. Its screen
test runs inside the range after G4 (or earlier if the user owns weapon 9):
- FIRE with the rifle selected and bullets > 0 → 500 ms fade-in and the view weapon
  disappears. FOV 190 instead of 290 magnifies the picture about 1.5×. The view sways
  slowly; the amplitude depends on accuracy.
- Z → zoom in, 360 ms smooth FOV change, `[sound] 1113` logged; up to 4 steps (190 → 163 →
  135 → 108 → 102). X steps back.
- Arrows/W/S/A/D nudge yaw/pitch by small steps (5..25 units). Pitch stops at ±`zoomMaxAngle`.
- Every 7th nudge the monsters take a turn (`zoomTurn & 7`).
- ESC or Backspace → zoom out with a fade. The view snaps to the nearest 90°.
- Tab while zoomed (not in target practice) opens the in-game menu.

---

## G2 — Scope HUD

- `UiAssets` (`new_src/ui/UiAssets.h:38` area, `.cpp:80` area): `Texture scope, sniperDial,
  sniperKnob;` loaded with `loadUiTexture` from `"scope.bmp"` (`src/App.cpp:456`),
  `"SniperScope_Dial.bmp"`, `"SniperScope_Knob.bmp"` (`src/Canvas.cpp:234-235`).
- `Hud` public:
  ```cpp
  // src/Hud.cpp:730-731: imgScope at (0, 20), anchor 0.
  void drawScope(Graphics2D& g);
  // src/Hud.cpp:753-767 + dial construction src/Canvas.cpp:241-243.
  void drawScopeDial(Graphics2D& g, int curFovPercent, int minFovPercent);
  ```
  Dial math (constants from `src/Canvas.cpp:241-242`, `src/Button.cpp:380-406`; knob position
  from `src/ZoomController.cpp:122-125`):
  ```
  bx = 400, by = 67, dialW = sniperDial.width(), barH = sniperDial.height();
  thumb  = barH / 16;                       // SetScrollBox(0,0,1,1,16): field_0x4c = 1*barH/16
  maxScr = barH - thumb;
  pos    = (int)((float)maxScr - (float)maxScr * ((float)cur / (float)min));   // field_0x48
  knobY  = pos + (thumb >> 1);
  draw sniperDial at (bx, by);
  draw sniperKnob at (bx - (knobW >> 1) + (dialW >> 1), by + (knobY << 2) / 5 - (knobH >> 1) + 16);
  ```
  Deviation, one-line comment: legacy uses `menuSystem->imgMenuDialKnob` dimensions, a pointer
  `src/` never loads (only declared, `src/MenuSystem.h:179`). Use the knob's own size.
  At `cur == 0` (initZoom) the knob is at the bottom, which matches `Update(0, 320)`.
- `GameContext::render`, inside `if (gameplayView)` (`GameContext.cpp:1077`): call
  `sys_.hud->drawScope(g)` first when zoomed (repaint bit 0x1, before effects,
  `src/Hud.cpp:728-733`). After `drawHud` (`:1105-1108`): when zoomed and `state_ != Dialog`
  and `gameplayView`, call `drawScopeDial(g, zoom.zoomCurFOVPercent, zoom.zoomMinFOVPercent)`
  (`src/Hud.cpp:747-768`).

**G2 acceptance:** when zoomed, the scope mask covers the 3D view (black surround, circular
hole, crosshair). The dial sits at the right edge near the top (x 400, y 67) with the knob at the
bottom. Each Z moves the knob up a quarter; at max zoom it is at the top. Neither shows when
not zoomed.

---

## G3 — Zoomed fire trace

- `MapData.h` (next to `heightAt`, `:66-69`):
  ```cpp
  // Legacy baked S_Z (postProcessSprites, src/Render.cpp:2459-2467): stored Z + terrain,
  // z-sprites -32. The rewrite stores raw Z and bakes at use (SpriteLerps::spriteZBias).
  int spriteZ(int s) const {
      const int x = mapSprites[s], y = mapSprites[numSprites + s];
      return mapSprites[2 * numSprites + s] + heightAt(x, y) - (s >= numNormalSprites ? 32 : 0);
  }
  ```
- `TraceSystem`:
  ```cpp
  // Zoomed fire trace: legacy Game::trace 10-arg with b == true (src/Game.cpp:199-327).
  // Same sweep as trace(); circle-branch entity hits additionally need the ray height at the
  // hit frac within ±(32 + radius) of the entity Z (:277-283). Line (wall-sprite) hits and
  // the world pass are unchanged.
  TraceHit trace3D(int x0, int y0, int z0, int x1, int y1, int z1, Entity* skipEnt, int mask, int radius);
  ```
  Implementation: set the private members `zCheck_ = true; z0_ = z0; z1_ = z1;`, call
  `trace(...)`, then set `zCheck_ = false`. In `traceEntityHits`' circle branch
  (`TraceSystem.cpp:158-159`, replacing the "Z filter omitted" note), after
  `frac < 16384`:
  ```
  if (zCheck_ && z0_ >= 0 && z1_ >= 0) {
      destZ = map.spriteZ(sprite);           // monsters too: legacy uses S_Z even at the goal tile (:228-231)
      rel = z0_ + ((z1_ - z0_) * frac >> 14) - destZ;
      if (!(rel > -(32 + radius) && rel < 32 + radius)) continue;   // reject
  }
  ```
  The `ET_PLAYER` arm (`destZ = canvas->destZ`) is never reached, because the fire trace
  skips the player entity. Accept without a test and say so in a comment.
- `Targeting`:
  - `Env` += `const int* view = nullptr;`, the 14.14 view matrix of the last world pass.
    Wire it to `scene_.camera().viewInt()` (a stable member array) in `GameContext::init`.
  - `electFireTarget(weapon)`: if `env_.game->zoom.isZoomedIn && env_.view`:
    ```
    endX = p.viewX + (tiles * -view[2] >> 8);   // src/PlayingInputHandler.cpp:216-219
    endY = p.viewY + (tiles * -view[6] >> 8);
    endZ = p.viewZ + (tiles * -view[10] >> 8);
    trace.trace3D(p.viewX, p.viewY, p.viewZ, endX, endY, endZ, playerEntity, mask, 2);
    ```
    Else keep the current 2D code and set `endZ = p.viewZ`. Store `fireEnd_[3] = {endX,
    endY, endZ}`. Public `const int* fireEnd() const`.
- `PlayerActions` fire commit (`PlayerActions.cpp:338-354`), elected branch, before
  `fireWeapon` (`src/PlayingInputHandler.cpp:518-522`):
  ```cpp
  if (zoomed) {
      const int* e = env_.targeting->fireEnd();
      ZoomState& z = env_.game->zoom;
      z.zoomCollisionX = p.viewX + (elected.frac * (e[0] - p.viewX) >> 14);
      z.zoomCollisionY = p.viewY + (elected.frac * (e[1] - p.viewY) >> 14);
      z.zoomCollisionZ = p.viewZ + (elected.frac * (e[2] - p.viewZ) >> 14);
  }
  ```
- `domain/game/ShotZones.h/.cpp` (new):
  ```cpp
  struct MediaMappings;
  // Render::getImageFrameBounds(tile, 3, 2, 0) (src/Render.cpp:2025-2036) + the zone loop of
  // Player::assessTargetPracticeShot (src/Player.cpp:2561-2568) / CombatEntity::calcHit
  // (src/CombatEntity.cpp:180-202). Returns 0 = head (frame 3), 1 = body (frame 2),
  // 2 = legs (frame 0), -1 = no zone. headZLo/headZHi: additive zone-0 Z tweak (revenant).
  int shotZone(const MediaMappings& m, int tileIndex, int dx, int dy, int dz,
               int headZLo = 0, int headZHi = 0);
  ```
  Body: `base = m.mappings[tileIndex]`. For `i, frame in {(0,3),(1,2),(2,0)}`:
  `k = (frame + base) << 2`; `b(j) = m.bounds[k + j] & 0xFF`;
  `x0 = b(0)*64/176 - 32; x1 = b(1)*64/176 - 32; z0 = (176 - b(3))*64/176 - 32;
  z1 = (176 - b(2))*64/176 - 32`; for `i == 0`, `z0 += headZLo; z1 += headZHi`.
  Match when `x0 < dx < x1 && x0 < dy < x1 && z0 < dz < z1` (dy against the **horizontal**
  range, verbatim, TP §2.2). The first match wins. Guard the indices (`base < 0` or out of
  range → -1).
  Self-check for this new logic: print one `[zones] tile=%d head=(..) body=(..) legs=(..)`
  line the first time each tile is evaluated. It also answers the open question about the
  tile-149 boxes (research, "Open questions").

**G3 acceptance:** build clean. Zoomed fire at a monster still hits (the Z band passes at
eye-level aim). Pitch far up or down with W/S and fire → the monster is no longer elected
(air shot). Not zoomed: behaviour unchanged.

---

## G4 — `TargetPractice` + opcode 84 + input hooks

### G4.1 `Player`

`int helpBitmask = 0;` with the comment `// src/Player.cpp:30, :1185-1188; session-only
(config persistence not ported)`. Change the comment at `Player.cpp:307` to "forceRemove
is done by the callers (TargetPractice::exit, EV_INVENTORY_OP 2)".

### G4.2 `new_src/core/TargetPractice.h/.cpp` (new)

```cpp
class TargetPractice {
public:
	struct Env {
		Game* game = nullptr;  Player* player = nullptr;  Hud* hud = nullptr;
		const Localization* loc = nullptr;  MapData* map = nullptr;
		const MediaLoader* media = nullptr;  ScriptVM* vm = nullptr;
		DialogSystem* dialogs = nullptr;  PlayerActions* actions = nullptr;
		ZoomController* zoom = nullptr;  StateHost* host = nullptr;
		const CinematicCamera* cinematic = nullptr;   // isCameraActive gate of showHelp
		const int64_t* upTimeMs = nullptr;
	};
	void init(const Env& env);
	bool active() const { return active_; }            // Player::inTargetPractice
	int score() const { return score_; }
	void enter(int tileX, int tileY, int dir, ScriptThread* thread);  // src/Player.cpp:2521-2551
	void assessShot(Entity* target);                                  // :2553-2598
	void exit();                                                      // :2600-2634
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
```

- **enter** (`src/Player.cpp:2521-2551`):
  1. `active_ = true; thread_ = thread;` save `viewX/Y/Z/Angle`.
  2. `angle = dir == 4 ? 512 : dir == 0 ? 0 : dir == 2 ? 256 : 768` (`:2531-2542`,
     `src/Enums.h:377-383`). `destAngle = viewAngle = angle; destX = viewX = (tileX << 6) + 32;`
     same for Y; `destZ = viewZ = map->heightAt(viewX, viewY) + 36`.
  3. Keep the two deviations the current stub already has (`ScriptVM.cpp:801-804`):
     `game->relinkPlayer(p)` (D1, spec 2026-09-22-monster-attacks §2.1) and
     `actions->finishRotationFired()`. Move their comments here.
  4. showHelp(17, false) (`src/Player.cpp:1175-1194`): `if (!cinematic->active() &&
     (p.helpBitmask & (1 << 17)) == 0) { p.helpBitmask |= 1 << 17;
     dialogs->enqueueHelpDialog(kTextMain, 17, -1); }`. `enqueueHelpDialog` already
     dequeues when the state is Playing (`DialogSystem.cpp:217`).
  5. `p.stripInventoryForTargetPractice(); score_ = 0; hud->startFade(now, 750, Hud::kFadeIn);`
- **assessShot(target)** (`:2553-2598`): `s = target->getSprite(); n = map->numSprites;`
  `dx = Z.zoomCollisionX - mapSprites[s]; dy = Z.zoomCollisionY - mapSprites[n + s];
  dz = Z.zoomCollisionZ - map->spriteZ(s); anim = (mapSpriteInfo[s] >> 8) & 0xF0;`
  `zone = shotZone(media->mappings(), target->def->tileIndex, dx, dy, dz);`
  - `zone == 0 || (zone != -1 && anim == Enums::MANIM_IDLE_BACK)` → msg 230, `score_ += 30`,
    `headShotTime_ = now + 500`, the other two = 0.
  - `zone == 1` → msg 231, `+20`, `bodyShotTime_ = now + 500`, the others 0.
  - `zone == 2` → msg 232, `+10`, `legShotTime_ = now + 500`, the others 0.
  - `-1` → msg 68, all three = 0.
  Messages: `hud->addMessage(loc->get(kTextMain, id), 0xFF7F0000, Hud::kMsgFlagImportant)`
  (flag 4; the important bar color is fixed, `src/Hud.cpp:257`). Log
  `[targetpractice] shot zone=%d d=(%d,%d,%d) score=%d`.
- **exit** (`:2600-2634`):
  1. `active_ = false;` restore view+dest X/Y/Z/Angle from `save*`. `game->relinkPlayer(p)`
     (the D1 counterpart of enter). `actions->finishRotationFired()` (`finishRotation(false)`;
     the `b` flag only touches a centre-message timer, `src/MovementController.cpp:295-297`).
  2. `zoom->forceRemove();` then `p.restoreInventory();`. Legacy calls `forceRemove` inside
     `restoreInventory` after the weapon re-give (`src/Player.cpp:2755`). Hoisting it is
     equivalent: `restoreInventory` reads no view state, and the 750 ms fade below overrides
     zoomOut's 500 ms fade in both orders.
  3. `hud->startFade(now, 750, Hud::kFadeIn);`
  4. `args[0] = to_string(score_)`. `if (score_ > 120)` (`240 / 2`, strict):
     `gain = p.modifyStat(Enums::STAT_ACCURACY, 1)`. If `gain > 0`: `args[1] = to_string(gain)`
     and msg 233, else msg 235. Otherwise msg 234. Compose with `composeArgs(text, args, k)` and
     post with `hud->addMessage(text, 0xAA000000, 3)` (FORCE|CENTER, `:2615-2627`).
  5. `if (thread_) { ScriptThread* t = thread_; thread_ = nullptr;
     host->requestState(StateId::Playing); vm->resumeThread(t); }` Capture-clear-run is the
     order already used at `PlayerActions.cpp:55-59`.
- **layerFrames(now, zoomed, out)**: for each `(timer, flash, idle)` in
  `(leg,0,5), (body,2,6), (head,3,7)` → `out[k] = timer != 0 ? (timer > now ? flash : (timer = 0, -1)) : idle;`
  then `out[3] = zoomed ? 1 : 4`.

### G4.3 Opcode 84 and EV_INVENTORY_OP

- `ScriptVM.cpp:789-813` becomes:
  ```cpp
  case Enums::EV_START_TARGETPRACTICE: {     // src/ScriptThread.cpp:1806-1813
      int v = readUShort(t);
      env_.ctx->targetPractice().enter((v >> 8) & 0x1F, (v >> 3) & 0x1F, v & 7, t);
      t->unpauseTime = -1;                   // parked until TargetPractice::exit resumes it
      n = 2;
      break;
  }
  ```
- `EV_INVENTORY_OP` op 2 (`ScriptVM.cpp:1577-1580`): call `env_.ctx->zoom().forceRemove();`
  before `player.restoreInventory()` (the same `src/Player.cpp:2755` hoist).

### G4.4 Ownership

`GameContext.h`: member `TargetPractice practice_;`, accessor `TargetPractice&
targetPractice()`. Wire `init` (media = `sys_.media`, cinematic = `&cinematic_`). Set
`ZoomController::Env::inTargetPractice = [this]{ return practice_.active(); }` and
`PlayerActions::Env::practice = &practice_` (new field `TargetPractice* practice = nullptr;`).

### G4.5 `PlayerActions` hooks (`tp` = `env_.practice`, `inTP = tp && tp->active()`)

| Site | Change | Source |
|---|---|---|
| `Action::Automap` (`:387`) | `if (inTP) break;` | `src/PlayingInputHandler.cpp:86-87` |
| `Action::BackKey` (`:391`) | after the Automap test: `if (inTP) { tp->exit(); break; }` | `:138-147` |
| `Action::Menu` (`:397`) | `if (inTP) { tp->exit(); break; }` first | `:149-158` |
| `Action::Items`, `Action::ItemsDrinks` | `if (inTP) { tp->exit(); break; }` first | `:159-178` |
| Fire, elected branch, after `fireWeapon` | `if (inTP) { if (p.ammo[1] == 0) tp->exit(); else tp->assessShot(target); }` | `:524-531` |
| Fire, air-shot branch, after `fireWeapon` | `if (inTP) { if (p.ammo[1] == 0) tp->exit(); else hud->addMessage(loc->get(kTextMain, 68), 0xAA000000, 0); }` | `:537-543` |

Note: legacy calls assess/exit even when `fireWeapon` refused. Do not gate on its return value.
The zoomed BACK/MENU never reach these rows, because `ZoomController::handle` takes them
first (TP §4).

**G4 acceptance (map00, ≥ 2 UAC credits; inventory slot 24):**
1. Walk to the terminal at tile (19,26) and face south. Press Enter → dialog 91 (yes/no) →
   Yes. The help "VIRTUAL TARGET PRACTICE…" appears, the first time per session only.
2. Close it → fade-in. You stand on tile (16,29) facing west, with the target two tiles ahead.
   The bottom bar shows the scoped rifle with 8 bullets. Log: `[targetpractice] …`.
3. Enter → zoom (G1/G2). Aim with the arrows/W/S, Enter → shot. Top red bar: "Head-shot!" /
   "Body-shot!" / "Leg-shot!" / "Missed!". Ammo counts down after each shot animation.
4. After 8 shots the next Enter shows "Out of Ammo!", which the result replaces at once:
   "Practice Score: N|+1 to Accuracy" (N > 120) or "…|No Accuracy Bonus". You are back at the
   terminal. Original weapons and ammo are restored (the rifle is gone unless you owned it).
5. Re-enter. Esc while zoomed only zooms out; Esc again exits with the score message. Tab
   does nothing in the range. Arrows when not zoomed turn 90°; W cannot leave tile (16,29)
   (map EVT 165 ABORT_MOVE).

---

## G5 — Target sprite layers

- `World3D`: public `void setPracticeTargetFrames(int legs, int body, int head, int board)`
  storing `int practiceFrames_[4] = { 5, 6, 7, 4 };`.
- `World3D::drawSprite`: after `lo`/`hi` are resolved (`World3D.cpp:675-677`) and before
  `int mediaId = lo + frame;`:
  ```cpp
  // TILENUM_PRACTICE_TARGET (src/Render.cpp:1672-1713): four billboards of tile 149 at the
  // sprite position in order legs, body, head, board, flags & ~0x20000; frames chosen by
  // TargetPractice::layerFrames (-1 = layer skipped this frame).
  if (tileNum == Enums::TILENUM_PRACTICE_TARGET) {
      const int flags = info & ~0x20000;
      for (int f : practiceFrames_) {
          if (f < 0 || lo + f >= hi) continue;
          const int mid = lo + f;
          if (!spriteTexByMedia_.count(mid)) ensureSpriteTexture(media, tileNum, mid);
          if (!spriteTexByMedia_.count(mid)) continue;
          drawBillboardPart(map, media, x, y, z, tileNum, mid, flags, scaleFactor);
      }
      return;
  }
  ```
  If the sprite carries `ORIENTED|FLAT` bits (not expected for a decor billboard), log once
  `[dbg] practice target has wall flags`. Do not add a wall path.
- `GameContext::render`, before `scene_.drawWorld`: `int f[4];
  practice_.layerFrames(upTimeMs, sys_.game->zoom.isZoomedIn, f);
  sys_.world->setPracticeTargetFrames(f[0], f[1], f[2], f[3]);`. This runs every frame,
  also outside target practice, because the branch is unconditional in legacy.

**G5 acceptance:** outside the range, look at the target from tile (16,29) or nearby. It shows
the idle composite: a silhouette (frames 5/6/7) on the board (frame 4). It is no longer a
single raw frame. Zoomed, the board changes to frame 1. On a scored shot the hit part flashes
in its "hit" frame for 0.5 s, then returns to idle.

---

## G6 — Score bar + soft key

- `Hud` public: `void drawTargetPracticeScore(Graphics2D& g, const Font& font, const Text&
  text);`, which returns early when the front message has `kMsgFlagImportant`
  (`src/MiniGameManager.cpp:224-226`), else calls `drawImportantMessage(g, font, text,
  0xFF7F0000)` (`:232`, `src/Hud.cpp:396-410`).
- `GameContext`: `Text practiceScoreText_;`. In `render`, after `drawHud` and before
  `drawMessages`, `if (practice_.active())`: compose `loc 0/229 + std::to_string(score)`
  (no space, `:229-231`) and draw. Not state-gated: legacy draws it whenever
  `inTargetPractice` (`src/Canvas.cpp:419-421`).
- `buildHudModel` soft keys (`GameContext.cpp:853-862`): `if (practice_.active())`, the left
  label is common 30 "Exit" and `softRight = nullptr`, with no right hit rect
  (`src/SoftKeyController.cpp:81-83`). The zoomed variant (52/55) equals the default, so
  nothing changes there. The left soft-key press already queues the Menu action, so it
  exits or zooms out through G4.5 / G1.

**G6 acceptance:** in the range a red bar at the top reads "Score:0", then "Score:30" etc.
"Head-shot!" replaces it briefly and it comes back. The bottom-left soft key reads "Exit"
and clicking it exits (or only zooms out while zoomed). The right soft key has no label.

---

## G7 — Monster scope crit (normal play)

- `Combat::Env` += `const MediaMappings* media = nullptr;`, wired in `Main.cpp` from
  `g_media.mappings()`.
- `CombatEntity::calcHit` (`CombatEntity.cpp:110-114`), replacing the log, port of
  `src/CombatEntity.cpp:173-215`:
  ```cpp
  if (Combat::checkWeaponMask(attackerWeaponId, 0x200)) {
      if (eType == Enums::ET_MONSTER && c.env().media != nullptr) {
          const MapData& m = *c.env().map;  const ZoomState& z = c.env().game->zoom;
          const int s = curTarget->getSprite(), n = m.numSprites;
          const int tile = curTarget->def->tileIndex;
          const bool rev = tile >= 35 && tile <= 37;   // isRevenant, src/Render.cpp:2987-2989; TILENUM_MONSTER_REVENANT..3 src/Enums.h:683-685
          const int zone = shotZone(*c.env().media, tile,
              z.zoomCollisionX - m.mapSprites[s], z.zoomCollisionY - m.mapSprites[n + s],
              z.zoomCollisionZ - m.spriteZ(s), rev ? -1 : 0, rev ? 60 : 0);
          const int anim = (m.mapSpriteInfo[s] >> 8) & 0xF0;
          if (zone != -1) {
              if (zone == 0 || anim == 16) c.crFlags |= 0x2;      // :203-205
              else if (zone == 1) c.crFlags |= 0x1;               // :206-208
              else c.crFlags |= 0x5;                              // :209-211
          }
      }
      return c.crFlags;                                           // :214 (every eType)
  }
  ```
  Add the revenant tile constants to `new_src/domain/game/Enums.h` instead of literals if the
  coder prefers (35/36/37, `src/Enums.h:683-685`). Update the header comment at
  `CombatEntity.cpp:97-99`.

**G7 acceptance:** with the rifle, zoom at an imp. A head-height shot gives a critical
(the crit message/damage the normal crit path shows). A body shot hits without a crit. A leg
shot hits with the 0x5 flags. Log the zone once per shot while testing.

## 3. Verification summary for the orchestrator

Each group: `cmake --build build_new -j 8` clean (re-configure after G1/G3/G4 for new files).
The screen checks are listed per group. Ask the user for G1/G2 (zoom feel, scope art), G4 (the
full session flow) and G5/G6 (target frames, score bar). Open fact the G3 log will answer:
the tile-149 zone boxes (research "Open questions").
