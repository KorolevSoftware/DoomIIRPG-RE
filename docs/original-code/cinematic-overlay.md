# Cinematic (ST_CAMERA = 18) screen overlay: what darkens the picture

Verified 2026-11-05 (`docs/research/2026-11-05-cinematic-overlay.md`). Reference build =
TinyGL software path (`gles::isInit == false`, see `corpse-outline.md`, `rendering.md` §6.4).
Canvas 480x320, all 2D coordinates below are absolute canvas pixels (`backPaint` runs after
`graphics.resetScreenSpace()`, `src/Canvas.cpp:986-987`). Detailed bar/text geometry also lives
in `cutscenes-camera.md` §7; the fade math in `fades.md`.

## 1. Summary: there is NO cinematic dim/blend layer

Searching every `ST_CAMERA` use in `src/` (`Canvas.cpp`, `Hud.cpp`, `Render.cpp`,
`MayaCamera.cpp`, `ParticleSystem.cpp`, `Combat.cpp`, `ScriptThread.cpp`, ...) finds no
translucent full-screen fill, no animated letterbox, no brightness change tied to the camera
state. The "darkening" during a cinematic is exactly two things:

1. **Letterbox bars** — two OPAQUE black fills, every ST_CAMERA frame, no animation (§2).
2. **Script fades** — `EV_FADEOP` -> `Render::startFade`, a full-screen black quad with
   time-varying alpha, drawn under the bars/HUD (§3). Map00 cinematics open with
   `FADEOP in 500/1000ms` and close with `FADEOP out` (§4).

The only other camera-specific render tweak: dizziness wobble is forced off in ST_CAMERA
(`n7 = 0`, `src/Render.cpp:2235-2237`). `postProcessView` runs only when `postProcessMode != 0`
(`src/MayaCamera.cpp:319-321`), which is set solely by the familiar type
(`src/Player.cpp:2500-2516`), not by cinematics.

## 2. Letterbox bars (opaque, static)

* `Hud::drawCinematicText` (`src/Hud.cpp:455-456`):
  `eraseRgn(0, 0, displayRect[2], cinRect[1])` and
  `eraseRgn(0, cinRect[1]+cinRect[3], displayRect[2], softKeyY-(cinRect[1]+cinRect[3]))`.
  With `cinRect = {0,42,480,250}` (`src/Canvas.cpp:151-154`), `softKeyY = 320`:
  **top (0,0,480,42)**, **bottom (0,292,480,28)**.
* `eraseRgn` = `setColor(0)` + `fillRect` (`src/Graphics.cpp:250-256`); `fillRect` passes
  `a = 1.0` (`src/Graphics.cpp:88-95`) and `FMGL_fillRect` disables blending when `a >= 1.0`
  (`src/Graphics.cpp:195-199`) -> pure opaque black, no alpha.
* Gate: Hud repaint bit `0x10`. `setState(ST_CAMERA)` sets `hud->repaintFlags = 24`
  (`src/Canvas.cpp:1212`); each frame `updateView` masks `&= 0x18` after
  `activeCamera->Render()` (`src/MovementController.cpp:391-395`). Drawn in the 0x10 block
  of `Hud::draw` (`src/Hud.cpp:799-801`). The 0x10 clear is commented out (`:800`).
* Timing: appears on the very first ST_CAMERA frame and disappears on the first frame of
  any other state — no slide/ease. ST_DIALOG over a cinematic (`repaintFlags = 47`) and
  ST_INTER_CAMERA (`43`) never have bars (`cutscenes-camera.md` §7.4).

## 3. Script fade (`Render::fadeScene` -> `Graphics::fade`)

* `fadeScene` (`src/Render.cpp:2548-2590`): `rect = {0,0,480,320}`;
  `t = upTimeMs - fadeTime`; weight `w = 65280*((t<<16)/(dur<<8)) >> 16` (0..255);
  FADEOUT: `w = 256 - w`; completes 50 ms early (`t >= dur-50`): FADEIN -> `endFade()` and
  nothing drawn; FADEOUT -> `fade(rect, 0, black)` held every frame until the next fade.
