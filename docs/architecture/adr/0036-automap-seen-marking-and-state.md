# ADR 0036 — Automap: "seen" marks flow through `ViewVisibility`, the world pass keeps running under the map, and the automap is a sibling of `Playing` in the input path

Date: 2026-10-30. Status: accepted.
Spec: [automap.md](../automap.md). Facts: `docs/original-code/automap.md` (AM).

## Context

The original marks automap data from three places (AM §2):

* line nibble bit `0x8` inside `Render::drawNodeLines`, i.e. in the middle of the
  software BSP walk, after `clipLine` + `occludeClippedLine` (`src/Render.cpp:717-744`);
* sprite bit `0x200000` inside `Render::addSprite` (`src/Render.cpp:877-879`);
* tile bit `0x80` in `uncoverAutomap` after a finished move
  (`src/AutomapController.cpp:35-63`).

All three are gated by `game->updateAutomap`, true only in ST_PLAYING / ST_COMBAT /
ST_AUTOMAP (`src/Canvas.cpp:802,811,830,934`); the type-6 (`LINE_TYPE_MAP`) line mark is
not gated (`src/Render.cpp:740-742`).

In the rewrite the BSP walk lives in `render/World3D` and already reproduces the legacy
cull for bookkeeping only (`ColumnOcclusion`, ADR 0031), and its results already reach
the simulation through one plain struct, `ViewVisibility`, copied once per frame by
`GameContext::render` (ADR 0029). Render must not mutate domain data on its own, and it
does not know the game state.

ST_AUTOMAP in the original runs the playing input handler unchanged
(`src/InputEventController.cpp:354-356`), keeps rendering the 3D view off-screen
whenever input arrived (`src/AutomapController.cpp:18-33`), snaps the view to the
destination (`src/MovementController.cpp:492-498`) and resolves the whole monster turn
instantly after a move (`src/MovementController.cpp:185-195`).

## Decision

1. **Render records, `GameContext` applies.** `ViewVisibility` gains
   `std::vector<int> seenLines`: every line index for which the legacy walk would have
   set bit 8 *if* `updateAutomap` were true (type 0/4 lines that tightened at least one
   occlusion column, plus every type-6 line of a visited leaf). `ColumnOcclusion::
   occludeLine` returns the legacy `occludeClippedLine` bool for that. Sprites need
   nothing new: `drawnSprites` is already the `addSprite` set (ADR 0031).
   `GameContext::render`, right after it copies the frame's visibility, calls
   `Automap::markSeen(map, vis, gate)` with `gate = state is Playing (incl. combat) or
   Automap`. `markSeen` reads the line type itself, so type-6 lines are marked even when
   the gate is false, exactly like the original.
2. **One small domain module, `domain/game/Automap.{h,cpp}`**, free functions on
   `MapData`: `markSeen`, `uncover`, `giveMap`, `buildModel`. No class, no state of its
   own except the cursor blink pair, which the caller owns. The ScriptVM (`giveMap`),
   `PlayerActions` (`uncover`) and `GameContext` (`markSeen`, `buildModel`) all call into
   it; nobody else writes the three seen bits except `WorldState` restore (already
   there).
3. **The world pass keeps running in `StateId::Automap`.** `GameContext::render` draws
   the world as in every other state (ADR 0009 band), then the automap view paints an
   opaque 480x320 background over it, the same way the in-game menu does. This is what
   keeps lines/sprites being marked while the player walks with the map open, and the
   ADR 0029 sight wake keeps working (`tickAutomap` calls `renderActivate`). The original
   renders only on frames after input (`automapDrawn`); rendering every frame is a
   superset with an identical result for a static view.
4. **`StateId::Automap = 6` is a sibling of `Playing` in the input path.** Queued actions
   in Automap go to the same `PlayerActions::handleAction`; the few automap-specific arms
   (toggle, BACK leaves, `snapMonsters(true)`, blocked move costs a turn, the automap
   branch of `finishMovement`) branch on `host->state()` at the legacy sites. The tick is
   a small `tickAutomap()` in `GameContext`: forced exits first, door snap, view snap to
   destination, arrival hooks.
5. **Forced exits are written where the original writes them** (ScriptVM `evWait`,
   `EV_DIALOG`, the two `playerPain` call sites), plus one catch-all at the top of
   `tickAutomap`: `combat.active` (the original enters ST_COMBAT from
   `Combat::performAttack`, `src/Combat.cpp:165`, which leaves the automap) and a
   script-blocked input (`src/Game.cpp:3256-3258`).

## Consequences

* `ViewVisibility` grows one vector (a few hundred ints per frame, reused capacity).
* `Game::visibility` is still written only by `GameContext::render`; `MapData` seen bits
  are written by `markSeen` from the same place, once per rendered frame.
* `WorldState` needs no change: it already saves/restores all three bits
  (`new_src/domain/game/WorldState.cpp:185-219,285,304`).
* The 3D world is drawn and thrown away under the map; cost is the same as under the
  in-game menu.
* Deviations accepted (spec §9): help dialogs are not dequeued in the automap; quest
  markers are deferred until the notebook exists; doors started in Playing that are
  still animating when the map opens are also snapped.

## Rejected alternatives

* **World3D writes `lineFlags` / `mapSpriteInfo` directly with a gate flag pushed in
  from `GameContext`.** Rejected: render would mutate domain data and need game-state
  knowledge; ADR 0029 already gives us the one-way channel.
* **Marking in the next tick from `Game::visibility`.** Rejected: the gate must be the
  state of the frame that rendered, and the original marks in the same pass; applying at
  copy time keeps that with no extra latch.
* **Skip the world pass in Automap.** Rejected: walking with the map open would stop
  revealing walls and sprites, and monsters would not wake by sight, both of which the
  original does.
* **A separate `AutomapSession` in `core/`.** Rejected: the automap has no screen state
  beyond a blink pair; the input is the playing handler. A session class would be an
  empty shell around four free functions.
