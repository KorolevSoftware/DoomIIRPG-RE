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

## 2026-08-25 — loot dwell + menu UI spec (research)
- Traced the full ST_LOOTING interaction: after the 500 ms crouch the original
  HOLDS the pose (dwell), plays sound 1055 once, draws a top-screen loot list
  (red body 0xFF660000 {0,36,479,48} + black "Looted Items:" bar) and accepts
  FIRE(page×3/close)/PASSTURN/BACK(close+grant)/UP/DOWN/LEFT/RIGHT
  (src/LootingSystem.cpp:85-152). Grant runs at UI close BEFORE stand-up;
  advanceTurn at stand-up expiry (:73-103).
- Decoded strings from the .ipa: str90 "%01%02x %03|", str91 "%01%02|",
  str227 "Looted Items:", str228 "None found!", entity-table str157
  "UAC Credits" (tables.bin header is skipped — offsets relative to byte 80).
- Deliverables: docs/original-code/loot-inventory.md §2.6 (port-ready spec +
  new_src delta list), docs/research/2026-08-25-loot-ui-spec.md (raw log).
- Rewrite gap confirmed: new_src auto-grants at settle and toasts instead of
  listing (GameContext.cpp:469-485, Game.cpp:791-818); delta list in §2.6.

## 2026-08-25 — elevator end-scene visibility audit (research)

- Task: why "standing legs" persist near the lift after the cutscene fixes;
  what the original end scene contains.
- Read-only audit of src/Render.cpp gating + raw decode of map00 sprites/defs
  (tmp_map00.bin layout mirrors LoadingManager.cpp:463-552; TE@0xED07,
  bytecode@62019 reconfirmed) and tmp_entities.bin defs.
- Key findings: (1) hidden bit 0x10000 has one funnel, checked before entity
  lookup — no render path bypasses it (src/Render.cpp:830-837,1501-1504).
  (2) Squad sprites 185-188 + props 203/204 are SPRITE_FLAG_NOENTITY
  (0x200000, Enums.h:1237) → load skips entity creation (Game.cpp:398-400),
  so legacy EV_HIDE only sets the hidden bit for them. (3) Corpse prop 105 =
  tile 138 = single-frame media 738, lying art, lerped to (5,19). (4) Player
  spawns at tile (4,19) angle 0 post-intro (mapSpawnIndex=612,
  Game.cpp:953-968); NPC 19 walks out to ~(9,19) then BACK to (5,19) — a
  standing NPC beside the corpse is faithful original behavior.
- Verdict: rewrite cannot draw hidden sprites and ScriptVM has no killer op
  in IP 2799-2933 anymore; residual sightings = stale build or the legitimate
  standing NPC 19. Verification lines documented.
- Deliverables: docs/research/2026-08-25-unhandled-script-events.md §5;
  new docs/original-code/rendering.md (hidden-bit gates, corpse-prop tiles,
  NOENTITY binding); game-flow.md §3.3.1 (spawnPlayer spawn position).

## 2026-08-25 — interactive loot dwell + loot menu UI (spec 2026-08-25-loot-dwell-ui)

- Implemented both groups: Game::LootPool + poolLootCorpse/giveLootPool replace
  lootCorpse (toast deleted); GameContext gained kLootPhaseMs, lootSettleSfx_,
  lootPool_, handleLootingAction/closeLootSession/drawLootingMenu, the Looting
  input dispatch, pool-at-entry, pose-hold dwell with one-shot sound 1055 log,
  and the render() overlay hook; DialogSystem::drawScrollBar promoted to public.
- Legacy fidelity: class-6 dedupe quirk kept verbatim (tests source
  entity->lootSet[j], src/LoothingSystem.cpp:186-194); mark-before-read for all
  eType-9 entities on the tile; grant-on-close ordering (give -> lootCrouch_
  false -> lootTime_ restart); guard/draw predicates strict `>` like :87/:122.
- Notes: spec's LootPool::entries is kMaxCorpseLoot=3 while legacy lootPool[9]
  — multi-corpse tiles beyond 3 entries drop (bound-guarded); quirk loop skips
  j >= 3 instead of reading past lootSet[].
- Build green (--clean-first), zero new warnings; lootCorpse/pendingLootCorpse_
  grep-clean. User verification pending (sp29 keycard list, sp121 3-line list,
  flavor corpses, re-open prevention).

## 2026-08-25 — review PASS + minor fixes (loot dwell + elevator batch)

- Reviewer verdict: **PASS**, 0 blockers, 6 minors across the uncommitted diff
  (loot dwell/UI vs spec 2026-08-25-loot-dwell-ui; elevator VM fixes vs
  research 2026-08-25-unhandled-script-events.md §4/§5). Full line-by-line
  fidelity confirmed (pooling order, dwell predicate strict `>`, grant-on-close,
  MANIM_DEAD routing cannot hijack doors/props, hidden-bit skips precede).
- Fixed minors: NOENTITY sprite skip in loadEntities
  (Game.cpp:137 ≈ src/Game.cpp:398-400; squad imps 185-187 no longer get
  entities, so EV_HIDE takes the bare hidden-bit path like legacy) and the
  EV_HIDE else-if indentation. Research §5-Q4 stale cite updated
  (GameContext.cpp:970-980).
