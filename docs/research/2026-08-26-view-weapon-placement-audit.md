# 2026-08-26 — View-weapon placement audit (new_src vs src)

## Hypothesis / question
User report: the first-person rifle is drawn TOO HIGH and looks SMALLER than in
the original. Audit `GameContext::drawViewWeapon` against
`Combat::drawWeapon` + `Render::draw2DSprite` (GL path) and produce exact
numeric deltas.

## Method
1. Read `src/Combat.cpp:621-844` verbatim (anchors, biases, wpinfo, lerp, flash).
2. Followed the draw call down: `src/Render.cpp:343-421` (quad build) →
   `src/GLES.cpp:483-548` (UV window, corner order) → `src/GLES.cpp:110-200`
   (glViewport) → `src/TinyGL.cpp:100-200` (viewport/projection) →
   `src/Canvas.cpp:114-154` (viewRect).
3. Parsed `UnPackGameData/newMappings.bin` (media dims/bounds) and
   `UnPackGameData/tables.bin` (tables 1 and 2) directly to verify constants,
   plus `gameMenu_Panel_bottom.bmp` / `HUD_Panel_top.bmp` BMP headers from the ipa.
4. Read the rewrite: `new_src/core/GameContext.cpp:605-695`,
   `new_src/render/World3D.cpp:100-135,319-368`,
   `new_src/render/Graphics2D.cpp:37-42`, `new_src/render/gl/SpriteBatch.cpp:206-232`,
   `new_src/render/RenderBackend.cpp:41-69`, `new_src/io/Tables.cpp:7-47`.

## Verdict
**PARTIAL** — doc §1 of `combat.md` was correct on every constant it listed
(anchors, 176×176 top-left quad, biases, wpinfo table, flash tile/frame/offset/
scale, SHOTHOLD×10), but incomplete on two facts that decide the visual result:
the fixed 176-texel UV window and the viewport-relative coordinate frame. Its
parenthetical "idle quad (183,43)-(359,219) lands the rifle at ≈(239-303,71-164)"
described a 256→176 downscale, i.e. the rewrite's bug, not the original.
Both rewrite defects (too high, too small) are **CONFIRMED** and localised.

## Evidence

### Original
* `src/Combat.cpp:627-628` `int scrX = (Applet::IOS_WIDTH / 2) - 44; int scrY =
  (Applet::IOS_HEIGHT / 2) - 29;` → 196 / 131 (`src/App.h:36-37`).
* `src/Combat.cpp:679-693` scrY += 3 (w1) / 10 (w2) / 12 (w3-6); weapon 0 gets
  no bias.
* `src/Combat.cpp:709` `sy = -std::abs(sy);`
  `:786-787` `int x = scrX + (wpX + sx); int y = scrY - (wpY + sy);`
* `src/Combat.cpp:839` `draw2DSprite(getWeaponTileNum(weapon), frame, x, y,
  flags, renderMode=0, renderFlags, 0x10000)`.
* `src/Render.cpp:358-373` `v12 = (176*scaleFactor)/0x10000`, verts
  (x,y+v12) / (x+v12,y+v12) / (x,y) → 176×176, top-left anchor.
* `src/GLES.cpp:539-542`
  `verts[i].s = (verts[i].s * 176) / sWidth; verts[i].t = (verts[i].t * 176) / tHeight;`
  with s,t ∈ {0,1024} → UV window = top-left 176×176 texels of a 256×256 media.
* `src/GLES.cpp:119-127` `posY = (posY == 37) ? 37 : 65;` + `:82`
  `glViewport(vPortRect…)` → gameplay GL viewport `(1, 65, 478, 248)` canvas
  units, GL y from bottom → canvas top edge y = 7.
* `src/TinyGL.cpp:166` `_setViewport(posX+1, posY+1, w-2, h-2)` on
  `viewRect = {0,20,480,250}` (`src/Canvas.cpp:124-127`) → 478×248.
* `src/Render.cpp:389-391` v27/v28/v33 all divided by `viewportWidth`;
  `src/Render.cpp:2223` `viewAspect = (viewFov<<14)/((vpW<<14)/vpH)` and
  `src/TinyGL.cpp:108-111` `m[0] = m[5]·aspect/fov` ⇒ isotropic 1:1 pixels,
  viewport centre (239,124).
