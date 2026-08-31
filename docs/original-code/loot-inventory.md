# Loot & Inventory (corpses, lootsets, pickup, player inventory, HUD)

> Status: COMPLETE. Every claim cites `src/<file>:<line>`; map00 claims verified against
> `tmp_map00.bin` (read-only parse; method + full disasm in `docs/research/2026-08-24-loot-inventory.md`,
> scanner `docs/research/assets/scan_loot_map00.py`, listing `docs/research/assets/map00_disasm.txt`).
> Opcode encodings summarized from `docs/original-code/tile-events-vm.md` §3 (still authoritative).

## 1. Corpses

### 1.1 Corpse defs and entity identity
- `ET_CORPSE = 9` (`src/Enums.h:19`). Placed corpse art: tileIndex 137=`(9,17,2)`, 138=`(9,17,0)`,
  139=`(9,17,1)` — subtype 17 = `CORPSE_SKELETON` (`tmp_entities.bin`; type constants `src/Enums.h:79`).
  Monster corpse family (one per monster type, mostly tileIndex 0) is produced by def-swap, see below.
- At map load every sprite gets an entity: def = `entityDefManager->lookup(mapSpriteInfo & 0xFF)`
  (+257 iff info bit 0x400000 — door trick) and `entity->initspawn()` runs (`src/Game.cpp:374-452`).
- `Entity::initspawn` keeps the lootSet alive **only** for `ET_MONSTER`/`ET_CORPSE`; everything else
  gets `delete lootSet; lootSet=nullptr`. Non-null lootSet ⇒ `populateDefaultLootSet()`
  (`src/Entity.cpp:103-111`).
- Skeleton corpses get `info |= 0x420000` at spawn (`src/Entity.cpp:96-98`).

### 1.2 EV_MAKE_CORPSE (opcode 72)
Encoding `S,B,B`: sprite=S&0xFFF, x=B1, y=B2 (`src/ScriptThread.cpp:1614-1625`).
Looks up entity via `mapSprites[S_ENT + sprite]`; requires `entity->monster != nullptr`, else no-op;
then `corpsifyMonster((B1<<6)+32, (B2<<6)+32, entity, true)` — coords are pixel tile-centers
(fixed-point tile = 64 units).

`ScriptThread::corpsifyMonster(x, y, entity, playSound)` (`src/ScriptThread.cpp:2249-2266`):
1. `snapLerpSprites`, `resetGoal`, `clearEffects`, `undoAttack`, `game->deactivate(entity)`
   (moves it to the `inactiveMonsters` ring).
2. Sprite visual death state: `mapSpriteInfo[s] = (info & 0xFFFE00FF) | 0x7000` (bits 8–14 = death
   frame/anim overlay; low byte keeps original art tileNum).
3. Position to (x,y), z = `getHeight(x,y)+32`; `relinkSprite`.
4. `entity->info = (info & 0xFFFF) | 0x1000000 | 0x20000 | 0x400000` (keep sprite id; corpse/inactive
   marker 0x1000000; active-visibility 0x20000).
5. **Def swap**: `def = entityDefManager->find(9, def->eSubType, def->parm)` — same subtype/parm,
   now an ET_CORPSE def. Name refreshed by callers that need it (`src/Entity.cpp:501-502` does the
   same in `died()`).
6. `unlink/linkEntity` at new tile, `checkMonsterDeath(false, b)` — plays MSOUND_DEATH when `b`
   (`src/Entity.cpp:403-406`); no XP because first arg false (`src/Entity.cpp:407-413`).
The other call site passes `b=false`: EV_HIDE on an eType-2 body (`src/ScriptThread.cpp:834-839`).

### 1.3 Death → corpse (natural kill)
`Entity::died()` for ET_MONSTER (`src/Entity.cpp:459-520`):
- `info |= 0x400000`; snap lerps; `mapSpriteInfo = (old & 0xFFFF00FF) | 0x7000`.
- Unless already hidden (`info & 0x10000`): `info |= 0x1020000` and `trimCorpsePile(x,y)`.
- `game->deactivate(this)` → corpse rests on `inactiveMonsters`.
- **Only** `MONSTER_LOST_SOUL`/`MONSTER_CACODEMON` immediately roll loot:
  `gsprite_allocAnim(241,…)` + hide (`n3 |= 0x10000`) + `game->spawnDropItem(this)`
  (`src/Entity.cpp:491-495`); everyone else leaves a lootable corpse.
- Def swap identical: `def = find(9, eSubType, parm)` (`src/Entity.cpp:501`).
- Difficulty-4 corpse-gib branch hides the corpse instead (`info |= 0x410000…`, unlink,
  `counters[4]++`) (`src/Entity.cpp:504-515`; also `src/Enums.h:1019 ERR_CLEANCORPSE`,
  `COUNTER_CORPSESGIBBED=4` `src/Enums.h:1256`).

### 1.4 Corpse-pile limit
`MAX_CORPSE_PER_TILE = 3` (`src/Enums.h:1389`). `trimCorpsePile(x,y)` walks `inactiveMonsters`;
any visible corpse (`info & 0x1010000` all set, spriteInfo bit 0x10000 clear) beyond the first 2
hides the 3rd+: `mapSpriteInfo |= 0x10000`, `info = (info & 0xFEFFFFFF)|0x10000`, unlink
(`src/Entity.cpp:1194-1208`).

### 1.5 What makes a corpse lootable
Action-button trace, `PlayingInputHandler.cpp` (ACTION_FIRE branch starts `:189`):
forward `trace()` collects `traceEntities[]` (`:218-227`); for `eType == 9` and adjacency
`dist == combat->tileDistances[0]` (`:279-280`):
- chainsaw equipped (`ce->weapon == 1`): IOS path just *selects* the corpse for melee gibbing
  (`:281-322`, J2ME variant `#if 0`-ed which had the param/lootSet checks);
- otherwise (and not zoomed-in, `:323`):
  - non-monster corpse (placed prop, `monster == nullptr`): lootable iff `param == 0 &&
    lootSet != nullptr` (`:324-330`) — `param` counts prior loots;
  - monster corpse: lootable iff `(monster->flags & 0x800) == 0 && lootSet != nullptr`
    (`:331-335`) — flag 0x800 = "already looted".
If selected candidate has loot (`n6 != 0`): `canvas->setState(ST_LOOTING);
canvas->poolLoot(entity->calcPosition()); return` — **before** any attack/tile-event processing
(`:374-378`). `ST_LOOTING = 23` (`src/Canvas.h:90`).
Render side shows a sparkle under lootable corpses: extra frame-13 sprite drawn when
`monster && !(flags & 0x800) && state != ST_CAMERA && !hasEmptyLootSet()`
(`src/Render.cpp:3470-3474`; same predicate at `:3530,3601,3754`; placed-corpse tileNums at `:1714-1715`).
`hasEmptyLootSet()` = `lootSet == nullptr || lootSet[0] == 0` (`src/Entity.cpp:2309-2311`).
Note asymmetry: input checks only `lootSet != nullptr` (can open an empty UI), render checks
`hasEmptyLootSet()`.

## 2. Lootsets

### 2.1 Storage & entry format
- `Entity::lootSet` is `int[3]` (`src/Entity.h:24`, alloc `src/Entity.cpp:2003`);
  `MAX_CORPSE_LOOT = 3` (`src/Enums.h:1388`). Loot tables are **per-entity runtime state**, not
  map data; seeded from defaults (below) and overridden by script.
- Entry = 16 bits: `[15:12]` class, `[11:6]` index/parm, `[5:0]` count
  (decode `src/LootingSystem.cpp:185-198`, encode `addToLootSet(n,idx,cnt) =
  (n<<12)|(idx<<6)|cnt` `src/Entity.cpp:2298-2307`).
- Classes seen in code: 0=inventory (`give(0,…)`), 1=weapon, 2=ammo, 3=food/health
  (grant branch pairs 0&3: `src/ScriptThread.cpp:2167-2170`), 5=quest (EV_GIVELOOT only,
  `:2147-2149`), 6=named/story string — display-only flavor, lower 12 bits = map-string id
  (`src/LootingSystem.cpp:186-194`, skipped when granting `:287`).
- Class 0 specials: idx 24 = UAC credits ×count, idx 25 = credits ×count×100
  (`src/LootingSystem.cpp:199-208`).
