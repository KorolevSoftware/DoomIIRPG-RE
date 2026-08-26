# 2026-08-26 — Stage-1 combat package: fire pipeline, turn structure, monster loop, HUD, rewrite delta

Tasking: port-ready research for (Stage 1) first-person weapon display + basic combat
(fire → hit/damage → pain/death → lootable corpse) and (Stage 2 outline) monster AI/turn
flow, so scripted map00 fights after the elevator area become playable.

Read-first docs already answered rendering/loot/collision; this report covers what they
did not: turn structure, fire→damage formulas, monster minimal loop, combat HUD, and an
EXISTS/MISSING audit of `new_src/`. Verdicts inline; every claim cites `src/` or
`new_src/`.

**Verdicts up front**

* TURN STRUCTURE: CONFIRMED — one player action ⇒ `Game::advanceTurn`
  (`src/Game.cpp:1238-1281`) arms `monstersTurn`; per-frame `updateMonsters`
  (`src/Game.cpp:2458-2474`) runs AI + lerps + queued attacks until quiet, then
  `endMonstersTurn`. `advanceTurn` under unsnapped monster lerps is fatal Error 95.
* FIRE PIPELINE: CONFIRMED end-to-end — short "use ray" picks a target entity;
  damage itself is pure stat math (`CombatEntity::calcHit/calcDamage`) vs table data;
  hitscan weapons apply `pain()` immediately in stage 0, death applies in stage 1 after
  the SHOTHOLD animation; no floating damage numbers exist (message log only).
* MONSTER LOOP: OUTLINE CONFIRMED — activation = render sight-trace / EV_WAKEMONSTER /
  being shot; goal machine {move/chase/attack/flee}; attacks queue on `combatMonsters`
  and execute between lerps inside the monster phase.
* REWRITE DELTA: PARTIAL — data layer fully present (all tables parsed), entity layer
  has monsters-without-monsters (`Entity::monster == nullptr`, no stats), no Combat
  state, no fire path; ScriptVM still thread-kills on ops 10/51/56.

---

## 1. Turn structure

### 1.1 What consumes a turn

| Action | Turn consumed at | Cite |
|---|---|---|
| Move (committed) | arrival → `finishMovement()` tail calls `advanceTurn()` | `src/MovementController.cpp:160-196` |
| Blocked move in ST_AUTOMAP | immediately (`advanceTurn()` even though rejected) | `src/MovementController.cpp:351-353` |
| ACTION_FIRE → tile event ran | after `executeTile(...)` returns true, unless `skipAdvanceTurn` | `src/PlayingInputHandler.cpp:398-404` |
| ACTION_FIRE → door open | after `performDoorEvent` | `src/PlayingInputHandler.cpp:451-453` |
| ACTION_PASSTURN | directly | `src/PlayingInputHandler.cpp:550-555` |
| Loot UI stand-up expiry | `setState(ST_PLAYING)` + `advanceTurn()` | `src/LoothingSystem.cpp:79-81` |
| Player attack | NOT synchronously — after the combat seq finishes, `combatState()` calls `advanceTurn()` when `combatDone && !interpolatingMonsters && curAttacker==nullptr` | `src/GameStateRunner.cpp:26-38` |
| Monster attack seq end | monster's own turn already armed; nothing extra | `src/GameStateRunner.cpp:47-79` |

Scripted `EV_ADVANCETURN` snaps monsters, undoes pending attacks, `endMonstersTurn()`,
then `advanceTurn()` (`src/ScriptThread.cpp:1276-1288`). Dialog close can set
`queueAdvanceTurn = true` which the next playing tick consumes
(`src/DialogSystem.cpp:531`, `src/Canvas.cpp:812`); `advanceTurn` clears it
(`src/Game.cpp:1240`).

### 1.2 `Game::advanceTurn()` (`src/Game.cpp:1238-1281`)

```
queueAdvanceTurn = false                                  (:1240)
if (interpolatingMonsters) app->Error(95)  // ERR_NONSNAPPEDMONSTERS   (:1241-1243)
b = true; if haste (statusEffects[2]>0): b = (++statusEffects[20] % 2 == 0)  (:1244-1253)
pushedWall=false; player->advanceTurn(); updateBombs()    (:1254-1256)
monstersTurn = b ? 1 : 2                                  (:1257-1262)
monstersUpdated = false; lastTurnTime = time              (:1263-1264)
if (b) { all entities updateMonsterFX(); updateFacingEntity=true }  (:1265-1270)
auto-close openDoors where CanCloseDoor                   (:1271-1278)
executeStaticFunc(6)  // SCR_PER_TURN                     (:1279)
canvas->startRotation(true)                               (:1280)
```

