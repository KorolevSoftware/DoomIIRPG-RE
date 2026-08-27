# Entities: NPC identity, characterChoice ↔ player representation (original RE port)

Every claim cites `src/` (legacy RE port). Bytecode IPs refer to the map00
script disassembly (`docs/research/assets/map00_disasm.txt`, provenance in
`docs/research/2026-08-26-choice-branch.md`).

## 1. The three selectable characters (characterChoice {1,2,3})

Written only by `Player::setCharacterChoice` which also stamps
`scriptStateVars[14]` (`src/Player.cpp:1264-1268`); call sites are the forced
intro select only (`src/IntroSequenceManager.cpp:599,628,676`; touch slot
order 0→1, 1→3, 2→2 at `:613-624`, keyboard cycle {1,3,2} via
`getCharacterConstantByOrder` `:328-341`). `Game::updateScriptVars` re-stamps
v14 every script batch (`src/Game.cpp:3468`).

| choice | class | proper name | base stats / gold (`src/Player.cpp:454-486`) | HUD portrait (`src/App.cpp:439-450`) | world art | pain/death snd (`src/Player.cpp:635-641,720-727`) |
|---|---|---|---|---|---|---|
| 1 | Major | Kira Morgan (MenuStrings 215, `src/MenuStrings.h:264`) | 8/9/97/12/110, 30 | `Hud_Player.bmp` | tile 68 | 1094 / 1092 |
| 2 | Sarge (heavy) | Stan Blazkowicz (216) | 12/14/92/6/100, 10 | `Hud_PlayerDoom.bmp` | tile 72 | 1093 / 1091 |
| 3 | Scientist | Riley O'Connor (217) | 8/8/87/6/150, 80 | `Hud_PlayerScientist.bmp` | tile 66 | 1093 / 1090 |

* **Choice 1 (Major) is the female hero** — tile 68 head frame shows a woman
  with long dark hair (verified from extracted art, frames per §2).
* The only other choice test in Combat is a weapon-14 attack sound
  (`src/Combat.cpp:298`); view-weapon art is shared across classes.
* Player start position/dir is NOT choice-gated: `Game::spawnPlayer`
  (`src/Game.cpp:941-972`) uses the map header spawn (map00: tile (4,19),
  angle 0) or the loadType==3 save-variant (3,15) dir 6.

## 2. NPC sprites are stacked composites

NPC frames per `src/Enums.h:604-613`: 0/1 front legs, 2 front torso,
3 front head, 4-7 back equivalents; composited by the sprite renderer
(`src/Render.cpp:3277` draws legs/torso/head parts at Z offsets). NPC tile
range 65-80 (`src/Enums.h:708-719`), `Render::isNPC` (`src/Render.cpp:2971-2972`).
Squad/NPC tiles: 66 = Riley O'Connor, 68 = Major, 69 = npc_bob, 71 = civilian,
72 = Sarge, 73 = generic female, 75 = scientist (`src/Enums.h:709-716`).

## 3. map00 squad sprites (7/9/10) and the player's world body