- Merging duplicates in the pool: same `(class<<6)|idx` merges counts, saturated to 63
  (`(existing & 0x3F) + cnt & 0x3F`, `src/LootingSystem.cpp:209-216`).

### 2.2 Defaults — `Entity::populateDefaultLootSet()` (`src/Entity.cpp:1997-2048`)
- Any ET_CORPSE except sentry-bot subtype: `lootSet[0] = 1089` = class 0, idx 17, ×1 =
  **Health Pack** (`INV_HEALTH_PACK=17` `src/Enums.h:218`). (Compares against
  `MONSTER_SENTRY_BOT`=11, not `CORPSE_SKELETON`, `src/Enums.h:71,79`.)
- Live monsters (used when they die as corpses):
  - ZOMBIE(0): `0x600 | (sprite%3 + 1)`; parm1: `%5+3`; parm2: `%8+5` → class-6 named string
    (`src/Entity.cpp:2015-2025`);
  - CACODEMON(6): `0x2100 | sprite%5+3` → 3–7 cells; MANCUBUS(8)/REVENANT(9): `0x2140 | …` →
    1–3 / 3–5 rockets; SENTRY_BOT(11): `0x2040 | sprite%6+6` → 6–11 bullets
    (`src/Entity.cpp:2026-2041`);
  - default (imps etc.): `0x6000 | findRandomJokeItem()` — per-map joke string table
    (`src/Entity.cpp:2042-2045`, `2050+`).
- Sentry-bot corpses built programmatically: `spawnSentryBotCorpse` uses def `find(9,11,{0,1})`,
  `populateDefaultLootSet()` then `addToLootSet(2,1,10)` or `(0,12,1)` plus health/water entries
  (`src/Game.cpp:2770-2801`).

### 2.3 EV_ASSIGN_LOOTSET (opcode 83)
`S,B,n×S` (`src/ScriptThread.cpp:1782-1804`): sprite = S&0xFFF; resolves entity through
`mapSprites[S_ENT]` (error 117 if none); `b8 = lootSet != nullptr`; consumes B and n×u16 operands
**regardless**; writes entries only if `b8`, zero-fills the rest of the 3 slots. Because
`initspawn()` guarantees non-null for monsters/corpses (§1.1), the assign works on living monsters
too — the loot lands in their future corpse.

### 2.4 Loot UI flow (state `ST_LOOTING`)
- Enter: `Canvas::setState` hook → `lootingSystem.onEnterLooting(destPitch)` (`src/Canvas.cpp:1142-1143`),
  which clears soft keys, starts 500 ms crouch (`LOOTING_CROUCH_TIME` `src/Canvas.h:46`)
  (`src/LootingSystem.cpp:26-33`).
- `poolLoot(pos)` scans the whole tile chain `findMapEntity(x,y,512)…nextOnTile`
  (`src/LootingSystem.cpp:154-224`):
  - processes only `eType == 9`; marks source looted **immediately**: `++entity->param` (prop,
    required `param==0`) or `monster->flags |= 0x800`; sets `info |= 0x400000` (`:163-179`);
  - copies up to 3 entries into `lootPool[]`, merges dupes, sums `lootPoolCredits`
    (`:180-221`; note the original indexes `entity->lootSet[j]` where `lootPool[j]` was meant,
    `:189` — harmless quirk, reproduce faithfully);
  - builds `lootText` lines: class-6 → `<icon> string`; class-1 → "weapon" line (str 91);
    else "N ×longName" (str 90); credits line; empty-corpse fallback str 228 (`:225-261`).
- Per-frame `lootingState()` drives crouch camera lerp, plays sound 1055 once settled
  (`src/LootingSystem.cpp:35-83`, sound at `:62-65`); stand-up ends with
  `setState(ST_PLAYING); app->game->advanceTurn()` (`:79-81`).
- **Loot-crouch camera** (details: `docs/research/2026-08-25-camera-pitch-loot.md`): not a
  cutscene — `lootingState()` rewrites `viewX/Y/Z/viewPitch` every frame and calls
  `updateView()` (`src/LootingSystem.cpp:47-59,:67-78`). Two 500 ms phases; crouch slides
  the eye 48 u forward, drops it +36→+26 above blended floor, and pitches
  `cachedPitch → cachedPitch−64` (22.5° down @ 1024/circle, clamped ≥ −64); stand-up is
  the mirror. `cachedPitch = canvas->destPitch` captured at `setState(ST_LOOTING)`
  (`src/Canvas.cpp:1142-1143`). FOV widens by |pitch| in projection
  (`src/TinyGL.cpp:199`). Pitch positive = up (slope target from
  `src/MovementController.cpp:273`, clamp ±64 at :275-280).
- Input: `InputEventController` routes to `handleLootingEvents` while ST_LOOTING
  (`src/InputEventController.cpp:432-434`; touch → `handleEvent(6)` `src/TouchController.cpp:29-31`).
  FIRE pages 3 lines at a time; on last page (or BACK/PASSTURN) closes and **then** grants
  (`src/LootingSystem.cpp:85-117`).
- `giveLootPool()` (`src/LootingSystem.cpp:281-307`): `player->give(class,idx,count,false)` per
  entry (class-6 skipped); weapon entries add starter ammo `max(usage,10)` of type
  `weapons[idx*9+AMMOTYPE]` (`:290-296`); credits via `give(0,24,credits)` (`:299-302`);
  `foundLoot(x,y,z,numLootItems)` bumps the run stat (`src/Game.cpp:3541-3543`).
- Drawing: `Canvas` paints `drawLootingMenu` during ST_LOOTING (`src/Canvas.cpp:469-472`), ticked
  from the state machine (`src/Canvas.cpp:939-942`).

### 2.5 Script loot — EV_GIVELOOT (opcode 41)
Different path: `composeLootDialog()` parses `B, n×S`, **grants instantly**
(`player->give(...)` inside the composer, `src/ScriptThread.cpp:2135-2174`), shows a dialog
(`showingLoot=true`, `setState(ST_DIALOG)` `:2131-2134`), pauses thread until closed
(`unpauseTime=-1` `:1141-1147`); supports header naming via `lootingSystem.lootSource`
(set from the faced eType-10 object, `src/PlayingInputHandler.cpp:189-195`, consumed
`src/ScriptThread.cpp:2123-2126`). `throwAwayLoot` threads skip UI/toasts
(`src/ScriptThread.cpp:965-969`, set `src/Game.cpp:3523-3531`).

## 3. Pickup model — three paths compared

| | World item (walk-over / script-touch) | Corpse loot | Script GIVEITEM |
|---|---|---|---|
| Trigger | `Entity::touched()` on overlap or opcode 33 mode 0 | ACTION_FIRE on facing adjacent corpse | opcode 33 |
| Grant | `touchedItem()` → `player->give(...)` | pool → `player->give(...)` on UI close | `give` direct (qty<0) or drop+touched (qty>0) |
| Removal | entity removed / def nulled (`src/Entity.cpp:127-130,276`) | none (marked looted) | drop removed on successful touch (`:999-1002`) |
| Feedback | HUD msgs + sound 1054 + foundLoot | loot list UI, sound 1055, foundLoot | same as world item (drop path) |

- `Entity::touched()` handles `ET_MONSTERBLOCK_ITEM`/`ET_ITEM`; success runs static script 11
  SCR_ITEM_PICKUP with `scriptStateVars[11]=def->tileIndex` (`src/Entity.cpp:123-132`;
  slot table `docs/original-code/tile-events-vm.md` §1.4).
- `touchedItem()` per class (`src/Entity.cpp:152-278`):
  - IT_INVENTORY: qty = dropped? `param` : (credits parm24 → `2+rnd%3`, else 1)
    (`:155-162`); fail → "full" msg str 83 (`:163-169`); keycards parm 19/20 → str 84 +
    `hud->repaintFlags |= 0x4` (`:171-175`); credits → str 86 "N ×name" (`:176-184`); others
    str 85 "You got the X" (`:186-190`); `foundLoot` unless dropped or parm 18/journal
    (`:192-194`).
  - IT_FOOD: +40/+20 health, fail str 46 (`:196-207`).
  - IT_AMMO: dropped→`param`, world→`2+rnd%4` (even for parm 2) (`:209-219`);
    `give(2,parm,q)`; ok → str 86 + foundLoot (`:220-236`).
  - IT_WEAPON: sentry-bot variants handled separately (`:238-253`); normal:
    `give(1,parm,1)` + starter ammo 10 (8 if `1<<parm & 0x200`, i.e. scoped rifle parm 9)
    of `weapons[parm*9+AMMOTYPE]` (`:254-264`) + str 85 + `showWeaponHelp` (`:265-270`) +
    foundLoot (`:272-274`).
  - Always `removeEntity` + sound 1054 on success (`:276-277`).
