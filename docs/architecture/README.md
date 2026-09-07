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
| `GameContext.h/.cpp` | Legacy-*Canvas* analog (phase 5, ADR 0003): state machine (`StateId{Playing=3,Loading=7,Dying=13}`, setState semantics with stateVars[9] reset + old/new hooks), clocks (`upTimeMs`, `gameTime`, `blockInputTime`), pending input actions, per-state ticks (two-phase Loading ordered per game-flow §3.3; Playing tick per GameStateRunner order incl. finishMovement FACE+ENTER+advanceTurn), render orchestration. Loot dwell (ADR 0006, spec `specs/2026-08-25-loot-dwell-ui.md`): ST_LOOTING crouch/dwell/stand phases on one clock, `handleLootingAction` (FIRE pages/closes, PASSTURN/BACK close, arrows scroll; guard drops input outside the dwell window) and `drawLootingMenu` overlay (red body + black title bar str227, 3×16 px line slots, shared scrollbar); grant runs at close before stand-up, `advanceTurn` at stand-up expiry. |
| `GameStates.h` | **(planned, ADR 0010 / spec `specs/2026-08-26-decomposition.md` P1-G1)** `StateId` + `Action` enums moved out of `GameContext.h`, plus the `StateHost` interface (`state()` / `requestState()`) — the only thing a module may know about the state machine. |
| `CinematicCamera.h/.cpp` | **(planned, P1-G2)** owns `MayaCamera` + the cinematic clock: `startCinematic`, `advanceCameraKey` parking, key boundaries/Snap, skip, `renderPose()` (nullptr = no cinematic owns the view — the single source of truth for fov / cockpit overlay / view-weapon suppression). |
| `LootSession.h/.cpp` | **(planned, P1-G3)** ST_LOOTING slice: crouch/dwell/stand pose clock, loot-pool session state, `handleAction`, loot menu overlay. |
| `MenuSession.h/.cpp` | **(planned, ADR 0013 / spec `specs/2026-08-28-menu.md` G3+)** the whole `ST_MENU` slice: retained menu state (current menu id/type, items copy, `selectedIndex`/`scrollIndex`, the 3-value nav stack of depth 10), `initMenu` per screen (file rows via `MenuData` + the code-built confirm/help/notebook bodies), `moveDir` wrap/clamp, page steps, `select`/`back`/`returnToGame`, and the per-frame `MenuViewModel` build (row composition, cursor oscillation, scroll pixels, scrollbar thumb). |
| `UiInputCollector.h/.cpp` | **(planned, ADR 0012, G3)** the single SDL -> `UiInput` normalizer (cursor in canvas coords, press/release edges, `Nav`, wheel) plus the moved keyboard->`Action` switch; keys and mouse both end up in `GameContext::pendingActions_`. |
| `PlayerActions.h/.cpp` | **(planned, P1-G7)** playing input (move/turn/use/fire commit); **(planned, ADR 0016)** the `Use` chain hoists the fire-target election above the front-tile script so the crate open (elect → crate → script → door → fire) sits where the original has it, and the elected hit + distance are computed once and reused, arrival hooks `finishMovement`/`finishRotationFired`, `flagForFacingDir`, `spawnPlayer`, the script GOTO handshake fields. |

