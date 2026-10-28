# Wall push feedback (fire key against a wall)

Verified facts about the "bump the wall with the fire key" feedback and the
weapon lower/raise lerp it drives. Raw log:
`docs/research/2026-10-28-wall-push.md`.

## 1. Trigger (`src/PlayingInputHandler.cpp:461-487`)

Inside `ACTION_FIRE` (`src/PlayingInputHandler.cpp:189`), after loot,
`executeTile` on the facing tile (`:394-402`), the water-spout / door chain
(`:404-460`). `entity`/`dist2` are the elected facing trace entity and its
squared distance; `(n13, n14)` is the facing tile
`(destX + viewStepX) >> 6, (destY + viewStepY) >> 6` (`:395-396`).

Branch order:

1. **Secret push wall** (`:461-466`): `eType == 12` (ET_SPRITEWALL,
   `src/Enums.h:22`) AND `dist2 <= tileDistances[0]` AND
   `mapFlags[n14*32 + n13] & 0x4` -> `performDoorEvent(0, entity, 1, true)`;
   if it returns true -> `awardSecret(true)`. `return true`. Map flag 0x4
   therefore means "secret door", NOT "wall push".
2. **Wall push** (`:467-487`): `eType == 0` (ET_WORLD) or `eType == 12`
   AND `dist2 <= tileDistances[0]` (= 64*64, one tile,
   `src/Combat.cpp:42`):
   * `canvas->isZoomedIn` -> `return true` with no effect (`:468-470`).
   * Sound, flags 0, priority 3 (`:472-480`): familiar -> 1111
     `SentryBot_pain.wav`; `characterChoice == 1` -> 1073 `no_use2.wav`;
     `characterChoice` 2..3 -> 1072 `no_use.wav`; anything else -> none
     (names: `src/Sounds.h:7`, id-1000 index).
   * `combat->shiftWeapon(true)` (`:482`) - lower the view weapon.
   * `canvas->pushedWall = true; canvas->pushedTime = gameTime + 500`
     (`:483-484`).
   * `render->rockView(1000, viewX + (viewStepX>>6)*2,
     viewY + (viewStepY>>6)*2, viewZ)` (`:485`) - camera jolt 2 world units
     TOWARD the wall (viewStep is +-64 per axis, `src/Canvas.h:108`, or
     `viewCos*64>>16`, `src/PlayingInputHandler.cpp:41`).
   * `return true` (`:486`): no `advanceTurn`, no damage, no HUD message,
     no screen shake.

Re-pressing while still in contact restarts rockView, replays the sound and
pushes `pushedTime` out again; `shiftWeapon(true)` is a no-op then (guard,
§2).

## 2. `Combat::shiftWeapon(bool down)` (`src/Combat.cpp:846-855`)

```
if (down == lerpWpDown || down == weaponDown) return;
lerpingWeapon = true; lerpWpDown = down;
lerpWpStartTime = gameTime; lerpWpDur = LOWERWEAPON_TIME;   // 200 ms
```
`LOWERWEAPON_TIME = 200`, `LOWEREDWEAPON_Y = 38` (`src/Combat.h:46-47`).
Reset at level load: `lerpWpDown = weaponDown = false`
(`src/Game.cpp:677-678`); `pushedWall = false` (`src/Game.cpp:1254`).

### Consumer: `Combat::drawWeapon` (time base = `app->gameTime`)

`loweredWeaponY = 38` on the GL path (+13 = 51 on the software path,
`src/Combat.cpp:629-634`). Screen Y grows downward, so `+` lowers.

* Steady down: `punchingMonster == 0 && !lerpingWeapon && weaponDown` ->
  `scrY += 38` (`:672-674`).
* Lerp, only in the `else if` after the attack-pose branch, i.e. when the
  player's own attack anim is not driving the pose (`:769-784`):
  * down: `scrY += ((t - start) << 16) / dur * 38 >> 16` (`:770-771`)
  * up: `scrY += (65536 - ((t - start) << 16) / dur) * 38 >> 16`
    (`:773-774`)
  * once `start + dur <= gameTime`: `lerpWpStartTime = 0;
    lerpingWeapon = false; weaponDown = lerpWpDown;` and `+38` if down
    (`:776-782`). The latch lives in the draw call.
* Final `y = scrY - (wpY + sy)` (`:787`).

### Ending the push

`GameStateRunner::playingState` (`src/GameStateRunner.cpp:166-169`):
`if (pushedWall && pushedTime <= gameTime) { shiftWeapon(false);
pushedWall = false; }`. Timeline: 0-200 ms lower, 200-500 ms held at +38,
500-700 ms raise.

### Other `shiftWeapon` users (same lerp)
`checkFacingEntity` (`src/MovementController.cpp:135-156`: facing ET_WORLD
within one tile while `weaponDown` keeps it down; an NPC (eType 3) lowers;
otherwise raises), `ST_TREADMILL` enter (`src/Canvas.cpp:1147`),
`src/MiniGameManager.cpp:127`. `Player::fireWeapon` refuses to fire while
`weaponDown` or lowering, and snaps a raise to done (`src/Player.cpp:761-771`).

## 3. `Render::rockView` (`src/Render.cpp:2962-2968`, consumer `:2248-2253`)

Stores `dur`, `rockViewTime = upTimeMs`, target `x<<4, y<<4, z<<4`. Per frame
(`app->time == upTimeMs`, `src/Canvas.cpp:751`), with render-space
`viewX = (canvas.viewX << 4) + 8` (`src/Canvas.cpp:1344`):
```
if (time < rockViewTime + dur) {
  w = sinTable[((time - rockViewTime) << 16) / (dur << 8) + 256 & 0x3FF] >> 8;  // 256..0
  viewX += w * ((rockViewX - viewX) << 8) >> 16;   // same for Y, Z
}
```
`sinTable` is 16.16 over 1024 steps, so `w = 256*cos(pi/2 * t/dur)`: the
camera starts AT the target (instant jolt) and eases back over `dur`. The
target omits the `+8` render bias, so the peak offset per axis is
`32*step - 8` render units (step = viewStep>>6 in {-1,0,1}); Z dips 8
render units (0.5 world unit).