* **Error 95 constraint**: every caller that can run mid-lerp must `snapMonsters(true)`
  first (fire path `src/PlayingInputHandler.cpp:401`; EV_ADVANCETURN
  `src/ScriptThread.cpp:1278-1280`). `snapMonsters` also *drives* the monster turn:
  when `monstersTurn != 0` it runs `monsterAI()` + `monsterLerp()` and can launch a
  queued `performAttack` synchronously (`src/Game.cpp:2411-2449`).
* `Player::advanceTurn` (`src/Player.cpp:51-92`): counters, status-effect ticks
  (poison [53], infection [13] −3 hp w/ msg 82+71), combat flag decay after 4 idle
  turns, `turnTime`.
* **PER_TURN static func**: index 6 = `SCR_PER_TURN` (`src/Enums.h:504`); map00
  `staticFuncs[6] = 253` (imp-corpsify poller,
  `docs/research/2026-08-25-unhandled-script-events.md` §Method). Other indices:
  0 INIT_MAP, 1 END_GAME, 2/3/4 boss 75/50/25%, 5 BOSS_DEAD, 7 ATTACK_NPC,
  8 MONSTER_DEATH, 9 MONSTER_ACTIVATE (`src/Enums.h:498-510`; fired from
  `Game::activate` when `MFLAG_TRIGGERONACTIVATE`, `src/Game.cpp:800-803`).
* `monstersTurn == 2` (haste parity) lets only `isHasteResistant()` monsters act
  (`src/Game.cpp:890`).

### 1.3 When monsters act relative to the player

`monstersTurn != 0` opens the window; `playingState()` runs `updateMonsters()` each
frame while there is no knockback / propagator / animating effect / help dialog
(`src/GameStateRunner.cpp:181-183`). Input during the window is dropped by the
`snapMonsters()` gate except turning and weapon switching
(`src/PlayingInputHandler.cpp:71-74`). When the last lerp settles and no attacker is
queued → `endMonstersTurn()` sets `monstersTurn = 0`
(`src/Game.cpp:2452-2456`, `:2468-2473`).

## 2. Fire pipeline end-to-end

### 2.1 Target selection — the ACTION_FIRE probe (`src/PlayingInputHandler.cpp:189-378`)

* Mask `n5 = 13997` = CONTENTS_WEAPONSOLID (= playersolid − PLAYERCLIP + CORPSE;
  decomposition in `docs/original-code/player-collision.md` §2.2). Chainsaw
  (`CheckWeaponMask(w,2)` i.e. id 1) shrinks the probe to `n7 = 1` unit and adds
  bit 0x10 (PLAYERCLIP lines block); holy-water pistol (id 2) adds 0x4100
  (ENV_DAMAGE + DECOR_NOCLIP) (`:200-213`).
* Probe ray = **only ~6 units long**, along the camera forward taken from the TinyGL
  view matrix rows −view[2]/−view[6]/−view[10] (`:218-221`). This ray does NOT do the
  damage — it elects the target entity and loot candidates.
* Candidate scan over sorted `traceEntities[]` (`:224-368`): world/spritewall/
  playerclip (eType 0/12/4) stops the election; ATTACK_INTERACTIVE (10) selectable
  unless subtype-bit0 set or chainsaw; NPC (3) needs `dist >= 8192` (= tileDistances[1]... note: 8192 = (64·√2)²? no — 8192 = 2·4096; treated as raw compare);
  **ET_MONSTER (2) always wins immediately** (`:264-269`); doors (5), corpses (9)
  only at `dist == combat->tileDistances[0]` (adjacent tile) with loot rules —
  chainsaw selects any adjacent corpse for gibbing (IOS branch `:315-321`),
  otherwise lootable iff `(monster->flags & 0x800)==0 && lootSet != nullptr`
  (`:331-335`).
* Loot wins first: `n6 != 0` → `setState(ST_LOOTING)` + `poolLoot` + return
  (`:374-378`).
* Zoom-capable weapon (mask 512 → scoped rifle id 9) with ammo enters zoom instead of
  firing (`:504-507`).
* Fallbacks: wall push (`shiftWeapon(true)` + rockView, msg-free, `:467-487`);
  env-damage shot (`:497-502`); sentry-bot deploy/self-destruct (`:489-496`); else
  `player->fireWeapon(entity|entities[0], collisionX, collisionY)` (`:497-545`).

### 2.2 `Player::fireWeapon` (`src/Player.cpp:754-799`)

Guards, in order: soul cube needs a monster target (`:757-759`);
`combat->weaponDown` or active weapon masked by `disabledWeapons` → refuse (`:761-763`)
(**disabledWeapons mask** — set by op 60 EV_DISABLED_WEAPONS,
`src/ScriptThread.cpp:1443-1451`); lower/raise lerp cancel (`:765-771`);
`entity->monster->flags &= 0xfff7` (clear MFLAG_KNOCKBACK 0x8) (`:773-775`);
chainsaw vs corpse/barricade/furniture → `usedChainsaw(false)` gib path (`:777-779`);
ammo check `ammo[weapons[w*9+4]] < usage` → msgs 117 (soul cube) / 115 (empty) /
116 (partial) and refuse (`:781-796`). Then `performAttack(nullptr, entity, x, y)`.

