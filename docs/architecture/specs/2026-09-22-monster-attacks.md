# Spec 2026-09-22 — Monster attacks (turn pipeline, attack execution, player damage, movement, missiles, death)

Status: ready for implementation, group by group
ADRs: [0027](../adr/0027-player-is-a-linked-entity.md), [0028](../adr/0028-monster-ai-module-and-attack-sequencing.md),
[0029](../adr/0029-render-visibility-feedback.md), [0030](../adr/0030-runtime-sprite-slots.md);
builds on ADR 0017 (projectile machine, default arm first), ADR 0018 (pain/died dispatch).
Verified facts (read these, they are the ground truth for every formula below):
`docs/original-code/monster-turn.md` (MT), `docs/original-code/monster-attack.md` (MA),
`docs/original-code/player-damage.md` (PD), `docs/original-code/monster-movement.md` (MM),
`docs/original-code/combat.md` (C).

---------------------------------------------------------------------------------------------
## 0. Scope and ordering

Today monsters wake (shot / script), take pain and die, but never think, move or attack
(`new_src/domain/game/MonsterSystem.cpp:141-157` closes the monster turn immediately).

On map00 the census is imp (3,0) ×20, zombie (0,0) ×15, zombie (0,1) ×3, imp (3,1) ×2
(MA §7). Imp (3,0) = CLAW 16 (melee, 1 tile); zombies = FLESH 19 (projType 6, 1-3 tiles);
imp (3,1) = CLAW 16 or FIREBALL 20 (projType 7, 1-3 tiles). Per ADR 0017 the projectile
machine's `default:` arm already treats every not-yet-ported positive `PROJTYPE` as instant, so
**zombies and fireball imps attack (instant damage, no flying sprite) from G3 on**; the flying
sprite arrives in G7. That is why missiles are not a prerequisite for the first visible milestone.

| Group | Delivers | Needs | New files (⇒ CMake reconfigure) |
|---|---|---|---|
| **G1** | Player entity linked at its destination (ADR 0027) | — | — |
| **G2** | Monster turn pipeline + decision + attack queue; attack sequence is a stub that ends at once | G1 | `domain/game/MonsterAI.{h,cpp}` |
| **G3** | Real attack sequence (poses, timing), hit/damage to the player, HUD health, combat messages, death → existing Dying stub | G2 | — |
| **G4** | Damage feedback: vignette, arrow, weapon red tint, rock-back, pain sound log, `EV_DAMAGEPLAYER` | G3 | — |
| **G5** | Sight wake from the rendered frame (ADR 0029) | G2 | `domain/game/ViewVisibility.h` |
| **G6** | Monster movement: path search, step lerp, walk frames, doors, monster clip, input gate snapping | G5 | — |
| **G7** | Runtime sprites (ADR 0030) + missiles 6/7 + impact + claw marks | G3 | `domain/game/GameSprites.{h,cpp}` |
| **G8a** | Death sequence (fall, fade) + dead menu | G3 | — |
| **G8b** | "Restart Level" = full level reload | G8a | — |

Delegate strictly in this order: G1, G2, G3, then G4 / G5 in any order, G6 after G5, G7, G8a,
G8b. Each group builds and runs on its own; each ends with a user check.

---------------------------------------------------------------------------------------------
## 1. Conventions used by every group

### 1.1 Clocks (all ms)

| Legacy | Rewrite | Used for |
|---|---|---|
| `app->gameTime` | `*Combat::env().gameTime` (= `GameContext::gameTime`, advances in Playing/Camera/Looting) | `nextStageTime`, `animStartTime`, `animEndTime` |
| `app->time` for `monster->frameTime` | `Game::lerps.clockMs()` (spec combat-stage1 deviation D-6: monster pose clocks are on the SpriteLerps clock) | monster `frameTime` reads/writes |
| `app->time` for HUD/camera effects (`damageTime`, `rockViewTime`, `shakeTime`, gsprites) | `GameContext::upTimeMs` (new `Combat::Env::upTimeMs`, G4) | damage feedback, rock, shake, runtime sprites |
| `LerpSprite::startTime` | `SpriteLerps::clockMs()` | monster step lerps (G6) |

All three advance by exactly 15 ms per Playing tick, so the legacy mixing is preserved.

### 1.2 Random numbers

`Combat::nextByte()` (= `std::rand() & 0xFF`, `new_src/domain/game/Combat.cpp:47-49`) is the
only source for new rolls; each roll sits at the legacy call site and nowhere else. New sites:
weapon choice when both weapons are usable (`src/Entity.cpp:835-836`), path-search tie order
(`src/Entity.cpp:946-948`), the monster hit/crit/damage rolls inside the existing
`calcHit`/`calcDamage`, and `dodgeDir` (missile path only, ADR 0028 decision 5; `nextInt` =
`std::rand()`). `getMonsterSound` is called for attack sound types only, which never roll.

### 1.3 Masks (add to `Contents` in `domain/game/Enums.h`, each pinned by `static_assert`)

| Name | Value | Composition | Legacy use |
|---|---|---|---|
| `MONSTER_ATTACK_LOS` | 5295 | `WORLD|PLAYER|MONSTER|NPC|DOOR|DECOR|ATTACK_INTERACTIVE|SPRITEWALL` | `src/Entity.cpp:768`, `:656` |
| `MONSTER_ATTACK_CLEAR` | 15791 | `MONSTER_ATTACK_LOS|ENV_DAMAGE|MONSTERBLOCK_ITEM|NONOBSTRUCTING_SPRITEWALL` | `src/Entity.cpp:780` |
| `BOSS_ATTACK_LOS` | 4131 | `WORLD|PLAYER|DOOR|SPRITEWALL` | `src/Entity.cpp:709`, `:757` |
| `MONSTER_SIGHT` | 5293 | `MONSTER_ATTACK_LOS & ~PLAYER` | `src/Render.cpp:1579` |

`MONSTERSOLID` (15535) already exists. Also add to `Enums`:
`EVADING_MONSTERS = 0x4604`, `FEAR_IMMUNE_MONSTERS = 0x1F801`, `MOVE2ATTACK_MONSTERS = 0x180D0`
(`src/Enums.h:82-84`), `MSOUND_ATTACK1 = 3`, `MSOUND_ATTACK2 = 4`, `MSOUND_NONE = 255`,
`MSOUND_TYPES = 8` (`src/Enums.h:85-95`), `CR_HITMASK = 0x1007` (`src/Enums.h:977`),
`MELEE_WEAPON_MASK = 0x78002` (ids 1,15,16,17,18, `src/Entity.cpp:784-791`),
`TILENUM_MONSTER_IMP = 23`, `TILENUM_MONSTER_IMP3 = 25` (`src/Enums.h:671-673`),
`LS_FLAG_ENT_NORELINK = 0x10` (G6, `src/Enums.h:297`).

### 1.4 Named bits (add; no bare literals in new code)

* `Entity::kInfoOnScreenStep = 0x10000000` — "this step lerp is on screen, cannot be snapped"
  (MT §1.3).
* `Entity::kInfoCombatMark = 0x200000` — write-only in the original (set by `performAttack`,
  `src/Combat.cpp:66-71`); keep the existing literal comment style if preferred, but one name.
* In `EntityMonster.h`: `kGoalFlagLerping = 0x1`, `kGoalFlagRetreat = 0x8`,
  `kGoalFlagRepath = 0x10` (MT §1.3), goal type constants `kGoalNone=0, kGoalMoveTile=1,
  kGoalChase=2, kGoalAttack=3, kGoalFlee=4, kGoalRetreat=5, kGoalWait=6` (MT §3.2; names are
  ours, values legacy).
* `Enums::MFLAG_ATTACKING` (0x400), `MFLAG_NOTHINK` (0x20), `MFLAG_ABILITY` (0x1),
  `MFLAG_KNOCKBACK` (0x1000) already exist.

### 1.5 What stays deferred in every group (do not implement, keep a one-line log where hit)

Player buffs/status effects (reflect `buffs[0]`, antifire `buffs[9]`, fear `buffs[11]`, burn
status 13, dodge status 18, haste `statusEffects[2]` → `monstersTurn` is always 1), familiar /
sentry-bot player, knockback (`knockbackDist` is always 0), weapon 18 CHARGE lunge lerps
(`src/Combat.cpp:131-164`, `:535-551`), arch-vile raise, Vios teleport, boss HP scripts, frozen /
poison / fire monster FX (`updateMonsterFX`), ENV_DAMAGE arrival effects, monster sounds (logged
ids only — there is no audio backend), `EV_AIGOAL` from `ST_CAMERA` starting a fight mid-cinematic
beyond what the code below does, the `goalFlags 0x10` repath branch (dead in `src/`, MM §4).

---------------------------------------------------------------------------------------------
## 2. G1 — The player entity (ADR 0027)

### 2.1 Files and changes

**`domain/game/Game.h/.cpp`**

* In `loadEntities`, right after `db.resetEntities()` (`new_src/domain/game/Game.cpp:33`):
  ```
  Entity* pe = db.playerEntity();
  pe->def  = defs.find(Enums::ET_PLAYER, 0);      // src/Game.cpp:366-367
  pe->info = Entity::kInfoActive;                 // src/Game.cpp:369 (0x20000)
  ```
  If `find` returns `nullptr`, log `[spawn] ERROR no ET_PLAYER def` once and continue (monster
  traces will then never hit the player — the log makes that visible).
* Store the `Player*` handed to `setXPSystems` in a new private `Player* player_` (it is
  already passed there; G2 needs it).
* New public method:
  ```
  // Player::relink (src/Player.cpp:1425-1445): unlink entities[1] if linked, then link it at
  // (destX>>6, destY>>6) when 0 <= destX,destY <= 2047; mirror the destination into the trace.
  void relinkPlayer(const Player& p);
  ```
  Body: `db.unlinkEntity(pe)` when `pe->info & kInfoLinked`; range check; `db.linkEntity(pe,
  p.destX >> 6, p.destY >> 6)`; `trace.setPlayerDest(p.destX, p.destY)`.

**`domain/game/EntityDb.cpp`**

* `resetEntities()`: additionally `std::fill(entityDb_, entityDb_ + 1024, nullptr)` (update the
  header comment that says heads are not cleared).
* `findEntityBySprite(int sprite)`: `if (sprite < 0) return nullptr;` first.

**`domain/game/TraceSystem.h/.cpp`**

* `void setPlayerDest(int x, int y)`, members `playerDestX_ = -1, playerDestY_ = -1`,
  accessors `playerDestX()/playerDestY()`.
* `traceEntityHits` `ET_PLAYER` branch (`TraceSystem.cpp:132-137`): `cx = playerDestX_;
  cy = playerDestY_;` (`src/Game.cpp:238-241`). Replace the "Deviation/unreachable" comment.
* `distFrom(const Entity* e, ...)`: new branch before the sprite read — `e->def->eType ==
  ET_PLAYER` → Chebyshev² to `(playerDestX_, playerDestY_)` (`src/Entity.cpp:1379-1382`).

**Call sites of `relinkPlayer`** (all right after the destination is written):

| Site | Legacy |
|---|---|
| `PlayerActions::spawnPlayer`, end (`new_src/core/PlayerActions.cpp:39`; drop the "not linked" note) | `src/Game.cpp:969` |
| `PlayerActions::handleAction` Forward/Back, right after `p.attemptMove(tx, ty)` + height (`PlayerActions.cpp:153-156`) | `src/MovementController.cpp:348` |
| `ScriptVM` `EV_GOTO`, after the dest writes, for both the animated and the snap branch (`ScriptVM.cpp:1267-1301`) | `src/ScriptThread.cpp:656` |
| `ScriptVM` target-practice teleport (`ScriptVM.cpp:787`) | none — **deviation D1** (legacy leaves a stale player link there; we relink so no phantom player entity blocks a tile) |

**`core/GameContext.cpp` `tick()`**: next to `trace.setPlayerPos(viewX, viewY)`
(`GameContext.cpp:222`) add `trace.setPlayerDest(p.destX, p.destY)` (legacy reads the live
`canvas->destX`, so the trace must never lag a dest write that forgot to relink).

**2.1a Difficulty default (user decision 2026-09-23, §12 Q1).**
`new_src/core/GameContext.cpp:316`: `sys_.vm->vars[12] = 1;` (legacy field default 1,
`src/Game.cpp:43`). Update the comment above it (the "+25% hp at difficulty 2" note): at 1 the
spawn-time monster HP bump does not apply. No other code change; every `difficulty()` reader
already branches on the value. G3 acceptance numbers below that say "difficulty 2 math" are
superseded: at difficulty 1 there is no ×1.5 + mapID bonus (`src/Combat.cpp:984-990`) — imp
claw ≈ 5 per hit, zombie throw ≈ 8-10.

### 2.2 Acceptance

