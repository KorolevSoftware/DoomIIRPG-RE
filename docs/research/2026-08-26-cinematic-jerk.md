# Cinematic jerk + "accelerated animations" at the elevator exit (map00) — root cause

**Date:** 2026-08-26 · **Verdict:** CONFIRMED (primary root cause) + 2 secondary divergences
**Symptom:** hard camera jerk when the player exits the elevator area right after the boot
intro; user suspects animations run accelerated. Matches known bug "camera judder in
cinematics" (`docs/status.md:23`).

## 0. TL;DR

The rewrite skips **key 0 of every Maya camera**. Legacy starts a camera with
`activeCameraKey = -1` (`src/ScriptThread.cpp:188`), so the script's *first*
`EV_ADV_CAMERAKEY` calls `NextKey()` and lands on **key index 0**, which then plays its
full authored duration. The rewrite initializes `activeCameraKey_ = 0`
(`new_src/core/GameContext.cpp:583`) and `advanceCameraKey()` unconditionally does
`nextKey()` → `++activeCameraKey_` (`new_src/core/GameContext.cpp:599`, `:612`), so the
first key is never animated. Consequences:

1. Every cinematic is shortened by key0's duration — cam7: 1500 ms played vs 2750 ms
   authored (**−45%**); cam5: −1999 ms; cam0: −999 ms; cam1: −1499 ms. This is the
   literal "animations are accelerated" feel.
2. The opening frame pose comes from **key1's base** instead of continuing from the
   static key0 pose that legacy shows — for cam7/cam10 key0 carries the `-2`
   player-inherit yaw, so legacy opens seamlessly on the player's facing while the
   rewrite **hard-cuts** to a fixed yaw → the visible JERK.
3. Cameras whose whole shot *is* key0 (cam10, 500 ms + ms=0 tail) complete **instantly**
   in the same tick they start — the doorway cutaway at tile (10,19) never renders.

Clock-rate divergence was **refuted** as the acceleration source (§3): no rewrite clock
advances where legacy freezes; all divergences run the other way.

---

## 1. Q1 — The elevator-exit sequence in map00 data

Bytecode anchors verified by re-parsing `tmp_map00.bin` (header LE per
`src/LoadingManager.cpp:362-397`; staticFuncs@60651, tileEvents@60679,
bytecode@62019..72034, cameras after an unvalidated marker; decoder:
`docs/research/assets/probe_cam_jitter.py`). All bytecode operands big-endian
(`src/ScriptThread.cpp:2095-2119`).

### 1.1 Which events fire around "exiting the elevator"

| Tile | Event | Trigger | IP | Content |
|---|---|---|---|---|
| (4,19) spawn | ev[49] | FACE W/SW `0x18` | 3687 | help popup dlg41 |
| (8,19) | ev[41]/ev[42] | `0x404`/`0x18` | 1923 / 3687 | face events |
| (9,19) sergeant | ev[54] | TRIGGER+all dirs `0xFF4` | 2993 | dialogs str21–25, squad choreography, **cam 7 @3250** |
| (10,19) doorway | ev[56] | ENTER+all dirs `0xFF1` | 4223 | self-disable, **cam 10 @4226**, scientist/Klingon pantomime, weapon-handout lead-out |

ev[54] decode (disasm `docs/research/assets/map00_disasm.txt`, lines 700–783):
`@3248 WAIT500 → @3250 STARTCINEMATIC cam=7 → @3252 GOTO 4308 → @3255 ADV_CAMERAKEY
resumes=4 → DOOROPs/HIDEs/bob-walk → RETURN`. GOTO operand 4308 = instant teleport
(bit14=0) to tile **(6,20)** center (416,1312) facing nibble 4 (=angle 512, S),
pitch/roll zeroed (`src/ScriptThread.cpp:602-653`) — exactly cam7's final key position
(see §1.2): the camera hands off *onto* the player.

ev[56] decode: `@4223 EVENTOP disable-self → @4226 STARTCINEMATIC cam=10 → @4228
ADV_CAMERAKEY resumes=1 → @4230 WAIT500 … ~13 s of lerp/dialog/fog choreography … @4485
WAIT1500 → @4487 ADV_CAMERAKEY resumes=1 → @4489 RETURN`.

### 1.2 Camera data (probe output, `tmp_map00.bin`)

