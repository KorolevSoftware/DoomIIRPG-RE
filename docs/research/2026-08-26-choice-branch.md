# 2026-08-26 — EVT 617 character-choice branch audit: why sprites 7/10 stand in the blue doorway in the rewrite

Tasking: decode EVT 617 (map00 event[54], entry IP 2993) fully; establish legacy `v14`
(characterChoice) semantics; compare `new_src`; produce the legacy end-state acceptance
table. Read-only research; no code changed.

**Verdict up front: CONFIRMED — root cause found.**
Legacy guarantees `scriptStateVars[14] = characterChoice ∈ {1,2,3}` before any map00
script runs (`src/Game.cpp:3468`, fed by the mandatory intro character select,
`src/IntroSequenceManager.cpp:599/628/676`). The rewrite hardcodes `vars[14] = 0`
(`new_src/domain/game/ScriptVM.cpp:300`) because it has no character-select flow, so
EVT 617 takes the compiler's **fall-through (else) branch**: sprites **7 and 10** walk
onto door tile **(10,19)** (IPs 3236/3243) and **none of the three choice-gated HIDE
blocks executes** (the guards test `==3`, `==1`, `==2` exclusively, IPs 3276–3315).
The event then completes normally and closes+locks sp22 on top of two live ET_NPC
entities → `[dbg] moveBlocked to 10,19 by spr=10 type=3`. EV_HIDE itself is faithful;
it is simply never reached for these sprites.

---

## Method

* Bytecode: `tmp_map00.bin`, bytecode at file offset **62019** (`file_off = IP + 62019`),
  identical provenance chain as `docs/research/2026-08-25-blue-door-passage.md`.
  Decoder mirrors `src/ScriptThread.cpp` arg consumption byte-for-byte (getUByteArg /
  getUShortArg / getIntArg, trailing `++IP`; EVAL token stream incl. 14-bit sign-extended
  constants, `src/ScriptThread.cpp:244-312,2095-2119`). Zero decode errors over
  2993–3470 once op 92 (EV_ENTITY_BREATHES, 2 arg bytes, `src/ScriptThread.cpp:1907-1923`)
  and op 49 (EV_SPEECHBUBBLE, ushort+byte, `src/ScriptThread.cpp:1247-1252`) consume args.
* Sprite ground truth re-derived with `tools/map_to_obj.py` (layout mirror of
  `src/LoadingManager.cpp:463-552`); anchors re-verified this session:
  `staticFuncs=[0]=0,[6]=253`, `event[54] ip=2993 w1=0x269`,
  spawn=612/dir=0, events=167, bcSize=10015 — all matching the prior verified docs.
  ⚠ Decoder traps found on the way, documented here for future sessions:
  PARABOLA travel time is a RAW ushort, not ×100 (`src/ScriptThread.cpp:1656,1682`);
  `readMarker` never validates marker bytes (`src/Resource.cpp:84-86`) so marker-walk
  anchoring only works for the two real 0xDEADBEEF media markers.
* **Cross-validation:** the final listing below was re-checked line-by-line against
  the prior session's independent full CFG disassembly
  (`docs/research/assets/map00_disasm.txt:753-850`) — instruction stream identical
  over IP 2993–3451, including `EVENTOP 32822/32809/32819`, `GOTO 4308` (=0x10D4),
  `DOOROP 1046/6166`, `OP27 43` (@3322) and all six EVAL guards. Boot-tail leader
  teleports likewise confirmed (`map00_disasm.txt:641,644,646`). Caveat when reading
  that file's neighbors: bytes `00 C8` at 2520–2521 are EV_FADEOP's ushort argument
  (`2519: OP32 200` in the canonical listing), not an EVAL — a linear decoder that
  skips op 32's args desyncs there.

---

## 1. EVT 617 complete annotated listing (entry IP 2993 → RETURN 3332, callees inlined)

### 1.1 Prologue (one-shot guard + opening dialog)

```
2993 f00fdf4  EVENTOP ev[54] DISABLE            ; self-disable (17 80 36)
2996 f00fdf7  EVENTOP ev[41] DISABLE            ; (17 80 29)
2999 f00fdfa  EVENTOP ev[51] DISABLE            ; (17 80 33)
3002 f00fdfd  DIALOG str(21) type=1 param=0     ; (0D 15 01)
```

### 1.2 Pre-scene dialogs (all values)