- Remaining documented deviations (accepted): lootPool cap 3 vs legacy int[9]
  (map00-inert), DAMAGEMONSTER non-monster skip logged (legacy died()
  unconditional), ENTITY_FRAME 0x7x-on-prop routing corner (dormant).
- Blue door passage: rewrite ALREADY honors tile-event self-disable
  (ScriptVM.cpp eventMatches :107 + EV_EVENTOP write :484) — replay hypothesis
  dead; awaiting user [dbg] moveBlocked log to name a blocker entity.
  Research: docs/research/2026-08-25-blue-door-passage.md (+ op=1 is
  UNLOCK+CLOSE correction; quiet LOCK on open door = lock-while-passable).

## 2026-08-26 — blue-door blocker root-caused: v14 (characterChoice) hardcoded 0

- Full annotated decode of EVT 617 (event[54], IP 2993-3332 + callees
  func@3333/3375/3401), cross-validated line-by-line against the canonical
  docs/research/assets/map00_disasm.txt. Walker table per choice:
  v14==1 → {9,10} onto doorway (10,19); ==2 → {7,9}; ==3/else → {7,10};
  HIDE guards test only ==3/==1/==2 (IPs 3276-3315) — an else-value hides
  NOTHING and the event still closes+locks sp22 on top of the walkers.
- Legacy var14 = player->characterChoice ∈ {1,2,3} guaranteed by the forced
  intro select (IntroSequenceManager.cpp:599/628/676; constants {1,3,2}
  :328-341) and refreshed into scriptStateVars[14] on every run()
  (Game.cpp:3468 via ScriptThread.cpp:230). Rewrite hardcodes vars[14]=0
  (ScriptVM.cpp:300) with no characterChoice anywhere → fall-through branch:
  spr7+spr10 lerp onto (10,19) (@3236/@3243), zero HIDEs, door closes over
  them → "[dbg] moveBlocked to 10,19 by spr=10 type=3". EV_HIDE/EV_EVAL/
  EV_GOTO verified faithful — not the cause.
- Acceptance target documented (marine default): 9+10 hidden mid-walk,
  spr7 stays at boot park (22,29), bob 19 →(21,21), props hidden, sp22
  closed+locked, 205/206 untouched at (1,19).
- Fix: set vars[14]=1 (marine per DialogSystem.cpp comment intent) in
  updateScriptVars. Research: docs/research/2026-08-26-choice-branch.md.

## 2026-08-25 — elevator shaft glass identified (spr155, tile 178) + whole/broken mechanism

- Marker-anchored re-parse of tmp_map00.bin (X@59078 Y@59339 info_lo@59600
  info_hi@59865 Z@60391 anim@60521, N=261; staticFuncs LE@60651; bytecode@62019)
  + legacy-exact CFG disassembly (BE operands, loop-tail ++IP).
- Glass = spr155: TILENUM_GLASS (Enums.h:792), W|2SIDE|NOENT z-sprite at
  (2,19), frame0 whole (media 779) / frame1 broken (780); renderMode 3 (ADD);
  wall-plane quad at x=128px, leaf 161. Car side = spr153 (52+257→309);
  floor slabs = FLAT|TILE spr154/spr152 (+257→451/455; raw ranges EMPTY).
- Break mechanism: ENTITY_FRAME(155,1) @3797 inside crash cinematic event[48]
  (trigger tile (18,19), camera 9 parked in-shaft at (160,1248)) and @1065 in
  the per-load restorer gated `EVAL v22==1`; NEXTSTATE 22 @3706 persists it.
  Corrected prior "ship landing" note in lerp-opcodes.md (155 not hidden).
- Rewrite audit: parser/renderMode/textures/leaf-math/script semantics all
  verify equal; confirmed renderer divergence for the slabs = missing FLAT
  plane branch (World3D.cpp:721-742 vertical-only); unproven whether event[48]
  ever fires in rewrite sessions (no executeTile(18,19)/camera=9 in any log).
  Probes specified per stage in docs/research/2026-08-25-elevator-glass.md §4.

## 2026-08-26 — hero choice ↔ player identity + first-person weapon display spec

