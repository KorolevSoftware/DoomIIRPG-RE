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
| `MENU_ITEMS` 72 | 1 LIST | header + "Weapons" → `MENU_ITEMS_WEAPONS` 73 | inventory root; drink/other pages are built in code (`:1930-2046`) |
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
