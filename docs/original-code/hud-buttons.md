# HUD bottom-bar buttons: what a tap does (original iOS port)

Geometry, art and data sources of the bar are in `ui.md` §1-§9; this file covers
the touch actions only. Every claim cites `src/`.

## 1. Buttons

`Hud::startup` registers 7 buttons in `m_hudButtons` (`src/Hud.cpp:65-86`) and 15
weapon-overlay buttons in `m_weaponsButtons`, initially parked at (480,320)
(`src/Hud.cpp:88-92`, `MAX_WEAPON_BUTTONS = 15` `src/Hud.h:43`).

| id | widget | rect (x,y,w,h) | release action (`src/Hud.cpp`) | effect |
|---|---|---|---|---|
| 0 | left soft key | 0,256,52,64 | unzoom, then `ACTION_MENU` (:1306-1311) | in-game menu |
| 1 | right soft key | 428,256,52,64 | `ACTION_BOT_DISCARD` if a bot weapon is held, else `ACTION_AUTOMAP`; unzooms first (:1312-1323) | automap |
| 2 | weapon icon | 268,258,102,44 | short tap: `ACTION_NEXTWEAPON`; long press: weapon overlay (§2) (:1324-1342, :1365-1378) | |
| 3 | portrait | 219,264,42,36 | `ACTION_PASSTURN` (:1343-1345) | |
| 4 | **shield/armor** | 49,258,79,44 | `ACTION_ITEMS_DRINKS` (:1346-1347) | opens `MENU_ITEMS_DRINKS` (75) = Nano Drinks list (`src/PlayingInputHandler.cpp:169-178`) |
| 5 | **health** | 133,258,79,44 | `ACTION_ITEMS` (:1349-1350) | opens `MENU_ITEMS` (72) = inventory root: health packs, armor, drinks submenu... (`src/PlayingInputHandler.cpp:159-168`) |
| 6 | keys (right of weapon) | 375,258,55,44 | `ACTION_QUESTLOG` (:1352-1353) | opens `MENU_INGAME_QUESTLOG` (`src/PlayingInputHandler.cpp:179-188`) |

So: **neither the armor nor the health panel uses an item directly.** They are menu
openers; using an item still goes through the list row -> "Use %01?" confirm ->
`Player::useItem` path (`ui.md` §17a, `loot-inventory.md` §8). Note the armor
plate opens the *drinks* menu, not an armor list; armor shards / flak jackets are
listed in `MENU_ITEMS` (health button). All three menu actions instead call
`exitTargetPractice()` when in the shooting range and clear `hud->msgCount`
(`src/PlayingInputHandler.cpp:159-188`). The area right of the weapon icon is not
empty: it is the keycard widget (button 6), drawn even with no keys (row 0,
`src/Hud.cpp:1154-1179`).

## 2. Weapon button: tap vs long press

- Press over id 2 stores `weaponPressTime = upTimeMs` (`src/Hud.cpp:1291-1295`).
- `Hud::update` (called each playing frame, `src/GameStateRunner.cpp:196`): while
  button 2 is still highlighted and `upTimeMs - weaponPressTime >= 300`, sets
  `isInWeaponSelect = true` unless `ST_COMBAT` or zoomed, then zeroes the timer
  (`src/Hud.cpp:1365-1378`).
- Release over id 2 (`src/Hud.cpp:1324-1342`): zoomed -> `ACTION_BACK` to the zoom
  handler (unzoom); elapsed `>= 300` (or timer already zeroed by `update`) -> nothing;
  elapsed `< 300` -> if the overlay is open it closes, else
  `handlePlayingEvents(0, ACTION_NEXTWEAPON)`.
- `ACTION_NEXTWEAPON` in play (`src/PlayingInputHandler.cpp:122-136`): no-op while
  weapon 14 is held; `Player::selectNextWeapon()` (skips weapons with no ammo unless
  ammoUsage 0 or weapon 2, wraps, `src/Player.cpp:192-217`); on change a HUD
  message with the weapon's `longName`; `helpBitmask |= 0x100`. It bypasses the
  animation/monster gate (`src/PlayingInputHandler.cpp:72`). No turn is spent.

### Weapon-select overlay (`Hud::drawWeaponSelection`, `src/Hud.cpp:1181-1210`)

