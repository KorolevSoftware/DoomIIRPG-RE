# Familiar (sentry-bot control) — spec

Date: 2026-11-02. ADRs: [0041](adr/0041-familiar-state-on-player-behaviour-in-core.md) (ownership,
`ST_BOT_DYING`, monster latch), [0042](adr/0042-drop-entity-pool.md) (drop-entity pool).
Supersedes ADR 0040 decisions 2, 3, 5.

Facts: `docs/original-code/familiar-core.md` (cited **FC §n**) and
`docs/original-code/familiar-presentation.md` (**FP §n**). Every legacy line below is quoted from
`src/`; the coder ports the cited function body verbatim unless a deviation is named here.

Conventions used throughout:
- "msg N flags F" = `hud->addMessage(loc->get(kTextMain, N), 0xAA000000, F)`; legacy flags 3 =
  `Hud::kMsgFlagForce | Hud::kMsgFlagCenter`, 2 = `kMsgFlagCenter`, 0 = none.
- "sound N" = `std::fprintf(stderr, "[sound] %d\n", N)` (no audio subsystem).
- `startShake(d, i, 500)` legacy → `hud->startShake(now, d, 2*i)` (rewrite intensity convention,
  `new_src/core/GameContext.cpp:236`).
- `startFade(d, 2)` → `hud->startFade(now, d, Hud::kFadeIn)`; `startFade(d, 1)` → `kFadeOut`.
- `tileDistances[0]` → `combat.tileDistSq(1)` (= 4096).
- Single-threaded GL loop; no new threads; all new state is plain members.

## 0. Module map of the change

| File | Change |
|---|---|
| `domain/game/Player.h/.cpp` | familiar fields, `give(..., forceReal)`, familiar branches of `getHealth`/`addHealth`/`pain`/`selectWeapon`/`addStatusEffect`, `swapStatusEffects`, `clearOutFamiliarsStatusEffects`, `stealFamiliarsInventory`, `reset` |
| `domain/game/PlayerState.cpp` | real familiar bytes |
| `domain/world/MapData.h`, `MapParser.cpp`, `domain/game/WorldState.cpp` | 16 drop sprite slots, `dropIndex` |
| `domain/game/Entity.h` | `int16_t name = -1` |
| `domain/game/EntityDb.h/.cpp` | drop ring |
| `domain/game/Game.h/.cpp` | drop entities appended at spawn; spawners; `attemptToDiscardFamiliar` |
| `domain/game/CorpseLoot.h/.cpp` | `addToLootSet` |
| `domain/game/EntityState.cpp` | drop + named branches |
| `domain/game/ItemPickup.cpp` | dropped quantity, dropped tail, `isFamiliar` refusal |
| `domain/game/DoorSystem.h/.cpp` | familiar door refusal, `canCloseDoor` |
| `domain/game/SpriteLerps.h/.cpp` | monster-at-body latch |
| `domain/game/CombatEntity.cpp`, `Combat.h/.cpp` | damage scaling, pain sound, bot low-ammo mask, `ce` death check, `radiusHurtEntities`/`hurtEntityAt` |
| `domain/game/ScriptVM.cpp`, `DialogSystem.cpp` | v1/v16, `EV_FORCE_BOT_RETURN`, `EV_DESTROY_PLAYER`, self-destruct close |
| **`core/Familiar.h/.cpp` (new)** | behaviour (ADR 0041) |
| `core/GameStates.h` | `StateId::BotDying = 25`, `Action::BotDiscard`, `Action::DebugGiveBot` |
| `core/GameContext.h/.cpp` | owns `Familiar`, wiring, death helper, BotDying tick/entry/render, monster latch consume, HUD model, view-weapon model, overlay call |
| `core/PlayerActions.h/.cpp` | `attemptMove` extracted, familiar checks, deploy/self-destruct/discard/return, input blocks, door refusal, wall-push sound |
| `core/MenuSession.h/.cpp` | "Return to player" row, action 33 |
| `core/UiInputCollector.cpp`, `core/GameLoop.cpp` | key B = BotDiscard (debug coords move to F3), F5 = debug give bot |
| `core/LoadingManager.cpp` | `save*` in the player header |
| `minigames/sentrybot/SentryBotGame.cpp`, `minigames/common/MiniGameHost.h` | real discard + help 16 |
| `render/SceneRenderer.cpp` | eye bob |
| `ui/Hud.h/.cpp`, `ui/UiAssets.h/.cpp`, `ui/HudModel.h`, `ui/HudView.cpp`, `ui/Ui.h/.cpp`, `ui/UiTypes.h`, `ui/ViewWeapon.h/.cpp` | overlay, brighten, sentry panels/vignette, bot block, soft keys, view-weapon pose |

New files: `core/Familiar.h`, `core/Familiar.cpp` only → **re-run the CMake configure step in G5**
(`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`; the build uses GLOB).

## 1. Shared definitions

### 1.1 `Player` additions (G1)

```cpp
// ---- Familiar (sentry-bot control), src/Player.h:45-117; ADR 0041 ----
bool isFamiliar = false;
int  familiarType = 0;                    // 0 none, 1 green shoot (w3), 2 green explode (w4),
                                          // 3 red shoot (w5), 4 red explode (w6) (src/Player.cpp:2404-2423)
bool attemptingToSelfDestructFamiliar = false;
int16_t inventoryCopy[26] = { 0 };        // weaponsCopy/ammoCopy already exist (shared with VIOS/range)
int16_t buffsCopy[30] = { 0 };
int  numbuffsCopy = 0;
int  statusEffectsCopy[54] = { 0 };
int  numStatusEffectsCopy = 0;
int  playerEntityCopyIndex = -1;          // body entity index, -1 none (src/Player.cpp:521, 2164)
bool botReturnedDueToMonster = false;     // write-only in legacy (src/Player.cpp:2384)
bool unsetFamiliarOnceOutOfCinematic = false;   // ADR 0041 §4 latch
bool noFamiliarRemains = false;           // EV_DESTROY_PLAYER arg 0 (src/ScriptThread.cpp:1942-1943)
int  saveX = 0, saveY = 0, saveZ = 0, saveAngle = 0, savePitch = 0;   // canvas->save* (body pose)

void clearOutFamiliarsStatusEffects();    // src/Player.cpp:2172-2183
void swapStatusEffects();                 // :2185-2206 (swap counts, buffs[30], statusEffects[54])
bool stealFamiliarsInventory();           // :2286-2311
bool give(int kind, int slot, int amount, bool quiet = false, bool forceReal = false);  // legacy b2
```
`goldCopy`, `tookBotsInventory`, `postProcessMode` are not added (ADR 0041 §1, §6).

### 1.2 `core/Familiar` (G5, grown in G8/G11)

