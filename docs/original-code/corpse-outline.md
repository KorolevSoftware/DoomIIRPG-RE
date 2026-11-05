# Lootable corpse "outline" (pulsating halo)

Verified against `src/` on 2026-11-05. Raw log: `docs/research/2026-11-05-corpse-outline.md`.

There is no edge-detection outline. The "outline" is a **second, enlarged copy of the
corpse sprite drawn BEFORE (behind) the normal sprite with `RENDER_FLAG_PULSATE` (512)**.
The normal sprite then covers the centre, so only a rim of the enlarged copy stays
visible, pulsing in brightness.
In the reference build (TinyGL software path, see "Which path runs") the copy is a flat,
opaque grey/white silhouette, grey level 127..254.

## 1. Where (call sites)

All calls are `renderSprite(x, y, z, tile, frame, flags, renderMode=0, scale', renderFlags=512)`
followed by the regular draw of the same sprite.

| Case | Site | Frame | Scale |
|---|---|---|---|
| Placed corpse props, tiles 138/139/137 (`TILENUM_OBJ_CORPSE/OTHER_CORPSE/SCIENTIST_CORPSE`, `src/Enums.h:754-756`) | `src/Render.cpp:1714-1717`, then normal draw at `src/Render.cpp:1728` | current n7 | `18 * scaleFactor >> 4` |
| Dropped sentry-bot corpse (tiles SENTRY_BOT / RED_SENTRY_BOT, `entity->isDroppedEntity()`) | `src/Render.cpp:1719-1722`, then `:1728` | current n7 | `(18 * scaleFactor) >> 4` |
| Dead monster, stacked characters, `MANIM_DEAD` (112, `src/Enums.h:576`) | `src/Render.cpp:3470-3474` (`renderSpriteAnim`) | 13 | `(18 * scaleFactor) >> 4` |
| Dead floater, **sentinels only** (`renderFloaterAnim`, `isSentinel` sub-switch `src/Render.cpp:3501-3537`) | `src/Render.cpp:3528-3532` | 13 | `17 * scaleFactor >> 4` |
| Dead special boss, Pinky (`renderSpecialBossAnim`) | `src/Render.cpp:3599-3605` | 13 | `17 * scaleFactor >> 4` |
| Dead special boss, Mastermind / Arachnotron | `src/Render.cpp:3660`, `:3753-3758` | 13 | `17 * scaleFactor >> 4` |

No halo: other floaters (lost soul, cacodemon) — their MANIM_DEAD at `src/Render.cpp:3568-3571`
draws only the body; Vios (`src/Render.cpp:3619-3659`) — its switch at `:3624` has no
MANIM_DEAD case at all (only IDLE/IDLE_BACK, WALK, ATTACK, PAIN).

`RENDER_FLAG_PULSATE = 512` (`src/Render.h:40`). The halo is always drawn with
`renderMode = 0` (RENDER_NORMAL), regardless of the sprite's own mode.

Monsters (dead or alive, `monster != nullptr`) go through `renderSpriteAnim` and return
at `src/Render.cpp:1624-1626`, so the 138/139/137 test at `:1714` only applies to
monster-less placed props.

## 2. When (exact condition)

Placed props (`src/Render.cpp:1715`):
`entity->param == 0 && canvas->state != ST_CAMERA && !entity->hasEmptyLootSet()`

Dropped sentry bot (`src/Render.cpp:1720`): `entity->param == 0 && canvas->state != ST_CAMERA`
(no loot-set test).

Dead monsters (`src/Render.cpp:3471`, same text at `:3530` sentinels, `:3601` Pinky, `:3754` Mastermind/Arachnotron):
`entity->monster != nullptr && (entity->monster->flags & 0x800) == 0 && canvas->state != 18 && !entity->hasEmptyLootSet()`

- `ST_CAMERA = 18` (`src/Canvas.h:84`): no halo during scripted cinematics.
- `hasEmptyLootSet()` = `lootSet == nullptr || lootSet[0] == 0` (`src/Entity.cpp:2309-2311`).
- "Already searched" markers are set by `LootingSystem::poolLoot`: props get `++entity->param`
  (`src/LootingSystem.cpp:166-170`), monster corpses get `monster->flags |= 0x800`
  (`src/LootingSystem.cpp:173-177`). Save/restore: corpse binary state restores
  `++param; lootSet = nullptr` (`src/Entity.cpp:1920-1925`).
- Default loot: ET_CORPSE props get `lootSet[0] = 1089` unless subtype is SENTRY_BOT
  (`src/Entity.cpp:2008-2012`); monsters get a per-type entry (`src/Entity.cpp:2014-2046`).
- Not tied to targeting/selection. Always on while the condition holds; brightness is
  time-pulsed (below).

## 3. Algorithm

### Geometry (billboard branch, `src/Render.cpp:455-487`)
Same anchor, larger quad: half-width `n19` and height `n20` both scale with `scaleFactor`
(`:459-461`); the quad is horizontally centred and grows upward from the fixed base
(`viewMtxMove(..., (n22*2-1)*n19, n21*n20)`, `:485`). So with 18/16 the halo sticks out
1/16 of the width on each side and 2/16 of the height on top, none at the bottom
(17/16: 1/32 per side, 1/16 on top).

