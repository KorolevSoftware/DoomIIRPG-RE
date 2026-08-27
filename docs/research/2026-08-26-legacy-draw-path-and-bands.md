# 2026-08-26 — Which draw path the legacy binary runs; real canvas rects for world / cinematic / view weapon

## Hypothesis (from the caller)

The legacy reference binary shows the rifle noticeably larger (~1.4×) and lower
(down to the bottom HUD, canvas y ≈ 256) than our rewrite's predicted
(265,154)-(358,226). Prime suspect: the legacy build takes the software-TinyGL
branch of `Combat::drawWeapon` (anchors −62/−18, per-weapon nudges, 1.35× scale)
which `docs/original-code/combat.md` §1 had dismissed as "not the port target".

## Verdict

**PARTIAL / both refuted and confirmed:**

1. **REFUTED — there is no compile-time path switch.** `src/` is compiled with
   *zero* preprocessor definitions; TinyGL-vs-GL is a runtime bool
   `gles::isInit`, persisted in the game's config file.
2. **CONFIRMED — the binary the user ran was in software-TinyGL mode.** Its
   `CONFIGFILENAME` has `isInit = 0` at offset 146. That binary is also *not*
   built from this repo's `src/` (it contains a whole Video-options menu with a
   `TinyGL:` toggle whose strings do not exist in `src/`).
3. **The dismissal was harmless for the numbers but the numbers were wrong
   anyway.** The GL path is NOT a 1:1 pixel blit: `draw2DSprite` builds a
   world-space billboard whose projection magnifies it by **K ≈ 1.336 about the
   viewport centre**. The two paths therefore agree within ~2 px, and BOTH give
   a rifle at canvas ≈ (273,162)-(400,261) clipped at y = 255 — matching the
   user's observation. Our (265,154)-(358,226) is wrong on both paths.
4. **CONFIRMED — cinematics use the same GL viewport as gameplay.** `cinRect`
   never reaches `glViewport`; `gles::BeginFrame` overwrites its y with 65.
5. **CORRECTION to this report's first pass:** the *viewport* is identical in
   both states, but the **FOV is not** — `MayaCamera::Render` passes **315** in
   `ST_CAMERA` and 290 in `ST_DIALOG` (`src/MayaCamera.cpp:301-323`). So the
   cinematic aspect is `(315<<14)/31578` = **163**, not 150, and the cinematic
   weapon magnification is K ≈ **1.221**, not 1.336. There is no per-camera FOV
   in the map data. See §6 below.

## Method

* Build config: root `CMakeLists.txt` `BUILD_LEGACY` branch → `src/CMakeLists.txt`
  (target `DoomIIRPG_Legacy`, no `target_compile_definitions`);
  `build/CMakeCache.txt`; `build/src/CMakeFiles/DoomIIRPG.dir/flags.make`
  (`CXX_DEFINES =` empty).
* `grep '^#if'` over `src/*.cpp|*.h` → only `_WIN32` and `#if 0 // J2ME/IOS/iPhone`.
* Traced `Combat::drawWeapon` → `Render::draw2DSprite` →
  `gles::DrawWorldSpaceSpriteLine` → `gles::DrawModelVerts`, plus
  `gles::BeginFrame`/`SetGLState`, `TinyGL::setViewport/_setViewport/setView/
  buildViewMatrix/buildProjectionMatrix`, `Canvas::startup/setState`,
  `Render::render`, `Render::drawRGB`, `Hud::drawOverlay`.
* Parsed `UnPackGameData/tables.bin` (table 1 = wpinfo, table 9 = 65536-scale
  sine table) with the exact reader semantics of `src/Resource.cpp:148-152,
  209-223,236-276,278-328`.
* Parsed `build/src/Doom2rpg.app/CONFIGFILENAME` against
  `Game::saveConfig`/`loadConfig` (`src/Game.cpp:1913-2050`).
* Re-simulated both paths with C integer-truncation semantics in Python.

## Evidence

### 1. No compile-time switch; runtime `gles::isInit`

* `src/CMakeLists.txt` — GLOBs sources, `add_executable(DoomIIRPG_Legacy …)`,
  links SDL2/ZLIB/OpenAL/OpenGL/hash-library. No `add_definitions`, no
  `target_compile_definitions`.
