# 2026-09-22 — Monster attack execution (Entity::attack → performAttack → monsterSeq → explodeOnPlayer)

Curated result: `docs/original-code/monster-attack.md`. This log keeps the
questions, method and verdicts.

## Questions (from the caller) and verdicts

1. **What does `Entity::attack` (`src/Entity.cpp:1160`) set up?** — CONFIRMED:
   *nothing but the queue*. `flags |= 0x400`, `nextAttacker = combatMonsters`,
   `combatMonsters = this` (`src/Entity.cpp:1160-1167`). Weapon was chosen earlier
   (`ce.weapon = aiWeaponForTarget(...)`, `src/Entity.cpp:546-551`, `:1124-1134`);
   target = `monster->target` (null = player, `src/Entity.cpp:574-579`).
   curAttacker/curTarget, attack frame, sounds are all set later by
   `Combat::performAttack` / `monsterSeq`.
2. **performAttack + monsterSeq with a monster attacker** — CONFIRMED (full
   timeline in the curated doc §3-§4). Key facts:
   * `attackFrame` 64/80 (ATTACK1/ATTACK2 = weapon matches monsterAttacks field 1)
     (`src/Combat.cpp:90-99`); `animLoopCount = NUMSHOTS` (`:112`).
   * stage 0 (`src/Combat.cpp:462-571`) rolls hit+damage via
     `player->ce->calcCombat(&attackerCe, playerEnt, true, dist, -1)` (`:489`),
     plays the attack sound (`:512-522`), sets pose frame 0 with
     `frameTime = time + SHOTHOLD*10` (`:528-531`), `nextStageTime = animEndTime`
     (`:563`).
   * After SHOTHOLD·10 ms the pose goes to frame 1 and `launchProjectile()` runs
     (`:434-441`). For PROJTYPE ≤ 1 that sets `exploded = true`
     (`:1572-1576`), and the same monsterSeq call's stage −1 branch
     (`:599-604`) runs `updateProjectile` → `explodeOnPlayer` (`:1421-1424`):
     **damage is applied at t = S, not at stage 0.**
   * Next stage after another S ms and no missiles/lerp effects (`:442-460`);
     stage 1 clears the pose, prints msg 112/131, returns 0 (`:574-597`).
   * CHARGE (18) is special: lunge lerp in performAttack (`:131-164`), damage
     applied in stage 0 while lerping back (`:535-551`).
   * Muzzle flash = render-side only: frame 1 of ATTACK1/2 draws art frame
     `base+1` either as the torso pose (no flare) or as an additive flash sprite
     for `hasGunFlare` tiles (`src/Render.cpp:3354-3356`, `:3413-3460`,
     `src/Render.cpp:3019-3021`).
3. **Hit/damage monster→player** — CONFIRMED with one correction to an older log.
   * `calcHit` monster branch (`src/CombatEntity.cpp:219-297`): accuracy vs
     `agi·96>>8`, −16/tile out of range, max 2 consecutive monster misses, crit
     only on difficulty 4, RNG = `rand()` via `nextByte` (`src/App.cpp:506-512`).
   * `calcDamage` monster branch (`src/CombatEntity.cpp:300-378`): rolled value is
     `STRMIN + nextByte() % (STRMAX − STRMIN)` → **STRMAX exclusive**;
     monster strength bonus ×1 (the ×3 is the player's, `!b` branch
     `:326-333`) — `docs/research/2026-08-26-combat-stage1.md` §2.4 has these two
     factors swapped; armor split `min(((171·base>>8)+1)/2, armor)`, damage −
     2·armorDmg; defense %; no weapon weakness for monster attackers.
   * `explodeOnPlayer` adds the difficulty term when `difficulty != 1`:
     `+ (d>>1) + loadMapID/NUMSHOTS` (×2 mapID on difficulty 4)
     (`src/Combat.cpp:984-990`). Default difficulty is 1 (`src/Game.cpp:43`). The
     menu → value mapping (NORMAL/DIFFICULT/NIGHTMARE strings,
     `src/MenuStrings.h:256-258`) was not found in this port: no writer of
     `game->difficulty` besides the default and save load (`src/Game.cpp:1984`) —
     inferred 1/2/4 from the tested values only.
   * Weapon stats: tables.bin table 2 (32×9) for the weapon, table 0
     `monsterAttacks` (shorts, 3 per sub/parm) for the weapon choice, table 3
     `monsterStats` for acc/str etc. Values extracted — see curated §7.
4. **Hitscan vs projectile for monsters** — CONFIRMED. Instant: 15-18 (melee,
   PROJTYPE 1), 23 (−1), 24/25 (0). Missiles: 19 (6), 20 (7), 21/29 (8), 22 (9),
   26 (4), 27 (10), 28/31 (5), 30 (11). Monster misses spawn no impact anim
   (`src/Combat.cpp:1380`); melee hits spawn 245/246/247 splash
   (`:1401-1420`). `explodeOnPlayer` (`:948-1055`) is the only damage sink for
   monster→player (hurtEntityAt handles splash, player side `:1127-1142`, not
   reached from monster missiles).
5. **Melee vs ranged selection** (`src/Entity.cpp:687-848`) — CONFIRMED: axis-
   aligned only (except Arch-Vile / VIOS parm≠0), LOS trace 5295, melee disabled if
   the 18-unit-ahead trace (mask 15791) fails, per-weapon tile range window, both
   valid → `nextByte() <= field2 ? field0 : field1`.

## Method