### domain/game/
| File | Responsibility |
|---|---|
| `Entity.h` | World entity record: `EntityDef*`, monster ptr, tile linked-list ptrs, packed sprite index in `info` low bits (:39-40), flag bits (:22-26). |
| `Enums.h` | Legacy constants: entity types (:10-25), trace masks (:28-37), stat slots (:40-48), doors (:51-56), sprite flags (:63-72), monster anim/flags (:75-106); phase-5 additions: EV_* opcode ids, EVAL_* terms, EVFL_* trigger masks, SCR_* static-func indices. |
| `CombatEntity.h/.cpp` | Battle-stat block (8 slots + weapon); clamped set/add, XP calc (:52-54); Stage-1 combat adds verbatim `calcCombat/calcHit/calcDamage` taking `Combat&` (spec `specs/2026-08-26-combat-stage1.md`). |
| `Combat.h/.cpp` | Combat subsystem instance on `Game::combat` (ADR 0008): weapon-field constants + `tileDistances`, monsterTemplates[51], `performAttack` → two-stage hitscan `playerSeq` timer (`tick()`), `explodeOnMonster` subset, `getWeaponTileNum`; Env wired from Main. **(planned, ADR 0017 / spec `specs/2026-08-31-weapon-branch.md` G1)** the degenerate projectile pair `launchProjectile()` / `updateProjectile()` (+ `exploded` / `missileAnim`) replaces the inlined stage-0 hit application, unblocking every `PROJTYPE -1` weapon (chainsaw, sentry bots); `flagForWeapon` (G4); real missiles are G5. |
| `EntityMonster.h` | Monster payload struct (ce, ring links, nextAttacker/target, frameTime, flags, goal fields) pooled [80] on Game with active/inactive circular rings via `Game::activate/deactivate` (ADR 0008). |
| `Player.h/.cpp` | Player state: stats, inventory[26]/ammo[9]/weapon bitmask, XP; discrete grid movement via `kViewStepValues` 8-dir table (:8-11). **(planned, spec `specs/2026-08-31-weapon-branch.md` G1-G2)** `fireWeapon` refuses only `PROJTYPE > 0`; `modifyStat` + `usedChainsaw` (strength-growth counter, screen shake, msg 241). |
| `Game.h/.cpp` | Simulation subset: 32x32 entityDb lists (Game.h:69), door anims (6 slots), faced-door use `useDoorFacing` (ADR 0001), faithful swept-capsule move trace `traceMove` per ADR 0002 / spec `specs/2026-08-23-faithful-player-collision.md`, turn-advance auto-close with tile-granular occupancy, linked-state door solidity. Phase-5 additions: `setLineLocked` (tileNum bit0 flip + def re-lookup by tileNum+257), `advanceTurn` subset, eventFlags movement masks, `findEntityBySprite`, blocking-door-open thread resume via `DoorAnim::ownerThread`. **(planned, ADR 0015/0016 / spec `specs/2026-08-30-blocking-crates-shelf-pickup.md`)** `loadEntities` becomes the verbatim legacy spawn rule (entity for every sprite with a def + def-less sprite-wall fallback + oriented link-tile nudge + per-family `initspawn` marks, hidden sprites spawned-but-unlinked), plus `openCrate()` (arm `param`, unlink immediately) and the crate frame stepper inside `update()` on the `SpriteLerps` clock; `numDestroyableObj` / `lootSource` counters. **(planned, ADR 0018 / spec `specs/2026-08-31-weapon-branch.md` G3-G4)** `entityPain` / `entityDied` dispatchers on `eType` with the prop and corpse arms (`painProp` / `diedProp` / `diedCorpse`) living here, the monster arms delegating to `MonsterSystem`. Corpse loot (ADR 0006): `poolLootCorpse` marks + pools all eType-9 entities on a tile and composes the display lines (`Game::LootPool`: entries/credits/`Text` + line table), `giveLootPool` grants on UI close (replaced the interim auto-grant `lootCorpse`). |
| `Targeting.h/.cpp` | **(planned, ADR 0010 / spec `specs/2026-08-26-decomposition.md` P1-G4)** view forward vector, facing probe (health-bar feed), ordered fire-target election. |
| `TraceHit.h` | **(planned, ADR 0011)** typed trace result: `kind{None,World,Ent}` + entity + frac + resolved eType; `startsInside()` names the legacy `frac == -1` own-tile convention, `isWorld()` replaces `def == nullptr`. |
| `TraceSystem.h/.cpp` | **(planned, P2-GA)** capsule/line + capsule/circle traces, entityDb broadphase, world-line pass, `distFrom`, trace scratch state — moved out of `Game`. |
| `DoorSystem.h/.cpp` | **(planned, P2-GB)** door use/open/close/anim/auto-close, `DoorAnim` pool. |
| `MonsterSystem.h/.cpp` | **(planned, P2-GC)** monster payload pool + rings, activate/deactivate, pain/died/XP, corpsify. |
| `SpriteLerps.h/.cpp` | **(planned, P2-GD)** script sprite-lerp pool (LERP* opcodes), `vecToDir`, `spriteZBias`. |
| `CorpseLoot.h/.cpp` | **(planned, P2-GE)** loot-set defaults, corpse pooling/composition, grant pass. |
| `ItemPickup.h/.cpp` | **(planned, ADR 0014 / spec `specs/2026-08-29-world-item-pickup.md` G2)** world-item touch slice: `touched`/`touchedItem` per item class (inventory/food/ammo/weapon rolls, caps, starter ammo), HUD messages 83-87/223, `EntityDb::removeEntity` + SCR_ITEM_PICKUP hook; driven by `Game::touchTile` from `PlayerActions::finishMovement` only. |
| `EntityDb.h/.cpp` | **(planned, P2-GF)** entity vector + 32x32 tile lists, link/unlink/find, world & player slots. |
| `WeaponTable.h` | **(planned, ADR 0011, P3-C2)** typed row views over `tables->weaponData` (stride 9, `WeaponField` enum) and `tables->weaponInfo` (stride 6, `WeaponPose`), replacing `weapons[id*9+field]`. |
| `ScriptVM.h/.cpp` | Faithful tileEvents interpreter (phase 5, ADR 0003 / spec `specs/2026-08-23-phase5-skeleton.md` §7): 20-thread pool (`ScriptThread`: IP/FP/stackPtr/unpauseTime/type/flags/state), big-endian dispatch with the legacy post-opcode `++IP` contract, trigger filter, `executeTile`/`executeStaticFunc`/`runScriptThreads`, `scriptStateVars[128]`; TIER-A opcodes functional (EVAL/JUMP/CALL/RETURN/ITEM_COUNT/DOOROP/EVENTOP/GIVEITEM/WAIT/ABORT_MOVE/MESSAGE/TILE_EMPTY…); **(planned, spec `specs/2026-08-30-blocking-crates-shelf-pickup.md`)** `EV_GIVEITEM` mode 0 = sprite-touch grant through `EntityDb::findEntityBySprite` + `ItemPickup::touched` (the shelf/"look + Enter" pickup), and `EV_GIVELOOT` composes/grants the script loot dialog, TIER-B logged no-ops (incl. parse-only EV_CHANGE_MAP, non-pausing EV_DIALOG). |
| `DialogSystem.h/.cpp` | Faithful port of Canvas::dialogSystem (ADR 0004 / spec `specs/2026-08-24-intro-sequence.md` GROUP 1): styled bottom boxes, %NN text-arg composition, wrap + paging, typewriter reveal, style-2 help FIFO, ST_DIALOG input dispatch. `drawScrollBar` is the shared Canvas::drawScrollBar port; public since ADR 0006 so the loot menu overlay can reuse it. **(planned, G4 of spec `specs/2026-08-30-blocking-crates-shelf-pickup.md`)** `startDialogText(thread, Text&, style, flags, resume)` — the composed-buffer entry point `EV_GIVELOOT` needs. |

