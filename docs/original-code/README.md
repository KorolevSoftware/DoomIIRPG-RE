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
| `map-change.md` | Level transition: `EV_CHANGE_MAP` decode, entry points, `loadMap`→`ST_SAVING`→`ST_TRAVELMAP`→`ST_LOADING`, saveType bits, spawn priority, per-map BRIEFWORLD (`SB_n`) persistence on revisit, what carries over |
| `save-load.md` | Save/load to disk: file names (FWORLD, CONFIGFILENAME, BRIEFPLAYER, FULLPLAYER, SB_1..10), path/endianness, config + player byte layouts, full vs brief world, save triggers, `hasSavedState`, load flow, menus/strings |
| `travel-map.md` | `ST_TRAVELMAP` inter-level screen: init from lastMapID/loadMapID, planets Moon/Earth/Hell, assets, phase timings (path reveal, spaceship, locator crosshair, prompt), input/skip, exit to `ST_LOADING`, `scriptStateVars[15]` travel direction, dead star-field page, mapID 10 = debug Test Map |
| `ui.md` | HUD, menus, dialogs, fonts |
| `automap.md` | `ST_AUTOMAP` (6): toggle keys/menu, shared playing input, seen bits (tile 0x80, line 0x8, sprite 0x200000) + persistence, exact `drawAutomap` layout/colors (cell 8, origin 112,32), cursor blink, givemap/god cheats |
| `vending-machine.md` | Vending machine minigame: EV_MINIGAME type 4 entry, terminal/help/hack screens, 4-digit hack puzzle, per-machine save bits, prices table, exit, hand-off to menus 83-88, test locations |
| `hacking-minigame.md` | Hacking ("Hacker") minigame: EV_MINIGAME type 2 args (custom, columns, turns), column-sort rules, scramble generation and IQ scaling, keys/touch, exact draw layout and colors, sounds, vars[7] result; brief sentry-bot sibling summary |
| `target-practice.md` | Target practice (map00 shooting range): EV_START_TARGETPRACTICE 84, strip to rifle+8 bullets, head/body/leg zones 30/20/10, score>120 → +1 accuracy, score bar, input/exit, target sprite frames |
| `scripts.md` | tileEvents bytecode, staticFuncs |
| `audio.md` | sound/music |
| `app.md` | App lifecycle, game states, input mapping |

Raw investigation reports go to [`../research/`](../research/).
