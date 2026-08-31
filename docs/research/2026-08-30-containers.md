# 2026-08-30 — Containers ("crates") in the original: identity, blocking, opening, animation, loot

## Hypotheses under test

1. Containers are a distinct entity kind in `entities.bin` (which `eType`/`eSubType`/`parm`?).
2. They block the player because their `eType` bit is inside `CONTENTS_PLAYERSOLID` (13501).
3. ACTION_FIRE opens them through some `use()`/`activate()`/`touched()` chain.
4. They have an opening animation (frames? lerp? separate animator?) comparable to doors.
5. Their loot uses the corpse route (`lootSet` + `poolLoot`/`giveLootPool` + list UI).
6. Some "boxes" are in fact pure tile-event scripts (EV_GIVELOOT), not entities.

## Method

- grep/read `src/` (`Enums.h`, `Entity.cpp`, `PlayingInputHandler.cpp`, `MovementController.cpp`,
  `Render.cpp`, `Hud.cpp`, `LootingSystem.cpp`, `ScriptThread.cpp`, `Combat.cpp`,
  `LoadingManager.cpp`, `Resource.cpp`).
- Parsed `tmp_entities.bin` with the exact loader layout (`src/EntityDef.cpp:35-43`,
  little-endian shifts `src/Resource.cpp:136-156`).
- Parsed the sprite tables of `tmp_map00.bin` following `src/LoadingManager.cpp:487-546`
  (scratch script, no bytecode decoding done by hand).
- Bytecode read with `tools/disasm_map_scripts.py tmp_map00.bin --verify` (0 failures),
  cross-referenced tile-event tile indices against crate sprite positions.
- Parsed `tmp_tables_real.bin` table 4 (`TBL_COMBAT_COMBATMASKS`, `src/App.cpp:393`,
  layout `src/Resource.cpp:225-247,313-327`).

## Verdicts

| # | Hypothesis | Verdict |
|---|---|---|
| 1 | Distinct def kind | **CONFIRMED** — `eType=ET_ATTACK_INTERACTIVE(10)`, `eSubType=INTERACT_CRATE(2)`, `parm=0`, `tileIndex=152` (`TILENUM_OBJ_CRATE`), single def (index 158 in `entities.bin`), name string 136. |
| 2 | Blocking via contents mask | **CONFIRMED** — bit 10 is set in 13501; movement traces with 13501 (`src/MovementController.cpp:326`). |
| 3 | Opening chain | **PARTIAL/REFUTED as phrased** — no `use()`/`activate()`/`touched()`. ACTION_FIRE writes `entity->param = upTimeMs + 200` and calls `Game::unlinkEntity` inline (`src/PlayingInputHandler.cpp:387-393`). |
| 4 | Animation | **CONFIRMED** — sprite-frame animation driven inside the renderer, 4 frames (0→3), 200 ms/frame, stops at 3; totally unlike the door `S_SCALEFACTOR` lerp. |
| 5 | Loot route | **REFUTED** — crates never have a `lootSet` (`src/Entity.cpp:100-108`), `poolLoot` only walks `eType==9` (`src/LootingSystem.cpp:163`). Loot comes from the crate tile's script via `EV_GIVELOOT`. |
| 6 | Script-only "boxes" exist | **CONFIRMED** — e.g. map00 EVT 148 @(20,28) HIDEs two `ET_ITEM` sprites and grants 52 credits with no container entity. |

## Evidence

### Def (entities.bin)

`src/EntityDef.cpp:35-43` reads 8 bytes per def: `short tileIndex, byte eType, eSubType, parm,
name, longName, description` (little-endian, `src/Resource.cpp:136-141`).
Parsing `tmp_entities.bin` (190 defs) gives every `eType==10` def:

```
def[138] tileIndex=121 (OBJ_TABLE) eSub=0 INTERACT_FURNITURE  parm=1 name=128
def[139] tileIndex=123 (TOILET)    eSub=3 INTERACT_PICKUP     parm=3 name=131
def[141] tileIndex=127 (SINK)      eSub=3 INTERACT_PICKUP     parm=3 name=132
def[146] tileIndex=135 (OBJ_CHAIR) eSub=0 INTERACT_FURNITURE  parm=1 name=134
def[158] tileIndex=152 (OBJ_CRATE) eSub=2 INTERACT_CRATE      parm=0 name=136
def[165] tileIndex=178 (GLASS)     eSub=1 INTERACT_BARRICADE  parm=2 name=137
```

Tile-number constants: `src/Enums.h:751,752,765,792,738,741`. Subtypes: `src/Enums.h:54-57`.

### Blocking

