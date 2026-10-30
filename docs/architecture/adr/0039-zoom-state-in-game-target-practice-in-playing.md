# ADR 0039 — Sniper zoom data on `Game::zoom`, behaviour in `core/ZoomController`; target practice is an in-playing session, not a minigame

Date: 2026-10-30
Status: accepted
Spec: `../target-practice.md`

## Context

The original keeps the zoom in 15 `Canvas` fields (`src/Canvas.h:330-344`) driven by
`ZoomController` (`src/ZoomController.cpp`). Many places read them:
- the fire trace and the zoom-collision point (`src/PlayingInputHandler.cpp:221`, `:518-522`)
- the render pose (`src/MovementController.cpp:525-541`)
- the HUD (`src/Hud.cpp:730-767`)
- the monster scope crit (`src/CombatEntity.cpp:173-215`)
- target-practice scoring (`src/Player.cpp:2557-2559`)
- the state machine (`src/Canvas.cpp:1085-1124`)

The rewrite has no `Canvas` god object. Domain readers (`Targeting`, `CombatEntity`) must not
depend on `core/`, and the zoom behaviour needs `core` services: the
`PlayerActions::finishRotationFired` hook, `StateHost`, fades and messages.

Target practice (`EV_START_TARGETPRACTICE`, TP doc §1-§4) parks the script thread like a
minigame, but it never leaves `ST_PLAYING`. It is played with the normal movement, fire and
zoom input, the normal HUD and the normal combat sequence. `MiniGameHost` (ADR 0033) owns
`ST_MINI_GAME`, replaces the playing input and draws a full-screen view. None of that fits.

## Decision

1. **Zoom data** is a plain struct `ZoomState` on `Game::zoom` (`domain/game/ZoomState.h`).
   Domain code reads it and writes only `zoomCollision*`.
2. **Zoom behaviour** is `core/ZoomController`, owned by `GameContext`: initZoom, zoomOut,
   forceRemove, the state-entry drop, `handle(Action)` (port of `handleZoomEvents`) and
   `renderPose(ZoomPose&)`. `PlayerActions` routes ST_PLAYING input to it first while zoomed.
   `SceneRenderer::drawWorld` takes an optional `const ZoomPose*`, the same way it takes the
   cinematic `MayaPose*`.
3. **Target practice** is `core/TargetPractice`, a session owned by `GameContext` like
   `LootSession`. It is not a `MiniGame`. It ports `enter/assessShot/exit` and keeps the
   canvas-side fields (`save*`, `*ShotTime`, `targetPracticeThread`). Its hooks into the playing
   input, the HUD, the soft keys and World3D are small `if (active())` branches at the
   legacy sites.
4. The frame-bounds zone test is one free function `shotZone` (`domain/game/ShotZones`), shared by
   scoring and the monster crit.
5. `forceRemoveFromScopeZoom` is not called from `Player::restoreInventory`, because `Player`
   stays free of core dependencies. The two callers (`TargetPractice::exit`,
   `EV_INVENTORY_OP 2`) call `ZoomController::forceRemove()` right before it. The order is
   equivalent, see spec G4.2.

## Consequences

- One owner per concern. Domain readers see the zoom without a core include. The render path
  gains only a nullable pose parameter.
- `ZoomController` and `PlayerActions` point at each other (FIRE is delegated back, and zoomOut
  needs the rotation hook). Both only store pointers set in `GameContext::init`.
- `helpBitmask` lands on `Player` but is not persisted (config writes zeros).

## Rejected alternatives

- **Zoom fields on `Player`**: the zoom is camera state (Canvas), not player stats. Save code
  would then have to skip them explicitly.
- **Target practice as a `MiniGame`**: wrong state (`ST_MINI_GAME`), and it would duplicate
  movement, fire, zoom and HUD. The original never leaves `ST_PLAYING`.
- **Logic inside `Player`** (legacy placement): `Player` would need `Game`, `ScriptVM`,
  `DialogSystem`, the map, media and the state host. That reverses the domain → core boundary
  of ADR 0010.
