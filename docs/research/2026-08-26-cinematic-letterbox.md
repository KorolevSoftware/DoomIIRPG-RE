# 2026-08-26 — Cinematic letterbox: exact geometry, gating and draw order

## Hypothesis (from the caller)

"The cinematic letterbox is produced by the cockpit-overlay art anchored at
`cinRect[1] = 42` (`src/Hud.cpp:620-625`)" — as recorded in the amendment to
`docs/architecture/adr/0009-legacy-world-viewport.md` and in
`docs/original-code/rendering.md` §6.3. Observed defect: after making the
cinematic world render into the gameplay band `(1,7,478,248)`, the black bar
ABOVE the cockpit is missing in `new_src/`.

## Verdict

**REFUTED (attribution) / CONFIRMED (viewport chain).**
The letterbox is NOT the cockpit art. It is two opaque black `eraseRgn` fills
issued by `Hud::drawCinematicText` (`src/Hud.cpp:455-456`), painted AFTER the
world pass, and the top one **overpaints the top 35 rows of the world band**.
The cockpit art is an independent, script-toggled effect that is off by default
and enabled in map00 for camera 0 only.

## Method

* `grep -rn "cinRect\|CAMERAVIEW_BAR_HEIGHT" src/` → 4 rect writes
  (`src/Canvas.cpp:149-154`), 1 viewport call (`:1215`), 2 HUD fills
  (`src/Hud.cpp:455-456`), 2 cockpit blits (`:623-624`), particles
  (`src/ParticleSystem.cpp:205-208`), scroll text
  (`src/IntroSequenceManager.cpp:243`), weapon shift (`src/Combat.cpp:703`),
  subtitle (`src/Hud.cpp:469`).
* Read `Hud::drawCinematicText` (`src/Hud.cpp:447-491`), `Hud::draw`
  (`src/Hud.cpp:723-818`), `Canvas::backPaint` (`src/Canvas.cpp:381-...`),
  `Canvas::run` state branches (`:805-826`, `:919-925`, `:949-958`),
  `Canvas::setState` (`:1020-1216`), `MovementController::updateView`
  (`:390-395`), `MayaCamera::Render` (`src/MayaCamera.cpp:302-324`),
  `Graphics::eraseRgn/fillRect/FMGL_fillRect/drawRegion`
  (`src/Graphics.cpp:88-104`, `:250-258`, `:302-400`), `Image::DrawTexture`
  rotate modes (`src/Image.cpp`).
* Independent cross-check of the gate: `ScriptThread.cpp:538-539`
  (`hud->repaintFlags = 16`) and `ScriptThread.cpp:1710-1713`
  (`cockpitOverlayRaw ^= 1`, opcode 76 `EV_TOGGLE_OVERLAY`,
  `src/Enums.h:476`); `Hud::Hud` memset (`src/Hud.cpp:24-26`) proves the
  default is off; `Render::drawRGB` early-returns on the same flag
  (`src/Render.cpp:2937-2940`).
* Script cross-check with `tools/disasm_map_scripts.py tmp_map00.bin`:
  `TOGGLE_OVERLAY` appears at IP 1945 and 2062 only, bracketing
  `STARTCINEMATIC camera=0` (IP 1946); all 13 `CAMERA_STR` sites are inside
  ST_CAMERA cinematics; no `START_INTERCINEMATIC` in map00.
* `cockpit.bmp` size read from the BMP header inside the .ipa: 240x234.

## Evidence

Rect derivation (canvas 480x320, `src/App.h:36-37`, `src/Canvas.cpp:49-52`):
`softKeyY = 320`, `screenRect = {0,0,480,320}`, `viewRect = {0,20,480,250}`,
`cinRect = {0,42,480,250}` (`src/Canvas.cpp:100-154`).

```
src/Hud.cpp:455  graphics->eraseRgn(0, 0, canvas->displayRect[2], canvas->cinRect[1]);
src/Hud.cpp:456  graphics->eraseRgn(0, canvas->cinRect[1] + canvas->cinRect[3], canvas->displayRect[2],
                                    canvas->softKeyY - (canvas->cinRect[1] + canvas->cinRect[3]));
```
→ `(0,0,480,42)` and `(0,292,480,28)`; `eraseRgn` = colour 0 + `fillRect`, and
`fillRect` passes `a = 1.0` (`src/Graphics.cpp:88-95`, `:250-256`) = opaque black.

