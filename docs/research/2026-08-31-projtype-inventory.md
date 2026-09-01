# 2026-08-31 — PROJTYPE inventory: which weapons need a real missile pool

## Question (from the caller)
The rewrite implements only the `default:` branch of the projectile machine
(PROJTYPE <= 0 → instant damage on the target) and *refuses to fire* weapons with
`projType > 0`. A reviewer claimed this is stricter than the original, because
legacy `Combat::launchProjectile` supposedly has no cases for projType 1/5/6/9,
so those would also fall into `default:` and explode instantly.

## Verdict
**PARTIAL — the reviewer is wrong about 5/6/9, right about 1.**

* `case 5`, `case 6` and `case 9` **do exist** in `launchProjectile`
  (`src/Combat.cpp:1518`, `:1527`, `:1533`). Only **projType 1 (`WP_PROJ_MELEE`)**
  and projType 0 / −1 (and any value ≥ 14) fall through to `default:`.
* projType 1 is used **exclusively by monster weapons 15–18** (BITE, CLAW, PUNCH,
  CHARGE). **No player weapon has projType 1.**
* Therefore, for the 15 player weapon rows the predicate `projType > 0` is
  **exactly equivalent** to "needs a real missile pool": it rejects
  **6 weapons, all 6 correctly**, and **0 weapons wrongly**.
* The bug is on the *monster* side: if the same `projType > 0` refusal is applied to
  monster attacks, rows 15–18 (the most common monster melee attacks) would be
  wrongly refused — in the original they are instant-damage `default:` attacks.
* **Not a complete solution visually** for those four: the `default:` path still
  produces a hit animation for weapons 15/16/17/18 through the second branch of
  `Combat::updateProjectile` (`src/Combat.cpp:1400-1420`), anims 245/246/247.

## Method
1. Read the whole `Combat::launchProjectile` switch (`src/Combat.cpp:1433-1573`) and
   enumerated the `case` labels mechanically:
   `sed -n 1478,1572p src/Combat.cpp | grep -n 'case \|default'`.
2. Re-parsed table 2 (`TBL_COMBAT_WEAPONDATA`, `src/App.cpp:360,391`) from
   `tmp_tables_real.bin` using the **correct** offset scheme
   (`src/Resource.cpp:209-247`): the 80-byte header holds 20 LE ints that are
   **end** offsets relative to the header, so table *i* starts at
   `80 + (i ? tableOffsets[i-1] : 0)` and begins with an LE int payload size.
   (A naive `start = tableOffsets[i]` reading gives garbage — that was the earlier
   bad dump.) Result: table 2 payload = 288 bytes = 32 rows × 9 bytes, which
   matches `WP_MAX = 32` (`src/Enums.h:184`) and the stride `id * 9`
   (`src/Combat.cpp:102`, `:1200`). Self-check anchors: row 0
   `8,10,0,5,1,1,0,2,50` (rifle) and row 1 `18,22,0,1,0,0,-1,1,100` (chainsaw),
   both already independently established in `docs/research/2026-08-31-melee-weapons.md`.
3. Parsed `tmp_entities.bin` (2-byte count + 190 × 8-byte defs,
   `src/EntityDef.cpp:31-44`) for all `eType == ET_ITEM (6)` /
   `eSubType == IT_WEAPON (1)` defs to see which weapon ids are obtainable.
4. Cross-checked the fire path (`src/PlayingInputHandler.cpp:485-540`) to see which
   owned weapons are actually *fired* vs. *deployed*.

## Evidence

### 1. Exhaustive case list of `launchProjectile`
`src/Combat.cpp:1433-1573`. Special-cased **before** the switch:
```
src/Combat.cpp:1439-1443
    if (this->attackerWeaponProj == 12) {
        this->soulCubeIsAttacking = true;
        this->launchSoulCube();
        return;
    }
```
`switch (this->attackerWeaponProj)` begins at `src/Combat.cpp:1479`.

**Real projectiles (allocate a GameSprite missile, fly, then explode):**

