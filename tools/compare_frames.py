#!/usr/bin/env python3
"""Compare F12 / tick-keyed frame captures (24-bit BMP) byte for byte.

Usage:
  compare_frames.py A.bmp B.bmp
      Pixel diff of two captures. Exit 0 iff the files are identical.

  compare_frames.py --golden CAPTURE_DIR [--backend sokol]
      Compare every golden frame tests/golden/frames/tNNNN.bmp with
      CAPTURE_DIR/capture-<backend>-tNNNN.bmp. Exit 0 iff all identical.

  compare_frames.py --update CAPTURE_DIR [--backend sokol]
      (Re)create a golden frame from every CAPTURE_DIR/capture-<backend>-tNNNN.bmp
      (overwrites existing ones; stale goldens must be deleted by hand).

Captures come from DoomIIRPG run with DOOM2RPG_CAPTURE_TICKS / DOOM2RPG_MENU_TICKS,
see tests/golden/frames/README.md.
"""

import argparse
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN_DIR = os.path.join(REPO, "tests", "golden", "frames")
GOLDEN_RE = re.compile(r"^t(\d{4})\.bmp$")


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def parse_bmp(data, path):
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 24:
        raise ValueError(f"{path}: {bpp} bpp, expected 24")
    stride = (width * 3 + 3) & ~3
    rows = [data[offset + y * stride: offset + y * stride + width * 3]
            for y in range(abs(height))]
    return width, abs(height), rows


def diff(path_a, path_b):
    """Prints the comparison, returns True when the files are byte-identical."""
    a = read_bytes(path_a)
    b = read_bytes(path_b)
    wa, ha, rows_a = parse_bmp(a, path_a)
    wb, hb, rows_b = parse_bmp(b, path_b)
    if (wa, ha) != (wb, hb):
        print(f"  size mismatch: {wa}x{ha} vs {wb}x{hb}")
        return False
    differing = 0
    nonblack = 0
    x0, y0, x1, y1 = wa, ha, -1, -1
    for y in range(ha):
        ra, rb = rows_a[y], rows_b[y]
        for x in range(wa):
            pa = ra[x * 3: x * 3 + 3]
            if pa != b"\0\0\0":
                nonblack += 1
            if pa != rb[x * 3: x * 3 + 3]:
                differing += 1
                x0, y0 = min(x0, x), min(y0, y)
                x1, y1 = max(x1, x), max(y1, y)
    total = wa * ha
    print(f"  {wa}x{ha}, non-black {nonblack}/{total} in A, "
          f"differing pixels {differing} ({100.0 * differing / total:.3f}%)")
    if differing:
        # BMP rows run bottom-up; report top-down canvas coordinates.
        print(f"  diff bbox x {x0}..{x1}, y {ha - 1 - y1}..{ha - 1 - y0}")
    identical = a == b
    print("  files identical" if identical else "  files differ")
    return identical


def golden_frames():
    frames = []
    for name in sorted(os.listdir(GOLDEN_DIR)):
        m = GOLDEN_RE.match(name)
        if m:
            frames.append((int(m.group(1)), os.path.join(GOLDEN_DIR, name)))
    return frames


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--golden", metavar="CAPTURE_DIR")
    ap.add_argument("--update", metavar="CAPTURE_DIR")
    ap.add_argument("--backend", default="sokol")
    args = ap.parse_args()

    if args.update:
        capture_re = re.compile(rf"^capture-{re.escape(args.backend)}-t(\d{{4}})\.bmp$")
        ticks = sorted(int(m.group(1)) for m in map(capture_re.match, os.listdir(args.update)) if m)
        if not ticks:
            print(f"no capture-{args.backend}-tNNNN.bmp files in {args.update}")
            return 1
        os.makedirs(GOLDEN_DIR, exist_ok=True)
        for tick in ticks:
            golden = os.path.join(GOLDEN_DIR, f"t{tick:04d}.bmp")
            capture = os.path.join(args.update, f"capture-{args.backend}-t{tick:04d}.bmp")
            data = read_bytes(capture)
            parse_bmp(data, capture)
            with open(golden, "wb") as f:
                f.write(data)
            print(f"updated {golden} from {capture}")
        return 0

    if args.golden:
        frames = golden_frames()
        if not frames:
            print(f"no golden frames in {GOLDEN_DIR}")
            return 1
        ok = True
        for tick, golden in frames:
            capture = os.path.join(args.golden, f"capture-{args.backend}-t{tick:04d}.bmp")
            print(f"tick {tick}: {os.path.relpath(golden, REPO)} vs {capture}")
            if not os.path.exists(capture):
                print("  capture missing")
                ok = False
                continue
            ok = diff(golden, capture) and ok
        print("ALL IDENTICAL" if ok else "MISMATCH")
        return 0 if ok else 1

    if len(args.files) != 2:
        ap.error("give two files, --golden or --update")
    print(f"{args.files[0]} vs {args.files[1]}")
    return 0 if diff(args.files[0], args.files[1]) else 1


if __name__ == "__main__":
    sys.exit(main())
