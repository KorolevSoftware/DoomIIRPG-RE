# 2026-11-05 — Map00 cinematic light flicker

Hypothesis: some light/strobe effect produces the blinking room light in the
map00 intro cinematics.

Method: disassembled map00 (`tools/disasm_map_scripts.py tmp_map00.bin --verify`,
0 failures), histogram of opcodes in IPs 1889-2627: besides camera/fade ops it
runs LERP_FOG (6x), SCREEN_SHAKE, SPAWN_PARTICLES, PLAYSOUND, LERPSPRITE*.
Traced EV_LERP_FOG / EV_SET_FOG_COLOR in src/ScriptThread.cpp:1375-1386,
Render::startFogLerp (src/Render.cpp:1801-1820), per-frame lerp
(src/Render.cpp:2300-2314), TinyGL::getFogPalette (src/TinyGL.cpp:47-52),
buildFogTables (src/Render.cpp:1852-1883). Searched for brightness/flicker:
only Hud::brightenScreen (src/Hud.cpp:600), called once from src/Canvas.cpp:1136.

Verdict: CONFIRMED (mechanism identified) — the flicker is EV_LERP_FOG with
t=0 toggling fog between dense (0->32: fogMin 0, fogRange 512), light
(0->128 / 32->128) and off (1024->1024), fog colour C4000000 (black, alpha 196).
Camera 1 cinematic IPs 2132-2181; func_1219 (200ms cadence) from IPs 2661,
3472, 5850; inline at 4309-4359.

Rewrite state: new_src/domain/game/ScriptVM.cpp:1413-1423 consumes both
opcodes and logs "skipped"; fog disabled in new_src/core/Main.cpp:301-304
(World3D::setFog exists, new_src/render/World3D.cpp:255, GLES-style linear fog).
Details: docs/original-code/light-flicker.md.
