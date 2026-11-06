# 2026-11-05 — Sound options menu (Options 17/35 → Sound -3/55)

## Hypothesis / question

What exactly does the legacy port build and draw for the `[GEC]` Options list
(`MENU_MAIN_OPTIONS` 17, `MENU_INGAME_OPTIONS` 35) and the Sound sub-menu
(`MENU_MAIN_OPTIONS_SOUND` -3, `MENU_INGAME_OPTIONS_SOUND` 55), how is input handled, and
when are volumes persisted? Facts already in `docs/original-code/sound.md` §3/§6,
`front-end.md` §12.6 and `ui.md` §11-13/16 are not repeated.

## Method

Read `src/MenuSystem.cpp` (`initMenu`, `setMenuSettings`, `paint`, `getMenuItemHeight`,
`drawTouchButtons`, `drawCustomScrollbar`, `drawNumbers`, `updateTouchButtonState`,
`handleUserTouch`, `handleUserMoved`, `updateVolumeSlider`, `select`, `back`,
`handleMenuEvents`, `moveDir`), `src/InputEventController.cpp:186-271`, `src/Menus.h`,
`saveConfig` call sites. BMP sizes read from the headers of the files in the shipped
`Doom 2 RPG.ipa` (`Payload/Doom2rpg.app/Packages/`).

Note: `src/MenuSystem.cpp` is ISO-8859 with CRLF; plain `grep` treats it as binary and
silently prints nothing — use `grep -a`.

## Verdict

CONFIRMED / extracted (all four questions answered; quirks listed in §6).

## Evidence

### 1. Menu ids

`src/Menus.h:214` `MENU_MAIN_OPTIONS_SOUND = -3`; `:63` `MENU_MAIN_OPTIONS = 17`;
`:81` `MENU_INGAME_OPTIONS = 35`; `:105` `MENU_INGAME_CONTROLS = 62`. Counting the enum
from `MENU_INGAME` 29 (`:248-284`): `MENU_INGAME_BINDINGS` 54, `MENU_INGAME_OPTIONS_SOUND`
55, `MENU_INGAME_OPTIONS_VIDEO` 56, `MENU_INGAME_CONTROLS` 62, `MENU_INGAME_OPTIONS_INPUT`
63, `MENU_INGAME_CONTROLLER` 64. Video/Input main ids are -2/-1 (`:215-216`).

Flag values (`src/Menus.h:17-33`): NOSELECT 1, ALIGN_CENTER 8, DIVIDER 64 (0x40),
SCROLLBAR 0x20000, SCROLLBARTWO 0x40000, DISABLEDTWO 0x80000, PADDING 0x100000.
Actions: GOTO 1 (`:136`), CHANGESFXVOLUME 15 (`:151`), CHANGEMUSICVOLUME 37 (`:170`).

### 2. Labels: hard-coded literals via text args

All labels are English literals appended with `localization->addTextArg(textbuff)`; rows
reference them as `STRINGID(FILE_MENUSTRINGS=3, ARGUMENTk)` with `ARGUMENT1 = 241`
(`src/MenuStrings.h:290-291`), i.e. string 3:241 = the 1st arg, 3:242 the 2nd, etc. The
shipped string table is not used for these screens. `textField2` is `EMPTY_TEXT` =
`STRINGID(3, EMPTY_STRING=0)` (`src/MenuSystem.h:38`, `src/MenuStrings.h:41`), so no value
column is composed. `initMenu` resets text args first (`src/MenuSystem.cpp:1240`).

- 17/35 args (`src/MenuSystem.cpp:1395-1413`): 1 `"Options"`, 2 `"Sound"`, 3 `"Video"`,
  4 `"Input"`.
- -3/55 args (`src/MenuSystem.cpp:2329-2337`): 1 `"Sound Options"`, 2 `"SoundFx Volume:"`,
  3 `"Music Volume:"`.

### 3. Rows built

`initMenu` preamble: `selectedIndex=scrollIndex=0` only if `oldMenu != menu`,
`maxItems=4`, `drawLogo=false`, 50 items cleared (`src/MenuSystem.cpp:1226-1237`).
`type` and `background` are **not** reset and these cases do not call `loadMenuItems`, so
both are inherited from the previous menu: from `MENU_MAIN` type 4 + `imgMainBG`
(`:1262`, front-end.md), from `MENU_INGAME` type 1 (ui.md §11.1). Type only matters for
the type 5/7 special paths (`select` `:3004-3011`, cursor `:1064`), which do not apply.

**17 `MENU_MAIN_OPTIONS`** (`src/MenuSystem.cpp:1374-1384`), `drawLogo = true`:

