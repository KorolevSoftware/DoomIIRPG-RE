# 2026-08-26 — Combat Stage 1: fire pipeline, damage math, monster payload, HUD feed

> **PARTIALLY SUPERSEDED (2026-08-26)** by
> `specs/2026-08-26-combat-stage1-fixes.md`: §0.F's "one tile toward `viewStep`"
> facing probe (:157-160), the single-closest-hit fire election of §4/§9
> deviation 1, and §6.2's view-weapon anchors/source rects are WRONG and are
> replaced there (6-tile facing ray, ordered election walk over the sorted hit
> list, legacy world viewport + 176-texel UV window). Everything else in this
> spec stands.

Status: **spec for implementation**. Architect: design only; coder groups implement.
Normative inputs: `docs/research/2026-08-26-combat-stage1.md` (§refs below),
`docs/original-code/combat.md` §§1–6,
`docs/research/2026-08-26-hero-choice-and-weapon.md` Part B,
`docs/original-code/character-animation.md` §5.
Every claim cites `src/<file>:<line>` (original RE port = ground truth) or
`new_src/<file>:<line>` (rewrite).

Stage-1 slice (= research §6): view-weapon quad → fire input (swept-ray target
election, loot preempts) → `calcHit`/`calcDamage` verbatim incl. table
consumption → two-stage `playerSeq` timer (hitscan only) → `EntityMonster`
allocation + activate/deactivate rings → `pain()`/`died()` → real health-bar
feed via facing trace → ScriptVM ops 10/51/56/84 (+19/28/60 upgrades)
un-threadkilling.

---

## 0. Design decisions (tasking A–G)

### A. Combat math lives in a new module `domain/game/Combat.{h,cpp}`

* Mirrors legacy one-to-one: legacy has a peer subsystem `app->combat`
  (`src/Combat.h:12-176`) separate from `app->game`. We replay that shape:
  **class `Combat`**, instance owned by `Game` as a public member
  (`Game::combat`), constructed with `Game`, wired to its collaborators once
  via `Combat::init(const Env&)` from `Main.cpp` (same pattern as
  `vm.init(...)` / `dialogs.init(...)`, `new_src/core/Main.cpp:226-233`).
* Pure stat math stays on `CombatEntity` exactly like legacy:
  `calcCombat`/`calcHit`/`calcDamage` become methods taking `Combat&` as their
  first parameter (legacy reaches them implicitly through the `app` singleton;
  `src/CombatEntity.cpp:130,157,300`). No float math anywhere — every
  expression is ported as the same integer/fixed-point operation (`<<8/100`
  percents, `>>8` weakness multiply, Chebyshev² distances).
* RNG: legacy rolls are `app->nextByte()` = `std::rand() & 0xFF` in the RE
  port (`src/App.cpp:506-512`) — the original's LCG was already replaced, so
  roll-bit-exactness is NOT a legacy invariant. Rewrite: local
  `Combat::nextByte()` returning `std::rand() & UINT8_MAX`, seeded by
  `std::srand` at boot (cite above).
* Tables consumed through `Env::tables` (`Tables::weaponData` = legacy
  `weapons` int8 table stride 9; `monsterStats` stride 6; `monsterWeakness`;
  `combatMasks`; `new_src/io/Tables.h:25-68`). Weapon-field offsets are the
  legacy constants (`WEAPON_FIELD_*`, `src/Combat.h:26-36`) — replicate as
  `Combat::kField*`.

### B. EntityMonster: real payload struct now, allocated from a fixed pool

Decision (ADR 0008): implement **`domain/game/EntityMonster.h`** as a plain
struct mirroring `src/EntityMonster.h:13-42` — `ce` (CombatEntity),
`nextOnList/prevOnList` (Entity* ring links), `nextAttacker`, `target`,
`frameTime`, `flags` (short, MFLAG_* bits already in
`new_src/domain/game/Enums.h:275-290`), `monsterEffects` (int), goal fields
(`goalType/goalFlags/goalTurns/goalX/goalY/goalParam`), plus `reset()` and
`resetGoal()`.

* Storage: `EntityMonster entityMonsters_[80]` on `Game` + `numMonsters_`
  (legacy pool `entityMonsters[80]`, Error 37 on overflow
  `src/Game.cpp:430-436`; rewrite logs `[monster] ERR_MAX_MONSTERS (37)` and
  skips). `Entity::monster` (already declared, `new_src/domain/game/Entity.h:33`)
  points into this pool. Pool lifetime = one map load; `loadEntities` resets
  `numMonsters_ = 0`.
* Rings: `Game::activeMonsters / inactiveMonsters` (Entity* heads, circular
  doubly-linked through the EntityMonster links) with faithful
  `activate(Entity*, bool runStaticFunc, bool rangeCheck, bool alertSound,
  bool unused)` and `deactivate(Entity*)` ports
  (`src/Game.cpp:752-808`, `:825-855`). Spawning inserts every monster on the
  inactive ring (`src/Game.cpp:442-443`).
* **monstersTurn / interpolatingMonsters / Error-95**: fields exist now
  (`bool interpolatingMonsters = false` added; `monstersTurn` exists,
  `new_src/domain/game/Game.h:246`). Nothing sets `interpolatingMonsters` in
  Stage 1, so `advanceTurn`'s guard is a defensive log-only branch
  (`if (interpolatingMonsters) stderr "[turn] ERR_NONSNAPPEDMONSTERS (95)"` +
  snap). `snapMonsters(bool)` is a Stage-1 stub that simply calls
  `endMonstersTurn()` if `monstersTurn != 0` (safe: no lerps exist).
  `endMonstersTurn()` sets `monstersTurn = 0` (`src/Game.cpp:2452-2456`);
  the tickLoading inline clear (`new_src/core/GameContext.cpp:259`) switches
  to calling it.
* **Stage-1 monster behavior contract** (binding for Stage 2): an activated
  monster is wakeable, damageable, killable, lootable and renders pain/death
  poses — but it NEVER moves or attacks (`aiThink` not ported; op 51 stores
  goal state only, see §7). The `monstersTurn` window opens in `advanceTurn`
  and closes in the same Playing tick via `Game::updateMonsters()` (a Stage-1
  stub whose whole body is `if (monstersTurn != 0) endMonstersTurn();` —
  placed where legacy runs AI + lerps, `src/Game.cpp:2458-2474`). All data
  structures `aiThink` needs (`goal*`, `target`, `nextAttacker`, Game-side
  `combatMonsters` head — declare `Entity* combatMonsters = nullptr`) exist
  from day one so Stage 2 adds behavior without re-plumbing.

### C. Turn integration: fire consumes its turn after the seq finishes

