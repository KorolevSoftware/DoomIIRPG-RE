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
