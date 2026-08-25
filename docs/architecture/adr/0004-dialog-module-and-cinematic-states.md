# ADR-0004: Dialogs as a dedicated module; cinematics inside GameContext

Date: 2026-08-24
Status: ACCEPTED

## Context

Phase 5 shipped a "dialog-lite" (modal flag + gray HUD panel). The intro unit
needs faithful styled boxes (styles table per dialog-system.md §1), typewriter
paging, speaker titles, help FIFO, and dialogs that run DURING cinematics while
restoring the prior state on close (ST_DIALOG routing per src/DialogSystem.cpp).
It also needs a cinematic camera driven by MapData mayaCameras.

## Decision

1. Introduce `domain/game/DialogSystem.{h,cpp}` owning styles/text/paging/input;
   GameContext keeps only state routing (ST_DIALOG becomes a real state,
   replacing the modal flag; dialog-lite code is deleted, not dual-pathed).
   Rationale: legacy keeps DialogSystem self-contained the same way; drawing
   stays delegated to Hud/Graphics2D so GL state ownership does not move.
2. Cinematic machinery lives in `core/GameContext` (state transitions, setup/
   Snap handshake, fade overlay) with interpolation isolated in a new
   `core/MayaCamera` value class fed by the already-parsed `MapData::mayaCameras`.
   Rationale: matches legacy Canvas-state placement; keeps Camera3D untouched
   for gameplay rendering.
3. Loot this cycle = entity-flag + roll-to-inventory via existing give() paths;
   full ST_LOOTING UI deferred (intro needs none).

## Consequences

- ScriptVM DIALOG site migrates to DialogSystem API.
- Prior-state restore stack required (INTER_CAMERA > CAMERA > PLAYING).
- characterChoice treated as 0 until a selection screen exists.

## FPS lock addendum 2026-08-24

User testing of the intro cinematic showed periodic slow/fast waves at high
display refresh rates: simulation advances in fixed 15 ms quanta while render
evaluated the maya pose from gameTime between ticks (staircase at 140 fps).
Per user decision, the loop is paced to the legacy cadence instead of
decoupling now: `GameLoop::run` delays to the next 15 ms boundary measured
from the real frame start (~66 fps), giving one `ctx.tick()` + one render pass
per frame; input polling stays per-frame. This matches the legacy DoLoop
throttle, which runs one simulation+render pass only once 15 ms elapsed since
the previous frame started (src/Main.cpp:83-88), with the same 125 ms
catch-up clamp the rewrite keeps (src/Main.cpp:133-135). The maya pose is
therefore evaluated once per rendered frame at legacy cadence. Revisit
variable-dt decoupling later.