- Drawn at the end of `Hud::draw` while `isInWeaponSelect` (`src/Hud.cpp:772-780`).
- 50% black fill from `y = imgPanelTop->height` to the bottom (`:1187`).
- One `drawWeapon` icon per owned, non-disabled weapon (`~disabledWeapons & weapons`,
  bits 0..14), 4 per row, step `w + 4` horizontally; first row at
  `y = screenRect[3] - 108 - 4`, each further row 48 px **higher**; row origin
  `x = (screenRect[2] - 16 - 4*w) / 2` (`:1184-1209`). Each icon's touch area is set
  to its draw rect and `drawButton = true`; only drawn buttons hit-test
  (`src/Button.cpp:277-287`).
- Release on an overlay icon: `player->selectWeapon(id)`, overlay closes
  (`src/Hud.cpp:1281-1288`). **No ammo check, no weapon-name message** (unlike
  next-weapon). `selectWeapon` itself unzooms, ignores the call while familiar,
  and falls back to `selectNextWeapon` only if the weapon is not owned
  (`src/Player.cpp:132-163`).
- Release on any other HUD button closes the overlay and performs that button's
  action (`src/Hud.cpp:1299-1301`); release elsewhere does nothing (overlay stays). While open, playing
  actions are swallowed (`src/PlayingInputHandler.cpp:67-69`) and controls/swipe
  are not highlighted (`src/TouchController.cpp:36,71,167,174`); every release is
  routed to the HUD (`src/TouchController.cpp:508-511`).

## 3. Gating

- Release dispatch: `TouchController::touchToKey_Play` calls
  `hud->handleUserTouch(x,y,false)` when `pressY > 256` or the overlay is open
  (`src/TouchController.cpp:508-511`); press highlights via
  `handleUserTouch(...,true)` (`src/TouchController.cpp:34,70`).
- `blockInputTime != 0` -> ignored (`src/Hud.cpp:1238-1239`).
- A drag of more than 3 px from the touch start sets `canvas->touched`
  (`src/TouchController.cpp:131-134`) and the release is dropped (`src/Hud.cpp:1276`).
- In `ST_COMBAT` the release switch does not run (`src/Hud.cpp:1303`): no HUD
  action during the combat animation (except overlay closing via `isInWeaponSelect = false`).
- Familiar (sentry bot) mode: only ids 0 (menu), 1 (bot discard) and 3 (pass turn,
  also the sentry plate at 143,262) react (`src/Hud.cpp:1241-1266`).

## 4. `new_src` status (as of 2026-11-04)

| Piece | new_src has | missing |
|---|---|---|
| Soft keys 0/1 (Menu / Automap / BotDiscard) | hit rects + actions, narrowed to arrow icons (`new_src/ui/HudView.cpp:93-122`) | — |
| Portrait id 3 -> PassTurn | `new_src/ui/HudView.cpp:153-155`, familiar plate `:52-54` | — |
| Shield id 4 -> ItemsDrinks | `Action::ItemsDrinks` -> `openMenu(kMenuItemsDrinks)` (`new_src/core/PlayerActions.cpp:456-460`, key O `new_src/core/UiInputCollector.cpp:104`) | no hit rect / Active sheet in `HudView` (`new_src/ui/HudView.cpp:122-127` D3 note); no `UiAction` for it (`new_src/ui/UiTypes.h:39-57`), no mapping in `GameContext::applyUiAction` (`new_src/core/GameContext.cpp:1198`) |
| Health id 5 -> Items | `Action::Items` -> `openMenu(kMenuItems)` (`new_src/core/PlayerActions.cpp:451-455`, key I `:103`) | same as above |
| Keys id 6 -> QuestLog | `kMenuQuestLog` screen exists in `MenuSession` (`new_src/core/MenuSession.cpp:344,481`) | no `Action::QuestLog` (`new_src/core/GameStates.h:36-41`), no button |
| Use item from list | `MenuSession` confirm + `Player::useItem` + `advanceTurn` (`new_src/core/MenuSession.cpp:1800-1801`, `new_src/domain/game/Player.cpp:507`) | — |
| Weapon id 2 short tap -> NextWeapon | `Player::selectNextWeapon` ported (`new_src/domain/game/Player.cpp:332-352`); `Action::NextWeapon` exists | playing handler ignores it (`new_src/core/PlayerActions.cpp:462-464` "shortcut" default); no weapon-name message; no hit rect |
| Weapon id 2 long press -> overlay | `Player::selectWeapon(i, def)` (`new_src/domain/game/Player.cpp:354`) | no `isInWeaponSelect`, 300 ms timer, overlay draw, overlay buttons, input swallow |
| `selectPrevWeapon` | — | not ported (only needed for keys, not for HUD taps) |
