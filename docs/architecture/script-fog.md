# Script-driven depth fog (software 16-level palette fog)

Status: spec, 2026-11-05. ADR: [0049](adr/0049-script-fog-software-palette.md).
Facts: `docs/original-code/light-flicker.md`, `docs/research/2026-11-05-light-flicker.md`.

## 0. Goal and symptom

The map00 intro (camera-1 cinematic, IPs 2132-2181) and the explosion scripts
(`func_1219` IPs 1219-1259, inline copy IPs 4309-4359, one-shots at 5853, 7757,
8075) flicker the room light with `EV_LERP_FOG` at duration 0
(light-flicker.md "Trigger sites"). new_src drops both opcodes
(`new_src/domain/game/ScriptVM.cpp:1413-1423`), never feeds fog
(`new_src/core/Main.cpp:303-306`) and its only fog path is the GLES linear
formula (`new_src/render/World3D.cpp:253-278`,
`new_src/render/sokol/shaders/world.glsl` fs_world). The reference runs the
TinyGL software path, so we port the software 16-level palette fog.

## 1. Original behaviour being reproduced (all cited)

State (TinyGL + Render):
- `tinyGL->fogColor` (ARGB), `fogMin`, `fogRange`; boot values 0 / 32752 / 1
  (`src/TinyGL.cpp:34-36`).
- Lerp: `baseFogMin/baseFogRange/destFogMin/destFogRange/fogLerpStart/fogLerpTime`
  (`src/Render.cpp:1801-1820`).

Writers:
- `EV_SET_FOG_COLOR` → `buildFogTables(argb)` (`src/ScriptThread.cpp:1375-1379`):
  stores `fogColor`; if alpha == 0 also `fogMin = 32752, fogRange = 1` and
  returns (`src/Render.cpp:1852-1861`).
- `EV_LERP_FOG` → `startFogLerp(v & 0x7FF, (v >> 11) & 0x7FF, ((v >> 22) & 0xFF) * 100)`
  (`src/ScriptThread.cpp:1381-1386`).
- `startFogLerp(n, n2, t)` (`src/Render.cpp:1801-1820`): `base = current`,
  `destMin = n << 4`, `destRange = (n2 - n) << 4`, `0 → 1`; `t == 0` writes
  `fogMin/fogRange = dest` immediately and zeroes the lerp, else arms
  `fogLerpStart = app->time, fogLerpTime = t`.
- Per frame, right after `setView` at the start of the 3D render
  (`src/Render.cpp:2299-2314`): if `fogLerpTime != 0`: while
  `time < start + lerpTime`: `f = ((time - start) << 12) / lerpTime`,
  `fogMin = baseMin + ((destMin - baseMin) * f >> 12)`, same for range, range
  `0 → 1`; else `fogLerpTime = 0`, `fogMin/fogRange = dest`.
- Map unload: `startFogLerp(32752, 32752, 0)` (`src/Render.cpp:214`) →
  `fogMin = 524032, fogRange = 1`; `fogColor` is NOT reset. Called from the map
  load sequence `LoadingManager::unloadMedia` (`src/LoadingManager.cpp:625`,
  `:746-750`).
- map00 INIT: `SET_FOG_COLOR C4000000` (IP 31) then `LERP_FOG 1024->1024`
  (IP 36) → colour black alpha 196, `fogMin 16384`, `fogRange 1`
  (light-flicker.md "Opcodes").

Reader (software path):
- `getFogPalette(fracZ)` (`src/TinyGL.cpp:47-52`):
  `level = clamp(((0x7FFFFF / (fracZ >> 16) - fogMin) << 4) / fogRange, 0, 15)`.
- `fracZ >> 16` is the screen-linear `z = 0x7FFFFF / w` set by `projectVerts` /
  `RasterizeConvexPolygon` (`src/TinyGL.cpp:521`, `:552`) and interpolated
  `<< 16` by `TGLEdge::setFromVerts` (`src/TGLEdge.cpp:24`, `:29`).
- `w` is clip w of the 14.14 MVP: projection `[11] = -MATRIX_ONE`, `[15] = 0`
  (`src/TinyGL.cpp:114-118`) ⇒ `w = -eyeZ` in TinyGL vertex units (world << 4;
  view origin is `viewX = player.viewX << 4`, `new_src/render/SceneRenderer.cpp:98-100`).
  So `depthUnits = 0x7FFFFF / (0x7FFFFF / w)` ≈ eye depth in world<<4 units
  (1 tile = 1024).
