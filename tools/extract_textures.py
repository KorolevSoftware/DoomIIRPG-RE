#!/usr/bin/env python3
"""
Doom II RPG texture extractor.
Reads newMappings.bin, newPalettes.bin, newTexels000-038.bin and saves PNG files.

Usage:
    python extract_textures.py <game_data_dir> [output_dir]
    python extract_textures.py <game_data_dir> [output_dir] --tile 301   # single tile
    python extract_textures.py <game_data_dir> [output_dir] --media 42   # single media ID

Format summary (from GLES.cpp / Render.cpp):
  newMappings.bin  : mediaMappings(512×i16) + mediaDimensions(1024×u8)
                     + mediaBounds(4096×i16) + mediaPalColors(1024×i32)
                     + mediaTexelSizes(1024×i32)
  newPalettes.bin  : for each non-reference media ID: n×u16 RGB565 + 4-byte marker
  newTexels*.bin   : for each non-reference media ID: n bytes + 4-byte marker
                     (file split when cumulative offset > 0x40000)

Texel decoding:
  Wall (Size == width*height): raw palette indices, row-major
  Sprite (Size != width*height): column RLE
    Layout: [pixel_data | nybble_section | run_pairs | 2-byte_count]
    nybble_section: 4-bit run count per column (columns shapeMin..shapeMax-1)
    run_pairs: (y_start u8, run_height u8) per run
    pixel_data: raw palette indices consumed by runs
"""

import struct
import sys
import os
import argparse
from pathlib import Path
from typing import Optional

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow not installed — run: pip install Pillow")


MEDIA_FLAG_REFERENCE = 0x80000000
TEXEL_FILE_SPLIT     = 0x40000   # switch to next newTexels file after this many bytes
MEDIA_MAX            = 1024


# ──────────────────────────────────────────────────────
# Read newMappings.bin
# ──────────────────────────────────────────────────────

def load_mappings(path: Path):
    data = path.read_bytes()
    pos = 0

    def read_i16_array(n):
        nonlocal pos
        arr = list(struct.unpack_from(f'<{n}h', data, pos))
        pos += n * 2
        return arr

    def read_u8_array(n):
        nonlocal pos
        arr = list(data[pos:pos+n])
        pos += n
        return arr

    def read_i32_array(n):
        nonlocal pos
        arr = list(struct.unpack_from(f'<{n}i', data, pos))
        pos += n * 4
        return arr

    media_mappings   = read_i16_array(512)      # tile_id → media ID range
    media_dimensions = read_u8_array(1024)       # packed log2(w) | log2(h)
    media_bounds_raw = read_i16_array(4096)      # 4 per media: shapeMin,shapeMax,minY,maxY
    media_pal_colors = read_i32_array(1024)      # palette entry counts / reference flags
    media_texel_sizes= read_i32_array(1024)      # texel byte counts / reference flags

    # Split bounds into per-media tuples; cast to uint8 (as the game does)
    media_bounds = []
    for i in range(1024):
        b = media_bounds_raw[i*4:(i+1)*4]
        media_bounds.append(tuple(v & 0xFF for v in b))  # (shapeMin, shapeMax, minY, maxY)

    return media_mappings, media_dimensions, media_bounds, media_pal_colors, media_texel_sizes


# ──────────────────────────────────────────────────────
# Read newPalettes.bin
# ──────────────────────────────────────────────────────

