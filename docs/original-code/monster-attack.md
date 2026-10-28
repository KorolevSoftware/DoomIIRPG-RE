# Monster attack execution (original RE port)

How a queued monster attack is *executed*: weapon choice, the `Combat::monsterSeq`
state machine, hit/damage math monster→player, hitscan vs missile, and the table
data behind it. Every claim cites `src/`. Raw log:
`docs/research/2026-09-22-monster-attack-execution.md`.

Related: player-side fire pipeline and shared formulas → `combat.md` §5, §10;
PROJTYPE inventory → `docs/research/2026-08-31-projtype-inventory.md`; monster
anim encoding/art frames → `character-animation.md` §1-§2; turn scheduling →
`combat.md` §6. Player damage reception (`Player::pain`, `painEvent`) and the
monster turn pipeline (`monsterAI`/`aiThink`) are covered by sibling docs.

## 1. Queueing: `Entity::attack` does almost nothing

`Entity::attack` (`src/Entity.cpp:1160-1167`) only enqueues:

```
if ((monster->flags & 0x400) == 0) { monster->flags |= 0x400;
    monster->nextAttacker = game->combatMonsters; game->combatMonsters = this; }
```

* No weapon choice, no animation, no sound. LIFO push onto `Game::combatMonsters`.
  `0x400` = "attack queued/done this turn"; cleared at the start of the next
  `aiThink` (`src/Entity.cpp:674-676`), also re-set at monsterSeq stage 1
  (`src/Combat.cpp:594`).
* The weapon was already chosen before the call: `monster->ce.weapon =
  aiWeaponForTarget(player)` in `aiCalcSimpleGoal` (`src/Entity.cpp:546-551`) or
  `aiReachedGoal_MOVE` (`src/Entity.cpp:1124-1134`, move-then-attack monsters).
* Target: `monster->target` — `nullptr` means the player
  (`src/Entity.cpp:574-579`, `:1127-1132`); a non-null target only comes from a
  goal-type-3 AIGOAL with an entity param.
* `undoAttack` (`src/Entity.cpp:1169+`) clears `0x400`, clears the anim byte for
  weapon 18, `resetGoal()` and unlinks from the chain.

Execution start: `Game::updateMonsters` / `monsterAI` call
`combat->performAttack(combatMonsters, combatMonsters->monster->target, 0, 0, false)`
once lerps have settled (`src/Game.cpp:2458-2474`, `:907-911`). After each attack
`GameStateRunner::combatState` walks `curAttacker->monster->nextAttacker`, dropping
attackers whose `aiIsAttackValid()` now fails, and starts the next
(`src/GameStateRunner.cpp:52-78`); empty chain → `endMonstersTurn` +
`combatDone` (`:70-76`). Player death state aborts: every queued attacker
`undoAttack()`s (`src/GameStateRunner.cpp:41-46`).

## 2. Weapon selection — `Entity::aiWeaponForTarget` (`src/Entity.cpp:687-848`)

Per-monster attack row = table 0 `monsterAttacks` (shorts), 3 fields at
`(eSubType*9 + parm*3 + n)` (`src/Combat.cpp:1058-1060`, loader `src/App.cpp:356,386`):
field 0 = primary weapon, field 1 = secondary weapon, field 2 = byte threshold.

Rules for normal monsters (the `else` branch, `src/Entity.cpp:767-846`):

1. **Axis-aligned only**: if both `dx != 0` and `dy != 0` (world units, monster
   sprite → `canvas->viewX/Y`) → `-1` (`:748`, `:765-766`). Exceptions:
   Arch-Vile (sub 10) and VIOS (sub 15, parm != 0) may fire off-axis with field 0
   / field 1 respectively, range-checked + trace mask 4131 (`:748-764`).
