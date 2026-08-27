# 2026-08-26 — Shaft/lift RIDE cutscenes (map00): trigger chain, key timing, why ours "rushes"

## Hypotheses under test

1. The lift ride uses a separate ride/lerp camera path, not the cinematic Maya keys.
2. Per-key durations come from somewhere other than the map key table (script operand / constant).
3. The script thread advances on its own clock during the ride (not on camera-key completion).
4. A wrong time base (fixed step vs delta) explains "far too fast".
5. The residual jerk after the intro elevator shares the ride's cause.

## Verdicts

| # | Verdict | Summary |
|---|---------|---------|
| 1 | **REFUTED** | Same cinematic path: `EV_STARTCINEMATIC` → Maya cameras **11** (down) and **12** (up), `EV_ADV_CAMERAKEY` handshake. No separate ride system. |
| 2 | **REFUTED** | Durations are the `OFS_MAYAKEY_MS` channel of the map camera key table (cam 11: 600/200/0 ms, cam 12: 800/0 ms). No script operand, no constant. |
| 3 | **REFUTED** | The thread parks on `ADV_CAMERAKEY` (`unpauseTime=-1`) and is resumed only by `MayaCamera::Snap` after N key completions. |
| 4 | **PARTIAL / not the cause** | Legacy uses a real wall-clock delta clamped to 125 ms per frame; the ride is genuinely short (**800 ms total, both directions**). Our fixed 15 ms quantum is not a speed-up. |
| 5 | **CONFIRMED (same root cause)** | Camera 7 (elevator exit) is fully tweened; the actual defect is that `new_src` addresses tween bytes with a legacy *global* rebase inside a *per-camera* blob, so every tween read falls out of range and silently returns 0. Cameras 11/12/7 lose all in-key motion and cover the whole distance in the last partial sample window (100 ms / 50 ms / 125 ms) — exactly "rushes through its keys". |

## Method

* Full CFG disassembly of map00 with the mandated tool:
  `python3 tools/disasm_map_scripts.py tmp_map00.bin -o map00.txt --verify` → `verify: 0 failure(s)`.
* Camera key/tween tables decoded with a new probe following
  `src/LoadingManager.cpp:385-397` + `src/Game.cpp:586-640`:
  `docs/research/assets/probe_lift_cams.py` (little-endian shorts,
  `src/Resource.cpp:136-141`).
* Line-level reading of `src/MayaCamera.cpp`, `src/ScriptThread.cpp`,
  `src/Canvas.cpp`, `src/MovementController.cpp`, `src/Main.cpp`.

## 1. Trigger chain (all map00, IPs bytecode-relative)

Actors: sprite **207** = upper wall-button decal, sprite **208** = lower wall button,
sprite **169** = the lift platform, script var **v23** = "platform parked at (11,19)".

```
EVT 43  tile (10,18) TRIGGER dir E   ip 4568   -- the wall button behind the blue door
 4568 EVAL v16==0            (else -> 4663 CALL 9586 = "nothing happens" helper)
 4575 PLAYSOUND 1121         (button click)
 4578 EVAL v23==1  ? platform is up : platform is away
   v23==1 (send it down / open the shaft):
     4585 ENTITY_FRAME 207 frame=1        button lit
     4589 WAIT 200ms
     4591 EVAL v48==0 -> 4598 GOTO (11,18) face=S animate   (one-time step-aside, ST_PLAYING)
     4606 WAIT 100ms; 4608 PLAYSOUND 1088 (lift move)
     4611 LERPSPRITE 169 -> (11,19) z=-32 t=400ms BLOCK      platform sinks
     4617 WAIT 200ms; 4619 PLAYSOUND 1089 (lift stop)
     4622 LERPSPRITE 169 -> (10,21) NO_TIME|DEFAULT_Z        park it off-screen
     4626 SETSTATE v23=0                                     shaft now OPEN
   v23==0 (call it back up):
     4633 ENTITY_FRAME 207 frame=0; 4637 WAIT 300ms; 4639 PLAYSOUND 1088
     4642 LERPSPRITE 169 -> (11,19) z=-32 NO_TIME
     4647 LERPSPRITE 169 -> (11,19) z=24 t=600ms BLOCK ; 4653 PLAYSOUND 1089
     4656 SETSTATE v23=1
```