```
camera 7: keys=4 sr=125 totalMs=2750      ← evt[54], watches player walk out
  key0 pos=(544,1248,484) yaw=-2  ms=1250   ← -2 = inherit PLAYER yaw (sentinel,
  key1 pos=(544,1248,484) yaw=512 ms=1000     src/MayaCamera.cpp:85-102,243-288)
  key2 pos=(416,1248,484) yaw=640 ms=500    ← authored data cut (pan)
  key3 pos=(416,1312,484) yaw=512 ms=0      ← ZERO-duration key = instant completion
camera 10: keys=2 sr=125 totalMs=500      ← ev[56] doorway cutaway
  key0 pos=(672,1248,484) yaw=-2 ms=500    ← player tile (10,19)+(32,32), inherit yaw
  key1 pos=(672,1248,484) yaw=0   ms=0
camera 5: keys=9 totalMs=7199             ← boot-intro finale (func@2328)
  key0 pos=(200,1248,528) pitch=924 ms=1999  ← opening high shot; ends ON player tile:
  key8 pos=(544,1248,484) yaw=0   ms=0       (544,1248) == post-GOTO@2424 player spot
```

Boot chain STARTCINEMATIC→first-ADV gaps (same script step unless WAITs intervene):
cam0: `OP5 0`@1946 → `OP18`@1954 (same frame). cam1: `OP5 1`@2098 → `OP18`@2103 (same
frame). cam5: `OP5 5`@2422 → first `OP18`@2517 (~1–3 s lead-in of WAIT500 + blocking
walk lerps). Final keys end every camera on an **ms=0 key** → completion is instantaneous
through `Snap`'s complete tail.

### 1.3 Frame-by-frame at the boundary (legacy)

