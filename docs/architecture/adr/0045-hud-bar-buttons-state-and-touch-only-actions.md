# ADR 0045 — HUD bar buttons: retained state in `core/HudButtons`, touch-only `QuestLog` / `HudNextWeapon`, direct overlay selection

Date: 2026-11-04. Status: accepted. Spec: `../hud-bar-buttons.md`. Facts:
`docs/original-code/hud-buttons.md`. Retires Deviation D3 of `specs/2026-08-27-ui-layer.md`.

## Context

The original's HUD bar has seven touch buttons (`src/Hud.cpp:65-92`); the rewrite wired only the
soft keys and the portrait. Buttons 4/5/6 open menus 75/72/46, button 2 cycles weapons on a short
tap and opens a weapon-grid overlay on a 300 ms hold. That needs state across frames (the
overlay flag, the press timestamp, the touch-start origin for the 3 px drag cancel), but
`UiState` is frozen to three things (ADR 0012 point 2) and the views are stateless. The task
also forbids any keyboard change, while the playing handler treats `Action::NextWeapon` (key Z)
as zoom-only (`new_src/core/PlayerActions.cpp:462-463`).

## Decision

1. **A new `core/HudButtons` class owns the retained state** (`open_`, `pressTime_`, press
   origin, drag latch) and post-filters the view's `UiResult` (`resolve`) before
   `GameContext::applyUiAction`. It mirrors the legacy split: `Hud` holds the flag and timer,
   `TouchController` holds the touch origin and `canvas->touched`.
2. **Touch-only actions** `Action::QuestLog` and `Action::HudNextWeapon`. `HudNextWeapon` runs the
   legacy `ACTION_NEXTWEAPON` playing arm; Z/X keep their current zoom-only meaning. Binding Z to
   it later is a one-line change, explicitly not done here.
3. **Overlay selection is applied directly** in `applyUiAction` (`Player::selectWeapon`), not
   queued: the legacy touch handler calls it before the ST_COMBAT check and outside
   `handlePlayingEvents` (`src/Hud.cpp:1281-1288`), so it works during a monster attack and has
   no animation/monster gate.
4. **The overlay is drawn by `HudView`** from model fields (`weaponSelect`, mask, per-weapon ammo)
   with the legacy layout; the view repaints `panelBottom` after the dim so the bar stays
   undimmed (legacy draws the bar background after the overlay).
5. **Overlay force-closed when the HUD is not interactive or familiar** (spec deviation HB-D1).

## Consequences

- One small new module; `GameContext` gains one member and a few lines. `UiState` unchanged.
- `PlayerActions` gains a swallow test via `Env::weaponSelectOpen` (keyboard included while the
  overlay is open, as in legacy) and a zoomed `HudNextWeapon -> BackKey` remap.
- The soft keys and portrait inherit the legacy drag cancel and `blockInputTime` gate.
- Golden frames unchanged: nothing is drawn differently without a press.

## Rejected alternatives

- **Implement `Action::NextWeapon` in the playing switch**: faithful to the original key map, but
  changes key Z's behaviour, which the task forbids.
- **Keep the state in `UiState` or `Hud`**: `UiState` is frozen by ADR 0012; `Hud` is being
  reduced to a non-drawing producer and has no input access.
- **Plain members in `GameContext`**: the file is already the largest in the tree; the 300 ms /
  drag / close rules are self-contained and read better as one unit.
- **Queue the overlay selection**: the combat gate in `PlayerActions` would drop it during a
  monster attack, unlike the original.
