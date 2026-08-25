# Character animation & rendering (monsters / NPCs) — original RE port

How walking humans (monsters, NPCs, squad characters) are composed from sprite parts,
animated, and rendered. All claims cite `src/<file>:<line>`.

## 1. Character composition: a stacked 4-part billboard

A humanoid is NOT a single sprite. `Render::renderSpriteAnim` draws up to 3 stacked
camera-facing billboards per character plus a ground shadow:

- **Legs** at entity z (`src/Render.cpp:3204`)
- **Torso** above legs (`src/Render.cpp:3231`)
- **Head** above torso (`src/Render.cpp:3252`)
- **Shadow** (tile `TILENUM_SHADOW = 232`) drawn flat on the floor first (`src/Render.cpp:3202`, enum at `src/Enums.h:830`)

All parts use the SAME `tileNum`; only the frame index differs. Frame layout per
tileNum (`src/Enums.h:582-602`):

| Frame | Content |
|---|---|
| 0 | front legs A (`MFRAME_FRONT_LEGS1`) |
| 1 | front legs B (`MFRAME_FRONT_LEGS2`) |
| 2 | front torso (`MFRAME_FRONT_TORSO`) |
| 3 | front head (`MFRAME_FRONT_HEAD`) |
| 4–7 | back variants (`MFRAME_BACK_*`) — selected via base offset `n12=4` for IDLE_BACK/WALK_BACK (`src/Render.cpp:3191-3193, 3257-3259`) |
| 8/10 | idle→attack1/2 transition pose (used as attack torso base, `src/Render.cpp:3303-3308`) |
| 9/11 | attack poses (`MFRAME_ATTACK1/2`, reached by `++n26` on frame 1, `src/Render.cpp:3355-3357`) |
| 12 | pain/slap torso (`MFRAME_PAIN`, drawn for MANIM_PAIN and MANIM_SLAP, `src/Render.cpp:3464-3468`) |
| 13 | dead/corpse (`MFRAME_DEAD`, `src/Render.cpp:3470-3475`) |
| 14–17 | slap torso/head, dodge (`src/Enums.h:598-601`) |

Back-facing selection is just the base index shift; there are no separate back
tileNums. Pinky has no head part in idle-back/walk-back (`src/Render.cpp:3239, 3283`).
Mancubus/Revenant-attack2 skip the head entirely because their torso art includes it
(`src/Render.cpp:3363-3374`, comment "el torso contiene la cabeza incluida").

Per-monster part offsets (lateral along camera-right `viewRightStepX/Y >> 6`,
vertical added to z): Revenant `-30`/`+140` idle, Arch-Vile `-36`/`+109`,
Cyberdemon `-30`/`-15`, Riley O'Connor/Civilian2/Scientist `-18` idle with Riley head
`-32` (`src/Render.cpp:3206-3228, 3240-3250`). Sentry bots flip their head sprite on a
`(time+n*1337)/2048 & 1` cadence (`src/Render.cpp:3234-3236`).

## 2. Animation states & encoding

Frame byte packed in `mapSpriteInfo` bits 8–15: high nibble = anim, low nibble =
frame (`MANIM_SHIFT=4`, `MANIM_MASK=240`, `MFRAME_MASK=15`, `src/Enums.h:567-581`).

Anims (`src/Enums.h:569-580`): IDLE=0, IDLE_BACK=16, WALK_FRONT=32, WALK_BACK=48,
ATTACK1=64, ATTACK2=80, PAIN=96, DEAD=112, SLAP=128, DODGE=144, NPC_TALK=160,
NPC_BACK_ACTION=176.

State writers (gameplay side):
- **Walk**: during movement lerp, `mapSpriteInfo |= (((1 + (progress * dist >> 12)) & 3) | anim) << 8` (`src/Game.cpp:2943`)
- **Pain**: frame byte `| 0x6000` (MANIM_PAIN), `monster->frameTime = time + 250` auto-revert (`src/Entity.cpp:350-368`); render reverts to frame 0 when `time > frameTime` (`src/Render.cpp:1600-1604`)
- **Attack**: `Combat::attackFrame = 64` or `80` chosen vs monster weapon field (`src/Combat.cpp:92-100`); written at seq start (`src/Combat.cpp:529`), frame 0→1 after SHOTHOLD delay then projectile launches (`src/Combat.cpp:434-440`); reset to 0 at stage end (`src/Combat.cpp:576`)
- **Death**: frame byte `0x7000` (MANIM_DEAD, frame 0) set in `Entity::died` (`src/Entity.cpp:463`)
- **Idle restore**: after lerp ends, WALK_BACK/IDLE_BACK without AUTO_FACE → `0x1000`, else `0x0000` (`src/Game.cpp:3105-3111`); alloc-side resets WALK_FRONT/AUTO_FACE → 0, WALK_BACK → 16 (`src/Game.cpp:3055-3062`)
- **Scripted pose**: opcode `EV_ENTITY_FRAME` writes any packed byte and freezes the monster (`frameTime = 0x7FFFFFFF`) (`src/ScriptThread.cpp:669-688`)