* `build/src/CMakeFiles/DoomIIRPG.dir/flags.make`: `CXX_DEFINES = ` (empty),
  `CXX_FLAGS = -g -std=c++17 -arch arm64 … -Wno-write-strings -fsigned-char`.
  `build/CMakeCache.txt`: `BUILD_LEGACY:BOOL=OFF`, `CMAKE_BUILD_TYPE=Debug`.
  (The stale `build/` tree still names the legacy target `DoomIIRPG`; the binary
  is `build/src/DoomIIRPG`, dated 2025-05-27.)
* `src/GLES.h:40` `public: bool isInit;`
* `src/GLES.cpp:44` `this->isInit = true;` inside `gles::GLInit(Render*)`;
  only call site `src/Render.cpp:164` `this->_gles->GLInit(this);` inside
  `Render::startup` (`:35`), called from `src/App.cpp:107`.
* `src/Game.cpp:1959` `OS.writeBoolean(_glesObj->isInit);` /
  `src/Game.cpp:2036` `_glesObj->isInit = IS.readBoolean();` — the
  "[GEC] Port Configurations" block of `saveConfig`/`loadConfig`.
  `src/App.cpp:120` `this->game->loadConfig();` runs AFTER `:107`
  `this->render->startup()`, so the file wins.
* No UI in `src/` writes `isInit` (grep `isInit` = 24 hits, all reads except the
  two above). `strings build/src/DoomIIRPG` yields
  `Video Options / Window Mode: / Windowed / Borderless / FullScreen / VSync: /
  Resolution: / TinyGL: / Apply Changes` — none present in `src/` (`grep -rn
  "VSync:" src/` → nothing). ⇒ the reference binary is a different, extended
  tree.

### 2. The config file proves software mode

