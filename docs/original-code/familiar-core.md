# Familiar (sentry-bot control): state and lifecycle (original RE port)

Scope: state fields, deploy, return, death, self-destruct, discard/pick-up, save/load, script vars.
Rendering, HUD, combat AI, movement details and input mapping are in other docs (see
`sentry-bot-minigame.md` §8 for the overview). Every claim cites `src/`. Raw log:
`docs/research/2026-10-30-familiar-core.md`.

Terms: "bot weapon" = weapon 3..6 (`WP_SENTRY_BOT_MASK = 120`, `src/Player.cpp:2485-2487`);
"body" = the stand-in entity left at the deploy spot; `save*` = `canvas->saveX/Y/Z/Angle/Pitch`.

## 1. State fields (`src/Player.h:45-117`)

| Field | Use |
|---|---|
| `isFamiliar` | true while the player controls a bot. Read by ~20 sites (combat, HUD, movement, render, input). |
| `familiarType` | 0 none, 1 shooting (w3), 2 exploding (w4), 3 red shooting (w5), 4 red exploding (w6) (`src/Player.cpp:2401-2423`). Always set through `setFamiliarType`, which calls `calcViewMode` → `render->postProcessMode` 0 / 1 (type 1,2) / 2 (type 3,4) (`:2489-2518`). |
| `attemptingToSelfDestructFamiliar` | true while the "Self-destruct?" dialog is open (`:2442`), consumed in `DialogSystem` (`src/DialogSystem.cpp:564-565`). |
| `ammo[7]` | bot health (0..100). `getHealth()` returns it while familiar (`src/Player.cpp:220`). Survives a normal return; set to 100 on gaining a new bot (`:983`) or to the stored value on picking up a dropped bot (`src/Entity.cpp:243-251`). |
| `weaponsCopy`, `ammoCopy[9]`, `inventoryCopy[26]` | the bot's pickup bag. Zeroed at deploy (`src/Player.cpp:2105-2111`). While familiar, `give()` with `b2=false` writes here instead of the real arrays (`:995-996, 1010, 1036`). Never cleared on return (cleared on next deploy). **Shared** with target-practice / VIOS inventory stripping (`:2665-2698`, `restoreInventory :2706-2760`) which use them as a backup of the real inventory. |
| `goldCopy` | declared only (`src/Player.h:51`), no use in `src/`. |
| `currentWeaponCopy` | not familiar: pick-up weapon 14 / target practice / VIOS (`src/PlayingInputHandler.cpp:417`, `src/Combat.cpp:413`, `src/Player.cpp:2695,2711,2740,2758`). |
| `buffs/buffsCopy[30]`, `numbuffs/numbuffsCopy`, `statusEffects/statusEffectsCopy[54]`, `numStatusEffects/…Copy` | swapped wholesale by `swapStatusEffects` (`:2185-2206`) at deploy and at every unset; the copy holds the *inactive* side. The bot starts with a cleared set (`clearOutFamiliarsStatusEffects`, `:2172-2183`, called before the swap at `:2112-2113`). Layout: `statusEffects[0..17]` turns, `[18..35]` amount, `[36..53]` stack count (save order `:1321-1323`). |
| `playerEntityCopyIndex` | index in `game->entities` of the body; -1 when none (`:521, 2164`). |
| `tookBotsInventory` | result of `stealFamiliarsInventory()` (`:2283`). Written only, never read. |
| `botReturnedDueToMonster` | set true in `forceFamiliarReturnDueToMonster` (`:2384`), reset in `reset()` (`:507`). Never read. |
| `unsetFamiliarOnceOutOfCinematic` | deferred forced return when a monster reaches the body during `ST_CAMERA` (`src/Game.cpp:3220-3221`); executed at camera end (`src/MayaCamera.cpp:386-388`). |
| `noFamiliarRemains` | set by `EV_DESTROY_PLAYER` with arg 0 while familiar (`src/ScriptThread.cpp:1942-1943`); makes `familiarDied` skip corpse/loot (`src/Player.cpp:2246-2251`). |
| `canvas->familiarSelfDestructed`, `familiarDeathTime`, `selfDestructScreenShakeStarted` | `ST_BOT_DYING` (= 25, `src/Canvas.h:92`) bookkeeping (`src/Canvas.cpp:1130-1142`). |

`Player::reset()` clears `isFamiliar`, type 0, self-destruct flag, both monster flags, `playerEntityCopyIndex=-1`,
`save*` and zeroes `buffsCopy`/`statusEffectsCopy` (`src/Player.cpp:501-554`). It does **not** zero
`weaponsCopy/ammoCopy/inventoryCopy`.

