# Doom II RPG — Tools

A pair of Python 3 scripts for extracting assets from the game's binary data files.

## Requirements

```bash
pip install Pillow
```

Python 3.8+ required. No other dependencies.

---

## extract_textures.py — texture extractor

Reads `newMappings.bin`, `newPalettes.bin`, and `newTexels000–038.bin` and saves
every texture / sprite as a PNG file with a human-readable name.

### Usage

```bash
# Extract everything
python extract_textures.py <game_data_dir> [output_dir]

# Single tile (all animation frames)
python extract_textures.py <game_data_dir> [output_dir] --tile 276

# Single media ID (internal index)
python extract_textures.py <game_data_dir> [output_dir] --media 870
```

**Arguments**

| Argument | Description |
|---|---|
| `game_data_dir` | Folder containing `newMappings.bin`, `newPalettes.bin`, `newTexels*.bin` |
| `output_dir` | Destination folder (default: `textures_out/`) |
| `--tile N` | Extract only tile N (maps to one or more media IDs / animation frames) |
| `--media N` | Extract a single media ID directly |

### Output naming

Output files are named after game constants from `Enums.h`. Examples:

```
assault_rifle_f00.png   ← tile 1, frame 0  (sprite, transparent background)
assault_rifle_f01.png   ← tile 1, frame 1
door_unlocked_f00.png   ← tile 276, frame 0  (wall texture, fully opaque)
flat_lava.png           ← tile 479  (floor flat, fully opaque)
monster_zombie_f00.png  ← tile 20, frame 0  (sprite, transparent background)
wall_350.png            ← unnamed wall tile 350
```

Animated tiles produce multiple `_f00`, `_f01`, … files.

### What each PNG contains

- **Wall / floor textures** — fully opaque RGBA, palette-indexed pixels mapped to RGBA8888
- **Sprites** — RGBA with transparent background (palette index 0 or magenta `#FF00FF` = alpha 0)

---

## map_to_obj.py — map geometry extractor

Reads a binary map file (`map00.bin` … `map09.bin`) and writes:

- `<name>.obj` — Wavefront OBJ geometry, one material group per wall texture
- `<name>.mtl` — MTL material file referencing PNG files from `extract_textures.py`
- `<name>.json` — JSON metadata (entities, triggers, camera paths, etc.)

### Usage

```bash
python map_to_obj.py <mapXX.bin> [output_dir]
```

**Example**

```bash
python map_to_obj.py ../UnPackGameData/map00.bin ./map00_out/
```

Opens `map00_out/map00.obj` in Blender, Maya, or any OBJ viewer.

### Coordinate system

The OBJ file uses standard Y-up right-handed coordinates:

| OBJ axis | Game axis | Scale |
|---|---|---|
| X | game X | 1 tile = 1 unit |
| Y | game Z (height) | 1 tile = 1 unit |
| Z | −game Y | 1 tile = 1 unit |

One game tile = 64 world units in the original fixed-point system.
The OBJ output normalises this so 1 OBJ unit = 1 tile = 64 original units.

### Texture references in MTL

The MTL file references textures by name, e.g. `door_unlocked.png`.
Place the output of `extract_textures.py` next to the OBJ file (or adjust the
paths in the MTL) and the viewer will load them automatically.

---

## Typical workflow

```bash
# 1. Unpack game data (done once)
#    Place all *.bin files into a single directory, e.g. UnPackGameData/

# 2. Extract all textures
python extract_textures.py UnPackGameData/ textures/

# 3. Convert a map
python map_to_obj.py UnPackGameData/map00.bin map00/

# 4. Copy textures next to the OBJ
cp textures/*.png map00/

# 5. Open map00/map00.obj in Blender / Maya / MeshLab
```

For reverse-engineering details see `readmeResourceEng.md` / `readmeResourceRu.md`.