```
3005 f00fe00  EVAL [v14 != 3] iffalse -> 3018   ; (00 03 8E 40 03 05 06)
3012 f00fe07    NPCCHAT param=0 spr=19          ; runs when v14 != 3 (incl. v14 == 0!)
3015 f00fe0a    JUMP -> 3018
3018 f00fe0d  EVAL [v14 == 1] iffalse -> 3033   ; (00 03 8E 40 01 04 08)
3025 f00fe14    WAIT 500ms                      ; ─ v14 == 1 only
3027 f00fe16    DIALOG str(23) type=8
3030 f00fe19    JUMP -> 3048
3033 f00fe1c  LERPOFFSET spr=7 dst=(592,1224) z=32 flags=BLOCK time=800ms  ; v14 != 1:
              ;                                    canvas (592,1224) = TILE (9,19)
3040 f00fe23  DIALOG str(22) type=1
3043 f00fe26  WAIT 500ms
3045 f00fe28  DIALOG str(24) type=8
```

### 1.3 Scientist-only extra beat (v14 == 3)

```
3048 f00fe2b  EVAL [v14 == 3] iffalse -> 3086   ; (00 03 8E 40 03 04 1F)
3055 f00fe32    WAIT 500ms
3057 f00fe34    LERPOFFSET spr=7 dst=(600,1192) z=32 BLOCK 400ms   ; = TILE (9,18)
3064 f00fe3b    DIALOG str(25) type=1
3067 f00fe3e    NPCCHAT param=0 spr=19
3070 f00fe41    WAIT 500ms
3072 f00fe43    CALL_FUNC func@3375             ; grenade re-run (§1.6)
3075 f00fe46    CALL_FUNC func@3401             ; give items (§1.7)
3078 f00fe49    MESSAGE str=133 HUD
3081 f00fe4c    WAIT 1000ms
3083 f00fe4e    JUMP -> 3086
```

### 1.4 Doorway-walk blocks — WHO walks depends on v14

All three variants: CALL_FUNC func@3333 (door dialog + UNLOCK + blocking OPEN, §1.5),
two ENTITY_FRAME frame=16 poses, `LERPSPRITE spr=19 dst=(11,19)` 1500 ms (bob steps
through), then TWO squad sprites lerp onto **the door tile (10,19)**.

```
3086 f00fe51  EVAL [v14 == 1] iffalse -> 3142   ; ── MARINE branch ──
3093 f00fe58    LERPOFFSET spr=9  dst=(592,1224)=(9,19) ASYNC|BLOCK 800ms
3100 f00fe5f    LERPOFFSET spr=10 dst=(592,1288)=(9,20) BLOCK 600ms
3107 f00fe66    CALL_FUNC func@3333
3110/3116       ENTITY_FRAME spr=9 / spr=10 frame=16
3120 f00fe73    LERPSPRITE spr=19 dst=(11,19) flags=ASYNC|BLOCK|DEF_Z time=1500ms
3127 f00fe7a    LERPSPRITE spr=9  dst=(10,19) flags=ASYNC|DEF_Z time=800ms   ← doorway
3134 f00fe81    LERPSPRITE spr=10 dst=(10,19) flags=ASYNC|DEF_Z time=1000ms  ← doorway
3139 f00fe86    JUMP -> 3248

3142 f00fe89  EVAL [v14 == 2] iffalse -> 3198   ; ── HEAVY branch ──
3149 f00fe90    LERPOFFSET spr=7  dst=(592,1224)=(9,19) ASYNC 400ms
3156 f00fe97    LERPOFFSET spr=9  dst=(592,1288)=(9,20) BLOCK 600ms
3163 f00fe9e    CALL_FUNC func@3333
3166/3172       ENTITY_FRAME spr=7 / spr=9 frame=16
3176 f00feab    LERPSPRITE spr=19 dst=(11,19) 1500ms
3183 f00feb2    LERPSPRITE spr=7  dst=(10,19) 800ms                          ← doorway
3190 f00feb9    LERPSPRITE spr=9  dst=(10,19) 1000ms                         ← doorway
3195 f00febe    JUMP -> 3248

3198 f00fec1  (fall-through: v14 == 3 AND ANY OTHER VALUE incl. 0)  ; ── SCIENTIST/else ──
3198 f00fec1    LERPOFFSET spr=7  dst=(592,1224)=(9,19) ASYNC 400ms
3205 f00fec8    LERPSPRITE spr=10 dst=(8,20) flags=NO_TIME|DEF_Z  ← INSTANT teleport
3209 f00fecc    LERPOFFSET spr=10 dst=(592,1288)=(9,20) ASYNC|BLOCK 600ms
3216 f00fed3    CALL_FUNC func@3333
3219/3225       ENTITY_FRAME spr=7 / spr=10 frame=16
3229 f00fee0    LERPSPRITE spr=19 dst=(11,19) 1500ms
3236 f00fee7    LERPSPRITE spr=7  dst=(10,19) 800ms                          ← doorway
3243 f00feee    LERPSPRITE spr=10 dst=(10,19) 1000ms                         ← doorway
```