- Walls/floors: one level per span, taken at the span's left edge
  (`src/TinyGL.cpp:654`, `:662`). Sprites: one level per sprite line from vertex 0
  (`src/Render.cpp:507`); 2D view weapon at fixed `fracZ 0x40000000` (depth 511,
  `src/Render.cpp:385`); sky quad / sky polygons at fixed `0x10000000`
  (depth 2047, `src/TinyGL.cpp:654/662` `isSkyMap`, `src/Render.cpp:2133`).
- Palette level i (1..15) built by `buildFogTables` / `buildFogTable`
  (`src/Render.cpp:1823-1832`, `:1863-1879`), on RGB565 base entries:
  - `n2 = ((i << 8) / 16 * alpha) >> 8`, `f = 256 - n2`;
  - `fogC = upSamplePixel(R*n2>>8, G*n2>>8, B*n2>>8)` (565 truncation,
    `src/Render.h:332`);
  - `dest = (((f>>2) * ((dat & 0x07E0) >> 6)) & 0x07E0 | (((f>>3) * (dat & 0xF81F)) >> 5) & 0xF81F) + fogC`.
  Level 0 is the untouched base palette.
- Render modes are applied by `setupPalette` AFTER the fog palette is picked,
  for every mode (`src/Render.cpp:507` → `:1885+`); PULSATE replaces the palette
  with flat grey and ignores fog (`src/Render.cpp:1968-1979`).
- Clear colour: `fogColor` when `fogRange > 1`, else 0, only when no sky map is
  drawn (`src/Render.cpp:2323-2336`).

## 2. Design summary (decisions, see ADR 0049)

1. **State owner: `Game::fog`**, a new plain domain struct `FogState`
   (`domain/game/FogState.h/.cpp`) — same pattern as `Game::particles`
   (ADR 0047): domain owns the state, scripts write it, the renderer reads it
   each frame. It is the merged TinyGL+Render fog fields.
2. **Per-fragment level, integer-exact quantisation** in `fs_world`: the shader
   reproduces `z = 0x7FFFFF / w`, `d = 0x7FFFFF / z`, the level division and the
   565 palette-table arithmetic bit-for-bit on the LUT texel. Per-fragment
   instead of per-span is the only deviation (walls: spans are columns of
   constant depth, so identical; floors: identical at pitch 0; billboards:
   constant depth).
3. **Fog before `color_mod`**, for every render mode (software order,
   `src/Render.cpp:507` then `setupPalette`); the GLES per-mode fog flag
   (`kRenderModes[].fog`) is no longer read by `SgScene3D`.
4. **Fog off is a uniform branch** (`fog_params.z == 0`, or level 0) that leaves
   the texel untouched → bit-identical output to today.
5. **Clear colour: not implemented.** new_src always draws the sky backdrop over
   the world band when a sky is uploaded (`new_src/core/Main.cpp:289-295`,
   `new_src/render/World3D.cpp:490-497`), so a clear colour is never visible;
   for map00 it would be black anyway (C4000000). The sky itself IS fogged at
   fixed depth 2047, as in `drawSkyMap` (`src/Render.cpp:2133`).
6. **Out of scope** (hooks named in §6): COLD buff lerp, view-weapon fog,
   save/load persistence, Hud ice overlay.

## 3. Units: our depth → legacy depth

- World vertices: `pos = raw / 16384` (`new_src/render/World3D.cpp:176-178`,
  Scene3D.h `WorldVertex`), raw = TinyGL vertex units.
- View matrix: `viewF = view_ / 16384` (`new_src/render/Camera3D.cpp:104`), where
  `view_` is the legacy 14.14 matrix whose translation is in TinyGL units
  (`new_src/render/Camera3D.cpp:36-38`). Hence the vertex shader's
  `fog_depth = -(view * pos).z` (world.glsl vs_world) equals `-eyeZ_tgl / 16384`.
- Legacy `w = -eyeZ_tgl` (§1). Therefore **`w_tgl = fog_depth * 16384.0`**.

## 4. Group G1 — renderer: palette fog in the world program (no caller yet)