## 2. Deploy

Trigger: FIRE (no usable entity in front) with a bot weapon selected and not familiar →
`attemptToDeploySentryBot()` (`src/PlayingInputHandler.cpp:493-495`).

`attemptToDeploySentryBot` (`src/Player.cpp:2387-2425`):
1. Walk `game->activeMonsters`; if any has `distFrom(destX,destY) <= combat->tileDistances[0]` and
   `aiIsAttackValid()` → message 219 ("You are under attack!|Sentry bot cannot be deployed."), abort (`:2390-2399`).
2. Tile in front (`(viewY+viewStepY)>>6`, `(viewX+viewStepX)>>6`) has `mapFlags & 0x10` → message 217, abort (`:2400-2403`).
3. type = `ce->weapon` 4→2, 5→3, 6→4, else 1 (`:2404-2423`); `setFamiliar(type)`.

No occupancy test here: occupancy is tested by the actual move inside `setFamiliar`.

`setFamiliar(type)` (`src/Player.cpp:2079-2126`), in order:
1. Return silently if `canvas->state == ST_AUTOMAP`.
2. `scriptStateVars[16] = 1`; `save* = view*` (X,Y,Z,Angle,Pitch); `isFamiliar = true`; `setFamiliarType(type)`;
   `updateScriptVars()`.
3. `canvas->attemptMove(viewX+viewStepX, viewY+viewStepY)` — a normal one-tile step (tile events of the
   current tile run, trace, collision; `src/MovementController.cpp:312-358`). On failure: v16 = 0,
   `isFamiliar=false`, type 0, `updateScriptVars`, message 190 ("No room to deploy, step back."), return.
   The camera therefore *animates* one tile forward; the bot occupies the tile in front.
4. `render->savePlayerFog()` (stores tinyGL fog min/range/color, `src/Render.cpp:1783-1789`); `startFade(400, 2)`.
5. Body: `game->spawnPlayerEntityCopy(saveX, saveY)` (`src/Game.cpp:2725-2768`): a free drop entity
   (`getFreeDropEnt`, ring of 16), def = `lookup(tile)`, name, `info = (info&0xFFFF)|0x400000`, sprite at
   z = floor+32, scale 64, rendermode 0, `param = 0`, linked at the tile. Tile/name by `characterChoice`:
   1 → 68 `TILENUM_NPC_MAJOR` / name 224; 3 → 66 `TILENUM_NPC_RILEY_OCONNOR` / 225; 2 → 72 `TILENUM_NPC_SARGE` / 226;
   default 72 / 224 (`src/Enums.h:709-713`). Its index → `playerEntityCopyIndex`.
6. `weaponsCopy = 0`, `ammoCopy[] = 0`, `inventoryCopy[] = 0`.
7. `clearOutFamiliarsStatusEffects()` then `swapStatusEffects()`: player effects/buffs go to the copies, the bot
   gets an empty set.
8. Help 12 (type 1/3) or 13 (type 2/4); if no help shown, `drawPlayingSoftKeys()`. Sound 1108.

What is NOT swapped: `ammo[]`, `inventory[]`, `weapons`, `ce` (stats, armor, health) stay the player's.
The bot uses `ammo[7]` as health; nothing checks `ammo[7] > 0` before deploying.
While familiar, `selectWeapon` is a no-op (`src/Player.cpp:135-137`), `addStatusEffect` refuses (`:1565-1567`),
env damage (fire/spikes) is ignored (`src/Entity.cpp:135`).

## 3. Return to body

`familiarReturnsToPlayer(b)` (`src/Player.cpp:2278-2284`): sound 1112, `unsetFamiliar(b)`,
`tookBotsInventory = stealFamiliarsInventory()`.

Triggers (all call `familiarReturnsToPlayer`):
| Trigger | b | Site |
|---|---|---|
| Bot steps onto the body tile (trace hits `entities[playerEntityCopyIndex]` at `save` X/Y) | true | `src/MovementController.cpp:333-336` |
| `ACTION_BOT_DISCARD` (key B) while familiar, not automap | false | `src/PlayingInputHandler.cpp:77-80` |
| In-game menu item 355/356, action 33 `ACTION_RETURN_TO_PLAYER` | false | `src/MenuSystem.cpp:1551-1557, 3256-3259` |
| `EV_FORCE_BOT_RETURN` (opcode 81) if `isFamiliar` | false | `src/ScriptThread.cpp:1744-1750` |
| Monster ends a lerp step next to the body | false (via `forceFamiliarReturnDueToMonster`) | `src/Game.cpp:3215-3226` |
| Same during `ST_CAMERA`: deferred to camera end | false | `src/Game.cpp:3220-3221`, `src/MayaCamera.cpp:386-388` |
| `equipForLevel` (new-game/level-skip equipping) | false | `src/Player.cpp:1697-1699` |

