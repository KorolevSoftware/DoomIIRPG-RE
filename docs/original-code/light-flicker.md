# Light flicker (script-driven fog strobe)

The "flickering room light" seen in the original during map00 cinematics (and
some in-game explosion events) is NOT a dedicated light/strobe effect. It is
the map script snapping the depth-fog parameters back and forth with
`EV_LERP_FOG` (opcode 58) at duration 0, between `WAIT 200ms` /
`ADV_CAMERAKEY` steps. Fog colour for map00 is black with alpha 0xC4, so
"denser fog" = the room goes darker; "fog off" = full brightness.

## Opcodes

- `EV_SET_FOG_COLOR` (57): `render->buildFogTables(getIntArg())`
  (src/ScriptThread.cpp:1375-1379). map00 INIT runs `SET_FOG_COLOR C4000000`
  at IP 31, then `LERP_FOG 1024->1024` at IP 36 (= fog effectively off).
- `EV_LERP_FOG` (58): packed int `a = v & 0x7FF`, `b = (v>>11) & 0x7FF`,
  `ms = ((v>>22)&0xFF)*100`; calls `render->startFogLerp(a, b, ms)`
  (src/ScriptThread.cpp:1381-1386; opcode value src/Enums.h:460).

## startFogLerp / per-frame update

`Render::startFogLerp(n, n2, t)` (src/Render.cpp:1801-1820):
`destFogMin = n << 4`, `destFogRange = (n2 - n) << 4` (0 -> 1). If `t == 0`
the values are written to `tinyGL->fogMin/fogRange` immediately
(src/Render.cpp:1815-1819); otherwise linearly lerped in 12-bit fraction each
frame at the start of the 3D render (src/Render.cpp:2300-2314).
All map00 strobe ops use `t = 0`, i.e. hard on/off steps.

Resulting values for the operands used in map00:

| op (a->b) | fogMin | fogRange | effect |
|---|---|---|---|
| 0 -> 32   | 0     | 512  | very dense: full fog at depth 480 |
| 0 -> 128  | 0     | 2048 | lighter: full fog at depth 1920 |
| 32 -> 128 | 512   | 1536 | lighter, starts at depth 512 |
| 1024->1024| 16384 | 1    | off (depth < 16384 -> level 0) |

## What it modulates (TinyGL software path)

- Per span / per sprite line palette selection, not a post-process:
  `TinyGL::getFogPalette(fracZ)`: `i = ((0x7FFFFF/(fracZ>>16) - fogMin) << 4) / fogRange`,
  clamped to 0..15, returns `paletteBase[i]` (src/TinyGL.cpp:47-52). Called
  per span for walls/floors (src/TinyGL.cpp:654, :662) and for sprites
  (src/Render.cpp:385, :507). Sky uses a fixed 0x10000000 depth.
- The 16 palettes are prebuilt by `buildFogTables(fogColor)`
  (src/Render.cpp:1852-1883): for level i (1..15)
  `n2 = ((i<<8)/16 * alpha) >> 8`, texel = base * (256-n2)/256 + fogColor*n2/256
  (`buildFogTable`, src/Render.cpp:1823-1832). With C4000000 (alpha 196, black)
  level 15 gives n2 = 183, i.e. texels scaled to ~29% brightness.
  Alpha 0 disables fog (fogMin=32752, fogRange=1; src/Render.cpp:1857-1861).
- Frame clear colour is `fogColor` when `fogRange > 1`, else 0
  (src/Render.cpp:2331-2336) — black in map00 either way.
- So the strobe darkens geometry/sprites by depth (near stays brighter);
  HUD/2D are unaffected.

## Trigger sites in map00 (disasm, tools/disasm_map_scripts.py)

- Camera 1 cinematic (func_2084, STARTCINEMATIC camera=1 at IP 2098):
  IPs 2132, 2139, 2160, 2167, 2174 alternate `0->32` / `32->128`, each
  followed by `ADV_CAMERAKEY keys=1`; IP 2181 `1024->1024` restores.
  Paired with PLAYSOUND 1032, SCREEN_SHAKE, SPAWN_PARTICLES (IPs 2121-2127).
- `func_1219` (IPs 1219-1259): `0->32`, `0->128`, `0->32`, `0->128`, `0->32`
  each with `WAIT 200ms`, then `1024->1024`. Called from IPs 2661, 3472, 5850
  (explosion sequences: PLAYSOUND 1032/1059 + SCREEN_SHAKE + SPAWN_PARTICLES).
- Inline copy at IPs 4309-4359 (event[56] chain), same 200ms cadence.
- Other one-shot fog ops: IP 5853 (`0->128`), 7757/8075 (`0->96`) followed
  by `1024->1024` at 7764/8082.
So it is script-driven, both inside cinematics and in ordinary event scripts.

## Other fog writers (not map00 strobe)

- Player COLD buff: `startFogLerp(1, 0, 2000)` / `(1024, 0, 2000)`
  (src/Player.cpp:1551, :1588).
- Save/load persist fogMin/fogRange/fogColor (src/Game.cpp:1591-1593, :1764-1772);
  `loadPlayerFog/savePlayerFog/snapFogLerp` (src/Render.cpp:1774-1799).
- `Hud::brightenScreen` / `brightenPostProcess` (src/Hud.cpp:600-617,
  src/Render.cpp:2620-2670) is a separate brightness post-process, only called
  from src/Canvas.cpp:1136; not part of the map00 flicker.
