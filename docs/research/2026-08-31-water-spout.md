# 2026-08-31 — Water spout conversion for INTERACT_PICKUP fixtures (toilet / sink)

## Question
G3 deferred `turnEntityIntoWaterSpout` (`src/Entity.cpp:385`) for
`ET_ATTACK_INTERACTIVE / INTERACT_PICKUP` (eSubType 3, toilet tile 123, sink tile 127, parm 3).
What exactly does the original do: the pain branch, the conversion itself, weapon eligibility,
message/XP/counters, on-screen result, and map00 instances?

## Method
grep for `turnEntityIntoWaterSpout` (5 call/def sites), read `src/Entity.cpp:281-391` (pain),
`:424-528` (died), `src/ArmorRepairSystem.cpp:56-63`, `src/Canvas.cpp:1615`,
`src/PlayingInputHandler.cpp:189-445` (ACTION_FIRE), `src/Combat.cpp:333-386,864-946,1120-1196`,
`src/Game.cpp:183-193,420-460,738-750,3545-3547`, `src/Render.cpp:1500-1552`,
`src/ScriptThread.cpp:456-474`, `src/LoadingManager.cpp:362-545`.
Data: `entities.bin` from the ipa parsed with the `src/EntityDef.cpp:35-44` record layout;
`map00.bin` from the ipa parsed with the `src/LoadingManager.cpp:463-539` order (all
DEADBEEF/CAFEBABE markers asserted); scripts via `tools/disasm_map_scripts.py --verify`
(0 failures).

## Verdict: CONFIRMED (with three corrections to the working assumptions)

1. **CONFIRMED** — `pain` returns before `removeEntity`: `src/Entity.cpp:383-387`
   `spawnParticles(1, -1, sprite); turnEntityIntoWaterSpout(this); return b;`, so
   `removeEntity`, `info |= 0x400000` and `mapSpriteInfo |= 0x10000` at `:388-390` are skipped.
2. **CONFIRMED** — the conversion is exactly 4 statements (`src/ArmorRepairSystem.cpp:59-62`):
   `def = lookup(134)`, `name = def->name | 0x400`, `mapSpriteInfo = (info & 0xFFFFFF00) | 134`,
   `info |= 0x400000`. No unlink, no hide, no scale/Z change; blocking disappears only because
   the new `eType` (14 `ET_DECOR_NOCLIP`) is absent from the solid masks (`src/Game.cpp:745`,
   `src/MovementController.cpp:125-128,326`).
3. **REFUTED (correction)** — "chainsaw only" is wrong. `tableCombatMasks[3] = 0xFFFFFFFB`
   (`src/Combat.cpp:879`, table 4 = `{0,0x2,0xFFFFFFFB,0xFFFFFFFB}`) allows every weapon
   **except id 2 `WP_HOLY_WATER_PISTOL`**; election accepts any weapon for `eType 10` with
   `eSubType != 0` (`src/PlayingInputHandler.cpp:238-239`).
4. **CORRECTION** — no message 89, no +5 XP, no `destroyedObject` for eSubType 3: `pain` swaps
   `def` before `Combat::playerSeq` reaches `died` (`src/Combat.cpp:349-351,383-386`), so
   `died` sees `eType == 14` and skips the whole `ET_ATTACK_INTERACTIVE` block
   (`src/Entity.cpp:437-448`), leaving only `info &= 0xFFFDFFFF` and
   `updateFacingEntity = true`. `numDestroyableObj` also excludes eSubType 2/3 at spawn
   (`src/Game.cpp:448-450`), so the statistic stays consistent.
5. **CORRECTION / new fact** — `INTERACT_PICKUP` primarily means *liftable*: ACTION_FIRE on an
   adjacent intact fixture with `ammo[8] (AMMO_ITEM) == 0` and `STAT_STRENGTH >= 11` calls
   `setPickUpWeapon(tileIndex)` + `give(2,8,1)` + `giveAmmoWeapon(14)` +
   `turnEntityIntoWaterSpout` + sound 1134 `Weapon_Toilet_Pull.wav`
   (`src/PlayingInputHandler.cpp:405-431`) — the toilet becomes the thrown `WP_ITEM`.
   With the holy water pistol equipped and `ammo[3] < 100` the same key refills instead
   (msg 248, `ammo[3] = 100`, sound 1046) and does not convert.
6. **CONFIRMED** — the spout is the refill station: `eType 14 / eSub 7` is traceable only when
   `weapon == 2` (`n5 |= 0x4100`, `src/PlayingInputHandler.cpp:205-207`), elected at exactly
   `tileDistances[0]` (`:354-359`), and ACTION_FIRE sets `ammo[3] = 100` with msg 248/249
   (`:433-444`).
7. **On screen**: static tile-134 sprite in the fixture's slot, frame bits preserved (mask
   `0xFFFFFF00`; normal map sprites always have frame 0, `src/LoadingManager.cpp:508-537`),
   never auto-animated (needs sprite bit `0x80000`, `src/Render.cpp:1544-1546`), visible,
   non-blocking, and reported as "empty" to scripts (`0x6240` includes eType 14,
   `src/ScriptThread.cpp:466`). Conversion FX = white particle type 1 (upward jet,
   `src/ParticleSystem.cpp:284-291`); no dedicated sound in `pain`.
8. **map00**: toilets 123 at (12,10),(12,11),(21,27),(21,30); sinks 127 at (14,11),(21,28),
   (21,29); a **pre-placed live spout** 134 at (14,10) (sprite 44, info 0x00000086) plus two
   parked spouts (sprites 162/163, info 0x00200086, `0x200000` = never spawn) at (19,30).
   No tileEvent on any of the first-restroom tiles. PER_TURN (`staticFuncs[6] = 253`) at
   IP 316-342 / 348-374: `TILE_EMPTY tile(21,28)`→`HIDE sprite=94`+`LERPSPRITE 162→(21,29)`,
   `TILE_EMPTY tile(21,27)`→`HIDE sprite=95`+`LERPSPRITE 163→(21,30)`.

## Open questions
* `Player::giveAmmoWeapon(14, true)` / `currentWeaponCopy` restore rules for the carried object
  (throw once → back to previous weapon?) were not traced here.
* `shouldFakeCombat` + `explodeThread` interaction on pickup (`src/PlayingInputHandler.cpp:423-426`)
  is unexplored.
* Whether `save`/`load` of a *carried* fixture keeps `setPickUpWeapon`'s in-place def patch.

## Durable doc
Facts integrated as `docs/original-code/combat.md` §11 "Water spout conversion".
