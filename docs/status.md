# Status

_Last updated: 2026-08-25 (evening)_

## Controls (2026-08-29, aligned with `src/Input.cpp:80-95`)

| Key | Effect |
|---|---|
| UP / W, DOWN / S | move forward / backward (menu + dialog + loot: scroll) |
| LEFT / RIGHT | turn (menu + dialog + loot: page) |
| A / D | turn (deviation: the original strafes; no strafe in the rewrite) |
| RETURN | attack / talk / use, menu select, dialog page advance, loot page |
| C | pass turn (also closes the loot list) |
| TAB | automap (logs only, no screen yet) |
| ESCAPE | open menu / back — **does not quit** |
| BACKSPACE | back (ours; also closes the loot list) |
| K | debug: grant keycards (ours, removable) |

There is no quit key: the window close button is the only exit, as in the
reference. Z/X/I/O/P/B are reserved-unmapped (no rewrite counterpart). Full
rationale: `docs/architecture/specs/2026-08-27-ui-layer.md` §"KEYMAP 2026-08-29".

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

## Current focus — MENU COMPLETE (2026-08-29, every group user-confirmed)

8 of 8 groups, spec `docs/architecture/specs/2026-08-28-menu.md` + ADR 0013:
G1 `io/MenuData` (menus.bin parsed, golden-checked at boot), G2 menu primitives
and sheets, G3 the menu opens (root list, wobbling cursor, soft keys, health/shield
readout), G4 scrolling + the 4-sheet scrollbar + faithful drag on list and bar,
G5 the navigation stack and sub-screens, G7 confirm screens, G6 help pages and the
PDA shell, G8 info buttons and the torn-page popup. Inventory and weapons bodies
are built in code as the original does, and selecting a weapon row equips it — the
rewrite's first weapon switch from the UI.

Working: root list, Inventory -> Weapons (with the equip), Status -> Player values,
Game Help's ten topics as real text, PDA shell, Options, the four confirms, per-row
info popups.

Deliberately refusing, with reasons logged: Save Game, View Map, Restart Level and
Save & Quit YES (no save system, no automap state, no map reload), Credits,
Controls, Nano Drinks, item use, the details screen. They draw as NORMAL rows —
the legacy `ITEM_DISABLED` look belongs only to rows the DATA disables, a
distinction the reference build taught us after we got it wrong.

Deviations, all recorded at the code and in the spec's DEVIATION section:
soft-key hit rects narrowed to the arrow icons; `kViewPx` 241 chosen over the
port-derived 256 after the user saw the last row jammed against the border; and
the help-page scroll clamp bounded by content height rather than the port's
item-count bound (neither side is J2ME behaviour — both are `[GEC]`).

Known gaps: no key bound to the info action (popup is mouse-only), `%NN` help
arguments would survive literally, quest/journal content absent, Level/Grades
values unsourced.

## Previous focus — UI layer COMPLETE (2026-08-28, every group user-confirmed)

Custom immediate-mode UI, 8 groups of 8, spec
`docs/architecture/specs/2026-08-27-ui-layer.md` + ADR 0012:
G1 real canvas scissor + window->canvas cursor mapping; G2 primitives, `UiState`,
`UiAssets`; G3 `core/UiInputCollector` (mouse did not exist before); G4 bottom HUD
via `ui.*` with clickable soft keys; G7 dialog box with clickable page icons;
G5 loot list; G6 view weapon.

**The layer invariant is now checkable, not merely stated**:
`grep -rn "#include \"domain/game\|#include \"core/" new_src/ui/` is empty.
UI receives values (per-frame model structs, never setter-fed fields) and returns
intents (`UiAction`), which `GameContext::applyUiAction` maps into the SAME
`pendingActions_` queue the keyboard uses — so mouse and keys cannot diverge on
turn semantics.

Deliberate deviations, all documented at the code and in the spec:
- soft-key hit rects narrowed to the arrow icons (9,268,32,32)/(438,268,32,32) at
  the user's request, so the `Menu`/`Map`/`Wait` text is not clickable — strict
  subsets of the legacy (0,256,52,64)/(428,256,52,64);
- pass-turn moved to the portrait (219,264,42,36), which is where the original has
  it (`src/Hud.cpp:76,1343`); the `Wait` literal has no button in the original and
  ours was invented by G4;
- D1: `ViewWeapon` keeps hand clipping (scissor cannot trim a source sub-rect);
- D2: the `flashDone` latch left the draw path, so the flash shows one rendered
  frame instead of two (~15 ms shorter, never later). User-confirmed acceptable.