### domain/world/
| File | Responsibility |
|---|---|
| `MapData.h` | Parsed mapXX.bin container: header (:21-38), media (:41-49), BSP geometry (:52-62), sprites (:64-70), events/bytecode/flags (:73-76), maya cams (:79-86), decoded `Polygon`s (:96-104); **planned (spec `specs/2026-08-26-decomposition.md` P1-G1)**: `heightAt(x,y)` terrain accessor so modules stop calling back into `GameContext::getHeight`. |
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
| `MenuData/.cpp` | **(planned, ADR 0013, G1)** `menus.bin` parser: 72 rows (`id` / `type` / item span) + 426 item ints unpacked into `MenuItemDef{labelId,flags,action,param,helpId}`; lookup by legacy menu id, absent id = empty menu + one log line. |
| `Resources.h` | File-name constants for all archive entries. |
| `Tables/.cpp` | tables.bin offset table → 18 typed vectors (combat, keys, sin table, sky A/B...) (:25-68). |
| `ZipArchive/.cpp` | Minimal zip reader: central dir, case-insensitive lookup, stored + raw-deflate. |

### platform/
| File | Responsibility |
|---|---|
| `FileSystem` | Base/save paths via SDL_GetBasePath, archive search, whole-file read, atomic write. |
| `InputSystem` | SDL event pump → single callback (:5-15); action-mapping pipeline is future work. |
| `Window` | SDL2 window + GL 3.3 core context (Apple) / compatibility (:42-50); letterbox viewport (:159-169); canvas fixed 480x320 (:25-26). **(planned, ADR 0012, G1)** `screenToCanvas` (:171-176, no callers) replaced by `windowToDrawable`; the canvas half of the inverse lives in `RenderBackend`. |

