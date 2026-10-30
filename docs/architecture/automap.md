# Automap (seen data, ST_AUTOMAP, drawing, script ops) — G1..G4b

Date: 2026-10-30. ADR: [0036](adr/0036-automap-seen-marking-and-state.md).
Facts (ground truth for every constant below): `docs/original-code/automap.md` (AM),
raw log `docs/research/2026-10-30-automap.md`. Whole original: `src/AutomapController.cpp`.

Threading: single game/GL loop (`GameLoop` → `GameContext::tick` / `render`). No new
threads, no new dependencies. Namespace `newcore` everywhere.

| Group | Content | Files | New files ⇒ reconfigure |
|---|---|---|---|
| G1 | render records seen lines | `render/ColumnOcclusion.h/.cpp`, `domain/game/ViewVisibility.h`, `render/World3D.cpp` | no |
| G2 | seen data + script ops on real data | NEW `domain/game/Automap.h/.cpp`; `domain/world/MapData.h`, `core/GameContext.cpp`, `core/PlayerActions.cpp`, `domain/game/ScriptVM.cpp` | **yes** |
| G3a | `StateId::Automap`, toggle, tick, input **TEST** | `core/GameStates.h`, `core/GameContext.h/.cpp`, `core/PlayerActions.cpp`, `domain/game/DoorSystem.h/.cpp` | no |
| G3b | forced exits in scripts + menu "View Map" | `domain/game/ScriptVM.cpp`, `core/MenuSession.cpp` | no |
| G4a | per-frame model builder | NEW `ui/AutomapModel.h`; `domain/game/Automap.h/.cpp` | **yes** |
| G4b | view, assets, wiring **TEST** | NEW `ui/AutomapView.h/.cpp`; `ui/UiAssets.h/.cpp`, `ui/UiTypes.h`, `render/Graphics2D.cpp`, `core/GameContext.h/.cpp` | **yes** |

Order is strict (each group compiles on top of the previous one).

Build: `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` after G2, G4a, G4b (GLOB);
`cmake --build build_new -j 8` after every group.

---

## 0. What already exists (reuse, do not duplicate)

* `MapData::mapFlags` (1024 bytes, `y*32+x`), `lineFlags` (nibble per line),
  `mapSpriteInfo`, `lineXs/lineYs` (`new_src/domain/world/MapData.h:59-61,76,88`).
  A fresh `MapData` is built per map load and moved over the old one
  (`new_src/core/Main.cpp:330-336`) — member defaults ARE the per-load reset.
* `WorldState` already saves/restores line bit 8, sprite `0x200000`, tile `0x80`
  (`new_src/domain/game/WorldState.cpp:185-219,285,304`). No change.
* `EV_HIDE_AUTOMAP` / `EV_UNHIDE_AUTOMAP` already write `mapFlags & 0x8`
  (`new_src/domain/game/ScriptVM.cpp:1575-1589`). No change.
* `World3D::occludeLeafLines` is the `drawNodeLines` port minus the `|= 8` write
  (`new_src/render/World3D.cpp:1269-1279`), called only for legacy-visible leaves.
* `ViewVisibility::drawnSprites` = the legacy `addSprite` set, hidden sprites excluded
  (`new_src/render/World3D.cpp:1390-1397`, ADR 0031).
* `GameContext::render` copies the frame's visibility at
  `new_src/core/GameContext.cpp:~967` (`sys_.game->visibility = scene_.visibility();`).
* TAB → `Action::Automap` (`new_src/core/UiInputCollector.cpp:95`); HUD right soft key →
  `UiAction::Automap` → `Action::Automap` (`new_src/core/GameContext.cpp:901`). Today
  `PlayerActions::handleAction` drops it in `default:`.
* `Player::god` (`new_src/domain/game/Player.h:57`); notebook/quests are NOT ported
  (`new_src/domain/game/Player.h:213`) → quest markers deferred (§9).

---

## G1 — render records seen lines (no visible change)

