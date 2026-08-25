# Status

_Last updated: 2026-08-23_

## Working on

- **Next unit: INTRO SEQUENCE** (user-picked 2026-08-23). Target: game start
  plays like the original — NPC dialog gives the gun → scripted move/cutscene
  into the spawn room → tutorial (corpse + loot). Step outline:
  1. Research: camera/cutscene states (ST_CAMERA/ST_INTER_CAMERA), full
     DialogSystem (styles/speakers/paging), loot & inventory flow
     (EV_MAKE_CORPSE/ASSIGN_LOOTSET/pickup).
  2. Architect spec (+ADR if needed) → coder groups → review → user eyes.
  Already in place from Phase 5: ScriptVM core, dialog-lite, EV_GIVEITEM,
  lazy textures, state machine.
- **Known bugs (deferred):**
  - **Camera judder in cinematics** — persists after MayaCamera tween-window
    fix + render-locked pacing; needs deeper investigation (suspects: key-boundary
    Snap truncation feel, shake randomization cadence, vsync-vs-sim drift ~10%).
  - **Hangar door (13,19) doesn't open on E** — silent unlock lives in evt#56
    @tile(10,19) BEHIND the blue door; user clarifies the shaft is behind the
    blue door and the hangar door is elsewhere — re-map which door/tile actually
    gates it before fixing.
  - Subtitles: CAMERA_STR bit14 showCinPlayer not portable yet; portrait art
    fallback for style 8.
- Backlog after intro: monsters/AI/combat; automap; save/load; EV_CHANGE_MAP
  real transitions; renderMode blending; stderr [script]/[load] log polish.

## Fidelity pass 2026-08-23 (doors + sprites)

Implemented A1–A8 + B1–B5 from the spec above across `Game.{h,cpp}`,
`World3D.{h,cpp}`, `Main.cpp`; reviewer PASS. Key behavior now matching legacy:
slip-door vertical split, slide-door UV pinning, DOORLERP lifetime, solidity
timeline, faced-door trigger (Chebyshev ≤ 1 tile), open-frame texture,
tile-granular auto-close occupancy, camera pull-back nudge, portal-eye z range,
billboard UV flips, FLAT plane branch, height-snapped sort keys with bias chain.
Follow-up fixes same day: media REFERENCE record resolution (vanishing green
doors) and faithful player collision trace (walls solid; flag semantics 0–7).
All user-verified.
Still missing (unchanged): keycard unlock path was delivered by Phase 5 scripts;
monster-blocks-close, door sounds, water streams remain open.

## Feature audit vs PLAN.md (2026-08-22)

Full module/wiring details: [architecture/README.md](architecture/README.md).

### Phases 1–2.7 — essentially complete

| Item | Status | Evidence |
|---|---|---|
| AppContext + .ipa (zlib), SDL2 window, GL 3.3 | done | core/AppContext.cpp:24-51, platform/Window.cpp:34-82, io/ZipArchive.cpp:128-141 |
| RenderBackend batch+shaders, letterbox 480x320 | done | render/RenderBackend.cpp:16-57, Window.h:25-26 |
| Font.bmp, Graphics2D, full HUD set | done | text/Font.cpp:7-97, render/Graphics2D.cpp, ui/Hud.cpp (**demo-fed values**, Hud.h:112-118) |
| newMappings/newPalettes/newTexels → MediaLoader | done | io/Media.cpp:10-153 (byte-exact fix verified, see PLAN.md) |
| MapParser full mapXX.bin + decodePolys (1787 polys map00) | done | domain/world/MapParser.cpp:13-298 |

### Phase 3 — one sub-item open

| Item | Status | Evidence / gap |
|---|---|---|
| Camera3D 14.14 fixed → float MVP | done | render/Camera3D.cpp:15-103 |
| World3D shader/fan/indexed textures/sky/fog/sprites/doors | done | render/World3D.cpp (see architecture README) |
| BSP traversal | **partial** | `cullBoundingBox` NOT ported — walks every node (World3D.cpp:789-793). PLAN's "simplified 2D-reject" claim does not match code. |
| drawNodeGeometry faceCull/swapXY/expandEdgePoly | **open** | edge expansion lives in MapParser (:206-234); GL path draws as-is; poly-flag culling unported |
| Texture animation (lava UV, AUTO_ANIMATE) | partial | World3D.cpp:363-369, :520-522; fire sprites lack the bit |
| Camera controls | done | Main.cpp:260-273; movement is now *discrete* legacy-style (90° turns, 1-tile steps), not continuous strafe as PLAN describes |

### Phase 4 — mixed

| Item | Status | Evidence / gap |
|---|---|---|
| Enums.h, CombatEntity, Entity, Player, Game entityDb/link/unlink | done | domain/game/* (Player classes: only marine defaults, Player.h:69) |
| Doors: load/open/close/E-key/auto-close | done | Game.cpp:43-74, :78-96, :130-245; locked doors refuse w/o key check (:100) |
| Player collisions (trace) | **partial — ahead of plan** | CapsuleToLineTrace + canPlayerStep exist in simplified form (Game.cpp:263-337); no CONTENTS-masked entityDb trace. PLAN checkbox should flip. |
| Discrete cell movement | **partial — ahead of plan** | Player.cpp:13-72 wired ad-hoc in Main.cpp:333-384, not inside a Game-driven advanceTurn. PLAN checkbox should flip. |
| loadEntities from TILE sprites | partial | creates **door entities only** (tile range filter Game.cpp:61); items/monsters/NPC/decor never instantiated |
| Monsters (EntityMonster/AI/activation) | **missing** | fwd-decl only (Entity.h:12); enums pre-staged (Enums.h:75-106) |
| Item pickup (touchedItem/give) | **missing** | Player::give exists (Player.cpp:95) but zero call sites from world pickup |
| Weapons/combat calcHit/calcDamage | **missing** | 0 hits; data ready but unused: io/Tables.cpp, io/EntityDefs.cpp |

### Phase 5 — missing

No game-state machine/worlds/tileEvents VM/dialogs/combat. `GameLoop` is an unused stub
(core/GameLoop.cpp:17-55); the loop is hand-rolled in Main.cpp:281-464.
tileEvents/bytecode parsed and stored but not interpreted (domain/world/MapData.h:73-75).

## Biggest gaps (priority hints)

1. Phase 5 skeleton: real game-state machine replacing the Main.cpp monolith.
2. Monsters: EntityMonster class + AI + activation + rendering.
3. Combat/weapons: consume tables.bin/entities.bin data (calcHit/calcDamage).
4. Item pickup: touchedItem → Player::give; extend loadEntities beyond doors.
5. BSP cullBoundingBox port (or documented decision to skip).
6. PLAN checkboxes lag the code: collisions & discrete movement are partially implemented.

## Next steps

- Decide next unit of work (suggest: PLAN checkbox sync + Phase 4 monsters or game-state skeleton).