2. Trace monster→player, mask 5295, must hit the target (`:768-771`).
3. Second trace starting 18 units ahead of the monster, mask 15791
   (`:780-781`); if it does NOT hit the target, melee weapons are disabled:
   `CheckWeaponMask(w, 0x78002)` = ids {1, 15, 16, 17, 18} → weapon set to 0
   (`:784-791`; `CheckWeaponMask` = `1<<n & mask`, `src/Entity.h:52-54`).
4. Weapon id 0 is the "none" sentinel; `field0 == field1` → field1 = 0 (`:795-797`).
5. Each candidate valid iff `RANGEMIN <= tileDist <= RANGEMAX`
   (`weapons[w*9+2..3]`, `:798-809`), `tileDist = WorldDistToTileDist(Chebyshev²)`.
   Adjacent tile = tileDist 1 (64² is not `< tileDistances[0]`, `src/Combat.cpp:41-44`,
   `:1235-1242`).
6. Charge (18) additionally requires no `baseVisitedTiles` bit on the
   `tileDist` tiles walked from the monster toward the player (`:811-832`) —
   semantics of `baseVisitedTiles` not traced here (open question).
7. Both valid → `nextByte() <= field2 ? field0 : field1`; one valid → that one;
   none → `-1` (`:834-846`). `nextByte = rand() & 0x7FFFFFFF & 0xFF`
   (`src/App.cpp:506-512`) — plain C `rand()`.

`aiIsAttackValid` (`src/Entity.cpp:644-671`) re-validates a queued attack against
`game->destX/destY`: trace mask 5295 hits the player (or the target), `RANGEMAX >=
tileDist`, and axis-aligned non-zero offset.

## 3. `Combat::performAttack` with a monster attacker (`src/Combat.cpp:54-166`)

* `curAttacker = monster`, `curTarget = monster->target` (null = player); both get
  `info |= 0x200000` (`:58-71`). `curTarget == nullptr` ⇒ `player->inCombat = true`,
  `lastCombatTurn = totalMoves` (`:72-75`); `updateStats()` too (`:64-66`).
* `attackerWeaponId = attackerMonster->ce.weapon` (`:88-89`);
  `attackFrame = 64` (MANIM_ATTACK1) for weapon 18, `80` (MANIM_ATTACK2) when the
  weapon equals monsterAttacks field 1, else `64` (`:90-99`).
* `attackerWeapon = id*9`; `animLoopCount = NUMSHOTS` (no ammo cap for monsters,
  `:112-120`); `attackerWeaponProj = PROJTYPE` (`:121`).
* `worldDist = curAttacker->distFrom(viewX, viewY)`, `tileDist` (`:122-125`).
* `stage = 0`, `nextStageTime = 0` (`:107-110`; the `punchingMonster` path is
  player-only).
