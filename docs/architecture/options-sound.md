# Options → Sound (main 17 / -3, in-game 35 / 55)

Date: 2026-11-05. ADR: `adr/0050-options-sound-in-menusession.md`. Retires the Options half
of deviation FE-D2 (`front-end.md` §10, `main-menu-subscreens.md` §6).
Facts: `docs/research/2026-11-05-sound-options-menu.md` (cited **R §n**), `docs/original-code/sound.md`
§3, §6, §6.1. `src/MenuSystem.cpp` is ISO-8859 + CRLF: use `grep -a`.

Scope: the `[GEC]` Options list with **only the Sound row** (main 17, in-game 35) and the
Sound screen (main -3 image sliders, in-game 55 rect sliders): rows, geometry, drawing,
keyboard edit mode, mouse press/drag, config save. No new files, no new module kinds:
both screens are `MenuSession` code-built screens (ADR 0013/0043/0044), drawn by the existing
`MainMenuView` / `MenuView`. **No CMake reconfigure** (no files added).

Groups, in delegation order (each builds and runs on its own):

| Group | Content | Files |
|---|---|---|
| **G1** | ids/flags/actions, the four row lists, `[GEC]` row heights, geometry of 55, Back soft key on 17/-3, FE-D2 refusal removed. Sliders are plain label rows (no bar yet). | `io/MenuData.h`, `core/MenuSession.cpp` |
| **G2a** | slider fields in `MenuRow`, producer fills them, in-game rect slider drawn | `ui/MenuModel.h`, `core/MenuSession.cpp`, `ui/MenuView.cpp` |
| **G2b** | three slider sheets, main image slider + digits drawn | `ui/UiAssets.{h,cpp}`, `ui/MainMenuView.cpp` |
| **G3** | edit mode (keys ±10), mouse press/drag, press sound, config save on leave, edit/drag reset | `core/MenuSession.{h,cpp}`, `core/GameContext.cpp` |

---------------------------------------------------------------------------------
## 1. G1 — screens and navigation

### 1.1 `io/MenuData.h`
* `MenuId`: `kMenuMainOptionsSound = -3` (`src/Menus.h:214`), `kMenuOptionsSound = 55`
  (`src/Menus.h:248-284`, R §1). Negative ids are fine: ids are only compared, stacked and
  passed to `MenuData::type()` (which returns -1 for both).
* `MenuItemFlag`: `kItemScrollBar = 0x20000, kItemScrollBarTwo = 0x40000, kItemPadding = 0x100000`
  (`src/Menus.h:17-33`). `menus.bin` flags are masked to 16 bits (`new_src/io/MenuData.cpp:39`),
  so no shipped row can carry them.
* `MenuAction`: `kActionChangeSfxVolume = 15, kActionChangeMusicVolume = 37` (`src/Menus.h:151,170`).

### 1.2 `core/MenuSession.cpp`
**Constants** (anonymous namespace):
```
kStrArgument1 = 241   // MenuStrings ARGUMENT1: rows reference 3:241.. as their text arg slot (src/MenuStrings.h:290-291)
kOptionsPadPx = 14    // ITEM_PADDING param of row 0 on 17 / -3 (src/MenuSystem.cpp:1376, :2306)
```
**`isCodeBuiltScreen`**: add `kMenuMainOptionsSound`, `kMenuOptionsSound` (absent from menus.bin).

**`unportedMainScreenReason`**: delete the `kMenuMainOptions` line (Other Games stays).

**`geometryFor`**: add `case kMenuOptionsSound:` to the `kMenuOptions` group (x 92, w 296;
`src/MenuSystem.cpp:4331-4351`, paint narrows to y 10 / h 241, `:847-849`). Main 17/-3 already get
`kMainRect` {138,126,204,176} through `listRect()` (R §4). (-3 also sets `menuItem_width = 204`,
`:4370`; it only affects action-row plates, and -3 has none in the view — not ported, no effect.)