**Walker table (who ends up standing on (10,19)):**

| v14 | walkers onto (10,19) | third squadmate untouched here |
|---|---|---|
| 1 (marine) | 9 and 10 | 7 |
| 2 (heavy) | 7 and 9 | 10 |
| 3 (scientist) | 7 and 10 | 9 |
| **anything else (0!)** | **7 and 10** | 9 |

### 1.5 Camera + THE CHOICE-GATED HIDE BLOCK + door tail (final segment)

```
3248 f00fef3  WAIT 500ms
3250 f00fef5  STARTCINEMATIC camera=7
3252 f00fef7  GOTO tile=(6,20) INSTANT angle=4      ; player yanked out of the way (0F 10 D4)
3255 f00fefa  ADV_CAMERAKEY count=4
3257 f00fefc  DOOROP spr=14 act=1 UNLOCK+CLOSE quietflag          ; elevator door shuts

3260 f00feff  EVAL [v14 != 3] iffalse -> 3276      ; give items now unless scientist
3267 f00ff06    CALL_FUNC func@3401                ;   (already given at 3075 for v14==3)
3270 f00ff09    MESSAGE str=133 HUD
3273 f00ff0c    JUMP -> 3276

3276 f00ff0f  EVAL [v14 == 3] iffalse -> 3290      ; (00 03 8E 40 03 04 07)
3283 f00ff16    HIDE spr=10                        ;   scientist: hides ITS walkers 10+7
3285 f00ff18    HIDE spr=7
3287 f00ff1a    JUMP -> 3290
3290 f00ff1d  EVAL [v14 == 1] iffalse -> 3304      ; (00 03 8E 40 01 04 07)
3297 f00ff24    HIDE spr=10                        ;   marine: hides ITS walkers 10+9
3299 f00ff26    HIDE spr=9
3301 f00ff28    JUMP -> 3304
3304 f00ff2b  EVAL [v14 == 2] iffalse -> 3318      ; (00 03 8E 40 02 04 07)
3311 f00ff32    HIDE spr=9                         ;   heavy: hides ITS walkers 9+7
3313 f00ff34    HIDE spr=7
3315 f00ff36    JUMP -> 3318
              ; ⚠ NO else-HIDE: v14 ∉ {1,2,3} hides NOTHING (this is the hole)

3318 f00ff39  LERPSPRITE spr=19 dst=(21,21) flags=INSTANT   ; bob teleports away pre-close
3322 f00ff3d  NEXTSTATE ++v43                        ; (1B 2B) — missed by prior docs
3324 f00ff3f  DOOROP spr=22 act=1 UNLOCK+CLOSE       ; blue door CLOSES (blocking 750 ms)
3327 f00ff42  DOOROP spr=22 act=2 LOCK quietflag     ; then quiet-LOCK (def swap only)
3330 f00ff45  NEXTSTATE ++v37                        ; (1B 25)
3332 f00ff47  RETURN
```

The close is legal even with walkers on the tile: `performDoorEvent` n==1 probes only
`eType == 2 && eSubType != 17` monsters for blocking (`src/Game.cpp:1069-1079`);
ET_NPC never blocks a scripted close.

### 1.6 func@3333 — door dialog, first open, grenade cleanup

```
3333 f00ff48  WAIT 500ms
3335 f00ff4a  EVAL [v14 == 1] iffalse -> 3348
3342 f00ff51    DIALOG str(26) type=8             ; marine gets silent-nod variant
3348 f00ff57  DIALOG str(26) type=1               ; everyone else spoken
3351 f00ff5a  WAIT 500ms
3353 f00ff5c  SPEECHBUBBLE id=27 color=1
3357 f00ff60  WAIT 1000ms
3359 f00ff62  ENTITY_FRAME spr=19 frame=16
3363 f00ff66  HIDE spr=203                        ; grenade prop removed BEFORE open
3365 f00ff68  DOOROP spr=22 act=3 UNLOCK
3368 f00ff6b  DOOROP spr=22 act=0 OPEN            ; blocking open (the "first opener")
3371 f00ff6e  ENTITY_BREATHES spr=19 flag=1
3374 f00ff71  RETURN
```