- EV_GIVEITEM (opcode 33) `B,B,b` (`src/ScriptThread.cpp:960-1008`):
  - flag(B3)==0: touch existing sprite `(B1<<8)|B2` → `entities[S_ENT].touched()`
    (`:970-979`);
  - else: def = `lookup(B1)` **by tileIndex**; qty = `(char)B2`:
    - qty < 0 → silent `player->give(def->eSubType, def->parm, qty, true)` — used to
      **take** items (see §6 door keycard consumption) (`:985-990`);
    - qty > 0 → `spawnDropItem(0,0,…)` then `touched()` = instant grant with full pickup
      feedback; failure removes the drop (`:996-1002`);
    - failure any path sets `n2=1` → `scriptStateVars[7]` (`:240,966-968,977,988,1001`).
- Dropped-entity plumbing: `spawnDropItem` reuses free drop ents, `param` = qty,
  `info |= 0x400000` (+0x20000 for eType 10), scale 64, optional toss lerp
  (`src/Game.cpp:2647-2679`); `throwDropItem` animates (`:2600-2645` area).
  Monster-death roll `spawnDropItem(Entity*)` uses **only** `lootSet[0]`, skips class 6,
  needs `find(6,class,idx)` and nonzero count (`src/Game.cpp:2692-2723`).
- EV_DROPMONSTERITEM (35) / EV_DROPITEM (25) place world drops directly
  (`src/ScriptThread.cpp:1023-1057`, `846-860`).

### 3.1 World item spawn & touch trigger
> Verified 2026-08-29, raw log `docs/research/2026-08-29-world-item-pickup.md`.

**Spawn.** Items have no dedicated loader: `Game::loadMapEntities()` (`src/Game.cpp:329`)
creates an entity for every map sprite whose tile index resolves to a def
(`lookup(n6)`, `:404`; `n6 = mapSpriteInfo & 0xFF`, +257 iff bit 0x400000, `:375-378`).
`eType == 6` (ET_ITEM) is decided purely by the def table — the loader has no item branch.
- `entity->info = (spriteIndex + 1) & 0xFFFF` (`:428`), `def = lookup` (`:429`),
  `initspawn()` (`:451`), `mapSprites[S_ENT + sprite] = entIndex` (`:452`), and
  `linkEntity(x>>6, y>>6)` **only if** `mapSpriteInfo & 0x10000` is clear (`:453-455`) —
  a hidden item is not in `entityDb` and cannot be touched.
- `Entity::initspawn()` has **no** ET_ITEM case (`src/Entity.cpp:49-112`): only
  `name = def->name | 0x400` (`:53`); no `0x20000` bit; `lootSet` deleted (`:103-108`).
  A world item therefore has `param == 0` (unlike dropped items, where `param` = quantity).
- Def fields used at pickup: `eSubType` = item class (0 IT_INVENTORY, 1 IT_WEAPON,
  2 IT_AMMO, 3 IT_FOOD, `src/Enums.h:96-99`), `parm` = index inside the class,
  `name`/`longName` = message strings, `tileIndex` = sprite art + script arg.
  **Quantity is never stored in map data** — it is rolled inside `touchedItem()` (§3).
- Def record (`entities.bin`) = 8 bytes little-endian: `s16 tileIndex, u8 eType,
  u8 eSubType, u8 parm, u8 name, u8 longName, u8 description`
  (`src/EntityDef.cpp:35-43`; LE per `src/Resource.cpp:136-140`). `EntityDef::touchMe`
  is declared but never read from the file. Shipped table: 190 defs, 46 with `eType==6`,
  **0 with `eType==11`** → the ET_MONSTERBLOCK_ITEM branch of `touched()` is dead code
  for shipped data. 16 item defs have `tileIndex == 0` (drop-only, reachable via
  `find(eType,eSubType,parm)`); world-placeable item tiles: 1..15 weapons
  (`src/Enums.h:649-664`), 85/86/88/89/90 ammo, 107/110-117/119 inventory+food.
- Placement/scale come from sprite data only: scale 64, Z 32 by default
  (`src/LoadingManager.cpp:474-481`), then `Z += getHeight(x,y)` (and −32 for z-sprites)
  in `postProcessSprites` (`src/Render.cpp:2462-2467`); sprite X/Y are 1 byte × 8 units
  (`src/Resource.cpp:154-156`).

**Draw.** `Render::renderSpriteObject` skips sprites with `mapSpriteInfo & 0x10000`
(`src/Render.cpp:1501-1503`), takes the frame index from bits 8-15 (`:1511`), and cycles
frames only when bit `0x80000` is set (`n7 = (n + time/100) % n7`, `:1544-1546`).
Items have none of the special cases → plain `renderSprite`, **no bob, no idle animation**.
World weapons (tile 1..12) get frame 2 forced at load: `mapSpriteInfo |= 0x200`
(`src/Game.cpp:405-407`); `spawnDropItem` does the same for `1 <= tile < 14`
(`src/Game.cpp:2653-2655`) — ranges differ by one (tiles 13/14, the red sentry bot,
keep frame 0 when placed on a map).

**Touch trigger.** `Game::touchTile(x, y, b)` (`src/Game.cpp:687-699`) walks the whole
`entityDb` tile chain (`findMapEntity(x,y)` = `entityDb[(y>>6)*32 + (x>>6)]`,
`src/Game.cpp:729-737`), caching `nextOnTile` **before** the call because `touched()` may
unlink, and calls `touched()` on every entity when `b`, else only on ET_ENV_DAMAGE.
Criterion is pure **tile occupancy** — no radius, no facing, no contents mask.
Type filtering lives inside `Entity::touched()` (`src/Entity.cpp:118-149`): ET_ITEM /
ET_MONSTERBLOCK_ITEM → `touchedItem()`, ET_ENV_DAMAGE → damage, everything else false
(the world entity `entities[0]` and the player copy `entities[1]` are in the chain too).
- The only pickup-capable call site is `MovementController::finishMovement()` with
  `b = true` (`src/MovementController.cpp:170`). All other sites pass `false`
  (`src/PlayingInputHandler.cpp:400,552`, `src/ZoomController.cpp:110`,
  `src/GameStateRunner.cpp:48`) and thus only apply environmental damage.
- Order inside a step (`src/MovementController.cpp:160-184`): gotoThread → `executeTile`
  facing-dir flags (`:168`) → `executeTile` movement flags (`:169`) → `touchTile(...,true)`
  (`:170`) → `advanceTurn()` (`:183`). So the pickup happens *after* the walk lerp has
  landed on the tile and *after* that tile's scripts, and costs **no extra turn**.
  `finishMovement()` runs from the lerp end (`src/MovementController.cpp:509-512`) or from
  an early input snap (`src/PlayingInputHandler.cpp:45-52`).
- **Items never block**: `attemptMove` traces with `CONTENTS_PLAYERSOLID = 13501`
  (`src/MovementController.cpp:326`, `src/Enums.h:29`) = eType bits {0,2,3,4,5,7,10,12,13};
  bits 6 (ET_ITEM) and 11 (ET_MONSTERBLOCK_ITEM) are clear. `CONTENTS_MONSTERSOLID = 15535`
  (`src/Enums.h:30`) = bits {0,1,2,3,8,10,11,12,13} *does* include 11 — ET_MONSTERBLOCK_ITEM
  blocks monsters only. `Game::trace` filters candidates by `mask & (1 << def->eType)`
  (`src/Game.cpp:221`); the move is taken only if `traceEntity == nullptr`
  (`src/MovementController.cpp:332-333`).
- Failure (`touchedItem()` false: inventory/health full, bot already owned) leaves the
  entity linked and visible, plays no sound and runs no script.

