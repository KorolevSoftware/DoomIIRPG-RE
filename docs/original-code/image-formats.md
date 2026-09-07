# Image & texture formats — original RE port (`src/`) and shipped data

Scope: every pixel container the original touches — world media (`newTexels*.bin` /
`newPalettes.bin` / `newMappings.bin`), the sky map inside `tables.bin`, the BMP
resource set, the procedural fade texture, and the software framebuffer.
Every claim cites `src/File.cpp:line` or a data file + offset.

Companion: blend modes / render modes are documented in
`docs/original-code/rendering.md` §8 — this file only covers *storage* formats and
the transparency encoding that feeds them.

---

## 0. Executive summary

| Category | Storage | Bits | Palette | Transparency |
|---|---|---|---|---|
| World textures (walls/floors/ceilings) | `newTexels*.bin`, **raw** 8-bit indices, row-major | 8 idx | `newPalettes.bin`, RGB565, 2..256 entries | none (opaque) |
| World sprites (monsters, items, decals) | `newTexels*.bin`, **column-RLE** of 8-bit indices | 8 idx | same | index 0 = background + palette entry `0xF81F` |
| Sky box | `tables.bin` tables 16/17 and 18/19 | 8 idx, 256x256 | 256-entry RGB565 in the same file | none |
| UI / HUD / fonts / comic / cutscenes | **real Windows BMP** files in `Packages/` | 4 or 8 idx | BMP palette, BGRX8888 → RGB565 | palette color `0xFF00FF` → RGB565 `0xF81F` |
| Fade overlay (tile 302) | procedural, never on disk | RGBA8888 | — | **real 8-bit alpha ramp** (the only one) |
| Software framebuffer | in-memory | RGB565 | — | — |

There is **no** ARGB8888/4444/1555 *asset* anywhere. `ARGB8888` appears only as
(a) CPU-side color constants (`Graphics::charColors`, fog color, `setColor`) and
(b) the 16x64 procedural fade texture. Every shipped image is **palette-indexed**.

---

## 1. Pixel formats actually present in code

### 1.1 RGB565 — the universal color space

* World palettes are read verbatim as little-endian `uint16` RGB565:
  `LoadingManager.cpp:189` — `render->mediaPalettes[n5][0][k] = app->resource->shiftUShort();`
  with the J2ME note `// j2me only -> upSamplePixel(...)`, i.e. the iPhone/PC data is
  *already* 565 on disk while the J2ME build up-converted from 888.
* `shiftUShort` is little-endian: `Resource.cpp:142-146`.
* RGB888 → RGB565 helper: `Render.h:332`
  `inline static int upSamplePixel(int pixel) { return (pixel >> 8 & 0xf800) | (pixel >> 5 & 0x07e0) | (pixel >> 3 & 0x001f); }`
* The software rasterizer framebuffer is `uint16_t* pixels` (`TinyGL.h:58`) and is
  presented as an RGB565 texture: `Render.cpp:3822`
  `glTexImage2D(..., GL_RGB, buffW, buffH, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, buffer);`
* All software blends operate in 565 (`Span.cpp:15-42`: `blend25_565`, `blend50_565`,
  `add565`, `sub565` with masks `0xF7DE`, `0xC718`).

### 1.2 RGBA5551 — the GPU upload format for anything with a color key

* World media: `GLES.cpp:1056` builds 5551 from the expanded RGBA8 palette
  `v58 = (((v55[0] >> 3) << 11) | ((v55[1] >> 3) << 6) | ((v55[2] >> 3) << 1) | (v55[3] >> 7));`
  then `GLES.cpp:1117` `glTexImage2D(..., GL_RGBA, ..., GL_UNSIGNED_SHORT_5_5_5_1, texData)`.
* BMP images: `Image.cpp:38-50`. RGB565 → RGBA5551 in place:
  ```
  if (pix == 0xf81f) { pix = 0x0000; }
  else { pix = pix & 0xffc0 | (uint16_t)((pix & 0x1f) << 1) | 1; }
  ```
  i.e. keep R5 + the top 5 of the 6 green bits, shift B5 up one, force A=1.
  **The green LSB is dropped** on the transparent-mask path.
* Opaque BMP path stays RGB565: `Image.cpp:34`.

### 1.3 ARGB8888 — only CPU-side constants and one procedural texture

* `Graphics::charColors[12]` (`Graphics.h:25-28`) — 0xAARRGGBB text colors, fed to
  `glColor4ub` in render mode 8 (`Image.cpp:186-188`).
