# 2026-10-29 — Mirror room: zombie reflection survives the zombie's death

## Hypothesis

User report (rewrite, map00): toilet room with a mirror; a zombie appears behind
the player in the reflection; after the zombie is killed its reflection stays.
Suspected: the original ties the reflection to the real zombie via
`EV_SETDEATHFUNC`, which the rewrite skips.

## Verdict: CONFIRMED

The reflection is a separate, inert monster entity (sprite 85) in a mirrored
copy of the room. The real zombie (sprite 86) has a death function bound at map
init (`SETDEATHFUNC sprite=86 deathfunc=ip 7693`); that function is a single
`HIDE sprite=85`. The original runs it from `Entity::died`. The rewrite skips
`EV_SETDEATHFUNC` and has no death-function table or call site, so `HIDE 85`
never runs. The reflection does NOT move in sync with the zombie in either
codebase. It is placed once by script and never moves after that.

## Method

* Extracted `map00.bin` from `build_new/new_src/Doom 2 RPG.ipa`. Ran
  `tools/disasm_map_scripts.py` once plainly and once with
  `--funcs 1660,1666,1701,1718,1735,1752,1775,1792,1811,1830,1847,1868,4510,4536,7693,8980,9011,9057`
  (all SETDEATHFUNC targets).
* Read `src/ScriptThread.cpp`, `src/Game.cpp`, `src/Entity.cpp`, `src/Render.cpp`.
  Then read the matching rewrite code in `new_src/domain/game/`.

## Evidence: map00 script data (disassembler output)

Map init (INIT_MAP -> func@384 / func@508):

```
  475: MONSTERFLAGOP sprite=86 ADD mask=0x8      ; NOACTIVATE
  478: MONSTERFLAGOP sprite=86 ADD mask=0x10     ; NORESPAWN
  481: MONSTERFLAGOP sprite=85 ADD mask=0x8      ; NOACTIVATE
  484: MONSTERFLAGOP sprite=85 ADD mask=0x80     ; NOTRACK
  487: MONSTERFLAGOP sprite=85 ADD mask=0x10     ; NORESPAWN
  524: SETDEATHFUNC sprite=86 deathfunc=ip 7693
```

Flag names: `src/Enums.h:347-351` (`MFLAG_NOACTIVATE = 0x8`,
`MFLAG_NORESPAWN = 0x10`, `MFLAG_NOTHINK = 0x20`, `MFLAG_NOTRACK = 0x80`).

Ambush script: EVT 151 (tile 21,28, FACE S) and EVT 155 (tile 22,28, FACE S),
both entry IP 7638:

```
 7642: EVAL [ v65 == 0 ]                         ; one-shot
 7649: NEXTSTATE ++v65
 7651: EVENTOP disable event[151]
 7654: LERPSPRITE sprite=86 dst=(22,27) flags=NO_TIME|DEFAULT_Z   ; real zombie appears behind the player
 7658: WAIT 300ms
 7660: LERPSPRITEOFFSET sprite=85 pixel=(1407,1896) z=+0  t=0ms
 7667: LERPSPRITEOFFSET sprite=85 pixel=(1407,1896) z=+42 t=1000ms flags=2  ; reflection rises in the mirror room
 7674: WAIT 700ms
 7676: GOTO walk to (22,28) face=N animate       ; player is turned to face the zombie
 7679: CALL_FUNC func_7509                       ; player reflection to (22,29)
 7682: LERPSPRITE sprite=85 dst=(22,30) flags=NO_TIME|DEFAULT_Z   ; mirrored position of (22,27)
 7686: WAKEMONSTER sprite=86
 7688: ADVANCETURN
```

Death function (only reachable via SETDEATHFUNC):

```
 7693: HIDE sprite=85
 7695: RETURN
```

Other script uses of 86: EVT 142 (21,27 FACE S) `MONSTERFLAGOP 86 ADD 0x20`
(NOTHINK) and EVT 143 (21,27 FACE E|N|W) `REMOVE 0x20` (IPs 7233/7237).

Mirror geometry. Every mirror-room tile script moves the player's double
(sprite 7/10/9 for hero v14 = 1/2/3) to `(x, 57 - y)` and then GOTO-walks the
player:

| Player tile | Double tile | Script IP |
|---|---|---|
| (21,28) | (21,29) | 7282 |
| (21,27) | (21,30) | 7350 |
| (22,27) | (22,30) | 7418 |
| (22,28) | (22,29) | 7486 -> func_7509 |
| (23,28) | (23,29) | 7184 |

Stepping south from row 28 (EVT 153, 22,28 EXIT S) hits `8388: ABORT_MOVE`,
so the mirror plane lies between rows 28 and 29. The zombie at (22,27) and its
reflection at (22,30) follow the same `57 - y` rule.

## Evidence: original engine

* `EV_SETDEATHFUNC` decode, `src/ScriptThread.cpp:1059-1075`: reads u8 sprite
  and s16 ip. It resolves the entity through `mapSprites[S_ENT + sprite]`. ip != -1 calls
  `addEntityDeathFunc(entity, ip)`, ip == -1 calls `removeEntityFunc(entity)`.
* `Game::addEntityDeathFunc`, `src/Game.cpp:3495-3510`: the first free pair in
  `short entityDeathFunctions[128]` (`src/Game.h:133`, 64 pairs of
  `{sprite, ip}`, free marker `-1`). It sets `entity->info |= 0x2000000`. When the table is full,
  `Error("Too many entity death functions set")`.
* `Game::removeEntityFunc`, `src/Game.cpp:3512-3521`: clears the pair and
  `info &= 0xFDFFFFFF`.
