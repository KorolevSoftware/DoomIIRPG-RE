# Secret doors (push walls) and secret awards — spec

Date: 2026-10-28. Facts: `docs/original-code/secret-doors.md` (§N below
refers to it), raw log `docs/research/2026-10-28-secret-doors.md`, trigger
order `docs/original-code/wall-push.md` §1.

## 0. Design decisions

1. **The two-phase secret lerp lives in `SpriteLerps`, not `DoorSystem`.**
   Legacy runs it through the generic LerpSprite pool
   (`allocLerpSprite(..., true)`, `src/Game.cpp:1081`) and re-arms the slot
   in `freeLerpSprite` (`src/Game.cpp:3170-3199`). Our `SpriteLerps` is that
   pool, and `SpriteLerps::animatingEffects()` already feeds every legacy
   `animatingEffects` gate (`new_src/core/PlayerActions.cpp:128`,
   `new_src/core/GameContext.cpp:454`, `new_src/domain/game/MonsterSystem.cpp:126`,
   `new_src/domain/game/Combat.cpp:259`). A slot flagged
   `kFlagAnimatingEffect` gives the 1500 ms input/monster block for free.
   `DoorSystem` (separate `DoorAnim`, not counted, registers `openDoors_`,
   sets DOORLERP at start, plays 1030) is left untouched.
2. **No renderer change.** `World3D` wall branch already applies the
   DOORLERP width collapse for every non-slip-door wall sprite
   (`new_src/render/World3D.cpp:618-619, 657-663` = `src/Render.cpp:586-599`),
   reads `S_SCALEFACTOR << 10` per frame (`World3D.cpp:520`), recomputes the
   sprite leaf every frame (`World3D.cpp:1212-1230`, so the map00 wall that
   slides from tile x 15 into x 16 stays drawn), skips HIDDEN sprites
   (`World3D.cpp:370`) and skips DOORLERP sprites in the occluder pass
   (`World3D.cpp:1155`, `src/Render.cpp:669-672`).
3. **`awardSecret` lives on `Game`** (legacy `Game::awardSecret`,
   `src/Game.cpp:3473-3491`), together with `mapSecretsFound`.
   `foundSecretsLevels` lives on `Player` (`src/Game.h`/`src/Player.cpp:557`).
4. **Achievement help dialog is logged, not shown.** Legacy enqueues a
   raw-Text help dialog of type 3 (`src/Player.cpp:1953-1989`); our
   `DialogSystem::dequeueHelpDialog` drops every type except 2
   (`new_src/domain/game/DialogSystem.cpp:229-236`). The +10 XP, the msg 103
   and the bit write are ported; the dialog text is logged.
5. No ADR: every choice above follows the legacy structure.

## G1 — `MapBits.h` + `SpriteLerps` two-phase secret lerp

### `new_src/domain/world/MapBits.h`
In `namespace TileFlag`, add (and drop 0x4 from the "still literals" list in
the namespace comment):
```cpp
// Secret push wall: fire on an ET_SPRITEWALL facing this tile opens it.
// File-only bit, never written at runtime; single reader
// src/PlayingInputHandler.cpp:461 (docs/original-code/secret-doors.md §4).
constexpr int SECRET_DOOR = 0x4;
```

### `new_src/domain/game/SpriteLerps.h`
- `Env`: add `bool* facingDirty = nullptr;` (canvas `updateFacingEntity`
  latch, same pattern as `MonsterSystem::Env`).
- `SpriteLerp` flags: add
  `kFlagSecretOpen = 0x100` (LS_FLAG_SECRET_OPEN) and
  `kFlagSecretHide = 0x200` (LS_FLAG_SECRET_HIDE) (`src/Enums.h:293-305`).
  Update the struct comment: the secret tail is now ported.