* Fog color is ARGB8888 (`Render::buildFogTables(int fogColor)`, `Render.cpp:1852`);
  the alpha byte `(fogColor & 0xFF000000) >> 24` is the fog *strength*, and the RGB
  part is folded down to 565 via `upSamplePixel` at `Render.cpp:1866`.
* The fade texture (`gles::CreateFadeTexture`, `GLES.cpp:234-293`) is the single
  true RGBA8888 surface: 16x64, all-black, alpha stepping down by 10 (`GLES.cpp:279-290`).

### 1.4 4-bit indexed — BMP only

`Applet::createImage` accepts exactly two depths (`App.cpp:194-197`):
```
if (desc.bitsPerPixel != 4 && desc.bitsPerPixel != 8) {
    Error("Expected image bpp 4 or 8. Found bpp %d", desc.bitsPerPixel);
```
4bpp rows are unpacked to 8bpp (`App.cpp:249-263`), high nibble = left pixel.

### 1.5 Formats NOT present

Grepping every texture upload in `src/` (`glTexImage2D`, `glTexSubImage2D`,
`SDL_PIXELFORMAT`) yields only: `GL_UNSIGNED_SHORT_5_6_5` (2 sites),
`GL_UNSIGNED_SHORT_5_5_5_1` (2 sites), `GL_UNSIGNED_BYTE` RGBA (1 site, the fade
texture). No 4444, no 1555 (as opposed to 5551), no compressed formats.
`GL_PALETTE8_RGB5_A1_OES` — the native GLES1 paletted upload the original iPhone
build used — is present but **commented out** at `GLES.cpp:1106`, replaced by a
CPU-side palette expansion loop at `GLES.cpp:1108-1116`. That comment is the proof
that the shipping iPhone binary uploaded *paletted* textures directly to the GPU.

---

## 2. World media: `newMappings.bin` + `newPalettes.bin` + `newTexels000..038.bin`

### 2.1 `newMappings.bin` — the directory (18432 bytes, exact)

Read in one pass at `LoadingManager.cpp:349-355`:

| Offset | Size | Field | Reader |
|---|---|---|---|
| 0 | 512 * 2 | `mediaMappings[512]` — `int16`, first media index of tile N | `readShortArray` |
| 1024 | 1024 * 1 | `mediaDimensions[1024]` — `uint8` | `readByteArray` |
| 2048 | 1024 * 4 * 2 | `mediaBounds[4096]` — `int16`, 4 per media | `readShortArray` |
| 10240 | 1024 * 4 | `mediaPalColors[1024]` — `int32` | `readIntArray` |
| 14336 | 1024 * 4 | `mediaTexelSizes[1024]` — `int32` | `readIntArray` |

Total 18432 = exact file size (verified against `tmp_newMappings.bin`). All fields
little-endian (`Resource.cpp:137,148`).

Constants: `MEDIA_MAX_MAPPINGS = 512`, `MEDIA_MAX_IMAGES = 1024` (`Render.h:99-100`),
`gles::MAX_MEDIA = 1024` (`GLES.h:43`).

**`mediaMappings`** is a frame-range table: media indices for tile `N` are
`[mediaMappings[N], mediaMappings[N+1])`, so frame count = the difference
(`LoadingManager.cpp:116-118`, `Game.cpp:379`, `Combat.cpp:1676`). A concrete media
index is always `mediaMappings[tileNum] + frame` (`Render.cpp:2047`).

**`mediaDimensions[i]`** packs two log2 sizes in one byte
(`Render.cpp:2100-2102`, mirrored at `GLES.cpp:851-856`):
```
widthBits  = (dims >> 4) & 0xF;   heightBits = dims & 0xF;
sWidth  = 1 << widthBits;   tHeight = 1 << heightBits;
```
Observed values in `tmp_newMappings.bin`: 0x88 (256x256), 0x77 (128x128), 0x67, 0x76,
0x73, 0x74, 0x66, 0x87, 0x78, 0x55, 0x46, 0x88, plus 0 for unused slots.

**`mediaBounds[4*i + 0..3]`** = `x0, x1, y0, y1` — the tight bounding box of the
sprite inside its texture. `Render.cpp:2105` masks with `0xFFF`; `GLES.cpp:961-964`
casts to `uint8_t`. Verified ranges in the data: for RLE sprites
`x0<=115, x1<=176, y0<=153, y1<=177` (fits `uint8`); for raw wall textures the
values are `[0,256,0,256]` and are never used by the RLE branch, so the `uint8_t`
truncation of 256→0 is harmless. `Render::getImageFrameBounds` (`Render.cpp:2025-2037`)
reinterprets them as a 176-unit sprite space:
`(bound & 0xFF) * 64 / 176 - 32`.

