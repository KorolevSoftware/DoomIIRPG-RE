# 2026-08-31 — Melee weapons in the original: how the CHAINSAW deals damage

## Hypotheses (from the caller)
1. There is a class of melee weapons (chainsaw, fists/knuckles) distinguished by a table column
   or an index range.
2. The chainsaw needs fuel/ammo, and with zero ammo it either refuses to fire or fires for 0
   damage — the suspected cause of "chainsaw picked up, selected, but deals no damage" in the
   rewrite.
3. `tableCombatMasks[parm]` can zero out the hit.

## Verdict
* **1 — PARTIAL.** There is exactly **one** player melee weapon: `WP_CHAINSAW = 1`
  (`src/Enums.h:137`), selected by the bitmask `WP_MELEEMASK = 2` (`src/Enums.h:155`) via
  `Entity::CheckWeaponMask(weapon, 2)` = `(1 << weapon) & 2` (`src/Entity.h:52-54`).
  There is **no fists/punch weapon in the shipped build**: `WP_PUNCH_MASK = 0`
  (`src/Enums.h:153`) makes every `(1 << weapon & 0x0)` test dead
  (`src/Combat.cpp:732`, `src/PlayingInputHandler.cpp:381`), and `Combat::punchingMonster`
  is never assigned a non-zero value anywhere in `src/` (only `= 2` inside an already
  `> 0` branch, `src/Combat.cpp:201-202`, and `= 0` at `:399`). Melee is *not* derived from a
  table column: the 9-byte weapon row has no "melee" field; `PROJTYPE` (col 6) is `-1`
  (`WP_PROJ_NONE`) for the chainsaw, but also for weapons 4, 6 and 23, so it is not the
  discriminator either. `WP_PROJ_MELEE = 1` (`src/Enums.h:120`) is used **only by monster
  weapons** 15-18 (rows 15-18 of table 2).
* **2 — REFUTED.** The chainsaw has `AMMOTYPE = 0` (`AMMO_NONE`, `src/Enums.h:103`) and
  `AMMOUSAGE = 0`, so *every* ammo gate is skipped by its `!= 0` guard:
  `Player::fireWeapon` (`src/Player.cpp:780-781`), `Combat::playerSeq`
  (`src/Combat.cpp:210-215`), `PlayingInputHandler::shouldFakeCombat`
  (`src/PlayingInputHandler.cpp:574-579`), and even the starter-ammo grant at pickup
  (`src/Entity.cpp:256`, gated on `AMMOUSAGE != 0`). The deduction
  `ammo[ammoType] -= ammoUsage` at `src/Combat.cpp:349-352` degenerates to `ammo[0] -= 0`.
  The chainsaw can never run dry and can never be blocked by ammo.
* **3 — PARTIAL / CONFIRMED with an inversion.** `tableCombatMasks` is consulted **only** for
  `ET_ATTACK_INTERACTIVE` targets (`src/Combat.cpp:876-882`) and is indexed by the target
  `def->parm`, not by the weapon; the weapon appears as the bit `1 << weapon`. Shipped table 4 =
  `{0, 0x2, 0xFFFFFFFB, 0xFFFFFFFB}`, so `parm == 1` means **chainsaw-only** — the chainsaw is
  the *privileged* weapon there, never the penalised one. It plays no role for `ET_MONSTER`.
* **Most likely cause of the rewrite symptom** (see §7): the chainsaw is `PROJTYPE = -1`, and in
  the original that hits `launchProjectile`'s `default:` branch which sets
  `exploded = true; return;` (`src/Combat.cpp:1566-1570`) so that the *same* stage-0 call applies
  the damage through `updateProjectile → explodeOnMonster` (`src/Combat.cpp:1421-1431`).
  A rewrite that only applies damage from a missile impact, or that treats `PROJTYPE == 0`
  (bullet) as the only hitscan case, will deal exactly 0 damage with the chainsaw.
  Second candidate: the 1-tile probe / `RANGEMAX == 1` — a target at 2+ tiles yields
  `crFlags 0x400` and damage 0 (message 64).

## Method
1. `grep` for `CheckWeaponMask`, `WP_MELEEMASK`, `punchingMonster`, `usedChainsaw`,
   `launchProjectile`, `tableCombatMasks` over `src/`.
