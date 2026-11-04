# Front end: boot → logo → intro movie → main menu → new game → character select → prologue → map

Verified against the legacy RE port in `src/`. Every claim cites `src/File.cpp:line`.
Menu rows were decoded from the shipped `menus.bin` / `strings*.bin` in
`Doom 2 RPG.ipa` (format: `docs/architecture/adr/0013-menu-tree-from-menus-bin.md`).
Labels keep their soft hyphens (`-`), which `dehyphenate()` strips at draw time.
Related: `game-flow.md` §3 (map lifecycle), `save-load.md` §6-8 (save detection,
load flow, in-game exit menus), `ui.md` §11-16 (in-game menu engine),
`travel-map.md`, `map-change.md`.

Grep caveat: `src/MenuSystem.cpp` is ISO-8859 + CRLF, so plain `grep` treats it as
binary and prints nothing. Use `grep -a`.

---------------------------------------------------------------------------------
## 1. State chain at a glance

```
App::startup → setState(ST_LOGO 26)                          src/App.cpp:128
ST_LOGO: l2.bmp, 122 frames                                  src/GameStateRunner.cpp:324-348
  → ST_INTRO_MOVIE 2 (prolog.bmp table-camera pan + scrolling text 0:133)
  → exitIntroMovie / any key → backToMain(false)             src/IntroSequenceManager.cpp:861-878, src/Canvas.cpp:833-841
ST_MENU 1, MENU_MAIN 3                                       src/Canvas.cpp:1402-1405
  New Game → [MENU_MAIN_CONFIRMNEW 14 if hasSavedState] → MENU_MAIN_DIFFICULTY 16
  → ACTION_DIFFICULTY: game->difficulty = 1|2|4; startGame(true)   src/MenuSystem.cpp:3095-3099
  → ST_CHARACTER_SELECTION 22                                src/MenuSystem.cpp:3603
  → Yes: setCharacterChoice; player->reset(); ST_INTRO 9     src/IntroSequenceManager.cpp:625-630
  → last prologue page passed → disposeIntro → loadMap(1, false, tm_NewGame=true)
                                                             src/Canvas.cpp:848-851, src/IntroSequenceManager.cpp:131-136
  → ST_TRAVELMAP → ST_LOADING → ST_PLAYING                   game-flow.md §3.2-3.3
  Continue → startGame(false) → loadState(getRecentLoadType(),3,194)  src/MenuSystem.cpp:3180-3182,3605-3608
```

Canvas state ids: `src/Canvas.h:68-93` (`ST_MENU 1`, `ST_INTRO_MOVIE 2`, `ST_INTRO 9`,
`ST_CREDITS 15`, `ST_SAVING 16`, `ST_CHARACTER_SELECTION 22`, `ST_LOGO 26`).

---------------------------------------------------------------------------------
## 2. Boot and `ST_LOGO` (26)

* `Applet::startup` ends with `game->loadConfig()`, `canvas->clearEvents(1)`,
  `canvas->setState(ST_LOGO)` (`src/App.cpp:119-128`).
* Port-only: right after startup `CAppContainer::Construct` forces
  `game->hasSeenIntro = true` (`src/CAppContainer.cpp:186-188`). That overrides the config
  value, so in the port the intro movie can always be skipped (§3.3).
* `GameStateRunner::logoState` (`src/GameStateRunner.cpp:324-348`):
  * caches sounds the first time (`:328-330`);
  * while `pacLogoTime <= 120` it increments the counter, loads `l2.bmp` once and sets
    `REPAINT_STARTUP_LOGO` (`:332-337`). `pacLogoTime` starts at -1 (`src/Canvas.cpp:60`),
    so the logo shows for **122 frames**. The timing counts frames, not milliseconds.
  * after that it frees the image, calls `setState(ST_INTRO_MOVIE)`, zeroes
    `numEvents`/`keyDown`/`keyDownCausedMove` and sets `ignoreFrameInput = 1`
    (`:339-347`).
* Paint: `fillRect(0,0,displayW,displayH, 0xFF000000)` then `l2.bmp` (136×154) drawn
  centred at (240,160) with anchor 3 (`src/Canvas.cpp:487-491`).
* Input: only raw key 18 does anything. In `ST_LOGO` it calls `app->shutdown()`
  (`src/InputEventController.cpp:167-172`).

---------------------------------------------------------------------------------
## 3. `ST_INTRO_MOVIE` (2)

### 3.1 Entry
`setState(ST_INTRO_MOVIE)` calls `initScrollingText(0, 133, false, 32, 1, 800)` and sets
`stateVars[0] = 1` (`src/Canvas.cpp:1179-1182`). Text 0:133 is the "Mysterious occurrences
involving UAC facilities…" opening crawl.

### 3.2 `IntroSequenceManager::playIntroMovie` (`src/IntroSequenceManager.cpp:752-859`)
It is called every paint (`src/Canvas.cpp:423-425`). The sub-phase is held in `stateVars[3]`:

| phase | behaviour | lines |
|---|---|---|
| guard | `skipIntro` → `backToMain(true)`; `hasSeenIntro && skipMovie` → `exitIntroMovie(false)` | `:756-764` |
| first frame | `stateVars[1] = gameTime`; `game->loadTableCamera(14, 15)` (table 14 = shorts, 15 = bytes; `src/Game.cpp:511-535`); clears input, `ignoreFrameInput = 1`; loads `prolog.bmp` (720×372) | `:766-774` |
| 0 | `playSound(1068, 1, 6)` (index 68 = `Music_General.wav`, `src/Sounds.h`). Once `stateVars[1] < gameTime`: `activeCameraKey = -1`, black **fade-in of 1500 ms** over `displayRect` (`fadeFlags = FADE_FLAG_FADEIN`, `fadeColor 0`), then go to 1 | `:779-793` |
| 1 | clips to a 220-px band `y = (displayH-220)/2`; updates the Maya camera with `gameTime - activeCameraTime`; camera `x,y >> posShift` picks the `prolog.bmp` source window (`texX = camX - SCR_CX`, `texY = camY - SCR_CY`, both clamped at 0; dest y = `max(0, SCR_CY - camY)`); `drawRegion`. Goes to 2 when `mayaCameras->complete` | `:795-835` |
| 2 | `initScrollingText(0,133,false,32,1,800)` again and draws the first frame | `:837-841` |
| 3 | `drawScrollingText`; once `scrollingTextDone` → `exitIntroMovie(false)` | `:842-847` |

