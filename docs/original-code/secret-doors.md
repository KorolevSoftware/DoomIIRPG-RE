# Secret doors (push walls) and secret awards

Verified facts about the secret spritewall mechanic and `Game::awardSecret`.
Raw log: `docs/research/2026-10-28-secret-doors.md`. Trigger context (the
fire-key branch order) lives in `wall-push.md` §1.

## 1. Trigger: fire key against a secret spritewall

`src/PlayingInputHandler.cpp:461-466`, inside `ACTION_FIRE`, after the
loot / `executeTile` / water-spout / door chain (`:394-460`) and BEFORE the
wall-push branch (`:467`):

```
if (entity && entity->def->eType == 12 && dist2 <= tileDistances[0]
    && (mapFlags[n14*32 + n13] & 0x4)) {
    if (performDoorEvent(0, entity, 1, true)) awardSecret(true);
    return true;
}
```

* `(n13, n14)` = FACING tile `(destX+viewStepX)>>6, (destY+viewStepY)>>6`
  (`:395-396`), not the entity's link tile.
* Independent of the weapon (`weapon2` is not tested) and of zoom.
* NO `advanceTurn`, no wall-push feedback (the `return true` precedes it).
* Input during the animation is blocked by the generic gate
  `animatingEffects != 0` (`src/PlayingInputHandler.cpp:72`), because the
  lerp is allocated with the animating-effect flag (§2).

## 2. `performDoorEvent(n=0, entity, n2=1, b=true)` (`src/Game.cpp:1047-1157`)

Overloads: `(n, e, n2)` -> `b=false` (`:1039-1041`); `(n, e, n2, b)` ->
`scriptThread=nullptr` (`:1043-1045`).

Common prologue (`:1050-1058`): `n4` = tile number (+257 if info `0x400000`);
`b2` = entity is linked (entity info `0x100000`, set/cleared by
`linkEntity`/`unlinkEntity`, `src/Game.cpp:118,92`); `b3` = `271 <= n4 < 281`
(door family). `eSubType == 1` -> `return false` (`:1059-1061`). Secret
spritewalls are created with the def `find(12, 0)` (eSubType 0,
`src/Game.cpp:371,471-473`), so this never refuses them. The `b3` paths
(`:1062-1080`, `:1150-1152`) do not apply to spritewall tiles.

What `b=true` changes versus a plain door open (`n=0, b=false`):

| Aspect | plain open (`b=false`) | secret (`b=true`) |
|---|---|---|
| LerpSprite flag | `LS_FLAG_DOOROPEN` 0x40 (`:1088`) | `LS_FLAG_SECRET_OPEN` 0x100 (`:1085`) |
| `Game::secretActive` | untouched | `= true` (`:1084`) |
| `SPRITE_FLAG_DOORLERP` (0x80000000) at start | set (`:1089`) | NOT set in phase 1 |
| Slide | N/S wall: `dstX += 32`; E/W wall: `dstY += 32`, skipped if `def->parm & 1` (`:1110-1112`, `:1123-1125`) | N/S wall (`0x3000000`): `dstY += 32` if NORTH (`0x1000000`) else `-= 32` (`:1102-1108`); E/W wall (`0xC000000`): `dstX += 32` if WEST (`0x8000000`) else `-= 32` (`:1115-1121`). i.e. pushed 32 units (half a tile) along the wall normal, away from the viewer. `parm` ignored. |
| Scale | `dstScale = 0` (`:1143`) | unchanged: `dstScale = srcScale` (`:1095`) |
| Sound | 1030 `door_open.wav` (`:1144`) | none from performDoorEvent |
| `updatePlayerDoors` (auto-close list) | if `b3` (`:1140-1142`) | never -> never auto-closes |

Shared: `allocLerpSprite(scriptThread, sprite, true)` (animating effect,
`:1081`; `src/Game.cpp:3071-3074`), clears `SPRITE_FLAG_HIDDEN` 0x10000
(`:1082`), writes `S_SCALEFACTOR/X/Y/Z = src*` (`:1146-1149`), `startTime = gameTime`, `travelTime = 750` (`:1127-1128`),
flags `|= LS_FLAG_ENT_NORELINK | LS_FLAG_S_NORELINK` (0x10|0x20, `:1129`),
snap only when `n2 == 0`, automap, or (`n2 == 2` and culled) (`:1153-1155`)
- the player call passes `n2 = 1`, so it animates. Returns `true`.

