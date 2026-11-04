# UI: HUD bottom bar (original RE port)

Scope: everything needed to drive the bottom status bar with live data.
Top-bar / monster-health-bar facts live in `docs/original-code/combat.md` §
and `docs/research/2026-08-26-facing-entity-health-bar.md`; the panel band
geometry is in `docs/original-code/rendering.md` §6.1.

## 1. Widget map (human player branch, `Hud::drawBottomBar` `src/Hud.cpp:636-724`)

| Widget | Source field | Sheet (w×h, rows) | Draw pos (480×320) |
|---|---|---|---|
| Bottom panel bg | — | `gameMenu_Panel_bottom.bmp` 480×64 | (0, 256) |
| Switch-left button | — | `Switch_Left_Normal/Active.bmp` 32×32 | (9, 268) |
| Switch-right button | — | `Switch_Right_Normal/Active.bmp` 32×32 | (438, 268) |
| Weapon icon | `player->ce->weapon` | `Hud_Weapon_Normal/Active.bmp` 102×616 = 14 rows × 44 | (268, 258) |
| Weapon ammo digits | `player->ammo[weapons[9*w+4]]` | `Hud_Numbers.bmp` 10×220 = 11 rows × 20 | (325, 266), (336, 266), (347, 266) |
| Shield plate | — | `HUD_Shield_Normal.bmp` / `Hud_Shield_Button_Active.bmp` 79×44 | (49, 258) |
| Shield digits | `ce->getStat(2)` (STAT_ARMOR, raw) | Hud_Numbers | (89, 268), (100, 268), (111, 268) |
| Health plate | — | `HUD_Health_Normal.bmp` / `Hud_Health_Button_Active.bmp` 79×44 | (133, 258) |
| Health digits | `ce->getStat(0)` (STAT_HEALTH, raw) | Hud_Numbers | (173, 268), (184, 268), (195, 268) |
| Portrait face | row from health, see §4 | `Hud_Player.bmp` (per hero) 32×160 = 5 rows × 32 | (224, 263) |
| Portrait frame | — | `HUD_Player_frame_Normal/Active.bmp` 48×49 | (216, 255) |
| Keys | `inventory[19]`, `inventory[20]` | `Hud_Key_Normal/Active.bmp` 55×176 = 4 rows × 44 | (375, 258) |
| Soft-key labels | `canvas->softKeyLeftID/RightID` + literal "Wait" | font | (2,320) / (240,320) / (478,320) |

All widget blits use `drawRegion(..., anchor 20)` = `TOP|LEFT` (J2ME anchors:
HCENTER 1, VCENTER 2, LEFT 4, RIGHT 8, TOP 16, BOTTOM 32), so the coordinates
above are top-left corners. Image sizes verified from the shipped BMP headers
in `Doom 2 RPG.ipa`.

The touch rects registered in `Hud::startup` (`src/Hud.cpp:64-91`) share the
same anchors: id0 switch-right area (0,256,52,64), id1 switch-left
(428,256,52,64), id2 weapon (268,258,102,44), id3 player (219,264,42,36),
id4 shield (49,258,79,44), id5 health (133,258,79,44), id6 key
(375,258,55,44). A highlighted button only swaps the *_Active sheet — no
position change.
What each button does on release (incl. the weapon long-press overlay) is in
`hud-buttons.md`.

## 2. Numbers renderer — `Hud::drawNumbers(g, x, y, space, num, weapon)` (`src/Hud.cpp:1127-1152`)

