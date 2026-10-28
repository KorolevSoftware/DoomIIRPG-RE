# 2026-09-22 — Monster turn pipeline (turn hand-off, AI decision, attack sequencing)

Curated result: `docs/original-code/monster-turn.md`. Builds on
`docs/research/2026-08-26-combat-stage1.md` §1 and `docs/original-code/game-flow.md` §5.

## Questions / hypotheses

1. Player action → `advanceTurn` → `updateMonsters` → `monsterAI` / `monsterLerp`
   → `performAttack` / `endMonstersTurn`; exact order, lists, flags, when the
   player acts again.
2. `aiThink` decision attack vs move vs idle; goal types; range/LOS; randomness;
   per-type table fields.
3. Sequencing of multiple attacks in one monster turn.
4. Special cases (arch-vile, bosses, sentry bots, frozen/feared).

## Method

Read `src/Game.cpp:701-939`, `:1238-1281`, `:2374-2474`, `:2958-3240`;
`src/Entity.cpp:535-882`, `:1030-1192`, `:1280-1404`, `:1944-1995`;
`src/GameStateRunner.cpp:21-88`, `:181-184`; `src/PlayingInputHandler.cpp:20-74`;
`src/InputEventController.cpp:364-386`; `src/ScriptThread.cpp:1262-1288`,
`:2226-2246`; `src/Combat.cpp:54-176`, `:427-450`, `:857-862`, `:1058-1060`,
`:1235-1242`; `src/Enums.h:10-25`, `:60-84`, `:343-358`. Dumped tables.bin
table 0 (`monsterAttacks`) and table 2 (`weapons`) with a throwaway Python
reader following `src/Resource.cpp:217-311` (LE, 80-byte header of 20 end
offsets, each table = int count + payload).

## Verdicts

1. **CONFIRMED** (extends stage-1 outline). Thinking is a single pass per turn
   latched by `monstersUpdated` (`src/Game.cpp:880-883`); step lerps run in
   parallel; attacks only after all lerps settle (`src/Game.cpp:2462-2467`).
   `combatMonsters` is a LIFO (`src/Entity.cpp:1163-1164`) → reverse think order.
   Re-validation loop re-thinks attackers invalidated by moves
   (`src/Game.cpp:898-911`). Input gate via `snapMonsters` — off-screen
   walkers are snappable (`info 0x10000000` marks on-screen steps,
   `src/Entity.cpp:1095-1097`, `src/Game.cpp:2374-2389`).
2. **CONFIRMED.** No randomness in attack-vs-move: attack iff
   `aiWeaponForTarget(player) != -1`, else chase (`src/Entity.cpp:546-565`).
   Randomness only in weapon pick `nextByte() <= field2` (`src/Entity.cpp:836`).
   Goals 0..6 numeric, no enum (`src/Entity.cpp:572-642`). Attack/chase-player
   goals are re-chosen every turn (`src/Entity.cpp:609-619`). Axis-aligned LOS
   required except arch-vile / Vios parm≠0 (`src/Entity.cpp:752-766`).
   Per-type data = `monsterAttacks[sub*9+parm*3+{0,1,2}]`
   (`src/Combat.cpp:1058-1060`, table 0 `src/App.cpp:389`) + weapon row
   RANGEMIN/RANGEMAX (`weapons[w*9+2/3]`).
3. **CONFIRMED.** Strictly one attack at a time in `ST_COMBAT`; the next
   attacker is chosen in `combatState` after `runFrame()` returns 0, skipping
   now-invalid ones (`src/GameStateRunner.cpp:51-79`). Evading attackers
   retreat (goal 5) right after their attack (`:53-58`). Turn closes via
   `endMonstersTurn` there or in `updateMonsters` if lerps are still running.
4. **PARTIAL** (flagged only). Arch-vile raise (`src/Entity.cpp:568-570`,
   `:1280-1318`), boss haste resistance (`:1398-1404`), Vios teleport
   (`:716-745`), boss-pinky special attack rule (`:704-714`), fear (goal 4,
   `:541-545`), frozen skip (`src/Game.cpp:890`). Friendly sentry bot = player
   (`player->isFamiliar`), not a ring member.

## Evidence (key quotes)

* `src/Game.cpp:890` — `if (0x0 == (activeMonsters->monster->monsterEffects & 0x2) && (this->monstersTurn == 1 || (this->monstersTurn == 2 && activeMonsters->isHasteResistant())))`
* `src/Entity.cpp:1163-1164` — `this->monster->nextAttacker = app->game->combatMonsters; app->game->combatMonsters = this;`
* `src/Entity.cpp:609` — `if (this->monster->goalTurns >= 16 || goalType == 3 || goalType == 0) return false;`
* `src/Entity.cpp:836` — `n17 = (((int)app->nextByte() <= app->combat->getMonsterField(this->def, 2)) ? monsterField2 : monsterField3);`
* `src/GameStateRunner.cpp:61` — `for (nextAttacker = curAttacker->monster->nextAttacker; nextAttacker != nullptr && nextAttacker->monster->target == nullptr && !nextAttacker->aiIsAttackValid(); ...) undoAttack();`
* `src/Game.cpp:2446-2447` — snapMonsters: `performAttack(...); return false;` (input swallowed).
* `src/Entity.cpp:1123` — move-then-attack for `MOVE2ATTACK_MONSTERS` / evading without `flags & 1`.

## Extracted constants

* Masks: `EVADING 0x4604` {2,9,10,14}; `FEAR_IMMUNE 0x1F801` {0,11,12-16};
  `MOVE2ATTACK 0x180D0` {4,6,7,15,16} (`src/Enums.h:82-84`).
* Trace masks (bit = 1<<eType, `src/Enums.h:10-25`): 5295 = WORLD|PLAYER|MONSTER|NPC|
  DOOR|DECOR|ATTACK_INTERACTIVE|SPRITEWALL; 15791 = 5295|ENV_DAMAGE|
  MONSTERBLOCK_ITEM|NONOBSTRUCTING_SPRITEWALL; 4131 = WORLD|PLAYER|DOOR|SPRITEWALL;
  15535 = 15791 minus ENV_DAMAGE.
* Melee weapon mask `0x78002` = ids {1,15,16,17,18} (`src/Entity.cpp:786-791`).
* Step lerp 275 ms; 500 ms for subtype 13 and 14 parm≠0 (`src/Entity.cpp:1099-1104`).
* Arch-vile raise radius Chebyshev² ≤ 0x19000 (= 320² → 5 tiles), cooldown 4 turns
  (`src/Entity.cpp:569`, `:1313`).
* Vios teleport cadence `rand%3+3` turns, ±4 tiles, 30 tries (`src/Entity.cpp:717-744`).
* Tables: see `monster-turn.md` §3.4 (monsterAttacks and weapon rows 15-30).

## Open questions

* No setter of `monsterEffects & 0x2` ("frozen") found in `src/`; save/load
  stores `monsterEffects` as a short (`src/Entity.cpp:1543`, `:1727`), truncating
  bits ≥16 (fire/respawn counters).
* `goalFlags 0x10` is read/cleared but never set (`src/Entity.cpp:1141-1150`).
* Monster subtype 1 has an all-zero `monsterAttacks` row and no name in
  `src/Enums.h:60-62` — never attacks.
* `aiIsAttackValid` does not re-check RANGEMIN or off-axis permission, so a
  queued off-axis arch-vile/Vios attack would be dropped if re-validated
  (`src/Entity.cpp:655-668`).
* Goal 4/5 path semantics (`b=true`, `closestPathDist=0`) belong to the
  movement report.
