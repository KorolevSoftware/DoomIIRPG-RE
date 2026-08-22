# new_src architecture

_Audit date: 2026-08-22. All citations are `new_src/<file>:<line>`. 58 source files,
~5,260 LOC across `core/ domain/ graphics/ io/ platform/ render/ text/ ui/`.
Namespace for everything: `newcore`._

## Module map

### core/
| File | Responsibility |
|---|---|
| `Main.cpp` | Entry point (`main()` :36). Phase-0 data verification, then an **ad-hoc inline render loop** with discrete movement, door use, HUD demo cycling (:252-465). |
| `AppContext.h/.cpp` | Composition root: Window, RenderBackend, InputSystem, ZipArchive (:41-44); `readResource()` applies archive prefix `Payload/Doom2rpg.app/Packages/` (AppContext.h:22, AppContext.cpp:57-59). |
| `GameLoop.h/.cpp` | Fixed-step (~15 ms tick, :14) loop skeleton — currently **dead code**, never invoked. |

### domain/game/
| File | Responsibility |
|---|---|
| `Entity.h` | World entity record: `EntityDef*`, monster ptr, tile linked-list ptrs, packed sprite index in `info` low bits (:39-40), flag bits (:22-26). |
| `Enums.h` | Legacy constants: entity types (:10-25), trace masks (:28-37), stat slots (:40-48), doors (:51-56), sprite flags (:63-72), monster anim/flags (:75-106). |
| `CombatEntity.h/.cpp` | Battle-stat block (8 slots + weapon); clamped set/add, XP calc (:52-54). No calcHit/calcDamage yet. |
| `Player.h/.cpp` | Player state: stats, inventory[26]/ammo[9]/weapon bitmask, XP; discrete grid movement via `kViewStepValues` 8-dir table (:8-11). |
| `Game.h/.cpp` | Simulation subset: 32x32 entityDb lists (Game.h:69), door anims (6 slots), faced-door use `useDoorFacing` (ADR 0001), wall collision `CapsuleToLineTrace` (:263-296), turn-advance auto-close with tile-granular occupancy, linked-state door solidity. Doors only so far. |

### domain/world/
| File | Responsibility |
|---|---|
| `MapData.h` | Parsed mapXX.bin container: header (:21-38), media (:41-49), BSP geometry (:52-62), sprites (:64-70), events/bytecode/flags (:73-76), maya cams (:79-86), decoded `Polygon`s (:96-104). |
| `MapParser.h/.cpp` | Static parser mirroring legacy `LoadingManager::loadMapData` (v3); `decodePolys()` expands leaf nodePolys into quads incl. 2-vert edge expansion (:206-298). |

### graphics/
| File | Responsibility |
|---|---|
| `Image.h` | Palette-indexed image from BMP loading (dims, indices, RGB565 palette, texture slot). |

### io/
| File | Responsibility |
|---|---|
| `BmpImageLoader` | 4/8-bpp BMP decoder replicating legacy transform exactly (nibble expand, row flip, RGB565, 0xF81F transparency) (header :12-17). |
| `DataReader/.cpp` | Sequential LE reader; `readCoord()` = byte\*8 (:35-36). |
| `DataWriter/.cpp` | Growable LE writer, atomic flush (:31) — for future saves. |
| `EntityDefs/.cpp` | Parses entities.bin → `EntityDef` (tileIndex/name/eType/eSubType/parm/touchMe); find by type or tileIndex. |
| `Localization` | strings.idx + stringsNN.bin chunks; `(type<<10)|index` ids; `titleOf()` splits on `\|`. |
| `Media/.cpp` | `MediaMappings::load` newMappings.bin (:10-31); `MediaLoader::registerMedia/finalize` loads palettes + rolling texels files (:41-153). |
| `Resources.h` | File-name constants for all archive entries. |
| `Tables/.cpp` | tables.bin offset table → 18 typed vectors (combat, keys, sin table, sky A/B...) (:25-68). |
| `ZipArchive/.cpp` | Minimal zip reader: central dir, case-insensitive lookup, stored + raw-deflate. |

### platform/
| File | Responsibility |
|---|---|
| `FileSystem` | Base/save paths via SDL_GetBasePath, archive search, whole-file read, atomic write. |
| `InputSystem` | SDL event pump → single callback (:5-15); action-mapping pipeline is future work. |
| `Window` | SDL2 window + GL 3.3 core context (Apple) / compatibility (:42-50); letterbox viewport (:159-169); canvas fixed 480x320 (:25-26). |