**Removal.** `touchedItem()` ends with `removeEntity(this)` then sound 1054
(`src/Entity.cpp:276-277`); `touched()` then sets `scriptStateVars[11] = def->tileIndex`
and runs static func 11 (SCR_ITEM_PICKUP) (`src/Entity.cpp:125-126`), and for dropped
entities clears the sprite→entity backlink and the def (`:127-130`).
`Game::removeEntity` (`src/Game.cpp:183-192`): hides the sprite
(`mapSpriteInfo[sprite] |= 0x10000`, only if `info & 0xFFFF` non-zero), `unlinkEntity` if
`info & 0x100000`, and `player->facingEntity = nullptr`. `unlinkEntity` splices the chain
and clears the link bit (`info &= 0xFFEFFFFF`, `src/Game.cpp:70-92`).
A world item keeps its slot, `def`, sprite id and `param` — only hidden + unlinked
(restorable via `Entity::restoreBinaryState`, `src/Entity.cpp:1861+`); a dropped item's
slot is fully recycled for `getFreeDropEnt` (`src/Game.cpp:2612-2622`).

### 3.2 Action-button ("look + Enter") pickup — it is a *script* path, not an entity path
> Verified 2026-08-30, raw log `docs/research/2026-08-30-action-pickup.md`.

**Headline: `ACTION_FIRE` never calls `touched()` on an item.** The whole `ACTION_FIRE`
branch (`src/PlayingInputHandler.cpp:189-540`) contains no `touched()`/`touchedItem()` call,
and its trace mask cannot even return an item. Items on shelves/ledges are picked up because
the action button runs the **tile script of the tile in front**, and that script executes
`EV_GIVEITEM` in "touch existing sprite" mode, which calls `Entity::touched()` on the item.

#### 3.2.1 `Player::facingEntity` — what the player "looks at"
- Computed lazily by `MovementController::checkFacingEntity()` (`src/MovementController.cpp:28-158`),
  guarded by `if (!canvas->updateFacingEntity) return;` (`:32-34`) and cleared at the end
  (`:157`). The **only** caller is `Hud::draw` when the top bar repaints and
  `state == ST_PLAYING` (`src/Hud.cpp:735-740`); the dirty flag is set by movement/turn end
  (`src/MovementController.cpp:180,308`), `setState(ST_PLAYING)` (`src/Canvas.cpp:1075`),
  entity death/removal, scripts, etc. So it is *event-driven*, not per-frame.
- Ray: `game->trace(dest + view*28>>14, ..., dest + view*6>>8, nullptr, 21741, 2, isZoomedIn)`
  (`src/MovementController.cpp:36-40`). View matrix is 14.14 (`16384 = 1.0`), so the ray starts
  **28 units** in front of the player and ends `16384*6/256 = 384` units = **6 tiles** ahead;
  trace radius (`n5`) = 2 units.
- Mask `21741` = bits {0,2,3,5,6,7,10,12,14} = WORLD, MONSTER, NPC, DOOR, **ITEM(6)**, DECOR,
  ATTACK_INTERACTIVE, SPRITEWALL, DECOR_NOCLIP. `ET_MONSTERBLOCK_ITEM` (11) is *not* in it.
  → a world item can be the facing entity and the HUD prints its name
  (`src/Hud.cpp:284-320`), which is why pickup *feels* like "look at it and press Enter".
- Candidate = nearest hit (`traceEntities` bubble-sorted by fraction, `src/Game.cpp:311-325`),
  with a fix-up loop that prefers a monster / decor / interactive behind a spritewall
  (`src/MovementController.cpp:41-86`).
- Distance clamp: `facingEntity = nullptr` if `def->eType != 2` (monster) and
  `distFrom(viewX,viewY) > tileDistances[2]` (`:90-93`). `Entity::distFrom` is
  `max(dx*dx, dy*dy)` (`src/Entity.cpp:1155-1158`) and `tileDistances[j] = (64*(j+1))^2`
  (`src/Combat.cpp:42`) → non-monsters must be within **3 tiles** (Chebyshev, squared).
- **No height test**: `Game::trace` only filters by Z when its last argument `b` is true,
  i.e. only while zoomed in (`src/Game.cpp:283-292`; the entity is otherwise added by the
  pure-2D `CapsuleToCircleTrace` with radius parameter 625, `:277`). A shelf item at
  `Z = floor+64` is therefore just as "faceable" as one on the floor.
- Extra: facing an adjacent (`<= tileDistances[0]`, i.e. 1 tile) `ET_ITEM` with
  `eSubType == 3` (IT_FOOD) fires `showHelp(4)` (`src/MovementController.cpp:115-117`).

#### 3.2.2 `ACTION_FIRE` chain (`src/PlayingInputHandler.cpp:189-540`), in order
1. `lootingSystem.lootSource = facingEntity->name` iff facing an `eType == 10` object, else −1 (`:190-195`).
2. Weapon trace: `n5 = 13997` (`CONTENTS_WEAPONSOLID`, `src/Enums.h:32`) `|= 0x4100` for
   weapon 2 (chainsaw-family/`weapon2==2`), `|= 0x10` and range `n7 = 1` tile for melee
   (`CheckWeaponMask(weapon,2)`), otherwise `n7 = 6` tiles (`:197-221`).
   **13997 has neither bit 6 (ITEM) nor bit 11 (MONSTERBLOCK_ITEM)** → the fire trace can
   never return a world item, no matter the distance or height.
3. Selection loop over `traceEntities` (`:223-368`): world/spritewall/playerclip stop it;
   corpses (`eType 9`) at exactly `tileDistances[0]` set the loot flag `n6`; `eType 13`
   → `entity2`; NPCs, doors, env-damage, decor tile 0x95, etc.
4. `if (n6 != 0) { setState(ST_LOOTING); poolLoot(...); return; }` (`:374-378`) — corpse loot
   short-circuits everything below (§1.5).
5. Range cull for `eType 10`, `entity2` substitution, `eType 10 / eSubType 2` grab (`:379-393`).
6. **Tile script of the tile in front** (`:395-404`):
   ```
   int flagForFacingDir = canvas->flagForFacingDir(4);
   int n13 = canvas->destX + canvas->viewStepX >> 6;
   int n14 = canvas->destY + canvas->viewStepY >> 6;
   if (app->game->executeTile(n13, n14, flagForFacingDir, true)) {
       if (!app->game->skipAdvanceTurn && canvas->state == Canvas::ST_PLAYING) {
           app->game->touchTile(canvas->destX, canvas->destY, false);
           app->game->snapMonsters(true);
           app->game->advanceTurn();
       }
   }
   ```
   `viewStep{X,Y} ∈ {0,±64}` (`src/Canvas.h:108`) → exactly the adjacent tile (8 directions).
   `flagForFacingDir(4)` = `4 | 1 << (((destAngle+512) & 0x3FF) >> 7) + 4`
   (`src/MovementController.cpp:214-224`): exec-type bit **4 = TRIGGER** plus the direction bit
   for the *reversed* facing (i.e. "player stands to the S of me"). Bit 4 is used **only** here;
   `finishMovement`/turn use bit 8 = FACE on the player's own tile (`:168,307,508`).
7. Only if no script ran: waterspout / door (`performDoorEvent` + `advanceTurn`, `:445-459`),
   secret spritewall, wall push, zoom, `fireWeapon` (`:405-540`).

`touchTile(destX, destY, false)` at `:400` is **not** a pickup: with `b == false`
`Game::touchTile` only touches `ET_ENV_DAMAGE` entities (`src/Game.cpp:687-698`).
The same `false` is used by `GameStateRunner.cpp:48`, `ZoomController.cpp:110`,
`PlayingInputHandler.cpp:552` (pass turn); only `MovementController.cpp:170` passes `true`.

#### 3.2.3 The actual grant: `EV_GIVEITEM` mode 0
`EV_GIVEITEM` (opcode 33, `B,B,b`) with the third operand `== 0` resolves
`sprite = (B1 << 8) | B2`, reads `mapSprites[S_ENT + sprite]` (error 16 if −1) and calls
`entities[n].touched()` (`src/ScriptThread.cpp:969-980`) — i.e. the *identical* grant path as
walking over the item (§3), including messages, sound 1054, `foundLoot` and `removeEntity`.
Failure (inventory full) sets `n2 = 1` → `scriptStateVars[7] = n2` after every opcode
(`src/ScriptThread.cpp:2041`), which is how the shipped scripts test success (`v7 == 0`).

Turn cost: an action pickup **costs a turn** (`advanceTurn` at `:402`) unless the tile event
carries flag `0x40000` (→ `skipAdvanceTurn`, `src/ScriptThread.cpp:76-79`) or the script
changed the state (dialog/camera). A walk-over pickup costs no *extra* turn — the move itself
already ends in `advanceTurn` (`src/MovementController.cpp:178-184`).

