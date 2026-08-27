#!/usr/bin/env python3
"""Probe: Maya camera key/tween data for the map00 lift-ride cinematics
(cameras 11 = ride down, 12 = ride up), 2026-08-26.

Layout follows src/LoadingManager.cpp:385-397 (header counts + per-channel
tween counts -> ofsMayaTween) and src/Game.cpp:586-640 (loadMayaCameras).
Shorts are LITTLE-endian (src/Resource.cpp:136-141).
Also replays MayaCamera::Update (src/MayaCamera.cpp:46-148) for one camera to
print the exact per-frame z(t) curve the original produces.
"""
import struct, sys

P = "/Volumes/Goida/Project/DoomIIRPG-RE/tmp_map00.bin"
d = open(P, "rb").read()
num_tile_events, bytecode_size = struct.unpack_from("<2h", d, 23)
total_cams = d[27]
(total_keys,) = struct.unpack_from("<H", d, 28)
# per-channel tween counts (header) -> ofsMayaTween
ofs_tween = []
acc = 0
for l in range(6):
    ofs_tween.append(acc)
    (v,) = struct.unpack_from("<h", d, 30 + 2 * l)
    if v != -1:
        acc += v
total_tweens = acc

BC = 62019
off = BC + bytecode_size + 4  # CAFEBABE marker
cams = []
chan_base = [0] * 6          # running per-channel base (Game.cpp:611-616 / :636)
tween_arr = [0] * total_tweens
key_arr = [[0] * total_keys for _ in range(7)]
key_off = 0
tw_idx = []
for ci in range(total_cams):
    num_keys = d[off]
    (sr,) = struct.unpack_from("<h", d, off + 1)
    off += 3
    for ch in range(7):
        for k in range(num_keys):
            (v,) = struct.unpack_from("<h", d, off); off += 2
            key_arr[ch][key_off + k] = v
    for l in range(num_keys * 6):
        (v,) = struct.unpack_from("<h", d, off); off += 2
        if v >= 0:
            v += chan_base[l % 6]
        tw_idx.append(v)
    cc = struct.unpack_from("<6h", d, off); off += 12
    off += 4  # DEADBEEF
    for ch in range(6):
        for n in range(cc[ch]):
            tween_arr[ofs_tween[ch] + chan_base[ch] + n] = struct.unpack_from("<b", d, off)[0]
            off += 1
    for ch in range(6):
        chan_base[ch] += cc[ch]
    cams.append((num_keys, sr, key_off, cc))
    key_off += num_keys

CH = "X Y Z PITCH YAW ROLL MS".split()
print(f"cams={total_cams} totalKeys={total_keys} ofsMayaTween={ofs_tween} totalTweens={total_tweens}")
for ci in sys.argv[1:] or ["11", "12"]:
    ci = int(ci)
    nk, sr, ko, cc = cams[ci]
    print(f"\n== camera {ci}: numKeys={nk} sampleRate={sr} keyOffset={ko} tweenCounts={list(cc)}")
    for k in range(nk):
        gi = ko + k
        vals = [key_arr[c][gi] for c in range(7)]
        est = 0 if gi + 1 == total_keys else ((vals[6] & 0xFFFF) - 1) // sr
        print(f"  key{k}: x={vals[0]} y={vals[1]} z={vals[2]} pitch={vals[3]} yaw={vals[4]} "
              f"roll={vals[5]} ms={vals[6] & 0xFFFF} estNumTweens={est}")
        ti = tw_idx[gi * 6:gi * 6 + 6]
        print(f"        tweenIdx={ti}")
        for ch in range(6):
            if ti[ch] not in (-1, -2):
                n = est if est else 0
                seq = [tween_arr[ofs_tween[ch] + ti[ch] + j] for j in range(max(n, 1))]
                print(f"        tweens[{CH[ch]}] = {seq}")
