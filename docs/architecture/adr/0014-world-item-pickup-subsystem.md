# ADR 0014 — World item pickup as a peer subsystem, entities stay passive

Date: 2026-08-29. Status: accepted.
Spec: `docs/architecture/specs/2026-08-29-world-item-pickup.md`.
Facts: `docs/original-code/loot-inventory.md` §3, §3.1;
`docs/research/2026-08-29-world-item-pickup.md`.

## Context

The legacy pickup path is three methods on `Entity`: `touched()` dispatches by `eType`,
`touchedItem()` grants by item class and `Game::touchTile()` walks the tile chain
(`src/Entity.cpp:118-278`, `src/Game.cpp:687-699`). In `new_src`, `Entity` is a passive
record (`new_src/domain/game/Entity.h`: fields + accessors only) and every behaviour slice
was already extracted into peer subsystems with an injected `Env`
(`DoorSystem`, `MonsterSystem`, `SpriteLerps`, `CorpseLoot`, `EntityDb`; ADR 0010).
`touchedItem` needs Player, Hud, Localization, Tables, EntityDb, ScriptVM and the Game
run-stat counter — i.e. exactly the dependency set an `Env` carries.

A second question came with it: item entities do not exist yet, because `loadEntities`
deliberately limits itself to door/monster/NPC/corpse families to avoid changing what
`TraceSystem::trace` treats as solid.

## Decision

1. Pickup logic lives in a new peer subsystem `domain/game/ItemPickup.{h,cpp}` owned by
   `Game` (`Game::items`), wired from `Main.cpp` like `Game::combat`. `Entity` gains no
   methods. `Game::touchTile` keeps only the tile-chain walk and delegates each entity to
   `ItemPickup::touched`.
2. `loadEntities` spawns an entity for every `ET_ITEM` sprite (legacy has no item branch at
   all — item-ness comes purely from the def table). Safety is by construction, not by
   filtering: `Contents::PLAYERSOLID` omits bit 6, so no item can ever block the player
   (`src/Enums.h:29`, `new_src/domain/game/Enums.h:56,74`).
3. Quantities are rolled at pickup time with `std::rand()` (the `Applet::nextInt` analog,
   `src/App.cpp:506-508`), never read from map data — map sprites carry no quantity.
4. `Player::give` is brought to legacy semantics (sentry-bot pre-step, first-acquisition
   auto-equip, bottled-water→holy-water conversion, `quiet` flag) instead of duplicating
   those rules inside `ItemPickup`.

## Consequences

- One more peer on `Game`; the `Env` pattern stays the only wiring idiom.
- Item entities now appear in trace results for masks that include bit 6. The only such
  mask is `Contents::FACING_PROBE` (`src/MovementController.cpp:38`), whose
  monster-promotion rescan already special-cases an item as first hit
  (`new_src/domain/game/Targeting.cpp:171-206`) — this activates legacy-faithful behaviour
  that was dormant. The monster health bar stays gated on `isMonster`.
- Entity slots: map00 adds a few dozen item entities inside the 275-slot array; no pressure,
  but the existing `nextSlot >= kEntities` break stays the guard.
- Deviation: hidden item sprites (`mapSpriteInfo & 0x10000`) produce **no** entity, while
  legacy creates one and merely skips linking (`src/Game.cpp:453-455`). Identical for
  pickup; it will matter only when a script unhides an item sprite and expects a live entity.
- Deviation: no sound (1054) and no dropped-item plumbing; both recorded in the spec §5.

## Rejected alternatives

- **Methods on `Entity` (`Entity::touched/touchedItem`)** — literal legacy shape, but it
  would force a `Game*`/`Applet*`-style back-pointer into the data record and reintroduce
  the global-reach pattern ADR 0010 removed.
- **Code inside `Game.cpp`** — `Game.cpp` is the file the decomposition kept shrinking;
  adding ~150 lines of item-class grant rules there reverses that.
- **Folding pickup into `CorpseLoot`** — different trigger (tile occupancy vs facing use),
  different data (def-driven vs lootSet-driven), no shared code beyond `give`.
- **Spawning items only when a def is on the player's path / filtering by tile** — cheap
  hack, diverges from the loader's uniform behaviour and breaks script sprite lookups later.
