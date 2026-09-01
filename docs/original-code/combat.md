# Combat rendering: first-person weapon display + HUD weapon icon (original RE port)

Every claim cites `src/` (legacy RE port). Values extracted from the shipped
`tables.bin` (UnPackGameData).

## 1. View weapon — `Combat::drawWeapon(int sx, int sy)` (`src/Combat.cpp:621-844`)

* Call sites: every 3D repaint from `Canvas::renderScene` after world render +
  `renderPortal`, skipped while `isZoomedIn` (`src/Canvas.cpp:1344-1355`);
  cinematics call it with (0,0) (`src/GameStateRunner.cpp:158`,
  `src/MayaCamera.cpp:313`) and may override the weapon via
  `game->cinematicWeapon` (`src/Combat.cpp:701-704`). Immediate mode —
  redrawn on every invalidate; HUD bottom bar paints over it later.
* **Two anchor sets, selected at RUNTIME — not at compile time** (correction
  2026-08-26, `docs/research/2026-08-26-legacy-draw-path-and-bands.md`; the
  earlier "not the port target" dismissal was misleading):
  `if (!app->render->_gles->isInit)` (`src/Combat.cpp:631`) switches to the
  software-TinyGL variant. `isInit` is a persisted user setting: `true` from
  `gles::GLInit` (`src/GLES.cpp:44`, via `src/Render.cpp:164` /
  `src/App.cpp:107`), then overwritten by `Game::loadConfig`
  (`src/Game.cpp:2036`, run later at `src/App.cpp:120`). Default run = GL path.
  See rendering.md §6.4 for the config layout and the string evidence that the
  reference binary has a `TinyGL:` menu toggle.
  * GL path: `scrX = 480/2 − 44 = 196`, `scrY = 320/2 − 29 = 131`
    (`src/App.h:36-37`, `src/Combat.cpp:627-628`), no per-weapon nudge here,
    `LOWEREDWEAPON_Y = 38`, no scale flag.
  * Software path: `scrX = screenWidth/2 − 62 = 178`,
    `scrY = screenHeight/2 − 18 = 107` (`screenWidth/Height` = `viewRect[2]`
    /`viewRect[3]` = 480/250, `src/Render.cpp:156-159`), `loweredWeaponY += 13`,
    `RENDER_FLAG_SCALE_WEAPON` (×1.35 scaleFactor, `src/Render.cpp:351-353`),
    plus per-weapon nudges for ids 1,2,3-6,7,8,10-13 (`src/Combat.cpp:631-668`)
    and `x += 8, y -= 7` when `isFamiliar` (`:790-794`).
  * The two paths are *meant* to coincide, and they do within ~2 px, because the
    GL path magnifies the quad by K ≈ 1.336 about the viewport centre
    (rendering.md §6.2 CORRECTION) while the software path fakes that with the
    1.35 factor. **Neither path draws a 176×176 gun on canvas.**
* Nothing drawn when `ST_DYING`, `player->weapons == 0` or `weapon == −1`
  (`:706-708`). Brightred flash when recently damaged (`:710-712`).
* Position: `scrY += 38` while `weaponDown` (`:672-674`, LOWEREDWEAPON_Y
  `src/Combat.h:46`); per-weapon bias scrY +3/+10/+12 for weapons
  1/2/3-6 (`:679-693`); `x = scrX + wpX + sx`, `y = scrY − (wpY + sy)`;
  `draw2DSprite` renders a **176×176 quad with top-left at (x,y)**
  (scale 0x10000; `src/Render.cpp:343-373`, top-edge convention proven at
  `src/TinyGL.cpp:751-752,820`).
* `wpinfo` = tables.bin table 1, 6 signed bytes/weapon: idleX, idleY, atkX,
  atkY, flashX, flashY (`src/Combat.h:53-59`, load `src/App.cpp:374,390`).
  Extracted table (idle/attack/flash):

  | id | weapon (view tile) | idle | attack | flash |
  |----|--------------------|------|--------|-------|
  | 0 | assault rifle (1) | (−13,88) | (−8,82) | (1,12) |
  | 1 | chainsaw (2) | (−33,80) | (−53,91) | (30,26) |
  | 2 | holy water pistol (3) | (−9,89) | (−4,81) | (0,0) |
  | 3 | sentry bot shoot (4) | (−23,65) | (−15,88) | (30,18) |
  | 4 | sentry bot explode (5) | (−23,65) | (−15,88) | (30,18) |
  | 5 | sentry bot red shoot (13) | (−23,65) | (−15,88) | (30,18) |
  | 6 | sentry bot red explode (14) | (−23,65) | (−15,88) | (30,18) |
  | 7 | super shotgun (6) | (−8,88) | (0,82) | (−1,14) |
  | 8 | chaingun (7) | (−20,88) | (−14,82) | (5,20) |
  | 9 | AR w/ scope (8) | (−13,88) | (−14,82) | (30,18) |
  | 10 | plasma gun (9) | (−2,88) | (1,82) | (30,18) |
  | 11 | rocket launcher (10) | (0,88) | (8,82) | (22,26) |
  | 12 | BFG (11) | (−44,88) | (−44,66) | (30,18) |
  | 13 | soul cube (12) | (−20,88) | (0,88) | (−25,−32) |
  | 14 | picked-up world weapon (15) | (−10,40) | (−20,88) | (−30,−48) |

* Tile mapping `getWeaponTileNum` (`src/Combat.cpp:1766-1784`): `n<5 → n+1`,
  `5→13`, `6→14`, `14→15`, else `n−1`. Media frame = `mediaMappings[tile] +
  frame` (`src/Render.cpp:2047`). Weapon tiles are 256×256 media
  (`newMappings.bin` mediaDimensions, verified by direct parse: tile 1 media
  0-3 all 256×256) with the gun in the lower part of the cell — tile 1 f0
  mediaBounds = x 81…174, y 104…177; tile 1 f3 (muzzle flash) = x 5…163,
  y 3…164.
* **The quad shows only the top-left 176×176 texels** of that 256×256 media
  (fixed UV window `s,t = 1024·176/{sWidth,tHeight}`, `src/GLES.cpp:539-542`);
  at scale 0x10000 that is a 1:1 pixel blit, at 0x8000 a 0.5× blit into an
  88×88 quad. Nothing is ever scaled by 256/176 — see rendering.md §6.2.
* `(x,y)` are pixels **relative to the 3D GL viewport**, whose top-left sits
  at canvas (1,7) and whose centre is canvas (240,131) (rendering.md §6.1) —
  and the quad is then **scaled about that centre by Kx = 1.33578 /
  Ky = 1.33975** in gameplay — **Kx = 1.22102 / Ky = 1.22423 in cinematics**,
  because `ST_CAMERA` renders with `fov = 315` instead of 290
  (`src/MayaCamera.cpp:309`, aspect 163 vs 150; rendering.md §6.2-6.3):
  `X = 240 + (x−239)·Kx`, `Ybottom = 131 + (y+v12−124)·Ky`.
  **Superseded claim:** earlier revisions said the canvas quad is `(1+x, 7+y)`
  with no scaling; that produced the too-small/too-high rewrite gun.
  Worked example, rifle (weapon 0) idle, no shake: `x = 196 − 13 = 183`,
  `y = 131 − 88 = 43`, `v12 = 176` →
  canvas quad **(165, 22)-(400, 258)** (235×236 px), visible gun pixels
  (media bounds x 81…174, y 104…177 inside the 176-texel window)
  canvas **(273, 162)-(399, 261)**, the last ~6 rows clipped by the viewport
  bottom edge at canvas y = 255 — i.e. the rifle touches the bottom HUD panel.
  (Old, wrong values were quad (184,50)-(360,226) / gun (265,154)-(358,226).)
* Cinematic weapon (`b2 = state == ST_CAMERA && game->cinematicWeapon != -1`,
  `src/Combat.cpp:699-705`): `weapon = game->cinematicWeapon`,
  `scrY -= CAMERAVIEW_BAR_HEIGHT (20)`, and `b5` forces the **attack pose**
  (`wpX/wpY = wpAtkX/wpAtkY`, `:735-737`). Quirk: the per-weapon `scrY` bias
  switch (`:679-693`) runs on the *player's* weapon id, while `wpinfo` is read
  for `cinematicWeapon`. Drawn from `MayaCamera::Render` with `drawWeapon(0,0)`
  after the world (`src/MayaCamera.cpp:311-314`), i.e. under the fov-315
  projection (K ≈ 1.221). Example, `cinematicWeapon = 0` and player weapon 0:
  `x = 188`, `y = 29` → quad canvas (177,14)-(392,230), gun art canvas
  (276,142)-(391,232).
