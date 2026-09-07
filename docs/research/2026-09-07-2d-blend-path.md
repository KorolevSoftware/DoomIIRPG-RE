# 2026-09-07 — Which mode switch does the muzzle flash hit, and is it modulated?

## Hypotheses

* **H1 (coder)** — the muzzle flash at `src/Combat.cpp:833` goes through
  `Render::draw2DSprite` → `gles::SetupTexture` and is therefore drawn as
  `RENDER_ADD50`: additive with `glColor4f(0.5, 0.5, 0.5, 1)`, i.e. half bright.
* **H2 (doubt)** — the 2D layer has its own mode switch
  (`Image::setRenderMode`, `src/Image.cpp:158-210`), so the 0.5 modulation may
  not apply to the flash at all.

## Method

1. Read `Combat::drawWeapon` around the flash call site.
2. Followed `Render::draw2DSprite` (`src/Render.cpp:343-421`) statement by
   statement down to the GL draw call.
3. Read `Render::setupTexture` (`src/Render.cpp:2043-2058`) for mode overrides,
   and `Render::renderMode` / `RENDER_DEFAULT` to evaluate them.
4. Read `gles::SetGLState` (`src/GLES.cpp:77-108`) to check whether it clobbers
   the color/blend state written by `SetupTexture` before the draw.
5. Cross-checked the software rasterizer (span tables + `Render::setupPalette`).
6. Located the real UI blit path by finding the callers of `Image::DrawTexture`
   (note: `grep` in `src/` needs `-a`; several files are ISO-8859 + CRLF and
   are otherwise treated as binary and silently skipped — this is why an
   earlier scan looked like `DrawTexture` had no callers).
7. Enumerated every `drawImage`/`drawRegion`/`fillRegion` call site with a
   brace-matching Python scan to extract the actual `renderMode` argument, then
   resolved the variable ones (`normalRenderMode`, `highlightRenderMode`,
   `fontRenderMode`, local `renderMode`).

## Verdict

**H1 CONFIRMED / H2 REFUTED for the flash** (H2's premise — that there is a
separate 2D switch — is true, but that switch is on a different path and is
never reached by `draw2DSprite`).

## Evidence

### 1. Full chain of the flash

`src/Combat.cpp:833`:

```cpp
app->render->draw2DSprite(this->getWeaponTileNum(0), 3, x + wpFlashX + xf, y + wpFlashY + yf,
                          flags, 5, (!app->render->_gles->isInit) ? 0x400 : 0, 0x8000);
```

* `src/Render.cpp:343` `Render::draw2DSprite(...)`
* `src/Render.cpp:357` `this->setupTexture(tileNum, frame, renderMode, renderFlags);`
* `src/Render.cpp:2056-2058` `if (this->_gles->isInit) { this->_gles->SetupTexture(tileNum, frame, renderMode, renderFlags); }`
* `src/Render.cpp:2065-2074` span selection for the software path;
  `src/Render.cpp:385` `this->setupPalette(app->tinyGL->getFogPalette(0x40000000), renderMode, renderFlags);`
* `src/Render.cpp:414` `this->_gles->SetGLState();`
* `src/Render.cpp:415` `bool v37 = this->_gles->DrawWorldSpaceSpriteLine(...);`
  → `src/GLES.cpp:483` → `gles::DrawModelVerts` → `glDrawElements` (`src/GLES.cpp:571`)
* `src/Render.cpp:416` `this->_gles->ResetGLState();`
* `src/Render.cpp:418` software fallback `app->tinyGL->drawClippedSpriteLine(...)`
  when the GL draw returned false.

`Image::setRenderMode` is **not** on this path. "2D" in `draw2DSprite` refers
only to the screen-pixel positioning; the quad is re-projected into world space
(`src/Render.cpp:387-412`) and drawn by the sprite pipeline.

### 2. The modulation is applied

`src/GLES.cpp:660-664`:

```cpp
case Render::RENDER_ADD50: {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // glBlendFunc(GL_ONE, GL_ONE);
    glColor4f(0.50f, 0.50f, 0.50f, 1.0f);
    this->fogMode = 0;
```

`5 == Render::RENDER_ADD50` (`src/Render.h:23`). Texture env is the fixed
`GL_MODULATE` re-asserted at `src/GLES.cpp:619`, and vertex colors are disabled
(`glDisableClientState(GL_COLOR_ARRAY)`, `src/GLES.cpp:99`), so
`C = 0.5 · C_tex`, `A = A_tex`.

Nothing between `SetupTexture` and the draw resets it: `gles::SetGLState`
(`src/GLES.cpp:77-108`) issues no `glColor*` and no `glBlendFunc`; it only
invalidates the cache (`this->renderMode = -1; this->flags = -1;`
`src/GLES.cpp:107-108`), which also guarantees the color does not leak into the
next sprite (the weapon body at `src/Combat.cpp:839` re-enters
`case RENDER_NORMAL` and sets `glColor4f(1,1,1,1)`, `src/GLES.cpp:631`).

No mode override interferes:

* `src/Render.cpp:2049-2054` rewrites the mode only for `ST_AUTOMAP` or the
  debug pipeline mask; `Render::renderMode` defaults to `RENDER_DEFAULT = 31`
  (`src/Render.cpp:48`, `src/Render.h:82`) → bit `0x10` set, bit `0x20` clear →
  the passed mode 5 is forwarded unchanged.
