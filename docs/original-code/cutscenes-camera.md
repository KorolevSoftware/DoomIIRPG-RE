# Scripted cutscenes & cameras (original `src/`)

How scripted cinematics work: game states, the `ScriptThread` opcodes that drive them,
the Maya camera key data, script-driven player movement, fades, and the map00 intro.
Every claim cites `src/<file>:<line>`; map-data claims additionally cite offsets in
`tmp_map00.bin` (parsed with a throwaway decoder following `src/LoadingManager.cpp`).

## 1. States: ST_CAMERA / ST_INTER_CAMERA / playing-with-cutscene

Constants (`src/Canvas.h`): `ST_PLAYING = 3` (:70), `ST_INTER_CAMERA = 4` (:71),
`ST_DIALOG = 8` (:75), `ST_CAMERA = 18` (:84).

Who sets them:
- `EV_STARTCINEMATIC` → `ScriptThread::setupCamera(n)` then
  `canvas->setState(ST_CAMERA)` unless already `ST_MENU`/`ST_CAMERA`
  (`src/ScriptThread.cpp:400-411`).
- `EV_START_INTERCINEMATIC` → `setupCamera(id & 0x7F)` +
  `setState(ST_INTER_CAMERA)` (`src/ScriptThread.cpp:1539-1544`). No dedicated input
  handler exists for ST_INTER_CAMERA (`src/InputEventController.cpp:416-440` has no case),
  and `MovementController::updateView` deliberately skips camera rendering in that state
  (`src/MovementController.cpp:391`, `:518`) — the world renders from the player view
  while scripts run lerp animations.
- Cinematic end: when the active camera runs past its last key,
  `MayaCamera::Snap` sets `complete=true`, `activeCameraView=false`, resets viewport and
  `setState(ST_PLAYING)` (only if state is still `ST_CAMERA`),
  clears pitch (`viewPitch=destPitch=0`) and calls `startRotation(true)`
  (`src/MayaCamera.cpp:376-397`).
- Dialog over cutscene: closing a dialog returns to `ST_INTER_CAMERA` if that was the
  pre-dialog state, else back to `ST_CAMERA` if `Game::isCameraActive()`,
  else `ST_COMBAT`/`ST_PLAYING` (`src/DialogSystem.cpp:541-554`).
- Combat can hand control back to an active cinematic:
  `setState(ST_CAMERA)` + `activeCamera->cameraThread->run()`
  (`src/GameStateRunner.cpp:31-36`).

`Canvas::setState` side effects on entry/exit:
- Entering `ST_CAMERA`: clears HUD messages/subtitles, sets
  `render->disableRenderActivate=true`, clears soft keys, and shrinks the GL viewport to
  `cinRect` (letterboxed cinematic rect; `cinRect[1]=42`) — `src/Canvas.cpp:1207-1216`,
  `src/Canvas.cpp:151-154`.
- Leaving `ST_CAMERA`: `disableRenderActivate=false`, `skippingCinematic=false`
  (`src/Canvas.cpp:1037-1040`).
- Entering `ST_DIALOG` while camera active pauses the *camera clock* by flipping
  `activeCameraTime = gameTime - activeCameraTime`; the same flip on close resumes it
  (`src/Canvas.cpp:1092-1094`, `src/DialogSystem.cpp:542`, `:546`). All camera timing is
  measured as `gameTime - activeCameraTime` (`src/MayaCamera.cpp:46-58` update arg).

Per-frame behavior in `ST_CAMERA` (`src/Canvas.cpp:949-958`):
`activeCamera->Update(activeCameraKey, gameTime - activeCameraTime)` →
`game->updateLerpSprites()` → `updateView()`; after `cinUnpauseTime` elapses the Skip
soft key appears (`setRightSoftKey(0,40)`).

Input handling per state:
- `ST_CAMERA`: only skip. Any of `ACTION_PASSTURN/AUTOMAP/FIRE` or key 18 after
  `game->cinUnpauseTime` calls `Game::skipCinematic()`
  (`src/InputEventController.cpp:416-420`). Movement/fire handlers never run.
- During cinematics scripts can also block input without a state change:
  `evWait` with thread flag bit0 sets `canvas->blockInputTime`, and
  `runInputEvents` drops all queued events until it expires
  (`src/ScriptThread.cpp:120-137`, `src/InputEventController.cpp:445-479`).
- Scripts set `skipAdvanceTurn`/`queueAdvanceTurn` so cinematics don't consume turns
  (`src/ScriptThread.cpp:409-410`, consumed in `src/Game.cpp` advanceTurn logic).
- Script threads are resumed from the main loop via
  `Game::runScriptThreads(gameTime)` each frame, but **only** when canvas state is
  `ST_PLAYING` or `ST_CAMERA` (`src/Game.cpp:3246-3267`); threads waiting on lerps/
  dialogs are instead resumed by their owners (`updateLerpSprites`, `Snap`,
  `closeDialog`).
- `handleCinematicInput(ACTION_FIRE)` re-executes the tile event under the player
  (`src/PlayingInputHandler.cpp:560-567`) — legacy path, not needed for the intro.

## 2. Script drivers (ScriptThread opcodes)

Opcode numbers: `src/Enums.h:399-481`. Bytecode is read through
`getUByteArg/getByteArg/getUShortArg/getShortArg/getIntArg`
(`src/ScriptThread.cpp:2095-2119`). **Multi-byte operands are BIG-ENDIAN**
(`getUShortArg`: `mapByteCode[IP+1]<<8 | mapByteCode[IP+2]`; same for int at :2116).
The instruction pointer auto-advances by 1 after each opcode (`run()` loop tail,
`src/ScriptThread.cpp:2040-2044`); args advance it further.

Cinematic-relevant opcodes:

| Op | Name | Encoding | Behavior |
|----|------|----------|----------|
| 5 | EV_STARTCINEMATIC | u8 camId | `setupCamera(camId)`; `activeCamera->cameraThread=this`; `setState(ST_CAMERA)`; `skipAdvanceTurn=true` (`src/ScriptThread.cpp:400-411`) |
| 18 | EV_ADV_CAMERAKEY | u8 resumeCount | If in ST_CAMERA/ST_INTER_CAMERA: `keyThreadResumeCount=arg`, `keyThread=this`, `NextKey()`, wait (`unpauseTime=-1, return 2`) (`src/ScriptThread.cpp:690-702`). The thread is resumed by `MayaCamera::Snap` after N more key completions (`src/MayaCamera.cpp:359-370`). Because `setupCamera` left `activeCameraKey = -1`, the FIRST ADV_CAMERAKEY in a cinematic lands on **key index 0** and starts it with a fresh clock — key0 always plays (its only "truncation" is zero lead-in when the ADV follows STARTCINEMATIC synchronously). Outside those states the arg is consumed with no effect (`:700-701`) |
| 12 | EV_CAMERA_STR | u16 packed, u16 ms | packed: bits0-13 stringID, bit14 showCinPlayer, bit15 title-flag. bit15=0 → subtitle (`hud->subTitleID/Time`), bit15=1 → big title (`cinTitleID/Time`), both shown for `ms` ms (`src/ScriptThread.cpp:519-543`) |
| 14 | EV_WAIT | u8 n | wait `n*100` ms via `evWait` (`src/ScriptThread.cpp:587-591`); skipped entirely while `skippingCinematic` (`:120-123`) |
| 15 | EV_GOTO | u16 packed | player teleport/walk, see §4 |
| 69 | EV_TURN_PLAYER | u8 | bits0-2 facing (=angle/128), bit3 animate; shortest-arc turn, animated version waits via `gotoThread` (`src/ScriptThread.cpp:1547-1574`) |
| 4 | EV_LERPSPRITE | u8×3 packed + [u8 dz] + [u8 time] | packed: bits14-21 sprite, bits9-13 dstX, bits4-8 dstY, bits0-3 flags. flags: 1=async, 2=block(alloc), 4=no-time-byte, 8=default-z. `dstX/Y = 32+(tile<<6)`, `dz = byte-48`, `time = byte*100` (`src/ScriptThread.cpp:344-397`, flags `src/Enums.h:285-290`) |
| 59 | EV_LERPSPRITEOFFSET | u8 spr, u8 t(*100), i32 BE | i32: bits11-21 dx, bits0-10 dy, bits22-23 flags, bits24-31 dz(+48). Offsets are absolute sprite coords (64/tile), not deltas (`src/ScriptThread.cpp:1388-1441`) |
| 61 | EV_LERPSCALE | u16 packed(spr<<4\|flags), u16 ms, u8 scale*2 | scale-only lerp (`src/ScriptThread.cpp:1453-1498`) |
| 75 | EV_LERPSPRITEPARABOLA | i32 BE, u16 ms | like LERPSPRITE + arc height byte (+48), adds `LS_FLAG_PARABOLA` (`src/ScriptThread.cpp:1653-1708`) |
| 32 | EV_FADEOP | u16 | bit15=1 → fade OUT duration `arg&0x7FFF`; bit15=0 → fade IN `arg` ms (`src/ScriptThread.cpp:938-957`) |
| 17 | EV_ENTITY_FRAME | u8 spr, u8 frame, u8 wait(*100) | force animation frame, optional wait (`src/ScriptThread.cpp:669-688`) |
| 29 | EV_SHOW_PLAYERATTACK | u8 weaponId | first-person weapon anim during cinematic (`cinematicWeapon`), drawn by `MayaCamera::Render`→`combat->drawWeapon` (`src/ScriptThread.cpp:894-908`, `src/MayaCamera.cpp:312-314`) |
| 13 | EV_DIALOG | u8 dlgId, u8 hi=flags lo=style | style 2 → queued help dialog (`enqueueHelpDialog`, keeps script running); otherwise `startDialog(..., style, flags, resumeAfterClose=true)` and the thread parks until dialog close (`src/ScriptThread.cpp:545-584`, styles rendered in `src/DialogSystem.cpp:140-214`) |
| 33 | EV_GIVEITEM | u8 a, u8 b, u8 mode | mode 0: treat a<<8\|b as sprite index, call `entity->touched()` (pickup). mode≠0: look up entity def `a`, give count `(char)b` to player (`src/ScriptThread.cpp:960-1008`) |

Blocking convention: any opcode that must pause the thread returns `n=2` from `run()`
with `unpauseTime=-1`; the owner (lerp finish, camera key Snap, dialog close, move
finish) later calls `thread->run()` again (`src/Game.cpp:3009-3013`,
`src/MayaCamera.cpp:390-394`, `src/DialogSystem.cpp:559-563`,
`src/MovementController.cpp:164-167`, `:298-302`).

## 3. Maya cameras

Data layout (per map): counts live in the map header — `totalMayaCameras` (u8),
`totalMayaCameraKeys` (u16), plus 6 per-channel tween-count shorts
(`src/LoadingManager.cpp:385-397`). Per camera (`Game::loadMayaCameras`,
`src/Game.cpp:586-640`):
1. u8 numKeys, u16 sampleRate
2. numKeys × 7 shorts: keys stored **channel-major** into a global array using
   `OFS_MAYAKEY_X/Y/Z/PITCH/YAW/ROLL/MS = k*totalKeys` strides
   (`Game::setKeyOffsets`, `src/Game.cpp:576-584`)
3. numKeys × 6 shorts tween indices (running-sum adjusted per channel)
4. 6 shorts per-channel tween counts, `0xDEADBEEF` marker, then the tween bytes (i8)

Key channel semantics (`MayaCamera::Update`, `src/MayaCamera.cpp:46-148`):
- X/Y/Z keys are in map units (64/tile); converted `<<4` before render
  (`setupCamera`, `src/ScriptThread.cpp:213-215`).
- Pitch/yaw/roll are 0..1023 angle units (1024 = full circle, yaw 512 = north etc.,
  cf. `Enums::ANGLE_*`, `src/Enums.h:377-384`).
- `OFS_MAYAKEY_MS` = key duration in ms (masked `& 0xFFFF`).
- Sentinel `-2` means "inherit the player's value at cinematic start"
  (`camPlayerX/Y/Z/Yaw/Pitch`, captured in `setupCamera`, `src/ScriptThread.cpp:192-196`;
  inheritance logic `src/MayaCamera.cpp:85-102`, `:253-287`).
