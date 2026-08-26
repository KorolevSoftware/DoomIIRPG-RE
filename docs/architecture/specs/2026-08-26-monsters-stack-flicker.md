# Spec 2026-08-26 — Monsters join the stacked path + walk-flicker fix

Status: READY FOR IMPLEMENTATION
Type: COMPACT DELTA on [`specs/2026-08-25-character-animation.md`](2026-08-25-character-animation.md)
(referenced below as **PS** — prior spec). Amends ADR-0005 decision 2 via
[ADR-0007](../adr/0007-monsters-stacked-character-path.md). Evidence base:
[`docs/research/2026-08-26-monster-render-flicker.md`](../../../docs/research/2026-08-26-monster-render-flicker.md)
(**RF**) and `docs/original-code/character-animation.md` §8 (**CA §8**).

Goal: live map00 monsters (sentry bots 18/19, zombies 20–22, imps 23–25,
arch-vile 50–52 — RF "minimal faithful port" inventory) render as full stacked
leg/torso/head figures instead of the legs-only billboard fallback
("половинчатые" импы), and marines stop flashing mid-walk during squad walks.
No gameplay/combat, no EntityMonster class, walk writer untouched.

Constraints honored: every constant cited; deviations listed in §9; render/
stays domain-clean (plain `charClass` bytes, PS §10).

---

## 1. DETECTION RULE v2 — enable ET_MONSTER (`new_src/core/GameContext.cpp:966-993`)

Replace the class predicate (PS §1) with:

```
class[i] = 1  ⟺  ∃ entity e (e.getSprite()==i, def valid) AND (
    e.def->eType == ET_NPC                                            // live NPCs        (unchanged)
 or (e.def->eType == ET_CORPSE AND (info[i]&0xFF) ∈ [65,80])          // corpsified NPC   (unchanged)
 or (e.info & kInfoCorpse AND MANIM_DEAD byte)                        // corpsified monster (unchanged, GameContext.cpp:989-992)
 or (e.def->eType == ET_MONSTER AND NOT isFloaterTile(tileNum[i])     // NEW: monster family
                                AND NOT isSpecialBossTile(tileNum[i]))
)
```

`tileNum[i] = info & 0xFF` (monsters never carry `SPRITE_FLAG_TILE`, so no
+257). Legacy gate being mirrored: `renderSpriteAnim` is entered for every
sprite whose entity has `monster != nullptr`
(`src/Render.cpp:1622-1626`). Equivalence: legacy allocates `monster` exactly
iff `def->eType == 2` during `loadMapEntities`
(`src/Game.cpp:430-436`), so `def->eType == ET_MONSTER` is the exact
rewrite proxy — all map00 monster-family entities qualify (task-approved).

### Floater / special-boss exclusion (routing choice)

Families that legacy diverts BEFORE the shared switch
(`src/Render.cpp:3157-3166`; predicates `:3023-3025`, `:3027-3029`) are kept
at class 0 (billboard path) because `drawCharacter` has no
`renderFloaterAnim`/`renderSpecialBossAnim` counterpart. Unreachable on map00
(tile inventory RF Q2), so this is a safety rail inherited from ADR-0005,
now narrowed to exactly the two diverted families.

Constants to ADD to `new_src/domain/game/Enums.h` (after the NPC/shadow block,
`:62-67`), each `static constexpr int`, values from `src/Enums.h:665-707`:

```cpp
// Monster art tiles (src/Enums.h:665-707) — classification-side subsets.
static constexpr int TILENUM_MONSTER_LOST_SOUL  = 29;  // ..31  (floater, src/Render.cpp:3007-3009)
static constexpr int TILENUM_MONSTER_LOST_SOUL3 = 31;
static constexpr int TILENUM_MONSTER_CACODEMON  = 41;  // ..43  (floater, src/Render.cpp:3003-3005)
static constexpr int TILENUM_MONSTER_CACODEMON3 = 43;
static constexpr int TILENUM_MONSTER_SENTINEL   = 44;  // ..46  (floater, src/Render.cpp:2979-2981)
static constexpr int TILENUM_MONSTER_SENTINEL3  = 46;
static constexpr int TILENUM_MONSTER_ARACHNOTRON = 53; // special boss, src/Render.cpp:3027-3029
static constexpr int TILENUM_BOSS_PINKY          = 56;
static constexpr int TILENUM_BOSS_MASTERMIND     = 57;
static constexpr int TILENUM_BOSS_VIOS           = 58; // ..62
static constexpr int TILENUM_BOSS_VIOS5          = 62;
```