- PART A verdict: CONFIRMED, v14=1 is correct. Choice 1 = Major **Kira
  Morgan**, the FEMALE marine-stat hero (names from strings.idx type-3 IDs
  215/216/217; art gender verified from extracted tiles 66/68/72 head frames;
  NPC frames are stacked legs/torso/head composites, Enums.h:604-613). No
  choice-gated spawn position (Game.cpp:941-972) and no choice-gated view
  hands (Combat.cpp:621-844); the chosen squad sprite (7/10/9) IS the
  player's world body: formation walk-in incl. chosen one (func@2328),
  INSTANT park to (22,29) @2531/2545/2552, later tile-event companion walk
  (IPs 7184-7633, tiles (21,27)-(23,28)). EVT 617 doorway walkers are always
  the two NON-chosen sprites, hidden after. Today's err.log proves the
  rewrite (v14=1) reproduces the legacy end state; the "duplicate hero" the
  user saw = intentional formation appearance (+ yesterday's v14=0 bug).
  No further fix needed.
- PART B: legacy view-weapon spec extracted (Combat::drawWeapon 621-844:
  176x176 quad at (196+wpX, 131-wpY), wpinfo table-1 values for all 15
  weapons, getWeaponTileNum map, muzzle flash = tile 1 frame 3, no
  reload/switch anims, 200ms lower/raise). Rewrite audit: HUD icon strip +
  Tables.weaponInfo/weaponData + Player weapon fields exist; NO view-weapon
  draw; Hud::drawBottomBar never called; Hud::weapon_ hardcoded 3. Minimal
  scope: tile fn + idle offsets + screen-space blit after drawBSP.
- Docs: docs/research/2026-08-26-hero-choice-and-weapon.md (full report);
  new curated docs/original-code/entities.md (choice↔identity, NPC
  composites, squad sprite lifecycle) and docs/original-code/combat.md
  (weapon HUD spec + rewrite audit).

## 2026-08-26 — elevator glass render-path audit (research, read-only)

- Hypothesis: spr155 reaches drawSprite yet shows nothing; find the stage that
  drops it. Method: full walk of legacy path (renderBSP → addSprite →
  renderSpriteObject → renderSprite wall branch → GLES state) vs rewrite
  (drawBSP → drawSprite → applyBatchState/flush), plus media decode and raw
  map-record parse.
- Verdict: every static renderer stage verifies EQUAL for this sprite
  (leaf attach 161, index space, painter order + biases, corner math on
  plane x=128px z∈[575,639]mu, ADD blend GL_SRC_ALPHA/GL_ONE + fog off,
  RAW-texture guard passes — media 779 is 16384==128×128 bytes).
  Refuted by evidence: double height-add (rewrite doesn't bake S_Z at load),
  index-space split, TWO_SIDED double-draw (GLES never enables cull face),
  DECAL bit (not set), batch-state inheritance (flush-before-switch),
  missing split-sprites (vestigial upstream — getNodeForPoint only returns
  leaves). Glass art is fully opaque mean RGB(60,124,110) — a solid teal
  veil under ADD, not hideable.
- #1 remaining suspect: viewing geometry — pane lives at z 575–639 mu, far
  above reachable eye heights; original's glass view = in-shaft camera 9
  which never fired in rewrite sessions (D-B). Smoking-gun test specified:
  env-gated renderMode=0 force + one-per-second NDC log of the pane corners;
  outcome table separates never-emitted / off-screen / blended-subtle /
  overdrawn.
- Q4: spr152 raw record proves NOT hidden (0x20F400C6, pos (96,1056), Z=64);
  it is leaf-less until event[48]'s LERPSPRITE parks it at (2,19); both
  engines skip leaf-less sprites identically. drawSprites confirmed dead code.
- Docs: appended "Glass render-path audit (2026-08-26)" to
  docs/research/2026-08-25-elevator-glass.md; corrected stale "no FLAT branch"
  claim + added RAW-guard/cull/bias facts to docs/original-code/rendering.md §5.

## 2026-08-26 — Walk-cinematic flicker root cause (BSP leaf-straddler drop)

- Symptom: squad NPCs flicker while walking; anim bytes verified clean
  (user dbg audit), alloc-reuse reset already monsters-only.
- Legacy mechanism fully mapped: getNodeForPoint band early-out attaches
  sprites within ±128 classify-units (±8 world units) of an internal node's
  plane to THAT node (src/Render.cpp:2422-2424); per-frame rescue via
  walkNode snapshot -> addSplitSprite -> addNodeSprites dual-leaf listing
  (src/Render.cpp:1085,1094-1096,896-910,917-922, cap 8/frame); membership
  refreshed per lerp tick (src/Game.cpp:2889-2896). No geometric clipping —
  character stacks draw as one unit under one leaf listing.
- Rewrite gap: per-frame spriteLeaf recompute exists (World3D.cpp:1277-1292)
  and the early-out is ported (:1229), but nodeIdxs_ holds leaves only
  (:1250-1253) and the `spriteLeaf[i]!=leaf` filter (:1311-1312) drops
  internal-band sprites entirely → frame-alternating vanish = flicker.
- Probe (docs/research/assets/probe_bsp_bands.py, byte-exact map00 replay):
  6 sprites invisible at load (incl. zombie spr53, imps spr54/55 — all
  classify EXACTLY 0 on split planes); 62/85 entity sprites cross the band
  within a tile of home; dbg squad walkers spend 12–17% of neighborhood
  positions in the dropped state.
- Corrected docs: rendering.md "split-sprites vestigial" claim was WRONG
  (getNodeForPoint DOES return internal nodes); sprite-placement.md §6
  severity updated. Report + coder checklist:
  docs/research/2026-08-26-walk-flicker.md.

## 2026-08-26 — Stage-1 combat package: fire pipeline, turn structure, monster loop

- Read-only research for the combat port. Full report:
  docs/research/2026-08-26-combat-stage1.md; new curated sections 5 (fire
  pipeline & damage math) and 6 (turn structure) appended to
  docs/original-code/combat.md.
- Key extractions (all cited there): advanceTurn flow + Error-95 snap
  invariant + monstersTurn window (src/Game.cpp:1238-1281,2458-2474);
  ACTION_FIRE target-election ray vs damage math split
  (src/PlayingInputHandler.cpp:189-378); exact calcHit/calcDamage formulas
  incl. Chebyshev tileDistances, weakness nibble table, miss-streak caps
  (src/CombatEntity.cpp:157-378); hitscan pain-in-stage-0 / death-in-stage-1
  sequencing (src/Combat.cpp:1572-1576,1421-1430,382-386); monster wake
  funnels + goal machine + combatMonsters queue (outline only).
- tables.bin values dumped directly (tools run in temp dir): weapon rows,
  monsterStats rows (imp(3,0)=50hp), weakness nibbles — recorded in report §2.4.
- Rewrite audit: all combat tables already parsed; monsters spawn as entities
  but Entity::monster is never allocated; no Combat state/fire path; ScriptVM
  ops 10/51/56 still kill threads (default case). EXISTS/MISSING table in §5.
- Recommended Stage-1 slice defined (view weapon quad → fire input → calcHit/
  calcDamage → EntityMonster alloc → pain/died → health-bar feed → VM ops
  10/51/56/84) with deferral rationale.

## 2026-08-26 — cinematic jerk root cause: first ADV_CAMERAKEY skips key 0 (research)

- User report: hard camera jerk exiting the elevator + "animations feel
  accelerated". Verdict CONFIRMED, single primary cause: legacy setupCamera
  starts activeCameraKey = -1 (src/ScriptThread.cpp:188) and the script's
  FIRST EV_ADV_CAMERAKEY lands on key 0 with a fresh clock
  (src/ScriptThread.cpp:695, src/MayaCamera.cpp:36-44); the rewrite starts
  at activeCameraKey_ = 0 (GameContext.cpp:583) and advanceCameraKey
  unconditionally nextKeys -> plays keys[1..].
- Consequences at the elevator exit: cam7 plays 1500/2750 ms and opens with a
  hard cut to key1 yaw instead of gliding from the -2 player-inherit sentinel;
  cam10 (ev[56], tile (10,19) ENTER) has ms=0 last key + last-key branch ->
  completes same-tick, doorway cutaway never renders; boot cams lose openers
  (cam5 -1999 ms, cam0 -999 ms). Clock-rate divergence REFUTED (our clocks only
  ever freeze more than legacy). Secondary: missing Snap-tail pitch reset +
  startRotation(true) analog in finishCinematic; shake envelope linear decay
  vs legacy flat-until-deadline.
- Deliverables: docs/research/2026-08-26-cinematic-jerk.md (+ probe script);
  curated op-18/lifecycle facts merged into docs/original-code/cutscenes-camera.md.
  Journal entry by orchestrator (researcher lacked journal perms).

## 2026-08-26 — combat stage 1 GROUP 1 (domain core) implemented

- Files: NEW new_src/domain/game/EntityMonster.h (58), Combat.h (130),
  Combat.cpp (391); EDIT CombatEntity.{h,cpp} (verbatim calcCombat/calcHit/
  calcDamage trio, src/CombatEntity.cpp:130-378), Game.{h,cpp} (pool[80] +
  active/inactive rings + activate/deactivate ports, painMonster/diedMonster/
  awardKillXP, updateMonsters/endMonstersTurn/snapMonsters stubs, advanceTurn
  Err-95 guard, pain auto-revert sweep in update()), Player.{h,cpp}
  (disabledWeapons/facingEntity/xpGained, addXP/addLevel/calcLevelXP,
  fireWeapon guard chain), ScriptVM.cpp (ops 10/19/28/51/56/60/84),
  Main.cpp (combat.init(Env) wiring + std::srand seed), GameContext.cpp
  (tickPlaying step-4 combat.tick/updateMonsters split, combat.active input
  drop, tickLoading endMonstersTurn swap).
- Gotcha: rewrite CombatEntity::clone(other) copies FROM the arg — the legacy
  template.clone(dest) direction is reversed; first spawn run produced
  hp=0/0 until the call was flipped.
- Verify: build green (0 warnings); boot -> intro cinematic -> ST_PLAYING
  (D2R_AUTOTEST=1 reaches autotest Forward queue); [monster] spawn hp=62/62
  for imps (difficulty bump verified); op 19 DAMAGEMONSTER sprite=15 dmg=127
  now goes pain->died->corpse; PER_TURN corpsify of imps 41/42 intact;
  zero UNIMPLEMENTED/ERR_ lines on the boot route.

## 2026-08-26 — combat stage 1 GROUP 2 (fire path + facing feed) implemented

- Files: EDIT core/GameContext.{h,cpp} (Action::Use fire election after the
  door branch — one swept tile, CONTENTS_WEAPONSOLID 13997 radius 2, closest-hit
  classification monster-wins / NPC dist>=8192 / wall-push-log-no-turn / else
  air-shot into Player::fireWeapon -> Combat::performAttack; Action::Passturn
  msg45 + advanceTurn; facing probe updateFacingProbe() on the Game::facingDirty
  latch with tickPlaying step 4.5; D2R_AUTOPASS=N TEMP driver knob),
  domain/game/Game.{h,cpp} (lastTraceHits() accessor, worldEntity() air-shot
  slot, traceMove world contact-point capture = legacy traceCollisionX/Y,
  entityDistFrom ET_WORLD/def==nullptr resolves through it per calcPosition's
  ET_WORLD branch src/Entity.cpp:1375-1378, advanceTurn sets facingDirty per
  src/Game.cpp:1269, removeEntity clears player->facingEntity per src/
  Game.cpp:192), domain/game/Player.cpp (auto-equip now syncs ce.weapon —
  G1 gap surfaced by the fire path: give() set only the rewrite-era mirror).
- Pre-landed in G1 (no-op here): difficulty-order fix (vars[12]=2 before
  loadEntities, GameContext.cpp:250-253), diedMonster facingDirty latch.
- Deviations kept from spec §9: single-closest-hit election (stacked candidates
  differ), probe rescan subset skips the spritewall mapFlags gate + showHelp
  promotions, zoom-entry/initZoom logged once and consumes the input.
- Verify: build green; headless D2R_AUTOTEST=0 D2R_AUTOUSE=4 D2R_AUTODIALOG=1:
  after EVT 617 each Use prints [fire] election ... -> air + [combat]
  performAttack sprite=-1 type=0 dist=4096 tileDist=1 shots=2 + fire sound 1014;
  D2R_AUTOPASS=1 prints [turn] passturn msg45="Turn Passed"; [face] probe lines
  re-fire on Playing entry / advanceTurn / rotation arrival (spr=19 type=3 NPC
  tracked at spawn); doors/walk/loot flows unchanged; zero UNIMPLEMENTED.

## 2026-08-26 — playtest fixes: lift ride cutscenes + combat stage 1 corrections

User playtest of combat stage 1 + camera key-0 produced four defects. Camera key-0
itself CONFIRMED good by the user (intro flight, hangar door, lift ride, exit visible,
background room-explosion cutscene now at proper speed). Regression list clean:
doors, loot, dialogs, TAB "Turn Passed".

### Research (4 researchers in parallel)

- Facing/health bar: original bar target is `player->facingEntity`, produced by a
  **6-tile ray** (`src/MovementController.cpp:28-158`, mask 21741, radius 2, origin =
  tile centre + 28 units), nearest-hit + promotion post-filter; non-monsters dropped
  beyond `tileDistances[2]` (3 tiles), **monsters never distance-gated**; recomputed
  every rendered frame via `src/Hud.cpp:735-742`. Bar geometry for 480x320: 25 segments
  7x14, frame 202x17 at x=139, y = viewRect[1] + n3 (6 / 50 pinky / +20 zoomed),
  250 ms linear drain. Our spec's "one tile toward viewStep" was the defect source.
  → `docs/original-code/combat.md` §7, `docs/research/2026-08-26-facing-entity-health-bar.md`.
- Fire targeting: **separate** trace from `viewX/viewY/viewZ`, mask 13997
  (`CONTENTS_WEAPONSOLID` = PLAYERSOLID − 16 + 512: guns see corpses, the player walks
  through them), fixed 6-tile line (1 for chainsaw via `WP_MELEEMASK`). An own-tile
  entity gets frac −1 and sorts FIRST, so "nearest hit wins" was wrong: the original
  walks the sorted list with per-type accept/skip/break rules, and the corpse branch
  (`src/PlayingInputHandler.cpp:279`) accepts only at `dist == tileDistances[0]` (exact
  equality) and **never breaks** — hence the own-tile corpse is skipped.
  Refuted: `tileDistances[0]` at `src/Hud.cpp:981` is the dialog-bubble offset, not range.
  → `docs/original-code/combat.md` §8, `docs/research/2026-08-26-fire-target-acquisition-corpse-tile.md`.
- View weapon: `draw2DSprite` always samples only the top-left **176x176** texels of the
  256x256 media (`src/GLES.cpp:539-542`) — we passed 256x256 into a 176x176 quad (0.6875x
  shrink, which also lifted the gun); and the original's anchors are **viewport-relative**
  (`glViewport(1, 65, 478, 248)` → canvas band (1,7,478,248), centre (240,131)).
  → `docs/original-code/rendering.md` §6, `combat.md` §1/§4.1,
  `docs/research/2026-08-26-view-weapon-placement-audit.md`.
- Lift rides: NOT a timing bug. Durations were already right (cam 11 = 600/200/0 ms,
  cam 12 = 800/0 ms, probed from `tmp_map00.bin`). Tween indices are **camera-local**;
  legacy rebases them because it keeps one global per-channel array (`src/Game.cpp:612-620`),
  our per-camera blob must not. Also documented: instant `EV_GOTO` defers the destination's
  ENTER events past the cinematic — load-bearing, it prevents a recursive ride.
  → `docs/original-code/cutscenes-camera.md` §3, `docs/research/2026-08-26-lift-ride-cutscene-timing.md`,
  probe `docs/research/assets/probe_lift_cams.py`.

### Implementation

- Camera tween addressing (minimal mechanical fix, no spec): `pos = m_chanOfs[ch] + indx + step`,
  `m_basePrev` deleted, silent out-of-range `return 0` replaced by a loud `[cam]` diagnostic.
  Files: `new_src/core/MayaCamera.{cpp,h}`. **Reviewed PASS** — reviewer replayed both address
  schemes over `tmp_map00.bin`: 14/14 cameras byte-identical to legacy, 0 out-of-range.
  **User-confirmed: "фикс для кат сцен работает идеально"**, zero `[cam] tween out of range`
  in a full playthrough log.
- Spec for the other three: `docs/architecture/specs/2026-08-26-combat-stage1-fixes.md`
  (6 groups) + `docs/architecture/adr/0009-legacy-world-viewport.md`;
  `2026-08-26-combat-stage1.md` marked partially superseded (§0.F :157-160, §9).
- G1 facing probe (`core/GameContext.{h,cpp}`): 6-tile ray via new `viewForward()`, mask 21741,
  full promotion table incl. the `mapFlags & 0x2` gate, monsters ungated, probe moved from the
  `facingDirty` latch to the legacy per-frame render site. Coder followed `src/` over the spec
  where the promotion table's "(no break)" annotation contradicted `src/MovementController.cpp:74-83`.
- G2 bar geometry (`ui/Hud.{h,cpp}`): pinky `n3 = 50`, boss `++n4`, feed extended with
  `lowBar`/`boss`. Rest of the bar verified already correct.
- G3 fire election (`core/GameContext.{h,cpp}`): `electFireTarget(weapon, &frac)` — ordered walk
  over sorted hits with the per-type table, corpse branch never breaks. Loot preemption gated on
  `weapon != 1` (deviation, follows `src/PlayingInputHandler.cpp:280-334` over the spec, else the
  chainsaw could not gib). Plus the G2 feed flags wired.
- G4 bottom HUD panel (`ui/Hud.{h,cpp}`): `gameMenu_Panel_bottom.bmp` 480x64 at y=256 — required
  before G5 or the legacy band leaves a 65 px black strip. Deviation: call sits at the end of
  `drawTopBar` (same single call site/states) because `GameContext.cpp` was locked by G1.
- G5 viewport + weapon: in flight at time of writing.

Build green after every group (verified by the orchestrator, not only by coder reports).

### Open / follow-up

- Bottom panel currently draws background only: the widgets (shield, health, portrait, weapon
  icon, keys) live in `Hud::draw`, which has no caller in the gameplay render path.
- Non-blocking review notes on the camera fix: unthrottled tween diagnostic, a comment now
  sitting above the wrong loop, and the TEMP `[cam] nextKey` print at
  `new_src/core/GameContext.cpp:735` (kept deliberately until the rides were eye-confirmed —
  they now are, so it can go).
- Untested paths from the research: `estNumTweens == 0` for the map's last global key;
  map-placed non-skeleton corpses may be un-gibbable (`info & 0x20000`).

### 2026-08-26 (cont.) — playtest round 2: review FAIL fixed, weapon geometry re-derived

User eye-confirmed after groups 1-5: shooting while standing on a corpse works, health bar
shows at range, shots land beyond one tile, muzzle flash present, bottom panel present
(widgets still missing — known). Two defects remained: cutscene viewport "slides down",
rifle still too high (and, from a side-by-side with the legacy binary, too small).

Reviewer verdict **FAIL**, 2 blocking items, both fixed:

1. **Wall shots fired and burned the turn** (regression introduced by our own 6-tile ray).
   `Game::traceMove` pushes the world hit as `entities_[0]` with `def == nullptr`, so
   `hitType` resolved to −1 and the `ET_WORLD` wall-push branch was dead code. Legacy
   (`src/PlayingInputHandler.cpp:467-487`) does shiftWeapon+rockView and returns without
   firing or advancing the turn. Fixed with the `electFireTarget` convention
   (`def == nullptr → ET_WORLD`).
2. **Cinematic viewport.** NOT a y-flip bug (`RenderBackend::setCanvasViewport` is correct:
   GL y = 320−7−248 = 65, matching legacy). The original renders cinematics into the SAME
   GL rect as gameplay: `cinRect (0,42,480,250)` (`src/Canvas.cpp:1215`) → `(1,23,478,248)`
   (`src/TinyGL.cpp:149-167`) → `BeginFrame` → **y discarded**, `posY = 65`
   (`src/GLES.cpp:119-127`). The letterbox is the 2D cockpit overlay at y=42
   (`src/Hud.cpp:620-625`); `cinRect` is not a viewport rect. Fixed: one `kWorldRect` for
   both paths. ADR 0009 D1 refuted and amended (history kept).
   Root cause of the mistake: `docs/original-code/rendering.md` §6 documented only the
   gameplay chain — now fixed (§6.3).
   Also fixed: air-shot impact point now uses the trace collision point instead of
   `(viewX, viewY)` (invisible with a 1-tile ray, at the player's feet with 6 tiles);
   `Graphics2D.h` comment placement; one-shot latch on the tween diagnostic.

**View weapon — our whole model was wrong** (`docs/research/2026-08-26-legacy-draw-path-and-bands.md`):
`Render::draw2DSprite` on the GL path is NOT a screen-space blit. It builds a **world-space
billboard 400 units in front of the eye** (`src/Render.cpp:343-421`) that the world projection
magnifies (`src/GLES.cpp:483-547`): `Kx = m[0]/12800 = 1.33578`, `Ky = m[5]*248/(478*12800) =
1.33973`. That is also why the software fallback hard-codes `scaleFactor *= 1.35f` — an
approximation of the same factor. So the rifle is 235x236 on canvas, quad (165,22)-(400,258),
visible art (273,162)-(399,261) clipped at y=255 (it TOUCHES the bottom panel), flash 117 px.
Implemented via a `drawWeaponQuad` helper that scales about the viewport centre and trims the
quad + source sub-rect by hand.
Corrected two earlier claims: the "1:1 pixel mapping" in the view-weapon audit, and the
"software-TinyGL variant is not the port target" dismissal — both paths agree within 2 px,
so no path switch is needed.
Legacy-binary caveat: `build/src/DoomIIRPG` runs in **software** mode (`isInit = 0` in its
CONFIGFILENAME) and is not reproducible from this `src/` (it has a Video Options menu that
does not exist here) — fine for comparison given the 2 px agreement.

**FOV conflict resolved** (researcher's first answer was wrong, corrected on a second pass):
there is no fov field; `MayaCamera::Render` passes literals — **290** for `ST_DIALOG` with an
active camera, **315** for cinematics (`src/MayaCamera.cpp:301-323`). Full inventory of every
fov in the game now in `docs/original-code/rendering.md` §6.3. No per-camera/per-key fov exists
in map data (maya keys carry exactly X/Y/Z/PITCH/YAW/ROLL/MS). So cinematic aspect 163 is right.

### Debts recorded

- Weapon magnification is hard-coded for the GAMEPLAY projection; cinematics use Kx 1.221 /
  Ky 1.224 (fov 315). Harmless today because `cinematicWeapon` is not implemented in the
  rewrite at all, but wrong by construction — derive K from the active projection instead
  (needs an accessor for `Camera3D::projection_`, `new_src/render/Camera3D.h:63`).
- `Graphics2D::setClip` is a **no-op for the sprite batch** (`new_src/render/Graphics2D.cpp:17-29`
  records the region, never scissors) — a trap for anyone who relies on it.
- The fix spec contained four errors found by coders/reviewer while checking against `src/`
  (facing "one tile", the promotion table's "(no break)", loot preemption vs chainsaw, and a
  claimed `Game.h:364` accessor that did not exist). Worth a spec-vs-original review pass.
- Bottom HUD panel draws background only; the widgets live in `Hud::draw`, which has no caller
  in the gameplay render path.

**Round 2 eye-check — user-confirmed ALL of it (2026-08-26):** rifle renders correctly,
cutscene entry no longer jumps the viewport, a point-blank wall shot consumes no ammo and no
turn, impact position and muzzle flash correct. Together with the round-1 confirmations
(lift rides, health bar at range, shooting from a corpse tile) every defect from the playtest
is closed, including the wall-shot regression our own 6-tile ray introduced.

### 2026-08-26 (cont.) — cinematic letterbox + decomposition designed

Round-2 review returned FAIL again (2 blockers, both at the gameplay/cinematic projection
boundary the previous fix opened): cinematic fov must be **290 during a dialog** (`src/MayaCamera.cpp:305-310`;
315 otherwise), and the "latent" weapon-magnification bug was **live** — `drawViewWeapon`
accepted `StateId::Dialog`, so a camera-driven dialog drew the player's weapon with gameplay
magnification under the cinematic projection. Legacy draws no view weapon while a camera is
active unless `cinematicWeapon != -1` (`src/MayaCamera.cpp:311-314`). Both fixed; the
magnification is now derived from the live projection (`Camera3D::projectionInt()`,
`Kx = m[0]/12800`, `Ky = |m[5]|*124/(239*12800)`), verified to reproduce the old hard-coded
1.335781/1.339753 bit-for-bit, so the user-approved gameplay geometry did not move.
**User-confirmed:** weapon unchanged in gameplay, absent in cinematics.

**Cinematic letterbox** — third curated fact refuted this session. The letterbox is NOT the
cockpit overlay art: it is two opaque black fills painted after the world pass,
`(0,0,480,42)` and `(0,292,480,28)` (`src/Hud.cpp:455-456`), and the top one **overpaints the
top 35 rows of the world band** (visible cinematic picture = rows 42..254). Gated on
`state == ST_CAMERA` (`src/Canvas.cpp:1213`), hence ABSENT during a dialog over an active
camera (`:1095`) — the picture deliberately grows 35 rows upward and re-letterboxes on close.
The cockpit art is a separate script-toggled effect (opcode 76 `EV_TOGGLE_OVERLAY`), off by
default, on map00 only around the drop-ship shot — and it turned out we already implement it
correctly (`new_src/domain/game/ScriptVM.cpp:1011-1015`, `Hud::cockpitOverlay()`).
Implemented, **user-confirmed**. ADR 0009's amendment carried the refuted claim and now carries
a dated CORRECTION.

**Process note.** Three curated facts were wrong this session — "draw2DSprite is a 1:1 blit",
"the software-TinyGL path is not the port target", "the letterbox is overlay art". None were
authored by the orchestrator, but all three were accepted without challenge and propagated
into tasks, code and an ADR. Common failure mode: a conclusion drawn from ONE site instead of
following the chain to its final consumer (`BeginFrame` discarding the y; `DrawWorldSpaceSpriteLine`
projecting the billboard; `eraseRgn` painting the bars). Researcher briefs now require walking
the chain to the last consumer and re-verifying quoted citations rather than trusting them.

**Decomposition designed** (user-approved after reading the analysis):
`docs/architecture/specs/2026-08-26-decomposition.md` (19 groups, 2 phases) +
ADR 0010 (module boundaries and Env injection) + ADR 0011 (typed `TraceHit`, named encodings).
Phase 1 extracts `CinematicCamera`, `LootSession`, `Targeting`, `ViewWeapon`, `SceneRenderer`,
`PlayerActions` out of `GameContext` (target <= 400 lines, state machine only). Phase 2 splits
`Game` into `TraceSystem`/`DoorSystem`/`MonsterSystem`/`SpriteLerps`/`CorpseLoot`/`EntityDb`
behind one-line forwarders, so the two chains never share a file and can run in parallel.
Refactor constraint: zero behaviour change; acceptance = the user sees no difference.

### 2026-08-27 — decomposition, groups 1-7 (no behaviour change)

Landed and user-confirmed on screen after each batch:
- b7e74e1: P1-G1 (`core/GameStates.h`: `StateId`/`Action`/`StateHost`, `MapData::heightAt`),
  P1-G2 (`core/CinematicCamera`), P2-GA (`domain/game/TraceSystem` + typed `TraceHit`).
- f18cb7b: P1-G3 (`core/LootSession`), P1-G4 (`domain/game/Targeting`),
  P2-GB (`domain/game/DoorSystem`), P2-GC (`domain/game/MonsterSystem`),
  plus the review blocker fix.

Sizes: `GameContext.cpp` 1626 -> 1056, `Game.cpp` 1565 -> 858. Target for
`GameContext` is <= 400 lines (state machine + a ~20-line render composer).

Review of b7e74e1's refactor: FAIL on exactly one item, and a subtle one — the cinematic
skip frame had gained an extra `hud->update(kTickMs)` because the pre-refactor early
`return` on `skipCinematic_` preceded the HUD tick, while the extracted form ticked it
unconditionally. The spec had prescribed that wrong shape, so both code and spec were
corrected. Everything else verified equivalent by reconstructing the pre-commit bodies
(`git show b7e74e1^:…`) and diffing after normalizing access paths only.

Two process facts worth keeping:
- Parallel coders leave the tree transiently unbuildable (`Game.h` referenced `kOpenDoors`
  while `DoorSystem` was mid-move). "Verify the build after every delegation" means after the
  agent REPORTS, not at an arbitrary moment, or foreign half-work reads as a regression.
- Phase 1 is inherently serial: every group edits `GameContext.{h,cpp}`, so any unplanned fix
  in that file queues behind the current group.

Transitional duplications, all with named graves (tracked in `docs/status.md`):
`DoorSystem`/`MonsterSystem` each hold verbatim copies of `Game::linkEntity`/`unlinkEntity`
(P2-GF), `MonsterSystem::Env` holds a `std::function` lerp-snap shim (P2-GD),
`Game::lastTraceHits()` rebuilds a legacy vector (P3-B2), `GameContext::getHeight` and the
door forwarders are one-liners. Three shims outstanding; if they start accruing faster than
they are paid, stop extracting and close Phase 3 first.

### 2026-08-27 — decomposition complete (Phase 1: 7/7, Phase 2: 6/6)

User-confirmed on screen after the final batch: doors, monsters (wake/pain/death/corpse),
looting, script lerps, the shaft lift ride, shooting past a corpse, health bar at range.
Boot spawn-line count identical to the pre-refactor run (75).

`GameContext.cpp` 1626 -> 535, `Game.cpp` 1565 -> 305. Two god-objects became thirteen
modules with pointwise `Env` injection and no `GameContext*`/singleton anywhere:
`CinematicCamera`, `LootSession`, `Targeting`, `ViewWeapon`, `SceneRenderer`, `PlayerActions`
(Phase 1) and `TraceSystem`, `DoorSystem`, `MonsterSystem`, `SpriteLerps`, `CorpseLoot`,
`EntityDb` (Phase 2).

P2-GF paid the worst debt: five owners of verbatim entityDb helper copies collapsed into
`EntityDb`. Drift check before deleting found the copies identical except one null guard
`SpriteLerps` needed for its pointer-vs-member difference — an injection artifact, not drift,
so no live bug. The duplication existed only because the helpers' owner was scheduled LAST
while every module needing tile-list access came earlier; `EntityDb` should have been the
FIRST Phase-2 group. Lesson for the next multi-group refactor: extract shared owners before
their consumers.

Two claims corrected during the work, both mine:
- "one owner closes the pre-`loadEntities` null-deref window" — it does not. The window comes
  from WHEN `init` runs, not what it points at; closing it needs a `wire()`/`reset()` split
  because three modules' `init` also does the per-level reset. Unreachable today; own ticket.
- The fixes and the first refactor groups could not be split into separate commits because the
  refactor had already moved the fixed code. Commit before refactoring.

Process notes that earned their keep:
- Every coder verified its transfer mechanically (reverse the renames, diff back against the
  deleted block) and declared cosmetic deltas (re-indentation, comment re-wraps, a stray brace)
  so reviewers did not chase them.
- Three specs errors of the same shape — collapsing two conditions that look equivalent — were
  caught by coders reading `src/`; one had already shipped a defect (an extra `hud->update` on
  cinematic skip frames). The rule is now standing in `.claude/agents/coder.md`.
- Agents reached for `perl -pi`/heredocs to write C++; now forbidden in prose AND denied in
  `.claude/settings.json`.

Phase 3 remains (optional): carry `TraceHit` to its last consumers (`PlayerActions` still
decodes `def == nullptr`), name the content masks (13997/21741/13501) and the
`weapons[w*9+field]` indexing, sweep the standing forwarders in `Game.h`.
Non-refactor backlog: bottom HUD panel widgets, sound (still absent — a TEMP `[loot] sound`
print stands in), and the SDL `swapInterval=1` hang exposure (a blocked swap parks the loop
forever, where the original clamped frame time instead).
