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

## 2026-08-23 — Fix: media reference records (vanishing green doors)

- Round-1 verification of the fidelity pass: solidity / faced-door trigger /
  sprite placement all confirmed by the user; regression found — plain green
  doors (tile 276) vanish the moment they open and pop back at close end;
  level doors (278) unaffected.
- Root cause: the open-frame texture of tile 276 is mediaId 871 — a REFERENCE
  record in newMappings.bin (`0x80000000 | 869`). `MediaLoader::finalize`
  skipped reference entries, leaving `texelIndex_/paletteIndex_[871] = -1`;
  World3D's preload then never created the texture and `drawSprite`
  early-returned, hiding the sprite. Legacy `finalizeMapMedia`
  (src/LoadingManager.cpp) rewrites references to point at the loaded slot,
  so use-time masking always resolves. Evidence: media-table dumps under
  docs/research/assets/ (frame art exists, 275/276 frame 1 share record 869).
- Fix: single additive resolve pass at the end of `MediaLoader::finalize`
  aliasing reference entries to their source store indices
  (new_src/io/Media.cpp). Reviewer PASS; validated against shipped data
  (353 palette + 494 texel references, all resolvable, no ref→ref chains).
- User round-2 verification: green doors slide smoothly with correct
  animation, level doors unaffected, no sprite anomalies on the level.

