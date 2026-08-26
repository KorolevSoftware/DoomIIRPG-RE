# Unhandled script events: imp MAKE_CORPSE remnant & elevator-explosion soldiers (map00)

Date: 2026-08-25 · Status: verdicts below · Method: full CFG-based disassembly of map00
bytecode + cross-check of `new_src/domain/game/ScriptVM.cpp` against
`docs/original-code/tile-events-vm.md` §3 and legacy `src/ScriptThread.cpp`.

## Hypothesis

Visual remnants after corpse conversions (a: standing imp lower-half above the converted
imp corpse; b: elevator-cinematic soldiers stuck as standing legs) are caused by legacy
script ops that our ScriptVM skips/stubs and which would HIDE the source sprites.

## Method / data layout (verified)

* `tmp_map00.bin`: bytecode starts at **file offset 62019**, size 10015 (`staticFuncs`
  marker chain back-walked from the verified `BEBAFECA` markers; byte-equal to the prior
  session's `map00_bc.bin`). All IPs below are bytecode-relative;
  **file_off = IP + 62019**.
* `tileEvents` block at file `0xED07` (167 × 8 B LE pairs); `staticFuncs` values
  `[0]=0`, `[6]=253`, rest 65535 — matches `docs/original-code/tile-events-vm.md` §1.4.
* Decode anchored on known-good IPs (door trigger @4161 reproduces §5.1 exactly; IP 0 is
  the documented `EVAL[v17==0]` new-game gate).
* Entity defs (`tmp_entities.bin`, 190 × 8 B after count): **tileIndex 23 →
  eType 2 (ET_MONSTER), eSubType 3** = imp. Sprites 15, 41, 42, 46, 121, 185, 186, 187
  all carry art tileNum 23 (imp). Sprite 188 carries tileNum 232 = shadow art.
  Media range for tile 23 = `mappings[23]=114 .. mappings[24]=128` → 14 frames (0–13).

## 1. Skipped / stubbed / unimplemented opcode inventory

Cross-check of the `switch` in `new_src/domain/game/ScriptVM.cpp` (lines 326–1221)
against every opcode reachable in map00 bytecode (CFG walk from event IPs +
staticFuncs; no desync-prone linear scan):

### 1.1 UNIMPLEMENTED — hits `default:` at `ScriptVM.cpp:1214-1220`; **kills the thread**
(`t->state = 0; return 1`) so every following op in that script never runs:

| Op | Name | × | map00 sites (IP) | owning entry |
|---|---|---|---|---|
| 60 | EV_DISABLED_WEAPONS | 1 | **2835** | event ip1923 (elevator cinematic) |
| 51 | EV_AIGOAL | 4 | 8465, 8469, 8480, 8484 | event ip8443 |
| 10 | EV_WEAPON_EQUIPPED | 2 | 9041, 9094 | events ip9020 / ip9066 |
| 56 | EV_PLAYERATTACK | 2 | 9050 (spr131 w1), 9103 (spr116 w1) | events ip9020 / ip9066 |
| 97 | EV_SET_CALDEX_RENDER_HACK | 2 | 6371, 6378 | — |
| 84 | EV_START_TARGETPRACTICE | 1 | 8375 | — |
| 80 | EV_START_ARMORREPAIR | 1 | 8397 | — |

Legacy semantics: op 60 = `player->disabledWeapons = s16` (`src/ScriptThread.cpp:1443-1451`);
op 10 = `var[B&0x7F] = ce->weapon` (:477-482); op 56 = scripted attack + `evWait(1)`
(:1361-1373); op 51 = `setAIGoal` (:1262-1274); op 80 pauses into armor UI (:1734-1742);
op 84 target practice (:1806-1813); op 97 render hack flag (:2021-2025).

### 1.2 SKIPPED / stubbed — parsed, args consumed, logged, but no effect

| Op | Name | × in map00 | ScriptVM.cpp |
|---|---|---|---|
| 19 | EV_DAMAGEMONSTER | 1 (@2831!) | :843-848 "skipped (no monsters)" |
| 22 | EV_MONSTERFLAGOP | 33 | :830-835 |
| 28 | EV_WAKEMONSTER | 24 | :837-841 |
| 36 | EV_SETDEATHFUNC | 21 | :823-828 |
| 58 | EV_LERP_FOG | 24 | :1078-1082 |
| 31 | EV_SPAWN_PARTICLES | 22 | :785-794 |
| 38 | EV_NPCCHAT | 20 | :960-964 |
| 34 | EV_NAMEENTITY | 9 | :816-821 |
| 49 | EV_SPEECHBUBBLE | 13 | :953-958 |
| 41 | EV_GIVELOOT | 11 | :966-971 |
| 42/82 | MARKTILE / UNMARKTILE | 37 / 5 | :909-916 |
| 32 | EV_FADEOP | 10 | :796-801 |
| 43 | EV_UPDATEJOURNAL | 6 | :918-923 |
| 92 | EV_ENTITY_BREATHES | 4 | :871-883 (info bit set, no consumer) |
| 71 | EV_JOURNAL_TILE | 2 | :925-931 |
| 53 | EV_MINIGAME | 2 | :1052-1059 |
| 78 | EV_ENABLE_HELP | 4 | :896-899 |
| 57 | EV_SET_FOG_COLOR | 1 | :1072-1076 |
| 72 | EV_MAKE_CORPSE | 5 | :850-866 — **conditional**: silently skipped unless a
monster-family entity is found (`ent->isMonster()`, :858) |

Not present in map00: 20, 25, 47, 70, 73, 74, 93, 85, 86, 87-91 pass-through ops etc.

## 2. Imp conversion chain — decoded

Owner: PER_TURN static func @253 body (also reached behind INIT_MAP@0's
`CALL_FUNC func@1122` at IP 215 and `func@1128` at IP 218). Full sequence:

```
 1091: EVAL[v26==0] iff->1111
 1098:   MAKE_CORPSE spr=41 dst=(14,3)
 1103:   MAKE_CORPSE spr=42 dst=(14,4)
 1108:   JUMP -> 1121
 1111: MAKE_CORPSE spr=41 dst=(4,21)
 1116: MAKE_CORPSE spr=42 dst=(8,18)
 1121: RETURN
 1122: MAKE_CORPSE spr=121 dst=(27,23)     ; separate func (INIT_MAP call @215)
 1127: RETURN
```

There are **no ops between the MAKE_CORPSE calls** — no HIDE, no ENTITY_FRAME, no
LERPSCALE-to-zero. Verdict for symptom (a): **REFUTED at script level**.

What actually hides the source sprite in legacy is intrinsic to
`ScriptThread::corpsifyMonster` (`src/ScriptThread.cpp:2249-2266`):

```cpp
mapSpriteInfo[sprite] = ((mapSpriteInfo[sprite] & 0xFFFE00FF) | 0x7000);   // :2255
```

i.e. the packed anim byte (bits 8–15) becomes `0x70` = MANIM_DEAD. Monsters are drawn by
`Render::renderSpriteAnim` (`src/Render.cpp:1622-1626`: `if (monster != nullptr)
renderSpriteAnim(...)`), whose MANIM_DEAD case emits a single corpse quad, frame 13
(`src/Render.cpp:3466-3475`), resolved as `mappings[tileNum] + 13` = media 127 — inside
tile 23's 14-frame range. No hidden bit (0x10000) is involved here.

The rewrite mirrors this: `Game::corpsifyMonster` performs the same `| 0x7000` write
(`new_src/domain/game/Game.cpp:635`), and `GameContext.cpp` (~line 818-832) routes sprites
whose entity has the died-marker + MANIM_DEAD anim byte into `World3D::drawCharacter`'s
kManimDead single-quad branch (`new_src/render/World3D.cpp:1085-1088`,
`emitPart(13, ...)`).

**Open question (renderer)**: with the script side fully implemented, the residual
"standing lower-half above the corpse" must originate in the draw path, not the VM.
Candidates to test next (each cited):
1. charClass gate misses the sprite → falls to the billboard branch where
   `mediaId >= hi → mediaId = lo` clamps any nonzero frame onto the standing base art
   (`new_src/render/World3D.cpp:639-640`); gate lives in `GameContext.cpp` ~:818-832 and
   requires an entity with `kInfoCorpse` AND anim byte exactly MANIM_DEAD.
2. `defs_->find(ET_CORPSE, eSubType, parm)` failing changes only def identity, not the
   gate — verify with the `[dbg] corpsify` stderr line (`Game.cpp:659-665`).
3. Duplicate leaf membership in `World3D::drawBSP` drawing the sprite twice (legacy
   solves straddlers via `splitSprites`, `src/Render.cpp:894-921`; the rewrite has no
   equivalent).

## 3. Elevator-explosion chain — decoded (event ip1923; camera 1 started @2098)

```
 2639: DOOROP spr=14 OPEN            ; elevator door opens
 2642: DOOROP spr=14 LOCK quiet
 2645: LERPSPRITE spr=16 ->(6,19) 800ms   ; NPC steps into elevator
 2661: CALL_FUNC func@1219 ; 2664: CAMERA_STR str17 3000ms
 2669: LERPSPRITE spr=16 ->(5,19)
 2676: LERPSPRITE spr=15 ->(6,19) 300ms   ; imp 15 approaches
 2681: PLAYSOUND 1021 + SCREEN_SHAKE + SPAWN_PARTICLES
 2699: HIDE spr=16                        ; NPC vanishes
 2701: LERPSPRITE spr=105 ->(5,19)        ; CORPSE PROP (tileIndex 138, eType 9)
 2707-2736: ENTITY_FRAME spr=15 pain/walk frames + PARABOLA knockback into (5,19)
 2749-2770: LERPs move grenade prop 204 through the air
 2777: ENTITY_FRAME spr=19 frame=176      ; NPC talk/back pose
 2791: ENTITY_FRAME spr=15 frame=96       ; pain
 2795: LERPSPRITE spr=15 ->(3,26) instant ; repositioned
 2799-2811: LERPSPRITE spr=185/186/187 ->(5,19), spr=188 offset(353,1248)  ; squad teleports in
 2818/2825: spr=187 again ->(4,18)
 2831: DAMAGEMONSTER spr=15 dmg=127       ← STUBBED in rewrite ("skipped")
 2835: DISABLED_WEAPONS s16               ← UNIMPLEMENTED → THREAD DIES HERE
 ---- everything below never executes in the rewrite ----
 2838-2844: HIDE spr=185, 186, 187, 188
 2846/2857/2880/2908: ENTITY_FRAME spr=19 poses
 2850: LERPOFFSET spr=204 (grenade drop-back)
 2865/2872: CAMERA_STR str18/str19
 2877: NAMEENTITY spr=19 name=20
 2884: HIDE spr=204                       ; grenade prop removal (leftover otherwise!)
 2888-2931: LERPSPRITE/LERPOFFSET spr=19 (+203) walk-out
 2933: NPCCHAT spr=19 param=1
 2936-2987: character-choice squad repositioning (v14 branches, sprites 7/9/10)
 2990: NEXTSTATE ++v37                    ; state var never incremented in rewrite
 2992: RETURN
```

### What legacy does with the never-run ops

* **EV_DAMAGEMONSTER** (`src/ScriptThread.cpp:704-724`): `pain(dmg)`; lethal →
  `died(false, nullptr)` — the normal monster death path, leaving a *visible* corpse
  (frame 0x7000 via `src/Entity.cpp:1627`, no hidden bit). So imp 15 should end as a lying
  corpse at (3,26) — within view of the cinematic camera.
* **EV_HIDE on an eType==2 monster** (`src/ScriptThread.cpp:833-840`):
  `corpsifyMonster(linkIndex % 32, linkIndex / 32, entity, false)` — note it passes **tile
  indices where pixel coords are expected**, i.e. the corpse art state is written while the
  sprite is moved to ≈map origin — then `removeEntity(entity)` re-sets the hidden bit
  (`src/Game.cpp:183-190`) and `info |= 0x400000`. Net legacy effect: **the four squad
  imps VANISH** (they do not remain as visible corpses). The visible corpse in/near the
  elevator is the pre-placed prop sprite 105 (already working in the rewrite).
* **EV_DISABLED_WEAPONS** just stores a bitmask (`src/ScriptThread.cpp:1443-1451`).

### Verdict symptom (b): CONFIRMED (mechanism)

The thread dies at unimplemented op 60 (IP 2835) immediately after the stubbed
DAMAGEMONSTER (IP 2831) and before the four HIDEs (IPs 2838–2844). Consequences in the
rewrite: live standing imps 185–188 stay stacked inside the elevator geometry (upper
bodies occluded → "standing legs"), imp 15 never becomes a corpse, grenade prop 204 is
never removed, NPC 19 gets no name/chat, and `v37` desyncs. This is not a renderer bug;
it is two VM gaps + one stub.

## 4. Prioritized implement list (map00 impact)

| Pri | Item | Where | Effect |
|---|---|---|---|
| P0 | Add `case Enums::EV_DISABLED_WEAPONS:` consuming s16 (store-or-drop) | `new_src/domain/game/ScriptVM.cpp` switch | unblocks entire elevator tail (HIDEs, name/chat, ++v37) |
| P0 | EV_HIDE monster branch: set 0x10000, corpsify-in-place, remove/unlink entity (faithful incl. tile-as-pixel quirk) | `ScriptVM.cpp:492-513` (currently logs "corpseify") | squad imps vanish like legacy |
| P1 | EV_DAMAGEMONSTER: route lethal damage into `Game::corpsifyMonster` (pain anim optional) | `ScriptVM.cpp:843-848` | imp 15 corpse at (3,26) |
| P1 | EV_SPAWN_PARTICLES minimal burst effect | `ScriptVM.cpp:785-794` | explosion flashes @2656/2687/2694/2784 |
| P1 | EV_FADEOP fade in/out | `ScriptVM.cpp:796-801` | scene transitions (@2519 etc.) |
| P2 | NAMEENTITY / NPCCHAT / SPEECHBUBBLE visuals | `ScriptVM.cpp:816-821,953-964` | NPC name tag + chat bubble @2877/@2933 |
| P2 | ops 10, 51, 56, 80, 84, 97 (consume args; implement later as needed) | switch | later-map00 events (target practice @8375, armor repair @8397, scripted attacks @9050/9103, AIGOAL @8465+) |
| P3 | Renderer: trace "standing lower-half" remnant over converted imp corpse (candidates §2) | `new_src/render/World3D.cpp`, `new_src/core/GameContext.cpp` | symptom (a) |

## Open questions

1. Symptom (a) renderer source (§2 candidates); needs `[dbg] corpsify` stderr capture
   (`Game.cpp:659-665`) + user visual confirmation per AGENTS.md.
2. Does `defs_->find(ET_CORPSE, 3, 0)` succeed for imps (def list has tileIndex 23 =
   (2,3,0); an ET_CORPSE twin must exist or the def-swap silently keeps the monster def)?
3. Legacy `EV_HIDE`'s tile-as-pixel coordinate quirk: replicate verbatim or park corpses
   sensibly? (Visible outcome identical: sprite leaves the scene.)

## 5. End-scene visibility audit (post-fix follow-up, 2026-08-25)

Method: re-read every sprite-consumption path in `src/Render.cpp`; decoded map00
sprites/defs directly from `tmp_map00.bin` (parser mirrors `src/LoadingManager.cpp:463-552`;
verified `TE@0xED07`, `bytecode@62019`) and `tmp_entities.bin`
(`src/EntityDef.cpp:36-43`); re-decoded script tail bytes at IP+62019; cross-checked the
current rewrite (`new_src/domain/game/ScriptVM.cpp`, `new_src/render/World3D.cpp`,
`new_src/core/GameContext.cpp`). Curated facts distilled into
`docs/original-code/rendering.md`.

### Q1 — Hidden-bit semantics: CONFIRMED (no bypass path)

Single funnel, checked **before** entity lookup at both gates:
`Render::addSprite` bails on `mapSpriteInfo[n] & 0x10000` before reading
`S_ENT` (`src/Render.cpp:830-837`), so hidden sprites never enter the sorted
view list consumed by `renderBSP` (`src/Render.cpp:1752-1760`);
`renderSpriteObject` re-checks before its own entity lookup
(`src/Render.cpp:1501-1504` vs `:1565`). `renderSpriteAnim` is only reachable
from `renderSpriteObject` (`:1625,:1661`); split sprites re-enter through
`addSprite` (`:917-920`); wall/decal/z-sprites are ordinary map sprites behind
the same gates; `postProcessSprites` never draws (`:2459-2525`); the
mastermind/Caldex delayed buffers only hold already-gated sprites
(`:1519-1542,:1764-1771`). The raw emitter `renderSprite(x,y,z,tile,…)`
(`:426`) takes explicit args and cannot surface a hidden map sprite.
**A hidden sprite cannot be drawn, in legacy or in the rewrite**
(`new_src/render/World3D.cpp:549,:1190,:1225` skip before draw).

### Q2 — Net visibility of each elevator actor

Map-file ground truth (initial packed info / tile / pos):

| spr | art | def | initial | role |
|---|---|---|---|---|
| 14 | 19 orient E\|W | — | (7,19) hid=0 | elevator door |
| 15 | 23 imp | (2,3) | (8,18) hid=0, **has entity** | scripted imp |
| 16 | 71 NPC_CIVILIAN | **(3,3)** | (8,19) hid=0, entity | NPC into elevator |
| 19 | 69 NPC_BOB | **(3,2)** | (9,20) hid=0, entity | talk NPC |
| 105 | 138 OBJ_CORPSE | **(9,17,0)** | (24,15) hid=0, entity | corpse prop |
| 185–187 | 23 imp | none | (3,26) hid=0 **info&0x200000** | decor squad |
| 188 | 232 shadow | none | (3,26) hid=0 info&0x200000 | decor squad |
| 203/204 | 1 / 2 | none | (8,16) info&0x200000 | props |

Key discovery: sprites 185–188 and 203/204 carry `SPRITE_FLAG_NOENTITY`
(0x200000, `src/Enums.h:1237`): map load clears it and skips entity creation
entirely (`src/Game.cpp:398-400`), so their `S_ENT == -1`. Legacy `EV_HIDE`
therefore executes only the hidden-bit write for them
(`src/ScriptThread.cpp:820-823`) — the monster branch (`:836-840`),
`corpsifyMonster` and `removeEntity` never run. §3's corpsify analysis of the
prior session applies only to entity-carrying monsters; net visual outcome is
identical.

* **185–188**: final state = hidden bit set, nothing else. Never drawn again;
  no corpse anywhere. CONFIRMED invisible.
* **16** (ET_NPC): HIDE @2699 → bit set (`:821`), `info|=0x400000`,
  `unlinkEntity` (`:825-826`); eType 3 matches no special branch
  (`:828/:833/:836`) → vanishes mid-scene (replaced by prop 105). Invisible.
* **204** (NOENTITY grenade): bit set @2884 → invisible. (Prop **203 stays
  visible** and follows NPC 19's walk-out LERPOFFSETs.)
* **15** (imp, has entity): `EV_DAMAGEMONSTER` sets `info|=0x20000`
  (`src/ScriptThread.cpp:712`) which un-gates `Entity::pain`
  (`src/Entity.cpp:286-288`); lethal → `died(false,nullptr)` (`:713-716`).
  ET_MONSTER branch writes `n3=((n3&0xFFFF00FF)|0x7000)` **in place**
  (`src/Entity.cpp:463`) — position stays (3,26) where LERPSPRITE @2795 put it
  (`dst = 32+(tile<<6)`, `src/ScriptThread.cpp:361-362`); not hidden → corpse
  markers + pile trim (`src/Entity.cpp:469-470`); def swap `find(9,…)`
  (`src/Entity.cpp:501`). Rendered as a **lying imp corpse quad frame 13**
  (= media mappings[23]+13 = 127, inside tile 23's 14-frame range) via the
  MANIM_DEAD case (`src/Render.cpp:3470-3475`). Visible from anywhere that
  sees tile (3,26).

### Q3 — What lies in the elevator in the original end scene

* **Sprite 105 is the remains.** Art = `TILENUM_OBJ_CORPSE` (138,
  `src/Enums.h:754`), a single-frame media (mappings[138]=738, range 737→738)
  whose only image is a lying human corpse; lerped to tile (5,19) by
  LERPSPRITE @2701 (bytecode `[04 3C 4B 1A …]` decodes spr=105 dst=(5,19),
  verified). Drawn as one billboard frame 0 (`src/Render.cpp:1728`) plus the
  loot-pile under-quad when not in ST_CAMERA and lootable
  (`src/Render.cpp:1714-1717`); sort bias +2 for tiles 137–139
  (`src/Render.cpp:866-868`). It cannot render standing — its art range has
  exactly one frame.
* **Player position after the intro:** `Game::spawnPlayer` runs on every map
  load (`src/LoadingManager.cpp:673`); map00 header gives
  `mapSpawnIndex=612, mapSpawnDir=0` → spawn tile `(612%32, 612/32)` =
  **(4,19)** facing angle 0 (`src/Game.cpp:953-968`). The player therefore
  stands one tile west of the elevator cab, looking east at tile (5,19).
* **NPC 19 does NOT leave the scene:** the tail LERPs him out to ≈(9,19)
  (@2888) and then **back to (5,19)** (@2923, bytecode `[04 3B CB 04 …]` →
  spr=19 dst=(5,19)) — he ends standing over/n beside the corpse, with prop
  203 beside him, then gets NAMEENTITY @2877 / NPCCHAT @2933. So the original
  end state legitimately contains one standing figure (full marine NPC_BOB
  art, multi-part stacked render via the ET_NPC branch,
  `src/Render.cpp:1660-1661`).
* Complete expected visible set in tiles x=3..7, y=17..21 after RETURN @2992:
  * **(5,19)**: corpse 105 lying (media 738) + NPC 19 standing (tile 69,
    frames 0–8) + small prop 203 (tile 1);
  * static decor only elsewhere (tile numbers from `map00.bin`, positions
    pre-cinematic): animated props 136 @(4,18),(4,20) (auto-animate set at
    `src/Game.cpp:394-397`); crates 152 @(6,22),(7,18); pipes 180
    @(3,21),(4,21),(5,18); props 133 @(4,17),(6,17),(8,18); door 14 @(7,19);
    201 @(5,17); 85 @(3,17); plus out-of-window neighbors at x=2 / y=22;
  * **not present**: squad imps 185–188 and grenade 204 (hidden), NPC 16
    (hidden @2699);
  * outside the window: imp 15 as a **lying corpse** at (3,26) (moved there
    @2795, killed @2831).

### Q4 — Why our build could still show standing figures

The rewrite gates match legacy (`World3D.cpp:549/:1190/:1225` skip hidden
before draw), and current ScriptVM coverage of IP 2799–2933 contains **no
thread killer and no permanent parker**: EV_HIDE (:492-521, incl. faithful
NOENTITY/no-entity fallthrough), WAIT (:558), ENTITY_FRAME (:569),
LERPSPRITE/LERPOFFSET (:613/:660, park via `unpauseTime=-1` + completion
resume), CAMERA_STR (:729), PLAYSOUND (:780), SPAWN_PARTICLES (:793),
NAMEENTITY (:824), DAMAGEMONSTER (:851, graceful when entity missing),
DISABLED_WEAPONS (:871), SCREEN_SHAKE (:966), NPCCHAT (:993),
NEXTSTATE (:529). Only a genuinely unknown op hits the killing default
(:1247-1253) — none occurs in this window. With the HIDEs executed, no
remaining sprite can render as standing legs:

1. Hidden sprites are unreachable by the renderer (Q1).
2. Sprite 105 cannot stand (single-frame lying art, Q3).
3. Imp 15 routes to the single-quad DEAD branch (anim byte 0x70 →
   `World3D.cpp:610-611` hack + charClass gate `GameContext.cpp:970-980`
   [line refs updated 2026-08-25 after loot-dwell diff shifted lines];
   emits `emitPart(13,…)` only, `World3D.cpp:1093-1096`).
4. Billboard fallback cannot manufacture legs: out-of-range frames clamp onto
   the base art (`World3D.cpp:648`), which for NPCs/imps is a full standing
   figure, not lower halves.

Ranked candidate culprits for a residual "standing legs" report, each with
its proving stderr line:

1. **Stale binary / fix not actually running** (most likely). The four HIDEs
   log NOTHING for NOENTITY sprites (`ScriptVM.cpp:492-520` logs only inside
   entity branches), so prove execution via the surrounding ops instead:
   `[script] DAMAGEMONSTER sprite=15 dmg=127` (:854) must be followed by
   `[script] DISABLED_WEAPONS mask=971 consumed` (:879) — if DAMAGEMONSTER is
   the last line, the thread still died at IP 2835. Then
   `[script] ENTITY_FRAME sprite=19 frame=176` (:576) proves the HIDE block
   passed; absence of any `[script] UNIMPLEMENTED opcode … IP=` line
   (:1251) rules out a new killer.
2. **NPC 19 standing at (5,19) misread as a remnant** — this is faithful
   original behavior (Q3); verify `[dbg] LERPSPRITE spr=19 … dst=352,1248`
   (:647) appears twice (out to (9,19) and back to (5,19)). If his upper
   body is geometry-occluded, only legs show — same would happen in legacy.
3. **Imp-corpse misplacement**: `[dbg] corpsify spr=15 tile=3,26 … anim=0x7000`
   (`new_src/domain/game/Game.cpp:661-665`) must show anim 0xX7000; if a
   future change ever leaves anim byte ≠ 0x70 on an imp-family sprite while
   visible, the billboard clamp draws full standing imps (the pre-fix look).
4. Renderer leak of a hidden sprite: ruled out structurally (Q1); only a
   regression in the three skip sites could reintroduce it.

### Verdict summary

* Q1 CONFIRMED — one funnel, pre-entity-lookup, no bypass.
* Q2 PARTIAL→CORRECTED: 185–188/204 hide via the bare hidden-bit write only
  (NOENTITY, `src/Game.cpp:398-400`) — prior session's corpsify path doesn't
  apply to them; 16 hides plainly; 15 becomes a visible lying corpse at
  (3,26); 105 is the elevator remains; **203 and NPC 19 remain visible by
  design**, NPC 19 ending back at (5,19).
* Q3 CONFIRMED — original end scene = lying corpse prop (media 738) +
  standing NPC 19 (+prop 203) at (5,19), viewed from player spawn (4,19)
  facing angle 0 (`src/Game.cpp:953-968`); no standing monsters.
* Q4 REFUTED as renderer bug — post-fix ScriptVM cannot abort in this window
  and the rewrite cannot draw hidden sprites; residual sightings are stale
  build (check `[script] DISABLED_WEAPONS mask=971 consumed`) or the
  legitimate standing NPC 19.

