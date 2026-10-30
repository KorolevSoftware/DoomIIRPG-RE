# 2026-10-30 — Target practice (shooting range), original behavior

## Hypothesis
On map00 the player pays 2 credits (GIVEITEM def=107 qty=-2) and is moved to tile (16,29)
facing dir 4. The player shoots targets with a scoped weapon, and after N hits an accuracy
stat improves. Goal: find the entry, rules, drawing, input and sounds.

## Method
- Searched `src/` for `inTargetPractice|targetPractice|TARGETPRACTICE`. The hits are in
  Player.cpp, PlayingInputHandler.cpp, ZoomController.cpp, SoftKeyController.cpp,
  Hud.cpp, Canvas.cpp, MiniGameManager.cpp, Render.cpp, ScriptThread.cpp, Input.cpp
  and Enums.h.
- Ran `python3 tools/disasm_map_scripts.py tmp_map00.bin -o <scratch>/map00.txt --verify`
  (0 failures). Read the code around IP 8271-8389.
- Read strings from the ipa `Packages/strings.idx` + `strings0N.bin`, using the format
  from `src/Resource.cpp:169-207` and `src/Text.cpp:179-212`. Common table 0: ids 17, 30,
  68, 115, 229-235. Map00 table 4: ids 91, 92.

## Verdict
PARTIAL. The hypothesis is mostly right, but the mechanics differ:
- The game does not count a number of hits. It scores 8 shots at one static target:
  head +30, body +20, leg +10.
- The reward is +1 `STAT_ACCURACY` (stat 5) when score > 120 (max 240), capped at 99.

## Evidence (key quotes)
- `src/ScriptThread.cpp:1806-1813`: `enterTargetPractice(uShortArg20 >> 8 & 0x1F, uShortArg20 >> 3 & 0x1F, uShortArg20 & 0x7, this); this->unpauseTime = -1;`
- `src/Player.cpp:2521-2551`: enterTargetPractice saves canvas->save*, snaps to `(n<<6)+32`,
  sets Z = getHeight+36, then calls `showHelp(17,false)`, `stripInventoryForTargetPractice()`,
  `targetPracticeScore = 0` and `startFade(750,2)`.
- `src/Player.cpp:2694-2705`: strip → `give(1, 9, 1, true); give(2, 1, 8, true);`
  (rifle + 8 bullets).
- `src/Player.cpp:2553-2598`: zones from `getImageFrameBounds(tileIndex,3,2,0)`;
  `targetPracticeScore += 30/20/10`; messages 230/231/232, miss 68.
- `src/Player.cpp:2611-2627`: `int n = 240; ... if (score > n/2) modifyStat(5,1) → 233 / 235; else 234`.
- `src/Player.cpp:2629-2633`: `setState(ST_PLAYING); targetPracticeThread->run();`
- `src/PlayingInputHandler.cpp:524-531`, `:537-543`: `if (ammo[1] == 0) exitTargetPractice(); else assessTargetPracticeShot(entity)`.
- Ammo is deducted asynchronously in the combat stage (`src/Combat.cpp:355-357`).
  So all 8 shots are scored, and the 9th FIRE exits (fireWeapon prints 115 "Out of
  Ammo!", `src/Player.cpp:782-795`).
- `src/Render.cpp:1672-1713`: tile 149 drawn in layers: frames 5/6/7 idle, 0/2/3 for a
  500 ms flash, board frame 1 (zoomed) / 4.
- `src/MiniGameManager.cpp:222-233`: "Score:" + score bar via drawImportantMessage
  0xFF7F0000, suppressed when msg[0] has flag 4.
- `src/SoftKeyController.cpp:81-83`: left softkey 30 "Exit".
- `src/PlayingInputHandler.cpp:86-87,138-185`: AUTOMAP is ignored; BACK/MENU/ITEMS/
  DRINKS/QUESTLOG exit.
- `src/ZoomController.cpp:71-81`: zoomed BACK/MENU only zoom out; AUTOMAP is ignored in
  target practice.
- Script: event 134 at (19,26) dir S → DIALOG 91 yes/no → `1 < v67` (credits) → GIVEITEM
  def=107 qty=-2 → START_TARGETPRACTICE (16,29,4). Event 165 at (16,29) is EXIT →
  ABORT_MOVE.
- Target sprite: tile 149 at (14,29) (`docs/research/2026-08-30-sprite-blocking.md:196`).
  It is picked by the fire trace only via the `eType == 7 && tile == 0x95` branch
  (`src/PlayingInputHandler.cpp:346-352`).

## Strings
| Table | Id | Text |
|---|---|---|
| 0 | 17 | VIRTUAL TARGET PRACTICE help (… Session ends after 8 shots. Tap screen after 8 shots to exit …) |
| 0 | 30 | Exit |
| 0 | 68 | Missed! |
| 0 | 115 | Out of Ammo! |
| 0 | 229 | Score: |
| 0 | 230/231/232 | Head-shot! / Body-shot! / Leg-shot! |
| 0 | 233 | Practice Score: %01\|+%02 to Accuracy |
| 0 | 234 | Practice Score: %01\|No Accuracy Bonus |
| 0 | 235 | Practice Score: %01\|No Accuracy Bonus\|Accuracy at Max |
| 4 (map00) | 91 | Virtual Target Practice: Would you like to play? Requires 2 UAC Credits. |
| 4 (map00) | 92 | You do not have enough UAC Credits. |

## Open questions
- The meaning of map00 vars v16 / v56 / v66 in the terminal script (outside target
  practice proper).
- The `mediaBounds` values of tile 149 frames 0/2/3. They were not dumped, so the zone
  box sizes in world units are unknown.
- Whether `fireWeapon` against a DECOR target runs the full combat sequence (damage
  messages from Combat, e.g. its own 68 at `src/Combat.cpp:341-343`). Not traced.