### 2.3 `Combat::performAttack` (`src/Combat.cpp:54-166`)

Stores curAttacker/curTarget (+`info |= 0x200000` both), resets accumulators;
marks `player->inCombat` when attacking a monster (`:72-75`); caches
targetType/targetSubType/targetMonster; attackerWeaponId = player weapon or
`attackerMonster->ce.weapon`; attackFrame = 64 (ATTACK1) or 80 (ATTACK2) when the
weapon matches monster field 1 (`:89-100`); `animLoopCount = weapons[w*9+7]`
(NUMSHOTS, capped by ammo pool for the player `:113-120`); worldDist =
`curTarget->distFrom(view)` (**Chebyshev²**: `distFrom = max(dx²,dy²)`,
`src/Entity.cpp:1155-1158`); tileDist via `WorldDistToTileDist`
(`src/Combat.cpp:1235-1242`, thresholds `tileDistances[j] = (64(j+1))²`,
`:41-44`); melee-lunge staging for monster weapon 18 (`:131-164`); enters
`ST_COMBAT` (`:165`).

### 2.4 Hit & damage — exact formulas

Player-vs-monster uses `CombatEntity::calcCombat` → `calcHit` + `calcDamage`
(`src/CombatEntity.cpp:130-155`); CR flags land in `combat->crFlags`, damage in
`crDamage`.

`calcHit` (`src/CombatEntity.cpp:157-298`):

* Sniper-zoom path (weapon mask 0x200 = id 9): pixel-bbox body-part test vs
  `getImageFrameBounds(tile,3,2,0)` → head = crit flag 0x2, torso = 0x1, legs = 0x5
  (`:173-216`).
* Range: `td = WorldDistToTileDist(dist)`; outside `[RANGEMIN..RANGEMAX]` → penalty
  `n7` tiles; fixed-range (min==max) or punch-flag 0x40 weapons out of range →
  **CR 0x400 "out of range" auto-miss** (`:219-238`); shotgun(7)/pistol(2) beyond max
  set flag 0x4 (half-damage cap instead of hard miss) (`:224-229,:285-287`).
* `crHitChance = ((acc − agi') << 8)/100 − 16·n7`, floor 1; defender agility scaled
  `agi' = agi·96>>8` unless weapon mask 0x800 (`:242-251`).
* Miss-guard: a roll above chance misses only if the attacker hasn't already missed
  consecutively (player limit 1, monster limit 2) and (monster ‖ mapID<8 ‖ tileDist>1)
  (`:252-271`) — no infinite whiff streaks.
* Defender dodge status [18] → CR 0x100, consumed (`:272-278`).
* Crit chance = `crHitChance/20` (attacker is player OR difficulty 4; else 0),
  roll < chance → CR 0x2 else CR 0x1 (`:288-297`).

`calcDamage` (`src/CombatEntity.cpp:300-378`):

```
base = rand in [STRMIN..STRMAX] of weapons[ce.weapon*9 + {0,1}]      (:302-324)
  soul cube (13): ×2                                                 (:306-309)
  difficulty 4: base −= base>>2  (before crit doubling!)             (:314-316)
  CR 0x2|0x2000 → base = STRMAX*2 ; CR 0x4 → base = STRMAX/2         (:317-325)
  if !(CR & 0x20): strength bonus
     attacker monster: base += 3*(str%·base)>>8                     (:326-329)
     attacker player:  base += (str%·base)>>8                       (:330-333)
     (CR 0x20 set when hitting a MONSTER with anything but chainsaw,
      src/Combat.cpp:223-226)
if attacker is player (b=false):
  weakness = getWeaponWeakness(wp, subType, parm)                    (:340)
     = (monsterWeakness[(sub*3+parm)*8 + wp/2] nibble + 1) << 5      (src/Combat.cpp:29-31)
  holy water (wp 2): subtype 2 → ×4 (×2 on diff 4), subtype 0 → ×0   (:341-353)
  damage = weakness·base >> 8          (i.e. ×(nibble+1)/8)          (:354)
else attacker monster:
  armorDamage = min(((171·base>>8)+1)/2, armor); damage = base − 2·armorDamage  (:357-359)
final crDamage = damage − ((def% · damage) >> 8), def% = def·256/100 (:372)
```

Non-monster targets skip all of this: `hitType = Combat::calcHit(entity)` — range gate
+ `info & 0x20000` + optional `tableCombatMasks[parm] & 1<<weapon` for
ATTACK_INTERACTIVE — returns 0/1 only (`src/Combat.cpp:864-883`), and damage is rolled
straight from STRMIN/STRMAX with the difficulty-4 cut (`src/Combat.cpp:249-258`).

