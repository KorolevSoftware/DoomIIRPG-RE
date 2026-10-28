# Monster turn pipeline (original `src/`)

Scope: how the player's action hands the turn to monsters, how each monster
decides attack / move / idle, how several attacks are sequenced, and how the
state machine waits. Attack *execution* (`Combat::monsterSeq`, projectiles,
damage), player damage (`Player::pain`) and pathing (`aiGoal_MOVE` /
`calcPath`) are covered elsewhere — only cross-referenced here.

Already documented (not repeated): `Game::advanceTurn` step list
(`docs/original-code/game-flow.md` §5), turn-consuming actions table
(`docs/research/2026-08-26-combat-stage1.md` §1.1), hit/damage formulas
(`docs/original-code/combat.md` §5).

Raw log: `docs/research/2026-09-22-monster-turn-pipeline.md`.

--------------------------------------------------------------------------------
## 1. Data structures

### 1.1 Lists

| List | Shape | Link fields | Built by | Cite |
|---|---|---|---|---|
| `Game::activeMonsters` | circular doubly-linked ring, head pointer | `monster->nextOnList/prevOnList` | `Game::activate` inserts at the tail (before head) | `src/Game.cpp:756-808` (insert `:787-797`) |
| `Game::inactiveMonsters` | same ring type | same fields | `Game::deactivate` (called at spawn and on death) | `src/Game.cpp:825-855`; death `src/Entity.cpp:485` |
| `Game::combatMonsters` | singly-linked LIFO stack | `monster->nextAttacker` | `Entity::attack` pushes on front | `src/Entity.cpp:1160-1167` |

Consequence: attack order is the **reverse** of think order — the last
monster in ring order that queued an attack fires first (`src/Entity.cpp:1163-1164`).
`combatMonsters` is reset to `nullptr` at the start of every `monsterAI` pass
(`src/Game.cpp:884`).

### 1.2 Per-monster state (`EntityMonster`, `src/EntityMonster.h:17-35`)

`target` (always `nullptr` = player in practice, see §3.4), `frameTime`,
`flags` (MFLAG_*), `monsterEffects`, `goalType`, `goalFlags`, `goalTurns`,
`goalX/goalY` (tile coords), `goalParam`. `resetGoal()` zeroes all six goal
fields (`src/EntityMonster.cpp:39-46`). Save packing: `goalType|goalFlags<<4|
goalTurns<<8|goalX<<12|goalY<<17|goalParam<<22`, each goalType/Flags/Turns
field only 4 bits on load (`src/EntityMonster.cpp:25-37`).

### 1.3 Flag bits used by the turn pipeline

| Bit | Field | Meaning (from use) | Cite |
|---|---|---|---|
| `0x1` MFLAG_ABILITY | `monster->flags` | evading monsters: "has already attacked from this spot" toggle | `src/Entity.cpp:555-563`, `:1123`, `:1138-1140` |
| `0x20` MFLAG_NOTHINK | `monster->flags` | `aiThink` does nothing | `src/Entity.cpp:678` |
| `0x400` MFLAG_ATTACKING | `monster->flags` | "already on combatMonsters" guard | `src/Entity.cpp:1162`, `:1171`; cleared at every `aiThink` `src/Entity.cpp:675-677` |
| `0x1000` MFLAG_KNOCKBACK | `monster->flags` | lerp end is a knockback, not a goal step | `src/Entity.cpp:875-878` |
| `goalFlags 0x1` | `monster->goalFlags` | monster sprite is lerping one tile (set `aiInitLerp`, cleared `aiFinishLerp`) | `src/Entity.cpp:869`, `:874` |
| `goalFlags 0x8` | `monster->goalFlags` | evading monster: retreat after its attack | set `src/Entity.cpp:600-602`, used `src/GameStateRunner.cpp:53-58`, cleared by script `src/ScriptThread.cpp:2242` |
| `goalFlags 0x10` | `monster->goalFlags` | re-think after reaching goal; only read/cleared (`src/Entity.cpp:1141-1150`), no setter found in `src/` | — |
| `info 0x40000` | `entity->info` | on the active ring | `src/Game.cpp:798`, `:854` |
| `info 0x10000000` | `entity->info` | this step's lerp is ON SCREEN (bbox not culled) → cannot be snapped | set `src/Entity.cpp:1095-1097`; cleared `:1086`, `:877`, `:1122` |
| `monsterEffects 0x2` | `monster->monsterEffects` | "frozen" (blue-shift render) → skipped by `monsterAI` | `src/Game.cpp:890`, render `src/Render.cpp:1587-1589` |

