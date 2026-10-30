# 2026-10-29 — Script opcodes missing from the rewrite's ScriptVM

## Question

`new_src/domain/game/ScriptVM.cpp` `ScriptVM::run` sends every opcode without a
`case` to `default:`, which logs `[script] UNIMPLEMENTED opcode N`, sets
`t->state = 0` and returns 1, i.e. it **kills the thread**. That differs from
the original, where an unknown opcode is a fatal `Error` (`src/ScriptThread.cpp:2034-2037`).
In the rewrite it soft-locks maps (map00 START_ARMORREPAIR @8397, map01
PITCH_CONTROL @427 in INIT_MAP, so the fade-in never runs). Goal: list every
opcode the original handles that the rewrite lacks, with its decode,
thread-continuation behaviour, the subsystem it needs, its shipped uses and a
safe stand-in.

## Method

1. Diffed the `case Enums::EV_*` labels of `src/ScriptThread.cpp` (switch at
   `:243-2038`) against `new_src/domain/game/ScriptVM.cpp` (`run` at `:439-1556`).
2. Read every missing handler in `src/` plus the callee that matters for
   continuation (ArmorRepairSystem, DialogSystem close, MiniGameManager
   treadmill, MovementController pitch, Player strip/restore, Game drop items,
   Entity::resurrect).
3. Checked that each "skipped" stub in the rewrite consumes the same operand
   bytes as the original.
4. Disassembled all 10 shipped maps (`Payload/Doom2rpg.app/Packages/map00..09.bin`
   from `build_new/new_src/Doom 2 RPG.ipa`) with `tools/disasm_map_scripts.py`.
   First pass collected every `SETDEATHFUNC deathfunc=ip N` target. Second pass
   added them with `--funcs`. After that, the only unreachable bytes in all 10 maps
   were `map07 3667-3668` (2 bytes). So the usage lists below cover every
   reachable instruction.

## Verdict

CONFIRMED. **18 opcodes that the shipped maps use** have no `case` in the
rewrite. **11 more** are handled by the original (or are nops there) but never
appear in shipped bytecode. The rewrite's existing "skipped" stubs all consume
the correct operand widths. None of them desyncs the IP.

## Continuation rules (shared by every stand-in)

- Loop tail after every opcode that `break`s: `if (b) scriptStateVars[7] = n2; ++IP;`
  (`src/ScriptThread.cpp:2040-2043`). `n2` defaults to 0 (`:240`), so each
  missing opcode writes **var7 = 0**. The one exception is RESPAWN_MONSTER on
  failure, which writes var7 = -1 (`:1225`). The rewrite has the same tail
  (`new_src/domain/game/ScriptVM.cpp:1552-1553`). A stand-in that `break`s gets
  it for free.
- Parking: `this->unpauseTime = -1; n = 2; break;` still runs the tail. The IP
  therefore moves past the opcode before the `while (... && n != 2)` test ends
  the loop (`:243`, `:2043`). A later `run()` resumes at the NEXT opcode.
  `attemptResume` never wakes a thread with `unpauseTime == -1`
  (`src/ScriptThread.cpp:2063-2072`). Only an explicit external `run()` resumes it.
- Of the missing opcodes, **only START_ARMORREPAIR can park** (and only when
  `startArmorRepair` returns true). START_TREADMILL and END_GAME change the canvas
  state but return to the loop, and the thread keeps executing in the same
  `run()`.
- `getByteArg()` returns `uint8_t` in this port (`src/ScriptThread.cpp:2099-2101`),
  so the "byte" operands below are unsigned unless stated otherwise. The
  rewrite's `readByte` is `int8_t` (`new_src/domain/game/ScriptVM.h:132`), so
  use `readUByte` for exact parity where the value is not just tested `!= 0`.

## Catalog — used by shipped maps (no `case` in rewrite)

Enc: `B`=u8, `S`=u16 big-endian, `–`=no operands. "Uses" = map:IP.

### 29 EV_SHOW_PLAYERATTACK — B (weapon). Class (b)
- `src/ScriptThread.cpp:894-909`: `game->cinematicWeapon = B`. If not skipping
  and the canvas is in ST_CAMERA, it arms the combat weapon animation:
  `animLoopCount = 10*weapons[w*9+7]`, `animTime = weapons[w*9+8]*10`,
  `animStart/End`, `flashTime = 1`.