#### 3.2.4 What a "shelf" is in the data (map00, verified)
Map sprites are split into `numNormalSprites` + `numZSprites`; only the second group stores a
per-sprite Z byte (`src/LoadingManager.cpp:379-381,527`), and `postProcessSprites` adds the
floor height and subtracts 32 for z-sprites (`src/Render.cpp:2462-2467`). Normal sprites get
Z = 32 (`src/LoadingManager.cpp:474-481`). So a "shelf" item is simply a **z-sprite with a raw
Z byte > 64** (raw 96 → 64 units above the floor); nothing else marks it. There is no separate
flag, no `touchMe`, and no shelf entity — in map00 the item sprites are alone on their tile.

map00 evidence (tool: `tools/disasm_map_scripts.py tmp_map00.bin`, sprite table via
`tools/map_to_obj.py`):

| tile | event (trigger) | script | sprites on tile (raw Z) |
|---|---|---|---|
| 12,17 | `EVT 34` TRIGGER, all dirs | `v72==0 ? GIVEITEM sprite 245 : GIVEITEM sprite 244` + `EVENTOP disable` | 245 ammo (Z 64), 244 inventory parm 11 (Z 96) |
| 13,15 | `EVT 22` TRIGGER, all dirs | `GIVEITEM sprite 233` | 233 ammo (Z 96) |
| 8,24 | `EVT 103` TRIGGER, all dirs | `v71==0 ? sprite 237 : sprite 238` | 237 (Z 96), 238 (Z 64) |
| 25,17 | `EVT 37` TRIGGER, all dirs | `GIVEITEM sprite 235` | 235 ammo (Z 92) + object sprite 114 |

Each script advances its state var only when `v7 == 0` (previous op succeeded), so a full
inventory does not consume the shelf. None of these events carries `0x40000`, so each press
costs a turn. Floor items (map00 sprites 1, 4, 35, 64, 110, 129) are *normal* sprites with
Z = 32 and have **no** tile event — they are walk-over only.

#### 3.2.5 Double pickup
The two paths do not know about each other:
- Walk-over → `touchTile(...,true)` → `touched()` → `removeEntity()` hides + **unlinks** the
  entity (`src/Game.cpp:183-192`), so `findMapEntity` can never return it again.
- Script → `mapSprites[S_ENT + sprite]` **bypasses the tile chain**, and neither `touched()`
  nor `touchedItem()` checks the hidden bit `0x10000` (`src/Entity.cpp:118-278`).

Consequently, if an item that a script also gives sits on a walkable tile, picking it up by
walking and *then* triggering the script grants it a second time. The shipped data avoids this
with script state vars (`v71`/`v72` above) — which track *presses*, not the item — and by
placing the shelf items in wall niches. In map00 the niche at (12,17) is in fact walkable from
(12,18) (tile flag 0 = not solid, `tile_flags` bit 0x1 = solid world, `src/Game.cpp:497-503`),
so the double grant is reproducible there. Port note: reproducing the original faithfully means
*not* adding an "already taken" guard inside `touched()`.

## 4. Inventory model

### 4.1 Storage
- `short inventory[26]`, `short ammo[9]`, `int weapons` bitmask, `int disabledWeapons`
  (`src/Player.h:45-56`).
- Slot names: `INV_*` block `src/Enums.h:196-232`: 0–10 drinks (`INV_DRINK_*`), 11/12 armor
  large/small, 13 bottled water, 16 ration bar, 17 health pack, 18 journal,
  **19 RED_KEY, 20 BLUE_KEY**, 21 empty syringe, 22 holy water, 23 pack, 24/25 credit denominations;
  `INV_MAX=26`, `INV_MAX_ITEM_COUNT=999`, `INV_MAX_CREDITS=9999`.
- Ammo types `AMMO_*` `src/Enums.h:103-118` (9 slots; 6 soul cube cap 5, 7 sentry-bot).
- Weapons `WP_*` `src/Enums.h:135-152` — 15 bits, `WP_ITEM=14` unusable; bit `1<<wp`.

### 4.2 `Player::give(class, idx, qty[, quiet[, familiarReal]])` (`src/Player.cpp:962-1063`)
- qty==0 → false (`:972-974`).
- class 1 (weapon): sentry-bot guns clear other bot weapons & seed ammo (`:978-984`);
  new-weapon detection `b3 = !(weapons & bit)` (`:985`); qty<0 **removes** the bit and
  reselects if it was active (`:986-994`); familiar mirrors into `weaponsCopy` unless
  `b2` (`:995-1000`); `showWeaponHelp` unless quiet (`:1001-1003`); **auto-equip on first
  acquisition** `if (b3 && !isFamiliar) selectWeapon(idx)` (`:1004-1006`).
- class 0 (inventory): target array = `inventory` (familiar copy rules `:1010`);
  `new = qty + arr[idx]`; cap 9999 for idx 24, else 999; negative result → false
  (`:1011-1022`); **idx 13 bottled water converts to holy-water ammo ×20/unit**
  `give(2,3,new*20,true,true)` (`:1023-1025`); `showInvHelp` unless quiet (`:1029-1032`).
- class 2 (ammo): cap 100, soul cube (idx 6) cap 5, negative → false; `hud->repaintFlags |= 0x4`
  always (`:1035-1052`).
- class 3: `addHealth(qty)` (`:1054-1057`).
Stacking is pure counters — no slot pressure; "full" only via the caps above.

### 4.3 Active weapon
- State: `ce->weapon` + `activeWeaponDef = find(6,1,wp)` (`src/Player.cpp:153,160`).
- `selectWeapon(i)`: familiar-immune; wp≠14 clears sentry-bot pair bits `0xFFFFBFFF` and zeroes
  `ammo[8]` (`:138-141`); zoom-out; falls back to `selectNextWeapon()` if masked/unowned
  (`:145-148`); sets prevWeapon, `readyWeapon()` sound hook on change (`:149-159`).
- `selectNextWeapon/selectPrevWeapon` scan the owned mask `weapons & ~disabledWeapons`, skipping
  ammo-fed weapons with 0 ammo except holy-water pistol (id 2) (`:165-217`).
- Manual cycle: ACTION_NEXTWEAPON/PREVWEAPON → same helpers + "switched to" message
  (`src/PlayingInputHandler.cpp:122-137`); wp 14 rejected (`:124-126`).
- Combat weapon table stride 9: `[wp*9+AMMOTYPE=4]`, `[+AMMOUSAGE=5]` (`src/Combat.h:26-36`).
- EV_INVENTORY_OP strips/restores everything for VIOS fight/target practice
  (`src/ScriptThread.cpp:1627-1643` → `Player::stripInventory*`/`restoreInventory`).
- Journal: slot 18 exists but quest data lives in `notebookIndexes/questComplete/…`
  (`updateQuests` `src/Player.cpp:1073-1117`); journal pickup never counts as loot
  (`src/Entity.cpp:192`).

## 5. HUD

- Textures: `imgWeaponNormal` = `Hud_Weapon_Normal.bmp`, 44-px rows (`src/Hud.cpp:59`);
  current-weapon button `btnWeapon` 268,258 (`:73`).
- `Hud::drawWeaponButton` maps weapon id → texture row `texY` (hand/boot/snooper/rifle/…),
  highlighted variant swaps in `imgWeaponActive`; ammo digits drawn from
  `ammo[combat->weapons[9*weapon+4]]`; soul cube (13) renders "N  5"
  (`src/Hud.cpp:1090-1124`, digits `:1127-1153`).
- Weapon-select overlay: `drawWeaponSelection` iterates 15 bits of
  `~disabledWeapons & weapons`, lays out `imgWeaponNormal` buttons 4-per-row centered,
  registers touch areas (`src/Hud.cpp:1181-1210`, `MAX_WEAPON_BUTTONS=15` `src/Hud.h:43`).
- Keycard indicator: `drawCurrentKeys` picks row `v9 ∈ {0,1,2,3}` from
  `inventory[19]`/`inventory[20]` (red/blue), draws 44-px row of `imgKeyActive`/`imgKeyNormal`
  (`src/Hud.cpp:1155-1179`).
- Refresh triggers: ammo/keycard pickups set `repaintFlags 0x4`
  (`src/Player.cpp:1051`, `src/Entity.cpp:174`); weapon switch forces repaint + soft-key redraw
  (`src/Player.cpp:154-162`).
