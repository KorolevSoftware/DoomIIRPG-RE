# 2026-08-31 — Weapon branch: from "the chainsaw hurts" to real projectiles

Trigger: the player picked up the chainsaw, selected it, swings — and nothing happens.
Root cause is confirmed below (§1). This spec plans the whole weapon branch in five
independently deliverable groups, ordered smallest-first.

Sources of truth:
* `docs/research/2026-08-31-melee-weapons.md` (raw log)
* `docs/original-code/combat.md` §5, §8, §9 (curated)
* ADR 0017 (projectile model), ADR 0018 (pain/died dispatch)

Conventions: `src/...` = original RE port, `new_src/...` = rewrite; a bare `:NNN` inside a
paragraph refers to the file named at the start of that paragraph.

---

## 1. Confirmed diagnosis and current-state audit

### 1.1 The blocker (one condition)

`new_src/domain/game/Player.cpp:278-285`:

```cpp
const int proj = wdef.projType;
if (proj != 0) {
    std::fprintf(stderr, "[combat] projectile weapon %d unsupported (proj=%d)\n", weapon, proj);
    return false;
}
```

The chainsaw row is `STRMIN 18, STRMAX 22, RANGEMIN 0, RANGEMAX 1, AMMOTYPE 0, AMMOUSAGE 0,
PROJTYPE -1, NUMSHOTS 1, SHOTHOLD 100` (`combat.md` §9.1, verified by direct table parse), so
`proj == -1 != 0` and `Combat::performAttack` is never reached. The ammo gate above it
(`Player.cpp:270-277`) is a no-op for the saw (`AMMOTYPE 0`, `combat.md` §9.2) — it is not
involved. **This one condition is the entire bug.** The guard is also too wide: it blocks
weapons 4, 6 and 23 (also `PROJTYPE -1`, `combat.md` §9.1) as well.

`PROJTYPE -1` semantics (`combat.md` §9.4): `launchProjectile` has no case for `-1` *or* `0`;
both fall into `default: missileAnim = 0; exploded = true; return;`
(`src/Combat.cpp:1566-1570`), and the stage-0 `updateProjectile()` (`src/Combat.cpp:355`)
then applies the damage in the same frame through its `exploded` tail
(`src/Combat.cpp:1421-1430`). `-1` is a *degenerate projectile that detonates on the target*,
**not** a "hitscan bypass".

### 1.2 What already exists in the rewrite (verified by reading, do not redo)

