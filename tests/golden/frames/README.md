# Golden frames

Reference captures of the letterboxed canvas (960x640, 24-bit BMP, 1.8 MB each) that the
sokol-over-OpenGL build must reproduce **byte for byte**. They were taken on 2026-09-22
from the raw GL backend (`render/gl/`, retired in group G8 of
`docs/architecture/specs/2026-09-11-sokol-gfx-backend.md`) immediately before it was
deleted; sokol-glcore matched all of them with zero differing pixels at that time.

| File | Tick | Content |
|---|---|---|
| `t0200.bmp` | 200 | intro cinematic (camera 0) |
| `t0400.bmp` | 400 | intro cinematic, later |
| `t0500.bmp` | 500 | intro cinematic, later |
| `t0750.bmp` | 750 | gameplay with the HUD (cinematics skipped at tick 600) |
| `t0850.bmp` | 850 | in-game menu open (menu key at tick 800) |

## Why tick-keyed

Every animation reads `upTimeMs = ticks * 15` (`new_src/core/GameContext.cpp:173`), so
capturing after a given simulation tick yields the same frame on every run. Captures
keyed to wall time (plain F12) are not reproducible. Do not use ticks below ~200: at
tick 60 two runs of the same binary differ by ~140 pixels (something early is still
wall-clock bound).

## Re-capture and compare

Only the `glcore` sokol backend can capture (Metal logs "capture: not supported").
Run from any empty directory; the captures are written to the working directory and
the game quits after the last one. Do not touch the window while it runs (mouse hover
reaches the UI).

```sh
mkdir -p /tmp/frames && cd /tmp/frames
DOOM2RPG_CAPTURE_TICKS=200,400,500,750,850 DOOM2RPG_MENU_TICKS=600,700,800 \
    <repo>/build_new/new_src/DoomIIRPG "<repo>/build_new/new_src/Doom 2 RPG.ipa"
python3 <repo>/tools/compare_frames.py --golden /tmp/frames
```

`ALL IDENTICAL` / exit 0 is a pass. The menu key at 600 skips the intro cinematics,
the one at 700 has no visible effect (observed, cause not investigated), the one at
800 opens the menu.

To replace the goldens after an intended visual change (check the new frames by eye
first):

```sh
python3 <repo>/tools/compare_frames.py --update /tmp/frames
```