- Pickup feedback strings (localization ids): 83 inventory-full, 84 keycard, 85 "got the X",
  86 "N ×X"; loot list 90/91; empty corpse 228; loot title 129/130; credits name 157
  (`src/Entity.cpp:163-190`, `src/LootingSystem.cpp:236-260`, `src/ScriptThread.cpp:2123-2129`).
- Sounds: pickup 1054 (`src/Entity.cpp:277`), loot-menu settle 1055
  (`src/LootingSystem.cpp:64`).

## 6. map00 intro tutorial (bytecode-verified)

Spawn (4,19) facing E (`tmp_map00.bin` header; also `docs/original-code/tile-events-vm.md` §5).

- **Corpses created by script (INIT_MAP, staticFunc[0]@0 tail)**:
  - `@1091 EVAL [var26==0]` → true-branch `@1098/@1103 MAKE_CORPSE sp41@(14,3)`, `sp42@(14,4)`
    (in place — sprites 41/42 are live imps, tileNum 23 → def `(2,3,0)`);
    else-branch `@1111/@1116` relocates them to **(4,21)** and (8,18). `var26` has no writer in
    map00 bytecode → fresh games take the in-place branch; the near-spawn variant exists for
    alternate flows.
  - Helper fn `@1122 MAKE_CORPSE sp121@(27,23)` (its own tile; imp carrying a 3-entry lootset,
    see below).
- **Lootsets (ASSIGN_LOOTSET, all inside INIT_MAP @581-665)**: sp29 **[blue keycard ×1]**
  (skeleton corpse at (13,2), def `(9,17,0)`); sp111 [energy blast ×1]; sp70 [super quench ×1];
  sp49 [ration bar ×1]; sp17 [7 credits]; sp121 [snake venom ×1, caffeine block ×1, 36 credits];
  sp72/76/66/62/53/61/38/86 = class-6 named flavor strings (str 171–179) only.
  No map00 lootset contains a **weapon** (no class-1 anywhere).
- **Gun comes purely from script**: EVT tile 617 @(9,19) TRIGGER (intro cinematic by the blue
  door) grants `@3401 GIVEITEM def=1 qty=1` — def 1 = `(6,1,0)` = ET_ITEM/IT_WEAPON/
  **WP_ASSAULT_RIFLE** — then `@3414/@3428/@3442 GIVEITEM def=85 qty=90/40/10` (def 85 =
  `(6,2,1)` bullets). All use flag=1, qty>0 → drop+instant-touched pickup with normal
  "You got…" feedback. No loot route is involved.
- **Keycard flow**: loot corpse sp29 → `inventory[20]=1`; blue-door trigger EVT 618 @4161
  checks ITEM_COUNT inv[20], and its grant step is actually
  `@4175 GIVEITEM def=111 qty=-1` — **silent take of 1 keycard**, then UNLOCK+OPEN
  (byte-verified: `21 6F FF 01`; same pattern red door `@4782: 21 6E FF 01`). Keycards are
  consumed, contradicting the earlier "re-grant" reading in `tile-events-vm.md` §5 (corrected
  there). PER_TURN poller marks the journal quest when inv[20]==1
  (`docs/original-code/tile-events-vm.md` §5.3).
- Remaining GIVEITEMs: 4 mode-0 touch-sprite sites around @3971-4143 and 17 post-@8372
  (VIOS battle scripts, sprites 0xD9–0xF5) — all "pick up placed world item" calls
  (`docs/research/assets/map00_disasm.txt`).
- Search-object events use EV_GIVELOOT (instant-grant dialog): EVT tiles 583/830/916
  @1686-1884 (medkits, bullets, drinks, credits) — distinct from corpse looting.

## 2.6 Loot dwell + menu UI port spec (2026-08-25)

> Verdict CONFIRMED; full method/evidence in `docs/research/2026-08-25-loot-ui-spec.md`.
> §2.4 already covers pooling/grant math and entry conditions; this section is the
> interaction + pixel-exact UI spec for the dwell window between crouch-settle and stand-up.

### Session timeline

`setState(ST_LOOTING)` → `poolLoot()` **immediately after**, in that order
(`src/PlayingInputHandler.cpp:374-378`) — corpse(s) marked looted + list text built at ENTRY,
not at close. Phase A crouch lerp 500 ms (`src/LootingSystem.cpp:42-51`); at settle sound 1055
plays once per session (latch `field_0xac5_`, set in `onEnterLooting` `src/LootingSystem.cpp:32`,
checked `:62-65`). Then the **dwell**: crouch pose held every frame (`:66-72`) and
`drawLootingMenu` paints the list (`:122`). Close (input) → `giveLootPool()` runs **before**
standing up, `crouchingForLoot=false`, `lootingTime=now` restarts the clock with zero extra delay
(`:89-103`). Phase B stand-up is another 500 ms; on expiry snap + `setState(ST_PLAYING)` +
`advanceTurn()` (`:73-81`). Input during both transition windows is dropped by the guard
`crouchingForLoot && app->time > lootingTime+500` (`src/LootingSystem.cpp:87`) — note it also
blocks everything during stand-up because `crouchingForLoot` is false then.

### Input (dwell only) — `src/LootingSystem.cpp:85-117`

Line count `n = numPoolItems + (lootPoolCredits ? 1 : 0)`; scroll bound
`max = max(n - 3, 0)` (`:88`); `lootLineNum` = index of the top visible line, reset to 0 by
`poolLoot` (`:161`).

| Action | Effect | Cite |
|---|---|---|
| ACTION_FIRE | if `lootLineNum >= max` → `lootingTime=now; crouchingForLoot=false; giveLootPool();` (close+grant+start stand-up); else page `lootLineNum = min(lootLineNum+3, max)` | `:89-98` |
| ACTION_PASSTURN / ACTION_BACK | same close+grant immediately, any page | `:99-103` |
| ACTION_DOWN / ACTION_UP | `lootLineNum ± 1`, clamped to `[0,max]` (single-line scroll of the window) | `:104-109` |
| ACTION_LEFT / ACTION_RIGHT | jump to top (`0`) / bottom (`max`) | `:110-115` |

So with ≤3 lines (`max==0`, incl. empty corpse) the FIRST FIRE closes — there is no
"press again to confirm". Routing: ST_LOOTING → `handleLootingEvents(keyAction)`
(`src/InputEventController.cpp:432-434`). KEY_CLR as legacy raw code ±18 is swallowed upstream
(`src/InputEventController.cpp:164-184`, only LOGO/MENU/INTRO_MOVIE react); AVK_CLR maps to
ACTION_BACK (`:25`) and closes like PASSTURN. Touch during ST_LOOTING synthesizes key 6
(`src/TouchController.cpp:29-31`) → `keys_numeric[5]` = **ACTION_FIRE** (table 5
TBL_CANVAS_KEYSNUMERIC = `[5,9,1,10,3,6,4,12,2,14]`; loader `src/App.cpp:363,394`,
format `src/Resource.cpp:278-293` + header skip `:217-220,:236-248`; values extracted from
Packages/tables.bin). Any other action id is ignored (no menu/items shortcuts while looting).

### Empty corpse (str 228)

Still enters + crouches (entry checks only `lootSet != nullptr`); list text becomes exactly one
line "None found!" (`src/LoothingSystem.cpp:259-261`; string table below), source is STILL marked
looted (`:163-179`); `max==0` so one FIRE/PASSTURN closes; `giveLootPool` grants nothing but still
calls `foundLoot(..., numLootItems=0)` (= `lootFound += 0`, harmless,
`src/Game.cpp:3541-3543`) and disposes the buffer (`src/LootingSystem.cpp:281-307`). Turn still
consumed via stand-up's `advanceTurn()`.

### Menu geometry & paint (480×320 letterbox)

Drawn by LootingSystem itself — NOT DialogSystem (no style table; its own rects/colors).
Paint order: 3D view → HUD → `drawLootingMenu` state overlay (`src/Canvas.cpp:414-417,470-472`),
so it renders OVER both. Only drawn while dwelling (`src/LoothingSystem.cpp:122`).
`viewRect` = `{screenRect[0], 20, screenRect[2], evenHeight}` — y=20 is hardcoded
(`src/Canvas.cpp:124-127`; 480×320 ⇒ `{0,20,480,250}`, SCR_CX=240 `:122`).

