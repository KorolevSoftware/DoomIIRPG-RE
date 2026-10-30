# Automap (original, `src/`)

Verified 2026-10-30 (raw log: `docs/research/2026-10-30-automap.md`).
Main code: `src/AutomapController.cpp` (whole file, 348 lines).

## 1. State, toggling, exit

* State `Canvas::ST_AUTOMAP = 6` (`src/Canvas.h:73`). Action `Enums::ACTION_AUTOMAP = 7`
  (`src/Enums.h:999`).
* Keys mapped to `ACTION_AUTOMAP`: `AVK_SOFT2` (right softkey), `AVK_POUND`, `AVK_AUTOMAP`
  (`src/InputEventController.cpp:26,30,33`); `AVK_AUTOMAP` = `SDL_SCANCODE_TAB`
  (`src/Input.cpp:90`). Raw key 11 (`#`) and 20 (right soft) also map there
  (`src/InputEventController.cpp:110-111,133-134`). Touch: HUD button 1 sends
  `ACTION_AUTOMAP` (it sends `ACTION_BOT_DISCARD` instead when holding a sentry bot and
  not already in automap) (`src/Hud.cpp:1316-1322`).
* Menu "View Map": a menu row with `ACTION_CHANGESTATE` (9) does
  `canvas->setState(items[i].param)` (`src/MenuSystem.cpp:3086-3092`); param 6 = ST_AUTOMAP.
* Toggle: `handlePlayingEvents`, `ACTION_AUTOMAP` and not `inTargetPractice` →
  `setState(state != ST_AUTOMAP ? ST_AUTOMAP : ST_PLAYING)`
  (`src/PlayingInputHandler.cpp:86-98`). `ACTION_BACK` in automap → `ST_PLAYING`
  (`:138-141`). `ACTION_MENU` opens `MENU_INGAME` as in playing (`:150-157`).
* Input in ST_AUTOMAP: `automapDrawn = false`, then the SAME `handlePlayingEvents` as
  ST_PLAYING (`src/InputEventController.cpp:354-356`). So move/strafe/turn work while the
  map is open (`src/PlayingInputHandler.cpp:99-121`; turning also clears `automapDrawn`).
  Same gating as playing: ignored while view != dest, knockback, changeMapStarted,
  `blockInputTime`, weapon select, `activePropogators`/`snapMonsters(true)`/`animatingEffects`
  (`:25-74`; `b2 = state == ST_AUTOMAP` passed to `snapMonsters`, `:71-72`).
* `setState(ST_AUTOMAP)`: `automapDrawn = false; automapTime = app->time`
  (`src/Canvas.cpp:1188-1190`). Leaving: `player->unpause(time - automapTime)`
  (`src/Canvas.cpp:1030-1031`), but `Player::unpause` is an empty stub
  (`src/Player.cpp:1419-1423`).
* Softkeys in automap: `setSoftKeys(0,52, 0,53)` = "Menu" / "Leave"
  (`src/SoftKeyController.cpp:88-89`; strings per `docs/original-code/ui.md`).

### Does the world pause?
Not really. The game is turn-based; the automap does not advance turns by itself, but:
* Per frame in ST_AUTOMAP: held touch control button repeats every 250 ms, then
  `updateAutomap = true; automapState()` (`src/Canvas.cpp:927-935`).
  `automapState()` runs `game->updateLerpSprites()`, and if `!automapDrawn &&
  animatingEffects == 0` calls `updateView()` (3D render pass that still marks
  seen lines/sprites) and clears `REPAINT_VIEW3D`; then `drawPlayingSoftKeys()`
  (`src/AutomapController.cpp:18-33`).
* Script threads, `gsprite_update`, `UpdatePlayerVars` run every frame regardless of state
  (`src/Canvas.cpp:790-795`). Help dialogs are dequeued in automap too
  (`src/GameStateRunner.cpp:198-199`, `src/Player.cpp:1190`).
* Moving in automap: `MovementController` ST_AUTOMAP branch → `uncoverAutomap()`,
  `advanceTurn()` (monsters act), and if `animatingEffects != 0` → `setState(ST_PLAYING)`
  (`src/MovementController.cpp:185-190`).
* Lerp sprites (doors) are snapped instantly while in automap
  (`src/Game.cpp:1153`); `EV_DOOROP` skips its animation flag in automap
  (`src/ScriptThread.cpp:751`).