def load_palettes(path: Path, media_pal_colors: list) -> dict:
    """
    Returns: pal_for_media[media_id] = list of (r,g,b,a) tuples (256 max entries).
    Entries beyond the stored count are (0,0,0,0).
    """
    raw = path.read_bytes()
    pos = 0

    # palette_store[i] = rgba_lut list, filled sequentially
    palette_store = []
    # direct_index[media_id] = index into palette_store (for non-reference entries)
    direct_index  = {}

    for j in range(MEDIA_MAX):
        raw_val = media_pal_colors[j] & 0xFFFFFFFF
        is_reference = bool(raw_val & MEDIA_FLAG_REFERENCE)
        if is_reference:
            continue  # no data in file for references

        count = raw_val & 0x3FFFFFFF
        # Read count × uint16 RGB565 entries
        if pos + count * 2 > len(raw):
            break
        entries = struct.unpack_from(f'<{count}H', raw, pos)
        pos += count * 2
        pos += 4  # skip CAFEBABE marker

        # Convert RGB565 → RGBA8888 LUT (pad to 256)
        lut = []
        for rgb565 in entries:
            r5 = (rgb565 >> 11) & 0x1F
            g6 = (rgb565 >> 5)  & 0x3F
            b5 =  rgb565        & 0x1F
            r8 = (r5 << 3) | (r5 >> 2)
            g8 = (g6 << 2) | (g6 >> 4)
            b8 = (b5 << 3) | (b5 >> 2)
            # Magenta (R≥250, G==0, B≥250) = transparent
            a8 = 0 if (r8 >= 250 and g8 == 0 and b8 >= 250) else 255
            a8 = 0 if (r8 >= 250 and g8 == 4 and b8 >= 250) else a8
            lut.append((r8, g8, b8, a8))

        # Pad to 256 with transparent black
        while len(lut) < 256:
            lut.append((0, 0, 0, 0))

        direct_index[j] = len(palette_store)
        palette_store.append(lut)

    # Resolve reference entries
    pal_for_media = {}
    for j in range(MEDIA_MAX):
        raw_val = media_pal_colors[j] & 0xFFFFFFFF
        is_reference = bool(raw_val & MEDIA_FLAG_REFERENCE)
        if is_reference:
            ref_j = raw_val & 0x3FF
            if ref_j in direct_index:
                pal_for_media[j] = palette_store[direct_index[ref_j]]
        else:
            if j in direct_index:
                pal_for_media[j] = palette_store[direct_index[j]]

    return pal_for_media


# ──────────────────────────────────────────────────────
# Read newTexels*.bin
# ──────────────────────────────────────────────────────

def load_texels(data_dir: Path, media_texel_sizes: list) -> dict:
    """
    Returns: texels_for_media[media_id] = bytes of raw texel data.

    Matches FinalizeMedia() in Render.cpp:
      n12 = cumulative byte offset in current file (advances for every non-ref entry)
      m   = current file index (increments when n12 > 0x40000)
    """
    direct_index  = {}
    texel_store   = []

    m             = 0    # current file index (matches C++ m)
    loaded_m      = -1   # index of currently loaded file (matches C++ n10)
    file_data     = None
    n12           = 0    # position within current file (matches C++ n12/n11, same since we read all)

    for j in range(MEDIA_MAX):
        raw_val = media_texel_sizes[j] & 0xFFFFFFFF
        is_ref  = bool(raw_val & MEDIA_FLAG_REFERENCE)
        n14     = (raw_val & 0x3FFFFFFF) + 1

        if not is_ref:
            # Load file lazily when file index changes (matches C++: if (m != n10))
            if m != loaded_m:
                texel_path = data_dir / f"newTexels{m:03d}.bin"
                if not texel_path.exists():
                    print(f"  Warning: {texel_path} not found, stopping texel load")
                    break
                file_data = texel_path.read_bytes()
                loaded_m  = m

            texel_bytes = file_data[n12 : n12 + n14]
            if len(texel_bytes) < n14:
                texel_bytes = texel_bytes + bytes(n14 - len(texel_bytes))

            direct_index[j] = len(texel_store)
            texel_store.append(texel_bytes)

            # Advance position past data + 4-byte marker (matches C++: n12 = n11 = n12 + n14 + 4)
            n12 += n14 + 4

        # File-split check is unconditional in C++ (outside if/else), fires only after non-ref entry
        if n12 > TEXEL_FILE_SPLIT:
            m       += 1
            loaded_m = -1   # force file reload on next non-ref entry
            n12      = 0

    # Resolve references
    texels_for_media = {}
    for j in range(MEDIA_MAX):
        raw_val = media_texel_sizes[j] & 0xFFFFFFFF
        is_ref  = bool(raw_val & MEDIA_FLAG_REFERENCE)
        if is_ref:
            ref_j = raw_val & 0x3FF
            if ref_j in direct_index:
                texels_for_media[j] = texel_store[direct_index[ref_j]]
        else:
            if j in direct_index:
                texels_for_media[j] = texel_store[direct_index[j]]

    return texels_for_media