- Continues. Needs the cinematic first-person weapon animation (the rewrite only
  mentions `cinematicWeapon` in comments, `new_src/core/GameContext.cpp:890,989`).
- Uses: map06 5932 (weapon=12).
- Stand-in: `readUByte`, log, break.

### 30 EV_MONSTER_PARTICLES — B (sprite). Class (b)
- `:911-919`: when `mapSprites[S_ENT+sprite] != -1`, runs `particleSystem->spawnMonsterBlood(entity, false)`.
- Continues. Needs the particle system (the rewrite has none; SPAWN_PARTICLES is
  already a stub, `new_src/domain/game/ScriptVM.cpp:1012-1021`).
- Uses: map02 7798, 7826, 7854, 7882, 7972, 8000, 8028, 8056, 8084, 8112;
  map03 4116; map05 2380, 2414, 2439.
- Stand-in: `readUByte`, log, break.

### 35 EV_DROPMONSTERITEM — S, B, [B], B. Class (b), progression-critical
- Decode `:1023-1034`: `s = u16`, `d = u8`. If `s & 0x8000`, then
  `d = (d<<8)|u8` and `s &= 0x7FFF` (relocate mode). Then `qty = u8`.
  `x,y = mapSprites[S_X/S_Y + s]` (world units, not tiles).
- Drop mode (`:1035-1041`): `lookup(d)` by **tileIndex** (`src/EntityDef.cpp:76-83`).
  A missing def is fatal Error 109. Then `game->spawnDropItem(x, y, d, def, qty, true)`
  (`src/Game.cpp:2647-2679`): takes a free drop entity, sets sprite info = d,
  sets Z = 32 + floor height, sets `param = qty`, links it, and throws it.
- Relocate mode (`:1042-1053`): moves the entity of sprite `d` to (x,y), relinks
  it, `throwDropItem`, and sets `info |= 0x400000`.
- Then `canvas->staleView = true`. Continues.
- Needs the drop-entity pool plus throw lerp (the rewrite also stubs EV_DROPITEM,
  `new_src/domain/game/ScriptVM.cpp:1382-1388`).
- Uses (all drop mode):
  - map01 1028: def 2 = TILENUM_CHAINSAW (`src/Enums.h:650`). Deathfunc, gated by
    `ITEM_COUNT wpn[1] == 0`.
  - map01 6266: def 107 = ONE_UAC_CREDIT (`src/Enums.h:726`). Bridge sequence.
  - map02 1330, 1348, 1366, 1384, 1402, 1420: def 111 = TILENUM_KEY_BLUE
    (`src/Enums.h:728`). Six deathfuncs, each `++v34` and drops only when
    `v34 == 6`.
- Stand-in: read all operands exactly (watch the bit-15 extra byte) and break.
  Because the chainsaw and the **blue key** come from this opcode, a pure no-op
  makes the game impossible to progress. As a shortcut, the def can be given
  straight to the player (`player.give` with the item's class/index from the
  def found by tileIndex), logged as a shortcut until drop items exist.

### 47 EV_RESPAWN_MONSTER — S, B, B. Class (b)
- `:1212-1226`: `sprite = u16`, `tx = u8`, `ty = u8`. If the entity exists AND
  `info & 0x1010000` (dead/corpse) AND no entity with mask 1030 is on (tx,ty), it
  calls `entity->resurrect(tx*64+32, ty*64+32, 32)` (`src/Entity.cpp:1345-1369`:
  re-def to the eType-2 monster, full HP, relink, particles). Otherwise
  **`n2 = -1` → var7 = -1**.
- Continues. Needs monster resurrection.
- Uses: map05 1330, 1367, 1394, 2678, 2711, 2752. None of them is followed by an
  EVAL of v7. LERPSPRITE / ADV_CAMERAKEY come next.
- Stand-in: read the 4 bytes, set `n2 = -1` (the legacy "could not respawn"
  result), break.

### 70 EV_STATUS_EFFECT — B [, B]. Class (a)
- `:1576-1603`: `id = u8`. If `id & 0x80`, it calls `removeStatusEffect(id & 0x7F)`
  and **reads no second byte**. Otherwise `mag = u8`, and duration is 20 for id 2,
  10 for ids 9 and 1, 6 for id 11, 5 for id 17, and 30 for every other id. It
  calls `addStatusEffect(id, mag, dur)` and then `translateStatusEffects()`.
- Continues. The rewrite already has `addStatusEffect/removeStatusEffect/translateStatusEffects`
  (`new_src/domain/game/Player.h:155-158`).
