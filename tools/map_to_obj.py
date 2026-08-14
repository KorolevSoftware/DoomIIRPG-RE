#!/usr/bin/env python3
"""
Doom II RPG map binary parser.
Converts mapXX.bin to OBJ+MTL geometry and a JSON metadata file.

Usage:
    python map_to_obj.py map00.bin [output_dir]

Coordinate system (from source):
    Vertex byte value V → world unit = V * 128
    OBJ output is in world units, Y-up (X=game-X, Y=game-Z, Z=game-Y).
"""

import json
import os
import re
import struct
import sys
from dataclasses import dataclass, field
from pickletools import read_int4
from typing import List, Optional, Tuple

# ──────────────────────────────────────────────
# Texture ID → name  (from Enums.h TILENUM_*)
# ──────────────────────────────────────────────

_TILENUM_NAMES: dict = {
    # View weapons
    1: "assault_rifle",
    2: "chainsaw",
    3: "holy_water_pistol",
    4: "sentry_bot_shooting",
    5: "sentry_bot_exploding",
    6: "super_shotgun",
    7: "chaingun",
    8: "assault_rifle_scope",
    9: "plasma_gun",
    10: "rocket_launcher",
    11: "bfg",
    12: "soul_cube",
    13: "sentry_bot_red_shooting",
    14: "sentry_bot_red_exploding",
    15: "world_weapon",
    # Monsters
    18: "monster_red_sentry_bot",
    19: "monster_sentry_bot",
    20: "monster_zombie",
    21: "monster_zombie2",
    22: "monster_zombie3",
    23: "monster_imp",
    24: "monster_imp2",
    25: "monster_imp3",
    26: "monster_saw_goblin",
    27: "monster_saw_goblin2",
    28: "monster_saw_goblin3",
    29: "monster_lost_soul",
    30: "monster_lost_soul2",
    31: "monster_lost_soul3",
    32: "monster_pinky",
    33: "monster_pinky2",
    34: "monster_pinky3",
    35: "monster_revenant",
    36: "monster_revenant2",
    37: "monster_revenant3",
    38: "monster_mancubus",
    39: "monster_mancubus2",
    40: "monster_mancubus3",
    41: "monster_cacodemon",
    42: "monster_cacodemon2",
    43: "monster_cacodemon3",
    44: "monster_sentinel",
    45: "monster_sentinel2",
    46: "monster_sentinel3",
    50: "monster_arch_vile",
    51: "monster_arch_vile2",
    52: "monster_arch_vile3",
    53: "monster_arachnotron",
    54: "boss_cyberdemon",
    56: "boss_pinky",
    57: "boss_mastermind",
    58: "boss_vios",
    59: "boss_vios2",
    60: "boss_vios3",
    61: "boss_vios4",
    62: "boss_vios5",
    # NPCs
    66: "npc_riley_oconnor",
    68: "npc_major",
    69: "npc_bob",
    71: "npc_civilian",
    72: "npc_sarge",
    73: "npc_female",
    74: "npc_evil_scientist",
    75: "npc_scientist",
    76: "npc_researcher",
    77: "npc_civilian2",
    # Items & props
    85: "ammo_bullets",
    86: "ammo_shells",
    88: "ammo_rockets",
    89: "ammo_cells",
    90: "ammo_holy_water",
    107: "one_uac_credit",
    110: "key_red",
    111: "key_blue",
    112: "armor_jacket",
    113: "satchel",
    114: "pack_item",
    115: "worker_pack",
    116: "health_pack",
    117: "food_plate",
    119: "armor_shard",
    121: "obj_table",
    122: "hell_seal",
    123: "toilet",
    124: "dirt_decal",
    125: "ladder",
    126: "blood_splatter",
    127: "sink",
    128: "barred_window",
    129: "hazard_bar",
    130: "obj_fire",
    131: "treadmill_monitor",
    133: "tech_station",
    134: "water_spout",
    135: "obj_chair",
    136: "obj_torchiere",
    137: "obj_scientist_corpse",
    138: "obj_corpse",
    139: "obj_other_corpse",
    140: "dummy_pain",
    141: "attack_dummy",
    142: "exit_dummy",
    143: "use_dummy",
    147: "septic_station",
    149: "practice_target",
    150: "obj_printer",
    152: "obj_crate",
    153: "vending_machine",
    154: "armor_repair",
    155: "closed_portal_eye",
    156: "eye_portal",
    157: "portal_socket",
    158: "treadmill_side",
    159: "treadmill_front",
    161: "hell_hands",
    162: "doorjamb_decal",
    164: "stones_and_skulls",
    166: "nonobstructing_spritewall",
    168: "fence",
    170: "sentinel_spikes",
    171: "sentinel_spikes_dummy",
    173: "switch",
    175: "tech_detail",
    177: "hell_skulls",
    178: "glass",
    179: "terminal_target",
    180: "terminal_general",
    181: "terminal_vios",
    182: "terminal_bot",
    183: "terminal_hacking",
    184: "elevator_nums",
    187: "bush",
    188: "tree_top",
    189: "tree_trunk",
    193: "sfx_lightglow",
    197: "window",
    201: "glaevenscope",
    208: "fog_gray",
    212: "scorch_mark",
    223: "static_flame",
    # Projectiles / effects
    225: "missile_player_rocket",
    226: "missile_rocket",
    227: "flesh",
    232: "shadow",
    234: "anim_fire",
    235: "anim_water",
    236: "air_vent",
    239: "soul_cube_attack",
    240: "water_stream",
    241: "caco_plasma",
    242: "fire_ball",
    243: "plasma_ball",
    244: "bfg_ball",
    245: "monster_claw",
    246: "monster_bite",
    247: "monster_blunt_trauma",
    248: "electric_slide",
    251: "fear_eye",
    252: "acid_spit",
    254: "npc_chat",
    255: "alert",
    # Walls & doors
    257: "doorjamb",
    271: "red_door_locked",
    272: "red_door_unlocked",
    273: "blue_door_locked",
    274: "blue_door_unlocked",
    275: "door_locked",
    276: "door_unlocked",
    277: "level_door_locked",
    278: "level_door_unlocked",
    301: "sky_box",
    302: "fade",
    # Flats
    479: "flat_lava",
    480: "flat_lava2",
}


