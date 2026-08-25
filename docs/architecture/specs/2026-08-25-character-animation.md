# Spec 2026-08-25 — Character animation (stacked-billboard humans, walk cycles, squad)

Status: READY FOR IMPLEMENTATION
Goal: walking humans (intro squad sprites 7/9/10, other map00 NPCs) render as the
original's stacked leg/torso/head billboards with distance-driven walk cycles,
idle bob, front/back facing and script-reachable pain/death states — building
the EntityMonster groundwork without implementing monsters.

Sources of truth (normative):
- `docs/original-code/character-animation.md` (all sections; PORT CHECKLIST 1–9)
- `docs/original-code/lerp-opcodes.md` §"map00 boot cinematic usage"
- Legacy code cited inline as `src/<file>:<line>`; rewrite code as `new_src/<file>:<line>`.
- Data facts below were verified against the actual `.ipa` (newMappings.bin,
  entities.bin, map00.bin) on 2026-08-25 — see §9.

---

## 0. Current foundation (do not re-do)

- Single-quad billboard/wall/flat sprite drawing incl. RLE decode, bounds UVs,
  door lerp, painter ordering: `new_src/render/World3D.cpp:522-852`.
- BSP traversal draws per-leaf sprites sorted by mvp depth; caller-supplied
  per-sprite sort-bias hook: `World3D.cpp:919-1015`, built in
  `new_src/core/GameContext.cpp:771-782`.
- SpriteLerp pool + tick/free with relink: `Game.h:47-83`, `Game.cpp:770-889`;
  driven from `GameContext.cpp:166-169`.
- Script lerps: `ScriptVM.cpp` LERPSPRITE (:605), LERPOFFSET (:651),
  LERPSCALE (:1082), LERPSPRITEPARABOLA(+SCALE) (:1120).
- `EV_ENTITY_FRAME(17)` writes packed bits 8-15 verbatim: `ScriptVM.cpp:561-581`.
- `corpsifyMonster` death byte + def swap: `Game.cpp:561-593`.
- `loadEntities` creates DOOR/MONSTER/CORPSE entities only: `Game.cpp:82-133`.

## 1. DETECTION RULE (decision; see ADR 0005)

A map sprite `i` renders through the **character stack path** iff an entity is
bound to it whose definition says so:

```
class[i] = 1  ⟺  ∃ entity e with e.getSprite()==i AND (
                     e.def->eType == ET_NPC                                   // live NPCs
                  or (e.def->eType == ET_CORPSE AND (info[i] & 0xFF) ∈ [65,80]) // corpsified NPC
                 )
class[i] = 0  otherwise (incl. ALL ET_MONSTER this cycle — see §8)
```

Justification (each point verified):
- Mirrors the original gates exactly: stacked path entered when
  `entity->monster != nullptr` (`src/Render.cpp:1624-1625`) or
  `def->eType == ET_NPC` (`src/Render.cpp:1660-1661`); NPC art range
  `TILENUM_FIRST_NPC=65 .. TILENUM_LAST_NPC=80` (`src/Enums.h:708,719`,
  `src/Render.cpp:2971-2973`). Data-driven — no hardcoded sprite-index list.
- Verified data: all 8 map00 sprites with tileNum 65–80 (indices 7, 9, 10, 16,
  19, 30, 91, 205, 206, 246 …) resolve to `entities.bin` defs 113–122 with
  eType=3. Sprites 7/9/10 = tiles 68/66/72 (major/riley/sarge),
  frame bytes all 0x00 today.
- Mutual exclusion with doors/props BY CONSTRUCTION: doors resolve their def
  via tileNum+257 → eType=5 (never class 1); AUTO_ANIMATE props have no
  NPC-type entity. The character branch is tested before the AUTO_ANIMATE
  frame override in `drawSprite`, so bits 8-15 can never be double-consumed.
- Corpsified NPCs keep their NPC art tileNum (low byte preserved,
  `Game.cpp:567-569`) while their def becomes ET_CORPSE — hence the second
  clause. Placed corpse props use generic corpse defs (verified: every
  ET_CORPSE def in entities.bin has tileIndex=0) → stay class 0.
