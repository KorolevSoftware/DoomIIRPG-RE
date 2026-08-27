# 2026-08-26 — Monster health-bar visibility rules (`facingEntity` probe)

## Hypothesis / question

Our rewrite shows the monster health bar only for a monster on the adjacent
tile. In the original the bar appears for the monster the player is *looking at*
in direct line of sight, several tiles away. Find the exact rules: trace
geometry, range, blockers, recompute/invalidate points, bar geometry, and
whether the bar target equals the shot target.

## Method

grep/read of `src/MovementController.cpp` (`checkFacingEntity`),
`src/Game.cpp` (`Game::trace`), `src/Render.cpp` (`traceWorld`,
`CapsuleToCircleTrace`, `CapsuleToLineTrace`), `src/Hud.cpp`
(`drawMonsterHealth`, `drawTopBar`, repaint flags), `src/PlayingInputHandler.cpp`
(ACTION_FIRE trace), `src/Entity.cpp` (`distFrom`, `calcPosition`, `isBoss`,
`initspawn`, info bit 0x20000), `src/Combat.cpp` (`tileDistances`),
`src/Canvas.cpp` (`screenRect/viewRect/SCR_CX`),
`src/LoadingManager.cpp` (mapFlags nibbles). Cross-checks: independent uses of
`info & 0x20000` (`Entity::pain`, `Combat::hurtEntityAt`), independent use of
`tileDistances` (`Game::activate`), independent use of the same trace helper
(fire path, BFG/radius LOS).

## Verdict

**CONFIRMED that the original probe reaches 6 tiles** — our one-tile probe is
the defect. Details:

1. **Trace** (`src/MovementController.cpp:38`):
   `game->trace(dest + 28·fwd, dest + 384·fwd, nullptr, 21741, 2, isZoomedIn)`.
   `fwd = (-view[2], -view[6], -view[10])` in 14.14; `x*28>>14` = 28 units,
   `6*x>>8` = 384 units = **6 tiles** (64 u/tile). Origin is the *logical tile
   centre* `destX/destY/destZ`, not the interpolated eye. Radius 2, Z overlap
   tested only when zoomed in.
   Termination: nearest hit of the sorted hit list wins
   (`src/Game.cpp:313-326` bubble sort by frac, `traceEntity = traceEntities[0]`);
   world geometry enters the same list as `entities[0]` (`src/Game.cpp:295-311`).
2. **Types.** Mask 21741 `0x54ED` = {0 WORLD, 2 MONSTER, 3 NPC, 5 DOOR, 6 ITEM,
   7 DECOR, 10 ATTACK_INTERACTIVE, 12 SPRITEWALL, 14 DECOR_NOCLIP}. Excluded:
   PLAYER, PLAYERCLIP, ENV_DAMAGE, **CORPSE(9)**, MONSTERBLOCK_ITEM(11),
   NONOBSTRUCTING_SPRITEWALL(13). Sleeping monsters are included (activation only
   moves them between AI rings, `src/Game.cpp:756-855`, entityDb link untouched).
   Doors are entities of type 5 → a door hit becomes the target and (having no
   `monster`) suppresses the bar; a wall likewise (WORLD). Wall lines with
   `lineFlags&7 ∈ {4,6}` never block, `5` does not block for this mask
   (needs 0x10/0x800), `7` is one-sided (`src/Render.cpp:1236-1246`).
   Item/decor/spritewall/attack-interactive nearest hits trigger the promotion
   re-scan (`src/MovementController.cpp:41-86`) which can pull a monster further
   down the ray in front, unless a DOOR/WORLD/PLAYERCLIP or an opaque spritewall
   (`mapFlags[linkIndex] & 0x2`) is met first.
3. **Distance gate** (`:88-93`): non-monsters are dropped beyond
   `tileDistances[2] = 192² = 36864` (Chebyshev², `src/Entity.cpp:1155`),
   monsters are **never** distance-gated → up to the full 6-tile ray.
4. **Recompute**: latch `canvas->updateFacingEntity`, but `Hud::draw` forces
   `updateFacingEntity = true; canvas->checkFacingEntity();` before every
   `drawTopBar` while `ST_PLAYING` (`src/Hud.cpp:735-742`) and the HUD repaint
   bits are never cleared in this port (`src/Hud.cpp:728-748` commented-out
   clears, `src/Canvas.cpp:414-417`) → effectively **every rendered frame**.
   Hard clears: `src/Game.cpp:192` (removeEntity), `src/Game.cpp:651`
   (unloadMapData, + `hud->lastTarget` at `:654`), `src/Player.cpp:497`,
   `src/PlayingInputHandler.cpp:589-593` (turn/strafe that actually moved).
5. **Bar geometry** (`src/Hud.cpp:861-899`), 480-wide canvas:
   `n = 25` segments, `n4 = 2*(480<<8)/128>>8 = 7` → `8` after the
   boss/odd adjustment (both branches give 8 at 480 px), frame
   `w = 2 + 8*25 = 202`, `x = 240 - 101 = 139`, `y = viewRect[1](20) + n3`,
   `h = n4*2+1 = 17`; segments `7x14` at `y+2`, pitch 8, from `x+2`.
   `n3 = 6` default, `50` for `eSubType == 5` (PINKY) with `parm == 0`,
   `6+20 = 26` when `isZoomedIn`. Colors `0xFF000000` fill / `0xFFAAAAAA` frame /
   red `0xFFFF0000` when segs ≤ 6, green `0xFF00FF00` when segs ≥ 19, else
   orange `0xFFFF8800`. Segment count `ceil(25*shown/max)`, forced to ≥1 when
   `shown > 0`. Drain: 250 ms linear from the previous displayed value
   (`:854-859`).
6. **Bar target vs shot target: REFUTED as "one shared trace".** ACTION_FIRE
   traces separately from `viewX/viewY/viewZ` with mask 13997 (+0x4100 for
   weapon 2, +0x10 melee) and its own priority walk
   (`src/PlayingInputHandler.cpp:218-368`); same 6-tile length (`n7 = 6`,
   1 for melee) and radius 2. `facingEntity` is only read there for
   `lootingSystem.lootSource` (`:189-194`).

## Consequence for the rewrite

`docs/architecture/specs/2026-08-26-combat-stage1.md:157-160` specifies the probe
as "one tile toward `viewStep`" — that is the source of the observed defect. It
must become the 28→384 unit ray (6 tiles) from `dest` along the view forward
vector, with the monster/non-monster distance split of `:88-93`.

## Open questions

* `mapFlags & 0x2` semantics beyond "opaque spritewall": the low nibble is read
  verbatim from the packed per-tile table (`src/LoadingManager.cpp:558-563`); no
  other reader of bit 1 exists in `src/`.
* `eType == 11` in the promotion pre-condition (`src/MovementController.cpp:42`)
  is unreachable with mask 21741 — likely leftover from a J2ME revision.
