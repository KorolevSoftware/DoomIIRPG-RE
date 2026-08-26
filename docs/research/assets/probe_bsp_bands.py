#!/usr/bin/env python3
"""Research probe v2 (2026-08-26 walk flicker): replicate legacy
Render::getNodeForPoint (src/Render.cpp:2401-2442) over tmp_map00.bin with the
EXACT legacy field layout (src/LoadingManager.cpp:463-541):
  X bytes, Y bytes, MARKER, info-lo byte/sprite, MARKER, info-hi u16/sprite,
  MARKER, Z byte/zsprite, MARKER, info-mid byte/zsprite, MARKER.
"""
import struct

data = open("/Volumes/Goida/Project/DoomIIRPG-RE/tmp_map00.bin", "rb").read()
o = 0
def u8():
    global o; v = data[o]; o += 1; return v
def s8():
    global o; v = struct.unpack_from('<b', data, o)[0]; o += 1; return v
def u16():
    global o; v = struct.unpack_from('<H', data, o)[0]; o += 2; return v
def s16():
    global o; v = struct.unpack_from('<h', data, o)[0]; o += 2; return v
def i32():
    global o; v = struct.unpack_from('<i', data, o)[0]; o += 4; return v
def u32():
    global o; v = struct.unpack_from('<I', data, o)[0]; o += 4; return v
def mark():
    global o
    m = u32()
    assert m in (0xDEADBEEF, 0xCAFEBABE), hex(m)  # mapparser.py:86 CAFEBABE normals

version = s8(); compileDate = i32(); spawnIndex = u16(); spawnDir = u8()
flagsBitmask = s8(); totalSecrets = s8(); totalLoot = u8()
numNodes = u16(); dataSizePolys = u16(); numLines = u16(); numNormals = u16()
numNormalSprites = u16(); numZSprites = s16()
numTileEvents = s16(); mapByteCodeSize = s16()
totalMayaCameras = u8(); totalMayaCameraKeys = u16()
for l in range(6): s16()
mark()
mediaCount = u16()
media = [u16() for _ in range(mediaCount)]
mark()

normals = [s16() for _ in range(numNormals * 3)]
mark()
nodeOffsets = [u16() for _ in range(numNodes)]
mark()
nodeNormalIdxs = [u8() for _ in range(numNodes)]
mark()
child1 = [u16() for _ in range(numNodes)]
child2 = [u16() for _ in range(numNodes)]
mark()
nodeBounds = [u8() for _ in range(numNodes * 4)]
mark()
nodePolys = [u8() for _ in range(dataSizePolys)]
mark()
nlf = (numLines + 1) // 2
o += nlf + numLines * 2 * 2
mark()
heightMap = [u8() for _ in range(1024)]
mark()

numMapSprites = numNormalSprites + numZSprites
SX = [u8() * 8 for _ in range(numMapSprites)]   # shiftCoord byte*8 (Resource.cpp:154)
SY = [u8() * 8 for _ in range(numMapSprites)]
# NOTE: no marker here! LoadingManager.cpp:488-510 reads X, Y then info-lo
# back-to-back; the first marker after the coord arrays is at :512.
info_lo = [u8() for _ in range(numMapSprites)]  # LoadingManager.cpp:501-510
mark()
info_hi = [u16() for _ in range(numMapSprites)] # LoadingManager.cpp:515-524
mark()
SZ = [32] * numMapSprites                        # LoadingManager.cpp:498 default
for i in range(numZSprites):
    SZ[numNormalSprites + i] = u8()              # LoadingManager.cpp:527
mark()
info_mid = [u8() for _ in range(numZSprites)]    # LoadingManager.cpp:530-538
for i in range(numZSprites):
    idx = numNormalSprites + i
    info_lo[idx] |= info_mid[i] << 8

INFO = [(info_lo[i] | (info_hi[i] << 16)) & 0xFFFFFFFF for i in range(numMapSprites)]

def getHeight(x, y):   # Render.cpp:2444-2451
    x &= 0x7FF; y &= 0x7FF
    return heightMap[(y >> 6) * 32 + (x >> 6)] << 3

