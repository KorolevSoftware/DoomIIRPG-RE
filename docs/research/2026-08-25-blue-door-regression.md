# 2026-08-25 — Blue door sp22 @(10,19) regression: opens, then impassable / "задъехалась"

## Hypothesis (tasking)

After recent changes (nextKey/resume desync fixes, fps lock, Game::update hoist,
LERPSPRITE pool, NPC solidity, DOOROP frame-bit flip, renderMode blending), the
keycard-unlocked blue door slides open but the player cannot pass; user reports
the door may re-close ("дверь задъехалась") or stay solid.

## Method

Read `new_src` EV_DOOROP handler, Game::performDoorEvent/updateDoors/canCloseDoor/
unlinkDoor/advanceTurnDoors, GameContext tick ordering, resume paths; compared
against `src/Game.cpp` / `src/ScriptThread.cpp`; cross-checked recorded map data
(docs/original-code/tile-events-vm.md §5.1) and journal history. No live repro
(boot logs in build_new/new_src contain no door interaction).

## Verdict: PARTIAL

Auto-close / unlink / resume machinery verified correct for THIS door. Most
likely cause is **script-driven**: the story-trigger script tail executes
`DOOROP op=1` (close) + `op=6` (quiet lock) after the open — exactly one of two
candidate mechanisms below; runtime `[dbg]` log needed to pick between them.

## Verified timeline of the scripted open

1. E at (9,19) facing east → Use → `executeTile(faced=(10,19), flagForFacingDir(4))`
   (`new_src/core/GameContext.cpp:696-699`). With-key event @4161 runs
   (docs/original-code/tile-events-vm.md:366-379): EVENTOP disable event[60] →
   GIVEITEM top-up → `DOOROP op=3` UNLOCK → WAIT 300 → `DOOROP op=0` OPEN blocking.
2. UNLOCK: `setLineLocked` flips low byte 16→17 only
   (`new_src/domain/game/Game.cpp:580-590`) — def swap 273→274.
3. OPEN: `performDoorEvent(0,…)` (`new_src/domain/game/Game.cpp:204-315`);
   sp22 parm&1 ⇒ no slide, scale-only 64→0 over 750 ms (:270-285);
   `registerOpenDoor` (:293); frame bits `|=0x100` (:297-299) plus redundant
   ScriptVM flip (`new_src/domain/game/ScriptVM.cpp:456-464`); owner thread
   parked `unpauseTime=-1` (ScriptVM.cpp:465-468).
4. Use consumed the turn: `advanceTurn()` fires immediately
   (`GameContext.cpp:700-701`) → `advanceTurnDoors()` (`Game.cpp:599`): sp22 is
   registered BUT `canCloseDoor` is FALSE — see §axis. No close.
5. Completion in `Game::update`→`updateDoors`, ticked for Playing/Camera/Dialog
   (`GameContext.cpp:166-176`): `unlinkDoor` (`Game.cpp:1095`) → passable;
   single `resumeThread(owner)` (:1110-1115).

### Axis test vs legacy (task 3) — CORRECT for this door

- sp22 `mapSpriteInfo = 0x0C500010` (docs/original-code/tile-events-vm.md:353):
  has `0xC000000`, not `0x3000000`.
- Ours checks X-neighbors `(cx±64,cy)` in that case
  (`new_src/domain/game/Game.cpp:364-366`) ≡ legacy
  `src/Game.cpp:1229-1235`. Player west at (9,19) occupies `(cx-64,cy)`
  (`playerX_>>6==9`; `setPlayerPos(viewX,viewY)` every tick,
  `GameContext.cpp:153`) → auto-close BLOCKED. Auto-close is NOT the closer.

### Frame-bit flip (task 2) — HARMLESS

Bits 8-15 are read only by media/frame consumers; orientation bits 24-27 are
preserved by all `& 0xFFFF00FF` writers (ScriptVM.cpp:462, Game.cpp:298);
`setLineLocked` touches only the low byte (Game.cpp:585-587); no
solidity-relevant reader of bits 8-15 exists; `getLineLocked` does not exist in
`new_src`. Load-time `frameCount<<8 | 0x80000` (MapParser) likewise has no
collision consumer.

