# Monster movement, pathing and "line of sight" (original RE port)

Scope: how a monster picks and executes its per-turn step, the
`checkLineOfSight` helper, the movement lerp/animation and the `entityDb`
relink. Turn scheduling (`Game::monsterAI`, `aiThink`, haste) and attack
execution are covered elsewhere; only cross-references are given here.
Collision masks: see `entities.md` §6 (`15535` = CONTENTS monster-solid).

All positions are world units (64 per tile, tile centre = `tile*64+32`);
tiles are 32x32, `linkIndex = y*32 + x` (`src/Game.cpp:97`).

## 1. Call chain (per monster, per monster turn)

`Game::monsterAI` → `Entity::aiThink` (`src/Entity.cpp:673-685`):
if the current goal is invalid (`aiIsValidGoal`, `:605-642`) →
`aiChooseNewGoal` → `aiCalcSimpleGoal`; then `goalTurns++` and
`aiMoveToGoal` (`:572-594`).

`aiMoveToGoal`: goalType 1/2/4/5 → `aiGoal_MOVE()`; goalType 3 → `attack()`
(`:577-588`).

Goal types set by `aiCalcSimpleGoal` (`:535-566`):

| goalType | meaning | set when |
|---|---|---|
| 2, param 1 | chase the player (entity 1) | `aiWeaponForTarget(player) == -1` (`:559-561`) |
| 3, param 1 | attack player | a weapon is usable now (`:552-554`) |
| 4, param 1 | flee (fear) | `player->buffs[11] > 0` and monster not in `FEAR_IMMUNE_MONSTERS 0x1F801` (`:541-545`) |
| 1 | move to fixed tile `goalX/goalY` | knockback (`:1257-1260`) |
| 5, param N | flee N path steps | after an evading monster's attack, `goalFlags & 0x8` (`src/GameStateRunner.cpp:53-58`) |
| 2, param k≠1 | move to entity k | not produced by `aiCalcSimpleGoal`; validated at `:612-619` |

Goal validity: `goalTurns >= 16` always invalid; type 1 valid until the
monster stands on `goalX + goalY*32`; types 4/5 valid while
`goalTurns < goalParam` (`src/Entity.cpp:609-639`). Chase-player (2/1) is
never "valid" (branch requires `goalParam != 1`, `:614`), so it is
recomputed every turn.

Monster sets (`src/Enums.h:82-84`; bit = `eSubType`):
`EVADING_MONSTERS 0x4604` = {2 lost soul, 9 revenant, 10 arch-vile,
14 mastermind}; `MOVE2ATTACK_MONSTERS 0x180D0` = {4 saw goblin,
6 cacodemon, 7 sentinel, 15 Vios, 16 Vios2}; `FEAR_IMMUNE_MONSTERS 0x1F801`
= {0, 11 sentry bot, 12..16 bosses}.

## 2. `aiGoal_MOVE` — choosing and taking ONE step (`src/Entity.cpp:1030-1116`)

1. `snapLerpSprites(sprite)` — any movement still in flight for this sprite
   is finished first (`:1035`; can fire `aiFinishLerp`, §4).
2. Search state (`:1038-1049`): `closestPath = 0`, `closestPathDepth = 0`,
   `closestPathDist = 999999999`, `curPath = 0`, `pathDepth = 0`,
   `pathSearchDepth = 8`, `findEnt = nullptr`, `skipEnt = this`,
   `lineOfSight = 2`, `lineOfSightWeight = 0`, `interactClipMask = 32`
   (bit 5 = ET_DOOR), `visitedTiles = baseVisitedTiles` (copy).