| # | text | flags | action | param |
|---|---|---|---|---|
| 0 | EMPTY | NOSELECT\|PADDING (0x100001) | 0 | 14 (extra px) |
| 1 | ARG1 "Options" | NOSELECT\|ALIGN_CENTER (9) | 0 | 0 |
| 2 | ARG2 "Sound" | ALIGN_CENTER (8) | GOTO | -3 |
| 3 | ARG3 "Video" | 8 | GOTO | -2 |
| 4 | ARG4 "Input" | 8 | GOTO | -1 |

**35 `MENU_INGAME_OPTIONS`** (`:1386-1393`), no logo:

| # | text | flags | action | param |
|---|---|---|---|---|
| 0 | ARG1 "Options" | NOSELECT\|ALIGN_CENTER\|DIVIDER (0x49) | 0 | 0 |
| 1 | ARG2 "Sound" | 0 | GOTO | 55 |
| 2 | ARG3 "Video" | 0 | GOTO | 56 |
| 3 | ARG4 "Input" | 0 | GOTO | 63 |

**-3 `MENU_MAIN_OPTIONS_SOUND`** (`:2304-2316`), `drawLogo = true`:

| # | text | flags | action | param |
|---|---|---|---|---|
| 0 | EMPTY | NOSELECT\|PADDING | 0 | 14 |
| 1 | ARG1 "Sound Options" | NOSELECT\|ALIGN_CENTER | 0 | 0 |
| 2 | EMPTY | NOSELECT | 0 | 0 |
| 3 | ARG2 "SoundFx Volume:" | SCROLLBARTWO | 15 | 1 |
| 4 | ARG3 "Music Volume:" | SCROLLBARTWO | 37 | 2 |

**55 `MENU_INGAME_OPTIONS_SOUND`** (`:2317-2327`):

| # | text | flags | action | param |
|---|---|---|---|---|
| 0 | ARG1 "Sound Options" | NOSELECT\|ALIGN_CENTER\|DIVIDER | 0 | 0 |
| 1 | EMPTY | NOSELECT | 0 | 0 |
| 2 | ARG2 "SoundFx Volume:" | SCROLLBAR | 15 | 1 |
| 3 | EMPTY | NOSELECT | 0 | 0 |
| 4 | ARG3 "Music Volume:" | SCROLLBAR | 37 | 2 |

`if (!isUserMusicOn()) items[3].flags |= ITEM_DISABLEDTWO` (`:2339-2341`) is dead:
`isUserMusicOn()` returns `true` (`:5814-5816`). (It would also hit the wrong row on 55.)

Initial cursor: after the switch, if the selected row is EMPTY or has flag 0x8001,
`moveDir(1)` (`:2733-2738`) skips to the first selectable row (`:414-430`): 17 → row 2
"Sound", 35 → row 1, -3 → row 3 (SFX), 55 → row 2 (SFX). Toggling edit mode calls
`setMenu(this->menu)` (`:3123`, `:3279`), `oldMenu == menu`, so the cursor is kept.

### 4. Layout (rect, row heights)

`setMenuSettings` (`src/MenuSystem.cpp:4168-4428`) defaults: `menuItem_height 46`,
`menuItem_width 162`, both paddings 0 (`:4173-4178`).
- 17 and -3: `x = (480 - 204) >> 1 = 138` (`menu_button_background.bmp` is 204x38,
  loaded `:95`), `y = 169` (because `HasVibration()` returns true, `:5811-5813`), `w = 204`,
  `h = 300` (`:4294-4303`, `:4369-4378`). -3 also sets `menuItem_width = 204` (`:4370`); 17
  keeps 162.
- **But** `paint` re-sets the rect every frame for every `menu < MENU_INGAME` (negative ids
  included) to `y = 126, h = 176`, keeping x/w, and clips to
  `(screenRect.x, 111, screenRect.w, 206)` (`:808-811`); for 17/-3 none of the help-menu
  branches match, so `goto LABEL_61` keeps that rect (`:812-841`). Effective main rect:
  **x 138, y 126, w 204, h 176**.
- 35 and 55 (`menu >= 29`): `setMenuDimentions(70, 0, 340, 320 - panelBottom.h)`,
  `menuItem_width = inGame_menu_option_button.bmp width = 296`, `menuItem_paddingBottom = 10`
  (`:4190-4194`); then case 35/55: `x = (480-296)>>1 = 92, y 0, w 296, h 320`
  (`:4331-4351`); `paint` then overrides to `y = 10, h = 241` (`:847-849`). Effective
  in-game rect: **x 92, y 10, w 296, h 241**.

