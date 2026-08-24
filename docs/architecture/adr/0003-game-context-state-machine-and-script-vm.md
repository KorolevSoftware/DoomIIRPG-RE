# ADR 0003 — GameContext state machine + standalone ScriptVM

Date: 2026-08-23
Status: Accepted
Related: `docs/architecture/specs/2026-08-23-phase5-skeleton.md`,
`docs/original-code/game-flow.md`, `docs/original-code/tile-events-vm.md`

## Context

Phase 5 must replace the ad-hoc `main()` loop (`new_src/core/Main.cpp:252-480`)
with a faithful game-state machine and add the tileEvents interpreter. Two
structural questions had no prior answer:

1. Who owns the state machine? The dead `core/GameLoop` skeleton exists and an
   unwired `AppContext::run() → GameLoop::run()` path is already in place
   (`AppContext.cpp:61-63`), so "revive GameLoop" was the obvious candidate —
   but a loop driver and a state machine are different responsibilities
   (legacy splits them too: `Main.cpp` paces frames; `Canvas::setState` owns
   states).
2. Where does the 20-thread script pool live? Legacy embeds it in `Game`
   (`src/Game.h:109`) whose methods (`executeTile`, `executeStaticFunc`,
   `runScriptThreads`, `isInputBlockedByScript`) are the call surface used by
   movement/combat code.

## Decision

1. **New small `core/GameContext` owns the state machine** (states, clocks,
   per-state ticks, playing-action handlers, render orchestration) as the
   rewrite's *Canvas* analog; **GameLoop stays a dumb fixed-step driver**
   (accumulate clamped dt ≤ 125 ms, consume 15 ms quanta, call
   `ctx.tick()` / `ctx.render()`). The existing `AppContext::run()` path is
   kept as the entry point. Rationale: `setState` semantics (stateVars reset,
   old/new hooks, `stateChanged` latch) and clock ownership are Canvas
   concerns in legacy; keeping them out of GameLoop lets the loop stay
   testable and makes future states pure additions to one class.
   Subsystem references stay non-owning pointers created in `main()`.
2. **ScriptVM is a standalone class in `domain/game/`**, wired through an env
   struct of raw pointers (`MapData*, EntityDefs*, Game*, Player*,
   Localization*, Hud*, GameContext*, const int64_t* gameTime`). `Game` gets
   only thin conveniences plus a forward-declared `ScriptVM* vm_` back-pointer
   for `advanceTurn → executeStaticFunc(6)`. Rationale: embedding the pool in
   `Game` (legacy style) would force `Game`'s constructor to take five more
   dependencies it otherwise never touches, and the VM needs upward calls into
   UI/context objects that domain-`Game` deliberately does not know about;
   header-level dependency cycles are avoided by forward declarations.
   Call sites still read like legacy because `executeTile`-shaped calls go
   through the context/handlers.
3. **Fixed 15 ms timestep with ≤125 ms clamp** replaces the legacy
   variable-rate ~66 fps ceiling (`src/Main.cpp:73-142`). Deterministic tick
   count beats frame-rate coupling for turn logic and script timing; visual
   pacing is preserved by rendering once per frame regardless of quanta.
4. **Lenient opcode policy**: unimplemented opcodes log to stderr and kill the
   offending thread instead of the legacy fatal `Error(...)`; EV_DIALOG /
   blocking lerps do NOT pause threads while their resume partners (dialog
   system, lerp collection) are absent — they log and continue. A stuck or
   crashed bring-up build teaches nothing; stderr traces everything needed to
   finish those systems later.

## Consequences

* Positive: one mutator for state transitions mirrors legacy exactly
  (`src/Canvas.cpp:1020-1217`); Loading's ordered pipeline has explicit slots
  for future media streaming; scripts run synchronously inside move-finish /
  use handling exactly like the original, including the blocking-door-open
  external-resume protocol via `DoorAnim::ownerThread`.
* Accepted deviations (all detailed in the spec): fixed timestep; `gameTime`
  frozen outside Playing (legacy freezes only menus); direct-give EV_GIVEITEM
  without drop-item/toast; message routing straight to Hud demo APIs instead
  of the hud message-id queue; thread-pool exhaustion logs instead of fatal
  error; entity `name` bookkeeping omitted from `setLineLocked`.
* Future phases: adding Menu/Dialog/Combat means new `StateId` values +
  enter/exit hook implementations inside GameContext — no loop changes;
  dialog/camera resume points replace today's "log and continue" stubs at the
  exact TIER-B sites listed in the spec.

## Rejected alternatives

* **Revive GameLoop as loop + state machine + context** — rejected: conflates
  pacing with simulation policy; every future state edit would touch the
  frame-timing code; no legacy analog to validate against.
* **Embed script pool in `Game` (verbatim legacy shape)** — rejected: forces a
  wide constructor dependency fan-out on a class that is currently trivially
  constructible, and creates a Game↔UI include cycle; the vm_ back-pointer
  preserves the two legacy call sites (`advanceTurn`, door completion) without
  the cycle.
* **Own event-loop thread for scripts** — rejected outright: legacy executes
  threads synchronously inside caller contexts with per-frame resumption;
  any real threading would break the external-resume protocol
  (`unpauseTime == -1` + direct `run()`).
