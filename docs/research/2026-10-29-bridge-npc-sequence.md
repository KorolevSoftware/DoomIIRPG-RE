# 2026-10-29 — map00 bridge crossing with NPC Bob (sprite 19) never plays

## Hypothesis

User report (rewrite): on map00 the scripted "cross the bridge together with the NPC" sequence does not start and the level cannot be finished. Suspects: skipped opcodes (NPCCHAT, NAMEENTITY, SPEECHBUBBLE, UPDATEJOURNAL, JOURNAL_TILE, ENABLE_HELP, SPAWN_PARTICLES, LERP_FOG), EV_SETDEATHFUNC, or an unported precondition.

## Method

- Extracted `map00.bin`..`map09.bin` from `build_new/new_src/Doom 2 RPG.ipa`.
- `python3 tools/disasm_map_scripts.py map00.bin --funcs 1660,1666,1701,1718,1735,1752,1775,1792,1811,1830,1847,1868,4510,4536,7693,8980,9011,9057` (all SETDEATHFUNC targets from func@508).
- Traced every script touching sprite 19, v19/v53/v55/v56/v66, and events 80/86/107-135.
- Compared each opcode on the path with `new_src/domain/game/ScriptVM.cpp`; listed opcodes used in map00/map01 that have no `case` in the rewrite's switch.

## Verdict

CONFIRMED, root cause found. The trigger chain and every precondition work in the rewrite. The crossing script (event[80]) **dies on opcode 97 `EV_SET_CALDEX_RENDER_HACK`**, which has no `case` in the rewrite: it falls into `default:`, which kills the thread (`new_src/domain/game/ScriptVM.cpp:1525-1531`, log `[script] UNIMPLEMENTED opcode 97 at IP=6371`). This happens after the script has already done `++v56` and disabled event[80], but before it moves the NPC and opens door 82. The result: the NPC stays on the bridge tile (19,20), door 82 stays shut (no other script opens it), and event[80] can never fire again. The player is soft-locked.

Death functions, NAMEENTITY, NPCCHAT, SPEECHBUBBLE, LERP_FOG and SPAWN_PARTICLES are not involved (details below).

## Evidence

### Trigger chain (map00, disassembler IPs)

1. **event[86]**, tile (18,21), TRIGGER dir:E. Direction is the reverse of facing (`docs/original-code/tile-events-vm.md` §1.2, `src/MovementController.cpp:214-224`), so the player stands at (19,21) facing W and uses (18,21). Entry @5780: `EVAL (v16==0 && v53==0)` (v16 = `isFamiliar`, `src/Game.cpp:3469`; the rewrite fixes it to 0, `new_src/domain/game/ScriptVM.cpp:315`). Then `CALL func_5823`:
   - v56==0: DOOROP 98/83 LOCK, enable event[121], SCREEN_SHAKE, SPAWN_PARTICLES, LERP_FOG, DIALOG 76, `NEXTSTATE ++v56`, enable event[108]/[116]/[118]/[135], disable [107]/[115]/[117]/[134].
   - v56==1: denied sound + DIALOG 76.
   - **v56==2 && v19==0** @5934: `GOTO walk to (19,21) face=N`, func_6125, `ENTITY_FRAME 19`, `NPCCHAT 19 chat=1`, `LERPSPRITE 19 → (19,20) NO_TIME z=-48`, `LERPSPRITE 19 → (19,20) ASYNC 2000ms`, func_6183 (MESSAGE 123, bridge pieces 166/168/179-184/170-172 rise, `SETSTATE v19 = 1`), `EVENTOP enable event[80]`, then SPEECHBUBBLE 117/118/119 chosen by v55.
2. **Terminals** event[108] (19,24 W), [116] (19,25 W), [118] (19,25 E): each disables itself and calls func_8000. v66 counts 0→1→2. On the third use (v66==2) @8043-8108: disable the terminal events, **`SETSTATE v56 = 2`** @8055, DOOROP 98/83 UNLOCK, disable event[121].
3. **event[80]**, tile (19,20), TRIGGER all directions, DISABLED in the file. The player uses the NPC's tile from (19,21) via the fire/use path (`src/PlayingInputHandler.cpp:395-404`; rewrite `new_src/core/PlayerActions.cpp:247-252`, runs before door/fire handling, so it works):
   ```
   6352 SETSTATE v55 = 2
   6356 DIALOG str=114 style=1
   6359 NEXTSTATE ++v56            (-> 3)
   6361 EVENTOP disable event[80]
   6364 ENTITY_FRAME sprite=19 frame=16
   6368 NPCCHAT sprite=19 chat=0
   6371 SET_CALDEX_RENDER_HACK b=1   <-- rewrite: default -> thread killed
   6373 LERPSPRITE 19 -> (19,17) BLOCK|DEFAULT_Z 1500ms
   6378 SET_CALDEX_RENDER_HACK b=0
   6380 DOOROP sprite=82 OPEN       (the only DOOROP on door 82 in the map)
   6383 LERPSPRITE 19 -> (19,15) 1000ms
   6388 LERPSPRITE 19 -> (20,15) 500ms
   6393 ENTITY_FRAME 19 frame=0 ; 6397 NPCCHAT 19 chat=1 ; RETURN
   ```
