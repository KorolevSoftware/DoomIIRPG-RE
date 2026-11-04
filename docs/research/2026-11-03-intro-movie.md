# 2026-11-03 — Intro movie (ST_INTRO_MOVIE 2): research R1-R3 for G6b

Requested by: orchestrator, `docs/architecture/front-end.md` §9 (R1-R3).
Curated result: `docs/original-code/front-end.md` §3.4.

## Questions
* R1: layout of tables 14/15 (`Game::loadTableCamera(14, 15)`), and can `new_src/core/MayaCamera` consume them?
* R2: phase-1 pan math over `prolog.bmp` inside the 220-px band; timing; 1500 ms fade-in; music 1068.
* R3: scrolling crawl of text 0:133: wrap width, line height, start/end, clip, mapping to the rewrite font.

## Method
* Read `src/IntroSequenceManager.cpp:173-245, 752-878`, `src/Game.cpp:511-584`,
  `src/MayaCamera.cpp:36-397`, `src/Resource.cpp:148-311`, `src/Graphics.cpp` (drawRegion,
  drawString, drawChar, clipRect, setScreenSpace, fade), `src/Text.cpp:744-905`,
  `src/Canvas.cpp:49-160, 418-428, 499-509, 828-845, 1176-1184`, `src/App.h:39-41`.
* Extracted `tables.bin`, `prolog.bmp`, `strings.idx`, `strings00.bin` from the .ipa into the
  scratchpad and decoded them with throwaway read-only scripts that mirror
  `Resource::loadShortTable/loadByteTable` and `Resource::loadFileIndex`
  (`src/Resource.cpp:169-200`) / `Localization::loadTextFromIndex` (`src/Text.cpp:179-212`).
* Simulated `MayaCamera::Update` (`src/MayaCamera.cpp:46-148`) over the shipped bytes.
* Line wrap of 0:133 computed by compiling a tiny harness against the rewrite's
  `new_src/text/Text.cpp` (`wrapText(42)`), which ports `src/Text.cpp:756-827`.

## Verdicts
* R1 CONFIRMED (fully decoded). Table 14 = 75 int16: `[5 keys, sampleRate 125, posShiftRaw 0
  (posShift 4), angleRaw 10]`, 35 keys channel-major, 30 tween indices, 6 tween counts
  `126,126,0,0,0,(0)`. Table 15 = 252 int8 (126 X + 126 Y). Keys:
  X `303,450,460,296,137`, Y `146,137,230,221,225`, Z 0, pitch 768, yaw 768, roll 0,
  MS `4133,4167,3333,4300,0`. Camera x/y after `>> posShift` are prolog.bmp pixels.
  The rewrite's `MayaCamera` can consume it unchanged via a synthetic one-camera `MapData`
  (`totalMayaCameraKeys = 5`, tweenCounts `{126,126,0,0,0,0}`); `CinematicCamera` cannot.
* R2 CONFIRMED. `texX = max(0,camX-240)`, `texY = max(0,camY-160)`, `dstY = max(0,160-camY)`,
  dest x 0, clip band (0,50,480,220); equivalent to drawing the bmp at `(-texX, 160-camY)`.
  First phase-1 frame = key 0 via `Update(-1)` → `Snap(-1)` → `NextKey` (reads ms of
  index `OFS_MS-1` = 0). End: key 4 has ms 0 → `complete` one frame after key 3 ends.
  Pan ≈ 15.93 s. Fade-in: black, `app->time`, 1500 ms, linear, full screen. Music 1068 looped.
* R3 CONFIRMED. Wrap 42 chars (`(480-8)/CHAR_SPACING[0]=11`), 9 lines (+1 → 10),
  spacing 32 px, 1600 ms per 32 px (20 px/s), end 28800 ms, text origin
  `y = 250 - off` in a (0,50,480,220) screen space → screen `300 - off + 32k`, HCENTER at 240.
  Background prolog (0,65,480,307) at (0,0), darkened by black alpha 63/255.
  Maps 1:1 to the rewrite font (same 12×16 / 9-px advance, same centring code).

## Evidence (key quotes)
* `this->posShift = 4 - (int)array[buffPos++];` — `src/Game.cpp:533`.
* `int posX = app->game->mayaCameras->x >> (uVar3 & 0xff);` — `src/IntroSequenceManager.cpp:809`.
* `if (i2 >= (mayaCameraKeys[game->OFS_MAYAKEY_MS + n3] & 0xFFFF)) { if (this->keyThreadResumeCount > 0) this->Snap(i);` — `src/MayaCamera.cpp:72-75`.
* `else if (this->isTableCam) { this->NextKey(); }` — `src/MayaCamera.cpp:371-373`.
* `canvas->fadeDuration = 1500;` — `src/IntroSequenceManager.cpp:791`;
  `int alpha = ((app->time - this->fadeTime) << 8) / this->fadeDuration;` — `src/Canvas.cpp:500`.
* `wrapText((canvas->displayRect[2] - 8) / Applet::CHAR_SPACING[app->fontType])` — `src/IntroSequenceManager.cpp:180`.
* `this->scrollingTextEnd = n * (this->scrollingTextLines + (n2 - 2));` — `src/IntroSequenceManager.cpp:218`.
* `graphics->drawString(canvas->dialogBuffer, canvas->SCR_CX, canvas->cinRect[3] - (...), this->scrollingTextSpacing, 1, 0, -1);` — `src/IntroSequenceManager.cpp:243`.
* `this->FMGL_fillRect(rect[0], rect[1], rect[2], rect[3], r, g, b, 1.0f - a);` — `src/Graphics.cpp:765`.

## Open questions
* Whether the screen outside the band is erased each frame in phase 1 (the paint loop
  before `playIntroMovie`, `src/Canvas.cpp:418-425`, was not traced); in phase 3
  `eraseRgn(displayRect)` does it explicitly (`src/IntroSequenceManager.cpp:229`).
* Phase 0 draws nothing from `playIntroMovie`; what is on screen during that single frame
  depends on the same unverified paint prologue.
* The 9-line wrap was computed with the rewrite's `Text::wrapText` port, not by running `src/`.
