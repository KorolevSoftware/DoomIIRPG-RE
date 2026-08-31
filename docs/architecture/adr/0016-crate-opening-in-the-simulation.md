# ADR 0016 — Crate opening is simulation state, not renderer state

Date: 2026-08-30
Status: Accepted
Spec: `specs/2026-08-30-blocking-crates-shelf-pickup.md` (G3)
Evidence: `docs/original-code/loot-inventory.md` §7.3-§7.4, `docs/research/2026-08-30-containers.md`

## Context

A container (`ET_ATTACK_INTERACTIVE` / `INTERACT_CRATE`, tile 152) has no `use()`, no `touched()`
and no loot set. Opening it is two statements inlined in the legacy fire handler
(`src/PlayingInputHandler.cpp:387-393`): `entity->param = upTimeMs + 200` and an immediate
`unlinkEntity`. The 4-frame, 200 ms/frame animation then runs **inside the renderer**
(`src/Render.cpp:1607-1617`), which mutates `entity->param` and `mapSpriteInfo` while drawing.

The rewrite keeps rendering side-effect free: `World3D`/`SceneRenderer` read `mapSpriteInfo` and
never write it; the equivalent case (monster pain/dodge pose revert, also renderer-side in the
original) was already moved into `Game::update` as deviation D-6 (`Game.cpp:300-319`).

There is also a clock question: legacy arms and advances `param` on `app->upTimeMs`. The rewrite
has `GameContext::upTimeMs` (wall clock, runs in every state) and the `SpriteLerps` clock
(simulation clock, advanced by `Game::update`, already the clock behind `monster->frameTime`).

## Decision

1. Opening lives in the domain: a new `Game::openCrate(Entity*)` performs the arm + unlink, and
   `Game::update` advances the frame for every armed crate. The renderer is untouched — it
   already resolves `mediaId = mappings[152] + frame` from `mapSpriteInfo` bits 8-15
   (`World3D.cpp:713,744`), and the crate's media range 753-756 supplies the four frames.
2. One clock: `SpriteLerps::clockMs()`. `param = clockMs() + 200`, advance while
   `clockMs() > param`. Mixing wall time with the simulation clock is what the D-6 move already
   rejected.
3. The unlink happens **before** the animation, exactly as in the original: the crate stops
   blocking, stops being traceable and stops being a facing target the instant the player acts —
   the visible opening is cosmetic catch-up.
4. The action chain hoists the fire-target election above the front-tile script so the crate
   check sits where the original has it (elect → crate → script → door → fire), and the elected
   hit plus its distance are computed once and reused by the fire block.

## Consequences

* Deterministic and testable from the simulation side; a paused/menu frame cannot advance a
  crate, and a crate not currently on screen still finishes opening (legacy advanced the frame
  only while drawing — an accepted, invisible difference; both latch at frame 3 after ~600 ms).
* `Entity::param` keeps its legacy multi-purpose meaning (loot counter for corpses, timer for
  crates, chat marker for NPCs); no new field.
* The hoisted election costs nothing: it replaces the trace the fire block did later. Distances
  are captured eagerly because a `World` hit's `distFrom` reads trace scratch
  (`TraceSystem.cpp:233-239`).
* "Already searched" is reproduced by the two original marks (the tile event disables itself; the
  entity stays unlinked at frame 3) with no extra bookkeeping.

## Rejected alternatives

* **Port the animation into `World3D` verbatim.** Faithful to the letter, but it re-introduces a
  writing renderer and would need the entity array inside the render layer — the exact coupling
  ADR 0010/0012 removed.
* **Reuse `DoorSystem`'s animation pool.** Doors lerp `S_SCALEFACTOR`; crates step art frames and
  never touch the scale factor. Different mechanism, no shared state.
* **Model the crate as a loot container (`ST_LOOTING`).** Contradicts the data: crates have no
  loot set, `poolLoot` walks only `eType == 9`, and the contents come from the tile script's
  `EV_GIVELOOT` dialog.
