# 2026-08-30 — Shelf pickup, crates, and the full blocking-sprite spawn

Design for three user complaints, in one dependency chain:

| # | Complaint (verbatim) | Group |
|---|---|---|
| a | "предметы на полках не берутся взглядом+действием, а их ~90%" | **G1** |
| b | "ящики должны не пускать игрока, открываться действием, с анимацией открытия" | **G3** (+ **G4** for the contents) |
| c | "прочие спрайты, которые в оригинале не пускают игрока, у нас проходятся насквозь" | **G2** |

Inputs (already verified, do NOT re-research):
`docs/research/2026-08-30-sprite-blocking.md`, `docs/research/2026-08-30-containers.md`,
`docs/research/2026-08-30-action-pickup.md`, `docs/original-code/entities.md` §6,
`docs/original-code/loot-inventory.md` §3.2 / §7.
Decisions: ADR 0015 (full legacy spawn rule), ADR 0016 (crate opening lives in the simulation).

## 0. Group order and why it is not "cheapest first" all the way

| Group | Files | Size | Depends on |
|---|---|---|---|
| **G1** shelf pickup — `EV_GIVEITEM` mode 0 | `domain/game/ScriptVM.cpp` | ~15 lines | nothing |
| **G2** full entity spawn (blocking families) | `domain/game/Game.cpp`, `Game.h` | ~90 lines | nothing |
| **G3** crates: block / open / animate | `core/PlayerActions.cpp`, `domain/game/Game.cpp`, `Game.h` | ~60 lines | **G2** |
| **G4** crate contents — `EV_GIVELOOT` | `domain/game/ScriptVM.cpp`, `DialogSystem.h/.cpp` | ~120 lines | G3 (naming only) |

G1 first: it is self-contained, ~15 lines, and fixes the biggest gameplay hole (the shelf
items). G3 (crates) *cannot* precede G2: a crate is `ET_ATTACK_INTERACTIVE(10)` and the rewrite
spawns no entity at all for that family today (`new_src/domain/game/Game.cpp:100-103`), so there
is nothing to block the player, nothing for the fire probe to elect and nothing to unlink.
G2 therefore comes second even though it is the biggest group. G4 is optional-last: without it a
crate opens, animates and its script disables itself, but grants nothing.

One group = one delegation. Verify each with a build (`cmake --build build_new -j 8`) and the
on-screen checks at the end of each group.

---

## G1 — Shelf pickup: `EV_GIVEITEM` mode 0

### Problem, confirmed by reading the code

`new_src/domain/game/ScriptVM.cpp:551-556` — the opcode's `mode == 0` arm prints
`"[script] GIVEITEM sprite-touch give unsupported"` and returns without granting anything.
Everything else on the path already exists:

* the action entry point: `new_src/core/PlayerActions.cpp:187-194` already runs
  `executeTile((destX+viewStepX)>>6, (destY+viewStepY)>>6, flagForFacingDir(4), true)` and
  advances the turn when a script ran — the exact legacy chain
  (`src/PlayingInputHandler.cpp:395-404`);
* the entity for the shelf item exists: `Game::loadEntities` spawns `ET_ITEM`
  (`new_src/domain/game/Game.cpp:102`), and the map00 shelf sprites are neither hidden nor
  `0x200000` (checked in `tmp_map00.bin`: sprites 244 `info=0x70`, 245/233/235 `info=0x55`,
  237/238 `info=0x6B`), so they are all spawned and linked today;
* the sprite→entity resolution exists: `EntityDb::findEntityBySprite`
  (`new_src/domain/game/EntityDb.cpp:87-92`) is the rewrite's `mapSprites[S_ENT + i]`;
* the grant exists: `ItemPickup::touched` (`new_src/domain/game/ItemPickup.cpp:44-57`),
  reachable as `env_.game->items.touched(ent)` (`new_src/domain/game/Game.h:110`).