* Build clean. Startup log shows one line `[spawn] player entity def=<ptr> tile=x,y` (add it in
  `relinkPlayer` for the spawn call only, or log in `loadEntities` + first relink).
* Monsters take fewer shots to kill than before (no +25 % HP at difficulty 1).
* User on screen: walking, turning, doors (open/close), shooting a monster, looting, item pickup,
  the scripted greeter NPC and the intro all behave exactly as before. Nothing visible changes.

---------------------------------------------------------------------------------------------
## 3. G2 — Monster turn pipeline, decision, attack queue

### 3.1 New module `domain/game/MonsterAI.h/.cpp` (ADR 0028)

```
class MonsterAI {
public:
	struct Env {
		EntityDb* db = nullptr;
		MapData* map = nullptr;
		TraceSystem* trace = nullptr;
		Combat* combat = nullptr;          // weaponDef, getMonsterField, tileDist math, nextByte
		MonsterSystem* monsters = nullptr; // attack() / undoAttack() / interpolatingMonsters
		SpriteLerps* lerps = nullptr;      // G6
		DoorSystem* doors = nullptr;       // G6
		const Player* player = nullptr;    // viewX/viewY/destX/destY
		const ViewVisibility* visibility = nullptr; // G6 (nullptr until G5 exists)
	};
	void init(const Env& env);   // per level: also rebuilds baseVisitedTiles (§3.1.6)

	void think(Entity* e, bool b);                    // Entity::aiThink        src/Entity.cpp:673-685
	int  weaponForTarget(Entity* e, Entity* target);  // Entity::aiWeaponForTarget src/Entity.cpp:687-850
	bool isAttackValid(Entity* e);                    // Entity::aiIsAttackValid  src/Entity.cpp:644-671
	void finishLerp(Entity* e);                       // Entity::aiFinishLerp     src/Entity.cpp:873-882 (G6)

	// Static monster clip (Game::baseVisitedTiles, src/Game.h:153; MM §3).
	void setMonsterClip(int x, int y);                // src/Game.cpp:3656-3658 (G6 wires the script op)
	void unsetMonsterClip(int x, int y);              // src/Game.cpp:3660-3662
	bool monsterClipExists(int x, int y) const;       // src/Game.cpp:3664-3666
	bool baseVisited(int x, int y) const;             // bit test used by weapon 18 and G6

private:
	bool isValidGoal(Entity* e);                      // src/Entity.cpp:605-642
	void chooseNewGoal(Entity* e, bool b);            // src/Entity.cpp:596-603
	void calcSimpleGoal(Entity* e, bool b);           // src/Entity.cpp:535-566
	void moveToGoal(Entity* e);                       // src/Entity.cpp:572-594
	bool goalMove(Entity* e);                         // src/Entity.cpp:1030-1116 — G2: stub, G6: real
	Env env_;
	uint32_t baseVisitedTiles_[32] = {};
};
```

Player position helpers inside the .cpp: "target position" of the player = `player->viewX/viewY`
(`src/Entity.cpp:692-697`); "destination" = `player->destX/destY` (`Game::destX` =
`canvas->destX`, `src/Game.cpp:863`). Monster position = `mapSprites[S_X/S_Y + sprite]`.
`Entity* playerEnt = env_.db->playerEntity()`.

#### 3.1.1 `think(e, b)` — verbatim (MT §3.1)

```
m->flags &= ~MFLAG_ATTACKING;
if (!(m->flags & MFLAG_NOTHINK)) {
    if (!isValidGoal(e)) chooseNewGoal(e, b);
    ++m->goalTurns;
    moveToGoal(e);
}
```

#### 3.1.2 `isValidGoal` — verbatim port of `src/Entity.cpp:605-642` (MT §3.2 table)

`goalTurns >= 16 || goalType == 3 || goalType == 0` → false. Type 2 with `goalParam != 1`: entity
`k = &db->entities()[goalParam]`, valid iff `k->linkIndex != e->linkIndex` and the first entity on
`k`'s tile matching mask 15535 is `nullptr` or `k` (helper `firstOnTile(tx, ty, mask)`, walking
`findMapEntity` with `EntityDb::TileWalk`). Type 1: valid iff `linkIndex != goalX + goalY*32`.
Types 4/5: `goalTurns < goalParam`. Type 6: `<` → true; `==` → `resetGoal()`, false. Else false.

#### 3.1.3 `chooseNewGoal` / `calcSimpleGoal` — verbatim (MT §3.3)

```
chooseNewGoal: sub = def->eSubType; m->resetGoal(); calcSimpleGoal(e, b);
               if ((1 << sub) & EVADING_MONSTERS && m->goalType == 3) m->goalFlags |= kGoalFlagRetreat;
calcSimpleGoal:
    if (sub == 10 /*arch-vile*/) { log "[ai] arch-vile raise deferred" once; }   // aiCalcArchVileGoal -> false
    // fear (player buffs[11]) — no buffs in the rewrite: branch absent (§1.5)
    int w = weaponForTarget(e, playerEnt);
    if (w != -1) { m->ce.weapon = w; goalType = 3; goalParam = 1; if evading: flags |= MFLAG_ABILITY; }
    else         {                   goalType = 2; goalParam = 1; if evading: flags &= ~MFLAG_ABILITY; }
```

#### 3.1.4 `moveToGoal` — verbatim (`src/Entity.cpp:572-594`)

Types 1/2/4/5 → `goalMove(e)`. Type 3 → `m->target = (goalParam == 1) ? nullptr :
&entities[goalParam]; env_.monsters->attack(e)`. Subtype 13 writes its position to
`vm->vars[9..10]` — deferred (boss, no VM in this Env; log once).

**G2 `goalMove` stub**: `return false;` plus a per-call debug log
`[ai] spr=%d chase tile=%d,%d (movement: G6)`. No position change.

#### 3.1.5 `weaponForTarget(e, target)` — verbatim port of `src/Entity.cpp:687-850`

Read the code there; the structure (MT §3.4, MA §2):

```
(tx,ty) = target is ET_PLAYER ? (player->viewX, viewY) : target sprite xy   // :692-702 (see note)
sx,sy   = e sprite xy; a = tx - sx; a2 = ty - sy
sub 13 (boss pinky): off-axis → -1; trace(sx,sy, targetPos(dest for player), e, BOSS_ATTACK_LOS, 2);
                     nearest hit entity == target → getMonsterField(def,0) else -1       // :704-714
sub 15 (Vios) teleport pre-step: deferred, log once                                    // :716-745
off-axis (a != 0 && a2 != 0):
    sub 10, or sub 15 with parm != 0: f = field(sub==15 ? 1 : 0); td = tileDist(distFrom(target, sx, sy));
        RANGEMIN(f) <= td <= RANGEMAX(f) and trace(sx,sy, targetPos, e, BOSS_ATTACK_LOS, 2) hits target → f
    else → -1                                                                          // :746-766
axis-aligned:
    h = trace(sx, sy, tx, ty, e, MONSTER_ATTACK_LOS, 2); nearest hit entity != target → -1   // :768-772
    n9 = sign(a), n10 = sign(a2)                                                       // :773-779
    b = trace(sx + n9*18, sy + n10*18, tx, ty, e, MONSTER_ATTACK_CLEAR, 2) hits target   // :780-781
    f0 = field(0), f1 = field(1); if (!b) { melee f0 → 0; melee f1 → 0 }  (MELEE_WEAPON_MASK)
    td = worldDistToTileDist(distFrom(target, sx, sy))                                 // :793
    if (f0 == f1) f1 = 0
    ok0 = f0 != 0 && RANGEMIN(f0) <= td <= RANGEMAX(f0); ok1 likewise
    if (f0 == 18 || f1 == 18): walk td tiles from (sx>>6, sy>>6) by (n9,n10) (do-while, first
        tile = own tile); any baseVisited bit → that weapon not ok                     // :812-830
    both ok → (nextByte() <= field(2)) ? f0 : f1;  one ok → it;  none → -1             // :832-846
```

* "hits target": `TraceHit h = trace(...); h.isEntity() && h.entity == target`.
* `distFrom(target, x, y)` = `env_.trace->distFrom(target, x, y)`; for the player entity this is
  the **destination** (G1).
* Note the legacy asymmetry, kept: the axis test and the first trace use the player's *view*
  point, the boss/off-axis traces use `calcPosition` = the *destination*; both coincide
  whenever monsters think (the player has arrived).
* `RANGEMIN/MAX` = `combat->weaponDef(w).rangeMin/rangeMax` (signed bytes, compare as int).
* `field(k)` = `combat->getMonsterField(def, k)` (§3.3).

#### 3.1.6 `isAttackValid(e)` — verbatim (`src/Entity.cpp:644-671`, MT §3.5)

```
w = m->ce.weapon; if (w <= -1) return false;
h = trace(sx, sy, player->destX, player->destY, e, MONSTER_ATTACK_LOS, 2);
if (!h.isEntity()) return false;
inRange = weaponDef(w).rangeMax >= worldDistToTileDist(Chebyshev²(sx,sy → destX,destY));
n = sx - destX; n2 = sy - destY; axis = inRange && (n||n2) && (n==0 || n2==0);
return inRange && axis && ((m->target == nullptr && h.eType == ET_PLAYER) || m->target == h.entity);
```

`baseVisitedTiles_` is built in `init`: zero all, then for every tile `i` with
`map->mapFlags[i] & 0x1` set bit `i%32` of row `i/32` (`src/Game.cpp:497-507` →
`linkWorldEntity`, `:179`). Name the bit `TileFlag::SOLID_AT_LOAD = 0x1` in `MapBits.h`
(comment: "file bit 0; the world entity is linked on these tiles at load").

### 3.2 `MonsterSystem` changes

Env: `const Combat* combat` becomes `Combat* combat`; add `MonsterAI* ai`, `const Player* player`.

New public members:

```
bool monstersUpdated = false;               // Game::monstersUpdated (src/Game.cpp:883), reset by advanceTurn
void attack(Entity* e);                     // Entity::attack   src/Entity.cpp:1160-1167 (LIFO push)
void undoAttack(Entity* e);                 // Entity::undoAttack src/Entity.cpp:1169-1191 verbatim
void monsterAI();                           // src/Game.cpp:874-913
void monsterLerp();                         // src/Game.cpp:915-939 (no invalidateRect)
void updateMonsters();                      // src/Game.cpp:2458-2474 (replaces the Stage-1 stub)
bool canSnapMonsters() const;               // src/Game.cpp:2374-2389
bool snapMonsters(bool b);                  // src/Game.cpp:2391-2450 (was void)
void onMonsterAttackDone();                 // GameStateRunner::combatState monster arm, src/GameStateRunner.cpp:41-78
static bool isBoss(const Entity* e);        // src/Entity.cpp:1398-1400: sub 12..16 except (14, parm 0)
static bool isHasteResistant(const Entity* e) { return isBoss(e); }   // src/Entity.cpp:1402-1404
```

Algorithms (all verbatim; pointers cached before calls exactly as in the original):

* **`attack(e)`**: `if (!(m->flags & MFLAG_ATTACKING)) { flags |= MFLAG_ATTACKING;
  m->nextAttacker = combatMonsters; combatMonsters = e; }`.
* **`undoAttack(e)`**: return unless `MFLAG_ATTACKING`; clear it; weapon 18 → clear anim byte
  (`mapSpriteInfo &= 0xFFFF00FF`); `resetGoal()`; unlink from the chain with the legacy
  prev/next walk (`src/Entity.cpp:1180-1190`, including its `else if (combatMonsters != nullptr)
  combatMonsters = e->nextAttacker` head case).
* **`monsterAI()`**: MT §2.3 steps 1-4. `disableAI` does not exist (never true). The ring walk
  runs `ai->think(cur, false)` for `(monsterEffects & 0x2) == 0 && (monstersTurn == 1 ||
  (monstersTurn == 2 && isHasteResistant(cur)))`; sets `i = 1` when `goalFlags & kGoalFlagLerping`.
  Loop condition `while (cur != activeMonsters && activeMonsters != nullptr)`. Re-validation loop
  as in `src/Game.cpp:898-911` (`isAttackValid` false → `undoAttack` + `think` again).
* **`monsterLerp()`**: `if (!interpolatingMonsters) return;` then recompute
  `interpolatingMonsters` = any active monster with `goalFlags & kGoalFlagLerping`.
* **`updateMonsters()`**: `monsterAI(); monsterLerp(); if (!interpolatingMonsters) { *facingDirty
  = true; if (combatMonsters) combat->performMonsterAttack(combatMonsters,
  combatMonsters->monster->target); else endMonstersTurn(); }`.
