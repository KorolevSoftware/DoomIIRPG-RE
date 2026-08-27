# ADR 0009 — Restore the legacy world viewport (canvas 1,7,478,248)

Date: 2026-08-26. Status: accepted (decided by the user);
**amended 2026-08-26 — deviation D1 refuted, cinematics now share the same
viewport (see "Amendment" below)**.
Related: spec `specs/2026-08-26-combat-stage1-fixes.md` §3,
`docs/original-code/rendering.md` §6,
`docs/research/2026-08-26-view-weapon-placement-audit.md`.

## Context

Until now `new_src/` rendered the 3D world into the whole 480x320 canvas with
`viewAspect = (290<<14)/((480<<14)/320) = 193`
(`new_src/render/RenderBackend.cpp:59-69`,
`new_src/core/GameContext.cpp:1276-1277`).

The original GL path renders the world into a strictly smaller viewport:
`Canvas::startup` builds `viewRect = {0,20,480,250}` (`src/Canvas.cpp:124-127`),
`TinyGL::setViewport` shrinks it by one pixel per side to 478x248
(`src/TinyGL.cpp:149-167`) and `gles::BeginFrame` emits
`glViewport(1, 65, 478, 248)` — canvas rect **(1, 7, 478, 248)**, centre
(240,131) — with `viewAspect = (290<<14)/((478<<14)/248) = 150`
(`src/GLES.cpp:119-127`, `src/Render.cpp:2223`; rendering.md §6.1).

Two consequences of our deviation were visible in play:

* The eye level / horizon sat at canvas y = 160 instead of 131 and the vertical
  FOV was 68° instead of 53° (whole world subtly wide-angle).
* Every screen-space overlay whose legacy coordinates are *viewport-relative*
  landed wrong. `Render::draw2DSprite` places quads in pixels relative to the
  viewport top-left (rendering.md §6.2), so the first-person weapon anchored at
  `(196,131)` must appear at canvas `(197,138)`. Ours drew at canvas (196,131) —
  ~40 px too high — on top of the separate 256→176 source-rect bug.

Two options were on the table (audit "open questions"): keep the full-canvas
render and re-anchor the weapon to the new centre (`scrX 197`, `scrY 167`), or
restore the legacy viewport and use the legacy anchors verbatim.

## Decision

Restore the legacy world viewport. The world renders into canvas rect
**(1, 7, 478, 248)** with `viewAspect = (290<<14)/((478<<14)/248)` (= 150), and
the view weapon uses `scrX = 197`, `scrY = 138` (legacy anchors plus the
viewport origin), keeping the existing `wpinfo` / per-weapon-bias math and the
`y = scrY − (wpY + sy)` convention.

Mechanically: `RenderBackend::setCanvasViewport(1,7,478,248)` wraps only the
`drawSky`/`drawBSP` pair; everything else (weapon, HUD, cockpit overlay,
dialogs, loot menu) draws after `restoreCanvasViewport`, i.e. in full canvas
space. Both viewport calls flush the sprite batch first so batched 2D quads are
never retro-scaled.

## Consequences

* Every future screen-space overlay taken from `draw2DSprite` can use legacy
  numbers with the constant offset (+1, +7) — no per-feature re-derivation.
* World geometry, sprite placement and fog are unaffected (MVP-driven); only the
  projection aspect and the pixel rect change.
* The canvas rows y 0…19 / 255…319 and columns x = 0 / 479 are no longer covered
  by the world. The HUD top panel (480x20 at y 0) and the bottom panel
  (`gameMenu_Panel_bottom.bmp`, 480x64 at y 256, `src/TouchController.cpp:544-545`)
  cover them; the leftover 1 px seams (x 0, x 479, y 255) are black in the
  original too and are accepted as faithful.
* The bottom panel had to start being drawn during gameplay (background image
  only; its widgets still read demo fields).
* HUD coordinate math is untouched: `viewRect[1] = 20` remains the frame for the
  monster health bar and messages.

### Deviations kept for now

* **D1 — cinematic path unchanged. REFUTED 2026-08-26, see the amendment
  below.** ~~We keep our `cinRect {0,42,480,250}` viewport and aspect 210
  because the cinematic framing was tuned and eye-validated during the key-0
  work (`specs/2026-08-26-camera-key0.md`).~~ The original does NOT use a
  separate cinematic GL rect; keeping ours caused a user-visible 36 px viewport
  step at cutscene entry.
* **D2 — no `RENDER_FLAG_SCALE_WEAPON`** (1.35x) and no TinyGL software anchors:
  the rewrite targets the GL path only (`src/Render.cpp:351-353`). **Open
  follow-up:** it is not yet established which rasterizer path the legacy
  reference binary actually compiles. If it is the software TinyGL path, the
  view-weapon geometry of this ADR (anchors `197/138`, 176x176 quad, no 1.35x
  scale) may be the wrong branch — the software path applies a 1.35x weapon
  scale flag and different anchors (`src/Combat.cpp:631-668`). A researcher is
  establishing this; do not pre-empt the answer here. The world viewport rect
  and the aspect formulas of this ADR are unaffected either way (they come from
  `gles::BeginFrame` / `Render::buildProjectionMatrix`, shared by both paths);
  only the weapon numbers may need a follow-up decision (and, if so, a new ADR
  amending D2).

## Amendment 2026-08-26 — one viewport for gameplay AND cinematics (D1 refuted)