`Game::findMapEntity(x,y,mask)` tests `mask & (1 << def->eType)` (`src/Game.cpp:745`), and the
trace uses the same masks. `CONTENTS_PLAYERSOLID = 13501` (`src/Enums.h:29`) =
bits {0,2,3,4,5,7,10,12,13} = WORLD, MONSTER, NPC, PLAYERCLIP, DOOR, DECOR,
ATTACK_INTERACTIVE, SPRITEWALL, NONOBSTRUCTING_SPRITEWALL. `MovementController::attemptMove`:
`int n3 = app->player->noclip ? 0 : 13501;` (`src/MovementController.cpp:326`) then
`trace(..., n3, 16)` (`:333`). Crates are linked into `entityDb` at spawn
(`src/Game.cpp:451-454`), so bit 10 makes them solid.

### ACTION_FIRE chain

`src/PlayingInputHandler.cpp:189-195`:
```
if (app->player->facingEntity != nullptr && app->player->facingEntity->def->eType == 10) {
    canvas->lootingSystem.lootSource = app->player->facingEntity->name;
}
```
then trace with `n5 = 13997` (`:200`), the eType-10 arm of the picker (`:238-247`), and:
```
if (entity != nullptr && entity->def->eType == 10 && entity->def->eSubType == 2) {   // :387
    if (dist2 <= app->combat->tileDistances[0]) {                                    // :388
        entity->param = app->upTimeMs + 200;                                         // :389
        app->game->unlinkEntity(entity);                                             // :390
```
`tileDistances[i] = 64*(i+1)*(64*(i+1))` (`src/Combat.cpp:42`), so `[0]=4096` = squared
one-tile radius. `unlinkEntity` clears `info & 0x100000` (`src/Game.cpp:92`) → the crate
disappears from `entityDb` → no longer solid, no longer a facing/trace target.
Right after, the tile in front is scripted:
`executeTile(destX+viewStepX>>6, destY+viewStepY>>6, flagForFacingDir(4), true)`
(`src/PlayingInputHandler.cpp:395-398`); `flagForFacingDir(4)` returns
`4 | 1 << (((destAngle+512) & 0x3FF) >> 7) + 4` (`src/MovementController.cpp:214-223`) —
exec-type TRIGGER plus the *reversed* facing direction bit.

### Animation

`Render::renderSpriteObject` (`src/Render.cpp:1497`): `n3 = info & 0xFF` (tile),
`n7 = (info & 0xFF00) >> 8` (frame) (`:1505,1508`). Then:
```
if (entity->def->eType == 10 && entity->def->eSubType == 2 && entity->param != 0) { // :1607
    if (app->time > entity->param) { ++n7; entity->param = app->time + 200; }       // :1608-1611
    if (n7 > 3) { entity->param = 0; n7 = 3; }                                      // :1612-1615
    this->mapSpriteInfo[n] = ((n2 & 0xFFFF00FF) | n7 << 8);                         // :1616
```
`app->time` is the per-frame snapshot of `upTimeMs` (`src/Canvas.cpp:749-751`), the same clock
used to arm `param`. So: 4 frames 0,1,2,3, 200 ms each (~600 ms total), then `param=0` freezes
frame 3 forever. Contrast with doors, which animate `mapSprites[S_SCALEFACTOR]`
(`docs/original-code/doors.md`); crates never touch the scale factor.

### Loot

- `Entity::initspawn` deletes the lootSet of everything that is not `ET_MONSTER`/`ET_CORPSE`
  (`src/Entity.cpp:100-108`) → crates have `lootSet == nullptr`.
- `LootingSystem::poolLoot` iterates the tile list and only processes `entity->def->eType == 9`
  (`src/LootingSystem.cpp:163`), and `ST_LOOTING` is only entered when the ACTION_FIRE picker
  set `n6 = 1`, which happens exclusively in the `eType == 9` arm
  (`src/PlayingInputHandler.cpp:283-336,373-378`). Crates never reach the loot list UI.
