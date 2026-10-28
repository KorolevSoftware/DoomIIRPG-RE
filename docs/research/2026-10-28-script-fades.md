# 2026-10-28 — Script fades (`EV_FADEOP`) and the Render fade

## Hypothesis / questions
Port `EV_FADEOP`: operand decode, flags passed to `Render::startFade`, blocking,
all-flag `fadeScene` behaviour, frame order, other fade readers, map00 FADEOP list.

## Method
Read `src/ScriptThread.cpp:938-958`, `src/Render.cpp:2527-2590,2934-2959`,
`src/Render.h:84-92`, `src/Canvas.cpp:381-510`, every `startFade/isFading/
getFadeFlags/endFade` call site (grep). map00 via
`python3 tools/disasm_map_scripts.py tmp_map00.bin --verify --ipa "build_new/new_src/Doom 2 RPG.ipa"`
(verify: 0 failures).

## Verdict
CONFIRMED with two corrections to earlier docs:
1. **bit15 = FADEIN, not FADEOUT.** `if (b6) startFade(v & 0x7FFF, 2)` /
   else `startFade(v, 1)` (`src/ScriptThread.cpp:950-954`); flag 2 = FADEIN,
   1 = FADEOUT (`src/Render.h:85-86`). `tools/disasm_map_scripts.py` `h_fadeop`,
   `cutscenes-camera.md` and `tile-events-vm.md` had it inverted (docs fixed;
   the tool is outside the researcher write scope and still prints the inverted
   label). The rewrite stub `new_src/domain/game/ScriptVM.cpp:1016-1020` logs the
   same inverted label.
2. `FADE_SPECIAL_FLAG_MASK = -29` = `~0x1C` keeps FADEOUT (`src/Render.h:91`);
   earlier doc said it strips FADEOUT|FADEIN.

## Evidence
- Decode/skip/non-blocking: `src/ScriptThread.cpp:940-957` — skipping:
  `if (b6) endFade()` else nothing; normal: startFade; only `break`.
- startFade/endFade: `src/Render.cpp:2535-2546` (time base `upTimeMs`).
- fadeScene: `src/Render.cpp:2552-2586`; early completion `t >= dur - 50`;
  FADEIN → `endFade()` (`:2572-2574`); FADEOUT → `fade(rect, 0, 0)` and keeps
  flags (`:2576-2579`); `alpha = 65280*((t<<16)/(dur<<8))>>16`, `256-alpha` for
  FADEOUT (`:2582-2585`). Colour always 0. `Graphics::fade` body absent
  (declared `src/Graphics.h:74`); the held-black end state passing 0 shows the
  argument is scene weight.
- Frame order: GL `src/Canvas.cpp:391-396`, SW `src/Canvas.cpp:401` +
  `src/Render.cpp:2955-2957`; both before particles/HUD/dialog
  (`src/Canvas.cpp:405-448`). ST_CAMERA VIEW3D from `src/MayaCamera.cpp:323`
  after the cockpit overlay (`:317-319`).
- Readers: `src/InputEventController.cpp:305` (DENYSKIP, never set),
  `src/Game.cpp:2530-2531`, `src/GameStateRunner.cpp:271-272,312-313`.
- map00 list: see `docs/original-code/fades.md` §6. Only 9671 (FADEOUT 800 →
  `CHANGE_MAP map=2`, no fade bit) is not followed by a FADEIN in-map.

## Rewrite gaps
1. `EV_FADEOP`: bit15 → `startFade(now, v&0x7FFF, 2)`, else `startFade(now, v, 1)`;
   when skipping a cinematic, FADEIN → endFade, FADEOUT → no-op.
2. `Hud::drawFade`: add FADEIN (overlay = 256-a clamped / scene weight a) and its
   self-clear at `t >= dur-50`; keep the FADEOUT hold. Extend, do not replace.
3. Draw order: rewrite paints the fade after HUD/dialog/loot
   (`new_src/core/GameContext.cpp:976-978`); the original paints it right after
   the 3D view (and cockpit overlay), before particles, HUD, cinematic bars,
   subtitles and dialog.
4. Skip-cinematic end: `startFade(500, FADEIN)` unless `flags & 0xC`.
5. Open: map02 entry must fade in from the 9671 held black (or the stats menu /
   loadMap path in the rewrite must handle it); rewrite level restart already
   calls endFade.

## Open questions
- `cutscenes-camera.md` §6 calls func_1889 the "new-game intro", but the CFG
  runs func_1913 (cameras 0,1,5) when `v17==0` and reaches func_1889 only when
  `v17!=0 && v15!=1` (bytecode 221-251). Meaning of v15/v17 not re-verified here.