Gate: `setState(ST_CAMERA)` → `app->hud->repaintFlags = 24` (0x08|0x10)
(`src/Canvas.cpp:1213`); every camera frame
`app->hud->repaintFlags &= 0x18` (`src/MovementController.cpp:394`); the
post-draw clear `repaintFlags &= ~0x10` is commented out (`src/Hud.cpp:800`).
ST_DIALOG assigns 47 and the frame ends at 0x2B (`src/Canvas.cpp:1095`,
`:919-925`) → no bars; ST_INTER_CAMERA assigns 43 (`src/Canvas.cpp:1081`) →
no bars.

Order: full-screen `glClear` (`src/Main.cpp:125-126`) → world + portal +
cinematic weapon + optional cockpit art (`src/MayaCamera.cpp:302-324`, inside
`canvas->run`) → `backPaint`: fade-3D, particles, `hud->draw`
(`src/Canvas.cpp:391-417`) → 0x10 block: **bars**, title, subtitle, bubbles,
Skip label (`src/Hud.cpp:799-818`) → canvas fade (`src/Canvas.cpp:499-510`).

Numbers: world band rows 7..254 (rendering.md §6.3); top bar blacks rows 7..41
of it; visible world during a cinematic = rows 42..254. Bottom bar rows
292..319. Rows 255..291 stay black only because of the per-frame clear.

Text: title `(240, 1)` HCENTER|TOP (`src/Hud.cpp:465`); subtitle
`n4 = (292 + ((320-292-32)>>1)) - 10 = 280`, second line `+16 = 296`
(`src/Hud.cpp:469-470`, `:481-486`); Skip `drawString(text, 478, 320, 40)`
= RIGHT|BOTTOM (`src/Hud.cpp:813`).

Cockpit art: 240x234 twice — `(0,42)` plain and `(480,42)` flags 24 = RIGHT|16
with `rotateMode 4` = `glScalef(-1,1,1)` mirror (`src/Hud.cpp:623-624`,
`src/Graphics.cpp:349-368`, `src/Image.cpp` DrawTexture case 4) →
covers `(0,42,480,234)`, rows 42..275.

`CAMERAVIEW_BAR_HEIGHT = 20` has exactly one consumer:
`Combat::drawWeapon`, `scrY -= CAMERAVIEW_BAR_HEIGHT` when
`state == ST_CAMERA && cinematicWeapon != -1` (`src/Combat.cpp:675`, `:701-704`).

## Edge case found (port artifact, documented not recommended)

`EV_CAMERA_STR` **assigns** `hud->repaintFlags = 16`
(`src/ScriptThread.cpp:538-539`) and bit 0x10 is never cleared after drawing in
this port (`src/Hud.cpp:800`), so a subtitle fired outside ST_CAMERA would turn
the bars on until the next `setState`. In map00 no `CAMERA_STR` runs outside
ST_CAMERA, so it never manifests; the J2ME original cleared the bit after
drawing. For the rewrite the safe gate is `state == Camera`.

## Curated docs updated

* `docs/original-code/cutscenes-camera.md` — new §7 (bars, cockpit art, order,
  per-state matrix, text placement).
* `docs/original-code/rendering.md` §6.3 — corrected the letterbox attribution.
* `docs/architecture/adr/0009-legacy-world-viewport.md` — NOT edited (outside
  the researcher write scope); its amendment's letterbox attribution needs the
  same correction.

## Open questions

* Rows 276..291 (below the cockpit art, above the bottom bar) are covered only
  by the per-frame `glClear` in this port; on the real device the framebuffer is
  persistent, so a stale strip could have been visible there. Not verifiable
  from `src/`.
* `imgPlayerFaces->width` (used for the `showCinPlayer` subtitle offset) was not
  measured; only `Hud_Player.bmp` would give the exact x.