Legacy order (research §1): player attack → ST_COMBAT seq → `combatState()`
calls `advanceTurn()` only when the seq reports done
(`src/GameStateRunner.cpp:26-38`). Our loop has no state swap, so:

1. `handlePlayingAction(Action::Use)` fire path ends with
   `game.combat.performAttack(target, x, y)` which sets `combat.active`.
2. `tickPlaying` step 4 (where the monster-phase placeholder sits today,
   `new_src/core/GameContext.cpp:313-315`) becomes:

   ```cpp
   if (sys_.game->combat.active) {
       if (!sys_.game->combat.tick()) {          // runFrame analog, §5
           sys_.game->combat.active = false;
           sys_.game->advanceTurn();             // turn consumed AFTER seq
       }
   } else {
       sys_.game->updateMonsters();              // Stage-1 stub (B above)
   }
   ```

   (Deviation: legacy flips back to ST_PLAYING a frame later; we advance in
   the same tick — invisible.)
3. Input gate: `handlePlayingAction` returns immediately while
   `sys_.game->combat.active` (legacy drops all input in ST_COMBAT because
   state ≠ PLAYING; turning included). Movement gate
   (`new_src/core/GameContext.cpp:771-774`) stays as-is underneath.
4. PER_TURN static func 6 keeps firing inside `Game::advanceTurn`
   (`new_src/domain/game/Game.cpp:600`) — unchanged, already correct.

Loot/tile-event/door preemption order in ACTION_FIRE is preserved exactly as
built (`new_src/core/GameContext.cpp:831-864`); the fire path slots in after
the door branch (§4).

### D. No new StateId this stage

ST_COMBAT (legacy value 5) is NOT added to `StateId`
(`new_src/core/GameContext.h:33-42` keeps it un-enumerated). The combat seq
is a timer inside Playing: visuals are identical (legacy ST_COMBAT renders the
same world + weapon; `src/Canvas.cpp:1344-1355` draws drawWeapon during
combat), and we avoid stateVars reset / enter-exit churn. Deviation logged in
§11. A real state id arrives only if Stage 2's monster attack seq needs
distinct input/render gating.

### E. Fire input mapping and disabledWeapons

* `E` → `Action::Use` exists (`new_src/core/GameLoop.cpp:39`); the Use handler
  grows the fire election (§4). Loot-first ordering already matches legacy.
* op 60 EV_DISABLED_WEAPONS stops being a consume-only stub: store the s16
  mask into new `Player::disabledWeapons`
  (`src/ScriptThread.cpp:1443-1451`: assignment + `selectNextWeapon()` when
  the current weapon is masked). `selectNextWeapon` is NOT ported (single
  weapon on the map00 route); masked-current-weapon logs instead. The
  `fireWeapon` guard consumes the mask (`src/Player.cpp:761-763`).
* Weapon switching UI: deferred entirely. Auto-equip on first pickup exists
  (`new_src/domain/game/Player.cpp:99-113`); the map00 route equips exactly
  the rifle via EVT 617 before any combat.

### F. Health-bar feed: facing probe replaces the demo feed
<!-- SUPERSEDED: the one-tile probe below is the source of the "bar only for an
adjacent monster" defect. Use spec 2026-08-26-combat-stage1-fixes.md §1. -->

* New `Player::facingEntity` (`Entity*`, default nullptr — field absent
  today; audit row `docs/research/2026-08-26-combat-stage1.md` §5).
* Probe (subset of `src/MovementController.cpp:32-87`): a latch
  `GameContext::facingDirty_` set by (a) `advanceTurn`, (b)
  `finishRotationFired`, (c) `Game::diedMonster` (canvas
  `updateFacingEntity=true` analog, `src/Entity.cpp:527`), (d) Playing-state
  entry. When set, `tickPlaying` runs one `traceMove` from
  `(destX,destY)` toward `viewStep` (one tile, radius 2, mask **21741**
  local constant — decompose from `src/MovementController.cpp:38`:
  bits {0 world, 2 monster, 3 npc, 5 door, 6 item, 7 decor, 10
  attack_interactive, 12 spritewall, 14 decor_noclip}; i.e. WEAPONSOLID
  minus corpse(512)/nonobstructing-spritewall(8192), plus item(64) +
  decor_noclip(16384)), then applies the legacy
  gates: non-monster candidates beyond Chebyshev²
  `tileDistances[2] = 36864` are dropped; monsters always kept
  (`:88-93`). Monster preference rescan uses the full sorted hit list via a
  new `Game::lastTraceHits()` accessor (closest non-preferred hit is
  replaceable by a monster further along the ray, `:43-86` subset).
* Feed: `Hud::feedMonsterHealth(int id, int hp, int maxHp)` (id = faced
  sprite index, −1 clears) called from `GameContext::render` each frame —
  render resolves `player->facingEntity` → `monster->ce` stats. Replaces the
  dormant demo API (`setDemoMonster`, `new_src/ui/Hud.h:103-107`; zero call
  sites today). Bar drain animation over 250 ms lands inside Hud (latch
  display HP when `id` changes, ease to `hp` across 250 ms of `Hud::update`;
  legacy `src/Hud.cpp:822-899`).
* Wiring: `drawTopBar` gains a real caller — `GameContext::render` invokes it
  for states Playing/Looting/Dialog right before `drawMessages`. To avoid
  double message drawing, the message block is REMOVED from
  `drawTopBar` (messages stay solely in `drawMessages`,
  `new_src/ui/Hud.cpp:291-307,419-444`). Health-bar y becomes
  `viewRect[1]+6 = 26` (resolves research open question 3; legacy
  `src/Hud.cpp:822-899`, viewRect y=20 `src/Canvas.cpp:124-127`).

### G. VM ops 10/51/56/84 (+19/28/60) semantics

See §7 for the exact contract each opcode implements. Summary: 10 = read
player weapon into a var; 51 = store goal + activate (no aiThink);
56 = scripted player attack through the real performAttack; 84 = target-
practice teleport with parked thread (scoring deferred); 19 = real pain/died
path; 28 = real wake; 60 = store disabled mask.

---

## 1. Constants and tables (port once, cite forever)

```cpp
// domain/game/Combat.h  (values from src/Combat.h:17-59)
kMaxTileDistances = 16;                 // MAX_TILEDISTANCES (:19)
tileDistances[j] = 64*(j+1) * 64*(j+1); // startup fill (:41-44); [0]=4096 [2]=36864 [3]=65536
kFieldStrMin=0, kFieldStrMax=1, kFieldRangeMin=2, kFieldRangeMax=3,
kFieldAmmoType=4, kFieldAmmoUsage=5, kFieldProjType=6, kFieldNumShots=7,
kFieldShothold=8;                       // stride 9 (:26-35)
// CR flags used by calcHit/calcDamage/playerSeq:
// 0x001 hit, 0x002 crit, 0x004 far-shot cap, 0x010 (punch, omitted),
// 0x020 no-strength-bonus, 0x040 punch-flag ranged gate, 0x100 dodge/no-damage,
// 0x400 out-of-range auto-miss, 0x2000 forced-crit (unused Stage 1)
```

