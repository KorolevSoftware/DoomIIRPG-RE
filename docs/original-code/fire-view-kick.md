# Player fire: view kick (rockView) and view-weapon motion

Scope: what the original does to the camera and to the view-weapon sprite when
the player fires. Reference build = TinyGL software path; nothing below differs
between software and GLES except the weapon base anchor (noted).

## 1. View kick = `Render::rockView` (camera pushed back, cosine return)

### Trigger (once per shot)
`Combat::playerSeq`, stage 0, right after `launchProjectile()`
(`src/Combat.cpp:325-332`):

```
if ((1 << attackerWeaponId & 0x2406) == 0) {          // 0x2406 = Enums::WP_NORECOIL (src/Enums.h:162)
    int n4 = 2;
    if (attackerWeaponId == 11) n4 = 6;               // rocket launcher
    render->rockView(animTime,
        canvas->viewX - n4 * (canvas->viewStepX >> 6),
        canvas->viewY - n4 * (canvas->viewStepY >> 6),
        canvas->viewZ);
}
```

* `WP_NORECOIL = 0x2406` = bits 1, 2, 10, 13 = chainsaw, holy water pistol,
  plasma gun, soul cube (ids `src/Enums.h:136-149`). The constant is only
  used as this literal mask. Every other player weapon kicks, including the
  sentry-bot weapons 3-6, BFG (12) and weapon 14.
* Stage 0 is re-entered for each shot of a multi-shot weapon
  (`--animLoopCount > 0 ... stage = 0`, `src/Combat.cpp:387-388`), so the
  rifle kicks twice per attack and the chaingun four times; each call
  restarts the rock.
* Duration = `animTime` = `weapons[w*9+8]` (SHOTHOLD) x10 ms, x5 when
  `player->statusEffects[2] > 0` (`src/Combat.cpp:311-317`). With the
  table-2 SHOTHOLD values (see `docs/research/2026-08-26-fire-target-acquisition-corpse-tile.md`):
  rifle 500 ms, super shotgun 300, chaingun 150, scoped rifle 300,
  rocket 300, BFG 300, shooting bots 350, exploding bots 300, weapon 14 350.
* Direction: straight backwards along the facing. `viewStepX/Y` come from
  `Canvas::viewStepValues` (+-64 / 0 per axis, `src/Canvas.h:108`,
  assigned `src/MovementController.cpp:290`), so `viewStepX >> 6` is
  -1/0/+1. Amplitude = **2 world units** (rocket: **6**); one tile = 64
  units. No pitch, roll, yaw or Z component (`viewZ` passed unchanged).
  (Free-angle `viewStep = cos*64>>16` exists only on the `renderOnly` path,
  `src/PlayingInputHandler.cpp:41-42`.)

### Storage (`src/Render.cpp:2962-2968`, fields `src/Render.h:239-243`)
`rockViewDur = dur; rockViewTime = app->upTimeMs; rockViewX/Y/Z = x/y/z << 4`
(absolute target position in render units = world << 4).

### Application (`src/Render.cpp:2248-2253`, inside `Render::render`)
`viewX/Y/Z` here are render units: `Canvas::renderScene` passes
`(viewX << 4) + 8` etc. (`src/Canvas.cpp:1344`).

```
if (app->time < rockViewTime + rockViewDur) {
    int n8 = sinTable[(app->time - rockViewTime << 16) / (rockViewDur << 8) + 256 & 0x3FF] >> 8;
    viewX += n8 * (rockViewX - viewX << 8) >> 16;     // same for Y, Z
}
```

* sinTable is 1024 entries, amplitude 65536 (`viewStepX = viewCos*64 >> 16`
  gives 64, `src/PlayingInputHandler.cpp:41`). `n8` is a 0..256 weight.
* Phase index = `elapsed*256/dur + 256`: runs 256 -> 512, i.e.
  `n8 = 256*cos(pi/2 * t/dur)`. The camera **jumps instantly** to the full
  offset at t=0 and eases back to rest with a quarter cosine; no ramp-in.
* Offset in world units: `D(t) = target - view = -n4*step` (plus a constant
  -0.5 unit in X/Y/Z because the target lacks the `+8` bias) times
  `cos(pi/2 * t/dur)`.
* Clock: `app->time` == `upTimeMs` sampled at frame start
  (`src/Canvas.cpp:749-751`), real milliseconds, not gameTime.
