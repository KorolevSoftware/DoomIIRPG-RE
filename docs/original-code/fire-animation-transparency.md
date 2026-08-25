# Fire/flame animation & transparency (original `src/`)

Topic: how the original animates fire tiles (OBJ_FIRE 130, ANIM_FIRE 234,
FIRE_BALL 242, TORCHIERE 136) and what makes their black background invisible.
Every claim cites `file:line`.

--------------------------------------------------------------------------------
## 1. Animation

### 1.1 AUTO_ANIMATE is injected at map load, not stored in the map

Raw map data carries **no** animation bits: a decode of `tmp_map00.bin`
(sprite arrays at file offset 59078, layout verified against
`src/LoadingManager.cpp:488-539`) shows **0 sprites with bit 0x80000 set**
in the raw high info word. Instead, during entity setup on every map load:

```cpp
// src/Game.cpp:379   frame count = number of media entries for the tile
int n7 = app->render->mediaMappings[n6 + 1] - app->render->mediaMappings[n6];
if (n6 == 234) { n7 = 4; }                 // src/Game.cpp:380-382 (ANIM_FIRE forced 4)
...
if (n6 == 136 || n6 == 234 || n6 == 130) { // TORCHIERE, ANIM_FIRE, OBJ_FIRE
    app->render->mapSpriteInfo[n5] &= 0xFFFF00FF;
    app->render->mapSpriteInfo[n5] |= (n7 << 8 | 0x80000);  // src/Game.cpp:394-397
}
```

So tiles **130 / 234 / 136** get `SPRITE_FLAG_AUTO_ANIMATE` (0x80000,
`src/Enums.h:1234`) plus the frame count stored in bits 8–15. AIR_VENT 236
gets count 3 (`src/Game.cpp:388-392`), EYE_PORTAL 156 gets count 2 +
0x80200 (`src/Game.cpp:383-387`).

Frame counts from `tmp_newMappings.bin` (mediaMappings ranges, consumed as
`short[512]`, `src/LoadingManager.cpp:333,350-351`):

| tile | mapping range | frames |
|---|---|---|
| 130 OBJ_FIRE   | 726..730 | 4 |
| 136 TORCHIERE  | 736..737 | 1 |
| 223 STATIC_FLAME | 805..806 | 1 |
| 234 ANIM_FIRE  | 810..814 | 4 (also forced, `src/Game.cpp:380-382`) |
| 242 FIRE_BALL  | 829..833 | 4 |
| 193 SFX_LIGHTGLOW1 | 798..799 | 1 |

### 1.2 Runtime cycle: 100 ms per frame, phase = sprite index

```cpp
// src/Render.cpp:1544-1546 (renderSpriteObject)
if ((n2 & 0x80000) != 0x0) {
    n7 = (n + app->time / 100) % n7;      // n = sprite index, n7 = frame count
}
```

`app->time` is the global ms clock → fire flickers at 10 fps; each sprite is
phase-shifted by its own index so neighboring fires don't pulse in sync.
The selected frame indexes the texture: `mediaID = mediaMappings[tile] + frame`
(`src/GLES.cpp:586`, `src/Render.cpp:2047`). The frame byte can be overwritten
later by the ENTITY_FRAME script op (`EV_ENTITY_FRAME`,
`src/ScriptThread.cpp:669-687`: writes `args << 8` over bits 8–15) — that is a
cutscene tool, not the ambient fire driver.

### 1.3 Dynamically spawned fire effects: 200 ms gsprite path

Projectiles/effects allocated through `Game::gsprite_allocAnim`
(`src/Game.cpp:1327-1343`) use a different clock: `numAnimFrames = 4`,
`duration = 200 * numAnimFrames`, and `gsprite_update` advances
`frame = elapsed/200 % numAnimFrames` while flag 0x40 is set
(`src/Game.cpp:1490-1495`; alloc flag 66 = 0x40|0x02, `src/Game.cpp:1330,1317`).
Cases: POOF 241 → renderMode 4 (`src/Game.cpp:1345-1348`), ANIM_FIRE 234 →
renderMode 3, scale 48, z = getHeight+32, random FLIP_H 0x20000
(`src/Game.cpp:1349-1358`), FIRE_BALL 242 → renderMode 3
(`src/Game.cpp:1360-1363`). Player/monster missiles also carry 0x80000 with
count = mappings range (`Combat::allocMissile`, `src/Combat.cpp:1676-1687`)
and ride the 100 ms path.

