# Doom II RPG — Resource Format Reference

This document describes the binary file formats used by Doom II RPG (J2ME/iPhone mobile version)
and explains how they were reverse-engineered from the decompiled C++ source code.

---

## How the formats were discovered

All formats were recovered by **reading the decompiled C++ source** directly, not by guessing:

| File | Contains |
|---|---|
| `src/Render.cpp` | `FinalizeMedia()` — palette and texel loading; `beginLoadMap()` — map parsing |
| `src/GLES.cpp` | `CreateTextureForMediaID()` — sprite/texture decoding |
| `src/TinyGL.cpp` | Matrix math, coordinate system |
| `src/Enums.h` | `TILENUM_*` constants — names for all tiles |
| `src/Resource.h` | Resource filenames |

Methodology:
1. Read the C++ loader function (e.g. `FinalizeMedia`)
2. Identify the byte layout from variable names and offsets
3. Reproduce the same logic in Python, keeping the same variable names (`n12`, `m`, `n14`…)
4. Run against real files, inspect output, iterate

---

## newMappings.bin

The master index file. Contains metadata for all media resources in the game.
Fixed size: **18 432 bytes**.

### Layout (sequential)

```
512  × int16   mediaMappings     — tile N → media ID range [mapping[N], mapping[N+1])
1024 × uint8   mediaDimensions   — packed log2: bits [7:4] = log2(width), [3:0] = log2(height)
4096 × int16   mediaBounds       — 4 int16 per media ID: shapeMin, shapeMax, minY, maxY (used as uint8)
1024 × int32   mediaPalColors    — palette entry count / reference flag
1024 × int32   mediaTexelSizes   — texel byte count / reference flag
```

### Reading a tile

```python
tile_id = 276  # TILENUM_DOOR_UNLOCKED
lo = mediaMappings[tile_id]       # first media ID
hi = mediaMappings[tile_id + 1]   # one past the last (exclusive)
# media IDs for this tile: range(lo, hi)
# multiple IDs = animation frames
```

### mediaDimensions

```
byte = mediaDimensions[media_id]
log_w = (byte >> 4) & 0xF    # log2(width)
log_h =  byte       & 0xF    # log2(height)
width  = 1 << log_w          # always a power of two
height = 1 << log_h
```

Common values: `0x88` → 256×256, `0x87` → 256×128, `0x44` → 16×16.

### mediaPalColors and mediaTexelSizes — flag scheme

Both fields share the same flag encoding:

```
Bit 31 = MEDIA_FLAG_REFERENCE (0x80000000)
  → this media ID is an alias for another
  → bits [9:0] hold the source media ID
  → no data in the file; reuse the source's data

Bit 31 = 0
  → bits [29:0] = element count (for palettes) or size−1 (for texels)
```

---

## newPalettes.bin

Contains the colour palette (lookup table) for every media resource.

### Format

Entries are stored **sequentially** for media IDs 0–1023, skipping reference entries:

```
for each media_id where (mediaPalColors[id] & 0x80000000) == 0:
    N × uint16   RGB565 colours  (N = mediaPalColors[id] & 0x3FFFFFFF)
    4 bytes      CAFEBABE marker
```

N is always 256 (a complete 8-bit palette).

### RGB565 → RGBA8888 conversion

RGB565 packs three channels into 16 bits:

```
bits [15:11] = R5   [10:5] = G6   [4:0] = B5

r8 = (r5 << 3) | (r5 >> 2)   # 5-bit → 8-bit (bit-replicated scale)
g8 = (g6 << 2) | (g6 >> 4)   # 6-bit → 8-bit
b8 = (b5 << 3) | (b5 >> 2)   # 5-bit → 8-bit
a8 = 255
```

### Transparency colour key

Magenta pixels are treated as transparent (alpha = 0):

```
if r8 >= 250 and g8 == 0 and b8 >= 250:  alpha = 0   # #FF00FF
if r8 >= 250 and g8 == 4 and b8 >= 250:  alpha = 0   # arachnotron attack variant
```

The same check appears in `GLES.cpp::CreateTextureForMediaID`.

---

## newTexels000.bin … newTexels038.bin

Raw texel data (palette indices, one byte per pixel).

### File-splitting logic

Data is split across files at **262 144-byte** (0x40000) boundaries.
The boundary is tracked by a cumulative offset `n12` (variable name from `FinalizeMedia` in C++):

```python
n12 = 0   # position within the current file
m   = 0   # file index (newTexels{m:03d}.bin)

for each media_id (skip references):
    read n14 bytes at offset n12
    skip 4-byte marker
    n12 += n14 + 4

    if n12 > 0x40000:
        m  += 1    # move to the next file
        n12 = 0    # position resets
```

Reference media IDs (bit 31 = 1) are skipped entirely — they have no data in the file.

### Computing n14

```python
raw_val = mediaTexelSizes[media_id] & 0xFFFFFFFF
n14 = (raw_val & 0x3FFFFFFF) + 1   # +1 because the stored value is size−1
```

---

## Decoding textures

### Detecting wall vs. sprite

```python
size = len(texel_bytes)   # equals n14
area = width * height

if size == area:
    # Wall / floor / ceiling: raw palette indices, row-major
else:
    # Sprite: column-RLE compressed
```

### Wall textures

The simplest format: `size` bytes, each one a palette index, row by row left to right:

```python
for i, idx in enumerate(texel_bytes):
    x = i % width
    y = i // width
    pixel[y][x] = palette[idx]
```

### Sprites — column RLE

Implemented in `GLES.cpp` lines 946–1031. The buffer layout:

