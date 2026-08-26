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
* GL-path anchors: `scrX = 480/2 − 44 = 196`, `scrY = 320/2 − 29 = 131`
  (`src/App.h:36-37`, `src/Combat.cpp:627-628`). (Software-TinyGL variant
  uses −62/−18 anchors, per-weapon nudges and a 1.35× scale flag,
  `:631-668` + `src/Render.cpp:352-354` — not the port target.)
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
  frame` (`src/Render.cpp:2047`). Weapon tiles are 256×256 media with the gun
  in the lower cell portion (tile 1 f0 alpha bbox (81,103)-(174,176)) — the
  idle quad (183,43)-(359,219) lands the rifle at ≈(239-303, 71-164):
  center-right, mid-height.
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

## 5. Fire pipeline & damage math (added 2026-08-26; full evidence in
`docs/research/2026-08-26-combat-stage1.md` §2)

* ACTION_FIRE probe (`src/PlayingInputHandler.cpp:189-378`) is a ~6-unit ray along
  −view[2]/−view[6]/−view[10], mask 13997 CONTENTS_WEAPONSOLID radius 2; chainsaw
  shrinks it to 1 unit (+PLAYERCLIP bit), holy-water pistol adds 0x4100
  (`:200-221`). It only ELECTS the target (ET_MONSTER wins immediately `:264-269`;
  corpses only at Chebyshev distance² == tileDistances[0] with loot rules
  `:279-337`); loot preempts everything at `:374-378`.
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
  difficulty 4 −25% first; strength bonus gated by CR 0x20 (set when hitting a
  MONSTER with non-chainsaw, `src/Combat.cpp:223-226`); weakness =
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
