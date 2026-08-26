#!/usr/bin/env python3
"""Scratch decoder for tmp_map00.bin (cinematic-jerk investigation 2026-08-26):
header, staticFuncs, tileEvents, Maya camera key tables. Layout follows
src/LoadingManager.cpp:362-397,540-556 + src/Game.cpp:586-640.
Anchors: staticFuncs@60651, tileEvents@60679, bytecode@62019 (docs/journal.md
2026-08-25 glass entry)."""
import struct, sys

P = "/Volumes/Goida/Project/DoomIIRPG-RE/tmp_map00.bin"
d = open(P, "rb").read()
print(f"file size {len(d)}")

ver = d[0]
(spawn_index,) = struct.unpack_from("<H", d, 5)
spawn_dir = d[7]
num_tile_events, bytecode_size = struct.unpack_from("<2h", d, 23)
total_cams = d[27]
(total_keys,) = struct.unpack_from("<H", d, 28)
print(f"spawn=({spawn_index%32},{spawn_index//32}) dir={spawn_dir} "
      f"tileEvents={num_tile_events} bcSize={bytecode_size} cams={total_cams} keys={total_keys}")

SF = 60651
TE = SF + 24 + 4
BC = 62019
print(f"d[SF-4:SF]={d[SF-4:SF].hex()} d[TE-4:TE]={d[TE-4:TE].hex()} d[BC-4:BC]={d[BC-4:BC].hex()}")
static_funcs = struct.unpack_from("<12H", d, SF)
print("staticFuncs:", list(static_funcs))

events = []
for i in range(num_tile_events):
    w0, w1 = struct.unpack_from("<ii", d, TE + i * 8)
    events.append((w0, w1))
print(f"\n== tileEvents ({num_tile_events}) ==  [w0: ip<<16|tile, w1: trigger mask]")
for i, (w0, w1) in enumerate(events):
    tile = w0 & 0x3FF
    x, y = tile % 32, tile // 32
    ip = (w0 >> 16) & 0xFFFF
    print(f"ev[{i:3d}] ({x:2d},{y:2d}) ip={ip:5d} mask=0x{w1:06X}")

off = BC + bytecode_size
off += 4  # unvalidated CAFEBABE marker
cams = []
chan_base = [0]*6
for ci in range(total_cams):
    num_keys = d[off]
    (sr,) = struct.unpack_from("<h", d, off+1)
    off += 3
    ck = []
    for j in range(7):
        col = []
        for k in range(num_keys):
            (v,) = struct.unpack_from("<h", d, off); off += 2
            col.append(v)
        ck.append(col)
    ti = []
    for l in range(num_keys * 6):
        (v,) = struct.unpack_from("<h", d, off); off += 2
        if v >= 0:
            v += chan_base[l % 6]
        ti.append(v)
    cc = struct.unpack_from("<6h", d, off); off += 12
    for n4 in range(6):
        chan_base[n4] += cc[n4]
    off += 4  # unvalidated marker
    tblob = d[off:off+sum(cc)]; off += sum(cc)
    cams.append((num_keys, sr, ck, ti, cc, tblob))


ANG = "N NE E SE S SW W NW".split()
def angname(a):
    if a < 0: return str(a)
    return f"{a}:{ANG[((a+128)&0x3FF)>>7]}"

for ci, (nk, sr, ck, ti, cc, tb) in enumerate(cams):
    total_ms = sum(k & 0xFFFF for k in ck[6])
    print(f"\n== camera {ci}: keys={nk} sampleRate={sr} totalMs={total_ms} tweenCounts={list(cc)}")
    for k in range(nk):
        x,y,z,p,yw,r,ms = (ck[c][k] for c in range(7))
        print(f"  key{k:2d} pos=({x:5d},{y:5d},{z:5d}) pitch={angname(p)} yaw={angname(yw)} roll={r:4d} ms={ms & 0xFFFF}")

OPS = {5:"STARTCINEMATIC",14:"WAIT",15:"GOTO",17:"ENTITY_FRAME",
       18:"ADV_CAMERAKEY",12:"CAMERA_STR",32:"FADEOP",23:"EVENTOP",
       21:"DOOROP",13:"DIALOG",34:"NAMEENTITY",7:"CALL_FUNC"}
SZ1 = {5:1,14:1,18:1,17:3,13:2,34:2}
SZ2 = {12:4,15:2,32:2,23:2,21:2,7:2}

bc = d[BC:BC+bytecode_size]
def dump(lo, hi, label=""):
    print(f"\n== bytecode {label} [{lo}..{hi}] ==")
    ip = lo
    while ip <= hi:
        op = bc[ip]
        name = OPS.get(op)
        nip = ip + 1
        if op == 4:
            print(f"{ip:5d}: EVAL raw={bc[ip:ip+9].hex()}")
            ip += 1
            continue
        if name is None:
            print(f"{ip:5d}: op{op} raw={bc[ip:ip+10].hex()}")
            ip += 1
            continue
        args = ""
        if op in SZ1:
            vals = list(bc[nip:nip+SZ1[op]]); nip += SZ1[op]
            args = ",".join(str(a) for a in vals)
        elif op in SZ2:
            n = SZ2[op]
            val = int.from_bytes(bc[nip:nip+n], "big"); nip += n
            args = str(val)
            if op == 15:
                y = val & 0x1F; x = (val >> 5) & 0x1F
                face = (val >> 10) & 0xF; anim = (val >> 14) & 1; adv = (val >> 15) & 1
                args = f"-> tile({x},{y}) face={face} anim={anim} advTurn={adv}"
            elif op == 32:
                args = ("OUT " if val & 0x8000 else "IN ") + str(val & 0x7FFF) + "ms"
        print(f"{ip:5d}: {name} {args}")
        ip = nip

if len(sys.argv) > 2:
    dump(int(sys.argv[1]), int(sys.argv[2]), sys.argv[3] if len(sys.argv) > 3 else "")