**`initMenu`**, two branches before the final `else` (labels are English literals, R §2; row
label ids are the legacy `ARGUMENTk` ids so they are never `EMPTY_TEXT`):
```cpp
} else if (menuId == kMenuMainOptions || menuId == kMenuOptions) {      // [GEC] (:1374-1413)
    const bool main = menuId == kMenuMainOptions;
    const auto arg = [&](int k) { return env_.loc->makeId(kTextIngame2, kStrArgument1 + k - 1); };
    type_ = kMenuTypeList;                         // legacy inherits type (1/4); only 5/7 matter (OPT-D5)
    if (main) addItem(kEmptyTextId, kItemNoSelect | kItemPadding, kActionNone, kOptionsPadPx, kEmptyTextId);
    addLiteralRow(arg(1), "Options", kItemNoSelect | kItemAlignCenter | (main ? 0 : kItemDivider),
        kActionNone, 0, kEmptyTextId);
    addLiteralRow(arg(2), "Sound", main ? kItemAlignCenter : kItemNormal, kActionGoto,
        main ? kMenuMainOptionsSound : kMenuOptionsSound, kEmptyTextId);
    // "Video" (-2 / 56) and "Input" (-1 / 63) are not ported (OPT-D1).
} else if (menuId == kMenuMainOptionsSound || menuId == kMenuOptionsSound) {   // [GEC] (:2304-2337)
    const bool main = menuId == kMenuMainOptionsSound;
    const auto arg = /* same lambda */;
    const int slider = main ? kItemScrollBarTwo : kItemScrollBar;
    type_ = kMenuTypeList;
    if (main) addItem(kEmptyTextId, kItemNoSelect | kItemPadding, kActionNone, kOptionsPadPx, kEmptyTextId);
    addLiteralRow(arg(1), "Sound Options", kItemNoSelect | kItemAlignCenter | (main ? 0 : kItemDivider),
        kActionNone, 0, kEmptyTextId);
    addItem(kEmptyTextId, kItemNoSelect, kActionNone, 0, kEmptyTextId);
    addLiteralRow(arg(2), "SoundFx Volume:", slider, kActionChangeSfxVolume, 1, kEmptyTextId);
    if (!main) addItem(kEmptyTextId, kItemNoSelect, kActionNone, 0, kEmptyTextId);
    addLiteralRow(arg(3), "Music Volume:", slider, kActionChangeMusicVolume, 2, kEmptyTextId);
    // the ITEM_DISABLEDTWO patch (:2339-2341) is dead: isUserMusicOn() == true (:5814-5816).
}
```
Resulting rows (R §3): 17 = pad, title, Sound; 35 = title+divider, Sound; -3 = pad, title,
spacer, SFX, Music; 55 = title+divider, spacer, SFX, spacer, Music. The existing initMenu tail
(`moveDir(1)` past unselectable rows) puts the cursor on Sound (17: row 2, 35: row 1) and on SFX
(-3: row 3, 55: row 2) — R §3. Back pops to the pushed cursor as today (`:591-602`).

**`itemHeight`** — port `getMenuItemHeight` fully (`src/MenuSystem.cpp:4947-4984`):
```cpp
int MenuSession::itemHeight(int i) const {
    const MenuItemDef& d = items_[i].def;
    const bool slider = (d.flags & (kItemScrollBar | kItemScrollBarTwo)) != 0;
    int extra = 0;                                             // [GEC] sheight (:4956-4966)
    if (d.flags & kItemPadding) extra += d.param;
    if (d.flags & kItemScrollBar) extra += kFontHeight;
    else if (d.flags & kItemScrollBarTwo) extra += kFontHeight << 1;
    if (d.action != 0 && !slider) {
        if (i == numItems_ - 1) return kItemHeightPx;          // last row: no padding, no extra
        if (isMainMenu(menu_)) return kItemHeightPx + kMainItemPadding + extra;
        return kItemHeightPx + (isVendingMenu(menu_) ? kVendingPaddingBottom : kItemPaddingBottom) + extra;
    }
    if (i == numItems_ - 1) return kFontHeight;
    return kFontHeight + kItemFontPaddingBottom + extra;
}
```
Fix the comment above it (the `[GEC]` heights now have rows). Expected tops (R §4):
-3: 126, 156, 172, 188, 236; 55: 10, 26, 42, 74, 90; 17: 126, 156, 172; 35: 10, 26.

