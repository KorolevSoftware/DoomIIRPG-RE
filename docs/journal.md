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

## 2026-08-23 — Phase 5 skeleton: state machine + script VM + dialogs-lite

- Research (parallel): `docs/original-code/game-flow.md` — Canvas state
  machine, boot chain, two-phase loading, exact ST_PLAYING tick order,
  advanceTurn contract; `docs/original-code/tile-events-vm.md` — strings.idx/
  chunk format, staticFuncs, 20-thread ScriptThread pool with -1 external
  resume, full opcode table, map00 traces (blue/red door unlock scripts,
  story-unlock event).
- Architect spec `docs/architecture/specs/2026-08-23-phase5-skeleton.md` +
  ADR-0003 (GameContext owns states {3,7,13}; GameLoop stays a dumb fixed-step
  driver; lenient bring-up policy). Coder implemented in groups; reviewer
  PASS after fixes (kTextMap loading, unsigned EV_EVAL operand reads,
  duplicate loadEntities removed).
- User-reported freeze diagnosed with targeted [dbg] instrumentation:
  angle-wrap turn lockup — rewrite masked angles in startRotation while
  accumulating raw; legacy keeps both RAW forever and masks at use
  (src/MovementController.cpp:455-466). Ported verbatim → fixed.
- Second user-reported bug: blue door vanished instantly on scripted open.
  Chain of causes fixed: (1) media REFERENCE records skipped by finalize
  (commit 61ee1d8); (2) preload only covered load-time tileNum — scripted
  setLineLocked flips 273→274 so draw resolved never-uploaded mediaIds;
  fixed with lazy create-and-cache `World3D::ensureSpriteTexture`
  (≡ legacy setupTexture caching); (3) font space glyph garbage (missing
  legacy clamp src/Graphics.cpp:648-652) and no word wrap + broken
  dehyphenate eating chars after hyphens ("Need_Blue_Keycr") — all ported
  faithfully; dialog-lite gray panel replaces red banner.
- EV_ENTITY_FRAME (17) added (INIT_MAP thread was dying at it); remaining
  known skip: EV_MAKE_CORPSE (72) until loot lands.
- All debug instrumentation stripped; build green; user verified: smooth
  split animation on scripted blue-door open, auto-close, turns clean.


## 2026-08-24 — Font clamp + dialog word wrap (FIX A/FIX B)

- FIX A: `Font::drawChar` now ports the legacy out-of-range guard exactly
  (`new_src/text/Font.cpp:85`, src/Graphics.cpp:641-652): `index1` compares
  unsigned so `getCharIndices` negatives (space 0x20 -> -1) fall back to the
  '?' cell 30 instead of sampling outside the 192x144 sheet.
- FIX B: `Hud::drawDialogMessage` word-wraps before the '|' split with
  budget `(480 - 8) / 9 = 52` chars (`kDialogWrapChars`), mirroring legacy
  prepareDialog order (src/DialogSystem.cpp:679-681, src/Canvas.cpp:89).
- Required fix outside the task's file list (flagged to orchestrator):
  `Text::dehyphenate` kept its first hit across deletions and ate following
  characters — reproduced the user-reported corruption "Need Blue Keycr"
  verbatim. Now re-searches each iteration like legacy (src/Text.cpp:718-725).
  Verified 14/14 wrap/dehyphenate cases byte-identical against a verbatim
  legacy transcription (incl. soft-hyphen breaks "Need Blue Key-"/"card").
- Note: game data stores soft hyphens ("Ac-cess", "Key-card"); legacy
  displays them only at line breaks, otherwise consumes them
  ("Need Blue Keycard"). Build green; boot reaches ST_PLAYING (~60+ FPS).

## 2026-08-24 — DialogSystem full RE (research only, src/ untouched)

- Curated: docs/original-code/dialog-system.md (styles 1-16 w/ exact fills,
  geometry 480x320, text pipeline incl. strings.idx format + composeText
  escapes + wrap budgets 53/52 chars, typewriter 25ms/char, full dialog
  keymap, EV_DIALOG park/unpauseTime=-1/run() resume + skipDialog gate, help
  FIFO 16, queueAdvanceTurn rules, port checklist).
- Raw: docs/research/2026-08-24-dialog-system.md.
- map00 intro replayed from tmp_map00.bin + ipa strings04: INIT_MAP@0 →
  func@1913 → cameras 0/1/5/7 (subtitles str9-19) → evt[54] tile(9,19) TRIGGER
  ip=2993: str21 s1 → str22 s1/str23 s8 → str24 s8 → str25 s1 + GIVEITEM pistol
  def1 (+difficulty ammo) → str26 s8/s1 + bubble "Let's go!" + blue-door
  UNLOCK/OPEN. Speaker names = first '|'-line of buffer for styles 2/16/9;
  style 8 = green hero + portrait; style 2 = gray-header tutorial box;
  style 9 = green-on-black terminal. Quirk documented: characterChoice==3
  skips the str25/pistol branch verbatim in data.