def tile_name(tile_id: int) -> str:
    """Return a human-readable name for a TILENUM texture ID."""
    if tile_id in _TILENUM_NAMES:
        return _TILENUM_NAMES[tile_id]
    # Range-based fallback
    if 1 <= tile_id <= 14:
        return f"viewweapon_{tile_id}"
    if 18 <= tile_id <= 63:
        return f"monster_{tile_id}"
    if 65 <= tile_id <= 80:
        return f"npc_{tile_id}"
    if 81 <= tile_id <= 212:
        return f"item_{tile_id}"
    if 225 <= tile_id <= 256:
        return f"sprite_{tile_id}"
    if 257 <= tile_id <= 449:
        return f"wall_{tile_id}"
    if 450 <= tile_id <= 512:
        return f"flat_{tile_id}"
    return f"tex_{tile_id}"


# ──────────────────────────────────────────────
# Binary reader
# ──────────────────────────────────────────────


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def skip(self, n: int):
        self.pos += n

    def read_bytes(self, n: int) -> bytes:
        chunk = self.data[self.pos : self.pos + n]
        self.pos += n
        return chunk

    def u8(self) -> int:
        v = self.data[self.pos]
        self.pos += 1
        return v

    def i8(self) -> int:
        v = struct.unpack_from("<b", self.data, self.pos)[0]
        self.pos += 1
        return v

    def u16(self) -> int:
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def i16(self) -> int:
        v = struct.unpack_from("<h", self.data, self.pos)[0]
        self.pos += 2
        return v

    def i32(self) -> int:
        v = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def u32(self) -> int:
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def coord(self) -> int:
        """readCoordArray: byte * 8"""
        return self.u8() * 8

    def marker(self, expected: Optional[int] = None):
        """readMarker: consume 4 bytes (value not validated in source either)"""
        v = self.u32()
        return v

    def short_array(self, count: int) -> List[int]:
        return [self.i16() for _ in range(count)]

    def ushort_array(self, count: int) -> List[int]:
        return [self.u16() for _ in range(count)]

    def byte_array(self, count: int) -> List[int]:
        return [self.u8() for _ in range(count)]

    def int_array(self, count: int) -> List[int]:
        return [self.i32() for _ in range(count)]

    def coord_array(self, count: int) -> List[int]:
        return [self.coord() for _ in range(count)]