**`composeLabel`**: slider rows are drawn as plain label rows and get no plate/hit area
(paint forces `action = 0`, `:915`; `drawTouchButtons` skips them, `:5087`):
`row.action = d.action != 0 && (d.flags & (kItemScrollBar | kItemScrollBarTwo)) == 0;`

**`buildViewModel`**, main branch: the Back soft key (button 11) also on 17 and -3
(`updateTouchButtonState`, `:4509-4605`, R §5) — add `menu_ == kMenuMainOptions ||
menu_ == kMenuMainOptionsSound` to the `softLeft` condition. 35/55 already get Back + Resume
through the in-game path.

### 1.3 G1 acceptance
1. Main menu → Options: logo top, "Options" centred at y≈156, one plate "Sound" below
   (no Video/Input); Back switch bottom-left. No `refused` log line.
2. Sound: "Sound Options" centred; "SoundFx Volume:" at y 188 and "Music Volume:" at y 236,
   both left-aligned at x 138 for now (G2b moves them to 113); cursor on SoundFx, UP/DOWN toggles
   between the two. FIRE plays the pistol and logs `[menu] action 15 (param 1) has no rewrite path`
   (until G3). Back → Options with cursor on Sound → Back → main menu with cursor on Options.
3. In game: menu → Options: "Options" title with divider, one plate "Sound" (the old
   Controls/Back rows are gone). Sound → title + divider, "SoundFx Volume:" at y 42,
   "Music Volume:" at y 90. Back → Options → in-game root; Resume returns to the game.

---------------------------------------------------------------------------------
## 2. G2a — model fields + in-game rect slider (55)

### 2.1 `ui/MenuModel.h` — add to `MenuRow`
```cpp
// [GEC] volume slider (drawCustomScrollbar, src/MenuSystem.cpp:5296-5458).
// 0 = none, 1 = ITEM_SCROLLBAR rect slider (in-game), 2 = ITEM_SCROLLBARTWO image slider (main).
int  slider = 0;
int  sliderValue = 0;     // 0..100 as shown: allow flag ? volume : 0 (:5312, :5322)
bool sliderEdit = false;  // edit mode: "< N >" text (kind 1) / "<" ">" around the digits (kind 2)
int  sliderDragX = -1;    // >= 0: the slider button is held, knob follows this canvas x (valueScroll)
```
For kind 1 `value` carries the "N" / "< N >" text (the view draws it itself, see 2.3).

### 2.2 `core/MenuSession.cpp`
File-local helper: `int shownVolume(int param)` → `param == 1 ? (S.allowSounds ? S.soundFxVolume() : 0)
: (S.allowMusics ? S.musicVolume() : 0)` with `S = Sound::instance()` (`:5311-5330`).

In `composeLabel`, after `row.label = &t;`:
```cpp
if (d.flags & (kItemScrollBar | kItemScrollBarTwo)) {
    row.slider = (d.flags & kItemScrollBar) ? 1 : 2;
    row.sliderValue = shownVolume(d.param);
    row.sliderEdit = editSlider_ == d.param;                       // G2a: member exists, always 0 until G3
    row.sliderDragX = sliderDrag_ == d.param ? sliderDragX_ : -1;  // idem
    Text& v = valueBuf_[i];                                        // (:5317-5319)
    v.setLength(0);
    if (row.sliderEdit) v.append("< ");
    v.append(row.sliderValue);
    if (row.sliderEdit) v.append(" >");
    row.value = &v;
}
```
Add the three members to `MenuSession.h` in this group (so G2a builds), next to the drag state:
```cpp
// [GEC] Sound screen state (R §6). Values are the slider row's param: 1 = SFX, 2 = Music, 0 = none.
int editSlider_ = 0;    // changeSfxVolume / changeMusicVolume (+ changeValues) as one value
int sliderDrag_ = 0;    // updateSlider + sliderID: the held slider button
int sliderDragX_ = 0;   // sfxVolumeScroll / musicVolumeScroll: last pointer x while held
```
The volume is read live each frame, so no `setMenu(menu_)` refresh is needed anywhere.