`build/src/Doom2rpg.app/CONFIGFILENAME`, 791 bytes, mtime 2026-08-26 19:02
(the user's session). Layout derived from `src/Game.cpp:1913-1970`
(`writeInt` = 4 bytes native LE, `src/JavaStream.cpp:315-322,10-24`;
`writeBoolean` = 1 byte, `:342-348`):

| off | field |
|---|---|
| 0 | int 11 (version) |
| 4…9 | difficulty, allowSounds, fxVol, musicVol, vibrate, enableHelp |
| 10…33 | 6 ints (language, deaths×2, animFrames, controlLayout, controlAlpha) |
| 34 | isFlipControls |
| 35…54 | 5 help bitmasks |
| 55…94 | 10 menu indexes |
| 95,96 | recentBriefSave, hasSeenIntro |
| 97…116 | 10 shorts numLevelLoads |
| 117…128 | totalPlayTime, currentGrades, bestGrades |
| 129 | int windowMode = **0** |
| 133 | bool vSync = **1** |
| 134 | int resolutionIndex = **0** |
| 138 | int gVibrationIntensity = **80** |
| 142 | int gDeadZone = **50** |
| **146** | **bool isInit = 0** |
| 147…786 | 16×10 ints keyBinds |
| 787 | int 0xDEADBEEF ✓ |

Size and trailer match exactly, so the field order is right.

### 3. GL path geometry (the port target)

* `gles::BeginFrame` (`src/GLES.cpp:111-141`):
  `posX = x + viewRect[0]`, `posY = y + viewRect[1]`, then
  `if (posY == 37) posY = 36+1; else posY = 64+1;` → **65** for every real call
  (gameplay `posY = 1+20 = 21`, cinematic `23+20 = 43`).
  `vPortRect = {posX, posY, w, h}`, used at `:82` `glViewport(vPortRect…)`
  (GL origin bottom-left).
  `transformCoord2f((float*)&vPortRect[0], …)` (`:140-141`) casts `int[4]`
  (`src/GLES.h:56`) to `float*`; the values are small ints ⇒ denormal floats
  whose bit pattern equals the integer, so multiplying by the retina factor is
  (accidentally) an exact integer scale.
* Gameplay chain: `viewRect = {0,20,480,250}` (`src/Canvas.cpp:124-127`) →
  `TinyGL::setViewport(0,20,480,250)` → `_setViewport(1,1,478,248)`
  (`src/TinyGL.cpp:149-167`) → `BeginFrame(1,1,478,248)` (`src/TinyGL.cpp:193`)
  → `glViewport(1,65,478,248)` → canvas **(1,7,478,248)** (top edge
  320−65−248 = 7, bottom edge 320−65 = 255).
* Projection: `viewAspect = (viewFov<<14)/((vpW<<14)/vpH)`
  (`src/Render.cpp:2223`); `viewFov = 290` from every `renderScene` call site
  (`src/GameStateRunner.cpp:267,274,290,308,315`, `src/LoadingManager.cpp:724`,
  `src/MiniGameManager.cpp:155,207`). With 478×248: `(478<<14)/248 = 31578`,
  `(290<<14)/31578 = 150`. `buildProjectionMatrix(290,150,…)`
  (`src/TinyGL.cpp:100-119`) with `tables.bin` table 9 (`sin[75] = 29106`,
  `sin[331] = 58718`, 65536-scale) gives `n5 = 7276`, `n6 = 14679`,
  `n4 = 31675`, **`m[5] = 33053`, `m[0] = 17098`**, `m[10] = −16384`,
  `m[11] = −16384`, `m[14] = −512` (halved to −256 by `src/GLES.cpp:174-176`).
* `Render::draw2DSprite` (`src/Render.cpp:343-421`):
  `v12 = 176·scaleFactor/0x10000`; screen verts (unused on GL) are BL/BR/TL;
  world verts `mv[0..2]` are built at `eye + 400·forward` (from
  `5·((view[k]&~31)+8·(view[k]>>5))>>8` = 400 for `view[k] = ±16384`) offset by
  `v27>>5` along `view[0]/[4]/[8]` (camera right) and `v28>>5` along
  `view[1]/[5]/[9]` (camera down), edge `v33>>5`. `v27/v28/v33` all divide by
  `viewportWidth` (`:389-391`).
* `gles::DrawWorldSpaceSpriteLine` (`src/GLES.cpp:483-547`) completes the quad
  (`verts[3] = v3 − (v2 − v1)`), sets s,t ∈ {0,1024} then
  `s = s·176/sWidth`, `t = t·176/tHeight` (`:539-542`) → the **top-left 176×176
  texels** of the 256×256 media; returns `DrawModelVerts` which is true unless
  `render->isSkyMap` (`:551-576`) ⇒ the software fallback at
  `src/Render.cpp:419-420` is never taken for the weapon.
* Closed form (derived in `docs/original-code/rendering.md` §6.2):
  `Δpx = (x − vpW/2)·m[0]/12800`, `Δpy = (y+v12 − vpH/2)·m[5]·vpH/(vpW·12800)`
  ⇒ **Kx = 17098/12800 = 1.33578, Ky = 33053·248/(478·12800) = 1.33975**.
  `m[0] = m[5]·aspect/fov` (`src/TinyGL.cpp:108`) is what makes them equal;
  the residual 0.3 % is the truncation of `aspect` 150.46 → 150.
* Rifle, weapon 0, idle, `sx = sy = 0`: `wpinfo[0..5] = (−13,88,−8,82,1,12)`
  (tables.bin table 1, payload byte 388) ⇒ `x = 196−13 = 183`,
  `y = 131−88 = 43`, `v12 = 176`.
  Integer simulation (`v27 = −3838`, `v28 = 6512`, `v33 = 12065`,
  `x_view = −120`, `y_view = 203`, edge 377, `w = 400`):
  * quad canvas **(165.2, 22.2)-(400.2, 258.0)**, 235.1 × 235.8 px
  * gun art (media 0 bounds x 81…174, y 104…177) canvas
    **(273.4, 161.5)-(398.9, 260.6)**, clipped at canvas y = 255
  * muzzle flash (`x+1+40, y+12+40` = (224,95), scale 0x8000 → v12 = 88):
    canvas **(219.4, 92.2)-(336.6, 209.8)**, 117.2 px

### 4. Software-TinyGL path (what the user actually saw) — cross-check

* `src/Combat.cpp:631-668`: `scrX = screenWidth/2 − 62 = 178`,
  `scrY = screenHeight/2 − 18 = 107` (`Render::screenWidth/Height` =
  `viewRect[2]/[3]` = 480/250, `src/Render.cpp:156-159`), `loweredWeaponY += 13`,
  `renderFlags |= RENDER_FLAG_SCALE_WEAPON` (= 1024, `src/Render.h:41`), no
  nudge for weapon 0.
* `src/Render.cpp:351-353`: `scaleFactor = (int)(scaleFactor · 1.35f)` →
  `(int)(65536·1.35f) = 88473`, `v12 = 176·88473/65536 = 237`.
* Raster frame: `TinyGL::setViewport` forces `posY = 3` when `!isInit`
  (`src/TinyGL.cpp:155-157`) → `_setViewport(1,4,478,248)`, and
  `this->pixels = backBuff + screenWidth·3 + viewRect[0]`
  (`src/TinyGL.cpp:164`) ⇒ raster row 0 = canvas row 3, band = canvas rows
  7…254, x 1…478 — the same band as GL. `Render::drawRGB` additionally forces
  `viewportY = 0, viewportHeight = 320` in this mode
  (`src/Render.cpp:2947-2951`).
* Result: quad canvas (165,22)-(402,259); gun art canvas
  **(274.1, 162.0)-(400.7, 261.7)** clipped at 255; flash 118 px at (221,94)
  (`xf = 40+15`, `yf = 40+20`, extra flag 0x400, `src/Combat.cpp:826-834`).
* ⇒ GL vs software differ by ≤ 2 px. The user's "1.4× wider, reaches the HUD"
  describes **both** paths; our rewrite is the outlier.

### 5. Cinematic band

* `Canvas::startup`: `cinRect = {viewRect[0], 42, viewRect[2], viewRect[3]}` =
  `{0,42,480,250}`, `CAMERAVIEW_BAR_HEIGHT = 20` (`src/Canvas.cpp:149-154`).
* `Canvas::setState` (`:1020`), `ST_CAMERA` branch `:1207-1216`:
  `app->tinyGL->setViewport(cinRect[0], cinRect[1], cinRect[2], cinRect[3])` —
  the ONLY consumer of `cinRect` as a rect.
* `TinyGL::setViewport` (`src/TinyGL.cpp:149-167`): `posX = 0`, `posY = 22`,
  `_setViewport(1, 23, 478, 248)`.
* `TinyGL::setView` (`:178`) line `:193`:
  `BeginFrame(viewportX=1, viewportY=23, 478, 248, view, projection)`.
* `gles::BeginFrame` (`src/GLES.cpp:119-127`): `posY = 23+20 = 43 ≠ 37` →
  `posY = 65`. **Same GL rect as gameplay**: `glViewport(1,65,478,248)`,
  canvas (1,7,478,248); `viewAspect` also unchanged at 150
  (`src/Render.cpp:2223`, `vpW/vpH` = 478/248 in both states).
* Letterbox = 2D art: `Hud::drawOverlay` (`src/Hud.cpp:620-625`) blits
  `imgCockpitOverlay` at `(cinRect[0], cinRect[1]) = (0,42)` anchor 0 and at
  `(cinRect[2], cinRect[1]) = (480,42)` anchor 24|4.
* Only the cinematic *weapon* uses the bar height: `scrY -=
  CAMERAVIEW_BAR_HEIGHT` when `state == ST_CAMERA && cinematicWeapon != −1`
  (`src/Combat.cpp:701-705`).
* The `posY == 37` branch is dead in practice: `posY = viewportY + 20`, and the
  only two viewport setters give 21 (gameplay) and 43 (cinematic).

## Numbers to implement (canvas 480×320)

| quantity | value |
|---|---|
| world band, gameplay AND cinematic | canvas `(1, 7, 478, 248)`; GL `glViewport(1, 65, 478, 248)` (y from bottom = `320 − 7 − 248`) |
| projection, gameplay | `fov = 290` → `aspect = (290<<14)/31578 = 150`, `m[0] = 17098`, `m[5] = 33053` |
| projection, cinematic (ST_CAMERA) | `fov = **315**` → `aspect = (315<<14)/31578 = **163**`, `m[0] = 15629`, `m[5] = 30203` (`src/MayaCamera.cpp:309`) |
| view-weapon frame | viewport-relative pixels, origin canvas (1,7), centre (239,124) = canvas (240,131) |
| view-weapon transform | `X = 240 + (x − 239)·Kx`, `Ybottom = 131 + (y + v12 − 124)·Ky`, `Ytop = Ybottom − v12·Ky`; clip to the band. Gameplay `Kx = 1.33578, Ky = 1.33975`; cinematic (fov 315) `Kx = 1.22102, Ky = 1.22423` |
| rifle idle quad | canvas (165, 22)-(400, 258) |
| rifle idle visible art | canvas **(273, 162)-(399, 261)**, clipped at y = 255 |
| muzzle flash | quad canvas (219, 92)-(337, 210) (88-px quad × K) |
| source texels | top-left 176×176 of the 256×256 media, always |
| cinematic letterbox | 2D cockpit-overlay art at y = 42, NOT a viewport change |

## Open questions

* The reference binary `build/src/DoomIIRPG` is not reproducible from `src/`
  (extra Video/Input options menu, `TinyGL:` toggle). Any future pixel
  comparison should either rebuild `DoomIIRPG_Legacy` from `src/` with
  `-DBUILD_LEGACY=ON` **and delete/patch `CONFIGFILENAME` so `isInit = 1`**, or
  explicitly state that the software path is being compared.
* `transformCoord2f`'s int→float* cast is exact only because the values are
  denormals; at very large window scales it would break. Not relevant to the
  rewrite.


## 6. Second pass (same day): the cinematic FOV is 315, not 290

Question from the coordinator: the rewrite uses fov 315 for cinematics; does
`src/` back that up?

**CONFIRMED — 315 is correct, and the reviewer's aspect 163 is correct.**

* Assignment (a literal argument, there is no fov *field*):
  `src/MayaCamera.cpp:301-323` —
  `ST_DIALOG → render(..., 290)` (`:306`), **`else → render(..., 315)`**
  (`:309`), followed by `renderPortal()` and
  `if (game->cinematicWeapon != -1) combat->drawWeapon(0,0);` (`:311-314`).
* Reached only through `MovementController::updateView`
  (`src/MovementController.cpp:377`), whose first branch is
  `if (app->game->isCameraActive() && canvas->state != Canvas::ST_INTER_CAMERA)
  { activeCamera->Update(...); activeCamera->Render(); return; }` (`:518-523`).
  `ST_CAMERA` dispatches there from `src/Canvas.cpp:948-953`.
* All other fov literals are 290; the only other variable fov is the scope zoom
  (`src/MovementController.cpp:528-541`, base `zoomFOV = zoomDestFOV = 190`
  `src/ZoomController.cpp:27`, `zoomDestFOV = 190 + ((-55·pct)>>7)` clamped
  `>= 102` `:120-121`, touch variant `110·x/15 + 80`
  `src/TouchController.cpp:148`) — and the view weapon is not drawn while
  zoomed.
* **No per-camera / per-key FOV exists in the map data.** `Game::setKeyOffsets`
  (`src/Game.cpp:576-584`) lays out exactly 7 planes over
  `totalMayaCameraKeys`: `OFS_MAYAKEY_X/Y/Z/PITCH/YAW/ROLL/MS`
  (`src/Game.h:35-41`); `MayaCamera::aggComponents` and
  `mayaTweenIndices[i*6+j]` carry the same six channels plus the MS duration
  (`src/MayaCamera.cpp:135-147,204-240`, `:290-299`). `grep -i fov
  MayaCamera.cpp MayaCamera.h Game.h` → no match. So 315 is a hard constant of
  the cinematic state, not a default that map00 happens to use.
* Consequences with the (unchanged) 478×248 viewport:
  `aspect = (315<<14)/((478<<14)/248) = 5160960/31578 = 163`;
  `buildProjectionMatrix(315,163)` → `n3 = 81`, `n4 = 31662`, `n5 = 7812`,
  `n6 = 14401`, **`m[5] = 30203`, `m[0] = 15629`** (hfov 92.70°, vfov 56.96°
  vs 87.56°/52.73° in gameplay);
  **`Kx = 15629/12800 = 1.22102`, `Ky = 30203·248/(478·12800) = 1.22423`**.
* Cinematic view weapon (`b2` true, `src/Combat.cpp:699-705`): `scrY -= 20`
  (`CAMERAVIEW_BAR_HEIGHT`) and `b5` forces the **attack pose**
  `wpX/wpY = wpAtkX/wpAtkY` (`src/Combat.cpp:735-737`). Quirk: the per-weapon
  `scrY` bias switch at `:679-693` runs on the *player's* weapon while `wpinfo`
  is then read for `game->cinematicWeapon` (`:701-705`). Worked example,
  `cinematicWeapon = 0`, player weapon 0: `x = 196−8 = 188`,
  `y = 111−82 = 29` → quad canvas (177.3, 14.4)-(392.2, 229.9), gun art canvas
  (276, 142)-(391, 232).