Table rows verified in research §2.4: rifle(0) 8–10 dmg range 0–5 bullets×1
NUMSHOTS 2 SHOTHOLD 50; shotgun(7) 25–30 shells×2; chaingun(8) 12–15 ×4
SHOTHOLD 15; imp(3,0) hp 50 armor 0 def 5 str 12 acc 95 agi 0; imp weakness
nibbles rifle 7 (×1.0), holy water 15 (×2.0).

`getWeaponWeakness(w, sub, parm) =
(monsterWeakness[(sub*3+parm)*8 + w/2] >> ((w&1)<<2) & 0xF) + 1 << 5`
(`src/Combat.cpp:29-31`; table 11 signed bytes).

`WorldDistToTileDist(n)`: first j<n16 with n < tileDistances[j], else 15
(`src/Combat.cpp:1235-1242`).

Chebyshev distance: `distFrom(e, x, y) = max((x−ex)², (y−ey)²)` using the
entity's sprite position from `mapSprites[sprite]`
(`src/Entity.cpp:1155-1158`; rewrite helper `Game::entityDistFrom` reads
`map_->mapSprites` like `traceEntityHits` does,
`new_src/domain/game/Game.cpp:481-486`).

Monster templates: `Combat::init` builds `monsterTemplates[51]` via
`CombatEntity(5*(monsterStats[i*6+0]&0xFF), monsterStats[i*6+1..5])`
(`src/Combat.cpp:37-39`). Spawn clones index `eSubType*3 + (int8_t)parm`
(`src/Entity.cpp:60`) then difficulty bump: difficulty==4 (any) or ==2
(non-boss) → `maxHP += maxHP>>2`, health follows
(`src/Entity.cpp:62-67`). Difficulty source: `ScriptVM::vars[12]` (rewrite
default 2, `new_src/core/GameContext.cpp:252`) read via `Game::difficulty()`
helper (vm_ null → 2).

RNG: `Combat::nextByte() = std::rand() & 0xFF` (`src/App.cpp:506-512`).

---

## 2. New files

### 2.1 `new_src/domain/game/EntityMonster.h`

Struct per decision B. Methods: `reset()` zeroes everything (ce.weapon=−1),
`resetGoal()` clears `goalType/goalFlags/goalTurns/target` (subset; the full
legacy body is irrelevant until aiThink lands, `src/EntityMonster.h:23-42`).

### 2.2 `new_src/domain/game/Combat.{h,cpp}`

```cpp
class Combat {
public:
    struct Env {                       // wired once from Main.cpp
        Game* game; Player* player; Hud* hud; Localization* loc;
        const Tables* tables; MapData* map;
        const int64_t* gameTime;       // ctx->gameTime
    };
    void init(const Env& env);         // builds tileDistances + monsterTemplates[51]

    // --- state (subset of src/Combat.h:61-139 used by the hitscan path) ---
    bool active = false;               // rewrite-only: "we are in ST_COMBAT"
    Entity* curTarget = nullptr;
    int targetType = 0, targetSubType = 0;
    EntityMonster* targetMonster = nullptr;
    int attackerWeaponId = -1, attackerWeapon = 0, attackerWeaponProj = 0;
    int stage = -1, nextStage = -1, nextStageTime = 0;
    int animStartTime = 0, animTime = 0, animEndTime = 0;
    int flashTime = 0; bool flashDone = false; int flashDoneTime = 0;
    int animLoopCount = 0;
    int damage = 0, totalDamage = 0, accumRoundDamage = 0, deathAmt = 0;
    int hitType = 0; bool gotCrit = false, gotHit = false, targetKilled = false;
    int crFlags = 0, crDamage = 0, crArmorDamage = 0, crHitChance = 0, crCritChance = 0;
    int worldDist = 0, tileDist = 0;
    int playerMissRepetition = 0, monsterMissRepetition = 0;
    bool shotsFired = false;           // sight-wake suppression flag for Stage 2
    int tileDistances[kMaxTileDistances] = {};
    std::vector<CombatEntity> monsterTemplates;   // 51 entries
    int loadMapID = 1;                 // miss-guard gate (b||mapID<8||tileDist>1); map00 ⇒ <8

    void performAttack(Entity* target, int attackX, int attackY, bool scripted);
    bool tick();                       // returns true while seq still running
    int  calcHitEntity(Entity* e);     // non-monster targets, src/Combat.cpp:864-883
    void explodeOnMonster();           // subset, src/Combat.cpp:885-946
    static int getWeaponTileNum(int n);// src/Combat.cpp:1766-1784
    short getWeaponWeakness(int w, int sub, int parm) const;
    int  worldDistToTileDist(int n) const;
    uint32_t nextByte();
private:
    int  playerSeq();                  // stage machine body
    Env env_;
};
```

`performAttack` ports `src/Combat.cpp:54-166` minus the melee-lunge block
(weapon 18 unreachable; keep the lines commented out with the citation).
Details: `info |= 0x200000` on both entities (bit has no other consumer yet —
store anyway for fidelity); `player->inCombat` analog skipped (no consumer);
`attackerWeaponId = player.ce.weapon`; `attackerWeapon = id*9`;
`animLoopCount = weapons[w*9+kFieldNumShots]` capped by
`ammo[ammoType]/usage` for the player (`:113-120`);
`attackerWeaponProj = weapons[w*9+kFieldProjType]`;
`worldDist = game.entityDistFrom(target, player.viewX, player.viewY)`;
`tileDist = worldDistToTileDist(worldDist)`; `stage = 0; nextStageTime = 0;
animEndTime = 0`; `active = true` (instead of `setState(ST_COMBAT)`, :165).

---

## 3. Modified files — domain layer

### 3.1 `CombatEntity.{h,cpp}` — the verbatim trio

Signatures (first param `Combat& c` replaces the implicit `app->combat`):

```cpp
void calcCombat(Combat& c, CombatEntity& attackerCe, Entity* targetEnt,
                bool vsPlayer, int worldDist, int targetSubParm);  // :130-155
int  calcHit(Combat& c, CombatEntity& attackerCe, CombatEntity& defenderCe,
             bool vsPlayer, int worldDist, bool zoomB2);           // :157-298
int  calcDamage(Combat& c, CombatEntity& attackerCe, Entity* targetEnt,
                CombatEntity& defenderCe, bool vsPlayer, int parm);// :300-378
```

Port rules, line-anchored:

* calcHit: omit ONLY the sniper-zoom pixel-bbox block (`:173-216`; log
  `[combat] zoom bbox path unsupported` if mask 0x200 ever seen) and the
  punch branches (`:233-238` crFlags 0x10, `:253-261` punchingMonster).
  Keep verbatim: range penalty `n7` (`:218-232`), fixed-range/punch-flag
  hard miss CR 0x400 (`:236-237`), `b2 || weapon==13` auto-hit (`:239-241`),
  agility scale `agi*96>>8` unless weapon mask 0x800 (`:242-246`),
  `crHitChance = ((acc − agi') << 8)/100 − 16*n7` floor 1 (`:247-251`),
  miss-streak guard (player limit 1 / monster limit 2, gated by
  `vsPlayer || loadMapID < 8 || tileDist > 1`, `:262-271`), dodge status
  branch OMITTED (Player has no statusEffects; leave a `// statusEffects[18]`
  comment at the site, `:272-278`), streak reset (`:279-284`), far-cap CR 0x4
  return (`:285-287`), crit chance `crHitChance/20` when `!vsPlayer ||
  difficulty==4` else 0, roll `< chance` → CR 0x2 else 0x1 (`:288-297`).
  NOTE the flag accumulation discipline: legacy OR-s into `c.crFlags` and
  early-returns WITHOUT clearing — `playerSeq` clears `crFlags` at stage-0
  entry (`src/Combat.cpp:196`); preserve exactly.
* calcDamage: verbatim `:300-378` with these omissions: buffs
  (`:311-313`, `:337-339`, `:367-370`) and familiar (`:357-366` → keep only
  the normal armor split `armorDamage = min(((171*dmg>>8)+1)/2, armor);
  damage = dmg − 2*armorDamage`), soul-cube ×2 kept (`:306-309`), difficulty-4
  cut BEFORE crit doubling kept (`:314-316`), strength bonus gated on CR 0x20
  with the monster-vs-player asymmetry `3*(str%*base>>8)` vs `(str%*base)>>8`
  (`:326-333`), weakness + holy-water special cases (`:340-354`), final
  `damage − ((def·256/100)*damage >> 8)` (`:372`).
* calcCombat: verbatim `:130-155` including the chainsaw `usedChainsaw(true)`
  hook REPLACED by a log (no chainsaw on map00) and the
  `damage==0 && crArmorDamage==0 → crFlags |= 0x100` rule (`:150-152`).

### 3.2 `Game.{h,cpp}` — monster payload + lifecycle + pain/died

New members/methods (all cited in §0.B):

```cpp
EntityMonster entityMonsters_[80]; int numMonsters_ = 0;
Entity* activeMonsters = nullptr; Entity* inactiveMonsters = nullptr;
Entity* combatMonsters = nullptr;         // Stage-2 queue head (declared only)
bool interpolatingMonsters = false;
Combat combat;

void activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4);
void deactivate(Entity* e);
void updateMonsters();                    // Stage-1 stub: endMonstersTurn()
void endMonstersTurn();
void snapMonsters(bool b);                // Stage-1 stub (see §0.B)
int  difficulty() const;                  // vm_->vars[12], default 2
static bool isBossDef(const EntityDef*); // eSubType in [FIRSTBOSS..LASTBOSS]
int  entityDistFrom(const Entity* e, int x, int y) const;
bool painMonster(Entity* e, int dmg, int attackerWeaponId);   // Entity::pain subset
void diedMonster(Entity* e, bool giveXP);                     // Entity::died subset
void awardKillXP(const EntityMonster& m);                     // XP + msg 103
const std::vector<std::pair<int,Entity*>>& lastTraceHits() const;
```

`loadEntities` ET_MONSTER branch (replaces the generic body for monsters,
keeping doors/corpses/NPCs untouched):

```
e.monster = &entityMonsters_[numMonsters_++]; e.monster->reset();     // src/Game.cpp:430-436
random art flip: if ((randByte & 1)==0 && !isBossDef) info |= 0x20000;// :438-441 (0x20000 = FLIP_HORIZONTAL)
initspawn monster half (src/Entity.cpp:59-81):
    combat.monsterTemplates[sub*3 + (int8_t)def->parm].clone(&e.monster->ce);
    difficulty bump (+25% hp, §1);
    z snap: mapSprites[S_Z] = 32            // rewrite stores raw-relative Z (see corpsifyMonster note, Game.cpp:637-640); legacy baked getHeight+32
    scale: mapSprites[S_SCALEFACTOR] = 64   // 42 for mastermind/pinky parm0 (:68-75)
    e.info |= kInfoActive;                  // 0x20000 (:77)
populateDefaultLootSet(e)                   // existing helper (:109-111)
common corpse/NPC tail as today, then linkEntity(...)
deactivate(&e);                             // onto inactive ring (:442-443)
stderr "[monster] spawn sprite=%d sub=%d parm=%d hp=%d/%d"
```

`painMonster` (non-boss subset of `src/Entity.cpp:281-394`):

```
if (!(e->info & kInfoActive)) return false;                        // :286-288
boss phase hooks 75/50/25%: DEFERRED (log once)                    // :293-339
int n2 = ce.getStat(0) - dmg;
if ((ce.flags & MFLAG_NOKILL) && n2 <= 0) n2 = 1;                  // :341-343
ce.setStat(0, n2);
if (n2 > 0) {
    stderr MSOUND_PAIN log                                         // :347-348 (no audio)
    spriteInfo |= 0x6000; m->frameTime = nowMs() + 250;            // :350-353
    if (attackerWeaponId != 2 /*holy water*/) m->resetGoal();      // :354-356
} else {
    spriteInfo |= 0x6000;                                          // :358-359
    m->frameTime = nowMs() + 450;                                  // :360-368 (250+200 lethal hold)
}
return false;
```

