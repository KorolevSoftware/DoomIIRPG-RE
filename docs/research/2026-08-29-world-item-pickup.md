# 2026-08-29 — World item spawn & pickup trigger (ET_ITEM / ET_MONSTERBLOCK_ITEM)

## Hypotheses

1. World items are born in the map-load entity pass from the sprite's tile index
   (`entityDefManager->lookup(tileNum)`), with `def->eSubType` = item class,
   `def->parm` = item index; quantity is not stored in map data.
2. `Entity::touched()` is invoked when the player steps onto the tile; items do not
   block movement; the pickup costs the same turn as the step.
3. `removeEntity()` hides the sprite and unlinks the entity; `def` is only nulled for
   dropped entities.

## Verdict

1. CONFIRMED (with detail: no `parm`/`param` quantity in map data; quantity is rolled in
   `touchedItem()`; sprite frame override for tile 1..12).
2. CONFIRMED — trigger is `Game::touchTile(destX, destY, true)` from
   `MovementController::finishMovement()`, i.e. after the walk lerp reaches the
   destination tile and after the tile-event scripts of that tile, immediately before
   `advanceTurn()`. Tile-granular (whole `entityDb` chain of the destination tile), no
   radius test. Items are absent from `CONTENTS_PLAYERSOLID`, so they never block.
3. CONFIRMED.

Bonus finding: `entities.bin` (190 defs) contains **zero** `eType == 11`
(ET_MONSTERBLOCK_ITEM) defs — that branch of `touched()` is unreachable with shipped data.

## Method

- grep for `touched()` call sites, `touchTile`, `ET_ITEM`, clip masks;
- read `Game::loadMapEntities` (`src/Game.cpp:329-500`), `Entity::initspawn`,
  `Entity::touched/touchedItem`, `MovementController::attemptMove/finishMovement`,
  `Render::renderSpriteObject`;
- independent data check: parsed `tmp_entities.bin` (record layout per
  `src/EntityDef.cpp:35-43`, little-endian per `src/Resource.cpp:136-140`) and
  `tmp_map00.bin` sprites via `tools/map_to_obj.py::parse_map`.

## Evidence

### A. Spawn

`Game::loadMapEntities()` (`src/Game.cpp:329`) walks all map sprites:

```
int n6 = app->render->mapSpriteInfo[n5] & 0xFF;            // src/Game.cpp:375
if ((app->render->mapSpriteInfo[n5] & 0x400000) != 0x0) { n6 += 257; }  // :376-378
EntityDef* lookup = app->entityDefManager->lookup(n6);     // :404
if (n6 >= 1 && n6 <= 12) { app->render->mapSpriteInfo[n5] |= 0x200; }   // :405-407
...
entity3->info = (n5 + 1 & 0xFFFF);                         // :428
entity3->def  = lookup;                                    // :429
entity3->initspawn();                                      // :451
app->render->mapSprites[app->render->S_ENT + n5] = this->numEntities++; // :452
if ((app->render->mapSpriteInfo[n5] & 0x10000) == 0x0) {   // :453
    this->linkEntity(entity3, n15 >> 6, n16 >> 6);         // :454
}
```

- Item-ness comes purely from the def table: `lookup(tileIndex)` returns the first def
  with that `tileIndex` (`src/EntityDef.cpp:75-82`); `eType == 6` makes it an item.
  No `eType == 6` test exists in the loader — items take the generic path.
- `Entity::initspawn()` has **no** ET_ITEM branch (`src/Entity.cpp:49-112`): items get
  `name = def->name | 0x400` (`:53`), keep `info` = sprite id + 1, get **no** `0x20000`
  bit, and their `lootSet` is deleted (`:103-108`, non-monster/non-corpse).
- Position/scale/Z are pure sprite data: scale default 64, Z default 32
  (`src/LoadingManager.cpp:474-481`), Z then += `getHeight(x,y)` and −32 for z-sprites
  (`src/Render.cpp:2462-2467`). Sprite X/Y are 1 byte × 8 units (`src/Resource.cpp:154-156`).
- Hidden sprites (`mapSpriteInfo & 0x10000`) are created but NOT linked into `entityDb`
  (`src/Game.cpp:453-455`) → they cannot be touched until a script links them.