Row heights (`getMenuItemHeight`, `:4947-4984`, FONT_HEIGHT[0] = 16 per ui.md): a row
with `action != 0` and no slider flag = `menuItem_height + paddingBottom`; otherwise
`16 + fontPaddingBottom (0)`; PADDING adds `param`; SCROLLBAR adds 16; SCROLLBARTWO adds 32.
The **last row returns early without the extra** (`:4969-4971`, `:4976-4978`).

- -3: row heights 30, 16, 16, 48, 16 → row tops y = 126, 156, 172, 188, 236.
- 55: row heights 16, 16, 32, 16, 16 → row tops y = 10, 26, 42, 74, 90.
- 17: 30, 16, then buttons 46, 46, 46 (last 46) → tops 126, 156, 172, 218, 264.
- 35: 16, then buttons 56, 56, 46 → tops 10, 26, 82, 138.

Neither screen scrolls (total height < menuRect.h at init time, `:2740-2753`), so no
scrollbar is set up.

### 5. Drawing

Background: 17/-3 take the `background` branch — `imgMainBG` (`logo.bmp`, 480x320,
loaded `src/Canvas.cpp:1383`) drawn at (240,160) anchor 3, then `logo2.bmp` when
`drawLogo` (`src/MenuSystem.cpp:765-771`). 35/55 take the in-game branch (game-menu
background + bottom panel with health/armor, `:734-759`). Soft keys: button 11 "Back"
for 17 and -3; buttons 11 + 15 ("Resume") for 35 and 55
(`updateTouchButtonState` `:4509-4605`; drawing per ui.md §16). -3/55 additionally set
`drawButton = true` on slider buttons 12 (SFX) and 13 (Music, gated by the always-true
`isUserMusicOn`) (`:4516-4521`, `:4589-4595`).

Row plates: slider rows get no plate and no row touch area — `drawTouchButtons` skips
items with SCROLLBAR/SCROLLBARTWO (`:5087`). Action rows of 17/35 are ordinary button rows.

Label (paint loop `:911-1084`): for slider rows `action` is forced to 0 (`:915`), so the
label is drawn as a plain text row: anchor 0 (top-left) at the row top `v80`;
x = `menuRect.x` (non-centered branch `:982-992`); SCROLLBARTWO overrides x to
`((480 - BOX3.w) >> 1) + 15 = 98 + 15 = 113` (`:1060-1062`, `Menu_Option_BOX3.bmp`
284 wide, loaded `:112`, used only for its width). If the row is selected (types 5/7
excluded) the cursor is drawn at `x + OSC_CYCLE + 3` anchor RIGHT and the label moves +8
(`:1064-1075`). Then `drawCustomScrollbar(g, item, textBuffer1, v80)` (`:1082-1084`).

`drawCustomScrollbar` (`:5296-5458`). Per param: 1 = SFX: `value = allowSounds ?
soundFxVolume : 0`, `valueScroll = sfxVolumeScroll`, button 12, `change =
changeSfxVolume`; 2 = Music: `value = allowMusics ? musicVolume : 0`,
`valueScroll = musicVolumeScroll`, button 13, `disable = !isUserMusicOn()` (always false),
`change = changeMusicVolume` (`:5311-5330`). The value text is `"< N >"` when editing,
else `"N"` (`:5317-5319`).

**In-game, `ITEM_SCROLLBAR` (55)** (`:5361-5408`), with `L = menuRect.x = 92`,
`W = menuItem_width = 296`, `v70 = 46 >> 1 = 23`, `ty = rowTop + 16`:
- value text: `drawString(text, L + W - 4 = 384, rowTop, 24)` (anchor RIGHT|TOP);
  font render mode 2 if disabled.
- touch area of button 12/13: `(L, ty+1, W, v70-2 = 21)`.
- track: outline `0xFF323232` `drawRect(L, ty+1, W-1, 20)`; fill
  `FMGL_fillRect(L+1, ty+2, W-2, 19, r .42 g .35 b .31 a .7)`.
- knob (outline `0xFF323232` `drawRect(kx, ty+1, 23, 20)`):
  - button highlighted (touch drag): `kx = valueScroll - 12` clamped to
    `[L, L+W-24]`; fill `(kx+1, ty+2, 22, 19)` rgba `.9 .9 .65 1`.
  - otherwise: `kx = L + (W-24)*value/100 = 92 + 272*value/100`; fill rgba `.75 .69 .65 1`.
- disabled overlay `(L, ty+2, W, 21)` rgba `.5 .5 .5 .3` (never, see §3).
- For 55: SFX row top 42 → value text at y 42, track y 59..79; Music row top 90 → track
  y 107..127.