### 2.3 `ui/MenuView.cpp`
* Pass 2 value column: draw only `if (r.value != nullptr && !r.centered && r.slider == 0)`.
* Pass 2, right after the label (and the disabled overlay) of a row, `if (r.slider == 1)`
  draw the rect slider — `L = m.rect.x` (92), `W = m.itemWidth` (296), `ty = y0 + 16`
  (FONT_HEIGHT), all from `src/MenuSystem.cpp:5361-5408`; legacy `drawRect(x,y,w,h)` maps to
  `ui.frame(UiRect{x,y,w,h})`, `FMGL_fillRect(x,y,w,h,r,g,b,a)` to `ui.panel(UiRect{x,y,w,h}, argb)`
  (the `DialogView` convention):
  ```
  ui.label(*r.value, L + W - 4, y0, kAnchorRight | kAnchorTop)          // (:5365-5367)
  ui.frame({L, ty + 1, W - 1, 20}, 0xFF323232)                          // track (:5372-5373)
  ui.panel({L + 1, ty + 2, W - 2, 19}, kTrackFill)
  if (r.sliderDragX >= 0) {                                             // button highlighted (:5383-5395)
      kx = r.sliderDragX - 12;
      if (kx < L) kx = L; else if (r.sliderDragX + 12 > L + W) kx = L + W - 24;
      ui.frame({kx, ty + 1, 23, 20}, 0xFF323232); ui.panel({kx + 1, ty + 2, 22, 19}, kKnobHeld);
  } else {                                                              // (:5397-5401)
      kx = L + (W - 24) * r.sliderValue / 100;
      ui.frame({kx, ty + 1, 23, 20}, 0xFF323232); ui.panel({kx + 1, ty + 2, 22, 19}, kKnobIdle);
  }
  ```
  Constants (float channels × 255, rounded): `kTrackFill = 0xB36B594F` (r .42 g .35 b .31 a .7),
  `kKnobHeld = 0xFFE6E6A6` (.9 .9 .65 1), `kKnobIdle = 0xFFBFB0A6` (.75 .69 .65 1).
  The disabled overlay (`:5403-5405`) is dead (R §3): not drawn.

### 2.4 G2a acceptance
In game → Options → Sound: right of each label the number `80` (or the saved value) right-aligned
at x≈384; under each label a 296×21 translucent brown track (SFX y 59..79, Music y 107..127) with a
light 24-px knob at about 80 % (x≈309). Main Sound screen unchanged from G1.

---------------------------------------------------------------------------------
## 3. G2b — main image slider (-3)

### 3.1 `ui/UiAssets.{h,cpp}`
Add `Texture optSliderBar, optSliderOn, optSliderOff;` (next to the main-menu skin), loaded with
`loadUiTexture` from `Menu_Option_SliderBar.bmp` (238×65), `Menu_Option_SliderON.bmp` (24×22),
`Menu_Option_SliderOFF.bmp` (24×22) (`src/MenuSystem.cpp:94-138`, sizes R §5). `Hud_Numbers.bmp`
is already `numbers`.

### 3.2 `ui/MainMenuView.cpp`
Constants (`src/MenuSystem.cpp:5409-5455`; `Menu_Option_BOX3.bmp` is only used for its width 284):
```
kOptBoxX       = (480 - 284) >> 1   // 98  (v59)
kOptBarX       = kOptBoxX + 1       // 99  (v6)
kOptLabelX     = kOptBoxX + 15      // 113 label x of a SCROLLBARTWO row (:1060-1062)
kOptKnobMinX   = kOptBarX           // 99
kOptDigitsX    = kOptBoxX + 246     // 344 (v10)
kOptKnobTravel = 245 - 24           // 221 (245 - SliderON.w)
```
In the labels loop:
* label x: `if (r.slider == 2) labelX = kOptLabelX;` instead of the non-centred `m.rect.x` (the
  cursor then sits at `113 + osc + 3`, label +8, as for any selected label row).