- Tween indices `-1`/`-2` mark channels without tween data
  (`hasTweens`, `src/MayaCamera.cpp:161-171`).

Interpolation math (`src/MayaCamera.cpp:104-131`, `:234-241`):
- Keys are sampled at `sampleRate` ms steps; per-step deltas come from the i8 tween
  tables (`getTweenData`/`updateTweenBase`, `:181-195`, `:290-300`), accumulated into
  `aggComponents[6]`.
- Fraction inside one sample step: `t = ((elapsed - curTweenTime) << 16) / sampleRate`;
  position: `x = (agg << 20) + (t * (delta << 4)) + 32768) >> 16`;
  angles: `a = ((agg << 16) + (t * delta) + 32768) >> 16`.
- Angle deltas use shortest-arc wrap ±512 (`getAngleDifference`, `:151-159`).
- The final partial step uses the next key directly (`getKeyOfs`, `:197-232`).
- `estNumTweens = ((keyMs - 1) / sampleRate)` (`:173-179`).

Selection/lifecycle: a script picks the camera id (`EV_STARTCINEMATIC`);
`setupCamera` initializes from key 0 and stamps `cinUnpauseTime = now + 1000`
(input locked for ~1 s so a skip can't be instant-skipped)
(`src/ScriptThread.cpp:183-228`). **`setupCamera` sets `activeCameraKey = -1`**
(`src/ScriptThread.cpp:188`) and only writes the STATIC key0 pose into the camera;
while the key is −1 the ST_CAMERA tick skips `Update` entirely
(`src/Canvas.cpp:950`), so any pre-first-ADV lead-in (WAITs, blocking lerps)
displays the frozen key0 pose. Playback of key0 proper begins at the first
`EV_ADV_CAMERAKEY`, whose `NextKey()` restarts `activeCameraTime = gameTime` and
moves −1 → 0 (`src/MayaCamera.cpp:36-44`). Keys with `ms == 0` complete instantly:
the boundary test `elapsed >= 0` is true on the first Update after they become
active (`src/MayaCamera.cpp:72-77`), so cameras authored to end on an ms=0 key cut
away in the same frame the last segment starts. Rendering goes through
`activeCamera->Render()` → `render(x,y,z,yaw,pitch,roll, fov=315 (or 290 during
dialogs))` (`src/MayaCamera.cpp:302-324`). `activeCameraView && activeCamera != nullptr`
= `isCameraActive()` (`src/Game.cpp:3297-3299`).

Skip: `Game::skipCinematic()` loops {snap gamesprites/lerps; `activeCamera->Snap(key)`
until complete} while resuming the camera thread with a huge timestamp
(`attemptResume(gameTime + 0x40000000)`) so all WAITs fall through; ends subtitles,
weapon anim, shake, frees particles, forces a 500 ms fade-in
(`src/Game.cpp:2507-2544`).

Table cameras: `Game::loadTableCamera` builds a single always-looping camera
(`isTableCam=true`, `NextKey` forever) used for menus/backgrounds
(`src/Game.cpp:511-574`, `src/MayaCamera.cpp:371-373`).

### map00 elevator-exit cameras (tmp_map00.bin; decoder docs/research/assets/probe_cam_jitter.py)

All 14 cameras use sampleRate=125. The exit-from-the-docking-elevator beats:

* **cam 7** (evt[54], tile (9,19) TRIGGER, IP 2993): `WAIT500 @3248 → STARTCINEMATIC
  cam=7 @3250 → GOTO 4308 @3252 → ADV_CAMERAKEY resumes=4 @3255`. GOTO 4308 is an
  instant teleport to tile (6,20) center (416,1312) facing angle 512 with pitch/roll
  zeroed (`src/ScriptThread.cpp:602-653`) — exactly cam7's final key spot. Keys:
  key0 (544,1248,484) **yaw −2 (inherit player)** 1250 ms → key1 same pos yaw 512
  1000 ms → key2 (416,1248,484) yaw 640 500 ms → key3 (416,1312,484)
  yaw 512 **ms=0**. Total 2750 ms; opens seamlessly on the player's facing.
  Tween counts `[X 7, Y 3, Z 0, pitch 0, yaw 10, roll 0]`: key0 has NO tweens (static
  hold, yaw glided by the inherit block), key1 tweens X `[-11,-15,-19,-19,-20,-18,-16]`
  + YAW `[11,18,20,23,21,18,12]`, key2 tweens Y `[13,19,19]` + YAW `[-17,-39,-43]`.
  So this camera is fully smooth — an earlier note calling key2 an "authored data cut"
  was wrong; a jerk at its end means the port lost the tween bytes (see PITFALL below).
  The only authentic discontinuity is the fov 315→290 / `+8` coord change at the cut.
* **cam 10** (ev[56], tile (10,19) ENTER-all-dirs, IP 4223): self-disable, then
  `STARTCINEMATIC cam=10 @4226 → ADV_CAMERAKEY resumes=1 @4228` synchronously, then
  ~13 s of doorway pantomime choreography, final `ADV_CAMERAKEY resumes=1 @4487`.
  Keys: key0 (672,1248,484) yaw −2, 500 ms; key1 same pos yaw 0, ms=0. The single
  static shot holds for the whole choreography and ends instantly.
* Boot-chain first-ADV gaps: cam0 `OP5@1946→OP18@1954` and cam1 `OP5@2098→OP18@2103`
  are same-frame; cam5 `OP5@2422→first OP18@2517` has a ~1–3 s lead-in that shows the
  frozen key0 high shot. Every boot camera's last key has ms=0.

### Tween-byte addressing (PITFALL)

Per-camera tween indices in the map file are **camera-local** (verified: cameras
0/4/5/7/11/12 all start their first tweened channel at raw index 0 —
`docs/research/assets/probe_lift_cams.py`). The legacy loader keeps ONE array for the
whole map, split by channel via `ofsMayaTween[ch]` (`src/LoadingManager.cpp:389-396`),
so it **rebases the indices at load time**: `if (shiftShort >= 0) shiftShort += array[l%6]`
where `array[]` holds the cumulative per-channel byte counts of the PREVIOUS cameras
(`src/Game.cpp:612-620`; those counts are only added at `:636`, after the indices are
read — hence "previous"). The per-camera blob is written channel-major with a running
per-channel cursor (`src/Game.cpp:632-637`), and fetches use
`mayaCameraTweens[ofsMayaTween[j] + indx + step]` (`src/MayaCamera.cpp:181-195`).

Consequence for a port that keeps **per-camera** tween blobs: the cross-camera rebase
must NOT be applied — the address is `chanOfs[ch] + rawIdx + step`. Applying it makes
every fetch of a late camera fall out of range; if the fetch silently yields 0 the key
holds still and then covers its whole distance inside the final partial sample window
(`keyMs - (estNumTweens)*sampleRate` ms), which reads on screen as the camera
"rushing"/cutting. Cameras 11/12 (lift rides, 4/6 Z bytes) and 7 (elevator exit) are
the loudest victims; camera 0 is the only map00 camera with a zero rebase.

### map00 lift-ride cameras 11 / 12 (shaft behind the blue door)

The ride is NOT a separate system: same `EV_STARTCINEMATIC` + `EV_ADV_CAMERAKEY`
cinematic path, cameras parked at (736,1248) = centre of the shaft tile (11,19),
`yaw = -2` (inherit player) on every key so only Z moves. `sampleRate = 125`.

* **Trigger chain** — wall button decal sprite 207: ev[43] tile (10,18) TRIGGER dir E
  (IP 4568) toggles the platform sprite 169 via LERPSPRITE (z −32 / +24, sounds
  1088/1089) and flips script var v23 ("platform parked on (11,19)").
* **Ride down** — ev[61] tile (11,19) ENTER (IP 4667), guarded `EVAL v23==0`:
  `STARTCINEMATIC cam=11` → `GOTO (11,25) face=keep` (INSTANT, bit14 clear) →
  `ADV_CAMERAKEY keys=3`; tail after resume re-parks the platform, resets the button
  frame and `SETSTATE v23=1`.
  Keys: key0 z 484 ms=600 tweens[Z]=[-23,-34,-37,-32]; key1 z 342 ms=200 **no tweens
  (static 200 ms landing hold)**; key2 z 352 ms=0. Total **800 ms**.
* **Ride up** — ev[129] tile (11,26) TRIGGER dir N (IP 4707): click sound 1121,
  `ENTITY_FRAME 208 frame=1`, `WAIT 200`, animated `GOTO (11,25) face=E`,
  `MARKTILE`, `ADVANCETURN`, sound 1088, then `STARTCINEMATIC cam=12` →
  `GOTO (11,19) face=E` (INSTANT) → async platform lerp → `ADV_CAMERAKEY keys=2`;
  tail plays 1089, resets the button, `SETSTATE v23=1`.
  Keys: key0 z 352 ms=800 tweens[Z]=[14,20,25,24,23,17]; key1 z 480 ms=0.
  Total **800 ms**.
* Exact Z curves (key-space units; render pose = value<<4):
  cam 11 key0 `484→461→427→390→358` at 0/125/250/375/500 ms then `358→342` over the
  final 100 ms; cam 12 key0 `352→366→386→411→435→458→475` at 0..750 ms in 125 ms steps
  then `475→480` over the final 50 ms.
* Neither ride is skippable: `cinUnpauseTime = gameTime + 1000`
  (`src/ScriptThread.cpp:185`) outlives the 800 ms ride, so the Skip soft key
  (`src/Canvas.cpp:954-957`) and the skip input path
  (`src/InputEventController.cpp:416-419`) never arm.
* **Deferred destination events.** The instant `EV_GOTO` sets `gotoTriggered`
  (`src/ScriptThread.cpp:653`), but the consumer sits at
  `src/MovementController.cpp:503-509`, i.e. AFTER the camera early-return at `:390-395`
  — so the destination tile's ENTER events only fire on the first `ST_PLAYING` frame
  after the cinematic. This is load-bearing for the ride up: it teleports the player
  onto (11,19) where ev[61] (ride DOWN) is an ENTER event; by the time the deferred
  events run, the resumed tail has already set `v23=1`, so ev[61]'s guard fails.
  Firing destination events during the cinematic would recursively restart camera 11.
  (A facing nibble != 15 does run `finishRotation(true)` synchronously,
  `src/ScriptThread.cpp:641` → type-8 FACE events only; (11,19) has none.)
* Player movement/turn interpolation is frozen for the whole ride: `updateView`
  returns right after `activeCamera->Render()` (`src/MovementController.cpp:390-395`),
  skipping the glide block at `:402-481`; input in `ST_CAMERA` only ever considers the
  skip gesture (`src/InputEventController.cpp:416-419`).

### Frame-time source (no fixed step anywhere)

`Main.cpp` measures a real per-frame delta and clamps it to **125 ms**
(`src/Main.cpp:132-134`), passing it to `DoLoop`, which runs exactly one
`canvas->run()` and then `upTimeMs += time` (`src/CAppContainer.cpp:55-61`).
`Canvas::run` sets `app->time = upTimeMs` and advances
`gameTime += app->time - app->lastTime` only while `!pauseGameTime && state != ST_MENU`
(`src/Canvas.cpp:749-770`). The camera does no clamping of its own — it reads
`gameTime - activeCameraTime` (`src/Canvas.cpp:951`), and every `NextKey()` restamps
`activeCameraTime = gameTime` (`src/MayaCamera.cpp:36-44`), so each key has its own
fresh clock (no cumulative timeline). Dialogs pause it by flipping
`activeCameraTime = gameTime - activeCameraTime` (`src/Canvas.cpp:1092-1094`).

FOV/coord discontinuity at every cinematic end is authentic: cinematic renders raw
`<<4` camera coords at fov **315** (290 under a dialog, `src/MayaCamera.cpp:302-312`),
gameplay renders `(viewX<<4)+8` at fov **290** (`src/Canvas.cpp:1344`,
`src/MovementController.cpp:544`).

### map00 camera data (from tmp_map00.bin)

Header: `totalMayaCameras=14`, `totalMayaCameraKeys=86`, all `sampleRate=125`.
Camera ids used by scripts: 0..13 (e.g. `STARTCINEMATIC cam=9` @bytecode 3708,
`cam=4` @1892, `cam=2` @4931). Camera 4 (the boot fly-over, 11 keys ≈ 8.8 s):
starts at (1504,800,560) yaw 256 for 2.5 s, dollies to (1504,320,560) looking down
(pitch≈996), cuts at key5 to (1760,1088,560) yaw 768, walks in to end at
(1376,224,548) yaw 512.

## 4. Player move-by-script (EV_GOTO / EV_TURN_PLAYER)

`EV_GOTO` (u16 packed, `src/ScriptThread.cpp:593-661`):
- dstTile = (bits5-9, bits0-4); `destX/Y = tile*64+32`;
  `destZ = getHeight()+36`; face nibble bits10-13 (15 = keep angle);
  bit14 = animated; bit15 = advance-turn-after.
- Always zeroes pitch/roll (`viewPitch=destPitch=0`, `viewRoll=destRoll=0`) and
  knockback.
- Instant (bit14=0): view snaps (`viewX/Y/Z = dest…`), `viewAngle=destAngle=nibble<<7`,
  optional `advanceTurn()` if bit15, then `startRotation(false)` outside cutscenes;
  marks `gotoTriggered` so the destination-tile events fire from `updateView`
  (`src/MovementController.cpp:503-509`) — but that block sits AFTER the
  camera-active early-return at `:390-395`, so during a cinematic the destination
  ENTER events are DEFERRED to the first `ST_PLAYING` frame after it ends
  (load-bearing for the lift ride up, see §3). Finally `player->relink()`,
  `clearEvents(1)`, `updateFacingEntity=true`, repaint.
- Animated (bit14=1): shortest-arc turn setup, `zStep=(|Δz|+animFrames-1)/animFrames`,
  and if anything differs, hands control to `gotoThread=this` and waits
  (`unpauseTime=-1, return 2`). The walk itself is the standard linear glide in
  `MovementController::updateView` (±`animPos` per frame toward dest; angle ±`animAngle`;
  z ±`zStep`; pitch ±`pitchStep`) (`src/MovementController.cpp:402-481`). On arrival
  `finishMovement()` runs destination tile events and, if `gotoThread` was set and the
  angle settled, resumes the script (`src/MovementController.cpp:160-196`,
  `:510-512`).
- So "script moves the player" = plain `destX/Y/Z/destAngle` writes reused by normal
  gameplay interpolation — no fake input, no separate lerp system. View sync afterwards
  is implicit because rendering always uses `canvas->view*`.

`EV_TURN_PLAYER`: same angle math without movement; animated variant also parks on
`gotoThread` (`src/ScriptThread.cpp:1563-1571`).

## 5. Fades / transitions

Flags (`src/Render.h:84-91`): NONE=0, FADEOUT=1, FADEIN=2, CHANGEMAP=4, SHOWSTATS=8,
DENYSKIP=16, EPILOGUE=32; special-mask keeps only flag bits (`FADE_SPECIAL_FLAG_MASK=-29`,
i.e. strips FADEOUT|FADEIN). State: `fadeTime/fadeDuration/fadeFlags`
(`Render::startFade/endFade`, `src/Render.cpp:2535-2546`).

- Progress drawn in `Render::fadeScene`: `alpha = 65280*((elapsed<<16)/(duration<<8))>>16`,
  inverted for FADEOUT (`src/Render.cpp:2582-2586`). On completion: FADEIN just ends;
  FADEOUT holds black until something clears it; CHANGEMAP triggers
  `canvas->loadMap(nextMap)`; SHOWSTATS opens level stats
  (`src/Render.cpp:2553-2574`). Input is swallowed while fading with DENYSKIP
  (`src/InputEventController.cpp:305-307`).
- `EV_FADEOP` maps bit15 → OUT/IN (see §2).
- Map change: `EV_CHANGE_MAP` builds `FADE_FLAG_FADEOUT [| SHOWSTATS | CHANGEMAP]` and
  fades 1000 ms when bit7 of its first operand is set (`src/ScriptThread.cpp:484-516`).
- Cutscene skip always ends with a 500 ms FADEIN (`src/Game.cpp:2530-2532`).
- A second, HUD-level fade pair exists (`Canvas::fadeFlags`, FADE_FLAG_FADEOUT/IN only),
  driven linearly in `backPaint` (`src/Canvas.cpp:499-510`) — used e.g. by the intro
  movie (`src/IntroSequenceManager.cpp:788`).

## 6. map00 intro trace (tmp_map00.bin, read-only)

Boot chain (`src/LoadingManager.cpp:673-720`):
1. `Game::spawnPlayer()` places the player from header fields `mapSpawnIndex/mapSpawnDir`
   → map00 spawns at tile **(4,19)** facing dir 0 = EAST (`spawnPlayer`,
   `src/Game.cpp:946-971`; header bytes of tmp_map00.bin confirm spawn=(4,19) dir=0).
2. `executeStaticFunc(0)` runs the map's static function #0
   (`src/LoadingManager.cpp:693`, dispatch `src/Game.cpp:3342-3351`).
3. `executeTile(viewTile, 4081)` fires type-1 events under the player (none on (4,19)),
   `finishRotation(false)` ×2 executes type-8 "standing" events via
   `flagForFacingDir(8)` (`src/LoadingManager.cpp:703-707`,
   `src/MovementController.cpp:307`, `flagForFacingDir` `src/MovementController.cpp:214-224`).
4. `dequeueHelpDialog(true)` may immediately pop a queued help dialog; state becomes
   `ST_PLAYING` (`src/LoadingManager.cpp:705`, `:715-717`).

staticFunc[0] (@bytecode 0) does, in order:
- names NPC sprites (`NAMEENTITY spr=7 name=42`, spr10→43, spr9→44, …),
- fog tables (`SET_FOG_COLOR`, `LERP_FOG`),
- decor placement (`LERPSCALE/LERPSPRITEOFFSET`),
- chat bubbles over sprites 19 and 16 (`NPCCHAT spr=19 param=0` @64),
- one-time init guarded by `var17==0` → CALL 1942/2084/2328, `NEXTSTATE var17` (@221-233),
- tail: `EVAL var15==1` → either plain `FADEOP OUT` (reload path) or **CALL 1889**
  (new-game intro) (@236-251).

New-game intro (@1889):
```
1889 CALL_FUNC ->1260        ; SETSTATE var5=1
1892 STARTCINEMATIC cam=4    ; fly-over camera (see §3)
1894 FADEOP OUT 500ms
1897 WAIT 100ms
1899 PLAYSOUND snd=1057 vol=0 prio=5
1902 ADV_CAMERAKEY resumes=9 ; play keys 1..9, then resume
1904 DOOROP spr=96 op=0      ; open blast door mid-shot
1907 ADV_CAMERAKEY resumes=3 ; play remaining keys
1909 DOOROP spr=96 op=5      ; close door
1912 RETURN                  ; camera finishes -> MayaCamera::Snap -> ST_PLAYING
```
While it plays the canvas is in `ST_CAMERA`; the thread is kept alive by
`runScriptThreads` (`src/Game.cpp:3259-3261`) and by `Snap` between ADV_CAMERAKEYs.

First-input beat: spawn-tile type-8 event ev[38] (@3687, tile (4,19)) shows help dialog
**dlg=41** once (`EVAL var43==0` guard): `DIALOG dlg=41 style=2` → queued tutorial popup
(`enqueueHelpDialog`, `src/ScriptThread.cpp:561-572`), dequeued when playing
(`src/GameStateRunner.cpp:198-200`).

Story beats near the spawn room (all verified in bytecode):
- Talk to the sergeant: type-4 (fire-at-facing) events open NPC dialogs —
  ev[54] tile (9,19) `DIALOG dlg=21 style=1` + `SPEECHBUBBLE tex=118` (@2993-3026);
  ev[44]/ev[78] tiles (13,18)/(13,20) run a scripted approach with subtitles
  `CAMERA_STR str18/str19`, `NAMEENTITY spr=19 name=20` (@2865-2880).
- Weapon hand-out: `GIVEITEM a=<def> b=255 mode=1` inside door-opening scripts —
  @4175 (ev[60], tile (10,19), followed by `DOOROP … op=5` twice) and @4782 (ev[66],
  tile (16,19)); mode≠0 routes through `player->give` (`src/ScriptThread.cpp:981-995`).
- Auto-move: `GOTO x,y face=n+anim` (animated walk) appears throughout, e.g. @4598
  `GOTO (11,18) face=6+anim`, @4723 `GOTO (11,25) face=0+anim`, @4928
  `GOTO (17,19) face=0+anim` paired with `STARTCINEMATIC cam=2` and subtitle
  `CAMERA_STR str58` (@4942) — this is the "game moves the player while a cutscene
  camera watches" pattern; each GOTO parks the thread until the walk completes (§4).
- Tutorial popups: further `DIALOG … style=2` help dialogs (dlg=45/46/59…) and
  `MESSAGE strN hud` toast lines accompany the beats above.

Note: dialog/string ids are indexes into text table `loadMapStringID = 4+(mapID-1) = 4`
for map00 (`src/LoadingManager.cpp:310`); their English text lives in the localization
resource, not in the map file.

## 7. Cinematic letterbox: black bars, cockpit art, subtitle/Skip placement

Verified 2026-08-26 (`docs/research/2026-08-26-cinematic-letterbox.md`).
Canvas = 480x320 (`Applet::IOS_WIDTH/HEIGHT`, `src/App.h:36-37`), so
`displayRect = {0,0,480,320}` (`src/Canvas.cpp:49-52`), `screenRect = {0,0,480,320}`,
`softKeyY = 320` (`src/Canvas.cpp:100-119`), `viewRect = {0,20,480,250}` (`:123-127`),
`cinRect = {0,42,480,250}` (`:151-154`), `CAMERAVIEW_BAR_HEIGHT = 20` (`:149`).

### 7.1 The bars are two opaque black fills, NOT the cockpit art

`Hud::drawCinematicText` opens with (`src/Hud.cpp:455-456`, verbatim):

```
graphics->eraseRgn(0, 0, canvas->displayRect[2], canvas->cinRect[1]);
graphics->eraseRgn(0, canvas->cinRect[1] + canvas->cinRect[3], canvas->displayRect[2],
                   canvas->softKeyY - (canvas->cinRect[1] + canvas->cinRect[3]));
```

Substituting: **TOP bar `(0, 0, 480, 42)`** (rows 0..41) and
**BOTTOM bar `(0, 292, 480, 28)`** (rows 292..319, since 42+250 = 292 and
320-292 = 28). `eraseRgn` = `setColor(0)` + `fillRect` (`src/Graphics.cpp:250-256`)
and `Graphics::fillRect` forces `a = 1.0` (`src/Graphics.cpp:88-95`), so both are
**opaque black**, in absolute display coordinates (`fillRect` does not add
`transX/transY`; `graphClipRect` is `displayRect` because `backPaint` is entered
right after `graphics.resetScreenSpace()`, `src/Canvas.cpp:986-987`).

This is the whole letterbox. The **cockpit overlay is a different, script-gated
effect**: `Hud::drawOverlay` (`src/Hud.cpp:620-625`) runs only from
`MayaCamera::Render` under `if (app->hud->cockpitOverlayRaw)`
(`src/MayaCamera.cpp:316-318`); that flag starts false (`Hud::Hud` memsets the
object, `src/Hud.cpp:24-26`) and is only flipped by opcode 76 `EV_TOGGLE_OVERLAY`
(`this->app->hud->cockpitOverlayRaw ^= 1;`, `src/ScriptThread.cpp:1710-1713`,
id `src/Enums.h:476`). In map00 it is enabled for exactly one shot — the drop-ship
arrival camera 0: `TOGGLE_OVERLAY @1945 -> STARTCINEMATIC cam=0 @1946 ... ->
TOGGLE_OVERLAY @2062` (disassembler run over `tmp_map00.bin`). Every other
cinematic in map00 shows bars with NO cockpit art.

Overlay geometry: `cockpit.bmp` is **240x234** (BMP header in
`Payload/Doom2rpg.app/Packages/cockpit.bmp`: width 0xF0, height 0xEA), loaded via
`Applet::loadImage("cockpit.bmp")` (`src/App.cpp:418`). Two blits:
`drawImage(img, cinRect[0]=0, cinRect[1]=42, flags=0, rot=0, mode=0)` → top-left
anchor, covers `(0,42)-(239,275)`; `drawImage(img, cinRect[2]=480, 42, flags=24,
rot=4, 0)` → flags 24 = 8(RIGHT)|16, so `x = 480-240 = 240`, and `rotateMode = 4`
is `glScalef(-1,1,1)` = horizontal mirror (`src/Graphics.cpp:349-368` anchor math,
`src/Image.cpp` `DrawTexture` case 4). Combined the art covers
**`(0,42,480,234)` = rows 42..275**, its top edge flush with the bottom of the
top black bar.

### 7.2 Draw order per frame, and what overpaints the world

The world 3D pass happens during `canvas->run()`
(`ST_CAMERA` branch `src/Canvas.cpp:948-958` → `updateView` →
`MayaCamera::Render`, `src/MovementController.cpp:390-395`); the 2D pass happens
afterwards in `backPaint` (`src/Canvas.cpp:381`, called at `:986-987`). Order:

1. `Main.cpp` clears the whole framebuffer to black every frame
   (`glClearColor(0,0,0,1); glClear(...)`, `src/Main.cpp:125-126`).
2. `MayaCamera::Render`: world (fov 315 / 290 in dialog) → `renderPortal()` →
   cinematic view weapon (`combat->drawWeapon(0,0)` if `cinematicWeapon != -1`) →
   **cockpit overlay** if `cockpitOverlayRaw` → post-process
   (`src/MayaCamera.cpp:302-324`). All of this lands in the GL band
   `glViewport(1,65,478,248)` = canvas `(1,7,478,248)` = rows 7..254
   (rendering.md §6.3) — the overlay excepted, it is plain 2D at rows 42..275.
3. `backPaint`: fade-of-3D (`REPAINT_VIEW3D`), then particles
   (`REPAINT_PARTICLES`, clipped to `cinRect+1/-2` in ST_CAMERA,
   `src/ParticleSystem.cpp:201-215`), then `hud->draw` (`src/Canvas.cpp:405-417`).
4. `Hud::draw` in ST_CAMERA has `hud->repaintFlags = 0x18` only, so it runs the
   bubble block (0x8) and then, **last**, the 0x10 block
   (`src/Hud.cpp:799-818`): `drawCinematicText` → **top bar, bottom bar**, big
   title, subtitle (+ player face), `drawBubbleText` again (`src/Hud.cpp:490`),
   then the right soft-key label.
5. `backPaint` tail: `Canvas::fadeFlags` fade over everything
   (`src/Canvas.cpp:499-510`).

**Yes — the top bar paints over already-rendered world.** The world band is rows
7..254; the top fill blacks out rows 7..41 of it (35 rows). The bottom fill
(292..319) is entirely below the world band and only hides HUD leftovers; rows
255..291 are black only because of the per-frame full-screen clear (nothing sets
`REPAINT_CLEAR` in ST_CAMERA). Net visible world during a cinematic:
**rows 42..254 (213 px tall), full width**.

### 7.3 What consumes `cinRect` and `CAMERAVIEW_BAR_HEIGHT`

`cinRect[1] = 42` / `cinRect[3] = 250` are consumed by exactly five places:
* the two `eraseRgn` bars (`src/Hud.cpp:455-456`) — `cinRect[1]` = top-bar height,
  `cinRect[1]+cinRect[3]` = 292 = bottom-bar top edge;
* the subtitle baseline (`src/Hud.cpp:469-470`, §7.5);
* the cockpit blits' anchor (`src/Hud.cpp:623-624`);
* particle clipping in ST_CAMERA (`src/ParticleSystem.cpp:204-215`);
* the ST_CAMERA `setViewport` call, which the GL path throws away
  (`src/Canvas.cpp:1215`, rendering.md §6.3);
* plus `cinRect[3]` as the scrolling-text base y in the story screens
  (`src/IntroSequenceManager.cpp:243`).

`CAMERAVIEW_BAR_HEIGHT = 20` (`src/Canvas.cpp:149`) has **one** consumer:
`Combat::drawWeapon` shifts the cinematic weapon up by it —
`if (b2) { weapon = game->cinematicWeapon; scrY -= CAMERAVIEW_BAR_HEIGHT; }`
with `b2 = (state == ST_CAMERA && cinematicWeapon != -1)`
(`src/Combat.cpp:675`, `:701-704`).

### 7.4 Gating — which cinematic sub-case gets bars

The bars ride on `hud->repaintFlags` bit `0x10`. Where it is written:
* `setState(ST_CAMERA)`: `app->hud->repaintFlags = 24` (= 0x08|0x10) plus
  `clearSoftKeys()` and the `cinRect` viewport call (`src/Canvas.cpp:1207-1216`).
* Every ST_CAMERA frame `MovementController::updateView` masks
  `app->hud->repaintFlags &= 0x18` right after `activeCamera->Render()`
  (`src/MovementController.cpp:390-395`) — so 0x18 survives and only bubble+
  cinematic draw; the HUD panels never appear. The `&= ~0x10` clear after drawing
  is commented out in this port (`src/Hud.cpp:800`), i.e. the bit is sticky.
* `EV_CAMERA_STR` **assigns** `app->hud->repaintFlags = 16`
  (`src/ScriptThread.cpp:538-539`). Harmless in ST_CAMERA; outside it this would
  turn the bars on until the next `setState` (all 13 `CAMERA_STR` sites in map00
  are inside ST_CAMERA, so it never fires there).

Consequences per sub-case:

| case | state | bars | HUD panels | cockpit art |
|---|---|---|---|---|
| boot intro fly-over / any scripted cinematic | ST_CAMERA | **yes** | no | only if opcode 76 was toggled (map00: camera 0 only) |
| map00 drop-ship arrival (cam 0) | ST_CAMERA | **yes** | no | yes, rows 42..275 |
| dialog opened during a cinematic | ST_DIALOG (camera still active) | **no** | top bar only | still drawn (`MayaCamera::Render` runs, fov 290) |
| `EV_START_INTERCINEMATIC` scene | ST_INTER_CAMERA | **no** | yes (0x2B) | n/a (`updateView` skips camera render, `src/MovementController.cpp:391`) |
| gameplay | ST_PLAYING/ST_COMBAT | no | yes (0x2F) | n/a |

Details: `setState(ST_DIALOG)` assigns `hud->repaintFlags = 47` and calls
`tinyGL->resetViewPort()` (`src/Canvas.cpp:1084-1097`); the ST_DIALOG frame runs
`updateView` (mask → 0x08) and then `|= 0x2B` (`src/Canvas.cpp:919-925`), final
0x2B → **no 0x10, no bars**. So the picture un-letterboxes (grows upward by 35
rows) while a dialog box is up over a cinematic, and re-letterboxes when the
dialog closes back into ST_CAMERA (`src/DialogSystem.cpp:541-554`).
`setState(ST_INTER_CAMERA)` assigns `43` (0x2B) (`src/Canvas.cpp:1080-1081`) and
each frame `|= 0x2B` (`src/Canvas.cpp:825-826`) — never 0x10.
Skippable vs non-skippable changes nothing about the bars: `cinUnpauseTime` only
gates the Skip soft key and the skip input (`src/Canvas.cpp:955-957`,
`src/InputEventController.cpp:416-419`).

### 7.5 Text/soft-key placement relative to the bars

All inside the same 0x10 block, drawn AFTER the fills:
* Big cinematic title: `drawString(largeBuffer, SCR_CX = 240, 1, flags = 1)`
  (`src/Hud.cpp:465`) — HCENTER|TOP at y = 1, i.e. **inside the top black bar**.
  Wrapped to `subtitleMaxChars = 480/9 = 53` chars (`src/Canvas.cpp:96`), or
  `53-7 = 46` when `showCinPlayer` (`src/Hud.cpp:457`).
* Subtitle: `n3 = cinRect[1]+cinRect[3] = 292`;
  `n4 = (n3 + ((screenRect[3] - n3 - 32) >> 1)) - 10 = (292 + ((320-292-32)>>1)) - 10`
  = `292 + (-4>>1) - 10` = **280** (`src/Hud.cpp:469-470`); drawn at
  `(240, 280)` HCENTER|TOP, second wrapped line at `y = 296`
  (`src/Hud.cpp:481-486`). So line 1 sits in the un-drawn gap below the world
  band and line 2 sits on the bottom black bar.
* Player face (bit 14 of `EV_CAMERA_STR` → `showCinPlayer`):
  `drawRegion(imgPlayerFaces, 0,0,32,30, 5, n4 - (width-32)/2, 0,0,0)`
  → x = 5, y = 280 - (imgPlayerFaces->width - 32)/2; the subtitle then switches
  to LEFT anchor at x = `imgPlayerFaces->width + 10` (`src/Hud.cpp:474-477`).
* "Skip" soft key: `drawString(texBuff, 478, 320, 40)` — flags 40 = 8(RIGHT)|
  32(BOTTOM), absolute display coords (`src/Hud.cpp:803-817`, string at `:813`), i.e. bottom-right
  **on** the bottom bar. Armed by `setRightSoftKey(0,40)` once
  `gameTime > cinUnpauseTime` (`src/Canvas.cpp:955-957`).

None of these positions are derived from the bar rects at runtime beyond
`cinRect[1]/[3]`, so fixing/adding the bars does not move any text: title y = 1,
subtitle y = 280, soft key anchored to (478,320).

## Port checklist (minimum viable map00 opening)

1. **States**: implement ST_PLAYING/ST_CAMERA/ST_INTER_CAMERA/ST_DIALOG transitions with
   the exact setters/restorers listed in §1; gate script resumption on
   PLAYING/CAMERA (`src/Game.cpp:3259`).
2. **Camera runtime**: parse per-map `numKeys/sampleRate/keys[7ch]/tweenIndices/tweens`
   (§3 layout); reproduce `setupCamera` (incl. `-2` inherit sentinel and 1000 ms
   `cinUnpauseTime`), the 125 Hz-sample interpolation formulas **including the
   camera-local vs global tween-index rebase** (§3 PITFALL — getting it wrong silently
   zeroes every tween delta and makes keys lurch/rush), shortest-arc angles,
   `Update(activeCameraKey, gameTime-activeCameraTime)` ticking, FOV 315/290,
   viewport swap to `cinRect`, and end-of-keys → ST_PLAYING reset.
3. **Opcodes**: STARTCINEMATIC(5), ADV_CAMERAKEY(18) with resume-count handshake,
   CAMERA_STR(12), WAIT(14)+evWait blocking rules, LERPSPRITE family (4/59/61/75),
   ENTITY_FRAME(17), FADEOP(32), PLAYSOUND(37), DIALOG(13) incl. style-2 help queue,
   GIVEITEM(33), GOTO(15)/TURN_PLAYER(69) with `gotoThread` handshake, EVENTOP(23),
   DOOROP(21), HIDE(24), NEXTSTATE/SETSTATE/EVAL/JUMP/CALL_FUNC/RETURN.
4. **Player-by-script**: reuse gameplay movement — write `destX/Y/Z/destAngle`, zero
   pitch/roll, animate via fixed-step glide, park script on `gotoThread`, run
   destination events + `relink()` on arrival (§4).
5. **Dialogs concurrently**: dialog close must restore previous state (INTER_CAMERA /
   CAMERA / PLAYING) and resume the parked script thread (§1, `src/DialogSystem.cpp:559-563`).
6. **Input parking**: in ST_CAMERA accept only skip (after `cinUnpauseTime`); support
   `blockInputTime`; honor `skipAdvanceTurn`.
7. **Skip**: implement `skipCinematic` loop semantics (snap lerps/camera keys, fast-forward
   thread with huge timestamp, clear subtitles/particles, 500 ms fade-in).
8. **map00 boot**: run staticFunc[0]; place player at (4,19,E); fire type-8 spawn-tile
   event (help dlg 41); new-game branch → var5=1 + cam=4 fly-over with door ops at
   ADV_CAMERAKEY boundaries; then regular play.
