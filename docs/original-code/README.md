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
| `wall-push.md` | Pushing into a wall/spritewall: sound, `shiftWeapon` lower/raise (200/500/200 ms), `rockView` 1000 ms; secret-door branch (mapFlags 0x4) |
| `secret-doors.md` | Secret push walls (mapFlags 0x4 + ET_SPRITEWALL): `performDoorEvent(...,true)` two-phase 750+750 ms lerp, unlink+hide; `awardSecret` msg 119/sounds/XP/achievement; `EV_AWARDSECRET`; map00 secret walls |
| `player-damage.md` | Player taking damage: armor/defense math, `pain`/`painEvent`, shake, rock-back, vignette, direction arrow, weapon red tint, `died()` → `ST_DYING` fall/fade → dead menu |
| `fades.md` | Render scene fade: flags, `fadeScene` math/end behaviour, frame order, `EV_FADEOP` decode (bit15 = FADEIN), readers, map00 FADEOP list |
| `ui.md` | HUD, menus, dialogs, fonts |
| `vending-machine.md` | Vending machine minigame: EV_MINIGAME type 4 entry, terminal/help/hack screens, 4-digit hack puzzle, per-machine save bits, prices table, exit, hand-off to menus 83-88, test locations |
| `scripts.md` | tileEvents bytecode, staticFuncs |
| `audio.md` | sound/music |
| `app.md` | App lifecycle, game states, input mapping |

Raw investigation reports go to [`../research/`](../research/).