Extracted table values (`UnPackGameData/tables.bin`, layout verified against loader
`src/App.cpp:354-408`, stride constants `src/Combat.h:26-36`):

* weapons (table 2, 9 int8/weapon) — assault rifle (0): dmg 8–10, range 0–5,
  ammo bullets×1, proj 0 (hitscan), NUMSHOTS 2, SHOTHOLD 50; super shotgun (7):
  25–30, shells×2, proj 0, SHOTHOLD 30; chaingun (8): 12–15 ×4 shots, SHOTHOLD 15;
  holy water pistol (2): 5–8, water×2, proj 2, SHOTHOLD 50.
* monsterStats (table 3, 6 int8 per `subType*3+parm`, read as
  `new CombatEntity(5*b0, b1..b5)` `src/Combat.cpp:37-39`): **imp (3,0)** =
  hp 50, armor 0, def 5, str 12, acc 95, agi 0; imp (3,1) = 60/…; pinky (2,0) =
  100 hp / agi 25.
* Difficulty scaling at spawn: difficulty 4 (any) or 2 (non-boss) →
  `maxHP = HP += hp>>2` (+25%) (`src/Entity.cpp:62-67`).
* monsterWeakness (table 11) imp(3,0) nibbles: rifle 7 → ×1.0, holy water 15 → ×2.0,
  most others 7 → ×1.0 (index 15 unused).

### 2.5 Sequence playback & applying the damage

`Canvas::setState(ST_COMBAT)` drives `combatState()` → `combat->runFrame()` →
`playerSeq()` (attacker null) / `monsterSeq()` (`src/Combat.cpp:857-862`,
`src/GameStateRunner.cpp:20-87`).

`playerSeq` stage 0 (`src/Combat.cpp:188-366`):

* resets totals; `flashTime = 1`; counters[6]++ (`:188-200`); low-ammo warnings
  msgs 62/63 (`:213-222`).
* monster target → calcCombat (§2.4) then hit bookkeeping: crit → `gotCrit`,
  hitType 2/1, `deathAmt = hp − dmg`, damage-stat counters (`:223-247`).
* per-weapon fire sound: {3,5,9}→1086, 2→1045, 7→1117, {0,8}→1014, 10→1087,
  11→1101, 12→1009, 13→1114, 14→1136/1137 (choice 1), 1→1015 (`:261-308`).
* view-weapon animation clock: `animTime = weapons[w*9+8](SHOTHOLD) ×10` (×5 under
  haste [2]); `animStartTime/gameTime`; flashDoneTime = start + flashTime
  (`:311-321`) — these are exactly the fields `drawWeapon` consumes
  (`docs/original-code/combat.md` §1).
* `launchProjectile()` — **proj 0 default case allocates NO missile and sets
  `exploded = true` immediately** (`src/Combat.cpp:1572-1576`); projectile weapons
  allocate gsprite missiles (`:1433+`).
* recoil rockView for weapon ids ∉ mask 0x2406 (chainsaw/holy water/plasma/soul cube)
  (`:326-332`).
* **ammo deducted here**: `ammo[type] -= usage`, clamp ≥0 (`:355-358`);
  `hud->repaintFlags |= 0x4` refresh (`:353`); miss counter [7] (`:363-365`).
* stage 0 ends calling `updateProjectile()` (`:361`) which — because `exploded` —
  invokes `explodeOnMonster()` **in the same frame for hitscan weapons**
  (`:1421-1430`).

`explodeOnMonster` (`src/Combat.cpp:885-946`):

* runs pending script `explodeThread` (shouldFakeCombat tile scripts), sets
  `render->shotsFired` (suppresses further sight-wakes this batch) (`:887-894`).
* **activates an inactive monster that was shot**
  `activate(curTarget, true, false, true, true)` (`:895-897`).
* `totalDamage > 0` → `checkMonsterFX()` (status-effect transfer), then
  **`curTarget->pain(totalDamage, playerEnt)`** + blood particles (`:904-910`);
  knockback hook when flagged; rocket/BFG splash `radiusHurtEntities`
  (`:922-924`); healing cap branch for negative damage (`:926-931`).
* targetType 10 → pain; targetType 9 (corpse) → hide sprite + gib + `spawnDropItem`
  + sound 1037 (`:933-945`) — the **chainsaw corpse-gib outcome** (entered from the
  chainsaw selection at `src/PlayingInputHandler.cpp:315-321` +
  `usedChainsaw`, `src/Player.cpp:777-779`).

`Entity::pain` (`src/Entity.cpp:281-394`):

* gate `info & 0x20000`; boss phase hooks staticFuncs 2/3/4 at 75/50/25% hp with
  clamps (`:293-339`); MFLAG_NOKILL (0x4) floors hp at 1 (`:341-343`).