### Re-close / re-link vectors enumerated (task 1)

- `advanceTurnDoors`: every `advanceTurn` (queue flush `GameContext.cpp:276-279`,
  use :701, loot stand-up) — blocked while adjacent (above).
- Script `DOOROP act=1` close / `act=2` lock: quiet ops SNAP instantly
  (`snapMode=0` → `performDoorEvent` sets t=dur and calls updateDoors same tick,
  `Game.cpp:308-311`; close-start relink :237-239 → SOLID IMMEDIATELY).
- DoorAnim slots reuse own slot / free-unregistered only, never steal
  (Game.cpp:247-259) — no cross-door steal.
- `flushParkedThreads`/`resumeKeyWaits` drain ONLY `cameraResumeList_`
  (cinematic ADV_CAMERAKEY parks, `GameContext.cpp:669-694`) — a door-blocked
  thread is never listed there; no double-resume path found.
- SpriteLerp completion relinks LINKED entities to the dst tile
  (`Game.cpp:1049-1053`; per-tick :1027-1033) — relevant to candidate B.

## Most likely causes (ranked)

A. **Story-trigger tail closes + re-locks the door.** EVT tile 617 (9,19)
   TRIGGER-all-dirs (the long intro cinematic: NPC lerps 7/10/19, camera,
   dialog) ends with **@3324 `op=1` CLOSE** and **@3327 `op=6` quiet LOCK** —
   "quietly re-locked" (docs/original-code/tile-events-vm.md:362-364). A quiet
   close snaps solid the same tick. Earlier sessions could not reach this tail:
   journal records the intro VM dying at unimplemented opcodes
   (docs/journal.md:114-117 era); dialogs-v2/cinematics/lerp/resume commits
   (51b2b76, eac8563) made the full script runnable — so the tail that always
   existed now fires. If tile-617's disable bit is not honored (or dispatch
   re-runs standing-tile TRIGGER events), every approach/E re-runs it.
B. **NPCs 7/10/19 parked in the doorway post-lerp.** Lerp completion keeps
   them LINKED at their dst tile (Game.cpp:1049-1053); ET_NPC is playersolid
   (`CONTENTS_PLAYERSOLID=13501` bit3, `new_src/domain/game/Enums.h:31` ≡
   `src/MovementController.cpp:326`) → door visually open, passage blocked.
   Matches the "stays solid" half of the report; existing
   `[dbg] moveBlocked … by spr=%d type=%d` print (`GameContext.cpp:669-676`)
   will name the blocker instantly.

## Distinguishing evidence to collect (user/log)

- `[dbg] doorEvent spr=22 n=1 …` / `[script] DOOROP sprite=22 unlock+close` /
  `setLineLocked -> locked` right after the open ⇒ candidate A (add thread/IP
  to the DOOROP log line to name the script).
- `[dbg] moveBlocked … type=3 (ET_NPC)` with door still open ⇒ candidate B.

## Proposed minimal fix

1. Instrument `EV_DOOROP` act==1/act==2 with the owning thread's IP/static-func
   id (one fprintf) and reproduce once — confirms A vs B from stderr alone.
2. If A: honor the tile-event disable bit for the already-spent story trigger
   (EVT 617) so its @3324/@3327 tail cannot re-fire post-intro; do NOT change
   door code — the door path is faithful.
3. If B: correct the NPC lerp destinations (they must end off the doorway
   tiles) rather than weakening NPC solidity (legacy mask blocks bit3 too).
4. Latent port gap worth fixing separately: `canCloseDoor` uses
   `else if (info & 0xC000000)` (`Game.cpp:364`) — legacy checks X-neighbors
   UNCONDITIONALLY when `0x3000000` is clear (`src/Game.cpp:1229-1231`); an
   orientation-less door would never be neighbor-guarded in the rewrite.

## Open questions

- Exact event index/disable state of EVT 617 at the time of the keycard use
  (needs one runtime log or an IPA bytecode dump around @3300-3370).
- Whether our Use/move dispatch ever executes standing-tile TRIGGER-all-dirs
  events (legacy predicate §4.5, tile-events-vm.md:419) — decides how A fires.