**Idle bob**: torso+head raised by `26` units when `((app->time + n*1337)/1024) & 1`
(per-instance phase via sprite index n*1337; suppressed if `entity->info &
0x20000000`) (`src/Render.cpp:3195-3199`). Floaters bob ±16 at `/512` instead
(`src/Render.cpp:3546-3550`). Fear-eye overlay reuses the same formula
(`/1024`, or `/512` for Cacodemon/Sentinel/Lost Soul) (`src/Render.cpp:3045-3057`).

The `(index + time/100) % count` formula exists ONLY for props with
`SPRITE_FLAG_AUTO_ANIMATE` (fires etc.), not characters:
`n7 = (n + app->time / 100) % n7` (`src/Render.cpp:1544-1546`, flag `src/Enums.h:1234`).

## 3. Walk cycle mechanics

Only 2 leg art frames exist per facing (frames 0/1); 4 visual phases come from mirroring:

- Leg sprite = frame `frame & 0x1`, H-flip flag applied when `((frame>>1) ^ 1)` → phases 0,1 flipped, 2,3 not (`src/Render.cpp:3261-3265`)
- Torso/head get lateral sway: offset `±(frame&1)` along view-right, sign negative when bit1 clear (left on phase 1, right on phase 3) (`src/Render.cpp:3266-3271`)
- Torso/head vertical bob `+(frame&1)<<4` (up on odd phases) (`src/Render.cpp:3277, 3290`)
- NPC torsos (except Sarge) mirror together with legs (`src/Render.cpp:3277`)

Phase source: `Game::updateLerpSprite` computes progress
`n8 = (elapsed << 16)/(travelTime << 8)` ∈ [0..256] (`src/Game.cpp:2877`) and frame
`= (1 + ((n8 * dist) >> 12)) & 3` where `dist` = Euclidean move length in map units
(64/tile, `src/LerpSprite.cpp:48`) → exactly one full cycle per tile travelled.
Animation is therefore distance-synced to movement, not time-synced.

Front/back decision at lerp start: if current anim is walk/idle-front (or
AUTO_FACE walk-back) and move is >1 tile with `|viewAngle - VecToDir(move)| < 256`
(90° of 1024) → WALK_BACK else WALK_FRONT; IDLE_BACK always → WALK_BACK
(`src/Game.cpp:2908-2921`). Boss Cyberdemon/Mastermind play footstep sounds when the
phase pair changes (`src/Game.cpp:2925-2941`).

## 4. Squad humans / intro NPCs

- NPCs are entities with `def->eType == ET_NPC (=3)` (`src/Enums.h:13`); art range
  `TILENUM_FIRST_NPC=65 .. TILENUM_LAST_NPC=80` (`src/Enums.h:708,719`); `isNPC()` gates
  identical stack rendering (`src/Render.cpp:2971-2973`). Named ones include
  RILEY_OCONNOR=66, MAJOR=68, BOB=69, SARGE=72, SCIENTIST=75 (`src/Enums.h:709-718`).
- ET_NPC construction sets `param = 1` → chat icon overhead (`src/Entity.cpp:99-101`),
  rendered as tile 254/255 at z+160 while `param != 0` (`src/Game.cpp:1660-1669`).
- They DO have full walk support: same `Game::updateLerpSprite` path animates
  `eType == ET_NPC || monster` (`src/Game.cpp:2903-2944`) using frames 0-7.
- The pre-game intro "movie" is a table-camera script over a map
  (`loadTableCamera(14,15)`, `src/IntroSequenceManager.cpp:768`); squad members there
  are ordinary map sprites posed by script events (`EV_ENTITY_FRAME`,
  `src/ScriptThread.cpp:669-688`). Which exact tileNums the three squad NPCs use is
  data-driven (entity defs), not hardcoded — open question.
- The character-select screen uses separate 2D BMP stacks (Major/Riley/Sarge
  `*_legs.bmp` + `*_torso.bmp`) with a 1px time-based bob
  (`y - (app->time / 1000 & 1)`, `src/IntroSequenceManager.cpp:122-127, 498-499`).

## 5. Death / corpse transition

There is NO death animation: dying sets the single corpse frame immediately.

