# World item pickup (touchTile / touched / touchedItem) — implementation spec

> Target: `new_src/`. Facts source: `docs/original-code/loot-inventory.md` §3 + §3.1,
> `docs/research/2026-08-29-world-item-pickup.md`, `docs/original-code/entities.md`.
> Every constant below is cited into `src/`. PLAN.md:110 item.
> Design decisions: ADR `docs/architecture/adr/0014-world-item-pickup-subsystem.md`.

## 0. What this delivers

Walking onto a tile that carries an ET_ITEM sprite grants the item, hides the sprite,
posts the HUD message and runs static script 11 (SCR_ITEM_PICKUP) — without costing an
extra turn. Three delegation groups:

| Group | Scope | Files |
|---|---|---|
| **G1** | Item entities are spawned, linked, non-blocking; world weapons get frame 2 | `domain/game/Game.cpp`, `domain/game/Enums.h` |
| **G2** | `ItemPickup` subsystem: `touchTile` → `touched` → `touchedItem`, grants, messages, removal, script hook, `foundLoot` | new `domain/game/ItemPickup.{h,cpp}`, `domain/game/Game.{h,cpp}`, `core/Main.cpp` |
| **G3** | `Player::give` brought to legacy semantics (quiet flag, sentry-bot branch, auto-equip, bottled-water conversion) | `domain/game/Player.{h,cpp}`, `core/Main.cpp` |

G2 works on top of today's `give` (G3 only refines it), so the groups can be delegated in
order G1 → G2 → G3 with a build+run check after each.

## 1. Facts this rests on (short form, all verified)

- Spawn has **no item branch** in the legacy loader: every sprite whose tileNum resolves to
  a def becomes an entity, and `def->eType == 6` (ET_ITEM) makes it an item
  (`src/Game.cpp:374-455`). `initspawn()` has no ET_ITEM case: `lootSet` deleted, no
  `0x20000`, `param` stays 0 (`src/Entity.cpp:49-112`).
- Linking is skipped for hidden sprites (`mapSpriteInfo & 0x10000`, `src/Game.cpp:453-455`).
- World weapons: `if (tileNum >= 1 && tileNum <= 12) mapSpriteInfo |= 0x200`
  (`src/Game.cpp:405-407`) — frame 2 of the weapon art. Tiles 13/14 (sentry bots) keep frame 0.
- Trigger: `Game::touchTile(x, y, true)` from `MovementController::finishMovement()`
  (`src/MovementController.cpp:170`), after the walk lerp lands and after the destination
  tile's scripts, before `advanceTurn()` (`:160-184`). Criterion = pure tile occupancy over
  the `entityDb` chain, `nextOnTile` cached before `touched()` (`src/Game.cpp:687-699`).
- Items never block: `CONTENTS_PLAYERSOLID = 13501` has bit 6 clear (`src/Enums.h:29`,
  `src/MovementController.cpp:326`). In the rewrite this is already true —
  `Contents::PLAYERSOLID` (`new_src/domain/game/Enums.h:56,74`) omits `ITEM`.
- Removal: `removeEntity` hides the sprite + unlinks (`src/Game.cpp:183-192`), already
  ported as `EntityDb::removeEntity` (`new_src/domain/game/EntityDb.cpp:95-105`).
- Script hook: on success `scriptStateVars[11] = def->tileIndex; executeStaticFunc(11)`
  (`src/Entity.cpp:125-126`).

The one call site of `touchTile(dest, true)` already exists in the rewrite:
`new_src/core/PlayerActions.cpp:62` (inside `PlayerActions::finishMovement`, after both
`executeTile` calls and before `advanceTurn`). **No new call site may be added.**
`Game::touchTile` is currently an empty stub (`new_src/domain/game/Game.cpp:235-238`).

## 2. GROUP G1 — spawn item entities

### G1.1 `new_src/domain/game/Game.cpp`, `loadEntities`

Current filter (`:75-90`) keeps doors/monsters/corpses/NPCs and `continue`s on everything
else. Change exactly two things:

1. Accept `def->eType == Enums::ET_ITEM` in the family filter (add it to the
   `else if (def->eType != ...)` chain). Nothing else in the branch chain changes: an item
   falls through to the final `else` today — it must NOT, so give it its own branch that
   does nothing except the shared tail (no `kInfoActive`, no `kInfoDirty`, no lootset, no
   `param`), mirroring the empty `initspawn` path (`src/Entity.cpp:49-112`).