```
EVT 61  tile (11,19) ENTER all dirs  ip 4667   -- RIDE DOWN (shaft open)
 4667 EVAL v23==0                   ; iffalse -> 4697 RETURN
 4674 STARTCINEMATIC camera=11
 4676 GOTO tile(11,25) face=15(keep) anim=0    ; INSTANT teleport (bit14 clear)
 4679 ADV_CAMERAKEY keys=3          ; thread parks here
 4681 LERPSPRITE 169 -> (11,19) z=24 NO_TIME   ; platform back up top
 4686 ENTITY_FRAME 207 frame=0
 4690 SETSTATE v23=1
 4697 RETURN
```

```
EVT 129 tile (11,26) TRIGGER dir N   ip 4707   -- RIDE UP (bottom button)
 4707 EVAL v16==0                   ; iffalse -> 4764 CALL 9586
 4714 PLAYSOUND 1121 ; 4717 ENTITY_FRAME 208 frame=1 ; 4721 WAIT 200ms
 4723 GOTO tile(11,25) face=E ANIMATE          ; real walk, still ST_PLAYING
 4726 MARKTILE (11,19) ; 4729 ADVANCETURN ; 4730 PLAYSOUND 1088
 4733 STARTCINEMATIC camera=12
 4735 GOTO tile(11,19) face=E anim=0           ; INSTANT teleport
 4738 LERPSPRITE 169 -> (11,19) NO_TIME|DEFAULT_Z
 4742 LERPSPRITE 169 -> (11,19) z=24 t=600ms ASYNC
 4748 ADV_CAMERAKEY keys=2          ; thread parks here
 4750 PLAYSOUND 1089 ; 4753 ENTITY_FRAME 208 frame=0 ; 4757 SETSTATE v23=1
 4767 RETURN
```

Opcode semantics: `EV_STARTCINEMATIC`=5 (`src/ScriptThread.cpp:400-411`),
`EV_ADV_CAMERAKEY`=18 (`src/ScriptThread.cpp:690-702`), `EV_GOTO`=15 instant branch
(`src/ScriptThread.cpp:634-655`). Camera system = the ordinary Maya cinematic camera
(`src/MayaCamera.cpp`), no ride-specific code path exists anywhere in `src/`.

## 2. Ground-truth key data (probe_lift_cams.py, tmp_map00.bin)

```
== camera 11 (RIDE DOWN): numKeys=3 sampleRate=125 keyOffset=79 tweenCounts=[0,0,4,0,0,0]
  key0: x=736 y=1248 z=484 pitch=0 yaw=-2 roll=0 ms=600 estNumTweens=4
        tweenIdx=[-1,-1,171,-1,-1,-1]   (raw file idx 0, rebased +171)
        tweens[Z] = [-23, -34, -37, -32]
  key1: x=736 y=1248 z=342 pitch=0 yaw=-2 roll=0 ms=200 (NO tweens -> static hold)
  key2: x=736 y=1248 z=352 pitch=0 yaw=-2 roll=0 ms=0   (instant -> completes)

== camera 12 (RIDE UP): numKeys=2 sampleRate=125 keyOffset=82 tweenCounts=[0,0,6,0,0,0]
  key0: x=736 y=1248 z=352 pitch=0 yaw=-2 roll=0 ms=800 estNumTweens=6
        tweenIdx=[-1,-1,175,-1,-1,-1]   (raw file idx 0, rebased +175)
        tweens[Z] = [14, 20, 25, 24, 23, 17]
  key1: x=736 y=1248 z=480 pitch=0 yaw=-2 roll=0 ms=0
```

