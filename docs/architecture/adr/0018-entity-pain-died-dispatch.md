# ADR 0018 — `Entity::pain` / `Entity::died` become one dispatcher pair on `Game`, with the arms in the owning subsystem

Date: 2026-08-31
Status: accepted
Spec: `specs/2026-08-31-weapon-branch.md` (groups G3, G4)

## Context

In the original, damage and death are **entity methods that switch on `eType`**:

* `Entity::pain(int damage, Entity* attacker)` (`src/Entity.cpp:283-393`) — arms for
  `ET_MONSTER` (:289-369) and `ET_ATTACK_INTERACTIVE` (:371-391, which destroys the prop
  outright: props have no HP).
* `Entity::died(bool giveXP, Entity* attacker)` (`src/Entity.cpp:424-...`) — arms for
  `ET_ATTACK_INTERACTIVE` (:437-447: message 89 with the prop name, `addXP(5)`,
  `destroyedObject`), `ET_CORPSE` (:448-458) and `ET_MONSTER` (:459+).

The rewrite has no `Entity` methods (`Entity.h` is a POD record, ADR 0014: "entities stay
passive"). Only the monster arms exist, as `MonsterSystem::painMonster` /
`MonsterSystem::diedMonster` (`new_src/domain/game/MonsterSystem.cpp:190-222`), and `Combat`
calls them **directly** (`new_src/domain/game/Combat.cpp:302,403`). Both early-return for a
non-monster entity, so today the chainsaw's prop/corpse targets are silently inert
(`diedMonster` guard `!e->isMonster()`, `MonsterSystem.cpp:191`).

Once the chainsaw fires, three eTypes reach the same two call sites, which is exactly the
polymorphism the original expresses with the `eType` switch.

## Decision

Add to `Game` (which already owns prop lifecycle — `openCrate`, `destroyedObj`, `lootSource`):

```cpp
bool entityPain(Entity* e, int damage);      // src/Entity.cpp:283-393
void entityDied(Entity* e, bool giveXP);     // src/Entity.cpp:424-...
```

Both are pure dispatchers on `e->def->eType`; each arm lives with its owner:

| eType | pain arm | died arm |
|---|---|---|
| `ET_MONSTER` (2) | `monsters.painMonster` (exists) | `monsters.diedMonster` (exists) |
| `ET_ATTACK_INTERACTIVE` (10) | `Game::painProp` (new, G3) | `Game::diedProp` (new, G3) |
| `ET_CORPSE` (9) | none (legacy has no arm) | `Game::diedCorpse` (new, G4) |

`Combat` stops calling `monsters.*` for damage/death and calls `env_.game->entityPain/
entityDied` instead. The `(info & 0x20000)` guard of the original (`src/Entity.cpp:286-288`,
`:431-433`) lives in the dispatcher, once, instead of being duplicated per arm.

## Consequences

* One entry point per event: every future damage source (scripts, splash damage, monster-vs-
  monster, env damage) routes through the same pair, as in the original.
* `MonsterSystem`'s guards stay as they are — they become redundant for the `Combat` path but
  keep the module safe for its other callers.
* Slight naming asymmetry (`entityPain` on `Game`, `painMonster` on `MonsterSystem`) is
  accepted: `Game` is the dispatcher and cannot be avoided as the module that knows all
  peers.
* Rejected: making `Entity` a class with virtual/`switch` methods (ADR 0014 keeps entities as
  passive records and every subsystem's `Env` would have to be reachable from `Entity`);
  rejected: putting the prop arms in `MonsterSystem` (nothing monster-related about
  furniture).
