# 2026-08-25 — Blue door sp22 @(10,19): can anything keep the player out? (event lifecycle / intro positions / solidity audit)

Tasking: three questions about the legacy `src/` behavior behind "дверь не пускает"
on repeat approaches, plus a discrimination plan for `new_src`.
Read-only research; no code changed. Method: full CFG disassembly of map00
bytecode (decoder mirrors `src/ScriptThread.cpp` arg consumption exactly;
zero decode errors over all 167 event entries + staticFuncs 0/253), plus
line-level verification of every cited mechanism.

**Bytecode data provenance**: `tmp_map00.bin`, bytecode at file offset **62019**
(re-verified by marker walk: `tileEvents` block `0xED07`, 167×8 B, ends 62015,
followed by `BEBAFECA`; `staticFuncs=[0]=0,[6]=253,rest 65535` at `0xECEB`).
All IPs below are bytecode-relative; `file_off = IP + 62019`.

---

## Q1 — Event spent/disable lifecycle: verdict CONFIRMED (one-shot by self-DISABLE)

### Q1.1 The only per-event state is word1 bit19

* Dispatch filter (`src/ScriptThread.cpp:75`): an event runs iff
  `(word1 & 0x80000) == 0 && …`. There is **no "fired" counter, no per-event
  runtime record anywhere** — `executeTile` (`src/ScriptThread.cpp:59-89`) is
  stateless apart from that bit. Same predicate in `queueTile`
  (`src/ScriptThread.cpp:105`).
* `EV_EVENTOP` (`src/ScriptThread.cpp:808-816`) is the only writer:
  `arg bit15` is the **new value** of bit19 — `0x8xxx` = DISABLE, `0x0xxx` =
  ENABLE (`tileEvents[n*2+1] = (old & 0xFFF7FFFF) | bit15<<19`, :813).
  ⚠ Note: `docs/original-code/tile-events-vm.md` §1.2/§5.1 describe the
  semantics correctly but several prior session notes read the polarity
  backwards; raw args quoted below settle it.
* Persistence: save packs bit19 of every event into an i32 bitmask
  (`src/Game.cpp:1683-1693`); load restores it
  (`src/Game.cpp:1841-1849`). Spent state survives save/load.

### Q1.2 EVT 617 disables ITSELF as its first instruction

Event table (decoded from `tmp_map00.bin`):

```
event[54] tile=617 (9,19) ip=2993 w1=0xFF4  TRIGGER+all-dirs   ← THE story trigger
event[55] tile=617 (9,19) ip=3687 w1=0x048  FACE+N             (ship-landing cinematic)
event[56] tile=618 (10,19) ip=4223 w1=0xFF1 ENTER+all-dirs     first-entry cinematic
event[59] tile=618 (10,19) ip=8388 w1=0x080FF2 TRIGGER         DISABLED IN MAP DATA
event[60] tile=618 (10,19) ip=4161 w1=0xFF4  TRIGGER+all-dirs  ← blue-keycard use
```

Head of event[54] (bytes at 2993: `17 80 36 17 80 29 17 80 33`):

```
2993: EVENTOP ev[54] DISABLE (arg=0x8036)   ← self-disable; one-shot guard
2996: EVENTOP ev[41] DISABLE (arg=0x8029)
2999: EVENTOP ev[51] DISABLE (arg=0x8033)
3002: DIALOG str(21) type=1
3005: EVAL [v14 != 3] iffalse -> 3018       ← characterChoice branches only
3018: EVAL [v14 == 1] iffalse -> 3033
3048: EVAL [v14 == 3] iffalse -> 3086
3086: EVAL [v14 == 1] iffalse -> 3142
3142: EVAL [v14 == 2] iffalse -> 3198
```

There is **no EVAL var-guard needed at entry — the self-DISABLE *is* the
guard**. The dispatcher checks bit19 *before* `alloc`
(`src/ScriptThread.cpp:68-83`), so the running instance completes but any
later trigger finds the bit set and skips. Full-CFG EVENTOP scan over ALL
reachable map00 code: the only ops ever touching events 54/41/51 are these
three DISABLEs. Nothing re-enables them (the big INIT_MAP arm-block at
IP 1619-1874 targets other events exclusively).

The same self-disable idiom heads the other door scripts:
`@4172 EVENTOP ev[60] DISABLE (0x803C)` inside the keycard success path,
`@4223 ev[56] DISABLE (0x8038)` in the first-enter cinematic,
`@3452 ev[52] DISABLE`, `@4698 ev[62] DISABLE (self)`.

### Q1.3 Correction: @3324 is UNLOCK+**CLOSE**, not UNLOCK+OPEN

