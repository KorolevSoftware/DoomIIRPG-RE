# 2026-08-26 — Fire target acquisition: why a corpse under the player never blocks a shot

## Hypothesis (from orchestrator)

In `new_src/` shooting a monster on the tile ahead deals no damage while the
player stands on a BODY/corpse tile; the target search probably elects the
corpse on the player's OWN tile (or aborts on it). Question: how does the
original pick a target, and what makes an own-tile entity unreachable?

## Method

grep/read of the actual fire path in `src/`: `InputEventController` →
`PlayingInputHandler::handlePlayingEvents` (ACTION_FIRE) → `Game::trace` →
`Player::fireWeapon` → `Combat::performAttack` → `Combat::playerSeq` →
`CombatEntity::calcCombat`/`Combat::calcHit` → `Combat::explodeOnMonster` →
`Entity::died`. Cross-checks: `Render::CapsuleToCircleTrace` fraction algebra,
`MovementController::checkFacingEntity` (independent second trace user),
CONTENTS masks in `Enums.h`, and a direct parse of `tables.bin` table 2
(weapon data) for the range fields.

## Verdict

**CONFIRMED** that the original's own-tile entities *are* returned by the fire
trace (first, with fraction −1), and **CONFIRMED** the exact reason they can
never be chosen: the corpse branch of the election loop requires the Chebyshev
distance² to be *exactly* `tileDistances[0]` (4096), and it never `break`s, so
the loop walks on to the monster behind it. Two independent facts, both needed.

Also **REFUTED** side-lead: `src/Hud.cpp:981` is not a weapon-range table use —
it is the dialog-bubble vertical offset (`facingEntity` closer/farther than one
tile → `n += 10` / `n += 20`).

## Evidence

### 1. Chain

| step | location |
|---|---|
| key → action | `src/InputEventController.cpp:40` (`AVK_SELECT → ACTION_FIRE`), dispatch `:356,362,382` |
| playing handler | `src/PlayingInputHandler.cpp:19` `handlePlayingEvents`, ACTION_FIRE at `:189` |
| probe ray + target election | `src/PlayingInputHandler.cpp:198-355` |
| target pruning / special cases | `:356-497` |
| shot commit | `:496-540` → `Player::fireWeapon` |
| guards + ammo | `src/Player.cpp:754-798` |
| attack setup (`curTarget`) | `src/Combat.cpp:54-167` `performAttack` |
| per-frame sequencer | `src/Combat.cpp:857-862` `runFrame` → `playerSeq` `:178` |
| hit/damage roll, stage 0 | monster: `src/Combat.cpp:224` `calcCombat`; non-monster: `src/Combat.cpp:249` → `Combat::calcHit` `:864-883`; monster math `src/CombatEntity.cpp:157-378` |
| damage application | `src/Combat.cpp:885-947` `explodeOnMonster` (`pain` at `:907`, `:935`; corpse gib `:937-945`) |
| death, stage 1 | `src/Combat.cpp:382-386` → `Entity::died` `src/Entity.cpp:424-527` |

### 2. `curTarget` origin

`Combat::performAttack(curAttacker=nullptr, curTarget=entity, …)` stores the
argument verbatim (`src/Combat.cpp:58-59`), and `targetMonster`/`targetType`/
`targetSubType` are derived from it (`:75-79`). The argument is the `entity`
elected by the ACTION_FIRE scan (`src/PlayingInputHandler.cpp:523`), or
`&app->game->entities[0]` — the ET_WORLD entity — when the shot goes into a wall
(`:515`, `:536`). It is **not** `player->facingEntity`; the only place
`facingEntity` feeds `curTarget` is dialog style 1/5 for eType 2/3
(`src/DialogSystem.cpp:619-627`). In ACTION_FIRE `facingEntity` is used only to
set `lootingSystem.lootSource` for eType 10 (`src/PlayingInputHandler.cpp:190-195`).

### 3. The probe ray starts on the player's own position

```
int n8 = canvas->viewX + (n7 * -app->tinyGL->view[2] >> 8);   // :218
app->game->trace(canvas->viewX, canvas->viewY, canvas->viewZ, n8, n9, n10,
                 nullptr, n5, 2, canvas->isZoomedIn);          // :221
```
`view[]` is 14-bit fixed (1.0 = 16384, cf. `src/TinyGL.cpp:206-208`), so
`n7 * 16384 >> 8 = n7 * 64` world units = **n7 tiles**. `n7 = 6` for every
weapon, `n7 = 1` for melee (`CheckWeaponMask(weapon,2)`, i.e. weapon 1 =
chainsaw) (`:208-215`). Radius argument `n5 = 2`.

`Game::trace` (`src/Game.cpp:199-323`) sweeps the entityDb over the segment AABB
inflated by the radius (`:213-221`), i.e. **including the start tile**, filters
by `mask & (1 << eType)` (`:227`), and for sprite entities calls
`Render::CapsuleToCircleTrace` (`:274`). That function clamps the projection
parameter to `[0, len²]` (`src/Render.cpp:1113-1118`) and returns
`(t >> 2) - 1` on hit (`:1123`): an entity sitting on the start point yields
fraction **−1**, and the final bubble sort (`src/Game.cpp:311-322`) puts it
**first**. Circle radius² is 625 (r = 25) plus radius² 4 → 629
(`src/Game.cpp:274`, `src/Render.cpp:1122`). So a corpse under the player is
definitely element 0 of `traceEntities`.