--------------------------------------------------------------------------------
## 2. Turn hand-off and the monster phase

### 2.1 Arming (`Game::advanceTurn`, `src/Game.cpp:1238-1281`)

`monstersTurn = 1` on a normal turn, `2` on the odd turns of player haste
(`statusEffects[2] > 0`, parity counter `statusEffects[20]`, `:1245-1262`);
`monstersUpdated = false` (`:1263`). Monster FX ticks (`updateMonsterFX`) only on
normal turns (`:1265-1270`). Calling it while `interpolatingMonsters` is fatal
Error 95 (`:1241-1243`). Full step list: `game-flow.md` §5.

### 2.2 The per-frame driver (`ST_PLAYING`)

`GameStateRunner::playingState` calls `Game::updateMonsters()` every frame iff
`knockbackDist == 0 && activePropogators == 0 && animatingEffects == 0 &&
monstersTurn != 0 && numHelpMessages == 0` (`src/GameStateRunner.cpp:181-183`),
then `updateLerpSprites()` (`:184`) which advances monster step lerps.

`Game::updateMonsters` (`src/Game.cpp:2458-2474`):

```
monsterAI();                 // runs ONCE per turn (monstersUpdated latch)
monsterLerp();               // recompute interpolatingMonsters
if (!interpolatingMonsters) {
    updateFacingEntity = true;
    if (combatMonsters) combat->performAttack(combatMonsters, combatMonsters->monster->target, 0,0,false); // -> ST_COMBAT
    else { endMonstersTurn(); drawPlayingSoftKeys() unless camera/blockInput }
}
```

So within one monster turn: **all thinking happens in the first frame**, all
step lerps (275 ms, 500 ms for boss-pinky / mastermind parm≠0,
`src/Entity.cpp:1099-1104`) run **concurrently**, and attacks start only
after every walking monster has arrived.

### 2.3 `Game::monsterAI` (`src/Game.cpp:874-913`)

1. Return if `disableAI` (never set true in `src/`, only `= false` at
   `src/Game.cpp:61`) or if `monstersUpdated` already set (`:877-882`).
2. `monstersUpdated = true; combatMonsters = nullptr` (`:883-884`).
3. Walk the active ring once from the head, caching `next` before each call
   (`:886-897`). A monster thinks iff `(monsterEffects & 0x2) == 0` (not
   frozen) and (`monstersTurn == 1` or (`monstersTurn == 2` and
   `isHasteResistant()`)) (`:890`). `isHasteResistant() == isBoss()`
   (`src/Entity.cpp:1402-1404`); `isBoss` = subtype 12..16 except
   mastermind parm 0 (`src/Entity.cpp:1398-1400`). Call `aiThink(false)`; if
   the monster started a step lerp (`goalFlags & 1`) set `i = 1`.
4. **Re-validation loop** (`:898-911`): while the previous pass started any
   lerp, walk `combatMonsters`; any attacker whose `aiIsAttackValid()` now
   fails (e.g. a monster that just moved into its line of fire — traces treat
   a lerping monster as already standing on its goal tile,
   `src/Game.cpp:227-231`) gets `undoAttack()` + `aiThink(false)` again; if
   that re-think moved it, loop again.

### 2.4 `Game::monsterLerp` (`src/Game.cpp:915-939`)

No-op unless `interpolatingMonsters` is already true (`:920-922`; it is set
by `aiGoal_MOVE` when a step starts, `src/Entity.cpp:1098`). Otherwise
recomputes it = "any active monster has `goalFlags & 1`" and invalidates the
screen if one of those goal tiles is visible (`:923-938`). The step lerp
itself ends in `Game::updateLerpSprite` → `entity->aiFinishLerp()`
(`src/Game.cpp:3213-3216`) → `aiReachedGoal_MOVE` (`src/Entity.cpp:873-882`),
which can **queue an attack after the move** (§3.6).