```cpp
class Familiar {
public:
    struct Env {
        Game* game = nullptr;  Player* player = nullptr;  ScriptVM* vm = nullptr;
        MapData* map = nullptr;  Hud* hud = nullptr;  const Localization* loc = nullptr;
        const EntityDefs* defs = nullptr;  DialogSystem* dialogs = nullptr;
        PlayerActions* actions = nullptr;  StateHost* host = nullptr;
        const CinematicCamera* cinematic = nullptr;  const int64_t* upTimeMs = nullptr;
    };
    void init(const Env& env);
    void attemptToDeploy();                 // src/Player.cpp:2387-2425        (G5)
    void setFamiliar(int type);             // :2079-2126                      (G5)
    int  unsetFamiliar(bool intoBody);      // :2128-2170, returns old type    (G5)
    void returnsToPlayer(bool intoBody);    // :2278-2284                      (G5)
    void startSelfDestructDialog();         // :2439-2449                      (G5)
    void consumeMonsterLatch(StateId s);    // ADR 0041 §4                     (G8)
    void onSelfDestructDialogClosed(bool yes);  // src/DialogSystem.cpp:564-568 (G11)
    void dying(bool selfDestruct);          // src/Player.cpp:735-752          (G11)
    void onEnterBotDying();                 // src/Canvas.cpp:1130-1142        (G11)
    void tickDying();                       // src/GameStateRunner.cpp:283-322 (G11)
private:
    void died();                            // src/Player.cpp:2208-2253        (G11)
    void explode(int tx, int ty, int type); // :2255-2268                      (G11)
    void handleBotRemains(int x, int y, int w);  // :2313-2380                 (G11)
    bool showHelp(int n, bool force);       // :1175-1194                      (G5)
    Env env_;
    int64_t deathTime_ = 0;  bool selfDestructed_ = false;  bool shakeStarted_ = false;
};
```
Owned by value in `GameContext` (`Familiar familiar_;`), accessor `Familiar& familiar()`, wired in
`GameContext::init` next to `practice_`. `PlayerActions::Env` gains `Familiar* familiar`.

`showHelp(n, force)`: `if (cinematic->active()) return false; if (!force && (player.helpBitmask &
(1<<n))) return false; helpBitmask |= 1<<n; dialogs->enqueueHelpDialog(kTextMain, n, -1); if (n != 5
&& (host->state() == Playing || Automap)) dialogs->dequeueHelpDialog(); return true;`
(`src/Player.cpp:1175-1194`; `enableHelp` is not ported, treated as on — same as
`new_src/core/TargetPractice.cpp:45`).

---

## G1 — Player state, give routing, health/pain branches, save bytes, script vars

Files: `domain/game/Player.h/.cpp`, `domain/game/PlayerState.cpp`, `core/LoadingManager.cpp`,
`domain/game/ScriptVM.cpp`, `core/GameContext.cpp` (two lines).

1. Add §1.1 fields and methods.
2. `Player::reset()` additionally: `isFamiliar=false; familiarType=0;
   attemptingToSelfDestructFamiliar=false; botReturnedDueToMonster=false;
   unsetFamiliarOnceOutOfCinematic=false; playerEntityCopyIndex=-1; save*=0; numbuffsCopy=0;
   buffsCopy[]=0; numStatusEffectsCopy=0; statusEffectsCopy[]=0` (`src/Player.cpp:501-554`). Do NOT
   touch `weaponsCopy/ammoCopy/inventoryCopy` (legacy leaves them). `noFamiliarRemains` is not reset
   by legacy either.
3. `give` (`src/Player.cpp:970-1063`), replacing the comment at `Player.cpp:235-237`:
   - kind 1: sentry pre-step becomes `if (bot bit && amount > 0) { if (isFamiliar) return false;
     give(2,7,100,true); weapons &= ~0x78; }`; `amount < 0`: only `if (!isFamiliar || forceReal)`
     clear the bit (+ the existing weapon=-1 fallback), return true either way; `amount > 0`:
     `(isFamiliar && !forceReal ? weaponsCopy : weapons) |= bit`; auto-select only
     `if (isNew && !isFamiliar)`.
   - kind 0: target array `= (isFamiliar && !forceReal) ? inventoryCopy : inventory`; slot-13
     conversion only `if (!isFamiliar || forceReal)` and it calls `give(2, 3, n*20, true, true)`;
     otherwise slot 13 is stored in the chosen array.
   - kind 2: target `= (isFamiliar && !forceReal && slot != 6) ? ammoCopy : ammo`; same caps.
4. `getHealth()` → `isFamiliar ? ammo[AMMO_SENTRY_BOT] : ce.getStat(STAT_HEALTH)`
   (`src/Player.cpp:219-221`). `levelInit` switches to `ce.getStat(STAT_HEALTH) == 0` (`:102`).
5. `addHealth` (`:422-451`): when familiar, `stat = ammo[7]`, `max = 100`, apply
   `ammo[7] = max(0, ammo[7] + amount)` (no upper clamp), msg-111 delta from `ammo[7]`.
6. `pain` (`:652-701`): after the `n == 0` return, `if (isFamiliar) { addHealth(-n); return
   PainBand::None; }`.
7. `selectWeapon`: first line `if (isFamiliar) return;` (`:135-137`). `addStatusEffect`: first line
   `if (isFamiliar) return false;` (`:1565-1567`).
8. `clearOutFamiliarsStatusEffects`, `swapStatusEffects`, `stealFamiliarsInventory` verbatim
   (`:2172-2206`, `:2286-2311`; steal gives with `quiet=true`, `forceReal=false`, called only after
   `isFamiliar` is false).
9. `PlayerState::save` (`src/Player.cpp:1351-1419`): write `p.isFamiliar`, `(short)p.familiarType`,
   `p.playerEntityCopyIndex`, `p.inventoryCopy[26]`; after the statusEffects block,
   `if (p.isFamiliar) { writeByte(numStatusEffectsCopy); if (n != 0) for i<18: copy[36+i], copy[0+i],
   copy[18+i] as shorts }`. `load` (`:1270-1348`): read them into `tp` (the copy block exactly as
   `:1325-1340`: zero the copy when the count is 0; same `> 18` rejection as the main block). Extend
   `selfCheck()` with a second round trip that has `isFamiliar=true`, `familiarType=3`,
   `playerEntityCopyIndex=200`, non-zero `inventoryCopy` and 2 copy effects; assert equality.
10. `LoadingManager::headerAtPlayer` (`core/LoadingManager.cpp:78-93`): `saveX/Y/Z/Angle/Pitch =
    p.isFamiliar ? p.save* : p.view*` (legacy writes `canvas->save*`, `src/Game.cpp:2161-2165`; the
    non-familiar value stays what the rewrite writes today). `loadState`: after a successful
    `loadPlayer`, `player.saveX = h.saveX; …savePitch = h.savePitch;` (`src/Game.cpp:2178-2183`).
    Update the comment at `:78`.
11. `ScriptVM::updateScriptVars`: `vars[1] = ce.getStat(STAT_HEALTH)`, `vars[16] = isFamiliar ? 1 :
    0` (`src/Game.cpp:3461-3471`).