`drawScrollingText` (`src/IntroSequenceManager.cpp:207-245`) for this state: erases the
screen; clips to the 220-px band; draws the `prolog.bmp` region (0,65,480,307) at (0,0);
`graphics->fade(rect, 192, 0)`; sets screen space to the band. The text is drawn centred at
`SCR_CX`, starting at `y = cinRect[3] - elapsed/n * spacing`.
With spacing 32 and msPerLine 800: `n = 800 * ((32<<16)>>4) >> 16 = 1600` ms per 32 px,
so the text scrolls at **20 px/s**. The end time is
`scrollingTextEnd = n * (lines + screenH/32 - 2)` (`:211-219`). Line count:
`initScrollingText` wraps at `(displayW-8)/CHAR_SPACING` and adds `numLines` (1)
(`src/IntroSequenceManager.cpp:173-193`).

### 3.3 Exit
* `Canvas::run`: `(hasSeenIntro && numEvents != 0) || scrollingTextDone` → dispose
  `dialogBuffer`, `hasSeenIntro = true`, `saveConfig()`, `backToMain(false)`
  (`src/Canvas.cpp:833-841`). This means **any key** skips once the intro has been seen,
  which in the port is always (§2).
* `handleEvent` in `ST_INTRO_MOVIE`: `skipMovie = (keyAction == ACTION_FIRE)`
  (`src/InputEventController.cpp:323-325`). Key 18 calls `exitIntroMovie(true)` and then
  `shutdown()` (`:178-181`).
* `exitIntroMovie(b)` (`src/IntroSequenceManager.cpp:861-878`): `cleanUpCamMemory()`,
  frees `prolog.bmp` and `dialogBuffer`, `soundStop()`. If `!b`: `hasSeenIntro = true`,
  `saveConfig()`, `backToMain(false)`.

### 3.4 Intro movie: exact data and math (research R1-R3, 2026-11-03)

Raw log: `docs/research/2026-11-03-intro-movie.md`. Screen constants for the 480×320
build: `displayRect = {0,0,480,320}` (`src/Canvas.cpp:49-52`), `screenRect[3] = 320`,
`SCR_CX = 240`, `SCR_CY = 160` (`src/Canvas.cpp:98-123`), `cinRect[3] = viewRect[3] = 250`
(`src/Canvas.cpp:127,151-154`). Band = `(0, (320-220)/2 = 50, 480, 220)`, screen rows 50..269.

#### R1. Tables 14/15 (the table camera)
* Source: plain `tables.bin` (`Resources::RES_TABLES_BIN_GZ = "tables.bin"`,
  `src/Resource.h:14`). Header = 20 little-endian int32 end offsets (`src/Resource.cpp:217-220`,
  `shiftInt` LE `:148-152`); table `i` starts at `80 + (i ? off[i-1] : 0)` (`seekTable`,
  `:236-248`; 80-byte skip `:233`) with an int32 element count, then the elements
  (`loadShortTable :295-311`, `loadByteTable :278-293`). The rewrite's `io/Tables` already
  parses this container but skips tables 14/15 (`new_src/io/Tables.cpp:31-50`).
* Table 14 = 75 int16, table 15 = 252 int8 (shipped data). Decoded by
  `Game::loadTableCamera` (`src/Game.cpp:511-574`):

| shorts | field | value |
|---|---|---|
| `[0]` | `totalMayaCameraKeys` (`:531`) | 5 |
| `[1]` | `sampleRate` (`:532`) | 125 |
| `[2]` | `posShift = 4 - v` (`:533`) | v=0 → **posShift 4** |
| `[3]` | `angleShift = 10 - v` (`:534`) | v=10 → 0 (unused here) |
| `[4..38]` | keys, 5×7 channel-major (`:536-539`, offsets `setKeyOffsets :576-584`) | X `303,450,460,296,137`; Y `146,137,230,221,225`; Z 0×5; pitch 768×5; yaw 768×5; roll 0×5; MS `4133,4167,3333,4300,0` |
| `[39..68]` | tween indices, 5×6 (`:541-544`) | key0 `0,0,-1,-1,-1,-1`; key1 `33,33,-1…`; key2 `66,66,-1…`; key3 `92,92,-1…`; key4 all `-1` |
| `[69..73]` | per-channel tween counts → `ofsMayaTween = {0,126,252,252,252,252}` (`:546-556`) | `126,126,0,0,0` |
| `[74]` | 6th (roll) count, not read by the loader | 0 |

  Table 15 = X tween bytes `[0..125]` then Y tween bytes `[126..251]`, copied verbatim
  into `mayaCameraTweens` (`:558-559`). Indices are channel-local (fetch
  `tweens[ofsMayaTween[ch] + idx + step]`, `src/MayaCamera.cpp:181-195`, `:290-300`).
  `estNumTweens` per key = `(ms-1)/125` = 33, 33, 26, 34 (sum 126 = channel count;
  last key 0) (`src/MayaCamera.cpp:173-179`).
* The camera: one `MayaCamera`, `isTableCam = true`, `keyOffset 0`, `numKeys 5`,
  `complete = false`, `keyThreadResumeCount = 5`, no `keyThread` (`src/Game.cpp:525-562`).
* Units: `Update` produces `x = key << 4` (`src/MayaCamera.cpp:94-96`, `:238-239`), and
  `playIntroMovie` shifts back by `posShift = 4` (`src/IntroSequenceManager.cpp:806-810`),
  so **camera x/y = `prolog.bmp` pixel coordinates of the screen centre**.