* survives: MSOUND_PAIN sound, anim byte `|= 0x6000` (MANIM_PAIN) +
  `frameTime = time+250` auto-revert, `resetGoal()` (unless chainsaw attacker)
  (`:345-357`).
* lethal: pain pose + longer frame freeze (`:358-369`).

`playerSeq` stage 1 (after animEndTime, `src/Combat.cpp:368-415`):

* BFG `checkForBFGDeaths` (`:379-381`).
* **`if (targetKilled || (targetType==2 && hp<=0)) curTarget->died(true,
  playerEnt)`** — death lands one full SHOTHOLD-animation AFTER the hit (`:382-386`).
* multi-shot repeat: `--animLoopCount > 0` and targetType ∉ {0,5,8,12} → back to
  stage 0 (`:387-392`).
* accumulated damage toast: str 70 ("Critical!") prefix + str 71 "took N damage"
  (`:401-409`); weapon 14 unequip-back (`:411-414`); return 0 →
  `touchTile` + `combatDone = true` → next tick `advanceTurn()` + ST_PLAYING
  (`src/GameStateRunner.cpp:26-50`).

`Entity::died` ET_MONSTER branch (`src/Entity.cpp:459-521`) — see
`docs/original-code/character-animation.md` §5 + `loot-inventory.md` §1.3 for the
visual/loot side; combat-relevant adds here: XP via `checkMonsterDeath(b=true)`
= `ce.calcXP()` (+130 bosses) (`src/Entity.cpp:396-422`); Lost Soul/Cacodemon leave
no corpse (poof + direct drop) (`:491-495`); def swap `find(9, subType, parm)`
(`:501`); `deactivate()` onto inactiveMonsters (`:485`).

Monster attack seq (stage 2 outline): `monsterSeq` (`src/Combat.cpp:427-610`)
advances the attack pose after SHOTHOLD·10 ms then launches the projectile
(`:434-441`); vs player rolls `calcCombat(&attackerCe, playerEnt, true, dist, -1)`
(`:487-503`); `updateProjectile` → `explodeOnPlayer`
(`src/Combat.cpp:948-1055`): damage-dir arrow + `painEvent` sound 1094/1093,
armor split, difficulty/map scaling `totalDamage += totalDamage>>1 + mapID/NUMSHOTS`
(non-buff, non-easy) (`:984-990`), `Player::pain`, msg 112 "<name> hits you for N" /
131 buff-absorb at stage 1 (`:581-590`), msg 69/73 misses, msg 72 zero-damage.

## 3. Monster minimal loop (map00 fights)

Names + cites only (deep dive deferred):

* **Spawn/load**: every sprite gets an entity; eType 2 allocates from
  `entityMonsters[80]`, `reset()`, random art flip, `info |= 0x40000` then
  `deactivate()` → starts on the `inactiveMonsters` ring
  (`src/Game.cpp:427-447`, `:825-855`); `initspawn` clones shared stats
  `combat->monsters[subType*3+parm]->clone(&m->ce)` + difficulty scaling + z snap +
  scale 64 (42 mastermind/pinky) + `info |= 0x20000` (`src/Entity.cpp:50-112`).
* **Wake conditions** (three funnels):
  1. Sight: while rendering a visible monster sprite, trace view→sprite
     (mask 5293, radius 2, z-check) hitting exactly that entity →
     `activate(e, true, /*range*/true, /*alert sound*/true, false)`
     (`src/Render.cpp:1576-1583`; range = tileDistances[3] = 4 tiles,
     `src/Game.cpp:763-765`).
  2. Shot: `explodeOnMonster` activate-on-hit (`src/Combat.cpp:895-897`).
  3. Script: op 28 EV_WAKEMONSTER → clear anim byte, `frameTime = 0`,
     `activate(e,true,false,false,true)` (silent)
     (`src/ScriptThread.cpp:876-892`); op 51 EV_AIGOAL → `setAIGoal`
     (`src/ScriptThread.cpp:1262-1274`).
* `Game::activate` (`src/Game.cpp:752-808`): clears anim byte, unhooks from
  `inactiveMonsters`, appends to circular `activeMonsters`, `info |= 0x40000`,
  clears MFLAG_NOACTIVATE, fires SCR_MONSTER_ACTIVATE (9) when
  MFLAG_TRIGGERONACTIVATE, plays MSOUND_ALERT1 in playing state.
* **op 51 semantics** (`src/ScriptThread.cpp:2226-2247`): ushort arg
  `{goalType = arg>>12&0xF, sprite = arg&0xFFF}` + byte arg goalParam; resetGoal;
  types 2/3 force goalParam=1, 4/6 take the byte; activates if not on the active
  list; `aiThink(true)`; type 3 additionally re-runs any queued `combatMonsters`
  attack immediately.
