# 2026-08-25 — Character rendering & animation (sprite-stack, MANIM/MFRAME)

## Hypothesis
Characters (monsters/NPCs) are composed of stacked billboard parts (legs/torso/head)
with frame selection driven by packed `(anim<<4)|frame` bytes; walk cycles sync to
movement distance; squad/intro humans use the same path.

## Method
Grepped `MANIM_`/`MFRAME_`/`renderSpriteAnim`/`mapSpriteInfo` writers across `src/`;
read `Render::renderSpriteAnim`, `Render::renderSprite`, `Game::updateLerpSprite`,
`Game::allocLerpSprite/freeLerpSprite`, `Combat::monsterSeq`, `Entity::pain/died`,
`ScriptThread::corpsifyMonster`/`EV_ENTITY_FRAME`, `IntroSequenceManager`,
`setupTexture`, `MovementController::checkFacingEntity`.

## Verdict: CONFIRMED (composition + encoding + walk math), PARTIAL for intro-NPC art IDs

Key verified facts (full detail in `docs/original-code/character-animation.md`):
- 4-part stack legs→torso→head from ONE tileNum, frames 0-3 front / 4-7 back (`src/Render.cpp:3202-3253`, `src/Enums.h:582-590`).
- Frame byte in `mapSpriteInfo` bits 8-15: anim nibble + frame nibble (`src/Enums.h:567-581`).
- Walk phase = `(1 + ((progress*dist)>>12)) & 3`, progress ∈ [0..256] over lerp → 1 cycle per tile (`src/Game.cpp:2877,2943`; dist = Euclidean px, `src/LerpSprite.cpp:48`). Leg mirroring gives 4 phases from 2 leg frames (`src/Render.cpp:3261-3265`).
- Idle bob = `((time + n*1337)/1024 & 1) * 26` on torso/head z (`src/Render.cpp:3195-3199`). The `(i + time/100) % count` formula is props-only AUTO_ANIMATE (`src/Render.cpp:1544-1546`).
- Attack frames written by Combat (64/80 chosen by weapon field, `src/Combat.cpp:92-100,529`); PAIN=0x6000 with 250ms revert (`src/Entity.cpp:350-368`; `src/Render.cpp:1600-1604`).
- Death = instant frame-13 corpse swap `0x7000` (`src/Entity.cpp:463`); EV_MAKE_CORPSE → corpsifyMonster also swaps def to ET_CORPSE and floors the sprite (`src/ScriptThread.cpp:2256-2262`).
- Billboards always face camera; direction = front/back anim set by view-angle vs move dir (<90° → back) + H-flip 0x20000 (`src/Game.cpp:2908-2921`; `src/Render.cpp:576-579`).
- Shadow tile 232 at floor height, scale shrinks with altitude over 256 units (`src/Render.cpp:3177-3180`); loot glow = enlarged PULSATE(512) duplicate (`src/Render.cpp:3471-3473`, `src/Render.h:40`).
- NPCs (tileNums 65-80) share monster animation paths incl. full walk (`src/Game.cpp:2903`); intro movie is table-camera script posing sprites via EV_ENTITY_FRAME (`src/IntroSequenceManager.cpp:768`; `src/ScriptThread.cpp:669-688`).

## Open questions
- Exact entity-def tileNums of the three "intro squad" NPCs (7/9/10) — data-driven,
  requires parsing map/entitydef binary data, not visible in code.
- Meaning of `entity->info & 0x20000000` (suppresses idle bob, `src/Render.cpp:3196`) — likely "no-bob" decor/NPC flag.