4. After the crossing: event[25] (20,15) is the NPC's chat (DIALOG 115/116, v57), and event[13] (19,9) ENTER hides the NPC. event[35]/[87] (EXIT S at (19,17), EXIT N at (19,21)) run ABORT_MOVE while v19==0.
5. Reset helper func_5442 (from event[20], [82], [120] and from lowering the bridge): sets v18=v19=0, puts bridge pieces back, and if `v55 != 2`, parks the NPC at (1,1) and disables event[80] (@5553-5564).

### Opcode 97 in the original

- `src/ScriptThread.cpp:2021-2025`: `this->app->render->useCaldexHack = (this->getByteArg() != 0);`. It has one u8 operand (disassembler: 6371→6373, 2 bytes) and no other side effects.
- Render use only: `src/Render.cpp:1536-1542` (in `renderSpriteObject`, `src/Render.cpp:1498`, where the tile is `mapSpriteInfo & 0xFF`, +257 if flag 0x400000, `:1506,1515-1517`). The first sprite with tile 69 (`TILENUM_NPC_BOB`, `src/Enums.h:711`) is deferred into `delayedSpriteBuffer[2]` and drawn at the end of `renderBSP` (`src/Render.cpp:1769-1771`). Saved/loaded as one byte (`src/Game.cpp:1719`, `:1887`). The rewrite already writes a 0 placeholder (`new_src/domain/game/WorldState.cpp:231`).
- The legacy `default:` is a fatal `app->Error("Cannot handle event")` (`src/ScriptThread.cpp:2034-2037`). The original therefore handles every opcode it ships with. The rewrite replaced that Error with killing the thread (`new_src/domain/game/ScriptVM.cpp:1525-1531`).
- Only map00 uses opcode 97, and only at @6371 and @6378. map01..map09 have zero uses (disassembler grep).

### Ruled out

- **EV_SETDEATHFUNC** (skipped, `new_src/domain/game/ScriptVM.cpp:1053-1058`; original `src/ScriptThread.cpp:1059-1071`): the 18 map00 death functions only touch v41/v24, event[0]/[12]/[18]/[37]/[40]/[91]/[100]/[102]/[126]/[130]/[137]/[138]/[163], and sprites 30/46/47/85/234-236. None of them touch v19/v53/v55/v56/v66 or events 80/86/107-135. They are not on the bridge path, but they are a real gap elsewhere (left to the mirror-reflection investigation).
- **NAMEENTITY** (`src/ScriptThread.cpp:1010-1021`): only sets `entity.name` and `info |= 0x400000`. EV_EVAL has no name operand, and `ScriptThread.cpp` reads `.name` nowhere else. Scripts address entities by sprite index.
- **NPCCHAT** (`src/ScriptThread.cpp:1094-1105`): sets `entity->param` (chat bubble state) on an eType-3 NPC. No script condition reads it. Skipping it is cosmetic.
- **SPEECHBUBBLE** (`src/ScriptThread.cpp:1247-1252`): HUD only. **LERP_FOG / SPAWN_PARTICLES**: visual only. All skipped handlers read the correct operand width (rewrite `ScriptVM.cpp:1008-1017,1245-1256,1385-1389` agree with the disassembler instruction lengths), so the IP does not desync.
- **UPDATEJOURNAL / JOURNAL_TILE / ENABLE_HELP**: none appears on the event[86]/[80]/terminal path.

### Other unhandled opcodes (same failure mode, other content)

Opcodes used in map00/map01 that have no `case` in the rewrite: map00 @8397 `START_ARMORREPAIR`. map01: `PITCH_CONTROL` @427, `ANGER_VIOS` @753/758, `START_TREADMILL` @4723, `START_ARMORREPAIR` @4980, `USED_CHAINSAW` @5265/5330/5395, `DROPMONSTERITEM` @6266, `INVENTORY_OP` @7027/7344. Each one kills its thread the same way.

## Minimal port

Add the following to `ScriptVM::run`:
```cpp
case Enums::EV_SET_CALDEX_RENDER_HACK:   // src/ScriptThread.cpp:2021-2025
    env_.game->useCaldexHack = readUByte(t) != 0;   // or just readUByte(t) for now
    break;
```
That alone unblocks the sequence. For visual parity, also defer the first tile-69 sprite to the end of the sprite pass while the flag is set (`src/Render.cpp:1536-1542,1769-1771`), and save the flag (`WorldState.cpp:231`). Recommended: make the `default:` branch loud (assert in Debug), so the next missing opcode is not a silent soft-lock.

## Open questions

- Door 82's tile position and lock state were not decoded (the disassembler does not list sprites). Either way the crossing needs door 82 opened by @6380.
- A save made after the failure carries v56=3 with event[80] disabled. Such saves stay stuck even after the fix, so test from a save made before event[80] fires, or from a new game.