# ──────────────────────────────────────────────
# Data classes
# ──────────────────────────────────────────────


@dataclass
class MapHeader:
    version: int
    compile_date: int
    spawn_index: int
    spawn_dir: int
    flags_bitmask: int
    total_secrets: int
    total_loot: int
    num_nodes: int
    data_size_polys: int
    num_lines: int
    num_normals: int
    num_normal_sprites: int
    num_z_sprites: int
    num_tile_events: int
    map_bytecode_size: int
    total_maya_cameras: int
    total_maya_camera_keys: int
    maya_tween_offsets: List[int]


@dataclass
class Vertex:
    x: float
    y: float
    z: float
    s: float
    t: float


@dataclass
class Polygon:
    verts: List[Vertex]
    texture_id: int
    flags: int


@dataclass
class Sprite:
    x: int
    y: int
    z: int
    info: int

    @property
    def sprite_type(self) -> int:
        return self.info & 0xFF

    @property
    def extra_flags(self) -> int:
        return (self.info >> 8) & 0xFF

    @property
    def params(self) -> int:
        return (self.info >> 16) & 0xFFFF


@dataclass
class TileEvent:
    tile_index: int
    raw_data: int


@dataclass
class LineSegment:
    x0: int
    y0: int
    x1: int
    y1: int
    flags: int


@dataclass
class MapData:
    header: MapHeader
    media_indices: List[int]
    normals: List[Tuple[int, int, int]]
    height_map: List[int]  # 32×32
    polygons: List[Polygon]
    line_segments: List[LineSegment]
    sprites: List[Sprite]
    tile_events: List[TileEvent]
    tile_flags: List[int]  # 1024
    static_funcs: List[int]
    node_bounds: List[Tuple[int, int, int, int]]  # minX,maxX,minY,maxY per node
    bytecode: bytes


# ──────────────────────────────────────────────
# Parsing
# ──────────────────────────────────────────────

POLY_FLAG_VERTS_MASK = 7
POLY_FLAG_AXIS_MASK = 24
POLY_FLAG_AXIS_X = 0
POLY_FLAG_AXIS_Y = 8
POLY_FLAG_AXIS_Z = 16
POLY_FLAG_SWAPXY = 64
POLY_FLAG_UV_DELTAX = 128


def expand_edge_poly(verts: List[Vertex], poly_flags: int) -> List[Vertex]:
    """
    The game expands 2-vertex 'edge' polys into a quad.
    Mirrors Render.cpp lines 1486-1530.
    """
    v0, v1 = verts[0], verts[1]
    v2 = Vertex(v0.x, v0.y, v0.z, v0.s, v0.t)
    v3 = Vertex(v2.x, v2.y, v2.z, v2.s, v2.t)
    axis = poly_flags & POLY_FLAG_AXIS_MASK
    uv_deltax = poly_flags & POLY_FLAG_UV_DELTAX

    if axis == POLY_FLAG_AXIS_X:
        v1 = Vertex(v2.x, v1.y, v1.z, v1.s, v1.t)
        v3 = Vertex(v0.x, v3.y, v3.z, v3.s, v3.t)
    elif axis == POLY_FLAG_AXIS_Y:
        v1 = Vertex(v1.x, v2.y, v1.z, v1.s, v1.t)
        v3 = Vertex(v3.x, v0.y, v3.z, v3.s, v3.t)
    elif axis == POLY_FLAG_AXIS_Z:
        v1 = Vertex(v1.x, v1.y, v2.z, v1.s, v1.t)
        v3 = Vertex(v3.x, v3.y, v0.z, v3.s, v3.t)

    if uv_deltax:
        v1 = Vertex(v1.x, v1.y, v1.z, v2.s, v1.t)
        v3 = Vertex(v3.x, v3.y, v3.z, v0.s, v3.t)
    else:
        v1 = Vertex(v1.x, v1.y, v1.z, v1.s, v2.t)
        v3 = Vertex(v3.x, v3.y, v3.z, v3.s, v0.t)

    return [v0, v1, v2, v3]


MAX_MESH_COUNT = 64  # sanity cap: meshes per node
MAX_POLY_COUNT = 127  # max polys per mesh (7 bits in packed uint16)
MAX_VERT_COUNT = 9  # (POLY_FLAG_VERTS_MASK=7) + 2


