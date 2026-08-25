# ADR 0005 — Character detection & stacked-billboard rendering boundary

Date: 2026-08-25 (spec `specs/2026-08-25-character-animation.md`)

## Context

Humanoid characters in Doom II RPG are not single sprites: the original
composes them from up to three stacked camera-facing billboards plus a ground
shadow, all from ONE art tileNum with a 14+ frame sheet
(`docs/original-code/character-animation.md` §1). The rewrite's renderer draws
every sprite as a single quad resolving bits 8-15 of `mapSpriteInfo` as a
media-frame index. Those same bits are also the door open/closed media frame
and the AUTO_ANIMATE prop counter, so a stacked-character path must be
mutually exclusive with them by construction. The intro squad (map00 sprites
7/9/10) is the first consumer; EntityMonster comes later.

## Decision

1. **Detection is entity-def-driven**: a sprite takes the character stack
   path iff an entity bound to it has `def->eType == ET_NPC`, or is an
   ET_CORPSE whose art tileNum (`info & 0xFF`) lies in the NPC range 65-80
   (corpsified NPCs keep their NPC art tile after the def swap,
   `new_src/domain/game/Game.cpp:567-589`). No hardcoded sprite-index list.
   This reproduces the original gates (`src/Render.cpp:1624,1660`,
   `isNPC` range check `src/Render.cpp:2971-2973`) and excludes doors/props
   structurally: their defs resolve to other eTypes, so bits 8-15 can never
   be double-consumed.
2. **ET_MONSTER is excluded this cycle** even though monster entities exist:
   monster sheets need per-family routing (floaters, special bosses,
   torso-includes-head variants) that arrives with EntityMonster. The Game-side
   walk-state writer keeps the `ET_NPC || ET_MONSTER` gate, so the gameplay
   groundwork is pre-wired but visually dormant.
3. **The renderer stays domain-clean**: `World3D` receives a plain per-sprite
   classification byte array built by `GameContext::render` next to the
   existing `spriteSortBias` array (same borrowed-pointer ownership). No
   EntityDefs/Game dependency enters `render/`.
4. All character anim state remains the packed `(anim<<4)|frame` byte in
   `mapSpriteInfo` bits 8-15 — no new runtime struct; scripts, lerps and
   corpsify read/write one byte exactly like the original.

## Consequences

- Creating NPC entities (prerequisite) makes squad members solid to player /
  monster traces and counts them in `EV_TILE_EMPTY` — faithful to legacy but
  user-visible; documented in the spec checklist.
- Monster rendering improvements are deferred wholesale; monsters keep today's
  single-quad look until the EntityMonster unit ports
  `renderSpriteAnim`'s family routing.
- Pain auto-revert timers and lootable-corpse pulsate halos need
  monster-side state (`monster->frameTime`, flags) and ship later; scripts can
  already pose pain/death via ENTITY_FRAME / MAKE_CORPSE.

## Rejected alternatives

- **Hardcoded tileNum list** (e.g. "65..80 ⇒ character"): would render placed
  corpse props or future decor reusing those tiles incorrectly, and diverges
  from the original's entity-gated behavior.
- **Media-shape heuristic** ("mapping range ≥ 9 frames ⇒ sheet"): fragile —
  several non-character tiles have multi-frame ranges; fails on single-frame
  edge sheets; no legacy counterpart.
- **Renderer resolves defs itself** (pass EntityDefs into World3D): couples
  render/ to io/ game semantics and duplicates the entity-binding knowledge
  that only Game owns; rejected to keep World3D data-driven like the rest of
  the draw path.
- **Include ET_MONSTER now**: rejected — without floater/boss routing several
  map00 monsters would render through the humanoid stack with wrong part
  composition (e.g. mancubus head-over-torso), a visible regression risk for
  zero gameplay gain this cycle.
