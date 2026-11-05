# 2026-11-04 — Particle call sites in the original (src/)

Scope: every place `src/` spawns or clears particles, with trigger, exact
arguments, co-located sounds and dependencies; plus where each path lives (or
not) in `new_src/`. ParticleSystem internals (what a "type" / color does) are
out of scope (separate research).

## Method

`grep -rn -i particle src/` (excluding `src/ParticleSystem.*`), then each site
read in context; callers of the enclosing functions grepped to establish
reachability. new_src checked with grep on the same function names.

## Verdict

CONFIRMED all call sites listed in the task; no additional *direct* call
sites exist. Additional findings:

- `Entity::resurrect` (`src/Entity.cpp:1368`) is reached from FOUR callers,
  so every monster resurrection spawns the orange burst:
  arch-vile raise (`src/Entity.cpp:1334`), `Game::prepareMonsters`
  (`src/Game.cpp:718`), `EV_RESPAWN_MONSTER` (`src/ScriptThread.cpp:1220`),
  `Game::raiseCorpses` (`src/Game.cpp:3570`).
- `Game::raiseCorpses` (`src/Game.cpp:3549`) has NO caller in `src/` (grep
  `raiseCorpses` finds only the definition) — sites `src/Game.cpp:3570/3578`
  are dead code.
- `Combat::hurtEntityAt(..., bool b, ...)` is only ever called through the
  8-arg wrapper with `b = false` (`src/Combat.cpp:1111`; the only other caller
  `src/Combat.cpp:1094` uses the wrapper). Hence `src/Combat.cpp:1146` and
  `src/Combat.cpp:1179` are unreachable (both require `b`); with `b == false`
  a corpse on the tile makes the whole loop `return` (`src/Combat.cpp:1141-1143`).
- `EV_SPAWN_PARTICLES` color index is `& 0x7` (0..7) but
  `ParticleSystem::levelColors` has only 7 entries
  (`src/ParticleSystem.h:43`) — index 7 reads out of bounds.

## API shapes used at call sites

- `spawnMonsterBlood(Entity*, bool gib)`
- `spawnParticles(int type, int color, int sprite)` — position taken from a map sprite
- `spawnParticles(int type, int color, int x, int y, int z)` — world units
- `freeAllParticles()`

Colors (ARGB, signed literal -> hex): `-1` = 0xFFFFFFFF, `-8421505` =
0xFF7F7F7F, `-161512` = 0xFFFD8918, `-3355444` = 0xFFCCCCCC, `0xFF81603D`.
`levelColors[] = {0xFFFFFFFF, 0xFF3A332E, 0xFF626262, 0xFF7F7F7F, 0xFFD1CF11,
0xFFFD8918, 0xFF20B00D}` (`src/ParticleSystem.h:43`).

Rendering slot: `REPAINT_PARTICLES` (32, `src/Canvas.h:127`) drawn in
`Canvas` after the REPAINT_VIEW3D block (which runs `fadeScene`) and before the
swipe area and `hud->draw` (`src/Canvas.cpp:391-417`). Thorn flight also sets
the flag explicitly (`src/Combat.cpp:1283`).

## Call sites