* right after `ui.label(...)` of that row, `if (r.slider == 2)`, with `ty = y0 + 16`:
  ```
  ui.image(art.optSliderBar, kOptBarX, ty - 20, kTopLeft)                        // (:5416)
  if (r.sliderDragX >= 0) {                                                     // highlighted (:5417-5430)
      kx = r.sliderDragX - 12;                                                  // valueScroll - SliderON.w/2
      if (kx < kOptKnobMinX) kx = kOptKnobMinX; else if (kx + 24 > kOptDigitsX) kx = kOptDigitsX - 24;   // 320
      ui.image(art.optSliderOn, kx, ty, kTopLeft);
  } else {
      ui.image(art.optSliderOff, kOptBarX + kOptKnobTravel * r.sliderValue / 100, ty, kTopLeft);       // (:5432)
  }
  int xr = kOptDigitsX;
  if (r.sliderEdit) { ui.glyph('<', xr + 5, ty + 5, kAnchorRight | kAnchorTop); xr += 10; }          // 349 / 354 (:5435-5441)
  ui.number3(art.numbers, xr, ty + 3, 0, r.sliderValue, false);                 // drawNumbers, 3 cells, leading zeros (:5442, :5794-5809)
  if (r.sliderEdit) ui.glyph('>', xr + 40, ty + 5, kAnchorRight | kAnchorTop);  // (:5443-5448)
  ```
  Order per row = legacy: label, then bar/knob/digits (`:1082-1084`). The disabled overlay
  (`:5450-5453`) is dead: not drawn.

### 3.3 G2b acceptance
Main → Options → Sound: labels at x 113; under "SoundFx Volume:" the slider bar image at
(99,184), knob at x≈275 y 204 for value 80, digits `080` at (344,207); Music bar at (99,232),
knob y 252, digits y 255. Report how the bar image overlaps the "Music Volume:" label
(drawn before the Music bar, legacy order; R open question 1).

---------------------------------------------------------------------------------
## 4. G3 — input and persistence

### 4.1 `core/MenuSession.h`
`Env`: `std::function<void()> saveConfig;   // Game::saveConfig (leaveOptionsMenu, src/MenuSystem.cpp:4041-4049)`.
Private: `UiRect sliderRect(int i) const;   // the slider button's touch area (:5370, :5414)`.

### 4.2 `core/GameContext.cpp`
`menuEnv.saveConfig = [this] { loading_.save().saveConfig(); };` next to `menuEnv.quit`.

### 4.3 `core/MenuSession.cpp`

> **Superseded in part (2026-11-06, OPT-D6):** the edit mode below was removed at the user's
> request. LEFT/RIGHT on the selected slider row step it ±5 directly, FIRE on a slider row is
> inert, `editSlider_` no longer exists, and `sliderEdit` marks the selected slider row
> (`new_src/core/MenuSession.cpp:1706-1718`). Drag and saveConfig text still applies.
**Edit mode — `select()`** (after the existing 1086 pistol, `:3013`):
```cpp
case kActionChangeSfxVolume:      // (:3120-3125)
case kActionChangeMusicVolume:    // (:3276-3281)
    editSlider_ = editSlider_ != 0 ? 0 : items_[i].def.param;
    break;
```
(Legacy toggles `changeSfx/MusicVolume` and `changeValues` separately; while editing, the cursor
cannot move, so `select` always hits the same row and one value is equivalent.)