Constants: `src/Enums.h:293-305` (LS flags), `src/Enums.h:1231-1248`
(sprite flags).

### Phase 2 (`Game::freeLerpSprite`, `src/Game.cpp:3170-3199`)

When the 750 ms phase 1 completes, `updateLerpSprite` calls
`freeLerpSprite` (`src/Game.cpp:2868-2871`), which first writes
`S_X/S_Y/S_Z/S_SCALEFACTOR = dst*` (`:3085-3091`); no relinks (NORELINK
flags). Because `SECRET_OPEN` is set and neither DOORCLOSE/DOOROPEN/
SECRET_HIDE are, it reaches `:3170` and RE-ARMS the same slot instead of
freeing it:

* flags: `SECRET_OPEN` -> `SECRET_HIDE` 0x200 (`:3171-3172`)
* `src = dst` (`:3173-3174`); `mapSpriteInfo |= SPRITE_FLAG_DOORLERP`
  (`:3175`); `S_SCALEFACTOR = (uint8)dstScale` (`:3176`), `dstScale = 0`
  (`:3177`) - note `srcScale` keeps its original value (64);
  `startTime = gameTime` (`:3178`).
* sideways slide by orientation (`:3179-3196`): EAST `dstY += 32`,
  NORTH `dstX += 32`, WEST `dstY -= 32`, SOUTH `dstX -= 32`.
* `travelTime = 750` (`:3197`), `automapDrawn = false`, `return`
  (`:3198-3199`) - before the animating-effect decrement (`:3205-3208`),
  so input stays blocked.

Rendering during phase 2: `DOORLERP` forces full size and collapses only
the WIDTH for non-door tiles: `n30 = n10*n30/65536; n14 = n10*n14/65536;
n13 += n34 - n14` (`src/Render.cpp:586-599`), where `n10` is the real
scale `S_SCALEFACTOR << 10` (`src/Render.cpp:1512`, `:427-431`). Scale
interpolates 64 -> 0 (`src/Game.cpp:2881`), so the wall slides sideways
while narrowing to nothing.

### End of phase 2 (`src/Game.cpp:3127-3135`)

`SECRET_HIDE` takes the same branch as `DOOROPEN`: `updateFacingEntity =
true`, `secretActive = false`, `automapDrawn = false`,
`unlinkEntity(entity)` (tile becomes passable / untraceable), and since
eType 12 != ET_DOOR, `mapSpriteInfo |= SPRITE_FLAG_HIDDEN` (sprite no
longer drawn). Then the slot is freed and `animatingEffects--`
(`:3205-3208`).

Total: 1500 ms, input blocked throughout. `mapFlags` is NOT written; the
secret cannot close again (no `openDoors` entry, entity unlinked + hidden;
`n=1` would be refused anyway because `b2` is false, `:1066-1068`).

Snap note: if snapped (automap), `snapLerpSprites` zeroes start/travel and
calls `updateLerpSprite` (`src/Game.cpp:2972-2976`) -> phase 1 completes at
once and phase 2 is re-armed with a fresh 750 ms.

### `secretActive` reader

`DialogSystem` help-dialog pump returns early while `secretActive`
(`src/DialogSystem.cpp:766-768`) - queued help messages wait until the
secret wall has vanished. Reset at level load (`src/Game.cpp:351`).

## 3. `Game::awardSecret(bool b)` (`src/Game.cpp:3473-3491`)

1. `hud->addMessage((short)119)` (`:3476`) -> `addMessage(0, 119, 0)`:
   text group 0, flags 0 (`src/Hud.cpp:138-156`). String 119 in group 0 =
   `"Found Se-cret!"` (hyphens are soft-hyphen markers; read from the
   shipped `strings.idx`/`stringsNN.bin`).
2. `mapSecretsFound++` (`:3477`, `uint8_t`, `src/Game.h:130`).
3. Sound, flags 0, priority 3: `b == true` (push wall) -> 1103
   `secret.wav`; `b == false` (script) -> 1020 `chime.wav`
   (`:3479-3484`; names = index id-1000 in `src/Sounds.h`).
4. If `mapSecretsFound == totalSecrets` AND bit `loadMapID-1` of
   `player->foundSecretsLevels` is clear (`:3486`):
   `showAchievementMessage(1)` + set that bit (`:3487-3488`).
   Otherwise `player->addXP(5)` (`:3490-3491`).