### `render/ColumnOcclusion.h/.cpp`
Change `void occludeLine(...)` to `bool occludeLine(int x0, int y0, int x1, int y1, bool sortX);`
returning the legacy `occludeClippedLine` result (`src/TinyGL.cpp:719-745`):
* `false` if `project()` fails (= `clipLine` false, `src/Render.cpp:733`);
* `false` if `endX <= begX`;
* otherwise `true` iff at least one column was tightened
  (`if (n3 < columnScale_[begX]) { columnScale_[begX] = n3; b = true; }`).
No other behaviour change. Existing caller `occludeLeafSprites` ignores the result.

### `domain/game/ViewVisibility.h`
Add, with a one-line comment:
```cpp
std::vector<int> seenLines;   // drawNodeLines candidates for line bit 8 (ADR 0036)
```

### `render/World3D.cpp`
* `drawBSP`: `vis_.seenLines.clear();` next to `vis_.drawnSprites.clear();` (before the
  early return).
* `occludeLeafLines` (replace the "not ported" comment), per line `i` of the leaf:
```cpp
const int t = map.lineFlags[i >> 1] >> ((i & 1) << 2) & 7;
if (t == 6) { vis_.seenLines.push_back(i); continue; }          // LINE_TYPE_MAP, src/Render.cpp:740-742
if (t != 0 && t != 4) continue;                                   // :723
if (occl_.occludeLine(...same args as today..., false))          // :733-736
    vis_.seenLines.push_back(i);
```
**Accept:** builds; game looks and plays exactly as before.

---

## G2 — seen data and script ops on the real data

### NEW `domain/game/Automap.h/.cpp`
```cpp
namespace newcore {
class MapData;
struct ViewVisibility;
namespace Automap {
// Applies one rendered frame's marks. gate = legacy game->updateAutomap
// (Playing incl. combat, or Automap; src/Canvas.cpp:802,811,830,934).
void markSeen(MapData& map, const ViewVisibility& vis, bool gate);
// uncoverAutomap (src/AutomapController.cpp:35-63). Caller guarantees the gate.
void uncover(MapData& map, int destX, int destY);
// Game::givemap (src/Game.cpp:1016-1036).
void giveMap(MapData& map, int x, int y, int w, int h);
} }
```
Constants in the .cpp: `kLineSeen = 0x8` (`LINE_FLAG_AUTOMAP`, `src/Enums.h:1204`),
`Enums::SPRITE_FLAG_AUTOMAP_VISIBLE` (exists, `new_src/domain/game/Enums.h:344`),
`kTileVisited = 0x80`, `kTileHidden = 0x8`; reuse `TileFlag::SOLID_AT_LOAD` (0x1) and
`TileFlag::BLOCKS_SIGHT` (0x2) from `domain/world/MapBits.h`.

`markSeen`:
```
if (!vis.valid) return;
for i in vis.seenLines:
    type = lineFlags[i>>1] >> ((i&1)<<2) & 7
    if (gate || type == 6) lineFlags[i>>1] |= 8 << ((i&1)<<2)
if (gate) for s in vis.drawnSprites: mapSpriteInfo[s] |= 0x200000   // src/Render.cpp:877-879
```
`uncover` (verbatim, incl. the `< 31` quirk — row/col 31 never uncovered):
```
n = destX >> 6; n2 = destY >> 6
if (n < 0 || n >= 32 || n2 < 0 || n2 >= 32) return
for i in n2-1 .. n2+1: if (i < 0 || i >= 31) continue
  for j in n-1 .. n+1: if (j < 0 || j >= 31) continue
    b = mapFlags[i*32+j]
    if ((j == n && i == n2) || (b & 0x2) == 0) mapFlags[i*32+j] |= 0x80
```
`giveMap`:
```
for i in 0 .. numLines-1: lineFlags[i>>1] |= 8 << ((i&1)<<2)
for j in 0 .. numMapSprites-1:            // file sprites only (legacy numMapSprites)
    tx = mapSprites[j] >> 6; ty = mapSprites[numSprites + j] >> 6
    if (tx >= x && tx < x+w && ty >= y && ty < y+h) mapSpriteInfo[j] |= 0x200000
for k in y .. y+h-1: for l in x .. x+w-1:
    if ((mapFlags[k*32+l] & 1) == 0) mapFlags[k*32+l] |= 0x80
```
Self-check (house pattern, `new_src/domain/game/WorldState.cpp:25-28,157-160`): an
`#ifndef NDEBUG` function run once (function-local `static bool checked`) from
`uncover`: synthetic `MapData` with `mapFlags.assign(1024,0)`, set `mapFlags[1*32+2] =
0x2` (x=2, y=1); `uncover(m, 1*64+32, 1*64+32)` ⇒ every tile x,y in 0..2 has 0x80
except (x=2, y=1); then `uncover(m, 31*64+32, 31*64+32)` ⇒ (30,30) has 0x80 and no tile
on row 31 or column 31 has it (the quirk). `assert` each; restore nothing (synthetic map).