`EV_DOOROP` passes its action straight through:
`n44==1 → setLineLocked(false)` then `performDoorEvent(n44, …)`
(`src/ScriptThread.cpp:756-763`). In `performDoorEvent`, **n==1 is CLOSE**
(dstScale→64, `door_close` sound 1028, `LS_FLAG_DOORCLOSE`,
`src/Game.cpp:1130-1137`; slide reversal :1097-1100; close-noop guard
"already closed" :1066-1068), n==0 is OPEN (:1139-1144). So:

```
3324: DOOROP spr=22 args=0x0416  act=1 UNLOCK+CLOSE  NON-quiet (interactive/blocking)
3327: DOOROP spr=22 args=0x1816  act=2 LOCK +quiet  (def swap only)
3330: NEXTSTATE ++v37
```

Corroborating usage of the same "unlock+close behind you" idiom:
`@1909 DOOROP spr=96 act=1+quiet` (after `@1904 act=0 OPEN`),
`@3257 DOOROP spr=14 act=1+quiet` (elevator door closed after the squad
walked in; that door was opened `@2639 act=0` and quietly locked `@2642
act=6`). The label "op=1 UNLOCK+OPEN" in `docs/original-code/tile-events-vm.md`
§3.3 and the "@3324 (NPC steps out)" reading in
`docs/original-code/tile-events-vm.md` §5.1 /
`docs/research/2026-08-25-blue-door-regression.md` are **wrong**; see §Q2 for
what actually steps where.

Timing subtlety: because @3324 is non-quiet, the thread parks
(`unpauseTime=-1`, `src/ScriptThread.cpp:760-762`) until the 750 ms close
finishes; **@3327 executes only after the door is fully closed**, so the
quiet LOCK lands on a CLOSED door (see Q3.3).

### Q1.4 Verdicts

* **Can the tail @3324/@3327 run more than once ever?** NO. It runs exactly
  once per game-lineage, inside the single execution of event[54]
  (self-disabled at :2993 before anything else; disable bit persisted,
  §Q1.1).
* **Does it execute in a normal fresh playthrough BEFORE the first card
  use?** YES — necessarily. Event[54] is itself the scripted *first opener*
  (`@3365 DOOROP spr=22 act=3 UNLOCK`, `@3368 act=0 OPEN` blocking) and
  re-locker; the keycard trigger event[60] can only succeed once
  `inventory[20]>0` (blue card looted from corpse sprite 29), and it too is
  one-shot (@4172). After both scripts are spent, every subsequent open of an
  unlocked sp22 goes through the plain native path
  (`src/PlayingInputHandler.cpp:451-452`: `performDoorEvent(0,…)` +
  `advanceTurn()`), which no script ever closes again.
  Edge order-inversion (card used before ever aiming at tile 617) changes
  nothing: event[54] still fires at most once, and its `@3368` open on an
  already-open door just re-registers auto-close and does not park
  (`n==0 && b2 && b3` → `updatePlayerDoors(true); return false`,
  `src/Game.cpp:1062-1065`; non-zero return required to park,
  `src/ScriptThread.cpp:760`).

---

## Q2 — Who stands where after the intro: verdict REFUTED (nobody legit rests in the doorway)

Full decoded chains: INIT_MAP@0 → `CALL_FUNC 1913` → 1942 (camera 0) → 2084
(camera 1) → 2328 (camera 5, RETURN @2992); later event[54]. All
LERPSPRITE/LERPOFFSET destinations below are verbatim from the CFG listing
(LERPSPRITE dst = tile units; LERPOFFSET dst = absolute canvas units,
tile = coord>>6).

### Boot intro final positions (when control returns)

| Sprite | Identity | Final placement (IP) |
|---|---|---|
| 19 | npc_bob | walks `(104,383)→(104,575)` @2010; INSTANT tile (9,20) @2063; scene arcs; walks to (9,19) @2888 (async 1700 ms); final offset dst=`(608,1328)` @2924 → rests at/link-tile **(9,20)** — south-west shoulder, NOT the doorway |
| 46 | type-23 NPC | INSTANT tile (14,16) @2073 |
| 7/9/10 | squad majors | instant col-2 placements @2346-2415; walk to `(288,1215)/(288,1281)/(272,1248)` ≈ tiles (4,19)-(4,20) @2439-2453; leader-of-choice INSTANT **(22,29)** @2531/2545/2552; boot tail parks the other two at tiles **(8,18)** and **(7,19)** @2943-2983 |
| 15 | imp | hops, DMG_MON dmg=127 @2831 (legacy: lethal → corpse), final INSTANT (5,19) @2834 (brief (3,26) visit @2795) |
| 16 | elevator NPC | (6,19) @2645 → (5,19) @2669 → **HIDE** @2699 |
| 105 | corpse prop | INSTANT (5,19) @2701 |
| 185/186/187/188 | squad imps + shadow | INSTANT (5,19) @2799-2807 / offset (353,1248) @2811; 187 hop (4,18) @2825; **ALL HIDDEN @2838-2844** |
| 203/204 | grenade props | arcs near (9,20)/(6,19); **HIDE** @2884 |