- ET_MONSTER deliberately excluded until the EntityMonster unit ports the
  floater/special-boss routing (`isFloater`/`isSpecialBoss`,
  `src/Render.cpp:3023-3029`) — enabling it now risks wrong stacks on
  mancubus/revenant-class sheets. The Game-side walk writer (§4) keeps the
  `ET_NPC || ET_MONSTER` gate so monster groundwork is pre-wired but dormant.

Plumbing: `GameContext::render` extends its existing per-frame loop
(`GameContext.cpp:775-781`) to fill a `std::vector<uint8_t> spriteCharClass`
alongside `spriteSortBias`, passed to `drawBSP(..., const uint8_t* charClass)`
as a borrowed nullable pointer (same ownership pattern as `spriteSortBias`,
spec `2026-08-23-fix-doors-sprite-placement.md` B5). `World3D` stays
def-agnostic — no EntityDefs/Game dependency enters the renderer.

Prerequisite: `Game::loadEntities` must create entities for ET_NPC sprites
(remove the exclusion at `Game.cpp:110`, keep hidden-sprite skip at :103).
Set `param = 1` like legacy NPC construction (`src/Entity.cpp:99-101`); the
chat-icon overhead quad is NOT rendered this cycle (documented elision;
legacy draws tiles 254/255 at z+160, `src/Game.cpp:1660-1669`).
Known behavioral deltas of creating NPC entities — faithful, flag to user:
solidity (trace masks include the ET_NPC bit: CONTENTS_PLAYERSOLID 13501 /
MONSTERSOLID 15535, `Enums.h:31-32`; candidates gated by
`mask & (1<<eType)` at `Game.cpp:417`) and `EV_TILE_EMPTY` counting
(`ScriptVM.cpp:420-422`).

## 2. STACKED RENDER PATH (World3D)

### 2.1 Refactor

Extract the current billboard body of `World3D::drawSprite`
(`World3D.cpp:633-695`: pushback, corner build, UV flip override, emit) into:

```cpp
// Emits one camera-facing billboard quad (legacy Render::renderSprite
// billboard branch, src/Render.cpp:455-513 GL sub-path). All offsets in the
// units of the existing drawSprite preamble. Must be called between begin()/end().
void drawBillboardPart(const MapData& map, const MediaLoader& media,
                       int x, int y, int zRenderUnits,   // canvas x/y; z already <<4
                       int tileNum, int mediaId,         // texture resolved by caller
                       int flags,                        // info low bits (0x20000 etc.)
                       int scaleFactor);                 // byte<<10
```

The single-quad path keeps identical output by calling the helper (pure
refactor, no behavior change — checkpoint C1a).

Texture resolution stays as today: `mediaId = mappings[tileNum] + frame`,
lazy `ensureSpriteTexture`, flush on bind change (`World3D.cpp:559-574,
614-625`). Character parts share one tileNum, so each part binds its own
mediaId — 3–4 binds per character per frame, matching the legacy GL path
(which also bound per part).

### 2.2 Frame decoding

Per sprite (`i` = sprite index, needed for bob phase):

```
info      = mapSpriteInfo[i]
tileNum   = info & 0xFF                    (characters never carry 0x400000)
animByte  = (info >> 8) & 0xFF             // ONE packed byte, checklist item 1
anim      = animByte & 0xF0                // MANIM_* nibble
frame     = animByte & 0x0F                // MFRAME_* nibble
x,y       = mapSprites[i], mapSprites[i+n]          (canvas units)
zR        = (mapSprites[i+2n] << 4) + terrainH    (render units; existing
           preamble World3D.cpp:540-557 WITHOUT the numNormalSprites -32
           nudge for ground math — see groundZ below)
scaleF    = mapSprites[i+8n] << 10
flags     = info (only low bits used: 0x20000 H-flip)
```

Sheet layout per tileNum (frames within `mappings[tileNum] .. `):
`[0]=frontLegsA [1]=frontLegsB [2]=frontTorso [3]=frontHead
 [4..7]=back set (legsA,legsB,torso,head) [8]=attack1 windup [9]=attack1 pose
 [10]=attack2 windup [11]=attack2 pose [12]=pain [13]=dead [14..17]=slap/dodge`
(`src/Enums.h:582-602`, `docs/original-code/character-animation.md` §1;
empirically confirmed: tile 68 maps media 610-620, tile 72 → 641-650, with
legs/torso/head-sized bounds at the expected slots).

