# Status

_Last updated: 2026-08-25 (evening)_

## Working on

- **User acceptance batch (map00 intro area)** — implemented + reviewed, AWAITING
  USER EYES on the fresh build:
  1. Lift cutscene end scene: squad imps 185-188/grenade hidden via NOENTITY
     hidden-bit path; corpse prop 105 lies at (5,19) + standing NPC beside it
     (= faithful); imp corpse at (3,26). If "legs" persist → capture stderr.
  2. Loot dwell: E on corpse → 500ms crouch → red "Looted Items:" top panel,
     E pages/closes, TAB/BACK closes, grant on close, stand-up, turn consumed.
     Spec docs/architecture/specs/2026-08-25-loot-dwell-ui.md, ADR-0006;
     reviewer PASS (see journal 2026-08-25 review entry).
  3. Blue door sp22: opens with card. "Doesn't let through" — root cause NOT yet
     proven; self-disable replay refuted (eventMatches honors bit19). Need one
     stderr run: `[dbg] moveBlocked … by spr=N type=T` names the blocker;
     `[script] DOOROP sprite=22` close pair would name a script.
- **Deferred by user:** hangar door unlock chain (item dropped this cycle).
- Fire additive flicker + scorch stains: done, user-confirmed matches original.
- **Known bugs (deferred):**
  - Camera judder in cinematics (unchanged).
  - Subtitles: CAMERA_STR bit14 showCinPlayer not portable yet; portrait art
    fallback for style 8.
- Backlog: monsters/AI/combat; automap; save/load; EV_CHANGE_MAP real
  transitions; renderMode blending leftovers; stderr log polish.

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

STALE as of 2026-08-26: the state machine, tileEvents VM, dialogs and combat all exist now
(`core/GameContext.cpp`, `domain/game/ScriptVM.cpp`, `domain/game/DialogSystem.cpp`,
`domain/game/Combat.cpp`), and `GameLoop` is wired (`core/AppContext.cpp:62`), not a stub.
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

## 2026-08-26 — playtest fixes in progress (see docs/journal.md for the full entry)

Camera key-0 CONFIRMED good by the user. Four playtest defects; research done for all
four (4 researchers in parallel), curated into `docs/original-code/{combat,rendering,cutscenes-camera}.md`
with raw logs in `docs/research/2026-08-26-*.md`.

| # | Defect | Root cause | Status |
|---|---|---|---|
| 4 | Shaft lift rides rushed | tween indices are camera-local; we still added the legacy global rebase, so every delta silently resolved to 0 | **DONE** — reviewed PASS, user-confirmed "работает идеально" |
| 2 | Health bar only on the adjacent tile | original uses a 6-tile facing ray; monsters are never distance-gated. Our spec said "one tile" | **DONE** — user-confirmed |
| 3 | Standing on a corpse blocked the shot | own-tile entity sorts first (frac −1); the original's corpse branch accepts only at exactly one tile and never breaks | **DONE** — user-confirmed |
| 1 | Rifle too high and too small | `draw2DSprite` is a world-space billboard 400 units ahead, magnified by the world projection (Kx 1.336 / Ky 1.340) — not a 1:1 screen blit | **DONE** — user-confirmed |
| 5 | Cutscene viewport slid down (found in round 2) | the original never changes the viewport for cinematics; `cinRect` is the cockpit-overlay anchor, and `BeginFrame` discards the y | **DONE** — user-confirmed |
| 6 | Wall shots fired and burned the turn (regression from our 6-tile ray) | world hit has `def == nullptr`, so the wall-push branch was dead code | **DONE** — user-confirmed |

Spec: `docs/architecture/specs/2026-08-26-combat-stage1-fixes.md` (6 groups),
ADR `docs/architecture/adr/0009-legacy-world-viewport.md` (user chose to restore the legacy
world band (1,7,478,248), horizon back at y=131). `2026-08-26-combat-stage1.md` is partially
superseded. G4 (bottom HUD panel 480x64 at y=256) landed as a prerequisite for G5.

Regression list confirmed clean by the user: doors, loot, dialogs, TAB "Turn Passed",
monster wake/pain/death/corpse looting.

## Next steps

All four playtest defects + the self-inflicted wall-shot regression are user-confirmed fixed
(2026-08-26): lift ride cutscenes, health bar at range, shooting from a corpse tile, rifle
size/height, cutscene entry without a vertical step, wall shot not consuming ammo/turn,
impact point and flash correct.

Also user-confirmed on 2026-08-26: cinematic fov/weapon-suppression fixes (weapon unchanged in
gameplay, absent in cinematics) and the cinematic letterbox black bars.

## Current focus — decomposition

Spec `docs/architecture/specs/2026-08-26-decomposition.md` (19 groups, 2 phases),
ADR 0010 (modules + Env injection), ADR 0011 (typed TraceHit + named encodings).
Zero behaviour change; acceptance = the user sees no difference. Phase 1 (GameContext ->
CinematicCamera/LootSession/Targeting/ViewWeapon/SceneRenderer/PlayerActions) is sequential;
Phase 2 (Game -> TraceSystem/DoorSystem/MonsterSystem/SpriteLerps/CorpseLoot/EntityDb behind
forwarders) runs in parallel with it.

### Known gaps found in passing, not yet scheduled

- `Hud::clearMessages()` has zero call sites; legacy zeroes `msgCount` on ST_CAMERA entry
  (`src/Canvas.cpp:1209`) and never draws messages during a cinematic.
- Bottom HUD panel draws background only (widgets live in the uncalled `Hud::draw`).
- A far `ET_SPRITEWALL`/`ET_DOOR`/`ET_DECOR_NOCLIP` election becomes an air shot here, while
  legacy `src/PlayingInputHandler.cpp:509` fires at the entity for any `eType != 0`.
- Then: bottom-panel widgets (shield/health/portrait/weapon icon/keys currently unreachable —
  they live in `Hud::draw`, which has no caller in the gameplay render path).
- Cleanup: drop the TEMP `[cam] nextKey` print (rides now eye-confirmed), throttle the tween
  out-of-range diagnostic, fix the stale comment placement in `MayaCamera.cpp`.