**Nothing else is missing. The opcode arm is the whole fix.**

### Change (single file: `new_src/domain/game/ScriptVM.cpp`, case `EV_GIVEITEM`)

Operands are already read as `defId`, `qtyByte`, `mode`. Replace the `mode == 0` stub with the
legacy body (`src/ScriptThread.cpp:969-980`):

1. `sprite = (defId << 8) | qtyByte` — the two operand bytes are a big-endian **sprite index**
   in this mode, not a def id and a quantity (`src/ScriptThread.cpp:971`).
2. `Entity* ent = env_.game->db.findEntityBySprite(sprite);` — `mapSprites[S_ENT + i]` analog
   (`:972`).
3. `ent == nullptr` → legacy calls `app->Error(... 16)` (ERR_GIVE_ITEM, fatal). **Deviation:**
   log `"[script] GIVEITEM sprite=%d has no entity (Err16)"`, set `n2 = 1`, `break`.
4. `if (!env_.game->items.touched(ent)) n2 = 1;` (`:976-979`), then `break`.

`n2` is already the local that lands in `scriptStateVars[7]` after the opcode
(`src/ScriptThread.cpp:2041`), which is what map scripts branch on.

Keep a one-line log of the outcome (`sprite`, `def->tileIndex`, ok/fail) — the map00 shelf
scripts are the fastest way for the user to tell us what happened.

### Deliberately NOT ported in G1

* `throwAwayLoot` pre-check (`src/ScriptThread.cpp:966-970`): `ScriptThread` in the rewrite has
  no such flag (it is set only by `Game::foundLoot`-driven "discard" threads, `src/Game.cpp:3523`).
* Fatal `Error(16)`.
* The double-pickup guard: **the original allows a second grant** from a shelf tile that is also
  walkable, because `touched()` never checks the hidden bit
  (`docs/research/2026-08-30-action-pickup.md` §6). Do not add a guard — reproduce it.

### Acceptance (what the user must see)

Start map00. Walk to the shelf niche and **face** tile **(12,17)** (EVT 34), press the action
key without stepping on the tile:

* a pickup message appears in the HUD ("You found …" family, strings 83-87) and the item count
  changes in the menu inventory;
* the shelf sprite disappears (`removeEntity` hides it);
* the turn advances (monsters get their move) — pickup by action costs a turn;
* pressing action a second time at the same tile grants the *second* item of that script
  (sprite 244 after 245, script-state driven), then stops.
* Other shelves to verify: **(13,15)** EVT 22, **(8,24)** EVT 103 (two items), **(25,17)** EVT 37.
* stderr must no longer contain `sprite-touch give unsupported`.

---

## G2 — Spawn an entity for every sprite with a def (the blocking families)

### Decision (ADR 0015): full legacy rule, not a longer family whitelist

`Game::loadEntities` (`new_src/domain/game/Game.cpp:75-114`) currently spawns only
DOOR/MONSTER/NPC/CORPSE/ITEM. We do **not** extend that `if` with four more eTypes; we
implement the legacy rule verbatim (`src/Game.cpp:373-486`): *every sprite whose
`lookup(tileNum)` returns a def gets an entity*, plus the def-less sprite-wall fallback.
Rationale and risk analysis: ADR 0015. Capacity is proven safe (worst shipped map 209/275
entities, 65/80 monsters — research §7).

### Rewrite of the spawn loop (`new_src/domain/game/Game.cpp`)

Keep the two pre-passes (AUTO_ANIMATE injection, per-sprite render mode) exactly as they are.
Replace the body of the third loop with the legacy order. Per sprite `i`:

```
info    = map.mapSpriteInfo[i]
tileNum = (info & 0xFF) + (info & SPRITE_FLAG_TILE ? 257 : 0)          // src/Game.cpp:374-377
```