(736,1248) is the centre of tile (11,19) — the shaft tile. `yaw=-2` = inherit the
player's yaw for the whole ride (both cameras' next key is also −2, so the
`inherit*` glide block at `src/MayaCamera.cpp:133-148` never overrides it).
Only Z moves. Totals: **down 800 ms (600 move + 200 hold), up 800 ms**.

Resulting exact piecewise-linear Z curves (key-space units; render pose = value<<4):

| camera 11 key0 | t (ms) | z |
|---|---|---|
| seg 0 | 0 → 125 | 484 → 461 |
| seg 1 | 125 → 250 | 461 → 427 |
| seg 2 | 250 → 375 | 427 → 390 |
| seg 3 | 375 → 500 | 390 → 358 |
| final partial (den = 600−500 = 100) | 500 → 600 | 358 → 342 |
| key1 (200 ms) | hold | 342 |

| camera 12 key0 | t (ms) | z |
|---|---|---|
| seg 0..5 | 0,125,250,375,500,625 | 352→366→386→411→435→458→475 |
| final partial (den = 800−750 = 50) | 750 → 800 | 475 → 480 |

Spot-checked against the legacy state machine: at i2=200 → z=378
(`366 + (75/125)*20`), at i2=760 → z=476 (`475 + (10/50)*5`).

## 3. Per-key timing semantics (`src/MayaCamera.cpp`)

* Duration source: `mayaCameraKeys[OFS_MAYAKEY_MS + keyOffset + key] & 0xFFFF`,
  **milliseconds** (`src/MayaCamera.cpp:72`, mask at `:59`; channel-major strides
  `src/Game.cpp:576-584`).
* Elapsed accumulation: `Update(activeCameraKey, gameTime - activeCameraTime)`
  (`src/Canvas.cpp:949-952`). `activeCameraTime` is re-stamped to `gameTime` by
  every `NextKey()` (`src/MayaCamera.cpp:36-44`) — so each key runs on a **fresh
  clock**, there is no cumulative timeline.
* FIRST key: `setupCamera` sets `activeCameraKey = -1` and writes only the static
  key0 pose (`src/ScriptThread.cpp:183-228`); `Canvas.cpp:950` skips `Update`
  entirely while the index is −1. Key0 playback starts with the first
  `ADV_CAMERAKEY` (`NextKey()` −1 → 0). For both rides that ADV is in the same
  frame as `STARTCINEMATIC`, so key0 gets the full 600/800 ms.
* LAST key: `Snap` takes its final branch when `key+1 >= numKeys`
  (`src/MayaCamera.cpp:335-337, 376-397`) — the last key's own pose is NEVER
  interpolated toward anything and a last key with `ms == 0` completes on the
  first `Update` after it becomes active (`elapsed >= 0`). Camera 11's key2
  (z=352) is therefore only a one-frame end pose; camera 12's key1 (z=480) is the
  end pose.
* Intra-key motion needs tween bytes: `hasTweens()` false → the key is a **static
  pose** for its whole duration (`src/MayaCamera.cpp:78-102`) — this is why cam 11
  key1 is a 200 ms freeze at z=342 (the "landing" beat).
* Tween machine: `estNumTweens = (keyMs-1)/sampleRate` (`:173-179`), agg base from
  `resetTweenBase` (`:243-288`), per-125 ms steps added by `updateTweenBase`
  (`:290-300`), the window after the last sample interpolates straight to the next
  key with `sampleRate = keyMs - curTweenTime` (`:124-127`).

## 4. Blocking / handshake

* `EV_ADV_CAMERAKEY n`: `keyThreadResumeCount = n; keyThread = this; NextKey();
  unpauseTime = -1; return 2` (`src/ScriptThread.cpp:690-702`) — the thread is
  parked, not polled.