Monster rule (`Game::freeLerpSprite`, `src/Game.cpp:3215-3226`): entity has `monster`, `isFamiliar`,
`distFrom(saveX,saveY) <= tileDistances[0]` and `(pos.x == saveX) XOR (pos.y == saveY)` (orthogonally aligned,
not on the tile itself).

`unsetFamiliar(b)` (`src/Player.cpp:2128-2170`):
1. `hud->stopBrightenScreen()`, `stopScreenSmack()`.
2. Angle: `saveAngle`, but if `b` the angle faces from the bot toward the body (saveX>viewX → WEST, < → EAST,
   saveY>viewY → NORTH, < → SOUTH). `viewAngle = destAngle = angle`; `viewPitch = destPitch = savePitch`.
3. If `!b`: `dest/view X,Y,Z = save X,Y,Z` (instant teleport back). If `b`, position is not touched: the
   ongoing `attemptMove` then sets `destX/Y` to the body tile, so the camera walks into the body
   (`src/MovementController.cpp:340-345`).
4. `swapStatusEffects()` (player effects back; the bot's go to the copies).
5. If `!b`: `render->loadPlayerFog()`. (With `b` the fog is not restored.)
6. `startFade(400, 2)`; `removeEntity(entities[playerEntityCopyIndex])`; index = -1; `unlink`,
   `finishRotation(true)`, `relink`.
7. `isFamiliar = false`; return old type; `setFamiliarType(0)`; `updateScriptVars()`; `drawPlayingSoftKeys()`.

No weapon re-select happens: `ce->weapon` is still the bot weapon (the bot weapon is kept on a normal return).

Loot transfer `stealFamiliarsInventory` (`src/Player.cpp:2286-2311`), after `isFamiliar=false`, so `give` writes
the real arrays: every non-zero `inventoryCopy[i]` → `give(0,i,n,true)` (item 13 converts to `ammo[3] += 20*total`,
`:1023-1024`; caps 999 / 9999 for item 24); every non-zero `ammoCopy[j]` (j 0..8) → `give(2,j,n,true)` (cap 100,
slot 6 cap 5); every bit k<15 of `weaponsCopy` → `give(1,k,1,true)` (may auto-select a new weapon,
`:1004-1006`). No message beyond `give`'s silent mode. Copies are not cleared.

## 4. Bot damage and death

Damage to the player while familiar (`CombatEntity`, `src/CombatEntity.cpp:357-366`): no armor absorption;
`damage = dmg*7/8` (type 1/2) or `dmg*5/8` (type 3/4), then the player's DEFENSE percent still applies
(`:371`). `Player::pain` while familiar: no low-health messages, no `noDeathFlag` clamp, `addHealth(-n)`
(`src/Player.cpp:675-677`). `addHealth` on the bot: `ammo[7] = max(0, ammo[7]+i)`, refuses positive only when
exactly 100 (no upper clamp, `:422-441`). `painEvent` plays 1111 instead of the voice (`:631-633`).

Death detection: `ammo[7] <= 0` in `playingState` (sets 0, `src/GameStateRunner.cpp:174-177`) and at the end of a
monster attack (`src/Combat.cpp:1035-1036`) → `familiarDying(false)`. `EV_DESTROY_PLAYER` deals `pain(ammo[7])`
(`src/ScriptThread.cpp:1931-1945`).

`familiarDying(selfDestruct)` (`src/Player.cpp:735-752`): ignored if already `ST_BOT_DYING`. Self-destruct:
`combat->curAttacker = nullptr`. Otherwise sound 1109, `startShake(350,5,500)`. Then
`canvas->familiarSelfDestructed = flag; setState(ST_BOT_DYING)`.

`setState(ST_BOT_DYING)` (`src/Canvas.cpp:1130-1142`): hud repaint 47, clear softkeys, `familiarDeathTime=time`,
`selfDestructScreenShakeStarted=false`, `brightenScreen(100,0)`, `smackScreen(100)` unless self-destruct, `destPitch=64`.

`familiarDyingState` (`src/GameStateRunner.cpp:283-322`), t = time - familiarDeathTime:
- self-destruct: render; at t ≥ 750 once `startShake(1500-t, 5, 500)`; at t ≥ 1500 sound 1032,
  `setState(ST_PLAYING)`, `familiarDied()`.
- killed: t < 750: f = ((750-t)<<16)/750; `viewZ = floor+18+(20f>>16)`, `viewPitch = 96+(-96f>>16)`, roll `16+(-16f>>16)`
  (the bot sinks and tilts); 750 ≤ t < 1500: `startFade(750,1)` if not fading, roll 16; t ≥ 1500: `ST_PLAYING`, `familiarDied()`.

`familiarDied()` (`src/Player.cpp:2208-2253`):
1. If `curAttacker`: reset its sprite frame byte if eType 2, message 112 with attacker name and
   `accumRoundDamage` (if > 0), stop shake/damage HUD, `curAttacker->monster->flags |= 0x400`, `gsprite_clear(64)`.
2. Remember bot `viewX/viewY`; `type = unsetFamiliar(false)` (teleport back to the body, fog restored).
3. Message 192 "Your Sentry Bot has perished!".
4. Type 2/4: `explodeFamiliar(botX>>6, botY>>6, type)` (§5).
5. Weapon w = 4 (type 2), 5 (3), 6 (4), else 3.
6. If `noFamiliarRemains`: clear it; else `handleBotRemains(botX, botY, w)`.
7. `give(1, w, -1, true)`: removes the bot weapon (`isFamiliar` is false now), `selectNextWeapon()` if it was
   current (`:986-992`). `ammo[7]` remains 0.

`handleBotRemains(x, y, w)` (`src/Player.cpp:2313-2380`), from the copies (bot's bag):
- inventory 0..10 and 24 → given straight to the player (`give(0,i,n,true,true)`) with message 86 "+n name";
- inventory 13 → summed into corpse loot n5; 16 → corpse loot n4;
- inventory 11, 12, 17, 19, 20 → `spawnDropItem` at the bot spot; other indices are lost;
- ammo 1, 2, 4, 5 → `spawnDropItem`; ammo 0, 3, 6, 7, 8 lost;
- each `weaponsCopy` bit → `spawnDropItem(getWeaponTileNum(k))`;
- `spawnSentryBotCorpse(x, y, w, n4, n5)`.

`spawnSentryBotCorpse` (`src/Game.cpp:2770-2811`): drop entity, def `find(9 ET_CORPSE, 11, n7)` with n7 = 0 (w 3/4,
tile 19) or 1 (w 5/6, tile 18); name `def->name|0x400`; default loot set (sentry corpses get no default entry,
`src/Entity.cpp:2008-2011`); loot: shooting (w 3/5) ammo 1 ×10, exploding item 12 ×1; then item 16 ×n4,
item 13 ×n5. Loot word = `class<<12 | idx<<6 | count`, only count > 0, max 3 slots (`src/Entity.cpp:2298-2307`),
count is not masked (n > 63 would spill into idx). `info |= 0x1420000`, sprite frame 0xD (`|= 0xD00`), z floor+32.

## 5. Self-destruct (types 2 and 4)

FIRE while familiar type 2/4 (when no wall/door/NPC interaction applies) → `startSelfDestructDialog()`
(`src/PlayingInputHandler.cpp:489-491`). It (not in automap) sets `attemptingToSelfDestructFamiliar`, composes
string 194 "Self-destruct?" and `startDialog(nullptr, text, 12, 1, false)` (`src/Player.cpp:2439-2449`).
On close (`src/DialogSystem.cpp:564-568`): flag cleared; if `scriptStateVars[4] == 1` (yes) and not skipped →
`familiarDying(true)`. Then §4 flow. A *killed* exploding bot explodes too (step 4 depends only on type).

`explodeFamiliar(tx, ty, type)` (`src/Player.cpp:2255-2268`): `combat->attackerWeaponId = 4` (type 2) or 6;
dmg = `weapons[id*9+0]&0xFF`, max = `[id*9+1]&0xFF`, if different `dmg += nextByte() % (max-min)`;
`radiusHurtEntities(tx, ty, 0, dmg, getPlayerEnt(), nullptr)`. Runs after the teleport back to the body.

## 6. Discard and pick-up

`attemptToDiscardFamiliar(w)` (`src/Player.cpp:2427-2437`): if the player's own tile has `mapFlags & 0x20` →
message 221; else `spawnDropItem(viewX, viewY, getWeaponTileNum(w), 6, 1, w, ammo[7], false)` (item param =
current bot health) and `give(1, w, -1)`. Callers: `ACTION_BOT_DISCARD` when not familiar and the current weapon is a
bot (`src/PlayingInputHandler.cpp:81-83`); `awardSentryBot` before giving a new bot (`src/SentryBotGame.cpp:786-800`).
Not callable while familiar (B returns instead).

Pick-up (`Entity::touchedItem`, `src/Entity.cpp:238-252`): refused if `isFamiliar || hasASentryBot()`
(`hasASentryBot` = any of bits 3..6, `src/Player.cpp:2479-2483`); else `give(1, parm, 1)` (sets `ammo[7]=100`,
clears other bot bits) then `ammo[7] = param` for a dropped item (100 for a map-placed one), message 223.
Script `EV_GIVEITEM` gives bot weapons directly via `give` (`src/ScriptThread.cpp:991-994`).

## 7. Save / load and level changes

Player file (`FULLPLAYER`/`BRIEFPLAYER`, `Game::savePlayerState`, `src/Game.cpp:2155-2170`): `prevX, prevY,
saveX, saveY, saveZ` (int), `saveAngle, savePitch` (short) before `Player::saveState`. `Player::saveState`
(`src/Player.cpp:1351-1419`) writes `weaponsCopy`, `isFamiliar`, `familiarType`, `playerEntityCopyIndex`,
`ammo[9]`, `ammoCopy[9]`, `inventory[26]`, `inventoryCopy[26]`, status effects, and **only if `isFamiliar`** the
byte `numStatusEffectsCopy` plus 54 shorts when non-zero. Not saved: buffs/buffsCopy (rebuilt from
statusEffects by `translateStatusEffects`, `:1475`, called after load `:1346` and in `updateStatusEffects` `:1472`),
`numbuffsCopy`, `goldCopy`, `currentWeaponCopy`, the three bookkeeping bools, the saved fog.

Load (`src/Game.cpp:2172-2189`, `src/Player.cpp:1270-1348`): view = saved view (the bot position when deployed),
`save*` = body; `setFamiliarType` restores the tint. The body is a linked drop entity, so it is in the world
record (`Entity::getSaveHandle` keeps linked drops, `src/Entity.cpp:1796-1798`; drop branch of `loadState`
`:1740-1752` restores `def = find(eType, subType, parm)` and sets the name only if `name == -1`).
`playerEntityCopyIndex` is restored verbatim. Edge: `render->playerFog*` is not saved, so a later
`loadPlayerFog` after a load-while-deployed uses whatever is in memory (not verified further).

Level change: `EV_CHANGE_MAP` has no familiar handling (`src/ScriptThread.cpp:484-514`). Maps instead gate exits
on v16: e.g. map04 event[6] at IP 602 `EVAL v16 == 1` → `func_5560` = sound 1111 + message 87 (refuse), else the
cinematic + `CHANGE_MAP` (disasm of map04). Doors: as familiar, using a door plays 1111 + message 193
(`src/PlayingInputHandler.cpp:446-457`); `CanCloseDoor` returns false (`src/Game.cpp:1218`). Portals are disabled
(`src/Render.cpp:2702-2705`). Movement onto `mapFlags & 0x10` tiles (except the body tile) → message 222
(`src/MovementController.cpp:322-325`). Boss cinematics force the return: map05 IP 2365, map07 IPs 4755, 4832, 5439
(`FORCE_BOT_RETURN` right after a fade-out / `STARTCINEMATIC`).

## 8. Script-visible state

`updateScriptVars` (`src/Game.cpp:3461-3471`): v16 = `isFamiliar`; v1 = `ce` health (the player's, not the bot's);
v2/v3 = `viewX>>6`/`viewY>>6` (the **bot's** tile while deployed); v14 = characterChoice; v8 = inventory[24].
`setFamiliar` also writes v16 directly (`src/Player.cpp:2085, 2096`). v4 = dialog yes/no result used by the
self-destruct dialog. v16 checks in disassembly: map00 77, map01 56, map02 61, map03 50, map04 57, map05 43, map06 29,
map07 21, map08 19, map09 0 (grep count of listing lines).

## 9. new_src status (2026-10-30)

- No `isFamiliar`/`familiarType`/`inventoryCopy`/status copies in `new_src/domain/game/Player.h` (`:136` notes the
  absence); `weaponsCopy`, `ammoCopy`, `currentWeaponCopy` exist for target practice/VIOS (`:150-152`).
- `PlayerState.cpp` writes `isFamiliar=false`, type 0, index 0, zero `inventoryCopy` (`:74-85`) and skips them on read,
  discarding `statusEffectsCopy` (`:134-160`). Note: the original writes `playerEntityCopyIndex = -1` when not deployed.
- `ScriptVM.cpp:321` v16 = 0; `:1578` `EV_FORCE_BOT_RETURN` is a no-op; `:1636` `EV_DESTROY_PLAYER` ignores the bot branch.
- `PlayerActions.cpp:339-342` deploy is a logged stub; `LoadingManager.cpp:78` ignores `saveX` on read.
