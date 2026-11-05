# Screen fades (`Render` fade + script `EV_FADEOP`)

Verified facts about the 3D-scene fade owned by `Render` and driven by scripts,
map changes, death and cutscene skip. Every claim cites `src/`.
Raw log: [`../research/2026-10-28-script-fades.md`](../research/2026-10-28-script-fades.md).

## 1. State and flags

- State: `Render::fadeTime`, `fadeDuration`, `fadeFlags` (`src/Render.h:236-238`).
- Flags (`src/Render.h:84-91`): NONE=0, FADEOUT=1, FADEIN=2, CHANGEMAP=4,
  SHOWSTATS=8, DENYSKIP=16, EPILOGUE=32.
- `FADE_SPECIAL_FLAG_MASK = -29` (`src/Render.h:91`) = `~0x1C`: it clears
  CHANGEMAP|SHOWSTATS|DENYSKIP and **keeps** FADEOUT/FADEIN/EPILOGUE.
- `startFade(dur, flags)`: `fadeTime = app->upTimeMs; fadeDuration = dur;
  fadeFlags = flags` — overwrites any fade in progress (`src/Render.cpp:2535-2541`).
  Time base is `upTimeMs` (wall clock), not `gameTime`.
- `endFade()`: `fadeFlags = 0; fadeTime = -1` (`src/Render.cpp:2543-2546`).
- `isFading()` = `fadeFlags != 0`; `getFadeFlags()` returns the raw word
  (`src/Render.cpp:2527-2533`).

## 2. `Render::fadeScene` (all flags, `src/Render.cpp:2548-2590`)

```
rect = {0,0,480,320}
t = upTimeMs - fadeTime
if t >= fadeDuration - 50:                 // completes 50 ms early
    t = fadeDuration
    CHANGEMAP: flags &= MASK; fade(rect,0,black); canvas->loadMap(nextMap); return
    SHOWSTATS: flags &= MASK; fade(rect,0,black); canvas->saveState(51,3,196); return
    EPILOGUE : endFade(); setState(ST_EPILOGUE); return
    FADEIN   : endFade(); return            // nothing drawn this frame -> full scene
    FADEOUT  : fade(rect,0,black); return   // flags stay = 1: black is HELD
alpha = 65280 * ((t<<16) / (fadeDuration<<8)) >> 16   // = t*256/dur * 255/256, 0..255
if FADEOUT: alpha = 256 - alpha
fade(rect, alpha, black)
```

- The `alpha` passed to `Graphics::fade` is the **scene weight**: the held
  FADEOUT end state passes 0 (`src/Render.cpp:2578`) and is fully black.
  So FADEIN goes black → scene (weight 0→255), FADEOUT goes scene → black
  (weight 256→~1, then held 0). `Graphics::fade` is only declared
  (`src/Graphics.h:74`); body at `src/Graphics.cpp:755-766` (file is ISO-8859, grep with
  `LC_ALL=C grep -a`): black quad over `rect` with GL alpha `1 - alpha/256`
  (`COLOR_BYTE_TO_FLOAT` = x/256, `src/GLES.cpp:21`), blended SRC_ALPHA/ONE_MINUS_SRC_ALPHA
  unless that alpha >= 1.0 (then opaque, `src/Graphics.cpp:195-206`); see
  `cinematic-overlay.md`. Colour is always
  `0x00000000` in this fade — no white/colour fade exists here.
- FADEIN self-clears (`endFade`, `:2572-2574`); FADEOUT never clears itself and
  holds black until another `startFade` or `endFade`.
- After CHANGEMAP/SHOWSTATS completes, the mask leaves `fadeFlags = FADEOUT`, so
  the black hold carries into the stats menu / next map until something
  fades in (`src/Render.cpp:2555-2566`).
- Durations ≤ 50 ms complete on the first poll; no division happens.
- EPILOGUE and DENYSKIP are never passed to `startFade` anywhere in `src/`
  (all callers: `src/ScriptThread.cpp:507,951,954`, `src/Game.cpp:2531`,
  `src/GameStateRunner.cpp:272,313`, `src/Player.cpp:2104,2162,2550,2610`,
  `src/ZoomController.cpp:39,53`). `EV_END_GAME` sets `ST_EPILOGUE` directly
  (`src/ScriptThread.cpp:1649`).

## 3. Where it is drawn (frame order)

`fadeScene` is only polled from the `REPAINT_VIEW3D` slot of `Canvas::backPaint`:
GL path `if (isFading()) fadeScene(graphics)` (`src/Canvas.cpp:391-396`); software
path via `drawRGB` → `if (fadeFlags != 0) fadeScene(graphics)`
(`src/Canvas.cpp:400-401`, `src/Render.cpp:2955-2957`). Both sit **before**
particles (`src/Canvas.cpp:405-408`), swipe controls (`:410-412`), the HUD
(`:414-417`) and the per-state 2D layers (dialog, automap, … `:423+`).

