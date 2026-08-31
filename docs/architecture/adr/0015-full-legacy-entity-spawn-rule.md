# ADR 0015 — Spawn an entity for every sprite with a def (drop the family whitelist)

Date: 2026-08-30
Status: Accepted
Spec: `specs/2026-08-30-blocking-crates-shelf-pickup.md` (G2)
Evidence: `docs/research/2026-08-30-sprite-blocking.md`, `docs/original-code/entities.md` §6

## Context

`Game::loadEntities` (`new_src/domain/game/Game.cpp:75-114`) spawns entities only for doors,
monsters, NPCs, corpses and items; every other sprite `continue`s. The original spawns an entity
for **every** sprite whose `lookup(tileNum)` returns a def, plus a fallback for def-less sprite
walls (`src/Game.cpp:373-486`).

Consequence of the whitelist: five eTypes that are inside `CONTENTS_PLAYERSOLID (13501)` —
`ET_PLAYERCLIP(4)`, `ET_DECOR(7)`, `ET_ATTACK_INTERACTIVE(10)`, `ET_SPRITEWALL(12)`,
`ET_NONOBSTRUCTING_SPRITEWALL(13)` — never become entities, so **86 player-solid objects on
map00 alone** (51 decor + 28 interactive + 7 def-less sprite walls) are walked through: the
user's complaint (в). It also makes crates impossible to implement (complaint (б)): there is no
entity to unlink, elect or animate.

Two ways out:
1. extend the whitelist with the four missing families;
2. implement the legacy rule verbatim and delete the whitelist.

## Decision

**Option 2: the legacy rule.** Every sprite with a def gets an entity; hidden sprites get one too
and are simply not linked; def-less sprites with `SPRITE_FLAG_SOLIDSIDE (0x800000)` borrow
`find(12,0)` / `find(13,0)`; the link tile is computed with the oriented-sprite nudge; the
`initspawn` per-family marks are restructured to mirror `src/Entity.cpp:50-111` branch for
branch.

The nudge and the `initspawn` branches are **part of the decision, not details**: without the
nudge 20 of 51 oriented map00 sprites block the wrong tile (a wall terminal would block the
middle of the corridor instead of the wall); without the decor unhide, decor flagged hidden in
the map data stays invisible *and* unlinked; without `ET_ATTACK_INTERACTIVE`'s `info |= 0x20000`
crates are not damageable objects and `Combat::calcHitEntity` reads them wrong.

## Consequences

Positive:
* complaints (в) and (б) become implementable; blocking matches the original everywhere,
  including the two "blocking without a family entity" mechanisms (def-less sprite walls here,
  `ET_PLAYERCLIP` map lines already handled in `TraceSystem::traceWorldFrac`);
* no further "family X is missing" bug class — the rule is data-driven, so future maps and
  script opcodes (`EV_HIDE`, `EV_ENTITY_FRAME`, `EV_GIVEITEM` mode 0) resolve *any* sprite to an
  entity, exactly like the original;
* capacity verified over all ten shipped maps: worst case 209/275 entities, 65/80 monsters
  (research §7) — no overflow.

Risks, and why they are acceptable (each checked against the current code):
* **TraceSystem** — more blockers, oriented sprites tested as ±32 segments: already implemented
  (`TraceSystem.cpp:143-165`); this is the intended effect.
* **Targeting** — the DECOR / ATTACK_INTERACTIVE / SPRITEWALL / DECOR_NOCLIP arms of
  `electFireTarget` become reachable for the first time. They are already ported verbatim,
  including the eType-10 weapon-range cull (`Targeting.cpp:132-138`).
* **Combat** — shooting a decor or a crate: `calcHitEntity` requires `kInfoActive` (decor never
  gets it → hitType 0) and gates eType 10 on `combatMasks[def->parm]`, and the crate's `parm=0`
  mask is 0 (`Combat.cpp:354-383`). The non-monster kill tail calls
  `MonsterSystem::diedMonster`, which early-returns for non-monsters
  (`MonsterSystem.cpp:191-192`). No crash, no destruction — objects are simply indestructible
  until `Entity::died/pain` is ported.
* **MonsterSystem** — `CONTENTS_MONSTERSOLID` also contains bits 7/10/12/13, so monster paths
  narrow. Faithful; if a monster looks stuck, that is the original's behaviour.
* **Renderer (stacked characters)** — `SceneRenderer` classifies by def family and by
  `info & 0x1010000` (`SceneRenderer.cpp:127-152`); the new families match neither, so
  billboards keep the plain path.
* **Hidden sprites now spawn** — they consume entity/monster slots exactly as in the original
  (the map00 census is identical: 35 monsters), and the renderer skips hidden art.

Mitigation strategy: the group ships alone (no crate work in the same delegation), the spawn log
prints family + sprite + **linked** tile for every entity, and the acceptance list names concrete
map00 tiles for both the new blockers and the four nudge cases.

## Rejected alternatives

* **Extend the whitelist with the four families.** Cheaper to write, but it keeps a rule the
  original does not have, silently omits `ET_ENV_DAMAGE`/`ET_DECOR_NOCLIP`/def-less sprite walls
  (the barred windows the user walks through), and would have to be reopened for every future
  subsystem. Rejected: the whitelist is the bug, not the families in it.
* **Spawn everything but keep skipping hidden sprites.** Would break `EV_GIVEITEM` mode 0 /
  `EV_HIDE` / `EV_ENTITY_FRAME` on sprites a script reveals later, for no benefit.
* **Synthesize blockers without entities (a per-tile solid mask at load).** Faster traces, but it
  cannot express oriented ±32 segments, cannot be unlinked when a crate opens, and diverges from
  the one data structure every other subsystem already reads (`entityDb`).