Implement `isFloaterTile` / `isSpecialBossTile` as file-local helpers in
`GameContext.cpp` mirroring `src/Render.cpp:3023-3029` verbatim.

Comment refresh in the same loop: the `spriteSortBias` line
(`GameContext.cpp:964-965`) drops "none exist yet"; the block comment
(`:967-969`) cites ADR-0007 instead of the blanket ET_MONSTER exclusion.

Known residual gap (accepted, §9-D5): monsters hidden AT LOAD get no entity
(`new_src/domain/game/Game.cpp:136` skips `SPRITE_FLAG_HIDDEN`; legacy does
not skip hidden sprites, `src/Game.cpp:398-447`). Fixing that changes trace
solidity = gameplay → out of scope. Load-visible monsters (everything the
acceptance criteria cover) are unaffected.

## 2. MINIMAL MONSTER BRANCHES — `World3D::drawCharacter` ATTACK case only

Everything already shipped in PS §2.4 is reused verbatim for monsters:
IDLE/IDLE_BACK (bob, shadow), WALK_FRONT/WALK_BACK (phase math, sway, mirror),
PAIN/SLAP (single f12 quad), DEAD (single f13 quad, no shadow), NPC_TALK /
NPC_BACK_ACTION, and the shadow/ground math — monsters correctly take the
altitude-shrink path today (grounded ⇒ `min=0` ⇒ full-size floor shadow;
matches legacy for grounded monsters, `src/Render.cpp:3172-3180`; the
`monster->flags & 0x4000` glue case is the accepted simplification, RF
checklist item 5). Walk-torso mirror gate already excludes monsters correctly
(`npc` is tile-range based, `World3D.cpp:1111` = `src/Render.cpp:3277` minus
the deferred arch-vile term).

ONLY the ATTACK case (`World3D.cpp:1116-1125`) changes. New body:

```cpp
case kManimAttack1:
case kManimAttack2: {
    // Monster-family deltas (spec 2026-08-26 §2). n25 semantics: legs-only
    // flag carrier (src/Render.cpp:3298-3301, consumed at :3333 ONLY —
    // torso/head take raw flags, :3359/:3409).
    int atkFlags = info;
    if (isZombieFamily(tileNum) && frame == 1) atkFlags ^= 0x20000; // :3299-3301
    int pose = (anim == kManimAttack1) ? 8 : 10;                    // :3303-3308
    if (frame == 1 && !hasGunFlareFamily(tileNum)) ++pose;          // :3355-3357, set :3019-3021
    emitShadow();                                                   // :3310
    emitPart(0, x, y, zR, atkFlags ^ 0x20000, scaleFactor);         // legs, n24=0 :3333
    emitPart(pose, x, y, zR, atkFlags, scaleFactor);                // torso, n15=0 on map00 :3359
    if (!isZombieFamily(tileNum) && !isImpFamily(tileNum))          // head gate :3376
        emitPart(3, x, y, zR, info, scaleFactor);                   // head, n18=n16=0 on map00 :3409
    break;
}
```

Why each piece is exact for map00 families:

- **Head suppression covers imp AND zombie families**: the legacy gate is one
  line — `if (n19 != 0 && !isZombie(tileNum) && !isImp(tileNum) && renderHead)`
  (`src/Render.cpp:3376`). `n19` is 1 unless arch-vile attack zeroes it
  (`:3185`, `:3335-3340` — deferred); `renderHead` is only cleared for
  mancubus / revenant-attack2 (`:3366-3374` — deferred families). Effective
  map00 gate = `!isZombie && !isImp`. Ranges: imp 23–25
  (`src/Enums.h:671-673`, `src/Render.cpp:2975-2977`), zombie 20–22
  (`src/Enums.h:668-670`, `:3015-3017`). Imp attack sheets have no separate
  head art expectation — attack torso frames 8–11 carry the pose (RF frame
  inventory); drawing head f3 over them is the bug this fixes.