Files: `new_src/render/api/Scene3D.h`, `new_src/render/sokol/SgScene3D.h`,
`new_src/render/sokol/SgScene3D.cpp`, `new_src/render/sokol/shaders/world.glsl`,
`new_src/render/sokol/SgFrame.h` (comment only), `new_src/render/World3D.h`,
`new_src/render/World3D.cpp`.

### 4.1 Scene3D API (replace the float overload)

```cpp
// Legacy TinyGL software depth fog (ADR 0049): fogColorARGB = tinyGL->fogColor,
// fogMin / fogRange = tinyGL->fogMin / fogRange, verbatim ints. Alpha 0 = fog off
// (src/Render.cpp:1857-1861). Applies to every render mode, before the colour
// modulation; the sky draws at the fixed legacy depth 2047. Call outside a scene.
virtual void setFog(int fogColorARGB, int fogMin, int fogRange) = 0;
```

### 4.2 World3D

`World3D::setFog(int fogColorARGB, int fogMin, int fogRange)` keeps its
signature; body becomes `if (scene_) scene_->setFog(fogColorARGB, fogMin, fogRange);`.
Delete the GLES conversion (`World3D.cpp:253-278`) and rewrite the header
comment (`World3D.h:93-96`) to "forwards the legacy software fog state
(ADR 0049)".

### 4.3 SgScene3D

- Members: replace `fogStart_`, `fogEnd_`, `float fogColor_[4]` and
  `currentFogOn_` with `int fogColor_ = 0; int fogMin_ = 32752; int fogRange_ = 1;`
  and keep `bool fogEnabled_ = false;`.
- `setFog`: `assert(vertices_.empty())` (outside a scene), store the three ints,
  `fogEnabled_ = ((uint32_t)argb >> 24) != 0`.
- `setRenderMode`: the fog term drops out of the cache compare; early-out is
  `pulsateA_ < 0.f && mode == currentRenderMode_`. Comment: software path fogs
  every mode (`src/Render.cpp:507`), so `kRenderModes[].fog` is not consulted.
  `beginScene` drops the `currentFogOn_` line.
- Helper (file-local) filling the fog uniforms:
  ```cpp
  // fog_color = legacy ARGB bytes as 0..255 floats; fog_params = (fogMin, fogRange, mode, forcedZ).
  void fillFog(SgFrame::Uniforms& u, int argb, int fogMin, int fogRange, bool on, float forcedZ) {
      u.fogColor[0] = (float)((argb >> 16) & 0xFF);   // R (no swap: upSamplePixel, src/Render.h:332)
      u.fogColor[1] = (float)((argb >> 8) & 0xFF);
      u.fogColor[2] = (float)(argb & 0xFF);
      u.fogColor[3] = (float)(((uint32_t)argb >> 24) & 0xFF);
      u.fogParams[0] = (float)fogMin;    // exact: |values| < 2^24 (max 524032)
      u.fogParams[1] = (float)fogRange;
      u.fogParams[2] = on ? 1.f : 0.f;
      u.fogParams[3] = forcedZ;
  }
  ```
- `flush()`:
  - pulsate branch: `colorMod = 1`, `fogColor = {a, a, a, 1}`,
    `fogParams = {0, 0, 2, 0}` (mode 2 = flat colour; replaces the
    `kPulsateFogEnd` trick, same output).
  - normal branch: `colorMod = row.mod`, `fillFog(u, fogColor_, fogMin_, fogRange_, fogEnabled_, 0.f)`.
- `recordSky()`: `fillFog(u, fogColor_, fogMin_, fogRange_, fogEnabled_, 4096.f)`;
  4096 = `0x10000000 >> 16` (`src/TinyGL.cpp:654`, `src/Render.cpp:2133`).
  Update the "sky drawn with fog off" comment accordingly.
- `SgFrame.h:32` comment: `.x fogMin .y fogRange .z mode (0 off, 1 depth, 2 flat) .w forced z (0 = per fragment)`.

### 4.4 world.glsl fs_world (exact code; vs_world unchanged)