* `src/Combat.cpp:826-834` flash = tile `getWeaponTileNum(0)` = 1, **frame 3**,
  at `(x + wpFlashX + 40, y + wpFlashY + 40)`, renderMode **5** (`RENDER_ADD50`
  = `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` + `glColor4f(.5,.5,.5,1)`,
  `src/GLES.cpp:660-664`), scale `0x8000` → 88×88 quad, sampling the same
  176×176 texel window (0.5×).
* `src/Combat.cpp:199` `flashTime = 1;` `:311-321` `animTime =
  weapons[w*9+8] (×10, or ×5 with statusEffects[2]); flashDoneTime =
  animStartTime + flashTime;` `:747-767` lerp `n15 = (gameTime −
  animStartTime)<<16 / animTime` — the lerp clock starts at animStartTime, it
  does **not** subtract flashTime.
* Data parses: tables.bin table 1 payload @byte 388, weapon 0 =
  `(-13,88,-8,82,1,12)`; table 2 payload @byte 482, weapon 0 field 8
  (SHOTHOLD) = 50 → animTime 500 ms. newMappings: tile 1 → media 0…3, all
  256×256; media 0 bounds x 81…174 / y 104…177, media 3 bounds x 5…163 / y 3…164.
* HUD bars: `HUD_Panel_top.bmp` 480×20 at (0,0) (`src/Hud.cpp:239`),
  `gameMenu_Panel_bottom.bmp` 480×64 at y 256
  (`src/TouchController.cpp:544-545`).

### Rewrite
* `new_src/core/GameContext.cpp:620-621,625,631-636,664-665` — anchors, bias,
  wpinfo all correct (Tables payload offset verified,
  `new_src/io/Tables.cpp:14-31`: table 1 = `[80+offsets[0]+4, …)` = byte 388).
* `new_src/core/GameContext.cpp:693`
  `g.drawImage(*tex, 0, 0, tex->width(), tex->height(), x, y, 176, 176, 0);`
  — full 256×256 source → 0.6875× downscale (BUG 1).
* `new_src/core/GameContext.cpp:674-675` same for the flash
  (256→88 = 0.34375× instead of 0.5×).
* `new_src/render/gl/SpriteBatch.cpp:222-232` — src rect is in texels, dst
  top-left anchored, v0 = srcY/texH (row 0 at the top): the same convention as
  the original, so only the numbers are wrong.
* `new_src/render/World3D.cpp:100-134` — `decodeSpriteRLE` writes into a full
  `width*height` buffer at absolute coordinates, so a (0,0,176,176) sub-rect is
  meaningful and matches the original UV window.
* `new_src/render/RenderBackend.cpp:59-69` — the world is rendered on the full
  480×320 canvas viewport; `new_src/core/GameContext.cpp:1276-1277` camera aspect
  `(290<<14)/((480<<14)/320)` = 193 (original 150) (BUG 2 / open design point).
* `new_src/core/GameContext.cpp:653-656` — lerp t subtracts `c.flashTime`
  (1 ms) which the original does not; cosmetic (0.2% of a 500 ms recoil).
* Flash blend: plain alpha `drawImage` instead of `RENDER_ADD50`.

## Numbers (rifle, weapon 0, idle, no shake)
| quantity | original | rewrite |
|---|---|---|
| anchor scrX/scrY | 196 / 131 (viewport-relative) | 196 / 131 (canvas) |
| quad top-left | vp (183,43) → canvas (184,50) | canvas (183,43) |
| quad size | 176×176 | 176×176 |
| source texels | (0,0)-(176,176) of 256×256 | (0,0)-(256,256) |
| art scale | 1.0× | 0.6875× |
| visible gun rect | canvas (265,154)-(358,226) | canvas (239,114)-(303,165) |
| flash quad | 88×88 from 176×176 texels (0.5×) | 88×88 from 256×256 (0.34×) |

Vertical error = 40 px (gun top) to 62 px (gun bottom) too high; size error
= 0.6875×.

## Open questions
* Should the rewrite restore the legacy 478×248@(1,7) world viewport (then the
  weapon anchor is literally `196/131` plus `(1,7)`), or keep full-canvas
  480×320 and re-anchor the weapon to the view centre (`scrX = 197`,
  `scrY = 167`)? The FOV also differs (vfov 150 vs 193 angle units, hfov 87.4°
  vs 90.5°), so this affects the whole world render, not just the gun.
* `posY == 37` branch in `gles::BeginFrame` — which state produces it (all
  gameplay/cinematic paths compute 21 or 43)? Not needed for the gun.