* Sprite selection (`src/Combat.cpp:797-840`): sentry-bot weapons 3-6 draw
  tiles 19/18 frames 2+3 stacked with a timed blink (flags 0x20000 XOR over
  `(time + n*1337)/1024 & 0xF` windows, second copy at y+1..2); weapon 14
  draws `activeWeaponDef->tileIndex` frame 1 at flash offset when
  `ammo[8] > 0`, else tile 15 frame 0 (def(6,1,14) placeholder,
  `src/Player.cpp:1636-1656`); muzzle flash for weapons {0,7,8} = tile 1
  **frame 3** at `(x+flashX+40, y+flashY+40)`, renderMode 5, scale 0x8000
  (`:826-834`); weapon 9 underlays tile 1 before its own tile (`:835-837`);
  `frame = 1` during chainsaw-return or weapons 13/8 attack pose
  (`:822-825`); main draw `:839`.
* Fire animation: hold attack offsets until `flashDone`, then 16.16 lerp
  back to idle over `animTime = weapons[weapon*9+8] (SHOTHOLD, table 2) ×10`
  (×5 with statusEffects[2]) (`:311-321,735-767`); chainsaw jitters ±2px
  then eases (`:752-761`). No reload/switch animations exist — weapon switch
  swaps art instantly; the only transition is the 200 ms lower/raise lerp
  (`shiftWeapon`, `:846-855`, LOWERWEAPON_TIME `src/Combat.h:47`), triggered
  by NPC-facing/dialog (`src/MovementController.cpp:134-156`), weapon-select
  (`src/PlayingInputHandler.cpp:482`).
* Status overlay `drawEffects` (`:612-619`): tile 234 frames 0-3 at
  bottom-center while statusEffects[13] active.

## 2. HUD weapon icon — `Hud::drawWeapon` (`src/Hud.cpp:1048-1125`)

(Full bottom-bar widget map, geometry, gating and per-weapon ammoType table:
`docs/original-code/ui.md`.)

Bottom-bar icon at (268,258) (`src/Hud.cpp:683`) from
`imgWeaponNormal`/`imgWeaponActive` 44px-tall rows; texY per weapon id:
{0:0, 1:1, 2:2, 3/4:10, 5/6:11, 7:3, 8:4, 9:5, 10:6, 11:7, 12:8, 13:9,
14:12, default 13}; ammo digits via `drawNumbers` (weapon 1 none, weapon 13
shows "N5" style). Strip image is 102×616 (14 rows).

## 3. Starting weapon

EVT 617 `GIVEITEM def=1` → entity def tile 1 = item(eType 6, sub 1, parm 0)
→ weapon id 0 (assault rifle), then MESSAGE str 133 "You equipped your
weapon." Weapon ids are the parm of item defs (6,1,parm); id 14 is the
picked-up-world-weapon placeholder (tile 15).

## 4. Rewrite status (audit 2026-08-26)

Ported: icon strip + texY map + selection grid + digits
(`new_src/ui/Hud.cpp:24-48,514-545`); `drawBottomBar` written but never
called (`new_src/ui/Hud.h:117`); Player weapons bitmask/index
(`new_src/domain/game/Player.h:25-29`, auto-equip `Player.cpp:99-113`);
Tables weaponInfo/weaponData parsed (`new_src/io/Tables.h:27-28,52-53`);
media texture cache `World3D::ensureSpriteTexture`
(`new_src/render/World3D.h:105-109`). Missing: everything of the view-weapon
draw (tile fn, wpinfo read, 176×176 blit after `drawBSP` in
`new_src/core/GameContext.cpp:994-1011`), and `Hud::weapon_` is hardcoded 3
(`new_src/ui/Hud.h:159`). Scope list:
`docs/research/2026-08-26-hero-choice-and-weapon.md` §B.3.

### 4.1 View-weapon placement audit (2026-08-26, group 3 landed)

`GameContext::drawViewWeapon` (`new_src/core/GameContext.cpp:611-695`) now
implements the draw. Correct: anchors 196/131 (`:620-621`), per-weapon scrY
bias 3/10/12 (`:625`), wpinfo indices and values (`:631-636`, Tables payload
offset verified: table 1 payload starts at file byte 388 →
weapon 0 = (−13,88,−8,82,1,12)), `sy = −|sy|` and
`x = scrX+wpX+sx / y = scrY−(wpY+sy)` (`:664-665`), flash gate masks 0x200 /
0x181 and the +40/+40 flash offset (`:670-675`), `getWeaponTileNum`
(`new_src/domain/game/Combat.cpp:69-78`), animTime = SHOTHOLD×10
(`new_src/domain/game/Combat.cpp:248-254`, rifle 50 → 500 ms), flashTime = 1 ms.

Two placement bugs (deltas vs §1):

1. **Source rect** — `:693` blits the whole media
   (`drawImage(tex, 0,0, tex->width(), tex->height(), x,y, 176,176)`) instead
   of the 176×176 texel window, i.e. a 176/256 = **0.6875× downscale**. The
   art is bottom-anchored inside the window (rows 104…177), so the gun ends up
   both 31% too small and ~40 px too high (bottom edge 62 px too high). Fix:
   `srcW = srcH = 176`. Same for the muzzle flash at `:674-675` (dst 88×88 must
   sample src 176×176, i.e. 0.5×, not 256→88 = 0.34×).
2. **Viewport origin AND centre magnification** — our coordinates are absolute
   canvas, the original's are viewport-relative (+1,+7 canvas) *and* magnified
   by K ≈ 1.336 about canvas (240,131) (rendering.md §6.2 CORRECTION, §6.4).
   This second factor was missed by the 2026-08-26 audit and is why the shipped
   rewrite gun is ~1.34× too small and ~35 px too high. Our 3D world is rendered full-canvas
   480×320 (`new_src/render/RenderBackend.cpp:59-69`, camera aspect
   `(290<<14)/((480<<14)/320) = 193` at `new_src/core/GameContext.cpp:1276-1277`)
   versus the original 478×248 at canvas (1,7) with aspect 150, so the horizon
   moved from canvas y 131 to y 160. Keeping the gun on the horizon under the
   current full-canvas projection means `scrX = 197`, `scrY = 167`
   (= centre 240/160 plus the original centre-relative offsets −43/+7);
   restoring the legacy viewport instead makes plain `196/131 + (1,7)` exact.

Known omissions (all cited in the code): weaponDown/shiftWeapon lerp, chainsaw
jitter, sentry-bot stack, weapon 9/14 branches, `RENDER_ADD50` additive blend
for the flash (currently a plain alpha blit), BRIGHTREDSHIFT damage tint.

## 5. Fire pipeline & damage math (added 2026-08-26; full evidence in
`docs/research/2026-08-26-combat-stage1.md` §2)

* ACTION_FIRE probe (`src/PlayingInputHandler.cpp:189-378`) is a **6-tile** ray (see
  §7 for the full rules) along
  −view[2]/−view[6]/−view[10], mask 13997 CONTENTS_WEAPONSOLID radius 2; chainsaw
  shrinks it to 1 unit (+PLAYERCLIP bit), holy-water pistol adds 0x4100
  (`:200-221`). It only ELECTS the target (ET_MONSTER wins immediately `:264-269`;
  corpses only at Chebyshev distance² **exactly** == tileDistances[0], and never
  blocking the scan, `:279-337` — §7.4); loot preempts everything at `:374-378`.
* `Player::fireWeapon` guards (`src/Player.cpp:754-799`): soul cube needs monster
  target; `weaponDown` or `(disabledWeapons & 1<<weapon)` refuse; ammo short →
  msgs 115/116/117.
* Distance is **Chebyshev**: `Entity::distFrom = max(dx²,dy²)` (`src/Entity.cpp:
  1155-1158`); `tileDistances[j] = (64(j+1))²` (`src/Combat.cpp:41-44`);
  `WorldDistToTileDist` = first j with dist < threshold (`src/Combat.cpp:1235-1242`).
