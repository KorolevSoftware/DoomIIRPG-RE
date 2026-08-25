# Loot-crouch camera (pitch-down when looting a corpse)

Date: 2026-08-25 · Status: **CONFIRMED** (mechanism fully traced; all claims cite `src/`)

## Hypothesis

User observation: when looting a corpse, the first-person camera tilts/pans down toward
the corpse. Question: is this a canvas pitch field, a script mini-cutscene
(`ScriptThread`/`EV_*`), or a MayaCamera pitch channel? What are timings/targets,
trigger conditions, and the return-to-player transition?

## Method

Grepped `ST_LOOTING`, `viewPitch`, `destPitch`, `onEnterLooting`, `crouchingForLoot`;
read `src/LootingSystem.cpp` (whole file), `src/LootingSystem.h`,
`src/Canvas.cpp` (`setState`, tick, `lootingState` delegate),
`src/MovementController.cpp` (`startRotation`, `updateView`),
`src/PlayingInputHandler.cpp` (fire trace), `src/TinyGL.cpp` (`buildViewMatrix`,
`setView`), `src/InputEventController.cpp` (routing), `src/Combat.cpp`
(`tileDistances`), `src/Entity.cpp` (`distFrom`). Cross-checked existing notes in
`docs/original-code/loot-inventory.md` §1.5/§2.4 against source.

## Verdict

**CONFIRMED**: the tilt is **not** a cutscene. There is no `ScriptThread`, no
`EV_STARTCINEMATIC`, no MayaCamera involved. Entering canvas state `ST_LOOTING = 23`
(`src/Canvas.h:90`) hands the *first-person view itself* to `LootingSystem`, which each
frame overwrites `canvas->viewX/viewY/viewZ/viewPitch` with a hand-authored crouch pose
and calls `canvas->updateView()` — the normal gameplay renderer then draws it
(`fov 290`). Two phases of exactly 500 ms: crouch-down, then (after the loot UI closes)
stand-up back to the player pose.

## Mechanism

### Entry

- Fire action on a lootable corpse selects the entity, then:
  `if (n6 != 0) { canvas->setState(Canvas::ST_LOOTING); canvas->poolLoot(entity->calcPosition()); return true; }`
  — before any attack/tile-event processing (`src/PlayingInputHandler.cpp:374-378`).
- `Canvas::setState(ST_LOOTING)` hook caches the current **terrain** pitch target:
  `this->lootingSystem.onEnterLooting(this->destPitch)` (`src/Canvas.cpp:1142-1143`);
  `onEnterLooting` stores `lootingCachedPitch = destPitch`, `crouchingForLoot = true`,
  `lootingTime = app->time`, clears soft keys (`src/LootingSystem.cpp:26-33`).
  Note it caches `destPitch` (slope target), **not** `viewPitch`.
- Tick: while state == `ST_LOOTING`, main loop calls `lootingState()` every frame
  (`src/Canvas.cpp:940-941`; delegate `src/Canvas.cpp:1471-1473`). Input is routed to
  `handleLootingEvents` (`src/InputEventController.cpp:432-434`; touch
  `src/TouchController.cpp:29-31`).

### Phase A — crouch down (500 ms, time-driven, not frame-count)

`LootingSystem::lootingState()` (`src/LootingSystem.cpp:35-60`), while
`app->time < lootingTime + 500` (constant duplicated as literal 500;
`LOOTING_CROUCH_TIME = 500` exists at `src/Canvas.h:46` but is unused there):

```
t   = app->time - lootingTime            // 0..500
n   = ((500 - t) << 16) / 500            // REMAINING fraction, 16.16 (65536 -> 0)
n2  = 65536 - n                          // elapsed fraction
h0  = render->getHeight(destX, destY)                    // player tile floor
h1  = render->getHeight(destX + viewStepX, destY + viewStepY) // faced tile floor
hb  = (h0 > h1) ? h0 : ((h0*n + h1*n2) >> 16)            // blend only downhill
viewX = destX + (48 + ((-48*n) >> 16)) * (viewStepX >> 6)
viewY = destY + (48 + ((-48*n) >> 16)) * (viewStepY >> 6)
viewZ = hb + 26 + ((10*n) >> 16)
viewPitch = std::max(-(64 - ((64*n) >> 16)) + lootingCachedPitch, -64)
canvas->updateView()
```

- Eye slides from tile center **48 map-units (0.75 tile) forward** along the cardinal
  facing (`viewStepValues` entries are ±64/0 → `>>6` = ±1, `src/Canvas.h:108`).
- Eye height drops from floor+36 to floor+26 (10 units ≈ 15% tile) using the blended
  floor height (max of the two tiles wins if player side is higher).
- Pitch glides from `cachedPitch` to `cachedPitch - 64`. Angle units: 1024 = full turn,
  so 64 = **22.5° down** (positive pitch = up, see "Pitch sign" below). Clamped so the
  total never goes below −64; e.g. on an uphill slope (`cachedPitch=+64`) the crouch
  merely levels the view to 0 — faithful quirk.