1. **No-entity sprites** (`src/Game.cpp:397-400`): if `info & SPRITE_FLAG_NOENTITY (0x200000)`
   → `map.mapSpriteInfo[i] &= ~0x200000;` **and `continue`** (legacy clears the bit; the current
   code skips without clearing).
2. **Link coordinates** are read *before* any nudge:
   `x = mapSprites[i + 0*numSprites]`, `y = mapSprites[i + 1*numSprites]` (`:401-403`).
3. **Weapon pose** (`:404-406`): `if (tileNum >= 1 && tileNum <= 12) map.mapSpriteInfo[i] |= 0x200;`
   Legacy does this for every sprite in the tile range regardless of the def; drop the current
   `def->eType == ET_ITEM` guard (behaviour-identical on shipped data — every def with
   tileIndex 1..12 is `ET_ITEM` — but the code should read like the original).
   Re-read `info = map.mapSpriteInfo[i]` after this write.
4. **Link-tile nudge** (`src/Game.cpp:407-418`) — **mandatory**, 20 of 51 oriented map00 sprites
   change tile:
   ```
   if ((info & SpriteInfo::ORIENTED) && (((x & 0x3F) == 0) || ((y & 0x3F) == 0))) {
       if      (info & Enums::SPRITE_FLAG_EAST)  ++x;   // 0x4000000
       else if (info & Enums::SPRITE_FLAG_SOUTH) ++y;   // 0x2000000
       else if (info & Enums::SPRITE_FLAG_NORTH) --y;   // 0x1000000
       else if (info & Enums::SPRITE_FLAG_WEST)  --x;   // 0x8000000
   }
   ```
   `x`/`y` are **local** — never written back into `mapSprites` (legacy uses locals `n15/n16`).
   They are used only for `linkEntity(e, x >> 6, y >> 6)`.
5. **`def = defs.lookup(tileNum)`** (bounds-check `0 <= tileNum < 512` as today).
6. **`def != nullptr` → spawn** (`src/Game.cpp:419-455`):
   * slot: `if (nextSlot >= EntityDb::kEntities) { log "ERR_MAX_ENTITIES(35)"; break; }`
   * `e.def = def; e.setSprite(i);`
   * per-family `initspawn` equivalent (`src/Entity.cpp:50-111`) — **restructure the current
     if/else chain so that only the listed families get their marks**:
     | family | action | citation |
     |---|---|---|
     | `ET_MONSTER` | unchanged (payload alloc, flip, template clone, difficulty hp, S_Z=32, scale 64/42, `kInfoActive`, default loot set, `kInfoOnActiveList` + `deactivate`) | `src/Entity.cpp:59-80`, `src/Game.cpp:430-447` |
     | `ET_DECOR` with `eSubType != 3` (DECOR_STATUE) | `map.mapSpriteInfo[i] &= ~0x10000;` (**unhide** — decor marked hidden in map data is shown and linked) and, if `(info & 0xFF) == 173`, `mapSprites[i + 8*numSprites] = 32;` (S_SCALEFACTOR) | `src/Entity.cpp:81-86` |
     | `ET_ATTACK_INTERACTIVE` | `e.info \|= Entity::kInfoActive;` (0x20000 — required by `Combat::calcHitEntity`) | `src/Entity.cpp:87-89` |
     | `ET_CORPSE` | keep the current rewrite behaviour (`kInfoActive\|kInfoDirty` + `CorpseLoot::populateDefaultLootSet`) | existing deviation, see §"not ported" |
     | `ET_NPC` | `e.param = 1;` and **no** loot set | `src/Entity.cpp:99-101,103-108` |
     | `ET_DOOR` | keep `e.info \|= Entity::kInfoActive;` and keep the existing `TILENUM_FIRST_DOOR..TILENUM_LAST_DOOR` range guard (`Game.cpp:98-100`). Verified harmless: the only `eType==5` defs in `entities.bin` are tileIndex 271-278. | existing |
     | everything else (`ET_ITEM`, `ET_ENV_DAMAGE`, `ET_DECOR_NOCLIP`, `ET_SPRITEWALL`, `ET_NONOBSTRUCTING_SPRITEWALL`) | nothing | `src/Entity.cpp:50-111` has no branch |
     Loot sets are populated **only** for `ET_MONSTER` / `ET_CORPSE` (`src/Entity.cpp:103-111`).
   * destroyable counter (`src/Game.cpp:448-450`): add `int numDestroyableObj = 0;` to `Game`
     (reset in `loadEntities`) and `++numDestroyableObj` for `eType == 10` with
     `eSubType != 2 && eSubType != 3`. No reader yet; it is one int and the map-completion
     stats will want it.
   * **link gate** (`src/Game.cpp:451-454`): `if ((map.mapSpriteInfo[i] & 0x10000) == 0) db.linkEntity(&e, x >> 6, y >> 6);`
     Read `mapSpriteInfo` **again** here: the decor branch above may have just cleared the bit.
     **Remove the current early `if (info & 0x10000) continue;` at `Game.cpp:78`** — hidden
     sprites do get an entity, they are only left unlinked.
   * post-spawn hide (`src/Game.cpp:455-457`): `if (tileNum >= 140 && tileNum <= 143) map.mapSpriteInfo[i] |= 0x10000;`