* Read `src/Combat.cpp:54-166, 427-610, 857-862, 948-1060, 1235-1717`,
  `src/CombatEntity.cpp:130-378`, `src/Entity.cpp:535-848, 1116-1180`,
  `src/GameStateRunner.cpp:20-87`, `src/Game.cpp:874-913, 1294-1560, 2440-2474,
  3060-3111, 3771-3845`, `src/Render.cpp:1590-1605, 3019-3021, 3295-3462`.
* Parsed `UnPackGameData/tables.bin` (identical to `tmp_tables_real.bin`) with the
  offset scheme of `src/Resource.cpp:209-322` (header = 20 LE end-offsets; table i
  starts at `80 + tableOffsets[i-1]`, first int = element count; shorts/ints LE,
  `src/Resource.cpp:136-150`). Anchor check: weapon row 0 = `8,10,0,5,1,1,0,2,50`
  (asserted). Table 0 = 150 shorts, table 3 = 324 bytes, table 13 = 136 bytes.
* Parsed `tmp_entities.bin` (u16 count + 8-byte defs, `src/EntityDef.cpp:31-44`)
  and map00 sprites via `tools/map_to_obj.py` `parse_map` (tile = info & 0xFF,
  +257 if 0x400000, per `docs/original-code/entities.md`). Scratch script kept in
  the session scratchpad only (not a project tool).
* Sound names resolved from `src/Sounds.h` `RESOURCE_FILE_NAMES` (index =
  resID − 1000, `src/Sound.cpp:180`).

## Evidence highlights (verbatim)

`src/Combat.cpp:434-440`
```
if ((n3 == 64 || n3 == 80) && n4 == 0) {
    if (app->time >= this->curAttacker->monster->frameTime) {
        ...frameTime = app->time + weapons[attackerWeapon + SHOTHOLD] * 10;
        this->nextStageTime = app->gameTime + weapons[...SHOTHOLD] * 10;
        mapSpriteInfo[sprite] = ((n & 0xFFFF00FF) | (n3 | n4 + 1) << 8);
        this->launchProjectile();
```

`src/CombatEntity.cpp:322-334`
```
else if (dmgStrMax != dmgStrMin) { dmgStrMin += app->nextByte() % (dmgStrMax - dmgStrMin); }
if ((app->combat->crFlags & 0x20) == 0x0) {
    if (!b) { dmgStrMin += 3 * (ce->getStatPercent(STAT_STRENGTH) * dmgStrMin >> 8); }
    else    { dmgStrMin += ce->getStatPercent(STAT_STRENGTH) * dmgStrMin >> 8; }
```

`src/CombatEntity.cpp:263` (miss streak cap)
```
if (((!b && playerMissRepetition < n8) || (b && monsterMissRepetition < 2)) && nextByte > crHitChance && (b || ...))
```

### Extracted data (monster attack rows; full table in curated §7)

| monster (sub,parm) | field0 | field1 | thr | weapon row(s) STRMIN,STRMAX,RMIN,RMAX,..,PROJ,NUMSHOTS,SHOTHOLD |
|---|---|---|---|---|
| zombie (0,0) | 19 | 0 | 0 | 19: 8,10,1,3,0,0,6,1,30 |
| imp (3,0) | 16 | 0 | 0 | 16: 5,6,1,1,0,0,1,2,10 |
| imp (3,1) | 16 | 20 | 75 | 20: 10,12,1,3,0,0,7,1,10 |
| lost soul (2,0) / pinky (5,0) | 15 | 18 | 75 | 15: 6,8,1,1,..,1,1,20; 18: 8,12,1,2,..,1,1,20 |
| sentry bot (11,0) | 24 | 0 | 0 | 24: 3,4,1,4,0,0,0,2,30 |

Worked example — imp (3,0) CLAW vs player on difficulty 1, armor 0:
base = 5 (5 + r % 1), str 12 → pct 30 → bonus `30·5>>8 = 0`, damage 5, minus
player defense %; 2 shots → ≤ 10 per attack; hit chance vs agility A:
`((95 − (A·96>>8)) << 8)/100`.

map00 census: imp (3,0) ×20, zombie (0,0) ×15, zombie (0,1) ×3, imp (3,1) ×2
(tiles 23/20/21/24). No hitscan monster on map00.

## Dead/odd code noted

* `crFlags & 0x80` → msg 67 (`src/Combat.cpp:568-569`): 0x80 is never OR-ed into
  `crFlags` anywhere in the port.
* `launchProjectile` `if (!b) info &= 0xFFF700FF` (`src/Combat.cpp:1663-1666`)
  with `b` always false wipes the frame bytes set at `:1633-1645`.
* `updateProjectile` suppression `... && !b` (`src/Combat.cpp:1375-1377`) with
  `b = true` (`:1287`) — never taken.
* Player status effect [18] sets CR 0x100 but the hit still lands with damage
  (`src/CombatEntity.cpp:272-297`).
* Melee splash anim is spawned at `(destZ, destY, destZ)` (`src/Combat.cpp:1413`).

## Open questions

* Meaning of `game->baseVisitedTiles` in the CHARGE eligibility loop
  (`src/Entity.cpp:811-832`).
* Menu → `difficulty` value mapping (no writer found in `src/`).
* `Player::calcDamageDir`, `painEvent`, `pain`, knockback — delegated to the
  "player receiving damage" researcher.
* Status effect 13 (`addStatusEffect(13,5,3)`) semantics (burn?) — player-side.