12. `GameContext::tickPlaying` death checks (`:595`, `:632`): `sys_.player->ce.getStat(STAT_HEALTH)
    <= 0` (`src/GameStateRunner.cpp:170`). The familiar branch arrives in G11.

Acceptance: builds; Debug start passes `PlayerState::selfCheck`; an existing save loads; no visible
change (nothing sets `isFamiliar` yet).

## G2 — Drop-entity pool (ADR 0042)

Files: `domain/world/MapData.h`, `domain/world/MapParser.cpp`, `domain/game/WorldState.cpp`
(selfCheck sizing only), `domain/game/Entity.h`, `domain/game/EntityDb.h/.cpp`,
`domain/game/Game.h/.cpp`, `domain/game/CorpseLoot.h/.cpp`.

1. `MapData`: `static constexpr int kMaxDropSprites = 16;` (`src/Render.h:54`),
   `int firstDropSprite() const { return numMapSprites + kMaxCustomSprites; }`. `MapParser:96`:
   `numSprites = numMapSprites + kMaxCustomSprites + kMaxDropSprites`
   (`src/LoadingManager.cpp:382`); the existing default loop over `[numMapSprites, numSprites)`
   (`MapParser.cpp:137+`) covers the new slots (`src/Render.cpp:2511-2523`). `WorldState.cpp:36`
   selfCheck uses the same sum.
2. `Entity`: `int16_t name = -1;` (-1 = def default).
3. `EntityDb`: `int firstDropIndex = 0, dropIndex = 0, lastDropEntIndex = 0;`
   `Entity* getFreeDropEnt();` verbatim `src/Game.cpp:2612-2622` (note the legacy loop tests
   `entities[lastDropEntIndex]` and advances it in the increment expression — keep that exact
   shape); `bool isDropped(const Entity* e) const` = index in `[firstDropIndex, firstDropIndex+16)`
   (`src/Entity.cpp:1405-1410`). `resetEntities()` zeroes the three ints.
4. `Game::loadEntities`, after the spawn loop and before the census log: `db.firstDropIndex =
   nextSlot;` then for k < 16: if `nextSlot == kEntities` log ERR_MAX_ENTITIES(35) and stop; else
   `Entity& d = db.entities()[nextSlot++]; d = Entity{}; d.setSprite(map.firstDropSprite() + k);
   map.mapSpriteInfo[map.firstDropSprite() + k] |= 0x10000;` (`src/Game.cpp:487-495`).
5. `Game` spawners (all verbatim; `getHeight` = `map.heightAt`, `S_ENT` = row 7, `relinkSprite` is a
   no-op in the rewrite, `name | 0x400` = text type 1):
   - `Entity* spawnDropItem(int x, int y, int tile, const EntityDef* def, int param, bool doThrow)`
     `src/Game.cpp:2647-2679` (tile `1..13` gets `|= 0x200`; `info = (info & 0xFFFF) | 0x400000`,
     `| 0x20000` for eType 10; Z = height+32; scale 64; link at `(x>>6, y>>6)`; throw if asked).
   - `Entity* spawnDropItem(int x, int y, int tile, int eType, int sub, int parm, int param, bool
     doThrow)` → `defs.find(eType, sub, parm)`, log + nullptr if absent (`:2681-2690`).
   - `void throwDropItem(int x, int y, int height, Entity* e)` `:2822-2853` on `lerps.allocLerpSprite
     (nullptr, sprite, eType != 6)` with `kFlagAsync | kFlagParabola`, height 48, 850 ms,
     `dropDirs = {64,0, 64,64, 0,64, -64,64, -64,0, -64,-64, 0,-64, 64,-64}` (`src/Game.h:25`),
     start dir `combat.nextByte() & 7`, `trace.trace(src, dst, e, 15855, 25)` must hit nothing and
     `!ai.isMonsterClip(dst tile)` (the `baseVisitedTiles` test).
   - `Entity* spawnPlayerEntityCopy(int x, int y, int characterChoice)` `:2725-2768`: tile/name
     72/224 default, 1 → 68/224, 3 → 66/225, 2 → 72/226; `def = defs.lookup(tile)`; `name = nameId`
     (text type 0, no `|0x400`); `info = (info & 0xFFFF) | 0x400000`; sprite info = tile; Z =
     height+32; rendermode 0; scale 64; `param = 0`; link.
   - `Entity* spawnSentryBotCorpse(int x, int y, int w, int n4, int n5)` `:2770-2811`: tile 19 / n7 0
     for w 3/4, tile 18 / n7 1 for w 5/6; `def = defs.find(9, 11, n7)`; `CorpseLoot::
     populateDefaultLootSet(e)` (sentry corpses get no default entry, `src/Entity.cpp:2008-2011`);
     `param = 0`; `addToLootSet(2,1,10)` for w 3/5 else `(0,12,1)`; then `(0,16,n4)`, `(0,13,n5)`;
     `info = (info & 0xFFFF) | 0x1420000`; sprite info `tile | 0xD00`; Z height+32; mode 0; scale 64.
6. `CorpseLoot::addToLootSet(Entity& e, int cls, int idx, int count)` verbatim
   `src/Entity.cpp:2298-2307` (requires `hasLootSet`, count > 0, first zero slot, count unmasked).

Acceptance: builds; `[spawn]` log shows 16 more slots used; maps look unchanged (all drop sprites
hidden). No caller yet.

## G3 — Drop entities in saves and pick-up

Files: `domain/game/EntityState.cpp`, `domain/game/WorldState.cpp`, `domain/game/ItemPickup.cpp`.

1. `EntityState` (`EntityState::Env` already reaches the db/map): replace the "no drop slots"
   simplifications with the legacy branches:
   - `isNamedEntity(e, out)` = `def != nullptr && name != -1 && name != ((1 << 10) | def->name) &&
     def->eType != ET_CORPSE` (`src/Entity.cpp:1484-1495`; -1 means default, ADR 0042 §4).
   - `getSaveHandle` (`:1787-1857`): `if (dropped && !(info & kInfoLinked)) return -1;` the dynamite
     brief skip; `| 0x20000` when named; the brief corpse rule keeps its `!dropped` term.
   - `saveState` (`:1497-1572`): name short first when `0x20000`; X/Y bytes when dropped; `writeInt
     (param)` when dropped; no `linkIndex` for drops; the `else if (dropped)` tail (type|sub<<4,
     parm, the three loot ints when `0x100000`, the dynamite byte).
   - `loadState` (`:1575-1785`): name read when `0x20000`; the dropped X/Y + Z = height+32 + param
     read; the dropped tail: `def = defs.find(type, sub, parm)`, `if (name == -1) name = def->name |
     0x400`, sprite tile (`find(2, sub, parm)->tileIndex` for corpse records), loot ints when
     `param == 0`, `S_ENT = index`; the final link.