* **`canSnapMonsters()`**: false iff any active-ring monster has `info & kInfoOnScreenStep`.
* **`snapMonsters(b)`**: MT §2.6. "Animating effect owned by a non-monster" test =
  `!lerps->animatingEffectsAreMonsters(*db)` (§3.6). Snapping = `lerps->snap(sprite)` for every
  active-ring monster (cache `next` first). Monster-turn arm ends with
  `if (combatMonsters && !interpolatingMonsters) { combat->performMonsterAttack(...); return false; }`.
* **`onMonsterAttackDone()`** (called when `Combat::tick()` returns false with
  `curAttacker != nullptr`):
  ```
  if (player->getHealth() <= 0) {                       // ST_DYING check, src/GameStateRunner.cpp:41-46
      while (combatMonsters) undoAttack(combatMonsters);
      combat->active = false; return;
  }
  // knockbackDist == 0 always (§1.5)
  Entity* cur = combat->curAttacker;
  if (cur->monster->goalFlags & kGoalFlagRetreat) {     // :53-58
      cur->monster->resetGoal(); cur->monster->goalType = 5; cur->monster->goalParam = 1;
      ai->think(cur, false);
  }
  Entity* next = cur->monster->nextAttacker;            // :59-64
  while (next && next->monster->target == nullptr && !ai->isAttackValid(next)) {
      Entity* n2 = next->monster->nextAttacker; undoAttack(next); next = n2;
  }
  if (next) { combat->performMonsterAttack(next, next->monster->target); return; }   // :65-66
  combatMonsters = nullptr; combat->active = false;     // :67-78
  if (!interpolatingMonsters) endMonstersTurn();        // else updateMonsters closes it later
  ```
* **`diedMonster`**: add `undoAttack(e);` right after `deactivate(e)` (`src/Entity.cpp:485-486`).
* Keep `endMonstersTurn()` as is (`startRotation(true)` has no counterpart).

### 3.3 `Combat` changes (G2 part)

New public state: `Entity* curAttacker = nullptr; EntityMonster* attackerMonster = nullptr;
int attackFrame = Enums::MANIM_ATTACK1;` and `int numActiveMissiles = 0;` (stays 0 until G7).

New public methods:

```
// src/Combat.cpp:1058-1060: monsterAttacks[sub*9 + parm*3 + k]; out of range -> 0 ("no weapon").
int getMonsterField(const EntityDef* def, int k) const;
// performAttack with curAttacker != nullptr (src/Combat.cpp:54-166). The weapon-18 lunge
// (:131-164) is deferred (log once); the attack then runs as a plain melee attack.
void performMonsterAttack(Entity* attacker, Entity* target);
```

`performMonsterAttack` body (verbatim order): `curAttacker = attacker; curTarget = target;
accumRoundDamage = 0; attacker->info |= kInfoCombatMark; if (target) target->info |= ...;`
target type fields (nullptr → 0/0/nullptr, `:76-84`); `attackerMonster = attacker->monster;
attackerWeaponId = attackerMonster->ce.weapon; attackFrame = (w == 18) ? 64 : (w ==
getMonsterField(def,1)) ? 80 : 64;` `stage = 0; nextStageTime = 0; animEndTime = 0;
animLoopCount = weaponDef(w).numShots;` (no ammo cap for monsters, `:112-120`)
`attackerWeaponProj = weaponDef(w).projType; worldDist = trace.distFrom(attacker, player->viewX,
player->viewY); tileDist = worldDistToTileDist(worldDist); active = true;` + one log line
`[combat] monster attack spr=%d w=%d proj=%d shots=%d tileDist=%d`.

The existing player `performAttack` additionally sets `curAttacker = nullptr; attackerMonster =
nullptr;` at the top.

`tick()` becomes the `runFrame` dispatch (`src/Combat.cpp:857-862`): the current body moves
unchanged into a new private `bool playerSeq()`; `tick()` = `curAttacker == nullptr ?
playerSeq() : monsterSeq()`. **G2 `monsterSeq()` stub**: log
`[combat] monsterSeq stub spr=%d w=%d` and `return false;`.

`GameContext::buildViewWeaponModel` (`GameContext.cpp:599`): the attack-pose condition becomes
`c.active && c.curAttacker == nullptr && c.attackerWeaponId == w` (legacy `b5`,
`src/Combat.cpp:735`).

### 3.4 `Game` changes

* New member `MonsterAI ai;` next to `monsters`; wire in `loadEntities` after `monsters.init`:
  `ai.init({ &db, &map, &trace, &combat, &monsters, &lerps, &doors, player_, nullptr })` and pass
  `&ai`, `player_` into `monsters.init` (update the Env initialiser).
* `advanceTurn()`: add `monsters.monstersUpdated = false;` next to `monstersTurn = 1`
  (`src/Game.cpp:1263`). The Error-95 branch calls `monsters.snapMonsters(true)` (now returns
  bool; ignore it).

### 3.5 `GameContext::tickPlaying` step 4 (replaces `GameContext.cpp:360-368`)

```
sys_.game->combat.shotsFired = false;
Combat& c = sys_.game->combat;
if (c.active) {
    if (!c.tick()) {
        if (c.curAttacker == nullptr) { c.active = false; sys_.game->advanceTurn(); }
        else sys_.game->monsters.onMonsterAttackDone();
    }
} else if (sys_.game->lerps.animatingEffects() == 0 &&       // src/GameStateRunner.cpp:181-183
           sys_.game->monstersTurn != 0 &&
           sys_.dialogs->numHelpMessages() == 0) {            // knockbackDist/activePropogators: 0 (G7 adds the latter)
    sys_.game->monsters.updateMonsters();
}
```

And fix the help-popup dequeue gate at `GameContext.cpp:376`: legacy is
`!isCameraActive() && ST_PLAYING` with **no** `monstersTurn` term (`src/GameStateRunner.cpp:197-199`)
→ `if (!cinematic_.active()) sys_.dialogs->dequeueHelpDialog();`. (With the new
`numHelpMessages == 0` gate on `updateMonsters`, the old `monstersTurn == 0` gate would
deadlock the turn.) The dialog-close tail `DialogSystem.cpp:335` keeps its `monstersTurn == 0`
term — that one is legacy (`src/DialogSystem.cpp:109-111`).

### 3.6 Small additions

* `SpriteLerps`: `int animatingEffects() const` = number of active slots with
  `kFlagAnimatingEffect` (count on demand — the script opcode tails overwrite `flags` after
  alloc, `ScriptVM.cpp:848/890/1368/1422`, so an incrementally maintained counter could drift;
  counting is exact). `bool animatingEffectsAreMonsters() const` = every such slot's entity
  (`db->findEntityBySprite`) exists and is `ET_MONSTER` (`src/Game.cpp:2394-2408`).
* `DialogSystem`: `int numHelpMessages() const { return numHelpMessages_; }`.

### 3.7 Input gate (`core/PlayerActions.cpp` `handleAction`)

After the existing `gateBlocked` return (`PlayerActions.cpp:106-107`), port
`src/PlayingInputHandler.cpp:71-74`:

```
if (a != Action::TurnLeft && a != Action::TurnRight &&
    (!env_.game->monsters.snapMonsters(false) || env_.game->lerps.animatingEffects() != 0))
    return;     // (activePropogators term: G7)
```

`snapMonsters` may start a monster attack (`combat.active` becomes true) — the action is
swallowed, exactly like the original.

### 3.8 `ScriptVM`

* `EV_AIGOAL` (`ScriptVM.cpp:734-760`) completes `setAIGoal` (`src/ScriptThread.cpp:2226-2246`):
  after activation, `env_.game->ai.think(ent, true)`; then `if (goalType == 3) {
  m->goalFlags &= ~kGoalFlagRetreat; if (monsters.combatMonsters)
  combat.performMonsterAttack(head, head->monster->target); }`. Remove the two "deferred" logs.
* `EV_ADVANCETURN` (`ScriptVM.cpp:729-731`) → `src/ScriptThread.cpp:1276-1288`:
  `if (monsters.interpolatingMonsters) monsters.snapMonsters(true); while (combatMonsters)
  monsters.undoAttack(combatMonsters); monsters.endMonstersTurn(); advanceTurn();`
  (`clearEvents(1)` = `ctx` pending actions; skip, the queue is consumed per tick).

### 3.9 Build

New files `MonsterAI.h/.cpp` ⇒ `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`, then build.

### 3.10 Acceptance (G2)

Setup: stand on a tile axis-aligned and **adjacent** to an imp, shoot it once (wakes it), then
press Wait (Passturn) a few times.

* Log after the shot and after each Wait: `[ai] ...` shows the imp choosing goal 3 with weapon
  16, then `[combat] monster attack spr=<imp> w=16 ...` and `[combat] monsterSeq stub`.
* Step off-axis (diagonal to the imp): the log shows goal 2 (chase stub), no attack.
* A zombie on the same row/column 2-3 tiles away, woken by a shot: attack with w=19.
* On screen: **nothing new happens yet**, and the game never locks up — after every Wait the
  player can move again immediately. Intro, dialogs and help popups still work.

---------------------------------------------------------------------------------------------
## 4. G3 — The attack sequence and damage to the player

**Carry-overs from the G2 review (2026-09-23, do them in G3):**
* Script threads keep ticking while `combat.active` in Playing (`new_src/core/GameContext.cpp:239`);
  legacy `ST_COMBAT` does not tick them. An `EV_AIGOAL` type 3 during the player's attack would
  overwrite `curAttacker` and the turn would never advance. Gate script ticking on
  `!combat.active` (or record a deviation with a guard).
* `MonsterAI.cpp:193` dereferences `target->def` unchecked — return -1 when `def == nullptr`.
* Reset `combat.curAttacker` / `attackerMonster` together with `active` on level load.
* Comment `EntityMonster.h:60`: `resetGoal` clearing `target` is a (harmless) deviation from
  `src/EntityMonster.cpp:39-46`.

**Status 2026-09-24:** items 1-3 DONE (script resume gated on `!combat.active` —
`new_src/core/GameContext.cpp:233-246`, legacy `src/Game.cpp:3252-3265`; null-def guard
`new_src/domain/game/MonsterAI.cpp:186`; combat reset in `loadEntities`,
`new_src/domain/game/Game.cpp:41-43`). Item 4 (comment) still open for G3.
Further legacy-ST_COMBAT mismatches found while fixing (decide in G3): `tickPlaying` still runs
during `combat.active` the `queueAdvanceTurn` consumer (legacy only ST_PLAYING,
`src/Canvas.cpp:812-815`), the death check (`src/GameStateRunner.cpp:170-173`; legacy handles
death in combat via runFrame, :41-46), `dequeueHelpDialog` (:197-199) and `hud->update` (:196);
script threads are swept only in Playing/Camera, legacy sweeps in every state except the
sound-enable menu (`src/Canvas.cpp:791-796`) — a finished BLOCKINPUT thread may hold its slot
through Dialog/Looting/Menu until the next Playing tick (pre-existing, record as deviation);
legacy `combatState` calls `monsterLerp()` every frame (:24) — the rewrite only inside
`updateMonsters`; `unloadMapData` also resets `combat.curTarget` (`src/Game.cpp:652`).

### 4.1 `Combat::monsterSeq()` — verbatim port of `src/Combat.cpp:427-610` (MA §4)

Let `S = weaponDef(attackerWeaponId).shothold * 10`, `clock = game->lerps.clockMs()`,
`gt = (int)*env_.gameTime`, `sprite = curAttacker->getSprite()`.