`Player::showAchievementMessage(1)` (`src/Player.cpp:1953-1989`): composes
string 137 (`"se-crets"`) as text arg into group-0 string 135
(`"Con-grat-u-la-tions!|You have found all of the %01 in this lev-el."`),
`canvas->enqueueHelpDialog(text, 3)`, `addXP(10)`, sound 1020.
`addXP` posts msg 103 `"Gain-ed %01 XP."` (`src/Player.cpp:264-276`).
So the last secret gives 10 XP (first time per level), every other one 5.

### Counters and readers

| Field | Set | Reset | Readers |
|---|---|---|---|
| `Game::totalSecrets` (u8) | map header byte after `mapFlagsBitmask` (`src/LoadingManager.cpp:373`) | ctor (`src/Game.cpp:58`) | `awardSecret` (`:3486`), `Player::levelGrade` (`src/Player.cpp:2030-2033`) |
| `Game::mapSecretsFound` (u8) | `awardSecret` | map unload (`src/Render.cpp:306`), ctor (`src/Game.cpp:57`) | `awardSecret`, `levelGrade` (`src/Player.cpp:2033`); saved/loaded (`src/Game.cpp:1588,1763`) |
| `Player::foundSecretsLevels` (int bitmask, bit = mapID-1) | `awardSecret` | `src/Player.cpp:557` | `calcScore` (`src/Player.cpp:396-405`); saved/loaded (`src/Player.cpp:1284,1367`) |

`levelGrade`: `max(((6*kills/total) + (6*secrets/totalSecrets)) >> 1 -
currentLevelDeaths, 1)` in 8.8 fixed point (`src/Player.cpp:2033`);
returns 0 if `totalSecrets == 0` (`:2030-2031`). Called on epilogue and
on level-change saves with the stats bit (`src/Canvas.cpp:1153`,
`src/Game.cpp:2069-2071`).

Port bug: `calcScore` reuses loop counter `i` (already 9 after the
monsters loop, `src/Player.cpp:369`) in `while (i <= 8)` (`:397`), so the
per-level secret bonus never runs and the "all secrets" +1000 is always
granted. `calcScore` has no callers in `src/`.

## 4. Other secret triggers and mapFlags 0x4

* Script op `EV_AWARDSECRET` = 50 (`src/Enums.h:452`) ->
  `awardSecret(false)` (chime) (`src/ScriptThread.cpp:1256-1259`). No
  operands. No other caller of `awardSecret` exists.
* `mapFlags` 0x4 comes ONLY from the map file: low nibble of the packed
  512-byte tile-flag table (`src/LoadingManager.cpp:559-564`). The only
  runtime writers OR/clear 0x1, 0x8, 0x10/0x20 (`EV_MARKTILE` /
  `EV_UNMARKTILE` use `(flags & 0xC) << 2`, `src/ScriptThread.cpp:1176-1179,
  1774-1778`), 0x40, 0x80 - never 0x4. The single reader of 0x4 is
  `src/PlayingInputHandler.cpp:461`.
* Walking onto a tile awards nothing by itself; scripted secrets use
  tile events that run `EV_AWARDSECRET` (e.g. map01 event 21, FACE N on
  tile (9,10)).
* Shipped data: `totalSecrets` == (#tiles with 0x4) + (#`EV_AWARDSECRET`
  ops) on every map 00-09 (see research log).

## 5. map00 secret walls

`totalSecrets = 2`, two 0x4 tiles, no `EV_AWARDSECRET`:

| 0x4 tile | sprite | pos (world) | tile | orientation | stand on / face | phase 1 | phase 2 |
|---|---|---|---|---|---|---|---|
| (13,9) | 33 | (864,632) | 326 | SOUTH | (13,10) facing N | `dstY -= 32` -> y 600 | `dstX -= 32` |
| (15,25) | 59 | (992,1608) | 340 | NORTH | (15,24) facing S | `dstY += 32` -> y 1640 | `dstX += 32` |

Both have `SPRITE_FLAG_SOLIDSIDE` (0x800000) and no def for their tile, so
they get the ET_SPRITEWALL def (`src/Game.cpp:462-473`), linked on their
own tile (no edge nudge: neither coordinate is a multiple of 64,
`src/Game.cpp:407-419`). Distance from the stand tile center to the wall
is 40 units < 64.