2. `WorldState`: write `db.dropIndex` instead of 0 (`:272`, `src/Game.cpp:1708`), read it back
   (`:358`, `src/Game.cpp:1876`).
3. `ItemPickup::touchedItem`: `grantItem(def, sprite, db->isDropped(e) ? e->param : -1)`;
   `touched`: after the static func, if dropped: `mapSprites[S_ENT row + sprite] = -1; e->def =
   nullptr;` (`src/Entity.cpp:125-130`). Replace the "not ported" comment at `:53-54`.

Acceptance: builds; save → load on map01 still restores the world (no drops exist yet, so
`dropIndex` 0 round-trips).

## G4 — Discard, pick-up, Discard soft key, debug grant

Files: `core/GameStates.h`, `core/UiInputCollector.cpp`, `core/GameLoop.cpp`,
`core/GameContext.h/.cpp`, `core/PlayerActions.cpp`, `domain/game/Game.h/.cpp`,
`domain/game/ItemPickup.cpp`, `minigames/sentrybot/SentryBotGame.cpp`,
`minigames/common/MiniGameHost.h`, `ui/UiTypes.h`, `ui/HudModel.h`, `ui/HudView.cpp`.

1. `Action::BotDiscard` (ACTION_BOT_DISCARD = 21, `src/Enums.h:1010`) and `Action::DebugGiveBot`.
   `UiInputCollector::keyAction`: `SDL_SCANCODE_B → BotDiscard` (AVK_BOTDISCARD, `src/Input.h:149`,
   `src/InputEventController.cpp:43`); `SDL_SCANCODE_F5 → DebugGiveBot` (DEBUG, removable).
   `GameLoop.cpp:77-83`: the coordinate overlay moves from B to F3; update both comments.
2. `GameContext` debug dispatch (next to `DebugSentryBot`, `:363`): `DebugGiveBot` cycles
   `w = 3,4,5,6` (member counter) and calls `player.give(1, w, 1)` (DEBUG, removable).
3. `Game::attemptToDiscardFamiliar(int w)` (`src/Player.cpp:2427-2437`): own tile
   `mapFlags[(viewY>>6)*32 + (viewX>>6)] & 0x20` → msg 221 flags 3; else `spawnDropItem(viewX, viewY,
   Combat::getWeaponTileNum(w), ET_ITEM, ITEM_CLASS_WEAPON, w, player.ammo[7], false)` and
   `player.give(1, w, -1)`. Uses `player_`, `hud_`, the loc from `setXPSystems`.
4. `PlayerActions::handleAction`, new `case Action::BotDiscard` (it passes the monster gate like any
   non-turn action, `src/PlayingInputHandler.cpp:71-84`): `if (state != Automap) { if (isFamiliar)
   familiar->returnsToPlayer(false) /* G5 */; else if (bot weapon held) game->attemptToDiscardFamiliar
   (ce.weapon); }`. Until G5 the familiar arm is unreachable; write it in G5.
5. `ItemPickup` sentry pick-up (`:137`): refuse when `player.isFamiliar || player.hasASentryBot()`
   (`src/Entity.cpp:240-242`).
6. `SentryBotGame::award` (`src/SentryBotGame.cpp:784-804`): the first owned bot bit in order
   3,4,5,6 → `game->attemptToDiscardFamiliar(k)` then help 16 forced: `player.helpBitmask |= 1<<16;
   dialogs->enqueueHelpDialog(kTextMain, 16, -1)` (state is MiniGame, so no dequeue). Add
   `DialogSystem* dialogs` to `MiniGameHost::Env` and wire it. Remove the `[familiar]` shortcut log.
7. Soft keys (`src/SoftKeyController.cpp:77-100`) in `GameContext::buildHudModel`: add
   `UiAction softRightAction = UiAction::Automap;` to `HudModel`; precedence target practice >
   zoom (right 55) > automap > **familiar: right 216 "Return", action BotDiscard** > **bot weapon
   held: right 220 "Discard", action BotDiscard** > default 55/Automap. Touch rule of
   `src/Hud.cpp:1312-1320`: the bot-held discard applies only when the state is not Automap (the
   HUD model is only built in gameplay states, so `state_ != Automap` there). `HudView` returns
   `m.softRightAction` instead of the hardcoded `UiAction::Automap` (`HudView.cpp:90`). Add
   `UiAction::BotDiscard` and map it to `Action::BotDiscard` in `GameContext::applyUiAction`.

Acceptance (user): press F5 → bot weapon in hand (sprite of tile 19, bobbing), right soft key reads
"Discard". Press B → the bot lies on the floor in front of the feet (tile 19 sprite), the hands are
empty. Step off and back onto the tile → message 223 ("picked up") and the bot is in hand again;
F5 to red (w5), drop, pick up: health kept (debug log `ammo[7]`). On a tile with `mapFlags & 0x20`
B shows message 221. Save with a bot on the floor, reload → the bot is still on the floor.

## G5 — Familiar core: deploy, control, return to body, restrictions

Files: **new `core/Familiar.h/.cpp`** (reconfigure CMake), `core/GameContext.h/.cpp`,
`core/PlayerActions.h/.cpp`, `domain/game/DoorSystem.h/.cpp`, `domain/game/Game.cpp` (door wiring).

1. `PlayerActions::attemptMove(int tx, int ty) -> bool` (public): extract the body of
   `case Forward/Back` (`PlayerActions.cpp:164-210`) verbatim (leave event, abortMove, trace, NPC
   start-inside loop, commit, automap blocked-step turn), then add the legacy familiar checks of
   `src/MovementController.cpp:312-358`:
   - first: `if (isFamiliar && (mapFlags[(ty>>6)*32+(tx>>6)] & 0x10) && (tx != saveX || ty != saveY))
     { msg 222 flags 3; return false; }`;
   - after the trace: `intoBody = isFamiliar && tx == saveX && ty == saveY && h.isEntity() &&
     playerEntityCopyIndex >= 0 && h.entity == &db.entities()[playerEntityCopyIndex];` commit when
     `!h.blocks() || intoBody`, calling `familiar->returnsToPlayer(true)` first when `intoBody`.
   Forward/Back become `attemptMove(viewX ± stepX, viewY ± stepY); break;`.
2. Fire chain (`PlayerActions.cpp:339-344`), legacy order `src/PlayingInputHandler.cpp:489-495`:
   `if (isFamiliar && (type == 2 || type == 4)) { familiar->startSelfDestructDialog(); break; }`
   `if (!isFamiliar && bot weapon) { familiar->attemptToDeploy(); break; }`. A shooting familiar
   (type 1/3) falls through to the normal shot with `ce.weapon` 3/5 (G9 polishes it).