- Uses: map02 1445 (add 16, mag 10); map07 3164 (add 13, mag 10). Effect 13 plays
  sound 1034 and is refused while `buffs[9] > 0` (`src/Player.cpp:1568-1573`).

### 73 EV_INVENTORY_OP — B. Class (a) (Player-local, ~60 lines)
- `:1627-1643`:
  - 0 → `stripInventoryForViosBattle` (`src/Player.cpp:2664-2692`): saves ammo[3]
    and removes weapons 2 and 13, plus the first of weapons 3/4/5/6, recording
    them in weaponsCopy.
  - 1 → `stripInventoryForTargetPractice` (`:2694-2705`).
  - 2 → `restoreInventory` (`:2707-2759`). It branches on `weapons & 1`: the Vios
    path re-gives the stripped weapons, keeps ammo[7], selects
    `currentWeaponCopy` and sets `angryVIOS = false`. The else path does the
    target-practice restore.
- Continues. Needs `weaponsCopy/ammoCopy/currentWeaponCopy` on Player (absent,
  `new_src/domain/game/Player.cpp:199`).
- Uses: strip-for-vios at map01 7027, map03 2478, map04 1372, map06 3233,
  map08 2048. Restore at map01 7344, map03 2627 and 2683, map04 1607 and 1926,
  map06 3372 and 3632, map08 2224. Mode 1 is never used.
- Stand-in if not ported: `readUByte`, log, break. This is self-consistent
  because strip and restore both become no-ops.

### 74 EV_END_GAME — –. Class (b)
- `:1645-1651`: `player->gameCompleted = true; canvas->endingGame = true; setState(ST_EPILOGUE)`.
  ST_EPILOGUE entry runs `levelGrade(true)` and loads the epilogue text
  (`src/Canvas.cpp:1152-1160`).
- Continues: map08 4040 is followed by `FADEOP in 500ms; RETURN`.
- Needs the epilogue/credits state.
- Stand-in: set a `gameCompleted` flag (it also feeds SCR_END_GAME at load,
  `src/LoadingManager.cpp:696-697`), log, break.

### 79 EV_SET_MM_RENDER_HACK — B. Class (a)
- `:1728-1732`: `render->useMastermindHack = (B != 0)`. It is saved in the save
  state (`src/Game.cpp:1718,1886`) and read by the renderer (`src/Render.cpp:1519,1765`).
- Continues. Same pattern as the existing `useCaldexHack`
  (`new_src/domain/game/Game.h:109`).
- Uses: map05 1659 (1), 1772 (0), 2113 (1), 2455 (0).

### 80 EV_START_ARMORREPAIR — –. Class (b), the only parking opcode here
- `:1734-1742`: `if (canvas->startArmorRepair(this)) { unpauseTime = -1; n = 2; }`.
- `src/ArmorRepairSystem.cpp:16-42`:
  - `showHelp(15,false)` returns true → returns false (thread continues).
  - Otherwise, `inventory[12] >= 5` → stores the thread, `repairingArmor = true`,
    `startDialog(nullptr, str 211, style 13, flags 1)` and returns true (**park**).
  - Otherwise it plays sound 1073 (characterChoice 1) or 1072 (2..3), shows HUD
    msg 210 and returns false (continues).
- Resume: closing the dialog (`src/DialogSystem.cpp:570-597`). If var4 == 1 and
  `inventory[12] >= 5`, it does `give(0,12,-5)`, `give(0,11,1)`, sound 1054,
  `modifyStat(3,1)` and msg 212/213. Then `endArmorRepair` sets ST_PLAYING, calls
  `armorRepairThread->run()` (resumes after the opcode) and clears the thread
  (`src/ArmorRepairSystem.cpp:44-54`).
- Uses: map00 8397, map01 4980, map02 3300, map03 1670, map04 5402, map05 4247,
  map06 2113, map07 4737, map08 840. In every case the next instructions are
  `JUMP → RETURN` (for example map00: `EVAL v16==0 → START_ARMORREPAIR → JUMP → RETURN`).
- Stand-in: **do not park**. Log and break (this equals the legacy "return false"
  path). The script expects nothing back: var7 = 0 either way.

### 81 EV_FORCE_BOT_RETURN — –. Class (a)
- `:1744-1751`: if `player->isFamiliar`, it calls `familiarReturnsToPlayer(false)`
  (`src/Player.cpp:2278-2284`: sound 1112, unsetFamiliar, steal bot inventory).