* Hit roll `CombatEntity::calcHit` (`src/CombatEntity.cpp:157-298`):
  chance = ((acc − agi·96>>8)<<8)/100 − 16·(tiles outside RANGEMIN..RANGEMAX),
  floor 1; fixed-range weapons out of range → CR 0x400 hard miss; shotgun/pistol
  far shots cap via CR 0x4; miss streaks capped (player 1, monster 2);
  crit = chance/20 → CR 0x2; sniper-zoom (wp 9) uses pixel body-part bboxes instead
  (`:173-216`).
* Damage `CombatEntity::calcDamage` (`src/CombatEntity.cpp:300-378`): base
  STRMIN..STRMAX of `weapons[w*9+{0,1}]`; crit → ×2 STRMAX, far → STRMAX/2;
  difficulty 4 −25% first; strength bonus applied only when CR 0x20 is **clear**
  (`src/CombatEntity.cpp:326-332`, player gets `3*(strPct*dmg>>8)`), and 0x20 is set for every
  **non-chainsaw** hit on a MONSTER (`src/Combat.cpp:223-226`) ⇒ the strength stat boosts
  **only the chainsaw** vs monsters (§9); weakness =
  `(monsterWeakness[(sub·3+parm)·8+w/2] nibble + 1)<<5` then `damage = weak·base>>8`
  (`src/Combat.cpp:29-31`); final `−(def%·damage)>>8`. Monster→player splits armor:
  `armorDmg = min(((171·base>>8)+1)/2, armor); damage −= 2·armorDmg` (`:357-359`).
* Sequencing: hitscan (PROJTYPE 0) allocates no missile and applies
  `explodeOnMonster()` → `Entity::pain(totalDamage,…)` in stage 0
  (`src/Combat.cpp:1572-1576`, `:1421-1430`, `:904-910`); death `died(true,
  playerEnt)` lands one SHOTHOLD animation later in stage 1 (`:382-386`).
  animTime = SHOTHOLD×10 ms (×5 haste [2]) — same fields the view weapon reads
  (§1). Ammo deducted at stage 0 (`:355-358`), repaintFlags |= 0x4 (`:353`).
* Weapon data (tables.bin table 2, verified by direct parse): rifle(0) 8–10 dmg,
  range 0–5, bullets×1, 2 shots, SHOTHOLD 50; shotgun(7) 25–30, shells×2;
  chaingun(8) 12–15 ×4, SHOTHOLD 15. monsterStats table 3 row index
  `subType*3+parm`, hp stored as byte×5 (`src/Combat.cpp:37-39`): imp(3,0)=50hp/
  def 5/acc 95/agi 0. Spawn-time difficulty bump +25% hp on difficulty 4 (all)
  and 2 (non-boss) (`src/Entity.cpp:62-67`).

## 6. Turn structure (added 2026-08-26)

`Game::advanceTurn` (`src/Game.cpp:1238-1281`): Error 95 if
`interpolatingMonsters`; haste parity picks monstersTurn 1 vs 2 (only
haste-resistant act on 2, `:890`); `player->advanceTurn` status ticks
(`src/Player.cpp:51-92`); door auto-close sweep; PER_TURN static func 6 =
SCR_PER_TURN (`src/Enums.h:498-510`; map00 staticFuncs[6]=253). Monster phase runs
per frame via `updateMonsters` (`src/Game.cpp:2458-2474`, driven from
`src/GameStateRunner.cpp:181-183`): `monsterAI` walks the circular
`activeMonsters` list calling `aiThink(false)` and collecting queued attackers on
`combatMonsters` (`src/Game.cpp:874-913`); when lerps settle an attacker fires
`performAttack`, else `endMonstersTurn` clears the window (`:2452-2456`). Input
during the window drops unless monsters are snappable (which itself advances their
lerps/attacks) except turn/weapon-switch (`src/PlayingInputHandler.cpp:71-74`).
A player attack consumes its turn only after the combat seq finishes
(`src/GameStateRunner.cpp:26-38`).

## 7. Facing entity (`player->facingEntity`) and the monster health bar
(added 2026-08-26; raw log `docs/research/2026-08-26-facing-entity-health-bar.md`)

### 7.1 The probe — `MovementController::checkFacingEntity` (`src/MovementController.cpp:28-158`)

Gated by the latch `canvas->updateFacingEntity` (`:32-34`, cleared at `:157`),
but in the C++ port the HUD forces it every rendered frame while
`ST_PLAYING`: `drawTopBar` is reached through `repaintFlags & 0x2` which is
never cleared (`src/Hud.cpp:735-742` — `updateFacingEntity = true;
canvas->checkFacingEntity();` right before `drawTopBar`). So: recomputed each
frame, not once per turn.

Single ray (`src/MovementController.cpp:38`), `view` = `tinyGL->view` (14.14):

```
start = (destX + (-view[2]*28 >> 14), destY + (-view[6]*28 >> 14), destZ + (-view[10]*28 >> 14))
end   = (destX + (6*-view[2] >> 8),  destY + (6*-view[6] >> 8),  destZ + (6*-view[10] >> 8))
game->trace(start, end, ignore=nullptr, mask=21741, radius=2, zCheck=canvas->isZoomedIn)
```

* Origin is the player's **logical tile centre** `destX/destY/destZ`, pushed
  **28 world units** (of 64/tile) forward — still inside the player tile.
* End point is **384 units = 6 tiles** ahead of the tile centre; the ray is
  therefore ~5.56 tiles long. **This is the key fact: the bar is not limited
  to the adjacent tile.**
* `-view[2] / -view[6] / -view[10]` = forward vector in 14.14 (`16384` = 1.0);
  `x*28>>14` = 28 units, `6*x>>8` = `6*64` = 384 units.
* Capsule radius 2 units; Z overlap is only tested when zoomed in
  (`b == isZoomedIn`, see `src/Game.cpp:275-286`).
* Mask 21741 `0x54ED` = WORLD, MONSTER, NPC, DOOR, ITEM, DECOR,
  ATTACK_INTERACTIVE, SPRITEWALL, DECOR_NOCLIP (table in
  `docs/original-code/player-collision.md` §2.2). Not in the mask, hence
  invisible to the probe: PLAYER(1), PLAYERCLIP(4), ENV_DAMAGE(8),
  **CORPSE(9)**, MONSTERBLOCK_ITEM(11), NONOBSTRUCTING_SPRITEWALL(13).
  (`eType == 11` is tested at `:42` but can never be produced — dead branch.)

`Game::trace` (`src/Game.cpp:199-327`) details that matter here:

* Broad phase = the entity-per-tile lists `entityDb[i + 32*j]` over the AABB of
  the segment inflated by `radius`, clamped to `[0,2047]` (`:213-221`).
* `eType == 0` entities in the lists are skipped (`:229`); world geometry is
  added once via `render->traceWorld` **only if mask bit 0 is set**, as
  `entities[0]`, together with `traceCollisionX/Y/Z` (`:295-311`).
* Entity narrow phase: interpolating monsters (`goalFlags & 0x1`) are tested at
  their **goal tile centre** `32 + (goalX<<6)` (`:232-238`); circle radius²
  `625` (25 u) for everything, `256` (16 u) for ENV_DAMAGE(8) (`:271-274`);
  sprites with `mapSpriteInfo & 0xF000000` are tested as a ±32 line segment
  (axis chosen by `& 0x3000000`) — that is how spritewalls/doors are hit
  (`:249-269`).
* Hits are bubble-sorted by frac ascending and `traceEntity = traceEntities[0]`
  (`:313-326`), i.e. **nearest hit wins**; the whole sorted list stays available
  in `traceEntities[]/traceFracs[]/numTraceEntities`.
* Walls: `Render::traceWorld` (`src/Render.cpp:1212-1284`) is 2D-only; line
  flag `n7 = lineFlags & 0x7`: `4` and `6` are ignored (pass-through), `5` is
  ignored unless the mask has `0x10`/`0x800` (both absent from 21741, so
  pass-through for this probe), `7` is one-sided (back faces skipped), all other
  values block. So a solid wall between player and monster becomes the nearest
  hit → `facingEntity` is the WORLD entity → no bar.

**Monster promotion re-scan** (`:41-86`): only when the nearest hit is
ITEM(6) / MONSTERBLOCK_ITEM(11) / SPRITEWALL(12) / ATTACK_INTERACTIVE(10) /
DECOR_NOCLIP(14), the sorted list is walked from index 0 and the target may be
replaced by something further along the ray:

| encountered eType | action |
|---|---|
| 2 MONSTER | promote to the monster unless the current pick is a SPRITEWALL(12); `break` |
| 5 DOOR, 4 PLAYERCLIP, 0 WORLD | `break` (blocks promotion) |
| 12 SPRITEWALL with `render->mapFlags[linkIndex] & 0x2` | `break` (opaque tile flag; `mapFlags` low nibble comes from the packed map table, `src/LoadingManager.cpp:558-563`) |
| 7 DECOR | promote only if current pick is SPRITEWALL(12); `break` |
| 14 DECOR_NOCLIP | promote if current pick's `eSubType != 6` |
| 10 ATTACK_INTERACTIVE with eSubType 1/2/3 | promote if current pick is not ITEM(6) |
| anything else | `++i` (keep scanning) |

**Distance gate** (`:88-93`), `distFrom` = **Chebyshev squared**
`max(dx², dy²)` (`src/Entity.cpp:1155-1158`), `tileDistances[j] =
(64*(j+1))²` (`src/Combat.cpp:42`):

```
if (eType != 2 && dist > tileDistances[2] /* 192² = 36864, 3 tiles */) facingEntity = nullptr;
```

i.e. **monsters are kept at the full ray length (up to 6 tiles); every other
type is dropped beyond 3 tiles.** The remaining branches only fire help
messages at `dist <= tileDistances[0]` (4096, 1 tile): NPC→help 0,
ATTACK_INTERACTIVE sub 1/2/3→help 2/3/9, DOOR→help 1/7, ITEM sub 3→help 4,
`tileIndex == 158`→help 18 (`:94-121`).

Sleeping monsters count: `activate`/`deactivate` only move entities between the
`activeMonsters`/`inactiveMonsters` AI rings (`src/Game.cpp:756-855`), they never
unlink from `entityDb`, so an un-activated monster is a valid `facingEntity`.

The tail of the function (`:123-156`) is a *separate* probe with mask 4141
`0x102D` used for lowering the weapon / auto-help, not for the bar.

### 7.2 Invalidation of `facingEntity`

Direct clears: `Game::removeEntity` (`src/Game.cpp:192`), `Game::unloadMapData`
(`src/Game.cpp:651`, together with `hud->lastTarget = nullptr` at `:654`),
`Player::reset`-path (`src/Player.cpp:497`), and
`PlayingInputHandler::endOfHandlePlayingEvent` when a turn/strafe actually
started moving (`src/PlayingInputHandler.cpp:589-593`).
`updateFacingEntity = true` (recompute request) is set from ~25 sites, e.g.
`src/Canvas.cpp:1075`, `src/Hud.cpp:738`, `src/Entity.cpp:527,1367`,
`src/Game.cpp:1269,2427,2441,2464,2533,3015,3123,3128`,
`src/MovementController.cpp:180,304,308,471,478,542`, `src/ZoomController.cpp:84,91`,
`src/MayaCamera.cpp:385`, `src/DialogSystem.cpp:620`, `src/ScriptThread.cpp:658,842`,
`src/Player.cpp:161,1930`.

### 7.3 The bar — `Hud::drawMonsterHealth` (`src/Hud.cpp:822-899`)

Called from `drawTopBar` (`src/Hud.cpp:246-248`) whenever
`canvas->state != ST_DYING`, i.e. after the top panel image, so the bar is drawn
over the 3D view.

Skip rules:
* `facingEntity == nullptr || facingEntity->monster == nullptr` → clear
  `lastTarget` (+ `invalidateRect` if it was set) and return (`:826-831`).
  This is what filters walls/doors/items: they have no `monster`.
* `eType == 3` (NPC) → return; `eType == 2 && (info & 0x20000) == 0` → return
  (`:833-835`). `info` bit `0x20000` = entity spawned/alive: set in
  `Entity::initspawn` for MONSTER and ATTACK_INTERACTIVE (`src/Entity.cpp:77,89`),
  cleared by the remove path (`src/Entity.cpp:431-433`), and required by
  `Entity::pain` (`src/Entity.cpp:286`) / `Combat::hurtEntityAt`
  (`src/Combat.cpp:1122`).
* Note CORPSE(9) can never appear (not in mask 21741).

Drain animation (`:836-860`): `stat = ce.getStat(1)` (max hp),
`stat2 = ce.getStat(0)` (current hp). Target change → snap
(`monsterStartHealth = monsterDestHealth = stat2`, `changeTime = 0`);
hp change on the same target → `start = old dest`, `dest = stat2`,
`changeTime = app->time`. `start` is clamped to max. If
`app->time - changeTime > 250` the displayed value snaps to `stat2`, else

```
shown = start - (start - dest) * (time - changeTime) / 250
```

Geometry (`:861-899`), with `screenRect[2] = 480`, `SCR_CX = 240`,
`viewRect[1] = 20` on this port (`src/Canvas.cpp:86,122,125`):

```
n  = 25                                   // segments
n2 = ((n<<8) * ((shown<<16)/(max<<8)) >> 8) + 256 - 1 >> 8   // = ceil(25*shown/max)
if (n2 == 0 && shown > 0) n2 = 1
n3 = 6                                    // y offset from viewRect[1]
if (eSubType == 5 /*PINKY*/ && parm == 0) n3 = 50
else if (canvas->isZoomedIn)              n3 += 20   // → 26
n4 = 2 * (screenRect[2]<<8) / 128 >> 8    // 480 → 7  (segment width)
if (isBoss()) ++n4; else if (n4 & 1) ++n4 // 480 → 8 either way
n5 = 2 + n4*n                             // 202   (frame width)
n6 = SCR_CX - (n5>>1)                     // 139   (frame x)
fillRect (n6, viewRect[1]+n3, n5, n4*2+1)  color 0xFF000000
drawRect (n6, viewRect[1]+n3, n5, n4*2+1)  color 0xFFAAAAAA
n6 += 2
for i in 0..n2-1: fillRect(n6, viewRect[1]+n3+2, n4-1, n4*2-2); n6 += n4
```

480x320 concrete values: frame `x=139, y=26, w=202, h=17`; segments
`w=7, h=14` at `y=28`, pitch 8, first at `x=141`. Zoomed in: `y=46`.
Pinky with `parm == 0`: `y=70`.
Segment colors: `n2 <= n/4` (≤6) → `0xFFFF0000` red;
else `n - n2 <= n/4` (n2 ≥ 19) → `0xFF00FF00` green; else `0xFFFF8800` orange.
`isBoss()` = `eSubType` in `[FIRSTBOSS..LASTBOSS]` and not
(MASTERMIND with `parm == 0`) (`src/Entity.cpp:1398-1400`).

### 7.4 Bar target vs shot target — two independent traces

The ACTION_FIRE handler runs its **own** trace
(`src/PlayingInputHandler.cpp:218-221`): origin `canvas->viewX/viewY/viewZ`
(interpolated eye, *not* `dest + 28·fwd`), end `+ n7·fwd` with `n7 = 6` tiles
(`n7 = 1` for melee, `CheckWeaponMask(w,2)`), radius 2, `zCheck = isZoomedIn`,
mask `n5 = 13997 | (weapon==2 ? 0x4100 : 0) | (melee ? 0x10 : 0)` — includes
CORPSE(9) and NONOBSTRUCTING_SPRITEWALL(13), excludes ITEM(6)/DECOR_NOCLIP(14)
unless the weapon adds them. Target selection is a different priority walk over
the sorted hits (`:222-368`) plus a weapon-range check
`WorldDistToTileDist(dist) > weapons[w*9+3]`. `facingEntity` is used in that
handler only to set `lootingSystem.lootSource` (`:189-194`).
So: **same length (6 tiles), same radius, different origin, mask and selection
rules** — the bar target and the shot target are computed separately and can
disagree (e.g. an ITEM in front only blocks the bar's probe, a CORPSE only
blocks the shot probe).

## 8. Target acquisition — the ACTION_FIRE scan (added 2026-08-26)

Full evidence: `docs/research/2026-08-26-fire-target-acquisition-corpse-tile.md`.
This section supersedes the compressed §5 bullet on the probe.

### 7.1 Chain

`AVK_SELECT → ACTION_FIRE` (`src/InputEventController.cpp:40`, dispatch
`:356,362,382`) → `PlayingInputHandler::handlePlayingEvents`
(`src/PlayingInputHandler.cpp:19`, fire branch `:189`) → probe `Game::trace`
(`:221`) → election loop (`:223-354`) → pruning/special cases (`:356-497`) →
`Player::fireWeapon` (`:515,523,536`; body `src/Player.cpp:754-798`) →
`Combat::performAttack` (`src/Combat.cpp:54`) → `Combat::runFrame`/`playerSeq`
(`:857`, `:178`) → hit roll (monster: `calcCombat` `:224`; other:
`Combat::calcHit` `:249`, body `:864-883`) → `explodeOnMonster` (`:885`,
`pain` `:907`/`:935`, corpse gib `:937-945`) → stage 1 `curTarget->died(true,
playerEnt)` (`:384`, body `src/Entity.cpp:424`).