3. Wall push (`PlayerActions.cpp:332`): sound `isFamiliar ? 1111 : 1073` (`src/PlayingInputHandler.cpp:472-480`).
4. Door: `DoorSystem::useDoorFacing(..., bool asFamiliar)` returns new `DoorUseResult::BotRefused`
   on the first linked door candidate when `asFamiliar` (before the locked test,
   `src/PlayingInputHandler.cpp:446-457`); `PlayerActions` maps it to sound 1111 + msg 193 flags 2,
   consumed, no turn. `DoorSystem::Env` gains `const Player* player`; `canCloseDoor` starts with
   `if (env_.player && env_.player->isFamiliar) return false;` (`src/Game.cpp:1215-1219`); wire
   `player_` in `Game.cpp:30`.
5. Input blocks: at the top of `handleAction`, `if (isFamiliar && (a == Items || a ==
   ItemsDrinks)) return;` (`src/InputEventController.cpp:79-91`; PDA has no rewrite key). Weapon
   keys need nothing (`selectWeapon` no-op, G1).
6. `Familiar::attemptToDeploy` verbatim `src/Player.cpp:2387-2425`: walk the
   `game->monsters.activeMonsters` ring (`nextOnList`, stop when back at the head or the head
   became null); `trace.distFrom(m, destX, destY) <= 4096 && ai.isAttackValid(m)` → msg 219 flags 3,
   return; front tile `mapFlags[((viewY+viewStepY)>>6)*32 + ((viewX+viewStepX)>>6)] & 0x10` → msg 217
   flags 3, return; type from `ce.weapon` (4→2, 5→3, 6→4, else 1); `setFamiliar(type)`.
7. `Familiar::setFamiliar(type)` verbatim `:2079-2126`: return if state is Automap; `vm.vars[16]=1;
   save* = view*; isFamiliar = true; familiarType = type; vm.updateScriptVars();`
   `if (!actions->attemptMove(viewX+viewStepX, viewY+viewStepY)) { vars[16]=0; isFamiliar=false;
   familiarType=0; updateScriptVars(); msg 190 flags 3; return; }`; fog save skipped (ADR 0041 §6);
   `hud->startFade(now, 400, kFadeIn)`; `playerEntityCopyIndex = index of
   game->spawnPlayerEntityCopy(saveX, saveY, characterChoice)`; zero `weaponsCopy`, `ammoCopy[9]`,
   `inventoryCopy[26]`; `clearOutFamiliarsStatusEffects(); swapStatusEffects();`
   `showHelp(type 1/3 ? 12 : 13, false)` (soft keys are per-frame, nothing to redraw); sound 1108.
8. `Familiar::unsetFamiliar(intoBody)` verbatim `:2128-2173`: `hud->stopBrightenScreen()` (G6 adds
   it; until then omit), smack skipped; angle = `saveAngle`, or when `intoBody`: `saveX > viewX` →
   512 (ANGLE_WEST), `<` → 0, `saveY > viewY` → 256, `<` → 768 (`src/Enums.h:377-384`);
   `viewAngle = destAngle = angle; viewPitch = savePitch` (no destPitch in the rewrite); if
   `!intoBody` `dest/view X,Y,Z = save X,Y,Z`; `swapStatusEffects()`; fog load skipped;
   `startFade(now, 400, kFadeIn)`; `db.removeEntity(&entities[playerEntityCopyIndex])`;
   `playerEntityCopyIndex = -1`; `game->relinkPlayer(p)` (= unlink+relink, the legacy
   `unlink`/`relink` pair); `actions->finishRotationFired()` (legacy `finishRotation(true)`);
   `isFamiliar = false`; `old = familiarType; familiarType = 0; vm.updateScriptVars();` return old.
   Rewrite guard: also clear `unsetFamiliarOnceOutOfCinematic` (ADR 0041 §4).
9. `Familiar::returnsToPlayer(intoBody)`: sound 1112; `unsetFamiliar(intoBody)`;
   `player.stealFamiliarsInventory()` (`:2278-2284`).
10. `Familiar::startSelfDestructDialog` verbatim `:2439-2449`: if state != Automap:
    `attemptingToSelfDestructFamiliar = true`; `Text t(loc->get(kTextMain, 194));
    dialogs->startDialogText(nullptr, t, 12, 1, false)`. (Yes does nothing until G11.)
11. `PlayerActions::BotDiscard` familiar arm (G4 §4) now calls `familiar->returnsToPlayer(false)`.

Acceptance (user): F5 for a green shooting bot, face an open corridor, Enter → the view glides one
tile forward with a short fade-in, the hero (Major composite NPC) stands on the tile behind you
(turn around to see it). Walk around: the bot moves normally. Try a door: "Sentry Bot cannot open
doors." Try a no-bot tile (`mapFlags & 0x10`): "No Bot Access". Press B → instant snap back to the
hero's spot and view, the body is gone, bot still in hand. Deploy again, walk back into the body →
the view walks onto the body tile and you are the hero again, facing per the legacy rule. Pick up an
item as the bot, return → the item is in the hero's inventory. Deploy facing a wall: "No room to
deploy, step back." Deploy next to an awake monster: "You are under attack!…". I and O do nothing
while deployed. Exploding bot (F5 to w4) + Enter → "Self-destruct?" yes/no box.

## G6 — Presentation: tint, scanlines, brighten ramp, eye bob

Files: `ui/Hud.h/.cpp`, `render/SceneRenderer.cpp`, `core/GameContext.cpp`, `core/Familiar.cpp`.

1. `Hud` brighten state (`src/Hud.cpp:600-617`): members `bool brighten_; int64_t brightenBegin_,
   brightenMaxReached_; int maxLocalBrightness_, brightnessInitialBoost_;`
   `void brightenScreen(int64_t now, int max, int boost)` / `void stopBrightenScreen()` verbatim.
   `drawDamageEffects`' expiry branch (`Hud.cpp:177-179`) calls `stopBrightenScreen()` as legacy
   does (`src/Hud.cpp:525-530`); update that comment (smack stays unported).
2. `void Hud::drawFamiliarOverlay(Graphics2D& g, int64_t now, int familiarType)` = the GL part of
   `src/Render.cpp:2619-2659`; function statics become members `float ppUpdate_ = 0.f; int ppScanY_
   = 0;`:
   - brighten ramp verbatim (`:2619-2641`, auto-stop after max+1000 ms), `max` default 32;
   - `if (brighten_) g.fillRect(0, 0, 480, 320, 255, 255, 255, alpha8(min(1.0, max / 2226.0)))`;
   - `ppUpdate_ += 0.05f; ppScanY_ = (ppScanY_ + 1) % 30; alpha = sin(ppUpdate_) * 0.05 + 0.35;`
   - `(uint16_t)(familiarType - 1) > 1u` → red `(255,0,0)` else green `(0,255,0)`, rect
     `(0, 0, 480, 256)`, `alpha8(alpha)`;
   - `for (i = 0; i < 256; i += 10) g.fillRect(0, i + ppScanY_ / 3, 480, 5, 0, 0, 0, alpha8(0.15))`.
   `alpha8(a) = (uint8_t)std::lround(std::clamp(a, 0.0, 1.0) * 255.0)`.