| Legacy piece | Rewrite state |
|---|---|
| melee probe: 1 tile + `PLAYERCLIP` (`src/PlayingInputHandler.cpp:208-215`) | DONE — `new_src/domain/game/Targeting.cpp:41-52` (`melee ? 1 : 6`, `Contents::MELEE_EXTRA`) |
| chainsaw elects FURNITURE (`:238-239`) | DONE — `Targeting.cpp:65-72` |
| chainsaw elects a 1-tile corpse (`:279-317`) | DONE — `Targeting.cpp:89-105` |
| melee `entity2` promotion of eType 13 (`:247-252,:362-364`) | DONE — `Targeting.cpp:73-76,141-146` |
| chainsaw skips the loot session on a corpse (`:280-317` vs `:318-334`) | DONE — `new_src/core/PlayerActions.cpp:187-190` |
| `crFlags 0x20` **not** set for the saw (`src/Combat.cpp:223-226`) | DONE — `new_src/domain/game/Combat.cpp:224-226` |
| strength bonus when `0x20` clear (`src/CombatEntity.cpp:326-333`) | DONE — `new_src/domain/game/CombatEntity.cpp:210-216` |
| weakness nibble incl. the saw's high nibble (`src/Combat.cpp:29-31`) | DONE — `new_src/domain/game/Combat.cpp:50-62` |
| `tableCombatMasks` gate for eType 10 (`src/Combat.cpp:876-882`) | DONE — `new_src/domain/game/Combat.cpp:354-379` (`calcHitEntity`) |
| prop/wall plain table roll (`src/Combat.cpp:249-259`) | DONE — `Combat.cpp:248-259` |
| `animTime = SHOTHOLD * 10` (`src/Combat.cpp:311-317`) | DONE — `Combat.cpp:288-290` |
| fire sound id 1015 (`src/Combat.cpp:301-303`) | DONE as a log — `Combat.cpp:261-279` (no audio backend at all) |
| stage-0 → `explodeOnMonster` in the same frame | DONE but inlined with a comment — `Combat.cpp:282-286,310` |
| `explodeOnMonster` monster arm (`src/Combat.cpp:904-931`) | DONE — `Combat.cpp:396-407` |
| `explodeOnMonster` chainsaw-miss arm (`src/Combat.cpp:892-894`) | **MISSING** — `Combat.cpp:389-395` dropped the first arm and turned the legacy `else if` into an `if` |
| `explodeOnMonster` eType 10 arm (`src/Combat.cpp:933-937`) | **MISSING** (log only, `Combat.cpp:409-411`) |
| `explodeOnMonster` corpse-gib arm (`src/Combat.cpp:938-945`) | **MISSING** (log only, `Combat.cpp:412`) |
| `usedChainsaw` (`src/Player.cpp:2636-2653`) | **MISSING** (logs at `CombatEntity.cpp:83-85`, `Player.cpp:263-266`) |
| `Player::modifyStat` (`src/Player.cpp:223-247`) | **MISSING** |
| `Entity::pain/died` for eType 10 / 9 (`src/Entity.cpp:371-391,437-458`) | **MISSING**; `MonsterSystem::diedMonster` early-returns on non-monsters (`MonsterSystem.cpp:191`) |
| `launchProjectile`, missile sprites, splash | **MISSING** (all of G5) |
| `flagForWeapon` = 4096 for melee (`src/MovementController.cpp:198-212`) | **MISSING**; it has no live consumer yet (see §7) |

### 1.3 Secondary suspects from the research — checked, no action needed

* *"6-tile probe with RANGEMAX 1 → `crFlags 0x400`"*: cannot happen for the saw — the probe
  is already 1 tile (`Targeting.cpp:45`, `melee ? 1 : 6`). At two tiles nothing is elected, so the shot
  becomes an air shot into the world slot and no monster is touched (correct legacy
  behaviour: the swing still costs the turn and plays the pose).
* *`Player::fireWeapon` ownership guard* (`new_src/domain/game/Player.cpp:249`,
  `(weapons & (1 << weapon)) == 0`): correct as written and satisfied — the weapon was picked
  up, so the bit is set (the user could select it, which requires the same bit).

---

## 2. Group G1 — the degenerate projectile path (chainsaw deals damage)

Scope: two files, no new modules. Implements ADR 0017's `default:` arm and restores the one
dropped legacy arm in `explodeOnMonster`.

### G1.1 `new_src/domain/game/Combat.h`

Add state (next to the existing seq fields, keep the citation style of the file):

```cpp
	// Degenerate-projectile state (src/Combat.h:99,105 exploded/missileAnim).
	bool exploded = false;
	int missileAnim = 0;
```

Add methods (public, next to `explodeOnMonster`):

```cpp
	// Projectile launch (src/Combat.cpp:1433-1600). Only the default: arm is
	// implemented (ADR 0017): PROJTYPE -1 and 0 allocate no missile and set
	// exploded = true, so updateProjectile applies the hit in the same frame.
	void launchProjectile();

	// Missile stepping + the exploded dispatch tail (src/Combat.cpp:1249-1431).
	// Today only the tail exists; the missile loop arrives with group G5.
	void updateProjectile();
```

### G1.2 `new_src/domain/game/Combat.cpp`

1. **New `Combat::launchProjectile()`** — body is the legacy `default:` arm only:

```
missileAnim = 0;
exploded = true;
if (attackerWeaponProj > 0) log once: "[combat] projType %d not implemented, treated as instant"
```
Citation: `src/Combat.cpp:1566-1570`. The positive-type log can only fire if a future caller
bypasses the `Player::fireWeapon` guard; it is the safety net that keeps the fallback visible.