### render/
| File | Responsibility |
|---|---|
| `Camera3D` | Faithful 14.14 fixed view/projection/MVP via 1024-entry sin table (:15-62); GLES BeginFrame projection tweaks (:83-92); float + int matrix accessors. |
| `Graphics2D` | Canvas-space 2D over SpriteBatch: rects/lines, blits with rotateModes 0-8, anchors, scaled draws, `drawString` with `^N` colors/buff icons; clip recorded but not applied (:20-24) — **(planned, ADR 0012, G1)** `setClip`/`clearClip` become a real GL scissor through `SpriteBatch::setScissorCanvas`, single level (nesting is `Ui::pushClip`'s job). |
| `RenderBackend` | Frame owner: beginFrame clear + letterbox viewport + batch begin (:37-47); endFrame flush+swap (:49-52); `setCanvasViewport/restoreCanvasViewport` scope the 3D band — ONE world rect (1,7,478,248) for gameplay and cinematics alike (ADR 0009 + its 2026-08-26 amendment: `src/GLES.cpp:119-127` hardcodes the GL y for both, the cinematic letterbox is the cockpit overlay at canvas y = 42, not a viewport); both flush the sprite batch first; **(planned, G1)** latches the letterbox rect into `SpriteBatch` and exposes its inverse (`letterboxRect`, `drawableToCanvas`) for cursor mapping. |
| `SceneRenderer` | **(planned, ADR 0010, P1-G6)** the world pass: viewport band, camera setup (player view or cinematic `MayaPose`), screen shake, per-sprite sort-bias/character classification, `drawSky`+`drawBSP`; owns `Camera3D`. |
| `World3D` | **(planned, ADR 0019 / spec `specs/2026-09-01-blend-modes.md`)** the full 14-row `RENDER_*` blend table (blend func + `uColorMod` modulation uniform + per-mode fog) behind `applyBatchState`'s flush-compare-assign cache, the tile-136 torchiere glow (extra tile-193 `ADD50` quad drawn before the lamp body, `drawTorchiereGlow`), the tile-212 `SUB` back-end override and the per-tile geometry mode in `drawPoly`. GL 3.3 world renderer: palette-LUT textures (:202+), sky (:399-454), polys (:329+), BSP walkNode painter's algorithm with per-leaf sprites (:788-881), billboard/wall/flat/slip-door sprites + RLE (:100+, :507-746), eye-space fog (:180-200), time animation (:78), per-sprite sort-bias hook on `drawBSP`. Stacked characters (ADR 0005): NPC branches + monster-family ATTACK deltas (ADR 0007, spec `specs/2026-08-26-monsters-stack-flicker.md`); floater/special-boss families still excluded. |

### render/api/
**(planned, ADR 0020 / spec `specs/2026-09-02-render-backend-split.md`)** target
`dr_render_core`: the backend-neutral render library. No GL, no SDL, no game deps.

| File | Responsibility |
|---|---|
| `TextureId.h` | Opaque `TextureId` (0 = invalid) + creation-time `TextureFlags{TransparentKey, Tiled}`. |
| `TextureStore.h` | Texture creation interface: `createIndexed(indices, w, h, palette565, count, flags)`, `createRgba`, `destroy`, `query`, `textureBytes`. Indexed data stays the currency because every shipped image is palette-indexed (`docs/original-code/image-formats.md` §0). |
| `Texture.h/.cpp` | Move-only RAII handle every caller keeps (`uploadIndexed(store, ...)`); caches w/h for the anchored blits. Replaces `render/gl/Texture.h` for `ui/`, `text/`, `render/World3D`. |
| `Draw2D.h` | 2D device: `drawQuad`, `fillQuad`, `setRenderMode`, `setClipCanvas`/`clearClip`, `flush`. Derived from the actual `Graphics2D`/`Ui`/`Font` call inventory. |
| `Scene3D.h` | 3D device: `beginScene(SceneView{mvp, view})`, `setTexture`, `setRenderMode`, `submitTriangles(WorldVertex*, n)`, `setFog`, `drawSky(tex, uOffset)`, `endScene`. No shaders, no matrices as uniforms, no depth buffer. |
| `RenderBackend.h/.cpp` | Abstract frame/device owner (same method names as the old concrete class) + `requestCapture(path)`, `draw2d()`, `scene3d()`, `textures()`, `name()`. |
| `RenderModes.h/.cpp` | The 14-row legacy `RENDER_*` table in neutral `BlendFactor` enums + `clampRenderMode`/`renderModeNeedsWarning` (moved out of `World3D.cpp:145-201`, ADR 0019). |
| `PixelConvert.h/.cpp` | RGB565 -> RGBA8 bit replication, palette expansion with the `0xF81F` key, indexed -> RGBA expansion (shared by the GL LUT and the SDL store). |
| `QuadUV.h/.cpp` | Legacy `rotateMode` 0..8 -> quad UV permutation, extracted from `SpriteBatch::draw` so backends cannot drift. |
| `CanvasViewport.h/.cpp` | Letterbox rect + canvas<->drawable mapping (moved from `Window::computeViewport` / `RenderBackend::drawableToCanvas`), shared so input mapping is bit-identical across backends. |
| `BmpWriter.h/.cpp` | 24-bit BMP dump for F12 frame capture (replaces the unreferenced `saveIndexedBmp` debug helper). |

### render/sdl/
**(planned, ADR 0021)** target `dr_render_sdl`: `SdlTextureStore` (measured 2026-09-02:
SDL2 rejects `INDEX8` on every driver, so it expands once via an INDEX8 surface into a
runtime-negotiated 32-bit format — ~40 MB for the whole world texture set; SDL3 supports
`INDEX8` + `SDL_SetTexturePalette` natively and would keep the 9.7 MB indexed pipeline,
which is why the interface never speaks RGBA — see spec §4.2.0-§4.2.3 for the fork and
the required palette-swap measurement), `SdlDraw2D` (everything through
`SDL_RenderGeometry` + a 1x1 white texture for fills, canvas-space clip rect),
`SdlScene3D` (CPU vertex pipeline: near clip, NDC -> canvas viewport, object-space UV
tile split because SDL textures are CLAMP_TO_EDGE, batching by texture+mode,
per-vertex fog + haze pass; **planned, ADR 0023 / spec
`specs/2026-09-07-sdl-tessellation.md`**: adaptive `n x n` tessellation of the
near-clipped triangles against the affine warp — criterion = peak affine displacement in
canvas pixels, cut by averaging clip-space vertices, `n = ceil(sqrt(M/2px))` capped at 8,
downstream of the tile split; env knobs `DOOM2RPG_SDL_TESS` / `DOOM2RPG_GFX_STATS`), `SdlBlendModes` (14 rows -> `SDL_BlendMode`, custom mode
for `RENDER_SUB`, `RENDER_NONE` = skip), `SdlRenderBackend`.

### render/gl/
| File | Responsibility |
|---|---|
| `GlCommon.h` | Platform GL header selection (:4-11). |
| `Shader` | RAII program wrapper, uniform setters incl. mat4. |
| `GlDraw2D` | **(planned, ADR 0020, G2)** the renamed `SpriteBatch` implementing `Draw2D`; `GlTextureStore` owns the `GlTexture` slots behind `TextureId`; `GlScene3D` gets the world shader/VAO/VBO/fog/sky moved out of `World3D.cpp`; `GlRenderBackend` is the old concrete `RenderBackend`. Target `dr_render_gl`. |
| `SpriteBatch` | Batched quad renderer (4096 max, :18-19); three programs: indexed-palette, RGBA, flat color (:64-66). **(planned, ADR 0012, G1)** canvas-space scissor (`setLetterbox`/`setScissorCanvas`/`clearScissor`) following the `setBlendMode` compare-flush-assign precedent (:145-149); GL y measured from the bottom. |
| `Texture` | Move-only texture: indexed R8 + RGBA8 palette LUT with 0xF81F kill, optional GL_REPEAT (:33), plain RGBA8 (:37). |

### text/
| File | Responsibility |
|---|---|
| `Font` | Font.bmp atlas: 12x16 glyphs, advance 9 (:18-20), `^N` color table (:23-26), char→glyph mapping (:43). |
| `Text` | Mutable text buffer port of legacy Text: append/find/edit, wrap with break char (:58-62), width measure (:67-69). |

### ui/
| File | Responsibility |
|---|---|
| `ViewWeapon` | First-person weapon quad + muzzle flash: legacy viewport-relative anchors magnified by the live projection, hand-clipped to the world band by hand (quad + source sub-rect — scissor cannot reproduce it). **(planned, ADR 0012, G6)** takes a `ViewWeaponModel` instead of reading `Player`/`Combat`; the `flashDone` latch moves to `Combat::tick`. |
| `Hud` | Loads ~24 BMP textures (`startup()` :72-106); status bars, cockpit, weapon select (:211-233), bubbles, vignette (:136-169), arrows, monster bar (:180-209), messages, bottom bar (:533-565). Demo fields still exist (`Hud.h:166-172`). **(planned, ADR 0012, G4)** all `draw*` move to `HudView`; `Hud` keeps only the non-drawing producer runtime (shake, monster-bar drain, cockpit toggle). |
| `UiTypes.h` | **(planned, ADR 0012, G2)** `UiId` / `UiAction` / `Nav` enums, `UiInput`, `UiRect`, `UiResult`. |
| `UiState.h/.cpp` | **(planned, G2)** the ONLY retained UI state: `activeId`, per-region scroll offsets, text wrap cache (`<start,len>` tables). Nothing else may be added. |
| `Ui.h/.cpp` | **(planned, G2)** immediate-mode primitives (`panel`, `image`, `number3`, `label`, `textRows`, `textBlock`, `button`, `softKey`, `listHit`, `scrollBar`, `face`, `weaponIcon`, `keys`, `pushClip`) + the press/release hit-test rule. All constants come from the caller. `scrollBar` is the moved `DialogSystem::drawScrollBar`. **(planned, spec `specs/2026-08-28-menu.md` G2)** `+ glyph(char,...)` (the legacy `drawCursor`) and `+ scrollBarMenu(barRect, thumbOffset, thumbLen)` — the second scrollbar style (`gameMenu_ScrollBar` + three sliders), which is a different widget from `scrollBar`. |
| `UiAssets.h/.cpp` | **(planned, G2)** owner of the UI BMP sheets (moved `Hud::startup`), loaded through an injected `ResourceReader` so `ui/` stops including `core/AppContext.h`. |
| `HudModel.h` | **(planned, G4)** `HudModel` / `TextSlot` / `MonsterBarModel` — per-frame structs rebuilt by `GameContext`, never stored. |
| `HudView.h/.cpp` | **(planned, G4)** `UiResult drawHud(Ui&, const HudModel&)`; bottom-bar geometry per `docs/original-code/ui.md` §1-§6. |
| `LootView.h/.cpp` | **(planned, G5)** `drawLootList` — moved verbatim from `LootSession::draw`; 3×16 px rows, no scissor. |
| `DialogView.h/.cpp` | **(planned, G7)** `drawDialog` — the drawing half of `DialogSystem` only. |
| `MenuModel.h` | **(planned, spec `specs/2026-08-28-menu.md` G3)** `MenuRow` + `MenuViewModel`: menuRect/clip/itemWidth geometry, ALL rows with their resolved heights, `scrollPx`, `selectedRow`, cursor offset, scrollbar thumb, chrome flags and the two soft-key labels. No menu id, no `StateId`. |
| `MenuView.h/.cpp` | **(planned, G3+)** `UiResult drawMenu(Ui&, const MenuViewModel&)`: opaque background + bottom panel + health/shield readout + soft keys, then the clipped row walk (296x32 plates at 25 %/100 % alpha, `'\x8A'` cursor glyph, right-aligned value column) and the menu's own 4-sheet scrollbar. |

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
- All shaders are inline string literals (World3D.cpp:57-95, SpriteBatch.cpp:9-59) and stay **inside the backend** after the split (ADR 0020).
- **(planned, spec `specs/2026-09-02-render-backend-split.md`)** four targets: `dr_render_core` (interfaces + neutral helpers), `dr_render_gl`, `dr_render_sdl`, `DoomIIRPG` (glob filtered with `list(FILTER ... EXCLUDE REGEX "/render/(api|gl|sdl)/")`); backend chosen at startup by `--backend=gl|sdl`.
- zlib only for zip raw-deflate (`inflateInit2 -15`, ZipArchive.cpp:128-140).
- BMP/font hand-rolled; no SDL_image/SDL_ttf.

## Known incomplete spots

1. ~~Dead code path: `GameLoop` never called~~ — resolved 2026-08-23 (phase 5: GameLoop revived as the fixed-step driver; see spec `specs/2026-08-23-phase5-skeleton.md`, ADR 0003).
2. `AppContext::startup()` empty stub (AppContext.cpp:53-55).
3. ~~`Game::useDoorNear` declared but never defined~~ — resolved 2026-08-23 (replaced by `useDoorFacing`, see spec `specs/2026-08-23-fix-doors-sprite-placement.md`, ADR 0001).
4. Debug helper `saveIndexedBmp` unreferenced (World3D.cpp:16-55).
5. Hardcoded test values: fog (Main.cpp:207-209), FOV 290 (:225-227/:380-382), HUD demo fields (Hud.h:111-118), demo weapons mask (Hud.cpp:217-218), fake timestep `+=15` (Main.cpp:283).
6. `Graphics2D::setClip` records but scissor never applied (Graphics2D.cpp:20-24) — callers must clip by hand (`ViewWeapon`); scheduled as GROUP 1 of spec `specs/2026-08-27-ui-layer.md` (ADR 0012). `ViewWeapon`'s hand clipping stays on purpose (deviation D1).
7. Spawn math duplicated for camera and player (Main.cpp:213-228 vs :234-246).
8. `SDL_WINDOW_ALWAYS_ON_TOP` leftover (Window.cpp:55-56).

## ADRs

_See [adr/](adr/):_

- [0001 — Faced-door use without a trace system](adr/0001-faced-door-use-without-trace.md) (2026-08-23)
- [0002 — Faithful swept-capsule collision trace](adr/0002-faithful-player-collision-trace.md) (2026-08-23; amends 0001)
- [0003 — GameContext state machine + standalone ScriptVM](adr/0003-game-context-state-machine-and-script-vm.md) (2026-08-23)
- [0004 — Dialog module and cinematic states](adr/0004-dialog-module-and-cinematic-states.md) (2026-08-24)
- [0005 — Character detection & stacked-billboard rendering boundary](adr/0005-character-detection-stacked-billboards.md) (2026-08-25)
- [0006 — Loot dwell UI without a LootingSystem module](adr/0006-loot-ui.md) (2026-08-25)
- [0007 — Monsters join the entity-driven stacked-character path](adr/0007-monsters-stacked-character-path.md) (2026-08-26; amends 0005)
- [0008 — Combat module placement and monster payload structs](adr/0008-combat-module-and-monster-payload.md) (2026-08-26)
- [0009 — Restore the legacy world viewport (canvas 1,7,478,248)](adr/0009-legacy-world-viewport.md) (2026-08-26; amended 2026-08-26 — deviation D1 refuted, cinematics share the same viewport, aspect 163)
- [0010 — Decompose `GameContext` and `Game` into peer modules with narrow `Env` injection](adr/0010-module-decomposition-and-injection.md) (2026-08-26)
- [0011 — Typed trace result (`TraceHit`) and named legacy encodings](adr/0011-typed-trace-result-and-named-encodings.md) (2026-08-26)
- [0012 — Immediate-mode UI layer with per-frame models, and no game logic in `ui/`](adr/0012-immediate-mode-ui-layer.md) (2026-08-27)
- [0013 — The in-game menu tree is parsed from `menus.bin`, not hardcoded](adr/0013-menu-tree-from-menus-bin.md) (2026-08-28)
- [0014 — World item pickup as a peer subsystem, entities stay passive](adr/0014-world-item-pickup-subsystem.md) (2026-08-29)
- [0015 — Spawn an entity for every sprite with a def (drop the family whitelist)](adr/0015-full-legacy-entity-spawn-rule.md) (2026-08-30)
- [0016 — Crate opening is simulation state, not renderer state](adr/0016-crate-opening-in-the-simulation.md) (2026-08-30)
- [0017 — The projectile system is introduced degenerate-first (`launchProjectile`/`updateProjectile` from day one)](adr/0017-projectile-model-degenerate-first.md) (2026-08-31)
- [0018 — `Entity::pain`/`Entity::died` become one dispatcher pair on `Game`](adr/0018-entity-pain-died-dispatch.md) (2026-08-31)
- [0019 — Blend modes are one table; color modulation is a per-draw `uColorMod` uniform](adr/0019-blend-mode-table-and-color-mod-uniform.md) (2026-09-01)
- [0020 — The graphics backend is a library behind two need-shaped interfaces (`Draw2D`/`Scene3D`)](adr/0020-render-backend-two-interfaces.md) (2026-09-02)
- [0021 — SDL_Render is a full second backend, 3D view included (affine mapping accepted)](adr/0021-sdl-render-second-backend-with-3d.md) (2026-09-02)
- [0022 — Fog is a backend-dependent effect (per-pixel on GL, per-vertex on SDL)](adr/0022-fog-is-backend-dependent.md) (2026-09-02)
- [0023 — The affine warp is fought by adaptive tessellation, measured in screen space and cut in clip space (SDL only)](adr/0023-sdl-clip-space-adaptive-tessellation.md) (2026-09-07)

## Specs

- [2026-08-23 — Fix "doors work incorrectly" + "sprites slightly shifted"](specs/2026-08-23-fix-doors-sprite-placement.md)
- [2026-08-23 — Faithful player collision (swept-capsule trace)](specs/2026-08-23-faithful-player-collision.md)
- [2026-08-23 — Phase 5 skeleton: game-state machine + tileEvents ScriptVM](specs/2026-08-23-phase5-skeleton.md)
- [2026-08-24 — Intro sequence (dialogs v2, cinematics, corpse loot)](specs/2026-08-24-intro-sequence.md)
- [2026-08-25 — Character animation (stacked-billboard humans, walk cycles, squad)](specs/2026-08-25-character-animation.md)
- [2026-08-25 — Interactive loot dwell + loot menu UI (ST_LOOTING)](specs/2026-08-25-loot-dwell-ui.md)
- [2026-08-26 — Monsters join the stacked path + walk-flicker fix (delta on 2026-08-25 character animation)](specs/2026-08-26-monsters-stack-flicker.md)
- [2026-08-26 — Combat Stage 1 (fire pipeline, damage math, monster payload, HUD feed)](specs/2026-08-26-combat-stage1.md)
- [2026-08-26 — Cinematic camera key-0 fix: legacy tri-state startup (delta on 2026-08-24 intro sequence)](specs/2026-08-26-camera-key0.md)
- [2026-08-26 — Combat Stage 1 fixes: facing probe, fire-target election, world viewport + view weapon (supersedes parts of the combat-stage1 spec)](specs/2026-08-26-combat-stage1-fixes.md)
- [2026-08-26 — Decomposition: GameContext + Game split, typed TraceHit, named legacy encodings](specs/2026-08-26-decomposition.md)
- [2026-08-27 — UI layer: immediate-mode framework (`Ui`/`UiState`/models) + migration of HUD, loot list, dialogs, view weapon](specs/2026-08-27-ui-layer.md)
- [2026-08-28 — In-game menu (`ST_MENU`): `menus.bin` loader, `MenuSession`, `MenuView`, the menu scrollbar and the cursor glyph (GROUP 8 of the UI layer)](specs/2026-08-28-menu.md)
- [2026-08-29 — World item pickup: item entity spawn, `Game::touchTile`, `ItemPickup::touched/touchedItem`, `Player::give` legacy semantics](specs/2026-08-29-world-item-pickup.md)
- [2026-08-30 — Shelf pickup (`EV_GIVEITEM` mode 0), full blocking-sprite spawn, crates (open/animate/unlink), `EV_GIVELOOT`](specs/2026-08-30-blocking-crates-shelf-pickup.md)
- [2026-09-01 — Blend-mode table, `uColorMod` modulation, torchiere glow](specs/2026-09-01-blend-modes.md)
- [2026-09-02 — Render backend split: `dr_render_core` + GL and SDL_Render implementations (`Draw2D`/`Scene3D`, backend flag, frame capture)](specs/2026-09-02-render-backend-split.md)
- [2026-09-07 — SDL path: adaptive triangle tessellation against the affine warp (G7.1-G7.3)](specs/2026-09-07-sdl-tessellation.md)