def parse_node_polys(
    raw: bytes, num_nodes: int, node_offsets: List[int], node_child_offset1: List[int]
) -> List[Polygon]:
    """
    Extract polygon data only from LEAF nodes (nodeOffsets[n] == 0xFFFF).
    Internal BSP nodes use nodeChildOffset1 for child references, not poly data.
    """
    polygons: List[Polygon] = []
    visited_offsets: set = set()

    for n in range(num_nodes):
        # Only leaf nodes carry geometry in nodePolys
        if (node_offsets[n] & 0xFFFF) != 0xFFFF:
            continue

        offset = node_child_offset1[n] & 0xFFFF
        if offset in visited_offsets or offset >= len(raw):
            continue
        visited_offsets.add(offset)

        mesh_count = raw[offset]
        offset += 1

        if mesh_count > MAX_MESH_COUNT:
            continue  # corrupt / non-polygon data

        for _ in range(mesh_count):
            if offset + 6 > len(raw):
                break
            # bytes 0-3: unknown mesh header (bounds/flags, not used for export)
            # bytes 4-5: packed uint16 → bits 0-6 = polyCount, bits 7-15 = textureID
            packed = raw[offset + 4] | (raw[offset + 5] << 8)
            offset += 6

            texture_id = packed >> 7
            poly_count = packed & 0x7F

            if poly_count == 0 or poly_count > MAX_POLY_COUNT:
                continue

            for _ in range(poly_count):
                if offset >= len(raw):
                    break
                poly_flags = raw[offset]
                offset += 1
                num_verts = (poly_flags & POLY_FLAG_VERTS_MASK) + 2

                if num_verts > MAX_VERT_COUNT:
                    break  # corrupt record

                verts: List[Vertex] = []
                for _ in range(num_verts):
                    if offset + 5 > len(raw):
                        break
                    x = (raw[offset + 0] & 0xFF) << 7
                    y = (raw[offset + 1] & 0xFF) << 7
                    z = (raw[offset + 2] & 0xFF) << 7
                    s = struct.unpack_from("<b", raw, offset + 3)[0] << 6
                    t = struct.unpack_from("<b", raw, offset + 4)[0] << 6
                    offset += 5
                    verts.append(Vertex(x, y, z, s, t))

                if len(verts) != num_verts:
                    break  # truncated

                if num_verts == 2:
                    verts = expand_edge_poly(verts, poly_flags)

                if verts:
                    polygons.append(Polygon(verts, texture_id, poly_flags))

    return polygons


