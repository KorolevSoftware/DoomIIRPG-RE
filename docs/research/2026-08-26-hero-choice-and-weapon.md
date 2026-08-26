# 2026-08-26 — hero choice ↔ player identity + first-person weapon display spec

Tasking: (A) resolve how legacy `characterChoice` ({1,2,3}) determines the player's
in-world representation, which choice is the female, and whether the user's
"two identical main characters" observation is a bug; (B) extract the legacy
first-person weapon HUD spec and audit the rewrite. Read-only; no code changed.

**Verdicts up front:**

* **PART A: CONFIRMED — v14=1 is correct and the user IS seeing the intended
  default character.** Choice **1 = Major "Kira Morgan" — the FEMALE
  marine-stat character** (name strings + world art verified). There is no
  separate "male marine"; the rewrite's "marine" colloquialism = the Major.
  The "duplicate" is legacy-intended behavior: the chosen character's world
  sprite deliberately appears in the boot formation cinematic (it is you),
  and after the doorway scene it is *not* hidden — it is parked at tile
  (22,29) by the boot script and later scripted to follow the player through
  the (21–23,27–30) room. The doorway scene itself never contains the chosen
  sprite: the two NON-chosen squad sprites do the walk-in and are hidden.
  Today's `err.log` proves the rewrite (with v14=1) already reproduces the
  legacy end state exactly. **No further fix needed.**
* **PART B: PARTIAL (spec extracted, rewrite has icons only).** Legacy draws
  the view weapon via `Combat::drawWeapon` (immediate mode, every 3D repaint,
  176×176 screen quad from media tile 1–15 frame 0/1). The rewrite has the
  bottom-bar weapon *icon* (102×616 strip), `Tables::weaponInfo/weaponData`,
  `Player::weapons/weapon`, and a reusable media-texture path — but **no
  first-person weapon draw at all** and `Hud::drawBottomBar` is never called.
  Minimal scope listed in §B.4.

---

## PART A — characterChoice ↔ player identity

### A.1 The mapping (all sites cited)

`characterChoice` is written exactly once in gameplay:
`Player::setCharacterChoice(short)` sets the field **and** stamps
`scriptStateVars[14]` (`src/Player.cpp:1264-1268`); save/load round-trips it
(`src/Player.cpp:1355`, `:1274` region). Call sites are only the mandatory
intro character select: `src/IntroSequenceManager.cpp:599`, `:628`, `:676`
(touch buttons map slot0→1, slot1→3, slot2→2 at `:613-624`; keyboard cycling
walks `getCharacterConstantByOrder` = {1,3,2}, `:328-341`).
`Game::updateScriptVars` re-stamps `scriptStateVars[14] =
player->characterChoice` before every script run batch (`src/Game.cpp:3468`,
within `:3461-3471`).

| choice | select-screen class (`src/IntroSequenceManager.cpp:359-372`) | proper name (MenuStrings 215/216/217, `src/MenuStrings.h:264-266`; strings extracted from `UnPackGameData/strings.idx`+`strings0*.bin`, type 3) | base stats + gold (`src/Player.cpp:454-486`) | HUD portrait (`src/App.cpp:439-450`) | world art (`src/Game.cpp:2728-2746`) | pain/death sounds (`src/Player.cpp:635-641`, `:720-727`) |
|---|---|---|---|---|---|---|
| **1** | "Major" | **Kira Morgan** ("Major Morgan") | DEF 8 / STR 9 / ACC 97 / AGI 12 / IQ 110, 30 gold | `Hud_Player.bmp` | **tile 68** `TILENUM_NPC_MAJOR` | 1094 / 1092 |
| **2** | "Sarge" (sergeant/heavy) | **Stan Blazkowicz** ("Sergeant Blazkowicz") | DEF 12 / STR 14 / ACC 92 / AGI 6 / IQ 100, 10 gold | `Hud_PlayerDoom.bmp` | **tile 72** `TILENUM_NPC_SARGE` | 1093 / 1091 |
| **3** | "Scientist" | **Riley O'Connor** ("Dr. O'Connor") | DEF 8 / STR 8 / ACC 87 / AGI 6 / IQ 150, 80 gold | `Hud_PlayerScientist.bmp` | **tile 66** `TILENUM_NPC_RILEY_OCONNOR` | 1093 / 1090 |