```glsl
layout(binding=1) uniform fs_world_params {
    vec4 color_mod;
    vec4 fog_color;    // mode 1: legacy fog ARGB bytes (r,g,b,a) 0..255; mode 2: flat rgb 0..1
    vec4 fog_params;   // .x fogMin, .y fogRange, .z mode 0/1/2, .w forced z (0 = from fog_depth)
};
...
// C integer division (truncates toward zero) for possibly negative operands.
int cdiv(int a, int b) {
    int q = abs(a) / abs(b);
    return ((a < 0) != (b < 0)) ? -q : q;
}

void main() {
    float index = texture(sampler2D(tex, smp), uv).r;
    vec4 col = texture(sampler2D(pal, smp_pal), vec2(index, 0.5));
    if (fog_params.z == 2.0) {
        col.rgb = fog_color.rgb;                 // RENDER_FLAG_PULSATE flat grey (ADR 0048)
    } else if (fog_params.z == 1.0 && col.a != 0.0) {
        // TinyGL software fog, ADR 0049. w = fog_depth * 16384 (script-fog.md §3).
        int z = fog_params.w > 0.0 ? int(fog_params.w)
              : int(8388607.0 / max(fog_depth * 16384.0, 1.0));  // projectVerts, TinyGL.cpp:521
        int d = 8388607 / max(z, 1);                              // getFogPalette, TinyGL.cpp:48
        int lvl = clamp(cdiv((d - int(fog_params.x)) * 16, int(fog_params.y)), 0, 15);
        if (lvl > 0) {
            int n2 = ((lvl << 8) / 16 * int(fog_color.a)) >> 8;   // Render.cpp:1864
            int f = 256 - n2;
            ivec3 c8 = ivec3(col.rgb * 255.0 + 0.5);           // LUT = 565 bit-replicated
            int dat = ((c8.r >> 3) << 11) | ((c8.g >> 2) << 5) | (c8.b >> 3);
            int sc = (((f >> 2) * ((dat & 0x07E0) >> 6)) & 0x07E0)
                   | ((((f >> 3) * (dat & 0xF81F)) >> 5) & 0xF81F); // buildFogTable, Render.cpp:1830
            ivec3 fc = (ivec3(fog_color.rgb) * n2) >> 8;
            int o = (sc + (((fc.r >> 3) << 11) | ((fc.g >> 2) << 5) | (fc.b >> 3))) & 0xFFFF;
            int r5 = (o >> 11) & 31; int g6 = (o >> 5) & 63; int b5 = o & 31;
            col.rgb = vec3(float((r5 << 3) | (r5 >> 2)), float((g6 << 2) | (g6 >> 4)),
                           float((b5 << 3) | (b5 >> 2))) / 255.0;
        }
    }
    col *= color_mod;   // setupPalette render-mode transform comes after the fog palette
    frag_color = col;
}
```

Notes for the coder:
- The LUT texel is the bit-replicated RGB565 entry (`new_src/render/api/PixelConvert.cpp:7-15`),
  so `c8 >> 3 / >> 2` recovers the 565 fields exactly; the re-expansion uses
  the same replication, so output equals what the 565 fog table would show.
- `col.a == 0` (transparent key) is left untouched (fog touches RGB only).
- The C left shift `<< 4` is written `* 16` (defined for negatives in GLSL).
- `fogRange` is never 0 (`src/Render.cpp:1809-1811`, `:2305-2307`); `cdiv`
  handles the negative ranges the COLD buff produces (§6).

### 4.5 G1 acceptance

- Builds (Debug; shader compiled by sokol-shdc; also the Metal target if it is
  part of the default build).
- Nothing calls `World3D::setFog` yet (`Main.cpp` call stays commented) ⇒ fog is
  off ⇒ golden run `DOOM2RPG_CAPTURE_TICKS=200,400,500,750,850 DOOM2RPG_MENU_TICKS=600,700,800 ./DoomIIRPG`
  + `python3 tools/compare_frames.py --golden tests/golden/frames` reports ALL IDENTICAL.
- Corpse halo still a flat pulsating grey silhouette (mode 2 path).
- Temporary manual check (do not commit): uncomment
  `world.setFog(0xC4000000, 0, 512)` in Main ⇒ everything beyond ~half a tile is
  ~29% brightness, the sky dark, near walls brighter in 16 visible bands; revert.

## 5. Group G2 — domain: `FogState`, script opcodes, per-frame tick, map reset

