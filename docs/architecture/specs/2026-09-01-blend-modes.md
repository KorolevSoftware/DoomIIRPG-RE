# Spec — sprite blend modes (`RENDER_*`) with color modulation, and the torchiere glow

Date: 2026-09-01
ADR: `adr/0019-blend-mode-table-and-color-mod-uniform.md`
Research: `docs/original-code/rendering.md` §8 (§8.1-§8.7),
`docs/research/2026-08-31-blend-modes.md`, §7 for the animation context.
Touched files: `new_src/render/World3D.h`, `new_src/render/World3D.cpp` only.
Threading: single-threaded GL loop, all work inside `World3D::begin()/end()`.

## 0. Where we are (verified 2026-09-01)

| Fact | Location |
|---|---|
| `applyBatchState` handles **only** modes 3 and 7; everything else falls to alpha blending | `new_src/render/World3D.cpp:426-446` |
| **No color modulation anywhere** — the world fragment shader has no color term | `new_src/render/World3D.cpp:76-96` |
| Fog exists and works (eye-space linear, `uFogEnabled/uFogStart/uFogEnd/uFogColor`) | `new_src/render/World3D.cpp:60-96`, `:237-259`, `begin()` `:379-386` |
| Fog is currently switched off for modes 3 and 7 only (`renderMode != 3 && renderMode != 7`) — modes 4/5/6/10 wrongly keep fog on | `new_src/render/World3D.cpp:429` |
| Batching: triangles accumulate in `vertices_`, one `glDrawArrays` per `flush()`; flushed on texture change and on `applyBatchState` change | `:414-421`, `:475-484`, `:1008-1017`, `:426-446` |
| The per-sprite mode is read from `S_RENDERMODE` and applied before any emission | `:684-685` |
| `postProcessSprites` port writes modes 3/7/2/4 at load | `new_src/domain/game/Game.cpp:67-81` |
| Wall/floor geometry always draws with `applyBatchState(0)` — the legacy per-tile geometry modes are not ported | `new_src/render/World3D.cpp:449-452` |
| `drawSky` uses the same program **without** `begin()`, setting its own uniforms | `new_src/render/World3D.cpp:530-560` |
| Sprite textures are uploaded lazily by `mediaId` via `ensureSpriteTexture` | `:341-378` |
| `MediaLoader::finalize` loads **all** 1024 media, not just the map's registered ids, so off-map tiles (e.g. tile 193) are available | `new_src/io/Media.cpp:69-150` |

Net: modes 1/2/4/5/6/9/12 currently draw as plain `RENDER_NORMAL`, and the ADD
family draws at full strength with fog left on for 4/5/6.

map00 census (from `tmp_map00.bin`, tile id = `info & 0xFF`, `+257` when
`0x400000`):

| tile | mode per §8.4 | count on map00 | notes |
|---|---|---|---|
| 130 `OBJ_FIRE` | 3 ADD | 4 | sprites 0, 25, 31, 32 |
| 242 `FIRE_BALL` | 3 ADD | 4 | sprites 145-148 |
| 178 `GLASS` | 3 ADD | 10 | wall/flat branch (`ORIENTED` bits set) |
| 212 `SCORCH_MARK` | 7 SUB | 3 | sprites 159-161, all at tile (2,22), `FLAT` |
| 136 `OBJ_TORCHIERE` | 0 (glow is hardcoded ADD50) | 8 | sprites 5, 6, 141-144, 195, 196 |
| 161, 208, 234, 236, 244, 240, 479 | 2 / 3 / 3 / 3 / 4 / — / 0 | **0** | not present on map00 |

So map00 exercises modes 0, 3, 7 statically; modes 1/2/4/5/6 are only reachable
through spawned effect sprites (not ported yet) **and** through the torchiere
glow of group G2 — which is why G2 is the visual proof that modulation works.

---

## G1 — Blend-mode table + `uColorMod` uniform (infrastructure)

Files: `new_src/render/World3D.h`, `new_src/render/World3D.cpp`. No new files, no
CMake reconfigure.

### G1.1 Shader

In `kWorldFragment` (`new_src/render/World3D.cpp:76-96`) add

```
uniform vec4 uColorMod;
```

and, immediately after the palette fetch (`vec4 col = texture(uPalette, ...)`)
and **before** the fog block:

```
col *= uColorMod;   // fixed-pipeline GL_MODULATE, src/GLES.cpp:620
```

Order is load-bearing: in the fixed pipeline the texture env runs first and fog
is applied to the already-modulated fragment, so an alpha-blend mode with
`Acolor = 0.5` must still fog its RGB (`src/GLES.cpp:709-715`).

`Aout = Atex · Acolor` follows automatically from `col *= uColorMod` because our
palette LUT already carries `A = 0` for the transparent key `0xF81F`
(`Texture::uploadIndexed(..., killMagenta = true)`, call sites `:294`, `:369`).
That is what keeps additive modes from filling the sprite's bounding box.

Cache the location in `initialize()` next to the existing ones
(`:205-210`): `locColorMod_ = shader_.uniform("uColorMod");` and add
`GLint locColorMod_ = -1;` beside `locFogEnabled_` etc. (`World3D.h:180`).

### G1.2 The table

File-local (anonymous namespace) in `World3D.cpp`, next to the other constants:

```cpp
// gles::SetupTexture switch, src/GLES.cpp:615-706; fog per src/GLES.cpp:709-715.
// docs/original-code/rendering.md §8.2. Index = legacy Render::RENDER_* value.
struct BlendMode {
    GLenum src, dst;   // glBlendFunc
    float  mod[4];     // legacy glColor4f primary color (GL_MODULATE)
    bool   fog;        // fogMode == 2
};
```

Rows (all 14 — `RENDER_MAX = 14`, `src/Render.h:31`):

| idx | name | src | dst | mod (r,g,b,a) | fog | citation |
|---|---|---|---|---|---|---|
| 0 | NORMAL | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,1 | true | `src/GLES.cpp:625-635` |
| 1 | BLEND25 | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,0.25 | true | `:636-641` |
| 2 | BLEND50 | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,0.50 | true | `:642-647` |
| 3 | ADD | `GL_SRC_ALPHA` | `GL_ONE` | 1,1,1,1 | false | `:648-653` |
| 4 | ADD75 | `GL_SRC_ALPHA` | `GL_ONE` | 0.75,0.75,0.75,1 | false | `:654-659` |
| 5 | ADD50 | `GL_SRC_ALPHA` | `GL_ONE` | 0.50,0.50,0.50,1 | false | `:660-665` |
| 6 | ADD25 | `GL_SRC_ALPHA` | `GL_ONE` | 0.25,0.25,0.25,1 | false | `:666-671` |
| 7 | SUB | `GL_ZERO` | `GL_ONE_MINUS_SRC_COLOR` | 1,1,1,1 | false | `:672-678` |
| 8 | UNK | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,1 | true | see G1.4 |
| 9 | PERF | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,0.50 | true | `:679-685` |
| 10 | NONE | `GL_ZERO` | `GL_ONE` | 1,1,1,1 | false | `:686-690` |
| 11 | (unused) | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,1 | true | see G1.4 |
| 12 | BLEND75 | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,0.75 | true | `:691-696` |
| 13 | BLENDSPECIALALPHA | `GL_SRC_ALPHA` | `GL_ONE_MINUS_SRC_ALPHA` | 1,1,1,1 | true | `:697-701`, see G1.4 |

Notes that must survive as comments:

* Additive modes deliberately keep `GL_SRC_ALPHA` as the **source** factor; the
  original's `glBlendFunc(GL_ONE, GL_ONE)` is commented out at `src/GLES.cpp:650`.
  Result: `dst += Atex·(k·Ctex)` — transparency still cuts the sprite out and a
  glow never becomes an opaque white blob.
* Mode 7 keeps the GL meaning `dst = dst·(1 − Csrc)`; the software rasterizer's
  true subtract (`src/Span.cpp:230-244`) is a divergence of the original itself
  (ADR 0019).

### G1.3 `applyBatchState`

Rewrite the body of `World3D::applyBatchState(int renderMode)`
(`new_src/render/World3D.cpp:426-446`) as:

1. `int mode = renderMode;` — if `mode < 0 || mode >= 14`, log once (G1.4) and
   use `0`.
