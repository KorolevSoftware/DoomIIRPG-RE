# 2026-08-30 — Which sprites block the player, and what the rewrite under-spawns

## Hypothesis

The rewrite's `Game::loadEntities` (`new_src/domain/game/Game.cpp:78-102`)
spawns entities only for doors (271-278), monsters, NPCs, corpses and items;
everything else `continue`s. The original spawns an entity for EVERY sprite
whose `lookup(tileNum)` returns a def, plus a fallback family for def-less
sprite walls. Therefore decorations, crates, terminals, glass, etc. — solid in
the original — are walk-through in the rewrite.

## Verdict

**CONFIRMED.** Five eTypes are in `CONTENTS_PLAYERSOLID` but are not spawned
by the rewrite: `ET_PLAYERCLIP(4)`, `ET_DECOR(7)`, `ET_ATTACK_INTERACTIVE(10)`,
`ET_SPRITEWALL(12)`, `ET_NONOBSTRUCTING_SPRITEWALL(13)`. On map00 that is
**86 linked, player-solid entities** that the rewrite never creates
(51 ET_DECOR + 28 ET_ATTACK_INTERACTIVE + 7 ET_SPRITEWALL).
Full spawn is safe: map00 peaks at **209 / 275** entities and 35 / 80 monsters;
the worst of the ten shipped maps is still 209 / 275.

## Method

1. `src/Enums.h:10-40` — bit-decomposed every `CONTENTS_*` mask against the
   `ET_*` ordinals.
2. Read `Game::trace` (`src/Game.cpp:199-323`), `Game::linkEntity`
   (`src/Game.cpp:95-119`), `Game::loadMapEntities` (`src/Game.cpp:329-507`),
   `Entity::initspawn` (`src/Entity.cpp:50-111`),
   `Render::traceWorld` (`src/Render.cpp:1212-1283`).
3. Parsed `tmp_entities.bin` with the layout from
   `EntityDefManager::startup` (`src/EntityDef.cpp:35-43`): `short numDefs`,
   then 190 records of 8 bytes `{i16 tileIndex, u8 eType, u8 eSubType,
   u8 parm, u8 name, u8 longName, u8 description}` (little-endian; 2+8*190 =
   1522 = file size).
4. Parsed `tmp_map00.bin` and `map00..map09` from the `.ipa` with
   `tools/map_to_obj.py::parse_map`, replaying the exact `loadMapEntities`
   spawn decision.

## Evidence

### 1. Mask ↔ eType bits

`src/Enums.h:26-40`; bit *i* of a mask = eType *i* (`src/Game.cpp:221`:
`0x0 != (n4 & 1 << nextOnTile->def->eType)`).

```
PLAYERSOLID    13501 = WORLD MONSTER NPC PLAYERCLIP DOOR DECOR ATTACK_INTERACTIVE SPRITEWALL NONOBSTRUCTING_SPRITEWALL
MONSTERSOLID   15535 = WORLD PLAYER MONSTER NPC DOOR DECOR ATTACK_INTERACTIVE MONSTERBLOCK_ITEM SPRITEWALL NONOBSTRUCTING_SPRITEWALL
WEAPONSOLID    13997 = WORLD MONSTER NPC DOOR DECOR CORPSE ATTACK_INTERACTIVE SPRITEWALL NONOBSTRUCTING_SPRITEWALL
VIEWSOLID       5293 = WORLD MONSTER NPC DOOR DECOR ATTACK_INTERACTIVE SPRITEWALL
MONSTERWPSOLID  5295 = VIEWSOLID + PLAYER
INTERACTIVE     1068 = MONSTER NPC DOOR ATTACK_INTERACTIVE
PICKUP            64 = ITEM
SPRITEWALL     12288 = SPRITEWALL NONOBSTRUCTING_SPRITEWALL
NOFLOAT        12424 = NPC DECOR SPRITEWALL NONOBSTRUCTING_SPRITEWALL
DYNAMITE_SOLID 13349 = WORLD MONSTER DOOR ATTACK_INTERACTIVE SPRITEWALL NONOBSTRUCTING_SPRITEWALL
ISEMPTY_SCRIPT 25152 = ITEM CORPSE NONOBSTRUCTING_SPRITEWALL DECOR_NOCLIP
SPLASH_SOLID    4129 = WORLD DOOR SPRITEWALL
LINE_O_SIGHT    4131 = WORLD PLAYER DOOR SPRITEWALL
WORLD              1 = WORLD
```