Contrast — the facing trace starts **28 units ahead**:
`src/MovementController.cpp:40` traces from `destX + (-view[2]*28 >> 14)`, so an
own-tile entity is 28 units off the segment (784 > 629) and is dropped; its mask
21741 also lacks bit 9, so corpses never become `facingEntity`.

### 4. Election loop, per eType (`src/PlayingInputHandler.cpp:223-354`)

Mask `n5 = 13997 = CONTENTS_WEAPONSOLID` (`src/Enums.h:31`) = bits
{0,2,3,5,7,9,10,12,13} = WORLD, MONSTER, NPC, DOOR, DECOR, CORPSE,
ATTACK_INTERACTIVE, SPRITEWALL, NONOBSTRUCTING_SPRITEWALL. Chainsaw adds bit 4
PLAYERCLIP (`|= 0x10`, `:214`); the holy-water pistol (weapon 2) adds
`0x4100` = bits 8 ENV_DAMAGE + 14 DECOR_NOCLIP (`:203-205`).
(`CONTENTS_WEAPONSOLID = CONTENTS_PLAYERSOLID(13501) − 16 + 512`: guns see
corpses, players walk through them.)

* eType 0/12/4 (WORLD, SPRITEWALL, PLAYERCLIP) — `break` **always**, target =
  the wall if nothing was picked yet (`:229-236`). Blocking.
* eType 2 MONSTER — `entity = e; n6 = 0; break` unconditionally (`:264-269`).
  Wins over everything already collected.
* eType 9 CORPSE — only inside `dist == tileDistances[0]` (`:279`); chainsaw
  (`weapon2 == 1`) picks the highest `linkIndex` of the pile as an attack target
  (`:280-317`), any other weapon picks it as a **loot** target
  (`n6 = 1`, requires `monster == nullptr ? param == 0 && lootSet` else
  `(monster->flags & 0x800) == 0 && lootSet`, and `!isZoomedIn`) (`:318-334`).
  **No `break` in any branch** → falls through to `++i` (`:352`).
* eType 3 NPC — target only when `dist >= 8192` (`:257-263`), i.e. NOT adjacent
  (adjacent = 4096); adjacent NPCs are transparent to fire.
* eType 5 DOOR — first one wins, `break` (`:270-277`).
* eType 10 ATTACK_INTERACTIVE — `(1 << eSubType & 1) == 0` (⇒ eSubType ≠ 0
  FURNITURE) or chainsaw; `break` (`:238-247`); later dropped if
  `WorldDistToTileDist(dist2) > weapons[w*9+3]` (`:359-361`).
* eType 7 DECOR — only if `mapSpriteInfo[sprite] & 0xFF == 0x95` (`:339-343`).
* eType 6 ITEM / 11 MONSTERBLOCK_ITEM — not in the mask; explicitly excluded
  from the fallback (`:350`).
* eType 13 — remembered in `entity2` for melee only, promoted at `:362-364`.
* eType 8 ENV_DAMAGE — holy water with `ammo[3] >= 2` (`:335-338`);
  eType 14 sub 7 water spout at 1 tile (`:344-348`).
* `n6 != 0` (loot) short-circuits everything: `ST_LOOTING` + `poolLoot`
  (`:356-358`).

### 5. Distances and range

* `Entity::distFrom` = `max(dx², dy²)` — **Chebyshev squared**
  (`src/Entity.cpp:1155-1158`); measured from `canvas->viewX/viewY` in the scan,
  from `destX/destY` in `Combat::calcHit`.
* `tileDistances[j] = (64·(j+1))²` (`src/Combat.cpp:41-44`): [0] = 4096,
  [1] = 16384 …
* `WorldDistToTileDist(d)` = first `j` with `d < tileDistances[j]`
  (`src/Combat.cpp:1235-1242`): own tile → **0**, adjacent (incl. diagonal) → 1.