`curTarget` is **exactly** the entity elected by the scan — `performAttack`
stores its argument verbatim (`src/Combat.cpp:58-59,75-79`). It is never
`player->facingEntity` (that only feeds `curTarget` in dialog styles 1/5 for
eType 2/3, `src/DialogSystem.cpp:619-627`); in the fire branch `facingEntity` is
used solely to set `lootingSystem.lootSource` for eType 10
(`src/PlayingInputHandler.cpp:190-195`). A shot into geometry passes the
ET_WORLD entity `&game->entities[0]` (`:515,536`).

### 7.2 The probe ray

`trace(viewX, viewY, viewZ → viewX + n7·(-view[2])>>8, …, mask, radius 2,
isZoomedIn)` (`src/PlayingInputHandler.cpp:218-221`). `view[]` is 14-bit fixed
(1.0 = 16384, `src/TinyGL.cpp:206-208`) so `n7·(-view[2])>>8` = `n7·64` world
units = **n7 tiles**: `n7 = 6` for all weapons, `n7 = 1` for melee
(`CheckWeaponMask(weapon,2)` — chainsaw only) (`:208-215`).
Mask `13997 = CONTENTS_WEAPONSOLID` (`src/Enums.h:31`) = eTypes
{0 world, 2 monster, 3 npc, 5 door, 7 decor, 9 corpse, 10 attack-interactive,
12 spritewall, 13 nonobstructing-spritewall}; chainsaw `|= 0x10` (playerclip),
holy-water pistol (weapon 2) `|= 0x4100` (env-damage + decor-noclip).
Note `CONTENTS_WEAPONSOLID = CONTENTS_PLAYERSOLID (13501) − 16 + 512`: guns see
corpses, the player walks through them.

**The ray starts on the player's own position**, so `Game::trace`
(`src/Game.cpp:199-323`, AABB inflated by the radius `:213-221`) returns
own-tile entities. `Render::CapsuleToCircleTrace` clamps `t` to `[0,len²]`
(`src/Render.cpp:1113-1118`) and returns `(t>>2)−1` (`:1123`), so an entity on
the start point gets fraction **−1** and the bubble sort
(`src/Game.cpp:311-322`) makes it element 0. Hit threshold is
`radius² + 625` = 629 (r ≈ 25; 256 instead of 625 for eType 8),
`src/Game.cpp:274`. The Z window is only applied while zoomed in
(`src/Game.cpp:277-284`).
Contrast: the `facingEntity` trace starts **28 units ahead**
(`src/MovementController.cpp:40`) and uses mask 21741 (no bit 9), so it can
never see an own-tile entity nor any corpse.

### 7.3 Election rules per eType (`src/PlayingInputHandler.cpp:223-354`)

| eType | rule | terminates loop? |
|---|---|---|
| 0 WORLD / 12 SPRITEWALL / 4 PLAYERCLIP | becomes target if none yet (`:229-236`) | **yes, always** |
| 2 MONSTER | always wins, `n6 = 0` (`:264-269`) | yes |
| 3 NPC | target only if `dist >= 8192` (not adjacent) (`:257-263`) | only then |
| 5 DOOR | first one wins (`:270-277`) | yes |
| 7 DECOR | only if `mapSpriteInfo[sprite] & 0xFF == 0x95` (`:339-343`) | only then |
| 9 CORPSE | only if `dist == tileDistances[0]`; chainsaw → attack target, highest `linkIndex` of the pile (`:279-317`); any other weapon → **loot** target `n6 = 1` if `lootSet != nullptr` and (`monster == nullptr && param == 0`) or `(monster->flags & 0x800) == 0`, and `!isZoomedIn` (`:318-334`) | **never** |
| 10 ATTACK_INTERACTIVE | `(1 << eSubType & 1) == 0` (eSubType ≠ 0 FURNITURE) or chainsaw (`:238-247`); later dropped when `WorldDistToTileDist(dist2) > weapons[w*9+3]` (`:359-361`) | yes |
| 8 ENV_DAMAGE | holy water, sub 1, `ammo[3] >= 2` (`:335-338`) | yes |
| 13 | stored in `entity2`, promoted for melee (`:249-254`, `:362-364`) | no |
| 14 DECOR_NOCLIP | sub 7 (water spout) at 1 tile (`:344-348`) | yes |
| 6 ITEM / 11 MONSTERBLOCK_ITEM | never a fire target (not in the mask; excluded from the fallback `:350`) | – |

`n6 != 0` (loot elected) short-circuits the whole handler into `ST_LOOTING` +
`poolLoot` (`:356-358`), and makes the eType 3/10 branches `break` without
overriding the corpse.

### 7.4 Distances, range, and the own-tile rule (root cause note)

* `Entity::distFrom` = `max(dx², dy²)`, Chebyshev **squared**
  (`src/Entity.cpp:1155-1158`); the scan measures from `viewX/viewY`,
  `Combat::calcHit` from `destX/destY` (`src/Combat.cpp:867`).
* `tileDistances[j] = (64(j+1))²` (`src/Combat.cpp:41-44`);
  `WorldDistToTileDist` = first `j` with `d < tileDistances[j]`
  (`:1235-1242`) ⇒ own tile 0, adjacent (incl. diagonal) 1.
* Weapon range = `weapons[w*9+2] RANGEMIN` / `+3 RANGEMAX` in tiles
  (`src/Combat.h:28-29`). It does **not** bound the scan; it only gates
  eType-10 pruning (`src/PlayingInputHandler.cpp:359-361`), non-monster
  `Combat::calcHit` (`src/Combat.cpp:867-871`, out of range ⇒ `crFlags 0x400`,
  hit 0, and `entity->info & 0x20000` required `:872-874`), and the accuracy
  penalty `−16·tilesOutside` (`src/CombatEntity.cpp:219-238`).
  Parsed table 2 ranges: every player weapon has RANGEMIN 0; RANGEMAX = 5
  except chainsaw 1, holy water 3, shotgun 3, bot-explode 4. Therefore
  `RANGEMIN == RANGEMAX` is never true for the player and both code paths keyed
  on it (`src/CombatEntity.cpp:234-236`,
  `src/PlayingInputHandler.cpp:510-516`) are dead for player weapons; the
  player's hard out-of-range miss comes from `crFlags 0x40`, set when
  `(1 << weaponId & 0x77FF) == 0`, i.e. **only weapon 11 rocket launcher**
  (`src/Combat.cpp:204-206`).
* **An entity on the player's own tile is not protected by any generic rule.**
  It is traced, sorted first, and would pass `Combat::calcHit` (tileDist 0 is
  inside every weapon's range). Standing on a corpse does not block fire only
  because the eType-9 branch (a) requires the *exact* equality
  `dist == tileDistances[0]` — own tile yields `dist == 0` — and (b) contains no
  `break`, so the loop continues (`:352`) and the ET_MONSTER branch
  (`:264-269`) elects the monster ahead. A corpse is thus fully transparent to
  gunfire except at exactly one tile, where non-chainsaw weapons turn the
  keypress into looting and the chainsaw gibs it
  (`Combat::explodeOnMonster` targetType 9, `src/Combat.cpp:937-945`).
* Only the chainsaw is adjacency-limited: id 1 = `WP_CHAINSAW`
  (`src/Enums.h:137`), the only bit in `WP_MELEEMASK = 2` (`src/Enums.h:155`),
  RANGEMAX 1, 1-tile probe; every other player weapon uses the full 6-tile line
  probe. `Player::fireWeapon` routes chainsaw-vs-CORPSE/BARRICADE/FURNITURE to
  `usedChainsaw(false)` (`src/Player.cpp:777-779`).
* Note: `src/Hud.cpp:975-985` `tileDistances[0]` is the **dialog bubble**
  vertical offset (`n += 10` within a tile, else `+= 20`), not a weapon range.

## 9. Melee = the chainsaw only (added 2026-08-31; raw log
`docs/research/2026-08-31-melee-weapons.md`)