- `Entity::died`: snap lerps, frame byte `0x7000`, `info |= 0x400000`, loot glow flags;
  hidden monsters instead get `0x17000` (`src/Entity.cpp:459-471`).
- Render draws frame 13 twice when lootable (`monster->flags & 0x800 == 0`,
  canvas state ≠ 18, `!hasEmptyLootSet()`): first pass renderMode 0, scale
  `(18*scaleFactor)>>4` (+12.5%), `renderFlags = 512` = `RENDER_FLAG_PULSATE`
  (`src/Render.cpp:3470-3475`, flag list `src/Render.h:40`).
- Scripted art swap `EV_MAKE_CORPSE` → `corpsifyMonster`: snaps lerps, clears
  effects/goal/attack, deactivates AI, frame byte `| 0x7000`, moves sprite to
  `getHeight(x,y)+32`, relinks, and swaps def to an ET_CORPSE def
  `find(9, eSubType, parm)` (`src/ScriptThread.cpp:1614-1625, 2249-2265`).

## 6. Direction handling

Billboards ALWAYS face the camera; there is no 8-dir art rotation.

- Un-oriented sprites build a quad from `viewCos/viewSin` (`src/Render.cpp:455-489`);
  N/S/E/W `SPRITE_FLAG_*` (bits 24-27, `src/Enums.h:1240-1243`) instead select wall-
  aligned orientation steps (`src/Render.cpp:525-561`) — used by decor, not characters.
- Facing is binary front/back via IDLE_BACK/WALK_BACK anim choice relative to the
  camera (`src/Game.cpp:2908-2921`), plus horizontal mirroring via
  `SPRITE_FLAG_FLIP_HORIZONTAL = 0x20000` which negates texture S (`src/Render.cpp:576-579`,
  `src/Enums.h:1232`). Muzzle-flash side, eye positions, and part offsets all honor it.

## 7. Shadow / glow layers

- Shadow: `TILENUM_SHADOW` (232) frame 0 at ground z `n13 = (getHeight(x,y)+32) << 4`;
  scale shrinks linearly with altitude: `n14 = scaleFactor * (256 - clamp(z-n13,0,256)) / 256`
  (`src/Render.cpp:3172-3180`). Monsters flagged `0x4000` keep full-size shadow at own z
  (`src/Render.cpp:3172-3175`). Drawn for idle/walk/attack/pain/NPC_BACK_ACTION but NOT dead
  (`src/Render.cpp:3202, 3263, 3310, 3466, 3478` vs `3470-3475`).
- Lootable-corpse pulsating halo: see §5.
- Fear eyes: white dots (tile 251) over monsters actively fleeing (goalType==4, anim
  idle/walk, not fear-immune); per-subtype L/R x-offsets and Z heights
  (`src/Render.cpp:3031-3141`), mirrored on H-flip (`src/Render.cpp:3130-3134`).
- Muzzle flash: extra frame `n26+1`, renderMode 4, scale/3, palette cycling
  `((totalMoves + animLoopCount) & 3) << 17`, per-monster offsets
  (`src/Render.cpp:3411-3460`); gated by `hasGunFlare()` = Mancubus/Revenant/SentryBot/
  Cyberdemon/Mastermind (`src/Render.cpp:3019-3021`).

## Port checklist — animating a walking human sprite-stack

1. Store per-sprite state as ONE byte: `(anim<<4)|frame` (mirror original semantics, `src/Enums.h:567-581`).
2. Art sheet layout per character: [legsA, legsB, torso, head, backLegsA, backLegsB, backTorso, backHead, attack…, pain=12, dead=13].
3. Idle: draw shadow → legs(f0/base) → torso(f2, z+26 if `(t+n*1337)/1024&1`) → head(f3, same bob); apply per-character offsets.
4. Walk: derive phase from DISTANCE travelled, `(1 + dist_travelled/16) & 3`; legs = phase&1 mirrored on phases 0-1; torso/head sway ∓(phase&1) laterally, +(phase&1)<<4 vertically.
5. Choose WALK_FRONT vs WALK_BACK by comparing move direction to view angle (<90° behind camera → back); base index +4 for back set.
6. On lerp end revert to IDLE (or IDLE_BACK if AUTO_FACE was used).
7. Attack: two frames (pose at frame 1) + optional muzzle flash; PAIN: frame 12 with ~250 ms auto-revert timer.
8. Death: instant swap to frame 13 (+ pulsate halo copy if lootable); EV_MAKE_CORPSE additionally relocates to floor height and swaps entity def to corpse type.
9. Always draw shadow first (scale by height above floor), billboard every part toward camera, honor 0x20000 H-flip everywhere.