`nowMs()` = `lerpClock_` (Game's own animation clock; see Deviation D-6).

Pain auto-revert (legacy render-side `src/Render.cpp:1600-1604`, missing from
the rewrite stack renderer): add to `Game::update(dtMs)` a sweep over
`entities_`: if sprite anim byte ∈ {MANIM_PAIN, MANIM_DODGE} and
`lerpClock_ > monster->frameTime` → rewrite anim bits to 0 (IDLE). Gate on
`ent->monster != nullptr` and skip hidden sprites (mirror the existing walk
writer guards, `new_src/domain/game/Game.cpp:1086-1089`).

`diedMonster` (ET_MONSTER subset of `src/Entity.cpp:459-521`):

```
guard: if (!(info & kInfoActive)) return;                           // :431
info &= ~kInfoActive;                                               // :434
info |= kInfoActivated;                                             // :460
snap script lerps of this sprite (reuse corpsifyMonster's snap loop) // :462 (Game.cpp:620-631 pattern)
spriteInfo = (spriteInfo & 0xFFFF00FF) | 0x7000;                    // :463
if (hidden) spriteInfo |= 0x17000; else info |= 0x1020000;          // :465-471 (trimCorpsePile skipped)
deactivate(e);                                                      // :485
if (giveXP) awardKillXP(*e->monster);                               // :496 + :407-413
def = defs_->find(ET_CORPSE, eSubType, def->parm);                  // :501
Lost Soul/Cacodemon poof branch: DEFERRED (absent on map00 route)    // :491-495
facing-dirty latch (ctx hook — set via vm_/ctx pointer or a std::function; simplest: public bool Game::facingDirty flag GameContext polls)  // :527
stderr "[monster] died sprite=%d xp=%d"
```

`awardKillXP`: `xp = ce.calcXP()` (bosses +130 — unreachable), then
`player.addXP(xp)` and compose message str 103 with the xp arg via
Localization (`src/Player.cpp:264-281` split: state in Player, presentation
here). Message composition helper: copy the `%NN` `composeArgs` pattern from
`new_src/domain/game/Game.cpp:700-715` into Combat/Game shared static (one
copy in Game.cpp, declared in Game.h as a free function so Combat.cpp
reuses it).

`advanceTurn` additions (`src/Game.cpp:1238-1281`): at top, the Error-95
guard (log + `snapMonsters(true)`); haste-parity block left as a
`// statusEffects[2]` comment (monstersTurn = 1 always); player-side ticks
(poison/infection/combat decay) deferred with citations
(`src/Player.cpp:51-92`).

### 3.3 `Player.{h,cpp}`

```cpp
int disabledWeapons = 0;              // op 60 store (src/ScriptThread.cpp:1445)
Entity* facingEntity = nullptr;       // forward-declare Entity (Game.h includes Player.h — use pointer + fwd decl)
int xpGained = 0;
void addXP(int xp);                   // pure state: currentXP/xpGained, level-up loop (src/Player.cpp:264-281)
int  calcLevelXP(int n) const;        // 500n + 100((n−1)³ + (n−1))  (src/Player.cpp:360-362)
void addLevel();                      // subset: level++, nextLevelXP, baseCe MAX_HEALTH +10 clamp 999 (src/Player.cpp:283-307; DEF/STR/IQ modifyStat bumps deferred, logged)
bool fireWeapon(Combat&, Entity* target, int x, int y);   // §4 guards
```

`reset()` zeroes the new fields.

`fireWeapon` port (`src/Player.cpp:754-799`) in this order:

1. soul cube guard: `weapon == 13 && target->monster == nullptr` → false
   (`:757-759`).
2. ~~weaponDown / lower-raise lerp~~ — system absent; comment + skip
   (`:761-771`).
3. `disabledWeapons != 0 && (weapons & 1<<weapon) == 0` → false (`:761-763`).
   (This also rejects weapon == −1 / unowned: add explicit
   `weapon < 0 || !(weapons & 1<<weapon)` guard first — legacy UB guard.)
4. knockback-flag clear on monster targets: `flags &= ~MFLAG_KNOCKBACK`
   (`:773-775`; bit already defined Enums.h:288).
5. chainsaw-gib branch: unreachable (no chainsaw) — keep code path out, log
   if seen (`:777-779`).
6. ammo check: `usage > 0 && ammo[type] − usage < 0` → msgs 117 (soul cube) /
   115 (empty) / 116 (partial), return false (`:781-796`). Messages go
   through `env.hud->showCenterMessage(loc.get(kTextMain, N), ...)` — the
   rewrite's message-log stand-in.
7. `combat.performAttack(target, x, y, false); return true;` (`:797`).

### 3.4 `ScriptVM.cpp` — seven opcodes

All inside the existing dispatch switch (killer default stays for anything
else, `new_src/domain/game/ScriptVM.cpp:1252-1258`).

| Op | Contract | Cite |
|---|---|---|
| 10 WEAPON_EQUIPPED | `vars[byteArg & 0x7F] = player.ce.weapon;` | src/ScriptThread.cpp:477-482 |
| 19 DAMAGEMONSTER | resolve entity via findEntityBySprite; if monster: force `info \|= kInfoActive`, `game.painMonster(e, dmgVal, −1)`; if `hp <= 0` afterwards `game.diedMonster(e, false)`. Non-monster entity → `diedMonster` skipped, log (legacy dies it; our died is monster-only). | src/ScriptThread.cpp:704-724 |
| 28 WAKEMONSTER | entity = findEntityBySprite(sprite); null → log Err23 (no fatal); if ET_MONSTER: `frameTime = 0`, clear anim byte (`info & 0xFFFF00FF`), `game.activate(e, true, false, false, true)` (silent) | src/ScriptThread.cpp:876-892 |
| 51 AIGOAL | ushort `{goalType = v>>12 & 0xF, sprite = v & 0xFFF}` + byte param; entity null → log Err76; else: `m->resetGoal(); m->goalType = goalType;` `goalType ∈ {2,3} → goalParam = 1`, `{4,6} → goalParam = arg`; if not on active list → `game.activate(e, true, false, false, true)`; **`aiThink(true)` NOT ported → stderr `[script] AIGOAL aiThink deferred (Stage 2)`**; type 3 queued-attack re-run → log skipped (combatMonsters empty in Stage 1) | src/ScriptThread.cpp:1262-1274, :2226-2247 |
| 56 PLAYERATTACK | ushort `{weapon = v>>12 & 0xF, sprite = v & 0xFFF}`; entity found → `player.ce.weapon = weapon; game.combat.performAttack(entity, 0, 0, true);` then `n = evWait(t, 1);` (park 1 ms — seq plays out via the Playing tick) | src/ScriptThread.cpp:1361-1373 |
| 60 DISABLED_WEAPONS | `player.disabledWeapons = readShort(t);` if masked current weapon → stderr log (selectNextWeapon deferred) | src/ScriptThread.cpp:1443-1451 |
| 84 TARGETPRACTICE | uShort `{tx = v>>8 & 0x1F, ty = v>>3 & 0x1F, dir = v & 7}`; teleport player (view=dest=(tx<<6)+32/(ty<<6)+32, destZ=getHeight+36, snap angle per dir map {4:W,0:E,2:N,else:S}<<7), `finishRotationFired()`, park thread (`unpauseTime = -1; n = 2;`) storing it nowhere (exit protocol deferred); stderr `[targetpractice] enter tile=%d,%d dir=%d (inventory strip/score deferred)` | src/ScriptThread.cpp:1806-1813, src/Player.cpp:2521-2551 |

Op 19 replaces the current lethal-corpsify approximation
(`new_src/domain/game/ScriptVM.cpp:856-874`); keep the operand decoding.

---

## 4. Fire input path (`core/GameContext.cpp` `handlePlayingAction`)

Insert after the door branch (still inside `case Action::Use`), preserving
the built order loot → tile event → door → fire:

```
// ---- fire (legacy probe src/PlayingInputHandler.cpp:189-378) ----
int weapon2 = p.weapon;
if (weapon2 >= 0 && !game.combat.active) {
    // Election ray: DEVIATION — legacy probes ~6 units along the TinyGL view
    // matrix rows (:218-221); we sweep ONE TILE along the discrete view step
    // with mask CONTENTS_WEAPONSOLID (13997), radius 2, skipping the player
    // entity. Behavior-equivalent for adjacent-target election (research
    // open question 1); boundary stacks may differ.
    int tx = p.viewX + p.viewStepX, ty = p.viewY + p.viewStepY;
    Entity* hit = nullptr; int frac = 16384;
    bool clear = game.traceMove(map, p.viewX, p.viewY, tx, ty,
                                game.playerEntity(), CONTENTS_WEAPONSOLID, 2,
                                &hit, &frac);
    // classify per legacy candidate scan (:224-368):
    //  hit == nullptr                      -> air shot
    //  ET_MONSTER                          -> elected (wins immediately :264-269)
    //  ET_NPC and dist >= 8192             -> elected (:253-262)
    //  ET_WORLD / SPRITEWALL(12)           -> wall push (below) :229-235
    //  anything else                       -> log + treat as air shot
    int dist2 = hit ? game.entityDistFrom(hit, p.viewX, p.viewY) : 0;
    ...
    // wall push (within 1 tile, i.e. dist2 <= tileDistances[0]):
    //   legacy shiftWeapon(true)+rockView, NO turn consumed (:467-487).
    //   No lower/raise system yet -> stderr log only, no turn.
    // else:
    Entity* target = (elected ? hit : &game.entities()[0]);   // air shots target the WORLD slot (legacy entities[0], :515/:536)
    int ax = elected ? sprite X of target : p.viewX;          // legacy passes calcPosition/traceCollision coords
    if (p.fireWeapon(game.combat, target, ax, ay)) {
        // NO advanceTurn here — the seq completion in tickPlaying consumes the turn (§0.C)
    }
}
```

Rules the coder must not break:

* The election trace runs BEFORE the tile-event branch in legacy; we keep it
  AFTER (our tile events already preempt). Consequence: a faced trigger tile
  consumes the interaction even when a monster stands behind it — identical
  to legacy because executeTile returns first there too (`:395-404`).
* Loot still wins before everything (existing
  `findLootableCorpseFacing` branch, `new_src/core/GameContext.cpp:841-846`).
* Air/world shots MUST pass an entity whose `def == nullptr` safely:
  `calcHitEntity` treats `def == nullptr` as eType 0 (range gate applies,
  `info & 0x20000` clear → hitType 0) — add that guard explicitly
  (`src/Combat.cpp:864-883` never sees a null def; our entities_[0] has one).
* `shouldFakeCombat` (scripted combat reactions) is DEFERRED — no
  `doesScriptExist` API; log nothing (silent deviation, §11).
* Zoom entry (mask 512 → initZoom) deferred: log once per shot attempt.
* Holy-water pistol extra mask bits (0x4100) and chainsaw shrink (n7=1,
  +0x10) are dead on the map00 route — include as a one-line comment, not
  code.

ACTION_PASSTURN case (missing today): msg 45 via Localization +
`game.advanceTurn()` (`src/PlayingInputHandler.cpp:550-555`).

Input drop while `combat.active`: first line of `handlePlayingAction`.

---

## 5. `Combat::tick()` — the two-stage playerSeq timer

Faithful `playerSeq` (`src/Combat.cpp:178-425`) reduced to the hitscan path;
called once per Playing tick while `active` (§0.C). Head stage-advance check
first (`:182-187`):

```
if (stage == 0) {                                   // :188-366
    totalDamage/totalArmorDamage/hitType/deathAmt/gotCrit/gotHit/
    crFlags/damage/targetKilled = 0; flashTime = 1;  // :189-199
    if ((1 << attackerWeaponId & 0x77FF) == 0) crFlags |= 0x40;   // :204-206
    ammoPool = ammo[weapons[w*9+4]] / usage (usage>0)             // :207-212
    low-ammo warnings: pool==2 -> msg 62; 1<pool<5 -> msg 63 w/ arg pool−1
                      (only targetType != 2 && weapon != 14)      // :213-222
    if (targetType == ET_MONSTER) {                                // :223-247
        if (!(1 << attackerWeaponId & 0x2)) crFlags |= 0x20;       // :224-226
        player.ce.calcCombat(*this, player.ce, curTarget, false, worldDist, targetSubType);
        if ((crFlags & 0x1007) != 0) {                             // :228-246
            curTarget->info |= 0x4000000;
            crit -> gotCrit/hitType=2; hit|far -> hitType=1; gotHit=true;
            damage = crDamage; deathAmt = targetHp − damage;
            damage-stat counters: SKIP (run-stats absent)
        }
    } else {                                                       // :248-259
        hitType = calcHitEntity(curTarget);
        damage = STRMIN (+ rand%(STRMAX−STRMIN)); diff4 cut;
    }
    // per-weapon fire sound ids {3,5,9}:1086 {2}:1045 {7}:1117 {0,8}:1014 …
    //   -> stderr "[combat] fire sound %d" (no audio backend)     // :260-308
    totalDamage += damage; totalArmorDamage += crArmorDamage;      // :309-310
    animTime = weapons[w*9+8] * (haste? 5 : 10);                   // :311-317 (no haste → ×10; leave comment)
    animStartTime = *env.gameTime; animEndTime = start + animTime; // :318-319
    flashDone = false; flashDoneTime = start + flashTime;          // :320-321
    if (usage>0 && ammoPool < animLoopCount) animLoopCount = ammoPool;  // :322-324
    launchProjectile(): proj==0 → NOTHING allocated, exploded=true  // :325, src/Combat.cpp:1572-1576
    rockView: SKIP (camera-rock system absent; comment)            // :326-332
    if (totalDamage == 0 && targetType == 2) {                     // :333-345
        crFlags&0x100 -> msg 59; elif 0x400 -> msg 64; elif !(crFlags&1) -> msg 68;
    } else if (targetType == 2) accumRoundDamage += totalDamage;   // :346-348
    else if (hitType != 0) targetKilled = true;                    // :349-351
    stage = -1;                                                    // :352
    explodeOnMonster() [inlined updateProjectile call]             // :361 + :1421-1430
    ammo[type] -= usage; clamp ≥ 0                                 // :355-358
    nextStage = 1; nextStageTime = animEndTime;                    // :359-360
    if (totalDamage == 0 || hitType == 0) ++missCounter (log only) // :363-365
}
else if (stage == 1 && *gameTime >= nextStageTime) {               // :368-416
    if (targetType == 2) curTarget->info &= ~0x4000000;            // :372-374
    if (targetKilled || (targetType == 2 && targetHp <= 0))
        game.diedMonster(curTarget, true);                         // :382-386
    else if (--animLoopCount > 0 && targetType ∉ {0,5,8,12} && targetType != 10)
        { stage = 0; animTime = animEndTime = 0; nextStageTime = 0; return true; }  // :387-392
    if (targetType == 2 && accumRoundDamage != 0 && (curTarget->info & 0x20000)) {
        msg = loc(kTextMain,70) if gotCrit  ++  loc(kTextMain,71) w/ arg accumRoundDamage
        hud->showCenterMessage(msg, ...)                           // :401-410
    }
    weapon-14 unequip-back: SKIP (comment)                         // :411-414
    return false;                                   // seq done → caller advances turn
}
return true;
```

`explodeOnMonster` Stage-1 subset (`src/Combat.cpp:885-946`):

```
shotsFired = true (unless weapon==1-chainsaw && hitType==0 — dead code here)  // :891-894
if (targetType==2 && targetMonster && !(curTarget->info & 0x40000))
    game.activate(curTarget, true, false, true, true);                        // :895-897
if (hitType == 0) return;                                                     // :898-903
if (targetType == 2 && totalDamage > 0) {
    checkMonsterFX: SKIP (status effects absent; comment)                     // :168-176, :906
    game.painMonster(curTarget, totalDamage, attackerWeaponId);               // :907
    blood particles: SKIP (no particle system)                                // :909
    knockback/radiusHurtEntities/negative-damage healing: SKIP                // :911-931
}
targetType 10 pain / 9 gib branches: DEFERRED (log)                           // :933-945
```

---

## 6. View weapon + HUD (render/ui group)

### 6.1 `World3D` public accessor

```cpp
// Returns the cached (uploading lazily) texture for mediaMappings[tileNum]+frame.
const Texture* spriteTexture(const MediaLoader& media, int tileNum, int frame);
```

Implementation: resolve `mediaId = mappings().mappings[tileNum] + frame`
(bounds-checked), delegate to the existing private
`ensureSpriteTexture(media, tileNum, mediaId)`
(`new_src/render/World3D.cpp:323-355`), return `&spriteTexByMedia_[mediaId]`
or nullptr.

### 6.2 `GameContext::drawViewWeapon(Graphics2D&)` (new private method)

Port of `Combat::drawWeapon` GL-path anchors
(`docs/original-code/combat.md` §1 = `docs/research/2026-08-26-hero-choice-and-weapon.md`
§B.1; legacy `src/Combat.cpp:621-844`):

```
gate: state ∈ {Playing, Looting, Dialog}; player.weapon >= 0; player.weapons != 0   // :706-708
int w = p.weapon;
int scrX = 196, scrY = 131;                        // 480/2−44, 320/2−29  (:627-628)
// weaponDown/lower-raise lerp: absent — skip scrY += 38            (:672-674)
per-weapon bias: scrY += (w==1 ? 3 : w==2 ? 10 : (w>=3&&w<=6) ? 12 : 0)  // :679-693
int wpX, wpY;                                      // idle or attack pair:
bool attacking = game.combat.active && game.combat.attackerWeaponId == w;
if (attacking) {
    // hold (atkX,atkY) until flashDone, then lerp back over animTime (16.16):
    elapsed = *gameTime − animStartTime;
    t = clamp(elapsed − flashTime, 0, animTime) * 65536 / max(animTime,1);
    wpX = atkX + ((idleX − atkX) * t >> 16);  wpY likewise          // :735-767
} else { wpX = idleX; wpY = idleY; }
wpX/wpY = signed bytes tables.weaponInfo[w*6 + {0,1}] idle, {2,3} attack  // src/Combat.h:53-59
int x = scrX + wpX + shakeX, y = scrY − (wpY + shakeY);   // TOP-LEFT of 176×176  (:695-696)
const Texture* tex = world.spriteTexture(media, getWeaponTileNum(w), 0);
if (tex) g.drawImage(*tex, 0, 0, tex->width(), tex->height(), x, y, 176, 176, 0);
// muzzle flash: attacking && (1 << w) & 0x181 ({0,7,8}) →
//   tile 1 frame 3 at (x+flashX+40, y+flashY+40), 88×88 (half scale)      // :826-834
```

Call site: `GameContext::render` right after the `drawBSP` block (before the
cinematic viewport restore / overlays) — hero-choice doc §B.3 item 3 names
this exact hook (`new_src/core/GameContext.cpp:1023-1027`). Sentry-bot
stacks, weapon-14 special case, weapon-9 underlay: deferred (hero-choice doc
§B.3 exclusions).

### 6.3 `Hud` changes

* `void feedMonsterHealth(int id, int hp, int maxHp);` — replaces
  `setDemoMonster` (delete it; no callers). Internals: `monsterId_` latch;
  on change snap `displayHp_`; otherwise ease `displayHp_` toward `hp` by
  `250 ms` worth per `update()` tick (drain animation, legacy
  `src/Hud.cpp:822-899`).
* `drawTopBar` loses its message block (stays in `drawMessages`); health bar
  call becomes `drawMonsterHealth(g, 240, 26)` (y = viewRect[1]+6).
* `drawMonsterHealth` reads `displayHp_` instead of `monsterHp_`; segment
  math stays as ported (`new_src/ui/Hud.cpp:220-249`).
* GameContext::render: `hud->feedMonsterHealth(id, hp, maxHp)` each frame —
  id = `facingEntity->getSprite()` when facing a LIVE ET_MONSTER
  (`monster != nullptr && hp > 0`), else −1; then
  `if (state ∈ {Playing, Looting, Dialog}) hud->drawTopBar(g, *font, 480);`
  before the existing `drawMessages`.

---

## 7. Coder groups (≤3, build-green checkpoints)

### GROUP 1 — domain core (compiles green, boots, scripts unblocked)

Files: NEW `domain/game/EntityMonster.h`, NEW `domain/game/Combat.{h,cpp}`;
EDIT `domain/game/CombatEntity.{h,cpp}`, `domain/game/Game.{h,cpp}`,
`domain/game/Player.{h,cpp}`, `domain/game/ScriptVM.cpp` (ops 10/19/28/51/56/
60/84), `core/Main.cpp` (construct-order-safe `game.combat.init({...})` next
to `vm.init`, Main.cpp:226-233), `core/GameContext.cpp` MINIMAL hooks:
tickPlaying step-4 replacement (`combat.tick` / `updateMonsters`) +
`combat.active` input-drop + tickLoading `endMonstersTurn()` swap.

Verify: build green; boot reaches ST_PLAYING; stderr shows
`[monster] spawn ... hp=62/62` (imp 3,0 at difficulty 2: 50 + 50>>2 = 62);
PER_TURN corpsified imps 41/42 still work (op 19 now goes through
pain/died); no regressions in door/loot flows.

### GROUP 2 — fire path + facing feed (playable shooting)

Files: EDIT `core/GameContext.{h,cpp}` (fire election in Action::Use,
ACTION_PASSTURN, facing probe latch + probe, difficulty-order fix: move
`vars[12] = 2` before `loadEntities` in tickLoading), EDIT
`domain/game/Game.{h,cpp}` (`lastTraceHits()` accessor, facing-dirty flag),
`domain/game/Player.{h,cpp}` if not landed in G1.

Verify: build green; headless smoke `D2R_AUTOTEST` unaffected; manual: E
near wall logs wall-push, E in open air spends 2 rifle bullets per shot
(stderr `[combat]` lines), messages appear.

### GROUP 3 — view weapon + HUD bar

Files: EDIT `render/World3D.{h,cpp}` (accessor), `ui/Hud.{h,cpp}`
(feed/topBar changes), `core/GameContext.cpp` (drawViewWeapon + render
wiring + feed call).

Verify: build green; full user-eyes acceptance below.

CMake uses GLOB — reconfigure after the two NEW files
(`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`), then
`cmake --build build_new -j 8`.

---

## 8. Acceptance criteria (USER EYES)

1. After EVT 617 ("You equipped your weapon.") the assault-rifle art appears
   center-right at mid-height and tracks screen shake.
2. Walking to the target-practice range and pressing E at an imp: rifle
   kicks to the attack offset and eases back (~500 ms), muzzle-flash starburst
   overlays briefly; message log prints "… took N damage" (crit prefix when
   rolled); imp flashes the pain pose (~250 ms) then resumes idle.
3. Misses print the miss message (68) and still spend ammo; repeated misses
   never streak beyond the legacy guard (second consecutive roll above
   chance auto-hits).
4. The imp near the elevator area dies after ~4 hits (62 hp, 8–10 base ×2
   shots, weakness ×1.0): corpse pose (lying frame) appears instantly at
   death, XP message fires, and the health bar disappears.
5. The corpse pulses (loot halo is renderer-deferred — accept: corpse is
   lootable) and E over it opens the loot UI with its loot set.
6. While aiming at a live imp, the segmented health bar shows under the top
   panel at y≈26, draining red→orange→green thresholds as damage lands.
7. Shooting a previously inactive imp wakes it: stderr activation log; it
   stays put (Stage-1 contract) but remains shootable; fights near the
   elevator are winnable with zero incoming damage.
8. No thread-death stderr (`UNIMPLEMENTED opcode`) anywhere on the route
   through the elevator area and past VIOS scripts (ops 10/51/56/84 handled).

---

## 9. Deviations (all deliberate, cited)
<!-- SUPERSEDED items: deviation 1 (single-hit election ray) and the view-weapon
placement notes — see spec 2026-08-26-combat-stage1-fixes.md §2 and §3. -->

1. **Election ray**: one swept tile along the discrete view step vs the
   legacy ~6-unit matrix-row probe (research open question 1). Adjacent-
   target election equivalent; stacked-candidate boundaries may differ.
2. **No ST_COMBAT StateId** — timer inside Playing (§0.D).
3. **Same-tick turn consumption** after the seq (legacy: next frame,
   `src/GameStateRunner.cpp:26-38`).
4. **Facing probe geometry**: one tile from dest along viewStep vs legacy's
   matrix-row ray (`src/MovementController.cpp:38-40`); monster-preference
   rescan reduced to the sorted-hit-list scan.
5. **shouldFakeCombat / explodeThread**: not triggered by shots (no
   `doesScriptExist`); ranged-modifier tile events therefore don't fire from
   combat yet.
6. **Clocks**: combat timers on `gameTime` (legacy `app->gameTime`), pain
   `frameTime` on `Game::lerpClock_` (renderer-visible animation clock);
   legacy used one `app->time` for both. Pain auto-revert moved from render
   side (`src/Render.cpp:1600-1604`) to a `Game::update` sweep.
7. **rockView recoil camera** and screen shake on fire: skipped (system
   absent); view-weapon kick conveys the shot.
8. **Sounds**: all combat sounds logged as ids (no audio backend).
9. **Blood particles / corpse gib / drop items**: skipped (systems absent);
   targetType 9/10 branches log.
10. **difficulty source**: `vars[12]` default 2 read at spawn time; legacy
    carried RMS/save difficulty (`src/LoadingManager.cpp:692` region).
11. **op 60 selectNextWeapon** skipped (single weapon on route).
12. **op 84 inventory strip/score/exit** deferred; thread parks (legacy
    would resume after target practice). Unreachable content on the current
    playtest route.
13. **entities_[0] air-shot target**: rewrite slot has `def == nullptr`;
    `calcHitEntity` guards it to the eType-0 path (legacy world entity has a
    def; research open question 2 resolved by construction).
14. **XP messaging split**: state in `Player::addXP`, presentation
    (msg 103) in `Game::awardKillXP`; `addLevel` stat bumps limited to
    maxHealth +10 (`modifyStat` system deferred).

## 10. Out of scope (deferred with research §6 rationale)

Projectiles/proj≠0 weapons (grenades, rockets, plasma, BFG, soul cube),
monster movement/pathfinding/AI goals in action, monster attacks
(`monsterSeq`/`explodeOnPlayer`/armor UI), wake-on-sight during render,
interpolatingMonsters + snap discipline in earnest, zoom/sniper bbox +
target-practice scoring, bosses/phases/staticFuncs 2-5, audio backend,
knockback, fear/flee, sentry bots/familiar, weapon-switch UI, status
effects (haste parity, poison, infection, dodge), floating damage numbers
(don't exist in legacy either — research §4).

## 11. Stage-2 handoff notes

`combatMonsters` head + `nextAttacker` chain, `interpolatingMonsters` +
`snapMonsters` real bodies, `updateMonsters` AI iteration
(`src/Game.cpp:874-913`), `monsterSeq` + `explodeOnPlayer`
(`src/Combat.cpp:427-610, 948-1055`), wake-on-sight hook point
(`src/Render.cpp:1576-1583`), `prepareMonsters` respawn
(`src/Game.cpp:701-727`) — all data structures this spec adds are shaped for
those ports; no re-plumbing expected.