* Playback (key advance and end):
  - Phase 0 sets `activeCameraKey = -1` but not `activeCameraTime`
    (`src/IntroSequenceManager.cpp:782`). The first phase-1 `Update(-1, …)` reads
    `ms` at `OFS_MAYAKEY_MS - 1` = roll of key 4 = 0, so `i2 >= 0` holds and it calls
    `Snap(-1)` (`src/MayaCamera.cpp:72-75`): pose = key 0 static (`:336-343`), then, as there
    is no `keyThread` and `isTableCam`, `NextKey()` (`:371-373`) → key 0,
    `activeCameraTime = gameTime` (`:36-44`). So frame 1 of phase 1 shows key 0 (303,146).
  - On the frame where `elapsed >= ms[k]`, `Snap(k)` sets the pose to key `k+1` static and
    calls `NextKey` (clock restarts at that frame's `gameTime`; the overshoot is dropped).
  - Key 4 has `ms = 0`: the next frame's `Update(4, …)` → `Snap(4)` → `n2+1 == numKeys` →
    `complete = true` (`:336`, `:376`); the `ST_CAMERA` epilogue is skipped (`:380-382`).
    That frame still draws (key-4 pose), then `stateVars[3]` → 2
    (`src/IntroSequenceManager.cpp:836-837`). Pan length ≈ 4133+4167+3333+4300 = 15933 ms
    plus one frame of overshoot per boundary.
* Curves (simulated with the legacy algorithm over the shipped bytes; camera px):
  key0 (303,146)→(450,137); key1 →(460,230) peaking near x 475; key2 →(296,221);
  key3 →(137,225). Tween sums equal the key deltas except key1 X (sum 11 vs Δ10, the
  final partial window absorbs it, `src/MayaCamera.cpp:123-126`, `:197-232`).
* **Rewrite compatibility:** `new_src/core/MayaCamera` consumes this data as-is if it is
  wrapped in a synthetic `MapData` with `totalMayaCameraKeys = 5` and one
  `MapData::MayaCamera{numKeys 5, sampleRate 125, keys = shorts[4..38],
  tweenIndices = shorts[39..68], tweens = table15 (252 bytes), tweenCounts {126,126,0,0,0,0}}`
  (`new_src/domain/world/MapData.h:37,107-114`). Its addressing `chanOfs[ch] + idx + step`
  with prefix-summed `tweenCounts` equals `ofsMayaTween` here (single camera, zero rebase;
  `new_src/core/MayaCamera.cpp:21-31`, `:232-251`) and `estNumTweens` keys off
  `totalMayaCameraKeys` (`:224-230`). `update(k, e)` returns render units (`<<4`); divide by
  16 for bmp px. `CinematicCamera` is NOT reusable (script/thread-driven); the intro session
  must advance keys itself with the table-cam rule above.

#### R2. Phase 1 pan over `prolog.bmp` (720×372, 8-bit BMP)
Per frame (`src/IntroSequenceManager.cpp:795-835`):
```
clip (0, 50, 480, 220)                      // only if displayRect[3] > 220 (:796-798)
stateVars[0] = gameTime - activeCameraTime;   Update(activeCameraKey, stateVars[0])
camX = maya.x >> 4;  camY = maya.y >> 4
texX = max(0, camX - 240);  texY = max(0, camY - 160);  dstY = max(0, 160 - camY)
texW = 480 (clamped to 720 - texX); texH = 320 (clamped to 372 - texY)
drawRegion(prolog, texX, texY, texW, texH, 0, dstY, anchor 0)   // dest x always 0
```
* Equivalent for the rewrite: blit the whole `prolog.bmp` at screen
  `(-texX, 160 - camY)` clipped to the band. Y is symmetric (centre row maps to screen
  y 160 in both branches; source rows `camY-110 .. camY+109` are visible); X is NOT: for
  `camX < 240` the image stops at `texX = 0` (no positive dest x). With the shipped path
  this happens only in key 3 (296→137), so the image stops panning horizontally there.
  The texW/texH clamps never bite with this data (max camX ≈ 475 → texX 235, 235+480 ≤ 720).
* The final pose (137,225) gives `texX 0`, top band row = source row 115 — exactly the
  framing of the text-phase background (`drawRegion(prolog, 0,65,…)` at y 0 → band top =
  row 115), so the hand-off is seamless except for the 25 % darkening that appears.
* Fade-in: set once on the phase 0→1 transition (`:787-791`): `fadeRect = displayRect`,
  `FADE_FLAG_FADEIN`, black, `fadeTime = app->time` (wall-clock, not `gameTime`),
  1500 ms. Drawn after the state paint (`src/Canvas.cpp:499-509`):
  `alpha = ((time - fadeTime) << 8) / 1500` → `graphics->fade(rect, alpha, 0)`, which
  fills black with GL alpha `1 - alpha/255` (`src/Graphics.cpp:755-767`) over the full
  screen, i.e. black→clear in 1.5 s; flags cleared when it expires.
* Phase 0: `playSound(1068, flags 1, priority 6)` each phase-0 frame
  (`src/IntroSequenceManager.cpp:780`); flags bit 0 = loop (`src/Sound.cpp:439`), a repeat
  with the same id is ignored while it plays (`:350-351`, `:369-373`). It leaves phase 0 on the first
  frame with `stateVars[1] < gameTime` (normally the next frame). The music runs until
  `exitIntroMovie` → `soundStop()` (`:872`) or `backToMain` → 1071.

#### R3. Scrolling text 0:133
* Text: "Mysterious oc-cur-ren-ces in-volv-ing UAC fa-ci-li-ties …  has been sent to
  in-ves-ti-gate\x85" (soft hyphens; `dehyphenate = false`, so `wrapText` breaks at them
  and dehyphenates, `src/Text.cpp:756-827`).
* Wrap: `wrapText((480 - 8) / CHAR_SPACING[fontType])` (`src/IntroSequenceManager.cpp:180`)
  = **42 chars** (`CHAR_SPACING[0] = 11`, `src/App.h:41`; `fontType = 0`, `src/App.cpp:42`,
  `setFont` has no callers). Note: 11 is not the 9-px glyph advance — wrap at 42, do not
  derive it from the rewrite font.
* Result (computed with the rewrite's port of `Text::wrapText`): **9 lines** —
  `Mysterious occurrences involving UAC faci-` (42 ch, 378 px) /
  `lities have everyone on high alert. Not` / `much is known about the classified inci-` /
  `dent on Mars but containment is becoming` / `more difficult as a series of distress` /
  `signals have been sent from a UAC outpost` / `on Earth's Moon.  A contingent of Space` /
  `Marines, along with a rookie scientist,` / `has been sent to investigate…` (29 ch).
* Parameters (`initScrollingText(0,133,false,32,1,800)`, `:173-193`): spacing 32,
  `scrollingTextLines = 9 + 1 = 10`, msPerLine 800.
* Timing (`drawScrollingText`, `:207-245`): `n = 800 * ((32<<16)>>4) >> 16 = 1600`;
  start stamped on the first draw (phase 2 frame); `scrollingTextEnd =
  n * (lines + (320/32 - 2)) = 1600 * 18 = 28800 ms`. Done when
  `gameTime - start > 28800` (strict); then `start` is clamped so the frame holds the end
  position, and phase 3 exits the same frame (`:842-847`; `Canvas::run` also checks it,
  `src/Canvas.cpp:833-841`).
* Offset: `off(t) = (((t << 8) / 1600) * (32 << 8)) >> 16` = `floor(floor(256t/1600)/8)`
  ≈ t/50 → 20 px/s; `off(28800) = 576`.
* Placement: background first — `eraseRgn(displayRect)`; clip band; `drawRegion(prolog,
  0, 65, 480, 307)` at (0,0) → band shows source rows 115..334; `graphics->fade(displayRect,
  192, 0)` = black at alpha 63/255 (≈25 % darker) over the whole screen (fade resets the
  clip to full, `src/Graphics.cpp:761-764`); then `setScreenSpace(0, 50, 480, 220)`
  (translate + clip, `src/Graphics.cpp:741-748`). Text: `drawString(buf, 240, 250 - off,
  h 32, flags 1 HCENTER)` in band space (`:243`), i.e. **screen y of line k =
  50 + 250 - off + 32k**, glyph top-left anchored (`drawChar` → `drawRegion` anchor 0,
  12×16 cell, `src/Graphics.cpp:637-663`), white (char colour 0).
  Line 0 enters the band (local y < 220) at off > 30 (t ≈ 1.5 s); line 8 leaves it
  (y+16 ≤ 0) at off 522 (t ≈ 26.1 s); at the end line 8 sits at local y -70, so the
  last ~2.7 s show only the darkened image. `resetScreenSpace()` restores displayRect (`:244`).
* Horizontal: first line `x = 240 - (w/2 + (w & 1))`, later lines `x = 240 - w/2` of the
  next line's width, `w = 9 px × chars` (spaces 9 px too) (`src/Graphics.cpp:518-525`,
  `:551-563`, `:575-582`; `Text::getStringWidth`, `src/Text.cpp:857-894`). Line 0 → x 51.
