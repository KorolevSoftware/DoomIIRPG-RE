# Target practice (shooting range)

Verified facts about the original "Virtual Target Practice" on map00. It is **not** an
`ST_MINI_GAME` minigame (unlike hacking / vending, see `hacking-minigame.md`,
`vending-machine.md`). It runs inside normal `ST_PLAYING` with the sniper-scope zoom,
gated by the flag `Player::inTargetPractice`.
Raw investigation log: `docs/research/2026-10-30-target-practice.md`.

## 1. Entry

### 1.1 map00 script (disassembled with `tools/disasm_map_scripts.py`)

- Terminal: `EVT 134` tile (19,26), TRIGGER dir S, entry IP 8271. Gate `v16 == 0`
  (else `CALL func_9586`). It plays `PLAYSOUND 1065` at 8278 and then calls `func_8350`.
- `func_8350`:
  - 8350 `DIALOG str=91 type=1` (yes/no). Map string 91 = "Virtual Target Practice:
    Would you like to play? Requires 2 UAC Credits."
  - 8353 `EVAL v4 == 1` (yes).
  - 8360 `ITEM_COUNT inv[24] -> v67`, then 8364 `EVAL 1 < v67`, i.e. the player needs
    at least 2 credits.
  - 8371 `GIVEITEM def=107 qty=-2` takes 2 credits.
  - 8375 `START_TARGETPRACTICE args=(16,29,4)`, then 8378 `JUMP` → 8387 `RETURN`.
  - If the player has too few credits: 8381 `MESSAGE str=92`, "You do not have enough
    UAC Credits."
- `EVT 165` tile (16,29) is EXIT, all directions, IP 8388 `ABORT_MOVE`. The player cannot
  step off the shooting tile.
- The single target is sprite tile 149 `practice_target` (ET_DECOR) at tile (14,29), two
  tiles west of the firing tile (`docs/research/2026-08-30-sprite-blocking.md:196`).

### 1.2 Opcode `EV_START_TARGETPRACTICE` = 84

- Defined at `src/Enums.h:484`. Handler at `src/ScriptThread.cpp:1806-1813`. It reads one
  u16 `v` and calls `enterTargetPractice((v>>8)&0x1F, (v>>3)&0x1F, v&7, this)`.
  It then sets `unpauseTime = -1` and `n = 2`: the thread parks until the exit runs it again.
- A related opcode is `EV_INVENTORY_OP`, byte arg: 0 = strip for the VIOS battle,
  1 = `stripInventoryForTargetPractice`, 2 = `restoreInventory`
  (`src/ScriptThread.cpp:1627-1640`). map00 does not use it for target practice.

### 1.3 `Player::enterTargetPractice` (`src/Player.cpp:2521-2551`)

1. Sets `inTargetPractice = true` and `canvas->targetPracticeThread = thread` (`:2524-2525`).
2. Saves `canvas->saveX/Y/Z/Angle` from the view (`:2526-2529`).
3. Maps dir to an angle (`:2531-2542`): 4 → `ANGLE_WEST` 512, 0 → `ANGLE_EAST` 0,
   2 → `ANGLE_NORTH` 256, anything else → `ANGLE_SOUTH` 768 (`src/Enums.h:377-383`).
4. Snaps view and dest to the target tile: X/Y = `(tile<<6)+32`,
   Z = `getHeight(x,y)+36`, angle snapped (`:2543-2546`). For map00 that is X=1056,
   Y=1888, angle 512. The original does **not** relink the player entity or call
   finishRotation here.
5. Calls `showHelp(17,false)` (`:2547`). This is common string 17 "VIRTUAL TARGET
   PRACTICE|To become a great marksman, go for headshots, it's an automatic critical. …
   Session ends after 8 shots. Tap screen after 8 shots to exit target practice."
   It shows only once per game: `showHelp` checks the `helpBitmask` bit and
   `enableHelp` (`src/Player.cpp:1175-1194`). It uses table 0
   (`src/DialogSystem.cpp:839-841`).