* spr7 = tile 68 (Major), spr9 = tile 66 (O'Connor), spr10 = tile 72 (Sarge)
  — entity defs (3,1)/(3,0)/(3,4) (`tmp_entities.bin`; art list in
  `docs/research/2026-08-26-choice-branch.md` §4).
* Boot chain (camera 5, func@2328): all three walk in formation **including
  the chosen one** (v14==1: spr10/9/7 → x=160 column, IPs 2346-2360; walks
  @2439-2453), then the chosen one is INSTANT-teleported out of the scene to
  tile (22,29): spr7 @2531 (v14==1), spr10 @2545 (v14==2), spr9 @2552
  (v14==3/else); the other two park at (8,18)/(7,19) @2936-2987.
* EVT 617 (doorway): the two NON-chosen sprites walk to (10,19) and are
  HIDEen (@3283/3285, @3297/3299, @3311/3313). The chosen sprite never
  enters this scene.
* After EVT 617 the chosen sprite lives on as a scripted companion: tile
  events (21,27)-(23,28) (event IDs 885/886/917/918/919, y*32+x packing)
  step it through (23,29)→(21,29)→(21,30)→(22,30)→(22,29)→(23,29)→(24,30)
  (IPs 7184-7633), each step gated by `EV_TILE_EMPTY` (op 9,
  `src/Enums.h:411`) so it only advances when the tile is clear.
* Familiar-mode body double: `Game::spawnPlayerEntityCopy`
  (`src/Game.cpp:2725-2759`) spawns an entity with the chosen class's art
  (1→68, 3→66, 2→72) at saveX/saveY (`src/Player.cpp:2105`, removed
  `:2163-2164`); `playerEntityCopyIndex` is save/loaded
  (`src/Player.cpp:1293,1376`).

## 4. Rewrite status

`vars[14] = 1` hardcoded marine/Major (`new_src/domain/game/ScriptVM.cpp:300-305`);
full v14==1 boot+EVT 617 behavior verified against err.log
(`docs/research/2026-08-26-hero-choice-and-weapon.md` §A.4).

## 5. `Entity::info` bit inventory (the entity word, NOT `mapSpriteInfo`)

`Entity::info` is a single `int` field (`src/Entity.h:21`). The original RE port
has **no** symbolic names for any of its bits — every site is a raw literal, so
`src/Enums.h` cannot be used as a naming source here. The inventory below is
derived from setter/tester pairs; each row cites them.

### 5.1 Structural proof that it is a different word from `mapSpriteInfo`

| | bits 0-7 | bits 8-15 |
|---|---|---|
| `Entity::info` | sprite index + 1, whole low 16 bits (`Entity::getSprite` = `(info & 0xFFFF) - 1`, `src/Entity.cpp:115`; independent use `src/Hud.cpp:286` `(facingEntity->info & 0xFFFF) - 1`, `src/Combat.cpp:1456,1476`) | (same field) |
| `mapSpriteInfo[s]` | tile number (`(mapSpriteInfo[sprite] & 0xFF) == TILENUM_...`, `src/Entity.cpp:1450`) | animation/frame byte (`(mapSpriteInfo[sprite] & 0xFF00) >> 8`, `src/Entity.cpp:1524`) |

The two low halves hold different things, so the `SPRITE_FLAG_*` layout
(`src/Enums.h:1231-1248`) **does not apply** to the entity word. Where a bit
value coincides, the meaning differs (see 5.3). Consequently
`SpriteInfo::kAnimByteMask` and the `(x & 0xFFFF00FF) | (v << 8)` write-back
idiom belong to `mapSpriteInfo` only: in the original that idiom appears ~35
times and **every** occurrence targets `app->render->mapSpriteInfo[...]`
(`src/Game.cpp:774,1151,1494,2943,3062,3107`, `src/Combat.cpp:157,438,529`,
`src/Entity.cpp:351,374,1627,1664,1889`, `src/Render.cpp:1604,1616`,
`src/ScriptThread.cpp:675,887`, `src/DialogSystem.cpp:626`, ...). It is never
applied to `Entity::info`. The only bits-8-15 touch of the entity word is the
sprite index it shares with bits 0-7.

### 5.2 Persistence window: bits 16-23

`saveState` writes `this->info >> 16 & 0xFF` (`src/Entity.cpp:1521`) and
`loadState` restores `info = (info & 0xFF00FFFF) | (readByte() & 0xFF) << 16`
(`src/Entity.cpp:1681`). So only bits 16-23 survive a save; bits 24+ are
runtime-only, bits 0-15 are re-derived from the sprite link.

Beware a second, unrelated bitfield with overlapping values: the **save
handle** returned by `Entity::getSaveHandle` (`src/Entity.cpp:1783-1855`) and
consumed as the `n` argument of `saveState`/`loadState`/`restoreBinaryState`.
There `0x200000` means "monster position record follows" (`src/Entity.cpp:1514`,
`:1655`) or "NPC param != 0" (`:1832`), and `0x4000000` means "NPC param == 2"
(`:1634,1834`). These are handle bits, not `info` bits.

### 5.3 Bit table

| bit | meaning | setter(s) | tester(s) | verdict |
|---|---|---|---|---|
| `0x0000FFFF` | sprite index + 1 (0 = no sprite) | link/spawn | `src/Entity.cpp:115`, `src/Game.cpp:186`, `src/Entity.cpp:1793` | CONFIRMED |
| `0x00010000` | entity removed / dead-and-gone (no world state to save) | `src/Combat.cpp:1145,1178`, `src/Entity.cpp:451,498,511,1203,1619` | `src/Entity.cpp:465,497,1295,1522,1682,1828`, `src/Game.cpp:3558` | CONFIRMED |
| `0x00020000` | alive / spawned-and-interactive (rewrite `kInfoActive`) | `src/Entity.cpp:77,89,97,1360` | `src/Entity.cpp:286,431,1953`, `src/Combat.cpp:873,1122`, `src/Game.cpp:815`, `src/Hud.cpp:833` | CONFIRMED |
| `0x00040000` | monster is on `Game::activeMonsters` (activated) | `src/Game.cpp:442,798` | `src/Game.cpp:770,827`, `src/Combat.cpp:895,1150`, `src/Hud.cpp:284` | CONFIRMED |
| `0x00080000` | "gib instantly on death" (skips the Nightmare zombie path, forces the corpse-gib branch) | `src/Entity.cpp:1715` (from save-handle `0x2000000`); cleared by `info &= 0xFFF7FFFF` `src/Entity.cpp:452,512` | `src/Entity.cpp:504,509,1825` | PARTIAL — no runtime setter in this port, only the save round-trip |
| `0x00100000` | linked into `Game::entityDb` tile list | `src/Game.cpp:118` (`linkWorldEntity`) | `src/Game.cpp:189,1053`, `src/Entity.cpp:1429,1593,1653,1666,1777,1796` | CONFIRMED |
| `0x00200000` | **UNKNOWN** — write-only in the whole port. Set on both combatants in `Combat::performAttack` and on monsters caught by splash damage; never tested anywhere, only persisted as part of the 16-23 byte | `src/Combat.cpp:67,70,1149` | none | UNKNOWN |
| `0x00400000` | dirty / "state differs from the map default, needs a full save record" | 24 sites: `src/ScriptThread.cpp:365,678,801,825,839,1016,1053,1405,1471,1670,1980,2261`, `src/Entity.cpp:389,460,1613,1639,1643,1647,1677,1711`, `src/Game.cpp:766,2660,2754`, `src/LootingSystem.cpp:179`, `src/ArmorRepairSystem.cpp:62` | `src/Entity.cpp:1816-1821` (`if ((info & 0x400000) == 0) n \|= 0x80000;` → short record, and ET_DECOR non-statue is skipped entirely), `src/Entity.cpp:1469` | CONFIRMED |
| `0x00800000` | unused (no site) | — | — | n/a |
| `0x01000000` | leaves a raisable corpse (set when a monster dies with its body kept) | `src/Entity.cpp:469` (`info \|= 0x1020000`), `:1724,1761`, `src/Game.cpp:2806`, `src/ScriptThread.cpp:2261` | `src/Entity.cpp:1297,1736`, `src/Game.cpp:3558`; cleared by `info &= 0xF6FEFFFF` on `resurrect` `src/Entity.cpp:1359` | CONFIRMED |
| `0x02000000` | an entity death function is registered for this entity | `src/Game.cpp:3509` (`setEntityFunc`), `src/Entity.cpp:1601` | `src/Entity.cpp:522` (fires `executeEntityFunc`, then clears), `:1813`; cleared `src/Game.cpp:3520`, `src/Entity.cpp:524` | CONFIRMED |
| `0x04000000` | **UNKNOWN** — write-only. Set on the monster target when the attack roll hits (`crFlags & 0x1007`), cleared when the attack animation leaves stage 1. Never tested; not in the persisted byte | `src/Combat.cpp:229`; cleared `src/Combat.cpp:373` (`info &= 0xFBFFFFFF`) | none | UNKNOWN |
| `0x08000000` | corpse is reserved as an Arch-Vile raise target | `src/Entity.cpp:1311`, `src/Game.cpp:3568` | `src/Entity.cpp:1296`, `src/Game.cpp:3558`; cleared `src/Entity.cpp:1331`, and by `info &= 0xF6FEFFFF` in `resurrect` `:1359` | CONFIRMED |
| `0x10000000` | monster is mid-move and its move is on-screen (blocks AI snapping) | `src/Entity.cpp:1096` — only when `cullBoundingBox(...) == false`, i.e. the path segment is visible | `src/Game.cpp:2380` (`canSnapMonsters` returns false while any active monster has it); cleared `src/Entity.cpp:877,1086,1122` | CONFIRMED |
| `0x20000000` | idle **breathing/bob suppressed** for this entity (set = do NOT breathe) | `src/ScriptThread.cpp:1918` (`EV_ENTITY_BREATHES` arg 0); cleared `:1915` (arg 1) | `src/Render.cpp:3196` (`renderMonster`: zeroes the idle bob `n20 = (time + n*1337)/1024 & 1`, `*26` px), `src/Render.cpp:3054` (`renderFearEyes`: zeroes the matching eye Z offset) | CONFIRMED |
| `0x40000000`, `0x80000000` | unused (no site) | — | — | n/a |

### 5.4 Bit-value collisions with `SPRITE_FLAG_*` are coincidences

| value | `mapSpriteInfo` (`src/Enums.h`) | `Entity::info` | shares layout? |
|---|---|---|---|
| `0x20000000` | `SPRITE_FLAG_FLAT` (`:1245`), tested on the sprite `flags` word in `src/Render.cpp:552,624` | breathing suppression (5.3) | NO |
| `0x00200000` | `SPRITE_FLAG_AUTOMAP_VISIBLE` / `SPRITE_FLAG_NOENTITY` (`:1236-1237`), set at `src/Render.cpp:878`, tested `src/AutomapController.cpp:142` | UNKNOWN combat mark (5.3) | NO |
| `0x04000000` | `SPRITE_FLAG_EAST` (`:1242`), part of `SPRITE_FLAGS_ORIENTED` `0xF000000` tested at `src/Render.cpp:685`, `src/Entity.cpp:1686` | UNKNOWN transient hit mark (5.3) | NO |
| `0x00400000` | `SPRITE_FLAG_TILE` (`:1238`), +257 tile-number bank, `src/Entity.cpp:1582`, `src/Render.cpp:847,1515` | dirty/needs-save (5.3) | NO |
| `0x00010000` | `SPRITE_FLAG_HIDDEN` (`:1232`) | entity removed | NO — related in effect, but distinct words; `src/Entity.cpp:1591-1593` sets `mapSpriteInfo \|= 0x10000` while separately testing `info & 0x100000` |

Rule for the rewrite: never name an `Entity::info` bit after a `SPRITE_FLAG_*`
constant, and never route the `0xFFFF00FF` anim-byte idiom through the entity
word.