* Order inside `render`: dizzy fov wobble -> **rock** -> camera bob ->
  screen shake (`:2265-2270`) -> familiar bob -> viewNudge (-160 along
  facing, `:2279-2282`) -> `tinyGL->setView` (`:2297`). Only the local
  render copy changes; `canvas->viewX/Y` and gameplay are untouched.
* 3D view only. `Combat::drawWeapon` takes `(shakeX, shakeY)` only
  (`src/Canvas.cpp:1349`); it never reads rockView, so the weapon sprite
  does not follow the kick.

Other rockView callers (same mechanism): wall push, 1000 ms, 2 units
**forward** (`src/PlayingInputHandler.cpp:485`); monster melee hit on the
player, 200 ms, 6 units away from the attacker per axis
(`src/Combat.cpp:1005-1015`).

### Not triggered by firing: screen shake
`Canvas::startShake` (`src/Canvas.cpp:1000-1018`) is never called from the
player fire path. The only fire-related shake is the **rocket impact**
(projectile type 4 landing, any attacker): `startShake(500, 4, 500)`
(`src/Combat.cpp:1293-1297`), i.e. 500 ms, `shakeIntensity = 8`; each
`updateView` draws `shakeX/Y = nextByte() % 16 - 8`
(`src/MovementController.cpp:381-389`), applied laterally/vertically to
the view (`src/Render.cpp:2265-2270`) AND to the weapon sprite
(`drawWeapon(shakeX, shakeY)`, `sy = -abs(sy)`, `src/Combat.cpp:709`).

## 2. The original ALSO moves the weapon sprite on fire

`Combat::drawWeapon` (`src/Combat.cpp:621-844`), attack pose `b5`
(`:732`: own player seq, `nextStage == 1`):

* Frame 1 of the shot (`!flashDone`): pose = `(wpAtkX, wpAtkY)`, muzzle
  flash flag set (`:735-743`). `flashTime = 1` (`:199`), so `flashDone`
  latches the next frame (`:742`).
* Then `n15 = (gameTime - animStartTime << 16) / animTime`; linear 16.16
  lerp atk -> idle: `wpX = (65536-n15)*atkX + n15*idleX >> 16`
  (`:745-767`), idle once `n15 >= 65536`. Clock is **gameTime**, same
  `animTime` as the rock.
* Chainsaw (1): jitter `+2/-1` px on bit 3 of n15 for the first 2/3, then
  eased return with frame 1 (`:752-761`) — chainsaw has no rock.
* Position: `x = scrX + wpX + sx`, `y = scrY - (wpY + sy)` (`:786-787`).
  Per-weapon atk/idle offsets are the `wpinfo` table (table 1), e.g. rifle
  idle (-13,88) -> attack (-8,82): sprite jumps 5 px right and 6 px down,
  then slides back (values: `docs/research/2026-08-26-hero-choice-and-weapon.md`).
* Software path differs only in the base anchor and the
  `RENDER_FLAG_SCALE_WEAPON` scaling (`:631-668`); the pose arithmetic is
  shared.

So both happen together: a sprite pose snap+lerp (in 2D) and a camera push
back (in 3D) with the same duration, for every weapon outside WP_NORECOIL.

## 3. Rewrite status (new_src)

* Sprite pose lerp is already ported verbatim:
  `new_src/core/GameContext.cpp:1186-1210` (chainsaw jitter omitted, `:1200`),
  consumed at `new_src/ui/ViewWeapon.cpp:102-103`. Keep it.
* rockView infrastructure exists: `Hud::startRockView` / `rockView()`
  (`new_src/ui/Hud.h:85-90`), applied in the camera build at
  `new_src/render/SceneRenderer.cpp:101-121` (verbatim formula, then
  `camera_.setView`). Used by wall push (`new_src/core/PlayerActions.cpp:360`)
  and monster hit (`new_src/domain/game/Combat.cpp:401`).
* Missing: the fire kick. `new_src/domain/game/Combat.cpp:736`:
  `// rockView recoil skipped (camera-rock system absent) :326-332.`
  The fix is one call there, mirroring `src/Combat.cpp:326-332`, using
  `env_.player->viewX/viewY/viewZ/viewStepX/viewStepY`
  (`new_src/domain/game/Player.h:83`) and `*env_.upTimeMs` as start time.