* `Graphics::fade` (`src/Graphics.cpp:755-766`): `a = w/256` (`COLOR_BYTE_TO_FLOAT`,
  `src/GLES.cpp:21`), then `FMGL_fillRect(rect, 0,0,0, 1.0f - a)`; clip reset to the whole
  backbuffer. `FMGL_fillRect` with alpha < 1 enables `GL_BLEND`
  `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` + alpha test `> 0`
  (`src/Graphics.cpp:200-206`). So the on-screen result is
  `dst = scene * (w/256)` (black overlay with alpha `1 - w/256`); `w = 0` -> opaque black.
* Time base `upTimeMs` (wall clock), `startFade` in `src/Render.cpp:2535-2541`.
* Software-path poll: `backPaint` REPAINT_VIEW3D slot -> `Render::drawRGB`
  (`src/Canvas.cpp:398-402`) -> `if (fadeFlags != 0) fadeScene(graphics)`
  (`src/Render.cpp:2955-2957`). `MayaCamera::Render` sets `REPAINT_VIEW3D`
  (`src/MayaCamera.cpp:323`).
* **Software-path quirk**: `drawRGB` returns immediately when `hud->cockpitOverlayRaw`
  (`src/Render.cpp:2937-2939`), so while the cockpit overlay is toggled on (map00 camera 0,
  IPs 1945..2062) the fade is neither drawn nor advanced; FADEIN's `endFade` cannot fire then.
  When the overlay toggles off, the pending fade resumes with its elapsed wall time.
* `drawRGB` also draws a 1-px black `drawRect(viewportX-1, -1, viewportWidth+1, 321)`
  (`src/Render.cpp:2942-2954`; software forces `viewportY=0, height=320`, `:2948-2951`);
  with tinyGL `viewportX = 1`, `viewportWidth = 478` (`src/TinyGL.cpp:149-166`) it outlines
  columns 0 and 479. Not cinematic-specific.

## 4. Map00 fade/cinematic pairing (disassembler, `tmp_map00.bin`)

`tools/disasm_map_scripts.py tmp_map00.bin --verify` (0 failures):
`STARTCINEMATIC cam=4 @1892` -> `FADEOP in 500ms @1894`;
`TOGGLE_OVERLAY @1945`, `STARTCINEMATIC cam=0 @1946`, `FADEOP in 1000ms @1948`,
`FADEOP out 1000ms @2057`, `TOGGLE_OVERLAY @2062`;
`STARTCINEMATIC cam=1 @2098` -> `FADEOP in 1000ms @2100`, `FADEOP out 1000ms @2285`;
`STARTCINEMATIC cam=5 @2422` -> `FADEOP in 1000ms @2427`, `FADEOP out 200ms @2519`,
`FADEOP in 500ms @2618`; `STARTCINEMATIC cam=3 @9652` -> `FADEOP out 800ms @9671`.
Cameras 7..13 and 2 have no FADEOP right at their start.

## 5. Frame order in ST_CAMERA (software path)

1. 3D world into the band rows 7..254 (`MayaCamera::Render`, fov 315,
   `src/MayaCamera.cpp:302-310`; software viewport `posY` forced to 3, so `setViewport(cinRect)`
   at `src/Canvas.cpp:1215` changes nothing vertically, `src/TinyGL.cpp:155-157`).
2. `renderPortal`, cinematic weapon if `cinematicWeapon != -1`, cockpit art if toggled,
   post-process if familiar (`src/MayaCamera.cpp:311-321`).
3. `backPaint`: `drawRGB` (border + **fade**) (`src/Canvas.cpp:391-402`).
4. Particles (clipped to cinRect, `src/ParticleSystem.cpp:204-215`) (`src/Canvas.cpp:405-408`).
5. `Hud::draw` with flags 0x18: bubble text (0x8), then **bars**, title y=1, subtitle y=280,
   Skip soft key (478,320, RIGHT|BOTTOM) (`src/Hud.cpp:783-818`). No top/bottom HUD panels
   (bits 0x1/0x2/0x4 masked off). Swipe controls are drawn only in PLAYING/COMBAT
   (`src/Canvas.cpp:410-412`).
6. `Canvas::fadeFlags` tail fade (`src/Canvas.cpp:499-510`) — intro movie only, not scripts.

Hence: fade darkens world+weapon+cockpit art; bars, titles, subtitles and "Skip" stay on top
at full brightness.