func@3375 (called only from the v14==3 beat @3072):

```
3375 f00ff72  LERPSPRITE spr=203 dst=(9,19) z=40 INSTANT   ; prop re-placed on trigger tile
3380 f00ff77  ENTITY_FRAME spr=19 frame=176
3384 f00ff7b  PARABOLA spr=203 dst=(8,19) h=16 BLOCK time=800ms  ; raw-ushort time!
3391 f00ff82  HIDE spr=203
3393 f00ff84  ENTITY_FRAME spr=19 frame=0
3397 f00ff88  ENTITY_BREATHES spr=19 flag=0
3400 f00ff8f  RETURN
```

### 1.7 func@3401 — give items (difficulty-gated via v12)

```
3401 f00ff8c  GIVEITEM def=1 count=1
3405 f00ff90  ENABLE_HELP 0
3407 f00ff92  EVAL [v12 == 1] iffalse -> 3421     ; v12 = difficulty (src/LoadingManager.cpp:692)
3414 f00ff99    GIVEITEM def=85 count=90
3418 f00ff9d    JUMP -> 3449
3421 f00ffa0  EVAL [v12 == 2] iffalse -> 3435
3428 f00ffa7    GIVEITEM def=85 count=40
3432 f00ffab    JUMP -> 3449
3435 f00ffae  EVAL [v12 == 4] iffalse -> 3449
3442 f00ffb5    GIVEITEM def=85 count=10
3446 f00ffb9    JUMP -> 3449
3449 f00ffbc  ENABLE_HELP 1
3451 f00ffbe  RETURN
```

---

## 2. v14 semantics (legacy) — CONFIRMED

* **Refreshed before every VM step batch:** `ScriptThread::run()` begins with
  `updateScriptVars()` (`src/ScriptThread.cpp:230-231`), which stamps
  `scriptStateVars[14] = app->player->characterChoice` (`src/Game.cpp:3461-3471`,
  assignment at `:3468`). No bytecode write can pin v14 to a stale value.
* **Only writer of `characterChoice`:** `Player::setCharacterChoice(short)`
  sets both the field and var14 (`src/Player.cpp:1264-1268`); also used by
  save-load (`src/Player.cpp:1274`).
* **Call sites — all in the mandatory intro character select:**
  `src/IntroSequenceManager.cpp:599`, `:628`, `:676`, always
  `setCharacterChoice(canvas->stateVars[0])`. Possible values are exactly
  **1, 2, 3**: touch buttons map slot0→1, slot1→3, slot2→2 (`:613-624`), and keyboard
  cycling walks `getCharacterConstantByOrder` = {1, 3, 2}
  (`src/IntroSequenceManager.cpp:328-341`). There is no skip path — confirming fires
  `ST_INTRO` and then map load.
* **Class identity per value** (HUD art, `src/App.cpp:439-450`; base stats +
  starting gold, `src/Player.cpp:454-486`):
  - **1 = marine** ("Hud_Player.bmp"; DEF 8 / STR 9 / ACC 97 / AGI 12 / IQ 110, gold 30)
  - **2 = heavy** ("Hud_PlayerDoom.bmp"; DEF 12 / STR 14 / ACC 92 / AGI 6 / IQ 100, gold 10)
  - **3 = scientist** ("Hud_PlayerScientist.bmp"; DEF 8 / STR 8 / ACC 87 / AGI 6 / IQ 150, gold 80)
* **Default marine start:** legacy has no "default" — selection is forced. The rewrite's
  documented intent is marine: "choice is fixed to the first marine"
  (`new_src/domain/game/DialogSystem.cpp:456-457`), i.e. **v14 should read 1**.
  A fresh legacy `Player` object is memset-zero (`src/Player.cpp:18-20`), but that
  value is unreachable at script time because of the forced select.
* Map load zeroes the var array first (`src/LoadingManager.cpp:633`) and stamps v12 =
  difficulty (`:692`); v14 is then owned by `updateScriptVars` from that point on.

---

## 3. Rewrite comparison — divergence pinpointed