**`mediaPalColors[i]`** (raw on-disk meaning):
* bit 31 `MEDIA_FLAG_REFERENCE = 0x80000000` (`Render.h:96`) set →
  low bits are the index of an *earlier* media that owns the palette
  (`LoadingManager.cpp:120-121`, mask `0x3FF`).
* otherwise `value & 0x3FFFFFFF` = **number of RGB565 entries** in this palette.

**`mediaTexelSizes[i]`** (raw on-disk meaning):
* bit 31 = reference, same convention (`LoadingManager.cpp:126-127`).
* otherwise `(value & 0x3FFFFFFF) + 1` = **byte length of the texel blob**
  (`LoadingManager.cpp:149`, `LoadingManager.cpp:225`). Note the **+1**: the file
  stores `size - 1`.

Data census of `tmp_newMappings.bin`: 671 owned palettes / 353 palette references;
530 owned texel blobs / 494 texel references. Every reference target is a lower
index and is itself an owner (verified exhaustively).

### 2.2 Load-time rewrite of the two directories

After loading, both arrays are rewritten to *runtime slot* form
(`LoadingManager.cpp:192-197` for palettes, `:264-270` for texels):

* owner → `0x40000000 | slot`
* reference → `0xC0000000 | slot` (the owner's slot)

and readers thereafter mask with `0x3FFF`:
`Render.cpp:2085-2086` `texIdx = mediaTexelSizes[mediaIdx] & 0x3FFF;`
`palIdx = mediaPalColors[mediaIdx] & 0x3FFF;`, also `Render.cpp:2040`, `GLES.cpp:845-849`.

Only media registered for the current map are allocated: `registerMapMedia`
(`LoadingManager.cpp:112-131`) ORs in `MEDIA_TEXELS_REGISTERED / MEDIA_PALETTE_REGISTERED`
(both `0x40000000`, `Render.h:97-98`), and `finalizeMapMedia` allocates and streams in
only those (`LoadingManager.cpp:143-166`).

### 2.3 `newPalettes.bin` — 671 records, sequential

Record = `n` little-endian `uint16` RGB565 entries, followed by a **4-byte marker**
(`LoadingManager.cpp:185-193`; skip path `:198-204`, both advancing by
`2*n + 4`). `Resource::readMarker` only *skips* 4 bytes, it never validates
(`Resource.cpp:84-90`; the nominal value is `0xCAFEBABE`).

Verification: `sum(2*n + 4)` over the 671 owned palettes of `tmp_newMappings.bin`
= 335932 = exact size of `tmp_newPalettes.bin`.

Palette sizes observed: 2, 4, 5, 10, 11, 16, 17, 18, 37, ..., 256. **Palettes are
not 256 entries** in general — a reimplementation that assumes 256 will read out of
bounds (and the original does exactly that, see §6.4).

`LoadingManager.cpp:168-174` contains a `[GEC]` data patch: if `newPalettes.bin`
matches a known MD5, bytes **266852..266855** are zeroed (the water-stream palette).

### 2.4 `newTexels000..038.bin` — 530 blobs, 0x40000-chunked

Blobs are emitted in ascending media index; references consume nothing. Each blob is
followed by a 4-byte marker, so the cursor advances by `size + 4`
(`LoadingManager.cpp:284`, skip path `:286-288`). When the cursor exceeds
`0x40000` the loader moves to the next file and resets the cursor to 0
(`LoadingManager.cpp:290-293`):
```
if (n12 > 0x40000) { ++m; n12 = 0; }
```
That is why most `newTexelsNNN.bin` are exactly 262160 bytes (0x40000 + 16).

Reconstructing the whole set with this rule from `tmp_newMappings.bin` +
`tmp_newTexels*.bin` lands all 530 blobs inside file bounds and ends on file 038.

Per-blob `[GEC]` MD5 checks for media 814..817 (the animated water) at
`LoadingManager.cpp:222-241`.

### 2.5 Raw vs column-RLE: the size test

The **only** discriminator is the blob length:

```
Size == sWidth * tHeight   →  raw 8-bit indexed bitmap, row-major, one byte per texel
Size != sWidth * tHeight   →  column-RLE sprite
```

Code: `GLES.cpp:900` `if ((v5 == mediaID) || (Size == __len))` (raw branch) and the
same test in the software path at `Render.cpp:461,492,538`
(`app->tinyGL->textureBaseSize == app->tinyGL->sWidth * app->tinyGL->tHeight`).

Census of `tmp_newMappings.bin` + dims:
* 156 raw blobs — dims 256x256 (112), 128x128 (22), 64x128 (8), 128x64 (3), 64x64 (3),
  128x256 (2), 256x128, 128x8, 128x16, 32x32, 32x128, 16x64 (1 each).
  Media index range 703..968.
* 374 RLE blobs — **all 256x256**, media index range 0..850.
* Boundary: `mediaMappings[257] = 851` and `TILENUM_FIRST_WALL = 257` (`Enums.h:831`).
  Every RLE blob has index < 851, i.e. **RLE is exclusively the sprite range**;
  walls/floors are always raw. (44 raw blobs also live below 851 — large raw sprites.)

The "`texel size 31443 != 65536`" observation resolves to media **798**, a 256x256
RLE sprite.

### 2.6 Column-RLE format — complete

Layout of a blob `T` of `Size` bytes (three regions, the first two grow forward,
the header is a 2-byte *trailer*):

```
[ 0                      .. nibbleBase-1 ]  pixel bytes (8-bit palette indices), one
                                            contiguous stream in column-major order
[ nibbleBase             .. runsBase-1   ]  run-count nibbles, one per column
[ runsBase               .. Size-3       ]  run descriptors, 2 bytes each: (y, height)
[ Size-2, Size-1         ]                  uint16 LE: byte length of the two regions above
```

Offsets (`GLES.cpp:947-966`, identical arithmetic in `TinyGL.cpp:758-761`):
```
nibbleBase = Size - ((T[Size-1] << 8) | T[Size-2]) - 2
x0 = mediaBounds[4*mediaID + 0];  x1 = mediaBounds[4*mediaID + 1]
y0 = mediaBounds[4*mediaID + 2];  y1 = mediaBounds[4*mediaID + 3]
runsBase   = nibbleBase + ((x1 - x0 + 1) >> 1)
```
Note the nibble array is sized `(x1 - x0 + 1) >> 1` bytes but only columns
`x0 .. x1-1` are iterated (`while (shapeMin < shapeMax)`, `GLES.cpp:983`).

Decoder (`GLES.cpp:983-1029`), rewritten faithfully:
```
dst = new uint8[width * height];   memset(dst, 0, width * height);   // GLES.cpp:938
px  = 0;                                   // cursor into the pixel stream (v83)
rp  = runsBase;                            // cursor into the run stream (v31)
for (int col = 0; col < x1 - x0; ++col) {
    int nRuns = (T[nibbleBase + (col >> 1)] >> ((col & 1) << 2)) & 0xF;  // low nibble first
    for (int r = 0; r < nRuns; ++r) {
        int y   = T[rp++];
        int len = T[rp++];                 // asserted: y + len <= y1
        for (int k = 0; k < len; ++k)
            dst[(y + k) * width + (x0 + col)] = T[px++];
    }
}
```
Invariants (verified on the shipped data): at the end `px == nibbleBase` and
`rp == Size - 2`. Both hold for **370 of the 374** RLE blobs; the four exceptions
are exactly media **814, 815, 816, 817** — the animated-water sprites the port
repairs by overriding individual run-count nibbles in
`Render::fixTexels` (`Render.cpp:3853-3870`), e.g. media 814 offset 5614 low nibble
forced to 16 (my computed `nibbleBase` for 814 is 5603, so 5614 is column 22 —
independent confirmation of the offset formula).

The uncovered area is left as **index 0** (`memset` above), which is why index 0 is
the transparent index for RLE sprites (§5.2).

The software rasterizer never materialises the sprite: `TinyGL::drawClippedSpriteLine`
(`TinyGL.cpp:745-834`) walks the same nibble/run streams *per screen column* and
stretches each run directly into the framebuffer, seeking forward/backward through
the run stream as the texture column advances (`TinyGL.cpp:783-806`).

### 2.7 Runtime palette variants: 16 shade/fog levels

Each palette slot is a `uint16_t*[16]` (`LoadingManager.cpp:341-346`), level 0 loaded
from file, levels 1..15 allocated at `LoadingManager.cpp:587-595` and *computed*:

`Render::buildFogTables(int fogColorARGB)` (`Render.cpp:1852-1884`), per level `i` in 1..15:
```
n2   = (i << 8) / 16 * fogAlpha >> 8              // fogAlpha = (fogColor >> 24) & 0xFF
fogTableColor = upSamplePixel( per-channel scale of fogColor.rgb by n2 )
fogTableFrac  = 256 - n2
```
then `Render::buildFogTable()` (`Render.cpp:1823-1832`) for every entry:
```
dest[i] = ( ((frac >> 2) * ((src & 0x07e0) >> 6)) & 0x07e0 )
        | ( ((frac >> 3) * (src & 0xF81F)) >> 5 ) & 0xF81F
        + fogTableColor
```
Here `0xF81F` is used as the **R|B channel mask of RGB565** (R=0xF800, B=0x001F) so
red and blue are scaled in one multiply — the same constant that doubles as the
transparency key (§5). Green is handled separately with `0x07E0`.

If `(fogColor & 0xFF000000) == 0` fog is disabled by pushing the range out:
`fogMin = 32752, fogRange = 1` (`Render.cpp:1857-1861`).

Level selection is **per span**, from the interpolated 1/z:
`TinyGL::getFogPalette` (`TinyGL.cpp:47-52`)
```
i = (((0x7FFFFF / (i >> 16)) - fogMin) << 4) / fogRange;   // clamp to [0,15]
return this->paletteBase[i];
```
called at `TinyGL.cpp:654,662` with `tglEdge->fracZ`, and with the constant
`0x10000000` for the sky (i.e. effectively unfogged).

The GL path does **not** use these 16 palettes at all — it uploads level 0 and
delegates to fixed-function `GL_LINEAR` fog (`GLES.cpp:100-103`, `:375-379`).

Caveat: `Render::buildFogTable(int, int, int)` (`Render.cpp:1834-1850`) gates on
`(mediaPalColors[n5] & 0x4000)`, but after the load-time rewrite the owner flag is
`0x40000000`; bit 14 can never be set for a slot index < 1024, so both calls at
`Render.cpp:1882-1883` are dead in this port. Flagged, not relied upon.

### 2.8 Sky box

`skyIndex = ((mapNameID - 1) / 5 % 2) * 2` (`LoadingManager.cpp:571`), then
`tables.bin` table `skyIndex+16` = palette (`loadUShortTable`) and table
`skyIndex+17` = texels (`loadUByteTable`) — `LoadingManager.cpp:572-583`.

Measured from `tmp_tables_real.bin` (20 `int32` LE offsets at offset 0, sizes via
`Resource::getNumTableBytes/Shorts`, `Resource.cpp:254-268`):
table 16 = 512 bytes = **256 RGB565 entries**, table 17 = **65536 bytes** = 256x256
8-bit indices; tables 18/19 identical for the second sky. Consistent with the
hard-coded `textureBaseSize = 256 * 256; paletteBaseSize = 256;`
(`Render.cpp:2075-2076`) and `widthBits = heightBits = 8` (`Render.cpp:2078-2079`).
The sky also gets 16 palette levels (`Render.cpp:1877-1881`).

---

## 3. BMP resources — yes, they are genuine Windows BMPs

### 3.1 The reader

`Applet::createImage(InputStream*, bool isTransparentMask)` — `App.cpp:162-274`.
The struct is a packed 54-byte `BITMAPFILEHEADER + BITMAPINFOHEADER`
(`App.cpp:164-183`, `static_assert(sizeof(ImageDesc) == 54)` at `App.cpp:184`).

Parsing sequence:
1. 54-byte header; `bitsPerPixel` must be 4 or 8 (`App.cpp:194-197`).
2. `if (colorsUsed == 0) colorsUsed = 1 << bitsPerPixel;` (`App.cpp:199-201`).
3. Palette: `colorsUsed` * `uint32` read **immediately after the header**
   (`App.cpp:208`), byte-swapped to host LE (`App.cpp:209-211`), then converted
   `SDL_PIXELFORMAT_RGB888 → SDL_PIXELFORMAT_RGB565` (`App.cpp:226-230`).
   `offBeg` is **ignored** — the reader assumes `offBeg == 54 + colorsUsed*4`.
   Verified: this holds for **all 275** BMPs in the shipped `Packages/`.
4. Pixel rows: `srcStride = ((bitsPerPixel*width + 31) / 32) * 4` (`App.cpp:236`),
   `height * srcStride` bytes read at once (`App.cpp:241`).
5. 4bpp → 8bpp expansion, high nibble first (`App.cpp:249-263`).
6. **Vertical flip** — BMP is bottom-up, so rows are reversed (`App.cpp:264-270`):
   `memcpy(colorsIndexes + width*row, pixelData + pixelStride*(height-1-row), width)`.

Result: `Image::colorsIndexes` (8-bit indices, top-down) + `Image::RGB565Palette`
(`Image.h:26-27`). No RGB expansion happens at load time.

### 3.2 Measured properties of the 275 shipped BMPs

Parsed directly from `Payload/Doom2rpg.app/Packages/**.bmp` inside
`build_new/new_src/Doom 2 RPG.ipa`:

* signature `BM`, `headerSize = 40` (BITMAPINFOHEADER), `colorPlanes = 1`,
  `compression = 0` (BI_RGB) — **uniformly, no exceptions**.
* `bitsPerPixel`: 8bpp for 249 files, 4bpp for 26 files. The whole ComicBook set
  (both the 320x~425 pages and the 480x320 iPhone frames) is 4bpp.
* `height > 0` for all → bottom-up row order for all.
* `colorsUsed` varies wildly (0, 11, 15, 16, ..., 256); `0` means "all"
  (78 files rely on the `1 << bpp` default).
* `fileSize` in the header disagrees with the actual size for 23 files — harmless,
  the reader never uses it.
* 160 of 275 palettes contain the exact color `0x00FF00FF` (magenta) — the color key.

Odd-width 4bpp files exist (`images04` w=79, `images10` w=37, `toEarth` w=37,
`dpad*` w=117, `travelMapVertGrid` w=1). The port's unpack loop runs
`col < width / 2` (`App.cpp:255`), so **the last pixel of every odd-width 4bpp row is
left as 0** — a real (cosmetic) defect of the RE port to avoid reproducing.

### 3.3 What the BMPs are used for

Every `Applet::loadImage` call site in `src/` passes `isTransparentMask = true`
(88 distinct file names; zero calls with `false`). Categories:

* **Fonts** — `Font.bmp` (192x144, 8bpp), `Font_16p_Dark/Light.bmp`,
  `Font_18p_Light.bmp` (208x162), `WarFont.bmp` (`Canvas.cpp:169-178`).
  Fixed 12x16 grid, 16 glyphs per row: `Graphics::drawChar` (`Graphics.cpp:637-663`)
  `drawRegion(img, (index1 & 15) * 12, index1 & 240, 12, 16, ...)`, with an optional
  second overlaid glyph `index2` for diacritics.
* **HUD / menus / minigames / travel map** — `App.cpp:416-456`,
  `LoadingManager::loadMiniGameImages` (`LoadingManager.cpp:81-110`),
  `MenuSystem.cpp:144-145`.
* **Portraits & paper-doll parts** — `Hud_Player*.bmp`, `Hud_Portrait_Small.bmp`,
  `Major_torso/legs.bmp`, `Riley_*`, `Sarge_*` (`App.cpp:431-453`).
* **Cutscene / full-screen art** — `prolog.bmp`, `logo.bmp`, `logo2.bmp`,
  `spaceShip.bmp`, `charSelectionBG.bmp`, `endOfLevelStatsBG.bmp`.
* **Comic book** — `ComicBook/*.bmp` and `ComicBook/frames/*.bmp`
  (`ComicBook.cpp:261-...`), all 4bpp.
* **Portal render target source** — `portal_image2.bmp` (`Render.cpp:167`), drawn via
  `gles::DrawPortalTexture` (`GLES.cpp:1163-1210`).

Unreferenced leftovers in the archive (no grep hit anywhere in `src/`):
all 50 `.png` files (the J2ME asset set, e.g. `Font.png`, `ui_images.png`),
`images.idx` + `images00..12.bmp` (the old J2ME image bank),
`page_icons.zip` (a nested zip holding three `*_Icon.bmp` that are *also* present
loose and loaded from the loose copies), `skymap_01.bmp`, `tables.bin.gz`,
`entities 2.bin`. `Default.png` / `Icon.png` are iOS launch assets, not game data.

### 3.4 CPU expansion at first draw

BMP images are lazily expanded on first use, not at load:
`Graphics::drawRegion` (`Graphics.cpp:308-347`) rounds `width/height` up to
powers of two into `texWidth/texHeight`, allocates an RGB565 buffer, and does
```
uint16_t rgb = img->RGB565Palette[img->colorsIndexes[img->width * w + h]];
if (rgb == 0xf81f) { img->isTransparentMask = true; }
data[(w * img->texWidth) + h] = rgb;
```
then `Image::CreateTexture` (`Image.cpp:23-52`). Note `isTransparentMask` is
**recomputed here** (reset to false, then set by the key scan), overriding the
`loadImage` argument. Identical code duplicated in `gles::DrawPortalTexture`
(`GLES.cpp:1181-1197`) and `ComicBook.cpp:449-479`.

`[GEC]`-only asset repair: `fixImage` (`Utils.cpp:111+`) MD5-matches
`blockGameColors.bmp` / `vending_arrow_down.bmp` and splices a hard-coded 54x18
block of 8-bit indices into the image. Port-specific, not original behavior.

---

## 4. The `0xF81F` key — confirmed, everywhere

`0xF81F` = RGB565 pure magenta (R=31, G=0, B=31) = RGB888 `0xFF00FF`.

Confirmed sites:
* `Render.cpp:2093-2098` (world media, software path) — the transparent *index* is
  found by scanning the loaded palette:
  ```
  tinyGL->paletteTransparentMask = -1;
  for (int i = 0; i < tinyGL->paletteBaseSize; i++)
      if (tinyGL->paletteBase[0][i] == 0xF81F) tinyGL->paletteTransparentMask = i;
  ```
  (field marked `// [GEC] new` at `TinyGL.h:108`, i.e. generalised by the porter;
  consumed by `spanTransparent*` at `Span.cpp:59,77,95`:
  `if (b != pMask) { *pixels = spanPalette[b]; }`).
* `Image.cpp:41` (BMP → RGBA5551): `if (pix == 0xf81f) { pix = 0x0000; }` (alpha 0).
* `Graphics.cpp:336` and `GLES.cpp:1189` and `ComicBook.cpp:467` — key detection
  driving `isTransparentMask`.
* `GLES.cpp:880-889` (world media, GL path) — detection on the expanded RGB8 palette,
  with two tolerances:
  ```
  if ((v16[0] >= 250) && (v16[1] == 0)   && (v16[2] >= 250)) transAlpha = true;
  if ((v16[0] >= 250) && (v16[1] == 4)   && (v16[2] >= 250)) transAlpha = true;  // Arachnotron attack
  ```
  and the final zeroing at `GLES.cpp:1048-1054`.

---

## 5. Transparency per category

### 5.1 World textures (walls/floors/ceilings, media >= 851)
Fully opaque. No key, no alpha. Drawn with `spanTexture*` variants
(`Render.cpp:2069`, `useTransSpan == false`).

### 5.2 World sprites (media < 851)
Two independent mechanisms, both reduced to 1-bit alpha:
1. **Index 0 = background.** The RLE decode target is zero-filled
   (`GLES.cpp:938`), and `GLES.cpp:1035-1043` forces palette entry 0 to
   `(0,0,0,0)` when `transAlpha`. Before that, entry 0 is optionally replaced by the
   *average* of entries 1..255 (`GLES.cpp:939-944` / sums at `GLES.cpp:871-876`)
   so bilinear filtering bleeds a neutral color instead of black.
2. **Palette color `0xF81F` → alpha 0** (§4), applied to any entry, not just 0
   (`GLES.cpp:1048-1054`).
The software path uses only mechanism 2, via `paletteTransparentMask`.
Raw (non-RLE) images below `mediaMappings[257]` also get index-0 transparency —
`GLES.cpp:900-933` sets `v47 = 1` for `mediaID < mediaMappings[257]`, with explicit
exceptions for `mediaMappings[{197,202,128,160,168,162,129}]` and tiles 184..193.

### 5.3 BMP UI / fonts / comics
1-bit alpha only, derived from the `0xF81F` key at texture-creation time
(`Image.cpp:38-51`). Note `App.cpp:219-224`: on the transparent path every RGB888
palette entry is clamped with `std::max(rgb, 8)` before the final 565 conversion, so
a *non-key* dark color cannot accidentally collapse to 0.

### 5.4 Real alpha
Only two sources, neither of them an asset:
* the procedural fade texture, RGBA8888 with an alpha ramp (`GLES.cpp:277-290`);
* constant alpha from `glColor4f/glColor4ub` in the blend modes
  (`Image.cpp:167,175,187,197,201` — 0.25 / 0.5 / 0.75 / `charColors` alpha /
  `canvas->blendSpecialAlpha`) and the corresponding software equivalents in
  `Span.cpp:15-42`. See `docs/original-code/rendering.md` §8 for the mode table.

Consequence: alpha testing is always `glAlphaFunc(GL_GREATER, 0)`
(`Image.cpp:172,182,208`), i.e. a pure 1-bit cutout.

---

## 6. Consequences for the graphics backend (the load-bearing section)

### 6.1 Requires a programmable shader (or an equivalent GPU feature)

1. **Palette expansion on the GPU (R8 index + 256x1 RGBA palette).**
   The original iPhone build uploaded `GL_PALETTE8_RGB5_A1_OES` directly
   (`GLES.cpp:1106`, commented out in this port). Desktop GL has no such format, so
   an indexed pipeline needs a fragment shader with two samplers. *Optional*: the
   port itself proves the fallback works — expand palette → RGBA5551 on the CPU
   (`GLES.cpp:1108-1116`) and hand a plain RGBA texture to any 2D API. The cost is
   1 texture per (texels, palette) pair instead of per texel blob, and re-upload on
   palette change.
2. **Per-pixel / per-fragment fog.** The software original selects one of 16
   pre-baked palettes **per span** from interpolated 1/z (`TinyGL.cpp:47-52,654,662`);
   the GL original uses fixed-function `GL_LINEAR` fog (`GLES.cpp:100-103`), which
   does not exist in GL 3.3 core. Reproducing either needs a shader (or, on a fixed
   pipeline, the 16-palette trick — which in turn requires the indexed path from (1)).
3. **The 16-level shade/fog palette ramp itself** (`Render.cpp:1823-1832`) is a
   nonlinear per-channel operation in 565 space. If exact software-path fidelity is
   wanted, it must be evaluated either on the CPU into 16 palette textures or in a
   shader. Approximating it with GL fog is what the original GL path already does.
4. **Palette-level render-mode effects** — `Render::setupPalette`
   (`Render.cpp:1885-2023`): `RENDER_FLAG_GREYSHIFT (0x4)`, `RENDER_FLAG_PULSATE (0x200)`,
   `RENDER_FLAG_MULTYPLYSHIFT`, plus the `renderMode` channel masks/shifts
   (`0xFFFFE79C >> 1`, `0xFFFFF7DE`, `>> 2`, ...). These are per-palette in software
   and were emulated on GL with `glTexEnv` combiners (`gles::TexCombineShift`,
   `GLES.cpp:1258-1279`). In GL 3.3 core they are shader work.
5. **Blend modes 3 (`GL_SRC_COLOR, GL_ONE`) and the combiner-based additive shift**
   need explicit blend/shader state; see `rendering.md` §8.2.

### 6.2 Implementable on any 2D API (no shader required)

1. **Everything BMP-derived** — UI, HUD, fonts, comic pages, cutscene art, portraits.
   The original already expands them on the CPU to RGB565 / RGBA5551
   (`Graphics.cpp:330-343`, `Image.cpp:23-52`). A modern backend can produce RGBA8888
   once at load and forget the palette. No shader, no color key at draw time.
2. **The `0xF81F` color key** — resolved entirely at texture-build time into 1-bit
   alpha; at draw time it is a plain `alpha > 0` test, expressible as ordinary
   alpha blending / alpha test on any API.
3. **Column-RLE decoding** — pure CPU work producing a plain 8-bit (or RGBA) bitmap
   (`GLES.cpp:983-1029`). Nothing GPU-specific.
4. **Sprite/tile atlas geometry** — POT rounding, `mediaBounds`, 12x16 font cells,
   rotate/mirror modes 0..8 (`Image.cpp:120-155`) are all quad transforms.
5. **Constant-alpha and modulate blend modes** (0.25 / 0.5 / 0.75 / text color) map
   to any 2D API's tint + blend-mode parameters.
6. **The fade overlay** — 16x64 RGBA8888, generated in code, ordinary alpha blend.
7. **The software framebuffer presentation path** — one RGB565 (or RGBA8) full-screen
   textured quad (`Render.cpp:3822`).

### 6.3 The decision line

The **only** things pushing towards a programmable pipeline are (a) keeping textures
indexed on the GPU and (b) fog/palette-shift effects that were per-span palette swaps
or fixed-function fog. Both live exclusively in the **3D world renderer**.
The entire 2D layer (UI, fonts, HUD, comics, cutscenes, menus, minigames, travel map)
is expressible on a plain textured-quad 2D API with tint + blend mode + 1-bit alpha.
A backend split along that seam ("2D quad backend" vs "world backend") matches the
original's own split between `Graphics`/`Image` and `TinyGL`/`gles`.

### 6.4 Reimplementation hazards found in the original code

* `GLES.cpp:865-893` iterates the palette **256 times unconditionally**
  (`while (v15 < 256)`), but 671 of the shipped palettes have 2..255 entries →
  out-of-bounds read. A rewrite must clamp to `mediaPalettesSizes[palIdx]`.
* `App.cpp:255` loses the last pixel of odd-width 4bpp BMP rows (§3.2).
* `App.cpp:208` ignores BMP `offBeg`; safe for the shipped data but not for arbitrary BMPs.
* `Render.cpp:1837` tests `& 0x4000` where `& 0x40000000` was intended → dead code.
* `mediaBounds` values reach 256 and are cast to `uint8_t` in `GLES.cpp:961-964`;
  only safe because the RLE branch never sees 256.
* `Resource::readMarker` (`Resource.cpp:84-86`) does **not** validate the marker;
  do not rely on `0xCAFEBABE` being present.