```
dialogRect = { viewRect[0], viewRect[1]+16, viewRect[2]-x-1, 48 }   // :123-127 ⇒ {0,36,479,48}
fillRect(body)   color 0xFF660000 dark red                          // :128-129  ← "красный диалог"
fillRect(title)  {x, y-18, w, 18} color 0xFF000000 black            // :130-131
drawRect(title) + drawRect(body) color 0xFFFFFFFF white border      // :132-134
title: compose str227 -> dehyphenate -> drawString(SCR_CX, y-16, anchor HCENTER)  // :135-139
lines i=0..2: drawString(lootText, x+5, y+1+i*16, anchor TOP|LEFT,
             offset=lootPoolIndices[2*(i+lootLineNum)], len=[...+1])            // :140-144
scrollbar if n > pageSize(3): drawScrollBar(x+w, y+1, h-1, lootLineNum,
             min(lootLineNum+3,n), n, 3)                                        // :145-150
```

Anchors: 1=HCENTER, 20=TOP|LEFT (`src/Graphics.h:13-23`; handling `src/Graphics.cpp:509-543`;
16 px lines, '|' forces newline mid-string `src/Graphics.cpp:546-573`). Text is white
(`currentCharColor=0` → palette row 0 `0xFFFFFFFF`, `src/Graphics.h:26`); each item line starts
with glyph `\x88` rendered as one 12×16 font cell via `getCharIndices`
(`src/Graphics.cpp:583-604,637-659`; 0x88 falls through to default font index).

Scrollbar (`src/Canvas.cpp:1284-1314`): hidden when total ≤ page. Up/down arrow caps are
imgUIImages regions [60,0,7,7]/[60,7,7,7] anchored RIGHT|TOP / RIGHT|BOTTOM at x (`:1305-1306`);
track fill 0xFFB3AA93 rect `(x-7, y+7, 7, h-14)` (`:1307-1308`); thumb fill 0xFFE7CFAD height
`v15 = 3*h / (4*ceil(total/page))`, y-offset `v16 = ((first<<16)/(v12<<8) * ((h-v15-14)<<8))>>16`
with `v12 = max(total-page, first)`, snapped to `h - v15 - 14` on the last page
(`:1301-1304,:1310`); black 1px outlines around thumb and track (`:1311-1313`).

### Line composition (poolLoot tail, `src/LoothingSystem.cpp:225-261`)

Per pool entry, args pushed in order then composed into `lootText`:

- class 6 flavor: append `\x88` + map-string text (`:229-234`);
- class 1 weapon: args [`\x88`, longName] → common str **91** `%01%02|` ⇒ `<icon><Weapon>` (`:241-243`);
- everything else: args [`\x88`, count, longName] → common str **90** `%01%02x %03|` ⇒
  `<icon><COUNT>x <LongName>` (`:245-249`; `%NN` always eats two digits,
  `src/Text.cpp:309-320`);
- credits line (if `lootPoolCredits!=0`): args [`\x88`, credits, str157] through str 90 ⇒
  `<icon><N>x UAC Credits` (`:252-258`) — name is **entity-table** (type 1) string 157 via the
  `(short)1,(short)157` addTextArg overload (`src/Text.cpp:269-274`);
- no items at all: single common str **228** (`:259-261`).

Then `dehyphenate()` strips ALL '-' from the buffer (`src/Text.cpp:714-725`), and line offsets are
recorded into `lootPoolIndices[18]` (9 × <start,len>, split on '|', last pair covers the tail;
`src/LoothingSystem.cpp:263-278`). Strings verified against Packages/strings.idx + strings00.bin
(index format `src/Resource.cpp:169-207`, NUL-split `src/Text.cpp:179-212`):
type0[90]=`%01%02x %03|`, type0[91]=`%01%02|`, type0[227]=`Loot-ed Items:` ("Looted Items:"),
type0[228]=`None found!|`, type1[157]=`UAC Cre-dits` ("UAC Credits").

### HUD / softkeys during ST_LOOTING

Nothing is hidden: `lootingState` re-arms `hud->repaintFlags |= 0x22`
(TOP_BAR|HUD_OVERDRAW, `src/Hud.h:21,25`; `src/LoothingSystem.cpp:38`) and canvas
REPAINT_HUD|REPAINT_VIEW3D (`:39`); arrow controls stay except in ST_DIALOG
(`src/Hud.cpp:749-751`). Soft keys are cleared on entry (`onEnterLooting`,
`src/LoothingSystem.cpp:28`) and only restored by `setState(ST_PLAYING)`'s hook
(hud flags 0x2f, playing keys iff `monstersTurn==0` or oldState==ST_CAMERA,
`src/Canvas.cpp:1065-1079`). The ST_LOOTING `setState` hook does NOTHING else — no clearEvents,
no viewport change (`src/Canvas.cpp:1142-1144`).

### Delta list → new_src (as of 2026-08-25)

Current rewrite auto-grants and never shows a list. Required changes:

1. **Pool at entry**: `GameContext::handlePlayingAction` Use branch stores `pendingLootCorpse_`
   and defers all work (`new_src/core/GameContext.cpp:768-773`). Legacy order is
   `setState(ST_LOOTING)` THEN `poolLoot(...)` (`src/PlayingInputHandler.cpp:374-376`) — build the
   loot-line list + credits and mark looted AT ENTRY (also: legacy pools ALL eType-9 entities on
   the dest tile chain, not just the faced one, `src/LoothingSystem.cpp:154-224`).
2. **Dwell phase**: `tickLooting` grants at crouch settle and immediately starts standing
   (`new_src/core/GameContext.cpp:469-485`). Instead: settle = play sound 1055 (latched) + hold
   pose; grant/close moves into an input handler.
3. **Input routing**: pending actions break on `state != StateId::Playing`
   (`new_src/core/GameContext.cpp:161`). Add a Looting dispatch replicating
   `src/LootingSystem.cpp:85-117` (FIRE page/close, PASSTURN/BACK close, UP/DOWN ±1 clamp,
   LEFT/RIGHT jumps; guard drops input outside the dwell window).
4. **Grant-on-close ordering**: run the grant (existing `Game::lootCorpse` grant pass,
   `new_src/domain/game/Game.cpp:764-788`) when the UI CLOSES, then set `lootCrouch_=false` +
   restart `lootTime_` so stand-up begins with no extra delay (`src/LootingSystem.cpp:89-103`).
   Keep `advanceTurn()` at stand-up expiry (`new_src/core/GameContext.cpp:486-495` ✓ already).
5. **UI overlay**: replace the center-message toast (`new_src/domain/game/Game.cpp:791-818`) with
   the `drawLootingMenu` geometry above (red body 0xFF660000, black title bar str227, 3×16px
   white lines from the built buffer, scrollbar when >3 lines, "None found!" fallback).
6. **Mark-looted timing/unification**: `lootCorpse` marks at grant time and folds monster flag
   0x800 into `param` (`new_src/domain/game/Game.cpp:730-734`); legacy marks at pool time and keeps
   both markers distinct (`src/LoothingSystem.cpp:163-179`) — matters for render sparkle
   (`docs/original-code/loot-inventory.md` §1.5) and re-open prevention.
7. Minor: sound 1055 currently a stderr stub (`:789`); move playback to crouch-settle latch.

## 7. Containers — the "crate" (`ET_ATTACK_INTERACTIVE` / `INTERACT_CRATE`)
> Verified 2026-08-30, raw log `docs/research/2026-08-30-containers.md`.