### 9.1 Identification
* `WP_CHAINSAW = 1` (`src/Enums.h:137`) is the **only** bit of `WP_MELEEMASK = 2`
  (`src/Enums.h:155`); tests go through `Entity::CheckWeaponMask(w, 2)` =
  `(1 << w) & 2` (`src/Entity.h:52-54`) or the literal `weapon == 1`.
* **No fists/punch weapon exists in the shipped build:** `WP_PUNCH_MASK = 0`
  (`src/Enums.h:153`) makes `(1 << weapon & 0x0)` dead (`src/Combat.cpp:732`,
  `src/PlayingInputHandler.cpp:381`), and `Combat::punchingMonster` is never set to a
  non-zero value anywhere in `src/` (only `= 2` inside a `> 0` branch,
  `src/Combat.cpp:201-202`). All `punchingMonster` / `punchMissed` code is unreachable.
* Melee is **not** a table column. The 9-byte row has no melee flag; `PROJTYPE`(6) `= -1`
  (`WP_PROJ_NONE`) for the chainsaw but also for weapons 4/6/23, and `WP_PROJ_MELEE = 1`
  (`src/Enums.h:120`) is used only by monster weapons 15-18.
* Chainsaw row of tables.bin table 2 (verified by direct parse; column names
  `src/Combat.h:26-35`): `STRMIN 18, STRMAX 22, RANGEMIN 0, RANGEMAX 1, AMMOTYPE 0,
  AMMOUSAGE 0, PROJTYPE −1, NUMSHOTS 1, SHOTHOLD 100`.

### 9.2 No ammo, ever
`AMMOTYPE 0 = AMMO_NONE` (`src/Enums.h:103`) and `AMMOUSAGE 0` make every gate a no-op:
`Player::fireWeapon` (`src/Player.cpp:780-781`), `Combat::playerSeq`
(`src/Combat.cpp:210-215`), `shouldFakeCombat` (`src/PlayingInputHandler.cpp:574-579`),
starter-ammo grant at pickup (`src/Entity.cpp:256`, gated on AMMOUSAGE), and the deduction
`ammo[ammoType] -= ammoUsage` (`src/Combat.cpp:349-352`) which becomes `ammo[0] -= 0`.
The HUD likewise prints no digits for weapon 1 (§2). **The chainsaw needs no fuel.**

### 9.3 Probe: 1 tile + PLAYERCLIP
`src/PlayingInputHandler.cpp:197-221`: `n7 = 6` tiles normally, `n7 = 1` and
`n5 |= 0x10` (bit 4 `ET_PLAYERCLIP`) for melee ⇒ mask `14013`. The commented
`n5 |= 0x2000` at `:202-204` is a J2ME-only line (bit 13 is already in 13997).
`n7` is a tile count because `view[]` is 14.14 and `n7*16384>>8 = n7*64` units.
Separately, `ET_NONOBSTRUCTING_SPRITEWALL (13)` is remembered in `entity2` **only for melee**
(`:247-252`) and then overrides the elected target unless it is a monster(2) or corpse(9)
(`:362-364`) — the saw can cut decorative sprite-walls that guns pass through.
`RANGEMAX 1` means a target beyond one tile fails `calcHit` with `crFlags 0x400`
(msg 64, zero damage) even if some other path elected it.

### 9.4 Damage application is immediate (PROJTYPE −1)
`Combat::launchProjectile` has no case for −1 (or 0), so it falls into
`default: missileAnim = 0; exploded = true; return;` (`src/Combat.cpp:1566-1570`); the very
next statement of stage 0, `updateProjectile()` (`:355`), sees `exploded` and calls
`explodeOnMonster()` → `Entity::pain(totalDamage, playerEnt)` in the **same frame**
(`src/Combat.cpp:1421-1431`, `:905-910`, `src/Entity.cpp:284-345`).
*Rewrite pitfall:* keying "hitscan" on `PROJTYPE == 0` alone leaves the chainsaw (−1) with no
damage path at all.

### 9.5 Chainsaw-specific rules
| where | rule |
|---|---|
| `src/Combat.cpp:223-226` | `crFlags 0x20` **not** set for the chainsaw ⇒ `calcDamage` adds the player strength bonus `3*(strPct*dmg>>8)` only for the saw (`src/CombatEntity.cpp:326-332`) |
| `src/CombatEntity.cpp:146-148` | on a landed hit `player->usedChainsaw(true)`; `src/Player.cpp:2636-2653` counts +1 (+1 more on crit), at ≥30 → `modifyStat(STAT_STRENGTH,2)` + msg 241, and `startShake(666,1,0)` every time |
| `src/Player.cpp:777-779` | vs CORPSE / BARRICADE / FURNITURE the counter also ticks via `usedChainsaw(false)` before the attack |
| `src/Combat.cpp:890-892` | a **missed** saw swing clears `render->shotsFired` (no noise wake-up) |
| `src/Combat.cpp:936-944` | corpse target ⇒ gib (hide sprite, blood, `spawnDropItem`, sound 1037); only the saw elects corpses (§8.3) |
| `src/PlayingInputHandler.cpp:238-239` + `src/Combat.cpp:876-882` | `ET_ATTACK_INTERACTIVE` FURNITURE (eSub 0) is targetable and damageable **only** by the chainsaw: table 4 = `{0, 0x2, 0xFFFFFFFB, 0xFFFFFFFB}`, and both shipped furniture defs (tiles 121/135) have `parm == 1` → mask `0x2` = bit 1 |
| `src/MovementController.cpp:198-212` | `flagForWeapon` = **4096** for melee (8192 default, 16384 rocket launcher) → tile scripts can react to the saw specifically |
| `src/Combat.cpp:301-303` | sound 1015 = `chainsaw.wav` (`src/Sounds.h:23`, id = 1000+index), played hit or miss |
| `src/Combat.cpp:327` | `WP_NORECOIL = 9222 = 0x2406` (bits 1,2,9,13) ⇒ no `rockView` recoil |
| `src/Enums.h:158` | `WP_MUZZLE_FLASH = 385` (weapons 0,7,8) ⇒ no flash |
| `src/Combat.cpp:751-761,822-825` | jitter `+2/−1 px` while `n15 < 43690` (≈2/3 of the 1000 ms), then a 3× eased return with art **frame 1** |

### 9.6 Weakness nibble
`getWeaponWeakness(w, sub, parm)` = `(monsterWeakness[(sub*3+parm)*8 + w/2] >>
((w&1)<<2) & 0xF) + 1 << 5` (`src/Combat.cpp:29-31`): the chainsaw (`w=1`) reads the **high**
nibble of byte 0 of the row, the rifle the low nibble. Parsed table 11: `sub 0` ZOMBIE byte0
`0xF7` → 512 = **×2.0**; `sub 4` SAW_GOBLIN `0x07` → 32 = **×0.125**; all other subtypes
`0x77` → 256 = ×1.0 (`src/Enums.h:61-78`). Note the weakness/strength path exists only for
`targetType == 2`; props and walls take a plain table roll
`STRMIN + rnd%(STRMAX−STRMIN)` (−25% on difficulty 4) in `Combat::playerSeq:249-259`.

### 9.7 One hit per press
`NUMSHOTS 1` ⇒ `animLoopCount = 1` (`src/Combat.cpp:113`), so the stage-1 repeat branch
`--animLoopCount > 0` (`:381-386`) never fires: exactly one damage application and one turn
per `ACTION_FIRE`. There is no hold-to-saw mode.

## 10. PROJTYPE: which weapons are real missiles and which are instant (added 2026-08-31; raw log `docs/research/2026-08-31-projtype-inventory.md`)

`PROJTYPE` = column 6 of the 9-byte weapon row (`src/Combat.h:32`), read into
`attackerWeaponProj = weapons[id*9 + 6]` (`src/Combat.cpp:102,121`) and dispatched by
`Combat::launchProjectile` (`src/Combat.cpp:1433-1573`).

### 10.1 Dispatch
* `projType == 12` is handled **before** the switch: `soulCubeIsAttacking = true; launchSoulCube(); return;` (`src/Combat.cpp:1439-1443`).
* `switch (attackerWeaponProj)` (`src/Combat.cpp:1479`) has cases **2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13** — each allocates a flying missile GameSprite and sets `missileAnim` (240, 243, 225/226, 244, 227, 242, 241, 171, 252, 248, `activeWeaponDef->tileIndex` respectively).
* Everything else hits `default: { missileAnim = 0; exploded = true; return; }` — in shipped data that is exactly **−1 (`WP_PROJ_NONE`), 0 (`WP_PROJ_BULLET`), 1 (`WP_PROJ_MELEE`)**. `exploded = true` makes the same stage-0 call apply damage through `updateProjectile → explodeOnMonster / explodeOnPlayer` (`src/Combat.cpp:1421-1431`).
* `WP_PROJ_INSTANT = 3` (`src/Enums.h:134`) is a misleading alias of `WP_PROJ_PLASMA = 3`; it does **not** mark the instant class.