```
A. n2 = (info >> 8) & 0xFF; anim = n2 & 0xF0; frame = n2 & 0xF;          // :429-433
   if ((anim == 64 || anim == 80) && frame == 0) {
       if (clock >= attackerMonster->frameTime) {                            // :434-441
           attackerMonster->frameTime = clock + S; nextStageTime = gt + S;
           info = (info & 0xFFFF00FF) | ((anim | (frame + 1)) << 8);
           launchProjectile();
       }
   }
B. else if (nextStageTime != 0 && gt > nextStageTime && numActiveMissiles == 0 &&
            game->lerps.animatingEffects() == 0) {                           // :442-462
       // reflection branch (:443-455): buffs absent -> reflectionDmg is always 0 -> omitted
       stage = nextStage; nextStageTime = 0; nextStage = -1;
   }
C. if (stage == 0) {                                                        // :462-571
       totalDamage = totalArmorDamage = hitType = crFlags = 0;
       gotCrit = gotHit = targetKilled = false;
       soundType = (w == getMonsterField(def,1)) ? MSOUND_ATTACK2 : MSOUND_ATTACK1;
       if (weaponDef(w).strMax > 0) {                                       // signed byte
           if (curTarget == nullptr) {
               player->ce.calcCombat(*this, attackerMonster->ce, db.playerEntity(), true,
                                     trace.distFrom(curAttacker, player->destX, player->destY), -1);
               if (crFlags & CR_HITMASK) { gotCrit = crFlags & 0x2; hitType = gotCrit ? 2 : 1;
                                           gotHit = true; damage = crDamage; }
               else damage = 0;
           } else { hitType = 1; damage = 1; }
           totalDamage += damage; totalArmorDamage += crArmorDamage;
       }
       log "[sound] %d" with: w==17 && !(crFlags & CR_HITMASK) -> 1100; w==28 -> 1087;
                               else monsterSound(sub, parm, soundType)          // :512-522
       --animLoopCount; animTime = S; animStartTime = gt; animEndTime = gt + S;
       if (w != 18) { info = (info & 0xFFFF00FF) | (attackFrame << 8);
                      attackerMonster->frameTime = clock + animTime; }
       if (curTarget != nullptr) targetKilled = true;
       // w == 18 return lerp (:535-551): deferred with the lunge (§1.5)
       // gotHit && (w == 23 || sub == 2) -> burn status: deferred, log
       stage = -1; nextStage = (animLoopCount <= 0) ? 1 : 0; nextStageTime = animEndTime;
       if (def->tileIndex in [23,25] && w == 16 && animLoopCount == 0) info ^= 0x20000;  // :564-566
       updateProjectile();
   }
D. else if (stage == 1) {                                                   // :574-597
       info = (info & 0xFFFF00FF);                                           // ET_MONSTER attacker
       if (targetKilled) game->entityDied(curTarget, false);
       if (accumRoundDamage > 0) centerMessage(112, {attackerName, accumRoundDamage});
       [G4: hud->stopShake(); hud->setDamageDir(0); hud->setDamageTime(0);]
       attackerMonster->flags |= MFLAG_ATTACKING;
       [G7: game->sprites.clear(0x40, now);]
       return false;
   }
E. else if (stage == -1) {                                                  // :599-604
       if (numActiveMissiles != 0 || exploded) updateProjectile();
   }
if (player->getHealth() <= 0) return false;                                // ST_DYING analog :606-608
return true;
```

`info` means `map->mapSpriteInfo[sprite]` (read-modify-write in place). `attackerName` =
`Localization::titleOf(loc->get(kTextIngame, def->name & 0x3FF))` (same helper as
`Game::diedProp`, `Game.cpp:419-420`). `monsterSound(sub, parm, type)` = new private helper,
port of `Game::getMonsterSound` (`src/Game.cpp:3771-3845`) for `type ∈ {3,4}` only:
Pinky parm 0 → 1082; Mastermind parm 1 falls through; else
`tables->monsterSounds[sub*8 + type]`, `+1000` unless 255.

Timeline to expect (MA §4.1): imp claw, S = 100 ms, 2 shots → ≈ 400 ms; zombie throw
S = 300 ms, 1 shot → ≈ 600 ms (instant damage at 300 ms until G7).

### 4.2 `Combat::updateProjectile()` tail and `explodeOnPlayer()`

`updateProjectile` (`Combat.cpp:423-435`): replace the `explodeOnPlayer deferred` log with
`explodeOnPlayer();`. `launchProjectile` is unchanged in G3 (its default arm and the
positive-type log already give the instant fallback, ADR 0017).

New private `void explodeOnPlayer()` — port of `src/Combat.cpp:948-1056` (MA §6.3, PD §1.2);
bracketed lines are filled in by G4:

```
if (curTarget != nullptr) return;                                           // :950-952
[G4 A: damage dir + damage time]
if (gotHit && (totalDamage > 0 || totalArmorDamage > 0)) {                  // :972
    [G4 B: playerPainEvent(curAttacker, true)]
    db.unlinkEntity(curAttacker);                                            // :976 (linkIndex is kept)
    if (attackerWeaponProj == 7) log "[combat] burn status deferred";       // :977-980
    player->addArmor(-totalArmorDamage);                                     // :981
    if (totalDamage > 0) {
        // reflect (buffs[0]) absent                                         // :968-971, :1021-1025
        if (difficulty() != 1) {                                             // :984-990
            int m = loadMapID; if (difficulty() == 4) m *= 2;
            totalDamage += (totalDamage >> 1) + m / std::max(1, (int)weaponDef(w).numShots);
        }
        // VIOS bonus (:991-999): deferred
        accumRoundDamage += totalDamage;
        playerPain(totalDamage, curAttacker, false);                         // :1000-1001
        if (player->getHealth() == 0) return;                                // :1002-1004 (attacker stays unlinked: verbatim)
        [G4 C: rock view]
        // Pinky charge knockback (:1017-1019): deferred
    } else {
        [G4 D: hud->setDamageCount(0)]
        totalDamage = 0;                                                     // :1027-1030
    }
    if (totalDamage <= 0) centerMessage(72);                                 // :1031-1033
    db.linkEntity(curAttacker, curAttacker->linkIndex % 32, curAttacker->linkIndex / 32);  // :1034
} else {                                                                     // :1040-1054
    [G4 E1: hud->setDamageCount(0)]
    if (crFlags & 0x100) centerMessage(73);
    else if (!(crFlags & 0x80)) centerMessage(69, {attackerName});
    [G4 E2: hud->setDamageCount(-1)]
}
```

New private `void playerPain(int n, Entity* src, bool showMsg)` — the presentation half of
`Player::pain` (`src/Player.cpp:652-709`):

```
if (player->god) return;
if (showMsg) src ? centerMessage(112, {name(src), n}) : centerMessage(71, {n});   // :657-671
Player::PainBand band = player->pain(n);
switch (band) { NearDeath: hud->addMessage(text(113), color, kMsgFlagForce|kMsgFlagCenter);
                LowHealth: same with 114;                                         // :683-697 (flags 3)
                HalfHealth: log "[help] 10/11 deferred" (showHelp not ported) }
```

`centerMessage` gets the same `0xAA000000` colour as today. (Its Force variant is a direct
`hud->addMessage` with the composed kTextMain text.)

### 4.3 `Player` (state half)

```
enum class PainBand { None, NearDeath, LowHealth, HalfHealth };
// Player::pain state half (src/Player.cpp:652-709): god/n==0 early outs, the 1/256 crossing
// bands (n2 = (hp<<16)/(max<<8), n3 = ((hp-n)<<16)/(max<<8); only when n3 > 0:
// n2>26&&n3<=26 -> NearDeath, else n2>78&&n3<=78 -> LowHealth, else n2>128&&n3<=128 ->
// HalfHealth), then addHealth(-n). noDeathFlag is never set in src/ -> omitted.
// Death is NOT handled here (ADR 0028 decision 4).
PainBand pain(int n);
// src/Player.cpp:1933-1946: god blocks negatives; clamp [0,200] on STAT_ARMOR.
void addArmor(int n);
```

`helpBitmask & 0xC00` does not exist; `HalfHealth` is returned on every crossing (only logged).

### 4.4 Death wiring (`core/GameContext.cpp`)

* In the step-4 block (§3.5), right after the `c.tick()` handling:
  `if (sys_.player->getHealth() <= 0) { setState(StateId::Dying); return; }` (the original
  enters `ST_DYING` inside `pain`; the step-3 check stays as the second line of defence).
* `enterState_(Dying)` additionally: `sys_.game->monsters.combatMonsters = nullptr;
  sys_.game->combat.active = false;` and `std::fprintf(stderr, "[player] died\n")`
  (`src/Player.cpp:711-733`; counters, sound and shake arrive with G8a).
* The existing Dying stub stays (screen freezes; no HUD, no weapon).

### 4.5 Input while a monster attacks (`PlayerActions::handleAction`)

Replace the `if (combat.active) return;` line (`PlayerActions.cpp:104`) by
(`src/InputEventController.cpp:364-372`):

```
if (env_.game->combat.active) {
    if (env_.game->combat.curAttacker != nullptr) {
        if (a == Action::TurnRight) p.destAngle -= 256;
        else if (a == Action::TurnLeft) p.destAngle += 256;
        p.startRotation();                      // legacy calls it for every key
    }
    return;
}
```

(No idle gate here — the legacy ST_COMBAT handler has none, so two quick presses stack.)

### 4.6 Wiring / build

`Combat` already reaches `game->db`, `game->trace`, `game->lerps`, `player`, `loc`, `hud`,
`tables`. No new files.

### 4.7 Acceptance (G3) — what the user sees

* **Imp, adjacent, axis-aligned, woken by one shot**, then Wait: the imp plays its claw
  animation twice in ~0.4 s (wind-up, strike, wind-up, strike), then stands idle again. The
  health number in the bottom bar drops by about 7 per landed claw (difficulty 2 math: 5 base
  +2 +0, player armor 0 / defense 5). A message "Imp did N damage!" appears after the attack, or
  "Imp missed!" when both swings miss. The player cannot move while the imp swings but can
  press left/right to turn toward it. Afterwards the player moves normally.
* **Zombie on the same row/column, 2-3 tiles away**, woken by a shot: throw pose for ~0.6 s,
  health drops by 13 or 16 at the throw frame (base 8 or 9, strength bonus 0 or 1, then
  +50 % +1 for difficulty 2; no flying chunk yet — G7). Numbers assume the fresh player's
  armor 0 / defense 5 (`Player::reset`); armor pickups lower them.
* Standing diagonal to a woken imp: it does nothing (movement is G6).
* Letting monsters bring health to 0: HUD and weapon disappear and the view freezes (Dying
  stub). No crash, no endless attack loop.
* Several woken monsters in range: attacks come one after another, the last one to think
  attacks first (LIFO, MT §1.1).

---------------------------------------------------------------------------------------------
## 5. G4 — Damage feedback

### 5.1 `ui/Hud.h/.cpp`

Remove the demo API (`setDamageDemo`, the countdown in `update()` at `Hud.cpp:466-469`, the
unused `Hud::draw` arrow/vignette calls stay in the dead method or are deleted). New API:

```
// Legacy Hud::damageDir / damageTime / damageCount (PD §1.2, §2.4-2.5). damageTime is an
// absolute upTimeMs deadline, 0 = none.
void setDamageDir(int d);        int damageDir() const;
void setDamageTime(int64_t t);   int64_t damageTime() const;
void setDamageCount(int c);      int damageCount() const;
// Arrow (src/Hud.cpp:508-526) then vignette (src/Hud.cpp:535-583). Pure draw of the state.
void drawDamageEffects(Graphics2D& g, int64_t nowMs, bool totalDamagePositive);
// canvas->shakeTime = 0 (src/Combat.cpp:591).
void stopShake();
// Render::rockView (src/Render.cpp:2962-2968). Stores x<<4, y<<4, z<<4.
struct RockView { int64_t startMs = 0; int durMs = 0; int x = 0, y = 0, z = 0; };
void startRockView(int64_t nowMs, int durMs, int x, int y, int z);
const RockView& rockView() const;
```

`drawDamageEffects` (legacy geometry; `viewRect = {0,20,480,250}`, `screenRect` 480×320):

* Arrow iff `now < damageTime && damageCount > 0 && totalDamagePositive && (1 << dir) & 0xC1`:
  `drawRegion(attackArrows, 0, srcY, 12, 12, x, 275, kAnchorHCenter | kAnchorVCenter)` with
  dir 0 → x 460 srcY 24; dir 6 → x 20 srcY 12; dir 7 → x 240 srcY 0.
* Vignette iff `now < damageTime && damageCount >= 0 && totalDamagePositive`: mask by dir
  (1→2, 2→3, 3→15, 4→5, 5→4, 0/6/7→8); strips, each tiled with the 16×16 `damageVignette`
  (reuse the existing `tile` lambda, `Hud.cpp:145-157`):
  bit 1 `(0,20,480,16)` rot 1; bit 2 `(464,20,16,250)` rot 4; bit 4 `(0,20,16,250)` rot 0;
  bit 8 `(0,240,480,16)` rot 3 (the hard-coded 256−16).
* Expiry write (corrected 2026-10-28, follows `src/Hud.cpp:525-529`): when the arrow condition
  fails, `damageTime = 0` is written before the vignette test (`new_src/ui/Hud.cpp:125-175`).

### 5.2 `Combat` (fill the G3 brackets)

`Combat::Env` gains `const int64_t* upTimeMs` (Main wires `&ctx.upTimeMs`). New private
`void playerPainEvent(Entity* src, bool fromMonster)` (`src/Player.cpp:624-650`):
`if (!src) hud->setDamageDir(3); log "[sound] 1093"; hud->setDamageCount(1);
if (!fromMonster) hud->startShake(now, 500, 4);` (legacy `startShake(500,2,150)`,
intensity doubled as in `Hud::startShake`'s contract; the monster case `startShake(0,0,150)` is
vibration only → nothing).

* **A** (`src/Combat.cpp:953-967`): `hud->setDamageDir(Player::calcDamageDir(player->viewX,
  player->viewY, player->viewAngle, ax, ay)); hud->setDamageTime(now + (animLoopCount > 0 ? 150
  : 1000));` (`ax, ay` = attacker sprite xy; zoom absent).