- Public:
  ```cpp
  // performDoorEvent(0, e, 1, true) (src/Game.cpp:1047-1157, b=true path):
  // starts the two-phase secret push-wall lerp on an ET_SPRITEWALL.
  // Returns false (nothing started) for eSubType 1, no sprite, or pool full.
  bool startSecretWall(Entity* e);
  // Legacy Game::secretActive: true from startSecretWall until the wall
  // vanishes (src/Game.cpp:1084, :3129). Read by the help-dialog pump.
  bool secretActive() const { return secretActive_; }
  ```
- Private: `bool secretActive_ = false;`

### `new_src/domain/game/SpriteLerps.cpp`
`init()`: also `secretActive_ = false;` (level-load reset, `src/Game.cpp:351`).

`startSecretWall(Entity* e)`:
```
if (!e || !e->def || !env_.map) return false;
if (e->def->eSubType == 1) return false;                 // :1059-1061
sprite = e->getSprite(); if (sprite < 0) return false;
ls = allocLerpSprite(nullptr, sprite, true);             // :1081 (async + animating effect)
if (!ls) return false;
n = numSprites; info = mapSpriteInfo[sprite];
mapSpriteInfo[sprite] = info & ~SPRITE_FLAG_HIDDEN;      // :1082
secretActive_ = true;                                    // :1084
ls->flags |= kFlagSecretOpen | kFlagEntNoRelink;         // :1085, :1129 (S_NORELINK: no sprite relink in the rewrite)
sx = S_X, sy = S_Y, scale = S_SCALEFACTOR;
ls->srcX = ls->dstX = sx; ls->srcY = ls->dstY = sy;       // :1092-1095
ls->srcZ = ls->dstZ = S_Z + spriteZBias(sprite, sx, sy);  // baked space (see spriteZBias doc)
ls->srcScale = ls->dstScale = scale;
if (info & 0x3000000)      ls->dstY += (info & SPRITE_FLAG_NORTH) ? 32 : -32;  // :1101-1108
else if (info & 0xC000000) ls->dstX += (info & SPRITE_FLAG_WEST)  ? 32 : -32;  // :1114-1121
ls->startTime = lerpClock_; ls->travelTime = 750;         // :1127-1128
return true;
```
No DOORLERP, no sound, no snap (player passes n2=1, `:1153-1155`).
Z note: interpolation stays constant in baked space; `updateLerpSprite`
already re-derives stored Z against the tile under the new X/Y, which is
what the renderer re-adds, so the drawn height equals legacy's constant
baked `S_Z` even when the wall crosses into another tile.

`freeLerpSprite(ls)`: keep the existing head (dst write, relink — skipped by
ENT_NORELINK, NPC/monster idle restore — not a spritewall). Insert BEFORE
`ls->hSprite = 0;`:
```
if (ls->flags & kFlagSecretHide) {                        // end of phase 2, :3127-3135
    if (env_.facingDirty) *env_.facingDirty = true;
    secretActive_ = false;
    if (ent && (ent->info & Entity::kInfoLinked)) env_.db->unlinkEntity(ent);   // tile passable
    if (ent && ent->def && ent->def->eType != Enums::ET_DOOR)
        mapSpriteInfo[sprite] |= SPRITE_FLAG_HIDDEN;
    // fall through: slot freed below (animating effect ends, :3205-3208)
} else if (ls->flags & kFlagSecretOpen) {                 // phase 1 -> phase 2 re-arm, :3170-3199
    ls->flags = (ls->flags & ~kFlagSecretOpen) | kFlagSecretHide;
    ls->srcX = ls->dstX; ls->srcY = ls->dstY;
    mapSpriteInfo[sprite] |= SPRITE_FLAG_DOORLERP;
    // S_SCALEFACTOR = dstScale already written by the head (:3176)
    ls->dstScale = 0;                                     // srcScale keeps 64 (:3177)
    ls->startTime = lerpClock_;
    switch (mapSpriteInfo[sprite] & 0x0F000000) {         // exact match, like legacy switch
        case SPRITE_FLAG_EAST:  ls->dstY += 32; break;
        case SPRITE_FLAG_NORTH: ls->dstX += 32; break;
        case SPRITE_FLAG_WEST:  ls->dstY -= 32; break;
        case SPRITE_FLAG_SOUTH: ls->dstX -= 32; break;
    }
    ls->travelTime = 750;
    return;                                               // slot stays, input stays blocked
}
```
`ent` is the `findEntityBySprite` result already computed in the head; hoist
it if needed so it is available in both arms (it is `nullptr` when
`env_.map` is null — guard). `automapDrawn` writes are omitted (no automap).
`updateLerpSprite` returns 3 after the re-arm too; harmless (owner is null,
the slot is async). `snap()` on a secret sprite completes phase 1 and re-arms
phase 2 with a fresh 750 ms — same as legacy (§2 snap note).

