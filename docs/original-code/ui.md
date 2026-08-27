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