### 2.5 `endMonstersTurn` (`src/Game.cpp:2452-2456`)

`canvas->startRotation(true)` (pitch refresh) + `monstersTurn = 0`. Called
from `updateMonsters` (no attackers left), from `combatState` after the last
attacker (§4), and by script `EV_ADVANCETURN` (`src/ScriptThread.cpp:1276-1288`).

### 2.6 When the player can act again

* Player input in `ST_PLAYING` passes the gate
  `activePropogators == 0 && snapMonsters(automap) && animatingEffects == 0`
  (except weapon prev/next and turn L/R, which always pass)
  (`src/PlayingInputHandler.cpp:71-74`).
* `canSnapMonsters()` (`src/Game.cpp:2374-2389`) = true iff no active monster
  has `info & 0x10000000` (no on-screen step lerp). Off-screen walkers can be
  snapped; on-screen ones must finish animating.
* `snapMonsters(b)` (`src/Game.cpp:2391-2450`):
  * returns false if an animating-effect lerp belongs to a non-monster
    (`:2394-2408`);
  * `monstersTurn == 0`: snap all active monsters' lerps if allowed, clear
    `interpolatingMonsters`, return allowed (`:2410-2429`);
  * `monstersTurn != 0`: `monsterAI()` (no-op if already run), snap lerps if
    allowed — snapping fires `aiFinishLerp` and may queue move-then-attack —
    `monsterLerp()`, and if there are queued attackers and nothing
    interpolates → `performAttack(...)` (enters `ST_COMBAT`) and **return
    false** (input swallowed) (`:2430-2449`).
* So the player regains control either when `updateMonsters` calls
  `endMonstersTurn` (turn fully over) or earlier, when all remaining walkers
  are off-screen and no attacks are pending (input accepted while
  `monstersTurn` is still non-zero; `updateMonsters` then closes the turn in
  the same frame since input is handled before `playingState`,
  `src/Canvas.cpp:807-817`).
* Edge: a move that *arrives* while `monstersTurn != 0` does not call
  `advanceTurn` (`src/MovementController.cpp:178-183`).
* Turn-consumption of a player attack is deferred to `combatState` (§4).

--------------------------------------------------------------------------------
## 3. Per-monster decision

### 3.1 `Entity::aiThink(bool b)` (`src/Entity.cpp:673-685`)

```
flags &= ~0x400                       // MFLAG_ATTACKING cleared (list was reset)
if (!(flags & 0x20 /*NOTHINK*/)) {
    if (!aiIsValidGoal()) aiChooseNewGoal(b);
    goalTurns++;
    aiMoveToGoal();
}
```
`b` is forwarded to `aiCalcSimpleGoal` but unused there (`src/Entity.cpp:535-566`).

### 3.2 Goal types (numeric; no GOAL_* enum exists in `src/Enums.h`)

Names inferred from use:

| goalType | Meaning | Validity (`aiIsValidGoal`, `src/Entity.cpp:605-642`) | Action (`aiMoveToGoal`, `src/Entity.cpp:572-594`) |
|---|---|---|---|
| 0 | none | invalid → re-choose | nothing |
| 1 | move to tile `goalX,goalY` | valid while `linkIndex != goalX + goalY*32` | `aiGoal_MOVE` |
| 2 | chase entity `goalParam` (1 = player) | player (`goalParam==1`) → **always invalid** (re-chosen every turn); other entity valid while not on same tile and its tile has no blocker (mask 15535) | `aiGoal_MOVE` (player: goal = `game->destX/Y>>6`, `lineOfSightWeight = -4`, `src/Entity.cpp:1051-1056`) |
| 3 | attack (`goalParam` 1 = player) | **always invalid** (re-chosen every turn) | `target = (goalParam==1) ? nullptr : &entities[goalParam]`; `attack()` |
| 4 | flee (fear) for `goalParam` turns | valid while `goalTurns < goalParam` | `aiGoal_MOVE` with goal = player tile, `lineOfSight=1`, `b=true` (`:1062-1067`) |
| 5 | retreat step after attack | valid while `goalTurns < goalParam` | `aiGoal_MOVE`, `interactClipMask=0`, `pathSearchDepth=goalParam` (`:1057-1063`) |
| 6 | wait `goalParam` turns | valid while `goalTurns < goalParam`; at equality `resetGoal()` + invalid | nothing |