* **Rewrite mapping:** the rewrite font is the same sheet metrics (`kGlyphW 12`,
  `kGlyphH 16`, `kAdvance 9`, `new_src/text/Font.h:19-21`) and `Graphics2D::drawString`
  mirrors the legacy centring incl. the `(w & 1)` term (`new_src/render/Graphics2D.cpp:145-161`).
  So: `text.wrapText(42)`; `ui.label(text, 240, 300 - off, HCENTER, 32)` inside a clip
  `(0, 50, 480, 220)` — a 1:1 port, no metric conversion.

---------------------------------------------------------------------------------
## 4. `Canvas::backToMain(bool b)` (`src/Canvas.cpp:1372-1407`)

1. `loadMapID = 0`; `freeRuntimeImages()`; `player->reset()`; `game->unloadMapData()`;
   `render->unloadMap()`; `render->endFade()` (`:1375-1380`).
2. Reloads `menuSystem->imgMainBG = logo.bmp` (480×320) (`:1382-1383`). This is the only
   place that loads the main-menu background for the first time.
3. `b == true`: `clearEvents(1)`. If `skipIntro`, it reloads `startupMap` directly. Else it
   opens `MENU_SELECT_LANGUAGE` if `localization->selectLanguage`, otherwise `MENU_MAIN`
   (`:1385-1401`). `skipIntro` is always false (`src/Canvas.cpp:163`) and `selectLanguage`
   is never set to true anywhere in `src/` (grep). In practice this branch is dead.
4. `b == false` (every real caller): `playSound(1071, 1, 3, false)` (index 71 =
   `Music_Title.wav`), then `setMenu(MENU_MAIN)` (`:1403-1406`).

Callers: end of intro movie (§3.3); character-select Back / MENU (§8.4);
`ACTION_BACKTOMAIN` 5 (`src/MenuSystem.cpp:3051-3053`); `ACTION_MAIN_SPECIAL` 23
(`:3184-3186`); `ST_SAVING` with `saveType & 4` (`src/Canvas.cpp:879-880`, i.e.
`ACTION_SAVEEXIT` → `saveState(7)`).

`setMenu(MENU_MAIN)` clears the menu stack (`src/MenuSystem.cpp:619-621`) and enters
`ST_MENU` if needed (`:645-649`). `setState(ST_MENU)` stamps `menuSystem->startTime` and
calls `clearEvents(1)` when coming from a non-menu state (`src/Canvas.cpp:1192-1206`).

---------------------------------------------------------------------------------
## 5. Main-menu screens (ids < `MENU_INGAME` 29)

Ids: `src/Menus.h:216-246` (identical to the `#if 0` table at `:44-74`).

### 5.1 `menus.bin` rows (decoded from the shipped file)

**`MENU_MAIN` 3**, type 4, 8 rows. All rows have flags 0x8 (`ITEM_ALIGN_CENTER`) and help 0.

| # | label (3:id) | action | param |
|---|---|---|---|
| 0 | 71 "New Game" | 7 NEWGAME | 0 |
| 1 | 72 "Op-tions" | 1 GOTO | 17 `MENU_MAIN_OPTIONS` |
| 2 | 77 "Other Games" | 1 GOTO | 19 `MENU_MAIN_MORE_GAMES` |
| 3 | 391 "Comic Book" | 1 GOTO | 89 `MENU_COMIC_BOOK` |
| 4 | 73 "Mini Games" | 1 GOTO | 18 `MENU_MAIN_MINIGAME` |
| 5 | 74 "Game Help" | 1 GOTO | 4 `MENU_MAIN_HELP` |
| 6 | 75 "About" | 1 GOTO | 8 `MENU_MAIN_ABOUT` |
| 7 | 76 "Exit" | 1 GOTO | 13 `MENU_MAIN_EXIT` |

`initMenu` case `MENU_MAIN` (`src/MenuSystem.cpp:1256-1272`):
* if `game->hasSavedState()`, it first adds `CONTINUE_ITEM` (3:70 "Con-tin-ue"), flags 8,
  action 22 `ACTION_CONTINUE` (`:1258-1261`);
* `background = imgMainBG`, `drawLogo = true`;
* `loadMenuItems(3, 0, 2)` + `loadMenuItems(3, 3, -1)`, which skips row 2 "Other Games"
  (`:1266-1270`, a `[GEC]` change; the original call `loadMenuItems(menu,0,-1)` is
  commented out at `:1264`). `loadMenuItems(menu, beg, count)` semantics:
  `src/MenuSystem.cpp:4005-4035`.

Final list: `[Continue]`, New Game, Options, Comic Book, Mini Games, Game Help, About, Exit.
`selectedIndex` resets to 0 when coming from another menu (`:1226-1229`), so the cursor
starts on Continue if a save exists, otherwise on New Game.

**`MENU_MAIN_DIFFICULTY` 16**, type 6, 5 rows: #0 3:206 "Choose Game Mode:" (flags 9);
#1 blank (9); #2 3:207 "Nor-mal" → `ACTION_DIFFICULTY` 10, param **1**; #3 3:208
"Dif-fi-cult" → 10, param **2**; #4 3:209 "Night-mare" → 10, param **4**.
`initMenu` (`:1274-1281`) sets `selectedIndex = 1`, background + logo, and calls
`loadMenuItems(16, 1, -1)`. The title row #0 is **not shown**. The list is
[blank, Normal, Difficult, Nightmare] and the cursor starts on **Normal**.
(3:200-202 are *not* difficulty names. They are the end-ranking headers "RANKING" /
"DIFFICULT RANKING" / "NIGHTMARE RANKING", `src/MenuSystem.cpp:3710-3718`.)