* Each key boundary: `Update` sees `elapsed >= keyMs` and, **only if
  `keyThreadResumeCount > 0`**, calls `Snap(key)` (`src/MayaCamera.cpp:72-77`).
  `Snap` poses the NEXT key statically, decrements the count and either resumes the
  thread (count hits 0 → `return`, WITHOUT `NextKey`) or auto-advances one key
  (`src/MayaCamera.cpp:359-370`).
* Full ride-down trace: ADV(3) → NextKey→key0; 600 ms → Snap(0): count 3→2, NextKey→key1;
  200 ms → Snap(1): count 2→1, NextKey→key2; key2 ms=0 → Snap(2) hits the final
  branch → `complete`, ST_PLAYING, `keyThread->run()` resumes the tail (4681…).
  Ride-up: ADV(2) → key0; 800 ms → Snap(0): 2→1, NextKey→key1; ms=0 → final branch.
* No `EV_WAIT` gates either ride during the cinematic (the only WAITs are before
  `STARTCINEMATIC` in EVT 129 / inside EVT 43). `evWait` blocking-input flag is not
  used here (`src/ScriptThread.cpp:120-137`).
* Neither ride is skippable: `cinUnpauseTime = gameTime + 1000`
  (`src/ScriptThread.cpp:185`) but the ride ends after 800 ms, so the Skip soft key
  (`src/Canvas.cpp:954-957`) and the skip input path
  (`src/InputEventController.cpp:416-419`) never arm.

## 5. Player suspension & what ends the ride

* Input: in `ST_CAMERA` only the skip gesture is even considered
  (`src/InputEventController.cpp:416-419`); no movement/fire handler runs.