6. Calls `stripInventoryForTargetPractice()` (`:2548`), sets
   `targetPracticeScore = 0` (`:2549`) and `render->startFade(750, 2)` (`:2550`).

The game state is not changed, so it stays `ST_PLAYING`.

### 1.4 Inventory strip/restore

- `stripInventoryForTargetPractice` (`src/Player.cpp:2694-2705`):
  - `currentWeaponCopy = ce->weapon`, `memcpy(ammoCopy, ammo)`, `weaponsCopy = weapons`.
  - Zeroes `ammo[0..8]`, then `weapons = 0`.
  - `give(1, 9, 1, true)`: weapon 9, the scoped rifle (the only weapon with the
    `0x200` scope mask, see `docs/original-code/loot-inventory.md:190`). Because the
    weapon bit was clear, `give` auto-selects it (`src/Player.cpp:1003-1005`).
  - `give(2, 1, 8, true)`: **8 bullets** (`AMMO_BULLETS = 1`, `src/Enums.h:104`).
- `restoreInventory` (`src/Player.cpp:2707-2759`). In target practice `weapons` has only
  bit 9, so `(weapons & 0x1) == 0` and the **else** branch runs (`:2743-2758`):
  - `b = weaponsCopy & 0x200`, i.e. the player owned the rifle before.
  - For each `i` in 0..8: `ammo[i] = 0; give(2, i, ammoCopy[i], true)`. This restores
    ammo, capped at 100 (`give` case 2, `:1033-1052`).
  - Re-gives every weapon bit 0..14 from `weaponsCopy`.
  - `forceRemoveFromScopeZoom()` (`:2762-2769`). If zoomed it calls
    `handleZoomEvents(-6, 15, true)`.
  - If the player did not own the rifle before (`!b`): `give(1, 9, -1)` removes it.
  - `selectWeapon(currentWeaponCopy)`.

## 2. Rules

### 2.1 Shooting

- FIRE in `PlayingInputHandler` (`src/PlayingInputHandler.cpp:189+`):
  - Trace mask 13997 (`:200`) contains bit 7 = `ET_DECOR`. A DECOR trace hit is only
    accepted when the sprite tile is `0x95` = 149 (`:346-352`).
  - When not zoomed with a scoped weapon and ammo > 0, the first FIRE only calls
    `canvas->initZoom()` (`:504-507`). Every scored shot is therefore fired zoomed in.
  - Zoomed: `zoomCollisionX/Y/Z = view + (frac * (end - view) >> 14)`, where `frac` is
    the 14.14 trace fraction and `end = view + 6*(-view-matrix fwd)>>8` (`:216-219`,
    `:518-522`). Then `fireWeapon(entity, ...)` (`:523`).
  - In target practice (`:524-531`): if `ammo[1] == 0` → `exitTargetPractice()`, else
    `assessTargetPracticeShot(entity)`.
  - When nothing is hit (`:535-544`): fire at world. Then `ammo[1] == 0` → exit, else
    `hud->addMessage(68)` "Missed!".
- Ammo is **not** deducted inside `fireWeapon`. `performAttack` only switches to
  `ST_COMBAT` (`src/Combat.cpp:91`, `:161`). The deduction `ammo[type] -= usage` happens
  later in the combat stage machine (`src/Combat.cpp:355-357`). As a result:
  - Shots 1..8 see `ammo[1]` = 8..1 and are all scored.
  - The 9th FIRE has `ammo[1] == 0`. `fireWeapon` refuses with msg 115 "Out of Ammo!"
    (`src/Player.cpp:782-795`), then the exit branch runs.
  - So a session = **8 scored shots, then any FIRE/tap exits**. This matches help 17.
- While `ST_COMBAT` runs, input does not reach the playing handler; keys are dropped
  (`src/InputEventController.cpp:364-385`).

### 2.2 Hit zones (`Player::assessTargetPracticeShot`, `src/Player.cpp:2553-2598`)

- `d = zoomCollision - mapSprites[S_X/S_Y/S_Z of target sprite]` (`:2557-2559`).
  `anim = (mapSpriteInfo >> 8) & 0xF0` (`:2560`). A value of 16 = `MANIM_IDLE_BACK`
  (`src/Enums.h:570`).