* `Game::executeEntityFunc(entity, throwAwayLoot)`, `src/Game.cpp:3523-3534`:
  linear search by sprite. It runs `allocScriptThread()`, sets
  `throwAwayLoot`, calls `alloc(ip)` and then `run()` synchronously. After that it clears the pair.
* Call site: `Entity::died`, `src/Entity.cpp:522-525`, at the END of died,
  outside the eType switch (so it applies to any eType):
  `if ((this->info & 0x2000000) != 0x0) { app->game->executeEntityFunc(this, this->deathByExplosion(entity)); this->info &= 0xFDFFFFFF; }`
  This runs before `mapSpriteInfo[sprite] = n3` (`:526`). `deathByExplosion` =
  the killer is the player with weapon 11 (`:529-532`).
* died's early-out (`:431-433`, not active, or `MFLAG_NOKILL`) also skips
  the death function.
* `EV_HIDE`, `src/ScriptThread.cpp:818-844`: `mapSpriteInfo |= 0x10000`, unlink. For
  eType 2 (monster) it calls `corpsifyMonster(...)` and then `removeEntity` (`:836-840`). The reflection
  vanishes.
* Reset: `Game::unloadMapData` `src/Game.cpp:671-673` and ctor `:50-52` fill all
  128 shorts with -1.
* Persistence: `saveWorldState` `src/Game.cpp:1625-1636` writes a u8 count
  plus count pairs of s16 `{sprite, ip}`. `loadWorldState` `:1797-1800` reads them back. The
  entity bit 0x2000000 travels in the entity-state word as 0x1000000
  (`src/Entity.cpp:1813-1814` save, `:1600-1601` load).
* Why the reflection is inert. `MFLAG_NOACTIVATE` blocks the render-time sight
  wake (`src/Render.cpp:1578`). `MFLAG_NOTRACK` excludes it from `killAll`
  (`src/Game.cpp:816`), from the level-stats monster count (`src/Player.cpp:115`), and
  from the save sprite bit (`src/Game.cpp:1648`). Nothing in the engine moves sprite
  85 after 7682. There is no mirror/reflection code in `src/` (grep for
  "mirror|reflect" finds only Combat reflection damage).

## Evidence: rewrite

* `new_src/domain/game/ScriptVM.cpp:1053-1057`: `EV_SETDEATHFUNC` consumes its
  operands and logs `"[script] SETDEATHFUNC sprite=%d skipped (no monsters)"`.
* No death-function table, `executeEntityFunc` analog, or `info & 0x2000000`
  check exists in `Game::entityDied` (`new_src/domain/game/Game.cpp:435-457`) or
  `MonsterSystem::diedMonster` (`new_src/domain/game/MonsterSystem.cpp:386-421`).
* `WorldState.cpp:180` writes deathFunc count 0. `:268` skips the pairs on load.
* `EV_HIDE` monster branch IS ported (`ScriptVM.cpp:619-648`, corpsify+remove).
  `HIDE 85` would work if it ran.
* `EV_MONSTERFLAGOP` is also skipped (`ScriptVM.cpp:1059-1064`). As a result,
  sprite 85 lacks `MFLAG_NOACTIVATE`, and `MonsterSystem::renderActivate`
  (`MonsterSystem.cpp:123-141`, gate at :130) could sight-wake the reflection
  if a trace reaches it. Sprite 86's NOTHINK toggles (EVT 142/143) are lost.
  85 also lacks NOTRACK, so it would be counted as a killable monster.

## Root cause

The rewrite never registers or runs entity death functions, so map00's
`deathfunc ip 7693` (`HIDE sprite=85`) never runs when zombie 86 dies.

## Minimal port

1. `Game`: `int16_t entityDeathFunctions[128]` all -1. Reset on map
   load/unload (`src/Game.cpp:671-673`). Add `add/remove/execute` as in
   `src/Game.cpp:3495-3534`, using entity info bit 0x2000000.
2. `ScriptVM` `EV_SETDEATHFUNC`: resolve the sprite's entity, then add (ip != -1) or remove.
   Execute = allocThread + allocRaw(ip) + run, like
   `ScriptVM::executeStaticFunc` (`new_src/domain/game/ScriptVM.cpp:174-186`).
   Optionally carry `throwAwayLoot` (it matters only for GIVELOOT/GIVEITEM inside
   death funcs, `src/ScriptThread.cpp:965,1113,1142,2131-2211`).
3. `Game::entityDied`: after the eType switch, for every arm (the early-out
   returns must still skip it), `if (info & 0x2000000) { executeEntityFunc(e, killedByPlayerWeapon11); info &= ~0x2000000; }`.
   Entity bit 0x2000000 is already save/loaded (`new_src/domain/game/EntityState.cpp:88,218`).
4. WorldState: write/read the real pairs instead of count 0/skip.
5. Recommended (not required for the hide): port `EV_MONSTERFLAGOP`
   (`src/ScriptThread.cpp:782-806`). Op = `(b>>6)&3` (0 add, 1 remove, 2 set),
   mask `1 << (b & 0x3F)`. The op applies only if `entity->monster != nullptr`, and it sets `info |= 0x400000`.

## Open questions

* Whether the rewrite's 2D sight trace can actually reach sprite 85 through
  the mirror plane (i.e. whether the reflection gets woken today). Not
  verified. Porting MONSTERFLAGOP makes it moot.
* The other 17 map00 bindings (1660/1666 dialog counter; 1701-1868
  `EVENTOP disable`; 4510/4536 for sprites 46/47; 8980/9011/9057) are also dead in
  the rewrite today. The same fix covers them.