- Settled pose (`t >= 500`, written every frame until stand-up):
  `viewX/Y = dest ± 48*step`, `viewZ = max(h0,h1) + 26`,
  `viewPitch = max(cachedPitch - 64, -64)`
  (`src/LootingSystem.cpp:66-72`). Sound **1055** plays once via `field_0xac5_` latch
  (`src/LootingSystem.cpp:62-65`).

### Dwell / loot UI

`drawLootingMenu` paints the 3-line list only after the crouch settles
(`src/LootingSystem.cpp:122`). Input ignored during both transitions by the guard
`crouchingForLoot && app->time > lootingTime + 500` (`src/LootingSystem.cpp:87`).
FIRE pages 3 lines; on the last page (or PASSTURN/BACK) → `giveLootPool()` runs
**immediately** (grant happens before standing up), `crouchingForLoot = false`,
`lootingTime = app->time` restarts the clock for phase B
(`src/LootingSystem.cpp:89-103`). `giveLootPool` stats via
`foundLoot(viewX+viewStepX, …)` — i.e. the corpse tile (`src/LootingSystem.cpp:303`).

### Phase B — stand up (another 500 ms)

Mirror lerp (`src/LootingSystem.cpp:52-58`, settled `:73-81`):

```
hb' = (h0 > h1) ? h0 : ((h0*n2 + h1*n) >> 16)     // weights swapped vs phase A
viewX = destX + ((48*n) >> 16) * (viewStepX >> 6) // 48 -> 0
viewY = destY + ((48*n) >> 16) * (viewStepY >> 6)
viewZ = hb' + 36 + ((-10*n) >> 16)                // +26 -> +36
viewPitch = std::max(-((64*n) >> 16) + lootingCachedPitch, -64) // crouched -> cached
```

On the first frame past 500 ms the final pose is snapped
(`viewX=destX, viewZ=h0+36, viewPitch=cachedPitch`) and the session ends:
`canvas->setState(Canvas::ST_PLAYING); app->game->advanceTurn();`
(`src/LootingSystem.cpp:74-80`) — **looting consumes one turn**.
`setState(ST_PLAYING)` restores HUD repaint flags 0x2f, `lastTurnTime = now`,
playing soft keys, `invalidateRect()` (`src/Canvas.cpp:1065-1079`). Total motion =
500 ms down + user-controlled dwell + 500 ms up.

## Player view PITCH in gameplay (Q2)

Yes — `viewPitch`/`destPitch` are live gameplay fields, not cinematic-only:

- `MovementController::startRotation` computes the terrain-slope pitch target every
  move/turn: traces 384 units ahead along facing, then
  `destPitch = ((floorAhead + 36 - destZ) << 7) / distToHit`, clamped to ±64
  (`src/MovementController.cpp:230-280`); step size
  `pitchStep = |destPitch - viewPitch| / animFrames` (`:281`).
- `updateView` eases `viewPitch` toward `destPitch` by `pitchStep` per frame
  (`src/MovementController.cpp:468-481`) and renders
  `renderScene(viewX, viewY, viewZ, viewAngle, viewPitch, viewRoll, 290)` when not
  zoomed (`:545`); zoom adds `zoomPitch` (`:537-541`).
- Other gameplay consumers: savegames round-trip `viewPitch`
  (`src/Game.cpp:2133`, `:2178`), death cam pitches 96 → 0
  (`src/GameStateRunner.cpp:264`, `destPitch=64` on death `src/Canvas.cpp:1127`),
  MayaCamera::Snap zeroes it when a cinematic ends (`src/MayaCamera.cpp:396`).

### How rendering consumes pitch

`Canvas::renderScene` scales positions `<<4` (+8 centering) and forwards pitch verbatim
(`src/Canvas.cpp:1330-1344`). `TinyGL::setView` wraps it into 0..1023 and back to signed
(`src/TinyGL.cpp:185,195-197`), builds the view matrix rows from `sinTable[pitch+512]`
/ `sinTable[pitch+256]` (`buildViewMatrix`, `src/TinyGL.cpp:68-98`), and **widens FOV by
|pitch|**: `buildProjectionMatrix(viewFov + abs(viewPitch), …)` (`src/TinyGL.cpp:199`) —
so the full crouch renders at 290+64=354. Pitch sign convention: positive = look up.
Evidence: walking toward higher ground yields `destPitch > 0` (stairs tilt up,
`src/MovementController.cpp:273`), and the engine's own forward-vector users negate row
3 of the GL view matrix (`-view[2]/-view[6]/-view[10]`,
`src/MovementController.cpp:39-40`, `src/PlayingInputHandler.cpp:218-220`), matching the
OpenGL "camera looks down −z_eye" projection (`matrix[11] = -MATRIX_ONE`,
`src/TinyGL.cpp:117`). Therefore loot target `cachedPitch − 64` = 22.5° **down** — the
observed tilt.

