# new_src architecture

_Audit date: 2026-08-22. All citations are `new_src/<file>:<line>`. 58 source files,
~5,260 LOC across `core/ domain/ graphics/ io/ platform/ render/ text/ ui/`.
Namespace for everything: `newcore`._

## Module map

### core/
| File | Responsibility |
|---|---|
| `Main.cpp` | Entry point (`main()` :36): data-archive init + loader smoke logs, constructs subsystems and `GameContext`, then `AppContext::run()`. (Ad-hoc inline loop removed by phase 5 — spec `specs/2026-08-23-phase5-skeleton.md`.) |
| `AppContext.h/.cpp` | Composition root: Window, RenderBackend, InputSystem, ZipArchive (:41-44); `readResource()` applies archive prefix `Payload/Doom2rpg.app/Packages/` (AppContext.h:22, AppContext.cpp:57-59); `run()` delegates to `GameLoop::run`. |
| `GameLoop.h/.cpp` | Fixed-step driver: accumulates clamped dt (≤125 ms), consumes 15 ms quanta calling `GameContext::tick()`, renders once per frame (revived in phase 5, ADR 0003). |
| `GameContext.h/.cpp` | Legacy-*Canvas* analog (phase 5, ADR 0003): state machine (`StateId{Playing=3,Loading=7,Dying=13}`, setState semantics with stateVars[9] reset + old/new hooks), clocks (`upTimeMs`, `gameTime`, `blockInputTime`), pending input actions, per-state ticks (two-phase Loading ordered per game-flow §3.3; Playing tick per GameStateRunner order incl. finishMovement FACE+ENTER+advanceTurn), render orchestration. |

### domain/game/
| File | Responsibility |
|---|---|
| `Entity.h` | World entity record: `EntityDef*`, monster ptr, tile linked-list ptrs, packed sprite index in `info` low bits (:39-40), flag bits (:22-26). |
| `Enums.h` | Legacy constants: entity types (:10-25), trace masks (:28-37), stat slots (:40-48), doors (:51-56), sprite flags (:63-72), monster anim/flags (:75-106); phase-5 additions: EV_* opcode ids, EVAL_* terms, EVFL_* trigger masks, SCR_* static-func indices. |
| `CombatEntity.h/.cpp` | Battle-stat block (8 slots + weapon); clamped set/add, XP calc (:52-54). No calcHit/calcDamage yet. |
| `Player.h/.cpp` | Player state: stats, inventory[26]/ammo[9]/weapon bitmask, XP; discrete grid movement via `kViewStepValues` 8-dir table (:8-11). |
| `Game.h/.cpp` | Simulation subset: 32x32 entityDb lists (Game.h:69), door anims (6 slots), faced-door use `useDoorFacing` (ADR 0001), faithful swept-capsule move trace `traceMove` per ADR 0002 / spec `specs/2026-08-23-faithful-player-collision.md`, turn-advance auto-close with tile-granular occupancy, linked-state door solidity. Phase-5 additions: `setLineLocked` (tileNum bit0 flip + def re-lookup by tileNum+257), `advanceTurn` subset, eventFlags movement masks, `findEntityBySprite`, blocking-door-open thread resume via `DoorAnim::ownerThread`. |
| `ScriptVM.h/.cpp` | Faithful tileEvents interpreter (phase 5, ADR 0003 / spec `specs/2026-08-23-phase5-skeleton.md` §7): 20-thread pool (`ScriptThread`: IP/FP/stackPtr/unpauseTime/type/flags/state), big-endian dispatch with the legacy post-opcode `++IP` contract, trigger filter, `executeTile`/`executeStaticFunc`/`runScriptThreads`, `scriptStateVars[128]`; TIER-A opcodes functional (EVAL/JUMP/CALL/RETURN/ITEM_COUNT/DOOROP/EVENTOP/GIVEITEM/WAIT/ABORT_MOVE/MESSAGE/TILE_EMPTY…), TIER-B logged no-ops (incl. parse-only EV_CHANGE_MAP, non-pausing EV_DIALOG). |

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
→ data loaders (Tables, EntityDefs, Localization, MediaLoader, MapParser→MapData,
Font, Hud, World3D + textures + sky) → construct `GameContext` with pointers to all
subsystems → `app.run()`.

**Per frame** (GameLoop): input poll → accumulate clamped dt → per 15 ms quantum:
clock advance (gameTime only in Playing) → input gate (blockInputTime /
script-block) → action dispatch → globals (`setPlayerPos`, `runScriptThreads`)
→ state tick (Loading two-phase / Playing order / Dying stub) → render once:
camera from player view (+160-unit pull-back), `drawSky`+`drawBSP`, HUD messages, swap.
Movement decisions live in the Playing tick (`traceMove` before commit; leave-events
before the trace; FACE+ENTER+advanceTurn on arrival); E = faced-tile TRIGGER event
first, then door use.

## External dependencies & build

- CMake 3.22, target `DoomIIRPG`, C++17, sources via **GLOB_RECURSE** (reconfigure on add/remove).
- Link: SDL2, ZLIB, OpenGL (+ Apple OpenGL.framework). No GLEW/GLAD.
- All shaders are inline string literals (World3D.cpp:57-95, SpriteBatch.cpp:9-59).
- zlib only for zip raw-deflate (`inflateInit2 -15`, ZipArchive.cpp:128-140).
- BMP/font hand-rolled; no SDL_image/SDL_ttf.

## Known incomplete spots

1. ~~Dead code path: `GameLoop` never called~~ — resolved 2026-08-23 (phase 5: GameLoop revived as the fixed-step driver; see spec `specs/2026-08-23-phase5-skeleton.md`, ADR 0003).
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
- [0002 — Faithful swept-capsule collision trace](adr/0002-faithful-player-collision-trace.md) (2026-08-23; amends 0001)
- [0003 — GameContext state machine + standalone ScriptVM](adr/0003-game-context-state-machine-and-script-vm.md) (2026-08-23)
- [0005 — Character detection & stacked-billboard rendering boundary](adr/0005-character-detection-stacked-billboards.md) (2026-08-25)

## Specs

- [2026-08-23 — Fix "doors work incorrectly" + "sprites slightly shifted"](specs/2026-08-23-fix-doors-sprite-placement.md)
- [2026-08-23 — Faithful player collision (swept-capsule trace)](specs/2026-08-23-faithful-player-collision.md)
- [2026-08-23 — Phase 5 skeleton: game-state machine + tileEvents ScriptVM](specs/2026-08-23-phase5-skeleton.md)
- [2026-08-24 — Intro sequence (dialogs v2, cinematics, corpse loot)](specs/2026-08-24-intro-sequence.md)
- [2026-08-25 — Character animation (stacked-billboard humans, walk cycles, squad)](specs/2026-08-25-character-animation.md)