### 10.2 Shipped classification (table 2, 32 rows × 9 bytes)
Player weapons (all 15 have an `ET_ITEM`/`IT_WEAPON` def, `tmp_entities.bin` defs 57-71 / tiles 1-15, `src/Entity.cpp:238-256`):

| projType class | weapon ids |
|---|---|
| **instant** (`default:`) | 0 rifle (0 BULLET), 1 chainsaw (−1), 3 shooting sentry (0), 4 exploding sentry (−1), 5 red shooting sentry (0), 6 red exploding sentry (−1), 7 super shotgun (0), 8 chaingun (0), 9 scoped rifle (0) — **9 ids** |
| **real missile** | 2 holy water (2 WATER), 10 plasma (3), 11 rockets (4), 12 BFG (5), 13 soul cube (12), 14 thrown item (13) — **6 ids** |

Monster weapons: **instant** = 15 BITE, 16 CLAW, 17 PUNCH, 18 CHARGE (all projType **1 MELEE**), 23 M_FIRE (−1), 24 M_MACHINE_GUN (0), 25 M_CHAIN_GUN (0). **Missile** = 19 (6), 20 (7), 21 (8), 22 (9), 26 (4), 27 (10), 28 (5), 29 (8), 30 (11), 31 (5).

Consequence for the rewrite: a `projType > 0 ⇒ unsupported` gate is **exact for player weapons** (rejects the 6 that truly need a missile pool, rejects nothing wrongly) but **wrong for monsters**: ids 15-18 have `projType == 1` yet are instant in the original.

Full row dump (STRMIN, STRMAX, RANGEMIN, RANGEMAX, AMMOTYPE, AMMOUSAGE, PROJTYPE, NUMSHOTS, SHOTHOLD) is in the raw log. Note that STRMIN/STRMAX are stored as signed bytes and read with `& 0xFF` in `CombatEntity::calcDamage` (`src/CombatEntity.cpp:303-304`), so BFG = 200..220, soul cube = 250 (×2 at `:306-309`), red exploding sentry = 140..155.

Table-2 parsing recipe (the only correct one): the 80-byte header of `tables.bin` holds 20 LE ints that are **end** offsets relative to the header, so table *i* starts at `80 + (i ? off[i-1] : 0)` and begins with an LE int payload size (`src/Resource.cpp:209-247`, `getNumTableBytes` at `:257-263`). Table index 2 = `TBL_COMBAT_WEAPONDATA` (`src/App.cpp:360,391`), payload 288 bytes = 32 × 9, matching `WP_MAX = 32`.

### 10.3 Visuals on the instant path
`default:` sets `missileAnim = 0`: bullets have **no tracer and no impact sprite** in the original. Feedback comes from the muzzle flash (`WP_MUZZLE_FLASH = 385` = weapons {0,7,8}, `src/Combat.cpp:826-834`), `rockView` recoil (`WP_NORECOIL = 9222` exempts {1,2,9,13}, `src/Combat.cpp:327`) and `explodeOnMonster` blood (`src/Combat.cpp:905-910`).

**One exception:** monster melee ids 15/16/17/18 get a hit-splat anim from the `numActiveMissiles == 0` branch of `Combat::updateProjectile` (`src/Combat.cpp:1400-1420`): on `gotHit && (totalDamage > 0 || totalArmorDamage > 0) && exploded`, `missileAnim` = 245 (CLAW/16), 246 (BITE/15) or 247 (PUNCH/CHARGE), allocated at the attacker's screen position with `flags |= 0x800` and `S_RENDERMODE = 5`; anim 245 inherits bit `0x20000` from the attacker sprite. So "instant damage" alone is not a complete port of projType 1.

## 11. Water spout conversion — `INTERACT_PICKUP` (eSubType 3) fixtures (added 2026-08-31; raw log `docs/research/2026-08-31-water-spout.md`)

`ET_ATTACK_INTERACTIVE` / `INTERACT_PICKUP` (`Enums::INTERACT_PICKUP = 3`, `src/Enums.h:57`) is
**not** a plain destructible: a toilet (tile 123) or a sink (tile 127) is a *liftable* fixture,
and both the "grab it" and the "smash it" paths replace the entity by a **water spout**
(tile 134, `TILENUM_WATER_SPOUT`, `src/Enums.h:745`), which is the holy-water refill station.

Shipped defs (`entities.bin`, 190 records, layout `src/EntityDef.cpp:35-44`):

| tile | eType | eSubType | parm | name |
|---|---|---|---|---|
| 123 toilet | 10 `ET_ATTACK_INTERACTIVE` | 3 `INTERACT_PICKUP` | 3 | 131 |
| 127 sink | 10 | 3 | 3 | 132 |
| 134 water spout | 14 `ET_DECOR_NOCLIP` | 7 `DECOR_WATER_SPOUT` (`src/Enums.h:49`) | 1 | 133 |

### 11.1 `ArmorRepairSystem::turnEntityIntoWaterSpout` — the whole conversion
`Canvas::turnEntityIntoWaterSpout` is a one-line forwarder (`src/Canvas.cpp:1615`) to
`src/ArmorRepairSystem.cpp:56-63` (the function lives in that class for historical reasons only;
nothing armor-related happens):

```
entity->def  = lookup(TILENUM_WATER_SPOUT);                   // 134 -> eType 14, eSub 7, parm 1
entity->name = (short)(entity->def->name | 0x400);            // string set 1, id 133
mapSpriteInfo[sprite] = (mapSpriteInfo[sprite] & 0xFFFFFF00) | 134;
entity->info |= 0x400000;                                     // "state consumed" (save flag)
```

Exactly four effects, and note what is **absent**:
* **no unlink / relink** — the entity keeps its tile link and its sprite slot. It stops blocking
  purely because collision/trace masks are `1 << def->eType` sets (`src/Game.cpp:745`,
  `src/MovementController.cpp:125-128`) and `ET_DECOR_NOCLIP (14)` is in none of the solid masks
  (player move mask `13501` = `0x34BD`, `src/MovementController.cpp:326`).
* **no `mapSpriteInfo |= 0x10000`** — the sprite stays visible.
* **no Z / scale change** — Z and SCALEFACTOR keep the fixture's values (32 / 64 defaults from
  `src/LoadingManager.cpp:497-498`).
* **frame bits are preserved**: the mask is `0xFFFFFF00`, not `0xFFFF00FF`, so bits 8-15 survive.
  For normal (non-Z) map sprites those bits are always 0 (only Z-sprites get a byte at
  `src/LoadingManager.cpp:537`), so the inherited frame index is 0 — **but that is irrelevant**:
  tile 134 has its own render branch that ignores the frame bits and animates from the clock,
  `int n15 = app->time / 128; renderSprite(..., n3, (n15 & 0x1), ...); return;`
  (`src/Render.cpp:1648-1653`). The spout IS animated — 2 frames, 128 ms each
  (media 733/734, `mediaMappings[134..135] = 733,735`). The generic auto-animation bit
  `0x80000` (`src/Render.cpp:1544-1546`) is indeed never set here and is not needed.
  Corrected 2026-08-31; see `docs/original-code/rendering.md` §7.
* `info |= 0x400000` is the persisted "already used" bit: `Entity::saveBinaryState`
  (`src/Entity.cpp:1816`) and `getBinaryState` for `ET_DECOR_NOCLIP/DECOR_WATER_SPOUT`
  (`src/Entity.cpp:1467-1471`) store it, and `restoreBinaryState` re-applies the conversion on
  load (`src/Entity.cpp:1866-1871`).

### 11.2 Path A — smashing it (`Entity::pain`)
`src/Entity.cpp:379-391`, the `ET_ATTACK_INTERACTIVE` tail of `pain`:

```
else if (eSubType == Enums::INTERACT_PICKUP) {
    app->particleSystem->spawnParticles(1, -1, sprite);
    app->canvas->turnEntityIntoWaterSpout(this);
    return b;                      // <- early return
}
app->game->removeEntity(this);     // skipped for eSubType 3
this->info |= 0x400000;            // skipped (but set inside the conversion)
app->render->mapSpriteInfo[sprite] |= 0x10000;   // skipped -> sprite stays visible
```