ST_CAMERA tick (`src/Canvas.cpp:949-958`): `if (activeCameraKey != -1)
Update(activeCameraKey, gameTime - activeCameraTime)` → `updateLerpSprites()` →
`updateView()`. Boundary inside `Update` (`src/MayaCamera.cpp:72-77`):
`elapsed >= keyMs → keyThreadResumeCount > 0 ? Snap(i) : return` (hold).
`Snap` (`src/MayaCamera.cpp:326-374`): pose := **static values of key i+1** (no duration
charged, no advance), decrement count; at 0 → `keyThread->run()` and RETURN (the resumed
script's own next `EV_ADV_CAMERAKEY` re-parks and `NextKey()`s — `src/ScriptThread.cpp:693-695`);
else auto-advance one key. Complete tail when i is the last key
(`src/MayaCamera.cpp:376-397`): `complete=true; activeCameraView=false;
resetViewPort(); setState(ST_PLAYING)` → resume parked thread → **`viewPitch=destPitch=0;
startRotation(true)`**. Fades during these shots are plain FADEOPs (@1948/@2427 OUT 1000,
IN 200 @2519); skip path force-ends with 500 ms fade-in (`src/Game.cpp:2530-2532`).

### 1.4 What our code does at the same boundary

`startCinematic` (`new_src/core/GameContext.cpp:571-588`) captures the player pose
(viewX/Y/Z/viewAngle — legacy captures **dest** fields, `src/ScriptThread.cpp:192-196`;
equal except mid-walk/mid-turn), sets `activeCameraKey_=0`. `advanceCameraKey`
(`:602-613`) parks the thread and calls `nextKey()` (`:590-600`: restart clock,
`++activeCameraKey_`) → **key 1**. Boundary engine `tickCinematicClock` (`:644-683`) and
render-side resampling (`:929-959`, idempotent because `MayaCamera::update` resets
`m_agg` per call, `new_src/core/MayaCamera.cpp:64,92-96`) otherwise match the Snap
semantics fixed on 2026-08-25 (`docs/journal.md:194-213`). `finishCinematic`
(`:714-726`) omits legacy's pitch zero + `startRotation(true)` analog — currently
dormant (rewrite tracks viewPitch only for loot, `new_src/core/GameContext.h:198-199`),
but must not be forgotten.

## 2. Root causes, ranked

### RC#1 — First-ADV_CAMERAKEY key-index off-by-one (PRIMARY — explains both symptoms)

* Legacy: `setupCamera` sets `game->activeCameraKey = -1`
  (`src/ScriptThread.cpp:188`); the ST_CAMERA tick skips Update while it is −1
  (`src/Canvas.cpp:950`); the first `EV_ADV_CAMERAKEY` → `NextKey()` lands on **key 0**
  with a fresh clock (`src/ScriptThread.cpp:695`, `src/MayaCamera.cpp:36-44` —
  `activeCameraTime = gameTime; activeCameraKey++`).
* Rewrite: `startCinematic` sets `activeCameraKey_ = 0`
  (`new_src/core/GameContext.cpp:583`); `advanceCameraKey` always `nextKey()`
  (`:612`) → key 1. Key 0's motion never plays.
* Visible effects, elevator exit included:
  * **cam7**: opens with a hard cut from the player view to key1 (yaw 512 fixed)
    instead of gliding from the inherited player yaw over 1250 ms → **the jerk**; total
    shot 1500 ms instead of 2750 ms → **"accelerated"**.
  * **cam10**: `STARTCINEMATIC`+`ADV resumes=1` happen synchronously inside
    `finishMovement`; with key=1 (ms=0) `tickCinematicClock` hits the last-key branch
    (`:671-673`) and `finishCinematic()` runs **in the same tick** — cutaway never
    rendered (reads as a glitch/flash at the doorway).
  * **boot intro**: cam5 loses its 1999 ms opening high shot; during the ~1–3 s
    pre-ADV lead-in legacy shows the *static* key0 pose (Update gated on ≠−1) while the
    rewrite animates key0 and then cuts to key1.
* Corroboration: the 2026-08-25 timing measurements already played only keys[1..]
  ("cam0 16995 ms = **13400** keys + holds", "cam1 … 1499 truncated + 6600",
  `docs/journal.md:209-212`) — 13400 = Σms(keys1..11) of cam0, i.e. the skip is visible
  in our own logs; it was misread as faithful truncation.
* Fix shape (⚠ not a one-liner): restore tri-state start (key −1 = bound-but-not-started,
  render/tick show static resolved key0 pose), let first `nextKey()` land on 0, and
  update every `activeCameraKey_ >= 0` gate that doubles as "cinematic active"
  (`GameContext.h:108`, `GameContext.cpp:45,199,345,371,377,645,929`;
  ScriptVM gate `new_src/domain/game/ScriptVM.cpp:727`).

### RC#2 — Missing Snap-tail view reset at cinematic end (secondary, mostly dormant)

Legacy zeroes `viewPitch/destPitch` and recomputes step vectors via `startRotation(true)`
when a camera completes (`src/MayaCamera.cpp:396-397`); `finishCinematic` has no analog
(`new_src/core/GameContext.cpp:714-726`). With pitch always 0 outside loot today this
cannot yet produce visible motion, but any future pitched state will jerk at every
cinematic end.

### RC#3 — Screen-shake envelope differs (secondary, "cadence" suspect confirmed)

Legacy randomizes shake offsets every frame at FULL amplitude until the deadline, then
zeroes once (`src/MovementController.cpp:381-389`, `Canvas::startShake`
`src/Canvas.cpp:1000-1017`). The rewrite linearly decays amplitude across the window
(`new_src/ui/Hud.cpp:163-173`, flagged "task spec" but unfaithful). Boot intro and the
imp attack fire several SCREEN_SHAKE ops (e.g. @2127, @2656, ev[56] @4304), so shake
beats read softer/different but not juddering.

### Refuted candidates

* **Clock-rate divergence** (Q2): full enumeration — legacy advances `gameTime` in every
  non-menu state (`src/Canvas.cpp:769-771`), freezing it only under `pauseGameTime`;
  dialog pause uses the `activeCameraTime` flip instead (`src/Canvas.cpp:1092-1094`).
  The rewrite advances `gameTime` only in Playing/Camera/Looting
  (`new_src/core/GameContext.cpp:127-128`) — i.e. ours freezes where legacy runs
  (Dialog/Loading/InterCamera), the opposite of acceleration. Lerp clocks: legacy
  `elapsed = gameTime - startTime` (`src/Game.cpp:2862`) vs rewrite `lerpClock_ += dtMs`
  driven from a single site (`new_src/domain/game/Game.cpp:1222-1246` via
  `new_src/core/GameContext.cpp:193-203`) — no double-tick exists; cadence identical
  (one 15 ms quantum per rendered frame, `new_src/core/GameLoop.cpp:73-76,94-95` ≈
  legacy DoLoop pacing `src/Main.cpp:83-88`). Walk speeds match (animPos 7 /
  animAngle 26 per step: `src/MovementController.cpp:21-26` =
  `new_src/domain/game/Player.cpp:13-15`).
* **Boundary snap semantics**: equivalent since the 2026-08-25 fix (§1.4); ms=0 keys
  complete instantly in both engines.
* **Render-time resample** (`:941-944`): harmless — quantized to the same `gameTime`,
  and `update()` is stateless per call.

## 3. Discrimination probes (Q4)

Capture: `cd build_new/new_src && ./DoomIIRPG 2>err.log`, replay boot intro, walk out of
the elevator bay onto (10,19).

**(a) Key-index skip (RC#1 — expected smoking gun).** One-line temp log in `nextKey()`
(after `:599`):
`fprintf(stderr,"[cam] nextKey idx=%d elapsed=%ld\n", activeCameraKey_, (long)(gameTime-cameraStartTime_));`
Pattern today: `[script] STARTCINEMATIC camera=7` (already logged,
`ScriptVM.cpp:716`) followed immediately by `[cam] nextKey idx=1`, and `[cam] FINISH`
~1500 ms later. After the fix the first nextKey must log `idx=0` and cam7 must span
≈2750 ms. Zero-code variant: the existing pair `STARTCINEMATIC camera=10` +
`ADV_CAMERAKEY resumes=1` followed by same-tick completion already betrays RC#1 for cam10.

**(b) Clock-rate divergence.** In `GameLoop::run` after `ctx.tick()` (`GameLoop.cpp:75`),
once per second:
`fprintf(stderr,"[clk] wallΔ=%u gameΔ=%ld upΔ=%ld\n", now-lastNow, …)`.
Healthy: deltas equal ±15 ms. If `gameΔ < wallΔ` it happens only in Dialog/Loading
(freeze deviation) — proves clocking is NOT the acceleration.

**(c) Shake envelope.** One line in `Hud::tickShake` (`Hud.cpp:172`):
`fprintf(stderr,"[shake] amp=%d x=%d y=%d\n", amp, shakeX_, shakeY_);`
Legacy-shaped output = constant `amp` band then hard 0; current build ramps down
linearly.

**(d) Teleport-GOTO / completion cut.** Add to `finishCinematic` (`:720`):
`fprintf(stderr,"[cam] FINISH cam=%d gameTime=%ld\n", cameraCamIdx_, (long)gameTime);`
and to EV_GOTO in ScriptVM a tile print. Pattern: `[dbg] finishMovement destTile=10,19`
(existing, `GameContext.cpp:404`) + `STARTCINEMATIC camera=10` + `FINISH cam=10` all
within one game-second ⇒ instant-complete confirmed; a `GOTO -> tile(6,20)` line between
`STARTCINEMATIC camera=7` and `ADV_CAMERAKEY resumes=4` confirms the scripted teleport
cut point.

## 4. Evidence index

Legacy: `src/ScriptThread.cpp:183-228` (setupCamera, −1 @188, dest capture @192-196),
`:400-411` (STARTCINEMATIC, no NextKey), `:593-661` (GOTO incl. instant branch
@635-655, pitch zeroing @608, startRotation-suppressed-in-camera @646-652), `:690-702`
(ADV_CAMERAKEY, NextKey @695); `src/MayaCamera.cpp:36-44` (NextKey), `:46-148` (Update,
boundary @72-77), `:151-159/:234-241` (interp math), `:326-398` (Snap, counting
@359-370, complete tail @376-397, pitch reset @396-397); `src/Canvas.cpp:769-771`
(gameTime), `:949-958` (ST_CAMERA tick, ≠−1 gate @950), `:1000-1017` (startShake),
`:1092-1094` (dialog time flip); `src/MovementController.cpp:21-26` (anim steps),
`:381-389` (shake jitter), `:391-396` (camera early-return); `src/Game.cpp:2862`
(lerp time base), `:2507-2544` (skipCinematic).

Rewrite: `new_src/core/GameContext.cpp:127-128` (gameTime gating), `:181` (thread
gate), `:193-203` (single lerp driver), `:571-588` (startCinematic, key=0 @583),
`:590-600` (nextKey), `:602-613` (advanceCameraKey), `:622-683` (tickCamera/clock),
`:714-726` (finishCinematic), `:728-765` (skip/flush), `:914-959` (render branches);
`new_src/core/MayaCamera.cpp:51-134` (stateless update), `:155-171` (snap);
`new_src/domain/game/ScriptVM.cpp:712-732` (op handlers, gate @727);
`new_src/domain/game/Game.cpp:1036-1129,1222-1246` (lerps); `new_src/ui/Hud.cpp:147-174`
(shake); `new_src/domain/game/Player.cpp:13-15,37-67` (movement); 
`new_src/core/GameLoop.cpp:61-96` (quantum pacing).

Data: `tmp_map00.bin` header/tileEvents/bytecode/cameras decoded by
`docs/research/assets/probe_cam_jitter.py` (this report §1.2 tables); disasm
`docs/research/assets/map00_disasm.txt` IPs 1942-2083, 2084-2327, 2328-2992, 3248-3332,
4223-4489. Prior measurements `docs/journal.md:194-213`.