Other timed tile animations for comparison: WATER_SPOUT 240 toggles
`app->time/128 & 1` (`src/Render.cpp:1649-1652`); EYE_PORTAL cycles at
`app->time/1536` (`src/Render.cpp:1628-1642`).

### 1.4 map00 fire content (decoded from tmp_map00.bin)

* Spawn at tile (19,4).
* OBJ_FIRE (130) normal sprites at tiles (1,16), (12,6), (13,4), (13,5);
  high info word 0x00000000 (no flags — animation arrives via §1.1).
* FIRE_BALL (242) static z-sprites idx 145–148 at tiles (4,2), (4,12),
  (3,5), (3,8), Z bytes 6/118, high word 0x00200000
  (SPRITE_FLAG_AUTOMAP_VISIBLE/NOENTITY, `src/Enums.h:1236-1237`); these are
  decorative flames rendered additively (§2) but NOT auto-animated by §1.1
  (242 is absent from the `n6 == 136 || 234 || 130` test).
* TORCHIERE (136) instances incl. flipped ones (hi word 0x00200000);
  each also spawns an additive LIGHTGLOW1 halo
  (`src/Render.cpp:1643-1647`).

--------------------------------------------------------------------------------
## 2. Transparency conventions

Two independent mechanisms coexist:

### 2.1 Magenta color-key (most sprites/UI)

* **TinyGL**: `Render::setupTexture` scans the media palette and records the
  last entry equal to `0xF81F` into `paletteTransparentMask`
  (`src/Render.cpp:2093-2098`); the `spanTransparent*` spans skip any texel
  whose palette index equals that mask (`src/Span.cpp:59-67`, DT/DS variants
  `src/Span.cpp:77-86,95-103`).
* **GLES**: `CreateTextureForMediaID` converts the RGB565 palette to RGBA;
  if any entry expands to R≥250,G==0,B≥250 (magenta; plus an Arachnotron
  G==4 hack) then palette entry 0 gets A=0 (`src/GLES.cpp:883-889,939-943`),
  and in the final pass **every** magenta entry is zeroed to (0,0,0,0)
  (`src/GLES.cpp:1047-1054`), uploaded as RGBA5551
  (`src/GLES.cpp:1056-1058,1117`). Sprite texel buffers are pre-filled with
  index 0 (`memset(__b, 0, __len)`, `src/GLES.cpp:938`), so background =
  transparent index 0.
* Same convention outside the renderer: `src/Image.cpp:41`,
  `src/Graphics.cpp:336-337`, `src/ComicBook.cpp:467-468`.

### 2.2 Fire/glow family: NO magenta, black hidden by additive blend

Decoded palettes/texels (tools/extract_textures.py logic) show:

* OBJ_FIRE frames (media 726–729, 256×256 RLE): **zero magenta entries**
  (one stray non-black transparent slot at idx 126/127/132 per frame);
  dominant texel is index 0 ≈ 84% of the frame, and entry 0 is dark gray
  RGB(24,28,24) — **opaque**.
* FIRE_BALL frames (media 829–832): zero magenta entries, entry 0 =
  RGB(16,24,33), plus hundreds of pure-black pixels inside the flame art.
* ANIM_FIRE frames are byte-identical art to OBJ_FIRE (media 810–813 mirror
  726–729); STATIC_FLAME media 805 equals ANIM_FIRE's last frame (729).

There is **no separate underlay tile**: the fire art itself is a mostly-black
square ("backing") with the flame painted on top. Its background becomes
invisible solely because the tile is drawn with **RENDER_ADD**:

```cpp
// src/Render.h:21
static constexpr int RENDER_ADD = 3;
// src/Render.cpp:2475-2477 (postProcessSprites, per map load)
else if (n3 == 208 || n3 == 234 || n3 == 130 || n3 == 242) {
    mapSprites[this->S_RENDERMODE + i] = 3;    // FOG_GRAY, ANIM_FIRE, OBJ_FIRE, FIRE_BALL
}
```