2. **New `Combat::updateProjectile()`** — the legacy tail verbatim
   (`src/Combat.cpp:1421-1430`):

```
if (!exploded) return;                 // (missile stepping loop: G5)
exploded = false;                      // legacy clears it inside each arm
if (curTarget == nullptr) { log "[combat] explodeOnPlayer deferred"; return; }  // :1422-1424
explodeOnMonster();                    // :1426-1427
```
Note the ordering deviation: legacy clears `exploded` *after* the explode call
(`:1428`), the rewrite clears it before. Equivalent (nothing inside re-reads it) and safer
against re-entry; keep the comment saying so.

3. **Stage 0**: replace the inline comment block + bare call
   (`Combat.cpp:282-286` and `:310`) with the two real calls at the legacy positions:
   * `launchProjectile();` where the comment sits now (legacy `src/Combat.cpp:325`) —
     i.e. **before** the `totalDamage == 0` message block;
   * `updateProjectile();` where `explodeOnMonster();` sits now (legacy
     `src/Combat.cpp:355`, after the ammo deduction and `nextStage/nextStageTime`).
   Keep every existing line and comment around them unchanged.

4. **`explodeOnMonster` head** (`Combat.cpp:389-395`): restore the legacy two-arm shape
   (`src/Combat.cpp:891-897`):

```cpp
	shotsFired = true;                                   // :891
	if (checkWeaponMask(attackerWeaponId, 0x2) && hitType == 0) {
		shotsFired = false;                              // :892-894 missed saw swing makes no noise
	} else if (curTarget != nullptr && curTarget->monster != nullptr && ... ) {
		env_.game->monsters.activate(curTarget, true, false, true, true);   // :895-897
	}
```
This is behaviourally load-bearing: with the current `if`/`if` shape a **missed** chainsaw
swing would wake the monster, which the original explicitly avoids.

### G1.3 `new_src/domain/game/Player.cpp`

Replace the refusal at `:278-285` with:

```cpp
	// PROJTYPE -1 (WP_PROJ_NONE) and 0 (WP_PROJ_BULLET) share the degenerate
	// launchProjectile default: arm (src/Combat.cpp:1566-1570, ADR 0017) and are
	// fully supported. Positive types need real missiles (spec group G5): refuse
	// them like the soul-cube guard instead of mis-firing instant damage.
	const int proj = wdef.projType;
	if (proj > 0) {
		std::fprintf(stderr, "[combat] projectile weapon %d unsupported (proj=%d)\n", weapon, proj);
		return false;
	}
```

Nothing else in `fireWeapon` changes in G1 (the chainsaw-corpse `usedChainsaw` hook at
`:263-266` stays a log until G2/G4).

### G1.4 Build / integration

No new files → no CMake reconfigure. `cmake --build build_new -j 8`.

### G1.5 Acceptance — what the user must see

Preconditions: chainsaw owned and selected (HUD shows the saw, no ammo digits).

1. **Adjacent monster.** Stand one tile from a monster, face it (its health bar appears at the
   top), press fire:
   * the weapon sprite jumps to the attack pose and holds it for **~1 second** (SHOTHOLD 100
     → 1000 ms) — noticeably longer than the rifle;
   * the monster health bar drops by roughly **20-27** per swing for an ordinary monster
     (base 18-21 + strength bonus at STR 10 − ~5 % defence), i.e. clearly more than the
     rifle's 8-10, and a centre message "…damage" appears;
   * after 2-3 swings the monster dies (death frame + XP message), one swing = one turn
     (monsters move between swings).
   * against a **zombie** the drop is about **twice** as large (weakness high nibble 0xF →
     ×2.0, `combat.md` §9.6). Against a saw goblin it is ~⅛ (×0.125) — expected, not a bug.
2. **Two tiles away**: nothing is elected → no damage, no message, but the pose plays and the
   turn is consumed. Correct legacy behaviour (RANGEMAX 1).