### `new_src/domain/game/Game.cpp` (wiring only)
`lerps.init({ &db, &map, vm_, &ai, &facingDirty });` (`Game.cpp:34`).

## G2 — `Game::awardSecret`, counters, `Player::foundSecretsLevels`

### `new_src/domain/game/Player.h/.cpp`
- Field `int foundSecretsLevels = 0;` (bit = mapID-1), cleared in
  `Player::reset()` (`src/Player.cpp:557`).

### `new_src/domain/game/Game.h/.cpp`
- Fields: `uint8_t mapSecretsFound = 0;` (`src/Game.h:130`); private
  `const Localization* loc_ = nullptr; Hud* hud_ = nullptr;`.
- `setXPSystems(player, loc, hud)` also stores `loc_ = loc; hud_ = hud;`.
- `loadEntities`: `mapSecretsFound = 0;` (unload reset `src/Render.cpp:306`;
  every level start / restart goes through `loadEntities`).
- Public `void awardSecret(bool pushWall);`, private `void addXPMessage(int xp);`

```
// src/Player.cpp:264-276: legacy addXP posts msg 103 (102 for xp<0) with flags 0.
void Game::addXPMessage(int xp) {
    if (hud_ && loc_) {
        std::string msg = loc_->get(kTextMain, xp < 0 ? 102 : 103);
        std::string a[1] = { std::to_string(xp) };
        composeArgs(msg, a, 1);
        hud_->addMessage(msg, 0xAA000000, 0);
    }
    if (player_) player_->addXP(xp);
}

// src/Game.cpp:3473-3491
void Game::awardSecret(bool pushWall) {
    if (hud_ && loc_) hud_->addMessage(loc_->get(kTextMain, 119), 0xAA000000, 0);  // "Found Secret!" :3476
    ++mapSecretsFound;                                                              // :3477
    std::fprintf(stderr, "[sound] %d\n", pushWall ? 1103 : 1020);                   // :3479-3484
    const int bit = 1 << (combat.loadMapID - 1);
    if (player_ && map_ && mapSecretsFound == map_->totalSecrets &&
        (player_->foundSecretsLevels & bit) == 0) {                                 // :3486
        // showAchievementMessage(1) (src/Player.cpp:1953-1989): help dialog
        // type 3 (raw text) is not supported by DialogSystem -> logged.
        std::string text = loc_ ? loc_->get(kTextMain, 135) : std::string();
        std::string a[1] = { loc_ ? loc_->get(kTextMain, 137) : std::string() };
        composeArgs(text, a, 1);
        std::fprintf(stderr, "[help] achievement (deferred dialog): %s\n", text.c_str());
        addXPMessage(10);
        std::fprintf(stderr, "[sound] 1020\n");
        player_->foundSecretsLevels |= bit;                                         // :3488
    } else {
        addXPMessage(5);                                                            // :3490
    }
}
```
`combat.loadMapID` is the rewrite's only map id (`Combat.h:92`, = 1 for map00).
Message order matches legacy: "Found Secret!" then "Gained N XP."
`levelGrade` / save of `mapSecretsFound` are out of scope (no callers yet).

## G3 — trigger, script op, dialog guard