### `domain/world/MapData.h` (Misc block)
```cpp
// Automap markers written by EV_MARKTILE/EV_UNMARKTILE; -1 = none. Reset by every
// map load through the fresh MapData (src/LoadingManager.cpp:317-322).
static constexpr int kMaxLadders = 10;   // Render::MAX_LADDERS_PER_MAP (src/Render.h:71)
int16_t mapEntranceAutomap = -1;          // src/Render.h:180
int16_t mapExitAutomap = -1;              // src/Render.h:181
int16_t mapLadders[kMaxLadders] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
```

### `core/GameContext.cpp` — `render()`
Right after `sys_.game->visibility.cinematic = ...;`:
```cpp
// Seen marks of this frame (ADR 0036); gate = legacy updateAutomap.
Automap::markSeen(*sys_.map, sys_.game->visibility, state_ == StateId::Playing);
```
(G3a extends the gate with `|| state_ == StateId::Automap`.)

### `core/PlayerActions.cpp` — `finishMovement`
Inside the existing `if (gotoThread == nullptr && monstersTurn == 0 && !skipAdvanceTurn)`
branch, call `Automap::uncover(*env_.map, p.destX, p.destY);` BEFORE `advanceTurn()`
(`src/MovementController.cpp:178-183`). (The automap branch is G3a.)

### `domain/game/ScriptVM.cpp`
* `EV_GIVE_AUTOMAP` (:1364): `Automap::giveMap(*env_.map, 0, 0, 32, 32);`
  (`src/ScriptThread.cpp:1815-1818`); log `[script] GIVE_AUTOMAP`.
* `EV_MARKTILE` / `EV_UNMARKTILE` (:1208-1222), after the existing monster-clip arm,
  `MapData& m = *env_.map; const int tile = y * 32 + x;`:
  * MARK (`src/ScriptThread.cpp:1161-1178`):
    `if (flags & 0x20) m.mapEntranceAutomap = tile; if (flags & 0x10) m.mapExitAutomap = tile;`
    `if (flags & 0x2)` → first slot `== -1` in `mapLadders` gets `tile` (none free ⇒ nothing);
    `m.mapFlags[tile] |= (flags & 0xC) << 2;` (iOS variant, `:1179`).
  * UNMARK (`:1757-1778`): `if ((flags & 0x20) && m.mapEntranceAutomap == tile) = -1;`
    same for 0x10/exit; `if (flags & 0x2)` every `mapLadders[k] == tile` → -1;
    `m.mapFlags[tile] &= ~((flags & 0xC) << 2);`
  * Drop the "(automap entrance skipped)" log suffix.

**Accept:** builds; debug self-check passes silently on first move; walking still
works; on map change + return the log of `WorldState` still round-trips (no crash). No
visible change yet.

---

## G3a — `StateId::Automap`, toggle, tick, input **TEST**

### `core/GameStates.h`
Add `Automap = 6, // legacy ST_AUTOMAP (src/Canvas.h:73)` and drop "Automap" from the
"out-of-scope stubs" comment.