2. `const BlendMode& bm = kBlendModes[mode];`
3. `const bool fogOn = bm.fog && fogEnabled_;`
4. `if (mode == currentRenderMode_ && fogOn == currentFogOn_) return;`
5. `flush();` — pending vertices must draw under the **old** state/uniform.
6. `glBlendFunc(bm.src, bm.dst);`
7. `shader_.setVec4("uColorMod", bm.mod[0], bm.mod[1], bm.mod[2], bm.mod[3]);`
8. `shader_.setInt("uFogEnabled", fogOn ? 1 : 0);`
9. `currentRenderMode_ = mode; currentFogOn_ = fogOn;`

Keep the existing signature and keep the existing comment style. The compare
must use the *clamped* `mode`, not the raw argument, so an out-of-range value
does not defeat the cache.

`begin()` (`:379-397`) must now also write the identity modulation so state and
tracker agree: `shader_.setVec4("uColorMod", 1.f, 1.f, 1.f, 1.f);` alongside the
existing `currentRenderMode_ = 0;`.

`drawSky` (`:530-560`) must write `uColorMod = (1,1,1,1)` after `shader_.use()`,
right where it sets `uTexture`/`uPalette`. Uniforms live in the program object
across frames; without this line a torchiere glow's `0.5` from frame N darkens
the sky of frame N+1 (ADR 0019 point 4). `drawSky` also does not set the fog
uniforms — leave that as is, it is pre-existing behavior.

### G1.4 Unreachable modes (8, 11, 13) and `RENDER_NONE`

* 8 `RENDER_UNK` hits `default: assert(0)` in the original
  (`src/GLES.cpp:702-705`) and no call site can produce it for a 3D sprite
  (`docs/research/2026-08-31-blend-modes.md`, open questions). We must not crash
  a shipping frame: treat as mode 0 and emit **one** `fprintf(stderr, ...)` per
  distinct offending value (a `static bool` per value or a 16-entry
  `static uint8_t` table) naming the sprite mode. Same for 11 and any
  out-of-range value.
* 12/13 are 2D-only in the original (`src/Image.cpp:195-204`); 12 has a genuine
  factor (0.75) so keep its row exact, and 13 uses `canvas->blendSpecialAlpha`
  which the rewrite has no analog for — pin it to `1.0` and log once if it is
  ever selected.
* 10 `RENDER_NONE` is implemented as a normal table row
  (`GL_ZERO, GL_ONE`, draws nothing) with `mod = (1,1,1,1)`. Do **not** add an
  early-out in `drawSprite`, and do **not** reproduce the original's
  primary-color leak (`src/GLES.cpp:684-688` sets no color): it is unobservable
  because nothing is written, and reproducing it would make the uniform depend on
  draw history. Rationale in ADR 0019.

### G1.5 Fog

Fog is already implemented in the shader and driven by `fogEnabled_` +
`setFog()`; G1 only fixes *which* modes disable it, by taking `bm.fog` from the
table instead of the hand-written `renderMode != 3 && renderMode != 7`. Nothing
else about fog changes. `fogMode == 1` (`fogBlack`) is dead in the original
(`src/GLES.cpp:711`, never written) — do not port it.

### G1 acceptance

* Builds clean; `cmake --build build_new -j 8`.
* On map00 the picture must be **unchanged** relative to today for everything
  except the fog of ADD sprites: modes 0/3/7 have `mod = (1,1,1,1)` so the fire
  (tile 130, sprites 0/25/31/32), the fireballs (242), the glass (178) and the
  scorch marks (212, tile (2,22)) look exactly as before. G1 alone is a no-op on
  screen — that is the pass condition (no darkened world, no vanished sprites,
  no black sky).
* Sky: walk/turn for several seconds — the sky must never dim or flicker
  (the `drawSky` uniform guard).
* Console: no "unknown render mode" lines during a map00 walkthrough.

---

## G2 — Torchiere glow (first consumer of ADD50)

Original: `src/Render.cpp:1642-1647`, curated in
`docs/original-code/rendering.md` §8.5.

```cpp
else if (n3 == Enums::TILENUM_OBJ_TORCHIERE) {              // tile 136
    int zheight = ((10 * scaleFactor) / 65536) << 4;        // src/Render.cpp:1644
    n2 ^= (n7 & 0x1) << 17;                                 // :1645  horizontal-flip flicker
    this->renderSprite(x, y, z + zheight, Enums::TILENUM_SFX_LIGHTGLOW1, 0,
                       n2, Render::RENDER_ADD50, scaleFactor, n10);   // :1646
}
// no return -> falls through to the body draw at src/Render.cpp:1728
```