* Refuses `num >= 1000` (prints "ERROR: drawnumbers() does not currently
  support values over 999" and draws nothing).
* Always three glyphs, 10×20 each, **row = 20 · (9 − digit)** — the sheet is
  stored 9…0 top-down, so row 0 = '9', row 9 = '0', row 10 = a '/' glyph
  (verified by decoding `Hud_Numbers.bmp`, 11 rows of 20).
* Glyph x positions: `x`, `x + space + 10`, `x + 2*space + 20`. With
  `space = 1`: x, x+11, x+22. No leading-zero suppression — 100 shows "100",
  0 shows "000" (matches the screenshot's shield "000").
* `weapon == 13` special case: `d0 = num % 10`, `d1 = -1`, `d2 = 5`. Row for
  −1 is `20*(9-(-1)) = 200` = the '/' glyph, so weapon 13 renders
  **"N/5"** (N = ammo mod 10, the 5 being weapon 13's AMMOUSAGE). The middle
  glyph is still drawn and the third glyph still sits at `x+2*space+20`.

## 3. Weapon icon + ammo — `Hud::drawWeapon(g, x, y, weapon, highlighted)` (`src/Hud.cpp:1048-1125`)

Row (`texY`) per weapon id, 44 px rows in a 102×616 sheet (14 rows):

| weapon | 0 | 1 | 2 | 3,4 | 5,6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | else |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| texY | 0 | 1 | 2 | 10 | 11 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 12 | 13 |
| digits | y | **n** | y | y | y | y | y | y | y | y | y | y (N/5) | y | **n** |

Ammo value = `player->ammo[ combat->weapons[9*weapon + WEAPON_FIELD_AMMOTYPE] ]`
(`src/Hud.cpp:1122-1124`, field index 4 `src/Combat.h:30`). Digit anchor =
`x + (imgWeaponNormal->width >> 1) + 6, y + 8` → for the HUD slot
(268,258) that is (325, 266).

`ammoType` per weapon from tables.bin table 2 (9 signed bytes/row, 32 rows;
loaded `src/App.cpp:375,391`, layout `src/Combat.h:26-35`; dumped from the
shipped `tables.bin.gz`):

| weapon | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ammoType | 1 | 0 | 3 | 7 | 7 | 7 | 7 | 2 | 1 | 1 | 4 | 5 | 4 | 6 | 8 |
| ammoUsage | 1 | 0 | 2 | 0 | 0 | 0 | 0 | 2 | 1 | 1 | 1 | 1 | 10 | 5 | 1 |

(Weapon 1 has ammoType 0 *and* is the explicit no-digits case; rows 15…31 of
the table are monster attacks, hence weapon ids ≥15 → texY 13, no digits.)
Ammo is decremented after every resolved attack:
`ammo[ammoType] -= ammoUsage`, clamped at 0, together with
`hud->repaintFlags |= 0x4` (`src/Combat.cpp:353-359`).

## 4. Portrait row

```
v29 = 4 - 5 * ce->getStat(0) / ce->getStat(1);   // src/Hud.cpp:695-699
if (v29 < 0) v29 = 0;
```
Integer division; full health → `4-5 = -1` → clamped to **0** (healthiest
row); health 0 → row 4. No upper clamp is needed because
`CombatEntity::setStat(0, v)` clamps health to `getStat(1)` and negatives to 0
(`src/CombatEntity.cpp:72-89`), so `getStat(0) ∈ [0, max]` always. **Division
by `getStat(1)` is unguarded** — max health is never 0 in practice.

Row is recomputed **every draw pass**; nothing caches it. There is no
"damaged"/flashing portrait variant: `imgPlayerActive` is only the touch
highlight (drawn together with `imgPlayerFrameActive`, `src/Hud.cpp:701-703`).

The face sheet depends on the hero: `characterChoice` 1 → `Hud_Player.bmp`,
2 → `Hud_PlayerDoom.bmp`, 3 → `Hud_PlayerScientist.bmp` (+ `*_Active`),
all 32×160 (`src/App.cpp:433-449`).

## 5. Keys — `Hud::drawCurrentKeys(g, x, y)` (`src/Hud.cpp:1154-1179`)

Row index is **not** a bitmask read directly; it is built by nested tests on
two inventory slots:

| `inventory[19]` | `inventory[20]` | row |
|---|---|---|
| ≤0 | ≤0 | 0 (both slots empty) |
| >0 | ≤0 | 1 (red only) |
| ≤0 | >0 | 2 (blue only) |
| >0 | >0 | 3 (both) |

i.e. `row = (inv[19]>0 ? 1:0) | (inv[20]>0 ? 2:0)`. Colour assignment verified
by decoding `Hud_Key_Normal.bmp`: row 1's bright palette entries are red
(#C31C0D), row 2's are blue (#0D44C2), row 3 has both → **slot 19 = red
keycard, slot 20 = blue keycard**. Keycards are `IT_INVENTORY` items with
`def->parm` 19/20; picking one up emits string (0,84) "You got the key-card"
and sets `hud->repaintFlags |= 0x4` (`src/Entity.cpp:172-175`).

## 6. Soft-key labels ("Menu" / "Wait" / "Map")

* Left + right labels are drawn by `TouchController::drawTouchSoftkeyBar`
  (`src/TouchController.cpp:539-579`), which is the **first** thing
  `drawBottomBar` calls (`src/Hud.cpp:638-641`). That function also paints the
  bottom panel background, so the widgets always land on top of it.
  * left: `composeText(softKeyLeftID)` → `drawString(buf, 2, 320, 36)`
    (LEFT|BOTTOM);
  * right: `composeText(softKeyRightID)` → `drawString(buf, 478, 320, 40)`
    (RIGHT|BOTTOM).
* Centre label is hardcoded ASCII `"Wait"` in `drawBottomBar` itself, drawn
  only while both soft keys are set: `drawString(buf, 240, 320, 33)`
  (HCENTER|BOTTOM) (`src/Hud.cpp:715-723`).
* IDs come from `Localization::STRINGID(file, id) = file<<10 | id`
  (`src/Text.h:81-83`). Gameplay default is
  `setSoftKeys(0,52, 0,55)` in `SoftKeyController::drawPlayingSoftKeys`
  (`src/SoftKeyController.cpp:97-99`) — decoded from the shipped
  strings00.bin/strings.idx, text type 0: **52 = "Menu", 55 = "Map"**.
  Variants: target practice → left 30 "Exit", right cleared; zoomed-in →
  52/55; automap → 52/53 ("Leave"); familiar → 52/216 ("Re-turn");
  sentry-bot weapon equipped → 52/220 ("Dis-card")
  (`src/SoftKeyController.cpp:81-99`).
* Conclusion: the two side labels + the switch arrows belong to the
  soft-key/touch layer, but the original draws them from inside
  `drawBottomBar`, and the "Wait" label is literally part of it.

## 7. Familiar (sentry-bot) bottom bar

`player->isFamiliar` replaces the whole widget set (`src/Hud.cpp:645-681`):
`Hud_Sentry.bmp` 29×34 face region `(0, familiarType∈{3,4} ? 17 : 0, 29, 17)`
at `(SCR_CX - (15+118)/2, 278)` anchor 3 (HCENTER|VCENTER), plus a segment
gauge of `ammo[7]` (bot health): `n7 = ammo[7]*20/100`, `+1` unless already 20
or 0, then `n7` `fillRect(x, 271, 4, 14, colour)` steps of 6 px starting at
`SCR_CX + imgSentryBotFace->width - (15+118)/2`. Colour thresholds on
`ammo[7]`: >80 0xFF59A907, >60 0xFF96DA0E, >40 0xFFEDD703, >20 0xFFFF8B00,
else 0xFFFF0C00; highlighted → white + `HUD_sentry_active.bmp` at (143,261).

## 8. Draw order & gating

`Canvas::backPaint` (`src/Canvas.cpp:380-500`): world → particles → swipe area
→ **`hud->draw`** (only when `canvas.repaintFlags & REPAINT_HUD` and
`state != ST_MENU`, `:414-417`) → per-state overlays (looting list, dialog,
automap, mini-games) → menu → loading bar → fade. So the bottom bar is
underneath the dialog box and the loot list.

Inside `Hud::draw` (`src/Hud.cpp:726-812`) the order is by flag bit:

| bit | content |
|---|---|
| 0x1 | scope image + `drawEffects` |
| 0x2 | facing-entity probe + `drawTopBar` (top strip 480×20 + **monster health bar**) |
| 0x4 | arrow controls / sniper widgets, weapon-select overlay, **`drawBottomBar`** |
| 0x8 | bubble text |
| 0x10 | cinematic text + right soft key |

so bottom bar strictly after the top bar and the monster health bar.
`drawArrowControls` is skipped in `ST_DIALOG` but `drawBottomBar` is not
(`src/Hud.cpp:747-782`).

`hud->repaintFlags` per state (`Canvas::setState`):
`ST_COMBAT`/`ST_PLAYING`/`ST_DIALOG`/`ST_DYING`/`ST_BOT_DYING` = 47 (0x2F,
includes 0x4) (`src/Canvas.cpp:1057,1066,1095,1114,1131`);
`ST_INTER_CAMERA` = 43 (0x2B, **no** 0x4) (`:1081`); `ST_CAMERA` = 24 (0x18)
(`:1213`); `ST_LOADING`/`ST_SAVING` clear `REPAINT_HUD` (`:1184`).
States that set nothing (ST_LOOTING, ST_AUTOMAP, ST_MINI_GAME…) inherit the
previous mask, so ST_LOOTING still paints the bottom bar under the loot list.

**Every-frame vs invalidate:** in this port every `repaintFlags &= ~bit` inside
`Hud::draw` is commented out (`src/Hud.cpp:731,739,748,772,801`), so once a
state sets 0x2F the bottom bar is redrawn every frame and all widget values are
read live. The `|= 0x4` sites (`src/Combat.cpp:353` after an attack,
`src/Entity.cpp:174` on keycard pickup) are the J2ME-era invalidate hooks and
are redundant in the GL path.

## 9. Status in `new_src/` (as of 2026-08-27)

| Widget | data available? | draw code |
|---|---|---|
| weapon icon | `Player::weapon` (`new_src/domain/game/Player.h:32`) | `Hud::drawWeapon` ok, row table ok |
| ammo digits | `Player::ammo[9]` + `Tables::weaponDef(w).ammoType` (`new_src/io/Tables.h:56,60`) | present, but `Hud::ammo_` is a hardcoded 30 |
| shield | `player.ce.getStat(Enums::STAT_ARMOR)` (`new_src/domain/game/CombatEntity.h:22`) | ok |
| health / max | `Player::getHealth()/getMaxHealth()` (`new_src/domain/game/Player.h:100-101`) | ok |
| portrait | same health pair; `characterChoice` not modelled (hardcoded marine, `new_src/domain/game/ScriptVM.cpp:303-306`) → `Hud_Player.bmp` is the right sheet today | ok |
| keys | `Player::inventory[19]/[20]` (`new_src/domain/game/Player.h:24`) | `drawCurrentKeys` takes a row index; needs the two-slot fold |
| soft keys | not modelled (no `softKeyLeftID/RightID`, no localization type-0 lookups wired to the HUD) | missing |

Missing / divergent:

1. No setters: `Hud::health_/maxHealth_/shield_/weapon_/ammo_/keys_` are demo
   constants (`new_src/ui/Hud.h:166-172`).
2. `Hud::drawBottomBar` is never called from the render path —
   `GameContext::render` only calls `drawTopBar` (`new_src/core/GameContext.cpp:518`).
   It must be called right after it, before the dialog/loot overlays.
3. **Bug**: `new_src/ui/Hud.cpp:576-584` skips the middle glyph when
   `h2 == -1` and shifts the third glyph left, so weapon 13 renders "N5"
   instead of the original "N/5"; the original draws row 10 ('/') and keeps the
   third glyph at `x + 2*space + 20`.
4. Familiar bottom bar (§7) not implemented (no `isFamiliar` state).
5. Button-highlight (*_Active) variants are loaded but never selected (no
   touch button container).

---

# UI: the in-game menu (`MenuSystem`)

Scope: the screen behind the bottom-left `Menu` soft key. Everything below is
from `src/MenuSystem.cpp` / `src/MenuSystem.h` / `src/Menus.h` /
`src/MenuItem.cpp` plus the shipped `menus.bin` (item tables) and
`strings00.bin` type 3 (labels), both decoded from `Doom 2 RPG.ipa`.

**Port caveat.** `src/` is the [GEC] iOS/SDL port. Lines marked `// [GEC]` are
the porter's additions (volume sliders, video/input/bindings screens, the
pixel-precise touch scrollbar). Where a fact is port-only it is flagged.

## 10. Ownership and entry/exit

| Concern | Code |
|---|---|
| Open from gameplay | `ACTION_MENU` **and** `ACTION_BACK` → `hud->msgCount = 0; menuSystem->setMenu(MENU_INGAME)` (`src/PlayingInputHandler.cpp:138-158`); also on zoom-out (`src/ZoomController.cpp:78`) |
| State | `setMenu` → `initMenu(menu)` then `canvas->setState(ST_MENU)` if not already (`src/MenuSystem.cpp:649-654`) |
| Paint | `Canvas::backPaint` paints the menu only when `repaintFlags & REPAINT_MENU (=4)` (`src/Canvas.cpp:482-485`, `src/Canvas.h:124`); the flag is re-set every tick by `GameStateRunner::menuState` (`src/GameStateRunner.cpp:253`) |
| HUD suppressed | `hud->draw` is skipped while `state == ST_MENU` (`src/Canvas.cpp:414-417`), so the menu draws its own panel/soft keys |
| Input | `state == ST_MENU` → `menuSystem->handleMenuEvents(key, keyAction)` (`src/InputEventController.cpp:314-316`) |
| Leave | `returnToGame()` → `numItems = 0`, `app->time = lastTime = upTimeMs`, `setState(ST_INTER_CAMERA)` if a camera is active else `ST_PLAYING` (`src/MenuSystem.cpp:1209-1220`) |

`ST_MENU` entry only records `menuSystem->startTime = app->time` and clears the
event queue when coming from a non-menu state (`src/Canvas.cpp:1192-1206`).

## 11. Menu tree (data-driven from `menus.bin`)

`menus.bin` (`Resources::RES_MENUS_BIN_GZ`) is loaded in
`MenuSystem::startup` (`src/MenuSystem.cpp:46-74`): `short menuDataCount`,
`short menuItemsCount`, then `menuDataCount` LE ints (`menuData`), then
`menuItemsCount` LE ints (`menuItems`). Shipped file: 1996 bytes,
`menuDataCount = 72`, `menuItemsCount = 426`.

`menuData[j]` packing (`loadMenuItems`, `src/MenuSystem.cpp:4005-4035`):

```
bits  0..7   menu id (Menus::menus enum, src/Menus.h:210-311)
bits  8..23  END offset into menuItems, in ints (start = previous row's field, 0 for j==0)
bits 24..31  menu type (Menus::MENUTYPE_*, src/Menus.h:7-15)
```

Each item is **2 ints** `n7, n8`:

```
textField  = STRINGID(3, n7 >> 16)      flags  = n7 & 0xFFFF
action     = (n8 >> 8) & 0xFF           param  = n8 & 0xFF
helpField  = STRINGID(3, (n8 >> 16) & 0xFFFF)
```

`loadMenuItems(menu, begItem, numItems)` skips `begItem` items and takes
`numItems` (or all remaining when `-1`); a missing menu id is fatal
(`app->Error(29)`).

### 11.1 The root: `MENU_INGAME` (id 29, type 1 = LIST, 11 items)

Decoded verbatim from `menus.bin` (labels from strings type 3; `-` are the
soft-hyphens that `dehyphenate()` strips at draw time):

| # | label | action | target | help text |
|---|---|---|---|---|
| 0 | Inventory | GOTO | `MENU_ITEMS` (72) | "All of your items" |
| 1 | PDA | GOTO | `MENU_INGAME_QUESTLOG` (46) | "Logs important information" |
| 2 | Save Game | SAVE (4) | — | "Save game progress" |
| 3 | Load Game | GOTO | `MENU_INGAME_LOAD` (49) | "Restore last saved game" |
| 4 | View Map | CHANGESTATE (9) | `ST_AUTOMAP` (6) | "View the level map" |
| 5 | Status | GOTO | `MENU_INGAME_STATUS` (30) | "View game statistics" |
| 6 | Game Help | GOTO | `MENU_INGAME_HELP` (37) | "Help with the game" |
| 7 | Options | GOTO | `MENU_INGAME_OPTIONS` (35) | "Configure the game" |
| 8 | Restart Level | GOTO | `MENU_INGAME_RESTARTLVL` (52) | "Load auto-saved game" |
| 9 | Save & Quit | GOTO | `MENU_INGAME_SAVEQUIT` (53) | "Save game and exit" |
| 10 | Main Menu | GOTO | `MENU_INGAME_EXIT` (42) | "Exit to main menu" |

`initMenu` post-processing (`src/MenuSystem.cpp:1549-1573`):
`selectedIndex = 1` on open; `inventory[18] == 0` → item 2 gets
`ITEM_DISABLED`; while a camera is active items 3/4/5/10 are disabled and item
11 is retargeted to `MENU_INGAME_SPECIAL_EXIT` (58); when
`player->isFamiliar` the list is rebuilt with an extra "Return to player"
entry (`action 33 = ACTION_RETURN_TO_PLAYER`, strings 355/356) and items 0/6
disabled.

### 11.2 Sub-screens reachable from the root

| Menu | type | Content | Notes |
|---|---|---|---|
| `MENU_ITEMS` 72 | 1 LIST | header + "Weapons" → `MENU_ITEMS_WEAPONS` 73 | inventory root; drink/other pages are built in code (`:1930-2046`); see §17a |
| `MENU_INGAME_QUESTLOG` 46 | 7 NOTEBOOK | built by `LoadNotebook()` (`:3777-3809`): map name, divider, one wrapped line per notebook entry | not in `menus.bin` |
| `MENU_INGAME_LOAD` 49 | 6 VCENTER | `SetYESNO(136, 1, ACTION_LOAD, 0)` (`:1714-1719`) | plain confirm; **no slot list** |
| `MENU_INGAME_LOADNOSAVE` 50 | 6 VCENTER | "No Saved Game" + Back | |
| `MENU_INGAME_STATUS` 30 | 1 LIST | blank, "STATUS" divider, Player / Level / Grades | |
| `MENU_INGAME_PLAYER` 31 | 1 LIST | 17 read-only `label: value` rows (health, armor, rank, XP, defense, strength, accuracy, agility, IQ, time, turns, deaths, shots fired/missed, best shot, total damage) filled by `fillStatus(false,true,true)` | values land in `textField2` via `ARGUMENT1..17` |
| `MENU_INGAME_LEVEL` 32 | 1 LIST | Time / Secrets / Enemies / Turns / Deaths / Grade, `fillStatus(true,false,false)` | |
| `MENU_INGAME_GRADES` 33 | 1 LIST | one blank item; rows appended by `buildLevelGrades` | |
| `MENU_INGAME_HELP` 37 | 1 LIST | "GAME HELP" divider + 10 help topics + "Credits" (`CHANGESTATE ST_CREDITS`) | |
| help leaves 38/39/40/41/43/44/45/59/60/61 | **5 HELP** | `LoadHelpResource(n)` long text, n = 1 Play, 2 Move, 3 Attack, 7 Sniper, 4 Armor, 5 Effects, 6 Items, 8 Hacker, 9 Matrix-skip, 10 Power-up | see §14 |
| `MENU_INGAME_OPTIONS` 35 | 1 LIST | **original file rows**: "Controls" → 62, "Back". The port replaces the body with Sound/Video/Input sub-screens (`:1365-1458`, all `// [GEC]`) | |
| `MENU_INGAME_RESTARTLVL` 52 | 6 VCENTER | `SetYESNO(134, 1, ACTION_RESTARTLEVEL, 0)` | |
| `MENU_INGAME_SAVEQUIT` 53 | 6 VCENTER | `SetYESNO(135, 1, ACTION_SAVEQUIT, 0, 2, 0)` | |
| `MENU_INGAME_EXIT` 42 | 6 VCENTER | "Go to Main Menu?" + Save Game (`ACTION_SAVEEXIT`) / Don't Save (`ACTION_BACKTOMAIN`) / Cancel (`ACTION_BACK`) | |
| `MENU_INGAME_SPECIAL_EXIT` 58 | 6 VCENTER | `SetYESNO(137, 0, ACTION_MAIN_SPECIAL, 0)` | camera-active substitute for item 10 |

There is **no save-slot list and no options *values* screen in the shipped
data**: `ACTION_SAVE` calls `canvas->saveState(3|0x80, 3, 196)` and
`ACTION_LOAD` calls `canvas->loadState(getRecentLoadType(), 3, 194)`
(`src/MenuSystem.cpp:3035-3050`) — one implicit slot.

### 11.3 Navigation stack

`gotoMenu(m)` pushes `(current menu, selectedIndex, scrollY1, scrollY2,
scrollIndex)` unless `m == menu`, then `setMenu(m)`
(`src/MenuSystem.cpp:2818-2824`). Stack depth 10, overflow/underflow are fatal
(`:4059-4081`). `back()` pops and restores all five values (`:591-603`); with
an empty stack it calls `returnToGame()` for `MENU_INGAME`, `MENU_ITEMS`,
`MENU_ITEMS_DRINKS`, `MENU_INGAME_QUESTLOG`, `MENU_INGAME_SNIPER` (`:604-607`).
`setMenu(MENU_MAIN | MENU_INGAME | MENU_INGAME_KICKING)` clears the stack
(`:619-621`). `initMenu` resets `scrollIndex = selectedIndex = 0` only when the
menu actually changed (`oldMenu != menu`, `:1227-1230`).

## 12. List geometry (in-game branch, 480x320)

`setMenuSettings` runs first in `initMenu` (`src/MenuSystem.cpp:1242`,
body `:4168-4428`):

```
menuItem_height        = 46          // default, :4173
menuItem_width         = 162         // default, :4174
menuItem_fontPaddingBottom = 0       // :4177
menuItem_paddingBottom = 0           // :4178
if (menu >= MENU_INGAME) {           // :4191-4195
    setMenuDimentions(70, 0, 340, 320 - 64);   // menuRect = (70,0,340,256)
    menuItem_width         = imgInGameMenuOptionButton->width;   // 296
    menuItem_paddingBottom = 10;
}
```
(`inGame_menu_option_button.bmp` is 296x32, `gameMenu_Panel_bottom.bmp` 64 px
tall — verified from the shipped BMP headers.) `MENU_INGAME` itself hits the
`default: return` of the switch (`:4422-4424`), so its rect stays
(70, 0, 340, 256).

`paint` then re-sets the rect per frame (`:847-853`):
`menu >= MENU_INGAME && menu <= MENU_ITEMS_HOLY_WATER_MAX` →
`setMenuDimentions(menuRect[0], 10, menuRect[2], 241)` and
`setClipRect(screenRect[0], 10, screenRect[2], 241)`. **List region for the
in-game menu = x 70, y 10, w 340, h 241.**

### 12.1 Item height — `getMenuItemHeight(i)` (`:4947-4984`)

```
if (action != 0)  height = menuItem_height (46), padding = menuItem_paddingBottom (10)
else              height = FONT_HEIGHT[fontType] (16), padding = menuItem_fontPaddingBottom (0)
if (i == numItems-1) return height;      // last item gets no padding
return height + padding;
```
`ITEM_HIDDEN` (0x8000) items are skipped entirely by the callers.
So in the in-game menu a selectable row is **56 px** (46 + 10) and a
label/divider/help line is **16 px**.

### 12.2 How many are visible

Draw loop: `while (v79 < numItems && v33 < menuRect[3])` (`:911`), clipped to
the rect. 241 / 56 = 4 full rows + a clipped 5th. The logical window is
`maxItems`, set to **4** at the top of every `initMenu`
(`:1231`, comment `// [GEC] 4 por defecto`); it drives `scrollPageUp/Down`
(one page = 4 items) and the `moveDir` scroll window. UNKNOWN: what the
original J2ME build computed here — the port's own attempt
(`maxItems = maxItemsMain - maxItemsGame`) is commented out at `:5169`.

### 12.3 Per-item drawing (`paint`, `:905-1130`)

Selectable (`action != 0`) items, with `y0 = v33 + menuRect[1]`:

* background: `m_menuButtons` button `SetTouchArea(menuRect[0], y0,
  menuItem_width, menuItem_height)` and `Render` → `inGame_menu_option_button`
  (296x32) drawn normal or highlighted (`drawTouchButtons`, `:5087-5118`).
  Non-highlighted buttons are painted in the first `drawTouchButtons(g,false)`
  pass, highlighted ones in the second `(g,true)` pass (`:856-867`), so the
  pressed button always lands on top.
* label: truncated when `width + 10 > menuItem_width` — `setLength((296-10)/
  FONT_WIDTH[0]) = 23` chars then append glyph `'\x85'` (ellipsis)
  (`:943-952`); `dehyphenate()` unless `ITEM_NODEHYPHENATE`;
  `drawString(text, x, y0 + (46>>1), 2)` (anchor 2 = VCENTER) (`:1075-1076`).
* x origin: `menuRect[0] + 8 = 78`, or centered in `menuItem_width` when
  `ITEM_ALIGN_CENTER` (`:959-990`).
* value column (`textField2 != EMPTY_TEXT`): `drawString(v2, menuRect[0] +
  menuItem_width - 8 = 358, y0 + 23, 10)` (RIGHT|VCENTER) for selectable rows;
  for label rows `x = menuRect[0] + menuRect[2] = 410`, anchor 24
  (RIGHT|TOP) (`:992-1045`).
* `ITEM_CHECKED` prefixes glyph `'\x87'` + space (`:934-938`);
  `ITEM_DIVIDER` runs `buildDivider(text, menuRect[2]/CHAR_SPACING[0])`
  (`:954-957`); `ITEM_LEFT_ARROW`/`ITEM_RIGHT_ARROW` draw 12x12 regions of
  `imgAttArrow` at `x-17` / `x+width+5` (`:1115-1121`).
* `ITEM_DISABLED` on a selectable row overlays a translucent rect of the
  button colour; on a label row every character is replaced with glyph
  `'\x89'` and redrawn (`:1086-1114`).
* info ("i") button: `m_infoButtons` at `x = menuRect[0] + menuItem_width +
  menuItem_paddingBottom = 376`, `y = y0`, size 48x32
  (`gameMenu_infoButton_Normal.bmp`) — only for `menu <= MENU_INGAME_STATUS`,
  `MENU_INGAME_HELP`, `MENU_ITEMS*`, and the vending screens (`:5132-5161`).
* touch rows whose `posY > 210` are pushed off-screen (`SetTouchArea(x, 350,
  …)`) so a clipped row is not tappable (`:5099-5102`, `:5146-5148`).

### 12.4 Selection marker

No highlight bar and no `*_Active` text sheet: the selected row gets a **cursor
glyph** `'\x8A'` drawn to its left and the label is then shifted +8 px
(`:1064-1073`):

```
n11 = x + OSC_CYCLE[app->time / 100 % 4];
drawCursor(n11 + 3, y0 + (menuItem_height >> 1) - 8, 8);   // anchor 8 = RIGHT
x += 8;
```
`OSC_CYCLE` = `{-1, 0, 1, 0}` (tables.bin table 6, 4 signed bytes; loaded
`src/App.cpp:379,395` — decoded from the shipped `tables.bin`), so the cursor
oscillates ±1 px at 10 Hz. `drawCursor` is just
`drawString("\x8A", x, y, flags)` (`src/Graphics.cpp:670-686`).
The cursor is suppressed for `type 5` (HELP), `type 7` (NOTEBOOK) and the three
vending confirm/detail screens (`:1064`).

### 12.5 Movement, ends, key repeat

`handleMenuEvents` (`:2826-2969`):

| key action | effect |
|---|---|
| `ACTION_UP` / `ACTION_DOWN` | `scrollUp()` / `scrollDown()` → `moveDir(∓1)` (one item) |
| `ACTION_LEFT` / `ACTION_RIGHT` | `scrollPageUp()` / `scrollPageDown()` — up to `maxItems` (4) `moveDir` steps, stopping early if the selection would wrap |
| `ACTION_FIRE` | `select(selectedIndex)` |
| `ACTION_MENU`, `ACTION_BACK` | `back()` |
| `ACTION_MENU_ITEM_INFO` | opens the torn-page help popup for the selected row if it has a `helpField` (§15) |
| digits 0-9 | `enterDigit` cheat combo accumulator (`:2830-2832`, `:~280-310`) |

`moveDir(n)` (`:401-547`):

* **List types (everything except 5 and 7): the selection WRAPS.** Going below
  0 jumps to `numItems-1`, going past the end jumps to 0 (`:417-431`).
* Unselectable rows are skipped: `textField == EMPTY_TEXT` or
  `flags & 0x8001` (`ITEM_NOSELECT | ITEM_HIDDEN`) (`:427,432`).
* `scrollIndex` is then pulled so the selection stays inside the `maxItems`
  window (`:434-455`).
* **Types 5 (HELP) and 7 (NOTEBOOK) clamp instead of wrapping**: `scrollIndex`
  moves within `[0, numItems - maxItems]` and `selectedIndex = scrollIndex`
  (`:405-413`).
* Sounds: `soundClick()` = sound 1027 on every step (`:5874-5876`),
  sound 1065 when the scroll offset actually changed (`:539-541`),
  sound 1086 ("pistol") on `select`, sound 1122 on `back`.

**Key repeat on hold: none.** SDL key-repeat events are dropped
(`if (!sdlEvent.key.repeat)`, `src/Input.cpp:740,794`) and the event queue
holds exactly one pending event (`InputEventController::addEvents`,
`src/InputEventController.cpp:481-489`). Only touch-dragging the scrollbar
scrolls continuously.

## 13. The scrollbar

**It is not `Canvas::drawScrollBar`.** The menu owns a separate
`fmScrollButton m_scrollBar` (`src/MenuSystem.cpp:233`, class in
`src/Button.h:106-140`). `MenuSystem::drawScrollbar` (`:5020-5041`) picks:

* `isMainMenuScrollBar` (main-menu screens only) → a rotary dial:
  `Menu_Dial.bmp` (45x183) at `barRect.x/y` + `Menu_Dial_Knob.bmp` (22x24) at
  `x+12, y + (thumb*4)/5 - (knobH>>1) + 16`.
* otherwise `m_scrollBar->Render(g)`.

`fmScrollButton::Render` (`src/Button.cpp:536-589`): with images set it draws
`imgBar` centred in `barRect`, then `imgBarTop` at
`barRect.y + thumbOffset`, tiles `imgBarMiddle` down to
`thumbOffset + thumbLen - bottomH`, then `imgBarBottom`. Without images it
falls back to a `0x7F303030` track plus a solid thumb.

Setup at the tail of `initMenu` (`:2741-2815`):

```
contentH = sum(getMenuItemHeight(i)) over non-hidden items
if (menuRect[3] < contentH) {
    enabled = 1
    if (menu == MENU_LEVEL_STATS || menu >= MENU_INGAME || menu == MENU_END_RANKING) {
        images = gameMenu_ScrollBar (20x220) / topSlider (24x13) /
                 midSlider (24x6) / bottomSlider (24x13)
        h = 220;  y = menuRect[1] + ((menuRect[3] - 220) >> 1)
        barRect = (430, y, 50, 220)
        SetScrollBox(menuRect[0..3], contentH)          // thumb len computed
    } else { ... main-menu dial, isMainMenuScrollBar = true ... }
    if (menu >= MENU_VENDING_MACHINE) barRect.x = 350 + vending images
} else enabled = 0;
```
For `MENU_INGAME` (menuRect = (70,0,340,256) at that moment) →
`barRect = (430, 18, 50, 220)`; the bar image is centred at x = 445, the
slider images at x = 443. `contentH` = 10*56 + 46 = 606 > 256, so it is always
shown. `paint` draws it for every menu except the main-menu confirm/options/
difficulty/more-games/exit/end screens (`:869-879`).

`fmScrollButton` fields (`src/Button.cpp:380-406`): `field_0x3c_` = viewport
extent, `field_0x40_` = content extent, `field_0x44_` = content scroll offset
in px, `field_0x48_` = thumb offset in px, `field_0x4c_` = thumb length,
`field_0x50_` = content-px per thumb-px, `field_0x15_` = vertical (true here).

**Scrolling is per-pixel in this port.** `getScrollPos()` returns
`m_scrollBar->field_0x44_` (`:4922-4944`) and the draw loop starts at
`-getScrollPos()`. `moveDir` synchronises that pixel offset from
`selectedIndex`/`scrollIndex` in a block explicitly marked
`// [GEC] Actualiza la posicion del scroll touch` (`:463-546`); the
underlying original mechanic is the item-index pair
`scrollIndex`/`maxItems`. `getScrollPos` snaps to item boundaries only when
`isMainMenu` is true, i.e. never for `menu >= MENU_INGAME` (`:2813-2815`).

**Our `DialogSystem::drawScrollBar(g, x, y, h, top, bottom, total, visible)`
is a faithful copy of `Canvas::drawScrollBar` (`src/Canvas.cpp:1284-1315`)**
— same thumb formula `3h/(4*ceil(total/visible))`, same
`((top<<16)/(range<<8) * ((h-thumbH-14)<<8)) >> 16` offset, same 7x7 arrow
regions at `imgUIImages(60,0)`/`(60,7)` and the same track/thumb colours. That
is the right widget for the **dialog box and the loot list** (used by
`LootingSystem::drawLootingMenu`, `src/LootingSystem.cpp:146-150`), but it is
**not** what the menu uses. The menu needs the `gameMenu_ScrollBar` +
top/mid/bottom slider composite described above.

## 14. Long-text sections (help / PDA)

These are `type 5` (`MENUTYPE_HELP`) and `type 7` (`MENUTYPE_NOTEBOOK`) and use
**the same item list machinery — one wrapped line per `MenuItem`** — not the
dialog system.

`LoadHelpResource(n)` (`:3662-3702`):

1. `loadText(2); composeText(2, n, buf); unloadText(2)` — help bodies live in
   strings type 2.
2. `buf->wrapText(menu >= MENU_INGAME ? canvas->ingameScrollWithBarMaxChars : 35)`.
   `ingameScrollWithBarMaxChars = (displayRect[2] - 34) / 9 = (480-34)/9 = 49`
   chars (`src/Canvas.cpp:94`).
3. `LoadHelpItems(buf, 0)` (`:3811-3838`) splits on `'|'` and adds one item per
   line with `flags = ITEM_NODEHYPHENATE(2)`; a line starting with `'#'` also
   gets `ITEM_ALIGN_CENTER|ITEM_DIVIDER` (0x8|0x40) and the `'#'` is dropped.
   No action → 16 px per line.
4. If the total height exceeds `menuRect[3]` and `menu >= MENU_INGAME`, it
   re-wraps narrower: `menuRect[2] -= 27` then
   `wrapText(menuRect[2] / CHAR_SPACING[fontType])` = 407/11 = **37** chars
   (`:3685-3698`). Note this mutates `menuRect[2]` as a side effect.

Geometry for the in-game help leaves: `setMenuSettings` gives
`(x, y, w, h) = (23, 0, 434, 256)` (`:4351-4367`), `paint` narrows it to
`(23, 10, 434, 241)` → 241/16 = **15 visible lines**.

Scrolling: `moveDir` clamp branch — `scrollIndex` walks `[0, numItems-4]` one
line per Up/Down, 4 lines per Left/Right, and `selectedIndex` tracks it.
`select()` on a type-5/7 screen just calls `back()` (`:3004-3011`), and the
right soft-key id is suppressed (`type != 5` guard,
`src/GameStateRunner.cpp:216`).

`MENU_INGAME_QUESTLOG` (`LoadNotebook`, `:3777-3809`) is type 7: map-name item,
divider, then per quest one `composeText(loadMapStringID, notebookIndexes[i])`
wrapped to `ingameScrollWithBarMaxChars` through the same `LoadHelpItems`;
completed quests get glyph `'\x87'` + space prefixed, failed ones a
"failed" header (string 3/227) and `flags |= 4` (DISABLED). It opens scrolled
to the **bottom**: `n2 = (displayRect[3] - 26) / 16`,
`scrollIndex = selectedIndex = max(numItems - n2, 0)`.

Credits are **not** a menu section: item "Credits" is
`ACTION_CHANGESTATE → ST_CREDITS`, which runs
`Canvas::initScrollingText(2, 0, false, 16, 5, 500)` (`src/Canvas.cpp:1161-1165`).

## 15. The torn-page help popup

`ACTION_MENU_ITEM_INFO` on a row with a non-empty `helpField` sets
`drawHelpText = true; selectedHelpIndex = selectedIndex` (`:2849-2857`). While
it is up, **all other menu input is swallowed** and `ACTION_MENU`,
`ACTION_FIRE` or `ACTION_MENU_ITEM_INFO` dismisses it
(`drawHelpText = false; selectedHelpIndex = -1`, `:2840-2848`).

Rendering (`:1135-1171`): full-screen 50 % black wash
(`FMGL_fillRect(0,0,480,320, 0,0,0, 0.5)`), then
`gameMenu_TornPage.bmp` (**408x240**) at
`x = 230 - (408>>1) = 26`, `y = 10`; the help string is composed by
`MenuItem::WrapHelpText` → `composeTextField(helpField)` +
`wrapText(menuHelpMaxChars, 12, '\n')` (max **12** lines,
`src/MenuItem.cpp:29-34`) with `menuHelpMaxChars` temporarily overridden to
`imgGameMenuTornPage->width / FONT_WIDTH[fontType] = 408/12 = 34` chars
(`:1162`, restored at `:1164`); drawn at (230, 130) anchor 3
(HCENTER|VCENTER).

## 16. Soft keys inside the menu

`MenuSystem::drawSoftkeyButtons` (`:5181-5294`) draws them itself (the HUD is
not painted in `ST_MENU`), gated on `m_menuButtons` buttons **11** (left) and
**15** (right) having `drawButton` set by `updateTouchButtonState`
(`:4507-4628`) — for every `MENU_INGAME*` / `MENU_ITEMS*` screen both are on.

For `MENU_INGAME <= menu <= MENU_ITEMS_HOLY_WATER_MAX`:

| side | button rect | sheet | label | anchor |
|---|---|---|---|---|
| left | (9, 268, 99, 37) | `inGame_menu_softkey.bmp` 99x37 (`drawRegion` mode 2 when idle, full `drawImage` when pressed) | `composeText(3, 80)` = **"Back"**, dehyphenated | (42, 295) anchor 36 = LEFT\|BOTTOM |
| right | (372, 268, 99, 37) | same sheet | literal ASCII **"Resume"** (`"Exit"` on vending screens) | (448, 295) anchor 40 = RIGHT\|BOTTOM |

Both labels are drawn with font 0 forced (`setFont(0)` / restore).
Main-menu screens use `Switch_Left_*` at (10, 262) with the label at (8, 314);
vending screens use `inGame_menu_softkey`-like vending art at (56, 277) /
(362, 277).

`GameStateRunner::menuState` *also* computes `canvas` soft-key string ids every
tick (`src/GameStateRunner.cpp:203-253`): left = `(3,80)` "Back" when the menu
stack is non-empty, `(0,30)` "Exit" for `MENU_INGAME`/`_KICKING`/`_SNIPER`;
right = `(0,49)` "Details" when the row has `ITEM_SHOWDETAILS`, `(0,43)`
"Continue" on the stats screens, `(0,202)` "Buy" on vending confirm,
`(0,121)` "Select" when the row has an action, `(0,40)` "Skip" on
`MENU_SHOWDETAILS`. These ids are **not rendered** in this port (nothing draws
`softKeyLeftID/RightID` while `ST_MENU`); they are the J2ME-era labels and are
useful only as the semantic intent. String values decoded from
`strings00.bin` type 0 / type 3.

`back()` behaviour is §11.3. Note `ACTION_BACK` on `MENU_MAIN` goes to
`MENU_MAIN_EXIT` instead of popping (`:2938-2942`).

## 17. Menu state the rewrite does not have

| Original state | Where | In `new_src`? |
|---|---|---|
| `menuData`/`menuItems` from `menus.bin` | `src/MenuSystem.cpp:46-74` | no |
| `items[50]`, `numItems`, `type`, `menu`, `oldMenu` | `src/MenuSystem.h:139-146` | no |
| `selectedIndex`, `scrollIndex`, `maxItems` | `src/MenuSystem.h:143-148` | no |
| `menuStack[10]` + `menuIdxStack` + scroll stacks | `src/MenuSystem.h:151-154` | no |
| `drawHelpText`, `selectedHelpIndex` | `src/MenuSystem.h:172-173` | no |
| `m_menuButtons` / `m_infoButtons` / `m_scrollBar` | `src/MenuSystem.h:174-176` | no |
| `indexes[10]` + `saveIndexes/loadIndexes` (remembered cursor per inventory page) | `src/MenuSystem.h:126`, `:3026` | no |
| `player->characterChoice` (1 marine / 2 doom / 3 scientist) | `src/Player.h:65` | not modelled (hardcoded marine, `new_src/domain/game/ScriptVM.cpp:303-306`) |
| options values (`sfxVolumeScroll`, `musicVolumeScroll`, `alphaScroll`, vsync/resolution/deadzone/vibration) | `src/MenuSystem.h:165-171,214-224` | no — and all of these are `// [GEC]` port additions, not original data |
| `LEVEL_STATS_nextMap`, `detailsDef`, `goBackToStation`, `moreGamesPage` | `src/MenuSystem.h:118-124,160` | no |

`Canvas::menuRect[4]` / `setMenuDimentions` (`src/Canvas.cpp:1558-1564`) has no
counterpart either; the rewrite would need it (or its constants inlined) for
§12.

## 17a. Items menu and use confirm

Verified 2026-10-28 (raw log: `docs/research/2026-10-28-items-menu.md`).
All line numbers below are `src/MenuSystem.cpp` unless a file is named.

### 17a.1 Entry

| Trigger | Code |
|---|---|
| `ACTION_ITEMS` (key `AVK_ITEMS_INFO` in play, `src/InputEventController.cpp:79-81`; HUD button 5, `src/Hud.cpp:1349-1350`) | `setMenu(MENU_ITEMS)` (72), stack untouched (`src/PlayingInputHandler.cpp:159-168`) |
| `ACTION_ITEMS_DRINKS` (key `AVK_DRINKS`, `src/InputEventController.cpp:84-86`; HUD button 4, `src/Hud.cpp:1346-1347`) | `setMenu(MENU_ITEMS_DRINKS)` (75) directly (`src/PlayingInputHandler.cpp:169-178`) |
| root row 0 "Inventory" | `ACTION_GOTO 72` (§11.1) |

Every ST_MENU exit clears the stack (`src/Canvas.cpp:1033-1035`), so a list
opened from play has an empty stack and `back()` on it returns to the game
(`:604-607`, which names `MENU_ITEMS` and `MENU_ITEMS_DRINKS`).

### 17a.2 What `menus.bin` contributes (decoded from the shipped file)

| id | type | rows |
|---|---|---|
| 72 `MENU_ITEMS` | 1 | [0] blank flags 9; [1] "IN-VEN-TO-RY" (str 111) flags 73; [2] "Weap-ons" (175) flags 0 GOTO 73, help 176 "Your weap-ons" |
| 73 `MENU_ITEMS_WEAPONS` | 1 | [0] blank flags 9; [1] "WEAP-ONS" (66) flags 73 |
| 75 `MENU_ITEMS_DRINKS` | 1 | [0] blank flags 9; [1] "NA-NO DRINKS" (177) flags 73 |
| 77 `MENU_ITEMS_CONFIRM` | — | **absent**; built entirely in code |
| 79 `HEALTHMSG` | 6 | "Your health is" / "at its max-i-mum." / blank / "Ok" flags 8 action 26 |
| 80 `ARMORMSG` | 6 | "Your ar-mor is" / "at its max-i-mum." / blank / "Ok" action 26 |
| 81 `SYRINGEMSG` | 6 | "Can-not con-sume drink." / "Ef-fects at max-i-mum." / blank / "Ok" action 26 |
| 82 `HOLY_WATER_MAX` | 6 | "Can-not drink any more." / "Ef-fects at max-i-mum." / blank / "Ok" action 26 |

79 has its own `initMenu` case that is just `loadMenuItems(menu,0,-1)`
(`:2298-2302`); 80-82 use the `default:` branch, also `loadMenuItems(menu,0,-1)`
(`:2724-2727`). Action 26 = `ACTION_BACKTWO` (`src/Menus.h:162`).
Flags: 73 = `ITEM_DIVIDER|ITEM_ALIGN_CENTER|ITEM_NOSELECT`, 9 =
`ALIGN_CENTER|NOSELECT`, 32 = `ITEM_SHOWDETAILS` (`src/Menus.h:16-34`).

### 17a.3 Row construction (all item rows are built in code)

`initMenu` resets every row and calls `resetTextArgs()` first
(`:1232-1238`). Each item row is
`addItem(STRINGID(1, def->name), getLastArgString(), flags, action, param, STRINGID(1, def->description))`
after `addTextArg(<count as decimal>)`: the **value column** (`textField2`)
is `ARGUMENT<k>` = the k-th accumulated text arg (`:4090-4098`), i.e. the
plain count, no "x" prefix. Label = entity-def name, help popup text = entity-def
description (strings file 1 = `FILE_ENTITYSTRINGS`, `src/MenuStrings.h:21`).
Defs come from `entityDefManager->find(6, subtype, parm)` (eType 6 = item).

**`MENU_ITEMS` (72)**, in this exact order (`:1930-1986`):

1. Health group: for `n5 = 16..17` with `inventory[n5] > 0`; before the first
   such row, a divider "HEALTH" (str 3/112) flags 73 (`:1932-1946`). Row:
   def `(6,0,n5)`, flags 32, action 24 `ACTION_CONFIRMUSE`, **param = inventory
   index n5**.
2. Holy water: if `ammo[3] >= 25 && (weapons & 0x4)` — name/description from
   the **ammo** def `(6,2,3)`, count = `ammo[3]`, flags 32, action 24,
   **param = 22** (`INV_OTHER_HOLY_WATER`, a virtual slot; `:1947-1953`). No
   divider of its own: it follows the health group, or stands alone if that
   group is empty.
3. Armor group: `n8 = 11..12`, lazy divider "AR-MOR" (3/113), rows as in (1)
   with param n8 (`:1954-1968`).
4. `loadMenuItems(72, 1, 1)`: the "IN-VEN-TO-RY" divider (`:1970`).
5. If `player->hasANanoDrink()` (any `inventory[0..10] > 0`,
   `src/Player.cpp:2655-2662`): "Na-no Drinks" (3/178), value `STRINGID(3,0)`
   (empty), flags 0, `ACTION_GOTO 75`, help 3/179 "Your na-no drinks" (`:1971-1973`).
6. `loadMenuItems(72, 2, -1)`: "Weap-ons" GOTO 73 (`:1974`).
7. Credits: def `(6,0,24)`, count `inventory[24]`, flags 32, **action 16
   `ACTION_SHOWDETAILS`**, param 24 — always present (`:1977-1981`).
   (`showDetailsMenu()` is an empty stub in this port, `:3993-3995`.)

Then `loadIndexes(4)` and `selectedIndex = 0` (`// [GEC]`, `:1982-1984`); the
`initMenu` tail moves the cursor off the non-selectable row 0 (`:2732-2737`).
Menus.bin row 0 (blank) is never loaded (the BREW call is commented, `:1931`).

**`MENU_ITEMS_DRINKS` (75)** (`:2024-2045`): `loadMenuItems(75,0,-1)` (blank +
divider), `loadIndexes(2)`, `selectedIndex = 0`, then for `n13 = 0..10`
ascending with `inventory[n13] > 0`: def `(6,0,n13)`, flags 32, action 24,
param n13. If none: a single "[None]" (3/1) row flags 9.

**`MENU_ITEMS_WEAPONS` (73)** (`:1988-2021`): not a use-item path; rows use
`ACTION_USEITEMWEAPON` (18), see §17a.5.

Never listed anywhere: count-0 slots, 13 bottled water (converted to holy
water on pickup, `src/Player.cpp:1023-1025`), 18 journal, 19/20 keys,
21 syringe, 23 pack, 25. No sorting beyond the fixed index loops; no
disabled-row rules on 72/75 (no `ITEM_DISABLED` is ever set there).

### 17a.4 `ACTION_CONFIRMUSE` → `MENU_ITEMS_CONFIRM` (77)

`select` case 24 (`:3188-3198`): `saveIndexes(2)` on 75 / `saveIndexes(4)` on
72, `menuParam = items[i].param` (the inventory index, or 22),
`gotoMenu(77)` (pushes the list with its cursor).

`initMenu` case 77 (`:2047-2059`): `type = 6`, `scrollIndex = 0`;
`composeText(1, find(6,0,menuParam)->name)` → `addTextArg` (arg 1); then
`SetYESNO((short)184, 1, A, menuParam)` with **A = 20
(`ACTION_USEITEMSYRING`) if `peekMenu() == 75`, else 21
(`ACTION_USEITEMOTHER`)**. String 3/184 = `"Use %01?"`, so `%01` is the item
name. Note: for param 22 the name comes from `find(6,0,22)` — the
**inventory** def for slot 22, not the ammo def used in the list row.

SetYESNO plumbing (`:3611-3660`): the 4-arg overload composes string 184 *with
the current text args* and forwards `(text, 1, A, menuParam, 2, 0)`. The Text
overload resets args, emits one flags-9 row per `\n`-line (here one: "Use
<name>?"), a flags-9 blank row, **YES** (3/197) flags 8 action A param
menuParam, **NO** (3/198) flags 8 action 2 `ACTION_BACK` param 0, and with
preselect 1 puts the cursor on YES (`numItems - 2`).

NO, or `ACTION_MENU`/`ACTION_BACK` keys (`:2919-2955`) → `back()` → pops to
the list, which `setMenu` **rebuilds** (`:591-603`); cursor restored from the
stack after the rebuild.

### 17a.5 YES: `select` case 20/21 (`:3149-3179`, identical bodies)

```
bool ok = player->useItem((short)menuParam);
if (game->animatingEffects != 0) { returnToGame(); break; }
game->snapMonsters(true); game->snapLerpSprites(-1);
if (canvas->state != ST_MENU) break;
if (ok) { back(); break; }                         // to the rebuilt list
if (menuParam == 22)            gotoMenu(82);      // HOLY_WATER_MAX
else if (16 <= menuParam < 18)  gotoMenu(79);      // HEALTHMSG
else if (11 <= menuParam < 13)  gotoMenu(80);      // ARMORMSG
else                            gotoMenu(81);      // SYRINGEMSG (drinks)
```

The "Ok" row of 79-82 is `ACTION_BACKTWO` (`:3203-3207`): `popMenu` discards
the confirm's stack entry, then `back()` pops to the list (rebuilt).

So a successful use **keeps the menu open** and returns to the list with
fresh counts, unless (a) a lerp-sprite animating effect is running → menu
closes, or (b) `snapMonsters(true)` started a monster attack:
`snapMonsters` with `monstersTurn != 0` runs `monsterAI()` immediately, snaps
all monster lerps and, if a monster attacks, calls `performAttack`
(`src/Game.cpp:2431-2449`), which ends in `canvas->setState(ST_COMBAT)`
unconditionally (`src/Combat.cpp:165`) → the menu is left mid-flow.
Back in the rebuilt list, a used-up row vanishes; the restored
`selectedIndex` is not re-clamped (`back()` writes it after `setMenu`,
`:599`), and 75 shows "[None]" when the last drink is gone.

### 17a.6 `Player::useItem(n)` turn semantics (`src/Player.cpp:801-960`)

- `inventory[n] == 0 && n != 22` → false (`:804-806`).
- Per-item effect; any "already at max" returns **false** before any
  consumption: 16 `addHealth(20)`, 17 `addHealth(80)`, 11 `addArmor(50)`,
  12 `addArmor(10)` (`:810-833`); drinks 0-10 call `addStatusEffect(...)` +
  `translateStatusEffects()` (`:834-934`); 22 = `addHealth(25) |
  addStatusEffect(11,100,3)`, on success `ammo[3] -= 25` and no inventory
  decrement (`b2 = false`, `:935-944`); 23 → true with no side effects
  (`:945-947`); default → false.
- On success: `playSound(1124)`, `--inventory[n]` unless 22, and
  **`game->advanceTurn()` unless n == 8** (`INV_DRINK_SMOOTHIE` sets `b = false`,
  `:884-892`) (`:952-959`).

So using an item from the menu consumes the player's turn (monsters act at
once via the `snapMonsters(true)` above), except item 8.

### 17a.7 Other actions in these screens

- `ACTION_USEITEMWEAPON` (18) on 73: `saveIndexes(1)`; if `i > 0`,
  `selectWeapon(param)` then `returnToGame()` — no confirm, menu closes
  (`:3135-3143`).
- `ACTION_GOTO` 73/75 from 72 saves 72's cursor with `saveIndexes(4)`
  (`:3024-3028`).
- `ACTION_SHOWDETAILS` (16) on the credits row → empty stub (`:3126-3129`,
  `:3993-3995`).
- `ACTION_MENU_ITEM_INFO` on any row with a `helpField` opens the torn-page
  popup with the item description (§15, `:2849-2857`).
- No drop / discard / equip-from-inventory action exists in these screens.

## 17b. Vending menus (83-88)

Verified 2026-10-29 (raw log: `docs/research/2026-10-29-vending-menus.md`).
Line numbers are `src/MenuSystem.cpp` unless a file is named. The minigame
state machine (terminal, hacking, help screen) is out of scope here; see
`docs/original-code/vending-machine.md`.

Ids (`src/Menus.h:303-309`, enum): 83 `MENU_VENDING_MACHINE`, 84 `_DRINKS`,
85 `_SNACKS`, 86 `_CONFIRM`, 87 `_CANT_BUY`, 88 `_DETAILS`, `_LAST` = 88.
Actions (`src/Menus.h:164-166`): 28 `ACTION_CONFIRMBUY`, 29 `ACTION_BUYDRINK`,
30 `ACTION_BUYSNACK`. Currency = **`player->inventory[24]`** (credits) everywhere.

### 17b.1 Entry and exit

* Entry: the vending terminal (minigame `stateVars[1] == 2`) on FIRE with
  `mainTerminalCursor == 0` calls `menuSystem->setMenu(MENU_VENDING_MACHINE)`
  (`src/VendingMachine.cpp:199-201`, `:217-219`). `setMenu` does not push, and
  every ST_MENU exit clears the stack (`src/Canvas.cpp:1033-1035`), so 83 always
  starts with an **empty stack**.
* Leaving 83 by key: `ACTION_MENU` or `ACTION_BACK` on menu 83 calls
  `vendingMachine->returnFromBuying()` instead of `back()` (`:2927-2928`,
  `:2947-2948`). `returnFromBuying` puts the canvas back in `ST_MINI_GAME` with
  `stateVars = {4, 2, 0, -, 0}` (terminal screen) and, if
  `!machineCanBeHacked()` (already hacked, or no tries left), immediately
  `endGame(2)` (`src/VendingMachine.cpp:118-130`, `:132-134`). `endGame` sets
  `game->scriptStateVars[7] = 2`, goes to `ST_PLAYING` and resumes the paused
  calling script thread; from the main-menu minigame it returns to
  `MENU_MAIN_MINIGAME` instead (`src/VendingMachine.cpp:850-864`).
* Leaving by touch: the right soft key (button 15, "Exit") calls
  `returnToGame()` on **every** vending screen (`:4788-4790`) →
  `ST_PLAYING`/`ST_INTER_CAMERA` (`:1209-1220`). This bypasses `endGame`: the
  script thread paused by `EV_MINIGAME` (`unpauseTime = -1`,
  `src/ScriptThread.cpp:1305-1311`) is not resumed and `scriptStateVars[7]` is
  not written. Port artifact; the keyboard path never does this.

### 17b.2 What `menus.bin` contributes (decoded from the shipped file)

All five present screens are **type 9** (`MENUTYPE_VENDING_MACHINE`,
`src/Menus.h:15`); 88 is absent (built in code).

| id | rows (str = file 3 menu strings) |
|---|---|
| 83 | [0] "Ven-ding Ma-chine" (344) flags 73; [1] blank flags 9; [2] "Na-no Drinks" (178) flags 0 GOTO 84; [3] "Snacks" (352) flags 0 GOTO 85 |
| 84 | [0] "Na-no Drinks" (178) flags 73; [1] blank flags 9 |
| 85 | [0] "Snacks" (352) flags 73; [1] blank flags 9 |
| 86 | [0] "Con-firm Or-der" (345) flags 73; [1] blank flags 9 |
| 87 | [0] "Con-firm Or-der" (345) flags 73; [1] blank flags 9 |

No help strings on any of these rows.

### 17b.3 Common header ("Cre-dits: N")

83, 84, 85 (`:2062-2077`, `:2080-2094`, `:2108-2123`) all start with
`loadMenuItems(menu, 0, 1)` (title divider), then a row built as:
`addTextArg(<inventory[24] decimal>)`, `composeText(0, 197)` = "Cre-dits: %01",
reset args, `addTextArg(that)`, `addItem(getLastArgString(), EMPTY, 9, 0, 0,
EMPTY)`; then `loadMenuItems(menu, 1, -1)` (the blank row, and on 83 the two GOTO
rows). So every screen is: title (73) / "Cre-dits: N" (9) / blank (9) / rows.
86/87 build the same credits text but via `getSmallBuffer` (`:2151-2156`,
`:2218-2223`).

### 17b.4 `MENU_VENDING_MACHINE_DRINKS` (84) — prices from `tables.bin` table 10

After the header, `for n16 = 0..10` ascending, if
`vendingMachine->drinkInThisVendingMachine(n16)`: value =
`getDrinkPrice(n16)` as a decimal text arg, row
`addItem(STRINGID(1, def(6,0,n16)->name), getLastArgString(), 32, 28, n16,
STRINGID(1, def->description))` (`:2096-2103`). So label = drink name, value
column = **price** (no "x", no stock), flags 32 `ITEM_SHOWDETAILS`, action 28,
param = inventory slot 0..10. There is **no stock / quantity-on-hand** concept.

`energyDrinkData` = `tables.bin` table 10, int16 (`src/App.cpp:383,399`),
3 shorts per drink: `[normalPrice, hackedPrice, mapMask]`.
`drinkInThisVendingMachine(n) = (data[n*3+2] & 1<<currentMapNumber) != 0`;
`getDrinkPrice(n) = data[n*3 + (machineHasBeenHacked ? 1 : 0)]`
(`src/VendingMachine.cpp:805-811`). `currentMapNumber` is clamped to 1..9 in
`initGame` (`src/VendingMachine.cpp:53`). Decoded shipped table (33 shorts):

| slot | drink | price | hacked | mapMask |
|---|---|---|---|---|
| 0 | Energy Blast | 20 | 15 | 0x03FE |
| 1 | Focus Juice | 25 | 20 | 0x03FE |
| 2 | Super Quench | 30 | 20 | 0x03FE |
| 3 | Power Punch | 50 | 40 | 0x03FE |
| 4 | Snake Venom | 70 | 60 | 0x03FE |
| 5 | Youth Serum | 80 | 70 | 0x03FE |
| 6 | Caffeine Block | 100 | 80 | 0x03FE |
| 7 | Diet Might | 100 | 80 | 0x03FE |
| 8 | Smoothie | 200 | 150 | 0x03FE |
| 9 | Monster Shot | 400 | 300 | 0x03FE |
| 10 | Hell Knight Sweat | 666 | 400 | 0x03FE |

Mask 0x03FE = bits 1..9, so **every machine on every map sells all 11 drinks**
(the filter is a no-op with shipped data). Drink names: §17a / 
`docs/original-code/loot-inventory.md` (useItem table).

### 17b.5 `MENU_VENDING_MACHINE_SNACKS` (85) — cosmetic names, one item

After the header, `fillVendingMachineSnacks(canvas->loadMapID, textbuff)`
(`:2123`, body `:4100-4164`). Set by map: `n2 = 0` for maps 1-3, `2` for maps
7-9, else `1` (`:4103-4109`). One text arg = `getSnackPrice()`, shared by all
rows; each row is `addItem(STRINGID(3, s), getLastArgString(), 0, 28,
STRINGID(3, s), STRINGID(1, def(6,0,16)->description))` — flags 0, action 28,
and **param = the composite string id `3<<10 | s`** (always >= 11, which is how
86/87 tell snacks from drinks). 12 rows per set:

| set | menu string ids (file 3) |
|---|---|
| 0 (maps 1-3) | 358-369: Moon Cake, Moon Shine, Moon Tofu, Moon Paste, Space Chips, Space Coo-kies, Swiss Cheese, Gou-da Cheese, Ched-dar Cheese, Cheese Crack-ers, Mac n' Cheese, Christy's Cake |
| 1 (other maps) | 370-379, 368, 369: Baked Chips ... Rice Cakes, Mac n' Cheese, Christy's Cake |
| 2 (maps 7-9) | 380-390, 369: Ha-ba-nero Nuts ... Sri-ra-cha, Christy's Cake |

`getSnackPrice() = machineHasBeenHacked ? 8 : 10` (`src/VendingMachine.cpp:825-827`).
Every snack is the same item: inventory slot **16** (Snack, `addHealth(20)` on
use). String 353 "Meals" is never used.

### 17b.6 `ACTION_CONFIRMBUY` (28) → `MENU_VENDING_MACHINE_CONFIRM` (86)

`select` case 28 (`:3223-3232`): `menuParam = items[i].param`,
`vendingMachine->currentItemQuantity = 1`, then `gotoMenu(86)` (pushes 84/85
with cursor), or `setMenu(86)` if the current menu is 88 (no push).

`initMenu` case 86 (`:2127-2196`): drink iff `0 <= menuParam < 11`.
Drink: `def = find(6,0,menuParam)`, `price = getDrinkPrice(menuParam)`, name =
`STRINGID(1, def->name)`, buy action 29. Snack: `def = find(6,0,16)`,
`price = getSnackPrice()`, name = `menuParam` (the snack string id), action 30.
`vendingMachine->currentItemPrice = price` (cached; used by the buy and the
quantity checks). Rows:

| # | text | flags | action / param | note |
|---|---|---|---|---|
| 0 | "Con-firm Or-der" (menus.bin 0) | 73 | — | |
| 1 | "Cre-dits: N" | 9 | — | |
| 2 | blank | 9 | — | |
| 3 | blank (menus.bin 1) | 9 | — | |
| 4 | `"%01 x %02"` (3/350) = "<name> x <qty>" | **0** | 29 or 30 / menuParam | help = def description; the only selectable row |
| 5 | `"To-tal Cost: %01"` (3/351), `%01 = price*qty` | 1 | — | |
| 6 | blank | 9 | | |
| 7 | "Press UP/DOWN" (3/346) | 9 | | |
| 8 | "to change quan-ti-ty" (3/347) | 9 | | |
| 9 | blank | 9 | | |
| 10 | "Tap item to buy" (3/348) | 9 | | |
| 11 | blank | 9 | | |
| 12 | "Press back to go back  " (3/349) | 9 | | |

Entry cursor: `initMenu` zeroes `selectedIndex` on a menu change
(`:1227-1230`), then the tail `moveDir(1)` skips rows 0-3 (flags & 1 or empty)
→ **row 4** (`:2734-2739`). The cursor glyph is suppressed on 86/87/88
(`:1064`), so nothing marks it.

Quantity: `ACTION_UP` on 86: if `inventory[24] >= price * (qty+1)` → `++qty`,
`setMenu(86)` (rebuild; same menu, so `selectedIndex` survives)
(`:2859-2864`). `ACTION_DOWN`: if `qty > 1` → `--qty`, rebuild (`:2870-2875`).
So qty ranges 1..floor(credits/price) (or stays 1 when even one is
unaffordable). Touch: `m_vendingButtons` button 0 = up arrow at (320,130), button 1
= down arrow at (320, imgMenuArrowUp->height + 150), images
`vending_arrow_up_glow.bmp` / `vending_arrow_down_glow.bmp` (`:247-261`); same
checks, then `setMenu(86)` (`:4744-4763`). They are rendered only on 86
(`:5174-5175`) and highlight on touch/move (`:4713-4714`, `:4827-4828`).

Every `select` on 86 first plays sound **1125** (`:2983-2985`).

### 17b.7 Buying: `ACTION_BUYDRINK` (29) / `ACTION_BUYSNACK` (30)

* 29 (`:3234-3241`): `vendingMachine->buyDrink(menuParam)`
  (`src/VendingMachine.cpp:813-823`): if `inventory[24] < currentItemPrice *
  currentItemQuantity` → false; else qty times `inventory[24] -=
  currentItemPrice; player->give(0, n, 1, false)`. True → `back()`; false →
  `setMenu(87)`.
* 30 (`:3243-3254`): same affordability check inline; else qty times
  `inventory[24] -= getSnackPrice(); give(0, 16, 1, false)`; then `back()`.
  Failure → `setMenu(87)`.

`give(0, n, 1, false)` (`src/Player.cpp:1010-1031`): adds to `inventory[n]`
(or `inventoryCopy` while `isFamiliar`), **clamped to 999** (credits 9999); the
excess is silently lost — credits are still deducted. With `b = false` it calls
`showInvHelp(n)`, which enqueues the item's first-time help dialog once per
item if help is enabled (`src/Player.cpp:1147-1159`). No turn advance, no
sound besides 1125. **No max-count check and no other refusal reason**: the
only CANT_BUY cause is insufficient credits. Since UP never raises qty past
affordability, 87 is in practice reached only when a single unit is
unaffordable.

After success `back()` pops to 84/85, which `setMenu` rebuilds (fresh credits
header), cursor restored from the stack (`:591-600`).

### 17b.8 `MENU_VENDING_MACHINE_CANT_BUY` (87)

`initMenu` case 87 (`:2198-2255`), same drink/snack split as 86 (no action
choice):

| # | text | flags | action |
|---|---|---|---|
| 0 | "Con-firm Or-der" | 73 | |
| 1 | "Cre-dits: N" | 9 | |
| 2 | blank | 9 | |
| 3 | blank (menus.bin 1) | 9 | |
| 4 | "<name> x <qty>" (3/350) | 1 | |
| 5 | "To-tal Cost: <price*qty>" (3/351) | 1 | |
| 6 | blank | 9 | |
| 7 | "You can't af-ford this!" (3/354) | 9 | |
| 8 | "Tap Screen to Con-tin-ue" (0/96) | 8 | 2 `ACTION_BACK`, help = def description |

Cursor lands on row 8; FIRE → `back()`. Because 87 replaced 86 with `setMenu`,
`back()` pops straight to the list (84/85). `doDetailsSelect` is a no-op on 87
(`:555-557`).

### 17b.9 `MENU_VENDING_MACHINE_DETAILS` (88) — unreachable

`initMenu` case 88 (`:2257-2274`) sets type 9, lays out `detailsTitle` +
a flags-72 divider + `detailsHelpText`, row 0 gets action 28 with `menuParam`.
But nothing ever calls `setMenu/gotoMenu(88)` (only references: `:1064`,
`:2257`, `:3226`, `:4624`, `src/GameStateRunner.cpp:218`), and
`showDetailsMenu()` — what `ITEM_SHOWDETAILS` rows would trigger — is an empty
stub (`:3993-3995`). Dead in this port.

### 17b.10 Navigation summary

| From | Key back (`ACTION_MENU`/`ACTION_BACK`) | Touch left "Back" (btn 11) | Touch right "Exit" (btn 15) |
|---|---|---|---|
| 83 | `returnFromBuying()` → terminal | not shown | `returnToGame()` |
| 84/85 | `back()` → 83 (rebuilt) | `back()` | `returnToGame()` |
| 86 | `back()` → 84/85 | `back()` | `returnToGame()` |
| 87 | `back()` → 84/85 | `back()` | `returnToGame()` |

(`:2919-2955`, `:4785-4790`; buttons enabled per menu `:4617-4627`: 83 only
15, 84-88 both.) `ACTION_LEFT/RIGHT` page up/down; on 86 `UP/DOWN` are
quantity, elsewhere they move the cursor. `ACTION_MENU_ITEM_INFO` opens the
help popup for rows with a help field (drink/snack rows, the 86 item row, 87
row 8) (`:2849-2857`).

Type 9 cursor movement extras in `moveDir` (`:416-455`): wrap-to-top also
resets `scrollIndex = 0`, and the list keeps a 3-row look-ahead (scrolls one
row early when the cursor is within 3 of the window edge). `maxItems = 4`
(`:1231`).

### 17b.11 Presentation

* Background: `imgGameMenuPanelbottom` at the bottom, then
  `vendingBG.bmp` (`vendingMachine->imgVendingBG`, loaded
  `src/LoadingManager.cpp:95`) at (0,0) replacing the in-game menu background;
  no health/armor status line (`:734-741`). The help popup also uses
  `imgVendingBG` at (0,0) instead of the torn page (`:1149-1155`).
* menuRect is set at init to the in-game (70, 0, 340, 320 - panelbottom.h)
  (`:4192-4195`; the per-menu switch has no vending case, `:4422-4424`), BUT
  `paint()` overrides it every frame with `setMenuDimentions(70, 11, 203, 250)`
  for every menu > MENU_ITEMS_HOLY_WATER_MAX (`:847-851`); clip (`:853`) and touch
  areas (`:5091`, `:5143`) read the overridden rect (corrected 2026-10-29 after G6 review);
  `menuItem_width = imgVendingButtonLarge->width`
  (`vending_button_large.bmp`), `menuItem_paddingBottom = 3`; info buttons use
  `vending_button_help.bmp` render mode 2 (`:4208-4219`); action rows use
  `vending_button_large.bmp` (`:4458-4466`). Info buttons appear on 84, 85,
  86 action rows (`:4486`, `:5132-5139`).
* Scrollbar (when content > menuRect[3]): `barRect.x = 350` and
  `vending_scroll_bar.bmp` / `vending_scroll_button_{top,middle,bottom}.bmp`
  (`:2789-2793`). Note the test is `menu >= 83`, so it also hits 89 (comic).
* Soft keys (`drawSoftkeyButtons`, `:5179-5288`): vending art
  `vending_softkey_normal.bmp` / `_pressed.bmp` (`src/Hud.cpp:55-56`); left at
  (56,277), label "Back" (3/80) at (67,307) anchor 36; right at (362,277),
  literal "Exit" at (411,307) anchor 40; font 0.
* `GameStateRunner::menuState` J2ME soft-key ids (not rendered): right = `(0,202)`
  "Buy" on 86/88, `(0,49)` "Details" on rows with flags 32 (drink rows), none on
  87, `(0,121)` "Select" otherwise; left = "Back" (3/80) on 83 explicitly or
  when the stack is non-empty (`src/GameStateRunner.cpp:208-242`).

### 17b.12 Reuse in `new_src` (as of 2026-10-29)

`new_src/core/MenuSession.cpp` already has the pieces: `addParsedRows`
(= `loadMenuItems`, `:530-537`), `addEntityRow` (label/help from entity def,
`:543-555`), value column via `valueBuf_`/`hasValue` (as in
`buildDrinksScreen`, `:557-580`), literal composed labels via
`hasLiteralLabel` (`:737`, `:800`), stack `gotoMenu`/`back`/`popMenu`
(`:1424-1500`), `returnToGame` (`:1501`). Missing for vending: type 9 cases in
`moveDir` (explicitly omitted, `new_src/core/MenuSession.h:135`), the
UP/DOWN quantity override and the 83 back override in `handleAction`,
`screenHasInfoButtons` for 84/85/86 (`:242-245`), and the vending art
(background, large button, softkeys, arrows, scrollbar).

---

# 18. Timed text slots

Seven independent text surfaces. `app->time` is the gameplay clock,
`app->gameTime` the cinematic clock (the two cin slots use `gameTime`).

| Slot | Set by | Duration | Cleared by | Geometry (480x320) | Colour |
|---|---|---|---|---|---|
| message queue — normal (top bar) | `Hud::addMessage(text, flags)` / `getMessageBuffer`+`finishMessageBuffer` (`src/Hud.cpp:163-225`) | `calcMsgTime`: `len <= menuHelpMaxChars (49)` → **700 ms**; else `len*50` ms; `MSG_FLAG_FORCE` doubles it for the first message (`:122-136,195-200`) | `drawTopBar`: `msgCount>0 && time-msgTime > msgDuration+100` → `shiftMsgs()` (`:249-251`); queue cap 5 → oldest shifted (`:178-180`); `msgCount = 0` on menu open (`src/PlayingInputHandler.cpp:146,155`) and on `ST_CAMERA` entry (`src/Canvas.cpp:1208`) | drawn inside the 20 px top strip: `drawString(buf, 2 - screenRect[0], 3, 0, scrollOfs, len)`, `len` clipped to `(hudRect[2]-x)/9 - 1` (`:386-390`) | font default; horizontal marquee: after **750 ms** it slides `(elapsed-750)/50` chars, capped at `len-49` (`:268-277`) |
| message queue — `MSG_FLAG_IMPORTANT` (4) | same `addMessage` with flags&4 | same `msgDuration` | same shift path (`:249-251`) | `drawImportantMessage`: rect `(viewRect[0], viewRect[1], viewRect[2]-viewRect[0]-1, 18)`, text at `+2,+2` anchor 4 (LEFT) (`:396-410`) | fill **`0xFF7F0000`** dark red, white 1 px border (`:258,404-407`) |
| message queue — `MSG_FLAG_CENTER` (2) | same, flags&2; text pre-wrapped to `(viewRect[2]-9)/9` on insert (`:185-187`) | `len*50` **capped at 1500 ms** (`:132-134`) | same shift path; `isShiftingCenterMsg()` reports the expired-but-not-yet-shifted window (`:227-231`); `shiftMsgs` calls `canvas->invalidateRect()` for this flag (`:104-106`) | `drawCenterMessage`: `w = strWidth+8` (≤480), `x = -screenRect[0] + hudRect[2]/2`, `y = -screenRect[1] + 40`, box `16*numLines+3` tall, lines every **16 px** from `y+3`, anchor 17 (HCENTER\|TOP) (`:412-444`) | caller-supplied; the facing-entity path passes **`0xFF000000`** black with a `0xFFAAAAAA` border (`:379,425`) |
| speech bubble | `Hud::showSpeechBubble(strIdx, colorIdx)` from `EV_SPEECHBUBBLE` (`src/ScriptThread.cpp:1247-1252`, `src/Hud.cpp:901-932`) | `bubbleTextTime = app->time + 1500` (`BUBBLE_TEXT_TIME`, `src/Hud.h:38`) | `drawBubbleText`: `app->time >= bubbleTextTime` → dispose buffer, `bubbleText = nullptr`, clear `repaintFlags & 8` (`:942-948`); also self-clears when the buffer is already null (`:938-941`) | box `w = len*9 + 6`, `h = 20`; `y = viewRect[1]+1` (+10 if the facing entity is within `tileDistances[0]`, else +20); player colour → `y = screenRect[3] - 85`; `x = SCR_CX+5` pulled left so it fits `viewRect[2]`; text at `+2,+3` anchor 4; tail 10x6 region of `imgUIImages` at `x+5, y+20` (`:949-1005`) | by index: 0 `0xFF800000`, 1 `0xFF002864`, 2 `Canvas::PLAYER_DLG_COLOR`, 3 `0xFF2E0854`, 4 `0xFFFF9600`, default `0xFF800000`; white border lines. Tail atlas row = 0/10/20/30/45 by colour |
| cinematic subtitle | `EV_CAMERA_STR` with bit15 clear: `subTitleID = STRINGID(loadMapStringID, arg&0x3FFF)`, `subTitleTime = gameTime + operand` (`src/ScriptThread.cpp:519-542`) | `Hud::draw`: `subTitleID != -1 && subTitleTime < gameTime` → `subTitleID = -1` (`:793-798`); forced to −1 on `ST_CAMERA` entry (`src/Canvas.cpp:1209`) and in `Game.cpp:2524` | `drawCinematicText`: wrapped to `subtitleMaxChars` (=`displayRect[2]/9`=53), −7 when `showCinPlayer`; max 2 lines; `y = (cinRect[1]+cinRect[3] + ((screenRect[3] - that - 32) >> 1)) - 10`, `x = SCR_CX` anchor 1 (HCENTER); with `showCinPlayer` a 32x30 face is drawn at x=5 and the text becomes left-aligned at `x = imgPlayerFaces->width + 10`; second line at `+16` (`:468-488`) | font default; no box |
| cinematic title | same op with bit15 set: `cinTitleID`, `cinTitleTime = gameTime + operand` | same tick check (`:787-792`), same −1 clears | `wrapText(n2, 1, '\n')` (1 line) then `drawString(buf, SCR_CX, 1, 1)` — top-centre (`:461-466`) | font default |
| dialog text | `DialogSystem::prepareDialog` | no timer | closed by input (`closeDialog`) | see `docs/original-code/dialog-system.md` §3 | — |
| loot list | `LootingSystem::poolLoot` | no timer | gated on `crouchingForLoot && time > lootingTime + 500`; closing grants the pool (`src/LootingSystem.cpp:87-116,122`) | see `docs/original-code/loot-inventory.md` | body `0xFF660000`, title bar `0xFF000000`, border white |

## 18.1 Coexistence and draw order

The three message-queue variants are **mutually exclusive**: only
`messages[0]` is ever drawn, and flags&4 (important) takes the branch over
the normal/centre one (`src/Hud.cpp:256-280`). A centre message also
suppresses the facing-entity name text for that frame (`:377-391`).

Everything else can coexist. Order inside `Hud::draw` (`:726-820`), by
`repaintFlags` bit:

```
0x1  scope + drawEffects
0x2  drawTopBar   -> monster health bar, then the message slot
0x4  arrow controls / weapon select, drawBottomBar
0x8  drawBubbleText
0x10 drawCinematicText (= cin title, then cin subtitle, then drawBubbleText again)
```

`drawCinematicText` ends with its own `drawBubbleText(g)` call (`:490`), so a
bubble is painted twice when both 0x8 and 0x10 are set. `ST_CAMERA` sets
`hud->repaintFlags = 24` (0x8|0x10) and clears both cin ids
(`src/Canvas.cpp:1207-1213`); `EV_CAMERA_STR` sets `hud->repaintFlags = 16`
(`src/ScriptThread.cpp:539`), i.e. only the cinematic layer.

The dialog box and the loot list are painted **after** the whole HUD, as state
overlays in `Canvas::backPaint` (`src/Canvas.cpp:414-472`), so they sit on top
of every slot above.

## 18.2 Dialog reveal ("typewriter")

**Per line, not per page.** In the page loop
(`src/DialogSystem.cpp:334-354`), for the line whose page-relative index
equals `dialogTypeLineIdx`:

```
n15 = (app->time - dialogLineStartTime) / 25;     // 25 ms per char = 40 cps
if (n15 >= lineLen) { n15 = lineLen; ++dialogTypeLineIdx; dialogLineStartTime = app->time; }
```
Earlier lines of the page render in full, later lines render 0 chars.
`drawString(dialogBuffer, x, y, 0, lineStart, n15)` — the reveal is a char
count, not a clip. FIRE while revealing jumps `dialogTypeLineIdx` to
`dialogViewLines` (finish the page) instead of advancing the page
(`src/DialogSystem.cpp:34-49`). Scrolling one line down restarts the clock with
`dialogTypeLineIdx = viewLines - 1` so only the newly exposed line types
(`:86-88`).

## 18.3 State in `new_src` (as of 2026-08-27)

| Slot | `new_src` state | Fidelity |
|---|---|---|
| message queue | **no queue**: single `centerText_`/`hasCenterMessage_`/`centerTime_`/`centerDuration_` + single `importantText_`/`hasImportant_`/`importantTime_` (`new_src/ui/Hud.h:184-193`) | centre expiry matches (`centerTime_ >= centerDuration_ + 100`, `new_src/ui/Hud.cpp:497-502`) but the default duration is a caller argument (700 ms default) instead of `calcMsgTime`; **`kImportantDurationMs = 3500` is invented** — the original important message uses the same `msgDuration` as any other queue entry. No 5-slot shift, no duplicate-suppression (`compareTo`), no `MSG_FLAG_FORCE` doubling, no 750 ms/50 ms marquee. `drawMessages` draws important **and** centre exclusively, matching the original branch (`new_src/ui/Hud.cpp:469-478`) |
| speech bubble | `bubbleText_`, `bubbleColor_`, `bubbleTextTime_`, `bubbleTextDuration_` default **3000** (`new_src/ui/Hud.h:215-219`) | duration diverges (original 1500 ms); `update()` only accumulates `bubbleTextTime_` and never expires the bubble (`new_src/ui/Hud.cpp:507`) |
| cin subtitle | `hasSubtitle_`, `subText_`, `subTitleTime_`, `subTitleDuration_` | expiry rule matches (`>= duration` → clear, `new_src/ui/Hud.cpp:524-530`); geometry hardcoded to (240, 280) HCENTER\|TOP, no `showCinPlayer` face, no 2-line wrap (`:489-493`) |
| cin title | `hasCinTitle_`, `cinTitleText_`, `cinTitleTime_`, `cinTitleDuration_` | matches; drawn at (240, 1) (`:484-488`) |
| dialog text | `DialogSystem` with the real 25 ms/char reveal and the page machinery | ok |
| loot list | `LootSession` with the 500 ms crouch gate (`new_src/core/LootSession.cpp:105,141`) | ok |
| draw order | `drawMessages` paints important/centre **then** cin title **then** subtitle in one function (`new_src/ui/Hud.cpp:469-493`) | the original interleaves them across `repaintFlags` bits (top bar first, cinematic last) — same relative order, one pass |

# 19. Level stats and loading screens

Screens shown around a map change. The `loadMap` sequence itself (save
files, `ST_TRAVELMAP`, two-phase `loadMedia`) lives in
[`map-change.md`](map-change.md) and [`game-flow.md`](game-flow.md) §3;
this section covers only what is **shown** and how the player leaves it.
Raw log: [`../research/2026-10-29-level-stats-loading.md`](../research/2026-10-29-level-stats-loading.md).

## 19.1 How the stats screen is reached

The stats screen is **not a Canvas state**. It is the menu
`Menus::MENU_LEVEL_STATS = 1` (`src/Menus.h:46`), shown in `ST_MENU`.

1. `EV_CHANGE_MAP` stores `LEVEL_STATS_nextMap = arg & 0xF`; stats are shown
   only when `nextMap >= loadMapID` (going "back" to an earlier map skips
   them) (`src/ScriptThread.cpp:491-494`).
2. With the fade bit (`arg & 0x80`) it starts a 1000 ms
   `FADEOUT|SHOWSTATS` fade (`src/ScriptThread.cpp:496-507`); without it, it
   calls `canvas->saveState(51, 3, 194)` directly (`:509-510`).
3. When the fade completes, `Render::fadeScene` calls
   `canvas->saveState(51, 3, 196)` (`src/Render.cpp:2563-2564`).
   `LoadingManager::saveState` stores `saveType`, sets the loading-bar caption
   and enters `ST_SAVING` (`src/LoadingManager.cpp:47-53`).
   Caption ids (strings file 3): 196 = "Saving Game…", 194 = "Loading Game…"
   (the no-fade path uses 194, apparently a data quirk).
4. Next `Canvas::run` tick, `ST_SAVING` with `saveType = 51 = 0x33`
   (1 player file | 2 world file | 0x10 stats | 0x20 brief save):
   `Game::saveState(loadMapID, LEVEL_STATS_nextMap, spawn from spawnParam…)`
   (`src/Canvas.cpp:865-868`); inside it, bit 0x10 calls
   `player->levelGrade(true)` which **commits** the grade
   (`src/Game.cpp:2069-2071`). Then HUD message `(0,38)` "Game Saved" is queued
   (`src/Canvas.cpp:873`), `playSound(1069, flags 1, 3, …)` — 1069 is
   `Music_Level_End.wav` (`src/Sounds.h:77`, index 69 from line 8 = 0) — and
   `menuSystem->setMenu(MENU_LEVEL_STATS)` (`src/Canvas.cpp:888-890`);
   `saveType = 0; clearEvents(1)` (`:901-902`).
5. The epilogue reuses the same menu: `disposeEpilogue` does `soundStop`,
   `setMenu(MENU_LEVEL_STATS)`, `playSound(1069, 1, 3, false)`
   (`src/IntroSequenceManager.cpp:138-146`).

## 19.2 Content (`menus.bin` row 1 + `initMenu`)

`menus.bin` row for menu 1: type **5 (`MENUTYPE_HELP`)**, 15 items (decoded
from the shipped file with the §11 format). `initMenu` overrides some text
fields with `%NN` argument strings (`MenuStrings::ARGUMENT1..7 = 241..247`,
`src/MenuStrings.h:290-296`; strings are literally "%01".."%07") and fills
the args with `fillStatus(true, false, false)` (`src/MenuSystem.cpp:1244-1255`).

| # | flags | text (file 3) | value (`textField2`) | help |
|---|---|---|---|---|
| 0-2 | 0x9 | empty | — | — |
| 3 | 0x49 NOSELECT\|CENTER\|DIVIDER | `%01` = map name | — | — |
| 4 | 0x9 | empty | — | — |
| 5 | 0 | "Time:" (28) | `%02` | 29 "Time spent on this level" |
| 6 | 0 | "Secrets:" (30) | `%03` | 31 "Secrets found / total" |
| 7 | 0 | "Enemies:" (32) | `%04` | 33 "Enemies killed / total" |
| 8 | 0 | "Turns:" (34) | `%05` | 35 "Turns on this level" |
| 9 | 0 | "Deaths:" (56) | `%06` | 57 "Total number of deaths" |
| 10 | 0 | "Grade:" (36) | `%07` | 37 "Level Grade" |
| 11 | 0 | empty | — | — |
| 12 | 0x9 | empty | — | — |
| 13 | 0x9 | "Tap Screen to" (140) | — | — |
| 14 | 0x9 | "Continue  " (70) | — | — |

(Soft hyphens in the raw strings are stripped by `dehyphenate()`.)
`MENU_INGAME_LEVEL` (32) is the same six rows without the header/footer and is
filled by the same `fillStatus(true,false,false)` (see §11.2).

Argument order in `fillStatus` (`src/MenuSystem.cpp:3951-3967`):

| arg | source | format |
|---|---|---|
| %01 | strings file 3, id `render->mapNameField & 0x3FF` | text |
| %02 | `game->totalLevelTime + (gameTime - game->curLevelTime)` ms | `Player::formatTime`: `H:MM:SS` (hours unpadded) (`src/Player.cpp:1127-1146`) |
| %03 | `game->mapSecretsFound / game->totalSecrets` | `buildFraction` "a/b" (`src/MenuSystem.cpp:3840-3851`) |
| %04 | `player->monsterStats[0] / [1]` after `fillMonsterStats()` | "killed/total" |
| %05 | `player->moves` | integer |
| %06 | `player->currentLevelDeaths` | integer |
| %07 | `gradeToString(levelGrade(false))` | file-3 string |

### Counters during play

- **Time**: `curLevelTime = gameTime` at map load (`src/LoadingManager.cpp:663`);
  `totalLevelTime` is persisted in the world save: `saveWorldState` folds
  `gameTime - curLevelTime` into it and resets `curLevelTime`
  (`src/Game.cpp:1585-1587`), `loadWorldState` restores it or zeroes it when
  the map compile date differs / no file (`src/Game.cpp:1753,1762`).
- **Secrets**: `mapSecretsFound++` in `Game::awardSecret`
  (`src/Game.cpp:3477`), zeroed in `Render::unloadMap` (`src/Render.cpp:306`),
  restored from the world save (`src/Game.cpp:1763`); `totalSecrets` is a map
  header byte (`src/LoadingManager.cpp:373`).
- **Enemies**: not a counter — `Player::fillMonsterStats` scans all entities:
  total = entities with `monster != nullptr` and `!(monster->flags & 0x80)`
  (`MFLAG_NOTRACK`, `src/Enums.h:351`); killed = those with
  `info & 0x1010000` (removed or corpse bits) (`src/Player.cpp:107-126`).
- **Turns**: `moves++` in `Player::advanceTurn` (`src/Player.cpp:54`),
  `moves = 0` in `levelInit` (`src/Player.cpp:96`).
- **Deaths**: `currentLevelDeaths++` in `Player::died` (skipped if already
  `ST_DYING`) (`src/Player.cpp:717`). Reset to 0 only on the **first** load of
  a map (`numLevelLoads[map-1] == 0`, `src/LoadingManager.cpp:59-66`), on a
  brief-save load (`src/Game.cpp:2201`) and on new game
  (`src/MenuSystem.cpp:3595`). Persisted in the player file
  (`src/Game.cpp:1928,1995`).
- **Grade** (`Player::levelGrade`, `src/Player.cpp:2022-2041`): returns the
  already-stored current grade if nonzero; else 0 when `total monsters == 0`
  or `totalSecrets == 0`; else
  `max(((6*((killed<<8)/total)>>8) + (6*((secrets<<8)/totalSecrets)>>8)) >> 1
  - currentLevelDeaths, 1)`. With `b = true` it stores it into the 3-bit
  packed `currentGrades` and raises `bestGrades` (`src/Player.cpp:2034-2039,
  2059-2069`). `gradeToString`: 6→38 "A+", 5→39 "A", 4→40 "B", 3→41 "C",
  2→42 "D", 1→43 "F", else 44 "--" (`src/Player.cpp:1991-2020`).
  Because the save (step 4 above) commits the grade **before** the menu is
  built, the displayed grade is the committed one.

## 19.3 Layout (480x320)

- Background: `imgEndOfLevelStatsBG` (`endOfLevelStatsBG.bmp`, 480x320,
  loaded at startup `src/Canvas.cpp:170`) drawn at (0,0) instead of the usual
  menu backgrounds (`src/MenuSystem.cpp:762-763`).
- Menu rect: `setMenuSettings` → x=23, y=0, w=407, h=320
  (`src/MenuSystem.cpp:4233-4239`, applied at `:4427`); `paint` re-applies
  y=0, h=320 and skips the clip set-up (`src/MenuSystem.cpp:803-806`).
- Font type 0: row height = `FONT_HEIGHT[0] = 16` with
  `menuItem_fontPaddingBottom = 0` (`src/App.h:39`,
  `src/MenuSystem.cpp:4974-4983`, `:4177`) → item *i* top at `y = 16*i`;
  15 rows = 240 px < 320, so no scrollbar (`src/MenuSystem.cpp:2755`).
- Items whose text is the empty string id 3072 and are not dividers are not
  drawn (still take height) (`src/MenuSystem.cpp:931`).
- Row 3 divider: `buildDivider(text, 407/CHAR_SPACING[0]=37)` pads the map
  name with `' '` and `0x80` glyphs on both sides to ~37 chars
  (`src/MenuSystem.cpp:267-282`, `:909`, `:960-963`); centred at
  `x = 23 + 407/2 - width/2`, anchor 0 (`:966-974`, `:1076`).
- Rows 5-10: label at x=23 anchor 0 (TOP|LEFT); value right-aligned at
  `x = 23+407 = 430`, anchor 24 = RIGHT|TOP (`src/MenuSystem.cpp:991-1045`).
- Rows 13-14: centred like row 3.
- No selection cursor: type 5 is excluded (`src/MenuSystem.cpp:1064`).
- Soft keys (`GameStateRunner::menuState`): right soft key = `(0,43)`
  "Continue" for `MENU_LEVEL_STATS`/`MENU_END_RANKING`
  (`src/GameStateRunner.cpp:213-214`, `:243-246`).
- `Render` drops the dizzy FOV wobble while this menu is up
  (`src/Render.cpp:2235-2236`).

## 19.4 Input and exit

- FIRE → `select(selectedIndex)` (`src/MenuSystem.cpp:2891-2892`);
  AUTOMAP key → `select` (`:2903-2904`); BACK → `select` (`:2943-2944`);
  touch release outside any button → `canvas->handleEvent(13)`
  (`src/MenuSystem.cpp:4729-4771`). MENU → `back()`, which with an empty
  stack does nothing for this menu (`src/MenuSystem.cpp:2922-2932`, `:591-611`).
  There is no timer / auto-continue.
- `select` for `MENU_LEVEL_STATS` ignores the item: if `!canvas->endingGame`
  → `canvas->loadMap(LEVEL_STATS_nextMap, true, false)` and return; else
  `gotoMenu(MENU_END_RANKING)` (`src/MenuSystem.cpp:2996-3002`).
  `loadMap(…, b=true)` stops sounds (ends the level-end music) and, because
  `b` is true, does not save again: it sets caption `(3, levelNames[id-1])`
  and enters `ST_TRAVELMAP` (`src/LoadingManager.cpp:69-77`).
- The non-stats path (`nextMap < loadMapID`) goes `FADEOUT|CHANGEMAP` →
  `loadMap(next, false)` → `saveState(43, 3, 196)` (43 = 1|2|8|0x20) →
  `ST_SAVING` → after the save `setState(ST_TRAVELMAP)`
  (`src/Render.cpp:2556-2558`, `src/LoadingManager.cpp:72-74`,
  `src/Canvas.cpp:885-886`).

## 19.5 The loading / saving box (`ST_LOADING` 7, `ST_SAVING` 16)

Both states draw the same box via `LoadingScreenController`; there is no
background art, no progress percentage, no tips.

- Entry: `setState` clears `REPAINT_HUD`, sets `pacifierX = SCR_CX - 66`,
  calls `updateLoadingBar(false)` (`src/Canvas.cpp:1183-1187`). While in
  either state `REPAINT_VIEW3D` is cleared each tick (`src/Canvas.cpp:973-975`).
- Caption: `setLoadingBarText(file, id)` stores ids and sets
  `loadingFlags |= 3` (`src/LoadingScreenController.cpp:9-15`). Sources:
  travel map → `(0,41)` "Loading…" (`src/TravelMapManager.cpp:519`);
  inside `loadMedia` after map data loads: "Game Saved"(0,38) if `isSaved`,
  "Game Loaded"(0,39) if `isLoaded`, else the map name `(3, mapNameField&0x3FF)`
  (`src/LoadingManager.cpp:645-653`); save paths `(3,196)` / `(3,194)`.
  A missing caption (`-1`) falls back to `(0,57)` "Processing…"
  (`src/LoadingScreenController.cpp:30-32`).
- `updateLoadingBar(force)`: unless forced, throttled to once per 150 ms
  (`0x96`) of `upTimeMs`; sets `REPAINT_LOADING_BAR`
  (`src/LoadingScreenController.cpp:17-36`). Painted in `backPaint` after the
  menu (`src/Canvas.cpp:494-497`).
- `drawLoadingBar` (`src/LoadingScreenController.cpp:38-100`), SCR_CX=240,
  SCR_CY=160 (`src/Canvas.cpp:122-123`):
  - when `loadingFlags & 1` (full redraw, then cleared): `eraseRgn(displayRect)`
    (whole screen cleared); `FabricBG.bmp` (32x32) **tiled** into
    (165,131,150,58) (`Graphics::fillRegion`, `src/Graphics.cpp:429-454`);
    white 1-px `drawRect` (165,131,150,58); caption at (240,138) anchor 0x11
    HCENTER|TOP; `(0,58)` "Just a moment!" at (240,154) same anchor.
  - always: bar = black `fillRect(173,171,134,12)` + white `drawRect` same.
  - "fire" pacifier: `pacifierX += 10`; if it reaches `>= SCR_CX+62 = 302`
    or is `< SCR_CX-66 = 174` it resets to 174 → positions 174,184,…,294.
    Draws `loadingFire.bmp` (26x9) region `(0,0,w,9)` at (pacifierX, 173),
    `w = min(26, 307 - pacifierX)`; rotateMode = 6 (180° + mirror = vertical
    flip, `src/Image.cpp:139-142`) when `(pacifierX/10) % 6 < 3`, else 0.
    (The `((x/10)/6)*8` assignment is dead — always overwritten.)
- Timing: `ST_LOADING` with `loadType == 0` runs `loadMedia`: first tick
  draws the box and `flushGraphics()` then returns; the next tick loads the
  whole map synchronously (`src/Canvas.cpp:904-910`,
  `src/LoadingManager.cpp:610-616`). The many `updateLoadingBar` calls during
  the load only set a flag; nothing presents mid-load, so in this port the box
  is effectively one static frame. No minimum display time.
- Not skippable: neither state has an input handler (no `ST_LOADING` /
  `ST_SAVING` branch in `src/InputEventController.cpp`), and the work is
  synchronous. Exit: `loadMedia` ends in `ST_PLAYING`
  (`src/LoadingManager.cpp:739-741`); `ST_SAVING` exits per `saveType` bits
  (`src/Canvas.cpp:879-899`).