No depth test (`src/GLES.cpp` SetGLState: `glDisable(GL_DEPTH_TEST)`, `src/GLES.cpp:89`):
draw order = compositing order, halo first, body on top.

### Colour, software TinyGL path (`Render::setupPalette`, `src/Render.cpp:1968-1979`)
```
n7 = (app->time >> 2) & 0x1FF; if (n7 & 0x100) n7 = 511 - n7;   // triangle 0..255
grey = (n7 >> 1) + 127;                                           // 127..254
every palette entry = upSamplePixel(grey,grey,grey)  (RGB565)
```
Flat opaque grey silhouette (texel transparency kept by the trans span), fog ignored.
Period 2048 ms (512 steps x 4 ms).

### Colour, GL path (`gles::SetupTexture`, `src/GLES.cpp:719-733`)
```
glBlendFunc(GL_SRC_ALPHA, GL_ONE);                 // additive
t = app->time >> 2; a = t & 0x1FF; if (t & 0x100) a = (508 - a) + 3;   // = 511 - a
if (a < 31) a = 31;
glColor4ub(a, a, a, a);                            // GL_MODULATE with texture
```
Result: the texture itself (not flat) added at intensity (a/255)^2 x texel; range 31..255,
same 2048 ms period. Fog: RENDER_NORMAL sets `fogMode = 2` (`src/GLES.cpp:624-633`),
so fog stays enabled for the halo. The state block is only re-run when
`renderMode`/`flags` differ from the cached ones (`src/GLES.cpp:615`).

### Which path runs (CORRECTED 2026-11-05)
`gles::isInit` is a **user setting**, not a fixed choice:
- `Render::Init` calls `_gles->GLInit(this)` (`src/Render.cpp:164`) -> `isInit = true` (`src/GLES.cpp:44`),
  but `Game::loadConfig` then overwrites it from the config file:
  `_glesObj->isInit = IS.readBoolean();` (`src/Game.cpp:2036`, saved at `src/Game.cpp:1959`).
- It can be toggled at runtime with F1 (`src/Input.cpp:770-773`) or the Options item
  "TinyGL:" (`ACTION_TOG_TINYGL`, `src/MenuSystem.cpp:3357-3360`, label `src/MenuSystem.cpp:2414-2422`).
- With `isInit == false` every GLES entry point returns false (`src/GLES.cpp:305`, `:486`, `:552`)
  and TinyGL rasterises in software (`src/TinyGL.cpp:534`, `src/Render.cpp:509-510`).

The legacy build the user runs (`build/src/Doom2rpg.app/CONFIGFILENAME`, 791 bytes): the
bool sits right before the 16x10 int key map (`src/Input.h:86,94`, 640 bytes) and the
0xDEADBEEF marker. Byte 0x92 is `00` (after vibration 0x50=80 and dead zone 0x32=50,
little-endian ints), so **isInit = false: the legacy build runs the TinyGL software path**.
That matches the solid white silhouette the user sees.

### Final pixel formula (software path, what is on screen)
For every halo texel whose palette index is not `paletteTransparentMask` (`spanTransparent`,
`src/Span.cpp:59-63`, selected as `_spanTrans[RENDER_NORMAL]`, `src/Render.cpp:55-59`,
via `setupTexture`, `src/Render.cpp:2067-2069`):
`pixel = RGB565(g, g, g)` written **opaque** (overwrite, no blending), where
`tri = (time>>2) & 0x1FF; if (tri & 0x100) tri = 511 - tri; g = (tri>>1) + 127` -> 127..254
(`src/Render.cpp:1968-1979`, `upSamplePixel` = RGB888->RGB565, `src/Render.h:332`).
Transparent texels are skipped, so the shape is the exact sprite silhouette (scaled 18/16
or 17/16). Fog has no effect: the whole palette (including the fog palette passed in at
`src/Render.cpp:508`) is replaced by the flat colour. Period 2048 ms (`app->time` in ms).
The body sprite drawn afterwards overwrites the centre, leaving a light-grey pulsing rim.

The GL-path formula above (additive, textured) only applies when the user switches
TinyGL off; it is NOT what the reference build shows.

## 4. new_src hook points (as of 2026-11-05, not part of the original)

- `new_src/render/World3D.cpp:1187-1190` — `kManimDead` case of `drawCharacter`:
  comment "lootable pulsate halo deferred (§8)"; only the corpse quad is emitted.
- `new_src/render/World3D.cpp:716-743` — billboard branch of `drawSprite` (props 137-139 and
  dropped sentry bots would get the pre-draw here; torchiere glow is the precedent of an
  extra quad, `drawTorchiereGlow` at `new_src/render/World3D.cpp:1025`).
- `new_src/render/SceneRenderer.cpp:159-169` — routes corpsified monsters (MANIM_DEAD)
  to the stacked path.
- `new_src/domain/game/Entity.h:77` — `hasEmptyLootSet()` already exists.
- `new_src/core/GameStates.h:23` — `Camera = 18`.
- No sentinel/Pinky/Mastermind/Arachnotron dead halo (17/16) and no PULSATE mode in `new_src/render/api/RenderModes.h`
  (grep for "pulsat" finds nothing).