* `src/GLES.cpp:609-611` forces `RENDER_SUB` only for tile 212.
* `renderFlags == 0` on the GL path (`0x400` is `RENDER_FLAG_SCALE_WEAPON`,
  `src/Render.h:41`, applied only in the TinyGL branch at
  `src/Render.cpp:352-354`), so the `PULSATE`/`*_SHIFT` overrides at
  `src/GLES.cpp:717-757` do not run.

**Software cross-check (independent back-end, same answer):**
`_spanTrans[RENDER_ADD50]` aliases the additive span (`src/Render.cpp:72` →
`spanAddTransparent`, `src/Span.cpp:171-185`) and `Render::setupPalette` halves
the palette for mode 5: `array[n11] = (spanPalette[n11] & 0xFFFFE79C) >> 1`
(`src/Render.cpp:2002-2006`, `:2018-2020`). Same "add half the texel".

### 3. The two switches compared

The UI blit path is
`Graphics::drawImage` (`src/Graphics.cpp:302`) → `Graphics::drawRegion`
(`src/Graphics.cpp:308`) → `Image::DrawTexture` (`src/Graphics.cpp:397`, impl
`src/Image.cpp:54`) → `Image::setRenderMode` (`src/Image.cpp:63`).

Numbering is the *same namespace* (0/1/2/12/13 have identical meaning), but
`Image::setRenderMode` implements only `{0, 1, 2, 3, 8, 12, 13}` and its
`default:` is a bare `return` with no GL calls (`src/Image.cpp:205-206`).
Differences: mode 0 has an opaque fast path (`GL_REPLACE`, blend+alpha-test off
when `!isTransparentMask`, `src/Image.cpp:163-171`); mode 3 uses
`glBlendFunc(GL_SRC_COLOR, GL_ONE)` with `GL_REPLACE` (`src/Image.cpp:182-188`)
instead of the sprite path's `(SRC_ALPHA, ONE)` + `glColor4f(1,1,1,1)`; mode 8 is
UI-only (font color from `Graphics::charColors`, `src/Image.cpp:189-194`) and
asserts in the sprite path (`src/GLES.cpp:703-705`). Modes 4/5/6/7/9/10/11 do
not exist in the UI layer. Full table: `docs/original-code/rendering.md` §8.8.

### 4. Modes actually used by the UI layer

Exhaustive call-site scan (see §8.8 for the per-line list): **0** (163/170
sites), **1 `BLEND25`** (`src/MenuSystem.cpp:177`, `:242`, `:252`, `:259`,
`:4454`, `:4464`; `src/SentryBotGame.cpp:686`, `:699`;
`src/VendingMachine.cpp:533`/`536`, `:554`/`558`), **2 `BLEND50`** (all fonts via
`Applet::setFontRenderMode(2)` — `src/App.cpp:514`, read at
`src/Graphics.cpp:651`; `src/MenuSystem.cpp:5204-5207`, `:5256-5259`, `:4217`,
`:4448`; `src/TravelMapManager.cpp:438`, `:441`, `:442`), **8** (font color,
`src/Graphics.cpp:654`), **12 `BLEND75`** (`src/Canvas.cpp:333`, `:339`, `:345`;
`src/MiniGameManager.cpp:39`, `:45`; `src/SentryBotGame.cpp:110`, `:117`;
`src/VendingMachine.cpp:84`, `:102`), **13 `BLENDSPECIALALPHA`**
(`src/Canvas.cpp:291-310`; `src/Hud.cpp:1039`, alpha fed from
`m_controlAlpha * 0.01f` in `src/Button.cpp:171-172`, `:185-186`, `:196-197`,
`:217-218`). Mode 3 has no UI call site — dead code.

## Answers

1. `Combat.cpp:833` → `Render::draw2DSprite` (`src/Render.cpp:343`) →
   `Render::setupTexture` (`:357`) → `gles::SetupTexture` (`src/Render.cpp:2057`)
   → `gles::SetGLState` (`:414`) → `DrawWorldSpaceSpriteLine` (`:415`). It hits
   `gles::SetupTexture`, not `Image::setRenderMode`.
2. Yes — `glColor4f(0.50f, 0.50f, 0.50f, 1.0f)` at `src/GLES.cpp:662`, combined
   by the permanent `GL_MODULATE` env (`src/GLES.cpp:619`, `:92`).
3. Same numbering, different (partial) implementation — table above and in §8.8.
4. **Half brightness**: the original adds `A_tex · 0.5 · C_tex` per pixel
   (`src/GLES.cpp:661-662`; software equivalent `src/Render.cpp:2002-2006` +
   `src/Span.cpp:171-185`).
5. UI elements with modulated modes: fonts and softkey plates (`BLEND25`/
   `BLEND50`), travel-map grid lines (`BLEND50`), menu/vending/sentry-bot
   buttons (`BLEND25`, `BLEND75`), touch controls and the d-pad
   (`BLENDSPECIALALPHA` driven by `m_controlAlpha`), colored text (mode 8).
   No additive mode is used anywhere in the UI layer.

## Open questions

* `Image::setRenderMode` mode 0 disables `GL_BLEND` entirely when the image has
  no transparent mask — a port must re-enable blending for the next primitive.
  Whether the original relies on that (order-dependent state) at any specific
  UI site was not audited.
* `Image::setRenderMode` never restores `GL_ALPHA_TEST`/`GL_BLEND` to a known
  state on `default:`, so mode 4..11 in the UI would render with leftover
  state. Confirmed unreachable in `src/`, but worth an assert in the rewrite.