### `new_src/core/PlayerActions.cpp`
Directly after `if (consumed) break;` and BEFORE
`if (weapon2 >= 0 && !env_.game->combat.active)` (legacy `:461-466`
precedes the wall push `:467` and is not under the combat gate):
```cpp
// Secret push wall (src/PlayingInputHandler.cpp:461-466): elected
// ET_SPRITEWALL within one tile AND the FACING tile carries 0x4. No turn.
if (elected.isEntity() && elected.eType == Enums::ET_SPRITEWALL &&
    electedDist <= env_.game->combat.tileDistSq(1) &&
    (env_.map->mapFlags[ty * 32 + tx] & TileFlag::SECRET_DOOR) != 0) {
    if (env_.game->lerps.startSecretWall(elected.entity)) env_.game->awardSecret(true);
    break;
}
```
`tx`/`ty` are the facing tile already computed for `executeTile`
(`(destX+viewStepX)>>6`, = legacy `n13/n14`, `:395-396`). Include
`domain/world/MapBits.h` if not already included. No `advanceTurn`, no
wall-push feedback. Shortcut (add as code comment): `elected` is only filled
when `weapon2 >= 0`; with no weapon the secret cannot be opened — upgrade if
an unarmed state becomes reachable in play.

### `new_src/domain/game/ScriptVM.cpp`
New case (no operands, `src/ScriptThread.cpp:1256-1259`):
```cpp
case Enums::EV_AWARDSECRET:                // op 50
    env_.game->awardSecret(false);         // chime 1020
    break;
```
Fixes the thread kill by the default branch (`ScriptVM.cpp:1506-1511`) on
map01 event 21 / maps 06-08.

### `new_src/domain/game/DialogSystem.cpp`
Replace the stub comment at `:226` with the legacy guard (`src/DialogSystem.cpp:766-768`):
```cpp
if (env_.game->lerps.secretActive()) return;   // help waits until the secret wall vanished
```
Placed after the state checks, before `type_ = helpTypes_[0]`. The queued
help is retried every Playing tick by `GameContext.cpp:477`; no new caller.

## Build

No new files; no CMake reconfigure. `cmake --build build_new -j 8`.

## Golden frames

The renderer is untouched and secret walls are not faced during the capture
window; `secretActive()` stays false so help-dialog timing is unchanged.
Golden frames must be byte-identical. Any diff is a bug.

## Acceptance (user, map00)

Wall A: stand on tile (13,10), face north, press fire (sprite 33 at (864,632)).
Wall B: stand on tile (15,24), face south, press fire (sprite 59 at (992,1608)).

For each:
1. The wall slides half a tile straight away from you over ~0.75 s (A to
   y 600, B to y 1640), same width.
2. Then it slides sideways (A toward -X, B toward +X) while narrowing to
   nothing over ~0.75 s, and disappears.
3. Input (moves/turns/fire) and monsters are frozen for the full ~1.5 s.
4. The tile is now walkable; firing at it again does nothing secret.
5. Top messages: "Found Secret!" then "Gained 5 XP." for the first wall;
   the second gives "Found Secret!" then "Gained 10 XP." plus stderr
   `[help] achievement ... Congratulations! You have found all of the secrets...`
   and `[sound] 1103`, `[sound] 1020`. No turn passes (monsters do not act
   because of the press).
6. Fire at an ordinary wall still gives the wall-push jolt.
7. After a level restart (death -> restart) both walls are back and award
   again: 5 XP, then 10 XP + achievement log. Reason: the rewrite restart
   calls `Player::reset()` (`GameContext.cpp:379`), which clears
   `foundSecretsLevels` (legacy instead reloads the save, which keeps the
   bit). Known deviation of the restart path, not of this feature.

## Groups

- **G1**: `MapBits.h`, `SpriteLerps.h/.cpp`, `Game.cpp` lerps wiring line.
- **G2**: `Player.h/.cpp` field+reset, `Game.h/.cpp` counters/awardSecret.
- **G3**: `PlayerActions.cpp` branch, `ScriptVM.cpp` case, `DialogSystem.cpp` guard.