Cross-checks of mask usage: player walk `13501`
(`src/MovementController.cpp:326` `int n3 = app->player->noclip ? 0 : 13501;`),
weapon fire `13997` (`src/PlayingInputHandler.cpp:200`), monster move/LOS
`15535` (`src/Entity.cpp:733,1078,1302,1329`, `src/Game.cpp:717,3558`),
knockback picks `13501` for the player and `15535` for anything else
(`src/Entity.cpp:1223,1229`).

### 2. Trace predicate

`src/Game.cpp:216-226`:

```
Entity* nextOnTile = this->entityDb[i + 32 * j];
...
if (nextOnTile != entity && 0x0 != (n4 & 1 << nextOnTile->def->eType)) {
    if (nextOnTile->def->eType != 0) {
```

The candidate set is exactly the `entityDb[tile]` linked list over the bbox
tiles. **No `info` bit is consulted** — not `kInfoActive` (`0x20000`), not the
link bit `0x100000`, not `0x10000`. So being present in the tile list plus
having a def whose eType bit is in the mask is necessary *and sufficient*.
`eType == ET_WORLD` is skipped in the loop; world geometry enters separately
via `Render::traceWorld` when `n4 & 1` (`src/Game.cpp:296`).

Membership comes from `Game::linkEntity` (`src/Game.cpp:95-119`), which
prepends to `entityDb[y*32+x]` and only then sets `entity->info |= 0x100000`
(`:118`) as bookkeeping (`unlinkEntity` clears it, `:92`). `Game::deactivate`
(`src/Game.cpp:825-856`) touches only the monster ring and `0x40000`; it never
unlinks — inactive monsters still block.

Geometry per candidate (`src/Game.cpp:249-288`): if
`mapSpriteInfo[sprite] & 0xF000000` (oriented/wall sprite) → a ±32 segment
through the sprite centre, axis chosen by `& 0x3000000` (N/S ⇒ horizontal),
tested with `CapsuleToLineTrace`; otherwise a circle of r²=625 (r=25), or 256
(r=16) for `ET_ENV_DAMAGE`, via `CapsuleToCircleTrace`. Hit when frac < 16384.

### 3. Original spawn rule (`Game::loadMapEntities`, `src/Game.cpp:373-486`)

Per sprite `n5`:
* `n6 = mapSpriteInfo & 0xFF`, `+257` if `& 0x400000` (`:374-377`).
* `& 0x200000` ⇒ clear the bit and **spawn nothing** (`:397-400`).
* `lookup(n6) != nullptr` ⇒ entity at `entities[numEntities]`,
  `info = (n5+1) & 0xFFFF`, `def = lookup`, `initspawn()`,
  `mapSprites[S_ENT+n5] = numEntities++`, then link unless
  `mapSpriteInfo & 0x10000` (`:419-455`).
* `lookup == nullptr` **and** `mapSpriteInfo & 0x800000` ⇒ a def-less sprite
  wall gets `find(13,0)` for tiles 166/168 else `find(12,0)`, no `initspawn`,
  same link rule (`:457-481`).
* Link tile nudge for oriented sprites sitting exactly on a tile border
  (`:407-418`): if `(mapSpriteInfo & 0xF000000)` and `(x & 0x3F)==0 ||
  (y & 0x3F)==0`, then `+1 x` for `0x4000000`, `+1 y` for `0x2000000`,
  `-1 y` for `0x1000000`, `-1 x` for `0x8000000`. On map00 51 sprites hit this
  branch and **20 of them land in a different tile** than the raw `x>>6,y>>6`
  the rewrite uses.

`Entity::initspawn` extras for the missing families (`src/Entity.cpp:81-93`):
`ET_DECOR` with `eSubType != DECOR_STATUE(3)` clears `mapSpriteInfo & 0x10000`
(so decor is linked even if the map marked it hidden) and sets scale 32 for
`TILENUM_SWITCH == 173`; `ET_ATTACK_INTERACTIVE` sets `info |= 0x20000`.
`src/Game.cpp:448-450` bumps `numDestroyableObj` for
`ET_ATTACK_INTERACTIVE` with `eSubType` not 2 and not 3.

