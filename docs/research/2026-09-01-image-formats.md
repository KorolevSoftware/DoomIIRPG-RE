# 2026-09-01 — Image & texture formats in the original (`src/`) and the shipped data

## Hypotheses under test

1. There is a fixed, enumerable set of pixel formats; RGB565 and ARGB8888 are both used
   but for different things.
2. All shipped images are palette-indexed.
3. Some world media use a "column-RLE" compression; presence is signalled by
   `texel size != width * height` (the "31443 != 65536" observation).
4. The archive contains real Windows BMP files (not to be confused with the debug BMP
   writer in `new_src/render/World3D.cpp:15-50`).
5. `0xF81F` is the transparency color key.
6. Some format properties force a programmable-shader backend.

## Method

* grep/read over `src/` for every texture upload site, palette load, and RLE decode
  (`Image.cpp`, `App.cpp`, `Graphics.cpp`, `GLES.cpp`, `TinyGL.cpp`, `Span.cpp`,
  `Render.cpp`, `LoadingManager.cpp`, `Resource.cpp`).
  Note: `src/Graphics.cpp` is ISO-8859 + CRLF — plain `grep` reports nothing on it;
  `grep -a` is required.
* Extracted `Payload/Doom2rpg.app/Packages/` from `build_new/new_src/Doom 2 RPG.ipa`
  and parsed all 275 `.bmp` headers with a throwaway Python script.
* Independently re-derived the `newMappings.bin` record layout from the reader code and
  checked it against the file: predicted 18432 bytes == actual.
* Independently re-derived the `newPalettes.bin` record layout (`2*n + 4` per record over
  the 671 owner entries): predicted 335932 bytes == actual size of `tmp_newPalettes.bin`.
* Reconstructed the texel-blob → (file, offset) mapping using the `0x40000` chunking rule
  and re-implemented the column-RLE decoder from `GLES.cpp` to validate every blob.
* Read `tables.bin` table offsets to confirm sky-map sizes.

## Verdicts

| # | Hypothesis | Verdict |
|---|---|---|
| 1 | Enumerable format set; 565 vs 8888 split | **CONFIRMED** |
| 2 | All shipped images palette-indexed | **CONFIRMED** |
| 3 | Column-RLE, detected by size mismatch | **CONFIRMED** (bit-exact) |
| 4 | Real BMPs in the archive | **CONFIRMED** |
| 5 | `0xF81F` key | **CONFIRMED** (5 independent sites) |
| 6 | Shader-required properties exist | **PARTIAL** — required only for the 3D world path |

## Evidence

### Format inventory (H1)

Every texture upload in `src/`:
* `Image.cpp:34` — `GL_RGB / GL_UNSIGNED_SHORT_5_6_5` (opaque BMP)
* `Image.cpp:50` — `GL_RGBA / GL_UNSIGNED_SHORT_5_5_5_1` (color-keyed BMP)
* `GLES.cpp:1117` — `GL_RGBA / GL_UNSIGNED_SHORT_5_5_5_1` (world media)
* `GLES.cpp:290` — `GL_RGBA / GL_UNSIGNED_BYTE` (procedural fade texture, 16x64)
* `Render.cpp:3822` — `GL_RGB / GL_UNSIGNED_SHORT_5_6_5` (software framebuffer present)

No 4444, no 1555, no compressed formats. ARGB8888 exists only as CPU constants
(`Graphics.h:25-28` `charColors`, fog color at `Render.cpp:1852`) and in that one
procedural texture.

Proof that the shipping iPhone build was natively paletted — `GLES.cpp:1106`:
```
//glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE8_RGB5_A1_OES, width, height, 0, height * width + 512, data);
```
with the CPU expansion loop that replaced it at `GLES.cpp:1108-1116`. The 512-byte
prefix of `data` is exactly a 256-entry RGBA5551 palette (`GLES.cpp:1048-1060`).

### All images indexed (H2)

* World media: `newTexels*.bin` holds 8-bit indices only; color comes from
  `newPalettes.bin`, read as raw LE `uint16` RGB565 at `LoadingManager.cpp:189`.
* Sky map: 8-bit indices + a 256-entry RGB565 palette, both inside `tables.bin`
  (`LoadingManager.cpp:572-583`; sizes measured: table 16 = 512 B = 256 shorts,
  table 17 = 65536 B).
