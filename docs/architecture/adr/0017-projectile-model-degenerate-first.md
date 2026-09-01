# ADR 0017 — The projectile system is introduced degenerate-first: `launchProjectile`/`updateProjectile` exist from day one, only the `default:` branch is implemented

Date: 2026-08-31
Status: accepted
Spec: `specs/2026-08-31-weapon-branch.md`
Research: `docs/research/2026-08-31-melee-weapons.md`, `docs/original-code/combat.md` §9.4

## Context

The rewrite refuses to fire any weapon whose `PROJTYPE != 0`
(`new_src/domain/game/Player.cpp:279-285`), which is why the chainsaw (`PROJTYPE = -1`)
deals no damage: `Combat::performAttack` is never even reached.

Facts about the original (all verified, see §9.4 of `combat.md`):

* `Combat::launchProjectile` is a `switch (attackerWeaponProj)` with cases **2,3,4,5,6,7,8,
  9,10,11,13** only (`src/Combat.cpp:1481-1565`). Both `-1` (`WP_PROJ_NONE`) and `0`
  (`WP_PROJ_BULLET`) fall into `default: missileAnim = 0; exploded = true; return;`
  (`src/Combat.cpp:1566-1570`).
* The tail of `Combat::updateProjectile` dispatches on that flag:
  `if (exploded) { curTarget == nullptr ? explodeOnPlayer() : explodeOnMonster(); exploded = false; }`
  (`src/Combat.cpp:1421-1430`).
* Stage 0 calls `launchProjectile()` (`src/Combat.cpp:325`) and then `updateProjectile()`
  (`:355`) unconditionally, so a degenerate projectile applies its damage **in the same
  frame** — there is no separate "hitscan" code path in the original at all.

So "hitscan" and "chainsaw" are not two mechanisms: they are the same `default:` arm of one
projectile machine. The rewrite currently inlines that arm as a bare
`explodeOnMonster()` call with a comment (`new_src/domain/game/Combat.cpp:282-286,310`).

Options considered:

A. Keep the inline call and add `projType == -1` to the accept list in `Player::fireWeapon`
   (one-line fix).
B. Introduce a separate `applyMeleeDamage()` branch for melee weapons.
C. Introduce the real method pair `Combat::launchProjectile()` / `Combat::updateProjectile()`
   now, with the `exploded`/`missileAnim` state, and implement only the `default:` arm
   (which covers `-1`, `0` and — as a logged fallback — every not-yet-ported positive type).

## Decision

**Option C.** `Combat` gains the two legacy methods and the two legacy fields
(`bool exploded`, `int missileAnim`) immediately; the stage-0 body calls
`launchProjectile()` at the legacy position and `updateProjectile()` at the legacy position,
and the `exploded` dispatch lives in `updateProjectile`, exactly like `src/Combat.cpp:1421-1430`.
Positive `PROJTYPE`s stay refused in `Player::fireWeapon` for now, but the refusal condition
becomes `projType > 0` (a whitelist of "not yet implemented"), not `projType != 0`.

## Consequences

* The chainsaw (and weapons 4/6, exploding sentry bots, same `PROJTYPE -1`) start dealing
  damage through the very code the original uses, not through a parallel melee path.
* When real projectiles land (spec group G5) the work is *additive*: fill the `switch` cases,
  add the missile sprite pool and the per-frame missile stepping loop at the head of
  `updateProjectile`. No call site, no stage machine and no melee code has to be rewritten,
  and no "temporary" branch has to be deleted.
* The cost is two fields and two thin methods that look over-engineered while only the
  `default:` arm exists. Accepted: the alternative (option A/B) guarantees a rewrite of the
  damage-application call sites later, and option B would additionally invent a distinction
  ("melee") that the original does not have.
* `explodeOnPlayer()` (the `curTarget == nullptr` arm) is *not* created by this ADR: the
  player-directed half of combat (Stage 2) is out of scope; `updateProjectile` logs and skips
  that case.
