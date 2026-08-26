# ADR 0008 — Combat module placement and monster payload structs

Date: 2026-08-26 (spec `specs/2026-08-26-combat-stage1.md`). Evidence:
`docs/research/2026-08-26-combat-stage1.md` (§5 audit, §6 slice),
`docs/original-code/combat.md` §§5–6.

## Context

Stage 1 must make map00 fights playable: fire → hit/damage → pain/death →
lootable corpse, plus a real health-bar feed. The rewrite has tables, a
stats struct (`CombatEntity`) and monster-shaped entities — but no combat
math, no `EntityMonster` payload (`Entity::monster` is never set,
`new_src/domain/game/Entity.h:33`), no fire path, and four opcodes kill
script threads. Legacy keeps combat in a peer subsystem `app->combat`
(`src/Combat.h:12-176`) with per-shot state (`curTarget`, `stage`,
`animTime`, CR flags) and monsters as fixed-pool payloads
(`entityMonsters[80]`, `src/Game.cpp:430-436`) linked into circular
active/inactive rings.

Three structural questions had to be settled once: where combat math lives,
whether monsters become a class or stay a payload pointer, and whether the
combat sequence needs its own canvas state id.

## Decision

1. **New module `domain/game/Combat.{h,cpp}`; one instance owned by `Game`
   (`Game::combat`), wired once via `Combat::init(Env)` from `Main.cpp`.**
   This replays the legacy topology (`app->combat` peer of `app->game`)
   instead of smearing state over `GameContext` or free functions. Pure stat
   math stays on `CombatEntity` as methods taking `Combat&` as first
   parameter — same shape as legacy's implicit-singleton signatures
   (`src/CombatEntity.cpp:130,157,300`), so the ports stay line-comparable.
   All math is integer/fixed-point verbatim; rolls use `std::rand()&0xFF`,
   matching what the RE port itself does (`src/App.cpp:506-512` — the
   original LCG is already gone upstream, so roll-exactness is not an
   invariant).
2. **EntityMonster is a plain payload struct from a fixed pool of 80**
   (`src/Game.cpp:430-436`), pointed to by `Entity::monster`, with circular
   `activeMonsters`/`inactiveMonsters` rings and faithful
   `activate`/`deactivate` ports now (`src/Game.cpp:752-808, 825-855`). Not
   a polymorphic class: legacy has no virtuals here, the pool bounds entity
   count exactly like `entities_[275]`, and Stage 2's AI needs only the flat
   fields (`goal*`, `target`, `nextAttacker`) plus the Game-side
   `combatMonsters` queue head — all declared now.
3. **No ST_COMBAT StateId in Stage 1.** The player attack sequence runs as a
   timer inside Playing (`Game::combat.active` + `tick()` at the legacy
   updateMonsters position); input drops while active and `advanceTurn()`
   fires on seq completion (`src/GameStateRunner.cpp:26-38`). Visual output
   is identical (legacy ST_COMBAT renders the same world + weapon,
   `src/Canvas.cpp:1344-1355`); we avoid stateVars reset and enter/exit
   churn until Stage 2 proves it needs distinct gating.

## Consequences

- Combat code diffable against `src/Combat.cpp` / `src/CombatEntity.cpp`
  line-by-line during review; no hidden singleton lookups.
- Monsters gain stats/wake/kill/loot semantics without touching the
  renderer (stacked path already keys off `def->eType == ET_MONSTER`,
  ADR 0007). `monstersTurn` opens and closes within one Playing tick until
  Stage 2 lands AI; `interpolatingMonsters` exists but stays false, so the
  Error-95 discipline degenerates to a defensive log — safe by construction.
- A second consumer of the trace internals appears (facing probe); solved by
  exposing `Game::lastTraceHits()` rather than duplicating traces.
- The `active` flag lives outside the state machine: states entered while a
  seq runs (dialog via script) would keep ticking the seq — currently
  unreachable because scripts that shoot (op 56) park their thread and no
  dialog can open mid-seq on the map00 route.

## Rejected alternatives

- **Combat methods spread across `Game`/`GameContext`**: would bloat two
  already-large files with legacy-shaped mutable state and make the verbatim
  port harder to review; also breaks the Canvas/Game/Combat separation the
  original enforces.
- **Full `EntityMonster` class hierarchy (per-family behavior)**: nothing in
  the original supports it; families differ by data (monsterAttacks table)
  and render routing (ADR 0007), not by type. Would add indirection ahead of
  any Stage-2 need.
- **Defer rings/activate/deactivate to Stage 2** (research §6 item 4 offers
  an "implicitly active" shortcut): rejected — ~80 lines now buys correct
  wake-on-shot semantics (activate-on-hit, `src/Combat.cpp:895-897`), the
  MFLAG_TRIGGERONACTIVATE static func, and a clean Stage-2 base without a
  migration later.
- **Real ST_COMBAT state id now**: costs stateVars reset + enter/exit hooks
  + a third render branch for zero visible difference; revisit only if
  Stage 2's monster attack seq needs distinct input handling.

## Index

- Spec: [2026-08-26 — Combat Stage 1](../specs/2026-08-26-combat-stage1.md)