- `getImageFrameBounds(def->tileIndex, 3, 2, 0)` (`:2561`) builds three boxes from
  `mediaBounds` of frames 3, 2, 0 (`src/Render.cpp:2025-2036`). Each frame has 4 bytes
  `b0..b3`, in 176-px sprite space:
  - `[0] = b0*64/176 - 32`, `[1] = b1*64/176 - 32` (horizontal)
  - `[2] = (176-b3)*64/176 - 32`, `[3] = (176-b2)*64/176 - 32` (vertical)
- Zone `i` (0..2) matches when `[0] < dx < [1]` AND `[0] < dy < [1]` AND
  `[2] < dz < [3]`. Both dx and dy are tested against the same horizontal range
  (verbatim, `:2563-2567`). The first match wins.
- Scoring (`:2569-2597`):

| Zone | Condition | Message (common id, flag 4) | Score | Flash timer |
|---|---|---|---|---|
| head | zone 0 (frame 3) or anim == 16 | 230 "Head-shot!" | +30 | `headShotTime = time+500` |
| body | zone 1 (frame 2) | 231 "Body-shot!" | +20 | `bodyShotTime = time+500` |
| leg | zone 2 (frame 0) | 232 "Leg-shot!" | +10 | `legShotTime = time+500` |
| none | — | 68 "Missed!" | 0 | all three timers = 0 |

  Each hit zeroes the other two timers.
- The same zone logic is used for monster crits with scoped weapons
  (`src/CombatEntity.cpp:173-212`).
- Targets are static: one map sprite, no spawning, no movement, no pop-up. The only
  feedback is the 500 ms frame flash (§3.2).

### 2.3 Exit and reward (`Player::exitTargetPractice`, `src/Player.cpp:2600-2634`)

1. `inTargetPractice = false`. View and dest are restored from `save*`, followed by
   `finishRotation(false)` (`:2603-2608`). The player returns to where they triggered
   the terminal.
2. `restoreInventory()`, then `startFade(750, 2)` (`:2609-2610`).
3. Max score `n = 240` (= 8 × 30). Text arg 1 = score (`:2611-2613`).
4. If `score > 120` (`n/2`, strict):
   - `gain = modifyStat(5, 1)`. Stat 5 = `STAT_ACCURACY` (`src/Enums.h:312`).
   - If gain > 0: arg 2 = gain, message 233 "Practice Score: %01|+%02 to Accuracy",
     flag 3.
   - Otherwise message 235 "Practice Score: %01|No Accuracy Bonus|Accuracy at Max".
5. Else: message 234 "Practice Score: %01|No Accuracy Bonus" (`:2615-2627`).
6. If `targetPracticeThread` is set: `setState(ST_PLAYING)`, `thread->run()`, thread =
   null (`:2629-2633`). On map00 the script resumes at IP 8378 → RETURN. No script vars
   receive the result.

- `modifyStat` (`src/Player.cpp:223-247`) caps every stat except IQ (7, cap 200) at
  **99**. It changes `baseCe`, calls `updateStats()` and returns the actual delta.
- The game is repeatable without limit: no flag is set, and each run costs 2 credits.
  The minimum winning score is 130, e.g. 3 head + 2 body, or 5 body + 1 head.

## 3. Drawing

### 3.1 Score bar

- Drawn every frame while `inTargetPractice`, after the HUD
  (`src/Canvas.cpp:419-421` → `MiniGameManager::drawTargetPracticeScore`,
  `src/MiniGameManager.cpp:222-233`). The text is common 229 "Score:" with the decimal
  score appended directly, without a space.
- It uses `Hud::drawImportantMessage(..., 0xFF7F0000)` (`src/Hud.cpp:396-410`):
  - Rect = viewRect x/y, width `viewRect[2]-viewRect[0]-1`, height 18.
  - Fill 0xFF7F0000, white 0xFFFFFFFF outline.
  - Dehyphenated text at (+2, +2), anchor 4.