During `ST_LOOTING` the pose is drawn through this same normal path (no `cinRect`
viewport letterbox, no `activeCamera`): `lootingState` forces
`REPAINT_VIEW3D|REPAINT_HUD` (`src/LootingSystem.cpp:38-39`) and calls
`canvas->updateView()` (`:59,:71,:78` → `movementController.updateView()`,
`src/Canvas.cpp:1428`), which falls through to `renderScene(..., viewPitch, ..., 290)`
(`src/MovementController.cpp:544-545`). Reproduction note: `updateView` still applies
its residual `animPos/zStep/pitchStep` easing *after* the pose write each frame; because
the pose is recomputed from scratch next frame the visible path is the LootingSystem
formula — keep the same call order (write pose, then `updateView()`) for fidelity.

## Trigger conditions (Q3)

Fires only for the **corpse-loot UI route**, not for every pickup:

- Candidate selection inside the ACTION_FIRE trace loop: for hit entities with
  `eType == 9` (corpse) at exact facing adjacency
  `distFrom(viewX,viewY) == combat->tileDistances[0]` — `distFrom` is
  `max(|dx|², |dy|²)` (`src/Entity.cpp:1155-1158`) and `tileDistances[j] = (64*(j+1))²`,
  so index 0 = precisely one 64-unit tile away (`src/Combat.cpp:42`):
  - placed prop corpse (`monster == nullptr`): lootable iff `param == 0 && lootSet != nullptr`
    (`src/PlayingInputHandler.cpp:324-330`);
  - monster corpse: iff `(monster->flags & 0x800) == 0 && lootSet != nullptr`
    (`:331-335`); flag 0x800 = already looted, `param` counts prior loots;
  - chainsaw equipped (`ce->weapon == 1`) instead just picks the gib target and never
    sets the loot flag in this port (`:281-321`, J2ME variant `#if 0`-ed);
  - zoomed-in (`canvas->isZoomedIn`) excludes looting (`:323`).
- Only then `n6 != 0` → `setState(ST_LOOTING)` (`:374-378`). Walk-over items go through
  `Entity::touched()` (instant grant, no camera), search objects through EV_GIVELOOT
  dialog (`docs/original-code/loot-inventory.md` §2.5) — neither crouches.
- An *empty* corpse still crouches: entry checks only `lootSet != nullptr`, while
  `hasEmptyLootSet()` is only used for the sparkle render
  (`src/Render.cpp:3470-3474`, `src/Entity.cpp:2309-2311`); the UI shows string 228
  ("nothing") (`src/LootingSystem.cpp:259-261`).
- `poolLoot` merges **all** `eType == 9` entities on that tile (tile-chain walk) and
  marks each looted immediately (`src/LootingSystem.cpp:154-224`).

## Return-to-player transition (Q4)

See Phase B: 500 ms reverse lerp of X/Y/Z/pitch to
`(destX, destY, h0+36, cachedPitch)`, ended by
`setState(ST_PLAYING)` + `advanceTurn()` (`src/LootingSystem.cpp:74-80`). Input during
both 500 ms windows is swallowed (`:87`). No fade, no sound on stand-up (1055 latch is
one-shot per session, `:32,:62-65`), no viewport change.

## Formulas summary (port-ready)

Constants: phase length 500 ms; forward offset 48 u (0.75 tile, cardinal); eye height
stand 36 u, crouch 26 u; pitch delta −64 u (22.5° down @ 1024/circle); clamp
`viewPitch >= -64`; base FOV 290 (+|pitch|); sound 1055 on crouch-settle; grant before
stand-up; `advanceTurn()` after stand-up.

## Port checklist (new_src)

1. Add `ST_LOOTING`; on entry cache `destPitch` (terrain target, not `viewPitch`),
   `lootingTime = now`, `crouchingForLoot = true`, clear soft keys.
2. Per-frame pose override exactly as the four formulas above (16.16 intermediate `n`),
   then call the normal `updateView()`/`renderScene(…, 290)` path; do **not** touch yaw/
   roll/viewport/HUD visibility beyond repaint flags 0x22 / REPAINT_HUD|REPAINT_VIEW3D.
3. Gate loot input on `now > lootingTime + 500`; FIRE pages ×3, UP/DOWN/LEFT/RIGHT move
   `lootLineNum`; close via last-page FIRE or PASSTURN/BACK → grant pool, then restart
   timer with `crouchingForLoot = false`.
4. Stand-up mirror formulas; snap + `setState(ST_PLAYING)` + `advanceTurn()` on expiry.
5. Trigger: fire-trace adjacency test `distFrom == 4096` vs `eType == 9` candidates with
   unlooted markers and non-null `lootSet`; exclude zoom and chainsaw selection path.
6. Renderer: pitch sign positive-up, FOV widening `fov + |pitch|` in projection setup.

## Open questions

- Whether the disabled J2ME chainsaw branch (`#if 0`, `src/PlayingInputHandler.cpp:282-314`)
  ever reached `ST_LOOTING` on device (it sets `n6=1` without checking loot presence) —
  irrelevant for this port, noted for completeness.