- Crate content is `EV_GIVELOOT` in the crate tile's script: instant `player->give(...)` plus a
  dialog (`ScriptThread::composeLootDialog`, `src/ScriptThread.cpp:2121-2222`). The dialog header
  uses `lootingSystem.lootSource` (the crate's `name`, set at `src/PlayingInputHandler.cpp:191`),
  composing string 129 ("… <crate name> …") instead of the generic 130
  (`src/ScriptThread.cpp:2123-2130`).

**map00 cross-check.** All 10 sprites with tile 152 sit on a tile that owns exactly one
`TRIGGER` event whose body is `EVENTOP disable self; PLAYSOUND 1054; GIVELOOT […]; RETURN`:

| sprite | tile (x,y) idx | EVT | entry IP | payload |
|---|---|---|---|---|
| 69 | 18,3 (114) | 0 | 1796 | 5×idx12, 1×idx17, 24 ammo cls2 |
| 43 | 14,8 (270) | 12 | 1705 | 1×idx17, 24 ammo |
| 24 | 11,13 (427) | 18 | 1834 | 4×idx16, 24 ammo |
| 13 | 7,18 (583) | 40 | 1686 | 5×idx12, 1×idx17, 4×idx16 |
| 12 | 6,22 (710) | 91 | 1722 | 1×idx11, 1×idx17 |
| 58 | 15,23 (751) | 100 | 1815 | 1×idx17, 1×idx11, 12 ammo |
| 133 | 30,25 (830) | 126 | 1756 | 3×idx12, 1×idx17, 8×idx16, 24 ammo, 25 credits |
| 60 | 15,26 (847) | 130 | 1851 | 15×idx12, 2×idx17, 4×idx16, 24 ammo |
| 128 | 28,26 (860) | 137 | 1739 | 1×idx17, 24 ammo |
| 134 | 30,26 (862) | 138 | 1779 | 1×idx17, 24 ammo |

(disassembly `tools/disasm_map_scripts.py`, entries block + IPs 1686-1888.)
The 11th `GIVELOOT` in map00, EVT 148 @(20,28) idx 916 IP 1872, is **not** a crate: it does
`NEXTSTATE; EVENTOP disable; HIDE sprite=191; HIDE sprite=192; PLAYSOUND 1054; GIVELOOT 52 credits`,
where sprites 191/192 are tile 114 = def(6,0,23) `ET_ITEM` credits lying on the floor.

### "Already searched" marking

Two independent marks, both persisted:
- the tile event disables itself (`EV_EVENTOP` sets bit `0x80000` in `tileEvents[i*2+1]`,
  `src/ScriptThread.cpp:808-814`; `executeTile` skips events with `0x80000`,
  `src/ScriptThread.cpp:75`);
- the entity stays unlinked and its sprite frame stays 3. On save/load the crate is a "binary
  entity" iff still linked (`(info & 0x100000) != 0`, `src/Entity.cpp:1423-1436`), and
  `restoreBinaryState` for `INTERACT_BARRICADE|INTERACT_CRATE` restores the destroyed/opened
  state as frame `(eSubType == 2) ? 3 : 1` plus `unlinkEntity`
  (`src/Entity.cpp:1877-1893`).

### Related, non-container members of eType 10

- `INTERACT_FURNITURE(0)` table/chair and `INTERACT_BARRICADE(1)` glass are **destroyed by
  attacks**, not opened: `Entity::pain` spawns debris and (barricade) plays 1038 + unlink
  (`src/Entity.cpp:371-392`), `died()` awards 5 XP + message 89 and counts toward
  `destroyedObj` for every subtype except 2 and 3 (`src/Entity.cpp:437-447`,
  `src/Game.cpp:448-449`).
- `INTERACT_PICKUP(3)` toilet/sink is a holy-water refill station handled inline in ACTION_FIRE
  (`src/PlayingInputHandler.cpp:405-430`) and turns into a water spout when used.
- Damage gating: `Combat::calcHit` requires `tableCombatMasks[def->parm] & (1 << weapon)`
  (`src/Combat.cpp:876-882`). Table 4 of `tables.bin` = `{0x00000000, 0x00000002, 0xFFFFFFFB,
  0xFFFFFFFB}`. Crate `parm=0` → mask 0 → **no weapon can ever hit a crate**; furniture
  `parm=1` → only weapon 1; glass/pickup `parm=2/3` → every weapon except index 2.
- Splash damage bypasses `calcHit`: `radiusHurtEntities` → `hurtEntityAt` searches with mask
  30383 (bit 10 set) and calls `pain()` + `died()` on any eType-10 entity
  (`src/Combat.cpp:1085-1097,1120,1188-1192`) — an explosion therefore shatters a crate and its
  loot script is *not* run, because the explosion re-trigger flags are `0x4004` while crate
  events carry no `0x7000` attack bits (`src/Combat.cpp:1100-1105`, gate
  `src/ScriptThread.cpp:75`).

### HUD / prompts

`Hud` action icon: eType 10 with eSubType 2 or 3 → icon row 0 (the "hand/use" icon, same as
`ET_ITEM`); other eType-10 subtypes → row 1 (attack)
(`src/Hud.cpp:354-360`, blit `src/Hud.cpp:385`, 18×18 rows of `imgActions`).
Facing a crate at ≤1 tile also fires the tutorial hint `showHelp(3)`
(`src/MovementController.cpp:98-105`).

## Open questions

- String ids 136 (crate name) / 129 / 130 were not resolved to literal text (localization
  resource not decoded here).
- Whether any map ships a crate *without* a tile script (then ACTION_FIRE would animate it and
  fall through to `fireWeapon`, which cannot hit it because `tableCombatMasks[0] == 0`).
  Only map00 was cross-checked sprite-by-sprite.