2. Read the whole `ACTION_FIRE` branch (`src/PlayingInputHandler.cpp:189-540`),
   `Player::fireWeapon`, `Combat::performAttack`, `Combat::playerSeq`, `Combat::calcHit`,
   `Combat::launchProjectile`, `Combat::updateProjectile`, `Combat::explodeOnMonster`,
   `CombatEntity::calcCombat/calcHit/calcDamage`, `Entity::pain`, `Player::usedChainsaw`,
   `Combat::drawWeapon`.
3. Re-parsed `tmp_tables_real.bin` directly (header: 80 bytes = 20 LE int offsets,
   `src/Resource.cpp:216-219`; each table = LE int size + payload, `:278-289`) for
   table 2 (weapon data, 9 bytes/row), table 4 (combat masks, ints) and table 11
   (monster weakness, 8 bytes/row).
4. Cross-checked `tmp_entities.bin` (2-byte count + 190 × 8-byte defs) for every
   `eType == 10` def to see which `parm` values actually exist.

## Evidence

### 1. Melee identification
* `src/Enums.h:136-151` weapon ids; `WP_CHAINSAW = 1`.
* `src/Enums.h:155` `WP_MELEEMASK = 2`; `src/Entity.h:52-54`
  `static bool CheckWeaponMask(char n1, int n2) { return (1 << n1 & n2); }`.
* Melee call sites: `src/PlayingInputHandler.cpp:202,210,249` (probe length/mask, eType-13
  promotion). Everywhere else the code compares the id literally: `weapon2 == 1`
  (`src/PlayingInputHandler.cpp:238,281`), `ce->weapon == Enums::WP_CHAINSAW`
  (`src/Player.cpp:777`), `combatEntity->weapon == 1` (`src/CombatEntity.cpp:146`),
  `(1 << attackerWeaponId & 0x2)` (`src/Combat.cpp:223`, `src/Combat.cpp:891`),
  `weapon == 1` (`src/Combat.cpp:751`).
* Table 2 row for the chainsaw (index 1 → bytes `weapons[9..17]`), columns per
  `src/Combat.h:26-35`:

  | col | field | value |
  |---|---|---|
  | 0 | STRMIN | **18** |
  | 1 | STRMAX | **22** |
  | 2 | RANGEMIN | 0 |
  | 3 | RANGEMAX | **1** |
  | 4 | AMMOTYPE | **0 = AMMO_NONE** |
  | 5 | AMMOUSAGE | **0** |
  | 6 | PROJTYPE | **−1 = WP_PROJ_NONE** |
  | 7 | NUMSHOTS | **1** |
  | 8 | SHOTHOLD | **100** (→ animTime 1000 ms) |

  (Rifle row 0 for contrast: `8,10,0,5,1,1,0,2,50`.) Monster melee rows 15-18 do use
  `PROJTYPE = 1 (WP_PROJ_MELEE)` with `RANGEMIN = RANGEMAX = 1`.

### 2. Range and trace (`src/PlayingInputHandler.cpp:197-221`)
```
int n5 = 13997;
if (Entity::CheckWeaponMask(weapon2, 2) != 0x0) { /* n5 |= 0x2000; // J2ME only? */ }
if (weapon2 == 2) { n5 |= 0x4100; }
int n6 = 0;
int n7 = 6;
if (Entity::CheckWeaponMask(weapon2, 2) != 0x0) { n7 = 1; n5 |= 0x10; }
...
app->game->trace(viewX, viewY, viewZ, viewX + (n7 * -view[2] >> 8), ..., nullptr, n5, 2, isZoomedIn);
```
* `n7` is the ray length **in tiles**: `view[]` is 14.14, so `n7 * 16384 >> 8 = n7 * 64` world
  units. Melee → 1 tile, everything else → 6 tiles.
* Mask for the chainsaw = `13997 | 0x10 = 14013` — `0x10` is bit 4 = `ET_PLAYERCLIP`
  (`src/Enums.h:14`), i.e. the saw also "sees" invisible clip brushes.
  The commented-out `|= 0x2000` (bit 13, non-obstructing spritewall) is a J2ME-only line; bit 13
  is already inside 13997.
