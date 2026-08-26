# ADR 0006 — Loot dwell UI without a LootingSystem module

Date: 2026-08-25 · Status: ACCEPTED
Spec: `docs/architecture/specs/2026-08-25-loot-dwell-ui.md`

## Context

Legacy ST_LOOTING is driven by a dedicated `LootingSystem` object owned by the Canvas
(`src/LootingSystem.cpp`): it holds the crouch/stand pose clock, pools + marks corpse
loot at entry (`poolLoot`), composes the display text, handles dwell input, grants on
close, and paints its own menu rects (it deliberately does NOT use DialogSystem styles,
`src/LoothingSystem.cpp:121-150`; it calls Canvas's shared scrollbar like Dialog does).

The rewrite has no LootingSystem analog today. The camera phases landed inside
GameContext (spec `specs/2026-08-25-camera-pitch` work, `new_src/core/GameContext.cpp:
438-496`) with an interim auto-grant + toast in `Game::lootCorpse`. The interactive
dwell now needs a home for four concerns: session state, pooling/marking, input, painting.

## Decision

Do **not** create a `LootingSystem` module. Split the legacy object along the rewrite's
existing ownership lines:

1. **Session state + input + painting → GameContext** (the Canvas analog). The loot
   block already there grows the dwell fields (`lootPool_`, `lootSettleSfx_`),
   `handleLootingAction`/`closeLootSession`, and `drawLootingMenu`. This mirrors legacy
   structure: Canvas owned the state machine, ticked `lootingState`, routed input, and
   painted state overlays over world+HUD.
2. **Domain math → Game**: `poolLootCorpse` (tile-chain walk, mark-looted, pool merge,
   display-text composition) and `giveLootPool` (grant pass) replace `lootCorpse`.
   Game owns entities/tile lists/entity defs; composition needs both defs and
   Localization, matching what legacy `poolLoot` accessed through the app singleton.
3. **Shared widgets are reused, not duplicated**: the menu draws through
   `Graphics2D::drawString` sub-ranges on one `Text` buffer (the DialogSystem pattern)
   and borrows `DialogSystem::drawScrollBar` (promoted to public) — it is the ported
   `Canvas::drawScrollBar`, which legacy loot also shared.

## Consequences

+ One fewer module; the loot feature stays reviewable as two small diffs (Game pair of
  functions, GameContext block).
+ Pose clock, dwell predicate, input guard and draw guard all read the same two fields
  (`lootCrouch_`, `lootTime_`) — no redundant "dwellActive" flag to desync.
+ GameContext keeps growing into a large file (~1000 LOC); acceptable while it remains
  the single Canvas analog, but future state overlays should follow this pattern
  (private draw method + render hook) rather than new subsystem classes.
− `DialogSystem::drawScrollBar` becomes public API used outside dialogs — mild coupling,
  justified by it being the shared legacy implementation.
− `Game::poolLootCorpse` takes `const Localization&` so Game touches presentation-ish
  string composition; kept because splitting composer from pooler would duplicate the
  line-table bookkeeping or force a third type.

## Rejected alternatives

- **New `domain/game/LootingSystem.{h,cpp}`**: faithful class-for-class port, but it
  would need pointers to ctx/game/player/loc/hud/font (a second Env struct) purely to
  wrap ~120 lines that GameContext can host natively; rejected per YAGNI and the
  established pattern of folding Canvas-owned state machines into GameContext
  (cinematics precedent, ADR 0003).
- **Hud method `drawLootingMenu`**: Hud owns textures, not state-machine overlays;
  legacy explicitly did NOT paint loot via Hud. Also Hud bars aren't wired into
  `render()` yet, making the layering misleading.
- **DialogSystem style-N reuse** (style 4 loot popup): wrong geometry/colors entirely —
  legacy loot uses its own rects (`0xFF660000` body, top-bar placement) and a different
  scroll model (3-line window vs dialog paging).