* **Goal machine** (`src/Entity.cpp`): `aiThink` = valid-goal check → choose →
  `goalTurns++` → move/attack (`:673-685`). `aiCalcSimpleGoal` picks attack(3) when
  a weapon is valid for the range/orientation else chase(2); fear(4) under buff 11
  unless fear-immune (`:535-566`). Goal validity caps at 16 turns (`:605-642`).
  `aiGoal_MOVE`: A*-ish `calcPath` (depth 8) toward the player tile; commit step =
  single-tile trace radius 25 with doors-only interact mask; success → relink +
  `aiInitLerp(275ms)` (500ms slow subspecies) + `interpolatingMonsters = true`;
  blocked-by-door → open it (`:1040-1116`).
* **Adjacency attack**: `aiReachedGoal_MOVE` re-runs `aiWeaponForTarget`;
  melee requires axis-aligned same row/column and trace(mask 5295) hitting the
  player, plus weapon RANGEMAX ≥ Chebyshev tile distance
  (`src/Entity.cpp:1118-1152`, `:644-671`, `:656`); `attack()` pushes the monster
  onto the `combatMonsters` singly-linked list via MFLAG_ATTACKING 0x400
  (`src/Entity.cpp:1160-1167`).
* **Scheduling structures** (all on Game): `activeMonsters` / `inactiveMonsters`
  circular doubly-linked lists through `EntityMonster::{next,prev}OnList`
  (`src/EntityMonster.h:20-21`); `combatMonsters` + `nextAttacker` pending-attack
  chain (`src/Game.cpp:884`, `src/Entity.cpp:1164`); per-frame drivers
  `monsterAI` (`src/Game.cpp:874-913`), `monsterLerp` (keeps
  `interpolatingMonsters` true while any member has goalFlags&1, `:915-939`),
  `updateMonsters`/`endMonstersTurn` (`:2458-2474`, `:2452-2456`),
  `prepareMonsters` respawn-on-revisit (`:701-727`), `canSnapMonsters`
  (info 0x10000000 cull bit, `:2374-2389`).
* map00 fight hooks: PER_TURN corpsifies imps 41/42; imp 15 is cinematic-only
  (DAMAGEMONSTER @2831); live targets near the elevator area are sprite 46 (imp,
  tileNum 23) and later the @8465+ AIGOAL event + post-@8372 VIOS scripts
  (`docs/research/2026-08-25-unhandled-script-events.md` §§1,3;
  `docs/original-code/loot-inventory.md` §6).

## 4. HUD / UX during combat

* **Monster health bar**: fed by `player->facingEntity`, recomputed whenever
  `canvas->updateFacingEntity` is set (after every advanceTurn / monster settle /
  death / weapon switch — `src/Game.cpp:1269,:2427,:2441,:2464`,
  `src/Entity.cpp:527`). The facing probe is a short ray from dest position along
  the view matrix, mask 21741, radius 2, with a monster-preference rescan
  (`src/MovementController.cpp:32-87`). Bar draw `Hud::drawMonsterHealth`
  (`src/Hud.cpp:822-899`): 25 segments of `ceil(screenW·2/128)` px (evened), black
  bg + gray border at `y = viewRect[1]+6` (VIOS +44; zoom +20), color red ≤¼ /
  green ≥¾ full / orange between, animated drain over 250 ms
  (`monsterStartHealth`→current). Called from `drawTopBar` unless dying
  (`:246-248`). Rewrite twin exists and is wired into its own drawTopBar but only
  draws when demo state is fed (`new_src/ui/Hud.cpp:220-249,:295`,
  `new_src/ui/Hud.h:102-107,:176`).
* **No floating damage numbers.** Damage feedback = message log (str 70 crit
  prefix + 71 "took N", 112 attacker-hit, 59 no-damage, 64 too-far, 68 miss,
  69 blocked, 72 armor-ate-it-all, 73 dodged, 62/63 low ammo) + screen effects.
* **Player hurt UX**: direction arrow + vignette driven by `hud->damageDir/
  damageTime/damageCount` (`src/Hud.cpp:493-533`; set in `explodeOnPlayer`
  `src/Combat.cpp:958-966` and `painEvent` `src/Player.cpp:624-650`); brightred
  weapon flash while damaged (`docs/original-code/combat.md` §1).
* Health/ammo refresh: `hud->repaintFlags |= 0x4` on ammo spend
  (`src/Combat.cpp:353`) and pickups (`src/Player.cpp:1035-1052`); top-bar name of
  the faced entity (`src/Hud.cpp:284-309`).