**`MENU_MAIN_HELP` 4**, type 6, 12 rows: #0 Back (2); #1 "How to Play" → 9; #2 "How to Move"
→ 10; #3 "How to Attack" → 11; #4 "Sniper Rifle" → 12; #5 "Armor Help" → 5; #6 "Item Help"
→ 7; #7 "Effects List" → 6; #8 "Hacker" → 20; #9 "Matrix Skip" → 21; #10 "Power-Up" → 22;
#11 3:103 "Cre-dits" → `ACTION_CHANGESTATE` 9, param 15 (`ST_CREDITS`). `initMenu` calls
`loadMenuItems(4, 1, -1)`, so Back is dropped (`:1282-1287`). The leaf screens are type 5
`LoadHelpResource(n)` with `drawLogo = false`: n = 1 General, 2 Move, 3 Attack, 7 Sniper,
4 Armor, 5 Effects, 6 Items, 11 About (`:1288-1344`), and 8/9/10 for Hacker / Matrix Skip /
Power-Up (`:1466-1486`).

**`MENU_MAIN_MINIGAME` 18**, type 8: Back (dropped by `loadMenuItems(18,1,-1)`), blank,
"Hacker" → `ACTION_MINIGAME` 27 p2, "Matrix Skip" → 27 p0, "Power-Up" → 27 p4.
`selectedIndex = 1` lands on the blank row, and the init tail's `moveDir(1)` moves it to
Hacker (`:1274-1281`, `:2733-2739`). `ACTION_MINIGAME` calls `playFromMainMenu()`
(`:3208-3216`). When the minigame ends it returns via `setMenu(MENU_MAIN_MINIGAME)`
(`src/HackingGame.cpp:634`, `src/SentryBotGame.cpp:822`, `src/VendingMachine.cpp:862`).

**`MENU_MAIN_MORE_GAMES` 19**: unreachable, because its row is removed from `MENU_MAIN`.
**`MENU_MAIN_OPTIONS` 17**: the shipped row has **0 items**. The port fills it with `[GEC]`
Sound/Video/Input sub-menus (`src/MenuSystem.cpp:1365-1458`), and none of those is original.

**Code-built confirms** (`SetYESNO(textId, defSel, yesAction, yesParam)`; No =
`ACTION_BACK`; `defSel 0` preselects No; the text is split on `\n` into flag-9 rows, then a
blank row, then Yes (3:197) and No (3:198); `src/MenuSystem.cpp:3611-3660`):

| menu | type | call | text |
|---|---|---|---|
| `MENU_MAIN_EXIT` 13 | 4 | `SetYESNO(106, 0, 8 ACTION_EXIT, 0)` (`:1345-1350`) | 3:106 "Exit Game?" → `app->shutdown()` (`:3082-3084`) |
| `MENU_MAIN_CONFIRMNEW` 14 | 6 | `SetYESNO(107, 0, 1 GOTO, 16)` (`:1352-1357`) | 3:107 "This will erase\nyour saved game.\n\nAre you sure?" |
| `MENU_MAIN_CONFIRMNEW2` 15 | 6 | `SetYESNO(108, 0, 7 NEWGAME, 0)` (`:1359-1363`) | 3:108 "Are you sure?" (no code path opens it, grep) |

### 5.2 Actions used by the front end (`MenuSystem::select`, `src/MenuSystem.cpp:3020-3216`)

| action | effect |
|---|---|
| 7 `ACTION_NEWGAME` | `m_controlAlpha = 0x32` (port); `hasSavedState()` ? `gotoMenu(14)` : `gotoMenu(16)` (`:3072-3080`) |
| 10 `ACTION_DIFFICULTY` | `game->difficulty = (uint8_t)param`; `clearSoftKeys()`; `startGame(true)` (`:3095-3099`) |
| 22 `ACTION_CONTINUE` | `startGame(false)` (`:3180-3182`) |
| 8 `ACTION_EXIT` | `app->shutdown()` (`:3082-3084`) |
| 9 `ACTION_CHANGESTATE` 15 | Credits; plays 1071 only when `loadMapID != 0` (in-game) (`:3086-3093`) |
| 6 `ACTION_TOGSOUND` | on `MENU_ENABLE_SOUNDS` 25: `allowSounds = true`, `playSound(1068)`, `setState(ST_INTRO_MOVIE)` (`:3055-3061`). No code opens menu 25 (grep), so this is dead |

`select()` plays `1086` (`pistol.wav`, `[GEC]`) for every non-help selection (`:3013`).
`back()` plays `1122` (`switch_exit.wav`) except on `MENU_MAIN` / `MORE_GAMES` (`:577-581`).
With an empty stack, `back()` on `MENU_MAIN_MINIGAME` / `MENU_COMIC_BOOK` returns to
`MENU_MAIN` (`:608-610`).

### 5.3 `startGame(bool newGame)` (`src/MenuSystem.cpp:3571-3609`)
Always: frees `background` (if it is not `imgMainBG`) and `imgMainBG`, then `soundStop()`.
* `true`: `setLoadingBarText(-1,-1)`; `game->removeState(true)` (wipes config + all save
  files, `save-load.md` §6); `activeLoadType = loadType = loadMapID = lastMapID = 0`;
  `player->reset()`; `totalDeaths = currentLevelDeaths = 0`; help bitmasks
  (`help/invHelp/ammoHelp/weaponHelp/armorHelp`) = 0; `currentGrades = 0`;
  `ce->weapon = -1`; `clearEvents(1)`; `setState(ST_CHARACTER_SELECTION)` (`:3584-3604`).
* `false`: `canvas->loadState(getRecentLoadType(), 3, 194)` (`:3605-3608`), see
  `save-load.md` §7.

### 5.4 Main-menu input (`handleMenuEvents`, `src/MenuSystem.cpp:2826-3005`)
UP/DOWN → `scrollUp/Down`; LEFT/RIGHT → page up/down; FIRE → `select(selectedIndex)`
(`:2859-2891`). `ACTION_BACK` on `MENU_MAIN` → `gotoMenu(MENU_MAIN_EXIT)` (`:2939-2942`).
`ACTION_MENU` → `back()` (`:2921-2934`); on the root it does nothing, because
`MENU_MAIN` is not in `back()`'s return-to-game list (`:602-610`).
Soft keys (`GameStateRunner::menuState`, `src/GameStateRunner.cpp:203-252`): left = 3:80
"Back" when the stack is non-empty or the menu is `MENU_MAIN_MINIGAME`, so the root has
none. Right = 0:121 when the selected row has an action. For menus `< MENU_INGAME` the
Back soft key is drawn as `Switch_Left_Normal/Active.bmp` (32×32) at (10,262), with its
label at (8,314) and anchor 36 (`src/MenuSystem.cpp:5212-5217`, label `drawString(…, strX, strY, 36)` at `:5238`).