- **Zombie frame-1 flip is LEGS, not torso** — ERRATUM E1 (§9): RF Q1 and
  CA §8 say "attack torso mirrors on frame 1"; the source shows `n25` is
  consumed ONLY by the legs (`src/Render.cpp:3333`) while torso/head take raw
  `flags` (`:3359`, `:3409`). Net effect for zombie frame 1: legs lose the
  `^0x20000` (draw unflipped). Implemented via `atkFlags`.
- **Pose-increment gating**: `++pose` happens only when the family lacks gun
  flare (`src/Render.cpp:3355-3357`; `hasGunFlare` set = mancubus 38–40,
  revenant 35–37, sentry bots 18/19, Cyberdemon 54, Mastermind 57 —
  `:3019-3021`, `src/Enums.h:683-688,699,701`). Previously unconditional
  (`World3D.cpp:1120`): correct for imps/zombies/NPCs, wrong for map00 SENTRY
  BOTS reachable via scripted `EV_ENTITY_FRAME` attack bytes. Muzzle flash
  itself stays deferred (sentry frame 1 shows windup pose instead of flash —
  accepted, §8).
- **No offset terms for map00 families**: `n15/n16/n17/n18` start 0
  (`src/Render.cpp:3181-3184`) and are set only in deferred branches
  (Revenant/Arch-Vile/Cyberdemon/Chainsaw `:3206-3228,3234-3251,3335-3353`;
  head-offset block `:3378-3402`). Imps/zombies/sentries draw at plain `zR`.

New constants go in `World3D.cpp`'s anonymous namespace (next to the existing
`kTileNum*` block, `World3D.cpp:143-162` — same duplication pattern as
`kTileNumFirstNpc`; render/ stays decoupled from domain enums):

```cpp
constexpr int kTileNumMonsterImpFirst = 23;    // src/Enums.h:671-673
constexpr int kTileNumMonsterImpLast = 25;
constexpr int kTileNumMonsterZombieFirst = 20; // src/Enums.h:668-670
constexpr int kTileNumMonsterZombieLast = 22;
constexpr int kTileNumMonsterRedSentryBot = 18;// src/Enums.h:666-667
constexpr int kTileNumMonsterSentryBot = 19;
// hasGunFlare (src/Render.cpp:3019-3021): mancubus 38-40, revenant 35-37,
// sentry 18/19, cyberdemon 54, mastermind 57 (src/Enums.h:683-688,699,701).
```

plus `isImpFamily/isZombieFamily/hasGunFlareFamily` file-local helpers.

Header comment refreshes: `World3D.h:101-104` and the `drawCharacter` banner
(`World3D.cpp:1008-1011`) drop "NPC subset"/"deferred to EntityMonster" and
cite ADR-0007 + the ATTACK deltas.

## 3. FLICKER FIX — narrow alloc-reuse reset to monsters (`new_src/domain/game/Game.cpp:974-989`)

Legacy predicate VERBATIM (`src/Game.cpp:3049-3063`):

```cpp
Entity* entity = nullptr;
if (-1 != app->render->mapSprites[S_ENT + n2]) entity = &entities[...];
if (entity != nullptr && entity->monster != nullptr) {   // :3054  ← MONSTERS ONLY
    n3 = (mapSpriteInfo[n2] & 0xFF00) >> 8 & 0xF0;
    if (n3 == 32 || (lerpSprite->flags & LS_FLAG_AUTO_FACE)) n3 = 0;   // :3056-3058
    else if (n3 == 48) n3 = 16;                                        // :3059-3061
    mapSpriteInfo[n2] = ((mapSpriteInfo[n2] & 0xFFFF00FF) | n3 << 8);  // :3062
}
```

The rewrite widened this to `eType == ET_NPC || eType == ET_MONSTER`
(`Game.cpp:979-980`) — a rewrite-only divergence (RF candidate 2): any second
lerp reusing an ACTIVE NPC walker's slot stamps IDLE mid-stride → one-frame
flash per overlap. **Change**: remove `Enums::ET_NPC ||` so the gate reads
`ent != nullptr && ent->def != nullptr && ent->def->eType == Enums::ET_MONSTER`
(exact proxy for `monster != nullptr`, §1). Body and old-flags read order stay
as-is (`Game.cpp:981-988`). Update the comment to quote `:3054`.

