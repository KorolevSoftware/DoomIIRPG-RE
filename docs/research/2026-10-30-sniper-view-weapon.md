# 2026-10-30 — Sniper (weapon 9) view weapon shows only the scope

## Hypothesis
User sees only a small scope/lens piece at lower right when holding weapon 9.
Suspect the original composes the weapon-9 view weapon from more than one sprite.

## Method
Read `Combat::drawWeapon` (`src/Combat.cpp:621-844`) and `Combat::getWeaponTileNum`
(`src/Combat.cpp:1766-1784`); compared with `new_src/ui/ViewWeapon.cpp` and
`GameContext::buildViewWeaponModel` (`new_src/core/GameContext.cpp:~915-976`).

## Verdict: CONFIRMED — two-piece composition, rewrite omits the underlay

Evidence (`src/Combat.cpp`):
- `:835-837` `if (weapon == 9) { app->render->draw2DSprite(this->getWeaponTileNum(0), frame, x, y, flags, renderMode, renderFlags, 0x10000); }`
  — weapon 0's art (tile 1) drawn at the SAME x,y, same frame, same renderMode
  (RENDER_NORMAL), same renderFlags (BRIGHTREDSHIFT when hurt), same scale 0x10000.
- `:839` then draws `getWeaponTileNum(weapon)` = tile `1 + 9 - 2 = 8` (`:1780-1781`
  default branch) on top. Tile 8 is only the scope piece; the rifle body is tile 1.
- Order: muzzle flash (`:826-834`, never for weapon 9: gated by `(n14 & 0x200) == 0`
  at `:741` and mask `0x181` at `:826`) -> tile 1 underlay -> tile 8.
- `frame` for weapon 9 is always 0 (`:823-825` sets 1 only for b6 [weapon 1] or
  weapons 13/8 in attack pose).
- No other per-weapon multi-part draw except the sentry-bot stack (tiles 19/18,
  frames 2+3, `:797-813`) and weapon 14 (`:814-820`, single draw, alt tile/frame).
- Whole view weapon skipped while `isZoomedIn` (`src/Canvas.cpp:1348-1349`).

## Rewrite discrepancy
`new_src/ui/ViewWeapon.cpp` draws only `m.weaponTile` (tile 8 for weapon 9); its
comment explicitly lists "weapon 9 underlay (:835-837) deferred". Tables/offsets
(wpinfo pose, tile mapping `new_src/domain/game/Combat.cpp:90-96`) are correct.

## Fix
In `ViewWeapon::draw`, immediately before the main weapon draw, if `w == 9`
draw `spriteTexture(media, m.flashTile /* = tile 1 */, 0)` with
`drawWeaponQuad(g, tex, x, y, 176, 255, gb, gb, magX, magY)` (same tint as main).