* BMPs: `App.cpp:194-197` hard-rejects anything but 4 or 8 bpp:
  `Error("Expected image bpp 4 or 8. Found bpp %d", desc.bitsPerPixel);`
  Measured over all 275 shipped BMPs: 249 are 8bpp, 26 are 4bpp; none other.

### Column-RLE (H3)

Discriminator: `Size == sWidth * tHeight` → raw, else RLE
(`GLES.cpp:900`, and `Render.cpp:461,492,538` in the software path).

Format, from `GLES.cpp:947-966` + `:983-1029`:
```
nibbleBase = Size - ((T[Size-1] << 8) | T[Size-2]) - 2
runsBase   = nibbleBase + ((x1 - x0 + 1) >> 1)      // x0..x1 = mediaBounds[4i+0..1]
region [0, nibbleBase)         : 8-bit palette indices, column-major stream
region [nibbleBase, runsBase)  : 4-bit run counts per column (low nibble first)
region [runsBase, Size-2)      : (y, height) byte pairs
```
Decoder writes into a `memset(0)` buffer (`GLES.cpp:938`) so uncovered pixels are index 0.

Validation on the shipped data — the invariants `pixelCursor == nibbleBase` and
`runCursor == Size - 2` hold for **370 of 374** RLE blobs. The four failures are
media **814, 815, 816, 817**, which is *precisely* the set the port repairs in
`Render::fixTexels` (`Render.cpp:3853-3870`) by overriding run-count nibbles at
absolute blob offsets. For media 814 the port patches offset 5614; my computed
`nibbleBase` for 814 is 5603, so 5614 is column index 22 — independent confirmation
of the offset formula.

Census: 374 RLE blobs, all 256x256, all with media index < 851; 156 raw blobs of
12 distinct dimensions. `mediaMappings[257] = 851` and `TILENUM_FIRST_WALL = 257`
(`Enums.h:831`) → RLE is exclusively the sprite range, walls/floors are always raw.

The "31443 != 65536" case is media **798**, a 256x256 RLE sprite.

The software rasterizer never expands the sprite: `TinyGL::drawClippedSpriteLine`
(`TinyGL.cpp:745-834`) walks the same streams per screen column, with the identical
trailer arithmetic at `TinyGL.cpp:758`.

### Real BMPs (H4)

Reader: `Applet::createImage` (`App.cpp:162-274`), a packed 54-byte
`BITMAPFILEHEADER + BITMAPINFOHEADER` (`static_assert(sizeof(ImageDesc) == 54)`,
`App.cpp:184`). 88 distinct file names are loaded, **all** with
`isTransparentMask = true` (0 call sites pass `false`).

Measured over all 275 BMPs in `Packages/`:
* `BM`, `headerSize == 40`, `colorPlanes == 1`, `compression == 0` — uniformly.
* `height > 0` for all → bottom-up; the reader flips at `App.cpp:264-270`.
* `offBeg == 54 + effectiveColorsUsed * 4` for all 275 — the reader's assumption
  (it ignores `offBeg`, `App.cpp:208`) is safe for this data set.
* 160 of 275 palettes contain exact `0x00FF00FF`.
* `fileSize` header field is wrong in 23 files; unused by the reader.

Two defects worth not copying:
* `App.cpp:255` iterates `col < desc.width / 2` for 4bpp, dropping the last pixel of
  odd-width rows. Odd-width 4bpp files do ship: `images04` (79), `images10` (37),
  `toEarth` (37), `dpad*` (117), `travelMapVertGrid` (1).
* `GLES.cpp:865-893` always reads 256 palette entries (`} while (v15 < 256);`,
  `GLES.cpp:893`) although 671 of the shipped palettes have 2..255 entries — an
  out-of-bounds read. A rewrite must clamp to `mediaPalettesSizes[palIdx]`.

Unreferenced archive leftovers (zero grep hits in `src/`): all 50 `.png`,
`images.idx` + `images00..12.bmp`, `page_icons.zip`, `skymap_01.bmp`,
`tables.bin.gz`, `entities 2.bin`.

### `0xF81F` (H5)

`0xF81F` = RGB565 (31,0,31) = RGB888 `0xFF00FF`. Sites:
* `Render.cpp:2093-2098` — scans the loaded palette for `0xF81F` to derive
  `paletteTransparentMask`; consumed by `Span.cpp:59,77,95`
  (`if (b != pMask) { *pixels = spanPalette[b]; }`). Field marked `// [GEC] new`
  (`TinyGL.h:108`), i.e. the porter generalised what was probably a fixed index.