### `domain/game/DoorSystem.h/.cpp`
```cpp
// ST_AUTOMAP door snap (src/Game.cpp:1153): finish every active door animation now,
// through updateDoors so every completion side effect runs.
void snapAll();
```
Body: `for (auto& a : doorAnims_) if (a.active) a.t = a.dur; updateDoors();`
Replace the "ST_AUTOMAP force-snap has no counterpart" comment (`DoorSystem.cpp:146-149`)
with a pointer to `snapAll`.

### `core/PlayerActions.cpp`
1. Monster gate (`handleAction`): `snapMonsters(false)` →
   `snapMonsters(env_.host->state() == StateId::Automap)` (`src/PlayingInputHandler.cpp:71-72`).
2. New arm (inside the switch, i.e. behind the same gates — legacy order):
   ```cpp
   case Action::Automap:   // src/PlayingInputHandler.cpp:86-98 (inTargetPractice not ported)
       env_.host->requestState(env_.host->state() != StateId::Automap ? StateId::Automap
                                                                      : StateId::Playing);
       break;
   ```
3. Split `case Action::BackKey`: in Automap → `requestState(StateId::Playing)`
   (`:138-141`); otherwise unchanged (falls into the Menu body). `Action::Menu` unchanged
   (opens the menu from the automap too, `:150-157`).
4. Blocked move (`Forward/Back` else-branch): add
   `if (env_.host->state() == StateId::Automap) env_.game->advanceTurn();`
   (`src/MovementController.cpp:351-353`; knockback not ported).
5. `finishMovement` tail becomes (`src/MovementController.cpp:178-195`):
   ```cpp
   if (env_.host->state() == StateId::Automap) {
       Automap::uncover(*env_.map, p.destX, p.destY);
       env_.game->advanceTurn();
       if (env_.game->lerps.animatingEffects() != 0) env_.host->requestState(StateId::Playing);
       else { env_.game->monsters.snapMonsters(true); env_.game->lerps.snap(-1); }
   } else if (gotoThread == nullptr && monstersTurn == 0 && !skipAdvanceTurn) {
       Automap::uncover(...); env_.game->advanceTurn();          // G2
   }
   ```
   Note the legacy automap arm does not test gotoThread/monstersTurn/skipAdvanceTurn.

### `core/GameContext.h/.cpp`
* `.h`: `void tickAutomap();` next to `tickCamera`.
* `tick()`:
  * gameTime advances in Automap too (add to the `Playing || Camera || Looting` list;
    the original never pauses here, `Player::unpause` is a stub, `src/Player.cpp:1419-1423`).
  * Input loop: `if (blocked || state_ != StateId::Playing) break;` →
    `if (blocked || (state_ != StateId::Playing && state_ != StateId::Automap)) break;`
    (`src/InputEventController.cpp:354-356`: same handler).
  * Lerp/door tick state list: add `StateId::Automap` (`updateLerpSprites`,
    `src/AutomapController.cpp:22`). Update the "Automap has no rewrite counterpart"
    comment.
  * Script threads stay Playing/Camera only (`src/Game.cpp:3259`) — no change.
  * `switch (state_)`: `case StateId::Automap: tickAutomap(); break;`
* `enterState_`: `case StateId::Automap: break;` with comment: no queue clear, no
  side effects (`src/Canvas.cpp:1188-1190` only resets `automapDrawn`/`automapTime`).
* `render()`: markSeen gate becomes `state_ == StateId::Playing || state_ == StateId::Automap`.
  Nothing else in render changes in G3a (`gameplayView` already excludes Automap → no
  HUD / weapon; the world band still draws).
