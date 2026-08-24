# 2026-08-23 — tileEvents / mapBytecode VM (ScriptThread) deep dive

## Hypothesis

The mapXX.bin tail contains `staticFuncs` (12×u16) → `tileEvents` (N×2×i32) → `mapByteCode`;
`ScriptThread` interprets the bytecode; EV_ITEM_COUNT checks `player->inventory[n]` and
EV_DOOROP unlocks/opens doors via `setLineLocked`, gating the map00 red/blue keycard doors.

## Method

1. Grepped `src/` for `tileEvent|staticFunc|mapByteCode|executeTile|EV_|EVAL_`; read
   `src/ScriptThread.{h,cpp}` in full, `src/Game.cpp` (executeTile/staticFuncs/threads/
   setLineLocked/performDoorEvent/eventFlags/updateScriptVars/save-load), `src/Render.cpp`
   (findEventIndex), `src/LoadingManager.cpp` (map parse), `src/MovementController.cpp`,
   `src/PlayingInputHandler.cpp`, `src/Combat.cpp`, `src/Entity.cpp`, `src/Canvas.cpp`,
   `src/Resource.cpp` (stream primitives), `src/Enums.h` (opcodes/EVFL/SCR/inventory).
2. Wrote a read-only Python parser for `tmp_map00.bin` following
   `src/LoadingManager.cpp:362-556`; cross-checked block boundaries against the actual
   CAFEBABE/DEADBEEF marker positions; disassembled all 10015 bytecode bytes with exact
   operand sizes derived from each case in `ScriptThread::run`.
3. Decoded `tmp_entities.bin` (`src/EntityDef.cpp:31-44`) to identify door defs and
   keycard item defs; correlated EV_DOOROP sprite ids with mapSpriteInfo low bytes and
   sprite coords from the map file.

## Verdict

CONFIRMED (with expansions):
- Layout, addressing, thread lifecycle, pause/resume protocol, trigger filter, opcode
  semantics as documented; EV_ITEM_COUNT→inventory check and EV_DOOROP→setLineLocked
  door flow verified end-to-end on real map00 data.
- SURPRISE #1: real files have NO section marker between nodeChildOffset1/2 while
  `src/LoadingManager.cpp:474` reads one — `tools/map_to_obj.py:593-595` matches the files.
- SURPRISE #2: on first-playthrough map00 the blue key comes from looting an ET_CORPSE
  (lootSet entry inv[20]); the red door is opened by a story script. The door-tile
  TRIGGER scripts (EV_ITEM_COUNT + EV_DOOROP op3/op0) cover players already carrying a
  keycard (inventory persists across maps). Door scripts also re-grant the keycard via
  EV_GIVEITEM def=110/111 (defs resolved by tileIndex).
- SURPRISE #3: EV_DOOROP op values ≥4 occur in real data (bit2 = "quiet"/non-interactive).

## Evidence highlights (full citations in docs/original-code/tile-events-vm.md)

* Data tail order + mapFlags |= 0x40: `src/LoadingManager.cpp:542-552,546-549`.
* Event lookup iterator: `src/Render.cpp:182-209`; entry IP = word0>>16:
  `src/ScriptThread.cpp:156`; trigger filter incl. disable bit19:
  `src/ScriptThread.cpp:75`, mirrored `src/Game.cpp:3316`.
* Thread pool/tick/resume: `src/Game.cpp:3246-3284`, tick gate `src/Game.cpp:3259`,
  external resumes `src/Game.cpp:2985-3005`, `src/DialogSystem.cpp:559-562`,
  `src/MovementController.cpp:164-166`.
* Opcodes: switch `src/ScriptThread.cpp:243-2038`; constants `src/Enums.h:399-497`;
  EVAL grammar `src/ScriptThread.cpp:244-312`; ITEM_COUNT `433-454`; DOOROP `747-780`;
  EVENTOP `808-816`; arg readers `2095-2119`.
* Doors: `setLineLocked` tileNum±bit0 + lookup(tileNum+257): `src/Game.cpp:2477-2498`,
  lookup-by-tileIndex `src/EntityDef.cpp:76-83`; player-door range test b3 [271,281):
  `src/Game.cpp:1054-1058`; performDoorEvent lerp 750ms `src/Game.cpp:1081-1156`.
* Triggers: leave-before-move `src/MovementController.cpp:326-349`, face+enter on arrival
  `160-184`, attack trigger `src/PlayingInputHandler.cpp:395-404`, barrel explosion
  `src/Combat.cpp:372-378`, spawn-tile 4081 `src/LoadingManager.cpp:700-706`.
* map00 specifics (from tmp_map00.bin parse): staticFuncs=[0]=0,[6]=253; red door sprite 65
  @(16,19) low byte 14; blue sprite 22 @(10,19) low byte 16; EVT tile 618 @4161
  ITEM_COUNT inventory[20] → UNLOCK(3)+WAIT300+OPEN(0); EVT tile 624 @4768 same for
  inventory[19]; story unlock @5192 DOOROP sprite=65 op=1; intro lock-out @3324/@3327;
  corpse lootset inv[20] @581.

## Open questions

* Meaning of tileEvents word0 bits 10–15 (no reader in src/).
* Whether shipped .ipa maps contain the childOffset marker that src/ reads (port may rely
  on regenerated assets); tmp_map00.bin does not have it.
* SCR_MONSTER_DEATH / SCR_CHICKEN_KICKED slots have no call sites in this port.
* EV_GIVEITEM's odd "grant keycard again" inside the with-key branch — intentional top-up
  or designer artifact; behavior reproduced faithfully either way.