**freeLerpSprite completion restore — VERDICT: NO CHANGE.** Legacy gates it on
`eType == ET_NPC || eType == ET_MONSTER` — deliberately WIDER than the alloc
side (`src/Game.cpp:3100-3112`), with a nested `if (monster) frameTime = 0`
pain-timer clear (`:3102-3104`, deferred — no carrier). The rewrite
(`Game.cpp:1125-1135`, incl. the hidden-sprite guard = legacy `0x10000` test
`:3100`) already matches exactly. Do not narrow it.

Chooser/front-back logic (`Game.cpp:1071-1086` = `src/Game.cpp:2908-2921`) is
NOT touched: the whole-body front↔back art-set pop (RF candidate 1) is
faithful camera-driven behavior. Instrumentation only: extend the existing
TEMP `[dbg] lerp audit` (`Game.cpp:1043-1057`) to also print
`animByte 0x%02X -> 0x%02X` whenever the audited sprite's bits 8-15 change
(walk writer, resets, restores). Removed with the other TEMP logs at sign-off
(PS §7). The one-tick waypoint idle pop (RF candidate 3) stays — authentic.

## 4. TEMP HACK RETIREMENT — MANIM_DEAD-without-charClass routing (`World3D.cpp:640-654`)

After §1, every sprite the hack was protecting is entity-classified:

| sprite kind | covered by | note |
|---|---|---|
| dying live monster (byte 0x70, def ET_MONSTER) | ET_MONSTER clause (§1) | |
| corpse-swapped monster (EV_MAKE_CORPSE, def→ET_CORPSE) | kInfoCorpse clause (`GameContext.cpp:989-992`) | legacy keeps `monster != nullptr` after the swap, so `renderSpriteAnim` still runs (`CA §5`, `src/ScriptThread.cpp:1614-1625,2249-2265`) |
| dead NPC-range art | ET_CORPSE+range clause (PS §1) | unchanged |
| placed corpse props / NOENTITY dead sprites | NOTHING → billboard fallback | FAITHFUL: legacy gives them no `renderSpriteAnim` at all (gate is entity-driven only, `src/Render.cpp:1620-1626`; NOENTITY ⇒ `S_ENT==-1` ⇒ single raw-frame quad `:1728`, RF Q1 bonus finding). The anim-byte detour has no legacy counterpart. |

**Decision: RETIRE the disjunct.** `drawSprite`'s router reduces to
`if (charClass != nullptr && charClass[i] != 0) { drawCharacter(...); return; }`.
Replace the comment block (`World3D.cpp:640-649`) with: classification is
purely entity-def driven (ADR-0005/0007); legacy has NO anim-byte routing;
entity-less DEAD sprites intentionally fall back to the single-quad billboard
(raw-frame clamp `World3D.cpp:687-688` vs legacy flat-array bleed — known edge
divergence, RF "Note (edge anims)"). The kInfoCorpse clause in GameContext
STAYS (it is the corpsified-monster carrier).

Tripwire (TEMP, removed at sign-off): one-shot `[dbg] dead-fallback spr=%d
tile=%d` stderr line when a sprite with a MANIM_DEAD byte draws through the
fallback — proves the retirement caught nothing on map00 during the
acceptance run.

## 5. GROUPS & CHECKPOINTS (ONE group)

**GROUP M1 — monsters stack + flicker.**
Files: `new_src/core/GameContext.cpp` (§1 predicate + helpers + comments),
`new_src/render/World3D.cpp` (§2 ATTACK case + constants/helpers, §4 router +
tripwire, banner comments), `new_src/render/World3D.h` (comment only),
`new_src/domain/game/Game.cpp` (§3 gate + dbg anim log),
`new_src/domain/game/Enums.h` (constants block). Docs: ADR-0007, README index,
this spec. **No new files ⇒ no CMake reconfigure.**

- **C-M1a (classify)**: builds green; boot reaches ST_PLAYING; imps render as
  full stacked bodies (user eyes); doors/props/marines pixel-unchanged.
- **C-M1b (branches)**: imp-demo victim still collapses to lying corpse art
  and loots afterwards; any scripted attack/pain poses on imps/zombies render
  the right frames (no head in attack).
- **C-M1c (flicker)**: squad walks clean; `[dbg]` anim log reviewed — no
  IDLE-stamped overlaps on NPC slots; `dead-fallback` tripwire silent.

## 6. ACCEPTANCE CRITERIA (user = the eyes)