3. **Log contract**: `[combat] projectile weapon 1 unsupported` must be **gone**; each swing
   prints `[combat] performAttack ... proj=-1` followed by `[combat] fire sound 1015`.
4. **Regression**: the rifle behaves exactly as before (same damage numbers, same 500 ms pose).

---

## 3. Group G2 — chainsaw feedback and the strength-growth counter

Scope: `Player`, `Combat` env, `CombatEntity` hook. No prop/corpse work here.

### G2.1 `new_src/domain/game/Player.h/.cpp`

```cpp
	int chainsawStrengthBonusCount = 0;   // src/Player.h (usedChainsaw counter)

	// src/Player.cpp:223-247. Health arm delegates to addHealth (painEvent is
	// absent); stat arm clamps at 99 (200 for STAT_IQ = 7) and returns the
	// applied delta. updateStats() (:243) has no port: mirror the single
	// changed stat into ce, which is what the legacy recompute does here.
	int modifyStat(int stat, int delta);

	// src/Player.cpp:2636-2653. fromHit == true means "called from calcCombat
	// after a landed hit" and lets a crit count twice; combat supplies crFlags,
	// the message feed and the clock.
	void usedChainsaw(Combat& combat, bool fromHit);
```

`modifyStat` body (cite `src/Player.cpp:223-247`):
* `stat == 0` → `int before = getHealth(); addHealth(delta); return getHealth() - before;`
* else → `int before = baseCe.getStat(stat); int cap = (stat == 7) ? 200 : 99;
  if (before + delta > cap) delta = cap - before; if (delta != 0)
  baseCe.setStat(stat, before + delta); ce.setStat(stat, baseCe.getStat(stat));
  return baseCe.getStat(stat) - before;`

`usedChainsaw` body (cite `src/Player.cpp:2636-2653`):
```
++chainsawStrengthBonusCount;
if (fromHit && (combat.crFlags & 0x2) != 0) ++chainsawStrengthBonusCount;   // crit counts twice
if (chainsawStrengthBonusCount >= 30) {
    chainsawStrengthBonusCount %= 30;
    int applied = modifyStat(Enums::STAT_STRENGTH, 2);
    if (applied != 0) { args[0] = to_string(applied); combat.centerMessage(241, args, 1); }
}
combat.env().hud->startShake(*combat.env().upTimeMs, 666, 2);   // startShake(666, 1, 0)
```
`Hud::startShake` takes the **already doubled** amplitude (`new_src/ui/Hud.cpp:70-76`,
precedent `new_src/domain/game/ScriptVM.cpp:1196-1200`), hence the literal `2` for legacy
`i2 = 1`. The vibration argument (`i3 = 0`) has no desktop counterpart.

### G2.2 `new_src/domain/game/Combat.h` + `new_src/core/Main.cpp`

Shake runs on the **upTime** clock, not `gameTime` (`new_src/core/GameContext.cpp:286` calls
`tickShake(upTimeMs)`). Add to `Combat::Env`:

```cpp
		const int64_t* upTimeMs = nullptr;   // ctx->upTimeMs (Hud shake clock)
```
and extend the `game.combat.init({...})` call in `Main.cpp:287-288` with `&ctx.upTimeMs`
(update the trailing comment listing the Env fields).

### G2.3 Call sites

* `new_src/domain/game/CombatEntity.cpp:81-85`: replace the log with
  `c.env().player->usedChainsaw(c, true);` — legacy `src/CombatEntity.cpp:146-148`, i.e.
  **after** the hit test, before `calcDamage`, so `crFlags` already carries the crit bit.
* `new_src/domain/game/Player.cpp:262-266`: replace the "gib path deferred" log with the
  legacy pre-attack hook (`src/Player.cpp:777-779`): if `weapon == 1` and the target is
  `ET_CORPSE`, or `ET_ATTACK_INTERACTIVE` with `eSubType` `INTERACT_BARRICADE (1)` /
  `INTERACT_FURNITURE (0)`, call `usedChainsaw(combat, false)`.
  Guard for `target != nullptr && target->def != nullptr` (legacy dereferences blindly).

### G2.4 Acceptance