- Def record = 8 bytes LE: `s16 tileIndex, u8 eType, u8 eSubType, u8 parm, u8 name,
  u8 longName, u8 description` (`src/EntityDef.cpp:35-43`; `shiftShort` is little-endian
  `src/Resource.cpp:136-140`). `EntityDef::touchMe` is declared but never filled/read.
- Data check on `tmp_entities.bin`: 190 defs; 46 with `eType==6`; 0 with `eType==11`.
  Item subtypes present: 0 = IT_INVENTORY, 1 = IT_WEAPON, 2 = IT_AMMO, 3 = IT_FOOD
  (`src/Enums.h:96-99`). 16 of the 46 have `tileIndex == 0` (drop-only, reachable through
  `find(eType,eSubType,parm)` from `spawnDropItem`/EV_GIVEITEM, never through `lookup`).
  Weapon items: tileIndex 1..15 (`TILENUM_ASSAULT_RIFLE=1 … TILENUM_WORLD_WEAPON=15`,
  `src/Enums.h:649-664`); ammo: 85,86,88,89,90; inventory/food: 107,110–117,119.
- Data check on `tmp_map00.bin` (parsed with `tools/map_to_obj.py::parse_map`): item
  sprites exist, e.g. sprite 1 tile=107 (one UAC credit) at (160,992) = tile (2,15) center,
  sprite 129 tile=116 (health pack), sprite 203 tile=1 (assault rifle, z-sprite z=90,
  `info=0x00200201` — frame byte already 2 in the map data), sprite 65 tile=14
  (red sentry bot weapon def, `info=0x0C50000E`, frame byte 0 — outside the 1..12
  override range).

### B. Draw

`Render::renderSpriteObject(n)` (`src/Render.cpp:1498`):
- skipped entirely when `mapSpriteInfo & 0x10000` (`:1501-1503`);
- `n7 = (n2 & 0xFF00) >> 8` is the frame index (`:1511`); animation only when bit `0x80000`
  is set: `n7 = (n + app->time / 100) % n7` (`:1544-1546`) — items normally do not have it;
- items fall through all special cases to the generic `renderSprite(x, y, z, tile, n7,
  info, renderMode, scaleFactor, flags)` tail — no bobbing, no per-frame Z offset.
  The only per-entity animation hook for non-monsters is `eType==10 && eSubType==2`
  (`:1607-1617`), not items.
- Consequence of `mapSpriteInfo |= 0x200` at load: a weapon lying in the world (tile 1..12)
  is drawn with frame index 2 of the view-weapon art. `spawnDropItem` does the same for
  `1 <= tile < 14` (`src/Game.cpp:2653-2655`) — note the off-by-one range difference
  (load: 1..12, drop: 1..13).

### C. Touch trigger

Only one gameplay call site of `Entity::touched()` exists besides the script opcode:

```
bool Game::touchTile(int n, int n2, bool b) {                       // src/Game.cpp:687
    for (Entity* mapEntity = this->findMapEntity(n, n2); mapEntity != nullptr; mapEntity = nextOnTile) {
        nextOnTile = mapEntity->nextOnTile;                          // :692 (saved BEFORE touch)
        if (b || mapEntity->def->eType == Enums::ET_ENV_DAMAGE) {    // :693
            mapEntity->touched();                                    // :694
            b2 = true;
```

- `findMapEntity(n, n2)` = `entityDb[(y>>6)*32 + (x>>6)]` (`src/Game.cpp:729-737`) — the
  criterion is pure **tile occupancy**, no radius, no facing, no contents mask.
- Call sites: `MovementController::finishMovement()` with `b = true`
  (`src/MovementController.cpp:170`) — the only one that can pick items up; all others pass
  `b = false` and therefore touch only ET_ENV_DAMAGE: `PlayingInputHandler.cpp:400`
  (tile-event turn), `:552` (ACTION_PASSTURN), `ZoomController.cpp:110` (pass turn while
  zoomed), `GameStateRunner.cpp:48` (combat finished).