* Player interpolation: `MovementController::updateView` returns right after
  `activeCamera->Render()` when a camera is active and the state is not
  `ST_INTER_CAMERA` (`src/MovementController.cpp:390-395`) — the whole
  `viewX→destX / viewAngle→destAngle / viewZ→destZ` glide block (`:402-481`) is
  **skipped**, so an animated GOTO cannot progress during `ST_CAMERA` (both rides
  use instant GOTOs; the ascend's animated walk happens before the cinematic).
* Consequence worth porting carefully: the instant GOTO sets
  `game->gotoTriggered = true` (`src/ScriptThread.cpp:653`) but the block that
  consumes it lives at `src/MovementController.cpp:503-509`, **after** the
  camera early-return — so the destination tile's ENTER events fire only on the
  first `ST_PLAYING` frame after the cinematic. This matters: the ascend
  teleports the player onto (11,19), where EVT 61 (ride-down!) sits as an ENTER
  event. By the time the deferred events run, the resumed tail has already
  executed `SETSTATE v23=1` (@4757), so EVT 61's `EVAL v23==0` fails. Firing
  those events immediately would recursively start camera 11 mid-ride.
  Note the instant GOTO with a real facing nibble does call
  `finishRotation(true)` synchronously (`src/ScriptThread.cpp:641`), which runs
  the type-8 FACE events at the destination — none exist on (11,19).
* End of ride = `Snap` final branch (`src/MayaCamera.cpp:376-397`):
  `complete=true`, `skippingCinematic=false`, `cinUnpauseTime=0`,
  `activeCameraView=false`, viewport reset, `setState(ST_PLAYING)`,
  `updateFacingEntity=true`, parked thread resumed, `cameraThread=nullptr`,
  `viewPitch=destPitch=0`, `startRotation(true)`.

## 6. Frame-time source (no clamping inside the camera)

* `Main.cpp` measures a real delta per rendered frame and clamps it to 125 ms:
  `if (passedTime >= 125) passedTime = 125;` then `DoLoop(passedTime)`
  (`src/Main.cpp:128-138` (clamp at :132-134)); `DoLoop` runs one `canvas->run()` and does
  `upTimeMs += time` (`src/CAppContainer.cpp:55-61` (`+=` at :60)).
* `Canvas::run` copies `app->time = upTimeMs` and advances
  `gameTime += app->time - app->lastTime` only when `!pauseGameTime && state != ST_MENU`
  (`src/Canvas.cpp:749-770` (`gameTime +=` at :770)).
* The camera itself does no clamping and no fixed step: it just reads
  `gameTime - activeCameraTime` (`src/Canvas.cpp:951`). Dialogs pause the camera
  clock by flipping `activeCameraTime = gameTime - activeCameraTime`
  (`src/Canvas.cpp:1092-1094`).
* Both cinematic and gameplay views render through
  `Render::render(x,y,z,yaw,pitch,roll,fov)`; the cinematic uses **fov 315**
  (290 under a dialog) and raw `<<4` camera coords (`src/MayaCamera.cpp:302-312`),
  gameplay uses **fov 290** and `(viewX<<4)+8` (`src/Canvas.cpp:1344`,
  `src/MovementController.cpp:544`). The 315→290 + half-unit shift at the cut is
  authentic — expect a small pop when any cinematic ends.

## 7. Root cause of the rewrite's "rush" (cross-check into `new_src`, read-only)

Legacy stores ALL cameras' tween bytes in one array split by channel
(`ofsMayaTween[ch]`, `src/LoadingManager.cpp:389-396`) and therefore **rebases the
per-camera tween indices at load time**: `if (shiftShort >= 0) shiftShort += array[l%6]`
where `array[]` holds the cumulative per-channel byte counts of the PREVIOUS
cameras (`src/Game.cpp:612-620`; the counts are added at `:636` after the indices
are read). Fetches then use `mayaCameraTweens[ofsMayaTween[j] + indx + step]`
(`src/MayaCamera.cpp:181-195`).

Probe result: the raw indices in the file are **camera-local** (camera 11 key0 Z
raw index = 0, camera 12 key0 Z raw index = 0; cameras 0/4/5/7 likewise start at 0).

`new_src` keeps a **per-camera** blob (`new_src/domain/world/MapParser.cpp:163-177`)
with raw indices, but `MayaCamera::tweenSample` still adds the legacy global
rebase: `pos = m_chanOfs[ch] + m_basePrev[ch] + indx + step`
(`new_src/core/MayaCamera.cpp:242-250`), where `m_basePrev` is the previous
cameras' count (`new_src/core/MayaCamera.cpp:26-31`). For camera 11 that is
`0 + 171 + 0 = 171` against a 4-byte blob; the bounds guard silently returns 0.
Effect: `agg` never moves during the sampled windows, and the whole distance is
covered by `getKeyOfs` in the final partial window — 142 units in **100 ms** for
camera 11 and 128 units in **50 ms** for camera 12. That is the reported
"rushes through its keys". Camera 7 (elevator exit, `basePrev = [233,136,171,178,122,35]`
vs a 20-byte blob) loses its X/Y/YAW tweens the same way → the residual jerk.
Camera 0 is the only camera with `basePrev == 0`, i.e. accidentally correct — which
is consistent with the boot chain looking acceptable.

Fix direction (for the architect/coder): drop `m_basePrev` from the address
computation (`pos = m_chanOfs[ch] + indx + step`), or keep the legacy layout by
rebasing indices at parse time into a single global per-channel array. Add a hard
assert instead of the silent `return 0` so a future addressing error is loud.

## Open questions

* `estNumTweens` returns 0 for the map's very last global key
  (`src/MayaCamera.cpp:173-176`) — `new_src` mirrors this with
  `totalMayaCameraKeys`; not exercised by cams 11/12 but worth a regression note.
* Sounds 1088/1089 (lift move/stop) are played by the button scripts, not by the
  ride events; the descend (EVT 61) plays no sound at all — check this against the
  rewrite's audio layer once it exists.
