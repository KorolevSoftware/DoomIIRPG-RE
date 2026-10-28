# Original code knowledge base (`src/`)

Verified facts about the legacy C++ RE port of Doom II RPG that lives in `src/`.
One file per topic; **every claim cites `file:line`**.

A lot of already-verified material lives in the root [`PLAN.md`](../../PLAN.md)
(mapXX.bin layout, polygon decode, matrices, media mapping, GL pipeline notes).
Topic files here deepen and extend it — do not copy it.

Topic files (created on demand):

| File | Topic |
|---|---|
| `rendering.md` | TinyGL/GL pipeline, BSP traversal, sprites, sky, fog |
| `map-format.md` | mapXX.bin sections, geometry decoding |
| `media.md` | mappings/palettes/texels, texture creation, animation |
| `entities.md` | Entity/EntityDef/EntityMonster, spawning, entityDb grid |
| `monster-movement.md` | Monster step pathing (`aiGoal_MOVE`/`calcPath`), monster clip, move lerp/relink timing, walk frames, `checkLineOfSight` |
| `combat.md` | CombatEntity calcHit/calcDamage, weapons, table.bin/entities.bin |
| `monster-attack.md` | Monster attack execution: `aiWeaponForTarget`, `performAttack`/`monsterSeq` timeline, monster→player hit/damage, hitscan vs missile, monster attack tables |
| `monster-turn.md` | Monster phase: activeMonsters/combatMonsters, monsterAI, goal machine, aiWeaponForTarget, attack sequencing |
| `player-damage.md` | Player taking damage: armor/defense math, `pain`/`painEvent`, shake, rock-back, vignette, direction arrow, weapon red tint, `died()` → `ST_DYING` fall/fade → dead menu |
| `ui.md` | HUD, menus, dialogs, fonts |
| `scripts.md` | tileEvents bytecode, staticFuncs |
| `audio.md` | sound/music |
| `app.md` | App lifecycle, game states, input mapping |

Raw investigation reports go to [`../research/`](../research/).