### Forced exits to ST_PLAYING
* Script thread with `flags & 1` (blocking) starts (`src/ScriptThread.cpp:129-131`).
* Map-change with fade (`src/ScriptThread.cpp:504-506`), dialog op (`:551-553`).
* `Player::advanceTurn` took status-effect damage (`src/Player.cpp:57-89`).
* `Player::pain` (`src/Player.cpp:703-705`).
* `setFamiliar` / `startSelfDestructDialog` refuse to act in automap
  (`src/Player.cpp:2083-2085,2442`).

## 2. "Seen" data

All arrays are in `Render`; nothing lives in the entities.

| Data | Bit | Meaning | Set where |
|---|---|---|---|
| `mapFlags[1024]` (u8, `y*32+x`) | `0x80` | tile visited | `uncoverAutomap` (below); `Game::givemap` |
| `mapFlags` | `0x08` | tile hidden from automap | from `.bin` packed tile nibbles (`src/LoadingManager.cpp:558-563`); `EV_HIDE_AUTOMAP` sets, `EV_UNHIDE_AUTOMAP` clears (`src/ScriptThread.cpp:1827-1853`) |
| `lineFlags[(numLines+1)/2]` (4 bits per line, line `i` in nibble `(i&1)*4`) | `0x8` (`LINE_FLAG_AUTOMAP`, `src/Enums.h:1204`) | line seen | `Render::drawNodeLines` during 3D BSP walk (`src/Render.cpp:717-744`) |
| `mapSpriteInfo[sprite]` | `0x200000` (`SPRITE_FLAG_AUTOMAP_VISIBLE`, `src/Enums.h:1236`) | sprite seen | `Render::addSprite` (`src/Render.cpp:877-879`) |
| `render->mapEntranceAutomap`, `mapExitAutomap` (short, `y*32+x`, -1 = none) | — | green/red marker tile | `EV_MARKTILE` flags 0x20/0x10 (`src/ScriptThread.cpp:1161-1166`); cleared by `EV_UNMARKTILE` (`:1762-1767`) and on map load (`src/LoadingManager.cpp:317-318`) |
| `render->mapLadders[10]` | — | ladder tiles | `EV_MARKTILE` flag 0x2 (`src/ScriptThread.cpp:1167+`) |

All writes of `0x80`, line `0x8` and sprite `0x200000` are gated by
`game->updateAutomap`, which is reset to false each frame (`src/Canvas.cpp:802`) and set
true only in ST_PLAYING (`:811`), ST_COMBAT (`:830`), ST_AUTOMAP (`:934`).

* `uncoverAutomap()` (`src/AutomapController.cpp:35-63`): tile `(destX>>6, destY>>6)`;
  returns if outside 0..31. For the 3x3 block around it, rows/cols restricted to
  `0 <= i < 31`, `0 <= j < 31` (row/col 31 never uncovered — faithful quirk):
  mark `|= 0x80` if it is the centre tile or `(mapFlags & 0x2) == 0`.
  Called after a finished move in ST_PLAYING (`src/MovementController.cpp:178-183`,
  only if `gotoThread == nullptr && monstersTurn == 0`), in ST_AUTOMAP
  (`:185-186`), and at map load end (`src/LoadingManager.cpp:710`; likely a no-op there
  since `updateAutomap` is false outside the three states — not verified at runtime).
* `drawNodeLines(leaf)`: for each line of a visible BSP leaf: type 0 (`LINE_TYPE_LINE`)
  or 4 (`LINE_TYPE_VIS`) → set `0x8` if it survives `clipLine` + `occludeClippedLine` and
  `updateAutomap`; type 6 (`LINE_TYPE_MAP`) → set `0x8` unconditionally
  (`src/Render.cpp:722-742`). Called from the BSP walk unless `skipLines`
  (`src/Render.cpp:1061-1064`).
* `addSprite(n)`: returns early for hidden (`0x10000`) sprites, otherwise sets
  `0x200000` if `updateAutomap` (`src/Render.cpp:830-832,877-879`).
* `Game::givemap(x,y,w,h)` (`src/Game.cpp:1016-1036`): ALL lines `|= 0x8`; sprites whose
  tile is inside the rect `|= 0x200000`; tiles in the rect with `(mapFlags & 1) == 0` get
  `|= 0x80`.