* `tickAutomap()`:
```cpp
void GameContext::tickAutomap() {
    // Leaves exactly like the original: an attack seq means ST_COMBAT
    // (src/Combat.cpp:165), a paused blocking script forces ST_PLAYING
    // (src/Game.cpp:3256-3258).
    if (sys_.game->combat.active || sys_.vm->isInputBlockedByScript()) {
        setState(StateId::Playing);
        return;
    }
    sys_.game->doors.snapAll();                               // src/Game.cpp:1153
    // Sight wake of the last rendered frame (ADR 0029); the original's automap
    // updateView renders too (src/AutomapController.cpp:23-24).
    sys_.game->monsters.renderActivate(sys_.game->visibility);
    Player& p = *sys_.player;
    const bool moved = p.viewX != p.destX || p.viewY != p.destY;
    const bool turned = p.viewAngle != p.destAngle;
    // updateView in ST_AUTOMAP snaps view to dest (src/MovementController.cpp:492-498).
    p.viewX = p.destX; p.viewY = p.destY; p.viewZ = p.destZ; p.viewAngle = p.destAngle;
    if (moved) actions_.finishMovement();                     // (:510-512)
    if (turned && state_ == StateId::Automap) actions_.finishRotationFired();   // (:514-516)
}
```

**Accept / TEST (user):** TAB (or a click on the HUD "Map" arrow) → HUD bottom bar and
weapon disappear, 3D world still shown; arrow keys move/turn instantly (no glide)
and monsters take their turn; TAB again or Backspace → normal play with HUD; Esc
opens the in-game menu from the automap. Attacking a monster in automap (Enter) drops
back to play with the attack animating. Log: no new errors.

---

## G3b — forced exits in scripts + menu "View Map"

### `domain/game/ScriptVM.cpp`
Each is `if (env_.ctx->state() == StateId::Automap) env_.ctx->setState(StateId::Playing);`
* `evWait` (:285): inside `if ((t->flags & 0x1) != 0)`, after the `blockInputTime` line
  (`src/ScriptThread.cpp:129-131`).
* `EV_DIALOG` (:976): replace the "no automap state" comment, before the `skipDialog`
  test (`src/ScriptThread.cpp:551-554`).
* `EV_DAMAGEPLAYER` (:1200) and `EV_DESTROY_PLAYER` (:1634): right after each
  `combat.playerPain(...)` (= the tail of `Player::pain`, `src/Player.cpp:703-705`).
* `EV_DOOROP` (:566): `bool interactive = (op & 4) == 0 && env_.ctx->state() != StateId::Automap;`
  (`src/ScriptThread.cpp:751`); update the comment.
* `EV_CHANGE_MAP`: nothing — `changeMap` already leaves the state (Saving/Loading), the
  fade arm (`:504-506`) is the ignored deviation MD1.

### `core/MenuSession.cpp`
* `initMenu` refusal (:519-521): `case kActionChangeState:` disable only when
  `items_[i].def.param != 6` (reason "no ST_CREDITS state"); 6 = ST_AUTOMAP
  (`src/Canvas.h:73`).
* `select` switch: new arm
  ```cpp
  case kActionChangeState:                 // ACTION_CHANGESTATE (src/MenuSystem.cpp:3086-3092)
      if (items_[i].def.param == 6 && env_.host != nullptr) env_.host->requestState(StateId::Automap);
      break;
  ```
  (credits sound :3087-3091 not relevant: param 6 only reaches here.)

**Accept:** builds. In-game menu → "View Map" no longer logs a refusal and enters the
automap state (HUD gone). A script dialog triggered by walking onto its tile in the
automap opens normally (state leaves Automap first).

---

## G4a — per-frame model builder

### NEW `ui/AutomapModel.h`
```cpp
namespace newcore {
class Text;
// One fill or one 1-px line, canvas pixels, opaque ARGB (Graphics::setColor ignores
// alpha, src/Graphics.cpp:88-95). Rect: (x0,y0) top-left, (x1,y1) = (w,h).
// Line: endpoints, inclusive.
struct AutomapPrim { bool line; int16_t x0, y0, x1, y1; uint32_t argb; };
// Per-frame model of ST_AUTOMAP (AM §3). prims are in legacy draw order
// (tiles, ladders, walls, entities). Owned by GameContext, rebuilt every frame
// (clear() keeps capacity).
struct AutomapModel {
    std::vector<AutomapPrim> prims;
    bool cursor = false; int cursorSrcY = 0, cursorX = 0, cursorY = 0;  // 4x4 region, anchor 3
    const Text* softLeft = nullptr;   // "Menu"  (text 0/52)
    const Text* softRight = nullptr;  // "Leave" (text 0/53)
};
}
```