**Main, `ITEM_SCROLLBARTWO` (-3)** (`:5409-5455`), `b = (480 - 284) >> 1 = 98`,
`x0 = b + 1 = 99`, `ty = rowTop + 16`:
- touch area of button 12/13: `(99, ty-20, SliderBar.w = 238, SliderBar.h = 65)`.
- `drawImage(Menu_Option_SliderBar, 99, ty-20, anchor 0)`.
- knob (24x22): highlighted → `Menu_Option_SliderON` at `x = valueScroll - 12` clamped to
  `[99, b+246-24 = 320]`, y `ty`; otherwise `Menu_Option_SliderOFF` at
  `x = 99 + (245-24)*value/100 = 99 + 221*value/100`, y `ty`.
- number: `xr = b + 246 = 344`; editing → `"<"` drawn anchor 24 at `(349, ty+5)` and
  `xr = 354`; `drawNumbers(g, xr, ty+3, 0, value)`; editing → `">"` anchor 24 at
  `(xr+40, ty+5)`.
- disabled overlay `(99, ty-20, 244, 20)` rgba `.2 .2 .2 .5` (never).
- For -3: SFX row top 188 → bar at (99,184), knob y 204, digits y 207; Music row top 236 →
  bar (99,232), knob y 252, digits y 255.

`drawNumbers` (`:5794-5809`): three 10x20 cells from `Hud_Numbers.bmp`, always 3 digits
(leading zeros), no spacing (`space = 0`) → x, x+10, x+20; source y of digit d =
`(9 - d) * 20` (third digit expression simplifies to `(9 - number%10)*20`); values ≥ 1000
print an error. `drawRegion(..., anchor 20 = LEFT|TOP)`.

Assets (all present in the shipped `.ipa`, header sizes): `Menu_Option_SliderBar.bmp`
238x65, `Menu_Option_SliderON.bmp` 24x22, `Menu_Option_SliderOFF.bmp` 24x22,
`Menu_Option_BOX3.bmp` 284x171 (width only), `Hud_Numbers.bmp` 10x220, `logo.bmp` 480x320,
`logo2.bmp` 170x120, `menu_button_background(_on).bmp` 204x38,
`inGame_menu_option_button.bmp` 296x32. All 8-bit BMPs, loaded `src/MenuSystem.cpp:94-138`.
The in-game slider uses no images (rects only).

### 6. Input

Edit mode (keyboard/controller):
- `select()` on a slider row: plays 1086 pistol pri 5 (`:3013`), then
  `ACTION_CHANGESFXVOLUME`: `changeSfxVolume = !changeSfxVolume; changeValues =
  !changeValues; setMenu(menu)` (`:3120-3125`); `ACTION_CHANGEMUSICVOLUME` same with
  `changeMusicVolume` (`:3276-3281`). State vars: `MenuSystem::changeSfxVolume`,
  `changeMusicVolume`, `changeValues` (`src/MenuSystem.h:151,224,226`, init false
  `src/MenuSystem.cpp:85-88`).
- While `canvas->state == ST_MENU && changeValues`, `InputEventController` intercepts
  RIGHT/LEFT before menu dispatch: `volumeUp(10)`/`volumeDown(10)` (or
  `musicVolumeUp/Down(10)`), `soundClick()` (1027 pri 5), `refresh()` = `setMenu(menu)`,
  `return true` (`src/InputEventController.cpp:186-212`).
- Other keys reach `handleMenuEvents`; with `changeValues` set the normal navigation is
  bypassed: LEFT/RIGHT → `setMenu(menu)`; any action `<= ACTION_FIRE (6)` or
  `>= ACTION_BACK (15)` (UP 1, DOWN 2, MENU 5, FIRE 6, BACK 15, ...) → `select(selectedIndex)`
  which toggles edit mode off again (`src/MenuSystem.cpp:2960-2967`; action values
  `src/Enums.h:993-1009`). So UP/DOWN/Back while editing just leave edit mode, they do not
  move the cursor or leave the menu.
- Not editing: LEFT/RIGHT are `scrollPageUp/Down` (`:2880-2890`); no volume change.

Touch / mouse (works regardless of edit mode):
- Press on button 12/13 (drawButton true): highlight, `updateVolumeSlider(id, x)`; on
  success `sliderID = id; updateSlider = 1` (`:4775-4783`). Drag:
  `handleUserMoved` → `updateVolumeSlider(sliderID, x)` while `updateSlider`
  (`:4836-4840`); moving onto a slider button also starts a drag (`:4876-4884`).
  Release: un-highlight `sliderID`, `updateSlider = 0`, return — **no `select()`, no
  click sound** from this path (`:4666-4673`).