**Edit mode — `handleAction()`**, directly after the help-popup block, before the vending block:
```cpp
// [GEC] changeValues: LEFT/RIGHT step the volume; actions <= FIRE or >= BACK leave edit mode,
// AUTOMAP..PASSTURN (7..14: Automap, Passturn, Next/PrevWeapon, HudNextWeapon, Debug*) are swallowed
// (src/InputEventController.cpp:186-212, src/MenuSystem.cpp:2960-2967).
if (editSlider_ != 0 && a != Action::MenuResume) {
    if (a == Action::TurnLeft || a == Action::TurnRight) {
        Sound& s = Sound::instance();
        const int d = a == Action::TurnRight ? 10 : -10;                 // volumeUp/Down(10)
        if (editSlider_ == 1) s.setSoundFxVolume(s.soundFxVolume() + d);
        else s.setMusicVolume(s.musicVolume() + d);
        s.soundClick();                                                  // 1027, after the change
    } else if (a == Action::Automap || a == Action::Passturn || a == Action::NextWeapon ||
               a == Action::PrevWeapon || a == Action::HudNextWeapon /* || Debug* */) {
        // ignored while editing (src/MenuSystem.cpp:2964); review fix 2026-11-05
    } else {
        select(selectedIndex_);                                          // toggles edit mode off (plays 1086)
    }
    return;
}
```
`Action::MenuResume` (Resume soft key = touch button 15, `:4785-4790`) bypasses the block and
resumes. Not editing, LEFT/RIGHT keep the existing page-scroll path (`:2880-2890`).

**Mouse — `sliderRect(i)`**: `rowTop = listRect().y - scrollPixels() + Σ itemHeight(0..i-1)`
(both screens never scroll, R §4), `ty = rowTop + 16`:
* `kItemScrollBar` (55): `{ listRect().x, ty + 1, kItemWidthPx, (kItemHeightPx >> 1) - 2 }` =
  (92, ty+1, 296, 21) (`:5369-5371`);
* `kItemScrollBarTwo` (-3): `{ 99, ty - 20, 238, 65 }` (`:5413-5415`, `SliderBar` size).
Constants: `kOptBarX = 99` ((480 - 284) >> 1) + 1), `kOptBarW = 238`, `kOptBarH = 65`.
On -3 the two rects overlap at y 232..248; rows are tested in order, so SFX wins there
(button 12 precedes 13 in the legacy container, `src/MenuSystem.cpp:199-205`).

**Mouse — `updateDrag()`**, after `gestureConsumed_ = false;`, before the main-menu early return:
```cpp
// [GEC] slider buttons 12/13 (handleUserTouch :4775-4783, handleUserMoved :4836-4840,
// :4876-4884, release :4666-4673). Neither Sound screen scrolls.
if (menu_ == kMenuMainOptionsSound || menu_ == kMenuOptionsSound) {
    if (!in.down) { sliderDrag_ = 0; return; }                 // release: un-highlight, silent
    if (!in.cursorValid) return;
    if (sliderDrag_ == 0) {                                     // press on, or held finger moved onto, a slider
        for (int i = 0; i < numItems_ && sliderDrag_ == 0; ++i)
            if ((items_[i].def.flags & (kItemScrollBar | kItemScrollBarTwo)) &&
                sliderRect(i).contains(in.cursorX, in.cursorY)) sliderDrag_ = items_[i].def.param;
        if (sliderDrag_ == 0) return;
        if (!Sound::instance().isSoundPlaying(1027)) Sound::instance().playSound(1027, 0, 5);   // fmButton 12/13 highlight edge (src/Button.cpp:145-158)
    }
    sliderDragX_ = in.cursorX;                                  // valueScroll; follows the pointer anywhere while held
    const int v = menu_ == kMenuMainOptionsSound
        ? 100 * (sliderDragX_ - 114) / 221                      // 114 = SliderON.w/2 + 98 + 4 (:5848-5850)
        : 100 * (sliderDragX_ - (listRect().x + 12)) / (kItemWidthPx - 24);   // 104, 272 (:5852-5855)
    if (sliderDrag_ == 1) Sound::instance().setSoundFxVolume(v);   // clamps 0..100 + updateVolume (:5857-5865)
    else Sound::instance().setMusicVolume(v);
    return;
}
```
Integer division truncates toward zero like the legacy. The 3-px knob/finger offset on the main
slider (knob drawn from 99, inverse from 114) is verbatim (R §6).