### render/
| File | Responsibility |
|---|---|
| `Camera3D` | Faithful 14.14 fixed view/projection/MVP via 1024-entry sin table (:15-62); GLES BeginFrame projection tweaks (:83-92); float + int matrix accessors. |
| `Graphics2D` | Canvas-space 2D over SpriteBatch: rects/lines, blits with rotateModes 0-8, anchors, scaled draws, `drawString` with `^N` colors/buff icons; clip recorded but not applied (:20-24). |
| `RenderBackend` | Frame owner: beginFrame clear + letterbox viewport + batch begin (:37-47); endFrame flush+swap (:49-52). |
| `World3D` | GL 3.3 world renderer: palette-LUT textures (:202+), sky (:399-454), polys (:329+), BSP walkNode painter's algorithm with per-leaf sprites (:788-881), billboard/wall/flat/slip-door sprites + RLE (:100+, :507-746), eye-space fog (:180-200), time animation (:78), per-sprite sort-bias hook on `drawBSP`. |

### render/gl/
| File | Responsibility |
|---|---|
| `GlCommon.h` | Platform GL header selection (:4-11). |
| `Shader` | RAII program wrapper, uniform setters incl. mat4. |
| `SpriteBatch` | Batched quad renderer (4096 max, :18-19); three programs: indexed-palette, RGBA, flat color (:64-66). |
| `Texture` | Move-only texture: indexed R8 + RGBA8 palette LUT with 0xF81F kill, optional GL_REPEAT (:33), plain RGBA8 (:37). |

### text/
| File | Responsibility |
|---|---|
| `Font` | Font.bmp atlas: 12x16 glyphs, advance 9 (:18-20), `^N` color table (:23-26), char→glyph mapping (:43). |
| `Text` | Mutable text buffer port of legacy Text: append/find/edit, wrap with break char (:58-62), width measure (:67-69). |

### ui/
| File | Responsibility |
|---|---|
| `Hud` | Loads ~24 BMP textures (`startup()` :72-106); status bars, cockpit, weapon select (:211-233), bubbles, vignette (:136-169), arrows, monster bar (:180-209), messages. Currently demo-state driven. |

## Wiring (startup → frame)

**Startup** (`main()`, Main.cpp:36): `AppContext::initialize(archive)` (window/backend/input/zip)
→ stack-local Phase-0 objects: Tables (:53-55), EntityDefs (:73-76), Localization (:84-99),
MediaLoader mappings+finalize (:105-143), MapParser→MapData (:146-164), Font (:167-181),
Hud (:191-192), World3D + textures + sky + test fog (:196-209), Camera at spawn (:213-228),
Player reset+spawn (:231-246), Game.loadEntities (:248-249).

**Per frame** (inline Main.cpp:252-465): input poll (:260-273) → fixed 15 ms step
(`appTimeMs += 15`, :283-285) + scripted HUD demos (:287-331) → discrete movement gating
via `game.canPlayerStep` (:333-364) → E opens nearest door + `advanceTurnDoors` (:366-373)
→ camera follows player view (:380-382) → `world.drawSky` + `world.drawBSP` (:393-395)
→ HUD block compiled out via `kShowHud=false` (:400-401) → swap (:447);
auto-exit after 6000 frames (:463).

_Note:_ intended path `AppContext::run()` → `GameLoop::run()` exists
(AppContext.cpp:61-63, GameLoop.cpp:17-55) but nothing calls it.

## External dependencies & build

- CMake 3.22, target `DoomIIRPG`, C++17, sources via **GLOB_RECURSE** (reconfigure on add/remove).
- Link: SDL2, ZLIB, OpenGL (+ Apple OpenGL.framework). No GLEW/GLAD.
- All shaders are inline string literals (World3D.cpp:57-95, SpriteBatch.cpp:9-59).
- zlib only for zip raw-deflate (`inflateInit2 -15`, ZipArchive.cpp:128-140).
- BMP/font hand-rolled; no SDL_image/SDL_ttf.

## Known incomplete spots

1. Dead code path: `GameLoop` never called; Main.cpp carries its own ~210-line ad-hoc loop (structural debt to resolve when the real game state machine lands).
2. `AppContext::startup()` empty stub (AppContext.cpp:53-55).
3. ~~`Game::useDoorNear` declared but never defined~~ — resolved 2026-08-23 (replaced by `useDoorFacing`, see spec `specs/2026-08-23-fix-doors-sprite-placement.md`, ADR 0001).
4. Debug helper `saveIndexedBmp` unreferenced (World3D.cpp:16-55).
5. Hardcoded test values: fog (Main.cpp:207-209), FOV 290 (:225-227/:380-382), HUD demo fields (Hud.h:111-118), demo weapons mask (Hud.cpp:217-218), fake timestep `+=15` (Main.cpp:283).
6. `Graphics2D::setClip` records but scissor never applied (Graphics2D.cpp:20-24).
7. Spawn math duplicated for camera and player (Main.cpp:213-228 vs :234-246).
8. `SDL_WINDOW_ALWAYS_ON_TOP` leftover (Window.cpp:55-56).

## ADRs

_See [adr/](adr/):_

- [0001 — Faced-door use without a trace system](adr/0001-faced-door-use-without-trace.md) (2026-08-23)

## Specs

- [2026-08-23 — Fix "doors work incorrectly" + "sprites slightly shifted"](specs/2026-08-23-fix-doors-sprite-placement.md)
