# ADR 0007 — Monsters join the entity-driven stacked-character path

Date: 2026-08-26 (spec `specs/2026-08-26-monsters-stack-flicker.md`)
Amends: [ADR 0005](0005-character-detection-stacked-billboards.md) decision 2
(ET_MONSTER exclusion). Evidence:
`docs/research/2026-08-26-monster-render-flicker.md`.

## Context

ADR-0005 shipped stacked-billboard rendering for NPCs only, excluding
ET_MONSTER until "per-family routing arrives with EntityMonster". User-visible
result on map00: live imps render as a single legs-frame quad ("половинчато")
because unclassified sprites fall into the billboard fallback whose mediaId
clamp pins them to frame 0 art (research Q2). Meanwhile a temporary
MANIM-byte detour in `World3D::drawSprite` routed dead sprites around the
clamp — a crutch with no legacy counterpart. Separately, the rewrite-only
`eType == ET_NPC || ET_MONSTER` gate on the lerp alloc-reuse anim reset
stamps IDLE onto active NPC walkers when a second lerp reuses their slot —
the reported mid-walk flicker (research candidate 2).

Verified facts that shrink the original risk assessment: monsters share the
NPC stack renderer in the original (`monster != nullptr` gate,
`src/Render.cpp:1622-1626`); only two families divert before the switch
(`isFloater`/`isSpecialBoss`, `src/Render.cpp:3023-3029`, applied at
`:3157-3166`); map00's monster tiles (18/19 sentry, 20–22 zombie, 23–25 imp,
50–52 arch-vile) hit neither diversion; imp sheets have no full-body frames —
live imps exist ONLY as stacked parts.

## Decision

1. **Enable ET_MONSTER in the charClass rule**, mirroring the legacy
   `monster != nullptr` gate via its exact proxy
   `def->eType == ET_MONSTER` (legacy allocates `monster` iff eType==2,
   `src/Game.cpp:430-436), EXCEPT sprites whose tileNum belongs to the
   floater or special-boss families, which stay class 0 until their family
   renderers are ported (they would render wrongly through the humanoid
   stack; unreachable on map00).
2. **Port only the ATTACK-branch deltas map00 needs**: zombie frame-1 leg
   flip (`src/Render.cpp:3299-3301` + consumer `:3333`), imp/zombie attack
   head suppression (`:3376`), gun-flare-aware pose increment
   (`:3355-3357`, `:3019-3021`). All other per-family specials stay deferred
   (spec §8).
3. **Retire the MANIM_DEAD-without-charClass routing hack**: classification
   becomes purely entity-driven again; entity-less DEAD sprites intentionally
   fall back to the single-quad billboard, which is what legacy does
   (`S_ENT==-1` ⇒ no `renderSpriteAnim`). The kInfoCorpse clause keeps
   corpsified monsters on the stack path.
4. **Narrow the alloc-reuse anim reset to monsters** (verbatim legacy
   predicate `entity->monster != nullptr`, `src/Game.cpp:3054`); keep the
   completion restore at the wider legacy `ET_NPC || ET_MONSTER` gate
   (`:3101-3111`) — the asymmetry is original behavior.
5. The renderer remains domain-clean (plain byte array into `drawBSP`,
   ADR-0005 point 3); monster tile constants are duplicated as file-local
   constants in `World3D.cpp` like the existing NPC ones.

## Consequences

- Map00 monsters render as complete animated stacks with zero new gameplay
  surface; EntityMonster groundwork (walk writer, sort bias) simply activates
  visually.
- Floater/special-boss ET_MONSTER defs would render via the billboard
  fallback (wrong-ish art) if future maps place them before their renderers
  land — guarded by an explicit exclusion, documented.
- Monsters hidden at load still get no entity (rewrite `loadEntities` skips
  hidden sprites; legacy does not); showing one by script leaves it on the
  billboard path until entity-parity work — deliberately untouched because it
  changes trace solidity.
- ADR-0005's rejected-alternative "include ET_MONSTER now" is partially
  enacted: safe now because the exclusion is narrowed to exactly the two
  diverted families instead of deferring wholesale.

## Rejected alternatives

- **Keep monsters excluded until full EntityMonster** (status quo): leaves
  the visible half-imp regression in place for no gameplay gain; the feared
  wrong-stack risk applies only to floater/boss families, which remain
  excluded.
- **Port all family routing now** (arch-vile offsets, sentry flip, floaters,
  special bosses): large render-only diff with no map00 visibility for most
  of it; violates the minimal-delta scope.
- **Keep/extend the MANIM_DEAD anim-byte hack**: unfaithful (legacy has no
  anim-byte routing), and it would misroute entity-less props as characters.
- **Also narrow the completion restore to monsters**: would diverge from
  legacy `:3101-3111`, which deliberately covers NPCs too.