2. Right where the effective `tileNum` is computed, add the world-weapon frame force
   **before** the def-family filter, applied to the sprite info, guarded by the item type:

   ```
   if (def->eType == Enums::ET_ITEM && tileNum >= 1 && tileNum <= 12)
       map.mapSpriteInfo[i] |= 0x200;            // src/Game.cpp:405-407
   ```

   Legacy applies it to every sprite in the tile range regardless of def type; the range is
   the weapon tile range (`TILENUM_ASSAULT_RIFLE=1 … 12`, `src/Enums.h:649-664`), so the
   guard is behaviour-identical on shipped data and safer.
   Note `info` is read into a local at `:77` — write through `map.mapSpriteInfo[i]` and keep
   the local in sync, or recompute after the OR.

The shared tail already does what items need: `db.linkEntity(&e, x >> 6, y >> 6)` at
`:149-151`, then the `BODY/DOOR/NPC` log. Extend the log label with `"ITEM"` for
`ET_ITEM` and print `sub=def->eSubType parm=def->parm` — the coder verifies spawn from it.

Hidden sprites: the loop keeps its existing `if (info & 0x10000) continue;` (`:78`).
Legacy creates the entity but does not link it; the rewrite creates nothing. Behaviourally
identical for pickup (an unlinked entity can never be touched). Documented deviation, see
ADR 0014 §Consequences.

### G1.2 `new_src/domain/game/Enums.h`

Add the constants G2/G3 need (values from `src/Enums.h`):

```
static constexpr int ITEM_CLASS_FOOD = 3;              // IT_FOOD (src/Enums.h:99)
static constexpr int INV_JOURNAL = 18;                 // (src/Enums.h:215)
static constexpr int INV_RED_KEY = 19, INV_BLUE_KEY = 20;   // (src/Enums.h:216-217)
static constexpr int INV_BOTTLED_WATER = 13;           // (src/Enums.h:210)
static constexpr int AMMO_SENTRY_BOT = 7;              // (src/Enums.h:110-118)
static constexpr int WP_SENTRY_BOT_MASK = 120;         // 0x78 (src/Enums.h:157)
static constexpr int TILENUM_WEAPON_FRAME_FORCE_MIN = 1;  // src/Game.cpp:405
static constexpr int TILENUM_WEAPON_FRAME_FORCE_MAX = 12;
```

`ITEM_CLASS_INVENTORY/WEAPON/AMMO` already exist (`new_src/domain/game/Enums.h:112-114`).

### G1.3 Acceptance (G1)

- Build clean (`cmake --build build_new -j 8`); CMake GLOB reconfigure not needed (no new files).
- Startup log on map00 shows `ITEM entity sprite=1 tile=107 …` (one UAC credit),
  `sprite=129 tile=116` (health pack) — the sprite/tile pairs verified in
  `docs/research/2026-08-29-world-item-pickup.md` §A.
  CORRECTION (2026-08-30, verified during G1/G3 review): the other two anchors are
  wrong and must NOT be used for acceptance. `sprite=203` has `SPRITE_FLAG_NOENTITY`
  (info 0x00200201), so legacy spawns no entity for it either; `sprite=65` has
  `SPRITE_FLAG_TILE` (info 0x0C50000E), so its effective tile is 14+257=271 — a DOOR,
  not a red sentry bot. map00 ships no reachable sentry bot.
- **On screen (user check):** nothing changes visually except the assault rifle lying near
  the blue door, which must now be drawn in its *pickup* pose (frame 2) rather than the
  view-weapon frame 0. (If a weapon's media range has fewer than 3 frames the renderer
  clamps back to frame 0 — `new_src/render/World3D.cpp` frame clamp — that is acceptable.)
- Walking through/over item tiles is still possible (no invisible wall) and no item is
  picked up yet.
- The faced-entity probe may now latch items (`Contents::FACING_PROBE` includes `ITEM`,
  `new_src/domain/game/Enums.h:61`) — this is legacy-faithful
  (`src/MovementController.cpp:38`) and the monster-promotion rescan already handles an item
  as first hit (`new_src/domain/game/Targeting.cpp:171-206`). The monster health bar must
  stay hidden when facing an item (`Targeting::feedHealthBar` gates on `isMonster`).

## 3. GROUP G2 — the pickup itself