* The **eType-13 promotion** at `:247-252` / `:362-364` is a separate mechanism and has nothing
  to do with `0x10`:
```
else if (eType == 13) { if (CheckWeaponMask(weapon2,2) != 0) { entity2 = entity3; } }
...
if (entity2 != nullptr && (entity == nullptr || (1 << weapon2 & 0x0) != 0x0
    || (entity->def->eType != 2 && entity->def->eType != 9))) { entity = entity2; }
```
  `ET_NONOBSTRUCTING_SPRITEWALL (13)` never terminates the election loop; only a melee weapon
  remembers it in `entity2`, and afterwards it **overrides** the elected target unless that
  target is a monster (2) or a corpse (9). `(1 << weapon2 & 0x0)` is always false
  (`WP_PAINTINGMASK = 0`). Effect: the chainsaw can chop a decorative sprite-wall
  (e.g. hanging gore/vines) that guns pass through.

### 3. Full damage path for the chainsaw
`ACTION_FIRE` → `Player::fireWeapon(entity, x, y)` (`src/PlayingInputHandler.cpp:515/523/536`):
* `src/Player.cpp:754-799` guards: soul-cube-only rule, `weaponDown`,
  `disabledWeapons != 0 && (weapons & 1<<weapon) == 0` (note: the second test reads the
  **owned** mask, not `disabledWeapons`), lerping weapon, then
  `if (weapon == WP_CHAINSAW && (target is ET_CORPSE || (ET_ATTACK_INTERACTIVE &&
  eSubType is BARRICADE|FURNITURE))) usedChainsaw(false);` (`:777-779`) — the stat-growth
  counter ticks even for props — then the ammo gate (skipped, §2 of the verdict) and
  `Combat::performAttack(nullptr, entity, x, y, false)`.
* `Combat::performAttack` (`src/Combat.cpp:54-166`): `attackerWeaponId = player->ce->weapon`,
  `attackerWeapon = id*9`, `animLoopCount = weapons[+7] = 1`,
  `attackerWeaponProj = weapons[+6] = -1`, `worldDist/tileDist` from `viewX/viewY`,
  `stage = 0`, `setState(ST_COMBAT)`.
* `Combat::playerSeq` stage 0 (`src/Combat.cpp:186-360`):
  * `crFlags |= 0x40` only if `(1 << id & 0x77FF) == 0` → not the chainsaw.
  * target is a monster → `crFlags |= 0x20` **only when `(1 << id & 0x2) == 0`**
    (`:223-226`) ⇒ **the chainsaw never sets 0x20**, and `CombatEntity::calcDamage`
    applies the strength bonus exactly when 0x20 is clear (`src/CombatEntity.cpp:326-332`,
    `dmg += 3 * (strengthPercent * dmg >> 8)` for the player). So the strength stat boosts
    **only the chainsaw** among monster-directed player attacks.
  * `player->ce->calcCombat(...)` → `CombatEntity::calcHit` then `calcDamage`
    (`src/CombatEntity.cpp:130-154`); on a hit with `weapon == 1` it calls
    `player->usedChainsaw(true)` (`:146-148`).
  * non-monster target → `Combat::calcHit(curTarget)` (`src/Combat.cpp:864-883`) and a *direct*
    table roll `damage = STRMIN + rnd % (STRMAX-STRMIN)`, `-25%` on difficulty 4
    (`:249-259`) — `calcDamage`, hence the strength bonus and the weakness table, are **not**
    used for props/walls.
  * sound: `case 1: n3 = 1015;` (`src/Combat.cpp:301-303`) = `chainsaw.wav`
    (`src/Sounds.h:23`, id = 1000 + index), played through `playCombatSound(n3, 0, 4)`
    unconditionally (hit or miss).
  * `animTime = SHOTHOLD * 10 = 1000 ms` (`×5` instead of `×10` with haste `statusEffects[2]`,
    `:308-314`).
  * `launchProjectile()` (`:325`) → `default: missileAnim = 0; exploded = true; return;`
    (`src/Combat.cpp:1566-1570`) because `attackerWeaponProj == -1`.
  * recoil: `if ((1 << id & 0x2406) == 0) rockView(...)` (`:326-333`) —
    `0x2406 = WP_NORECOIL (9222)` = bits {1,2,9,13} ⇒ **no `rockView` for the chainsaw**
    (its shake comes from `usedChainsaw`'s `startShake(666, 1, 0)`,
    `src/Player.cpp:2652`).
  * ammo deduction `ammo[0] -= 0` (`:349-352`), `nextStage = 1`,
    `nextStageTime = animEndTime`, then `updateProjectile()` (`:355`) which, seeing
    `exploded == true` and `curTarget != nullptr`, calls **`explodeOnMonster()` immediately**
    (`src/Combat.cpp:1421-1431`).