- `updateVolumeSlider` (`:5818-5867`): stores `x` into `sfxVolumeScroll` /
  `musicVolumeScroll` (knob follows the finger while highlighted), then
  - menu 17, -3, -4, -6 (main image slider): `value = 100*(x - (SliderON.w/2 + b + 4)) /
    (245 - SliderON.w) = 100*(x - 114) / 221`;
  - else (55): `value = 100*(x - (menuRect.x + 12)) / (menuItem_width - 24) =
    100*(x - 104) / 272`;
  - integer division (truncates toward 0), clamp 0..100, written directly to
    `soundFxVolume`/`musicVolume`, then `sound->updateVolume()`. No sound played.
- Main mismatch: knob drawn at `99 + 221*v/100` (center `111 + ...`) but the inverse uses
  origin 114 → 3 px offset. In-game is consistent (center `104 + 272*v/100`).
- Leaving: button 11 → `back()`; button 15 → `returnToGame()` (`:4785-4790`).

### 7. Back / navigation and persistence

- 17 is entered from `MENU_MAIN` row "Options" GOTO 17 (front-end.md); 35 from
  `MENU_INGAME` row 7 GOTO 35 (ui.md §11.1). `gotoMenu` pushes `(menu, selectedIndex,
  scroll y1/y2, scrollIndex)` (`:2818-2824`).
- `back()` (`:568-611`): plays 1122 unless MAIN/MORE_GAMES (`:577-581`); **only for 17 and
  35** calls `leaveOptionsMenu()` → `game->saveConfig()` (`:583-585`, `:4041-4049`); then
  pops the stack: -3 → 17, 17 → `MENU_MAIN`, 55 → 35, 35 → `MENU_INGAME` 29, restoring the
  pushed `selectedIndex` (`:591-602`).
- Volume changes are applied live (`updateVolume`) but **persisted only when backing out of
  17/35**. Leaving 55 or 35 with "Resume" (button 15 → `returnToGame`, `:1209-1220`) skips
  `saveConfig`; the values are written at the next unrelated `saveConfig`
  (`LoadingManager::loadState` `src/LoadingManager.cpp:42`, `Game::saveState`
  `src/Game.cpp:2073`, intro end `src/Canvas.cpp:838`, `src/IntroSequenceManager.cpp:875`,
  video Apply `src/MenuSystem.cpp:3314`, F1 renderer toggle `src/Input.cpp:781`) or lost on
  quit.
- Quirk: `back()`/`returnToGame()` never clear `changeSfxVolume`/`changeMusicVolume`/
  `changeValues`. Touch Back (button 11) or Resume while in edit mode leaves edit mode set;
  in the next menu LEFT/RIGHT still change the volume and UP/DOWN/FIRE call `select()`
  on whatever row is selected (`src/InputEventController.cpp:186`,
  `src/MenuSystem.cpp:2960-2967`). The keyboard Back path cannot cause this (it is turned
  into `select`, §6).

### 8. Old rows of 35 and menu 62

- The original `MENU_INGAME_OPTIONS` initMenu body (`loadMenuItems(menu,0,1)` "Controls" +
  inline volume rows) is inside `#if 0 // Old` (`src/MenuSystem.cpp:1618-1650`), as is the
  old `MENU_INGAME_CONTROLS` body (`:1767+`). The decoded `menus.bin` rows "Controls" → 62
  and "Back" are not loaded for 35.
- 62 is still reachable: 35 → Input (63) → row "Touch Controls" GOTO 62
  (`:2446-2448`); the live `[GEC]` case shares the body with `MENU_MAIN_CONTROLS` -4,
  builds a title row and then `loadMenuItems(MENU_INGAME_CONTROLS, 0, -1)` (`:2474-2493`),
  so 62's shipped rows are used there (and its type is loaded). 62 also draws
  `hud->drawArrowControls` (`:777-779`). Out of scope for Sound.
- `paint` skips `drawButtonFrame` for 17/35/62 (`:788-795`; the old `drawOptionsScreen`
  call is commented out). `drawOptionsScreen` (`:5461-5790`, old fixed-position sliders at
  y 180/247) is dead code.

## Open questions

- Visual: `Menu_Option_SliderBar.bmp` is 65 px tall but drawn starting 20 px above the
  knob row (`ty-20`); where the visible groove lies inside the image can only be confirmed
  by the user looking at the screen / the image.
- `screenRect` x/w for the main clip were not resolved (horizontal clip only).