### G2.1 New module `new_src/domain/game/ItemPickup.{h,cpp}`

Rationale (ADR 0014): in the rewrite `Entity` is a passive data record (`Entity.h` has only
accessors), and every behaviour slice already lives in a peer subsystem with an `Env`
(`DoorSystem`, `MonsterSystem`, `CorpseLoot`, `EntityDb`). `touchedItem` needs
player+hud+loc+tables+defs+vm, i.e. exactly an `Env`. So: a new peer on `Game`, not methods
on `Entity`, and not more code in `Game.cpp`.

```cpp
// new_src/domain/game/ItemPickup.h
namespace newcore {

class EntityDb; class EntityDefs; class Game; class Hud;
class Localization; class MapData; class Player; class ScriptVM; class Tables;

// Peer subsystem owning the world-item touch slice: Entity::touched /
// Entity::touchedItem (src/Entity.cpp:118-278) plus the SCR_ITEM_PICKUP hook.
// Game::touchTile walks the tile chain and delegates here.
class ItemPickup {
public:
    struct Env {
        EntityDb* db = nullptr;          // removeEntity
        const EntityDefs* defs = nullptr;// (reserved: dropped-item defs)
        MapData* map = nullptr;
        Player* player = nullptr;
        Hud* hud = nullptr;
        const Localization* loc = nullptr;
        const Tables* tables = nullptr;  // weapon rows for starter ammo
        ScriptVM* vm = nullptr;          // vars[11] + executeStaticFunc(11)
        Game* game = nullptr;            // foundLoot counter
    };

    void init(const Env& env) { env_ = env; }

    // src/Entity.cpp:118-149, ET_ITEM / ET_MONSTERBLOCK_ITEM branch only.
    bool touched(Entity* e);

    // src/Entity.cpp:152-278.
    bool touchedItem(Entity* e);

private:
    // Applet::nextInt() analog: std::rand() & INT32_MAX (src/App.cpp:506-508).
    static int nextInt();
    void message(int stringIndex, const std::string* args, int numArgs, int flags);

    Env env_;
};

} // namespace newcore
```

`touched(Entity* e)`:

```
if (!e || !e->def) return false;
const int t = e->def->eType;
if (t != Enums::ET_ITEM && t != Enums::ET_MONSTERBLOCK_ITEM) return false;   // :123
if (!touchedItem(e)) return false;
if (env_.vm) { env_.vm->vars[11] = e->def->tileIndex; env_.vm->executeStaticFunc(Enums::SCR_ITEM_PICKUP); }  // :125-126
return true;
```

The dropped-entity tail (`mapSprites[S_ENT+sprite] = -1; def = nullptr`, `:127-130`) is NOT
ported: there are no dropped entities in the rewrite yet (`spawnDropItem` unported). Leave a
one-line comment citing `:127-130`.

`touchedItem(Entity* e)` — verbatim port of `src/Entity.cpp:152-278`, switch on
`e->def->eSubType`:

**IT_INVENTORY (0)** — `src/Entity.cpp:154-194`
```
qty = 1;
if (def->parm == 24) qty = 2 + nextInt() % 3;                 // :158-161 (world; dropped uses param, unported)
if (!player.give(0, def->parm, qty))                          // :162
    -> msg str 83 with arg[0] = titleOf(loc.get(kTextIngame, def->name)), flags 2; return false;   // :163-169
if (def->parm == 19 || def->parm == 20) msg str 84, no args, flags 3;   // :171-175  (keycards)
else if (def->parm == 24) msg str 86, args {qty, titleOf(loc.get(kTextIngame, def->longName))}, flags 1;  // :176-184
else msg str 85, args { titleOf(loc.get(kTextIngame, def->longName)) }, flags 1;   // :186-190
if (def->parm != 18) game->foundLoot(sprite, 1);              // :192-194 (journal excluded)
```
Note the legacy `repaintFlags |= 0x4` on keycards (`:174`) has no counterpart: the rewrite
rebuilds the HUD model every frame (`new_src/ui/HudView.cpp`). Comment it, do not add state.

**IT_FOOD (3)** — `src/Entity.cpp:196-207`
```
n = (def->parm == 0 || def->parm == 2) ? 40 : 20;             // :198-203
if (!player.addHealth(n)) { msg str 46, no args, flags 2; return false; }   // :204-207
```
No `foundLoot` for food — legacy has none.