### 5.5 Main-menu presentation (`MenuSystem::paint`, `src/MenuSystem.cpp:719-1135`)
* Background: `drawImage(background = logo.bmp, 240, 160, anchor 3)`. If `drawLogo`,
  `logo2.bmp` (170×120) is drawn at x `(480-170)>>1 = 155`, y 0 (`:764-771`). The logo is
  drawn again after the items, so it sits on top (`:1131-1134`).
* Item geometry: `setMenuSettings` defaults `menuItem_height = 46`,
  `menuItem_width = 162`, padding 0 (`:4173-4178`). Case `MENU_MAIN/MENU_MAIN_HELP`:
  `w = menu_button_background.width (204)`, `x = (480-204)/2 = 138`,
  `y = 120 + (200 - Menu_Main_BOX.h 161)/2 - 2 = 137`, `h = 38*4` (`:4241-4247`).
  `paint` then overrides the rect for every non-help main screen to
  `(x, 126, w, 176)` and clips to `y-15 .. y+h+15` (`:803-806`).
* Buttons: each actionable row is an `fmButton` with touch area
  `(menuRect.x, rowY, menuItem_width, menuItem_height)`. It renders
  `menu_button_background.bmp`, or `menu_button_background_on.bmp` (204×38) when
  highlighted (`src/MenuSystem.cpp:174-176`, `:5062-5115`).
* Text: with flag 8 and an action, x = `menuRect.x + menuItem_width/2 - textW/2`,
  y = `rowY + menuItem_height/2`, anchor 2 (`:961-966`, `:1072-1075`). Rows of
  `MENU_MAIN_EXIT/DIFFICULTY/MINIGAME/...` without an action get y += 8 (`:968-977`).
  `MENU_MAIN_CONFIRMNEW` row 3 gets y += 7 (`:1053-1056`).
* Cursor: on the selected row (not type 5/7), `drawCursor(x + OSC_CYCLE[time/100 % 4] + 3,
  rowY + h/2 - 8, 8)`, and the text shifts right by 8 (`:1064-1072`).
* Scrollbar: the rows total more than `menuRect.h`
  (7-8 rows × 46 px > 176 px), so `initMenu` switches on the **dial** scrollbar
  (`isMainMenuScrollBar`): `Menu_Dial.bmp` (45×183) at
  `(menuRect.x + 215, menuRect.y - 15)`. `Menu_Dial_Knob.bmp` (22×24) is drawn at
  `x+12, y + (thumbCentre*4/5 - 12) + 16` (`:2771-2785`, `:5019-5036`). For `isMainMenu`,
  `getScrollPos` snaps the scroll offset to row boundaries (`:4927-4945`).
* The scrollbar is not drawn on CONFIRMNEW/OPTIONS/DIFFICULTY/MINIGAME/MORE_GAMES/EXIT
  (`:869-879`).
* Assets that are loaded but **never drawn** in `src/`: `Main_Menu_OverLay`,
  `Main_Help_OverLay`, `Main_About_OverLay`, `Main_Menu_DialA_anim`, `Main_Menu_DialC_anim`,
  `Main_Menu_Slide_anim`, `Menu_ChooseDIFF_BOX` and `Menu_YesNo_BOX` (the last two are
  used only as sizes in `setMenuSettings`) (`src/MenuSystem.cpp:101-138`; grep finds no
  draw calls). `drawButtonFrame` only draws `Menu_Language_BOX` for
  `MENU_SELECT_LANGUAGE` (`:5043-5060`). So the main menu is: `logo.bmp`, then
  `logo2.bmp`, then 204×38 button plates, then the dial.

---------------------------------------------------------------------------------
## 6. `game->difficulty`

* Values 1 Normal / 2 Difficult / 4 Nightmare. They are set only by `ACTION_DIFFICULTY`
  (`src/MenuSystem.cpp:3096`). Default 1 (`src/Game.cpp:43`). The value is persisted in
  the config file (`src/Game.cpp:1921`, `:1984`).
* It is stamped into `scriptStateVars[12]` at map load (`src/LoadingManager.cpp:692`).
* Effect on the player: `player->reset()` sets base DEFENSE to 0 when `difficulty == 2`
  (`src/Player.cpp:561-563`). The character-select stats panel shows defense 0 in the same
  case (`src/IntroSequenceManager.cpp:530-532`).
* Other readers (combat/monsters): `src/Combat.cpp:255,984-986`, `src/Entity.cpp:62,479,504,1303`,
  `src/CombatEntity.cpp:288,314,343`, `src/ScriptThread.cpp:2189`, `src/Game.cpp:3558`.

---------------------------------------------------------------------------------
## 7. Character choice

* `Player::setCharacterChoice(i)`: `characterChoice = i`; `scriptStateVars[14] = i`
  (`src/Player.cpp:1264-1268`). It is called only from character select (§8.4).
* `setStatsAccordingToCharacterChoice` (`src/Player.cpp:454-486`), run from `reset()`
  (`:560`):

| choice | name (3:215-217) | short (3:212-214) | DEF | STR | ACC | AGI | IQ | credits `give(0,24,n)` |
|---|---|---|---|---|---|---|---|---|
| 1 | Kira Morgan | Major | 8 | 9 | 97 | 12 | 110 | 30 |
| 2 | Stan Blazkowicz | Sarge | 12 | 14 | 92 | 6 | 100 | 10 |
| 3 | Riley O'Connor | Sci-en-tist | 8 | 8 | 87 | 6 | 150 | 80 |

  The character-select panel hardcodes the same numbers again
  (`src/IntroSequenceManager.cpp:504-528`).
* Prologue name argument: 3:218 "Major Morgan" / 3:219 "Sergeant Blazkowicz" /
  3:220 "Dr. O'Connor" (`src/IntroSequenceManager.cpp:59-73`).
* HUD portraits per choice: `Hud_Player(_Active)` / `Hud_PlayerDoom(_Active)` /
  `Hud_PlayerScientist(_Active)` (`src/App.cpp:439-450`, `loadRuntimeImages`). Other
  readers: `src/Combat.cpp:298`, `src/DialogSystem.cpp:587`,
  `src/PlayingInputHandler.cpp:475`, `src/ArmorRepairSystem.cpp:33`, `src/Game.cpp:2730`
  (familiar body), `src/Game.cpp:3468` (re-stamps var 14 at load).