### 7.1 Identity
One single def in `entities.bin`: `tileIndex=152` (`TILENUM_OBJ_CRATE`, `src/Enums.h:765`),
`eType=10 ET_ATTACK_INTERACTIVE`, `eSubType=2 INTERACT_CRATE`, `parm=0`, `name=136`
(def record #158; layout `src/EntityDef.cpp:35-43`). The other eType-10 defs are **not**
containers: 121/135 table+chair (`INTERACT_FURNITURE`, parm 1), 178 glass
(`INTERACT_BARRICADE`, parm 2), 123/127 toilet+sink (`INTERACT_PICKUP`, parm 3 — holy-water
refill, `src/PlayingInputHandler.cpp:405-430`). Subtype enum `src/Enums.h:54-57`.
Crates get `info |= 0x20000` (damageable) at spawn and are *excluded* from the
"destroyable object" counter (`src/Entity.cpp:87-95`, `src/Game.cpp:448-449`).

### 7.2 Why they block
Mask tests are `mask & (1 << eType)` (`src/Game.cpp:745`). `CONTENTS_PLAYERSOLID = 13501`
(`src/Enums.h:29`) = bits {0,2,3,4,5,7,10,12,13} = WORLD, MONSTER, NPC, PLAYERCLIP, DOOR,
DECOR, **ATTACK_INTERACTIVE**, SPRITEWALL, NONOBSTRUCTING_SPRITEWALL. Movement traces with
literal 13501 (`src/MovementController.cpp:326,333`). A crate is linked into `entityDb` at map
load, so bit 10 blocks the step; after opening it is unlinked and the tile becomes walkable.

### 7.3 Opening (ACTION_FIRE) — no `use()`/`touched()` anywhere
Inline in the fire handler (`src/PlayingInputHandler.cpp`):
1. `:189-195` — if the faced entity is eType 10, `lootingSystem.lootSource = facingEntity->name`
   (this is what names the loot dialog later);
2. `:200-247` — weapon trace (mask 13997) picks the crate as `entity`;
3. `:387-393` — if `eSubType == 2` and `dist2 <= tileDistances[0]` (=4096, one tile,
   `src/Combat.cpp:42`): `entity->param = app->upTimeMs + 200;` then
   `Game::unlinkEntity(entity)` (clears `info & 0x100000`, `src/Game.cpp:92`) → animation armed
   and the crate stops being solid / stops being a facing target;
4. `:395-398` — `executeTile(destX+viewStepX>>6, destY+viewStepY>>6, flagForFacingDir(4), true)`
   runs the crate tile's script (TRIGGER + reversed facing-direction bit,
   `src/MovementController.cpp:214-223`); a script that runs consumes the action, so the
   weapon is never fired at the crate.
There is no `use`, `activate` or `touched` call for crates; state change = `param` + unlink.

### 7.4 Opening animation
Driven inside the renderer, not by a lerp/animator (`src/Render.cpp:1607-1617`):
frame = `(mapSpriteInfo >> 8) & 0xFF` (`:1508`); while `param != 0`, every time
`app->time > param` → `++frame; param = time + 200`; at `frame > 3` → `param = 0; frame = 3`.
So **4 frames 0→3, 200 ms each (~600 ms), latched open at frame 3**. `app->time` is the frame
snapshot of `upTimeMs` (`src/Canvas.cpp:749-751`) — same clock that armed `param`.
This is unrelated to the door mechanism, which lerps `mapSprites[S_SCALEFACTOR]`
(`docs/original-code/doors.md`); crates never touch the scale factor and never hide the sprite.

### 7.5 Loot — script route, not the corpse route
Crates have **no lootSet**: `initspawn` frees the lootSet of anything that is not
`ET_MONSTER`/`ET_CORPSE` (`src/Entity.cpp:100-108`). `LootingSystem::poolLoot` only walks
`eType == 9` (`src/LootingSystem.cpp:163`) and `ST_LOOTING` is only entered from the eType-9 arm
of the fire handler (`src/PlayingInputHandler.cpp:283-336,373-378`), so **the loot-list UI never
appears for a crate**. The content lives in the tile script as `EV_GIVELOOT` (§2.5): instant
`player->give(...)` + dialog, header string 129 with the crate's name taken from `lootSource`
(else generic 130) (`src/ScriptThread.cpp:2121-2130`). Typical crate script body:
`EVENTOP disable self; PLAYSOUND 1054; GIVELOOT [...]; RETURN`.
In map00 all 10 tile-152 sprites map 1:1 onto such events
(sprites 69/43/24/13/12/58/133/60/128/134 → EVT 0/12/18/40/91/100/126/130/137/138,
IPs 1796/1705/1834/1686/1722/1815/1756/1851/1739/1779; verified with
`tools/disasm_map_scripts.py` + sprite-table parse of `tmp_map00.bin`).

### 7.6 "Already searched"
Two persisted marks: (a) the script disables its own event — `EV_EVENTOP` sets `0x80000` in
`tileEvents[i*2+1]` (`src/ScriptThread.cpp:808-814`) and `executeTile` skips such events
(`src/ScriptThread.cpp:75`); (b) the entity stays unlinked at sprite frame 3. Save/load:
a crate counts as a binary entity only while still linked (`info & 0x100000`,
`src/Entity.cpp:1423-1436`); `restoreBinaryState` re-applies the opened state as
frame `(eSubType == 2) ? 3 : 1` + `unlinkEntity` (`src/Entity.cpp:1877-1893`).

### 7.7 Damage interactions (why you cannot shoot a crate open)
`Combat::calcHit` gates eType-10 targets on `tableCombatMasks[def->parm] & (1 << weapon)`
(`src/Combat.cpp:876-882`); table 4 of `tables.bin` (`src/App.cpp:393`) is
`{0x00000000, 0x00000002, 0xFFFFFFFB, 0xFFFFFFFB}` → crate (`parm=0`) mask 0 = **immune to every
weapon**; furniture (parm 1) only weapon 1; glass/pickup (parm 2/3) all weapons except index 2.
Splash damage bypasses `calcHit`: `hurtEntityAt` searches with mask 30383 (bit 10 set) and calls
`pain()`+`died()` (`src/Combat.cpp:1085-1097,1120,1188-1192`); `Entity::pain` for a crate spawns
grey debris particles and `removeEntity` + `mapSpriteInfo |= 0x10000` (sprite hidden)
(`src/Entity.cpp:371-390`), and the loot script is *not* re-triggered because the explosion
flags `0x4004` require matching `0x7000` attack bits the crate events do not have
(`src/Combat.cpp:1100-1105`, gate `src/ScriptThread.cpp:75`).

### 7.8 HUD / prompt
Faced crate → action icon row 0 of `imgActions` (the same "use/hand" icon as `ET_ITEM`;
other eType-10 subtypes get row 1 "attack"), `src/Hud.cpp:354-360,385`. At ≤1 tile the tutorial
hint `showHelp(3)` fires (`src/MovementController.cpp:98-105`).

### 7.9 Containers vs. scripted "search objects"
- **Entity container** = a sprite with tile 152 → solid, animated, unlinked on open, loot from
  its tile script.
- **Scripted search object** = no container entity at all; the tile event does the work. map00
  EVT 148 @(20,28) is the reference case: `NEXTSTATE; EVENTOP disable; HIDE sprite=191;
  HIDE sprite=192; PLAYSOUND 1054; GIVELOOT 52 credits`, where sprites 191/192 are tile 114 =
  def `(6,0,23)` = plain `ET_ITEM` credits. The earlier note in §6 listing "search-object events
  583/830/916" is refined here: **583 and 830 are crates** (sprites 13 and 133), only **916** is
  a pure script.
- **Usable decor** (`ET_DECOR`, parm 1 = `DECOR_PARAM_USE`, parm 2 = `USE_ONCE`,
  `src/Enums.h:52-53`; e.g. tiles 125/133/147/150/153/154/173/179-183/201) is a third family:
  the fire handler never selects decor as a target (except practice target 149,
  `src/PlayingInputHandler.cpp:346-352`), everything happens in the tile script; the HUD "used"
  test for parm 2 is sprite frame `== 0` (`src/Hud.cpp:326-335`), i.e. scripts flip the frame
  with `EV_ENTITY_FRAME` instead of the crate's built-in animator.

## Port checklist (minimal viable loop)

1. **Corpse entity**: def-swap `find(9, eSubType, parm)` + keep Monster struct; corpse visual
   `mapSpriteInfo bits 8-14 = 0x7000`; `deactivate()` to inactive list; looted markers
   (`param` / `monster->flags & 0x800`); pile trim at 3; EV_MAKE_CORPSE + natural-death path.
2. **Lootset**: `int[3]` u16 entries (class/index/count); `populateDefaultLootSet` table (§2.2);
   EV_ASSIGN_LOOTSET (consume operands even when ignored); pool merge/dedupe/credit math;
   ST_LOOTING crouch-list-close machine granting on close + `advanceTurn`; `foundLoot` stat.
3. **Pickup-to-inventory**: `touched()/touchedItem()` class behavior incl. starter ammo,
   credit stacks, caps 999/9999/100/5, water→holy-water conversion; EV_GIVEITEM modes 0 /
   def+qty / negative-consume; SCR_ITEM_PICKUP var11 hook.
4. **HUD feedback**: weapon button + 15-slot selection grid (44-px rows), keycard 4-state
   widget off inventory[19]/[20], ammo digits, repaintFlags 0x4, msgs 83-86, sounds 1054/1055.