Tile constants: `src/Enums.h:708-716`. Art gender verified visually by
extracting tiles 66/68/72 with `tools/extract_textures.py` and reading the
head frames (NPCs are stacked composites: frames 0/1 legs, 2 torso, 3 head —
`src/Enums.h:604-613`, composited in `src/Render.cpp:3277`):

* tile 68 (Major) head frame = **woman, long dark hair** → the FEMALE hero.
* tile 66 (O'Connor) head frame = blond man with forehead goggles.
* tile 72 (Sarge) head frame = full helmet (heavy armor, reads male).

**Answer to "which choice is the female": choice 1.** With the rewrite's
`vars[14] = 1` (`new_src/domain/game/ScriptVM.cpp:300-305`) the user is playing
exactly the intended default; the portrait the rewrite loads
(`Hud_Player.bmp`, `new_src/ui/Hud.cpp:103`) is her face. What the tasking
called "marine" **is** this character (marine-stat block, `Hud_Player.bmp`).

### A.2 How choice determines the player's in-world presence

* **Start position/direction: NOT choice-dependent.** `Game::spawnPlayer`
  (`src/Game.cpp:941-972`) has zero choice branching: fresh load uses
  `mapSpawnIndex/mapSpawnDir` from the map header (map00: 612/0 → tile (4,19),
  angle 0; `src/LoadingManager.cpp:370-371`); the `loadType == 3` variant
  (save-restore path) hardcodes (3,15) dir 6 (`:947-951`). During EVT 617 the
  script itself GOTOs the player to (6,20) angle 4 (bytecode IP 3252).
* **First-person hands/weapon art: NOT choice-dependent.** `Combat::drawWeapon`
  renders per-weapon media tiles with no `characterChoice` input
  (`src/Combat.cpp:621-844`; the only choice test in Combat is a weapon-14
  attack *sound*, `src/Combat.cpp:298`). All three classes share the same view
  weapon art.
* **Third-person presence: the chosen squad sprite IS the player's world
  body**, reused by the map scripts:
  * `Game::spawnPlayerEntityCopy` (`src/Game.cpp:2725-2759`) — the familiar-mode
    body double left at `saveX/saveY` (`src/Player.cpp:2105`, removed
    `:2163-2164`) — uses art **68 for choice 1, 66 for choice 3, 72 for
    choice 2**, i.e. the same NPC models as squad sprites 7/9/10.
  * Boot init script (camera-5 formation, func@2328): all three sprites
    (7=major, 9=O'Connor, 10=sarge — `docs/original-code/lerp-opcodes.md`
    §map00) are placed in a column and walked in **including the chosen one**
    (v14==1: spr10→(160,1224), spr9→(160,1272), spr7→(160,1248), IPs
    2346/2353/2360; walks @2439-2453; `docs/research/assets/map00_disasm.txt:606-635`).
  * Immediately after the formation cutscene the boot script **teleports the
    chosen one's sprite out of the scene**: v14==1 → `LERPSPRITE sp=7
    dst=(22,29)` INSTANT @2531; v14==2 → sp10 @2545; v14==3 (else) → sp9
    @2552 (`map00_disasm.txt:640-646`), then parks the two others near spawn
    @2556-2618 and re-parks them to (8,18)/(7,19) @2936-2987
    (`map00_disasm.txt:739-750`).
* **The doorway scene (EVT 617) never shows the chosen sprite.** Walkers are
  the two non-chosen: v14==1 → 9+10 (IPs 3086-3139), v14==2 → 7+9
  (3142-3198), v14==3/else → 7+10 (3198-3243); all three variants then hide
  exactly their walkers (@3283/3285, @3297/3299, @3311/3313 —
  `docs/research/2026-08-26-choice-branch.md` §1.5, confirmed byte-exact).

### A.3 The "duplicate hero" reconciliation

| choice | formation cinematic (camera 5) | doorway scene (EVT 617, camera 7) | chosen sprite after EVT 617 | later scripted life |
|---|---|---|---|---|
| 1 (Major/female) | **she walks in** (sp7) — intentional, it is you | walkers 9+10 (O'Connor+Sarge), **both hidden** @3297/3299 | visible at **(22,29)** | follows player: tile events (21,27)–(23,28) step sp7 through (23,29)→(21,29)→(21,30)→(22,30)→(22,29)→(23,29)→exit (24,30), IPs 7184-7633, gated by `EV_TILE_EMPTY` (op 9, `src/Enums.h:411`) checks (`map00_disasm.txt:1827-1933`; event table entries at tile IDs 885/886/917/918/919 decoded from `tmp_map00.bin`) |
| 2 (Sarge) | he walks in (sp10) | walkers 7+9, hidden @3283-3313 | visible at (22,29) | same choreography on sp10 |
| 3 (Scientist) | he walks in (sp9) | walkers 7+10, hidden | visible at (22,29) | same on sp9 |

So: seeing the female Major in the boot cutscene **is correct**; seeing her
model again near (22,29)/the follower room **is also correct** (legacy shows
your own model there on purpose). The genuinely broken state was yesterday's
v14=0 build (fall-through marched spr7+spr10 into the doorway and hid
nothing — `docs/research/2026-08-26-choice-branch.md`); that is what the user
is remembering as "it broke something".

### A.4 Rewrite verification (today's `build_new/new_src/err.log`, v14=1)

Every legacy step reproduced, in order: formation placement/walks
(err.log:418-431 ≡ IPs 2346-2360/2439-2453), chosen-one INSTANT teleport
sp7→(1440,1888)=(22,29) (err.log:496 ≡ @2531), parking sp9→(544,1184)=(8,18),
sp10→(480,1248)=(7,19) (err.log:589-590 ≡ @2943/2947), marine doorway walk
9+10 →(672,1248)=(10,19) (err.log:597-636 ≡ @3093-3134), items+message
(err.log ≡ func@3401/@3270), bob INSTANT (21,21) + sp22 close+lock
(err.log ≡ @3318/3324/3327). **No `moveBlocked` lines** (yesterday's symptom)
— the HIDEs took effect (the rewrite's EV_HIDE is silent for eType-3 NPCs,
`new_src/domain/game/ScriptVM.cpp:497-526`; renderer honors 0x10000,
`new_src/render/World3D.cpp:549,1230,1265`; collision skips hidden,
`new_src/domain/game/Game.cpp:136`).

**Minimal faithful fix: none beyond the existing `vars[14] = 1`.** When a
character-select UI lands, drive it from `setCharacterChoice` semantics
(1/2/3 with the button order {1,3,2}) — nothing else in the spawn path is
choice-gated.

---

## PART B — first-person weapon display (port-ready spec)

### B.1 Legacy draw path — `Combat::drawWeapon(int sx, int sy)` (`src/Combat.cpp:621-844`)

Called every 3D repaint from `Canvas::renderScene` **after** world render +
`renderPortal`, **skipped when `isZoomedIn`** (`src/Canvas.cpp:1344-1355`);
also from cinematics with (0,0) (`src/GameStateRunner.cpp:158`,
`src/MayaCamera.cpp:313`, where `cinematicWeapon` overrides the weapon,
`src/Combat.cpp:701-704`). Immediate mode — no caching; any `invalidateRect`
repaint redraws it. Draw order: world → weapon → postProcess → (later HUD
pass paints the bottom bar over it).

Constants: `scrX = 480/2 − 44 = 196`, `scrY = 320/2 − 29 = 131`
(`Applet::IOS_WIDTH/HEIGHT`, `src/App.h:36-37`; `src/Combat.cpp:627-628`).
The software-TinyGL path uses different anchors (+per-weapon nudges and a
1.35× scale flag, `:631-668`, `src/Render.cpp:352-354`) — the rewrite targets
the GL-path numbers only.

Per-frame position:

```
weapon = player->ce->weapon            (−1 or weapons==0 or ST_DYING → draw nothing, :706-708)
scrY += LOWEREDWEAPON_Y (38)           when weaponDown (static), :672-674, Combat.h:46
scrY += per-weapon bias                1:+3, 2:+10, 3..6:+12   (:679-693)
sx, sy  = canvas shakeX/shakeY         (passed in)
wpX/wpY = wpinfo idle or attack pair   (see below)
x = scrX + (wpX + sx)
y = scrY − (wpY + sy)                  ← top-left of a 176×176 quad (scale 0x10000)
                                        (src/Render.cpp:343-373; TinyGL.cpp:751-752,820:
                                         vert3.y is the TOP, spans y..y+176)
```

`wpinfo` = tables.bin **table 1** (TBL_COMBAT_WEAPONINFO), 6 signed bytes per
weapon: idleX, idleY, atkX, atkY, flashX, flashY (`src/Combat.h:53-59`,
loaded `src/App.cpp:374,390`). Extracted values (all 15 weapons):

| id | weapon (art tile, `src/Enums.h:648-664`) | idle | attack | flash |
|----|------------------------------------------|------|--------|-------|
| 0 | assault rifle (tile 1) | (−13,88) | (−8,82) | (1,12) |
| 1 | chainsaw (2) | (−33,80) | (−53,91) | (30,26) |
| 2 | holy water pistol (3) | (−9,89) | (−4,81) | (0,0) |
| 3 | sentry bot shoot (4) | (−23,65) | (−15,88) | (30,18) |
| 4 | sentry bot explode (5) | (−23,65) | (−15,88) | (30,18) |
| 5 | sentry bot red shoot (13) | (−23,65) | (−15,88) | (30,18) |
| 6 | sentry bot red explode (14) | (−23,65) | (−15,88) | (30,18) |
| 7 | super shotgun (6) | (−8,88) | (0,82) | (−1,14) |
| 8 | chaingun (7) | (−20,88) | (−14,82) | (5,20) |
| 9 | assault rifle w/ scope (8) | (−13,88) | (−14,82) | (30,18) |
| 10 | plasma gun (9) | (−2,88) | (1,82) | (30,18) |
| 11 | rocket launcher (10) | (0,88) | (8,82) | (22,26) |
| 12 | BFG (11) | (−44,88) | (−44,66) | (30,18) |
| 13 | soul cube (12) | (−20,88) | (0,88) | (−25,−32) |
| 14 | picked-up world weapon (15) | (−10,40) | (−20,88) | (−30,−48) |

Tile resolution `getWeaponTileNum(n)` (`src/Combat.cpp:1766-1784`): `n<5 →
n+1`; `5→13`; `6→14`; `14→15`; else `n−1`. Media frame = `mediaMappings[tile]
+ frame` (`src/Render.cpp:2047`). Weapon art cells are 256×256 media with the
gun in the lower portion (assault rifle frame 0 alpha-bbox (81,103)-(174,176)),
so the idle quad (183,43)-(359,219) puts the rifle at ≈(239..303, 71..164) —
center-right, mid-height (verified against extracted PNGs).

Sprite selection per frame (`src/Combat.cpp:797-840`):

* **Sentry-bot weapons 3/4/5/6**: tile 19 (green) / 18 (red), frames 2 and 3
  stacked (second at `y + (blink?1:2)`), with a time-based blink XOR of flag
  0x20000 over `(time + n*1337)/1024 & 0xF` windows.
* **Weapon 14** (picked-up world weapon, def(6,1,14), `src/Player.cpp:1636-1656`):
  `ammo[8] > 0` → `activeWeaponDef->tileIndex` **frame 1** at flash offset;
  else tile 15 frame 0.
* **Muzzle flash**: attacking && weapon ∈ {0,7,8} (mask 0x181) → tile 1
  **frame 3** (orange starburst, verified visually) at
  `(x+flashX+40, y+flashY+40)` GL (software +15/+20), renderMode 5, scale
  0x8000 (half).
* **Weapon 9** additionally underlays tile 1 at the current frame before
  drawing tile 8 (`:835-837`).
* `frame = 1` while a chainsaw return-lerp (b6) or weapons 13/8 attack pose
  is active (`:822-825`); otherwise 0. Main call `:839`.
* Status overlay (`drawEffects`, `:612-619`) draws tile 234 frames 0-3 at
  bottom-center when statusEffects[13] active.

Fire animation (attack pose → idle): hold (atkX,atkY) until `flashDone`
(`flashTime`), then interpolate back over `animTime =
weapons[weapon*9+8] (SHOTHOLD, table 2) ×10` (×5 with statusEffects[2]) in
16.16 (`:311-321, 735-767`); chainsaw (1) jitters ±2px then eases back
(`:752-761`). **There are no reload/switch animations** — switching weapons
swaps art instantly (`Player::selectWeapon`); the only transition is the
lower/raise lerp: `shiftWeapon` (`:846-855`) animates scrY by
LOWEREDWEAPON_Y=38 over LOWERWEAPON_TIME=200 ms (`:769-784`), triggered when
facing an NPC/dialog (`src/MovementController.cpp:134-156`), weapon-select
open (`src/PlayingInputHandler.cpp:482`), etc. Zoom replaces the weapon
entirely (`src/Canvas.cpp:1348`).

Starting weapon: EVT 617 `GIVEITEM def=1` → def tile 1 = item(6,1) parm 0 →
**weapon id 0 (assault rifle)** (entity defs parsed from `tmp_entities.bin`:
tile 1 → eType 6, sub 1, parm 0; tile 15 → parm 14), then MESSAGE str 133
"You equipped your weapon."

### B.2 HUD weapon icon (already ported)

`Hud::drawWeapon` (`src/Hud.cpp:1048-1125`) draws the bottom-bar icon at
(268,258) (`:683`) from `imgWeaponNormal`/`imgWeaponActive` 44px rows with
texY map {0:0, 1:1, 2:2, 3/4:10, 5/6:11, 7:3, 8:4, 9:5, 10:6, 11:7, 12:8,
13:9, 14:12} + ammo digits (`drawNumbers`, weapon 1 shows none, weapon 13
special-cased). The rewrite mirrors this 1:1 (`new_src/ui/Hud.cpp:24-48`
`weaponTexY`, `:514-522` `drawWeapon`, `:524-545` `drawNumbers`); the boot
log's `weaponNormal=102x616` is this 14-row strip (`Hud_Weapon_Normal.bmp`,
`new_src/ui/Hud.cpp:76,94-95`).

### B.3 Rewrite audit — what exists vs what must be built

Exists today:

* Icon strip textures + texY mapping + selection grid + ammo digits
  (`new_src/ui/Hud.cpp:24-48, 249-271, 514-545`).
* `drawBottomBar` (weapon icon at (268,258), shield/health digits, portrait
  row, keys) fully written **but never called** (`new_src/ui/Hud.h:117`; no
  call sites in `new_src/`).
* Player weapon state: `weapons` bitmask, `weapon` (index),
  `activeWeaponDef` (`new_src/domain/game/Player.h:25-29`); `give` kind 1
  sets the bit and auto-equips when nothing held
  (`new_src/domain/game/Player.cpp:99-113`) — so after EVT 617
  `player.weapon == 0` (assault rifle).
* Tables: `weaponInfo` (= wpinfo, table 1) and `weaponData` (= SHOTHOLD
  table, table 2) parsed (`new_src/io/Tables.h:27-28,52-53`);
  `weaponData` already used for loot ammo
  (`new_src/domain/game/Game.cpp:862-864`).
* Media pipeline with per-tile textures and a lazy sprite-texture cache
  keyed by (tileNum, mediaId) — `World3D::ensureSpriteTexture`
  (`new_src/render/World3D.h:105-109`), mappings access
  (`new_src/io/Media.h` `MediaMappings::mappings`).
* Equip message path: `EV_MESSAGE` str 133 → "You equipped your weapon."
  (`new_src/domain/game/ScriptVM.cpp:372-384`; present in today's err.log).

Missing for a minimal faithful display (static equipped weapon, no fire):

1. `getWeaponTileNum` port (pure function, `src/Combat.cpp:1766-1784`).
2. Idle offset read: `tables.weaponInfo[weapon*6 + 0/1]` as **signed** bytes.
3. A screen-space blit of media tile `getWeaponTileNum(weapon)` frame 0 as a
   176×176 quad at `(196 + idleX + shakeX, 131 − (idleY + shakeY))`, drawn
   after `drawBSP` and before HUD overlays (hook:
   `new_src/core/GameContext.cpp:994-1011` render section, gated on the
   playing state; zoom does not exist yet). Reuse
   `World3D::ensureSpriteTexture` + a textured quad; `Graphics2D::drawImage`
   with scaling is an acceptable substitute since the weapon is always
   screen-facing at a fixed rect.
4. Drive `Hud::weapon_` from `player.weapon` (currently hardcoded 3,
   `new_src/ui/Hud.h:159-160`) if/when `drawBottomBar` is wired up.

Explicitly out of minimal scope (later, with combat): attack-pose
interpolation, muzzle-flash overlay, sentry-bot blink stack, weapon-14
special case, lower/raise lerp, cinematicWeapon.

### B.4 Open questions

* Exact on-screen look of the 176px quad vs the rewrite's projection: the
  legacy GL path renders the weapon through the world projection
  (`DrawWorldSpaceSpriteLine`); a plain 2D blit at the computed rect should
  be pixel-identical at default pitch, but pitch/shake interplay should be
  user-verified once drawn.
* Weapon 9's tile-1 underlay (`src/Combat.cpp:835-837`) purpose not
  investigated (likely shared hands art); irrelevant until weapon 9 is
  obtainable.