* Every chainsaw swing (hit **or** miss, and also every swing at furniture/corpses) shakes the
  screen briefly — the whole canvas jitters for ~⅔ second, weapon sprite included.
* After 30 counted swings a centre message appears (str 241, "strength" growth) and the
  character sheet / menu stats show **Strength 12** instead of 10; damage per swing rises by
  ~2 accordingly. Crits count double, so it can arrive slightly earlier.
* No shake and no counting when the swing is refused (no weapon / wall push).

---

## 4. Group G3 — destructible props: the chainsaw cuts furniture, crates stay intact

Scope: ADR 0018 dispatch + the `ET_ATTACK_INTERACTIVE` arms.

### G3.1 `new_src/domain/game/Game.h/.cpp`

```cpp
	// Dispatchers mirroring the legacy entity methods (ADR 0018).
	bool entityPain(Entity* e, int damage);   // src/Entity.cpp:283-393
	void entityDied(Entity* e, bool giveXP);  // src/Entity.cpp:424-...
private-ish helpers (may stay public for symmetry):
	bool painProp(Entity* e);                 // src/Entity.cpp:371-391
	void diedProp(Entity* e);                 // src/Entity.cpp:437-447
```

`entityPain`:
```
if (e == nullptr || e->def == nullptr) return false;
if ((e->info & Entity::kInfoActive) == 0) return false;         // src/Entity.cpp:286-288
switch (e->def->eType) {
  case ET_MONSTER:            return monsters.painMonster(e, damage, combat.attackerWeaponId);
  case ET_ATTACK_INTERACTIVE: return painProp(e);
  default:                    return false;                      // legacy has no other arm
}
```
`entityDied`: same shape, arms `monsters.diedMonster(e, giveXP)` / `diedProp(e)` /
`diedCorpse(e)` (G4), plus the legacy pre-guard
`if ((e->info & kInfoActive) == 0) return;` (`src/Entity.cpp:431-433`).

`painProp` (props have **no HP** — any landed hit destroys them, `src/Entity.cpp:371-391`):
```
const int sub = e->def->eSubType;
if (sub == Enums::INTERACT_BARRICADE) {            // 1
    map->mapSpriteInfo[s] = (map->mapSpriteInfo[s] & 0xFFFF00FF) | 0x100;  // broken frame 1
    db.unlinkEntity(e);                            // becomes passable; entity survives
    // particles + sound 1038: no systems (log)
} else {
    if (sub == Enums::INTERACT_PICKUP) {           // 3
        std::fprintf(stderr, "[combat] water-spout conversion deferred\n");  // turnEntityIntoWaterSpout
        return false;                              // legacy returns before removeEntity
    }
    db.removeEntity(e);                            // hides the sprite (0x10000) + unlinks
    e->info |= Entity::kInfoDirty;                 // legacy info |= 0x400000
}
facingDirty = true;
return false;
```
`db.removeEntity` already sets `mapSpriteInfo |= 0x10000` (`new_src/domain/game/
EntityDb.cpp:95-103`), which covers the legacy `:389` line.

`diedProp` (`src/Entity.cpp:437-447`):
```
args[0] = Localization::titleOf(loc->get(kTextIngame, e->def->name));   // legacy name = def->name | 0x400
combat.centerMessage(89, args, 1);                                     // "%01 destroyed"
player->addXP(5);
if (e->def->eSubType != 3 && e->def->eSubType != 2) ++numDestroyableObj;   // destroyedObject(sprite)
```
(`Game` already reaches `loc`/`hud` for the loot text; reuse the same members. Field
`numDestroyableObj` exists at `new_src/domain/game/Game.h:112` — it is the map-completion
counter the legacy `destroyedObj` feeds.)

### G3.2 `new_src/domain/game/Combat.cpp`

* `explodeOnMonster` monster arm (`:404`): `env_.game->monsters.painMonster(...)` →
  `env_.game->entityPain(curTarget, totalDamage)`.