3. Per goal type (`:1050-1074`):
   * chase player (2/1): `findEnt = &entities[1]`, goal tile =
     `game->destX>>6, destY>>6` (player's destination tile),
     `lineOfSightWeight = -4`.
   * type 5: goal = player tile, `interactClipMask = 0`, flee (`b = true`),
     `lineOfSight = 1`, `pathSearchDepth = goalParam`.
   * type 4: goal = player tile, flee (`b = true`), `lineOfSight = 1`.
   * type 2/k: `findEnt = &entities[k]`, goal = `findEnt->linkIndex % 32, / 32`.
   * flee: `closestPathDist = 0` (maximise instead of minimise) (`:1075-1077`).
   * type 1: goalX/goalY kept as stored.
4. `calcPath(sX>>6, sY>>6, goalX, goalY, 15535, b)` (`:1078`). If it fails
   but `closestPathDist < 999999999`, the best partial path is used
   (`:1079-1083`). For flee `closestPathDist` starts at 0, so the partial
   path is always taken.
5. If a path with `pathDepth > 0` exists: take ONLY ITS FIRST STEP
   (`curPath >>= 64 - pathDepth*2`, low 2 bits = first direction,
   `:1084-1088`). `dX/dY = sX/sY + viewStepValues[dir]` (±64 in one axis).
   `goalX/goalY` is overwritten with that step tile (`:1089-1090`).
6. Final check `trace(sX, sY, dX, dY, this, interactClipMask, 25)` (`:1091`).
   The mask is `interactClipMask` (32 = doors only; 0 for goal 5 → never
   blocks). Walls/monsters were already checked inside `calcPath`.
   * clear (`numTraceEntities == 0`): `unlinkEntity(this)` +
     `linkEntity(this, dX>>6, dY>>6)` IMMEDIATELY (`:1093-1094`); visibility
     flag (§4); `interpolatingMonsters = true`; `aiInitLerp(500)` for
     `eSubType 13` (Pinky boss) or `eSubType 14 && def->parm != 0`
     (Mastermind variant), otherwise `aiInitLerp(275)` (`:1095-1104`).
   * a door is hit: goal reset to the current tile and
     `performDoorEvent(0, door, 2)` (open) (`:1105-1111`). The turn is spent
     opening the door; no move.
   * returns `true` (step taken or door opened); `false` when no path /
     `pathDepth == 0` (e.g. already adjacent to the player: `calcPath`
     returns success at depth 0) (`:1113-1115`).

So: **one tile per monster turn, 4-connected (no diagonals), depth-limited
greedy DFS, first step only.** No A*; the path is recomputed each turn.

### 2.1 `calcPath` (`src/Entity.cpp:908-1028`)

Recursive depth-first search with a GLOBAL visited bitset
(`visitedTiles[y] bit x`, set on entry `:916`, never cleared on backtrack),
64-bit `curPath` stack of 2-bit directions.

Node score (`:912-925`): `d = dx*dx + dy*dy` (tile units, squared Euclid to
goal); if `lineOfSight` mode matches (`0` & !LOS, or `1` & LOS) `d -= 30`
when fleeing else `d += 30`; if LOS `d += lineOfSightWeight`. With mode 2
(chase) only the weight applies. Every node with `pathDepth > 0` that
improves `closestPathDist` (smaller for chase, larger for flee) is recorded
as best partial path (`:927-931`). Reaching the goal tile → success
(`:932-937`). `pathDepth == pathSearchDepth` → stop (`:938-940`).

Neighbour expansion (`:941-975`):
* direction index `0..3` → `Canvas::viewStepValues[dir*4 + 0/1]`
  (`src/Canvas.h:108`): 0 = (+64,0) east, 1 = (0,-64), 2 = (-64,0),
  3 = (0,+64). Start = `(nextByte() & 3) + 1`, cycling — random tie order
  (`:946-948`).
* skipped: off-map (`0..31`), a tile containing an `ET_ENV_DAMAGE` entity
  (`findMapEntity(x<<6, y<<6, 256)`, `:955`), already-visited tile
  (includes all `baseVisitedTiles` bits, §3).
* each candidate scored like a node, then bubble-sorted ascending (chase)
  or descending (flee) (`:976-989`).

Edge test per sorted candidate (`:996-1026`):
`trace(centre(n) → centre(neighbour), skipEnt, 15535, radius 16)`.
* nearest hit == `findEnt` (the player, or the target entity) → success at
  the CURRENT depth (`:1005-1010`).
* otherwise `mask = interactClipMask` (32) or `1 << traceEntity->eType` if
  something was hit; recurse when `mask == 0 || (mask & interactClipMask)`
  (`:1011-1015`). Effect with the default 32: recurse if nothing hit OR the
  nearest hit is a DOOR (doors are path-transparent; the monster opens them
  in §2 step 6). World lines, monsters, NPCs, decor, sprite walls, the
  player (when not `findEnt`), `ET_MONSTERBLOCK_ITEM` block. With
  `interactClipMask = 0` (goal 5) only a clean trace recurses.
* `trace` treats a monster in mid-lerp (`goalFlags & 1`) as standing on its
  goal tile centre (`src/Game.cpp:227-231`), and the player at
  `canvas->destX/Y` (`:238-241`); the player entity is linked at its
  destination tile (`src/Player.cpp:1439-1445`).

## 3. Monster clip / static blocked tiles (`baseVisitedTiles`)

`int baseVisitedTiles[32]` (`src/Game.h:153`), one bit per tile, cleared on
map load (`src/Game.cpp:348-350`). Bits are set by:
* `linkWorldEntity(x,y)` — tiles flagged `mapFlags & 1` (solid world tiles)
  (`src/Game.cpp:179`), cleared by `unlinkWorldEntity` (`:128`).
* `setMonsterClip(x,y)` (`src/Game.cpp:3656-3658`) — script `EV_MARKTILE`
  with flag bit 0 (`src/ScriptThread.cpp:1152-1160`; arg: flags `>>10 & 0x3F`,
  x `>>5 & 0x1F`, y `& 0x1F`); `EV_UNMARKTILE` →
  `unsetMonsterClip` (`src/ScriptThread.cpp:1752-1761`, `src/Game.cpp:3660-3662`).
* `monsterClipExists` just reads the bit (`src/Game.cpp:3664-3666`).

Because `aiGoal_MOVE` seeds `visitedTiles` from it (`src/Entity.cpp:1049`),
these tiles are never entered by pathing. `aiWeaponForTarget` also uses
them to stop weapon 18 lines (`src/Entity.cpp:813-829`); knockback does not.

## 4. Lerp, timing, relink (`aiInitLerp` / `aiFinishLerp`)

`aiInitLerp(travelTime)` (`src/Entity.cpp:852-871`):
`allocLerpSprite(nullptr, sprite, true)` (async, ANIMATING_EFFECT);
src = current sprite x/y/z; dst = `32 + (goalX<<6)`, `32 + (goalY<<6)`,
`dstZ = 32 + getHeight(dstX, dstY)`; scale unchanged;
`startTime = gameTime`; flags `LS_FLAG_ENT_NORELINK | LS_FLAG_ASYNC`;
`monster->frameTime = app->time + travelTime`; `calcDist()`
(`dist = FixedSqrt(dx²+dy²<<8)>>8` → 64 for one tile, `src/LerpSprite.cpp:46-49`);
`goalFlags |= 1` ("moving").

Travel times: **275 ms** normal step, **500 ms** Pinky boss / Mastermind
variant (`src/Entity.cpp:1099-1104`), **400 ms** knockback
(`src/Entity.cpp:1262`).

Relink happens at the START of the move (`src/Entity.cpp:1093-1094`,
knockback `:1263-1264`). `LS_FLAG_ENT_NORELINK` disables the per-frame
(`src/Game.cpp:2946-2950`) and final (`src/Game.cpp:3115-3118`) relink.
During the lerp other traces see the monster at its goal tile (§2.1).

Interpolation (`Game::updateLerpSprite`, `src/Game.cpp:2855-2956`):
`t = elapsed*256/travelTime` (0..255, `:2876-2878`); x/y/z/scale =
`src + (t*(dst-src<<8) >> 16)` (`:2879-2882`); linear, no parabola for
monster steps. When `elapsed >= travelTime` → `freeLerpSprite` (`:2868-2871`)
which writes dst, resets `monster->frameTime = 0`, resets the anim nibble
(§5), and — because `goalFlags & 1` — calls `entity->aiFinishLerp()`
(`src/Game.cpp:3213-3216`); then an `ET_ENV_DAMAGE` entity on the arrival
tile hurts/affects the monster (`:3228-3240`).

`aiFinishLerp` (`src/Entity.cpp:873-882`): clears `goalFlags & 1`; if
`monster->flags & 0x1000` (knockback) just clears it and `info 0x10000000`;
else `aiReachedGoal_MOVE`.

`aiReachedGoal_MOVE` (`src/Entity.cpp:1118-1153`): clears
`info 0x10000000`. If goal is not 4/5 and the monster is in
`MOVE2ATTACK_MONSTERS` (or evading without `flags & 1`), it calls
`aiWeaponForTarget(player)` and, if a weapon is usable, `attack()` right
away (move + attack in the same turn). Evading monsters then set
`flags |= 1`. The `goalFlags & 0x10` re-path branch (`:1141-1151`) is dead:
nothing in `src/` sets bit 0x10 (grep of `goalFlags`).

Visibility / snapping: before the lerp, `!cullBoundingBox(min-16<<4,
..., max+16<<4, true)` (visible) sets `info |= 0x10000000`
(`src/Entity.cpp:1095-1097`). `Game::canSnapMonsters` returns false while
any active monster has that bit (`src/Game.cpp:2375-2389`); otherwise
`snapMonsters` snaps all monster lerps instantly (`src/Game.cpp:2413-2425`).
So only on-screen steps are animated/waited for; off-screen ones complete
immediately when input or turn flow calls `snapMonsters`
(`src/PlayingInputHandler.cpp:72`, `src/Canvas.cpp:812-814`).
`Game::monsterLerp` (`src/Game.cpp:915-939`) only recomputes
`interpolatingMonsters` = any active monster with `goalFlags & 1`, and
invalidates the screen when such a goal tile is visible; the actual motion
is `updateLerpSprites` (`src/GameStateRunner.cpp:182-184`,
`src/Canvas.cpp:921,953`).

## 5. Walk frames and facing

Sprites are billboards; "facing" is only the front/back anim choice in
`mapSpriteInfo` bits 8-15 (`anim = byte & 0xF0`, frame = `byte & 3`;
`MANIM_*` `src/Enums.h:567-580`: IDLE 0, IDLE_BACK 16, WALK_FRONT 32,
WALK_BACK 48, ATTACK1 64, ATTACK2 80, PAIN 96, DEAD 112).

In `updateLerpSprite` (`src/Game.cpp:2900-2936`) for monsters/NPCs:
* from IDLE / WALK_FRONT (or WALK_BACK with AUTO_FACE) and nonzero delta:
  WALK_BACK (+`LS_FLAG_AUTO_FACE`) iff move length in tiles
  `WorldDistToTileDist(max(dx²,dy²)) > 1` AND
  `|(viewAngle & 0x3FF) - VecToDir(dx,dy,true)| < 256` (moving away from the
  camera within 90°); else WALK_FRONT. For a normal 1-tile step
  `WorldDistToTileDist(4096) = 1` (`src/Combat.cpp:42,1235-1242`), so it is
  ALWAYS WALK_FRONT.
* IDLE_BACK → WALK_BACK.
* frame = `(1 + (t*dist >> 12)) & 3`: for dist 64 → 1,2,3,0 over the step
  (4 frames per tile). Cyberdemon/Mastermind play a footstep sound
  (1047 / 1003) whenever frame bit 1 changes (`:2913-2927`).
* Other anims (attack, pain...) are left untouched by the lerp.

`VecToDir(dx,dy,true)` returns `dir<<7` (1024-unit angle), thresholds
±32: dir 0=+x, 1=+x-y, 2=-y, 3=-x-y, 4=-x, 5=-x+y, 6=+y, 7=+x+y
(`src/Game.cpp:3596-3627`). No wrap-around is applied to the `< 256` test.

At the end (`freeLerpSprite`, `src/Game.cpp:3104-3111`): anim 16/48 without
AUTO_FACE → IDLE_BACK (`0x1000`), else IDLE (0). Re-allocating a running
lerp normalises WALK_FRONT/AUTO_FACE → IDLE and WALK_BACK → IDLE_BACK
(`src/Game.cpp:3054-3062`).

## 6. `Entity::checkLineOfSight` (`src/Entity.cpp:884-906`)

Signature `(x0, y0, x1, y1, mask)` in TILE coordinates. Only callers:
`calcPath` (`src/Entity.cpp:917,965`) with `mask = 15535 | 0x100`.

As written: returns false if both `dx != 0 && dy != 0`; normalises to unit
step; loop `while (n != n3 && n2 != n4)` (`:898`). Since one axis delta is
already zero at that point, the `&&` condition is false before the first
iteration — the `findMapEntity` test (`:901`) is **unreachable**. Effective
behaviour: **`checkLineOfSight` == "same row or same column"** (including
the same tile); nothing blocks it. It is only a scoring hint for `calcPath`
(chase: -4 for aligned tiles, which favours tiles from which an
axis-aligned attack is possible).

Real "can I hit" checks are traces, not this function:
`aiWeaponForTarget` (`src/Entity.cpp:687-850`: axis-aligned requirement
`:748`, `trace(... 5295, 2)` must hit the target first `:765-768`, second
trace from 18 units ahead with 15791 `:779`; arch-vile/Vios diagonal case
with `4131` `:751-757`), and `aiIsAttackValid` (`src/Entity.cpp:644-671`,
trace 5295 + range + axis-aligned). Details belong to the attack docs.

## 7. Knockback movement (for completeness)

`Entity::knockback` (`src/Entity.cpp:1211-1268`) for monsters: mask 15535,
`getFarthestKnockbackDist(..., 16, n3)`; sets goalType 1 to the landing tile,
`flags |= 0x1000`, `aiInitLerp(400)`, relinks immediately, and calls
`updateLerpSprite` once.