**Reset on leave** (OPT-D2): in `initMenu`'s `if (menuId != oldMenu_)` block add
`editSlider_ = 0; sliderDrag_ = 0;`; in `returnToGame()` add the same two assignments.

**Persistence** (OPT-D3): new file-local helper
`bool isOptionsScreen(int id) { return id == kMenuMainOptions || id == kMenuOptions || id == kMenuMainOptionsSound || id == kMenuOptionsSound; }`;
first statement after the 1122 sound in `back()`, and first statement of
`returnToGame()`:
```cpp
if (isOptionsScreen(menu_) && env_.saveConfig) env_.saveConfig();   // legacy: back() from 17/35 only (:583-585)
```

### 4.4 G3 acceptance
1. Main Sound: FIRE on SoundFx → pistol, digits become `< 080 >`. RIGHT → `090` + click (louder
   click), RIGHT → `100`, RIGHT stays `100`. LEFT ×10 → `000`: effects silent (no click heard).
   UP / DOWN / FIRE / Escape / Backspace → edit mode off (pistol), cursor does NOT move.
2. Not editing: LEFT/RIGHT do not change volumes.
3. Music: same on "Music Volume:" while the title music plays — it gets quieter / stops at 0 and
   resumes audibly when raised (music already looping keeps playing at the new gain).
4. Mouse: press on a slider bar → click sound, `SliderON` knob jumps under the cursor, value
   follows while dragging (also when the cursor leaves the bar); release → `SliderOFF` knob at the
   value position (about 3 px left of where the finger was, verbatim). Same in game with the
   rect slider: held knob is pale yellow, idle knob light grey.
5. Edit mode on, then tap the Back soft key → edit mode off only (OPT-D4); Back again → Options;
   LEFT/RIGHT there do NOT change any volume (OPT-D2).
6. Set SFX 30 / Music 50, Back to the main menu, quit, restart → Options → Sound shows 030 / 050.
   In game: change, press Resume, quit, restart → values kept (OPT-D3).

---------------------------------------------------------------------------------
## 5. Deviations

| Id | What | Why |
|---|---|---|
| OPT-D1 | 17/35 list only "Sound"; "Video" (-2/56) and "Input" (-1/63) rows omitted | user scope 2026-11-05; both are `[GEC]` desktop settings (SDL video modes, key bindings) with no rewrite counterpart |
| OPT-D2 | Edit mode and slider latch are cleared on every screen change and on Resume | legacy never clears them (`back()` / `returnToGame()`), so touch Back/Resume leaks edit mode into the next menu (R §7) |
| OPT-D3 | Config saved on Back from any of 17/35/-3/55 and on Resume from 35/55 | legacy saves only on Back from 17/35, so Resume loses the change until an unrelated save (R §7) |
| OPT-D4 | Back soft key while editing only leaves edit mode | the rewrite routes the soft key through `Action::BackKey` (= keyboard ACTION_BACK, which legacy turns into `select`); legacy touch button 11 calls `back()` directly |
| OPT-D5 | `type_` set to LIST on the four screens | legacy inherits it from the previous menu (4 or 1, R §3); only types 5/7 change behaviour |
| OPT-D6 | No edit mode: LEFT/RIGHT on a selected slider row step it ±5 (legacy ±10) + click 1027 directly; FIRE on a slider row is inert and silent; the `< >` marks show on the selected slider row. Supersedes the edit-mode text of §4.3 and makes OPT-D2/OPT-D4 moot | user request 2026-11-05 — legacy FIRE-to-edit was an extra step |

Verbatim quirks kept: the 3-px main slider offset; SFX wins the overlapping touch strip on -3;
the shown value is 0 whenever the allow flag is off; `soundClick` plays after the step (so at
SFX 0 it is silent).