### `domain/game/Automap.h/.cpp` — add
```cpp
struct Blink { int64_t nextToggleMs = 0; bool on = false; };  // automapBlinkTime/State
void buildModel(AutomapModel& m, const MapData& map, const EntityDb& db,
                const Player& p, int64_t timeMs, Blink& blink);
```
Leaves `softLeft/softRight` untouched (GameContext fills them). `m.prims.clear()` first.
Constants (`src/AutomapController.cpp:73-76`): `kCell = 8`, `kOx = 112`, `kOy = 32`,
`kDiv = 0x400000 / (8 << 8)` (= 2048). `px(v) = ((v << 16) / kDiv + 128) >> 8`;
`px1(v) = ((v << 16) / kDiv + 1 + 128) >> 8`.

1. **Tiles** (`:79-101`), `i` = row 0..31 outer, `j` = col inner, `b = mapFlags[i*32+j]`,
   skip if `b & 0x8`:
   * `tile == mapEntranceAutomap` → rect(112+8j, 31+8i, 8, 8, `0xFF00FF00`);
   * else if `b & 0x80`: `tile == mapExitAutomap` → rect(112+8j, 31+8i, 8, 8, `0xFFFF0000`);
     else if `(b & 1) == 0` → rect(112+8j, 32+8i, 8, 8, `0xFF90B9E7`).
2. **Ladders** (`:103-118`), each `k`: `t = mapLadders[k]`, require `0 <= t < 1024`,
   `mapFlags[t] & 0x80`, `!(mapFlags[t] & 0x8)`; `x = 112 + 8*(t%32)`, `y = 32 + 8*(t/32)`:
   rect(x, y, 8, 8, `0xFFFFFF00`); lines `0xFF000000`: for l in 0,2,4,6: (x, y+l)-(x+8, y+l);
   then (x, y)-(x, y+8); (x+8, y)-(x+8, y+8).
3. **Walls** (`:120-129`), each line `n < numLines`: `f = lineFlags[n>>1] >> ((n&1)<<2) & 0xF`;
   if `f & 8` and `(f & 7)` is 0 or 6 → line(112+lineXs[2n], 32+lineYs[2n],
   112+lineXs[2n+1], 32+lineYs[2n+1], `0xFF2A3657`).
4. **Entities** (`:131-231`), tile order row-major, chain from `db.tileHead(i*32+j)` with
   an `EntityDb::TileWalk` guard; skip `&ents[0]`, `&ents[1]` (`ents = db.entities()`,
   guard size), skip tile `b & 0x8`, skip `e->def == nullptr` or sprite `< 0 ||
   >= numSprites` (rewrite guards). `si = mapSpriteInfo[s]`; require `si & 0x200000` and
   `!(si & 0x10000)`. `frame = (si >> 8) & 0xFF`. First match:
   | condition | colour | lineType | need |
   |---|---|---|---|
   | eType 5, tileIndex 271/272 | `0xFFFF8400` | yes | 2 |
   | eType 5, tileIndex 273/274 | `0xFF00C0FF` | yes | 2 |
   | eType 5, other | `0xFF3D68E3` | yes | 2 |
   | `si & 0x400000` | `0xFF2A3657` | yes | 2 |
   | eType 13 | `0xFF8D8068` | yes | 2 |
   | eType 3 (NPC, `big`) | `0xFF0000FF` | no | 0x80 |
   | eType 10 && frame == 0 | `0xFF8000FF` | no | 2 |
   | eType 2 | `0xFFFF8000` | no | 0x80 |
   | eType 7 (tileIndex 173/180 → colour 0) | `0xFF8D8068` | no | 2 |
   | `p.god` && eType 6 && eSubType != 3 | `0xFF00FFEA` | no | 2 |
   Draw iff `colour != 0 && ((b & need) != 0 || (need & 0x80) == 0)` (`:190`).
   * `si & 0xF000000` (directional): `sx = mapSprites[s]`, `sy = mapSprites[numSprites+s]`;
     `xa = xb = sx; ya = yb = sy;` if `si & 0x3000000` → `xa -= 32; xb += 32;` else
     `ya -= 32; yb += 32;` then `x0 = px1(xa)`, `x1 = px(xb)`, `y0 = px(ya)`, `y1 = px(yb)`.
     lineType → line(112+x0, 32+y0, 112+x1, 32+y1); else rect(112+((x0+x1)>>1)-2,
     32+((y0+y1)>>1)-2, 4, 4).
   * else `big` → rect(112+8j+2, 32+8i+2, 6, 6); else rect(112+8j+3, 32+8i+3, 4, 4).