7. **`def == nullptr` and `info & SPRITE_FLAG_SOLIDSIDE (0x800000)` → def-less sprite wall**
   (`src/Game.cpp:457-481`), **ported** (7 entities on map00, incl. the barred windows):
   * slot + `e.setSprite(i)` as above;
   * `e.def = (tileNum == 166 || tileNum == 168) ? defs.find(13, 0) : defs.find(12, 0);`
     (`EntityDefs::find(eType, eSubType, parm = -1)`, `new_src/io/EntityDefs.h:29`); if the
     lookup returns `nullptr`, log and release the slot;
   * **no** `initspawn`, no loot set, no active bit;
   * same link gate as above.
   Legacy also writes `entity->name` here; the rewrite's `Entity` has no `name` field (names are
   read from `def`), so that line has no counterpart.
8. Keep the existing per-family stderr spawn logs; add `DECOR` / `INTERACT` / `SPRITEWALL`
   lines with sprite, tileNum and the **linked** tile so the nudge is auditable.

### Regression risks and their mitigations (detail in ADR 0015)

| Subsystem | Risk | Verdict |
|---|---|---|
| `TraceSystem` | new blockers, oriented-sprite segments | intended; the oriented/circle geometry is already ported (`TraceSystem.cpp:143-165`), the nudge (step 4) is what makes it land on the right tile |
| `Targeting::electFireTarget` | DECOR / ATTACK_INTERACTIVE / SPRITEWALL branches become reachable | already ported verbatim, incl. the eType-10 range cull (`Targeting.cpp:132-138`) |
| `Combat` | shot at a crate/decor | safe: `calcHitEntity` requires `kInfoActive` (decor never has it → miss) and gates eType 10 on `combatMasks[def->parm]` — crate `parm=0` ⇒ mask 0 ⇒ never hit (`Combat.cpp:354-383`); `MonsterSystem::diedMonster` early-returns for non-monsters (`MonsterSystem.cpp:191-192`), so nothing can crash |
| `MonsterSystem` | monsters now blocked by decor (mask 15535 includes bits 7/10/12/13) | faithful to the original |
| `SceneRenderer` stacked characters | new entities misclassified | safe: `charClass` is set only for NPC/monster/corpse defs and the sort bias only for `info & 0x1010000` (`SceneRenderer.cpp:127-152`); the new families match neither |
| `ScriptVM` `EV_HIDE` | now finds decor/interactive entities | that is the legacy behaviour (`ScriptVM.cpp:508-536` already has the eType-10/6 branches) |
| capacity | 275 entities / 80 monsters | proven: worst shipped map 209/275, 65/80 |

