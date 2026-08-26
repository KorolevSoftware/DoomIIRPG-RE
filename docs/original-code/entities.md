# Entities: NPC identity, characterChoice ↔ player representation (original RE port)

Every claim cites `src/` (legacy RE port). Bytecode IPs refer to the map00
script disassembly (`docs/research/assets/map00_disasm.txt`, provenance in
`docs/research/2026-08-26-choice-branch.md`).

## 1. The three selectable characters (characterChoice {1,2,3})

Written only by `Player::setCharacterChoice` which also stamps
`scriptStateVars[14]` (`src/Player.cpp:1264-1268`); call sites are the forced
intro select only (`src/IntroSequenceManager.cpp:599,628,676`; touch slot
order 0→1, 1→3, 2→2 at `:613-624`, keyboard cycle {1,3,2} via
`getCharacterConstantByOrder` `:328-341`). `Game::updateScriptVars` re-stamps
v14 every script batch (`src/Game.cpp:3468`).

| choice | class | proper name | base stats / gold (`src/Player.cpp:454-486`) | HUD portrait (`src/App.cpp:439-450`) | world art | pain/death snd (`src/Player.cpp:635-641,720-727`) |
|---|---|---|---|---|---|---|
| 1 | Major | Kira Morgan (MenuStrings 215, `src/MenuStrings.h:264`) | 8/9/97/12/110, 30 | `Hud_Player.bmp` | tile 68 | 1094 / 1092 |
| 2 | Sarge (heavy) | Stan Blazkowicz (216) | 12/14/92/6/100, 10 | `Hud_PlayerDoom.bmp` | tile 72 | 1093 / 1091 |
| 3 | Scientist | Riley O'Connor (217) | 8/8/87/6/150, 80 | `Hud_PlayerScientist.bmp` | tile 66 | 1093 / 1090 |

* **Choice 1 (Major) is the female hero** — tile 68 head frame shows a woman
  with long dark hair (verified from extracted art, frames per §2).
* The only other choice test in Combat is a weapon-14 attack sound
  (`src/Combat.cpp:298`); view-weapon art is shared across classes.
* Player start position/dir is NOT choice-gated: `Game::spawnPlayer`
  (`src/Game.cpp:941-972`) uses the map header spawn (map00: tile (4,19),
  angle 0) or the loadType==3 save-variant (3,15) dir 6.

## 2. NPC sprites are stacked composites

NPC frames per `src/Enums.h:604-613`: 0/1 front legs, 2 front torso,
3 front head, 4-7 back equivalents; composited by the sprite renderer
(`src/Render.cpp:3277` draws legs/torso/head parts at Z offsets). NPC tile
range 65-80 (`src/Enums.h:708-719`), `Render::isNPC` (`src/Render.cpp:2971-2972`).
Squad/NPC tiles: 66 = Riley O'Connor, 68 = Major, 69 = npc_bob, 71 = civilian,
72 = Sarge, 73 = generic female, 75 = scientist (`src/Enums.h:709-716`).

## 3. map00 squad sprites (7/9/10) and the player's world body

* spr7 = tile 68 (Major), spr9 = tile 66 (O'Connor), spr10 = tile 72 (Sarge)
  — entity defs (3,1)/(3,0)/(3,4) (`tmp_entities.bin`; art list in
  `docs/research/2026-08-26-choice-branch.md` §4).
* Boot chain (camera 5, func@2328): all three walk in formation **including
  the chosen one** (v14==1: spr10/9/7 → x=160 column, IPs 2346-2360; walks
  @2439-2453), then the chosen one is INSTANT-teleported out of the scene to
  tile (22,29): spr7 @2531 (v14==1), spr10 @2545 (v14==2), spr9 @2552
  (v14==3/else); the other two park at (8,18)/(7,19) @2936-2987.
* EVT 617 (doorway): the two NON-chosen sprites walk to (10,19) and are
  HIDEen (@3283/3285, @3297/3299, @3311/3313). The chosen sprite never
  enters this scene.
* After EVT 617 the chosen sprite lives on as a scripted companion: tile
  events (21,27)-(23,28) (event IDs 885/886/917/918/919, y*32+x packing)
  step it through (23,29)→(21,29)→(21,30)→(22,30)→(22,29)→(23,29)→(24,30)
  (IPs 7184-7633), each step gated by `EV_TILE_EMPTY` (op 9,
  `src/Enums.h:411`) so it only advances when the tile is clear.
* Familiar-mode body double: `Game::spawnPlayerEntityCopy`
  (`src/Game.cpp:2725-2759`) spawns an entity with the chosen class's art
  (1→68, 3→66, 2→72) at saveX/saveY (`src/Player.cpp:2105`, removed
  `:2163-2164`); `playerEntityCopyIndex` is save/loaded
  (`src/Player.cpp:1293,1376`).

## 4. Rewrite status

`vars[14] = 1` hardcoded marine/Major (`new_src/domain/game/ScriptVM.cpp:300-305`);
full v14==1 boot+EVT 617 behavior verified against err.log
(`docs/research/2026-08-26-hero-choice-and-weapon.md` §A.4).