Global expiry: `goalTurns >= 16` → invalid (`:609`). Scripted goals: op
`EV_AIGOAL` (51) → `ScriptThread::setAIGoal` sets type/param (2,3 → param 1;
4,6 → script byte), activates the monster if inactive and calls
`aiThink(true)` immediately; for type 3 it clears `goalFlags 0x8` and starts
`performAttack` on `combatMonsters` right away (`src/ScriptThread.cpp:1262-1274`,
`:2226-2246`). Pain resets the goal (`resetGoal()` unless attacker weapon
id 2) (`src/Entity.cpp:354-356`).

### 3.3 `aiChooseNewGoal` / `aiCalcSimpleGoal` (`src/Entity.cpp:596-603`, `:535-566`)

```
resetGoal()
if subtype == ARCH_VILE(10) && aiCalcArchVileGoal(): return   // goal stays 0 (raise used the turn)
if player->buffs[11] (FEAR) > 0 && !(1<<subtype & FEAR_IMMUNE_MONSTERS 0x1F801):
     goal = 4 (flee), param = 1; return
w = aiWeaponForTarget(&entities[1] /*player*/)
if w != -1: ce.weapon = w; goal = 3 (attack), param = 1; if evading: flags |= 0x1
else:       goal = 2 (chase player), param = 1;         if evading: flags &= ~0x1
// aiChooseNewGoal: if evading && goal == 3: goalFlags |= 0x8
```

There is **no randomness in attack-vs-move** and no idle state for a woken
monster: every turn it attacks if `aiWeaponForTarget` finds an in-range weapon
with a clear line, otherwise it steps toward the player. Idle = not on the
active ring (never woken) or NOTHINK / frozen / goal 6 script wait.

Subtype masks (`src/Enums.h:60-84`, bit = `1<<eSubType`):

* `EVADING_MONSTERS = 0x4604` → lost soul (2), revenant (9), arch-vile (10), mastermind (14).
* `FEAR_IMMUNE_MONSTERS = 0x1F801` → zombie (0), sentry bot (11), all bosses 12-16.
* `MOVE2ATTACK_MONSTERS = 0x180D0` → saw goblin (4), cacodemon (6), sentinel (7), Vios (15), Vios2 (16).

### 3.4 `aiWeaponForTarget(target)` (`src/Entity.cpp:687-850`)

`(a, a2) = target position − monster sprite position` in world units; for the
player the target position is `canvas->viewX/viewY` (`:692-702`). Weapons come
from `getMonsterField(def, k) = monsterAttacks[subtype*9 + parm*3 + k]`
(`src/Combat.cpp:1058-1060`; `monsterAttacks` = tables.bin table 0,
`src/App.cpp:389`): k=0 primary weapon, k=1 secondary weapon, k=2 primary
chance. Weapon id 0 means "no weapon". Range check uses the weapon row
`weapons[w*9 + 2]` = RANGEMIN, `[w*9 + 3]` = RANGEMAX in tile units, against
`WorldDistToTileDist(distFrom)` = Chebyshev tiles (`src/Combat.cpp:1235-1242`,
`tileDistances[j] = (64(j+1))²` `src/Combat.cpp:41-44`).

* **Boss pinky (13)**: axis-aligned only; trace mask 4131
  (WORLD|PLAYER|DOOR|SPRITEWALL) must hit the target; returns field 0 with
  **no range check** (`:704-714`).
* **Vios (15)** teleport pre-step, every call: if `param <= 0` set
  `param = rand%3+3` and try up to 30 random offsets in ±4 tiles; first spot
  with an empty trace (mask 15535) → teleport with particles + sound 1123;
  else `--param` (`:716-745`).