### During event[54] (story trigger)

* Two choice-matching squad sprites LERP **onto the door tile (10,19)**
  (@3127/3134, or 3183/3190, or 3236/3243) and bob 19 walks THROUGH to
  **(11,19)** (@3120/3176/3229) — while the door is open (@3365/@3368 ran
  moments earlier inside `CALL_FUNC 3333`).
* Player is teleported out of the way: `GOTO (6,20)` @3252 (instant, no anim
  bits), camera 7 @3250, `ADV_CAMERAKEY 4` @3255 — input blocked in
  ST_CAMERA throughout.
* Before control returns: **both doorway walkers are HIDDEN per choice**
  (@3276-3301: v14==3 hides 10+7 keeps 9; v14==1 hides 10+9 keeps 7; v14==2
  hides 9+7 keeps 10). `EV_HIDE` sets `mapSpriteInfo |= 0x10000` AND
  `unlinkEntity(entity)` for any live entity (`src/ScriptThread.cpp:821-826`)
  — i.e. they stop existing as obstacles. Grenade prop re-run: INSTANT onto
  (9,19) @3375, PARABOLA → (8,19) @3384, HIDE @3391.
* `@3318 LERPSPRITE spr=19 dst=(21,21) flags=12` — bob INSTANTly teleported
  to (21,21) **before** the door closes @3324 (so the close's monster-block
  check `src/Game.cpp:1070-1075` passes).

### Verdict

**No PLAYERSOLID entity legitimately rests on (9,19), (10,19) or (11,19) in
legacy once input returns.** The doorway occupants exist only transiently
mid-cutscene. Adjacent parkers after boot (NPCs at (7,19)+(8,18), bob at
(9,20)) can obstruct approach corridors but never the doorway itself, and
event[54]'s own cinematics pull them through/away.

Rewrite-relevant caveat: if the rewrite's VM dies mid-event[54] **between**
the doorway lerps (@3120-3243) and the HIDEs (@3276-3301) — e.g. at
STARTCINEMATIC/GOTO/ADV_CAMERAKEY — the squad stays linked on (10,19) with
the door open forever. That failure mode demonstrably exists in this port:
the elevator event ip1923 used to die at unimplemented op 60 @2835
(`docs/research/2026-08-25-unhandled-script-events.md` §3).

---

## Q3 — Solidity cross-check while sp22 is OPEN: verdict CONFIRMED (entityDb link is the only gate)

1. **Link state is everything.** The movement trace visits `entityDb` lists
   only (`src/Game.cpp:216-218`), filters by mask bit
   `1 << def->eType` (:221), and shapes oriented doors as a ±32 segment
   through the current sprite position (:249-268). Commit requires zero hits
   (`src/MovementController.cpp:332-333`). **No collision consumer of
   DOORLERP (0x80000000) or scale exists anywhere** — that bit is read only
   by the renderer (`docs/original-code/doors.md` §4) and by
   `freeLerpSprite` bookkeeping.
2. **Timeline.** Open start: stays linked + `ENT_NORELINK|S_NORELINK`
   (`src/Game.cpp:1129`) → solid through the whole 750 ms. Open end:
   `freeLerpSprite` `LS_FLAG_DOOROPEN` → `unlinkEntity`
   (`src/Game.cpp:3127-3131`) → passable. Close start: `b2 && b3` →
   monster-block probe then `linkEntity` immediately
   (`src/Game.cpp:1069-1079`) → solid same tick. `linkEntity` sets
   `info |= 0x100000` (`src/Game.cpp:118`); unlink clears it (:92).
3. **Quiet LOCK (act=2/6) on an ALREADY OPEN door**: `setLineLocked(true)`
   ONLY — flips bit0 of the sprite-info low byte and re-looks-up the def
   (`src/Game.cpp:2477-2498`); invoked at `src/ScriptThread.cpp:765-769`
   with no `performDoorEvent` call, no animation, no relink. Result:
   **lock-while-open = door stays visually open AND passable**; it is merely
   marked `eSubType==1`, which makes `performDoorEvent` refuse *every*
   further event — including auto-close and monster-open
   (`src/Game.cpp:1059-1061`) — until something unlocks it. It neither snaps
   closed nor turns solid. ⇒ Hypothesis A's "quiet LOCK" half cannot produce
   a solid-open door in legacy. The only thing that instantly solidifies an
   open door is a CLOSE (act=1 or auto-close) via the :1079 relink.