---------------------------------------------------------------------------------
## 8. `ST_CHARACTER_SELECTION` (22)

### 8.1 Entry (`src/Canvas.cpp:1166-1173`)
`clearSoftKeys()`; `setupCharacterSelection()`; `stateVars[0] = 1` (the highlighted
character constant: Major); `stateVars[1] = 0` (list mode); `stateVars[2] = 1`
(confirm cursor on Yes); `stateVars[8] = 0` (`[GEC]` keyboard sub-mode).

Assets (`src/IntroSequenceManager.cpp:108-127`, freed by `disposeCharacterSelection`
`:880-911`): `Character_select_stat_bar` (110×18), `Character_select_stat_header`
(110×16), `Character_select_top_bar` (163×22), `character_upperbar` (20×25, loaded but
never drawn), `charSelect` (59×92 atlas), `charSelectionBG` (480×320), `Hud_Player` /
`Hud_PlayerDoom` / `Hud_PlayerScientist` (32×160 face strips), and
`Major/Sarge/Riley_legs/_torso` (176×176 each).

Buttons (`src/IntroSequenceManager.cpp:30-48`): `m_characterButtons` ids 0-4
(0-2 = slots, 3 = Yes, 4 = Back). Their touch areas are set at draw time.

### 8.2 Slot order
`getCharacterConstantByOrder(i)`: 0 → 1 Major, 1 → **3 Scientist**, 2 → 2 Sarge
(`src/IntroSequenceManager.cpp:325-339`). Left to right the screen shows
Major, Riley, Sarge.

### 8.3 Layout (`drawCharacterSelection`, `src/IntroSequenceManager.cpp:341-462`; 480×320, `SCR_CX = 240`)
* `charSelectionBG` at (0,0); `Character_select_top_bar` at (240-81 = 159, 0)
  (`:351-352`).
* Title: the **full name** of the highlighted character (3:215/217/216) at (240, 3),
  anchor 1 (`:357-373`). 3:210 "Choose a Character" and 3:211 (help) are never drawn
  (grep).
* Three slots with `j = 152, 227, 302` (step 75), only one slot when `stateVars[1] != 0`
  (`:375-427`):
  * frame: `charSelect` (0,0,31,83) at (j-15, 55), plus the same region mirrored
    (transform 4) at (j+16, 55), for a 62×83 frame (`:380-381`);
  * touch-highlighted: `fillRect(j-15, 55, 62, 83, 0x2896FF)`, plus inner (31,0,27,60) at
    (j-11, 64) and mirrored at (j+16, 64) (`:383-389`). Touch area (j-15, 55, 62, 83)
    (`:391`);
  * links between slots 0-1 and 1-2: (51,60,6,19) tiled at x = j+46..j+63 step 6, y 81
    (`:393-397`);
  * selection marker under the highlighted constant: (31,60,20,32) at (j-3,161) plus
    (31,60,19,32) mirrored at (j+17,161) (`:399-402`);
  * face: region (0,0,32,32) of the character's `Hud_*` strip at (j, 75); short name
    (3:212/214/213) at (j+16, 142), anchor 1, `currentCharColor = 5` (`:404-430`).
* Avatar: `drawCharacterSelectionAvatar(choice, -10, 140)`. Legs at (-10,140); torso at
  `(-10, 140 - (time/1000 & 1))`, a 1-px bob that toggles every second
  (`:432`, `:464-488`).
* Stats: `drawCharacterSelectionStats(choice, x=362, y=115)` (`:433`, `:490-584`). Header
  image at (360,111); bars at (360, 129/149/169/189/209). Text rows "De-fense:" (3:13),
  "Strength:" (3:15), "Ac-cu-ra-cy:" (3:224), "Agi-li-ty:" (3:223), "IQ:" (3:225), each
  with the number appended, at (362, 131/151/171/191/211) (y+18-2 …), anchor 20, colour 5.
* Prompt 3:221 "Choose this character?", wrapped at 24 chars on `\n`, at (240, 235),
  anchor 1 (`:435-438`).
* Back button: `fillRect(155, 260, 70, 30, hl ? 0x2896FF : 0x646464)` + `drawRect` outline;
  label 3:80 "Back" at (190, 270), anchor 1; touch area = the rect (`:440-446`).
* Yes button: same at (255, 260, 70, 30); label 3:197 "Yes" at (290, 270) (`:454-460`).
* Keyboard confirm cursor (`[GEC]`, only when `stateVars[8] == 1`): `drawCursor` left of
  the Back label (`stateVars[2] == 0`) or the Yes label (`== 1`), x jittered by
  `OSC_CYCLE[time/100 % 4]` (`:448-463`).

### 8.4 Input (`handleCharacterSelectionInput`, `src/IntroSequenceManager.cpp:587-696`)
Routed from `handleEvent` (`src/InputEventController.cpp:317-318`). On touch release in this
state an `AVK_PASSTURN` event is queued and the handler reads the highlighted buttons
(`src/TouchController.cpp:334-337`, highlight on press/move at `:103-104`, `:235-236`).

* Touch: slot i highlighted → `stateVars[0] = 1/3/2`. **Yes** highlighted →
  `setCharacterChoice(stateVars[0])`, `player->reset()`, `setState(ST_INTRO)`,
  `disposeCharacterSelection()`. **Back** highlighted → `backToMain(false)`. All highlights
  are then cleared (`:612-634`). Touch confirms in one tap, with no second step.
* Keys (only when `!canvas->touched`), list mode (`stateVars[8] == 0`): RIGHT / LEFT cycle
  through the order 1 → 3 → 2 → 1 with `soundClick()` (1027, `Dialog_Help.wav`,
  `src/MenuSystem.cpp:5874-5876`); FIRE → `stateVars[8] = 1` + `playSound(1086)` (`:660-662`); MENU →
  dispose + `backToMain(false)` (`:636-667`).
* Confirm mode (`stateVars[8] == 1`): LEFT/RIGHT toggle Yes/Back (`stateVars[2]`) with a
  click; FIRE on Yes → choose + reset + `ST_INTRO`, FIRE on Back → dispose + `backToMain`,
  both with 1086 (`:685`); MENU → back to list mode with 1122 (`:687-692`).
* The `stateVars[1] == 1` branch (`:591-608`) is the old J2ME two-step confirm. Nothing
  sets `stateVars[1] = 1` (grep), so it is dead in the port.

---------------------------------------------------------------------------------
## 9. `ST_INTRO` (9): prologue pages