### C1 — `src/Combat.cpp:444` `Combat::monsterSeq` — damage reflection
Trigger: monster's attack sequence finished (`nextStageTime != 0 && gameTime >
nextStageTime && numActiveMissiles == 0 && animatingEffects == 0`,
`src/Combat.cpp:442`) and `reflectionDmg > 0 && curAttacker->monster != nullptr`
(`:443`). `reflectionDmg` is set in `explodeOnPlayer` when the player has
`buffs[0] > 0` (reflect) and attacker is not a boss (`src/Combat.cpp:968-971`,
`:1021-1022`: `this->accumRoundDamage = (this->reflectionDmg = this->totalDamage)`).
Call: `spawnMonsterBlood(this->curAttacker, false)`, then
`curAttacker->pain(reflectionDmg / 2, playerEnt)` (`:445`), `reflectionDmg = 0`;
death -> `died(true, playerEnt)` (`:447-449`). No direct sound here (pain plays
the monster pain sound, `src/Entity.cpp:346-347`).

### C2 — `src/Combat.cpp:909` `Combat::explodeOnMonster` — monster hit
Trigger: `hitType != 0`, `targetType == 2` (ET_MONSTER), `totalDamage > 0`
(`:904-905`). Order: `checkMonsterFX()` (`:906`), `pain = curTarget->pain(totalDamage, playerEnt)` (`:907`),
then `if (attackerWeaponId != 14) spawnMonsterBlood(curTarget, false)` (`:908-910`)
— no blood for WP_ITEM (14, `src/Enums.h:150`). No sound in this branch.

### C3 — `src/Combat.cpp:942` `Combat::explodeOnMonster` — corpse gib
Trigger: `hitType != 0` and `targetType == 9` (ET_CORPSE; `targetType =
curTarget->def->eType`, `src/Combat.cpp:77`). Sequence (`:939-944`):
```
app->render->mapSpriteInfo[sprite] |= 0x10000;   // hide corpse sprite
this->isGibbed = true;
app->particleSystem->spawnMonsterBlood(this->curTarget, this->isGibbed);
app->game->spawnDropItem(this->curTarget);
app->sound->playSound(1037, 0, 3, 0);
```
`isGibbed` is reset to false at the start of each player attack
(`src/Combat.cpp:189`, `playerSeq`) and only set here. Gib sound 1037, flags 0,
priority 3. Which weapons can target a corpse is not established here
(`src/Player.cpp:777` only handles chainsaw "usedChainsaw" on corpses).

### C4 — `src/Combat.cpp:1146` `Combat::hurtEntityAt` — splash on corpse (DEAD)
`else if (eType == ET_CORPSE) { if (!b) return; died(false, entity); info |= 0x10000; spawnMonsterBlood(mapEntity, true); }`
(`:1140-1147`). Unreachable: `b` is always false (see verdict).

### C5 — `src/Combat.cpp:1179` `Combat::hurtEntityAt` — dynamite kill gib (DEAD)
Monster killed by splash, `attackerWeaponProj != 5`, `b && attacker eType ==
ET_DECOR_NOCLIP && eSubType == DECOR_DYNAMITE` -> `info |= 0x10000;
spawnMonsterBlood(mapEntity, true)` before `died(true, entity)` (`:1176-1182`).
Unreachable (`b` false).

### C6 — `src/Combat.cpp:1273-1283` `Combat::updateProjectile` — thorn trail
Trigger: missile in flight (`actMissile->flags & 0x2`, `:1255`) with
`missileAnim == 171`, which is set only by `launchProjectile` case 9
(WP_PROJ_THORNS, `src/Enums.h:128`), which also resets
`numThornParticleSystems = 0` (`src/Combat.cpp:1545-1550`, with `z1/z2 -= 32`).
Per frame: dx/dy = unit step along the dominant axis from attacker sprite
(x1,y1) toward `game->viewX/viewY` (`:1260-1272`); candidate point
`x = x1 + n*dx*64 + dx*32`, `y = y1 + n*dy*64 + dy*32` (n =
numThornParticleSystems, `:1273-1274`). When the missile sprite is within 4
units of it on both axes (`:1275`):
`spawnParticles(1, 0xFF81603D, x, y, getHeight(x, y))` (`:1277`), plus
`gsprite_allocAnim(170, x, y, height + 32)` with scale 48 and duration extended
by the missile's remaining time (`:1278-1280`), `++numThornParticleSystems`.
`repaintFlags |= REPAINT_PARTICLES` every in-flight frame (`:1283`). No sound.

### C7 — `src/Combat.cpp:1344` `Combat::updateProjectile` — thrown item impact
Trigger: missile arrived, `attackerWeaponProj == 13` (WP_PROJ_ITEM; launched
with `missileAnim = activeWeaponDef->tileIndex`, `src/Combat.cpp:1567-1571`).
`missileAnim = 0; spawnParticles(1, -1, actMissile->sprite); playSound(1135, 0, 4, 0)`
(`:1342-1345`). Particles take the missile sprite's position, then the missile
is destroyed (`:1379`); no impact anim since missileAnim = 0.

### C8 — `src/Combat.cpp:1365` `Combat::updateProjectile` — thorn impact
Trigger: missile arrived, `attackerWeaponProj == 9`. `renderMode = 0;
missileAnim = 170; spawnParticles(1, 0xFF81603D, x, y, z + 16); z += 48`
(`:1362-1367`), x/y/z = `actMissile->pos[3..5]` (`:1289-1291`). Impact anim 170
only drawn if `curAttacker == nullptr || (curTarget == nullptr && hitType != 0)`
(`:1380`). No sound.

### C9 — `src/Entity.cpp:375` `Entity::pain` — barricade broken
Trigger: `eType == ET_ATTACK_INTERACTIVE`, `eSubType == 1` (INTERACT_BARRICADE)
(`:371-373`). Frame set to broken (`|0x100`, `:374`),
`spawnParticles(2, -1, sprite)`, `playSound(1038, 0, 3, false)`,
`unlinkEntity(this)` (`:375-377`).

### C10 — `src/Entity.cpp:381` `Entity::pain` — crate / furniture smashed
`eSubType == INTERACT_CRATE (2) || INTERACT_FURNITURE (0)` ->
`spawnParticles(2, -8421505, sprite)` (grey 0xFF7F7F7F); then
`removeEntity`, `info |= 0x400000`, sprite hidden `|= 0x10000` (`:388-390`).
No sound in this function.

### C11 — `src/Entity.cpp:384` `Entity::pain` — pickup (water spout)
`eSubType == INTERACT_PICKUP (3)` -> `spawnParticles(1, -1, sprite)`,
`canvas->turnEntityIntoWaterSpout(this)`, `return b` (`:383-386`).

Callers of `Entity::pain` on interactives: `explodeOnMonster` targetType 10
(`src/Combat.cpp:933-936`) and `hurtEntityAt` last arm (`src/Combat.cpp:1186-1190`).

### C12 — `src/Entity.cpp:735`, `:743` `Entity::aiWeaponForTarget` — Vios teleport
Trigger: `def->eSubType == 15` (BOSS_VIOS, `src/Enums.h:76`) and `param <= 0`
(`src/Entity.cpp:716-717`); `param` reloaded to `nextInt() % 3 + 3`. Up to 30 tries (shared
counter `n` between outer and inner loop, `:716-724`) pick a random offset in
[-4,4] tiles; `trace(..., 15535, 2)` must hit nothing (`:729-730`). Then:
`spawnParticles(7, -1, sprite)` at the OLD position (`:735`),
`playSound(1123, 0, 1, 0)` (`:736`), unlink, move sprite to (n6, n7,
getHeight+32), link, relinkSprite (`:737-742`), `spawnParticles(7, -1, sprite)`
at the NEW position (`:743`). Otherwise `--param` (`:749`).

### C13 — `src/Entity.cpp:1368` `Entity::resurrect` — monster revived
Last statement of `resurrect`: `spawnParticles(1, -161512, sprite)` (orange
0xFFFD8918) after repositioning/relinking (`:1345-1368`). No sound. Callers:
- `Entity::raiseTarget` (`src/Entity.cpp:1334`) <- `findRaiseTarget`
  (`:1312`) <- `aiCalcArchVileGoal` (`:569`) — arch-vile raise.
- `Game::prepareMonsters` (`src/Game.cpp:718`), called from
  `loadWorldState` (`src/Game.cpp:1900`) — re-entering a completed level
  revives some corpses. Runs AFTER the load clear (`src/LoadingManager.cpp:665`
  precedes `loadWorldState` at `:668`), so these bursts survive into the level.
- `EV_RESPAWN_MONSTER` (`src/ScriptThread.cpp:1212-1222`).
- `Game::raiseCorpses` (`src/Game.cpp:3570`) — dead code.

### C14 — `src/Game.cpp:3578` `Game::raiseCorpses` (DEAD)
Per raised corpse (max 4): resurrect, activate, anim 241 scaling 64->96, then
`spawnParticles(1, -3355444, calcPosition[0], calcPosition[1], getHeight(...) + 48)`
(`:3568-3578`). No caller in `src/`.

### C15 — `src/ScriptThread.cpp:911-919` `ScriptThread::run` — EV_MONSTER_PARTICLES (30)
Operands: `u8 sprite`. `sEnt = mapSprites[S_ENT + sprite]`; if `!= -1`,
`spawnMonsterBlood(&entities[sEnt], false)`.

### C16 — `src/ScriptThread.cpp:921-936` `ScriptThread::run` — EV_SPAWN_PARTICLES (31)
Operands, always 4 bytes: `u8 packed`, `u16 pos`, `u8 zb`.
- `type = packed >> 3 & 0xF`, `colorIdx = packed & 0x7`, `z = zb - 48`.
- `packed & 0x80` set (tile variant): `x = ((pos >> 11 & 0x1F) << 6) + 32`,
  `y = ((pos >> 6 & 0x1F) << 6) + 32`;
  `spawnParticles(type, levelColors[colorIdx], x, y, z + getHeight(x, y))`.
- clear (sprite variant): `spawnParticles(type, levelColors[colorIdx], pos)`
  — `pos` is a sprite index; `zb` is read and ignored.
Note bit 7 overlaps the top bit of the 4-bit type field (`>>3 & 0xF` covers
bits 3..6 only, so no overlap; bit 7 is a pure flag).

### Clears
- `src/Game.cpp:2537` `Game::skipCinematic` — `freeAllParticles()` near the end
  (after sound restore, `:2534-2537`).
- `src/LoadingManager.cpp:665` (map load, function starting `:604`
  `loadMedia`) — after `loadMapEntities` (`:657`), `clearEvents(1)`, before
  `player->levelInit()` (`:667`) and `loadWorldState` (`:668`).

## Rewrite (new_src) mapping

| Site | new_src status / hook point |
|---|---|
| C1 reflection | path not ported: `new_src/domain/game/Combat.cpp:263` (comment "buffs absent") |
| C2 monster hit blood | `new_src/domain/game/Combat.cpp:861` entityPain; hook right after it (stub comment :862-864), guard `attackerWeaponId != 14` |
| C3 corpse gib | stub `new_src/domain/game/Combat.cpp:868-869` (branch only logs; also needs hide/spawnDropItem/sound 1037) |
| C4/C5 | dead in original; new_src mirrors with log stubs `Combat.cpp:568`, `:597` — no hook needed |
| C6 thorn trail | path not ported: in-flight branch skipped `new_src/domain/game/Combat.cpp:1018-1019`; launchProjectile only types 6/7 (`:879-893`) |
| C7 item impact | path not ported (proj 13 falls to "treated as instant", `Combat.cpp:888-892`); would go beside `:1029` |
| C8 thorn impact | path not ported (same; beside `Combat.cpp:1029`) |
| C9 barricade | `new_src/domain/game/Game.cpp:680` (before sound 1038, inside `Game::painProp`) |
| C10 crate/furniture | `new_src/domain/game/Game.cpp:694` (before `db.removeEntity`, needs sub==0/2 guard) |
| C11 pickup | `new_src/domain/game/Game.cpp:687` (before `turnEntityIntoWaterSpout`) |
| C12 Vios teleport | path not ported: `new_src/domain/game/MonsterAI.cpp:501-504` |
| C13 resurrect | no `resurrect` in new_src; callers deferred: arch-vile `MonsterAI.cpp:135`, prepareMonsters `WorldState.cpp:380`, EV_RESPAWN_MONSTER `ScriptVM.cpp:1685-1691` |
| C14 | dead in original |
| C15 EV_MONSTER_PARTICLES | stub `new_src/domain/game/ScriptVM.cpp:1698-1700` |
| C16 EV_SPAWN_PARTICLES | stub `new_src/domain/game/ScriptVM.cpp:1000-1010` (comment says "bit8" — it is mask 0x80) |
| clear: skipCinematic | `new_src/core/CinematicCamera.cpp:197-215` (`skipCinematicNow`, near sound restore) |
| clear: map load | `new_src/core/LoadingManager.cpp:230` (`onEntitiesLoaded`, just before `levelInit` at :231; called from `new_src/core/GameContext.cpp:648`) |
| render slot | `new_src/core/GameContext.cpp:1409` drawFade; draw particles right after it, before the letterbox (:1420-1421) and HUD |