* Range fields: `weapons[w*9 + 2] = RANGEMIN`, `+3 = RANGEMAX` in tiles
  (`src/Combat.h:28-29`). Direct parse of `tables.bin` table 2 (offset 482,
  9 signed bytes/weapon, order STRMIN,STRMAX,RANGEMIN,RANGEMAX,AMMOTYPE,
  AMMOUSAGE,PROJTYPE,NUMSHOTS,SHOTHOLD):

  | w | weapon | dmg | range | ammo/use | proj | shots | shothold |
  |---|--------|-----|-------|----------|------|-------|----------|
  | 0 | assault rifle | 8–10 | 0–5 | 1/1 | 0 | 2 | 50 |
  | 1 | chainsaw | 18–22 | **0–1** | 0/0 | −1 | 1 | 100 |
  | 2 | holy water pistol | 5–8 | 0–3 | 3/2 | 2 | 2 | 50 |
  | 3 | sentry bot (shoot) | 12–15 | 0–5 | 7/0 | 0 | 1 | 35 |
  | 4 | sentry bot (explode) | 100–120 | 0–4 | 7/0 | −1 | 1 | 30 |
  | 5 | red bot (shoot) | 20–30 | 0–5 | 7/0 | 0 | 1 | 35 |
  | 6 | red bot (explode) | 140–155 | 0–4 | 7/0 | −1 | 1 | 30 |
  | 7 | super shotgun | 25–30 | 0–3 | 2/2 | 0 | 1 | 30 |
  | 8 | chaingun | 12–15 | 0–5 | 1/1 | 0 | 4 | 15 |
  | 9 | AR w/ scope | 20–25 | 0–5 | 1/1 | 0 | 1 | 30 |
  | 10 | plasma gun | 20–25 | 0–5 | 4/1 | 3 | 3 | 5 |
  | 11 | rocket launcher | 80–90 | 0–5 | 5/1 | 4 | 1 | 30 |
  | 12 | BFG | 200–220 | 0–5 | 4/10 | 5 | 1 | 30 |
  | 13 | soul cube | 250,250 | 0–5 | 6/5 | 12 | 1 | 30 |
  | 14 | picked-up weapon | 20–20 | 0–5 | 8/1 | 13 | 1 | 35 |

  (bytes are signed in the table; 140/155/200/220/250 read back as −116/−101/
  −56/−36/−6 and are used as `int8_t`, cf. `src/Combat.cpp:249-258` /
  `src/CombatEntity.cpp:300-378` — recorded here as raw parsed values.)
* RANGEMIN/RANGEMAX do **not** bound the scan loop; the scan is always 6 tiles
  (1 for melee). Range only matters afterwards: eType-10 pruning
  (`src/PlayingInputHandler.cpp:359-361`), `Combat::calcHit` for non-monster
  targets (`src/Combat.cpp:867-871`: outside `[min,max]` ⇒ `crFlags |= 0x400`,
  hit 0), and the accuracy penalty `−16 · tilesOutside` in
  `CombatEntity::calcHit` (`src/CombatEntity.cpp:219-238`).
* `RANGEMIN == RANGEMAX` (fixed-range) is **never true for player weapons**
  (every RANGEMIN is 0), so the two code paths keyed on it are effectively dead
  for the player: `src/CombatEntity.cpp:234-236` and the
  `weapons[+3] == 1 && weapons[+2] == 1` redirect at
  `src/PlayingInputHandler.cpp:510-516`. The out-of-range hard miss for the
  player is instead driven by `crFlags 0x40`, set when
  `(1 << weaponId & 0x77FF) == 0` (`src/Combat.cpp:204-206`) — that is bit 11
  only: **rocket launcher** (and all monster weapons ≥ 15).

### 6. Chainsaw / melee specifics

Chainsaw is weapon id 1 (`Enums::WP_CHAINSAW`, `src/Enums.h:137`) and the only
member of `WP_MELEEMASK = 2` (`src/Enums.h:155`). Everything melee-specific is
keyed on `CheckWeaponMask(weapon, 2)`:
probe shortened to 1 tile + PLAYERCLIP bit (`src/PlayingInputHandler.cpp:212-215`),
corpse election (`:280`), promotion of eType 13 (`:362`). Plus RANGEMAX = 1 in
the weapon table and `usedChainsaw(false)` for CORPSE / BARRICADE / FURNITURE
targets (`src/Player.cpp:777-779`). All other player weapons keep the 6-tile
line probe. So: **CONFIRMED** — only the chainsaw is adjacency-only.

### 7. Root cause for the own-tile corpse

Nothing generic protects an own-tile entity: it is traced, sorted first
(frac −1), and `Combat::calcHit` would even accept it (tileDist 0 ∈ [0,1] for
the chainsaw, ∈ [0,5] for guns). The only reason the original never targets the
corpse you stand on is the corpse branch's **equality** test
`dist == app->combat->tileDistances[0]` (`src/PlayingInputHandler.cpp:279`) —
own tile gives `dist == 0`, so the test fails — combined with the branch
containing **no `break`**, so iteration continues to the monster ahead
(`:352`, `:264-269`). A corpse is therefore fully transparent to gunfire unless
it is exactly one tile away, where a non-chainsaw weapon turns the keypress into
looting and the chainsaw gibs it.

## Open questions

* `Combat::calcHit` additionally requires `entity->info & 0x20000`
  (`src/Combat.cpp:872-874`); for corpses that bit exists only on
  CORPSE_SKELETON map decor (`src/Entity.cpp:95-97`, `0x420000`) and on
  monster-generated corpses that were not gibbed
  (`src/Entity.cpp:461` `info |= 0x1020000`). Map-placed non-skeleton corpses are
  thus un-gibbable — not verified against a specific map.
* Whether any script/`radiusHurtEntities` path can legitimately target an
  own-tile entity was not surveyed (`src/Combat.cpp:1085-1197`).
