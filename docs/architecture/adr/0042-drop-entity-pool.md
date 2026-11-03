# ADR 0042 — Drop-entity pool: 16 legacy drop entities + 16 sprite slots after the 48 custom ones; `EntityDb` owns the ring, `Game` owns the spawners

Date: 2026-11-02
Status: accepted
Spec: `../familiar.md` (groups G2, G3)

## Context

The familiar needs world entities created at run time: the body left at the deploy spot
(`Game::spawnPlayerEntityCopy`, `src/Game.cpp:2725-2768`), the sentry corpse
(`spawnSentryBotCorpse`, `:2770-2811`), dropped items thrown from the bot remains and the discarded
bot (`spawnDropItem`, `:2647-2679`, `throwDropItem`, `:2822-2853`). Legacy appends 16 drop entities
after the map entities (`src/Game.cpp:487-495`), bound to 16 drop sprites appended after the 48
custom sprites (`src/Render.cpp:2511-2523`), recycled by a ring (`getFreeDropEnt`,
`src/Game.cpp:2612-2622`), and saved in the world record with their own branches
(`src/Entity.cpp:1497-1572, 1700-1752, 1787-1798`) plus `dropIndex` (`src/Game.cpp:1708, 1876`).
The rewrite has none of it (`new_src/domain/game/EntityState.cpp:43-44, 79-80`,
`WorldState.cpp:272`).

## Decision

1. **Layout is legacy:** `MapData::numSprites = numMapSprites + 48 + 16`; drop sprite k is
   `firstDropSprite() + k`; the 16 drop entities are appended right after the spawn loop, entity k
   bound to drop sprite k, so entity indices match legacy and `playerEntityCopyIndex` / world-record
   handles stay byte-compatible.
2. **`EntityDb` owns the ring state** (`firstDropIndex`, `dropIndex`, `lastDropEntIndex`,
   `getFreeDropEnt`, `isDropped`) because it owns the entity array and the save codec reads it.
3. **`Game` owns the spawners** (`spawnDropItem` x2, `throwDropItem`, `spawnPlayerEntityCopy`,
   `spawnSentryBotCorpse`, `attemptToDiscardFamiliar`) because they need `map`, `defs`, `lerps`,
   `trace`, `ai` (monster-clip bitset), `hud`/`loc` — all already on `Game`.
4. **`Entity` gains `int16_t name = -1`** (-1 = "the def's default name"), used by drop entities and
   written/read through the legacy named-entity bit `0x20000`. Map entities keep -1 and never write a
   name, exactly as legacy (whose map entities always hold the default name).

## Consequences

- `EV_DROPMONSTERITEM`'s on-the-spot grant (`ItemPickup::touchedDrop`) can later switch to the real
  pool; not part of this work.
- World blobs written before this change carry `dropIndex = 0` and no drop records: they load
  unchanged.

## Rejected alternatives

- **Using the 48 `GameSprites` slots:** those are recycled every frame by missile/impact code and
  are not saved; drops must persist and be saved.
- **Growing the entity vector dynamically:** breaks legacy indices (saves, `playerEntityCopyIndex`).