- The rewrite has no familiar mode (`vars[16] = 0`,
  `new_src/domain/game/ScriptVM.cpp:319`), so the exact behaviour is a no-op.
- Uses: map04 4109; map05 2365; map07 4755, 4832, 5439.

### 86 EV_ANGER_VIOS — B. Class (a)
- `:1821-1825`: `game->angryVIOS = (B != 0)`. Readers: `src/Combat.cpp:996`,
  `src/Render.cpp:3621`. Cleared by `restoreInventory` (`src/Player.cpp:2741`)
  and by `src/DialogSystem.cpp:538`, reset at `src/Game.cpp:683`, saved at
  `src/Game.cpp:1727,1894`.
- Uses: map01 753 (0), 758 (1); map03 507, 512; map04 346, 351; map06 177, 182;
  map08 3 (0).

### 87 EV_UNHIDE_AUTOMAP / 88 EV_HIDE_AUTOMAP — B,B,B,B (x0,y0,x1,y1). Class (a)
- `:1827-1853`: for y in [y0..y1] and x in [x0..x1] (inclusive, operand order
  x0,y0,x1,y1), `mapFlags[y*32+x]` gets `&= ~8` (87) or `|= 8` (88). Then
  `automapDrawn = false`.
- Bit 0x8 is the automap-hidden bit (readers `src/AutomapController.cpp:106,237`).
  The rewrite has `MapData::mapFlags` (`new_src/domain/world/MapData.h:88`).
- Uses: map02 only. 88 at 6288, 6293, 6298, 6482, 6487, 6492, 6497. 87 at 6303,
  6308, 6313, 6318, 6467, 6472, 6477.

### 89 EV_PORTAL_EVENT — B. Class (a) (state only)
- `:1855-1868`: `state = B & 0xF`, `prev = (B>>4) & 0xF`. State 4 sets
  `portalScripted = false`. Any other state sets `portalScripted = true` and
  stores portalState/previousPortalState. Reset at map load to
  `PORTAL_DNE / 0 / false` (`src/LoadingManager.cpp:328-330`). The consumer is
  `src/Render.cpp:2713-2761`, which is not ported
  (`new_src/render/World3D.cpp:505-507`).
- Uses: map02 5969, 5972, 5975, 6075, 6113, 6193; map05 3868, 4003; map07 5646;
  map08 1962, 2060, 2149, 2228.

### 90 EV_PITCH_CONTROL — S. Class (a) (table only)
- `:1870-1899`: `v = u16`.
  - `dir = v & 7`, `x = (v>>8) & 0x1F`, `y = (v>>3) & 0x1F`, `remove = (v>>13) & 1`.
  - Add: the first free slot of `mapKeepPitchLevelTiles[20]` gets `x | y<<5 | dir<<10`.
    The add is silently dropped when the table is full.
  - Remove: every slot that matches both tile and dir becomes -1.
- Table reset to -1 at load (`src/LoadingManager.cpp:324-325`). Reader:
  `MovementController::pitchIsControlled(x, y, dir)`
  (`src/MovementController.cpp:361-375`), called from `startRotation` with
  `dir = VecToDir(view step)` (`:243`). A match suppresses the auto slope pitch.
- The disassembler prints `dir` as "level".
- The rewrite has no slope pitch yet, so only the table needs to exist.
- Uses: map01 427 (INIT_MAP — the black-screen bug). map02 150, 153, 156, 159,
  162 (add), 231–243 (remove), 4791–4803 (remove), 5209–5221 (add). map07
  377–440 (22 adds, step 3), 2828, 2831, 2834, 2837 (remove).

### 91 EV_USED_CHAINSAW — –. Class (a)
- `:1901-1905` → `Player::usedChainsaw(false)` (`src/Player.cpp:2636-2652`):
  - `++chainsawStrengthBonusCount`.
  - When the count reaches 30, it wraps (`%= 30`), calls `modifyStat(4,2)` and,
    if that returns non-zero, shows HUD msg 241 with the arg.
  - Always ends with `canvas->startShake(666, 1, 0)`.
- The rewrite has `modifyStat` and `Hud::startShake`.
- Uses: map01 5265, 5330, 5395; map02 5307, 5357, 5411, 5468, and 6883–7598
  (12 sites, step 65).