### Deliberately NOT ported in G2

* `Entity::died()` / `pain()` for destroyable objects (debris, +5 XP, message 89, glass shatter,
  `src/Entity.cpp:371-392,437-447`): decor and furniture stay indestructible for now.
* The 16 drop-item slots and the world-entity link pass over `mapFlags & 1`
  (`src/Game.cpp:487-506`): no drop system, and `trace` skips `ET_WORLD` in the entity loop.
* `ET_PLAYERCLIP` map lines: **already correct** — `TraceSystem::traceWorldFrac` implements the
  `flag == 5` clip gate (`TraceSystem.cpp:186-189`); no def of that eType exists.
* `cacheCombatSound` at monster spawn (`src/Game.cpp:444-446`): no audio backend.
* The corpse/skeleton `0x420000` split (legacy sets it only for `CORPSE_SKELETON`): the rewrite
  keeps its generalized marker — out of scope, unchanged by this spec.

### Acceptance (what the user must see) — map00

Blocking (walk into them; the player must not pass, and stderr prints the `[dbg] moveBlocked`
line with the matching eType):

* crates (`ET_ATTACK_INTERACTIVE`, tile 152): **(6,22) (7,18) (11,13) (14,8) (15,23) (15,26) (18,3)**;
* torchieres (`ET_DECOR`, tile 136): **(4,18) (4,20)**;
* septic stations (tile 147): **(9,17) (9,27)**; practice target (tile 149): **(14,29)**;
* toilets/sinks (`ET_ATTACK_INTERACTIVE`, tiles 123/127): **(12,10) (12,11) (14,11)**;
* def-less sprite walls (barred windows, tile 128): **(18,24) (18,26)** — these prove step 7.

Nudge correctness (step 4) — wall-mounted terminals/switches sit exactly on a tile border and
must block the tile **at the wall**, not the one in front of it:

| sprite | tile | raw tile | must block |
|---|---|---|---|
| 3 | 180 terminal | (3,21) | **(2,21)** |
| 73 | 180 terminal | (19,24) | **(18,24)** |
| 208 | 173 switch | (11,26) | **(11,25)** |
| 250 | 133 tech station | (13,18) | **(12,18)** |

(20 such sprites on map00; the full list is in the spawn log.)

Nothing else may regress: doors still open, monsters still walk, items are still picked up by
stepping, and no invisible wall appears in the middle of an empty corridor.
Spawn census to expect in the log: 191 entities on map00 — 35 monsters, 10 NPC, 21 doors,
25 items, 51 decor, 4 env-damage, 9 corpses, 28 attack-interactive, 7 sprite walls,
1 decor-noclip.

---

## G3 — Crates: open by action, animate, stop blocking

Depends on G2 (crates only exist as entities after it). Facts:
`docs/original-code/loot-inventory.md` §7.3-§7.4.

### G3.1 `Game::openCrate` (`new_src/domain/game/Game.h/.cpp`)

```cpp
// Crate opening (src/PlayingInputHandler.cpp:387-393): arm the 4-frame sprite
// animation and unlink immediately, so the crate stops blocking and stops
// being a trace/facing target BEFORE the animation plays.
void openCrate(Entity* e);
```

Body:
* `if (e == nullptr || e->def == nullptr) return;`
* `e->param = lerps.clockMs() + 200;` — legacy `entity->param = app->upTimeMs + 200` (`:389`).
  **Clock choice (ADR 0016):** the rewrite's simulation clock for sprite animation is the
  `SpriteLerps` clock (`new_src/domain/game/SpriteLerps.h:87`), already used for
  `monster->frameTime` (`MonsterSystem.cpp:179`) and read by the pain-pose revert in
  `Game::update` (`Game.cpp:315`). Arm and advance on that single clock.
* `db.unlinkEntity(e);` (`:390`).
* `facingDirty = true;` — small deviation: legacy re-runs `updateFacingEntity` on the next HUD
  repaint anyway; setting the latch makes the HUD drop the crate name in the same turn.