3. `GameContext::render`: call `sys_.hud->drawFamiliarOverlay(g, upTimeMs, p.familiarType)` when
   `p.isFamiliar`, after the view-weapon block and the cockpit overlay, before `drawFade`
   (`src/Canvas.cpp:1344-1353`, `src/MayaCamera.cpp:316-321`). Called once per rendered frame (the
   legacy statics advance per frame).
4. `SceneRenderer` player path, right after the shake (`SceneRenderer.cpp:112-116`), before the
   160-unit pull-back: `if (player->isFamiliar) rvz += (sinTable[(*upTimeMs >> 2) & 0x3FF] >> 11) +
   128;` (`src/Render.cpp:2271-2273`).
5. `Familiar::unsetFamiliar` now calls `hud->stopBrightenScreen()` (G5 §8).

Acceptance (user): deployed green bot → the 3D view (not the HUD) is washed green, pulsing slowly
(~2 s), with thin dark horizontal bands scrolling down; the eye sits a bit higher and gently bobs
(~4 s period). Red bot (w5/w6) → red wash. Returning removes all of it.

## G7 — HUD while deployed

Files: `ui/UiAssets.h/.cpp`, `ui/Hud.h/.cpp`, `ui/HudModel.h`, `ui/HudView.cpp`, `ui/Ui.h/.cpp`,
`core/GameContext.cpp`.

1. `UiAssets`: load `HUD_Panel_top_sentrybot.bmp` (`src/Hud.cpp:44`),
   `gameMenu_Panel_bottom_sentrybot.bmp` (`src/MenuSystem.cpp:119`), `damage_bot.bmp`
   (`src/App.cpp:420`), `Hud_Sentry.bmp`, `HUD_sentry_active.bmp` (`:452-453`) into
   `panelTopSentrybot, panelBottomSentrybot, damageVignetteBot, sentryFace, sentryActive`.
2. `Hud::drawTopBar(g, font, w, bool familiar)`: after `panelTop`, if familiar draw
   `panelTopSentrybot` at the same anchor (`src/Hud.cpp:239-245`). `drawBottomPanel(g, bool
   familiar)`: after `panelBottom`, if familiar draw `panelBottomSentrybot` at `(0, 320 - h)`
   (`src/TouchController.cpp:542-548`). `drawDamageEffects(g, now, totalDamagePositive, bool
   familiar)`: vignette sheet `familiar ? damageVignetteBot : damageVignette` (`src/Hud.cpp:568`).
   Callers in `GameContext::render` pass `p.isFamiliar`.
3. `Ui::imageRegion(const Texture&, int sx, int sy, int sw, int sh, int x, int y, int anchor)` (one
   `Graphics2D::drawRegion` call, clip-aware like `image`).
4. `HudModel`: `bool familiar = false; int botHealth = 0; bool botRed = false;`. Producer:
   `familiar = p.isFamiliar; botHealth = p.ammo[7]; botRed = familiarType == 3 || == 4;` and, when
   familiar, `portraitHit = kBotHit{144, 263, 193, 37}` (`src/Hud.cpp:1246-1250`: strict `>` against
   143/262 and the 194x38 image; DEVIATION: legacy also requires the press to start on any HUD
   button, the gaps between buttons are accepted here).
5. `HudView` when `m.familiar` (`src/Hud.cpp:644-680`): soft keys/arrows as usual; skip weapon,
   shield, keys, health and portrait; then
   - colour: `h > 80` 0xFF59A907, `> 60` 0xFF96DA0E, `> 40` 0xFFEDD703, `> 20` 0xFFFF8B00, else
     0xFFFF0C00;
   - hit test `HudPortrait` on `m.portraitHit` → `UiAction::PassTurn` (button 3, `:1258-1259`);
     while held: colour 0xFFFFFFFF and `ui.image(sentryActive, 143, 261, topLeft)`;
   - face: `ui.imageRegion(sentryFace, 0, botRed ? 17 : 0, 29, 17, 174, 278, HCENTER|VCENTER)`
     (`SCR_CX - (15+118)/2 = 174`);
   - bar: `x0 = 240 + sentryFace.width() - 66`; `segs = h*20/100; if (segs < 20 && h != 0) ++segs;`
     `ui.panel({x0 + 6*i, 271, 4, 14}, colour)` for i < segs (no clamp: h > 100 draws more);
   - the centre "Wait" literal as before.
6. Left soft key stays "Menu"; right per G4 §7 ("Return" 216 / action BotDiscard).

Acceptance (user): deployed → top strip and bottom panel show the sentry-bot art variants; the bottom
bar shows a small bot face (green or red) and a segmented bar instead of weapon/armor/health/keys;
the bar shrinks and turns yellow→orange→red as the bot takes damage; holding the mouse on the bar
whitens it and shows the highlight plate, releasing passes a turn; right key reads "Return" and
returns the bot; being hit shows the bot vignette.

## G8 — Other return triggers + menu row

Files: `domain/game/ScriptVM.cpp`, `domain/game/SpriteLerps.h/.cpp`, `domain/game/Game.cpp`
(lerps wiring), `core/GameContext.cpp`, `core/Familiar.cpp`, `core/MenuSession.h/.cpp`.

1. `EV_FORCE_BOT_RETURN` (`ScriptVM.cpp:1578`, `src/ScriptThread.cpp:1744-1750`): `if
   (player.isFamiliar) env_.ctx->familiar().returnsToPlayer(false);`.
2. `SpriteLerps::Env` gains `Player* player` and `const TraceSystem* trace`; in `freeLerpSprite`,
   after the `finishLerp` call (`SpriteLerps.cpp:342-345`), for monster entities
   (`src/Game.cpp:3215-3226`): `if (player->isFamiliar && trace->distFrom(ent, saveX, saveY) <= 4096)
   { sx, sy = sprite X/Y; if ((sx == saveX) != (sy == saveY)) player->unsetFamiliarOnceOutOfCinematic
   = true; }`.
3. `Familiar::consumeMonsterLatch(StateId s)`: `if (!latch || s == Camera || s == BotDying) return;
   latch = false; if (!isFamiliar) return; returnsToPlayer(false); botReturnedDueToMonster = true;`
   (`src/Player.cpp:2382-2385`, `src/MayaCamera.cpp:386-388`). Called once at the very end of
   `GameContext::tick()`.
4. `MenuSession` (`src/MenuSystem.cpp:1549-1557`): `Env` gains `std::function<void()>
   returnToPlayer` (wired to `familiar_.returnsToPlayer(false)`). In the `kMenuInGame` patch: if
   `player->isFamiliar`: `selectedIndex_ = 1; addParsedRows(menuId, 0, 1); addItem(loc->makeId
   (kTextIngame2, 355), 0, kActionReturnToPlayer /*33*/, 0, loc->makeId(kTextIngame2, 356));
   addParsedRows(menuId, 1, -1); items_[0].def.flags = kItemDisabled; items_[6].def.flags =
   kItemDisabled;` (FILE_MENUSTRINGS = 3 = `kTextIngame2`, `src/MenuStrings.h:23`); else the current
   path (inventory[18] rule). The camera-active variant stays unported. Action 33 (`:3256-3259`):
   `returnToPlayer(); returnToGame();`. Replace the comment at `MenuSession.cpp:458-460`.

