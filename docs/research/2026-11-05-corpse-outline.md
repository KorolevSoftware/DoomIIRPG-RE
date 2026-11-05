# 2026-11-05 — Corpse outline / highlight

## Hypothesis
The original draws an outline/highlight on corpses (ET_CORPSE = 9, tiles 137/138/139).

## Method
grep `src/` for TILENUM_OBJ_*_CORPSE, RENDER_FLAG_*, 512/0x200 in Render.cpp, followed
`renderSprite -> setupTexture -> gles::SetupTexture / Render::setupPalette`, checked loot
markers in LootingSystem.cpp and Entity.cpp, and checked GLES init.

## Verdict
PARTIAL / CONFIRMED in substance: not a geometric outline, but a pulsating enlarged
"halo" copy of the sprite drawn behind the corpse, only while the corpse is still
lootable and not during ST_CAMERA.

## Evidence
- `src/Render.cpp:1714-1717`: `if (entity->param == 0 && app->canvas->state != Canvas::ST_CAMERA && !entity->hasEmptyLootSet()) { this->renderSprite(x, y, z, n3, n7, n2, 0, 18 * scaleFactor >> 4, 512); }` then normal draw `:1728`.
- `src/Render.cpp:1719-1722`: dropped sentry bot, same without the loot test.
- `src/Render.cpp:3470-3474`, `:3528-3532`, `:3600-3604`, `:3754-3758`: MANIM_DEAD, `(monster->flags & 0x800) == 0 && state != 18 && !hasEmptyLootSet()`, frame 13, scale 18/16 (characters) or 17/16 (floaters, special bosses).
- Correction (review, verified): 17/16 halo only for sentinels (`src/Render.cpp:3501-3537`), Pinky (`:3599-3605`), Mastermind/Arachnotron (`:3660`, `:3753-3758`). Lost soul/cacodemon dead (`:3568-3571`) draw the body only; Vios (`:3619-3659`, switch `:3624`) has no MANIM_DEAD case.
- `src/Render.h:40`: `RENDER_FLAG_PULSATE = 512`.
- `src/Render.cpp:1968-1979`: software palette = flat grey `(tri>>1)+127`, tri = triangle of `(time>>2)&0x1FF`.
- `src/GLES.cpp:719-733`: GL `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`, `glColor4ub(a,a,a,a)`, a = triangle 0..255 clamped to >= 31.
- `src/LootingSystem.cpp:166-177`: searching sets `++param` (props) or `monster->flags |= 0x800` (monsters).
- `src/Entity.cpp:2309-2311`: `hasEmptyLootSet()`.
- ~~GL path always initialised -> GL halo is the default~~ WRONG. Correction (after user on-screen comparison): `isInit` is overwritten from the config (`src/Game.cpp:2036`), toggled by F1 (`src/Input.cpp:770-773`) / Options "TinyGL:" (`src/MenuSystem.cpp:3357-3360`). `build/src/Doom2rpg.app/CONFIGFILENAME` byte 0x92 = 00 -> isInit false -> TinyGL software. On screen: opaque flat grey `(tri>>1)+127` (127..254) on non-transparent texels (`src/Span.cpp:59-63`, `src/Render.cpp:1968-1979`), fog ignored.
- `src/GLES.cpp:89`: depth test disabled -> draw order is compositing order.

## Open questions
- GL state block is cached on (renderMode, flags) (`src/GLES.cpp:615`); two consecutive
  halo draws would reuse the first colour, practically irrelevant (each halo is followed
  by the body draw with different flags).
- `src/Render.cpp:1715` dereferences `entity` without a null check (props are assumed to have one).

Curated: `docs/original-code/corpse-outline.md`.
