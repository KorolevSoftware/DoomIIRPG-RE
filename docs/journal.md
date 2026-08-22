# Work journal

Append-only. One entry per completed unit of work. Newest at the bottom.

## 2026-08-22 — Multi-agent setup

- Configured opencode team: `orchestrator` (primary), subagents `researcher`, `architect`, `coder`, `reviewer`; commands `/research`, `/arch`, `/implement`.
- Created documentation skeleton under `docs/`; rules live in root `AGENTS.md`.
- Ran initial audit of `new_src/` state via two parallel explore agents:
  - Module/wiring inventory → `docs/architecture/README.md`.
  - Feature completeness vs PLAN.md → `docs/status.md`.
  - Key findings: phases 1–3 nearly complete (BSP culling skipped), phase 4 has doors +
    simplified collision/movement ahead of plan, monsters/combat/pickup/phase-5 missing,
    GameLoop dead code, HUD demo-driven.

## 2026-08-23 — Door & sprite-placement fidelity pass

- User reported two bugs: doors behave incorrectly; sprites look slightly shifted.
- Researcher pinned original behavior with citations:
  `docs/original-code/doors.md` (creation, use trigger, lerp state machine,
  renderer doorLerp block incl. slip-door split, auto-close, solidity timeline)
  and `docs/original-code/sprite-placement.md` (billboard/wall/flat placement
  math, camera pull-back nudge, sort-key biases); raw dated reports in
  `docs/research/2026-08-23-doors.md` / `-sprite-placement.md`.
- Architect produced `docs/architecture/specs/2026-08-23-fix-doors-sprite-placement.md`
  (+ ADR-0001 for the trace-free faced-door trigger simplification).
- Coder implemented Scope A (A1–A8): red/blue slip-door vertical split
  (two half-quads, growing gap, v-window recenter), slide-door UV pinning
  (full-delta u-shift), DOORLERP bit lifetime through close, legacy solidity
  timeline (solid during open, instant-solid on close start), faced-door use
  trigger (Chebyshev ≤ 1 tile), current-scale resume, open-frame texture +
  whole media-range preload, tile-granular auto-close occupancy.
  Scope B (B1–B5): render-camera pull-back nudge, portal-eye z range 155–156,
  GL-path billboard UV flips, FLAT plane branch, height-snapped sort keys with
  legacy bias chain. Build green.
- Reviewer verdict: PASS (minor non-blocking notes: stderr debug sweep pending;
  AUTO_ANIMATE frame cycling pre-dates this task).
- Pending: user visual verification per spec §5 checklist; PLAN.md full sync
  still queued.

