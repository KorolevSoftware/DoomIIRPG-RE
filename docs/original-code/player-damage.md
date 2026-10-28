# Player receiving damage (original `src/`)

Curated facts about how the player takes damage, what feedback is shown and how
death is sequenced. Raw investigation: `docs/research/2026-09-22-player-damage.md`.
Related: `combat.md` §5 (damage math, player→monster), `ui.md` §4 (portrait row),
`game-flow.md` (state table, `ST_DYING` = 13).

Time units: `app->time` is milliseconds, equal to `upTimeMs` sampled at frame
start (`src/Canvas.cpp:749-751`). Angles: 1024 = full circle, `ANGLE_EAST=0`,
`ANGLE_NORTH=256`, `ANGLE_WEST=512`, `ANGLE_SOUTH=768` (`src/Enums.h:377-389`).

## 1. Damage numbers

### 1.1 Monster → player (`CombatEntity::calcDamage`, `b == true`)
`src/CombatEntity.cpp:300-378`. Base roll `dmg` = weapon STRMIN..STRMAX
(`& 0xFF`), difficulty-4 and crit/half flags as in `combat.md` §5; attacker
strength adds `strPct*dmg>>8` (×1, not ×3, for monsters, `:327-333`).
Then, for the non-familiar player (`:356-359`):

```
armorDmg = min(((171*dmg >> 8) + 1) / 2, player.armor)   // ~1/3 of dmg, capped by armor
damage   = dmg - 2*armorDmg                              // each armor point soaks 2 HP
if (weapon has mask 0x900000 && buffs[BUFF_ANTIFIRE] > 0) armorDmg = damage = 0   // :365-368
crDamage = damage - (defPct*damage >> 8),  defPct = (DEFENSE<<8)/100             // :370, :58-60
crArmorDamage = armorDmg                                                         // :374
```
Reflect buff shortcut: `if (b && buffs[BUFF_REFLECT] > 0) return crDamage = STRMAX`
(`:311-313`). Familiar (sentry bot) instead takes `dmg*7/8` (types 1,2) or
`dmg*5/8` (`:360-365`).