**IT_AMMO (2)** — `src/Entity.cpp:209-236`
```
qty = 2 + nextInt() % 4;                                      // :214-215
if (def->parm == 2) qty &= ~1;                                // :216-218  (shells: even only)
if (!player.give(2, def->parm, qty)) { msg str 87, no args, flags 0; return false; }  // :220-222
msg str 86, args { qty, titleOf(loc.get(kTextIngame, def->name)) }, flags 1;          // :224-232
   ^ NOTE: ammo uses def->name (Entity::name = def->name | 0x400 -> text type 1,
     src/Entity.cpp:53 + composeTextField :231), NOT longName.
game->foundLoot(sprite, 1);                                   // :233-235
```
Legacy returns `true` even when `give` failed?? No — the failure branch falls through to the
common tail (`removeEntity` + sound + `return true`, `:276-278`), i.e. **a full ammo slot
still consumes the item and prints str 87**. Reproduce exactly: on `give` failure post str
87 and continue to the tail (do NOT return false). This is the one asymmetry versus the
inventory/food branches — comment it with the citation.

**IT_WEAPON (1)** — `src/Entity.cpp:238-274`
```
if ((1 << def->parm) & Enums::WP_SENTRY_BOT_MASK) {           // :239 weaponIsASentryBot (src/Player.cpp:2485-2487)
    if (player.hasASentryBot()) return false;                 // :240-242 (isFamiliar unported)
    player.give(1, def->parm, 1);                             // :249
    player.ammo[Enums::AMMO_SENTRY_BOT] = 100;                // :244-250 (world qty = 100)
    msg str 223, no args, flags 3;                            // :251
} else {
    player.give(1, def->parm, 1);                             // :254
    const WeaponDef& row = tables->weaponDef(def->parm);      // :255-256 weapons[parm*9 + 4/5]
    if (row.ammoUsage != 0)
        player.give(2, row.ammoType, ((1 << def->parm) & 0x200) ? 8 : 10);  // :257-263
    msg str 85, args { titleOf(loc.get(kTextIngame, def->longName)) }, flags 1;  // :265-269
    // showWeaponHelp (:270) unported — no help popup system for gameplay grants.
}
game->foundLoot(sprite, 1);                                   // :272-274
```
`0x200` is bit 9 = the scoped rifle (`WP_...` parm 9) → 8 rounds, everything else 10.

**Common tail** — `src/Entity.cpp:276-278`
```
env_.db->removeEntity(e);                 // :276 (hides sprite + unlinks + clears facingEntity)
// TODO sound 1054 (src/Entity.cpp:277) — no audio subsystem in new_src.
return true;
```

Messages: use `Hud::addMessage(text, 0xAA000000, flags)` with the legacy flag word
(`getMessageBuffer(flags)` argument): 2 = CENTER (str 83, 46), 3 = FORCE|CENTER (str 84,
223), 1 = FORCE (str 85, 86), 0 = plain (str 87). Flag semantics already match
(`new_src/ui/Hud.h:81-84` vs `src/Hud.cpp:163-199`). Compose with
`composeArgs(text, args, n)` (declared in `new_src/domain/game/Game.h:31`) over
`loc->get(kTextMain, id)`; strip the entity-string title with
`Localization::titleOf(...)` exactly like `CorpseLoot` does
(`new_src/domain/game/CorpseLoot.cpp:77-82`).

`nextInt()` = `std::rand() & 0x7FFFFFFF` (`src/App.cpp:506-508`); `#include <cstdlib>`.

### G2.2 `new_src/domain/game/Game.{h,cpp}`

- Add the peer member next to the other peers (`Game.h:88-94`):
  `ItemPickup items;  // peer subsystem (spec 2026-08-29-world-item-pickup §G2)`.
- Add the run stat: `short lootFound = 0;` and
  `void foundLoot(int sprite, int amount) { (void)sprite; lootFound += (short)amount; }`
  (`src/Game.cpp:3536-3543`; the x/y/z overload only forwards, the counter is the whole body).
- Implement `Game::touchTile` (replaces the stub at `new_src/domain/game/Game.cpp:235-238`),
  a verbatim port of `src/Game.cpp:687-699`:

```cpp
void Game::touchTile(int x, int y, bool b) {
    EntityDb::TileWalk walk("Game::touchTile");
    Entity* next = nullptr;
    for (Entity* e = db.findMapEntity(x, y); e != nullptr && walk.ok(e); e = next) {
        next = e->nextOnTile;                 // :692 cached BEFORE touched() unlinks
        if (!b) continue;                     // :693 ET_ENV_DAMAGE-only pass, unported
        items.touched(e);                     // :694
    }
}
```
`findMapEntity` takes canvas units and shifts internally — confirm against
`new_src/domain/game/EntityDb.cpp` and pass `x`/`y` exactly as `PlayerActions` does
(`new_src/core/PlayerActions.cpp:62` passes `destX/destY` in canvas units, like legacy `:170`).
The `b == false` path (environmental damage) stays a no-op; add one comment citing
`src/Entity.cpp:134-146` as "not ported: no pain/status-effect system".

### G2.3 `new_src/core/Main.cpp`

Wire the subsystem right after `game.combat.init(...)` (`new_src/core/Main.cpp:286-288`):

```cpp
game.items.init({ &game.db, &g_entityDefs, &g_map, &player, &hud, &loc, &tables, &vm, &game });
```
All referenced objects outlive `game` in that scope; `game.db` is an inner member, so its
address is stable across `loadEntities`.

### G2.4 Build notes

`ItemPickup.cpp/.h` are new files ⇒ re-run
`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` before building (project uses GLOB).

### G2.5 Acceptance (G2)

Run map00, walk the intro corridor.

- Step on the single UAC credit (sprite 1, tile 107, tile coords (2,15)):
  **on screen** a message "You got N UAC Credits" (str 86 form, N = 2..4) appears and the
  credit sprite vanishes from the floor in the same step. Log shows the give + `foundLoot`.
- Step on the health pack (sprite 129, tile 116): at full health the pickup happens with
  "You got the Health Pack"; the sprite disappears. (Inventory items never fail — `give`
  clamps at 999 and returns true, `src/Player.cpp:1009-1022`.)
- Ammo pile: message "You got N Bullets" with N in 2..5 (even-only for `parm == 2`).
- The pickup costs **no extra turn**: the monster/turn cadence after the step is unchanged
  (same number of monster moves as before this change).
- No pickup ever happens by looking at an item, by passing the turn, or by opening a door —
  only by finishing a step onto the tile.
- Picked-up items stay gone after walking away and back (sprite hidden + unlinked); the
  automap/render show no ghost.
- Static script 11 runs on success — visible in the VM log (`executeStaticFunc(11)`); on
  map00 it is defined but usually returns immediately.

## 4. GROUP G3 — `Player::give` legacy semantics

Reference: `src/Player.cpp:962-1063`. Current rewrite: `new_src/domain/game/Player.cpp`
`Player::give` (3-arg). Changes:

1. **Signature**: `bool give(int kind, int slot, int amount, bool quiet = false);`
   `quiet` gates only the unported `showWeaponHelp` / `showInvHelp` / `showAmmoHelp`
   popups (`src/Player.cpp:1001-1003,:1029-1032,:1049`) — keep it as a documented
   parameter that currently only suppresses a stderr log line. Do **not** let it change
   any state. Existing 3-arg call sites keep compiling (default argument).
2. **Weapon, sentry-bot pre-step** (`:978-984`): if
   `(1 << (slot & 0xFF)) & Enums::WP_SENTRY_BOT_MASK` and `amount > 0`:
   `give(2, Enums::AMMO_SENTRY_BOT, 100, true); weapons &= ~0x78;` before the normal path.
   Add `bool hasASentryBot() const` to `Player` — `((weapons & 0x8) || (weapons & 0x20)) ||
   ((weapons & 0x10) || (weapons & 0x40))` (`src/Player.cpp:2489-2493`) — used by
   `ItemPickup`.
3. **Weapon auto-equip on first acquisition** (`:985,:1004-1006`): today the rewrite only
   equips when `weapon == -1`. Legacy equips whenever the bit was previously clear:
   `bool isNew = (weapons & bit) == 0;` … `if (isNew) selectWeapon(slot, def)`.
   `Player::selectWeapon` needs the def the legacy looks up itself
   (`find(6, 1, wp)`, `src/Player.cpp:160`), so add to `Player`:
   `void setDefs(const EntityDefs* defs) { defs_ = defs; }` + `const EntityDefs* defs_`,
   wired in `new_src/core/Main.cpp` next to the other wiring
   (`player.setDefs(&g_entityDefs);`). `give` then calls
   `selectWeapon(slot, defs_ ? defs_->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, slot) : nullptr)`.
   Keep the existing negative-amount branch (`:986-994`), but make removal reselect
   through `selectWeapon` only if a replacement exists; `selectNextWeapon` is unported —
   keep today's `weapon = -1` fallback and comment the citation.