* **B**: `playerPainEvent(curAttacker, true);`
* **C** (`:1005-1016`): `sx = sign(viewX - ax), sy = sign(viewY - ay);
  hud->startRockView(now, 200, viewX + sx*6, viewY + sy*6, viewZ);`
* **D / E1 / E2**: as bracketed.
* Stage 1 (§4.1 D): `hud->stopShake(); hud->setDamageDir(0); hud->setDamageTime(0);`

`Player::calcDamageDir(int x1, int y1, int angle, int x2, int y2)` — static, verbatim
`src/Player.cpp:576-622` (PD §1.4): 8 compass angles from the signs of `dx, dy` (y grows
south: `dy<0` = north), `dAng = (ang − (angle & 1023)) & 1023`, `> 512 → dAng − 1024`,
`dir = dAng / 128 + 3` (C truncation), `dir < 0 → 7`, `dx == dy == 0 → 3`. Angle constants:
E 0, NE 128, N 256, NW 384, W 512, SW 640, S 768, SE 896.

### 5.3 Rendering

* **`render/SceneRenderer.cpp`**, player-view branch (`SceneRenderer.cpp:91-101`): compute the
  base first, apply rock, then shake, then the pull-back (legacy order
  `src/Render.cpp:2248-2282`):
  ```
  int bx = (viewX << 4) + 8, by = (viewY << 4) + 8, bz = (viewZ << 4) + 8;
  const Hud::RockView& r = hud->rockView();
  if (now < r.startMs + r.durMs) {
      int w = sinTable[(((now - r.startMs) << 16) / (r.durMs << 8) + 256) & 0x3FF] >> 8;
      bx += w * ((r.x - bx) << 8) >> 16; by += ...; bz += ...;
  }
  shake block on (bx, by, bz); then rvx = bx - (160*viewCos >> 16); rvy = by + (160*viewSin >> 16); rvz = bz;
  ```
  (`now` = `*env_.upTimeMs`, already in the Env.) Cinematic branch unchanged.
* **`core/GameContext.cpp` `render()`**: right after the view-weapon draw and before
  `drawTopBar` (legacy repaint bit 0x1 before 0x2, `src/Hud.cpp:725-742`):
  `if (gameplayView) sys_.hud->drawDamageEffects(g, upTimeMs, sys_.game->combat.totalDamage > 0);`
* **View-weapon red tint**: `ViewWeaponModel` gains `bool brightRed`; producer sets it to
  `upTimeMs < hud->damageTime() && hud->damageCount() >= 0 && combat.totalDamage > 0`
  (`src/Combat.cpp:710-712`). `ViewWeapon::draw` draws the gun art with modulation
  `(255, 128, 128, 255)` when set (`src/GLES.cpp:625-631`: `glColor4f(1, .5, .5, 1)`);
  `drawWeaponQuad` takes `r, g, b` instead of one `tint`. The flash keeps its current tint.
* **Turning clears the vignette**: `PlayerActions` TurnLeft/TurnRight (playing input, not the
  ST_COMBAT path) call `env_.hud->setDamageTime(0)` (`src/PlayingInputHandler.cpp:112-114`).

### 5.4 `EV_DAMAGEPLAYER` (`ScriptVM.cpp:1153-1159`) — `src/ScriptThread.cpp:725-744`

```
if (dmg > 0) { combat.playerPainEventPublic(nullptr, false); hud->setDamageTime(now + 1000);
               if (ang != -1) hud->setDamageDir(((uint8_t)ang + ((256 - (viewAngle & 0x3FF)) >> 7) + 1) & 7);
               combat.totalDamage = 1; combat.playerPainPublic(dmg, nullptr, true); }
else if (dmg < 0) player.addHealth(-dmg);
player.addArmor(-arm);
```

Expose `playerPainEvent` / `playerPain` as public on `Combat` (drop the "Public" suffix — they
are simply public). Bytes are signed (`int8_t`), as legacy.

### 5.5 Acceptance (G4)

* Imp clawing from the front: while the claws land, all four screen edges (inside the 3D band)
  get the red dithered border; the weapon turns reddish; the camera jolts back a little and
  settles within 0.2 s. Border and tint disappear when the attack ends (≤ 1 s).
* Imp attacking from the left/right (turn so it is at your side, then Wait): only the left/right
  edge is red. From behind: a small arrow at the bottom (left / centre / right) plus the bottom
  red strip.
* Turning left/right removes the red border immediately.
* A full miss shows no border and no tint.

---------------------------------------------------------------------------------------------
## 6. G5 — Sight wake (ADR 0029)

### 6.1 `domain/game/ViewVisibility.h` (header only)

```
struct ViewVisibility {
	bool valid = false;        // a world frame was rendered since the last consume
	bool cinematic = false;    // that frame was rendered from a MayaCamera pose
	std::vector<int> drawnSprites;   // sprites legacy renderBSP would submit (§6.5), may repeat
	uint32_t tiles[32] = {};         // bit x of tiles[y]: legacy checkTileVisibilty(x, y) (§6.5)
	bool tileVisible(int tx, int ty) const {
		return tx >= 0 && ty >= 0 && tx < 32 && ty < 32 && ((tiles[ty] >> tx) & 1u) != 0;
	}
};
```

### 6.2 Producer

* `World3D::drawBSP`: clear an owned `ViewVisibility vis_` at entry; `drawSprite` appends `i`
  to `vis_.drawnSprites` when called from the BSP pass (a member flag set around the leaf draw
  loop `World3D.cpp:1326`); after the visible-leaf list `nodeIdxs_` is complete, for every leaf
  `n` set the tile bits of `[x1>>6 .. (x2-1)>>6] × [y1>>6 .. (y2-1)>>6]` with
  `x1 = nodeBounds[n*4+0] << 3` etc. (canvas units, same decode as `World3D.cpp:1138-1141`).
  `vis_.valid = true`. Accessor `const ViewVisibility& visibility() const`.
* `SceneRenderer`: after `drawBSP`, copy into its own member and set `cinematic = (cinePose !=
  nullptr)`; accessor.
* `GameContext::render`: right after `scene_.drawWorld(...)`:
  `sys_.game->visibility = scene_.visibility();` — the only writer. `Game` gains the member
  `ViewVisibility visibility;`.

### 6.3 Consumer `MonsterSystem::renderActivate(ViewVisibility& vis)`

Port of `src/Render.cpp:1576-1583`, run once per rendered frame's list:

```
if (!vis.valid || vis.cinematic) { vis.drawnSprites.clear(); return; }     // disableRenderActivate in ST_CAMERA
if (lerps->animatingEffects() != 0 /* || activePropogators (G7) */) { clear; return; }
for (int s : vis.drawnSprites) {
    Entity* e = db->findEntityBySprite(s);
    if (!e || !e->monster) continue;
    if (e->info & (kInfoCorpse | kInfoOnActiveList | kInfoHidden)) continue;   // 0x1050000
    if (e->monster->flags & MFLAG_NOACTIVATE) continue;
    TraceHit h = trace->trace(trace->playerX(), trace->playerY(), spriteX(s), spriteY(s),
                              db->playerEntity(), Contents::MONSTER_SIGHT, 2);   // 2D: deviation D3
    if (h.isEntity() && h.entity == e) activate(e, true, true, true, false);
}
vis.drawnSprites.clear();
```

Called from `GameContext::tickPlaying` step 4, right after the `shotsFired = false` line and
before the combat/monster block (the original's render pass sees `shotsFired == false`, it is
cleared at render start, `src/Render.cpp:2351`).

### 6.4 Build / acceptance

New header ⇒ reconfigure. User: walk toward an imp that faces you, without shooting. When it is
within 4 tiles and in view, it wakes (log `[monster] activate sprite=`), and from then on
attacks when adjacent and aligned (G3). An imp shown from the back (back-turned pose) does not
wake by sight — only when shot. Monsters seen only during a cinematic do not wake.

### 6.5 Leaf cull for visibility (D12 fix, ADR 0031) — one coder delegation (G5b)

Goal: `ViewVisibility` reports what the ORIGINAL would have considered visible
(`Render::walkNode` cull, `src/Render.cpp:1053-1055`). **The GPU draw list must not change**:
`nodeIdxs_`, `splitPairs_` and the draw loop stay as they are, so golden frames stay
byte-identical. All new math is integer, 32-bit `int`, written exactly like the legacy
expressions (same shifts, same division order). Shifts like `a + 7 >> 3` mean `(a + 7) >> 3`.

#### 6.5.1 Facts this design rests on (all verified in `src/`)

* The cull is pure 2D. `transform2DVerts` uses `mvp2D` and fills only x/z/w
  (`src/TinyGL.cpp:279-292`). `clipLine` clips against planes `w+x`, `w-x`, `w+z`
  (`src/TinyGL.cpp:459-514`). `projectVerts`: `x = viewportXBias + x*viewportXScale/w`,
  `z = 0x7fffff/w` (`src/TinyGL.cpp:516-525`). y, s and t are irrelevant here.
* `mvp2D = view2D × projection(viewFov + |pitch|, viewAspect)`. `view2D =
  buildViewMatrix(viewX, viewY, 0, yaw, 0, 0)`. The pitch is signed (`>512 → −1024`). The
  projection is rebuilt fresh, without the GLES negations (`src/TinyGL.cpp:189`, `:192-200`).
* Viewport: `setViewport(viewRect[0], …)` gives `posX = 0` and calls `_setViewport(1, …, 478, …)`
  (`src/TinyGL.cpp:149-166`). So `viewportXScale = 478<<2 = 1912` and `viewportXBias =
  ((1 + 239)<<3) − 4 = 1916` (`src/TinyGL.cpp:141`, `:143`). The cinematic viewport has the
  same x and width (`cinRect[0]=viewRect[0]`, `cinRect[2]=viewRect[2]`,
  `src/Canvas.cpp:151-154`), so the X projection is identical in ST_CAMERA. Only x is used.
* Columns: `TinyGL::screenWidth = Render::screenWidth = viewRect[2] = 480`
  (`src/App.cpp:111`, `src/Render.cpp:156`, `src/Canvas.cpp:126`). `columnScale[480]` is reset
  to `COLUMN_SCALE_INIT = INT_MAX` every frame before the BSP walk (`src/Render.cpp:2315-2317`,
  `src/TinyGL.h:39`).
* What writes `columnScale` (= what occludes). Writes happen only during the walk, only from
  VISITED (non-culled) leaves, in walk order (near child first), and each leaf writes AFTER its
  own cull test:
  1. `drawNodeLines(leaf)` (`src/Render.cpp:717-744`; `skipLines` is always false,
     `src/Render.cpp:114`). For `i` in `[n2 & 0x3FF, (n2 & 0x3FF) + (n2 >> 10 & 0x3F))` with
     `n2 = nodeChildOffset2[leaf]`, the line type is `t = lineFlags[i>>1] >> ((i&1)<<2) & 7`.
     If `t == 0 || t == 4`, then occlude endpoints `((lineXs[2i]&0xFF)<<7, (lineYs[2i]&0xFF)<<7)`
     → `((lineXs[2i+1]&0xFF)<<7, (lineYs[2i+1]&0xFF)<<7)` in that order, with NO swap
     (back-facing lines give `endX <= begX` and write nothing). The `|= 8` automap write
     (`:735-740`) is not ported.
  2. Leaf sprites (`src/Render.cpp:1063-1080`): sprite `s` in the leaf's own chain with
     `!(info & 0x10000)`, an entity bound (`S_ENT != -1`), `info & 0x400000` (SPRITE_FLAG_TILE),
     and `(info & 0xFF) + 257 < 450`. These go to `occludeSpriteLine(s)`
     (`src/Render.cpp:666-715`), which returns at once if `info < 0` (DOORLERP bit 31). `n6 =
     EAST(0x4000000)?4 : NORTH(0x1000000)?6 : WEST(0x8000000)?4 : SOUTH(0x2000000)?6 : 0`, tested
     in this order. `n7 = ((n6 + 2) & 7) << 1`; `dx = viewStepValues[n7] >> 1 << 4`, `dy =
     viewStepValues[n7+1] >> 1 << 4`. The segment is `(x+dx, y+dy)` → `(x−dx, y−dy)` with
     `x = spriteX<<4`, `y = spriteY<<4`. After projection, swap the ends if `v0.x > v1.x`.
* `occludeClippedLine` / `clippedLineVisCheck` (`src/TinyGL.cpp:689-743`) are quoted in
  6.5.3. Every caller relevant here passes `b = true` (`src/Render.cpp:1053`, `:2456`,
  `src/Entity.cpp:1095`).