### 4. Def table statistics (`tmp_entities.bin`, 190 defs)

| eType | count | player-solid |
|---|---|---|
| 0 ET_WORLD | 1 | yes |
| 1 ET_PLAYER | 1 | no |
| 2 ET_MONSTER | 41 | yes |
| 3 ET_NPC | 10 | yes |
| 4 ET_PLAYERCLIP | 0 | yes |
| 5 ET_DOOR | 8 | yes |
| 6 ET_ITEM | 46 | no |
| 7 ET_DECOR | 19 | yes |
| 8 ET_ENV_DAMAGE | 2 | no |
| 9 ET_CORPSE | 44 | no |
| 10 ET_ATTACK_INTERACTIVE | 6 | yes |
| 12 ET_SPRITEWALL | 2 | yes |
| 13 ET_NONOBSTRUCTING_SPRITEWALL | 1 | yes |
| 14 ET_DECOR_NOCLIP | 9 | no |

Blocking tileIndex sets:
`ET_DECOR = {125,133,136,147,149,150,153,154,156,173,179,180,181,182,183,187,188,189,201}`,
`ET_ATTACK_INTERACTIVE = {121,123,127,135,152,178}`,
`ET_SPRITEWALL = {508,509}`, `ET_NONOBSTRUCTING_SPRITEWALL = {507}`
(507-509 are reachable only through the `+257` bank, i.e. wall tiles 250-252).
`ET_PLAYERCLIP` has no def at all — it exists only as a mask bit for
world clip lines (§6).

### 5. map00 spawn census

261 sprites, 70 carry `0x200000` (never spawn), 191 produce entities,
of which 7 are def-less sprite walls. Totals
`2 (world+player) + 191 + 16 drop slots = 209`.

| eType | entities | linked | player-solid |
|---|---|---|---|
| ET_MONSTER | 35 | 35 | yes |
| ET_NPC | 10 | 10 | yes |
| ET_DOOR | 21 | 21 | yes |
| ET_ITEM | 25 | 25 | no |
| ET_DECOR | 51 | 51 | yes |
| ET_ENV_DAMAGE | 4 | 4 | no |
| ET_CORPSE | 9 | 9 | no |
| ET_ATTACK_INTERACTIVE | 28 | 28 | yes |
| ET_SPRITEWALL | 7 | 7 | yes |
| ET_DECOR_NOCLIP | 1 | 1 | no |

No map00 sprite carries `mapSpriteInfo & 0x10000`, so every spawned entity is
linked; 76 spawned entities are oriented (`0xF000000`) and therefore trace as
line segments, not circles.

**86 player-solid entities missing in the rewrite** (first 21 shown; sprite
index, tileNum, name, eType, tile coords):

| # | tile | name | eType | tile xy |
|---|---|---|---|---|
| 3 | 180 | terminal_general | DECOR | (3,21) |
| 5 | 136 | obj_torchiere | DECOR | (4,18) |
| 6 | 136 | obj_torchiere | DECOR | (4,20) |
| 8 | 180 | terminal_general | DECOR | (4,21) |
| 11 | 180 | terminal_general | DECOR | (5,23) |
| 12 | 152 | obj_crate | ATTACK_INTERACTIVE | (6,22) |
| 13 | 152 | obj_crate | ATTACK_INTERACTIVE | (7,18) |
| 18 | 147 | septic_station | DECOR | (9,17) |
| 21 | 147 | septic_station | DECOR | (9,27) |
| 24 | 152 | obj_crate | ATTACK_INTERACTIVE | (11,13) |
| 26 | 123 | toilet | ATTACK_INTERACTIVE | (12,10) |
| 27 | 123 | toilet | ATTACK_INTERACTIVE | (12,11) |
| 33 | 326 | wall sprite (def-less) | SPRITEWALL | (13,9) |
| 43 | 152 | obj_crate | ATTACK_INTERACTIVE | (14,8) |
| 45 | 127 | sink | ATTACK_INTERACTIVE | (14,11) |
| 52 | 149 | practice_target | DECOR | (14,29) |
| 58 | 152 | obj_crate | ATTACK_INTERACTIVE | (15,23) |
| 59 | 340 | wall sprite (def-less) | SPRITEWALL | (15,25) |
| 60 | 152 | obj_crate | ATTACK_INTERACTIVE | (15,26) |
| 67 | 128 | barred_window (def-less) | SPRITEWALL | (18,24) |
| 68 | 128 | barred_window (def-less) | SPRITEWALL | (18,26) |