* log `"[use] crate opened sprite=%d"`.

### G3.2 The action chain (`new_src/core/PlayerActions.cpp`, `case Action::Use`)

The legacy order is: forward trace + target election (`:197-368`) → corpse loot (`:374-378`) →
**crate open** (`:387-393`) → front-tile script (`:395-404`) → doors (`:445`) → fire.
The rewrite currently elects the fire target *after* the script/door branches
(`PlayerActions.cpp:206-232`). Fix by **hoisting the election once**, not by tracing twice:

1. Keep the corpse-loot branch first (unchanged).
2. Immediately after it, before `int mask = flagForFacingDir(4);`:
   ```
   const int weapon2 = p.ce.weapon;                     // legacy :197
   TraceHit elected;                                    // legacy `entity`
   int electedDist = 0;
   if (weapon2 >= 0) {
       elected = env_.targeting->electFireTarget(weapon2);
       electedDist = elected.blocks()
           ? env_.game->trace.distFrom(elected, p.viewX, p.viewY) : 0;
   }
   if (elected.isEntity() &&
       elected.eType == Enums::ET_ATTACK_INTERACTIVE && elected.eSubType == 2 &&
       electedDist <= env_.game->combat.tileDistSq(1)) {          // tileDistances[0] = 4096
       env_.game->openCrate(elected.entity);
   }
   ```
   `tileDistSq(1)` **is** `tileDistances[0]` (`Combat.cpp:69-72`), i.e. the one-tile gate of
   `src/PlayingInputHandler.cpp:388`.
   `electedDist` must be captured here, not later: a `World` hit's `distFrom` reads the
   trace scratch collision point (`TraceSystem.cpp:233-239`), which a later trace would
   overwrite. Entity hits are position-derived and stable.
3. The existing fire block below **reuses** `elected` / `electedDist` instead of calling
   `electFireTarget` again; delete the second election and the local re-declaration of
   `weapon2` / `dist2` (keep the `!env_.game->combat.active` gate).
4. Order after the hoist stays: crate open → `executeTile` (script consumes the press and
   advances the turn) → door → fire. This matches legacy exactly, because in the original the
   crate's tile script always returns true and the shot never happens.

Known accepted deviation: if a map ever ships a crate with **no** tile script, legacy would fall
through to `fireWeapon` with the crate still elected (and miss, mask 0); the rewrite reaches the
same fire block with the same `elected` value — identical outcome.

### G3.3 The opening animation (`Game::update`, `new_src/domain/game/Game.cpp:296-320`)

Ported out of the renderer (`src/Render.cpp:1607-1617`), same deviation precedent as the
pain-pose revert already living there (D-6). Append after the existing monster loop:

```
const int now = lerps.clockMs();
for (Entity& ent : db.entities()) {
    if (ent.def == nullptr || ent.param == 0) continue;
    if (ent.def->eType != Enums::ET_ATTACK_INTERACTIVE || ent.def->eSubType != 2) continue;
    s = ent.getSprite();  bounds-check against map_->numSprites
    info  = map_->mapSpriteInfo[s];
    frame = (info >> 8) & 0xFF;                        // src/Render.cpp:1508
    if (now > ent.param) { ++frame; ent.param = now + 200; }   // :1608-1611
    if (frame > 3) { ent.param = 0; frame = 3; }               // :1612-1615
    map_->mapSpriteInfo[s] = (info & 0xFFFF00FF) | (frame << 8);  // :1616
}
```

Frames exist in the data: `mediaMappings[152] = 753`, `mediaMappings[153] = 757` → media
753-756 = frames 0-3 (`tmp_newMappings.bin`, same layout the auto-animate pass uses).
`World3D` already resolves `mediaId = mappings[tileNum] + frame` from bits 8-15
(`new_src/render/World3D.cpp:713,744`) — **no renderer change**.
Deviation: legacy advances the frame only while the crate is being drawn; we advance it in the
simulation for every armed crate. Both reach frame 3 in ~600 ms and latch there.