* Sounds (IDs only): per-weapon fire table §2.4; monster sounds via
  `getMonsterSound` into table 13 `monsterSounds[(sub*8)+type+rand]`
  (`src/Game.cpp:3776-3841`, types `src/Enums.h:85-95`: ALERT1..DEATH); pain/death
  per class (1094/1093 player, `src/Player.cpp:635-641`); gib 1037, pickup 1054,
  loot 1055, revenant swing 1100, explosion family 1032/1034.
* Turn banner: none — the only "pass turn" affordance is msg 45 on ACTION_PASSTURN
  (`src/PlayingInputHandler.cpp:551`).

## 5. Rewrite delta audit (EXISTS / MISSING)

| Item | Status | Evidence |
|---|---|---|
| Tables: weaponData(=weapons), weaponInfo, monsterStats, combatMasks, monsterWeakness, monsterSounds, monsterAttacks | **EXISTS** (all parsed) | `new_src/io/Tables.h:25-68`, `new_src/io/Tables.cpp:27-44` |
| Player weapons bitmask / weapon index / ammo / give(kind) | EXISTS | `new_src/domain/game/Player.h:21-33`, give `new_src/domain/game/Player.cpp` (give kind 1 auto-equip) |
| CombatEntity stats struct + setStat/calcXP | EXISTS | `new_src/domain/game/CombatEntity.h:12-27` |
| **calcHit / calcDamage / calcCombat** | **MISSING** | not present anywhere in `new_src/domain/game/CombatEntity.cpp` (file ends :56) |
| Weapon-table consumers (fire/ammo checks, SHOTHOLD anim) | MISSING (only loot starter-ammo reads weaponData) | `new_src/domain/game/Game.cpp:862-864` region |
| View-weapon draw (idle quad, getWeaponTileNum, wpinfo offsets) | MISSING (spec ready) | `docs/original-code/combat.md` §1, §4; hook point `new_src/core/GameContext.cpp:994-1011` |
| Hud::drawMonsterHealth | EXISTS, dormant (demo-fed only) | `new_src/ui/Hud.cpp:220-249`, call `:295`, state `Hud.h:102-107` |
| Facing-entity probe (facingEntity) | MISSING (field absent from rewrite Player) | `new_src/domain/game/Player.h` has no facingEntity; legacy `src/MovementController.cpp:32-158` |
| Monster entities on load | **PARTIAL**: ET_MONSTER sprites DO spawn entities with lootSet + kInfoActive, but **`Entity::monster == nullptr`** — no EntityMonster, no ce clone, no inactive ring | `new_src/domain/game/Game.cpp:133-174` (spawn), `new_src/domain/game/Entity.h:33` (raw pointer never set) |
| Trace vs monster circles | EXISTS mechanically (mask-driven, circle r=25, oriented-segment path, sorted fracs) — works as soon as masks include bit 2 AND entities are linked | `new_src/domain/game/Game.cpp:465-508`, `:545-570` |
| CONTENTS masks | EXISTS incl. 13501 walk mask; WEAPONSOLID 13997 not yet used anywhere | `new_src/domain/game/Enums.h` (CONTENTS_PLAYERSOLID), use sites `new_src/core/GameContext.cpp:798-813` |
| ST_COMBAT state + runFrame sequencer | **MISSING** (StateId documents Combat out of scope; value 5 absent) | `new_src/core/GameContext.h:30-42` |
| advanceTurn subset | EXISTS (arms monstersTurn=1, doors, PER_TURN func 6) but no player-status ticks and no Error-95 guard (nothing sets interpolatingMonsters yet) | `new_src/domain/game/Game.cpp:595-601` |
| monstersTurn consumer | MISSING (tick just clears it: `GameContext.cpp:259,:315`) | grep cited |
| findLootableCorpseFacing + loot dwell | EXISTS | `new_src/domain/game/Game.cpp:682+`, `new_src/core/GameContext.cpp:831-846` |
| corpsifyMonster (def swap + 0x7000 + relink) | EXISTS (subset; no deactivate ring/sound) | `new_src/domain/game/Game.cpp:614+`, header contract `new_src/domain/game/Game.h:180-186` |
| ScriptVM op 19 DAMAGEMONSTER | EXISTS (lethal-corpsify approximation, logged) | `new_src/domain/game/ScriptVM.cpp:856-874` |
| ScriptVM op 28 WAKEMONSTER | STUB (log only) | `new_src/domain/game/ScriptVM.cpp:850-854` |
| ScriptVM ops 60 DISABLED_WEAPONS | consume-only stub | `new_src/domain/game/ScriptVM.cpp:876-886` |
| ScriptVM op 10 WEAPON_EQUIPPED / 51 AIGOAL / 56 PLAYERATTACK | **MISSING → default kills the thread** (`t->state=0`) | switch has no cases (grep); killer default `new_src/domain/game/ScriptVM.cpp:1252-1258`; legacy semantics `src/ScriptThread.cpp:477-482`, `:1262-1274`, `:1361-1373` |
| Audio | **MISSING entirely** (no playSound/audio backend in new_src) | grep returned zero matches |

