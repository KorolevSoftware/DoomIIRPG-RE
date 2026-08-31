# 2026-08-30 — Item pickup by the ACTION button (ACTION_FIRE), not by stepping

## Hypothesis (from the caller)
"~90% of items lie on shelves/ledges that cannot be stepped on; they are taken by looking at
them and pressing Enter. `PlayingInputHandler` must call `touched()`/`touchedItem()` on
`Player::facingEntity` (or on the entity in the adjacent tile) somewhere in the ACTION_FIRE
chain, and that probably costs a turn."

## Verdict
**PARTIAL / REFUTED in mechanism.**
- REFUTED: `ACTION_FIRE` never calls `touched()`/`touchedItem()`, and it never even *sees* a
  world item — its trace mask `13997` (`CONTENTS_WEAPONSOLID`) has no `ET_ITEM` bit.
  `facingEntity` is used in that branch only to set `lootingSystem.lootSource`.
- CONFIRMED (mechanism): shelf items are given by the **tile script of the tile in front**,
  run by `executeTile(frontTile, flagForFacingDir(4), true)`; the script's `EV_GIVEITEM`
  mode 0 calls `Entity::touched()` on the item sprite — the same grant code as walking over it.
- CONFIRMED: such a pickup costs a turn (`advanceTurn`), unlike walk-over pickup where the
  move already spends the turn.
- CONFIRMED: `facingEntity` *can* be an item (mask 21741 includes bit 6) — that is why the HUD
  shows the item name and the interaction feels like "look + Enter".

## Method
1. `grep` for `facingEntity`, `touched(`, `touchTile`, `flagForFacingDir` in `src/`.
2. Read `MovementController::checkFacingEntity`, `Game::trace`, the whole `ACTION_FIRE` branch,
   `Game::touchTile`, `Entity::touched/touchedItem`, `Game::removeEntity`,
   `ScriptThread::executeTile` and `EV_GIVEITEM`.
3. Bit-decoded the trace masks against `src/Enums.h:10-40`.
4. Cross-checked against map data: `tools/disasm_map_scripts.py tmp_map00.bin --verify`
   (+ `--ipa`) for tile events, and `tools/map_to_obj.py` (`parse_map`) + `tmp_entities.bin`
   for the sprite table / entity defs.

## Evidence

### 1. facingEntity
- `src/MovementController.cpp:32-34` `if (!canvas->updateFacingEntity) { return; }` — lazy.
  Only caller: `src/Hud.cpp:735-740` (top-bar repaint, `state == ST_PLAYING`).
- `src/MovementController.cpp:36-40`:
  `app->game->trace(destX + (-view[2]*28 >> 14), ..., destX + (6*-view[2] >> 8), ..., nullptr, 21741, 2, canvas->isZoomedIn)`
  → start 28 units ahead, end 6 tiles ahead (view is 14.14: `16384*6/256 = 384 = 6*64`), radius 2.
- Mask 21741 = bits {0,2,3,5,6,7,10,12,14}; bit 6 = `ET_ITEM` (`src/Enums.h:16`), bit 11
  (`ET_MONSTERBLOCK_ITEM`) absent.
- Nearest hit wins: bubble sort of `traceFracs`, `traceEntity = traceEntities[0]`
  (`src/Game.cpp:311-325`); the loop at `:41-86` re-picks a monster/decor/interactive hidden
  behind a spritewall.
- `src/MovementController.cpp:90-93`: `dist = facingEntity->distFrom(viewX,viewY);`
  `if (def->eType != 2 && dist > tileDistances[2]) facingEntity = nullptr;`
  with `distFrom = max(dx²,dy²)` (`src/Entity.cpp:1157`) and
  `tileDistances[j] = 64*(j+1)*(64*(j+1))` (`src/Combat.cpp:42`) → 3 tiles for non-monsters,
  unlimited (within the 6-tile ray) for monsters.
- No height gate: in `Game::trace` the Z window
  `n9 > -(32+n5) && n9 < 32+n5` is applied only `if (n3 >= 0 && traceCollisionZ >= 0 && b)`
  (`src/Game.cpp:283-292`), and `b` = `canvas->isZoomedIn` at both call sites
  (`src/MovementController.cpp:40`, `src/PlayingInputHandler.cpp:221`).

### 2. ACTION_FIRE chain
`src/PlayingInputHandler.cpp:189-540`, in order: lootSource (`:190-195`) → trace with
`n5 = 13997` / range `n7 = 6` (1 for melee) (`:197-221`) → candidate loop (`:223-368`) →
corpse-loot short circuit `if (n6 != 0) { setState(ST_LOOTING); poolLoot(...); return true; }`
(`:374-378`) → `eType 10` culls (`:379-393`) → **front-tile script** (`:395-404`) → doors,
walls, zoom, `fireWeapon` (`:405-540`).
- 13997 = bits {0,2,3,5,7,9,10,12,13} — no bit 6 and no bit 11. `n5 |= 0x4100` (weapon 2) and
  `|= 0x10` (melee) never add bit 6.