### Persistence
Per-map world blob (`saveWorldState`, `src/Game.cpp:1576-1734`, read `:1756-1901`):
line bit 8 bit-packed per line (`:1609-1624`, read `:1793`); 2 bits per map sprite incl.
`0x200000` (`:1637-1662`, read `:1805-1824`); `mapFlags & 0x80` for 1024 tiles, 8 per
byte (`:1665-1672`, read `:1825-1832`). NOT saved: `mapFlags & 0x8` (reloaded from `.bin`,
so `EV_HIDE/UNHIDE_AUTOMAP` changes are lost unless re-run by scripts),
`mapEntranceAutomap`/`mapExitAutomap`/`mapLadders` (re-created by map scripts).
The rewrite's `new_src/domain/game/WorldState.cpp` already writes/reads all three seen
bits (`:185-188` lines, `:202-212` sprites, `:219` / `:304` tiles).

## 3. Drawing (`drawAutomap`, `src/AutomapController.cpp:65-348`)

Fixed full-screen view, no scrolling, no rotation, no zoom. `fullRefresh` is unused
(`:66`). Constants: cell `n3 = 8` px, origin `n4 = 112`, `n5 = 32`
(`:73-76`) → map area x 112..367, y 32..287 (32x32 tiles, (480-256)/2 = 112).
`screenRect = {0,0,480,320}` at 480x320 (`src/Canvas.cpp:85-119`), so the
`screenRect[0/1]` terms are 0. Sprite world→px divisor `n6 = 0x400000/(8<<8) = 2048`;
`px(w) = ((w<<16)/2048 + 128) >> 8` ≈ `round(w/8)` (64 world units per tile → 8 px).
Line coords `lineXs/lineYs` are `uint8_t` in 1/8-tile units (`src/Render.h:168-169`),
used directly as pixels.

Draw order:
1. Background `imgGameHelpBG` ("gameHelpBG.bmp", `src/Canvas.cpp:171`), region
   0,0,480,320 at (0,0) (`:71`).
2. Tiles, for every tile with `(mapFlags & 0x8) == 0` (`:79-101`):
   * entrance tile (even if unvisited): `0xFF00FF00` fillRect(112+8x, 31+8y, 8, 8);
   * else if visited `0x80`: exit tile `0xFFFF0000` fillRect(112+8x, 31+8y, 8, 8);
     else if `(mapFlags & 1) == 0`: floor `0xFF90B9E7` fillRect(112+8x, 32+8y, 8, 8).
     (Entrance/exit are 1 px higher than floor cells.)
3. Ladders (`:103-118`), for `mapLadders[k]` in 0..1023, visited and not hidden:
   `0xFFFFFF00` fillRect(112+8x, 32+8y, 8, 8); then black `0xFF000000` horizontal lines at
   y offsets 0,2,4,6 from x to x+8, plus vertical lines at x and x+8 (full height 8).
4. Walls (`:120-129`): color `0xFF2A3657`; every line with bit `0x8` and type
   `(nibble & 7)` 0 or 6: `drawLine(112+lx0, 32+ly0, 112+lx1, 32+ly1)`.
5. Entities (`:131-231`), per tile, chain `entityDb[tile]`, skipping `entities[0]`
   and `entities[1]`, tile not `0x8`; sprite info `si = mapSpriteInfo[sprite]`, require
   `si & 0x200000` (seen) and not `si & 0x10000` (hidden). Colour by `def->eType`
   (first match), with `need` = tile bit required (default 2 = effectively none, see below):
   | Condition | Color | Shape |
   |---|---|---|
   | eType 5 DOOR, tileIndex 271/272 | `0xFFFF8400` | line |
   | DOOR, tileIndex 273/274 | `0xFF00C0FF` | line |
   | DOOR, other | `0xFF3D68E3` | line |
   | `si & 0x400000` (TILE flag) | `0xFF2A3657` | line |
   | eType 13 NONOBSTRUCTING_SPRITEWALL | `0xFF8D8068` | line |
   | eType 3 NPC | `0xFF0000FF`, need tile `0x80` | big square |
   | eType 10 ATTACK_INTERACTIVE with frame `(si>>8)&0xFF == 0` | `0xFF8000FF` | small square |
   | eType 2 MONSTER | `0xFFFF8000`, need tile `0x80` | small square |
   | eType 7 DECOR (tileIndex 173/180 → colour 0, not drawn) | `0xFF8D8068` | small square |
   | `player->god` && eType 6 ITEM && eSubType != 3 | `0xFF00FFEA` | small square |
   Draw test: `color != 0 && ((tileFlags & need) != 0 || (need & 0x80) == 0)` (`:190`) — so
   only monsters/NPCs additionally need the tile visited.
   Shapes: if `si & 0xF000000` (wall-aligned sprite, N/S/E/W): endpoints from sprite
   `S_X,S_Y` ±32 world units along x if `si & 0x3000000` (N/S) else along y;
   `x0 = (((sx-32)<<16)/2048 + 1 + 128)>>8`, others without the `+1` (`:192-208`).
   "line" types draw `drawLine(112+x0, 32+y0, 112+x1, 32+y1)`; non-line types draw
   fillRect at the midpoint `(112+((x0+x1)>>1)-2, 32+((y0+y1)>>1)-2, 4, 4)` (`:209-214`).
   Otherwise (tile-centred): NPC big square fillRect(112+8x+2, 32+8y+2, 6, 6) (`:216-217`);
   other fillRect(112+8x+3, 32+8y+3, 4, 4) (`:219-220`). "Line" types without a direction
   flag also get this tile-centred small square (`b5` only matters in the directional
   branch).