# ──────────────────────────────────────────────────────
# Decode sprite (column-RLE compressed)
# ──────────────────────────────────────────────────────

def decode_sprite(raw: bytes, width: int, height: int, bounds) -> bytearray:
    """
    Returns flat palette-index buffer [height*width], row-major.
    bounds = (shapeMin, shapeMax, minY, maxY) all uint8.
    """
    size = len(raw)
    if size < 2:
        return bytearray(width * height)

    shape_min, shape_max, min_y, max_y = bounds

    # Last 2 bytes: byte count of (nybble_section + run_pairs)
    last2 = (raw[size - 1] << 8) | raw[size - 2]
    nybble_start = size - last2 - 2

    # Run pairs start after nybble section: one nybble per column, 2 per byte
    num_cols     = shape_max - shape_min        # number of columns with data
    nybble_bytes = (num_cols + 1) >> 1          # ceil(num_cols / 2)
    run_pair_start = nybble_start + nybble_bytes

    output = bytearray(width * height)  # all transparent = index 0

    pixel_idx    = 0   # sequential read from start of raw (pixel palette indices)
    run_ptr      = run_pair_start

    for col_offset in range(num_cols):
        col = shape_min + col_offset

        # Read 4-bit run count from nybble section
        nybble_byte = raw[nybble_start + (col_offset >> 1)]
        if col_offset & 1:
            run_count = (nybble_byte >> 4) & 0xF
        else:
            run_count = nybble_byte & 0xF

        for _ in range(run_count):
            if run_ptr + 1 >= size:
                break
            y_start    = raw[run_ptr];     run_ptr += 1
            run_height = raw[run_ptr];     run_ptr += 1

            for row in range(y_start, y_start + run_height):
                if row < height and col < width and pixel_idx < nybble_start:
                    output[row * width + col] = raw[pixel_idx]
                pixel_idx += 1

    return output


# ──────────────────────────────────────────────────────
# Decode one media ID → PIL Image
# ──────────────────────────────────────────────────────

def decode_media(media_id: int, media_dimensions: list, media_bounds: list,
                 pal_for_media: dict, texels_for_media: dict) -> Optional[Image.Image]:

    if media_id not in pal_for_media or media_id not in texels_for_media:
        return None

    dim    = media_dimensions[media_id]
    log_h  = dim & 0xF
    log_w  = (dim >> 4) & 0xF
    width  = 1 << log_w
    height = 1 << log_h

    lut    = pal_for_media[media_id]   # 256 × (r,g,b,a)
    raw    = texels_for_media[media_id]
    size   = len(raw)
    area   = width * height

    if size == 0:
        return None

    # Wall/flat texture: raw palette indices, row-major
    if size == area:
        indices = raw
    else:
        # Sprite: column-RLE compressed
        bounds = media_bounds[media_id]
        indices = decode_sprite(raw, width, height, bounds)

    # Apply LUT: index → RGBA
    img_data = bytearray(area * 4)
    for i, idx in enumerate(indices[:area]):
        r, g, b, a = lut[idx]
        img_data[i*4:i*4+4] = bytes([r, g, b, a])

    return Image.frombytes('RGBA', (width, height), bytes(img_data))


# ──────────────────────────────────────────────────────
# Tile name lookup (from Enums.h TILENUM_*)
# ──────────────────────────────────────────────────────