| projType | enum (`src/Enums.h:118-133`) | line | missileAnim / notes |
|---|---|---|---|
| 2 | `WP_PROJ_WATER` | `src/Combat.cpp:1483` | 240, renderMode 3, speed n=512 |
| 3 | `WP_PROJ_PLASMA` | `:1490` | 243, right-step offset, z1+15, n=512 |
| 4 | `WP_PROJ_ROCKET` | `:1503` | 225 (player) / 226 (monster), n=256+64 |
| 5 | `WP_PROJ_BFG` | `:1518` | 244, renderMode 4, ×1.5 speed if monster |
| 6 | `WP_PROJ_FLESH` | `:1527` | 227 |
| 7 | `WP_PROJ_FIRE` | `:1532`* | 242, renderMode 3 |
| 8 | `WP_PROJ_CACO_PLASMA` | `:1538`* | 241, renderMode 3 |
| 9 | `WP_PROJ_THORNS` | `:1544`* | 171, z1/z2 −32, resets `numThornParticleSystems` |
| 10 | `WP_PROJ_ACID` | `:1551`* | 252, z1+48 |
| 11 | `WP_PROJ_ELECTRIC` | `:1557`* | 248 |
| 12 | `WP_PROJ_SOUL_CUBE` | `:1439` (pre-switch) | `launchSoulCube()` |
| 13 | `WP_PROJ_ITEM` | `:1566`* | `missileAnim = player->activeWeaponDef->tileIndex` |

(*line numbers of the `case` label as counted from the `sed`+`grep` offsets 1478+n;
all inside the single switch 1479-1573.)

**Fall-through to `default:` → instant explosion, no missile:**
```
src/Combat.cpp (default branch, end of the switch)
    default: {
        this->missileAnim = 0;
        this->exploded = true;
        return;
    }
```
That is: **−1 (`WP_PROJ_NONE`), 0 (`WP_PROJ_BULLET`), 1 (`WP_PROJ_MELEE`)** — and
any value ≥ 14, which never occurs in shipped data. `exploded = true` makes the
*same* stage-0 call apply damage via `updateProjectile → explodeOnMonster /
explodeOnPlayer` (`src/Combat.cpp:1421-1431`).

Note `WP_PROJ_INSTANT = 3` (`src/Enums.h:134`) is a **misleading alias** for
`WP_PROJ_PLASMA = 3`; it does not mark the instant class.

### 2. Full shipped table 2 (32 rows × 9 bytes)
Columns per `src/Combat.h:26-35`: STRMIN, STRMAX, RANGEMIN, RANGEMAX, AMMOTYPE,
AMMOUSAGE, **PROJTYPE**, NUMSHOTS, SHOTHOLD. Signed bytes as stored; `calcDamage`
reads STRMIN/STRMAX with `& 0xFF` (`src/CombatEntity.cpp:303-304`), so negatives
mean 200+ damage.