4. **Inventory caps** (`:1011-1022`): already correct (9999 for slot 24, else 999,
   negative result → false). Add the **bottled water conversion** (`:1023-1025`):
   `if (slot == Enums::INV_BOTTLED_WATER) { give(2, Enums::AMMO_HOLY_WATER, n * 20, true); }
   else { inventory[slot] = (short)n; }` — note the legacy converts the *new total* `n`,
   not the delta, and does not store into `inventory[13]`. Reproduce verbatim, quirk and all.
5. **Ammo caps** (`:1035-1052`): already correct (100, soul cube slot 6 → 5). Keep the
   `repaintFlags |= 0x4` as a comment only.
6. `familiarReal` / `isFamiliar` mirror arrays (`weaponsCopy`/`inventoryCopy`/`ammoCopy`)
   are NOT ported — no familiar system exists. Single comment with `src/Player.cpp:995-1000`.

### G3 Acceptance

- Build clean; existing call sites (`CorpseLoot::giveLootPool`, `ScriptVM` GIVEITEM,
  `MenuSession`) unchanged in behaviour.
- **On screen:** picking up a weapon the player does not own auto-switches the HUD weapon
  button to that weapon immediately (legacy first-acquisition equip), and the ammo digits
  show the starter rounds (10, or 8 for the scoped rifle).
- Picking up a second copy of an owned weapon does NOT switch weapons.
- Sentry-bot branch: with a bot already owned nothing happens at all (no message, sprite
  stays); with no bot owned it grants the weapon, sets `ammo[7] = 100` and prints str 223.
  NOT verifiable on map00 — the map ships no reachable sentry bot (see the correction in §G1).
- Bottled water pickup increases holy-water ammo (capped at 100) and leaves
  `inventory[13] == 0`.

## 5. Deliberately NOT ported (record, do not implement)

| Legacy behaviour | Cite | Reason |
|---|---|---|
| Sound 1054 on pickup | `src/Entity.cpp:277` | no audio subsystem in `new_src`; TODO comment only |
| `ET_MONSTERBLOCK_ITEM` (eType 11) specifics | `src/Entity.cpp:123` | 0 defs of eType 11 in shipped `entities.bin` (research §A); the type is accepted by `touched()` for fidelity and never occurs |
| `ET_ENV_DAMAGE` touch (fire/spikes damage) | `src/Entity.cpp:134-146` | needs `painEvent`/status effects; `touchTile(b=false)` stays a no-op |
| Dropped items (`param` = qty, sprite backlink clear, def null) | `src/Entity.cpp:127-130`, `src/Game.cpp:2647-2679` | `spawnDropItem` unported; EV_GIVEITEM qty>0 path is a separate future task |
| `showWeaponHelp` / `showInvHelp` / `showAmmoHelp` popups | `src/Player.cpp:1001,1029,1049` | help-popup plumbing for gameplay grants not built; `quiet` kept as the gate |
| `hud->repaintFlags \|= 0x4` | `src/Entity.cpp:174`, `src/Player.cpp:1051` | HUD model is rebuilt per frame |
| `isFamiliar` / familiar mirror arrays | `src/Player.cpp:995-1000,1010,1036` | no familiar system |
| Hidden item sprites creating unlinked entities | `src/Game.cpp:453-455` | rewrite skips the sprite entirely; identical for pickup, differs only for a future EV_SHOW that unhides an item |
| Frame force for tiles 13/14 on drops (`spawnDropItem` uses 1..13) | `src/Game.cpp:2653-2655` | drop path unported; the load-time range 1..12 is the one implemented |

## 6. Threading / ownership

Single-threaded GL loop, unchanged. `ItemPickup` owns no state (pure `Env` + logic);
entity lifetime stays with `EntityDb`; the item entity is never destroyed, only hidden and
unlinked, so a later save/restore can revive it (`src/Entity.cpp:1861+`).
`touched()` may unlink the current entity — the `nextOnTile` cache in `touchTile` is
mandatory, and `EntityDb::TileWalk` guards against a corrupted chain, as everywhere else.