6. Quest markers (`:246-277`): for each notebook index not done and not failed,
   `x = pos>>5 & 31`, `y = pos & 31`, skip if `x+y == 0` or tile not visited; colour blinks
   `((time/1024) & 1) == 0 ? 0xFFFF0000 : 0xFF00FF00`. If `findMapEntity(x<<6, y<<6, 32)`
   exists → line along its sprite (same formula as walls); else fillRect(112+8x+2,
   32+8y+2, 6, 6).
7. Player cursor (`:292-324`): blink toggles every 333 ms (`automapBlinkTime =
   time + 333`). Direction from `destAngle & 0x3FF`: NORTH(256)→0, SOUTH(768)→1,
   EAST(0)→2, WEST(512)→3, any other → 0 (`src/Enums.h:377-383`). Source row
   `texY = dir*4 + (blink ? 16 : 0)`, 4x4 region of `imgMapCursor` ("Automap_Cursor.bmp",
   `src/App.cpp:416`). Position `cx = 112 + 8*(viewX-32)/64 + 4`,
   `cy = 32 + 8*(viewY-32)/64 + 4`; drawn with `drawRegion(..., flags 3)` = centred
   (flag 1 HCENTER, 2 VCENTER, `src/Graphics.cpp:351-368`), only if `cy < 320`.
   Uses `viewX/viewY` (smoothly interpolated), not dest.
8. Softkey labels (`:326-347`): `softKeyLeftID` text at (15,310) anchor 36,
   `softKeyRightID` at (465,310) anchor 40, dehyphenated; font render mode 2 unless the
   softkey button 19/20 is highlighted (then 0).
Then `Canvas` draws `m_softKeyButtons->Render` and `hud->drawArrowControls`
(touch d-pad) on top (`src/Canvas.cpp:441-446`). No map name, no 3D view, no HUD bars.

Disabled code: `#if 0` hidden-tile overlay `0xFF526F8B` (`:233-244`) and BSP node
bounds debug (`:280-290`).

## 4. Render path
Pure 2D: `Graphics::setColor` + `fillRect` (`src/Graphics.cpp:88-95`, alpha ignored,
drawn opaque) + `drawLine` (`:261-293`, GL_LINE_LOOP of 2 verts, 1 px, opaque) +
`drawRegion` (images) + `drawString`. All in canvas pixel coordinates (Graphics adds
`transX/transY` = `displayRect` origin). Maps 1:1 onto immediate-mode rect/line/image/
text primitives.

## 5. Cheats / reveal
* Debug menu `ACTION_GIVEMAP` (103) → `givemap(0,0,32,32)` (`src/MenuSystem.cpp:3377-3379`).
* Script `EV_GIVE_AUTOMAP` (85) → same call (`src/ScriptThread.cpp:1815-1818`).
* God mode (`ACTION_GODMODE` 107, `src/MenuSystem.cpp:3397-3399`) additionally shows
  items (eType 6, subtype != 3) in cyan `0xFF00FFEA` (`src/AutomapController.cpp:187-188`).
* `TILENUM_C_NO_AUTOMAP = 518` is declared (`src/Enums.h:855`) but unused in `src/`.