* **Off-axis** (`a != 0 && a2 != 0`): only arch-vile (10) and Vios with
  parm≠0 may attack — field 0 (arch-vile) / field 1 (Vios), range-checked,
  trace mask 4131 must hit the target (`:752-766`). Everyone else → −1.
* **Axis-aligned (general)** (`:767-848`):
  1. Trace monster→view with mask 5295 (WORLD|PLAYER|MONSTER|NPC|DOOR|DECOR|
     ATTACK_INTERACTIVE|SPRITEWALL) must hit the target, else −1 (`:769-772`).
  2. Second trace starting 18 units toward the target with mask 15791 (adds
     ENV_DAMAGE, MONSTERBLOCK_ITEM, NONOBSTRUCTING_SPRITEWALL); if it does not
     hit the target, any melee weapon in `0x78002` (ids 1, 15, 16, 17, 18) is
     disabled (`:781-792`).
  3. `field1 == field0 → field1 = 0`; each non-zero field is kept only if
     `RANGEMIN <= tileDist <= RANGEMAX` (`:795-811`).
  4. Weapon 18 (charge/lunge) additionally requires no `baseVisitedTiles`
     bit (world-solid or monster-clip, `src/Game.cpp:128,179,3656-3666`) on
     the `tileDist` tiles starting at the monster's own tile (`:812-830`).
  5. Both usable → `nextByte() (0..255) <= field2 ? field0 : field1`
     (`:835-836`; `nextByte = rand() & 0xFF`, `src/App.cpp:510-512`); one
     usable → that one; none → −1.

`monster->target` is only ever assigned `nullptr` (player) or
`&entities[goalParam]` with `goalParam==1` → in `src/` all monster attacks
target the player (`src/Entity.cpp:580-586`, `:1127-1132`, setters of
`goalParam` for goal 3 always write 1: `:553`, `src/ScriptThread.cpp:2229-2231`).

`monsterAttacks` values (tables.bin table 0, 150 shorts; `(primary, secondary, chance)` per parm 0/1/2; subtype 16 has only parm 0/1):

| sub | parm0 | parm1 | parm2 |
|---|---|---|---|
| 0 zombie | 19,0,0 | 19,0,0 | 19,0,0 |
| 1 (unnamed) | 0,0,0 | 0,0,0 | 0,0,0 |
| 2 lost soul | 15,18,75 | 15,18,75 | 15,18,75 |
| 3 imp | 16,0,0 | 16,20,75 | 16,20,50 |
| 4 saw goblin | 17,0,0 | 17,0,0 | 17,0,0 |
| 5 pinky | 15,18,75 | 15,18,152 | 15,18,178 |
| 6 cacodemon | 15,21,0 | 15,21,0 | 15,21,0 |
| 7 sentinel | 22,0,0 | 22,0,0 | 22,0,0 |
| 8 mancubus | 26,0,0 | 26,0,0 | 26,0,0 |
| 9 revenant | 26,17,178 | 26,17,203 | 26,17,229 |
| 10 arch-vile | 23,0,0 | 23,0,0 | 23,0,0 |
| 11 sentry bot | 24,0,0 | 24,0,0 | 24,0,0 |
| 12 cyberdemon | 26,0,0 | 0,0,0 | 0,0,0 |
| 13 boss pinky | 27,0,0 | 0,0,0 | 0,0,0 |
| 14 mastermind | 28,0,0 | 25,0,0 | 0,0,0 |
| 15 Vios | 29,0,0 | 29,30,152 | 29,30,152 |
| 16 Vios2 | 29,30,152 | 29,30,152 | — |

Monster weapon rows (tables.bin table 2 `weapons`, stride 9: STRMIN, STRMAX,
RANGEMIN, RANGEMAX, ammoType, usage, projType, numShots, shotHold; loader
`src/App.cpp:391`), ids 15-30:

| id | str | range [min,max] tiles | proj | shots | hold |
|---|---|---|---|---|---|
| 15 | 6-8 | 1-1 | 1 | 1 | 20 |
| 16 | 5-6 | 1-1 | 1 | 2 | 10 |
| 17 | 7-9 | 1-1 | 1 | 1 | 20 |
| 18 | 8-12 | 1-2 | 1 | 1 | 20 |
| 19 | 8-10 | 1-3 | 6 | 1 | 30 |
| 20 | 10-12 | 1-3 | 7 | 1 | 10 |
| 21 | 10-12 | 2-3 | 8 | 1 | 10 |
| 22 | 8-10 | 1-3 | 9 | 1 | 35 |
| 23 | 15-18 | 0-5 | −1 | 1 | 30 |
| 24 | 3-4 | 1-4 | 0 | 2 | 30 |
| 25 | 6-9 | 1-4 | 0 | 4 | 30 |
| 26 | 20-25 | 1-5 | 4 | 1 | 30 |
| 27 | 15-20 | 1-6 | 10 | 1 | 30 |
| 28 | 8-10 | 1-4 | 5 | 2 | 10 |
| 29 | 15-20 | 1-5 | 8 | 1 | 30 |
| 30 | 10-12 | 1-6 | 11 | 1 | 30 |

(Values dumped from `UnPackGameData/tables.bin`; header = 20 LE int end
offsets after the 80-byte header, each table = int count + payload,
`src/Resource.cpp:217-219`, `:236-248`, `:278-311`.)

### 3.5 `aiIsAttackValid()` — the re-check (`src/Entity.cpp:644-671`)