### 93 EV_DESTROY_PLAYER — B. Class (a)
- `:1925-1947`: `b = u8`.
  - `noclip` → break.
  - `amount = isFamiliar ? ammo[7] : ce->getStat(STAT_HEALTH)`.
  - Non-familiar: `painEvent(nullptr,false)`.
  - Always: `hud->damageTime = time+1000; combat->totalDamage = 1; player->pain(amount, nullptr, true)`.
  - Non-familiar: `addArmor(-getStat(STAT_ARMOR))`.
  - Familiar with b == 0: `noFamiliarRemains = true`.
- Continues. The player's death flows through the normal pain path. The rewrite
  has the same primitives (see its EV_DAMAGEPLAYER, `new_src/domain/game/ScriptVM.cpp:1186-1206`).
- Uses: map02 1808, 4238, 4250, 4262; map06 754–1159 (16 sites, step 27);
  map07 3167, 3207.

### 94 EV_START_TREADMILL — –. Class (b)
- `:1949-1953`: `canvas->setState(ST_TREADMILL)` and continue.
- ST_TREADMILL entry (`src/Canvas.cpp:1145-1151`) calls `shiftWeapon(true)` and
  `onEnterTreadmill`. The treadmill is self-contained
  (`src/MiniGameManager.cpp:52-163`) and returns to ST_PLAYING by itself.
  Rewards: AGI +2 with msg 243/244, or a fall (245) / step off (246). It has **no
  script callback**.
- Uses: map01 4723, map02 2723, map03 4186, map06 1770, map09 11. The next
  instruction is always JUMP/RETURN.
- Stand-in: log, break.

## Catalog — handled by src/ but unused by shipped maps (class c)

Operand widths are those of the C++ port. Nops read nothing.

| # | Name | src | Port decode |
|---|---|---|---|
| 39 | STOCKSTATION | `:1123-1126` | nop, no operands |
| 40 | LERPFLAT | `:1128-1133` | B, S read and ignored |
| 44 | BRIBE_ENTITY | `:1195-1198` | nop |
| 45 | PLAYER_ADD_STAT | `:1200-1205` | B: `modifyStat((B>>5)&7, sext5(B&0x1F))` |
| 46 | PLAYER_ADD_RECIPE | `:1207-1210` | nop |
| 54 | ENDMINIGAME | `:1351-1354` | nop |
| 55 | ENDROUND | `:1356-1359` | nop |
| 62 | GIVEAWARD | `:1500-1503` | nop |
| 65 | STARTMIXING | `:1505-1508` | nop |
| 67 | GOTO_MENU | `:1533-1537` | B: `menuSystem->setMenu(B)` |
| 68 | START_INTERCINEMATIC | `:1539-1545` | B: `setupCamera(B&0x7F)`, cameraThread=this, ST_INTER_CAMERA |

The opcode numbers 63, 64 and 98–254 are not defined (`src/Enums.h:464-497`).

## Existing rewrite stubs — operand check

All of these consume the same bytes as `src/`, so none of them desyncs the IP:

| Opcode | Rewrite | Original |
|---|---|---|
| 25 DROPITEM | `ScriptVM.cpp:1382-1388` S,B | `:846-860` S,B |
| 31 SPAWN_PARTICLES | `:1012-1021` B,S,B | `:921-936` |
| 34 NAMEENTITY | `:1050-1055` B,B | `:1010-1021` |
| 38 NPCCHAT | `:1266-1270` S | `:1094-1121` |
| 43 UPDATEJOURNAL | `:1224-1229` B,B | `:1183-1193` |
| 49 SPEECHBUBBLE | `:1259-1264` s,B | `:1247-1254` |
| 57 SET_FOG_COLOR | `:1393-1397` I | `:1375-1379` |
| 58 LERP_FOG | `:1399-1403` I | `:1381-1386` |
| 71 JOURNAL_TILE | `:1231-1237` B,B,B | `:1605-1612` |
| 77 FOG_AFFECTS_SKYMAP | `:1176-1179` B | `:1716-1720` |
| 78 ENABLE_HELP | `:1181-1184` B | `:1722-1726` |
| 85 GIVE_AUTOMAP | `:1364-1366` – | `:1815-1819` |

## Open questions

- In DROPMONSTERITEM relocate mode, the entity is resolved from sprite `d`
  without a `-1` check (`src/ScriptThread.cpp:1043`). The shipped maps never use
  that mode.
- The NPCCHAT error path (`src/ScriptThread.cpp:1106-1119`) is dead code after the
  fatal Error(14). It has no bearing on the rewrite.