* `explodeOnMonster` eType 10 arm (`:409-411`): replace the log with
  `if (totalDamage > 0) env_.game->entityPain(curTarget, totalDamage);`
  (`src/Combat.cpp:933-937`).
* Stage 1 death (`:325`): `env_.game->monsters.diedMonster(curTarget, true)` →
  `env_.game->entityDied(curTarget, true)` (`src/Combat.cpp:382-386`).

No change is needed in `calcHitEntity`: the `tableCombatMasks` gate is already ported
(`Combat.cpp:374-378`) and shipped table 4 = `{0, 0x2, 0xFFFFFFFB, 0xFFFFFFFB}`, so
`parm 1` furniture accepts **only** bit 1 = the chainsaw and `parm 0` crates accept
**nothing** (`combat.md` §9.5).

### G3.3 Acceptance

* map00 furniture is the two defs with tiles **121** and **135** (`combat.md` §9.5): walk up
  to one, equip the chainsaw, look at it (its name shows in the HUD), fire:
  * the object **disappears** (sprite gone, tile walkable afterwards),
  * a centre message names it ("… destroyed", str 89), XP goes up by 5 (the XP counter in the
    menu / next level-up arrives sooner);
* the same object with the **rifle** selected: it is not even targeted (the shot passes as an
  air shot; no message, nothing destroyed) — that is the legacy furniture rule;
* **crates** (tile 152, eSubType 2): chainsaw fire does **nothing** to them — no message, no
  disappearance; they still open with the normal "use" press (ADR 0016 path unchanged);
* barricades (tile 178) if reachable: change to a broken frame and stop blocking movement.

---

## 5. Group G4 — the chainsaw and corpses (gib), plus `flagForWeapon`

### G4.1 `new_src/domain/game/Game.cpp`: `diedCorpse` (`src/Entity.cpp:448-458`)

```
map->mapSpriteInfo[s] |= 0x10000;                       // hide
e->info |= Entity::kInfoHidden | Entity::kInfoDirty;    // legacy 0x410000
e->info &= ~0x80000;                                    // legacy &= 0xFFF7FFFF (gib-instantly bit)
db.unlinkEntity(e);
// counters[4] (corpses destroyed): run-stats absent
```

### G4.2 `new_src/domain/game/Combat.cpp`: the gib arm (`src/Combat.cpp:938-945`)

Replace the log at `:411-412` with: hide the sprite (`mapSpriteInfo |= 0x10000`), set a new
`bool isGibbed` field (`src/Combat.h`), log the deferred `spawnDropItem` (no port exists —
`new_src/domain/game/ItemPickup.cpp:53`) and the deferred sound 1037.
Stage 1 then reaches `entityDied` → `diedCorpse` via the existing `targetKilled` path
(non-monster targets set `targetKilled = true` at `Combat.cpp:298-300`).

### G4.3 `Combat::flagForWeapon` (`src/MovementController.cpp:198-212`)

```cpp
	// 4096 for melee, 16384 for the rocket launcher, 8192 otherwise.
	static int flagForWeapon(int weaponId);
```
Its two legacy consumers are the stage-1 corpse-script hook
(`src/Combat.cpp:373-376`, `executeTile(..., 0xFF4 | flagForWeapon(id), true)`) and
`shouldFakeCombat` (`src/PlayingInputHandler.cpp:580-584`). Port the **first** one here (it
belongs to the corpse branch this group implements: `targetType == 9 && targetSubType == 17`);
`shouldFakeCombat` stays out of scope (§7).

### G4.4 Acceptance

* Kill a monster with any weapon, then equip the chainsaw and swing at the corpse from one
  tile: the corpse **vanishes** (no loot menu opens — melee replaces looting), and the tile is
  clear afterwards.
* With any other weapon the same corpse still opens the loot UI (regression check).
* No item drops from the gib yet (`spawnDropItem` is unported) — a log line documents it.

---

## 6. Group G5 — real projectiles (positive `PROJTYPE`), the big step

Not to be started before G1-G4 are accepted. Scope sketch, in sub-groups; each needs its own
spec section when it comes up:

* **G5.1 dynamic sprite allocation.** `gsprite_allocAnim` (`src/Game.cpp`) has no rewrite
  counterpart: the missile is a *sprite created at runtime* in the map sprite arrays
  (map00 uses 191 of 275 slots, `docs/status.md`). Needs a `GameSprites` peer module owning
  the free-slot pool, the anim/duration fields and the per-frame update; `SpriteLerps` is the
  closest existing pattern but interpolates existing sprites only.
* **G5.2 `launchProjectile` cases 2,3,4,7,8,10,11** (`src/Combat.cpp:1481-1565`): start point
  (view or attacker sprite), end point (target sprite / `traceCollision`), `missileAnim`,
  render mode, the `crFlags 0x400` short-fall re-aim (`:1577-1590`) and the `hitType == 0`
  fly-past (`:1591-1600`).
* **G5.3 `updateProjectile` missile stepping** (`src/Combat.cpp:1249-1420`): the
  `numActiveMissiles` loop, arrival test, `exploded` latch — the head of the method whose tail
  G1 already ports.
* **G5.4 splash damage**: `radiusHurtEntities` (`src/Combat.cpp:899-901,922-924`) for weapons
  11/12, plus `checkForBFGDeaths` for `PROJTYPE 5`.
* **G5.5 the stage machine's projectile terms**: `numActiveMissiles`/`animatingEffects` in the
  head stage advance (`src/Combat.cpp:182-187`), currently noted as absent in
  `new_src/domain/game/Combat.cpp:180-181`.
* **G5.6** drop the `proj > 0` refusal in `Player::fireWeapon` case by case as the types land.

Ordering rationale: G5.1 is a prerequisite for everything else and is invisible on screen
until G5.2, so those two ship together; splash last.

---

## 7. Deliberately NOT ported in G1-G4 (with reasons)

| Legacy behaviour | Why not |
|---|---|
| chainsaw sound 1015, prop sound 1038, gib sound 1037 | the project has **no audio backend at all**; ids are logged (`Combat.cpp:261-279` precedent) |
| `punchingMonster` / `punchMissed` / fists | unreachable in the shipped build: `WP_PUNCH_MASK = 0` (`combat.md` §9.1); porting dead code would add branches nobody can execute |
| `Combat::drawWeapon` chainsaw jitter + frame-1 return (`src/Combat.cpp:751-761,822-825`) | cosmetic; belongs to the `ui/ViewWeapon` model (`new_src/core/GameContext.cpp:596-597` already flags it). Schedule as a UI-side group after G4 if the user misses it |
| `rockView` recoil | the saw is in `WP_NORECOIL` anyway (`combat.md` §9.5), so nothing is lost for this branch |
| muzzle flash | `WP_MUZZLE_FLASH = 385` excludes the saw |
| blood/debris particles, knockback | no particle system, no knockback system |
| `spawnDropItem` on gib, `turnEntityIntoWaterSpout` | no ports exist (`ItemPickup.cpp:53`); logged |
| `shouldFakeCombat` (`src/PlayingInputHandler.cpp:574-584`) and its `flagForWeapon` use | needs a `doesScriptExist` API (already deferred, `Combat.cpp:387-388`) |
| `Entity::pain` boss thresholds / status effects | out of the weapon branch |
| run-stat counters (`counters[4]`, `counters[6]`, `counters[7]`) | no run-stats system; logged |

---

## 8. Verification checklist per delegation

1. `cmake --build build_new -j 8` clean (no new files in G1-G4 → no reconfigure; G5.1 adds
   files → `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` first).
2. `grep` the removed log strings: after G1 `projectile weapon 1 unsupported` must not exist
   in the binary path; after G2 `usedChainsaw deferred` and `chainsaw gib path deferred` are
   gone; after G3 `ATTACK_INTERACTIVE pain branch deferred` is gone; after G4
   `corpse gib branch deferred` is gone.
3. Rifle regression after every group (damage, pose length, ammo count).
4. The screen-visible criteria of §2.5 / §3.4 / §4.3 / §5.4 confirmed by the user.