* GL path: `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` with white modulate color,
  fog off (`src/GLES.cpp:648-652`). Texture alpha is 1 everywhere (RGB5551,
  no magenta) ⇒ pure `dst += src`; adding black changes nothing.
* TinyGL path: `spanAddTransparent*` adds every pixel with saturating
  `add565` and no mask check (`src/Span.cpp:23-31,171-180`);
  `setupPalette` case 3 pre-scales the palette by masking `& 0xF7DE`
  (`src/Render.cpp:1997-2001,2019-2021`).

Same trick elsewhere: SCORCH_MARK 212 → RENDER_SUB 7
(`src/Render.cpp:2484-2486`, forced again in `src/GLES.cpp:609-611`;
subtractive spans `src/Render.cpp:74-78`, GL `glBlendFunc(GL_ZERO,
GL_ONE_MINUS_SRC_COLOR)` `src/GLES.cpp:672-677`); torchiere halo uses
RENDER_ADD50 (`src/Render.cpp:1646`, GL case `src/GLES.cpp:660-665`);
HELL_HANDS 161 → BLEND50 (`src/Render.cpp:2487-2489`); BFG_BALL 244 →
RENDER_ADD75 4 (`src/Render.cpp:2490-2492`).

Full renderMode table assigned by postProcessSprites
(`src/Render.cpp:2468-2495`): 479(wall+257)→0, {208,234,130,242}→3, 178→3,
236→3, 212→7, 161→2, 244→4, default 0. Enum values:
`src/Render.h:18-30` (0 NORMAL, 1 BLEND25, 2 BLEND50, 3 ADD, 4 ADD75,
5 ADD50, 6 ADD25, 7 SUB, 9 PERF, 10 NONE, 12 BLEND75, 13 BLENDSPECIALALPHA).

Special-case: standing on your own fire tile shifts the billboard 18 units
forward and drops z by 512 (`src/Render.cpp:1553-1560`).

--------------------------------------------------------------------------------
## 3. Rewrite status (new_src, context only)

* Runtime AUTO_ANIMATE cycle exists: `new_src/render/World3D.cpp:569-571`
  (`frame = (i + timeMs_/100) % frame`), but the load-time injection of
  `0x80000 | count<<8` for tiles 130/234/136 is missing —
  `new_src/domain/game/Game.cpp:83-143 loadEntities` never replicates
  `src/Game.cpp:374-397`, and raw maps store no such bits (§1.4) ⇒ fires sit
  on frame 0 forever (bug a).
* renderMode is read and discarded: `new_src/render/World3D.cpp:577-579`
  (`(void)renderMode;`) ⇒ additive fire quads composite with normal blending
  and their opaque black backing shows as a black box (bug b). Also listed
  as discrepancy #10 in `docs/original-code/sprite-placement.md`.

## 4. Port checklist

1. In map-load entity setup, replicate `src/Game.cpp:379-397`: for tiles
   130/136/234 clear bits 8–15 and write `(mappingCount << 8) | 0x80000`
   (count 4 forced for 234; 156→2, 236→3 handled by neighbors there).
2. Keep the 100 ms clock and index phase exactly: `(spriteIndex + time/100) % count`.
3. Implement renderMode per sprite (from `postProcessSprites` table or the
   S_RENDERMODE array) and map: 3 → additive `glBlendFunc(GL_SRC_ALPHA,GL_ONE)`
   with fog disabled; 7 → subtractive `glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR)`;
   2/1/12 → alpha blends at 0.5/0.25/0.75; 4 → additive ×0.75 (`src/GLES.cpp:623-704`).
4. Do NOT invent an underlay/backing sprite for fire — one quad per fire,
   additive, black background included.
5. Keep magenta-key transparency for all other media: skip/zero any palette
   entry expanding to R≥250,G==0,B≥250 (plus G==4 Arachnotron variant) and
   fill sprite backgrounds with index 0 (`src/GLES.cpp:883-889,938,1047-1054`).
6. Dynamic fire effects (poof/fireball gsprites) use 200 ms frames and die
   after `200*numAnimFrames` ms (`src/Game.cpp:1343,1494`).