### Deliberately NOT ported in G3

* `lootingSystem.lootSource` naming (`src/PlayingInputHandler.cpp:189-195`) → G4.
* Sound 1054, tutorial `showHelp(3)` (`src/MovementController.cpp:98-105`), the HUD "use" action
  icon row (`src/Hud.cpp:354-360`).
* `INTERACT_PICKUP` toilet/sink holy-water refill (`:405-430`) and the barricade-glass unlink.
* Save/restore of the opened state (`src/Entity.cpp:1877-1893`) — no save system.
* Splash damage shattering a crate (`src/Combat.cpp:1085-1097`) — no projectiles.

### Acceptance (what the user must see) — map00

1. Walk to the crate at **(7,18)** (or (6,22), (11,13), (14,8), (15,23), (15,26), (18,3)):
   the player is **blocked** by it (that part comes from G2).
2. Face it from one tile away and press the action key:
   * the crate **visibly opens over ~0.6 s** — the sprite steps through 4 frames and stays open
     (it must NOT disappear and NOT shrink like a door);
   * immediately (before/while the animation runs) the tile becomes **walkable** — the player
     can step onto the crate tile;
   * stderr shows `[use] crate opened sprite=…` followed by the tile script running
     (`EVENTOP disable`, `GIVELOOT …`) and one turn advancing;
   * pressing action again on the opened crate does nothing (the event disabled itself) and the
     crate stays at its last frame.
3. Firing at a closed crate must never destroy or open it.

---

## G4 — Crate contents: `EV_GIVELOOT`

`new_src/domain/game/ScriptVM.cpp:1097-1102` currently parses and discards the payload.
Port `ScriptThread::composeLootDialog` (`src/ScriptThread.cpp:2121-2222`) plus the opcode arm
(`src/ScriptThread.cpp:1135-1150`).

### G4.1 Loot source naming

* Add `int lootSource = -1;` to `Game` (legacy `LootingSystem::lootSource`).
* In `PlayerActions::Use`, at the very top of the branch (legacy `:189-195`, before the corpse
  check): if `p.facingEntity != nullptr && p.facingEntity->def != nullptr &&
  p.facingEntity->def->eType == 10` → `env_.game->lootSource = p.facingEntity->def->name;`
  (the rewrite has no `Entity::name`; legacy's is `def->name | 0x400`, i.e. the same string in
  the in-game text type).

### G4.2 Opcode arm (`ScriptVM.cpp`, `case EV_GIVELOOT`)

Legacy (`src/ScriptThread.cpp:1135-1150`): if a loot dialog is already showing → `unpauseTime = 1;
return 2;`. Else compose + grant, then `skipAdvanceTurn = true; queueAdvanceTurn = false;
unpauseTime = -1; n = 2;` (the thread parks until the dialog closes — the same handshake
`EV_DIALOG`/blocking opcodes already use in `ScriptVM`).

### G4.3 Composition + grant (new private helper `ScriptVM::composeLootDialog(ScriptThread*)`)

Header (`src/ScriptThread.cpp:2122-2130`): if `game->lootSource != -1` → **append**
`titleOf(loc.get(kTextIngame, lootSource))` into the buffer, then compose main string **129**,
then reset `lootSource = -1`; else compose main string **130**.

CORRECTION (2026-08-30, verified by the coder and independently by the reviewer, both by
unpacking the archive strings): main(0)[129] is `" Con-tents:"` and carries **no `%NN` slot at
all**, so the source name is NOT a text argument — `composeTextField` appends into the buffer
(`src/Text.cpp:328-330`) and is called *before* string 129, yielding `Crate Con-tents:`.
Passing it as an arg (as this spec first said) would have dropped the name entirely.
main(0)[130] = `"You Got:"`, main(0)[90] = `"%01%02x %03|"`, main(0)[91] = `"%01%02|"`.