TILE_NAMES = {
    1:"assault_rifle", 2:"chainsaw", 3:"holy_water_pistol",
    4:"sentry_bot_shooting", 5:"sentry_bot_exploding",
    6:"super_shotgun", 7:"chaingun", 8:"assault_rifle_scope",
    9:"plasma_gun", 10:"rocket_launcher", 11:"bfg",
    12:"soul_cube", 13:"sentry_bot_red_shooting", 14:"sentry_bot_red_exploding",
    15:"world_weapon",
    18:"monster_red_sentry_bot", 19:"monster_sentry_bot",
    20:"monster_zombie", 21:"monster_zombie2", 22:"monster_zombie3",
    23:"monster_imp", 24:"monster_imp2", 25:"monster_imp3",
    26:"monster_saw_goblin", 27:"monster_saw_goblin2", 28:"monster_saw_goblin3",
    29:"monster_lost_soul", 30:"monster_lost_soul2", 31:"monster_lost_soul3",
    32:"monster_pinky", 33:"monster_pinky2", 34:"monster_pinky3",
    35:"monster_revenant", 36:"monster_revenant2", 37:"monster_revenant3",
    38:"monster_mancubus", 39:"monster_mancubus2", 40:"monster_mancubus3",
    41:"monster_cacodemon", 42:"monster_cacodemon2", 43:"monster_cacodemon3",
    44:"monster_sentinel", 45:"monster_sentinel2", 46:"monster_sentinel3",
    50:"monster_arch_vile", 51:"monster_arch_vile2", 52:"monster_arch_vile3",
    53:"monster_arachnotron", 54:"boss_cyberdemon",
    56:"boss_pinky", 57:"boss_mastermind",
    58:"boss_vios", 59:"boss_vios2", 60:"boss_vios3",
    61:"boss_vios4", 62:"boss_vios5",
    66:"npc_riley_oconnor", 68:"npc_major", 69:"npc_bob",
    71:"npc_civilian", 72:"npc_sarge", 73:"npc_female",
    74:"npc_evil_scientist", 75:"npc_scientist",
    76:"npc_researcher", 77:"npc_civilian2",
    85:"ammo_bullets", 86:"ammo_shells", 88:"ammo_rockets",
    89:"ammo_cells", 90:"ammo_holy_water",
    107:"one_uac_credit", 110:"key_red", 111:"key_blue",
    112:"armor_jacket", 113:"satchel", 114:"pack_item", 115:"worker_pack",
    116:"health_pack", 117:"food_plate",
    119:"armor_shard", 121:"obj_table", 122:"hell_seal",
    123:"toilet", 124:"dirt_decal", 125:"ladder",
    126:"blood_splatter", 127:"sink", 128:"barred_window", 129:"hazard_bar",
    130:"obj_fire", 131:"treadmill_monitor",
    133:"tech_station", 134:"water_spout", 135:"obj_chair", 136:"obj_torchiere",
    137:"obj_scientist_corpse", 138:"obj_corpse", 139:"obj_other_corpse",
    140:"dummy_pain", 141:"attack_dummy", 142:"exit_dummy", 143:"use_dummy",
    147:"septic_station", 149:"practice_target", 150:"obj_printer",
    152:"obj_crate", 153:"vending_machine", 154:"armor_repair",
    155:"closed_portal_eye", 156:"eye_portal", 157:"portal_socket",
    158:"treadmill_side", 159:"treadmill_front",
    161:"hell_hands", 162:"doorjamb_decal",
    164:"stones_and_skulls", 168:"fence",
    170:"sentinel_spikes", 173:"switch", 175:"tech_detail",
    177:"hell_skulls", 178:"glass",
    179:"terminal_target", 180:"terminal_general",
    181:"terminal_vios", 182:"terminal_bot", 183:"terminal_hacking",
    184:"elevator_nums",
    187:"bush", 188:"tree_top", 189:"tree_trunk",
    193:"sfx_lightglow1", 197:"window3", 201:"glaevenscope",
    208:"fog_gray", 212:"scorch_mark", 223:"static_flame",
    225:"missile_player_rocket", 226:"missile_rocket",
    227:"flesh", 232:"shadow", 234:"anim_fire", 235:"anim_water",
    236:"air_vent", 239:"soul_cube_attack", 240:"water_stream",
    241:"caco_plasma", 242:"fire_ball", 243:"plasma_ball", 244:"bfg_ball",
    245:"monster_claw", 246:"monster_bite", 247:"monster_blunt_trauma",
    248:"electric_slide", 251:"fear_eye", 252:"acid_spit",
    254:"npc_chat", 255:"alert",
    257:"doorjamb",
    271:"red_door_locked", 272:"red_door_unlocked",
    273:"blue_door_locked", 274:"blue_door_unlocked",
    275:"door_locked", 276:"door_unlocked",
    277:"level_door_locked", 278:"level_door_unlocked",
    301:"sky_box", 302:"fade",
    479:"flat_lava", 480:"flat_lava2",
}