* `Combat::explodeOnMonster` (`src/Combat.cpp:885-944`):
  * `render->shotsFired = true`, but `if ((1 << id & 0x2) != 0 && hitType == 0) shotsFired =
    false;` (`:890-892`) — a *missed* chainsaw swing does not count as a shot (no monster
    wake-up by noise).
  * hit on a monster → `curTarget->pain(totalDamage, playerEnt)` + blood (`:905-910`);
    `Entity::pain` requires `info & 0x20000` (spawned/alive) and does
    `monster->ce.setStat(0, hp - damage)` (`src/Entity.cpp:284-345`).
  * `targetType == 9` (corpse) → gib: hide sprite, blood, `spawnDropItem`, sound 1037
    (`:936-944`) — chainsaw-only, since only the saw elects a corpse as an attack target.
* Stage 1 (`src/Combat.cpp:365-410`): death (`died(true, playerEnt)`),
  `--animLoopCount > 0` repeat (never for the chainsaw, NUMSHOTS 1), damage summary message.

**"Hit but 0 damage"** sources: `crFlags 0x100` (msg 59, set when `calcDamage` returned 0 and
armour absorbed nothing, `src/CombatEntity.cpp:149-151`); weakness nibble 0 (`×0.125`, §5);
target `getStatPercent(STAT_DEFENSE)` subtraction (`src/CombatEntity.cpp:371`).
**"Not hit at all"** sources: out of range → `crFlags 0x400` + msg 64
(`src/CombatEntity.cpp:230-237` for monsters, `src/Combat.cpp:867-871` for others);
accuracy roll (`nextByte() > crHitChance`, capped by `playerMissRepetition < 1`,
`src/CombatEntity.cpp:252-266`); `(info & 0x20000) == 0` (`src/Combat.cpp:872-874`);
`tableCombatMasks[parm]` bit missing for `ET_ATTACK_INTERACTIVE` (`:876-882`).

### 4. `tableCombatMasks` and the chainsaw
`src/Combat.cpp:876-882`:
```
if (entity->def->eType != Enums::ET_ATTACK_INTERACTIVE) { return 1; }
if ((this->tableCombatMasks[entity->def->parm] & 1 << app->player->ce->weapon) != 0x0) { return 1; }
return 0;
```
Shipped table 4 (4 ints): `{0, 0x2, 0xFFFFFFFB, 0xFFFFFFFB}`.
Shipped `eType == 10` defs (`tmp_entities.bin`): def 138 tile 121 (sub 0 FURNITURE, parm 1),
def 146 tile 135 (sub 0 FURNITURE, parm 1), def 165 tile 178 (sub 1 BARRICADE, parm 2),
defs 139/141 tiles 123/127 (sub 3 PICKUP, parm 3), def 158 tile 152 (sub 2 CRATE, parm 0).
⇒ **furniture (parm 1) is destructible by the chainsaw only**; barricade/pickup (parm 2/3) by
anything except the holy-water pistol (bit 2 cleared); crates (parm 0) by nothing — they are
"opened" by the unlink trick in the input handler (`src/PlayingInputHandler.cpp:383-388`),
not by damage. The election loop agrees: `eType == 10` is only targeted when
`(1 << eSubType & 0x1) == 0` (i.e. not FURNITURE) **or** `weapon == 1`
(`src/PlayingInputHandler.cpp:238-239`).