* Particle burst: type **1**, color **-1** (white), i.e. the narrow upward jet
  (Z velocity 96..110, XY spread `±64>>2`, `src/ParticleSystem.cpp:284-291`) — the same effect the
  thrown-toilet impact uses (`src/Combat.cpp:1342-1346`, projType 13 + sound 1135
  `Weapon_Toilet_Smash.wav`).
* Confirmed: `pain` returns **before** `removeEntity`, so the fixture is never hidden or unlinked;
  the conversion alone does the work. `pain` itself plays no sound for this subtype (unlike
  eSubType 1 → sound 1038 `glass.wav`, `src/Entity.cpp:378`).
* `pain` is gated by `info & 0x20000` (`src/Entity.cpp:286`), set for every
  `ET_ATTACK_INTERACTIVE` at spawn (`src/Entity.cpp:88-89`).

### 11.3 Which weapons can smash it
Damage eligibility for props: `Combat::calcHit` (`src/Combat.cpp:876-882`) requires
`tableCombatMasks[def->parm] & (1 << player->ce->weapon)`. Table 4 =
`{0, 0x2, 0xFFFFFFFB, 0xFFFFFFFB}`; toilet/sink have `parm == 3` → mask `0xFFFFFFFB`.

**Any weapon except id 2 (`WP_HOLY_WATER_PISTOL`, `src/Enums.h:138`) can smash a toilet/sink** —
this is NOT chainsaw-only (chainsaw-only is `parm == 1`, i.e. furniture tiles 121/135).
Target election agrees: `src/PlayingInputHandler.cpp:238-239` accepts any `eType == 10` whose
`eSubType != 0`, regardless of the equipped weapon (`(1 << eSubType & 0x1) == 0`), and the
range check at `:380` uses the weapon's RANGEMAX. Bit 2 is cleared exactly so that the holy water
pistol never destroys the water source (it refills from it instead, §11.5).
There is **no** chainsaw-specific branch for eSubType 3; the chainsaw usage counter is explicitly
*not* ticked for it (`src/Player.cpp:777-779` lists only CORPSE / BARRICADE / FURNITURE).

### 11.4 No message, no XP, no counter
`Combat::playerSeq` sets `targetKilled = true` for any non-monster target hit with damage
(`src/Combat.cpp:349-351`) and then calls `curTarget->died(true, playerEnt)`
(`src/Combat.cpp:383-386`) — but `pain` (which ran in the same stage, `src/Combat.cpp:933-936`)
has **already replaced `def`** by tile 134. So inside `died` (`src/Entity.cpp:424-448`)
`eType == 14`, the `ET_ATTACK_INTERACTIVE` branch is not taken and the fixture gets:
* **no message 89** ("<name> destroyed"),
* **no `addXP(5)`**,
* **no `destroyedObject()`**,
* only `info &= 0xFFFDFFFF` (`:434`, clears `0x20000`) → the spout is permanently immune to
  further `pain`/`calcHit` — and `updateFacingEntity = true` (`:527`).

The guard `eSubType != 3 && eSubType != 2` at `src/Entity.cpp:445` is therefore about
**crates** in practice (their `pain` keeps the def), and it mirrors the spawn-time census
`src/Game.cpp:448-450` which also excludes eSubType 2 and 3 from `numDestroyableObj` — so the
"objects destroyed" statistic never counts toilets/sinks/crates on either side.
The explosion path is identical in ordering: `pain` then `died` (`src/Combat.cpp:1189-1193`).

### 11.5 Path B — pulling it off the wall, and the refill
All of this lives in the **ACTION_FIRE** handler, not in a separate "use" action.

Trace mask (`src/PlayingInputHandler.cpp:200-207`): `n5 = 13997`, and **only** when
`weapon == 2` `n5 |= 0x4100` — bit 14 = `ET_DECOR_NOCLIP`. So an existing water spout is
traceable/electable only while the holy water pistol is equipped; election requires an exactly
adjacent tile: `eType == 14 && eSubType == 7 && dist == tileDistances[0]`
(`src/PlayingInputHandler.cpp:354-359`).

1. **Fixture still intact** (`src/PlayingInputHandler.cpp:405-431`), requires
   `eType == 10 && eSubType == 3 && dist2 <= tileDistances[0] && player->ammo[8] == 0`
   (`ammo[8]` = `AMMO_ITEM`, `src/Enums.h:112` — i.e. no carried object yet) and
   `!player->isFamiliar`:
   * `weapon == 2 && ammo[3] < 100` → **refill from the toilet itself**: msg 248,
     `ammo[3] = 100` (`AMMO_HOLY_WATER`), `showHelp(14)`, sound 1046
     `HolyWaterPistol_refill.wav`. No conversion.
   * else if `STAT_STRENGTH < 11` → msg 238 ("too heavy"), nothing else.
   * else → **pick it up**: `currentWeaponCopy = ce->weapon`,
     `setPickUpWeapon(def->tileIndex)` (rewrites the `find(6,1,14)` def's tile/name/longName/
     description in place, `src/Player.cpp` `setPickUpWeapon`), `give(2, AMMO_ITEM, 1, true)`,
     `giveAmmoWeapon(14, true)` (`WP_ITEM`), then
     `turnEntityIntoWaterSpout(entity)`, optional fake-combat/explode thread, sound **1134**
     `Weapon_Toilet_Pull.wav`. The carried fixture is later thrown as weapon 14 (projType 13,
     impact = same white particles + sound 1135).
2. **Already a spout** (`src/PlayingInputHandler.cpp:433-444`): `eType == 14 && eSubType == 7 &&
   dist2 <= tileDistances[0] && dist2 > 0 && weapon == 2` → `ammo[3] = 100`; msg 248 + help 14 +
   sound 1046 if it was below 100, msg 249 if already full. This is the refill action the player
   sees: equip the holy water pistol, stand next to the spout, press FIRE.

### 11.6 Scripts see the conversion (`TILE_EMPTY`)
`EV_TILE_EMPTY` (`src/ScriptThread.cpp:456-474`) walks `entityDb[x + 32*y]` and reports "empty"
unless some entity has `eType != 12 && (1 << eType & 0x6240) == 0` — `0x6240` = eTypes
6 ITEM, 9 CORPSE, 13 NONOBSTRUCTING_SPRITEWALL, **14 DECOR_NOCLIP**. A converted fixture therefore
makes its tile report **empty**, which map scripts use as a "the player broke/took the toilet"
trigger (see map00 below).

### 11.7 map00 census (parsed per `src/LoadingManager.cpp:463-539`; 135 normal + 126 Z sprites)
| sprite | tile | tile xy | info | note |
|---|---|---|---|---|
| 26 | 123 toilet | (12,10) | 0x0000007B | first restroom |
| 27 | 123 toilet | (12,11) | 0x0000007B | |
| 45 | 127 sink | (14,11) | 0x0000007F | |
| 44 | **134 spout** | (14,10) | 0x00000086 | pre-placed, live from map load — the tutorial refill point |
| 92 | 123 toilet | (21,27) | 0x0000007B | second restroom |
| 93 | 127 sink | (21,28) | 0x0000007F | |
| 94 | 127 sink | (21,29) | 0x0000007F | |
| 95 | 123 toilet | (21,30) | 0x0000007B | |
| 162,163 | **134 spout** | (19,30) Z | 0x00200086 | `0x200000` = never spawn; script props |

No sprite carries `0x80000` — which does not make them static: tile 134 animates via its
dedicated 128 ms branch (`src/Render.cpp:1648-1653`), see `docs/original-code/rendering.md` §7. No `tileEvents` entry sits on
(12,10)/(12,11)/(14,11)/(14,10) — the first restroom is pure engine behavior. The second restroom is
scripted from the PER_TURN static func (`staticFuncs[6] = 253`): at IP 316-342 and 348-374,
`TILE_EMPTY tile(21,28)` / `tile(21,27)` → `HIDE sprite=94` / `HIDE sprite=95` +
`LERPSPRITE sprite=162 dst=(21,29)` / `sprite=163 dst=(21,30)`, i.e. when the player converts one
fixture the neighbouring one is swapped for a parked spout sprite (disassembly via
`tools/disasm_map_scripts.py`, guards `v39`/`v40`).
