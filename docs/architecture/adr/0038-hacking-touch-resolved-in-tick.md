# ADR 0038 — Hacking column taps are resolved in the tick (release pass in `handleTouch`, drag-leave as pseudo button ids), not in draw

Date: 2026-10-30
Status: accepted
Spec: `../hacking-minigame.md`

## Context

The original resolves hacking column taps and the confirm screen's Yes/No boxes inside
`drawGameScreen` (`src/HackingGame.cpp:382-400`, `:460-503`), using the raw press samples
`currentPress`/`oldPress` that `touchStart/Move/End` record (`:638-688`). `touchEnd` runs first
(the event), then the next draw does the "released in a column" work. The draw-time path can
call `handleInput(FIRE)`, which on the won/lost screen calls `endGame` (script resume) from
inside the renderer.

In the rewrite (ADR 0033 point 4) a touch release becomes `UiAction::MiniGameTouch` returned by
draw and handled in the NEXT tick. Frames may render without a tick in between
(`new_src/core/GameLoop.cpp:116-120`). Resolving the column pass in draw would therefore
(a) run it BEFORE the `touchEnd` logic (order inversion: a winning column tap followed in the same
release by the won-screen "any release = FIRE" would end the game at once), and (b) call
`MiniGameHost::finish` → `ScriptVM::resumeThread` during render.

## Decision

1. `HackingGame` keeps its own `cur_`/`old_`/`touched_` samples, updated in `draw` from `ui.in()`
   with the legacy `touchStart/Move/End` rules.
2. The legacy "first draw after `touchEnd`" work (column pass on `old_`, then confirm Yes/No on
   `cur_`) runs in `handleTouch(buttonId)` right after the ported `touchEnd` switch. This is the
   legacy order, executed in the tick.
3. The only draw-time column event left, "drag leaves a column while still pressed", is returned
   from `draw` as `UiResult{MiniGameTouch, kColumnTapBase + i}` (`kColumnTapBase = 100`), the
   same pseudo-button idiom as vending's `kTerminalExit`. `draw` zeroes `old_` when it emits, as
   the legacy does, so the event is emitted once.
4. `draw` itself never changes rules state. It only tracks touch samples, the hover column and
   its click sound, and the button highlight sound.

## Consequences

- Every rule change and every `endGame` happens inside `GameContext::tick`, like all other
  minigame input.
- Faithful quirks kept: drag-leave fires a FIRE on the left column. A column tap on the confirm
  screen acts as FIRE on the confirm choice. A drag-leave on the won/lost screen ends the game.
- Dropped (documented in the spec, §8): (a) the release pass after a release in the letterbox
  bars (no `MiniGameTouch` is produced there); (b) the stale-`current` confirm hit, where a
  previous release that happened to lie inside the Yes/No box on the game screen fires on the
  first confirm-screen draw after a keyboard MENU. Both need a release position the rewrite does
  not report, and neither is reachable in normal play.

## Rejected alternatives

- **Resolve in draw verbatim:** this inverts the order against `touchEnd` and resumes the script
  from the render pass (see Context).
- **Call `handleTouch` synchronously from draw:** this keeps the order but still runs
  `endGame` → `resumeThread` from the renderer.
- **Delay the draw-time pass by one frame:** this is only correct if a tick runs between the
  two frames, which `GameLoop` does not guarantee.