Files: new `new_src/domain/game/FogState.h/.cpp`; `new_src/domain/game/Game.h`;
`new_src/domain/game/ScriptVM.cpp`; `new_src/render/SceneRenderer.h/.cpp`;
`new_src/core/GameContext.cpp`; `new_src/core/Main.cpp`. New files ⇒ re-run
`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (GLOB).

### 5.1 FogState.h

```cpp
#ifndef NEW_DOMAIN_GAME_FOGSTATE_H
#define NEW_DOMAIN_GAME_FOGSTATE_H
#include <cstdint>
namespace newcore {

// Legacy depth-fog state: tinyGL->fogColor/fogMin/fogRange + Render's lerp
// fields (src/TinyGL.cpp:34-36, src/Render.cpp:1801-1820). Owned by Game::fog;
// written by EV_SET_FOG_COLOR / EV_LERP_FOG, ticked + read by SceneRenderer.
struct FogState {
    int color = 0;        // ARGB; alpha 0 = fog off
    int min = 32752;
    int range = 1;
    int baseMin = 0, baseRange = 0, destMin = 0, destRange = 0;
    int64_t lerpStart = 0;
    int lerpTime = 0;     // ms, 0 = no lerp running

    void buildFogTables(int argb);                      // src/Render.cpp:1852-1861 (state part)
    void startLerp(int n, int n2, int ms, int64_t now); // src/Render.cpp:1801-1820
    void tick(int64_t now);                             // src/Render.cpp:2300-2314
    void unloadMap() { startLerp(32752, 32752, 0, 0); } // src/Render.cpp:214
    static void selfCheck();                            // asserts, Debug boot
};

} // namespace newcore
#endif
```

### 5.2 FogState.cpp (verbatim semantics)

- `buildFogTables(argb)`: `color = argb; if (((uint32_t)argb >> 24) == 0) { min = 32752; range = 1; }`.
  (The palette building itself is the shader's job.)
- `startLerp`: `baseMin = min; baseRange = range; destMin = n << 4;
  destRange = (n2 - n) << 4; if (destRange == 0) destRange = 1;`
  `if (ms != 0) { lerpStart = now; lerpTime = ms; } else { lerpStart = 0; lerpTime = 0; min = destMin; range = destRange; }`
- `tick(now)`: `if (lerpTime == 0) return;`
  `if (now < lerpStart + lerpTime) { int f = (int)(((now - lerpStart) << 12) / lerpTime);
  min = baseMin + (int)(((int64_t)(destMin - baseMin) * f) >> 12);
  range = baseRange + (int)(((int64_t)(destRange - baseRange) * f) >> 12);
  if (range == 0) range = 1; } else { lerpTime = 0; min = destMin; range = destRange; }`
  (int64 products: identical to the original wherever its int32 product does not
  overflow; the largest legal product 524032 * 4096 still fits int32.)
- `selfCheck()` (asserts): fresh state → `startLerp(0, 32, 0, 0)` gives
  `min 0, range 512`; `(0,128)` → `0, 2048`; `(32,128)` → `512, 1536`;
  `(1024,1024)` → `16384, 1` (light-flicker.md table);
  `buildFogTables(0)` → `32752, 1`; lerp `startLerp(0,32,1000,0)` from
  `16384/1`, `tick(500)` → `min == 16384 + ((0-16384)*2048 >> 12) == 8192`,
  `range == 1 + ((512-1)*2048 >> 12) == 256`; `tick(1000)` → `0, 512`, `lerpTime 0`.
  Called once at boot from `Main.cpp` next to the existing Debug self-checks
  (pattern: `PlayerState::selfCheck()`, `new_src/core/SaveGame.cpp:98`), under `#ifndef NDEBUG`.

### 5.3 Wiring

- `Game.h`: `#include "domain/game/FogState.h"`; member
  `FogState fog;   // depth fog (ADR 0049), reset by GameContext::tickLoading` next to
  `particles` (`new_src/domain/game/Game.h:222`).
- `ScriptVM.cpp:1410-1423` replace the two stubs:
  ```cpp
  case Enums::EV_SET_FOG_COLOR: {            // src/ScriptThread.cpp:1375-1379
      int argb = readInt(t);
      env_.game->fog.buildFogTables(argb);
      std::fprintf(stderr, "[script] SET_FOG_COLOR %08X\n", argb);
      break;
  }
  case Enums::EV_LERP_FOG: {                 // src/ScriptThread.cpp:1381-1386
      int v = readInt(t);
      int a = v & 0x7FF, b = (v >> 11) & 0x7FF, ms = ((v >> 22) & 0xFF) * 100;
      env_.game->fog.startLerp(a, b, ms, env_.ctx->upTimeMs);   // app->time, src/Render.cpp:1813
      std::fprintf(stderr, "[script] LERP_FOG %d->%d %dms\n", a, b, ms);
      break;
  }
  ```
  Drop the "Spec tier tables omit these four..." comment for these two (keep it
  for the remaining opcodes it covers, if any).