## 6. Staged plan recommendation

### Stage 1 slice — "walk in, shoot the imp, loot it" (map00 right after EVT 617)

1. **View weapon (static)**: port `getWeaponTileNum` + signed wpinfo idle offsets;
   blit media tile frame 0 as a 176×176 quad at (196+idleX+shakeX, 131−(idleY+shakeY))
   after the world pass (`docs/research/2026-08-26-hero-choice-and-weapon.md` §B.3
   items 1-3). No fire animation needed for the first cut — but reserve the
   `animStartTime/animEndTime/flashDone` fields.
2. **Fire input**: extend `handlePlayingAction(Action::Use)`: before the loot/tile-event
   branches, run the legacy order actually needed — loot check stays first
   (`src/PlayingInputHandler.cpp:374-378`), then tile event, then door, then
   **fire**: target = reuse `traceMove` with mask 13997 radius 2 from view toward
   viewStep (a real swept ray replaces the 6-unit legacy probe; document the
   deviation), fall back to world entity at traceCollision.
3. **Combat core**: port `CombatEntity::calcHit/calcDamage` verbatim (they are pure
   given `tables.*` + ce stats), `Player::fireWeapon` guards, and a minimal
   `playerSeq` two-stage timer (stage 0 = roll + ammo + pain application for
   hitscan; stage 1 = died check + messages) without projectiles (rifle/shotgun/
   chaingun are proj 0; defer proj ≠ 0 weapons with an "unsupported" guard like the
   legacy soul-cube guard).
4. **Monster structs**: allocate `EntityMonster` in `loadEntities` for eType 2
   (clone `monsterStats[subType*3+parm]` ×5 hp rule + difficulty bump), keep them
   permanently on an implicit "active for map00" basis OR implement the tiny
   activate/deactivate pair — recommended: port `activate`/`deactivate` +
   the two lists now (≈80 lines, unlocks Stage 2 cleanly).
5. **pain/died**: port `Entity::pain` (non-boss branch) and the ET_MONSTER branch of
   `died` (frame 0x7000 write exists via corpsifyMonster; add XP call, def swap
   `find(9,…)`, lootable markers — mostly already modeled by the loot doc port).
6. **HUD feed**: replace the demo feed of drawMonsterHealth with
   facingEntity-from-trace (reuse the fire trace result each tick), wire
   `repaintFlags` analog = redraw messages/top bar; message-log strings via
   existing Localization (str 70/71/112/68/59/64).
7. **ScriptVM unblockers**: op 10 (`vars[b&0x7F] = player.weapon`), op 56
   (`player.ce.weapon = arg>>12&0xF; performAttack(target,…,true); evWait`),
   op 51 (store goalType/goalParam on the EntityMonster; full AI may wait),
   op 84 target practice (teleport + strip/restore + score msgs — self-contained,
   `src/Player.cpp:2521-2630`).

Justified deferrals (with reasons): projectile flight/grenades/BFG (no map00 Stage-1
consumer before VIOS), monster movement/pathfinding depth (Stage 2 core), armor
absorption (player armor always 0 early), zoom/sniper bbox path, secondary attacks &
boss phases (no boss on map00 route), audio backend (separate workstream), knockback,
fear/flee goals, sentry-bot/familiar paths.

### Stage 2 outline — monster turn flow

1. monstersTurn window in `tickPlaying` (gate on knockback/effects analogs);
   `monsterAI` iteration + goal choose/move; `aiGoal_MOVE` greedy variant acceptable
   for map00 rooms (open layouts, few blockers) with the real `calcPath` behind a
   flag.
2. Attack queue (`combatMonsters`) + `monsterSeq` vs player + `explodeOnPlayer`
   damage-dir plumbing; end-of-turn `endMonstersTurn`.
3. Wake-on-sight during render tick (needs a view→sprite visibility test; the
   rewrite's BSP walk can answer with a line frac).
4. interpolatingMonsters + snap discipline (Error-95 invariant) once lerped steps
   exist.

## Open questions

1. Legacy fire-probe length is ~6 units — effectively "the wall/item in front of your
   face". The rewrite's replacement (full-tile swept ray, mask 13997, radius 2) should
   be behavior-equivalent for target election but changes which of two stacked
   candidates wins at exact boundaries; verify against the imp at point-blank.
2. `entities[0]` passed to `fireWeapon` for wall shots carries whatever def lookup
   gives tileIndex 0 — confirm its eType is 0 (world) so `calcHit` returns the
   miss path (legacy relies on `info & 0x20000` being clear).
3. Rewrite `Hud::drawTopBar` passes `viewTop=42` while legacy computes
   `viewRect[1]+6 = 26` — cosmetic offset to reconcile when wiring the real feed.