- It is skipped while the front HUD message has flag 4
  (`msgCount != 0 && messageFlags[0] & 4`). The HUD draws that message in the same
  bar style and color (`src/Hud.cpp:256-257`). So "Head-shot!" etc. temporarily replace
  "Score:N".
- Top bar: in target practice `Hud` sets `b = true` (`src/Hud.cpp:281-282`). This
  suppresses the facing-entity name and action icon (`:376+`).

### 3.2 Target sprite (`src/Render.cpp:1672-1713`, `TILENUM_PRACTICE_TARGET = 149`, `src/Enums.h:763`)

The flags are `n2 & 0xFFFDFFFF`, which clears bit 0x20000. All layers use tile 149 at the
same position, drawn in this order:

| Layer | Timer running (> time) | Timer == 0 | Timer expired |
|---|---|---|---|
| legs | frame 0 | frame 5 | timer reset to 0, nothing drawn this frame |
| body | frame 2 | frame 6 | same |
| head | frame 3 | frame 7 | same |
| board | frame 1 if zoomed, else frame 4 | — | — |

The board is drawn last. Hit-flash frames 0/2/3 are the same frames used for the zone
boxes.

### 3.3 Scope and softkeys

- The scope is the generic sniper zoom, with nothing specific to target practice:
  - `imgScope` (`src/Hud.cpp:730-731`)
  - dial `SniperScope_Dial.bmp` / `_Knob.bmp` and the scroll button at (400,67)
    (`src/Canvas.cpp:234-243`, `src/Hud.cpp:750-760`)
  - FOV 190 → min 102 (`src/ZoomController.cpp:21-43`, `:120-128`)
  - accuracy-stat sway `zoomAccuracy` (`:36`)
- Softkeys (`src/SoftKeyController.cpp:81-83`): left = common 30 "Exit", right cleared.
  This branch has priority over the zoomed softkeys.

## 4. Input

| Action | Not zoomed (`handlePlayingEvents`) | Zoomed (`handleZoomEvents`) |
|---|---|---|
| FIRE | 1st FIRE: initZoom (`src/PlayingInputHandler.cpp:504-507`); with 0 ammo: exit | shoot / score; with 0 ammo: exit (`src/ZoomController.cpp:152-155` → playing handler) |
| BACK | exit (`src/PlayingInputHandler.cpp:138-147`) | zoomOut only (`src/ZoomController.cpp:71-74`) |
| MENU, ITEMS, ITEMS_DRINKS, QUESTLOG | exit instead of opening menus (`:149-185`) | MENU: zoomOut |
| AUTOMAP | ignored (`:86-87`) | ignored; outside target practice it opens MENU_INGAME (`src/ZoomController.cpp:75-81`) |
| UP/DOWN/STRAFE | `attemptMove`, blocked by EVT 165 ABORT_MOVE | pitch |
| LEFT/RIGHT | 90° turn (`:112-122`) | yaw nudge |
| PREV/NEXT WEAPON | only weapon 9 owned | zoom FOV ±64 % with sound 1113 |

- Touch: HUD button 0 (menu) does `handleZoomEvents(BACK)` if zoomed, then
  `handlePlayingEvents(MENU)`, which exits (`src/Hud.cpp:1305-1310`).
- PC key-repeat path: `Input.cpp` skips the just-pressed/released keymap scan in
  `ST_PLAYING` while in target practice (`src/Input.cpp:988`). It falls through to the
  plain pressed scan (`:1053+`).

## 5. Sounds

- No target-practice-specific `playSound` call exists in `Player.cpp` or
  `MiniGameManager.cpp`.
- Sounds heard during it:
  - terminal `PLAYSOUND 1065` (script IP 8278)
  - zoom dial 1113 (`src/ZoomController.cpp:129`, `:147`)
  - generic weapon-fire sounds from the combat sequence

## 6. Rewrite status

`new_src/domain/game/ScriptVM.cpp:789-813` implements only the teleport subset. Not yet
implemented: flag, save/restore, strip, help, fade, scoring, exit and thread resume.