* `Image.cpp:41` — `if (pix == 0xf81f) { pix = 0x0000; }` before the 5551 upload.
* `Graphics.cpp:336`, `GLES.cpp:1189`, `ComicBook.cpp:467` — key detection driving
  `isTransparentMask`.
* `GLES.cpp:880-889` — GL world path, with two tolerances (`g == 0` and `g == 4`,
  the latter commented "Arachnotron attack"); final zeroing at `GLES.cpp:1048-1054`.

Second, independent transparency mechanism for sprites: **index 0**, because the RLE
target buffer is zero-filled (`GLES.cpp:938`) and entry 0 is forced to
`(0,0,0,0)` at `GLES.cpp:1035-1043` (optionally after being set to the average of
entries 1..255, `GLES.cpp:939-944`, so filtering bleeds neutral grey rather than black).

Note the *same* constant `0xF81F` is also the RGB565 R|B channel mask inside the fog
table builder (`Render.cpp:1823-1832`) — dual use, easy to misread.

True alpha exists in exactly two places: the procedural fade texture
(`GLES.cpp:277-290`) and constant `glColor4f/glColor4ub` alphas in the render modes
(`Image.cpp:167,175,187,197,201`). Alpha test is always `GL_GREATER, 0` → 1-bit cutout.

### Shader requirements (H6)

Requires programmable pipeline (or the indexed-texture trick):
* GPU palette expansion (the original's `GL_PALETTE8_RGB5_A1_OES`, `GLES.cpp:1106`);
  *optional*, since the port itself proves CPU expansion works.
* Fog: software = one of 16 pre-baked palettes selected **per span** from interpolated
  1/z (`TinyGL::getFogPalette`, `TinyGL.cpp:47-52`, called at `TinyGL.cpp:654,662`);
  GL = fixed-function `GL_LINEAR` fog (`GLES.cpp:100-103`), absent in GL 3.3 core.
* The 16-level ramp itself is a nonlinear 565-space op (`Render.cpp:1823-1832`).
* Palette-level render-mode effects: `Render::setupPalette` (`Render.cpp:1885-2023`) —
  greyshift `0x4`, pulsate `0x200`, multiply-shift, channel masks `0xFFFFE79C`/`0xFFFFF7DE`;
  emulated on GL via `glTexEnv` combiners (`gles::TexCombineShift`, `GLES.cpp:1258-1279`).

Needs nothing special:
* All BMP-derived content — already CPU-expanded to RGB565/RGBA5551
  (`Graphics.cpp:330-343`, `Image.cpp:23-52`).
* The `0xF81F` key — resolved at texture-build time into 1-bit alpha.
* Column-RLE decode — pure CPU.
* Atlas/quad geometry, rotate/mirror modes 0..8 (`Image.cpp:120-155`).
* Constant-alpha and modulate blend modes.
* Framebuffer presentation (one textured quad).

**Conclusion for backend planning:** the shader-only requirements are confined to the
3D world renderer (indexed textures + fog/palette shifts). The whole 2D layer runs on
a plain textured-quad API with tint, blend mode and 1-bit alpha. A backend split along
that seam mirrors the original's own split (`Graphics`/`Image` vs `TinyGL`/`gles`).

## Open questions

* Was the transparent index in the true original always 0 for RLE sprites, with the
  `0xF81F` palette scan being purely a `[GEC]` generalisation? `TinyGL.h:108` is marked
  `[GEC] new`, so the original software path's exact test is unrecovered from `src/`.
* `Render.cpp:1837` tests `(mediaPalColors[n5] & 0x4000)` where the post-load owner flag
  is `0x40000000`; bit 14 can never be set for a slot < 1024, so both calls at
  `Render.cpp:1882-1883` are dead in this port. Was this a transcription error, or does
  the original use a different encoding at that point?
* The exact meaning of `v47`'s per-tile exception list in `GLES.cpp:900-933`
  (`mediaMappings[{197,202,128,160,168,162,129}]`, tiles 184..193) — which raw images
  must *not* get index-0 transparency — is only partly explained by tile identity.
* `mediaBounds` for raw wall textures is `[0,256,0,256]` and is cast to `uint8_t` in
  `GLES.cpp:961-964`; harmless today, but it means `mediaBounds` semantics differ between
  the raw and RLE families.

## Durable output

`docs/original-code/image-formats.md` (new).