1. Live imps near the elevator / imp-demo area show COMPLETE three-part
   bodies — distinct legs, torso, head over a floor shadow — both standing
   idle (two-phase breathing bob) and walking (leg cycle + torso sway locked
   to travel). No more half-cut legs-only quads; walkers animate instead of
   sliding a frozen frame.
2. Marines no longer flash to idle/other poses mid-walk during the squad
   walk-ins (alloc-reuse fix); gait and arrival idle behave exactly as
   accepted in PS.
3. Corpses and dead imps unchanged: single lying quad, no shadow, lootable
   as today.
4. Zombies/sentry bots on the route (if passed) also render as complete
   stacked figures (sentry head statically correct — flip deferred).
5. Doors, pickups, decor, water, wall decals, dialog/loot UI: visually
   unchanged.
6. Monster HP bar still never appears (dormant HUD port, RF Q4 — untouched).

## 7. MANUAL CHECKLIST

- [ ] Boot intro → cameras with imps in view: full stacked imps idle + walking.
- [ ] Elevator-area encounter: walking imps show alternating leg art and stop
      into bobbing idle; nothing "половинчатое".
- [ ] Imp-demo kill: victim collapses to the lying frame; E-loot works after.
- [ ] Full squad walk-in ×2 replays: zero mid-walk pose flashes on marines.
- [ ] Roam past zombies/sentry bots (if on path): complete bodies, no crash,
      no texture garbage.
- [ ] Blue door open/close/auto-close + a corpse loot: unchanged.
- [ ] ESC-skip mid-cinematic: no stuck walk/pain frames afterwards.
- [ ] stderr shows no repeated `[dbg] dead-fallback` lines (tripwire quiet).

## 8. DEFERRED (explicit; unchanged from PS §8 unless noted)

- Arch-vile specials: idle torso −36 / head +109 (`src/Render.cpp:3218-3220,
  3243-3244`), walk torso −36 + torso-mirrors-with-legs (`:3273-3274,3277`),
  attack frame-0 z+288 + head off (`:3335-3340`). (Task-directed defer; map00
  arch-vile renders via the plain stack meanwhile.)
- Sentry-bot head H-flip cadence `/2048` idle, `/1024` walk
  (`:3234-3236,3280-3282`).
- Muzzle flash (`hasGunFlare` block `:3411-3460`); fear eyes
  (`:3031-3141,3487`); lootable-corpse pulsate halo (`:3471-3473`);
  pain auto-revert timer (`monster->frameTime`, `src/Entity.cpp:350-368`,
  `src/Render.cpp:1600-1604`; incl. the `:3102-3104` lerp-end clear).
- Revenant/Mancubus/Cyberdemon/Chainsaw offsets and head-in-torso variants
  (`:3206-3228,3363-3374,3378-3402`); floater + special-boss family
  renderers (`:3490+`); Cyberdemon scale halving (`:3149-3155`).
- Hidden-at-load monster entity parity (§1 residual gap) — bundled with the
  future EntityMonster/loadEntities-parity unit.

## 9. DEVIATIONS & ERRATA

- **D1** — `def->eType == ET_MONSTER` stands in for legacy
  `monster != nullptr` (equivalence: allocation iff eType==2,
  `src/Game.cpp:430-436`). Exact for every state the rewrite can reach.
- **D2** — floater/special-boss ET_MONSTER defs excluded from class 1
  (billboard fallback) until their family renderers land; unreachable on
  map00 (RF inventory). Mirrors legacy *routing*, not legacy *output*.
- **D3** — glued/ground shadow simplification for monsters accepted (RF
  checklist); grounded monsters already match legacy exactly.
- **D4** — out-of-range frame clamp (rewrite) vs legacy flat-array bleed:
  pre-existing, unchanged (RF note; PS §10).
- **D5** — hidden-at-load monsters have no entity → billboard path if ever
  shown by script; fixing = gameplay surface (trace solidity), deferred.
- **E1 (doc erratum, forward to researcher)** — RF Q1 bullet 4 and CA §8
  "Zombies" say the frame-1 attack mirror hits the TORSO; source shows `n25`
  feeds only the LEGS (`src/Render.cpp:3298-3301` → `:3333`), torso/head take
  raw `flags` (`:3359`, `:3409`). This spec implements the source behavior.