- `src/PlayingInputHandler.cpp:396-397`: front tile = `(destX + viewStepX) >> 6`,
  `viewStepValues[] = {64,0, 64,-64, 0,-64, ...}` (`src/Canvas.h:108`).
- `src/MovementController.cpp:214-224`: `flagForFacingDir(4)` adds 512 to the angle first
  → exec bit 4 (TRIGGER) + direction bit of the *opposite* facing. Bit 4 appears only at
  `src/PlayingInputHandler.cpp:395`; `finishMovement` and rotations use bit 8 (FACE) on the
  player's own tile (`src/MovementController.cpp:168,307,508`).
- `src/Game.cpp:687-698`: `touchTile(n,n2,b)` touches every entity on the tile when `b`,
  otherwise only `ET_ENV_DAMAGE`. `ACTION_FIRE` passes `false` (`:400`), `finishMovement`
  passes `true` (`src/MovementController.cpp:170`).

### 3. The grant
`src/ScriptThread.cpp:969-980` (EV_GIVEITEM, third operand 0):
```
short i = (short)(uByteArg17 << 8 | uByteArg18);
short n54 = mapSprites[S_ENT + i];
if (n54 == -1) Error(... 16);
if (!entities[n54].touched()) n2 = 1;
```
`Entity::touched()` → `touchedItem()` → `player->give(...)`, static script 11, sound 1054,
`removeEntity` (`src/Entity.cpp:118-278`). `scriptStateVars[7] = n2` after every opcode
(`src/ScriptThread.cpp:2041`) → scripts branch on `v7 == 0` for "give succeeded".

### 4. Turn
`src/PlayingInputHandler.cpp:398-404`: on a script hit, `advanceTurn()` unless
`game->skipAdvanceTurn` (set by tile-event flag `0x40000`, `src/ScriptThread.cpp:76-79`, or by
blocking opcodes) or the state left `ST_PLAYING`. Walk-over: turn comes from
`finishMovement` (`src/MovementController.cpp:178-184`).

### 5. Map00 data cross-check
Item sprites (def `eType == 6`) and their tile events:
- `EVT 34  tile 12,17  TRIGGER dir:all` → `EVAL v72==0 → GIVEITEM sprite 245`,
  else `GIVEITEM sprite 244` + `EVENTOP disable event[34]`; sprites on tile (12,17):
  245 (raw Z 64, ammo def 85) and 244 (raw Z 96, inventory def 112 parm 11). No other sprite.
- `EVT 22 tile 13,15` → sprite 233 (raw Z 96); `EVT 103 tile 8,24` → sprites 237/238
  (Z 96/64); `EVT 37 tile 25,17` → sprite 235 (Z 92).
- Operand check: the disassembler prints the second operand as a signed byte, so
  `qty=-11` = u8 245 = sprite id, `qty=-12` = 244, `qty=-23` = 233, `qty=-19/-18` = 237/238.
- Floor items (sprites 1, 4, 35, 64, 110, 129) are *normal* sprites (Z 32) on tiles without
  events → walk-over only.
- Z semantics: only the `numZSprites` group stores a Z byte
  (`src/LoadingManager.cpp:379-381,527`); `postProcessSprites` adds floor height and −32 for
  z-sprites (`src/Render.cpp:2462-2467`); normal sprites default to Z 32
  (`src/LoadingManager.cpp:474-481`).

### 6. Double pickup
`Game::removeEntity` (`src/Game.cpp:183-192`) hides (`mapSpriteInfo |= 0x10000`) and unlinks
(only if `info & 0x100000`, the link bit set in `linkEntity`, `src/Game.cpp:118`), so the
walk-over path cannot repeat. The script path indexes `mapSprites[S_ENT + sprite]` directly and
nothing in `touched()`/`touchedItem()` checks the hidden bit → a second grant is possible.
Shipped scripts guard with their own state vars, and map00's shelf tiles are wall niches;
however (12,17) *is* reachable (`tile_flags[17*32+12] == 0`, bit 0x1 = solid world per
`src/Game.cpp:497-503`), so the double grant is reproducible there.

## Open questions
- Whether every map follows the map00 pattern (TRIGGER tile event per shelf) — only map00 was
  disassembled here; the mechanism is map-data driven, so other maps may also use
  `EV_GIVELOOT`/dialogs for "search" objects (see loot-inventory §7.9).
- `ET_MONSTERBLOCK_ITEM` (11) is in neither trace mask; with 0 shipped defs of that type
  (loot-inventory §3.1) it stays dead code.