def tile_name(tile_id: int) -> str:
    if tile_id in TILE_NAMES:
        return TILE_NAMES[tile_id]
    if 1   <= tile_id <= 14:  return f"viewweapon_{tile_id}"
    if 18  <= tile_id <= 63:  return f"monster_{tile_id}"
    if 65  <= tile_id <= 80:  return f"npc_{tile_id}"
    if 81  <= tile_id <= 212: return f"item_{tile_id}"
    if 225 <= tile_id <= 256: return f"sprite_{tile_id}"
    if 257 <= tile_id <= 449: return f"wall_{tile_id}"
    if 450 <= tile_id <= 512: return f"flat_{tile_id}"
    return f"tex_{tile_id}"


# ──────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Doom II RPG texture extractor")
    ap.add_argument("data_dir",  help="Directory with newMappings.bin, newPalettes.bin, newTexels*.bin")
    ap.add_argument("output_dir", nargs="?", default="textures_out")
    ap.add_argument("--tile",   type=int, default=None, help="Extract single tile ID only")
    ap.add_argument("--media",  type=int, default=None, help="Extract single media ID only")
    args = ap.parse_args()

    data_dir   = Path(args.data_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Loading newMappings.bin …")
    (media_mappings, media_dimensions,
     media_bounds, media_pal_colors,
     media_texel_sizes) = load_mappings(data_dir / "newMappings.bin")

    print("Loading newPalettes.bin …")
    pal_for_media = load_palettes(data_dir / "newPalettes.bin", media_pal_colors)

    print("Loading newTexels*.bin …")
    texels_for_media = load_texels(data_dir, media_texel_sizes)

    print(f"  palettes loaded : {len(pal_for_media)}")
    print(f"  texel sets loaded: {len(texels_for_media)}")

    # Build list of (tile_id, media_id, name) to export
    tasks = []

    if args.media is not None:
        tasks.append((None, args.media, f"media_{args.media:04d}"))
    elif args.tile is not None:
        t = args.tile
        if t < 511:
            lo, hi = media_mappings[t], media_mappings[t + 1]
            for mid in range(lo, hi):
                if mid < 0:
                    continue   # sentinel for unused tile slots
                frame = mid - lo
                suffix = f"_f{frame:02d}" if (hi - lo > 1 and frame > 0) else ""
                tasks.append((t, mid, f"{tile_name(t)}{suffix}"))
    else:
        # All tiles
        for t in range(511):
            lo = media_mappings[t]
            hi = media_mappings[t + 1]
            for mid in range(lo, hi):
                if mid < 0:
                    continue   # sentinel for unused tile slots
                frame = mid - lo
                suffix = f"_f{frame:02d}" if (hi - lo > 1 and frame > 0) else ""
                tasks.append((t, mid, f"{tile_name(t)}{suffix}"))

    ok = skip = 0
    for tile_id, media_id, name in tasks:
        img = decode_media(media_id, media_dimensions, media_bounds,
                           pal_for_media, texels_for_media)
        if img is None:
            skip += 1
            continue

        out_path = output_dir / f"{name}.png"
        img.save(out_path)
        dim = media_dimensions[media_id]
        w = 1 << ((dim >> 4) & 0xF)
        h = 1 << (dim & 0xF)
        print(f"  {out_path.name:40s}  {w}×{h}  media={media_id}")
        ok += 1

    print(f"\nDone: {ok} saved, {skip} skipped (no data).")


if __name__ == "__main__":
    main()
