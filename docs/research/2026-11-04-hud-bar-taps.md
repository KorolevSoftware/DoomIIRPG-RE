# 2026-11-04 — HUD bottom-bar taps (armor, health, weapon, keys)

## Hypothesis (user wish)
Tap armor/health = replenish (auto-use shard/medkit); tap weapon = pick another weapon.

## Verdict: PARTIAL
- Armor/health: REFUTED. No direct use. Shield (id 4) opens `MENU_ITEMS_DRINKS`, health
  (id 5) opens `MENU_ITEMS` (`src/Hud.cpp:1346-1350`, `src/PlayingInputHandler.cpp:159-178`).
  Use goes through the item list + confirm (`ui.md` §17a).
- Weapon: CONFIRMED with detail. Tap < 300 ms = `ACTION_NEXTWEAPON` (`src/Hud.cpp:1324-1342`);
  hold >= 300 ms (polled in `Hud::update`, `src/Hud.cpp:1365-1378`) opens a grid overlay of
  owned weapons; tapping one calls `selectWeapon(id)` without ammo check
  (`src/Hud.cpp:1181-1210`, `:1281-1288`).
- "Empty panel right of weapon": not empty, it is the keycard widget, button 6 -> quest log
  (`src/Hud.cpp:85-86`, `:1352-1353`).

## Method
Read `Hud::startup/handleUserTouch/handleUserMoved/update/drawWeaponSelection`
(`src/Hud.cpp:65-96`, `:1181-1380`), `TouchController::touchStart/touchMove/touchToKey_Play`
(`src/TouchController.cpp:20-75`, `:120-180`, `:495-515`), `PlayingInputHandler`
(`src/PlayingInputHandler.cpp:60-190`), `Player::selectWeapon/selectNextWeapon`
(`src/Player.cpp:132-217`), `fmButtonContainer::GetTouchedButtonID` (`src/Button.cpp:277-287`).
Cross-check: keyboard path maps the same actions (`src/InputEventController.cpp:79-89`).
new_src grep over `ui/`, `core/`, `domain/game/`.

## Evidence
Curated in `docs/original-code/hud-buttons.md` §1-§4 (button table, timing, overlay layout,
gating, new_src gap table).

## Open questions
- `screenRect[2..3]` runtime values at 480x320 were not re-derived here (overlay y origin
  `screenRect[3] - 112` for the first row).
