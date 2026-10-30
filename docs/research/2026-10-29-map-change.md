# 2026-10-29 — Map change in the original

## Questions
1. `EV_CHANGE_MAP` operands and other entry points into a map change.
2. Full `Canvas::loadMap` sequence: unload, load, spawn, per-map reset, persistence, states, first scripts.
3. Is the state of a left map saved and restored on revisit?

## Method
Read `src/ScriptThread.cpp:484-517`, `src/LoadingManager.cpp:38-79,304-312,594-600,604-751`,
`src/Canvas.cpp:853-915,1062-1063,1375-1391`, `src/Game.cpp:330-360,642-685,701-727,941-972,
1576-1911,2059-2215,2250-2318,2500-2505,3690-3715`, `src/Render.cpp:211-260,2548-2575`,
`src/TravelMapManager.cpp:23-40,455-521,585-596`, `src/MenuSystem.cpp:285-298,2990-3002,3098-3108,3120-3136,3584-3610`,
`src/Player.cpp:94-105`, `src/Entity.cpp:1787-1827`. Grepped all `loadMap(` callers.
Disassembled map00..map09 from the IPA with `tools/disasm_map_scripts.py` (grep `CHANGE_MAP`).

## Verdicts
1. CONFIRMED: `B` u8 = map(&0xF) | dir((>>4)&7) | fade(&0x80); `S` u16 tile (&0x3FF, x|y<<5).
   `spawnParam = dir<<10 | tile`. Stats iff nextMap >= loadMapID. Other entry points: fadeScene
   CHANGEMAP/SHOWSTATS, level-stats continue, debug ACTION_CHANGEMAP, reload cheats, new game,
   load game / restart level (`Game::loadState`).
2. CONFIRMED: loadMap only sets ids and (normally) arms `saveState(43)`; the old map is unloaded
   only in `loadMedia` (second ST_LOADING frame). Spawn: spawnParam > loadType 3 fixed > header.
   Player object persists in memory; only `levelInit` touches it.
3. CONFIRMED: per-map files `SB_1..SB_10` (BRIEFWORLD[mapID-1]) written on leave (ST_SAVING) and
   on entry (`saveLevelSnapshot`), read by `loadWorldState` when compile date matches.
   Deleted on new game (`removeState`).

## Map data: CHANGE_MAP sites (file → op)
map00 (ID1): 9676 map=2 dir=0 tile=741
map01 (ID2): 7924 map=1 dir=4 tile=117; 7938 map=3 dir=6 tile=207
map02 (ID3): 8315 map=2 dir=4 tile=113; 8389 map=4 dir=4 tile=564
map03 (ID4): 730 map=5 dir=6 tile=877; 776 map=3 dir=6 tile=451
map04 (ID5): 627 map=6 dir=2 tile=879; 657 map=4 dir=0 tile=405
map05 (ID6): 773 map=7 dir=2 tile=486; 803 map=5 dir=0 tile=310; 4179 map=7 dir=2 tile=518
map06 (ID7): 1233 map=6 dir=0 tile=142; 1266 map=8 dir=4 tile=408
map07 (ID8): 6094 map=9 dir=2 tile=622; 6124 map=7 dir=6 tile=725
map08 (ID9): 793 map=8 dir=6 tile=168
map09 (ID10): none
No site sets bit7 (the tool would append " fade+stats"; that label is loose — bit7 only selects
the fade, stats depend on next >= current). Every map except map00 has a "back" exit to ID-1.

## Notes / open questions
- map08 (ID9) has only a back exit (map=8); how ID10 is reached (another opcode, a staticFunc entry not walked by the CFG, or the travel map) was not checked.
- `Game::changeMapParam` (`src/Game.h:88`) is unused.
- `prepareMonsters` resurrection on revisit of completed, fully-cleared maps (`src/Game.cpp:701-727`).
- Travel-map screen timing/input and the level-stats menu are out of scope (other researcher).
- Entity save record layout (`Entity::saveState`) not decoded here — belongs to save/load topic.

Curated: `docs/original-code/map-change.md`.