- ST_PLAYING: `renderScene` sets `REPAINT_VIEW3D` (`src/Canvas.cpp:1355`); the
  fade covers the 3D view + view weapon, then the HUD bars/messages are painted
  on top of it.
- ST_CAMERA: `MayaCamera::Render` draws world → cinematic weapon → **cockpit
  overlay** → post-process, then sets `REPAINT_VIEW3D` (`src/MayaCamera.cpp:311-323`).
  The fade therefore darkens the cockpit overlay too, while the cinematic
  letterbox bars, titles and subtitles (drawn by `Hud::draw`) stay visible over it.
- ST_DIALOG: the dialog box (`src/Canvas.cpp:446-448`) is painted after the fade.
- Completion (CHANGEMAP/SHOWSTATS/FADEIN clear) only advances on frames where
  `REPAINT_VIEW3D` is set.

A separate, unrelated `Canvas::fadeFlags` fade (linear, any colour, FADEOUT=1 /
FADEIN=2, `src/Canvas.h:64-66,349-351`) runs at the tail of `backPaint`
(`src/Canvas.cpp:499-510`); only the intro movie uses it
(`src/IntroSequenceManager.cpp:787-791`). Scripts never touch it.

## 4. `EV_FADEOP` (opcode 32, `src/ScriptThread.cpp:938-958`, `src/Enums.h:434`)

- Operand: one u16 `v`. `bit15 = (v & 0x8000) == 0x8000`.
- Normal: `bit15` → `startFade(v & 0x7FFF, 2)` = **FADEIN**; else
  `startFade(v, 1)` = **FADEOUT** (`:950-955`).
- While `game->skippingCinematic`: FADEIN ops → `endFade()`; FADEOUT ops are
  ignored (`:942-948`).
- Non-blocking: the case only `break`s; the thread continues immediately.
- Note: `tools/disasm_map_scripts.py` `h_fadeop` prints bit15 as `out`, and the
  rewrite stub logs the same — both are **inverted** relative to the code above.

## 5. Other readers / writers of the fade state

| Site | Behaviour |
|---|---|
| `src/InputEventController.cpp:158,305-307` | swallow every key while `fadeFlags & DENYSKIP` (never set in practice) |
| `src/Game.cpp:2530-2532` | end of `skipCinematic`: `startFade(500, FADEIN)` unless a CHANGEMAP/SHOWSTATS fade (`& 0xC`) is running |
| `src/GameStateRunner.cpp:270-273` | ST_DYING 750..2750 ms: `if (!isFading()) startFade(2000, FADEOUT)` |
| `src/GameStateRunner.cpp:311-314` | familiar dying: `if (!isFading()) startFade(750, FADEOUT)` |
| `src/ScriptThread.cpp:496-508` | `EV_CHANGE_MAP` bit7: `startFade(1000, FADEOUT\|SHOWSTATS or CHANGEMAP)` |
| `src/Player.cpp:2104,2162,2550,2610`, `src/ZoomController.cpp:39,53` | FADEIN 400/750/500 ms on familiar swap, target practice, zoom |
| `src/Canvas.cpp:1380` | `backToMain` → `endFade()` |

Nothing gates script/camera advance on the fade: `MayaCamera` and `ScriptThread`
never read it. `LoadingManager`/`loadMap` do not reset it (only
`src/Canvas.cpp:1380` and `src/ScriptThread.cpp:944` call `endFade`).

## 6. map00 FADEOPs (direction per §4, not per the disassembler label)

| IP | Code | Context |
|---|---|---|
| 243 | FADEIN 500 | staticFunc[0] tail, `v17!=0 && v15==1` branch |
| 1894 | FADEIN 500 | func_1889 (`v17!=0 && v15!=1`), after `STARTCINEMATIC camera=4` |
| 1948 | FADEIN 1000 | func_1942 (new game, `v17==0`), after `TOGGLE_OVERLAY`, `STARTCINEMATIC camera=0` |
| 2057 | FADEOUT 1000 | end of camera 0, 2 more keys play under black |
| 2100 | FADEIN 1000 | func_2084, after `STARTCINEMATIC camera=1` |
| 2285 | FADEOUT 1000 | end of camera 1 |
| 2427 | FADEIN 1000 | func_2328, after `STARTCINEMATIC camera=5` + `GOTO (8,19)` |
| 2519 | FADEOUT 200 | camera 5 mid-shot cut |
| 2618 | FADEIN 500 | camera 5, after NPC repositioning |
| 9671 | FADEOUT 800 | camera 3 exit, then `CHANGE_MAP map=2` (no fade bit) |

Every FADEOUT inside map00 is followed by a FADEIN except 9671, whose held black
carries into the level-stats screen / map02 (§2).