Verified data for the port: tile 193 → `mediaMappings[193] = 798`, single frame
(`mappings[194] = 799`); media 798 is 256x256, texel size 31443 ≠ 65536 ⇒
column-RLE, bounds `(0,176,1,177)`. Tile 136 → media 736, also RLE. Both
therefore take the **same** `useBounds == false` branch of `drawBillboardPart`
(`new_src/render/World3D.cpp:974-1005`) with the fixed 518/1036 extents, so the
glow quad is exactly the size of the body quad, as §8.5 states.

### G2.1 New constants

In the same anonymous namespace as the other tile constants
(`new_src/render/World3D.cpp:167-181` region):

```cpp
constexpr int kTileNumObjTorchiere   = 136; // src/Enums.h TILENUM_OBJ_TORCHIERE
constexpr int kTileNumSfxLightGlow1  = 193; // src/Enums.h:796 TILENUM_SFX_LIGHTGLOW1
constexpr int kRenderAdd50           = 5;   // src/Render.h:24 Render::RENDER_ADD50
```

(`render/` keeps duplicating tile ids locally instead of including
`domain/game/Enums.h` — spec 2026-08-25-character-animation §10, ADR 0007 pt 5.)

### G2.2 New private helper

`World3D.h`, next to `drawBillboardPart` (`World3D.h:104-112`):

```cpp
// Emits the torchiere's additive light-glow quad (tile 193 SFX_LIGHTGLOW1,
// frame 0) in RENDER_ADD50, before the lamp body — legacy tile-136 branch
// src/Render.cpp:1642-1647 (docs/original-code/rendering.md §8.5). Leaves the
// batch state on ADD50; the caller restores its own mode. Must be called
// between begin()/end().
void drawTorchiereGlow(const MapData& map, const MediaLoader& media,
                       int x, int y, int zRenderUnits, int flags, int scaleFactor);
```

Body (`World3D.cpp`):

1. `const int zheight = ((10 * scaleFactor) / 65536) << 4;` — with the default
   sprite scale byte 64, `scaleFactor = 64 << 10 = 65536` ⇒ `zheight = 160`,
   i.e. +10 map-Z units in the `z << 4` render units this function already uses
   (`src/Render.cpp:1644`).
2. Resolve the media: `const auto& m = media.mappings(); if (kTileNumSfxLightGlow1 >= (int)m.mappings.size()) return;`
   `int lo = m.mappings[kTileNumSfxLightGlow1]; if (lo < 0) return;`
   `const int mediaId = lo;` (frame **0** is hardcoded in the original).
3. `if (!spriteTexByMedia_.count(mediaId) && !ensureSpriteTexture(media, kTileNumSfxLightGlow1, mediaId)) { log once; return; }`
   — tile 193 is not a map00 sprite, so `uploadMapTextures` never preloaded it;
   the lazy path is the one used here. `MediaLoader::finalize` loads all 1024
   media (`new_src/io/Media.cpp:110-150`), so `texelIndexFor(798)` is valid; if
   it ever is not, the one-shot log is the diagnosis.
4. `applyBatchState(kRenderAdd50);`
5. `drawBillboardPart(map, media, x, y, zRenderUnits + zheight,
    kTileNumSfxLightGlow1, mediaId, flags, scaleFactor);`
   `drawBillboardPart` binds its own texture and flushes on change, so nothing
   else is needed.

### G2.3 Call site

In `World3D::drawSprite`, inside the billboard branch
(`new_src/render/World3D.cpp:800-802`, `if (!isWall) { ... }`), immediately
**before** `drawBillboardPart(...)` for the body:

```cpp
if (tileNum == kTileNumObjTorchiere) {
    info ^= (frame & 0x1) << 17;              // src/Render.cpp:1645 (see note)
    drawTorchiereGlow(map, media, x, y, z, info, scaleFactor);
    applyBatchState(renderMode);              // back to the body's own mode
}
```

Constraints, all load-bearing:

* **Order**: glow first, body over it — the legacy branch has no `return` and
  falls through to `src/Render.cpp:1728`. Do not swap.
* The XOR must be applied to the `info` used for **both** quads (legacy mutates
  `n2` in place, §8.5). With the load-time frame count of 1 that our
  `loadEntities` writes for tile 136 (`new_src/domain/game/Game.cpp:60`, matching
  `mappings[136..137]` = 1 frame), `frame` is always 0 and the XOR is a no-op —
  in the original too. Port it verbatim anyway; it is one line and documents the
  intent.