Checked item by item against `new_src/domain/game/ScriptVM.cpp`:

* **vars[] initialization:** `short vars[kNumStateVars] = { 0 }` with
  `kNumStateVars = 128` (`new_src/domain/game/ScriptVM.h:39,55`).
* **var 14 writers pre/during intro:** exactly one — the hardcode
  `vars[14] = 0; // character choice` inside `updateScriptVars()`
  (`new_src/domain/game/ScriptVM.cpp:300`), re-applied on every `run()`
  (`:316-317`). Generic SETSTATE/NEXTSTATE paths exist (`:384, :525, :531`) but no
  script writes v14 before EVT 617. Nothing else in `new_src/` references
  characterChoice (repo-wide grep: only the DialogSystem comment). Boot goes straight
  to map00 — `new_src/core/Main.cpp` parses and starts the game with no intro/select
  state machine (`:144-163`). `GameContext::stateVars[9]`
  (`new_src/core/GameContext.h:60,138`) is a different array, unrelated.
* **EV_EVAL:** faithful port incl. sign-extension and forward false-jump
  (`new_src/domain/game/ScriptVM.cpp:327-353` vs `src/ScriptThread.cpp:244-312`).
  Not the problem.
* **EV_GOTO:** faithful for both animated and instant forms
  (`new_src/domain/game/ScriptVM.cpp:1029-1079` vs `src/ScriptThread.cpp:593-661`).
  Not the problem.
* **EV_HIDE (incl. eType==3):** sets `mapSpriteInfo |= 0x10000`, resolves the entity,
  `info |= kInfoActivated` (0x400000) and `unlinkEntity`
  (`new_src/domain/game/ScriptVM.cpp:492-521`), mirroring
  `src/ScriptThread.cpp:818-843` where eType 3 intentionally hits the generic path
  (special cases only for eType 10 / loot-eType-6 / monsters-eType-2,
  `src/ScriptThread.cpp:828-840`). Sprites 7/9/10 carry entity-backed NPC defs
  (art 68 = def(3,1), 66 = (3,0), 72 = (3,4); `tmp_entities.bin` lookup per
  `src/EntityDef.cpp:76-83`), so a reached HIDE would unlink them correctly.
  **Not the problem.**
* **Branch actually taken with vars[14] == 0:** walk of §1 gives — NPCCHAT @3012 runs
  (v14 != 3 true), marine-dialog block skipped (3018), scientist beat skipped (3048),
  **fall-through walker block 3198–3243** (same code v14==3 uses): spr7 offset-walk,
  **spr10 INSTANT (8,20)** @3205, spr10 offset (9,20) @3209, door opened via func@3333,
  bob → (11,19) @3229, **spr7 → (10,19) @3236 and spr10 → (10,19) @3243**, camera/GOTO/
  elevator-close @3248-3257, items given via func@3401 (@3267 — v14 != 3 true),
  **all three HIDE guards false → zero HIDEs executed**, bob INSTANT (21,21) @3318,
  sp22 UNLOCK+CLOSE (blocking) + LOCK @3324-3327, RETURN @3332.
* **Predicted stderr fingerprints for our run** (to distinguish "wrong branch" from
  "thread died"): presence of `[dbg] GOTO raw=0x10D4 …`, `[script] GIVEITEM def=1
  qty=1`, `[script] GIVEITEM def=85 qty=<90|40|10>`, `MESSAGE str=133`,
  `[script] DOOROP sprite=22 unlock+close (blocking)` + `lock`, and
  **complete absence of `[script] HIDE sprite=7|9|10`** lines. The user-reported
  end state (door card-openable ⇒ ev60 path intact, doorway occupied by live spr=10
  type=3) matches full completion of this branch exactly.

---

## 4. Legacy end-state table after EVT 617 completes

Boot pre-positioning (INIT_MAP chain, for reference): leader-of-choice is INSTANTed to
(22,29) — spr7 for v14==1 @2531, spr10 for v14==2 @2545, spr9 for v14==3 @2552 (raw
`04 DC ED 01` / `04 DC AD 02` / `04 DC 6D 02`; canonical
`docs/research/assets/map00_disasm.txt:641,644,646`); the other two park at tiles (8,18)
and (7,19) @2936-2987 (`2936 EVAL[v14==1]` → 9@(8,18),10@(7,19); `2954 EVAL[v14==2]` →
9@(7,19),7@(8,18); `2972 EVAL[v14==3]` → 7@(8,18),10@(7,19);
`map00_disasm.txt:739-750`). For any value ∉ {1,2,3} all three guards fail → **spr9**
takes the final else @2552 → (22,29), and no parking lerps run.