2026-08-24 — G1 DialogSystem v2 (spec 2026-08-24-intro-sequence GROUP 1)
- new_src/domain/game/DialogSystem.{h,cpp}: full dialog port (style table with
  verbatim fills, title-bar layouts 2/16/9, style 8 gradient + Hud_Portrait_
  Small.bmp row 0, tails for 1/5/10/14, composeText %NN 50-slot pool, wrap
  53->52 retry (style8 -3 inset), viewLines {3:4,2/8:3}, typewriter 25ms/char,
  §4 keymap, help FIFO 16, closeDialog restore+resume, queueAdvanceTurn rule,
  scrollbar + page icons).
- GameContext: real ST_DIALOG(8) state replaces the modal flag; per-event
  routing to DialogSystem::handleInput; door lerps keep ticking under the box;
  dialog-lite (enterScriptDialog/dismissDialogStep/Hud gray panel) deleted.
- ScriptVM EV_DIALOG: B,B -> style=lo/flags=hi nibbles, style 2 = help enqueue
  by pool index, park unpauseTime=-1, skipDialog gate (Game::skipDialog added).
- InputSystem/GameLoop keymap: TAB=Passturn, M=Automap, Enter=Menu,
  Backspace=BackKey(swallowed); E=FIRE unchanged.
- Build green; smoke boot reaches [load] -> ST_PLAYING. Deviations: choice
  widget RENDERING deferred (spec §8), ESC quits app, typewriter enabled
  despite src/DialogSystem.cpp:135 reveal-all artifact (doc §2.4 normative).

## 2026-08-24 — map00 boot fly-over invisible (fixed)
- Symptom: boot cinematic ran by log (cam=0, ADV_CAMERAKEY chain) but view
  stayed on the player.
- Bytecode decode (tmp_map00.bin, read-only): boot intro is STARTCINEMATIC
  cam=0 at IP=1946 inside one-time-init func@1942 (staticFunc[0] var17 guard
  @221 -> CALL 1913 -> CALL 1942; CALL 1260 = SETSTATE var5=1). The doc §6
  cam=4 @1889 branch is the var15!=1 reload-path alternative, NOT boot.
  cam0 keys: no -2 sentinels, real 12-key ~15s descent (1124,98,948) ->
  (209,448,682), sampleRate 125.
- Root cause: STARTCINEMATIC fires during Loading; tickLoading's tail
  setState(Playing) overwrites ST_CAMERA, and render gated the maya pose on
  `state==Camera`. Legacy renders/updates the active camera whenever
  isCameraActive() regardless of PLAYING/CAMERA (src/MovementController.cpp:
  391-392, :518-520).
- Fix: GameContext::render camera branch now keyed on activeCameraKey_>=0;
  added throttled [dbg] in that branch + tickCamera + startCinematic state
  log + STARTCINEMATIC IP log.
- Build green; smoke boot logs cam-render state=3 with moving pose through
  keys 1..10, then cam=1 handoff. User to verify visually.

## 2026-08-25 — chained ADV_CAMERAKEY ate every 2nd key (fixed)
- Legacy answer: EV_ADV_CAMERAKEY parks AFTER NextKey (src/ScriptThread.cpp:
  690-702; IP already past the opcode, resume never re-executes it).
  MayaCamera::Update on boundary does NOT advance: elapsed>=ms ->
  resumeCount>0 ? Snap : hold (src/MayaCamera.cpp:72-77). Snap (:335-374)
  snaps pose to next key WITHOUT charging duration or advancing, decrements
  the count; at 0 it runs the thread and RETURNS (no advance) — the
  resumed script's own ADV_CAMERAKEY/NextKey starts the next key fresh
  (src/MayaCamera.cpp:36-44); while still counting it auto-advances once.
- Rewrite bug: the tick loop charged NextKey at each boundary (startTime +=
  dur; ++key) AND the resumed script charged another -> keys eaten (played
  8775ms vs authored 14399ms).
- Fix: GameContext::nextKey() split out; boundary branch now snap+resume OR
  auto-advance (never both); pose holds when elapsed>=keyMs (no update);
  render no longer recomputes pose; finish/skip use maya_.snap().
- Measured (temp [camtmp], removed after): cam1 authored 8099 = key0 1499
  truncated + 6600 played vs 6645 measured (+45ms tick quantization);
  cam0 16995ms = 13400 keys + legacy-faithful WAIT holds (2000+1500) +
  quantization. All temp logs ([camkey]/[camera] START/END/[dbg]) removed;
  build green; pre-existing unrelated: UNIMPLEMENTED opcode 92 @IP=2336.