- Map reset: `GameContext::tickLoading` phase 0 (`new_src/core/GameContext.cpp:627-628`),
  before `clearStateVars`: `sys_.game->fog.unloadMap();   // Render::unloadMap (src/LoadingManager.cpp:625, src/Render.cpp:214)`.
  Colour persists (legacy does not reset it); map00 INIT then sets
  `C4000000` + `16384/1` via the script.
- Clock: the lerp runs on `app->time` (`src/Render.cpp:1813`, `:2301-2302`), i.e. the
  monotonic `GameContext::upTimeMs` (the source `EV_FADEOP` uses, `ScriptVM.cpp:1025-1031`),
  NOT the pausing `gameTime`. `SceneRenderer::Env::upTimeMs` already carries it; no new field.
- `SceneRenderer::drawWorld`: after the `camera_.setView(...)` branches and
  BEFORE `env_.world->drawSky(camera_)` (legacy order: `setView` → lerp → clear/sky,
  `src/Render.cpp:2299-2329`):
  ```cpp
  FogState& fog = env_.game->fog;
  fog.tick(*env_.upTimeMs);                             // app->time, src/Render.cpp:2300-2314
  env_.world->setFog(fog.color, fog.min, fog.range);
  ```
- `Main.cpp:303-306`: delete the commented fog block (fog now comes from `Game::fog`).

### 5.4 G2 acceptance

- Builds after reconfigure; Debug boot passes `FogState::selfCheck()`.
- stderr on new game: `[script] SET_FOG_COLOR C4000000`, `[script] LERP_FOG 1024->1024 0ms`
  during map00 INIT; during the camera-1 cinematic five
  `LERP_FOG 0->32 / 32->128` lines then `1024->1024`.
- On screen (user): in the map00 intro, during camera 1 (explosion sound 1032 +
  shake + particles) the room strobes between dark (distant walls/sprites
  ~29% brightness, near ones less dark, sky dark) and lighter, in steps synced to
  the camera keys, then returns to normal brightness. Explosion events using
  `func_1219` strobe 5 times at 200 ms. HUD/2D unaffected.
- Goldens: frames 200/400/500 (intro) and 750 (gameplay) MAY change, because
  from map00 INIT on fog is live with `16384/1`: (a) any frame inside a strobe
  window changes; (b) outside a strobe, geometry farther than 16384 units
  (16 tiles) is at level 15 in the original too and darkens. Frame 850 (menu)
  must stay identical. The coder reports which frames differ and the
  `LERP_FOG` log lines preceding each capture tick; the orchestrator asks the
  user to confirm the picture before goldens are re-captured. Any diff in a
  frame with no strobe active and no far geometry is a bug.

## 6. Out of scope (hooks)

- **COLD buff** (`new_src/domain/game/Player.cpp:594-600`, `:702`, `:725`):
  legacy `startFogLerp(1, 0, 2000)` on remove and `fogMin = 0; if (fogRange > 0) fogRange = -1;
  startFogLerp(1024, 0, 2000)` on add (`src/Player.cpp:1550-1552`, `:1583-1589`).
  With `FogState` this is three lines, but `Player` holds `const Game*`
  (`new_src/domain/game/Player.h:127`, `:277`) and the negative range drives the
  unported Hud ice overlay (`src/Hud.cpp:496-497`); port both together later.
  The shader's `cdiv` already handles negative ranges.
- **View-weapon fog**: legacy fogs the 2D weapon at depth 511
  (`src/Render.cpp:385`); the rewrite draws it through `Graphics2D`
  (`new_src/ui/ViewWeapon.cpp:79+`), which has no palette-fog path. Not visible
  in cinematics; visible during `func_1219` strobes in gameplay.
- **Save/load**: legacy snaps and saves `fogColor/fogMin/fogRange`
  (`src/Game.cpp:1589-1593`, `:1764-1772`); `loadPlayerFog/savePlayerFog`
  (`src/Render.cpp:1774-1799`, familiar). Add to the world record with ADR 0037
  when needed.
- **Clear colour** (§2.5).