4. **Auto-close** cannot close sp22 while the player stands on the door tile
   or its X-neighbours: `advanceTurn` loop (`src/Game.cpp:1271-1278`) →
   `CanCloseDoor` (`src/Game.cpp:1215-1236`); sp22 info `0x0C500010` has
   `0xC000000` (vertical) so the ±X neighbours are tested (:1229-1235).
5. **Line flags / heightMap**: world BSP lines are static geometry read only
   by `traceWorld` (`src/Render.cpp:1212-1283`; flag rules per
   `docs/original-code/player-collision.md` §3.2 — a flag-5 line would block
   the player even with the door open, but none exists across this doorway,
   else the scripted open could never let anyone through);
   collision is strictly 2-D with unconditional
   `destZ = 36 + getHeight` (`src/MovementController.cpp:341-344`) — height
   can never gate passage.

⇒ Confirmed: **the only passage gate is the entityDb link**, cleared at open
end (`src/Game.cpp:3131`), restored at close start (:1079).

---

## Q4 — Discrimination plan (new_src)

Legacy truth reframes the two candidates:

* **A′ (most likely): event[54] RE-RUNS in the rewrite** because the
  self-DISABLE write (@2993 arg 0x8036), its persistence, or the dispatch
  bit-test is not honored. Every use/approach toward tile (9,19) then replays
  dialog str(21)+cinematic, and its tail `act=1` **closes** + `act=6` locks
  whatever state the door is in — including the card-opened door. Symptom
  match: "дверь задъехалась".
  *stderr proof*: after a successful card-open, a later approach logs fresh
  `[script] DOOROP … sprite=22 …` lines — specifically the close+lock pair —
  plus repeated `DIALOG str(21)`/camera restart. Add owner-thread IP to the
  DOOROP log line (one fprintf, as proposed in
  `docs/research/2026-08-25-blue-door-regression.md`) and IPs ≈3324/3327 name
  the culprit directly; absence of an `EVENTOP` effect log for ev[54] would
  pinpoint the disabled-bit gap.
* **B′: event[54] half-ran once and died between the doorway lerps and the
  HIDEs** (§Q2 caveat), leaving squad sprites linked on (10,19) with the door
  open.
  *stderr proof*: `[dbg] moveBlocked … by spr=7|9|10 type=<ET_NPC>`
  (`new_src/core/GameContext.cpp:669-676` area) while NO `[dbg]
  doorEvent`/DOOROP lines fire and the door renders open.
* **C (not a bug): legitimate auto-close** after the player leaves the
  X-neighbourhood — distinguishable by timing (only fires ≥1 tile away) and
  by the absence of any scripted dialog/DOOROP lines.

### What the user should do in-game (capture full stderr throughout)

1. Walk east along row 19 and stop ON (9,19) facing E at the blue door.
2. Press Use **once** toward the door. If dialog str(21)/a cinematic starts,
   event[54] is re-running ⇒ A′ confirmed visually; capture the following
   `[script] DOOROP` lines.
3. If the door simply opens: press Use again / try stepping E into (10,19).
   Capture any `[dbg] moveBlocked …` line — the named spr/type decides B′.
4. Step back west ≥2 tiles (to ~(7,19)), wait a beat, return and repeat —
   capture every `[script] DOOROP`, `[dbg] doorEvent`, `[dbg] moveBlocked`
   line. Auto-close (C) shows a close without any DOOROP/dialog activity.

Lines to grep from the log: `DOOROP`, `doorEvent`, `moveBlocked`,
`setLineLocked`.

---

## Summary of corrections vs prior docs

1. `EV_DOOROP` act=1 is UNLOCK+**CLOSE** (performDoorEvent n=1 = close),
   not UNLOCK+OPEN — affects `docs/original-code/tile-events-vm.md` §3.3/§5.1
   and `docs/research/2026-08-25-blue-door-regression.md` timeline step 3.
2. EVT 617 (event[54]) is strictly one-shot: it DISABLES itself (+ev[41],
   +ev[51]) at IP 2993-2999 before doing anything; nothing re-enables it.
   Hypothesis A can therefore only fire in the rewrite if the disable bit is
   lost/ignored.
3. The @3324 close happens while the squad is being hidden/teleported away —
   in legacy nobody stands in the doorway when it re-solidifies.