Used by the `monsterAI` re-validation loop and before each *next* attacker in
`combatState`. True iff `ce.weapon > -1` and a trace from the monster to
`game->destX/destY` (player's destination tile, not the lerping view) with
mask 5295 hits something that is the player (target==nullptr) or the target,
**and** `tileDist <= RANGEMAX` (RANGEMIN not re-checked) **and** the offset
is exactly axis-aligned and non-zero. Note: off-axis arch-vile/Vios attacks
queued in `monsterAI` fail this check if re-validated.

### 3.6 Move-then-attack (`aiReachedGoal_MOVE`, `src/Entity.cpp:1118-1153`)

When a step lerp ends (or is snapped): unless goal 4/5, MOVE2ATTACK monsters
and evading monsters with `flags & 1 == 0` re-run `aiWeaponForTarget(player)`
and, if a weapon is usable, set `ce.weapon` and `attack()` — i.e. they
**step and attack in the same turn**. Evading monsters then set `flags |= 1`.
All other monsters only move this turn and attack next turn.

--------------------------------------------------------------------------------
## 4. Sequencing multiple attacks (`ST_COMBAT`)

* One attack at a time. `Combat::performAttack(attacker, target, 0, 0, false)`
  stores `curAttacker`, sets up the weapon and **`setState(ST_COMBAT)`**
  (`src/Combat.cpp:54-165`); `setState(ST_COMBAT)` clears `combatDone`
  (`src/Canvas.cpp:1054-1060`).
* `GameStateRunner::combatState` per frame (`src/GameStateRunner.cpp:21-88`):
  `monsterLerp()`, `updateLerpSprites()`, then `combat->runFrame()`
  (→ `monsterSeq()` when `curAttacker != nullptr`, `src/Combat.cpp:857-862`).
  When the sequence returns 0 (finished):
  1. Player dying (`ST_DYING`/`ST_BOT_DYING`) → `undoAttack()` every queued
     attacker, return (`:41-45`).
  2. (player attack case `curAttacker == nullptr`: `touchTile` +
     `combatDone = true` `:47-50`.)
  3. Monster attacker, only when `knockbackDist == 0` (`:51`):
     * evading attacker (`goalFlags & 0x8`) → `resetGoal(); goalType = 5;
       goalParam = 1; aiThink(false)` — the one-tile retreat lerp starts now
       (`:53-58`);
     * walk `nextAttacker` from the current one, `undoAttack()`-ing those
       with `target == nullptr && !aiIsAttackValid()` (`:61-64`);
     * next valid attacker → `performAttack(next, ...)` (stays in ST_COMBAT)
       (`:65-66`);
     * none left → `combatMonsters = nullptr`; if a monster still
       interpolates → `setState(ST_PLAYING)` (then `updateMonsters` ends the
       turn once lerps settle — `monsterAI` will not re-run because
       `monstersUpdated` is set); else `endMonstersTurn()`,
       `drawPlayingSoftKeys()`, `combatDone = true` (`:67-78`).
* Next frame with `combatDone && !interpolatingMonsters`: `advanceTurn()`
  only if `curAttacker == nullptr` (the player's own attack), then
  `ST_PLAYING` or `ST_CAMERA` (`:26-39`). Key input in ST_COMBAT does the same
  hand-off and lets L/R rotate the view by 256 while a monster attacks
  (`src/InputEventController.cpp:364-386`).
* Full cycle of a player attack: fire → ST_COMBAT (player seq) →
  `advanceTurn` → ST_PLAYING → `updateMonsters` → per attacker ST_COMBAT
  (monster seq) → … → `endMonstersTurn` → ST_PLAYING, player input open.

Attack animation / projectile / damage internals: see the parallel reports
on `Combat::monsterSeq` and `Player::pain`.

--------------------------------------------------------------------------------
## 5. Special cases (flagged, not deep-dived)

* **Arch-vile (10)**: before normal goal logic, `findRaiseTarget(0x19000,0,0)`
  (Chebyshev² ≤ 102400 → within 5 tiles) over `inactiveMonsters` corpses
  (not boss, not gibbed, not already being raised; up to 4 candidates, random
  pick); success uses the turn and sets `param = 4` cooldown turns
  (`src/Entity.cpp:538-540`, `:568-570`, `:1280-1318`). Only monster that may
  attack off-axis (weapon 23, range 0-5, proj −1).
* **Bosses 12-16** (mastermind only with parm≠0): haste-resistant (act on
  both haste half-turns) and fear-immune; death runs static func 5 and clears
  `inCombat` (`src/Entity.cpp:487-490`; deactivate + undoAttack `:485-486`). Boss pinky (13): special attack rule
  (§3.4) and writes its position to `scriptStateVars[9..10]` every think
  (`src/Entity.cpp:589-593`); 500 ms steps for 13 and mastermind parm≠0.
  Vios (15): random teleport every 3-5 turns (§3.4). HP-threshold scripts
  2/3/4 (75/50/25%) from `pain` (`src/Entity.cpp:295-333`).
* **Sentry bot (11)** as an enemy is an ordinary monster (weapon 24,
  fear-immune). The player's *familiar* sentry bot is the player itself
  (`player->isFamiliar`): monsters still target `entities[1]`; doors don't
  auto-close while a familiar is out (`src/Game.cpp:1218-1220`); a monster
  step ending adjacent (axis) to `canvas->saveX/saveY` forces the familiar
  to return (`src/Game.cpp:3217-3226`).
* **Fear** (player buff 11): non-immune monsters flee 1 turn (goal 4).
* **Frozen** (`monsterEffects & 0x2`): skipped by `monsterAI`; counter ticks in
  `updateMonsterFX` (bits `9..12`, `src/Entity.cpp:1944-1995`); a monster step ending on an ENV_DAMAGE
  (mask 256) entity of subtype 1 sets fire bit 0x8 (3 turns) and clears
  bit 0x2 + its counter, lost souls excepted (`src/Game.cpp:3229-3236`).
  **No setter of bit 0x2 found in `src/`** (grep of `monsterEffects`) —
  open question.
* **Poison/fire DOT** (`monsterEffects` bits 0 / 3): damage 10 / 4 per normal
  turn in `updateMonsterFX` (`src/Entity.cpp:1953-1985`); poison applied by
  player status 12 on hit (`src/Combat.cpp:168-176`).
* **NOTHINK** (`MFLAG 0x20`) monsters never think; scripts can still give
  them a goal via `EV_AIGOAL` (`aiThink` is still gated → no effect while
  NOTHINK).
* **Scripted `EV_ADVANCETURN`**: snap lerps, undo all queued attacks,
  `endMonstersTurn`, `advanceTurn` (`src/ScriptThread.cpp:1276-1288`).
* **Replay resurrections**: `prepareMonsters` on re-entering a completed,
  fully-cleared level resurrects up to 8 corpses (≤ half the total) with
  `nextByte() <= 100` each (`src/Game.cpp:701-727`) — load-time only, not
  per-turn.