**What D1 claimed:** that our separate cinematic viewport
`cinRect {0,42,480,250}` with `viewAspect = (315<<14)/((480<<14)/320) = 210`
was a harmless, eye-validated deviation, safe to revisit later.

**Why it is wrong.** The original renders cinematics into the *same* GL rect as
gameplay; the letterbox is painted, not projected. Chain, verified end to end:

1. `Canvas::render` passes the cinematic rect `(0, 42, 480, 250)` to the
   viewport setter (`src/Canvas.cpp:1215`).
2. `TinyGL::setViewport` rebases it against `viewRect` and shrinks one pixel per
   side → `(1, 23, 478, 248)` (`src/TinyGL.cpp:152-166`).
3. `TinyGL::flush` forwards exactly that to `gles::BeginFrame(1, 23, 478, 248)`
   (`src/TinyGL.cpp:193`).
4. `gles::BeginFrame` **discards the y**: `posY = (posY + 20 == 37) ? 37 : 65`,
   i.e. `1 + 20 = 21 ≠ 37` → `posY = 65` (`src/GLES.cpp:119-127`) — the very
   same override that produces the gameplay rect.

So the cinematic GL rect in canvas space is **(1, 7, 478, 248)**, byte-identical
to gameplay.

> **CORRECTION 2026-08-26 (orchestrator).** The sentence that followed here —
> "the cinematic letterbox comes from the cockpit overlay art anchored at
> `cinRect[1] = 42`" — is **REFUTED**. The letterbox is two *opaque black fills*
> painted after the world pass: `(0,0,480,42)` and `(0,292,480,28)`
> (`src/Hud.cpp:455-456`, `eraseRgn` = `setColor(0)` + `fillRect` with forced
> alpha 1.0). The top fill **overpaints the top 35 rows of the world band**, so
> the visible world during a cinematic is rows 42..254, not 7..254. They are
> gated on `state == ST_CAMERA` (`src/Canvas.cpp:1213`,
> `src/MovementController.cpp:394`) and are therefore ABSENT during a dialog over
> an active camera (`src/Canvas.cpp:1095`) — the picture deliberately grows 35
> rows upward while a dialog box is up.
> The cockpit art is a separate, *script-toggled* effect (opcode 76
> `EV_TOGGLE_OVERLAY`, `src/ScriptThread.cpp:1710-1713`), off by default, enabled
> on map00 only around the drop-ship arrival shot; it is 240x234 blitted twice
> (plain at `(0,42)` and mirrored at `(480,42)`), covering rows 42..275.
> The viewport conclusions above are unaffected. Full evidence:
> `docs/original-code/cutscenes-camera.md` §7,
> `docs/research/2026-08-26-cinematic-letterbox.md`. The missing bars were a
> user-visible defect in our build, found by playtest on the same day.

**Consequence of the deviation (the defect it caused).** Our cinematic band
`(0,42,480,250)` put the cinematic projection centre at canvas y = 42 + 125 =
**167**, while gameplay after this ADR sits at **131** — a 36 px jump downward
the instant a cutscene starts. The user reported precisely this ("during
cutscenes the viewport slides down"). Before this ADR the same mismatch existed
but was only 7 px (gameplay centre 160 vs 167), which is why the key-0 eye-check
did not catch it: D1's "eye-validated" argument was validated against a
full-canvas gameplay render that no longer exists.

**Corrected decision.** One world viewport for both paths:

* rect: `kWorldRect` = canvas **(1, 7, 478, 248)** for gameplay *and* cinematics
  (`src/GLES.cpp:119-127`);
* gameplay aspect: `(290 << 14) / ((478 << 14) / 248)` = 150 (unchanged);
* cinematic aspect: fov **315** with
  `(315 << 14) / ((478 << 14) / 248)` = **163** — replaces 210
  (`src/Render.cpp:2223`, rendering.md §6.1);
* `kCinRect {0,42,480,250}` is demoted to an **overlay anchor only**: the
  cockpit `drawOverlay(g, 0, 42, 480)` call keeps its coordinates verbatim
  (`src/Hud.cpp:623-624`). It must no longer be passed to
  `setCanvasViewport`.

**Related inconsistency this also removes.** A maya camera can be active while
`state != Camera`; that path already rendered with the gameplay rect while using
the cinematic fov, so the two cinematic entry paths disagreed with each other.
With a single rect they are consistent by construction.

**Framing changes on purpose.** Cutscenes now get a wider vertical FOV (aspect
163 instead of 210) and a world band at canvas y 7…254 instead of 42…291, while
the cockpit overlay stays at y = 42. The previously eye-validated key-0 framing
is therefore *not* a reason to keep the old numbers — it was validated against a
now-refuted premise and needs a fresh eye-check (see the spec's acceptance
criteria).

## Rejected alternatives

* **Keep the full-canvas 480x320 render, re-anchor the weapon to
  `scrX 197 / scrY 167`.** Cheaper (one file), but permanently forks every
  screen-space anchor from the original, keeps the 68° vertical FOV and the
  horizon 29 px low, and would have to be undone the moment a zoom/scope or
  another `draw2DSprite` overlay is ported.
* **Letterbox by drawing black bars over a full-canvas render.** Same FOV error,
  plus wasted fill.
* **Scale the weapon art by 256/176 instead of windowing the UV.** Contradicts
  `src/GLES.cpp:539-542`; the remaining 80 texel rows/columns are never shown by
  the legacy path.