- Type filtering happens inside `Entity::touched()` (`src/Entity.cpp:118-149`):
  `ET_MONSTERBLOCK_ITEM || ET_ITEM` → `touchedItem()`; `ET_ENV_DAMAGE` → damage;
  everything else returns false. The world entity (`entities[0]`, eType 0) and the player
  copy (`entities[1]`, eType 1) also sit in the chain and are simply ignored.

Ordering inside the step (`src/MovementController.cpp:160-184`):

```
164  if (gotoThread && viewAngle == destAngle) gotoThread->run();
168  executeTile(destX>>6, destY>>6, flagForFacingDir(8), true);
169  executeTile(destX>>6, destY>>6, eventFlags[1], true);
170  touchTile(destX, destY, true);              // <- pickup happens here
178  else if (!gotoThread && state == ST_PLAYING && monstersTurn == 0)
183      advanceTurn();
```

`finishMovement()` is reached when the view lerp has arrived at dest
(`src/MovementController.cpp:509-512`) or when an early input snaps the view
(`src/PlayingInputHandler.cpp:45-52`). So: the pickup is **after** the visual move
completes and **after** the destination tile's scripts, and it does not cost an extra
turn — the same `advanceTurn()` of the step covers it.

Blocking: `attemptMove` traces with `n3 = player->noclip ? 0 : 13501`
(`src/MovementController.cpp:326`), and moves only if `traceEntity == nullptr`
(`:332-333`). 13501 = `CONTENTS_PLAYERSOLID` (`src/Enums.h:29`) = bits
{0,2,3,4,5,7,10,12,13} — bit 6 (ET_ITEM) and bit 11 (ET_MONSTERBLOCK_ITEM) are **clear**,
so neither blocks the player. `CONTENTS_MONSTERSOLID = 15535` (`src/Enums.h:30`) = bits
{0,1,2,3,8,10,11,12,13} — bit 11 set, bit 6 clear: ET_MONSTERBLOCK_ITEM blocks monsters
only (used e.g. `src/Entity.cpp:1302,1329`, `src/Game.cpp:717,3558`). `Game::trace` filters
by `n4 & 1 << def->eType` (`src/Game.cpp:221`).

### D. Removal

`touchedItem()` finishes with `removeEntity(this)` + `playSound(1054, 0, 4, false)`
(`src/Entity.cpp:276-277`), and only then `touched()` runs the pickup script:

```
if (this->touchedItem()) {                                    // src/Entity.cpp:124
    app->game->scriptStateVars[11] = this->def->tileIndex;    // :125
    app->game->executeStaticFunc(11);                         // :126  (SCR_ITEM_PICKUP)
    if (this->isDroppedEntity()) {                            // :127
        app->render->mapSprites[app->render->S_ENT + this->getSprite()] = -1; // :128
        this->def = nullptr;                                  // :129
    }
    return true;
```

```
void Game::removeEntity(Entity* entity) {                     // src/Game.cpp:183
    if ((entity->info & 0xFFFF) != 0x0) {
        app->render->mapSpriteInfo[entity->getSprite()] |= 0x10000;   // :186 hide sprite
    }
    if ((entity->info & 0x100000)) { this->unlinkEntity(entity); }    // :188-190
    app->player->facingEntity = nullptr;                              // :191
}
```

`unlinkEntity` splices the tile chain and clears the linked bit
(`info &= 0xFFEFFFFF`, `src/Game.cpp:70-92`). For a **world** item the entity slot keeps
its `def`, `info` (sprite id) and `param`; only the sprite is hidden and the entity is
unlinked — it can be restored by save/load state code (`Entity::restoreBinaryState`,
`src/Entity.cpp:1861+`). For a **dropped** item the slot is fully recycled
(`S_ENT = -1`, `def = nullptr`) so `getFreeDropEnt` can reuse it (`src/Game.cpp:2612-2622`).

Failure path: if `touchedItem()` returns false (inventory full, health full, sentry bot
already owned) the entity stays linked and visible and `touched()` returns false; no sound,
no script, no turn side effects.

## Open questions

- What consumed `EntityDef::touchMe` in the J2ME original (field exists, never loaded)?
- Are there maps where an `eType == 6` sprite is placed on a wall tile the player can never
  enter (making it effectively decorative)? Not surveyed beyond map00.