| id | `WP_*` name (`src/Enums.h:136-181`) | projType | class | ET_ITEM def? | row |
|---|---|---|---|---|---|
| 0 | ASSAULT_RIFLE | 0 BULLET | instant | def 57, tile 1 | 8,10,0,5,1,1,0,2,50 |
| 1 | CHAINSAW | −1 NONE | instant | def 58, tile 2 | 18,22,0,1,0,0,−1,1,100 |
| 2 | HOLY_WATER_PISTOL | **2 WATER** | **missile** | def 59, tile 3 | 5,8,0,3,3,2,2,2,50 |
| 3 | SHOOTING_SENTRY_BOT | 0 BULLET | instant | def 60, tile 4 | 12,15,0,5,7,0,0,1,35 |
| 4 | EXPLODING_SENTRY_BOT | −1 NONE | instant | def 61, tile 5 | 100,120,0,4,7,0,−1,1,30 |
| 5 | RED_SHOOTING_SENTRY_BOT | 0 BULLET | instant | def 69, tile 13 | 20,30,0,5,7,0,0,1,35 |
| 6 | RED_EXPLODING_SENTRY_BOT | −1 NONE | instant | def 70, tile 14 | 140,155,0,4,7,0,−1,1,30 |
| 7 | SUPER_SHOTGUN | 0 BULLET | instant | def 62, tile 6 | 25,30,0,3,2,2,0,1,30 |
| 8 | CHAINGUN | 0 BULLET | instant | def 63, tile 7 | 12,15,0,5,1,1,0,4,15 |
| 9 | ASSAULT_RIFLE_WITH_SCOPE | 0 BULLET | instant | def 64, tile 8 | 20,25,0,5,1,1,0,1,30 |
| 10 | PLASMA_GUN | **3 PLASMA** | **missile** | def 65, tile 9 | 20,25,0,5,4,1,3,3,5 |
| 11 | ROCKET_LAUNCHER | **4 ROCKET** | **missile** | def 66, tile 10 | 80,90,0,5,5,1,4,1,30 |
| 12 | BFG | **5 BFG** | **missile** | def 67, tile 11 | 200,220,0,5,4,10,5,1,30 |
| 13 | SOUL_CUBE | **12 SOUL_CUBE** | **missile** (`launchSoulCube`) | def 68, tile 12 | 250,250,0,5,6,5,12,1,30 |
| 14 | ITEM (thrown) | **13 ITEM** | **missile** | def 71, tile 15 | 20,20,0,5,8,1,13,1,35 |
| 15 | M_BITE | 1 MELEE | instant | — monster | 6,8,1,1,0,0,1,1,20 |
| 16 | M_CLAW | 1 MELEE | instant | — monster | 5,6,1,1,0,0,1,2,10 |
| 17 | M_PUNCH | 1 MELEE | instant | — monster | 7,9,1,1,0,0,1,1,20 |
| 18 | M_CHARGE | 1 MELEE | instant | — monster | 8,12,1,2,0,0,1,1,20 |
| 19 | M_FLESH_THROW | 6 FLESH | missile | — monster | 8,10,1,3,0,0,6,1,30 |
| 20 | M_FIREBALL | 7 FIRE | missile | — monster | 10,12,1,3,0,0,7,1,10 |
| 21 | M_PLASMA | 8 CACO_PLASMA | missile | — monster | 10,12,2,3,0,0,8,1,10 |
| 22 | M_FLOOR_STRIKE | 9 THORNS | missile | — monster | 8,10,1,3,0,0,9,1,35 |
| 23 | M_FIRE | −1 NONE | instant | — monster | 15,18,0,5,0,0,−1,1,30 |
| 24 | M_MACHINE_GUN | 0 BULLET | instant | — monster | 3,4,1,4,0,0,0,2,30 |
| 25 | M_CHAIN_GUN | 0 BULLET | instant | — monster | 6,9,1,4,0,0,0,4,30 |
| 26 | M_ROCKETS | 4 ROCKET | missile | — monster | 20,25,1,5,0,0,4,1,30 |
| 27 | M_ACID_SPIT | 10 ACID | missile | — monster | 15,20,1,6,0,0,10,1,30 |
| 28 | M_PLASMA_GUN | 5 BFG | missile | — monster | 8,10,1,4,0,0,5,2,10 |
| 29 | M_VIOS_PLASMA | 8 CACO_PLASMA | missile | — monster | 15,20,1,5,0,0,8,1,30 |
| 30 | M_VIOS_LIGHTNING | 11 ELECTRIC | missile | — monster | 10,12,1,6,0,0,11,1,30 |
| 31 | M_VIOS_POISON | 5 BFG | missile | — monster | 15,20,1,5,0,0,5,1,30 |

Unused projTypes in shipped data: none — every case 2..13 is referenced by at
least one row. No row uses a value ≥ 14.