* `renderMode` is the local already read at `new_src/render/World3D.cpp:684`
  (0 for tile 136 — 136 is absent from the §8.4 table). The glow ignores it by
  design.
* Both quads belong to one sprite entry and hence one sort key: no extra entry in
  the leaf sort lists, no change to `drawBSP` (§8.5, §8.7).
* Torchieres on map00 carry no `ORIENTED`/`FLAT` bits (`info` = `0x88` /
  `0x200088`, the latter being `SPRITE_FLAG_NOENTITY` which `loadEntities`
  clears), so they always reach the billboard branch. Do **not** add a glow to
  the wall branch.

### G2 acceptance ("what is on screen")

map00, the eight torchieres at tiles **(4,18)**, **(4,20)**, **(2,6)**, **(2,7)**,
**(3,6)**, **(3,7)**, **(19,24)**, **(19,26)**:

* A **soft additive halo** appears around each lamp, centred ~10 map units above
  the lamp origin, the same footprint as the lamp quad, brightening the wall/floor
  behind it. Ask the user explicitly whether it is a *soft glow* or an *opaque
  white/grey rectangle*: an opaque blob means the alpha channel is not reaching
  the blend (transparent key not killed, or the mode row wrong); an invisible
  glow means the texture upload of media 798 failed (check for the one-shot log)
  or the glow is drawn *after* the body.
* Its brightness must be visibly **half** of what a full `RENDER_ADD` would be —
  compare against the tile-130 fires nearby (sprites 0/25/31/32, e.g. tile
  (1,16), (12,6), (13,4), (13,5)), which are mode 3.
* Nothing else changes: the lamp body still draws opaque, in front of the glow;
  the sprites drawn after the torchiere in the same leaf keep their own look
  (proof that `applyBatchState(renderMode)` restores state).
* Approach a torchiere and back off: the halo must not turn into a fogged grey
  square (mode 5 has fog off, table row).

---

## G3 — Revision of the modes we already assign

Goal: make sure nothing on a map is silently drawn with the wrong mode now that
the table exists, and close the two known renderer-side gaps.

### G3.1 Wall/floor geometry modes (real gap)

`drawPoly` hardcodes `applyBatchState(0)` (`new_src/render/World3D.cpp:449-452`).
The original picks a mode per texture tile in `Render::drawNodeGeometry`
(`src/Render.cpp:950-963`, §8.4):

| texture tile | mode | note |
|---|---|---|
| 161 `HELL_HANDS` | 2 `BLEND50` | |
| 302 `FADE`, 212 `SCORCH_MARK` | 0 `NORMAL` on GL (`RENDER_SUB` only on TinyGL) | keep 0 — we are the GL path |
| 479 / 480 `FLAT_LAVA*` | 0 `NORMAL` + `CULL_NONE` | culling is already off globally (`begin()` `:374`) |
| everything else | 0 | |

Port: in `drawPoly`, replace the unconditional `applyBatchState(0)` with
`applyBatchState(p.textureId == 161 ? 2 : 0);` plus a comment citing
`src/Render.cpp:950-963`. Nothing else in `drawPoly` changes.

### G3.2 The GL back-end's tile-212 override

`gles::SetupTexture` forces `renderMode = RENDER_SUB` for tile 212 *inside the
back-end*, before the switch (`src/GLES.cpp:609-611`), i.e. for **every** draw of
that tile, not only for map sprites that `postProcessSprites` already tagged.
Our port only has the load-time tagging (`new_src/domain/game/Game.cpp:75`), so a
future runtime-spawned scorch decal would draw as `NORMAL`.

Port: in `World3D::drawSprite`, at the line that reads the sprite mode
(`new_src/render/World3D.cpp:684-685`), apply the override before
`applyBatchState`:

```cpp
int renderMode = map.mapSprites[i + 3 * n];
if (tileNum == 212 /* TILENUM_SCORCH_MARK, src/GLES.cpp:609-611 */) renderMode = 7;
applyBatchState(renderMode);
```

(`renderMode` therefore stops being `const`; the G2 call site reuses the same
local.)

### G3.3 One-shot census log (verification aid, remove at sign-off)