5. **Quest markers** (`:246-277`): NOT built (notebook not ported). Leave a
   `// shortcut: quest markers need Player notebook (src/AutomapController.cpp:246-277), add with the journal` comment.
6. **Cursor** (`:292-324`): `if (timeMs > blink.nextToggleMs) { blink.nextToggleMs = timeMs + 333; blink.on = !blink.on; }`.
   `dir` from `p.destAngle & 0x3FF`: 256→0, 768→1, 0→2, 512→3, else 0 (`src/Enums.h:377-383`).
   `cursorSrcY = dir*4 + (blink.on ? 16 : 0)`; `cursorX = 112 + (8*(p.viewX-32))/64 + 4`;
   `cursorY = 32 + (8*(p.viewY-32))/64 + 4`; `cursor = cursorY < 320`.

**Accept:** builds. No caller yet.

---

## G4b — view, assets, wiring **TEST**

### `render/Graphics2D.cpp` — `drawLine`
Fast path first: if `x0 == x1 || y0 == y1`, one `fillRect(min x, min y, |dx|+1, |dy|+1, ...)`
and return (identical pixels to the Bresenham loop; keeps a full automap from emitting
~16k one-pixel quads into the 4 MB frame buffer, ADR 0025 point 6).

### `ui/UiTypes.h`
`UiId`: add `AutomapSoftLeft, AutomapSoftRight` before `Count`.

### `ui/UiAssets.h/.cpp`
```cpp
Texture automapBackground;   // gameHelpBG.bmp 480x320 (src/Canvas.cpp:171)
Texture automapCursor;       // Automap_Cursor.bmp (src/App.cpp:416), 4 px wide frames
```
Load with `loadUiTexture` next to the travel-map block. After loading, log once
`[automap] cursor %dx%d` with the texture size (layout unverified, AM open question).
(`MiniGameAssets` loads its own `gameHelpBG.bmp`; a second 150 KB copy is accepted.)

### NEW `ui/AutomapView.h/.cpp`
```cpp
// Stateless view of ST_AUTOMAP (drawAutomap, src/AutomapController.cpp:65-348).
UiResult drawAutomap(Ui& ui, const AutomapModel& m);
```
1. `ui.image(art.automapBackground, 0, 0, kTopLeft)` (`:71`).
2. Each prim: line → `ui.g().drawLine(x0, y0, x1, y1, r, g, b)` (precedent
   `new_src/minigames/vending/VendingView.cpp:59`); rect → `ui.panel(UiRect{x0, y0, x1, y1}, argb)`.
3. `if (m.cursor) ui.imageRegion(art.automapCursor, 0, m.cursorSrcY, 4, 4, m.cursorX, m.cursorY, kAnchorHCenter|kAnchorVCenter)` (flags 3, `:322`).
4. Soft keys (`:326-347`; the font render mode written there is never read,
   `src/Canvas.h:140` has no reader): `ui.softKey(UiId::AutomapSoftLeft, *m.softLeft, 15, 310,
   kAnchorBottom|kAnchorLeft /*36*/, UiRect{0, 250, 100, 70})` → `UiAction::Menu`;
   `ui.softKey(UiId::AutomapSoftRight, *m.softRight, 465, 310, kAnchorBottom|kAnchorRight /*40*/,
   UiRect{380, 250, 100, 70})` → `UiAction::Automap`. Hit rects = soft-key buttons 19/20
   (`src/Canvas.cpp:354-358`); touch release in ST_AUTOMAP sends their id
   (`src/TouchController.cpp:290-294`): 19 = left soft = MENU, 20 = right soft =
   ACTION_AUTOMAP (`src/InputEventController.cpp:110-111,133-134`). Skip a null label.