Facts gathered on the way (in `docs/original-code/ui.md`): the arrow art IS the
soft key's pressed state, not a separate widget; the portrait is PASSTURN, the
weapon icon NEXTWEAPON, shield/health/keys are drinks/items/questlog; the loot
list's only touch target is the whole screen; dialog page icons are buttons 5/6/7/8
(90x90 at (390,20)/(390,110)) drawing at 75% alpha until pressed; the menu uses a
different scrollbar art from the dialog/loot one, selection is a wobbling cursor
glyph rather than a highlight bar, and there is no key repeat.

## Previous focus — decomposition COMPLETE (2026-08-27, user-confirmed)

Phase 1: 7/7, Phase 2: 6/6. `GameContext.cpp` 1626 -> 535, `Game.cpp` 1565 -> 305.
Thirteen modules, pointwise `Env` injection, no singleton or context back-references.
All five duplicated entityDb helper copies collapsed into `EntityDb` (P2-GF).

Phase 3 also COMPLETE (2026-08-27, each group user-confirmed):
- B2: `TraceHit` is the only carrier of trace results; the hand-decode of `def == nullptr` that
  produced the dead wall-push branch is gone (acceptance grep empty).
- C2: weapon tables are parsed structs (`WeaponDef`/`WeaponPose`) instead of a byte vector read
  through field-index arithmetic; identity proven against the shipped `tables.bin` for all rows.
- C1/C3: content masks (`Contents::*`) and tile/sprite encodings (`MapBits.h`) are composed from
  named bits and pinned; `13997`/`21741`/`13501`/`0xF000000`/`0x3000000` appear only on asserts.
- C4: `Combat::tileDistSq(tiles)` removes the off-by-one of `tileDistances[n]`.
- F sweep: all 15 forwarders gone from `Game.h` (115 lines, was 236). `setXPSystems` stays as a
  deliberate composite of two peer wirings.

Sizes: `GameContext.cpp` 1626 -> 535, `Game.cpp` 1565 -> 288, `Game.h` 236 -> 115.

Entity `info` bits: `0x20000000` (breathing suppressed, inverted setter) and `0x400000`
(dirty/needs-save) are CONFIRMED and named; `0x200000` and `0x4000000` are write-only in the
original with no reader anywhere and stay unnamed on purpose (our "highlight marker" comment
was refuted). The entity word does NOT share `mapSpriteInfo`'s layout, and a third colliding
bitfield exists (the `getSaveHandle` result) — see `docs/original-code/entities.md` §5.

### Original plan (kept for reference)

## Current focus — decomposition

Spec `docs/architecture/specs/2026-08-26-decomposition.md` (19 groups, 2 phases),
ADR 0010 (modules + Env injection), ADR 0011 (typed TraceHit + named encodings).
Zero behaviour change; acceptance = the user sees no difference. Phase 1 (GameContext ->
CinematicCamera/LootSession/Targeting/ViewWeapon/SceneRenderer/PlayerActions) is sequential;
Phase 2 (Game -> TraceSystem/DoorSystem/MonsterSystem/SpriteLerps/CorpseLoot/EntityDb behind
forwarders) runs in parallel with it.

### Decomposition debt — MUST be paid in P2-GF (do not lose this)

`DoorSystem` carries verbatim COPIES of `Game::findMapEntity`, `linkEntity` and
`unlinkEntity` (private members, marked "do not add logic here"), because those helpers
belong to `EntityDb` which only exists from P2-GF, and a module `Env` may not hold a `Game*`.
P2-GC (`MonsterSystem`) needs the same helpers (`corpsifyMonster` relinks entities) and must
copy the SAME comment rather than invent a variant. **P2-GF deletes both copies and switches
them to `EntityDb*`.** Duplicated logic drifting apart is exactly what this refactor exists to
prevent, so this is the one debt that cannot be quietly deferred.

`MonsterSystem::Env` carries a `std::function<void(int)> snapLerps` shim because the lerp pool
belongs to P2-GD; **P2-GD must replace it with a `SpriteLerps*`** and repoint `Env::clockMs` at
the lerp clock's new owner. `Game::snapSpriteLerps` (= legacy `snapLerpSprites`,
`src/Game.cpp:1149-1166`) becomes `SpriteLerps::snap`.

Also transitional, tagged FORWARDER, scheduled to die: `Game::lastTraceHits()` rebuilds a
legacy vector from `TraceSystem::hits()` per call (dies in P3-B2); `Game::traceMove` keeps an
unused `const MapData&` parameter; `Game::performDoorEvent` / `useDoorFacing` /
`GameContext::getHeight` are one-line forwarders.

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
