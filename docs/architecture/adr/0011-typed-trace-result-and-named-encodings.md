# ADR 0011 — Typed trace result (`TraceHit`) and named legacy encodings

_Date: 2026-08-26. Status: accepted. Spec:
[specs/2026-08-26-decomposition.md](../specs/2026-08-26-decomposition.md) §5._

## Context

The trace result is currently a `std::vector<std::pair<int, Entity*>>` plus a
`bool` return and two out-params (`new_src/domain/game/Game.h:186-192`,
`Game.cpp:604-643`). Two conventions ride on those naked values, documented only
in comments:

1. **A world hit is `entities_[0]` with `def == nullptr`** (`Game.cpp:611`).
   Every consumer must know that and decode it itself
   (`GameContext.cpp:1101-1102`, `:1229-1230`, `:1334-1335`, `Game.cpp:794-800`).
   The 2026-08-26 review blocker 1 — "wall shots fired and burned the turn" —
   existed because the fire commit mapped a world hit to `hitType == -1`, so the
   whole wall-push branch was dead code.
2. **`frac == -1` means the hit entity overlaps the sweep start** (produced by
   `Game.cpp:494,516`), therefore it sorts first (`Game.cpp:625-626`). That is
   what made a corpse under the player's feet swallow the shot.

Both bugs were *representable* because the type system said nothing.

Separately, several legacy encodings appear as bare literals whose meaning lives
only in `docs/`: content masks `13997` / `21741` / `13501` and the per-weapon
modifiers `| 0x4100` / `| 0x10`; the stride-9 weapon table
(`weapons[weaponId * 9 + field]`, `Combat.cpp:81`, `Game.cpp:1159-1160`) and the
stride-6 `weaponInfo`; `tileDistances[0/2/3/9]`; `mapSpriteInfo & 0xFF == 0x95`;
`mapFlags & 0x2`.

## Decision

1. **`TraceHit` (new `domain/game/TraceHit.h`)** carries
   `kind ∈ {None, World, Ent}`, `entity`, `frac`, `eType`, `eSubType`, with
   `blocks()`, `isWorld()`, `isEntity()` and `startsInside()`. `kFracMiss = 16384`
   and `kFracAtStart = -1` are named constants of the type.
2. **`eType` is stored, not derived.** `TraceSystem` resolves
   `def == nullptr → ET_WORLD` exactly once, when the hit is pushed. No consumer
   is allowed to test `entity->def == nullptr` again; the reviewer greps for it.
3. **`TraceSystem::trace(...)` returns a `TraceHit` by value**; the old
   `bool` + out-params disappear (`!hit.blocks()` is the commit gate).
   `hits()` returns `const std::vector<TraceHit>&`, sorted by `frac` as today.
4. **Distance stays a method, not a field**: `TraceSystem::distFrom(const
   TraceHit&, int x, int y)` switches on `kind` (the `World` case reads
   `collisionX/Y`, mirroring legacy `Entity::calcPosition`,
   `src/Entity.cpp:1375-1378`). A field would force computing it for every hit
   and would be undefined for a world hit before the world pass.
5. **Encodings get names, never new semantics.** Masks are *composed* from
   `Contents::bit(ET_*)` and pinned with `static_assert` against the legacy
   value (`PLAYERSOLID == 13501`, `WEAPONSOLID == 13997`,
   `FACING_PROBE == 21741`, `HOLY_WATER_EXTRA == 0x4100`, `MELEE_EXTRA == 0x10`).
   The existing `CONTENTS_*` names survive as aliases so no call site is forced
   to churn in the same group.
6. **Typed row views** (`domain/game/WeaponTable.h`): `WeaponRow` over
   `tables->weaponData` with a `WeaponField` enum and `kWeaponDataStride = 9`,
   `WeaponPose` over `tables->weaponInfo` with `kWeaponInfoStride = 6`. Existing
   clamp semantics (out-of-range reads as 0, `Combat.cpp:80-84`) are preserved
   inside the view; call sites that mask with `& 0xFF` keep the mask, because the
   unsigned reinterpretation is legacy behaviour.
7. **`Combat::tileDistSq(tiles)`** replaces `tileDistances[n]` at call sites:
   index `j` means `j+1` tiles (`tileDistances[j] = (64*(j+1))²`,
   `Combat.cpp:22-24`), which is exactly the off-by-one a reader trips over.
8. **Literals whose legacy meaning is not yet established are left alone**
   (`info & 0x200000`, `info & 0x20000000`, `0x2000000`, the loot-entry packing
   constants). Inventing a name for an unverified bit would be worse than the
   literal. They are listed in the spec §5.6 as researcher follow-ups.

## Consequences

- The two bug classes become unrepresentable: "the world" is a `kind`, not a null
  pointer, and "on my own tile" is `startsInside()`, not a magic `-1`.
- Bit-exactness is unaffected: the sort key, the `-1`, the `16384` sentinel, the
  push order and the "no hits ⇒ commit" rule are untouched. The
  `static_assert`s make any future mask edit that changes a value fail to
  compile.
- All trace consumers change signature in one migration group
  (`Targeting::electFireTarget`, `Targeting::updateFacingProbe`,
  `PlayerActions` move + fire commit, `Combat::calcHitEntity`) — enumerated in
  the spec §5.2 so none is missed.
- Slightly larger hit records (a `pair` becomes a 5-field struct). Irrelevant:
  hit lists are a handful of entries per trace on a single-threaded loop.

## Rejected alternatives

- **Keep `(frac, Entity*)` and add a helper `entityType(Entity*)`.** Leaves the
  world convention discoverable only by convention; a new caller can still write
  `def->eType` and crash or silently mean "-1".
- **A dedicated `WorldEntity` singleton instead of `entities_[0]`.** Deviates
  from the legacy data model that the combat math reads (`eType 0` semantics via
  `def == nullptr`) and would ripple into `Combat`/`CombatEntity`.
- **`std::variant<WorldHit, EntityHit>`.** More ceremony at every call site than
  a `kind` field, and the hit list wants one uniform element type for the sort.
- **`std::optional<TraceHit>` for "nothing hit".** `TraceHitKind::None` keeps the
  frac/collision-point information available on a miss, which the air-shot path
  needs (`traceCollisionX/Y` at the ray end).
- **Renaming the masks to new "improved" names (e.g. `MASK_SHOOTABLE`).**
  Rejected: the doc trail and `src/Enums.h` use `CONTENTS_*`; matching the
  original keeps every citation greppable.