```
[pixel indices ... | nybble section | run pairs | 2-byte count]
```

**Buffer layout:**

```
Size         = len(raw)
last2        = (raw[Size-1] << 8) | raw[Size-2]   # byte count of (nybbles + run pairs)
nybble_start = Size - last2 - 2                    # offset of nybble section

num_cols      = shapeMax - shapeMin                # number of columns with data
nybble_bytes  = (num_cols + 1) >> 1               # ceil(num_cols / 2) bytes
run_pair_start = nybble_start + nybble_bytes       # offset of (y, height) run pairs
```

**Nybble section** — 4 bits per column, packed low-nybble first:

```
col_offset 0 → low  nybble of byte 0
col_offset 1 → high nybble of byte 0
col_offset 2 → low  nybble of byte 1
...
```

The nybble value = number of runs in that column.

**Run pairs:** for each run:
```
y_start    uint8   — starting row
run_height uint8   — number of rows
```

**Pixels:** consumed sequentially from the start of the buffer (`pixel_idx = 0`),
filling runs as columns are processed.

**Decode algorithm:**

```python
pixel_idx = 0
run_ptr   = run_pair_start

for col_offset in range(num_cols):
    col       = shapeMin + col_offset
    run_count = nybble_at(col_offset)   # low/high nybble as above

    for _ in range(run_count):
        y_start    = raw[run_ptr];     run_ptr += 1
        run_height = raw[run_ptr];     run_ptr += 1

        for row in range(y_start, y_start + run_height):
            output[row * width + col] = raw[pixel_idx]
            pixel_idx += 1
```

---

## Map files: map00.bin … map09.bin

Binary BSP maps. Loader: `Render::beginLoadMap` in `Render.cpp`.

### High-level structure

```
Map header          (dimensions, counts)
Tile grid section   (grid data)
Entity section      (monsters, items, triggers)
BSP nodes           (tree nodes + polygon geometry)
Maya cameras        (camera paths for cutscenes)
```

Sections are separated by 4-byte markers `CAFEBABE` or `DEADBEEF`.

### Coordinate system

The game uses fixed-point integer math. Vertices are stored as `uint8`:

```
vertex_world = byte_value * 128   (left-shift by 7)
tile_index   = world_coord >> 6   (1 tile = 64 world units)
```

Converting to OBJ (float, Y-up right-handed):

```python
WORLD_SCALE = 1.0 / 64.0   # 1 OBJ unit = 1 tile

def to_obj_coords(x, y, z):
    return x * WORLD_SCALE, z * WORLD_SCALE, -y * WORLD_SCALE
```

The Y axis is negated because the game's Y axis points into the screen
while OBJ's Z axis points toward the viewer.

The scale `1/64` was confirmed by reading `TinyGL.cpp`:
`viewX >> 6 = tile_index`, i.e. 1 tile = 64 world units.

### BSP tree and polygon data

The BSP tree contains internal nodes (space partitioners) and leaf nodes
(nodes that hold actual polygons). Critical finding:

```
nodeOffsets[n] == 0xFFFF  →  leaf node: contains polygon meshes
nodeOffsets[n] != 0xFFFF  →  internal node: contains child references, not geometry
```

Reading internal nodes as geometry produces garbage (556 000+ bogus polygons
observed before this filter was applied). Filtering to leaf nodes only drops
polygon count to the correct range.

### nodePolys format (leaf geometry)

```
1 byte    meshCount          — number of meshes in this leaf
per mesh:
    4 bytes  unknown
    uint16   packed          — (textureID << 7) | polyCount
per polygon:
    uint8    vertCount
    vertCount × (uint8 x, uint8 y, uint8 z)
```

Sanity caps to detect corrupted data: `meshCount ≤ 64`, `polyCount ≤ 127`,
`vertCount ≤ 9`.

### Vertex winding and face orientation

The game renders **clockwise** (CW) faces as front-facing
(culling mode = cull CCW back faces). OBJ format expects **counter-clockwise** (CCW).

Solution — reverse vertex order on export:

```python
face_str = "f " + " ".join(f"{vi}/{ti}" for vi, ti in reversed(idxs))
```

### Maya camera paths

Each map may contain camera animation paths for cutscenes.
These are stored per-camera with variable-length data.

Per camera:
```
uint8         num_keys          — number of keyframes
int16         unknown
num_keys × 7 × int16            — position/rotation keyframe data
num_keys × 6 × int16            — additional keyframe data
6 × int16     n3_counts         — per-channel tween counts (clamped to ≥ 0)
CAFEBABE marker
n3 × bytes    tween data        (n3 = sum of max(0, each of the 6 counts))
```

Then one final CAFEBABE marker after all cameras.

**Bug that was hit:** using the map-level `total_maya_tweens` header value instead of
computing `n3` from the 6 per-camera int16 fields caused a buffer overflow error.
The per-camera computation is required.

---

## Step-by-step resource reading summary

```
1. newMappings.bin
   → for each tile: media ID range (animation frames)
   → for each media ID: dimensions, reference flags, sprite bounds

2. newPalettes.bin
   → read sequentially for all non-reference media IDs
   → each entry: N × uint16 RGB565 + CAFEBABE marker
   → convert to 256-entry RGBA8888 lookup table

3. newTexels000–038.bin
   → read sequentially, switch file when n12 > 0x40000
   → each entry: n14 bytes + CAFEBABE marker
   → if size == width × height: wall texture (raw indices)
   → otherwise: sprite (apply column-RLE decoder)

4. Apply LUT: texel_byte_value → palette[texel_byte_value] → RGBA pixel
```
