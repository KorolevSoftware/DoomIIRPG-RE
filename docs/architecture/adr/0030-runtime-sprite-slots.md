# ADR 0030 — Runtime sprites (missiles, impact and claw effects) use 48 custom slots appended to the map sprite arrays, owned by a `GameSprites` peer

Date: 2026-09-22
Status: accepted
Spec: `specs/2026-09-22-monster-attacks.md` group G7
Supersedes the G5.1 sketch in `specs/2026-08-31-weapon-branch.md` §6

## Context

Missiles and hit effects are sprites created at runtime: `Game::gsprite_alloc` takes one of
48 pre-reserved sprite slots (`Render::customSprites`, allocated right after the map's normal
and Z sprites, `src/Render.cpp:2497-2508`; `MAX_CUSTOM_SPRITES = 48`, `src/Render.h:53`) and
drives position/frames/lifetime in `gsprite_update` (`src/Game.cpp:1294-1570`). The renderer
has **no depth buffer** (ADR 0024 spec §0.3): correct occlusion of a flying sprite requires it
to take part in the BSP painter's order, i.e. to be a map sprite.

The rewrite's `MapData` holds exactly the file's sprites (`numSprites = numNormal + numZ`,
`new_src/domain/world/MapParser.cpp:35,95`); the renderer recomputes BSP membership for every
sprite every frame, so a slot only needs to exist in the arrays.

## Decision

1. `MapData` gains `numMapSprites` (the file's count) and **48 custom slots appended**
   (`numSprites = numMapSprites + 48`), laid out and initialised like the original: hidden
   (`0x10000`), `S_Z = 32`, scale 64, render mode 0, no entity.
2. Every loop that means "the file's sprites" (spawn, AUTO_ANIMATE injection, render-mode
   post-process in `Game::loadEntities`) iterates `numMapSprites`; everything else keeps
   `numSprites`.
3. A new peer `domain/game/GameSprites` owns the 48 `GameSprite` records (legacy field set:
   `flags`, `pos[6]`, `vel[3]`, `duration`, `time`, `numAnimFrames`, scale fields) and ports
   `gsprite_alloc`, `gsprite_allocAnim` (subset), `gsprite_destroy`, `gsprite_clear`,
   `gsprite_update` for the flag bits the ported missiles and effects use. Positions are kept in
   legacy baked Z space and written to storage through `SpriteLerps::spriteZBias`, exactly like
   the sprite lerps.
4. `GameSprites::update(nowMs)` runs once per tick for every state (the original calls
   `gsprite_update` from the global canvas loop).

## Consequences

* The flying fireball is sorted and occluded by the same BSP pass as every other sprite.
* Slot indices ≥ `numNormalSprites` get the Z-sprite storage bias (−32); `spriteZBias` already
  handles that, so the baked/stored convention stays single-sourced.
* The 16 "drop sprites" of the original (`src/Render.cpp:2509-2520`) are not added: dropped
  items are not ported.

## Rejected alternatives

* **A separate effect draw list after the world pass** — without depth testing it would draw
  missiles through walls or never occlude them correctly.
* **Allocate slots on demand by growing the arrays** — changes the array stride
  (`mapSprites[sprite + k*numSprites]`) at runtime and invalidates every cached index.