### 3. Obtainability
All 15 player ids 0..14 have an `ET_ITEM` / `IT_WEAPON` def in `tmp_entities.bin`
(defs 57-71, tiles 1-15), so all can be granted by
`Entity::pickup` → `player->give(1, def->parm, 1, false)`
(`src/Entity.cpp:238-256`). Two caveats:
* **Sentry bots 3/4/5/6** (`WP_SENTRY_BOT_MASK = 120 = bits {3,4,5,6}`,
  `src/Enums.h:157`, `src/Player.cpp:2485-2487`) are *never hand-fired*:
  `ACTION_FIRE` calls `attemptToDeploySentryBot()` and returns
  (`src/PlayingInputHandler.cpp:493-496`).
* **Weapon 14 (ITEM)** is a transient pseudo-weapon for throwing an inventory
  item: weapon switching is blocked while it is active
  (`src/PlayingInputHandler.cpp:124-126`) and stage 1 clears its bit and restores
  the previous weapon (`src/Combat.cpp:411-414`,
  `weapons &= 0xFFFFBFFF; selectWeapon(currentWeaponCopy)`).

### 4. The numbers asked for
Over the 15 player weapon rows:
* `projType > 0` → **6 weapons**: 2 (holy water), 10 (plasma), 11 (rockets),
  12 (BFG), 13 (soul cube), 14 (thrown item).
* All 6 have a real `case` in `launchProjectile` ⇒ **6 rejected correctly**,
  **0 rejected wrongly**.
* `projType <= 0` → **9 weapons**: 0, 1, 3, 4, 5, 6, 7, 8, 9 — all instant.
  Of the 9, four (3/4/5/6) are the sentry bots, so the hand-fired instant set is
  **5 weapons**: rifle, chainsaw, super shotgun, chaingun, scoped rifle.
* Over all 32 rows: `projType > 0` = 22 rows, of which **4 (ids 15-18, projType 1)
  are instant in the original** ⇒ a `projType > 0` refusal applied to monsters
  wrongly blocks 4 monster attacks.

### 5. Visuals on the `default:` path
`default:` sets `missileAnim = 0` — **no flying sprite, no impact anim** for
bullets / NONE. Bullet weapons in the original genuinely have **no tracer**; their
feedback is the muzzle flash (`WP_MUZZLE_FLASH = 385` = weapons {0,7,8},
`src/Enums.h:158`, `src/Combat.cpp:826-834`), `rockView` recoil
(`WP_NORECOIL = 9222` exempts {1,2,9,13}, `src/Combat.cpp:327`) and blood
particles from `explodeOnMonster` (`src/Combat.cpp:905-910`).

**Exception — monster melee (projType 1):** `Combat::updateProjectile` has a
second top-level branch taken when `numActiveMissiles == 0`
(`src/Combat.cpp:1400`):
```
else if (this->gotHit && (this->totalDamage > 0 || this->totalArmorDamage > 0)
         && this->exploded && (this->attackerWeaponId == 16 || == 15 || == 18 || == 17)) {
        if (== 16) missileAnim = 245; else if (== 15) missileAnim = 246; else missileAnim = 247;
        ... gsprite_allocAnim(missileAnim, canvas->destZ, canvas->destY, canvas->destZ);
        gSprite->flags |= 0x800; mapSprites[S_RENDERMODE + sprite] = 5;
```
i.e. a hit-splat anim **at the attacker's screen position**, only on a landed hit
with non-zero damage, keyed by *weapon id* (not projType). Anim 245 additionally
inherits bit `0x20000` from the attacker sprite. So "instant damage" is *not* the
full story for monster BITE/CLAW/PUNCH/CHARGE.

(Note the literal `calcPosition[...]` / `destZ, destY, destZ` argument list at
`src/Combat.cpp:1411-1414` — `destZ` is passed for both X and Z; verbatim from the
port, not a transcription error on our side.)

## Open questions
* `Combat::performAttack` for a sentry bot: which entity is `curAttacker` and
  whether `playerSeq` or `monsterSeq` runs it (`src/Combat.cpp:213` mentions
  `weaponIsASentryBot` inside `playerSeq`) — not traced here.
* `n` (speed, default 256), `n2`, `n3 = 16` and `dodgeDir`/`n5 = ±16` semantics in
  `launchProjectile` past line 1573 (flight integration) were not decoded; needed
  before implementing real missiles.
