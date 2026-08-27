# ADR 0010 — Decompose `GameContext` and `Game` into peer modules with narrow `Env` injection

_Date: 2026-08-26. Status: accepted. Spec:
[specs/2026-08-26-decomposition.md](../specs/2026-08-26-decomposition.md)._

## Context

`new_src/core/GameContext.cpp` reached 1613 lines — the legacy god-object
`src/Canvas.cpp` is 1625. `new_src/domain/game/Game.cpp` is 1565. We reproduced
the shape we set out to escape.

The cost is measured, not aesthetic:

- Of six implementation groups in the 2026-08-26 combat cycle, four had to edit
  `GameContext.cpp`, so coders had to be serialized (groups 1→3, 2→4).
- Both blocking items of that cycle's second review were the same class of bug:
  three different notions of "a cinematic owns the view" living in one file
  (fov keyed off `cameraActive()`, the cockpit overlay off
  `state == StateId::Camera`, the view weapon off a state list). A local
  `const bool cine = cameraActive();` patched the symptom.

`GameContext` owned, in one file: the state machine, five per-state ticks, input
action handling, the loot session and its UI, the view-weapon billboard math, the
cinematic camera clock plus script-thread parking, fire-target election, the
facing probe, a debug cheat and the whole render orchestration.

The anti-pattern we must not re-introduce is the legacy
`CAppContainer::getInstance()->app->…` reach-through — and its equally bad
cousin, a `GameContext*` back-pointer handed to every new module (which grants
exactly the same universal access, one arrow later).

## Decision

1. **Vertical slices become peer modules**, each in its own file pair:
   `core/CinematicCamera`, `core/LootSession`, `core/PlayerActions`,
   `domain/game/Targeting`, `render/SceneRenderer`, `ui/ViewWeapon`; and on the
   `Game` side `domain/game/{TraceSystem, DoorSystem, MonsterSystem,
   SpriteLerps, CorpseLoot, EntityDb}`.
2. **`GameContext` stays the state machine and the frame coordinator only**:
   clocks, `setState` + enter/exit hooks, the input dispatch loop, the per-state
   tick switch, and a `render()` that composes the frame. Target ≤ 400 lines.
3. **`Game` becomes a container of peer subsystems**, extending the precedent set
   by ADR 0008 (`Game::combat`): callers write `game->doors.performDoorEvent(…)`,
   `game->trace.trace(…)`. `Game` keeps only cross-cutting turn logic
   (`loadEntities`, `advanceTurn`, `update`, event flags).
4. **Dependency injection by narrow `Env` struct.** Every module declares
   `struct Env { … }` of non-owning pointers to *exactly the subsystems it uses*,
   wired once through `init(const Env&)` — the pattern `Combat`, `ScriptVM` and
   `DialogSystem` already use. Clocks are injected as `const int64_t*`
   (`Combat::Env::gameTime` precedent) so `GameContext` remains the sole owner of
   `upTimeMs`/`gameTime`. Domain subsystems take `EntityDb*`/`MapData*`, never
   `Game*`.
5. **The single permitted abstraction is `StateHost`** (`core/GameStates.h`):
   `state()` + `requestState(StateId)`, implemented only by `GameContext`. A
   module that must change state synchronously (the cinematic Snap tail changes
   state *before* resuming parked threads) uses those two methods and nothing
   else.
6. **The `cinePose` invariant.** `SceneRenderer::drawWorld` takes
   `const MayaPose*`; `nullptr` means "no cinematic owns the view". The frame
   composer computes it once (`cinematic_.renderPose()`) and every consumer —
   projection fov, cockpit overlay, view-weapon suppression — reads that one
   local. The gates that drifted are no longer expressible as three independent
   predicates.
7. **Refactor discipline:** verbatim moves, mechanical access-path renames only,
   no behaviour change, one module per implementation group, `git diff` proving a
   near-1:1 line transfer.

## Consequences

- Groups that edit `GameContext.{h,cpp}` remain sequential — that is inherent to
  extraction. Parallelism is recovered *across* the two chains: `Game`-side
  groups keep temporary one-line forwarders in `Game.h`, so they never touch the
  Phase-1 files, and the forwarders are swept at the end.
- Future features touch one small file: a monster-AI group edits
  `MonsterSystem`, a HUD group `ViewWeapon`/`Hud`, a cutscene group
  `CinematicCamera` — the four-groups-one-file serialization does not recur.
- More files (13 new pairs) and one more indirection at wiring time in
  `Main.cpp`. Accepted: wiring is explicit and greppable, which is the point.
- `render/SceneRenderer.h` includes `core/MayaCamera.h` for the plain-value
  `MayaPose`. No cycle today (that header pulls only
  `domain/world/MapData.h`); if one appears, `MayaPose` moves to
  `domain/world/`.
- Each module's `Env` documents its true dependency set, which makes an
  over-connected module visible in review instead of invisible inside a
  1600-line file.

## Rejected alternatives

- **`GameContext*` back-pointer in every module.** Identical universal access to
  the legacy singleton, just spelled differently; it also makes every module
  depend on the state machine's full header. Rejected.
- **A service-locator / `Systems&` god-struct passed everywhere.** Same problem:
  no module would declare what it actually uses.
- **Deferred state changes (a `requestState` mailbox applied at end of tick).**
  Would reorder script-thread resumes relative to state transitions — a
  behaviour change in a refactor whose acceptance criterion is "the user sees
  exactly the same game". Rejected; hence the `StateHost` interface.
- **Free functions taking `(GameContext&, …)`.** Keeps the state private-by-file
  but leaves the god-object's data members intact and gives no ownership
  boundary.
- **Splitting only `GameContext` and leaving `Game` at 1565 lines.** `Game` is
  where the trace lives, and the typed `TraceHit` work (ADR 0011) needs a home;
  extracting `TraceSystem` first is what makes that change small.
- **One big-bang refactor commit.** Unreviewable against "no behaviour change",
  and one lost subagent run would waste all of it.