At the end of `World3D::uploadMapTextures` (`new_src/render/World3D.cpp:262-334`),
after the sprite-texture preload loop, emit one line listing every distinct
`(effective tileNum → S_RENDERMODE)` pair present in the map, sorted by tile, e.g.

```
World3D: sprite modes: 130=3 136=0 178=3 212=7 ...
```

Mark it `// TEMP [dbg]` with this spec's path, like the existing tripwires
(`:640`, `:704`). It is the only way to see, without art, that no sprite is
being drawn with an unexpected mode; the orchestrator relays it to the user.
Expected on map00 exactly: `130=3 136=0 178=3 212=7` plus a tail of `=0` tiles,
and **no** other non-zero value (tiles 161/208/234/236/244 are absent from
map00 — verified against `tmp_map00.bin`).

### G3 acceptance

* Console shows the census line; the non-zero entries are exactly
  `130=3`, `178=3`, `212=7` (and `136=0`). Any other non-zero entry is a finding
  to report, not a failure of this group.
* Glass panels (tile 178) at tiles (24,17), (24,18), (28,22), (2,7), (2,19),
  (21,29), (22,29): still bright, translucent, additive — the wall behind shows
  through, and the panel is **not** a solid sheet. Unchanged from before G1.
* Scorch marks (tile 212) at tile (2,22): still a dark stain that *darkens* the
  floor under it, never a black opaque quad and never invisible.
* The fires (130) and fireballs (242) still read as additive flames with no
  visible rectangular border.
* No map00 geometry uses texture 161, so G3.1 must be a **no-op on screen** for
  map00: floors/walls unchanged. It is a correctness fix for hell maps.

---

## Explicitly NOT in this spec

* **`RENDER_FLAG_*` color shifts** (`PULSATE` 512, `RED/GREEN/BLUE_SHIFT`,
  `MULTYPLYSHIFT`, `BRIGHTREDSHIFT`; `src/GLES.cpp:717-757`,
  `gles::TexCombineShift` `:1258-1279`) — a separate, orthogonal channel and a
  separate work item. They matter for monster status effects, which force
  `renderMode = 0` precisely so the shift is visible
  (`src/Render.cpp:1587-1598`). When they land they reuse `uColorMod` (plus an
  additive `uColorAdd` for the `GL_COMBINE`/`GL_ADD` J2ME look), and
  `applyBatchState`'s cache key must become the `(renderMode, flags)` pair as in
  the original (`src/GLES.cpp:615`) — add a `currentFlags_` sibling then.
  `RENDER_FLAG_BRIGHTREDSHIFT`'s effect on mode 0 (`mod = 1.0,0.5,0.5,1.0`,
  `src/GLES.cpp:625-635`) belongs to that work item, which is why table row 0
  here is plain white.
* **Tile 240 `TILENUM_WATER_STREAM`** — the next consumer of this
  infrastructure, deliberately postponed (user's request). It is not a billboard
  at all: `Render::renderStreamSprite` (`src/Render.cpp:1412-1495`) stretches one
  `CULL_NONE` quad between two world points with 3x V tiling and a
  `time*3 & 0x3FF` V scroll, mode 0 when map-placed and 3 `ADD` when spawned as
  projectile type 2, sort key forced to `0x80000000` (§8.6). Our `drawSprite`
  currently early-returns on it (`new_src/render/World3D.cpp:722`). map00 has no
  tile-240 sprite. Separate spec.
* **Screen-space / 2D modes** (`Image::setRenderMode`, `src/Image.cpp:158-210`;
  the mode-5 extra muzzle flash at `src/Combat.cpp:833`; the mode-3 burning
  overlay at `src/Combat.cpp:617`). Those go through `SpriteBatch`, which has its
  own `setBlendMode` (`new_src/render/gl/SpriteBatch` :145-149) — a different
  state machine with different numbering. Out of scope.
* **Spawned effect sprite pools** (`gsprite_alloc*`, `Combat::allocMissile`) —
  not ported yet; they are the main future producers of modes 4/5/6.
* **A separate additive pass or any mode-aware sorting** — forbidden by §8.7 and
  ADR 0019.

## Delegation order

G1 → build + user check (must be a visual no-op) → G2 → user check (the glow) →
G3 → user check + census line. One group per delegation; G2 depends on G1, G3 is
independent of G2 but should follow it so the user sees one change at a time.