No arrow d-pad (the rewrite draws none in play either).

### `core/GameContext.h/.cpp`
* `.h`: members `AutomapModel automapModel_; Automap::Blink automapBlink_; Text automapLeftText_, automapRightText_;`
  include `ui/AutomapModel.h`, `domain/game/Automap.h`.
* `render()`: after the minigame block, before `ui->endFrame()` (opaque, owns the frame,
  same placement rule as the menu/travel map):
```cpp
if (state_ == StateId::Automap && ui != nullptr) {
    Automap::buildModel(automapModel_, *sys_.map, sys_.game->db, *sys_.player, upTimeMs, automapBlink_);
    setSoftKeyText(automapLeftText_, sys_.loc->get(kTextMain, 52));   // "Menu"  (src/SoftKeyController.cpp:88-89)
    setSoftKeyText(automapRightText_, sys_.loc->get(kTextMain, 53));  // "Leave"
    automapModel_.softLeft = &automapLeftText_;
    automapModel_.softRight = &automapRightText_;
    const UiResult r = drawAutomap(*ui, automapModel_);
    applyUiAction(r.action, r.index);
}
```
  (`setSoftKeyText` is the existing anonymous-namespace helper, `GameContext.cpp:~768`.)

**Accept / TEST (user), map01 start:**
* TAB: whole screen = the help-background art; a 256x256 area centred horizontally
  (x 112..367, y 32..287) shows light-blue (`#90B9E7`) 8x8 cells for the tiles walked
  around so far (3x3 per step), dark-navy (`#2A3657`) wall lines for everything seen
  in 3D, doors as orange / cyan / blue lines, monsters as orange 4x4 squares only on
  visited cells, NPCs as blue 6x6 squares.
* The player cursor (4x4 arrow from `Automap_Cursor.bmp`) sits on the player tile,
  points where the player faces, and flips its frame about 3 times per second.
* "Menu" bottom-left, "Leave" bottom-right; clicking "Leave" or TAB/Backspace returns
  to play; clicking "Menu" / Esc opens the in-game menu.
* Walking with the map open moves the cursor one tile per press and paints new cells.
* Leave the map, walk into an unseen room, reopen: its walls/cells now appear.
* Change map (N) and come back (Shift+N): previously seen parts are still there
  (WorldState).
* Report the `[automap] cursor WxH` log line.

---

## 9. Deviations (documented, accepted)

| # | Deviation | Why |
|---|---|---|
| A1 | World pass renders every frame under the map (original: only after input, `automapDrawn`). | Superset, identical marks for a static view (ADR 0036). |
| A2 | Help dialogs are not dequeued in the automap (`src/GameStateRunner.cpp:198-199` + `src/DialogSystem.cpp:763`). | They wait until Playing; legacy also needs force/monstersTurn there. |
| A3 | Quest markers not drawn. | Notebook not ported (`new_src/domain/game/Player.h:213`). |
| A4 | `snapAll` also snaps doors that started animating in Playing. | Invisible under the opaque map. |
| A5 | No `inTargetPractice`, sentry-bot discard, knockback, touch d-pad, held-button repeat. | Not ported anywhere in the rewrite. |
| A6 | `uncoverAutomap` at load end (`src/LoadingManager.cpp:710`) not called. | No-op in the original (gate false in ST_LOADING). |
| A7 | Lines rasterized inclusive (Bresenham / one rect) vs. GL_LINE_LOOP. | ≤1 px end difference. |
| A8 | `Player::advanceTurn` status-damage exit (`src/Player.cpp:88-90`) not wired. | That damage has no producer in the rewrite (`new_src/domain/game/Player.cpp:481-486`). |
