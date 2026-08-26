# 2026-08-26 — Monster render path vs NPC ("half" imps) + character walk flicker

## Hypothesis

(a) User: live imps render "половинчато" (half-cut) — suspected different monster
drawing logic vs NPCs. (b) User: characters flicker while moving ("не хватает
какого-то кадра анимации"). (c) User asks what "hp drawing method" exists in
new_src while no fight is visible.

## Method

Read `Render::renderSpriteAnim` and its callers in `src/`, decoded media frame
bounds + texel pixel counts for tiles 23 (imp) and 66–77 (NPCs) directly from
`tmp_newMappings.bin` / `tmp_newTexels*.bin` (layout per
`tools/extract_textures.py:50-84`), parsed `tmp_map00.bin` sprites with the
exact `new_src/domain/world/MapParser.cpp` field order, and traced the rewrite
paths in `new_src/render/World3D.cpp`, `new_src/domain/game/Game.cpp`,
`new_src/core/{GameContext,GameLoop}.cpp`.

## Verdicts

### Q1 — Monsters vs NPCs: CONFIRMED identical stacked logic (imps are NOT special)

- Routing: every entity with `monster != nullptr` enters
  `renderSpriteAnim` (`src/Render.cpp:1624-1626`); NPCs enter via
  `def->eType == ET_NPC` (`src/Render.cpp:1660-1661`). Same function, same
  4-quad stack.
- Imp exclusions don't apply: `isFloater` = Sentinel/Lost Soul/Cacodemon
  (`src/Render.cpp:3023-3025`), `isSpecialBoss` = Mastermind/Arachnotron/
  Boss Pinky/VIOS (`src/Render.cpp:3027-3029`). Imp tiles 23–25
  (`src/Enums.h:671-673`) hit neither → plain stack.
- Live idle imp emits exactly: shadow (tile 232) → legs f0 @z → torso f2
  @z+bob → head f3 @z+bob (`src/Render.cpp:3202,3204,3231,3252`). No per-monster
  offsets apply to imps (offsets exist only for Revenant/Arch-Vile/Cyberdemon/
  Riley/Civilian2/Scientist/Sentry, `src/Render.cpp:3206-3228,3234-3251`).
- Walk imp: legs `base+(frame&1)` with mirror bit, torso `base+2`, head
  `base+3` (`src/Render.cpp:3261-3291`) — identical to NPC; imp torso does NOT
  mirror (`(isNPC&&!SARGE)||isArchVile` gate, `src/Render.cpp:3277`).
- Attack imp: legs f0 `flags^0x20000` (`:3333`), torso f8/f10 (+1 on frame==1,
  `:3303-3308,3355-3357`), **no head** (`:3376` — `!this->isImp(tileNum)`),
  no muzzle flash (imp lacks `hasGunFlare`, `:3019-3021`).
- Pain: shadow + single f12 quad (`:3464-3468`). Dead: single f13 quad, no
  shadow (+ pulsating halo copy when lootable, `:3470-3475`).

Imp media inventory (tile 23, mappings[23]=114..127, 14 frames; bounds +
decoded RLE pixel counts from tmp_newMappings.bin/tmp_newTexels*.bin):

| f | media | art size (px) | rows | content |
|---|---|---|---|---|
| 0 | 114 | 50×87 | 87..174 | front legs A |
| 1 | 115 | 50×88 | 88..176 | front legs B |
| 2 | 116 | 114×79 | 42..121 | front torso (wide) |
| 3 | 117 | 30×46 | 20..66 | front head |
| 4 | 118 | 40×82 | 86..168 | back legs A |
| 5 | 119 | 105×91 | 86..177 | back legs B (wide/sparse) |
| 6 | 120 | 91×60 | 44..104 | back torso |
| 7 | 121 | 24×21 | 23..44 | back head |
| 8 | 122 | 121×94 | 23..117 | attack1 windup/torso |
| 9 | 123 | 80×91 | 18..109 | attack1 pose/torso |
| 10 | 124 | — | — | **exact copy of f8** (texel+palette REF → 122) |
| 11 | 125 | — | — | **exact copy of f9** (REF → 123) |
| 12 | 126 | 107×147 | 26..173 | pain (near-full-body) |
| 13 | 127 | 164×61 | 115..176 | dead (lying) |

No frame 0–11 is a full standing body — idle/walking imps only exist as stacked
parts. Only f12/f13 are single quasi-complete quads.

### Q2 — What the rewrite shows: CONFIRMED legs-only fallback

`drawSprite`: live imps get `charClass[i]==0` (ET_MONSTER deliberately excluded,
ADR-0005; classification at `new_src/core/GameContext.cpp:976-992`) and their
anim byte ≠ 0x70, so they fall into the billboard fallback
(`new_src/render/World3D.cpp:650-656`):

- **Idle imp** (anim byte 0x00): `frame=(info>>8)&0xFF = 0` →
  `mediaId = mappings[23]+0 = 114` → ONE quad of LEGS art. Exactly the lower
  half of the imp = "половинчато".
- **Walking imp**: Game-side walk writer DOES run for ET_MONSTER
  (`new_src/domain/game/Game.cpp:1062-1093`) and writes `0x20|phase` → frame
  byte 32..35 → `mediaId = 114+32… ≥ hi(128)` → clamped to lo=114
  (`World3D.cpp:687-688`): a STATIC legs-only quad sliding along the path, no
  animation, no mirror.
- **DEAD hack** (`World3D.cpp:650-653` routes anim nibble 0x70 into
  `drawCharacter`): works — `emitPart(13)` → media 127 lying art
  (`World3D.cpp:1133-1136`). Corpsified monsters also arrive via
  `kInfoCorpse` clause (`GameContext.cpp:989-992`).
- Bonus finding: map00 z-sprites 185/186/187 (tile 23, x=224,y=1696) carry
  anim bytes 0x00/0x02/0x03 and `SPRITE_FLAG_NOENTITY` — in BOTH engines these
  render as three separate part quads (legs/torso/head) composing one imp
  (legacy: NOENTITY ⇒ `S_ENT==-1` ⇒ no `renderSpriteAnim`, single quad with raw
  frame byte, `src/Render.cpp:1565-1574,1728`; rewrite: fallback, same media).
  Not a bug — a map-data art assembly.

**Minimal faithful monster port for map00** (map00 monster tiles: 18,19 sentry
bots; 20–22 zombies; 23–25 imps; 52 arch-vile — none is floater/special-boss):

1. Classification: add `ent.def->eType == ET_MONSTER` to the class-1 rule
   (`GameContext.cpp:976-981`). `drawCharacter`'s existing switch then handles
   IDLE/WALK/DEAD/PAIN correctly. Keep the DEAD clauses for corpse props.
2. ATTACK branch additions in `World3D::drawCharacter`
   (`World3D.cpp:1116-1125`): imp/zombie head suppression
   (`src/Render.cpp:3376`); zombie torso mirror on frame 1
   (`:3299-3301`); keep pose 8/10(+1) and legs `flags^0x20000`.
3. Arch-vile (tile 52) specials: idle torso −36/head +109
   (`:3218-3220,3243-3244`), walk torso −36 + torso-mirrors-with-legs
   (`:3273-3274,3277`), attack frame0 z+288 and no head (`:3335-3340`).
4. Sentry-bot (tiles 18/19) time-based head flip in idle/walk
   (`:3234-3236,3280-3282`).
5. Shadow: monsters shrink shadow from ground unless `monster->flags & 0x4000`
   (`:3172-3180`); script lerps SET 0x4000 on monsters
   (`src/ScriptThread.cpp:366-368`). Grounded monsters give alt=0 either way;
   acceptable simplification: keep current glued-shadow until EntityMonster.
6. Fear eyes / muzzle flash / loot halo: skip (not map00-visible; halo deferred
   per spec §8).

### Q3 — Walk flicker: PARTIAL (clamp ruled out; three real candidates)

Ruled OUT with data:

- **Missing/clamped frames during walk**: walk consumes indices ≤7
  (base 0/4 + {0,1,2,3}). Sheet counts: t66=9, t68=11, t69=9, t71=11, t72=10,
  t73/74/75/76/77=9 frames (mappings ranges decoded from
  tmp_newMappings.bin). No index ≥8 is touched while WALKING → the
  `mediaId >= hi → lo` clamps (`World3D.cpp:688,1070`) cannot fire mid-walk.
- **Mirror art**: mirroring is purely a UV S-flip — `flags ^ 0x60000 & 0x20000`
  reverses corner order (`src/GLES.cpp:515-542` fixed-size override; ported at
  `new_src/render/World3D.cpp:986-991`). No mirrored frames exist or are needed.

Real candidates, ranked:

1. **Front/back art-set flip mid-walk** (most likely what reads as flicker):
   the chooser re-evaluates `|viewAngle − vecToDir(move)| < 256` EVERY tick
   (`new_src/domain/game/Game.cpp:1071-1083` = verbatim
   `src/Game.cpp:2908-2917`). During cinematic pans the fed angle is the live
   maya yaw (`GameContext.cpp:199-201`); each crossing swaps base 0↔4 — the
   ENTIRE body art set (front ↔ back) in one tick. Legacy reads
   `app->render->viewAngle` which is equally camera-driven, so this is
   faithful — but it is the biggest whole-body pop available. Verify with an
   anim-byte log before changing anything.
2. **Rewrite-only divergence — alloc-reuse reset hits NPCs**: rewrite resets
   WALK→IDLE on slot reuse for `ET_NPC || ET_MONSTER`
   (`new_src/domain/game/Game.cpp:978-988`); legacy resets MONSTERS only
   (`entity->monster != nullptr`, `src/Game.cpp:3054-3062`). Any second lerp
   reusing an ACTIVE walker's slot (async/LERPSCALE/LERPOFFSET overlap) writes
   IDLE mid-stride in the rewrite only → one-flash-per-overlap. Exact-legacy
   fix: drop ET_NPC from that gate.
3. **One-tick idle pop at chained waypoints** (faithful, low priority):
   completion frees inside `game->update` and restores IDLE
   (`Game.cpp:1018-1021,1125-1135` = `src/Game.cpp:2868-2871,3105-3110`);
   the resumed thread re-allocates synchronously but the first walk-write only
   happens next tick (15 ms quantum, `GameLoop.cpp:73-80`, one tick per frame;
   immediate tick only for time==0 lerps, `ScriptVM.cpp:654-655` =
   `src/ScriptThread.cpp:379-386`). Exactly one rendered frame shows idle
   stance between segments — same count as legacy DoLoop ordering
   (`src/Main.cpp:83-88,138-140`).

Phase math verified equal (no distance-sync bug): `p=(elapsed<<16)/(tt<<8)`
(`Game.cpp:1025` = `src/Game.cpp:2877`), `dist=isqrt(S<<16)>>8` ≡ legacy
`FixedSqrt(S<<8)>>8` since FixedSqrt shifts `<<8` internally
(`Game.cpp:928-934`; `src/LerpSprite.cpp:48`; `src/Game.cpp:3636-3654`),
phase `(1+(p*dist>>12))&3` (`Game.cpp:1090` = `src/Game.cpp:2943`).

Note (edge anims, not walk): legacy does NOT clamp frame indices —
`mediaMappings[tile]+frame` indexes the FLAT media array
(`src/GLES.cpp:586-592`, `src/Render.cpp:2043-2047`), so e.g. Riley attack-pose
f9 would bleed into Major's sheet (media 610). The rewrite clamps to its own
lo instead (`World3D.cpp:1069-1070`). Different wrong-art in out-of-range edge
cases; harmless for map00 today, but worth remembering when comparing pixels.

### Q4 — HP bar: located

- Legacy: `Hud::drawMonsterHealth` (`src/Hud.cpp:822-899`), called from the HUD
  pass (`src/Hud.cpp:247`) only when `player->facingEntity` holds a monster —
  set by the aim/trace in `src/MovementController.cpp:87`. 25 cells wide,
  top-center at `viewRect[1]+6` (+50 subtype-5 parm-0, +20 zoomed),
  250 ms animated drain, colors red ≤¼ / orange / green ≥¾ full.
- new_src: the port exists — `new_src/ui/Hud.cpp:220-249`, drawn from
  `drawTopBar` (`:295`) — but its only feed is the demo setter
  `setDemoMonster` (`new_src/ui/Hud.h:103-107`) which has **zero callers**, so
  `monsterValid_` stays false and nothing draws. That's the "файта нет":
  targeting/combat isn't wired, the renderer is dormant, not missing.

## Open questions

- Does the intro camera pan actually cross the ±90° chooser boundary for the
  walking squad (candidate 1)? Needs an anim-byte trace or user observation.
- Do any map00 scripts overlap a walking NPC with a second lerp (would trigger
  candidate 2)? Script-traceable via existing `[dbg] LERPSPRITE` lines.
- Sprite 33 (tile 69, flags 0x02D00045: oriented + TILE) resolves def
  lookup to 69+257=326 → no entity → billboard wall branch in both engines?
  (legacy gives it an entity via loadMapEntities — verify its look.)

## PORT CHECKLIST (minimal monster rendering + flicker)

Monster rendering:
- [ ] Add `ET_MONSTER` to charClass rule (`GameContext.cpp:976-981`).
- [ ] ATTACK: skip head for imp/zombie (`src/Render.cpp:3376`); zombie torso
      mirror on frame 1 (`:3299-3301`).
- [ ] Arch-vile offsets + attack frame0 lift + no-head (`:3218-3220,3243-3244,
      3273-3274,3277,3335-3340`).
- [ ] Sentry head-flip cadence `/2048` idle, `/1024` walk (`:3234-3236,3280-3282`).
- [ ] Accept glued-shadow simplification (documented) until monster->flags exist.
- [ ] Defer: fear eyes, muzzle flash, loot halo, pain timer.

Flicker fixes, ranked:
1. Instrument first: log anim-byte changes per walking sprite; confirm whether
   flips (base 0↔4) coincide with perceived flicker before altering the
   faithful per-tick chooser.
2. Match legacy alloc-reuse gate: remove `ET_NPC` from the reset at
   `new_src/domain/game/Game.cpp:978-988` (legacy `src/Game.cpp:3054`).
3. Leave the one-tick waypoint idle pop as-is (authentic) unless the user wants
   it smoothed (would be a deliberate deviation).