Then `count = readUByte()` entries, each `u16 v` (`:2136-2199`):
* `cls = (v >> 12) & 0xF`
* `cls == 6` → flavour line: append `'\x88'` + `loc.get(kTextMap, v & 0xFFF)` + `"|"`.
* `cls == 5` → `updateQuests(v & 0xFFF, 0)` — **not ported**, log only.
* else `idx = (v & 0xFC0) >> 6`, `cnt = v & 0x3F`:
  * `cls == 0 && idx == 24` → accumulate `credits += cnt`; `idx == 25` → `credits += cnt * 100`
    (`:2151-2160`);
  * `cls == 0 || cls == 3` → `player->give(cls, idx, cnt, false)` + line from main string **90**
    with args `('\x88', cnt, titleOf(loc(kTextIngame, find(6, cls, idx)->longName)))`;
  * `cls == 1` (weapon) → `player->give(1, idx, cnt, true)`; if `weaponDef(idx).ammoUsage != 0`
    → `player->give(2, weaponDef(idx).ammoType, 10, true)` (`:2178-2181`, weapon row fields 4/5
    are `ammoType`/`ammoUsage` in `WeaponTable.h`); line from main string **91** with args
    `('\x88', longName)`;
  * `cls == 2` (ammo) → skip entirely when `difficulty() == 4`, else `player->give(2, idx, cnt,
    false)` + string **90** (`:2192-2199`).
* Tail (`:2201-2209`): if `credits != 0` → `player->give(0, 24, credits, false)` + string **90**
  with `('\x88', credits, titleOf(loc(kTextIngame, 157)))`.
* Drop the trailing `'|'`, then show the dialog.

The line composition mirrors `CorpseLoot::composeLootLines`
(`new_src/domain/game/CorpseLoot.cpp:150-183`) — same strings 90/91/157, same `'\x88'` bullet;
reuse its helpers rather than inventing a second style.

`Game::foundLoot(x, y, z, n)` (`:2222`) has no counterpart — increment the existing
`Game::lootFound` counter only.

### G4.4 `DialogSystem` addition

Legacy shows the composed buffer via `startDialog(thread, largeBuffer, 4, 0, true)`. The rewrite
only has the "compose from a string id" entry point (`new_src/domain/game/DialogSystem.h:56`).
Add the sibling overload:

```cpp
// startDialog(thread, Text&, style, flags, resume) — the composed-buffer entry
// point (src/DialogSystem.cpp:737-747 with the text already built).
void startDialogText(ScriptThread* thread, Text& text, int style, int flags, bool resumeScript);
```
Body = `startDialog` minus the `composeText` call: set `resumeScriptAfterClosed_`, `thread_`,
`prepareDialog(text, style, flags)`, `setState(StateId::Dialog)`. Style **4**, flags **0**.

### Deliberately NOT ported in G4

`throwAwayLoot` threads (message 145 toast), `updateQuests`, `foundLoot` sparkle/stat, sound 1054.

### Acceptance

Open the crate at **(7,18)**: a dialog appears titled with the crate's name (string 129 form,
not the generic 130) listing `5× <item 12>, 1× <item 17>, 4× <item 16>`; closing it resumes the
script and the items are in the inventory. Crate at **(15,26)** grants the large stack
(15×/2×/4× + 24 ammo). Crate at **(18,3)** grants 5×/1× + 24 ammo. The turn does **not** advance
twice (the opcode sets `skipAdvanceTurn`).

---

## Build notes

No new files in G1/G2/G3 (only `Game.h` gains `openCrate` + `numDestroyableObj`, and G4 adds a
`DialogSystem` method) → **no CMake reconfigure needed**. Build with
`cmake --build build_new -j 8`; run from `build_new/new_src`.
