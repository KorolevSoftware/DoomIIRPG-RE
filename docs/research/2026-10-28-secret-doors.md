# 2026-10-28 — Secret doors (push walls)

Curated result: `docs/original-code/secret-doors.md`.

## Hypotheses / questions

1. What does `performDoorEvent`'s 4th arg (`b = true`) change?
2. What does `awardSecret(bool)` do (counters, HUD, sound, XP, readers)?
3. Other secret triggers; how mapFlags 0x4 is set.
4. What the rewrite already has / is missing.
5. Which map00 spritewalls are secret.

## Method

* Read `src/Game.cpp:1039-1157` (performDoorEvent), `:2855-2957`
  (updateLerpSprite), `:3078-3215` (freeLerpSprite), `:3473-3491`
  (awardSecret), `src/Player.cpp` (addXP, showAchievementMessage,
  levelGrade, calcScore), `src/LoadingManager.cpp:373,559-564`,
  `src/ScriptThread.cpp:1152-1179,1256-1259,1753-1778`,
  `src/Render.cpp:426-431,586-599`, `src/Game.cpp:365-480` (spritewall
  entity creation).
* Strings resolved from the shipped `strings.idx` + `strings0N.bin`
  (group 0) with a scratch reader mirroring `new_src/io/Localization.cpp`
  (index format: 2-byte count, 5-byte records). Control: id 44 =
  "Door is Lock-ed", matching `src/PlayingInputHandler.cpp:447`.
* map00..09 from the `.ipa` (note: `tools/map00.bin` differs from the
  archive copy): tile flags + sprites via `tools/map_to_obj.parse_map`;
  scripts via `python3 tools/disasm_map_scripts.py <map>.bin -o ... --verify`
  (map00: 0 failures); entity defs from `entities.bin` (8-byte records,
  `src/EntityDef.cpp:31-43`).

## Verdicts

1. CONFIRMED / detailed. `b=true`: `secretActive = true`,
   `LS_FLAG_SECRET_OPEN` instead of `DOOROPEN`, no DOORLERP bit at start,
   push 32 along the wall normal (NORTH +Y / SOUTH -Y / WEST +X / EAST -X,
   `src/Game.cpp:1101-1126`), scale unchanged, no sound, no auto-close
   registration. Phase 2 re-armed in `freeLerpSprite`
   (`src/Game.cpp:3170-3199`): DOORLERP on, scale 64->0 (width collapse,
   `src/Render.cpp:586-599`), slide 32 sideways (EAST +Y, NORTH +X,
   WEST -Y, SOUTH -X), 750 ms. End (`src/Game.cpp:3127-3135`):
   `secretActive=false`, unlink, SPRITE_FLAG_HIDDEN. No mapFlags write;
   cannot close again.
2. CONFIRMED. Msg group 0 #119 "Found Se-cret!" flags 0;
   `mapSecretsFound++`; sound 1103 secret.wav (push) / 1020 chime.wav
   (script); last secret of a level (first time, `foundSecretsLevels` bit
   `loadMapID-1`) -> achievement help dialog (135 + arg 137), +10 XP,
   chime; otherwise +5 XP (msg 103). Readers: `levelGrade`, save/load,
   `calcScore` (dead code with a loop-counter bug, `src/Player.cpp:369,397`).
3. CONFIRMED. Only other trigger: `EV_AWARDSECRET` (op 50,
   `src/ScriptThread.cpp:1256-1259`, no operands). 0x4 is map-file data
   only (`src/LoadingManager.cpp:562-563`); no runtime writer sets it.
   Per-map check: totalSecrets == #0x4 tiles + #AWARDSECRET ops:

   | map | totalSecrets | 0x4 tiles | AWARDSECRET ops |
   |---|---|---|---|
   | 00 | 2 | (13,9) (15,25) | 0 |
   | 01 | 5 | (19,12) (15,20) (25,22) (7,25) | 1 (event 21, tile 9,10 FACE N, IP 2335) |
   | 02 | 2 | (21,7) (24,16) | 0 |
   | 03 | 4 | (11,11) (19,19) (17,23) (15,29) | 0 |
   | 04 | 4 | (27,13) (15,14) (24,18) (3,19) | 0 |
   | 05 | 6 | (7,15) (18,24) (23,24) (7,25) (13,26) (19,30) | 0 |
   | 06 | 5 | - | 5 |
   | 07 | 3 | - | 3 |
   | 08 | 1 | - | 1 |
   | 09 | 0 | - | 0 |
4. See "Rewrite gap list" below.
5. CONFIRMED. map00: sprite 33 (tile 326, SOUTH, pos 864,632) on tile
   (13,9) - stand on (13,10) facing N; sprite 59 (tile 340, NORTH, pos
   992,1608) on tile (15,25) - stand on (15,24) facing S. Both have
   SOLIDSIDE 0x800000 and no def of their own -> `find(12,0)` spritewall
   def (eSubType 0), `src/Game.cpp:462-473`.

## Rewrite gap list (new_src, as of 2026-10-28)

* `new_src/domain/game/DoorSystem.cpp:50` rejects anything that is not
  `isDoor()` -> spritewalls cannot enter; signature
  `performDoorEvent(int n, Entity*, int n2, ScriptThread*)`
  (`DoorSystem.h:60`) has no `secret` flag. DoorAnim is single-phase,
  sets 0x80000000 at start (`DoorSystem.cpp:135`), registers the door in
  `openDoors_` (`:138`), unlinks at the end but never sets
  SPRITE_FLAG_HIDDEN. A secret needs: push along normal with scale held,
  then a second 750 ms phase (DOORLERP on, scale 64->0, sideways slide),
  then unlink + HIDDEN, no openDoors registration, no door sound.
* DoorAnim does not count toward `lerps.animatingEffects()`
  (`new_src/core/PlayerActions.cpp:128`), so the legacy 1500 ms input
  block is absent.
* `new_src/core/PlayerActions.cpp:~255-300`: no secret branch. It must sit
  after the door chain and BEFORE the wall-push test, must not depend on
  `weapon2 >= 0 && !combat.active` (the current fire block gate), must use
  the FACING tile's mapFlags, and must not advance the turn.
* `new_src/domain/world/MapBits.h:14` comment calls 0x4 "wall push" - it
  is the secret-door flag; `TileFlag::SECRET = 0x4` is unnamed.
* No `secretActive` (stub comment `new_src/domain/game/DialogSystem.cpp:226`).
* No `mapSecretsFound`, no `foundSecretsLevels`, no `awardSecret`;
  `totalSecrets` is parsed (`new_src/domain/world/MapParser.cpp:27`).
  `Player::addXP` exists (state half, `new_src/domain/game/Player.h:144`);
  msg-103 composition lives in `MonsterSystem::awardKillXP`.
* `EV_AWARDSECRET` (50) has no case in `ScriptVM.cpp`; the default branch
  (`ScriptVM.cpp:1506-1511`) logs and sets `t->state = 0`, i.e. kills the
  thread - harmless on map00 (no such op) but breaks map01 event 21 and
  maps 06-08.
* No audio, no achievement help dialog path (`showAchievementMessage`).