* No per-sprite cull exists beyond the leaf cull. `renderBSP` (`src/Render.cpp:1732-1772`)
  calls `renderSpriteObject` for every sprite that `addNodeSprites` lists for each visited leaf
  (`:913-924`, own chain + split pairs). `addSprite` only rejects hidden sprites (`:830-832`).
  `renderSpriteObject` reaches the wake (`:1576-1583`) for every non-hidden sprite except the
  water stream tile 240 (`:1546-1549`, never a monster) and the mastermind/caldex delays
  (`:1519-1541`; the sprite is still rendered later, `:1764-1771`).
* `checkTileVisibilty(tx, ty) = !cullBoundingBox(tx<<10, ty<<10, (tx+1)<<10, (ty+1)<<10, true)`
  (`src/Render.cpp:2453-2457`). It is evaluated during simulation, with the last frame's
  camera and its final `columnScale`.

#### 6.5.2 `render/Camera3D.h/.cpp` — add `mvp2D`

* Private member `int mvp2D_[16];` (zero-init in the constructor). Public accessor
  `const int* mvp2DInt() const { return mvp2D_; }` with the comment `// legacy TinyGL::mvp2D
  (src/TinyGL.cpp:189, :195-200): yaw-only view × projection(fov+|pitch|), no GLES tweaks`.
* At the END of `setView` (the existing outputs do not change):
  ```
  int view2D[16], proj2D[16];
  buildViewMatrix(viewX, viewY, 0, viewYaw_, 0, 0, view2D);
  int p = viewPitch_; if (p > 512) p -= 1024;               // src/TinyGL.cpp:192-194
  buildProjectionMatrix(viewFov + std::abs(p), viewAspect, proj2D);
  multMatrix(view2D, proj2D, mvp2D_);
  ```
  (`<cstdlib>` for `std::abs`).

#### 6.5.3 New `render/ColumnOcclusion.h/.cpp`

```cpp
namespace newcore {
// CPU port of the 2D column-occlusion cull of legacy TinyGL/Render (ADR 0031).
// Bookkeeping only: never feeds the GPU draw path.
class ColumnOcclusion {
public:
	static constexpr int kColumns = 480;      // TinyGL::screenWidth (src/App.cpp:111, src/Render.cpp:156)
	static constexpr int kXScale = 478 << 2;  // viewportXScale (src/TinyGL.cpp:141, :166)
	static constexpr int kXBias = ((1 + 478 / 2) << 3) - 4; // viewportXBias (src/TinyGL.cpp:143)
	static constexpr int kCullExtra = 256 + 16;             // TinyGL::CULL_EXTRA (src/TinyGL.h:37)
	static constexpr int kColumnInit = INT_MAX;             // COLUMN_SCALE_INIT (src/TinyGL.h:39)

	// New frame: latch the camera, reset every column (src/Render.cpp:2315-2317).
	void begin(const int* mvp2D, int viewX, int viewY);
	// occludeClippedLine for a world-space segment (world units = canvas << 4).
	// sortX: swap the projected ends so x0 <= x1 (occludeSpriteLine, src/Render.cpp:708-713);
	// false for map lines (drawNodeLines, src/Render.cpp:735).
	void occludeLine(int x0, int y0, int x1, int y1, bool sortX);
	// Render::cullBoundingBox(n, n2, n3, n4, true) (src/Render.cpp:750-823): true = culled.
	bool cullBox(int x1, int y1, int x2, int y2) const;
private:
	struct V { int x, z, w; };
	// transform2DVerts + clipLine + projectVerts for two points; false = clipped away.
	bool project(int ax, int ay, int bx, int by, V& a, V& b) const;
	int mvp2D_[16] = {};
	int viewX_ = 0, viewY_ = 0;
	int columnScale_[kColumns];
};
}
```

Algorithms (verbatim, ints):

* `begin`: copy 16 ints, store viewX/viewY, fill `columnScale_` with `kColumnInit`.
* `project` (`src/TinyGL.cpp:279-292`, `:459-514`, `:516-525`). For each point:
  `x' = (x*m[0] + y*m[4] >> 14) + m[12]`, `z' = (x*m[2] + y*m[6] >> 14) + m[14]`,
  `w' = (x*m[3] + y*m[7] >> 14) + m[15]`. Then the three-plane `clipLine` loop exactly as in
  `src/TinyGL.cpp:462-512` (`n`/`n2` per plane i=0: `w+x`, i=1: `w−x`, i=2: `w+z`; both
  negative → return false. Case 1 (`a` out): `t = (n2<<14)/(n2−n)`, `a.f = b.f + ((a.f−b.f)*t
  >> 14)` for f in x, z, w. Case 2 (`b` out): `t = (n<<14)/(n−n2)`, `b.f = a.f + ((b.f−a.f)*t
  >> 14)`). Then for both: `x = kXBias + (x*kXScale)/w; z = 0x7fffff / w`.
  (`w >= 256` after the near plane, so there is no division by zero.)
* `occludeLine`: `if (!project(...)) return;` `if (sortX && a.x > b.x) std::swap(a, b);` then
  `occludeClippedLine(a, b)` (`src/TinyGL.cpp:719-743`):
  ```
  begX = a.x + 7 >> 3; endX = b.x + 7 >> 3;
  if (begX < 0) begX = 0; if (endX > kColumns) endX = kColumns; if (endX <= begX) return;
  n = (b.z - a.z) / (endX - begX); n2 = a.z + (n * ((begX << 3) - a.x) >> 3);
  for (; begX < endX; ++begX, n2 += n) { n3 = 0x7fffff / n2; if (n3 < col[begX]) col[begX] = n3; }
  ```
* `cullBox(n, n2, n3, n4)` (`src/Render.cpp:750-823`):
  1. Camera inside the box grown by `kCullExtra` on all sides → return false.
  2. Pick the silhouette segment (p0, p1) with the 9-way table of `src/Render.cpp:759-815`,
     copied exactly. `viewY_` inside `[n2, n4)` while `viewX_` is inside `[n, n3)` → return
     false.
  3. `if (!project(p0, p1, a, b)) return true;` then return `!visCheck(a, b)` where `visCheck`
     is the `b == true` branch of `clippedLineVisCheck` (`src/TinyGL.cpp:689-710`): the same
     begX/endX/clamp (`endX <= begX` → false), the same `n`/`n2` start, and per column
     `if (0x7fffff / n2 < col[begX]) return true;` → false after the loop. Do NOT swap a/b
     here; legacy does not.

#### 6.5.4 `render/World3D.h/.cpp`

* Members: `ColumnOcclusion occl_;`, `std::vector<int> legacyLeaves_;` (visited leaves in walk
  order) and `std::vector<int> legacySplitPairs_;` (flat `[leaf, sprite]`, cap
  `2*kMaxSplitSprites`). Clear both per frame next to `splitPairs_.clear()`.
* `drawBSP` gets a new trailing parameter `const uint8_t* spriteHasEntity = nullptr`
  (1 = sprite bound to an entity, the `S_ENT != -1` analog; null = none).
  Store it in a member `const uint8_t* hasEntity_` for the walk's duration.
* `addSplitSprite(map, firstVisibleLeaf, sprite)` becomes
  `addSplitSprite(map, const std::vector<int>& leaves, size_t first, int sprite, std::vector<int>& pairs)`
  with the same body over `leaves`/`pairs`. The existing call passes `nodeIdxs_, start, s,
  splitPairs_`, so draw behaviour is identical.
* `walkNode(map, n, viewX, viewY, viewZ, bool legacyVis)`; `drawBSP` calls it with `true`
  right after `occl_.begin(camera.mvp2DInt(), camera.viewX(), camera.viewY())`. Body:
  ```
  if (legacyVis && occl_.cullBox((nodeBounds[n*4+0]&0xFF)<<7, (nodeBounds[n*4+1]&0xFF)<<7,
                                 (nodeBounds[n*4+2]&0xFF)<<7, (nodeBounds[n*4+3]&0xFF)<<7))
      legacyVis = false;                         // src/Render.cpp:1053-1055: legacy stops here
  if (leaf) {
      nodeIdxs_.push_back(n);                    // unchanged: draw list
      if (legacyVis) { legacyLeaves_.push_back(n); occludeLeafLines(map, n); occludeLeafSprites(map, n); }
      return true;
  }
  size_t start = nodeIdxs_.size(), lstart = legacyLeaves_.size();
  ... recurse both children in the existing order, passing legacyVis ...
  for (int s : internalSprites_[n]) {
      addSplitSprite(map, nodeIdxs_, start, s, splitPairs_);                       // unchanged
      if (legacyVis) addSplitSprite(map, legacyLeaves_, lstart, s, legacySplitPairs_); // :1094-1096
  }
  ```
  Replace the "cullBoundingBox early-out is skipped" comment with one line: the draw walk
  visits every node, and the `legacyVis` flag reproduces the legacy cull for visibility only
  (ADR 0031).
* `occludeLeafLines(map, leaf)` and `occludeLeafSprites(map, leaf)` are private helpers
  implementing 6.5.1 items 1 and 2. Sprites are those with `spriteLeaf_[i] == leaf`, ascending
  `i` (chain order does not matter: min-writes commute). Use `Player::kViewStepValues`
  (`new_src/domain/game/Player.cpp:15`, value-identical to `Canvas::viewStepValues`).
  `hasEntity_ == nullptr` → no sprite occluders.
