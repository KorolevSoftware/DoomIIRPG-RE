import glob
import os
import struct
import subprocess

import numpy as np

W, H = 480, 320
fps = 60
pixels_per_frame = 1  # span-writes per video frame

script_dir = os.path.dirname(os.path.abspath(__file__))
build_dir = os.path.join(script_dir, "../build/src")
output_mp4 = os.path.join(script_dir, "output.mp4")

bin_files = sorted(glob.glob(os.path.join(build_dir, "spans_*.bin")))
if not bin_files:
    print(f"No spans_*.bin files found in {build_dir}")
    raise SystemExit(1)

print(f"Found {len(bin_files)} file(s): {[os.path.basename(f) for f in bin_files]}")

proc = subprocess.Popen(
    [
        "ffmpeg", "-y",
        "-f", "rawvideo",
        "-pixel_format", "rgb565le",
        "-video_size", f"{W}x{H}",
        "-framerate", str(fps),
        "-i", "pipe:0",
        "-c:v", "libx264",
        output_mp4,
    ],
    stdin=subprocess.PIPE,
)

total_pixels = 0

for bin_path in bin_files:
    buf = np.zeros((H, W), dtype=np.uint16)
    pixel_count = 0
    print(f"  {os.path.basename(bin_path)}...", end=" ", flush=True)

    with open(bin_path, "rb") as f:
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            idx, cnt, stride = struct.unpack("<IHH", hdr)
            if idx == 0xFFFFFFFF:
                break
            colors = struct.unpack(f"<{cnt}H", f.read(cnt * 2))
            for i, color in enumerate(colors):
                pi = idx + i * stride
                buf[pi // W, pi % W] = color
                pixel_count += 1
                if pixel_count % pixels_per_frame == 0:
                    proc.stdin.write(buf.tobytes())

    proc.stdin.write(buf.tobytes())
    total_pixels += pixel_count
    print(f"{pixel_count} pixels")

proc.stdin.close()
proc.wait()
print(f"Done: {output_mp4}  ({total_pixels} pixels total, {len(bin_files)} frame(s))")
