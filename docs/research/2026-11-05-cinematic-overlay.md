# 2026-11-05 — What the original draws over the screen in ST_CAMERA

Hypothesis: the original darkens the screen during cinematics (letterbox / dim / fade).

Method: grep all `ST_CAMERA` uses in `src/`; read `Canvas::backPaint` (`src/Canvas.cpp:381-510`),
`Canvas::setState` ST_CAMERA (`:1207-1216`), `MayaCamera::Render` (`src/MayaCamera.cpp:302-324`),
`Render::drawRGB` (`src/Render.cpp:2934-2960`), `Render::fadeScene` (`:2548-2590`),
`Graphics::fade` / `fillRect` / `FMGL_fillRect` (`src/Graphics.cpp:88-206,755-766`, read with
`LC_ALL=C`), `Hud::draw` 0x10 block (`src/Hud.cpp:783-818`), `drawCinematicText` (`:455-456`).
Disassembled `tmp_map00.bin` with `tools/disasm_map_scripts.py --verify` (0 failures).

Verdict: PARTIAL. Darkening = opaque static black letterbox bars (0,0,480,42) and
(0,292,480,28) + script FADEOP fades (full-screen black, alpha `1 - w/256`, under the bars).
No translucent cinematic dim exists; nothing animates on state entry/exit.

Extra findings:
* `Graphics::fade` body exists at `src/Graphics.cpp:755-766` (fades.md previously said it was
  absent — corrected).
* Software path skips `fadeScene` entirely while `cockpitOverlayRaw` (`src/Render.cpp:2937-2939`).
* Dizzy wobble forced off in ST_CAMERA (`src/Render.cpp:2235-2237`).

Rewrite status (for the caller): `new_src/core/GameContext.cpp:1433-1436` already fills both
bars when `state_ == StateId::Camera`; `new_src/core/GameContext.cpp:1408` calls
`Hud::drawFade` (`new_src/ui/Hud.cpp:155-166`) before particles and bars. HUD panels are gated
off by `gameplayView` (`new_src/core/GameContext.cpp:1383-1384`).

Curated: `docs/original-code/cinematic-overlay.md`.