### 2.3 Shadow + ground

```
getHeight(x,y) = heightMap[((y & 0x7FF) >> 6) * 32 + ((x & 0x7FF) >> 6)] << 3   // canvas
groundZ      = (getHeight(x,y) + 32) << 4            // src/Render.cpp:3177
if isNPC(tileNum):  groundZ = zR;  alt = 0        // shadow glued to feet, src/Render.cpp:3172-3175
else:               alt   = clamp(zR - groundZ, 0, 256)
shadowScale  = scaleF * (256 - alt) / 256          // src/Render.cpp:3180
```

Shadow = billboard quad, tile `TILENUM_SHADOW = 232` (add constant to
`domain/game/Enums.h`, `src/Enums.h:830`), frame 0, at `(x, y, groundZ)`,
scale `shadowScale`, same `flags` — drawn FIRST in every non-dead branch
(`src/Render.cpp:3202,3263,3310,3466,3478`). Dead draws no shadow (:3470-3475).

### 2.4 Branch table (port of `Render::renderSpriteAnim`, src/Render.cpp:3144-3488)

Let `bob = (((timeMs_ + i*1337) / 1024) & 1) * 26` (idle bob, :3195-3199;
timeMs_ is World3D's upTimeMs clock — legacy `app->time`). The
`entity->info & 0x20000000` suppression is not ported (no carrier; research
doc open question). Lateral offsets use the camera-right step:
`viewRightStepX = sinTable[yaw & 0x3FF] >> 10`,
`viewRightStepY = -sinTable[(yaw-256) & 0x3FF] >> 10` with
`yaw = camera.viewYaw()` (`src/Render.cpp:2276-2278`); lateral application is
`x += off * viewRightStepX >> 6` (canvas units).

| anim | draw order (frame @ z, flags) | cite |
|---|---|---|
| IDLE(0) / IDLE_BACK(16) | `base = (anim==16)?4:0`; shadow; legs `base+0` @ zR; torso `base+2` @ zR+bob + rileyTorso; head `base+3` @ zR+bob + rileyHead | :3190-3255 |
| WALK_FRONT(32) / WALK_BACK(48) | see 2.5 | :3257-3293 |
| ATTACK1(64)/ATTACK2(80) | `pose = (anim==64)?8:10; if(frame==1) ++pose`; shadow; legs `f0` @ zR with `flags^0x20000`; torso `pose` @ zR; head `f3` @ zR | :3295-3359, :3409 |
| PAIN(96)/SLAP(128) | shadow; single torso-part quad `f12` @ zR | :3464-3468 |
| DEAD(112) | single quad `f13` @ zR, NO shadow; lootable pulsate halo deferred (§8) | :3470-3475 |
| NPC_TALK(160) | single quad at raw `frame` (no shadow) | :3482-3484 |
| NPC_BACK_ACTION(176) | shadow; single quad `f = 8 + frame` @ zR | :3477-3480 |
| anything else (DODGE…) | draw NOTHING (faithful empty fall-through) | switch :3190-3486 |

`rileyTorso = -18`, `rileyHead = -32` only when `tileNum==66 && anim==IDLE`
(Riley O'Connor idle offsets, :3210-3216, :3246-3248). All vertical part
offsets are RENDER units (z is `<<4` space); sway is CANVAS units (§2.4 note).

Attack muzzle flash omitted this cycle (hasGunFlare monsters only,
:3019-3021, :3411-3460); the pose frames themselves ARE rendered so scripted
`ENTITY_FRAME 0x4x/0x5x` poses look right. Zombie/imp attack special cases
(torso flip :3299-3301, head suppression :3376) are monster-only and skipped.

### 2.5 Walk branch math (checklist items 3–5)

```
base    = (anim == MANIM_WALK_BACK) ? 4 : 0                      // back set, :3257-3259
mirror  = ((((frame >> 1) ^ 1))) << 17                           // H-flip on phases 0-1, :3261
legFlags= flags ^ mirror
shadow                                                     // :3263
legs    f = base + (frame & 1),  flags = legFlags, @ zR      // :3265
sway    s = (frame & 1); if ((frame & 2) == 0) s = -s        // left/right alternation :3266-3269
x += s * viewRightStepX >> 6;  y += s * viewRightStepY >> 6   // :3270-3271 (canvas units)
vBobZ = zR + ((frame & 1) << 4)                               // +16 render units on odd phases
torso   f = base + 2, @ vBobZ,
        flags = (isNPC(tileNum) && tileNum != 72 /*SARGE*/) ? legFlags : flags   // :3277
head    f = base + 3, @ vBobZ, flags                          // :3283-3290
```

Two leg art frames × mirror flag = 4 visual phases from the 2-bit `frame`
(checklist item 4). Painter order within the character is emission order —
the character occupies ONE depth slot in the leaf sort, so stacking vs other
sprites is unchanged.

## 3. STATE ENCODING (unchanged contract)

Bits 8-15 of `mapSpriteInfo` remain the ONLY anim state (checklist item 1):
`(anim<<4)|frame`, writers: MapParser (initial bytes, `MapParser.cpp:111-132`),
scripts (`ScriptVM.cpp:561-581`), walk writer (§4), corpsify (`Game.cpp:569`),
idle restores (§4). No new runtime struct for characters this cycle.

## 4. WALK STATE MACHINE (Game side; port of src/Game.cpp:2903-2944 + :3049-3063 + :3101-3111)

New members/state:

```cpp
// Game.h SpriteLerp additions:
static constexpr int kFlagAutoFace = 0x800;   // LS_FLAG_AUTO_FACE (lerp-opcodes.md §flags)
int dist = 0;                                 // Euclidean move length, canvas units
void calcDist();                              // dist = isqrt((dx²+dy²)<<8) >> 8   (src/LerpSprite.cpp:48)
void setLerpViewAngle(int a);                 // fed by GameContext each tick (see below)
static int vecToDir(int dx, int dy);          // 8-dir * 128, thresholds ±32 (src/Game.cpp:3596-3628, b=true)
```

Pseudocode — appended to `updateLerpSprite` AFTER position write, BEFORE the
relink block (`Game.cpp:811-867`):

```
ent = findEntityBySprite(sprite)
if ent != nullptr && !(info & SPRITE_FLAG_HIDDEN)
   && ent->def->eType ∈ {ET_NPC, ET_MONSTER}:            # monster half dormant (ADR 0005)
    anim = (info >> 8) & 0xF0
    dx = ls.dstX - ls.srcX;  dy = ls.dstY - ls.srcY
    if (anim==IDLE || anim==WALK_FRONT || (anim==WALK_BACK && (ls.flags & kFlagAutoFace)))
       && (dx | dy) != 0:
        delta = abs((lerpViewAngle_ & 0x3FF) - vecToDir(dx, dy))   # NO wrap fix (verbatim)
        if max(dx*dx, dy*dy) >= 16384 && delta < 256:              # ≥2 tiles Chebyshev & moving away
            anim = WALK_BACK;  ls.flags |= kFlagAutoFace           # :2911-2913
        else:
            anim = WALK_FRONT                                      # :2916
    elif anim == IDLE_BACK:
        anim = WALK_BACK                                           # :2919-2921
    if anim==WALK_FRONT || anim==WALK_BACK:
        phase = (1 + ((p * ls.dist) >> 12)) & 3                    # p∈[0..256]; 1 cycle / tile
        info = (info & 0xFFFF00FF) | ((phase | anim) << 8)         # :2943
```

Constants cited: `tileDistances[j] = (64*(j+1))²` → `[1]=16384`
(`src/Combat.cpp:42`, indexed by `WorldDistToTileDist` :1235-1242; the `>1`
test at `src/Game.cpp:2911` ⇔ `max(dx²,dy²) ≥ 16384`); `p` is the existing
progress variable `Game.cpp:824`; footstep-sound tail (:2925-2941) omitted.

Completion restore — added to `freeLerpSprite` before `hSprite = 0`
(`Game.cpp:872-889`):

```
if ent && !hidden && eType ∈ {ET_NPC, ET_MONSTER}:
    n5 = (info >> 8) & 0xF0
    restore = (n5==IDLE_BACK || n5==WALK_BACK) && !(ls.flags & kFlagAutoFace) ? 0x1000 : 0x0000
    info = (info & 0xFFFF00FF) | restore                   # :3105-3111
```

Alloc reuse reset — added to the reuse branch of `allocLerpSprite`
(`Game.cpp:770-795`), reading the OLD slot flags before overwrite:

```
if ent(NPC||MONSTER):
    n3 = (info >> 8) & 0xF0
    n3 = (n3==WALK_FRONT || (oldFlags & kFlagAutoFace)) ? IDLE
       : (n3==WALK_BACK ? IDLE_BACK : n3)                  # :3055-3062
    info = (info & 0xFFFF00FF) | (n3 << 8)
```

View-angle feed: `GameContext` stores what the last render used and calls
`game.setLerpViewAngle(...)` immediately before `sys_.game->update(kTickMs)`
(`GameContext.cpp:166-169`): `maya_.pose().yaw` while a cinematic key is
active, else `sys_.player->viewAngle` (same two sources render() uses,
`GameContext.cpp:724-769`). This reproduces legacy reading
`app->render->viewAngle` (the previous frame's view) inside
`updateLerpSprite`.

calcDist call sites: after src/dst assignment in ScriptVM LERPSPRITE
(:618-635), LERPOFFSET (:659-677), LERPSCALE (:1088-1103), and
LERPSPRITEPARABOLA/_SCALE (:1134-1156) — mirrors the four
`lerpSprite->calcDist()` sites of `src/ScriptThread.cpp:378,1417,1683,1994`.
Legacy also computes dist for entity/combat lerps (`src/Entity.cpp:868`,
`src/Combat.cpp:547`) — not applicable yet.

isqrt: integer Newton or shift-15 method over int64; exactness of the legacy
FixedSqrt approximation is irrelevant here (phase quantizes at >>12).

## 5. GROUPS & CHECKPOINTS

### GROUP 1 — Detection + stacked renderer (visible statics)
Files: `Game.{h,cpp}` (loadEntities NPC creation + param), `GameContext.{h,cpp}`
(charClass array + drawBSP signature threading), `World3D.{h,cpp}` (helper
refactor + `drawCharacter` + DEAD/PAIN/TALK/NPC_BACK branches + shadow),
`domain/game/Enums.h` (TILENUM_SHADOW=232, TILENUM_FIRST/LAST_NPC=65/80).
Reconfigure once (GLOB).
- C1a: pure billboard-helper refactor compiles; game visually IDENTICAL
  (user spot-check: doors, props, water, squad still single quads).
- C1b: with class array live, squad 7/9/10 + scientist 75 + civilians render
  as complete stacked figures standing on shadows; subtle two-phase idle bob,
  phase-staggered per sprite; doors/props untouched (mutual exclusion proof).

### GROUP 2 — Distance-driven walk cycle
Files: `Game.{h,cpp}` (dist/kFlagAutoFace/calcDist/setLerpViewAngle/vecToDir/
writer/restores), `ScriptVM.cpp` (4 calcDist calls), `GameContext.cpp`
(view-angle feed).
- C2: build green; scripted NPC walks (squad walk-in, npc_bob) show a leg
  cycle synced to travel (~1 cycle/tile), reverting to idle on arrival; no
  residual walk frames after arrival; pool reuse resets stale poses.

### GROUP 3 — Direction (front/back) + squad acceptance pass
Files: none new — completes §4 chooser + verifies §2.5 back-set rendering.
- C3: build green; full intro replay clean; manual checklist below passes.

Each group ends with: compile clean, smoke boot reaches ST_PLAYING through the
cinematic, doors/collision/loot regressions checked, user confirms visuals.

## 6. ACCEPTANCE CRITERIA

1. Squad walk-in (camera 5) shows three marines WALKING — alternating leg
   art, torso sway/bob locked to distance — instead of static slides.
2. Characters moving away from the camera display the back art set
   (frames base+4…); toward → front set. Both directions occur naturally
   during the boot cinematics.
3. After arrival, marines stand in IDLE with the 26-unit two-phase bob,
   visibly out of phase between individuals (n*1337 term).
4. The imp-demo victim collapses into corpse art (single dead-frame quad,
   no shadow) and remains lootable afterwards (existing loot path intact).
5. Idle NPCs elsewhere on map00 (scientist, civilians, npc_bob) render as
   complete figures (was: legs-only quad from frame-0 fallback clamping).
6. Doors, pickups, decor, water, wall decals: pixel-behavior unchanged.
7. No flicker/z-fighting among a character's own parts (fixed emission order).

## 7. MANUAL CHECKLIST (user = eyes)

- [ ] Boot → camera 5: marines walk IN with a visible leg cycle; gait speed
      follows walk duration differences (1500/1700/2000 ms per lerp doc §2328).
- [ ] During walk-in, at least one marine clearly shows the BACK art set
      (no face visible) while approaching from behind the camera.
- [ ] Marines stop into idle: gentle breathing bob, staggered between the
      three; no sliding, no stuck mid-stride frames.
- [ ] Later cinematic npc_bob walk (camera 0 segment) animates likewise.
- [ ] Imp demo: victim swaps to lying corpse art; E-loot still works after.
- [ ] Roam map00: scientist/civilians complete figures w/ idle bob; bumping
      into a squad member blocks the player (expected new solidity — see §1).
- [ ] Blue door unlock/open/close + auto-close behave EXACTLY as before.
- [ ] ESC skip during cinematic leaves no half-animated poses (idle restore).
- [ ] No new stderr spam besides intentional debug lines (remove TEMP [dbg]
      lerp-audit prints only after user sign-off, as before).

## 8. OUT OF SCOPE / DEFERRED (explicit)

- ET_MONSTER stacked rendering (floaters, special bosses, revenant/arch-vile/
  sentry offsets, pinky, muzzle flash, sentry head-flip) → EntityMonster unit;
  ADR 0005 records the boundary. Game-side walk writer already accepts them.
- Lootable-corpse PULSATE halo (needs monster-flag + lootset + state≠18 gates,
  src/Render.cpp:3471-3473) — halo deferred, plain dead frame shipped.
- PAIN auto-revert timer (`monster->frameTime`, src/Entity.cpp:350-368 +
  render revert :1600-1604): no frameTime storage exists; scripts re-pose via
  ENTITY_FRAME until EntityMonster lands.
- Attack timing logic (SHOTHOLD frame advance, Combat writes) — pose
  rendering only.
- Chat-icon overhead for NPC param=1; fear eyes; NPC_TALK gameplay wiring.
- Player first-person anything; AI; combat.

## 9. VERIFIED DATA APPENDIX (from the .ipa, 2026-08-25)

- map00 sprites: 7→tile 68 (media 610-620, 11 frames), 9→66 (601-609, 9),
  10→72 (641-650, 10); frame bytes 0x00; info words 0x44/0x42/0x48.
- Other NPC-range sprites: 16/91→71, 19→69, 30→77, 205/206→72, 246→75.
- entities.bin: defs 113-122 map tiles 66,68,69,71,72,73,74,75,76,77 →
  eType=3; ALL ET_CORPSE defs have tileIndex=0 (so clause 2 of §1 cannot
  misclassify placed corpse props).
- NPC frame bounds confirm layout, e.g. tile 68: f0 legs (63..70 × 118..177),
  f2 torso (52..81 × 125..151... stored as minX,maxX,minY,maxY), f3 head
  (76×100..134) — legs/torso/head sized slots at indices 0,2,3 and 4-7 back.

## 10. KNOWN CONFLICTS / NOTES

- Creating NPC entities changes collision & TILE_EMPTY results (§1) —
  faithful-to-legacy but user-visible; called out in checklist.
- Front/back `abs()` test has NO angle-wrap normalization in the original
  (`src/Game.cpp:2910`); ported verbatim. A walker crossing the 0/1023
  boundary may briefly misclassify — accepted (legacy behavior).
- Parabola hops also write walk frames mid-flight when anim=WALK_*
  (legacy writes unconditionally in the lerp tick) — authentic quirk.
- Idle bob clock = upTimeMs (runs during dialogs/pauses) — matches legacy
  `app->time`; do NOT switch to paused gameTime.
- `LERPSCALE` holds position ⇒ dist=0 ⇒ phase frozen at 1; harmless because
  the chooser requires movement and alloc-reset clears stale walk anims.
- World3D gains NO dependency on Game/EntityDefs (classification array is
  plain bytes) — keeps render/ domain-clean per module map.