Acceptance (user): deployed, ESC → the menu shows "Return to player" as the second row, Inventory and
Status are greyed; selecting it closes the menu with you back at the body. Lure a monster so it
steps orthogonally next to the body → the bot snaps back immediately. Map05 boss / map07 cinematic
entrances with a deployed bot return it before the cinematic.

## G9 — Combat as the bot + view weapon

Files: `domain/game/CombatEntity.cpp`, `domain/game/Combat.cpp`, `core/GameContext.cpp`,
`ui/ViewWeapon.h/.cpp`.

1. `CombatEntity::calcDamage` monster-attack arm (`:256-260`, `src/CombatEntity.cpp:356-366`): if
   `p && p->isFamiliar` → `armorDamage = 0; damage = dmgStrMin * 7 / 8` (type 1/2) or `* 5 / 8`
   (3/4); else the existing armor split. The ANTIFIRE and DEFENSE lines stay after it.
2. `Combat::playerPainEvent`: sound `player->isFamiliar ? 1111 : 1093` (`src/Player.cpp:631-640`).
3. Low-ammo messages (`Combat.cpp:513-520`): add `&& ((1 << attackerWeaponId) & WP_SENTRY_BOT_MASK)
   == 0` (`src/Combat.cpp:213`).
4. `Combat.cpp:390`: `if (env_.player->ce.getStat(STAT_HEALTH) == 0) return;`
   (`src/Combat.cpp:1002`) — with `getHealth()` the dead bot would leave its attacker unlinked.
5. View weapon (`src/Combat.cpp:694-724, 788-795`, GL path). `ViewWeaponModel` gains `int extraY =
   0;` applied in `ViewWeapon::draw` as `y += m.extraY` after `y` is computed (`:787`).
   `buildViewWeaponModel` when `p.isFamiliar`: exploding types (2/4) → `visible = false`; shooting
   types → `m.weapon` stays `ce.weapon` (keeps the +12 bot bias), pose row = `weaponPose(0)`, with
   `atkX' = 1 + (atkX - idleX)`, `atkY' = 50 + (atkY - idleY)`, idle `(1, 50)`; `flashX/Y` from row
   0; `weaponTile = flashTile = getWeaponTileNum(0)`; `botTile = -1`; flash gates evaluated with
   weapon 0 (`(1<<0) & 0x200 == 0`, `(1<<0) & 0x181 != 0`); `playerAttackPose(ce.weapon)`;
   `extraY = -35`.

Acceptance (user): as a shooting bot the hands show the rifle art lower in the view; Enter shoots
(pistol sound log 1086), the rifle kicks and flashes; monsters target the bot, its bar drops by ~7/8
(green) or ~5/8 (red) of normal damage, armor never changes, pain log 1111. An exploding bot shows no
weapon art.

## G10 — Splash damage (`radiusHurtEntities`)

Files: `domain/game/Combat.h/.cpp`.

`void radiusHurtEntities(int tx, int ty, int knock, int dmg, Entity* attacker, Entity* skip)` and
`void hurtEntityAt(int x, int y, int cx, int cy, int knock, int dmg, Entity* attacker, bool b, Entity*
skip)`, verbatim `src/Combat.cpp:1085-1189`:
- center `(tx<<6)+32, (ty<<6)+32, height+32`; for the 3x3 tiles `trace.trace3D(center, tile
  center+height+32, nullptr, 4129, 1)`; no hit → `hurtEntityAt(j, i, tx, ty, (j==tx && i==ty) ? 0 :
  knock, dmg, attacker, false, skip)`; then for the 3x3 tiles `mask = 0x4004 |
  Game::eventFlagForDirection(tx - l, ty - k)` → `vm.executeTile(l, k, mask, true)`.
- `hurtEntityAt`: `crFlags = 16; shotsFired = true;` walk the tile list (`findMapEntity` + the
  30383 mask filter as `TraceSystem` uses it), skip `skip` and entities without `kInfoActive`:
  player entity (proj != 5): `dmg >>= 1; armor = min(dmg*10/100, ARMOR); dmg -= armor; dmg -= dmg *
  DEFENSE / 100;` `playerPainEvent(nullptr, false)`; knockback logged (unported); if `dmg > 0`:
  `crArmorDamage = armor; playerPain(dmg, nullptr, true); addArmor(-armor); addStatusEffect(13, 5,
  3); translateStatusEffects()`. Corpse: `if (!b) return;` (verbatim early return) else
  `entityDied`, hidden, blood logged. Monster: `info |= 0x200000`; activate if not on the active
  list (`monsters.activate(e, true, false, true, true)`); `n = weakness(attackerWeaponId) * dmg >> 8;
  n -= DEFENSE% * n >> 8`; if `n > 0`: proj-5 anim branch verbatim via `sprites.allocAnim(244, …)`;
  `pain = game->entityPain(e, n)`; knockback logged; msg 74 (dead) / 75 with name and n (via
  `centerMessage`); dead and proj != 5 → `game->entityDied(e, true)` (dynamite gib only when `b`).
  Other eTypes except ET_ENV_DAMAGE: `entityPain(e, dmg); entityDied(e, true)`.
- `attackerWeaponProj` is read stale, as legacy (`explodeFamiliar` sets only the weapon id).

Acceptance: builds; no caller yet (G11 is the first). Reviewer checks against the cited lines.

## G11 — Bot death, `ST_BOT_DYING`, self-destruct, remains

Files: `core/GameStates.h`, `core/Familiar.cpp`, `core/GameContext.h/.cpp`,
`domain/game/DialogSystem.cpp`, `domain/game/ScriptVM.cpp`.

1. `StateId::BotDying = 25` (`src/Canvas.h:92`). `enterState_(BotDying)` → `familiar_.
   onEnterBotDying()`; tick switch → `familiar_.tickDying()`. Not in the `gameTime` list; input is
   dropped by the existing dispatch rule.
2. Death helper in `GameContext`, used at both `tickPlaying` sites (`src/GameStateRunner.cpp:170-177`):
   `if (ce HEALTH <= 0) { setState(Dying); return true; } if (isFamiliar && ammo[7] <= 0) { ammo[7]
   = 0; familiar_.dying(false); return true; } return false;`. The combat path needs nothing else:
   `Combat::tick` / `onMonsterAttackDone` already stop on `getHealth() <= 0` (ADR 0041 §5), and the
   second site runs in the same tick, with `curAttacker` still set for `died()`.
3. `Familiar::dying(selfDestruct)` `src/Player.cpp:735-752`: return if state is BotDying; self →
   `combat.curAttacker = nullptr`; else sound 1109 + `startShake(now, 350, 10)`; `selfDestructed_ =
   selfDestruct; host->requestState(BotDying)`.