* Entry: `clearSoftKeys()`, `loadPrologueText()`, `stateVars[0] = 1` (`src/Canvas.cpp:1174-1178`).
* `loadPrologueText` (`src/IntroSequenceManager.cpp:50-100`): composes **2:12** (file
  strings, " \x80\x80 Mission Update \x80\x80|Greetings, %01. Mars Departure has cleared us…")
  with arg 3:218/219/220. It wraps at `(480-30)/10 = 45` chars, puts `(320-40)/21 = 13`
  lines on a page and paginates on `|`. Text origin (15, 20).
* `drawStory` (`:260-323`): `gameHelpBG.bmp` (480×320) at (0,0) (via
  `hackingGame->imgHelpScreenAssets`, loaded at `src/LoadingManager.cpp:87`). Labels use
  font render mode 2 (dim), or mode 0 while their touch button is highlighted:
  "Back" 3:80 at (17,310), anchor 36. Before the last page the bottom-right label is
  "More" 0:56 and the top-right is "Skip" 0:40 at (463,10), anchor 8. On the last page the
  bottom-right is "Continue" 0:43 and the top-right is empty. Bottom-right label at
  (463,310), anchor 40. Body: `drawString(dialogBuffer, 15, 20, lineH 21, …,
  storyIndexes[0], storyIndexes[1]-storyIndexes[0])`. **Always the first page's slice**,
  regardless of `storyPage` (likely a decompilation bug; harmless because 2:12 fits on one
  page).
* Touch buttons (`:42-48`): 0 Back (0,280,60,40); 1 More/Continue (380,280,100,40), whose
  touch area is moved to (420,·,60) when not on the last page (`:289-299`); 2 Skip
  (420,0,60,40). On release the button id goes into `stateVars[0]` and a FIRE event (6) is
  queued (`src/TouchController.cpp:270-288`).
* Keys (`handleStoryInput`, `src/IntroSequenceManager.cpp:698-750`): LEFT/RIGHT toggle 0↔1;
  UP → 2 (Skip) if not on the last page; DOWN 2 → 1; FIRE: 0 → `changeStoryPage(-1)`
  (on page 0 this returns to `ST_CHARACTER_SELECTION`, with `stateVars[0] = characterChoice`),
  1 → next page, 2 → `storyPage = storyTotalPages`; AUTOMAP → next page; BACK/MENU →
  previous page.
* `changeStoryPage(-1)` on page 0 calls `setState(ST_CHARACTER_SELECTION)`, which re-runs
  `setupCharacterSelection` (`:247-258`).
* End: `Canvas::run` sees `storyPage >= storyTotalPages` → `disposeIntro()`
  (`src/Canvas.cpp:848-851`) → `loadMap(startupMap = 1, false, tm_NewGame = true)`
  (`src/IntroSequenceManager.cpp:131-136`; `startupMap = 1` at `src/Canvas.cpp:63,162`).
  From there see `game-flow.md` §3.2-3.3 (`loadMap` → `ST_TRAVELMAP` → `ST_LOADING`).
  The new-game first save happens inside `loadMedia` (`save-load.md` §2).

---------------------------------------------------------------------------------
## 10. Leaving a game

| Entry | Path | Result |
|---|---|---|
| In-game root "Main Menu" (#10) → `MENU_INGAME_EXIT` 42 | type 6 rows: 3:151 "Go to Main Menu?" (9), blank, 3:152 "Save Game" → 25 `ACTION_SAVEEXIT`, 3:153 "Don't Save" → 5 `ACTION_BACKTOMAIN`, 3:154 "Cancel" → 2 BACK (decoded; `initMenu` `src/MenuSystem.cpp:2292-2296`) | |
| Save Game | `saveState(7 = 1\|2\|4, 3, 196)` (`:3199-3201`) → `ST_SAVING` → save, HUD 0:38 "Game Saved", `saveType & 4` → `backToMain(false)` (`src/Canvas.cpp:853-880`) | `MENU_MAIN` with Continue, title music 1071 |
| Don't Save | `backToMain(false)` (`src/MenuSystem.cpp:3051-3053`) | `MENU_MAIN`; nothing saved |
| Save & Quit (#9) → 53 → Yes | `ACTION_SAVEQUIT` → `saveState(67 = 1\|2\|0x40, 3, 196)` (`:3113-3115`) → `saveType & 0x40` → `app->shutdown()` (`src/Canvas.cpp:882-883`) | process exits |
| Dead menu 51 "Main Menu" | row #4 3:172 → 5 `ACTION_BACKTOMAIN` (decoded) | `MENU_MAIN` |
| Camera active | `initMenu(MENU_INGAME)` disables rows 3/4/5/10 and writes `items[11].action/param = GOTO 58` (`src/MenuSystem.cpp:1563-1570`). With 11 rows (0-10) index 11 lies past `numItems`, so in the port `MENU_INGAME_SPECIAL_EXIT` 58 (`SetYESNO(137,0,23,0)`) is **unreachable** and "Main Menu" is simply disabled | |
| End of game | `ST_CREDITS` after ranking → `MENU_END_` (`src/InputEventController.cpp:387-398`) | out of scope here |

Credits from the main-menu Help (`CHANGESTATE 15`): `setState(ST_CREDITS)` loads text type 2
and calls `initScrollingText(2, 0, false, 16, 5, 500)` (`src/Canvas.cpp:1161-1165`). BACK or
FIRE with `loadMapID == 0` pushes `MENU_MAIN` (return index 3, or 4 with a save) and opens
`MENU_MAIN_HELP` (`src/InputEventController.cpp:400-407`).

---------------------------------------------------------------------------------
## 11. Sounds used by the front end
(The id is the index in `Sounds::RESOURCE_FILE_NAMES` + 1000, `src/Sounds.h`, `src/Sound.cpp:180`.)

| id | file | where |
|---|---|---|
| 1068 | Music_General.wav | intro movie phase 0, flags 1 (`src/IntroSequenceManager.cpp:780`) |
| 1071 | Music_Title.wav | `backToMain(false)`, flags 1 (`src/Canvas.cpp:1404`) |
| 1086 | pistol.wav | menu select (`src/MenuSystem.cpp:3013`), char-select FIRE (`src/IntroSequenceManager.cpp:662,685`) |
| 1122 | switch_exit.wav | menu back (`src/MenuSystem.cpp:577-581`), char-select MENU in confirm mode (`:691`) |
| 1027 | Dialog_Help.wav | `soundClick()` (char-select LEFT/RIGHT) |

`startGame` calls `soundStop()` (`src/MenuSystem.cpp:3582`), so the title music stops
before character select. Character select and the prologue play no music.