* After the walk, replace the leaf-bounds tile loop (`World3D.cpp` "Tiles touched by the
  visible leaves") with:
  ```
  for (int ty = 0; ty < 32; ++ty) for (int tx = 0; tx < 32; ++tx)
      if (!occl_.cullBox(tx << 10, ty << 10, (tx + 1) << 10, (ty + 1) << 10))  // checkTileVisibilty, src/Render.cpp:2453-2457
          vis_.tiles[ty] |= 1u << tx;
  ```
* Then build `vis_.drawnSprites` (and delete the push in the draw loop at `World3D.cpp:1346`):
  ```
  for (int k = (int)legacyLeaves_.size() - 1; k >= 0; --k) {   // renderBSP order, src/Render.cpp:1752
      const int leaf = legacyLeaves_[k];
      for (int i = 0; i < map.numSprites; ++i)
          if (spriteLeaf_[i] == leaf && !(map.mapSpriteInfo[i] & 0x10000)) vis_.drawnSprites.push_back(i);
      for (size_t p = 0; p + 1 < legacySplitPairs_.size(); p += 2)
          if (legacySplitPairs_[p] == leaf) vis_.drawnSprites.push_back(legacySplitPairs_[p + 1]);
  }
  ```
* Keep the `ViewVisibility.h` field comments in sync: `drawnSprites` = "sprites the legacy
  renderBSP would submit (visited leaves + split rescue)", `tiles` = "checkTileVisibilty per
  tile".

#### 6.5.5 `render/SceneRenderer.h/.cpp`

Add member `std::vector<uint8_t> spriteHasEntity_;`. Next to `spriteCharClass_.assign(...)`,
call `spriteHasEntity_.assign(numSprites, 0)`. In the entity loop, right after `si` is
computed and BEFORE the existing `continue`, add
`if (si >= 0 && si < numSprites) spriteHasEntity_[si] = 1;`. Pass `spriteHasEntity_.data()` as
the new last `drawBSP` argument.

#### 6.5.6 Build / acceptance

New `.h/.cpp` pair ⇒ reconfigure (`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`).
* Golden frames (`tests/golden/frames/README.md`, `tools/compare_frames.py`) are
  **byte-identical**. Any difference is a FAIL: the draw list must not have changed.
* Self-check (keep): `assert(!legacyLeaves_.empty())` after the walk when `map.numNodes > 0`.
  Every ancestor of the camera's leaf contains the camera (within `kCullExtra`), so the root
  is never culled. A failure means the silhouette table or the projection is wrong.
* User check, map00, without shooting:
  (a) Stand with an imp 2-3 tiles BEHIND you (turn away from it): it does NOT wake
  (previously it did).
  (b) Turn to face it: it wakes (`[monster] activate sprite=`) on the next frame.
  (c) An imp within 4 tiles behind a closed door in front of you does not wake until the door
  is open.
* Optional log for the reviewer: one stderr line per second with
  `legacyLeaves_.size()/nodeIdxs_.size()` and the popcount of `vis_.tiles`. Facing a wall up
  close, both numbers must drop sharply.

---------------------------------------------------------------------------------------------
## 7. G6 — Monster movement

### 7.1 `MonsterAI::goalMove(e)` — verbatim port of `src/Entity.cpp:1030-1116` (MM §2)

Search state becomes a private struct `PathSearch { uint64_t closestPath, curPath; int
closestPathDepth, closestPathDist, pathDepth, pathSearchDepth, lineOfSight, lineOfSightWeight,
interactClipMask; Entity* findEnt; Entity* skipEnt; uint32_t visitedTiles[32]; }` on
`MonsterAI` (the legacy `Game` scratch fields). Steps:

1. `lerps->snap(sprite)` (can fire `finishLerp` → maybe `attack`).
2. Reset: `closestPath = 0, closestPathDepth = 0, closestPathDist = 999999999, curPath = 0,
   pathDepth = 0, pathSearchDepth = 8, findEnt = nullptr, skipEnt = e, lineOfSight = 2,
   lineOfSightWeight = 0, interactClipMask = 32 (Contents::DOOR)`, `visitedTiles =
   baseVisitedTiles_`.
3. Goal by type exactly as `:1050-1074` (chase player: `findEnt = playerEnt`, goal =
   `player->destX>>6, destY>>6`, weight −4; type 5: goal = player dest tile, clip 0, flee,
   LOS 1, depth = goalParam; type 4: goal = player dest tile, flee, LOS 1; type 2/k: `findEnt =
   &entities[k]`, goal = its tile). Flee ⇒ `closestPathDist = 0`.
4. `ok = calcPath(sX>>6, sY>>6, goalX, goalY, MONSTERSOLID, flee)`; if `!ok &&
   closestPathDist < 999999999` → use the best partial path.
5. `ok && pathDepth > 0`: `curPath >>= 64 - pathDepth*2`; `info &= ~kInfoOnScreenStep`;
   `dir = curPath & 3`; `dX = sX + Player::kViewStepValues[dir*4 + 0]`, `dY = sY +
   Player::kViewStepValues[dir*4 + 1]` — the rewrite table is value-identical to
   `Canvas::viewStepValues` (`src/Canvas.h:108`), and stride 4 selects the four cardinal
   entries (+64,0), (0,−64), (−64,0), (0,+64) for dir 0..3 (MM §2.1). The same indexing is used
   inside `calcPath` (`viewStepValues[(b2 << 2) + 0/1] >> 6` for tile steps). `goalX = dX>>6,
   goalY = dY>>6`. `h = trace(sX, sY, dX, dY, e, interactClipMask, 25)`:
   * `!h.blocks()`: `db->unlinkEntity(e); db->linkEntity(e, dX>>6, dY>>6);` on-screen test →
     `if (visibility && visibility->valid && (tileVisible(sX>>6,sY>>6) || tileVisible(dX>>6,
     dY>>6))) info |= kInfoOnScreenStep;` (deviation D4); `monsters->interpolatingMonsters =
     true; initLerp(e, (sub == 13 || (sub == 14 && parm != 0)) ? 500 : 275);`
   * else: `goalX = sX>>6, goalY = sY>>6; if (h.isEntity() && h.eType == ET_DOOR)
     doors->performDoorEvent(0, h.entity, 2);`
   * return true.
6. Else return false.

`calcPath(n, n2, n3, n4, mask, flee)` — verbatim recursive port of `src/Entity.cpp:908-1028`,
with these mechanical translations:

* `visitOrder`/`visitDist` become local arrays of 4 (the legacy globals are packed into a
  local before recursion, so locals are equivalent).
* `curPath` is `uint64_t`: push = `curPath = (curPath >> 2) | ((uint64_t)dir << 62)`, pop =
  `curPath <<= 2` (identical bits to the legacy signed shift + mask).
* `findMapEntity(x<<6, y<<6, 256)` → `firstOnTile(x, y, Contents::ENV_DAMAGE) != nullptr`.
* Edge trace `trace((n<<6)+32, (n2<<6)+32, (n22<<6)+32, (n23<<6)+32, skipEnt, mask, 16)`;
  `findEnt && h.entity == findEnt` → success; `clip = h.blocks() ? (1 << h.eType) :
  interactClipMask` (a world hit yields `1 << 0`, same as the legacy world entity); recurse iff
  `clip == 0 || (clip & interactClipMask)`.
* Tie order start `b2 = nextByte() & 3`, then `b2 = (b2 + 1) & 3` four times.
* `checkLineOfSight(x0, y0, x1, y1, mask)` — **port verbatim including the dead loop**
  (`src/Entity.cpp:884-906`); effective result "same row or same column" (MM §6, decision Q2).

`initLerp(e, travelTime)` — `src/Entity.cpp:852-871`:
`SpriteLerp* ls = lerps->allocLerpSprite(nullptr, sprite, true);` `src = current S_X/S_Y`,
`srcZ = storedZ + lerps->spriteZBias(sprite, sx, sy)` (baked space), `dst = 32 + (goal<<6)`,
`dstZ = 32 + map->heightAt(dstX, dstY)`, scale unchanged, `startTime = lerps->clockMs()`,
`travelTime`, `flags |= kFlagEntNoRelink | kFlagAsync`, `m->frameTime = lerps->clockMs() +
travelTime`, `ls->calcDist()`, `goalFlags |= kGoalFlagLerping`.

`finishLerp(e)` — `src/Entity.cpp:873-882`; `reachedGoalMove(e)` — `src/Entity.cpp:1118-1153`
verbatim (clear `kInfoOnScreenStep`; MOVE2ATTACK / evading-without-ability → `weaponForTarget`
→ `attack`; evading → `flags |= MFLAG_ABILITY`; the `kGoalFlagRepath` branch is dead but port it
with `tileObstructsAttack` stubbed to `true` → `resetGoal`, log).

### 7.2 `SpriteLerps` changes

* `SpriteLerp::kFlagEntNoRelink = 0x10` (`src/Enums.h:297`). With it set, skip the per-tick
  relink (`SpriteLerps.cpp:275-281`) and the completion relink (`:299-303`).
* `Env` gains `MonsterAI* ai`. In `freeLerpSprite`, inside the NPC/MONSTER block add
  `if (ent->monster) ent->monster->frameTime = 0;` (`src/Game.cpp:3102-3104`); **after**
  `ls->hSprite = 0` add `if (ent && ent->monster && (ent->monster->goalFlags &
  kGoalFlagLerping) && env_.ai) env_.ai->finishLerp(ent);` (`src/Game.cpp:3211-3216`, slot freed
  first — legacy order). Wire `&ai` in `Game::loadEntities` (`lerps.init` moves after `ai`
  exists, or `ai` is passed by address before init — both are members of `Game`).
* The walk-frame writer and the alloc-reuse idle reset already handle `ET_MONSTER`
  (`SpriteLerps.cpp:92-107`, `:240-272`): the 1-tile step always selects WALK_FRONT, 4 phases
  per tile (MM §5). Update the "dormant" comment.

### 7.3 `TraceSystem` — lerping-monster branch

`traceEntityHits`, before the `ET_PLAYER` test (`src/Game.cpp:227-237`): `if (ent->monster &&
(ent->monster->goalFlags & kGoalFlagLerping)) { cx = 32 + (goalX << 6); cy = 32 + (goalY << 6);
}` — replaces the "Monster goal-lerp branch omitted" comment.

### 7.4 Monster clip script ops (`ScriptVM.cpp:1162-1169`)

`EV_MARKTILE`: `flags = (packed >> 10) & 0x3F; x = (packed >> 5) & 0x1F; y = packed & 0x1F;
if (flags & 1) game->ai.setMonsterClip(x, y);` (`src/ScriptThread.cpp:1152-1160`).
`EV_UNMARKTILE`: `if (flags & 1) unsetMonsterClip(x, y)` (`:1752-1761`). The automap-entrance
bit (0x20) stays a log.

Note: `MonsterAI::init` rebuilds the bitset per level; script marks happen after init (during
`SCR_INIT_MAP`), so they survive — keep `ai.init` in `loadEntities` before
`executeStaticFunc(SCR_INIT_MAP)` (it already is: `tickLoading` order).

### 7.5 Input and turn flow

No new code beyond what G2 put in place: `snapMonsters` now really snaps off-screen steps and
refuses input while an on-screen step animates; `monsterLerp`/`updateMonsters` wait for all
steps before attacks start (`lerps.animatingEffects()` gate in §3.5).

### 7.6 Acceptance (G6)

* Wake an imp two or more tiles away (shoot it): each turn it steps one tile toward you with a
  walk animation of about a quarter second, never diagonally, and claws once adjacent and
  aligned. While a visible monster walks, your key presses are ignored until it arrives (turning
  still works).
* Several woken monsters walk simultaneously; attacks start after all have arrived.
* A monster behind a closed door opens it (that turn it does not move), walks through next turn.
* Monsters do not step onto your tile, onto each other, or through walls; they stop at
  script-marked monster-clip tiles.
* A woken monster out of view does not slow you down (its steps are snapped).
* Performance watch item: no visible hitch when ~10 monsters are awake (the path search runs
  up to ~435 traces per monster per turn; report if a turn stalls).

---------------------------------------------------------------------------------------------
## 8. G7 — Runtime sprites and monster missiles 6/7 (ADR 0030)

### 8.0 Pre-check (do first, report before coding the rest)

Confirm that map00's media list covers tiles 227 (flesh), 242 (fireball) and 245/246/247
(claw/bite/blunt overlays): for each tile `t`, `media.mappings().mappings[t]` must be ≥ 0 and
`World3D::ensureSpriteTexture(media, t, id)` must succeed. If one is missing, stop and report:
the loader then needs the extra tile ranges registered before `finalize` (a separate decision).

### 8.1 `MapData` / `MapParser` / spawn loops

* `MapData`: `int numMapSprites = 0; static constexpr int kMaxCustomSprites = 48;
  int firstCustomSprite() const { return numMapSprites; }`.
* `MapParser.cpp:95`: `out.numMapSprites = numMapSprites; out.numSprites = numMapSprites +
  MapData::kMaxCustomSprites;` All per-field fill loops keep running to `numMapSprites` (they
  already do) and index with `out.numSprites` as stride (they already do). After the fills, for
  each custom slot `i`: `mapSpriteInfo[i] = 0x10000` (hidden — the rewrite has no `S_NODE = -1`
  unlink, so hidden is how a free slot stays invisible), `S_X = S_Y = 0`, `S_Z = 32`,
  `S_SCALEFACTOR = 64`, `S_RENDERMODE = 0` (`src/Render.cpp:2497-2508`). Audit the rest of
  `MapParser` for any array sized by the old count.
* `Game::loadEntities` loops at `Game.cpp:52, 70, 95` iterate `map.numMapSprites`.

### 8.2 New module `domain/game/GameSprites.h/.cpp`

```
class GameSprites {
public:
	static constexpr int kMax = 48;                          // src/Render.h:53
	struct Sprite {                                          // legacy GameSprite subset
		int slot = -1;           // map sprite index (firstCustomSprite + i)
		int flags = 0;           // 0x1 alive, 0x2 propagator, 0x4 no-relink, 0x40 frame anim,
		                         // 0x200 keep after duration, 0x400 scale lerp, 0x800 unlink on
		                         // destroy, 0x1000 view-relative, 0x2000/0x4000/0x8000 drop/bounce (not ported)
		int pos[6] = {};         // start xyz, end xyz (legacy baked Z)
		int vel[3] = {};         // units per 1000 ms
		int duration = 0;
		int64_t time = 0;        // upTimeMs at alloc
		int numAnimFrames = 0;
		int startScale = 64, destScale = 64, scaleStep = 0;
	};
	struct Env { MapData* map = nullptr; const SpriteLerps* lerps = nullptr; const Player* player = nullptr; };
	void init(const Env& env);                                // all free; slots hidden
	Sprite* alloc(int tile, int frames, int flags, int64_t now);           // src/Game.cpp:1294-1325
	Sprite* allocAnim(int tile, int x, int y, int z, int64_t now);        // src/Game.cpp:1327-1401 (cases 242, 245/246/247; others default)
	void destroy(Sprite* s);                                  // src/Game.cpp:1403-1462, 0x1/0x800 path only
	void clear(int flagMask);                                 // src/Game.cpp:1283-1292
	void update(int64_t now);                                 // src/Game.cpp:1464-1570
	int activePropogators() const;  int activeSprites() const;
private:
	void write(const Sprite& s, int x, int y, int zBaked);   // S_X/S_Y + stored Z = zBaked - spriteZBias
	Env env_; Sprite sprites_[kMax]; int activeSprites_ = 0, activePropogators_ = 0;
};
```

`update(now)` ports the loop for the ported bits: `n2 = now - time`; `n2 < 0` → count; else if
`!(flags & 0x200) && n2 >= duration` → `destroy`; else count, `0x40` → frame `n2/200 %
numAnimFrames` into bits 8-15; `0x1000` view-relative (positions relative to `player->viewX +
viewStepX` etc., `src/Game.cpp:1492-1521`; the rewrite's `viewStepX/Y` and `viewRightStepX/Y`
are the legacy ±64 cardinal values); else `0x2` propagator: linear `pos + vel*n2/1000`, on
`n2 >= duration` write the end position and clear `0x2`; `0x400` scale lerp. Relink calls are
no-ops (the renderer recomputes leaves every frame). `destroy` sets the hidden bit and clears
`0x1`.

`Game` owns `GameSprites sprites;` (init in `loadEntities`); `GameContext::tick` calls
`sys_.game->sprites.update(upTimeMs)` in the globals section every tick (legacy global loop).

### 8.3 `Combat` — missiles

* State: `GameSprites::Sprite* activeMissiles[8]; int numActiveMissiles; int dodgeDir;`.
* `launchProjectile()` — `src/Combat.cpp:1433-1668`: keep the default arm first for
  `projType <= 1` (no roll). For positive types: `dodgeDir = (std::rand() & 0x7FFFFFFF) >
  0x3FFFFFF`, `n5 = (dodgeDir*2 - 1) * 16`; start/end points (`:1448-1478`; attacker Z = stored +
  `spriteZBias`; target player = `viewX, viewY, heightAt + 32`); **cases 6 and 7 only**
  (6: rm 0, anim 227; 7: rm 3, anim 242); every other positive type keeps today's logged
  instant fallback; the out-of-range / miss / hit end adjustments and the major-axis push-outs
  verbatim (`:1577-1630`); `allocMissile` (`:1670-1717`): max 8, `sprites.alloc(anim, 0, 2562,
  now)`, positions, `vel = 1000*(end-start)/duration`, `duration = 1000*max(dx,dy)/256`;
  frame bits: `242` → frame byte 0x2000 (`:1641-1643`), then `if (!b) info &= 0xFFF700FF`
  (b is false for 6/7 → frame and AUTO_ANIMATE cleared); `exploded = false`.
* `updateProjectile()` head (`src/Combat.cpp:1250-1420`), for the ported types: per missile,
  flying (`flags & 0x2`) → nothing (248/171 branches not ported); arrived → impact: case 7 →
  log `[sound] 1034`, anim 242 rm 4 (antifire branch absent); default (6) → anim 0; destroy
  the missile; spawn the impact anim iff `anim != 0 && (curAttacker == nullptr || (curTarget ==
  nullptr && hitType != 0))` with `flags |= 0x800`, `nextStageTime = gt + duration`, render mode,
  scale 64, mirror bit inherited; compact the array; `exploded = true`.
  Melee overlay branch (`:1401-1420`) when no missiles: weapon 16 → 245, 15 → 246, 17/18 → 247,
  only if `isInFront(attacker tile)` (`src/Game.cpp:3583-3594`, port as a private helper using
  `SpriteLerps::vecToDir`); `allocAnim(anim, destZ, destY, destZ)` (verbatim argument quirk —
  the view-relative case overwrites the position), `flags |= 0x800`, render mode 5, 245 inherits
  the mirror bit.
* `monsterSeq` stage 1: `sprites.clear(0x40)` (`gsprite_clear(64)`).

### 8.4 Gates

* `GameContext` step-4 gate and `renderActivate` gate and the input gate gain
  `sprites.activePropogators() == 0` (`src/GameStateRunner.cpp:181`, `src/Render.cpp:1578`,
  `src/PlayingInputHandler.cpp:72`).
* `monsterSeq` B already reads `numActiveMissiles`.

### 8.5 Acceptance (G7)

* Woken zombie 2-3 tiles away on your row: after the wind-up a flesh chunk flies at you
  (~¼ s per tile) and the damage lands when it arrives. On a miss it flies past to one side.
* Imp (3,1) (the rarer, darker imp) at 2-3 tiles: a fireball flies, and on a hit a fire burst
  shows for ~0.8 s before the imp resumes; "Imp did N damage!" afterwards.
* An imp claw that hits while the imp is in front of you shows claw marks across the view for
  0.2 s.
* Missiles are hidden behind walls/pillars correctly (BSP order), never drawn through them.
* Intro/golden frames unchanged (the 48 extra slots are hidden).

---------------------------------------------------------------------------------------------
## 9. G8a — Death sequence and the dead menu

### 9.1 Research to request before coding (orchestrator)

* R1: what `BACK` does on `MENU_INGAME_DEAD` (51) in `src/MenuSystem.cpp` (does it return to
  the game?). Until answered, `BackKey` is ignored on menu 51.

### 9.2 Changes

* **Fade** on `Hud` (next to shake/rock): `startFade(int64_t now, int durMs, int flags)`,
  `bool isFading(int64_t now) const`, `void drawFade(Graphics2D& g, int64_t now)` — FADEOUT (1):
  black 480×320 with `alpha = 255*t/dur`, held at 255 after completion (`src/Render.cpp:2548-2587`,
  PD §3.3). Drawn last in `GameContext::render` (before the menu, which is opaque anyway).
* **`Player`**: `int viewRoll = 0;` `currentLevelDeaths`, `totalDeaths` counters.
  `SceneRenderer` passes `viewRoll` into `camera_.setView(..., roll, ...)` for the player branch
  (today a literal 0, `SceneRenderer.cpp:108`).
* **`enterState_(Dying)`** (`src/Canvas.cpp:1113-1129` + `src/Player.cpp:711-733`): counters++,
  log `[sound] 1090`, `hud->startShake(upTimeMs, 350, 10)`, health 0, clear queue (G3), stateVars
  already zeroed, `deathTimeMs_ = upTimeMs`, `player->destPitch`-analog: none (pitch driven
  below), `dialogs->clearHelpQueue()` (new: `numHelpMessages_ = 0`).
* **`tickDying()`** (`src/GameStateRunner.cpp:257-281`), `t = upTimeMs - deathTimeMs_`:
  * `t < 750`: `n = ((750 - t) << 16) / 750; viewZ = heightAt(destX, destY) + 18 + (20*n >> 16);
    viewPitch = 96 + (-96*n >> 16); viewRoll = 16 + (-16*n >> 16); updateView();`
  * `t < 2750`: first time → `hud->startFade(now, 2000, 1)`; `viewRoll = 16`.
  * else: `setState(StateId::Menu)` with menu 51: add `MenuSession::begin(int menuId =
    kMenuInGame)`; `enterState_(Menu)` passes the id through a member set by the caller.
  * The pitch sign: legacy positive pitch looks down; confirm against the rewrite's
    `viewPitch` ("positive = up", `Player.h`) — if opposite, negate here (coder verifies with the
    loot crouch, which is known to look down).
* **Render in Dying**: world from the player view (fov 290), no HUD, no weapon (already the
  case: `gameplayView` excludes Dying), plus the fade.

### 9.3 Acceptance (G8a)

Die to monsters: the view drops toward the floor while tilting and rolling for ¾ s, the screen
fades to black over 2 s, then the "Try Again?" menu appears with Load Saved Game / Restart Level
/ Main Menu. The dead menu's rows behave as the existing menu refusals dictate until G8b.

---------------------------------------------------------------------------------------------
## 10. G8b — Restart Level

`ACTION_RESTARTLEVEL` (12) → `loadState(2, …)` = the level-start brief save
(`src/MenuSystem.cpp:3105-3111`, PD §3.4). No save system exists; for map00 the level-start state
is a fresh start, so:

* `GameContext::Init` gains `std::function<bool()> reloadMap` (Main: re-read `map00.bin` and
  `MapParser::parse` into `g_map`; textures stay valid — same media).
* New `GameContext::restartLevel()`: `player->reset(); reloadMap(); hud->clearMessages();
  cinematic_.reset-equivalent; combat state reset (active=false, curAttacker=nullptr,
  numActiveMissiles=0); setState(StateId::Loading)`. `tickLoading` does the rest (VM pool reset,
  `loadEntities` re-inits every peer, spawn, `SCR_INIT_MAP`).
* `MenuSession`: enable `kActionRestartLevel` (both the dead menu and the in-game confirm) and
  call `ctx->restartLevel()` through a new `Env` hook.
* Deviation D6: the brief save is replaced by a fresh level start (identical on map00; wrong on
  later maps — revisit with the save system).

Acceptance: after dying, "Restart Level" puts you back at the map00 start with full health, the
intro/level scripts run again, all monsters asleep at their original places.

---------------------------------------------------------------------------------------------
## 11. Deliberate deviations (keep this list in sync with code comments)

| # | Deviation | Why |
|---|---|---|
| D1 | Target-practice teleport relinks the player entity | legacy leaves a stale link (`src/Player.cpp:2544`) |
| D2 | No `StateId::Combat`; `Combat::active` inside Playing | ADR 0028 |
| D3 | Sight-wake trace is 2D (no Z check) | rewrite trace has no Z variant (`src/Game.cpp:277-283`) |
| D4 | "On-screen step" = `tileVisible(src) || tileVisible(dst)`, where a tile bit is legacy `checkTileVisibilty` (§6.5). Legacy culls the ±16-unit union box of the step instead (`src/Entity.cpp:1095`). That box lies inside the two tiles, so the rewrite errs toward animating | ADR 0029, ADR 0031 |
| D5 | Sight wake one frame late. Sight-wake lists from frames drawn in Dialog are consumed only on the next Playing tick and only the last frame's list counts (legacy wakes during the dialog frames); the trace origin is the current tick's player view, not the rendered frame's | ADR 0029 |
| D6 | Restart Level = fresh level start | no save system |
| D7 | `dodgeDir` roll only on the missile path | ADR 0028 decision 5 |
| D8 | Weapon 18 lunge lerps absent; CHARGE runs as a plain melee attack | not on map00 |
| D9 | Script threads are swept (finished slots freed) only in Playing/Camera; legacy sweeps in every state except the sound-enable menu (`src/Canvas.cpp:791-796`), so a finished BLOCKINPUT thread may hold its slot through Dialog/Looting/Menu until the next Playing tick | pre-existing; no observable effect found (orchestrator decision, G3) |
| D11 | (G5, follows `src/` over §6.2) `ViewVisibility::cinematic` is `state == StateId::Camera` at render time, set in `GameContext::render` (`new_src/core/GameContext.cpp`), not `cinePose != nullptr` in `SceneRenderer`. Legacy `disableRenderActivate` is set on entering ST_CAMERA and cleared on leaving it (`src/Canvas.cpp:1211`, `:1038`), so a frame drawn from the camera pose in ST_DIALOG (dialog inside a cinematic) or in ST_PLAYING (surviving camera) still sight-wakes | faithful gate |
| D12 | (G5, residual after §6.5 / ADR 0031) Visibility follows the legacy cull exactly (CPU column-occlusion port), but (a) within one leaf `drawnSprites` is in ascending sprite index instead of legacy depth order, which matters only if two monsters of one leaf wake in the same frame and the first starts an animating effect; (b) the draw walk itself is still unculled (the GPU draws leaves the original skipped, which is invisible because they are occluded or off-screen) | golden frames stay byte-identical; ADR 0031 |
| D13 | (G6) Lerp pool exhausted on a monster step → instant placement on the goal tile (X/Y/stored Z as `freeLerpSprite` would write) + `reachedGoalMove`, `kGoalFlagLerping` stays clear (`new_src/domain/game/MonsterAI.cpp` `initLerp`); legacy is fatal Error 36 (`src/Game.cpp:3064-3066`, `src/Entity.cpp:856`) | no fatal errors in the rewrite; clears `kInfoOnScreenStep` so input cannot deadlock |

---------------------------------------------------------------------------------------------
## 12. Open questions for the user

**ANSWERED 2026-09-23 by the user:** Q1 → switch to difficulty **1 (Normal)** (done in G1,
§2.1a); Q2 → port the LOS quirk verbatim; Q3 → sound stays log-only; Q4 → golden-frame drift
accepted (re-capture after G2/G3/G6/G7). The original questions are kept below for history.

* **Q1 — Difficulty.** The rewrite stamps difficulty 2 ("Difficult") at load
  (`new_src/core/GameContext.cpp:320`); legacy's field default is 1 (`src/Game.cpp:43`) and the
  new-game menu chooses. At 2, monsters have +25 % HP (already accepted) and hit ×1.5 + mapID
  bonus (`src/Combat.cpp:984-990`) — an imp claw does 7 instead of 5. Keep 2 (spec default) or
  switch to 1 (Normal)?
* **Q2 — Line-of-sight quirk.** `checkLineOfSight` is effectively "same row or column"
  (MM §6); it only biases path choice toward aligned tiles. The spec ports it verbatim
  (faithful movement). Confirm, or ask for the "intended" blocking test.
* **Q3 — Sound.** All monster/player sounds are log lines only (no audio backend). OK for now?
* **Q4 — Determinism.** Awake monsters consume `std::rand()`; any saved/replayed input sequence
  in which a monster wakes will diverge from an older build. Golden-frame captures are re-run
  after G2, G3, G6, G7 — acceptable?

---------------------------------------------------------------------------------------------
## 13. Regression checklist (every group)

`cmake --build build_new -j 8` clean; intro plays to control; the golden-frame capture ticks
(`tests/golden/frames/README.md`) are re-captured and compared — differences must be explained
by the group; walk/turn/doors/loot/pickups/dialogs/menu unchanged; shooting and killing a monster
(XP message) unchanged; `D2R_AUTOTEST` headless run completes.
