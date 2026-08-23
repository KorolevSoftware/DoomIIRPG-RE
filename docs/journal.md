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

## 2026-08-23 — Faithful player collision (walls solid)

- Audit of the user report "player walks through walls" found the wall test
  effectively dead: `CapsuleToLineTrace` had sign-flipped closest-point
  parameters plus int32 overflow in the same expressions; only the
  tile-granular door check ever blocked. Explore verification: the old math
  detected 0 of 117 head-on wall crossings.
- Researcher documented the complete legacy collision path
  (`docs/original-code/player-collision.md`): swept capsule radius 16,
  mask CONTENTS_PLAYERSOLID = 13501, oriented entities as ±32 segments
  through their current animated sprite position, other entities circles
  r=25 combined d² < r²+R² (=881 walking), world-line flag rules
  (0–3 block / 4,6 never / 5 mask-gated / 7 one-sided), strictly-2D with
  unconditional destZ = 36 + getHeight.
- Architect spec + ADR-0002; notable catch: the legacy parallel-case
  "projection" branch is dead code (denom ≥ 0 always) — live behavior is
  s=t=0 via vanishing numerators and zero-guarded quotients.
- Coder implemented `Game::traceMove` replacing `canPlayerStep`: bit-faithful
  port of the src/Render.cpp:1128-1210 solve, exact world-line flag rules,
  generic masked entityDb trace (doors now block as their animated ±32 panel
  segments). Reviewer PASS (line-by-line against legacy).
- User verified: walls solid from both sides, corners refuse without sliding,
  long-wall hugging smooth, closed doors block / open pass, animated door
  panels block correctly. Flag-4 lines are now intentionally walkable
  (behavior change documented in PLAN.md and the spec).