def parse_map(data: bytes) -> MapData:
    r = Reader(data)

    # ── Header (42 bytes) ──────────────────────────────
    version = r.u8()
    compile_date = r.i32()
    spawn_index = r.u16()
    spawn_dir = r.u8()
    flags_bitmask = r.i8()
    total_secrets = r.i8()
    total_loot = r.u8()
    num_nodes = r.u16()
    data_size_polys = r.u16()
    num_lines = r.u16()
    num_normals = r.u16()
    num_normal_sprites = r.u16()
    num_z_sprites = r.i16()
    num_tile_events = r.i16()
    map_bytecode_size = r.i16()
    total_maya_cameras = r.i8()
    total_maya_camera_keys = r.i16()
    maya_tween_offsets = [r.i16() for _ in range(6)]

    header = MapHeader(
        version,
        compile_date,
        spawn_index,
        spawn_dir,
        flags_bitmask,
        total_secrets,
        total_loot,
        num_nodes,
        data_size_polys,
        num_lines,
        num_normals,
        num_normal_sprites,
        num_z_sprites,
        num_tile_events,
        map_bytecode_size,
        total_maya_cameras,
        total_maya_camera_keys,
        maya_tween_offsets,
    )

    num_map_sprites = num_normal_sprites + max(0, num_z_sprites)

    if version != 3:
        raise ValueError(f"Unsupported map version {version}, expected 3")

    # ── Media section ──────────────────────────────────
    r.marker()  # 0xDEADBEEF
    media_count = r.u16()
    print("media_count", media_count)
    media_indices = r.ushort_array(media_count)
    r.marker()  # 0xDEADBEEF

    # ── Normals ───────────────────────────────────────
    raw_normals = r.short_array(num_normals * 3)
    normals = [
        (raw_normals[i * 3], raw_normals[i * 3 + 1], raw_normals[i * 3 + 2])
        for i in range(num_normals)
    ]
    r.marker()  # 0xCAFEBABE

    # ── BSP nodes ─────────────────────────────────────
    node_offsets = r.short_array(num_nodes)
    r.marker()
    node_normal_idxs = r.byte_array(num_nodes)
    r.marker()
    node_child_offset1 = r.short_array(num_nodes)
    node_child_offset2 = r.short_array(num_nodes)
    r.marker()
    raw_bounds = r.byte_array(num_nodes * 4)
    node_bounds = [
        (
            raw_bounds[i * 4],
            raw_bounds[i * 4 + 1],
            raw_bounds[i * 4 + 2],
            raw_bounds[i * 4 + 3],
        )
        for i in range(num_nodes)
    ]
    r.marker()

    # ── Polygon data ──────────────────────────────────
    node_polys_raw = bytes(r.byte_array(data_size_polys))
    r.marker()

    # ── Line segments ─────────────────────────────────
    line_flags_raw = r.byte_array((num_lines + 1) // 2)
    line_xs = r.byte_array(num_lines * 2)
    line_ys = r.byte_array(num_lines * 2)
    r.marker()

    line_segments: List[LineSegment] = []
    for i in range(num_lines):
        nibble_byte = line_flags_raw[i // 2]
        flag = nibble_byte & 0xF if (i % 2 == 0) else (nibble_byte >> 4) & 0xF
        x0 = (line_xs[i * 2 + 0] & 0xFF) << 7
        x1 = (line_xs[i * 2 + 1] & 0xFF) << 7
        y0 = (line_ys[i * 2 + 0] & 0xFF) << 7
        y1 = (line_ys[i * 2 + 1] & 0xFF) << 7
        line_segments.append(LineSegment(x0, y0, x1, y1, flag))

    # ── Height map (32×32) ────────────────────────────
    height_map = r.byte_array(1024)
    r.marker()

    # ── Sprites ───────────────────────────────────────
    sprite_x = r.coord_array(num_map_sprites)
    sprite_y = r.coord_array(num_map_sprites)

    sprite_info = [r.u8() for _ in range(num_map_sprites)]  # low 8 bits
    r.marker()

    for i in range(num_map_sprites):
        sprite_info[i] |= (r.u16() & 0xFFFF) << 16  # high 16 bits
    r.marker()

    # Z-sprite Z coords (only for z-sprites)
    z_sprite_z = [r.u8() for _ in range(num_z_sprites)] if num_z_sprites > 0 else []
    r.marker()

    # Z-sprite extra flags (bits 8-15 of info)
    for i in range(num_z_sprites):
        sprite_info[num_normal_sprites + i] |= r.u8() << 8
    r.marker()

    sprites: List[Sprite] = []
    for i in range(num_map_sprites):
        z = z_sprite_z[i - num_normal_sprites] if i >= num_normal_sprites else 32
        sprites.append(Sprite(sprite_x[i], sprite_y[i], z, sprite_info[i]))

    # ── Static funcs ──────────────────────────────────
    static_funcs = r.ushort_array(12)
    r.marker()

    # ── Tile events ───────────────────────────────────
    raw_events = r.int_array(num_tile_events * 2)
    tile_events = [
        TileEvent(raw_events[i * 2] & 0x3FF, raw_events[i * 2])
        for i in range(num_tile_events)
    ]
    r.marker()

    # ── Bytecode ──────────────────────────────────────
    bytecode = bytes(r.byte_array(map_bytecode_size))
    r.marker()

    # ── Maya cameras ──────────────────────────────────
    # Per camera (mirrors Game::loadMayaCameras):
    #   1 byte numKeys + 2 bytes sampleRate
    #   numKeys * 7 * 2  bytes keyframes (7 channels)
    #   numKeys * 6 * 2  bytes tween indices (6 channels)
    #   6 * 2            bytes array2[6] (per-channel tween counts)
    #   4 bytes          DEADBEEF marker
    #   sum(array2) bytes tween bytes
    for _ in range(total_maya_cameras):
        num_keys = r.u8()
        r.i16()  # sampleRate
        r.read_bytes(num_keys * 7 * 2)  # keyframes
        r.read_bytes(num_keys * 6 * 2)  # tween indices
        n3 = sum(max(0, r.i16()) for _ in range(6))
        r.marker()  # DEADBEEF
        r.read_bytes(n3)  # tween bytes
    r.marker()  # CAFEBABE

    # ── Tile flags (32×32 grid, 2 nibbles per byte) ───
    packed_flags = r.byte_array(512)
    tile_flags = []
    for b in packed_flags:
        tile_flags.append(b & 0xF)
        tile_flags.append((b >> 4) & 0xF)
    r.marker()

    # ── Parse polygon geometry ─────────────────────────
    polygons = parse_node_polys(
        node_polys_raw, num_nodes, node_offsets, node_child_offset1
    )

    return MapData(
        header=header,
        media_indices=media_indices,
        normals=normals,
        height_map=height_map,
        polygons=polygons,
        line_segments=line_segments,
        sprites=sprites,
        tile_events=tile_events,
        tile_flags=tile_flags,
        static_funcs=static_funcs,
        node_bounds=node_bounds,
        bytecode=bytecode,
    )


# ──────────────────────────────────────────────
# OBJ / MTL export
# ──────────────────────────────────────────────

# Coordinate system (from source analysis):
#   Vertex raw  = uint8 << 7  (world int units, no fractional bits)
#   1 tile      = 64 world units  (canvas: viewX >> 6 = tile_index)
#   1 byte step = 128 world units = 2 tiles
#   → WORLD_SCALE = 1/64  gives OBJ coords in tile-space (1 OBJ unit = 1 tile)
#
# UV (texture):
#   raw = int8 << 6  (range −8192..8128)
#   Game rasteriser uses 2.14 fixed-point; 1 full texture repeat ≈ 1024 units
#   → UV_SCALE = 1/1024  maps one texture tile to [0..1]
#
# Winding: game renders CW front-faces (CULL_CCW culls back-faces).
# OBJ standard expects CCW front-faces → we reverse vertex order on export.

WORLD_SCALE = 1.0 / 64.0  # 1 OBJ unit = 1 game tile
UV_SCALE = 1.0 / 1024.0  # 1 full texture = 1.0 UV units


def to_obj_coords(x: float, y: float, z: float):
    """
    Game axes: X = East, Y = North, Z = Up.
    OBJ convention: Y-up.
    Mapping: OBJ X = game X,  OBJ Y = game Z,  OBJ Z = −game Y
    """
    return x * WORLD_SCALE, z * WORLD_SCALE, -y * WORLD_SCALE


def write_obj(map_data: MapData, obj_path: str, mtl_name: str):
    texture_ids = sorted({p.texture_id for p in map_data.polygons})

    # Build per-texture name cache used for both OBJ groups and MTL
    tex_name_cache = {tid: tile_name(tid) for tid in texture_ids}

    lines = [
        f"# Doom II RPG map – {len(map_data.polygons)} polygons",
        f"mtllib {mtl_name}",
        "",
    ]

    # Collect all unique vertices (global list)
    vert_index: dict = {}  # (x,y,z,s,t) → 1-based OBJ index
    verts_v: List[str] = []
    verts_vt: List[str] = []

    def get_vert_idx(v: Vertex) -> Tuple[int, int]:
        key_v = (v.x, v.y, v.z)
        key_vt = (v.s, v.t)
        if key_v not in vert_index:
            vert_index[key_v] = len(verts_v) + 1
            ox, oy, oz = to_obj_coords(v.x, v.y, v.z)
            verts_v.append(f"v {ox:.4f} {oy:.4f} {oz:.4f}")
        if key_vt not in vert_index:
            vert_index[key_vt] = len(verts_vt) + 1
            u = v.s * UV_SCALE
            t = 1.0 - v.t * UV_SCALE  # flip V (OBJ convention)
            verts_vt.append(f"vt {u:.6f} {t:.6f}")
        return vert_index[key_v], vert_index[key_vt]

    # Build face strings grouped by texture
    faces_by_tex: dict = {tid: [] for tid in texture_ids}

    for poly in map_data.polygons:
        idxs = [get_vert_idx(v) for v in poly.verts]
        # Reverse CW → CCW (game uses CW front-faces, OBJ standard is CCW)
        face_str = "f " + " ".join(f"{vi}/{ti}" for vi, ti in reversed(idxs))
        faces_by_tex[poly.texture_id].append(face_str)

    lines += verts_v
    lines.append("")
    lines += verts_vt
    lines.append("")

    for tid in texture_ids:
        name = tex_name_cache[tid]
        lines.append(f"usemtl {name}")
        lines.append(f"g {name}")
        lines += faces_by_tex[tid]
        lines.append("")

    with open(obj_path, "w") as f:
        f.write("\n".join(lines))

    print(
        f"  OBJ  → {obj_path}  ({len(verts_v)} verts, {len(map_data.polygons)} polys)"
    )


def write_mtl(map_data: MapData, mtl_path: str):
    texture_ids = sorted({p.texture_id for p in map_data.polygons})
    lines = ["# Doom II RPG materials"]
    for tid in texture_ids:
        name = tile_name(tid)
        lines += [
            f"\nnewmtl {name}",
            "Ka 1.0 1.0 1.0",
            "Kd 1.0 1.0 1.0",
            "illum 1",
            f"map_Kd {name}.png",
        ]
    with open(mtl_path, "w") as f:
        f.write("\n".join(lines))
    print(f"  MTL  → {mtl_path}  ({len(texture_ids)} materials)")


# ──────────────────────────────────────────────
# JSON export
# ──────────────────────────────────────────────


def map_to_json(map_data: MapData) -> dict:
    h = map_data.header
    return {
        "header": {
            "version": h.version,
            "compile_date": h.compile_date,
            "spawn_index": h.spawn_index,
            "spawn_dir": h.spawn_dir,
            "flags_bitmask": h.flags_bitmask,
            "total_secrets": h.total_secrets,
            "total_loot": h.total_loot,
            "num_nodes": h.num_nodes,
            "num_lines": h.num_lines,
            "num_normals": h.num_normals,
            "num_normal_sprites": h.num_normal_sprites,
            "num_z_sprites": h.num_z_sprites,
            "num_tile_events": h.num_tile_events,
            "map_bytecode_size": h.map_bytecode_size,
            "total_maya_cameras": h.total_maya_cameras,
            "total_maya_camera_keys": h.total_maya_camera_keys,
            "maya_tween_offsets": h.maya_tween_offsets,
        },
        "media_indices": map_data.media_indices,
        "static_funcs": map_data.static_funcs,
        "height_map": {
            "width": 32,
            "height": 32,
            "data": map_data.height_map,
        },
        "tile_flags": {
            "width": 32,
            "height": 32,
            "data": map_data.tile_flags,
        },
        "normals": [{"x": n[0], "y": n[1], "z": n[2]} for n in map_data.normals],
        "line_segments": [
            {"x0": ls.x0, "y0": ls.y0, "x1": ls.x1, "y1": ls.y1, "flags": ls.flags}
            for ls in map_data.line_segments
        ],
        "sprites": [
            {
                "x": s.x,
                "y": s.y,
                "z": s.z,
                "sprite_type": s.sprite_type,
                "extra_flags": s.extra_flags,
                "params": s.params,
                "raw_info": s.info,
            }
            for s in map_data.sprites
        ],
        "tile_events": [
            {"tile_index": te.tile_index, "raw": te.raw_data}
            for te in map_data.tile_events
        ],
        "geometry_summary": {
            "total_polygons": len(map_data.polygons),
            "unique_textures": [
                {"id": tid, "name": tile_name(tid)}
                for tid in sorted({p.texture_id for p in map_data.polygons})
            ],
        },
        "bytecode_hex": map_data.bytecode.hex(),
    }


# ──────────────────────────────────────────────
# Sky box extraction (from tables.bin)
# ──────────────────────────────────────────────
#
# Sky texture is NOT in newTexels*.bin — it lives in tables.bin.
# Table layout (Resource.cpp::loadUShortTable / loadUByteTable):
#   80-byte header: 20 × int32 cumulative end-offsets of each table (from after header)
#   Then sequentially: [int32 count | count × element] per table
#
# Sky assignment (Render.cpp::beginLoadMap):
#   skyIndex = ((mapNameID - 1) // 5 % 2) * 2
#   palette  = table skyIndex + 16  →  256 uint16 RGB565 entries
#   texels   = table skyIndex + 17  →  256×256 raw palette indices
#   Maps 1-5  → skyIndex=0 (tables 16, 17)
#   Maps 6-10 → skyIndex=2 (tables 18, 19)


def _read_table(data: bytes, offsets: list, index: int):
    """Return (count, raw_bytes) for the table at `index` in tables.bin."""
    start = 80 + (offsets[index - 1] if index > 0 else 0)
    count = struct.unpack_from("<i", data, start)[0]
    return count, data[start + 4 : 80 + offsets[index]]


def extract_sky_texture(tables_path: str, map_name_id: int, out_dir: str):
    """
    Decode and save sky_box.png from tables.bin.
    map_name_id: 1 = map00, 10 = map09.
    """
    try:
        from PIL import Image as PILImage
    except ImportError:
        print("  SKY  skipped — pip install Pillow")
        return

    if not os.path.exists(tables_path):
        print(f"  SKY  skipped — tables.bin not found at {tables_path}")
        return

    with open(tables_path, "rb") as f:
        tbl = f.read()

    offsets = list(struct.unpack_from("<20i", tbl, 0))

    sky_index = ((map_name_id - 1) // 5 % 2) * 2
    pal_idx = sky_index + 16
    tex_idx = sky_index + 17

    # Palette: `count` = number of uint16 RGB565 entries
    pal_count, pal_raw = _read_table(tbl, offsets, pal_idx)
    palette = struct.unpack_from(f"<{pal_count}H", pal_raw, 0)

    # RGB565 → RGBA8888 LUT (same conversion as extract_textures.py)
    lut = []
    for rgb565 in palette:
        r5 = (rgb565 >> 11) & 0x1F
        g6 = (rgb565 >> 5) & 0x3F
        b5 = rgb565 & 0x1F
        r8 = (r5 << 3) | (r5 >> 2)
        g8 = (g6 << 2) | (g6 >> 4)
        b8 = (b5 << 3) | (b5 >> 2)
        a8 = 0 if (r8 >= 250 and g8 == 0 and b8 >= 250) else 255
        lut.append((r8, g8, b8, a8))
    while len(lut) < 256:
        lut.append((0, 0, 0, 0))

    # Texels: `count` = 65536 raw palette indices for 256×256 image
    _, tex_raw = _read_table(tbl, offsets, tex_idx)
    area = 256 * 256
    img_data = bytearray(area * 4)
    for i, idx in enumerate(tex_raw[:area]):
        r, g, b, a = lut[idx]
        img_data[i * 4 : i * 4 + 4] = bytes([r, g, b, a])

    img = PILImage.frombytes("RGBA", (256, 256), bytes(img_data))
    out_path = os.path.join(out_dir, "sky_box.png")
    img.save(out_path)
    print(
        f"  SKY  → {out_path}  (sky_index={sky_index}, maps {'1-5' if sky_index == 0 else '6-10'})"
    )


# ──────────────────────────────────────────────
# Entry point
# ──────────────────────────────────────────────


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    map_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(map_path) or "."
    os.makedirs(out_dir, exist_ok=True)

    base_name = os.path.splitext(os.path.basename(map_path))[0]
    obj_path = os.path.join(out_dir, f"{base_name}.obj")
    mtl_name = f"{base_name}.mtl"
    mtl_path = os.path.join(out_dir, mtl_name)
    json_path = os.path.join(out_dir, f"{base_name}.json")

    print(f"Parsing {map_path} …")
    with open(map_path, "rb") as f:
        raw = f.read()

    # Try gzip decompression (some builds compress map files)
    import gzip
    import io

    try:
        raw = gzip.decompress(raw)
        print("  (gzip-compressed)")
    except (gzip.BadGzipFile, OSError):
        pass

    map_data = parse_map(raw)
    h = map_data.header
    print(
        f"  version={h.version}  nodes={h.num_nodes}  "
        f"sprites={h.num_normal_sprites}+{h.num_z_sprites}z  "
        f"lines={h.num_lines}  secrets={h.total_secrets}"
    )

    write_obj(map_data, obj_path, mtl_name)
    write_mtl(map_data, mtl_path)

    # Sky box — map00 → mapNameID=1, map09 → mapNameID=10
    m = re.search(r"map(\d+)", base_name, re.IGNORECASE)
    map_name_id = int(m.group(1)) + 1 if m else 1
    tables_path = os.path.join(os.path.dirname(os.path.abspath(map_path)), "tables.bin")
    extract_sky_texture(tables_path, map_name_id, out_dir)

    with open(json_path, "w") as f:
        json.dump(map_to_json(map_data), f, indent=2)
    print(f"  JSON → {json_path}")


if __name__ == "__main__":
    main()