Per-shot results accumulate into `Combat::totalDamage` / `totalArmorDamage`
(the accumulation is the monster-attack researcher's topic).

### 1.2 Application — `Combat::explodeOnPlayer` (`src/Combat.cpp:948-1056`)
Called once the monster's shot resolves. Order:

1. `hud->damageDir = player->calcDamageDir(viewX, viewY, viewAngle(+zoomAngle if
   zoomed), attackerSpriteX, attackerSpriteY)` (`:953-958`).
2. `hud->damageTime = time + 150` if more shots remain (`animLoopCount > 0`),
   else `time + 1000` (`:959-964`). If `damageDir != 3`,
   `canvas->staleTime = damageTime + 1` (forces redraws, `:965-967`).
3. `reflect = buffs[BUFF_REFLECT] > 0 && !attacker->isBoss()` (`:968-971`).
4. If `gotHit && (totalDamage > 0 || totalArmorDamage > 0)` (`:972`):
   * `player->painEvent(attacker, true)` (`:973`);
   * attacker unlinked for the duration (`:976`, relinked `:1034`);
   * proj type 7 → `addStatusEffect(13 /*BUFF_FIRE*/, 5, 3)` (`:977-980`);
   * `player->addArmor(-totalArmorDamage)` (`:981`) — armor is removed even
     when reflect is active;
   * if `totalDamage > 0` and not reflect:
     * difficulty ≠ 1 (1 = Normal, 2 = Difficult, 4 = Nightmare — `menus.bin`
       menu 16 params, decoded): `totalDamage += (totalDamage>>1) +
       loadMapID/weapons[attackerWeapon+NUMSHOTS]`, `loadMapID` doubled on
       Nightmare (`:984-990`);
     * VIOS boss bonus `+numMallocsForVIOS*8` (parm 4) or `+8` if angry (`:991-998`);
     * `accumRoundDamage += totalDamage`; `player->pain(totalDamage, attacker,
       false)` (`:999-1001`); return immediately if health became 0 (`:1002-1004`);
     * "rock" if no knockback pending: `render->rockView(200, viewX+sx*6,
       viewY+sy*6, viewZ)` with `sx,sy = sign(view − attacker)` (`:1005-1016`);
     * attacker `eSubType 5` with weapon id 18 → `knockback(…, 1)` of the player
       (`:1017-1019`);
   * reflect: `reflectionDmg = accumRoundDamage = totalDamage`, next stage 1
     (`:1021-1025`); the monster later takes `reflectionDmg/2` (`:443-446`);
   * `totalDamage == 0` (armor-only): `damageCount = 0`, `totalDamage = 0`
     (`:1027-1030`); any `totalDamage <= 0` → message 72 "Deflected!" (`:1031-1033`).
5. Miss branch (`:1040-1055`): `damageCount = 0`, message 73 "Weapon
   Deflected!" if `crFlags & 0x100`, else message 69 "%01 missed!" unless
   `crFlags & 0x80`; finally `damageCount = -1`.

At the end of the monster's attack in `Combat::monsterSeq` the round total is
reported — message 112 "%01 did %02 damage!" (or 131 "Reflected %01 damage!")
— and `shakeTime = 0`, `damageDir = 0`, `damageTime = 0` (`src/Combat.cpp:581-593`).
So the damage overlays last `min(1000 ms, rest of the attack sequence)`.

### 1.3 `Player::pain(n, entity, showMsg)` (`src/Player.cpp:652-709`)
* `god` → return, nothing happens (`:654-656`). `god` only toggles via debug
  menu `ACTION_GODMODE` (`src/MenuSystem.cpp:3397-3399`).
* `showMsg` → message (0,112) "%01 did %02 damage!" with entity name, or (0,71)
  "%01 damage!" when `entity == nullptr` (`:657-671`). Monster hits pass
  `false` (aggregated message instead, §1.2).
* `n == 0` → return (`:672-674`).
* `noDeathFlag` clamps lethal damage to `health-1` (`:680-682`) — never set
  anywhere in `src/` (only read), effectively dead.
* Health-crossing warnings in 1/256 of max (`r = health*256/max`):
  crossing ≤26 (~10 %) → message 113 "Near Death!"; ≤78 (~30 %) → 114 "Low
  Health!"; ≤128 (50 %) → help 10 (has medkit, inv 16/17) or 11, once
  (`helpBitmask & 0xC00`) — only if the new value stays > 0 (`:683-700`).
* `addHealth(-n)` (`:701`) → `hud->repaintFlags |= 0x4`, health clamped to
  [0, max] (`src/Player.cpp:416-444`, `src/CombatEntity.cpp:72-89`).
* In `ST_AUTOMAP` → `setState(ST_PLAYING)` (`:703-705`).
* `health <= 0` → `died()` (`:706-708`).

`Player::addArmor(n)`: god blocks negatives; clamp [0,200];
`hud->repaintFlags |= 0x4` (`src/Player.cpp:1933-1946`).

### 1.4 Damage-direction (`Player::calcDamageDir`, `src/Player.cpp:576-622`)
Attacker offset quantised to one of 8 compass angles (sign of dx, dy; y grows
south), `dAng = (ang − viewAngle) & 1023`, wrapped to (−512, 512],
`dir = dAng/128 + 3` (C truncation), `dir < 0 → 7`; `dx == dy == 0 → 3`.

| dir | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| meaning (relative to view) | right-back | right | right-front | front | left-front | left | left-back | behind |

`painEvent(nullptr, …)` forces `damageDir = 3` (`src/Player.cpp:627-629`).

## 2. Feedback

### 2.1 `Player::painEvent(entity, fromMonster)` (`src/Player.cpp:624-650`)
* sound: familiar → 1111 `SentryBot_pain.wav`; `characterChoice == 1` →
  1094 `player_pain2.wav`; else 1093 `player_pain1.wav` (flags 0, priority 3)
  (`:631-641`; id → file = `Sounds::RESOURCE_FILE_NAMES[id-1000]`,
  `src/Sound.cpp:180`).
* `hud->damageCount = 1` (`:643`).
* `fromMonster == false` → `startShake(500, 2, 150)`; `true` (monster hit,
  a `[GEC]` change) → `startShake(0, 0, 150)` = vibration only, **no screen
  shake** (`:644-649`).

### 2.2 Screen shake (`src/Canvas.cpp:1000-1018`, `src/MovementController.cpp:381-389`, `src/Render.cpp:2265-2270`)
`startShake(ms, intensity, vibrateMs)`: skipped while skipping a cinematic;
if `intensity != 0`: `shakeTime = time + ms`, `shakeIntensity = 2*intensity`;
controller vibration `vibrateMs`. Each view update while `time < shakeTime`:
`shakeX, shakeY = nextByte() % (2I) − I` (I = shakeIntensity, so [−I, I−1]);
afterwards zeroed. Render applies (render space = world<<4): lateral
`viewX += (shakeX<<4)*cos(viewAngle+90°)`, `viewY += (shakeX<<4)*(−sin)`
(i.e. `shakeX` world units sideways) and `viewZ += shakeY<<4`.

| caller | ms | intensity (→ ±px units) | vibrate |
|---|---|---|---|
| painEvent, env/explosion | 500 | 2 (±4) | 150 |
| painEvent, monster hit | — | 0 (none) | 150 |
| `Player::died` | 350 | 5 (±10) | 500 |
| projectile type 4 explosion | 500 | 4 (±8) | 500 (`src/Combat.cpp:1294-1297`) |

### 2.3 Rock-back (`Render::rockView`, `src/Render.cpp:2962-2968, 2248-2253`)
Monster hits only (§1.2). Target = view pushed 6 world units per axis away
from the attacker; weight `w = sin[256 + t*256/dur] >> 8` (256 at t = 0 → 0 at
t = 200 ms): `view += w*(target − view)/256`. The camera jumps back and eases
forward over 200 ms (cosine).

### 2.4 Screen-edge vignette (`Hud::drawDamageVignette`, `src/Hud.cpp:535-583`)
Condition: `time < damageTime && damageCount >= 0 && combat->totalDamage > 0`.
Image `damage.bmp` (16×16, 8-bit, red dither fading right, magenta-keyed —
decoded from the IPA) or `damage_bot.bmp` for the familiar (`src/App.cpp:419-420`),
drawn with `renderMode 0` (opaque + colour-key). Edge mask per `damageDir`:

| dir | 1 | 2 | 3 | 4 | 5 | 0, 6, 7 |
|---|---|---|---|---|---|---|
| mask | 2 (right) | 3 (top+right) | 15 (all) | 5 (top+left) | 4 (left) | 8 (bottom) |

Strips (`W = 16`, `viewRect = {0,20,480,250}`, `src/Canvas.cpp:121-126`), each
`Graphics::fillRegion` tiling 16×16 tiles, each tile rotated about its centre
(`src/Graphics.cpp:415-452`, `src/Image.cpp:120-146`):
* bit 1 top: `(0, 20, 480, 16)`, rotateMode 1 (90°);
* bit 2 right: `(464, 20, 16, 250)`, rotateMode 4 (mirror X);
* bit 4 left: `(0, 20, 16, 250)`, rotateMode 0;
* bit 8 bottom: `(0, 256−16 = 240, 480, 16)`, rotateMode 3 (270°) — `256` is a
  hard-coded y.

### 2.5 Direction arrow (`Hud::drawEffects`, `src/Hud.cpp:508-526`)
Condition: `time < damageTime && damageCount > 0 && totalDamage > 0` and
`damageDir ∈ {0, 6, 7}` (mask `0xC1`) — only for attackers behind the player.
`Hud_Attack_Arrows.bmp` (12×36, three 12×12 frames, `src/Hud.cpp:41`),
`drawRegion(…, 0, srcY, 12, 12, x, y = 320−20−25 = 275, anchor 3 = centred)`:
dir 0 → x = 460, srcY 24; dir 6 → x = 20, srcY 12; dir 7 → x = 240, srcY 0.
Expiry (`:527-531`): once `time >= damageTime`, `damageTime = 0`,
`stopBrightenScreen()`, `stopScreenSmack()`.
Turning left/right sets `damageTime = 0` at once (`src/PlayingInputHandler.cpp:112-114`).

### 2.6 View-weapon red tint (`Combat::drawWeapon`, `src/Combat.cpp:710-712`)
Same condition as the vignette (not in cinematic weapon mode `b2`):
`renderFlags |= RENDER_FLAG_BRIGHTREDSHIFT (256)`.
* GL path: RENDER_NORMAL modulates with `glColor4f(1, 0.5, 0.5, 1)`
  (`src/GLES.cpp:625-631`).
* Software path: palette add of RGB565(128,0,0) = `0x8000` with per-channel
  saturation (`src/Render.cpp:1942-1960`).

### 2.7 Things that do NOT exist
* No full-screen red flash for the player (only vignette + weapon tint);
  `brightenScreen`/`smackScreen` are used for the familiar death only
  (`src/Canvas.cpp:1136-1139`).
* No hurt portrait: the face row is `max(0, 4 − 5*health/maxHealth)`, recomputed
  every draw (`src/Hud.cpp:695-713`, `ui.md` §4). `hud->playerStartHealth` is
  written (`src/Player.cpp:442`) but never read.
* Env damage (fire/spikes) and `hurtEntityAt` do not set `damageTime`, so they
  show no vignette/arrow/tint — only shake + sound + message.

## 3. Death

### 3.1 `Player::died()` (`src/Player.cpp:711-733`)
Re-entry guard on `ST_DYING`; `currentLevelDeaths++`, `totalDeaths++`; sound
by hero: 1 → 1092 `player_death3.wav`, 2 → 1091 `player_death2.wav`,
3 → 1090 `player_death1.wav`; `startShake(350, 5, 500)`; health = 0;
`setState(ST_DYING)`; `game->combatMonsters = nullptr`.
Also reached from `playingState` when health ≤ 0 (`src/GameStateRunner.cpp:170-173`).
While in combat, `combatState` returns early once state is dying
(`src/GameStateRunner.cpp:40-46`; its `undoAttack` loop is empty because
`combatMonsters` was nulled).

### 3.2 Entering `ST_DYING` (`src/Canvas.cpp:1113-1129`)
`stateVars[0..8] = 0` (`:1024-1026`); `hud->repaintFlags = 47`; REPAINT_HUD;
sniper zoom cancelled (angle snapped to a multiple of 256); soft keys cleared;
`deathTime = time`; `destPitch = 64`; queued help messages dropped.

### 3.3 `GameStateRunner::dyingState` (`src/GameStateRunner.cpp:257-281`)
Every tick `hud->repaintFlags = 32` — bit 0x20 has no live path in `Hud::draw`
(`src/Hud.cpp:745-748` commented out), so after the first frame the HUD
(top bar, bottom bar, vignette, arrow) is not painted. The view weapon is not
drawn (`src/Combat.cpp:706-708`), nor buffs / monster health bar
(`src/Hud.cpp:505, 246`).

| t since death | action |
|---|---|
| 0–750 ms | `f = (750−t)/750` (16.16): `viewZ = floorH(destX,destY) + 18 + 20f`, `viewPitch = 96 − 96f`, roll `16 − 16f`; `updateView()` (shake keeps running); `renderScene(…, fov 290)` |
| 750–2750 ms | first tick `startFade(2000, FADE_FLAG_FADEOUT=1)`; `renderScene` with roll 16 |
| ≥ 2750 ms | `baseDizzy = destDizzy = 0`; `menuSystem->setMenu(MENU_INGAME_DEAD = 51)` |

Normal eye height is floor + 36 (`src/Game.cpp:967`), so the camera starts
2 units above it and sinks to floor + 18 while pitching toward the floor
(positive pitch = down, cf. `destPitch = 64`) and rolling 16 angle units.
Fade: `alpha = 255*t/2000` black over the 480×320 canvas (`src/Render.cpp:2548-2587`),
held fully black once finished (FADEOUT branch `:2576-2579`).

Input: FIRE/key 18 would jump to the dead menu only if `stateVars[0] > 0`
(`src/InputEventController.cpp:421-425`) — never true (zeroed on entry, not
set by `dyingState`), so the sequence is not skippable.

### 3.4 Dead menu (`MENU_INGAME_DEAD` = 51, decoded from `menus.bin`)
Type 6, 5 items (strings file 3): "Try Again?" (header, flags 9), blank,
"Load Saved Game" → ACTION_LOAD (3), "Restart Level" → ACTION_RESTARTLEVEL (12),
"Main Menu" → ACTION_BACKTOMAIN (5). Actions (`src/MenuSystem.cpp:3035-3053, 3105-3111`):
* LOAD → `loadState(recentBriefSave ? 2 : 1, 3, 194)` (`src/LoadingManager.cpp:753-760`);
* RESTARTLEVEL → `loadState(2 /*brief = level-start save*/, 3, 194)`;
* BACKTOMAIN → `canvas->backToMain(false)`.
`loadState` saves config, sets the loading-bar text and enters `ST_LOADING`
(`src/LoadingManager.cpp:38-45`).

## 4. Other callers of `pain` / `painEvent` (list)
* `Entity::touched` `ET_ENV_DAMAGE`: fire (not if `BUFF_ANTIFIRE`) → painEvent +
  `pain(20, ent, true)` + burn status 13; spikes → painEvent + `pain(20, ent, true)`
  (`src/Entity.cpp:134-147`).
* `Combat::hurtEntityAt` (explosions / splash): unless proj type 5,
  `d >>= 1; a = min(d*10/100, armor); d -= a; d -= d*DEFENSE/100`; painEvent;
  knockback; if `d > 0`: `pain(d, null, true)`, `addArmor(-a)`, burn status 13
  (`src/Combat.cpp:1123-1138`).
* Script `EV_DAMAGEPLAYER` (dmg, armor, angle): painEvent, `damageTime = time+1000`,
  `damageDir = (angle + ((256 − viewAngle) >> 7) + 1) & 7` unless angle = −1,
  `totalDamage = 1`, `pain(dmg, null, true)`; negative dmg heals; `addArmor(-armor)`
  (`src/ScriptThread.cpp:725-744`).
* Script `EV_DESTROY_PLAYER`: lethal `pain(health, null, true)` + strip armor,
  ignored with noclip (`src/ScriptThread.cpp:1925-1945`).
* `Player::modifyStat(0, negative)` → `addHealth` + `painEvent(nullptr,false)`
  (`src/Player.cpp:222-231`).
* Reflect: monster takes `reflectionDmg/2` via `Entity::pain` (`src/Combat.cpp:443-446`).
* Familiar death path: `familiarDying` / `ST_BOT_DYING` (`src/Player.cpp:735-752`,
  `src/GameStateRunner.cpp:284-…`) — separate flow.