4. `onEnterBotDying` (`src/Canvas.cpp:1130-1142`): `deathTime_ = now; shakeStarted_ = false;
   hud->brightenScreen(now, 100, 0);` smack skipped; destPitch n/a.
5. `tickDying` (`src/GameStateRunner.cpp:283-322`), `t = now - deathTime_`:
   - self-destruct: `t >= 1500` → sound 1032, `requestState(Playing)`, `died()`; else if
     `!shakeStarted_ && t >= 750` → `shakeStarted_ = true; startShake(now, 1500 - t, 10)`.
   - killed: `t < 750`: `n = ((750 - t) << 16) / 750; viewZ = heightAt(destX, destY) + 18 + (20*n >>
     16); viewPitch = 96 + (-96*n >> 16); viewRoll = 16 + (-16*n >> 16);` then `updateView()` keeping
     `viewZ` exactly like `GameContext::tickDying` (`:739-743`); `t < 1500`: `if (!isFading())
     startFade(now, 750, kFadeOut); viewRoll = 16;`; else `viewRoll = 0` (legacy roll is a render
     argument, the rewrite stores it), `requestState(Playing)`, `died()`.
6. `died()` `src/Player.cpp:2208-2253`: if `combat.curAttacker`: monster sprite info `&= 0xFFFF00FF`,
   msg 112 with name + `accumRoundDamage` when > 0 (same call as `Combat.cpp:343-345`),
   `hud->stopShake(); setDamageDir(0); setDamageTime(0); curAttacker->monster->flags |=
   MFLAG_ATTACKING; sprites.clear(GameSprites::kFlagAnim);`. Then `bx = viewX, by = viewY; type =
   unsetFamiliar(false); msg 192 flags 2;` type 2/4 → `explode(bx>>6, by>>6, type)`; `w` = 4/5/6 for
   type 2/3/4 else 3; `noFamiliarRemains ? (noFamiliarRemains = false) : handleBotRemains(bx, by, w)`;
   `player.give(1, w, -1, true)`.
7. `explode(tx, ty, type)` `:2255-2268`: `combat.attackerWeaponId = type == 2 ? 4 : 6`; `dmg = strMin
   & 0xFF`, `max = strMax & 0xFF`, `if (dmg != max) dmg += combat.nextByte() % (max - dmg)`;
   `combat.radiusHurtEntities(tx, ty, 0, dmg, db.playerEntity(), nullptr)`.
8. `handleBotRemains(x, y, w)` `:2313-2380` verbatim: inventory 24 and 0..10 → `give(0, i, n, true,
   true)` + msg 86 `"+n " + loc->get(kTextIngame, find(6,0,i)->longName)` flags 0; 13 → n5, 16 → n4;
   11/12/17/19/20 → `spawnDropItem(x, y, def->tileIndex, def, n, true)`; ammo 1/2/4/5 (loop starts at
   1) → same with `find(6,2,j)`; each `weaponsCopy` bit k < 15 → `spawnDropItem(x, y,
   getWeaponTileNum(k), defs.lookup(tile), 1, true)`; then `spawnSentryBotCorpse(x, y, w, n4, n5)`.
9. `DialogSystem::closeDialog`, after the resume block (`src/DialogSystem.cpp:564-568`): `if (style_
   == 12) env_.ctx->familiar().onSelfDestructDialogClosed(env_.vm->vars[4] == 1 && !skip);` →
   `if (!flag) return; flag = false; if (yes) dying(true);`. Replace the comment at `:201`.
10. `EV_DESTROY_PLAYER` (`ScriptVM.cpp:1636-1648`, `src/ScriptThread.cpp:1925-1946`): keep the
    arg byte; `amount = isFamiliar ? ammo[7] : ce HEALTH`; `playerPainEvent` only when not familiar;
    damageTime/totalDamage/`playerPain(amount, nullptr, true)` and the automap tail as now; then not
    familiar → `addArmor(-ARMOR)`; familiar and arg == 0 → `noFamiliarRemains = true`.
11. Render in `BotDying` (`hud->repaintFlags = 36`, `src/GameStateRunner.cpp:287`): world (player
    pose, fov 290), view weapon (add BotDying to the view-weapon gate; `drawWeapon` only skips
    `ST_DYING`, `src/Combat.cpp:705`), familiar overlay (still familiar), fade, then
    `drawBottomPanel(g, true)` + `drawHud` with `showBottomBar = true`, `interactive = false`, both soft
    keys null (`clearSoftKeys`, `src/Canvas.cpp:1133`). No top bar, no messages, no vignette.

Acceptance (user): let monsters kill a green shooting bot → white flash builds, the view sinks and
tilts, fades to black, then fade-in at the hero with "Your Sentry Bot has perished!"; the bot weapon
is gone from the hands; a broken sentry corpse lies where the bot died, items the bot picked up are
scattered around it (thrown with a hop), looting the corpse gives 10 bullets (+ medkits/water it
carried). Exploding bot, Enter, Yes → white flash, shake from 0.75 s, explosion log 1032 at 1.5 s,
back at the hero; monsters within one tile of the bot spot are hurt/killed (msgs 74/75); if the hero
stands within one tile he is hurt too. "No" closes the dialog, the bot lives. A killed exploding bot
also explodes.

---

## 2. Target-practice / VIOS sharing

`weaponsCopy`/`ammoCopy`/`currentWeaponCopy` stay shared exactly as legacy (FC §1). Familiar zeroes
`weaponsCopy`/`ammoCopy`/`inventoryCopy` only at deploy (G5 §7) and never clears them on return;
`stripInventoryFor*`/`restoreInventory` are untouched and never touch `inventoryCopy`. The two uses
cannot overlap (range terminals are script-gated on `v16 == 0`). `restoreInventory`'s calls of
`give(..., true)` keep `forceReal = false`, harmless because it never runs while familiar.

## 3. Deviations (all intentional, see ADR 0041 §4, §6, ADR 0042)

D1 monster-at-body return ≤ 1 tick late; D2 smack, fog save/load, particle greyscale, `goldCopy`,
`tookBotsInventory`, `equipForLevel`, `flagForWeapon` not ported; D3 bot touch area includes the gaps
between HUD buttons; D4 knockback inside `hurtEntityAt` logged, not applied (no knockback anywhere in
the rewrite); D5 `viewRoll` is stored on `Player` during BotDying and zeroed on exit; D6
`unsetFamiliar` clears the monster latch (legacy could teleport to a stale body after a scripted
return during a cinematic).

## 4. Group order and dependencies

G1 → G2 → G3 → G4 → G5 → G6 → G7 → G8 → G9 → G10 → G11. G6/G7/G8/G9 only depend on G5 and can be
reordered among themselves; G11 needs G6 (brighten), G7 (bottom bar in BotDying) and G10.
