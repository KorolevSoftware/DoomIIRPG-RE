# 2026-08-27 — HUD bottom bar: data sources, geometry, gating

## Hypotheses under test

1. Shield = stat 2 raw, health = stat 0 raw, no percentages.
2. Weapon icon rows = `Hud::drawWeapon` 44 px rows at (268,258); weapon 1 has
   no digits, weapon 13 is special.
3. Keys widget row = bitmask of two keycards.
4. Portrait row = `4 - 5*health/max`, clamped at 0.
5. Our current widget coordinates (weapon 268,258; shield 49,258 / 89,268;
   health 133,258 / 173,268; portrait 224,263, frame 216,255; keys 375,258)
   match the original.
6. `Menu`/`Wait`/`Map` belong to the bottom bar.

## Verdict: CONFIRMED (all six), with three corrections/extensions

* (2) extension: the "special case" list is bigger — weapons 3,4 share row 10
  and 5,6 share row 11; ids ≥15 and unknown → row 13 without digits.
* (3) correction: it is *not* read as a bitmask; it is a nested if-chain over
  `inventory[19]`/`inventory[20]` that yields the same value
  `(inv19>0) | 2*(inv20>0)`. Slot 19 = red, slot 20 = blue (verified from the
  sheet's palette).
* (5) all coordinates match exactly; the only mismatch found is inside our
  `drawNumbers` (missing '/' glyph for weapon 13) — see below.
* (6) the two side labels come from `softKeyLeftID/RightID` via
  `TouchController::drawTouchSoftkeyBar`, which `drawBottomBar` calls first
  (it also paints the panel background); "Wait" is hardcoded in
  `drawBottomBar`.

## Method

* grep/read `src/Hud.cpp` (`drawBottomBar`, `drawWeapon`, `drawNumbers`,
  `drawCurrentKeys`, `draw`, `startup`), `src/TouchController.cpp`,
  `src/SoftKeyController.cpp`, `src/Canvas.cpp` (`backPaint`, `setState`),
  `src/CombatEntity.cpp`, `src/Entity.cpp`, `src/App.cpp`.
* BMP header / palette decode of the shipped sheets inside
  `build_new/new_src/Doom 2 RPG.ipa` (read-only) to confirm row counts and key
  colours.
* tables.bin table 2 dump (offset table 20×uint32 LE, each table prefixed by an
  int32 element count per `src/Resource.cpp:277-291`) for ammoType/ammoUsage.
* strings.idx + strings00.bin decode for soft-key ids 52/55.

## Evidence (verbatim)

`src/Hud.cpp:683-709`:
```
this->drawWeapon(graphics, 268, 258, app->player->ce->weapon, this->m_hudButtons->GetButton(2)->highlighted);
...
graphics->drawImage(imgShield, 49, 258, 0, 0, 0);
this->drawNumbers(graphics, 89, 268, 1, app->player->ce->getStat(2), -1);
this->drawCurrentKeys(graphics, 375, 258);
...
graphics->drawImage(imgHealth, 133, 258, 0, 0, 0);
this->drawNumbers(graphics, 173, 268, 1, app->player->ce->getStat(0), -1);
int v28 = app->player->ce->getStat(0);
int v29 = 4 - 5 * v28 / app->player->ce->getStat(1);
if (v29 < 0) { v29 = 0; }
...
graphics->drawRegion(this->imgPlayerFaces, 0, 32 * v29, this->imgPlayerFaces->width, 32, 224, 263, 20, 0, 0);
graphics->drawImage(this->imgPlayerFrameNormal, 216, 255, 20, 0, 0);
```

`src/Hud.cpp:1122-1124` (ammo bucket):
```
this->drawNumbers(graphics, (x + (this->imgWeaponNormal->width >> 1) + 6), y + 8, 1,
    app->player->ammo[app->combat->weapons[9 * weapon + 4]], weapon);
```

`src/Hud.cpp:1136-1150` (digits, weapon-13 case):
```
v11 = num / 100; v12 = num % 100 / 10; v15 = num % 100 % 10;
if (weapon == 13) { v11 = num % 100 % 10; v15 = 5; }
if (weapon == 13) { v12 = -1; }
graphics->drawRegion(this->imgNumbers, 0, 20 * (9 - v11), 10, 20, x, y, 20, 0, 0);
int posX = x + space + 10;
graphics->drawRegion(this->imgNumbers, 0, 20 * (9 - v12), 10, 20, posX, y, 20, 0, 0);
graphics->drawRegion(this->imgNumbers, 0, 20 * (9 - v15), 10, 20, space + posX + 10, y, 20, 0, 0);
```
`Hud_Numbers.bmp` is 10×220 → 11 rows; ASCII dump of row 10 is a '/' glyph.

`src/Hud.cpp:1158-1172` (keys) and `src/CombatEntity.cpp:72-89` (health clamp)
quoted in `docs/original-code/ui.md` §5/§4.

`src/TouchController.cpp:544-573`:
```
graphics->drawImage(app->menuSystem->imgGameMenuPanelbottom, 0, 320 - ...->height, 0, 0, 0);
... graphics->drawImage(app->hud->imgSwitchLeftNormal, 9, 268, 0, 0, 0);
... graphics->drawString(smallBuffer, 2, 320, 36);
... graphics->drawImage(app->hud->imgSwitchRightNormal, 438, 268, 0, 0, 0);
... graphics->drawString(smallBuffer, 478, 320, 40);
```

`src/SoftKeyController.cpp:98` → `setSoftKeys(0,52, 0,55)`; strings00 type 0:
`52 = "Menu"`, `55 = "Map"`, `53 = "Leave"`, `216 = "Re-turn"`,
`220 = "Dis-card"`, `30 = "Exit"`.

Gating: `src/Canvas.cpp:414-417` (hud->draw), `src/Hud.cpp:747` (bit 0x4 gates
the bottom bar), `src/Canvas.cpp:1057/1066/1081/1095/1114/1131/1184/1213`
(per-state masks 47 / 43 / 24). Flag clears inside `Hud::draw` are commented
out → redraw every frame.

## Cross-check

* Second independent use of the ammo bucket formula:
  `src/Player.cpp:783` (`ammo[weapons[weapon + WEAPON_FIELD_AMMOTYPE]]` in the
  fire guard) and `src/Combat.cpp:353-359` (decrement after an attack).
* Second use of the weapon-icon row table: `Hud::drawWeaponSelection`
  (`src/Hud.cpp:1181-1210`) calls the same `drawWeapon` for the 4-column grid
  at `x = (screenRect[2] - 16 - 4*w)/2`, `y = screenRect[3] - 108`, step
  `w + 4` / row step 48, so any row/geometry change is shared.
* Second use of the keycard slots: `src/Entity.cpp:172-175` (pickup message +
  `repaintFlags |= 0x4`).

## Findings for the rewrite

1. `new_src/ui/Hud.cpp:576-584` deviates: when `h2 == -1` it skips the glyph
   and shifts `h3` to `x+space+10`. Original always draws three glyphs, row 10
   being '/'. Fix: drop the `h2 >= 0` / `h3 >= 0` guards, keep the third glyph
   at `x + 2*space + 20`, and let row `20*(9-h2)` render for h2 = −1.
2. `drawBottomBar` has no caller — add it right after
   `sys_.hud->drawTopBar(...)` in `new_src/core/GameContext.cpp:518` under the
   same `gameplayView` condition (legacy 0x4 group follows the 0x2 group), and
   before the dialog/loot overlays.
3. Needed setters (all sources already exist in `new_src`): health, maxHealth
   (`Player::getHealth/getMaxHealth`), shield (`ce.getStat(STAT_ARMOR)`),
   weapon (`Player::weapon`), ammo
   (`Player::ammo[tables.weaponDef(weapon).ammoType]`), keys
   (`(inv[19]>0) | 2*(inv[20]>0)`).
4. Not modelled at all: soft-key ids (Menu/Map), the "Wait" centre label needs
   only a literal, familiar/sentry-bot bar, touch-button highlight state,
   `characterChoice` portrait sheet selection (marine is correct today).