(remaining 65: crates 69/128/133/134, glass 100/101/107/109/118/120, tables
114/116/131, chairs 115/119, toilets/sinks 92/93/94/95, vending 125,
armor_repair 71, terminals 73/75/84/88/89/90/130/211/213/214/215/216/221/222/
223/224/225/227/247, terminal_hacking 226, terminal_target 228,
septic 230/231, switches 207/208/209/210, tech_station 248/250/251/252/254/
256/257/258/259, glaevenscope 249/253/255/260, def-less sprite walls 77/123/124.)

### 6. Blocking WITHOUT an entity

Two mechanisms:

1. **Def-less sprite walls** — `src/Game.cpp:457-472`: a sprite with no def but
   `mapSpriteInfo & 0x800000` still gets an entity, borrowing
   `find(12,0)`/`find(13,0)`. 7 such on map00 (tiles 128, 202, 264, 326, 340).
   The rewrite drops all of them.
2. **World lines** — `Render::traceWorld` (`src/Render.cpp:1226-1266`) runs only
   when the mask has bit 0 (`src/Game.cpp:296`). Per line,
   `n7 = lineFlags[i>>1] >> ((i&1)<<2) & 0x7`:
   `4` → the whole test block is skipped (never blocks);
   `6` → `continue`;
   `5` → `continue` unless `mask & 0x10` (ET_PLAYERCLIP bit) or `mask & 0x800`
   (ET_MONSTERBLOCK_ITEM bit) — i.e. clip lines block walking
   (13501 has 0x10, 15535 has 0x800) but **not** shots (13997 has neither),
   which is exactly what `ET_PLAYERCLIP` is for even though no def uses it;
   `7` → back-face cull via the cross product;
   `0..3` → always solid.
   Melee re-adds the clip bit: `n5 |= 0x10` when the weapon has mask 2
   (`src/PlayingInputHandler.cpp:209-212`).
   Additionally `mapFlags[tile] & 0x1` tiles get the shared world entity
   linked into `entityDb` (`src/Game.cpp:497-506`), but `trace` skips
   `eType == 0` in the entity loop, so that link serves pathing/`findMapEntity`,
   not the movement block.

### 7. Capacity risk of full spawn

Limits: `entities = new Entity[275]` (`src/Game.cpp:40`), guarded by
`if (numEntities == 275) app->Error(35)` at `src/Game.cpp:420,458,489`
(ERR_MAX_ENTITIES); `entityMonsters[80]` (`src/Game.h:58`) guarded by
`if (numMonsters == 80) app->Error(37)` (`src/Game.cpp:433`, ERR_MAX_MONSTERS).
Replaying the spawn rule over the ten shipped maps:

| map | sprites | entities (2+N+16) | monsters |
|---|---|---|---|
| map00 | 261 | 209 / 275 | 35 / 80 |
| map01 | 230 | 176 / 275 | 41 / 80 |
| map02 | 255 | 185 / 275 | 42 / 80 |
| map03 | 190 | 183 / 275 | 40 / 80 |
| map04 | 184 | 184 / 275 | 45 / 80 |
| map05 | 163 | 151 / 275 | 43 / 80 |
| map06 | 186 | 150 / 275 | 52 / 80 |
| map07 | 163 | 123 / 275 | 41 / 80 |
| map08 | 154 | 102 / 275 | 65 / 80 |
| map09 | 49 | 66 / 275 | 16 / 80 |

Worst case 209/275 (66 slots spare) and 65/80 monsters. **No overflow risk.**

## Open questions

* `mapSpriteInfo & 0x800000` is not enumerated in `src/Enums.h` under a
  `SPRITE_FLAG_*` name in this port; treated here purely as "sprite-wall
  fallback" from its single use site (`src/Game.cpp:457`).
* Whether any script later spawns `ET_PLAYERCLIP` entities at runtime — no
  `find(4, ...)` call exists in the port, so on shipped data the type is
  mask-only.