def classify(x, y, z, info):  # Render.cpp:2401-2442 verbatim
    n5 = 0
    i = nodeOffsets[n5]
    b = (info & 0xF000000) != 0
    n6 = info & 0xFF
    if (info & 0x400000) != 0:
        n6 += 256 + 1
    b2 = (n6 == 240)
    depth = 0
    while i != 0xFFFF:
        ni = (nodeNormalIdxs[n5] & 0xFF) * 3
        c = (((x * normals[ni]) + (y * normals[ni+1]) + (z * normals[ni+2])) >> 14) + nodeOffsets[n5]
        if c == 0 and b:
            n5 = child1[n5] if (info & 0x9000000) != 0 else child2[n5]
        else:
            if not b2 and -128 < c < 128:
                return ('INTERNAL', n5, c, depth)
            n5 = child1[n5] if c > 0 else child2[n5]
        i = nodeOffsets[n5]
        depth += 1
    x1 = (nodeBounds[(n5 << 2) + 0] & 0xFF) << 7
    y1 = (nodeBounds[(n5 << 2) + 1] & 0xFF) << 7
    x2 = (nodeBounds[(n5 << 2) + 2] & 0xFF) << 7
    y2 = (nodeBounds[(n5 << 2) + 3] & 0xFF) << 7
    if x < x1 or y < y1 or x > x2 or y > y2:
        return ('OOB', n5, None, depth)
    return ('LEAF', n5, None, depth)

leaves = sum(1 for n in range(numNodes) if nodeOffsets[n] == 0xFFFF)
print(f"nodes={numNodes} leaves={leaves} internal={numNodes-leaves} "
      f"sprites={numMapSprites} (normal={numNormalSprites} z={numZSprites})")

dbg = {7, 9, 10, 135, 150}
internal_list, oob_list = [], []
for i in range(numMapSprites):
    zb = SZ[i] + getHeight(SX[i], SY[i])       # postProcessSprites :2459-2467
    if i >= numNormalSprites:
        zb -= 32
    kind, node, c, depth = classify(SX[i] << 4, SY[i] << 4, zb << 4, INFO[i])
    tag = ' DBG' if i in dbg else ''
    if kind == 'LEAF':
        continue
    entry = (i, INFO[i] & 0xFF, INFO[i], SX[i], SY[i], kind, node, c, tag)
    (internal_list if kind == 'INTERNAL' else oob_list).append(entry)

print(f"\n=== INTERNAL-node attachments ({len(internal_list)}) -> never drawn by rewrite ===")
for e in sorted(internal_list):
    print(f" spr={e[0]:4d} tile={e[1]:3d} info=0x{e[2]:08x} pos=({e[3]:4d},{e[4]:4d}) "
          f"node={e[6]:3d} c={e[7]}{e[8]}")
print(f"=== out-of-bounds ({len(oob_list)}) -> also never drawn ===")
for e in sorted(oob_list)[:20]:
    print(f" spr={e[0]:4d} tile={e[1]:3d} info=0x{e[2]:08x} pos=({e[3]:4d},{e[4]:4d}) nearLeaf={e[6]}")

flip_sprites = []
for i in range(numMapSprites):
    if INFO[i] & 0x10000:
        continue
    zb = SZ[i] + getHeight(SX[i], SY[i])
    if i >= numNormalSprites:
        zb -= 32
    kinds = {}
    for dx in range(-64, 65, 16):
        for dy in range(-64, 65, 16):
            k, _, _, _ = classify((SX[i]+dx) << 4, (SY[i]+dy) << 4, zb << 4, INFO[i])
            kinds[k] = kinds.get(k, 0) + 1
    if len(kinds) > 1:
        flip_sprites.append((i, INFO[i] & 0xFF, SX[i], SY[i], kinds))
print(f"\n=== state FLIPS within +-64wu of load pos (hidden excluded): {len(flip_sprites)} ===")
for i, tn, sx, sy, kinds in flip_sprites:
    kk = ", ".join(f"{k}:{v}" for k, v in sorted(kinds.items()))
    print(f" spr={i:4d} tile={tn:3d} pos=({sx:4d},{sy:4d}) {kk}")