**DEFAULT/new-game-equivalent choice = marine, v14 == 1** (acceptance target):

| sprite | identity (art/def) | after EVT 617 | final tile | citation |
|---|---|---|---|---|
| 7 | art 68, NPC(3,1) — marine's own squad model | **visible**, unmoved by the event (leader sent to (22,29) at boot) | **(22,29)** | @2531 boot; no spr7 op in the v14==1 branch (3086-3139) |
| 9 | art 66, NPC(3,0) | **HIDDEN** (0x10000 + unlinked) mid-walk | gone from (≈9,19)→(10,19) | walk @3100/3127, HIDE @3299 |
| 10 | art 72, NPC(3,4) | **HIDDEN** (0x10000 + unlinked) mid-walk | gone from (≈9,20)→(10,19) | walk @3093/3134, HIDE @3297 |
| 19 | art 69, NPC_BOB | visible, teleported out of the corridor | **(21,21)** | @3318 |
| 203 | grenade prop (NOENTITY) | HIDDEN before door reopens (re-tossed variant only for v14==3, hidden again @3391) | gone | HIDE @3363 (and @3391) |
| 204 | grenade prop (NOENTITY) | already hidden at boot | gone | HIDE @2884 |
| 205 | art 72, NPC(3,4), info 0x00000048 | **untouched by EVT 617 and its callees** (zero ops target it in the full listing §1) — stays at map-load spot | **(1,19)** visible | map table (tools/map_to_obj.py ≡ `src/LoadingManager.cpp:463-552`) |
| 206 | art 72, NPC(3,4), info 0x00020048 | idem | **(1,19)** visible | idem |
| sp22 door | art 16, info 0x0C500010 | UNLOCK→OPEN @3365/3368 then **UNLOCK+CLOSE (blocking) + quiet LOCK** | closed+locked on its tile **(10,19)** | @3324/3327 |

Doorway (10,19) is **clear of every PLAYERSOLID entity** the moment the door
re-solidifies — the invariant the rewrite currently violates.

**Our build's actual (buggy) end state with v14 == 0:** spr7 visible ON (10,19),
spr10 visible ON (10,19) (the logged `moveBlocked … spr=10 type=3`), spr9 visible at
(22,29) (boot's else-branch teleports it there, @2552 — the marine's parking Lerps
@2563-2570/2943-2947 are skipped), bob at (21,21), door closed+locked, 205/206 at
(1,19). Matches the reported runtime behavior in full.

---

## Root cause (one line)

`new_src` never ports `characterChoice`: `ScriptVM::updateScriptVars` pins var14 to 0
(`new_src/domain/game/ScriptVM.cpp:300`) where legacy always supplies 1/2/3
(`src/Game.cpp:3468` via the forced intro select), so EVT 617 runs the shared
else-branch that marches sprites 7+10 onto door tile (10,19) but skips all three
choice-gated HIDE pairs (bytecode 3276-3315), leaving two live NPC entities standing
inside the doorway when sp22 closes and locks.

**Minimal fix:** initialize/refresh `vars[14] = 1` (marine, per
`new_src/domain/game/DialogSystem.cpp:456-457` intent) at
`new_src/domain/game/ScriptVM.cpp:300`. EVT 617 then takes the v14==1 column above:
walkers 9+10 are hidden by the existing (already-faithful) EV_HIDE path, and the
doorway clears. Boot placement of 7/9/10 also self-corrects (2936-2987 branches read
the same var).

## Corrections to prior notes

* `docs/research/2026-08-25-blue-door-passage.md` §Q2 listed the tail as
  "@3324/@3327 … NEXTSTATE ++v37": there is also `++v43` @3322 (var 43), and the
  hide-block span is 3276–3315 (guards) with HIDEs at 3283/3285, 3297/3299,
  3311/3313. Its per-choice hide mapping (v14==3→{10,7}, ==1→{10,9}, ==2→{9,7}) is
  **confirmed exactly**.
* It also stated the doorway walkers exist "only transiently mid-cutscene" — true for
  v14 ∈ {1,2,3}; the fall-through case (impossible in legacy, routine in the rewrite)
  leaves them permanently on the tile. That caveat is now the confirmed root cause.