### 5. Monster weakness for the chainsaw
`Combat::getWeaponWeakness(n, sub, parm)` (`src/Combat.cpp:29-31`):
`(monsterWeakness[(sub*3+parm)*8 + n/2] >> ((n & 1) << 2) & 0xF) + 1 << 5`.
For the chainsaw (`n = 1`) that is the **high nibble of byte 0** of the row (the rifle uses the
low nibble). Parsed table 11 (54 rows × 8 bytes): byte0 = `0xF7` for `sub 0` (ZOMBIE, all
parms) → nibble 15 → `512` → `damage = 512 * base >> 8` = **×2**; byte0 = `0x07` for `sub 4`
(SAW_GOBLIN) → nibble 0 → `32` → **×0.125**; every other subtype `0x77` → `256` → ×1.
(`src/Enums.h:61-78` for the subtype names.)
Worked example vs an imp (sub 3, `0x77`), strength 100, defence 5%:
base `18 + rnd%4` = 18..21 → `+3*(256*base>>8)` … with `getStatPercent(4) = (100<<8)/100 = 256`
the bonus is `3*base` ⇒ 4×base = 72..84 → `×256>>8` (weakness 1.0) → `−5%` ⇒ **68..79** per swing;
crit (`crFlags 0x2`) uses `STRMAX*2 = 44` as the base ⇒ 4×44 = 176 → 167.

### 6. Animation / feedback differences
* No muzzle flash: `WP_MUZZLE_FLASH = 385` (`src/Enums.h:158`) = weapons {0,7,8} only
  (`src/Combat.cpp:826-834`).
* No `rockView` recoil (`WP_NORECOIL = 9222`, `src/Combat.cpp:327`).
* Longest hold in the game: `SHOTHOLD 100` → 1000 ms attack pose.
* Unique two-phase return in `Combat::drawWeapon` (`src/Combat.cpp:751-761`): while
  `n15 = (t-animStart)<<16 / animTime < 43690` (≈ first 2/3) the sprite **jitters**
  `+2 / −1 px` whenever `n15 & 0x8`; afterwards it eases back with
  `3 * ((65536-n15)*atk + (n15-43690)*idle) >> 16` and sets `b6`, which forces
  **frame 1** of the weapon art (`:822-825`).
* Shake `startShake(666, 1, 0)` on every `usedChainsaw` call (`src/Player.cpp:2652`),
  and the growth counter: `chainsawStrengthBonusCount++` (+1 more on a crit), at ≥30 →
  `modifyStat(STAT_STRENGTH, 2)` + message 241 (`src/Player.cpp:2636-2653`).
* Tile scripts see a chainsaw-specific exec flag: `flagForWeapon` returns **4096** for
  `(1 << weapon) & 0x2`, 16384 for the rocket launcher, 8192 for everything else
  (`src/MovementController.cpp:198-212`), used by `shouldFakeCombat`
  (`src/PlayingInputHandler.cpp:580-584`) and by the corpse branch of stage 1
  (`src/Combat.cpp:373-376`).
* View sprite: weapon id 1 → tile 2, wpinfo idle (−33,80) / attack (−53,91)
  (already documented, combat.md §1).

### 7. Continuous damage?
No. `NUMSHOTS = 1` ⇒ `animLoopCount = 1` ⇒ the stage-1 repeat branch
`else if (--this->animLoopCount > 0 && ...)` (`src/Combat.cpp:381-386`) fails on the first
iteration, so exactly **one** damage application per `ACTION_FIRE`, one turn.
The only multi-hit player weapons are the ones with `NUMSHOTS > 1` (rifle 2, chaingun 4,
plasma 3, holy water 2), and they are additionally clamped by
`min(ammo/AMMOUSAGE, NUMSHOTS)` (`src/Combat.cpp:113-119`, `:339-341`).
There is no "hold to saw" mode anywhere; the chainsaw's only special continuous element is the
1000 ms jitter animation.

## Open questions
* `crFlags 0x10` (which would waive the range penalty in `CombatEntity::calcHit:229-231`) is
  never set anywhere in `src/` — presumably a J2ME leftover.
* `Combat::calcHit` returns 1 for `ET_WORLD`, and stage 1 then calls `died(true, ...)` on
  `entities[0]` when the roll produced damage; the consequences of that on the world entity were
  not traced here (out of scope).