* **Weapon 18 CHARGE lunge** (`:131-164`): lerp the sprite from its position to
  `(viewX + sx*64 − sx*28, viewY + sy*64 − sy*28)` (36 units short of the player
  on the monster's side, sx/sy = sign of monster−view), z = floor+32,
  `travelTime = (|dx+dy| >> 6) * 200` ms, flag ENT_NORELINK, `animatingEffects`
  lerp (`allocLerpSprite(..., true)`); anim byte = `attackFrame<<8` (0x40,
  frame 0), `frameTime = INT_MAX`; entity relinked onto the tile adjacent to the
  player; `stage = -1, nextStage = 0, nextStageTime = gameTime + travelTime`.
  The lerp's completion resets `frameTime = 0` and anim byte → 0/0x10
  (`src/Game.cpp:3100-3111`), which is what un-blocks monsterSeq's first branch.
* `canvas->setState(ST_COMBAT)` (`:165`).

## 4. `Combat::monsterSeq` — frame by frame (`src/Combat.cpp:427-610`)

Driver: `GameStateRunner::combatState` → `monsterLerp`, `updateLerpSprites`,
`combat->runFrame()` (`src/GameStateRunner.cpp:20-40`); `runFrame` = `playerSeq` if
`curAttacker == nullptr` else `monsterSeq` (`src/Combat.cpp:857-862`). Return 1 =
keep running, 0 = attack finished (`:597`, `:606-609`); also 0 when the
canvas entered ST_DYING / ST_BOT_DYING (`:606-608`).

Clocks: `S = SHOTHOLD * 10` ms (`weapons[w*9+8]`). `monster->frameTime` uses
`app->time`; `nextStageTime` / `animEndTime` use `app->gameTime`.

Every call, in this order:

**A. Pose advance + launch** (`:429-441`). Anim byte `n2 = info>>8 & 0xFF`; if
`(n2 & 0xF0) ∈ {64, 80}` and frame `(n2 & 0xF) == 0`:
if `time >= frameTime` → `frameTime = time + S`, `nextStageTime = gameTime + S`,
frame → 1, **`launchProjectile()`**. (While the pose is frame 0 the B branch is
skipped entirely — it is an `else if`.)

**B. Stage transition** (`:442-462`): if `nextStageTime != 0 && gameTime >
nextStageTime && numActiveMissiles == 0 && animatingEffects == 0`:
* reflection pending (`reflectionDmg > 0`, see §6) → monster blood,
  `curAttacker->pain(reflectionDmg/2, playerEnt)`; dead → `died(true, playerEnt)`
  else pain pose 250 ms + `nextStageTime = gameTime + 250`; `reflectionDmg = 0`
  (`:443-455`);
* else `stage = nextStage`, `nextStageTime = 0`, `nextStage = -1` (`:456-460`).

**C. Stage 0 — one shot** (`:462-571`, runs in the same call as B):
1. Reset `totalDamage, totalArmorDamage, hitType, crFlags, gotCrit, gotHit,
   targetKilled, reflectionDmg` (`:464-471`).
2. Sound type = MSOUND_ATTACK2 if weapon == field 1 else MSOUND_ATTACK1
   (`:479-485`).
3. If `STRMAX > 0` (signed byte): vs player →
   `player->ce->calcCombat(&attackerMonster->ce, playerEnt, true,
   player->distFrom(curAttacker), -1)` (`:488-489`; `Player::distFrom` =
   Chebyshev² to `canvas->destX/Y`, `src/Player.cpp:1948-1950`). Hit iff
   `crFlags & 0x1007` → `gotHit`, `hitType = 2` on crit (0x2) else 1,
   `damage = crDamage`; miss → `damage = 0` (`:490-503`). Vs a non-player target:
   `hitType = 1, damage = 1` (`:505-508`). `totalDamage += damage`,
   `totalArmorDamage += crArmorDamage` (`:509-510`).
   **Damage is only computed here; it is applied later (step A of a later frame).**
4. Sound (`:512-522`): weapon 17 PUNCH that missed → 1100 `Revenant_swing.wav`;
   weapon 28 → 1087 `plasma.wav`; else `getMonsterSound(sub, parm, type)`;
   `playCombatSound(id, 0, 4)` (`:512-522`). `getMonsterSound` = `monsterSounds[sub*8+type] +
   1000` (255 = none), with Pinky parm0 / Mastermind parm1 overrides
   (`src/Game.cpp:3771-3845`).
5. `--animLoopCount`; `animTime = S`; `animStartTime = gameTime`; `animEndTime =
   gameTime + S` (`:524-527`).
6. Weapon != 18: anim byte = `attackFrame` (frame 0), `frameTime = time + S`
   (`:528-531`).
7. `curTarget != nullptr` ⇒ `targetKilled = true` (`:532-534`) (scripted monster vs
   entity attacks always kill at stage 1).
8. Weapon 18: lerp back 500 ms to the centre of `linkIndex` (the tile adjacent to
   the player), anim byte 0, `frameTime = time + 500`, `launchProjectile()` now
   (`:535-551`) — so the charge's damage lands in this stage-0 call.
9. `gotHit && (weapon 23 M_FIRE || attacker eSubType 2 Lost Soul)` →
   `player->addStatusEffect(13, 5, 3)` + `translateStatusEffects()` (`:552-555`).
10. `stage = -1`; `nextStage = animLoopCount <= 0 ? 1 : 0`; `nextStageTime =
    animEndTime` (`:556-563`).
11. Imp + weapon 16 CLAW + last shot → toggle mirror bit `info ^= 0x20000`
    (`:564-566`) (tile check `render->isImp(tileIndex)`).
12. `updateProjectile()` (normally a no-op here — nothing launched yet unless weapon
    18), `crFlags & 0x80` → msg 67 (never set anywhere in this port), invalidate
    (`:567-571`).

**D. Stage 1 — finish** (`:574-597`): anim byte → 0 (ET_MONSTER); `targetKilled`
→ `curTarget->died(false, nullptr)`; toast: reflect buff & non-boss & damage → msg
131, else damage → msg 112 with args (attacker name `def->name & 0x3FF`,
`accumRoundDamage`); `shakeTime = 0`, `hud->damageDir = 0`, `damageTime = 0`;
`attackerMonster->flags |= 0x400`; `gsprite_clear(64)`; **return 0**.

**E. Stage −1 — waiting** (`:599-604`): `staleView = true`; while
`numActiveMissiles != 0 || exploded` → `updateProjectile()` (flight/impact and
damage application).

### 4.1 Timeline of one attack (non-charge)

| t | what happens | cite |
|---|---|---|
| 0 | stage 0: hit/damage rolled, attack sound, pose = ATTACK1/2 frame 0 | `:463-531` |
| S | frame 0→1 (fire pose / muzzle flash), `launchProjectile()`; hitscan/melee → `exploded=true` → same call stage −1 → `updateProjectile` → **`explodeOnPlayer` (damage applied)** | `:434-441`, `:599-604`, `:1421-1429` |
| S + flight | missile weapons: missile reaches target → impact anim (sets `nextStageTime = gameTime + anim duration`) → `explodeOnPlayer` | `:1286-1398`, `:1421-1424` |
| ≥ 2S (and no missiles / lerps) | B: `stage = nextStage` → next shot (stage 0, back to frame 0) or stage 1 | `:442-462` |

Total per shot ≈ 2·S + flight (+ impact anim). NUMSHOTS shots per attack. E.g.
imp CLAW (16): SHOTHOLD 10 → S = 100 ms, 2 shots → ≈ 400 ms; zombie FLESH_THROW
(19): S = 300 ms + flight (≈ 250 ms per tile at speed 256, §5.2), 1 shot.

### 4.2 Sprite frames drawn during the attack (`src/Render.cpp:3295-3462`)

Anim 64/80 → torso art base `n26 = 8` (ATTACK1) / `10` (ATTACK2) (`:3302-3307`).
Frame 0 = wind-up torso `n26`; frame 1 = `n26+1` **only for monsters without gun
flare** (and Revenant ATTACK2) (`:3354-3356`). `hasGunFlare` = Mancubus, Revenant,
sentry bots, Cyberdemon, Mastermind (`src/Render.cpp:3019-3021`): they keep torso
`n26` on frame 1 and draw the flash as art frame `n26+1` with renderMode 4,
`scaleFactor/3`, flip bits `((totalMoves + animLoopCount) & 3) << 17`, per-monster
offsets (Mancubus two flashes at ±22/−24 lateral z 128/160; Revenant −9/15 lateral
z 736; sentry z −64; Cyberdemon 14 lateral z 352) (`:3413-3460`). Zombies mirror
their legs on frame 1 (`:3299-3301`); Pinky draws its own 3-part layout
(`:3314-3321`); tileNum > TILENUM_LAST_MONSTER draws only `n26` (`:3310-3313`).
Imps have no head in attack poses (`:3376`). Imps/zombies/goblins: no flare; the
frame-1 art *is* the attack pose.

## 5. Hitscan vs missile for monsters

### 5.1 Dispatch — `launchProjectile` (`src/Combat.cpp:1433-1668`)

`default:` (PROJTYPE −1 NONE, 0 BULLET, 1 MELEE) → `missileAnim = 0; exploded =
true; return` (`:1572-1576`): **no missile, damage applied in the same frame**.
Monster weapons by class (table 2, full dump in the projtype-inventory log):

| class | ids (PROJTYPE) |
|---|---|
| melee, instant | 15 BITE, 16 CLAW, 17 PUNCH, 18 CHARGE (all 1) |
| hitscan, instant | 23 M_FIRE (−1), 24 M_MACHINE_GUN (0), 25 M_CHAIN_GUN (0) |
| missile | 19 FLESH (6), 20 FIREBALL (7), 21 PLASMA (8), 22 FLOOR_STRIKE (9), 26 ROCKETS (4), 27 ACID (10), 28 PLASMA_GUN (5), 29 VIOS_PLASMA (8), 30 VIOS_LIGHTNING (11), 31 VIOS_POISON (5) |

Monster missile cases (`:1482-1571`): 4 → anim 226 speed 320, lateral ±16 offset
(sign by the sprite's mirror bit, `:1617-1630`); 5 → 244 renderMode 4 speed 384
(`n += n>>1` for monsters, `:1522`); 6 → 227 rm 0; 7 → 242 rm 3; 8 → 241 rm 3; 9 → 171,
z −32 both ends, `numThornParticleSystems = 0`; 10 → 252 rm 3, z1 += 48;
11 → 248 rm 3. Default speed `n = 256` world units/s.

### 5.2 Missile geometry & flight

* Start = attacker sprite x/y/z; `n2 = 16` push-out toward the target on the major
  axis (`:1453-1461`, `:1605-1616`). End = `game->viewX/viewY`,
  `getHeight + 32` (`:1465-1469`).
* End adjustments (`:1578-1616`): hit → end pulled back `n3 = 16` along the major
  axis, no side offset; miss vs player (`hitType == 0`) → `n3 = 0` and a random
  side offset `n5 = ±16` on the minor axis (`dodgeDir = nextInt() > 0x3FFFFFF`,
  `:1444-1445`); out of range (`crFlags & 0x400`) → end clipped to
  `RANGEMAX*64` along the major axis.
* `duration = 1000*max(dx,dy)/n` ms (dx/dy measured before the ±16 tweaks)
  (`:1632`). `allocMissile` (`:1670-1717`): max 8 missiles (error
  `ERR_MAX_MISSILES`), gsprite flags `2562 = 0x2|0x200|0x800`, linear
  `vel = 1000*(end-start)/duration`. `gsprite_update` moves it
  `pos + vel*t/1000` (by `app->time`) and clears flag 0x2 on arrival
  (`src/Game.cpp:1531-1557`).
* Dead code: `launchProjectile` sets per-anim frame bytes (0x10/0x20/0x30) then
  `if (!b) info &= 0xFFF700FF` with `b` always false — the frame byte and bit 19
  are always cleared (`:1633-1645`, `:1663-1666`).

### 5.3 Impact — `updateProjectile` (`src/Combat.cpp:1248-1431`)

On arrival (flag 0x2 cleared) the switch picks an impact anim (`:1292-1373`):
4 → 242 rm 4 + `startShake(500,4,500)` + sound 1032; 10 → 252; 5 → 244;
7 → sound 1034, 242 rm 4 (or 208 rm 3 at half step toward the view when the
player hit has buff 9 ANTIFIRE); 9 → 170 + particles, z+48; 6/8/11 → `default`
→ no impact anim. The impact anim is spawned only if the attacker is the player
or (`curTarget == nullptr && hitType != 0`) — **monster misses leave no impact
anim** (`:1380`); it inherits the attacker's mirror bit (`:1389-1391`) and
sets `nextStageTime = gameTime + duration` (`:1383`). Missile freed,
`exploded = true` (`:1394-1397`) → `explodeOnPlayer` (`:1421-1424`). The
`(1 << damageDir & 0x1C) == 0 && !b` suppression at `:1375-1377` is dead (`b` is
always true, `:1287`).

Melee splash (`:1401-1420`): no missiles, `gotHit && (totalDamage > 0 ||
totalArmorDamage > 0) && exploded` and weapon ∈ {15,16,17,18} → anim 245 (CLAW
16) / 246 (BITE 15) / 247 (PUNCH/CHARGE) at `(destZ, destY, destZ)` (verbatim
port quirk), renderMode 5, only if `game->isInFront(attacker tile)`; 245 inherits
the attacker's mirror bit. Enum names: `TILENUM_MONSTER_CLAW = 245`,
`_BITE = 246`, `_BLUNT_TRAUMA = 247` (`src/Enums.h:818-820`).

Thorns (9) while flying spawn a particle burst + anim 170 on each tile passed
(`:1259-1284`); Electric (248) toggles its mirror bit each frame (`:1256-1258`).

## 6. Hit & damage monster → player

### 6.1 `calcCombat` / `calcHit` (`src/CombatEntity.cpp:130-298`)

Called on `player->ce` with attacker `ce` = monster, `b = true` (target is player).
`crDamage = crArmorDamage = 0` (`:133-134`). `crFlags` was zeroed by monsterSeq.

* Range penalty `n7` = tiles below RANGEMIN / above RANGEMAX (`:219-233`);
  fixed-range weapon (MIN == MAX, i.e. BITE/CLAW/PUNCH 1..1) out of range →
  `0x400` auto-miss (`:236-238`). (`crFlags & 0x10` zeroes n7 — splash only.)
* `hit = ((ACC_monster − (AGI_player·96 >> 8)) << 8) / 100 − 16·n7`, floor 1
  (`:242-251`; the 96/256 scaling is skipped only for weapon mask 0x800 = id 11).
* Roll `r = nextByte()`; miss iff `monsterMissRepetition < 2 && r > hit` →
  `++monsterMissRepetition` (`:252-270`, test at `:263`) — **at most 2 consecutive monster misses;
  the 3rd roll is a forced hit.** Hit resets the counter (`:279-281`).
* Player status effect [18] > 0 on a hit: decrement (remove at 0), set CR 0x100
  (`:272-278`) — but calcHit then continues to set 0x1/0x2, so damage is still
  computed; 0x100 is read only by miss messages (`src/Combat.cpp:1042`).
* Crit: chance = `hit/20` only on difficulty 4 for monsters (else 0)
  (`:288-292`); `nextByte() < chance` → 0x2 else 0x1.
* Back in `calcCombat`: damage 0 and armor damage 0 → add 0x100 (`:150-152`).

### 6.2 `calcDamage` monster branch (`src/CombatEntity.cpp:300-378`)

```
base = STRMIN & 0xFF ; max = STRMAX & 0xFF                          (:302-304)
if player buffs[0] (REFLECT) > 0: return crDamage = max (armor 0)   (:311-313)
difficulty 4: base -= base >> 2                                      (:314-316)
crit (0x2|0x2000): base = 2*max ; CR 0x4: base = max/2
else if max != base: base += nextByte() % (max - base)   -- max EXCLUSIVE (:317-325)
if !(crFlags & 0x20): base += (STR%·base) >> 8                       (:326-333)
   STR% = (str << 8)/100 (src/CombatEntity.cpp:58-60); monsters get 1x,
   the PLAYER gets 3x ("!b" branch).  crFlags is 0 here → always applied.
not familiar: armorDmg = min(((171·base >> 8) + 1) / 2, player ARMOR)
              damage   = base − 2·armorDmg                           (:356-359)
familiar type 1/2: damage = base·7/8 ; other familiar: base·5/8     (:360-365)
weapon ∈ {20 FIREBALL, 23 M_FIRE} (mask 0x900000) && buffs[9] ANTIFIRE → 0,0 (:366-369)
crDamage = damage − (DEF%_player · damage >> 8)                      (:371)
crArmorDamage = armorDmg                                             (:375-376)
```

No weapon weakness for monster attackers (`getWeaponWeakness` is only in the
player branch, `:340`). Correction to `docs/research/2026-08-26-combat-stage1.md`
§2.4, which has the ×3 / ×1 strength factors swapped (`combat.md` §5 is right).

### 6.3 `explodeOnPlayer` (`src/Combat.cpp:948-1055`) — application

* Returns if `curTarget != nullptr` (`:950-952`).
* `hud->damageDir = player->calcDamageDir(view, viewAngle(+zoomAngle), attacker
  sprite xy)`; `damageTime = time + 150` if more shots follow else `+1000`;
  `staleTime` when dir != 3 (`:953-967`).
* Hit with damage (`gotHit && (totalDamage > 0 || totalArmorDamage > 0)`,
  `:972`): `player->painEvent(attacker, true)`; attacker unlinked during the
  update; PROJTYPE 7 → status effect 13 (burn) (`:976-980`);
  `addArmor(−totalArmorDamage)` (`:981`). If `totalDamage > 0`:
  * Reflect buff (buffs[0] > 0 and attacker not a boss): player takes nothing;
    `accumRoundDamage = reflectionDmg = totalDamage`, `animLoopCount = 0`,
    `nextStage = 1` (`:1021-1025`) → monster takes `reflectionDmg/2` in §4 B.
  * Else **difficulty scaling** when `difficulty != 1` (default 1,
    `src/Game.cpp:43`): `totalDamage += (totalDamage >> 1) + loadMapID' /
    NUMSHOTS`, `loadMapID' = loadMapID·2` on difficulty 4 (`:984-990`); VIOS
    extras (+8·numMallocsForVIOS for parm 4, else +8 when angry) (`:991-999`);
    `accumRoundDamage += totalDamage`; **`player->pain(totalDamage, attacker,
    false)`** (`:1000-1001`); dead → return (`:1002-1004`); `rockView(200, view +
    6·sign(view − attacker), viewZ)` when no knockback in progress
    (`:1005-1016`); Pinky (sub 5) CHARGE (18) → `playerEnt->knockback(attacker
    xy, 1)` (`:1017-1019`).
  * `totalDamage <= 0` → `damageCount = 0`, msg 72 (`:1027-1033`).
  * Relink attacker; familiar with `ammo[7] <= 0` → `familiarDying` (`:1034-1037`).
* Miss (`:1040-1054`): `damageCount = 0`; CR 0x100 → msg 73; else (unless
  0x80) msg 69 with the attacker name; `damageCount = −1`.

## 7. Data — shipped values (UnPackGameData/tables.bin, tmp_entities.bin)

Tables per `src/App.cpp:353-402`, offsets per `src/Resource.cpp:209-322`: table 0
`monsterAttacks` = 150 shorts; table 2 weapons 32×9 int8 (verified anchor row 0 =
`8,10,0,5,1,1,0,2,50`); table 3 `monsterStats` 54×6 (loaded 51 at
`src/Combat.cpp:37-39`, `hp = 5·byte`); table 13 `monsterSounds` 17×8 ubyte.

Monster defs (eType 2; `tile sub parm → field0 / field1 / thr ; hp arm def str acc agi`):

| tile | monster | field0 | field1 | thr | hp | def | str | acc | agi |
|---|---|---|---|---|---|---|---|---|---|
| 20 | zombie (0,0) | 19 FLESH | – | 0 | 40 | 5 | 12 | 95 | 0 |
| 21 | zombie (0,1) | 19 | – | 0 | 50 | 10 | 18 | 97 | 0 |
| 23 | imp (3,0) | 16 CLAW | – | 0 | 50 | 5 | 12 | 95 | 0 |
| 24 | imp (3,1) | 16 | 20 FIREBALL | 75 | 60 | 10 | 15 | 97 | 5 |
| 25 | imp (3,2) | 16 | 20 | 50 | 75 | 10 | 18 | 100 | 10 |
| 26 | saw goblin (4,0) | 17 PUNCH | – | 0 | 50 | 10 | 18 | 100 | 5 |
| 29 | lost soul (2,0) | 15 BITE | 18 CHARGE | 75 | 100 | 20 | 38 | 97 | 25 |
| 32 | pinky (5,0) | 15 | 18 | 75 | 80 | 10 | 18 | 95 | 0 |
| 35 | revenant (9,0) | 26 ROCKETS | 17 | 178 | 150 | 35 | 50 | 95 | 20 |
| 38 | mancubus (8,0) | 26 | – | 0 | 125 | 55 | 50 | 95 | 5 |
| 41 | cacodemon (6,0) | 15 | 21 PLASMA | 0 | 100 | 20 | 25 | 95 | 20 |
| 44 | sentinel (7,0) | 22 FLOOR_STRIKE | – | 0 | 50 | 18 | 25 | 95 | 30 |
| 50 | arch-vile (10,0) | 23 M_FIRE | – | 0 | 150 | 35 | 44 | 95 | 15 |
| 19 | sentry bot (11,0) | 24 MACHINE_GUN | – | 0 | 50 | 5 | 12 | 97 | 15 |
| 18 | red sentry (11,1) | 24 | – | 0 | 100 | 10 | 24 | 100 | 25 |
| 53 | arachnotron (14,0) | 28 PLASMA_GUN | – | 0 | 150 | 20 | 25 | 95 | 60 |
| 57 | mastermind (14,1) | 25 CHAIN_GUN | – | 0 | 350 | 30 | 38 | 100 | 35 |
| 54 | cyberdemon (12,0) | 26 | – | 0 | 400 | 40 | 50 | 95 | 25 |
| 56 | boss pinky (13,0) | 27 ACID | – | 0 | 300 | 30 | 38 | 97 | 5 |
| 58-62 | VIOS (15,0-4) | 29 | 30 (parm≥1) | 152 | 400-500 | 15 | 38-88 | 100 | 65 |

Armor stat is 0 for every monster. Attack sounds (`monsterSounds` +1000,
`src/Game.cpp:3829-3833`): imp ATTACK1 1048 `impClaw.wav`, ATTACK2 1033
`fireball.wav`; zombie ATTACK1 1100 `Revenant_swing.wav`; lost soul/caco ATTACK1
1021 `claw.wav`; saw goblin 1016; sentry 1014 `chaingun.wav`; arch-vile 1005.

**map00 monster census** (sprites parsed via `tools/map_to_obj.py`): imp (3,0) ×20,
zombie (0,0) ×15, zombie (0,1) ×3, imp (3,1) ×2 — no hitscan monster at all.

## 8. Porting scope implications

* A "melee + hitscan" first stage covers weapons 15-18 and 23/24/25, but on map00
  it only makes the **imps (CLAW 16)** fight. **Zombies (the second most common
  map00 monster) need PROJTYPE 6 FLESH missiles** (anim 227, no impact anim), and
  imp (3,1) needs PROJTYPE 7 FIREBALL (242 + impact 242/sound 1034 + burn status).
* Minimum missile set for map00: 6 and 7. Next tier (other early maps unknown here):
  4 rockets, 8 caco plasma, 9 thorns, 5 BFG-type, 10 acid, 11 electric.
* Weapon 18 CHARGE (lost soul, pinky) needs the lunge lerp pair (§3, §4 C.8).
