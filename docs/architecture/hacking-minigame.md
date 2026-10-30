# Spec — Hacking minigame (`new_src/minigames/hacking/`)

Date: 2026-10-30
Facts: `docs/original-code/hacking-minigame.md` (cited below as **HG §n**), raw log
`docs/research/2026-10-30-hacking-minigame.md`.
Framework: ADR 0033, `new_src/minigames/common/*`. Pattern: `new_src/minigames/vending/*`.
Decision record: ADR 0038 (touch resolution in the tick).

## 1. Goal and what the user sees

Script `EV_MINIGAME type=2 (0, cols, turns)` (map00 secure-storage terminal, tile (29,27),
facing W; also map04/05/06) opens the "Hacker" puzzle. The help screen comes first ("Hacker"
title, body text, "Tap Screen to Continue", "Exit" bottom right). FIRE or a tap opens the game:
the `hackerBG` background, 3..6 columns of colored 18x18 squares, a marker under each column,
"Turns: N" top-left, "Goal" plus colored bars top-right, a prompt bottom-left, "Back"/"Exit"
labels at the bottom, and a pulsing gray selection box. Win → "Memory columns restored!", then
FIRE → back to the world, HUD message "Hack Successful! +2 to I.Q." (or "I.Q. is at Max."), and
the script grants access (dialog 73, door unlock). Lose/quit → dialog 74 "access denied".
Debug: **H** in the world starts the map00 puzzle (3 columns, 15 turns) with no script thread.

## 2. Reuse / no-change list

- `MiniGame` interface, `MiniGameHost::start/finish`, the action queue and `latchTouch`:
  unchanged (ADR 0033).
- `ScriptVM` `EV_MINIGAME` (`new_src/domain/game/ScriptVM.cpp:1393-1405`): **no change**. It
  already forwards `(type, b1, b2, b3, thread)` to `MiniGameHost::start` and parks only on
  `true`. Type 2 starts working as soon as the host has the case. Type 0 (sentry) keeps
  logging "not implemented" and does not park.
- Help screen: `MiniGameHelp` (scroll + model) and `drawMiniGameHelp` (view), as vending uses
  them (`new_src/minigames/vending/VendingMachine.cpp` `buildHelp`, `VendingView.cpp` `drawHelp`).
- Button hit test, highlight, release: `common/MiniGameButtons.h`.
- RNG: `ItemPickup::nextInt()` (`new_src/domain/game/ItemPickup.cpp:23`, `std::rand() &
  0x7FFFFFFF` = legacy `Applet::nextInt`, `src/App.cpp:506-508`). Vending uses the same one.
  **Decision:** keep it. The rewrite seeds `std::srand(time)` once at startup
  (`new_src/core/Main.cpp:43`), while the legacy never seeds (HG §2 RNG). So the boards differ
  from the original's first-run sequence, but the semantics (draw order, `& 0xFF`, divisor) are
  identical. This was already decided project-wide for vending and loot rolls, so it needs no ADR.
  `nextByte() = ItemPickup::nextInt() & 0xFF` (`src/App.cpp:510-512`) is a file-local helper
  in `HackingGame.cpp`.
- Label dimming (`fontRenderMode 2`) is a no-op in the original (Canvas::fontRenderMode is never
  read, already established in `new_src/minigames/vending/VendingView.cpp` `drawHelp` comment),
  so "Back"/"Exit" are drawn plain.
- Soft keys are never drawn in ST_MINI_GAME (HG §4), so every `clearSoftKeys` /
  `setLeft/RightSoftKey` is dropped.

## 3. Files

| File | Group | Kind |
|---|---|---|
| `new_src/ui/UiAssets.h/.cpp` | G1 | modify: the fix callback may change the width |
| `new_src/minigames/vending/VendingAssets.cpp` | G1 | modify: `fixArrowDown` to the new callback signature |
| `new_src/minigames/hacking/HackingAssets.h/.cpp` | G1 | new |
| `new_src/minigames/hacking/HackingModel.h` | G2 | new |
| `new_src/minigames/hacking/HackingView.h/.cpp` | G2 | new |
| `new_src/minigames/hacking/HackingGame.h/.cpp` | G3 | new |
| `new_src/minigames/common/MiniGameHost.h/.cpp` | G4 | modify |
| `new_src/core/GameContext.cpp`, `core/GameStates.h`, `core/UiInputCollector.cpp` | G4 | modify (hud env, debug key H) |

New files ⇒ re-run `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (GLOB) before building.
Layering (ADR 0033 point 1): `HackingModel.h`/`HackingView.*` include only `ui/`, `text/`,
`render/`, `minigames/common/MiniGameButtons.h`, `MiniGameHelpModel.h`/`MiniGameHelpView.h` and
their own headers. `HackingGame.*` may include `domain/`, `io/`, `ui/`, `core/GameStates.h`.

## 4. G1 — assets

### 4.1 `loadUiTexture` fix callback may widen the image
`blockGameColors.bmp` ships 106x35. The original's `fixImage` widens it to 108 and overwrites a
54x18 block (`src/Utils.cpp:116-161`). The current callback type
`void (*)(std::vector<uint8_t>&)` (`new_src/ui/UiAssets.h:78-79`) cannot change the width.

Change the parameter (declaration and definition) to:
```cpp
// May rewrite the indices and the width (rows stay `height`).
using FixIndicesFn = void (*)(std::vector<uint8_t>& indices, int& width, int height);
bool loadUiTexture(TextureStore& store, const UiAssets::ResourceReader& read,
	const char* name, Texture& out, FixIndicesFn fixIndices = nullptr);
```
In the fix branch (`new_src/ui/UiAssets.cpp:24-28`): `int w = img->width();
fixIndices(fixed, w, img->height()); out.uploadIndexed(store, fixed, w, img->height(), ...)`.
Update `fixArrowDown` in `VendingAssets.cpp` to the new signature (ignore width/height). There
are no other callers (grep `fixIndices`/`loadUiTexture(` before editing).

### 4.2 `HackingAssets`
```cpp
// Sheets only the hacking minigame draws (src/LoadingManager.cpp:85-87,105,108).
struct HackingAssets {
	Texture background;   // hackerBG.bmp 480x320 (legacy imgEnergyCore)
	Texture colors;       // blockGameColors.bmp, widened to 108 by fixBlockColors
	bool load(TextureStore& store, const UiAssets::ResourceReader& read);
};
```
`load`: `if (!read) return false;` then `loadUiTexture(... "hackerBG.bmp", background)` and
`loadUiTexture(... "blockGameColors.bmp", colors, fixBlockColors)`, combined with `&=` as in
`VendingAssets::load`.

`fixBlockColors(indices, width, height)` (file-local), a verbatim port of `src/Utils.cpp:116-161`:
- Guard: `if (width != 106 || height < 18) return;`. The legacy gates on the shipped file's MD5,
  and this is the only variant there is, the same reasoning as vending's `fixArrowDown`.
- `newW = width + 2`. Build a zeroed `newW*height` buffer, copy each source row `i` (width bytes)
  to `i*newW`. Then for rows `0..17` copy `fixData[i*54 .. +54]` to `i*newW + 54`. Assign the
  buffer back and set `width = newW`.
- `static const uint8_t kFixData[54 * 18]` is copied **by hand** from `src/Utils.cpp:124-153`
  (972 bytes, same order). `static_assert(sizeof(kFixData) == 54 * 18)`.

Atlas layout (HG §4 Assets): piece k (1..6) = src `(18k-18, 0, 18, 18)`. Column marker j =
src `(12j, 18, 12, 12)`.

**G1 acceptance:** builds. Vending's down arrows still render exactly as before (the callback
change is behaviour-neutral). `HackingAssets` compiles and is not wired yet.

## 5. G2 — model + stateless view

### 5.1 `HackingModel.h`
```cpp
// Per-frame view model of the hacking minigame, filled by HackingGame::draw.
// Every pointer is borrowed for that frame only.
struct HackingModel {
	enum class Screen { Help, Game, Confirm };   // legacy stateVars[1] 0 / 1 / 2
	Screen screen = Screen::Help;

	MiniGameHelpModel help;            // Help only
	const Text* exitLabel = nullptr;   // Code 30: Help and Game at (445, 302) anchor 3
	const Text* backLabel = nullptr;   // Menu 80: Game only at (35, 302) anchor 3

	// ---- Game and Confirm (drawGameScreen, src/HackingGame.cpp:375-510) ----
	int columns = 3;
	short board[5][6] = {};            // [row][col], row 0 = top, 0 = empty, k = piece k
	int hoverColumn = -1;              // white outline while touched over it
	const Text* turns = nullptr;       // "Turns:" + turnsLeft
	const Text* goal = nullptr;        // "Goal"
	const Text* prompt = nullptr;      // Game: 166/169/170, Confirm: 189; wrapped at 47
	int selRow = 0, selCol = 0;
	bool carrying = false;             // inner second outline
	uint32_t selColor = 0;             // pulsing gray, 0xFF000000 | g*0x010101
	// ---- Confirm only ----
	const Text* yesLabel = nullptr;    // Menu 197
	const Text* noLabel = nullptr;     // Menu 198
	bool yesHeld = false, noHeld = false;   // touched over the box: 0x7878FF fill
	int cursorX = 0;                   // OSC wobble applied, y 245, anchor 8

	MiniGameButton buttons[2];         // id 0 Exit (420,280,60,40), id 1 Back (0,280,60,40)
	int buttonCount = 2;
};
```

### 5.2 `HackingView.h/.cpp`
```cpp
// Stateless hacking screens (src/HackingGame.cpp:346-582). Returns the touch
// release resolved to a legacy button id (UiAction::MiniGameTouch).
UiResult drawHacking(Ui& ui, const HackingModel& m, const HackingAssets& art);
```
Primitive mapping (load-bearing):
- Legacy `drawRect(x,y,w,h)` outlines **(w+1)x(h+1)** (`src/Graphics.cpp:236-241`), while
  `Ui::frame` outlines w x h (`new_src/render/Graphics2D.cpp:57-63`). So legacy `drawRect(x,y,w,h,c)`
  becomes `ui.frame(UiRect{x, y, w+1, h+1}, c)`.
- Legacy `fillRect` is always opaque (`src/Graphics.cpp:88-95`). So `ui.panel(rect, 0xFF000000 | rgb)`.
- `drawRegion(img, sx,sy,sw,sh, x,y, 0, ...)` becomes `ui.imageRegion(tex, sx,sy,sw,sh, x,y, 0)`.
- `drawCursor(x,y,8)` becomes `ui.glyph('\x8A', x, y, 8)` (`src/Graphics.cpp:674-686`).

Help (`drawHelpScreen`, `src/HackingGame.cpp:359-373`): `drawMiniGameHelp(ui, m.help)`, then
`exitLabel` at (445, 302) anchor 3.

Game / Confirm, in this order (`src/HackingGame.cpp:375-510`):
1. `ui.image(art.background, 0, 0, 0)`.
2. If `hoverColumn >= 0`: `ui.frame({25 + 79*hoverColumn, 90, 36, 101}, 0xFFFFFFFF)` (legacy
   `drawRect(25+79i, 90, 35, 100, -1)`, `:383-386`; 79 = CORE_WIDTH 35 + 44).
3. Markers, for j < columns: `imageRegion(colors, 12j, 18, 12, 12, 37 + 79j, 195, 0)` (`:402-404`).
4. `turns` at (148, 28) anchor 1 (`:406-411`).
5. Goal: `goal` at (335, 38) anchor 1. Bars for i < columns:
   `panel({n + 9i, 22, 5, 15}, kBar[i])`, with `n = 335 - (5 + 9*(columns-1))/2` and
   `kBar = {0xFFA9E94E, 0xFFE77313, 0xFFFD1100, 0xFFC450FF, 0xFFFFF395, 0xFF101ADE}` (`:512-551`).
6. Screen::Game: `prompt` at (12, 215) anchor 4, `backLabel` at (35, 302) anchor 3,
   `exitLabel` at (445, 302) anchor 3 (`:416-453`).
   Screen::Confirm (`:454-506`): `prompt` at (75, 215) anchor 4. Yes box:
   `panel({142,240,45,25}, yesHeld ? 0xFF7878FF : 0xFF808080)`, then
   `frame({142,240,46,26}, 0xFFFFFFFF)`, then `yesLabel` at (150, 245) anchor 4. No box: same at
   x 258 with `noHeld`, `noLabel` at (270, 245) anchor 4. Then the cursor glyph at
   (`cursorX`, 245) anchor 8.
7. Pieces (`drawGamePieces(34, 94)`, `:553-582`), for row i 0..4 and col j < columns with
   `board[i][j] != 0`: `imageRegion(colors, 18*board[i][j] - 18, 0, 18, 18, 34 + 79j, 94 + 19i, 0)`.
   Selection at `x = 34 + 79*selCol`, `y = 94 + 19*selRow`: `frame({x, y, 19, 19}, selColor)`,
   and when `carrying` also `frame({x+1, y+1, 17, 17}, selColor)`. Pieces are drawn on the confirm
   screen too.

Return `touchRelease(m.buttons, m.buttonCount, ui.in())` on every screen. Both buttons exist on
all screens (`src/HackingGame.cpp:93-99`).

**G2 acceptance:** builds (nothing calls it yet). The reviewer checks every coordinate/color
against this section and HG §4.

## 6. G3 — `HackingGame` session

### 6.1 Header
```cpp
// The hacking minigame session (src/HackingGame.cpp), world branch only:
// no playFromMainMenu, no forceWin (ADR 0033 point 7).
class HackingGame : public MiniGame {
public:
	void init(MiniGameHost* host) { host_ = host; }
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);

	// EV_MINIGAME type 2: b1 = custom flag, b2 = columns, b3 = turns (-1 = 30)
	// (src/ScriptThread.cpp:1314-1347, src/HackingGame.cpp:53-100).
	void begin(int b1, int b2, int b3) override;
	void handleAction(Action a) override;      // handleInput (:182-301)
	void handleTouch(int buttonId) override;   // touchEnd (:661-688) + release pass (ADR 0038)
	UiResult draw(Ui& ui) override;            // updateGame (:346-357)

private:
	static constexpr int kRows = 5, kMaxCols = 6;
	static constexpr int kScreenHelp = 0, kScreenGame = 1, kScreenConfirm = 2;   // stateVars[1]
	static constexpr int kPlaying = 0, kWon = 1, kLost = 2;                      // stateVars[2]
	static constexpr int kColumnTapBase = 100;   // pseudo button ids, ADR 0038

	void fillBoard(int scrambles);             // fillGameBoardRandomly 5-arg (:133-180)
	bool solved() const;                       // gameIsSolved (:584-593)
	void attemptToMove(int n);                 // (:303-344)
	void releasePass();                        // first draw after touchEnd (:382-400, :460-503)
	void endGame(int n);                       // (:619-636)
	void compose(int type, int index, Text& out) const;   // appends, as vending

	MiniGameHost* host_ = nullptr;
	HackingAssets assets_;
	HackingModel model_;

	short board_[kRows][kMaxCols] = {};
	int columns_ = 3;
	int turnsLeft_ = 0;
	int selRow_ = 0, selCol_ = 0;
	bool selected_ = false;                    // carrying a block
	int cameFrom_ = 0;                         // columnBlockCameFrom
	int screen_ = kScreenHelp;
	int result_ = kPlaying;
	int confirmCursor_ = 0;                    // persists between confirm visits
	bool done_ = false;                        // endGame ran; stop the rest of a touch
	// Raw press samples (touchStart/Move/End, :638-659); (0,0) = cleared.
	int curX_ = 0, curY_ = 0, oldX_ = 0, oldY_ = 0;
	bool touched_ = false;
	int touchedColumn_ = -1;                   // legacy static: NOT reset by begin
	int lastHighlight_ = -1;                   // button highlight edge -> sound 1027

	Text exitLabel_, backLabel_, turns_, goal_, prompt_, yes_, no_;
};
```
Column rect helper (file-local): `UiRect columnRect(int i) { return {25 + 79*i, 90, 35, 100}; }`
with `UiRect::contains` = legacy `pointInRectangle` (`src/Utils.cpp:52-54`, `x >= rx && x < rx+w`).

### 6.2 `begin(b1, b2, b3)` (`src/ScriptThread.cpp:1314-1347`, `src/HackingGame.cpp:36-100`)
1. `if (b1 != 0)` log `[hacking] custom puzzle %d not supported` and continue as a standard
   puzzle. The legacy calls `app->Error` (`:1317-1318`). No shipped map uses it, and failing
   here would park the thread forever (the VM parks after `start` returns true).
2. `columns_ = clamp(b2, 3, 6)`. `turns = (b3 == -1) ? 30 : b3`.
3. `iq = host_->env().player->ce.getIQPercent()` (`src/CombatEntity.cpp:62-64`).
   `scrambles = (10000*turns + turns*(100-iq)*(100-iq)) / 20000` (int, `src/HackingGame.cpp:65`).
   `fillBoard(std::max(scrambles, 1))`. The `max` is a shortcut: with 0 scrambles the legacy
   recurses forever on a solved board, and the shipped turns (15..40) always give ≥ 7.
4. setupGlobalData: `confirmCursor_ = 0; screen_ = kScreenHelp; result_ = kPlaying;
   host_->help().reset(); selected_ = false;`. Then `turnsLeft_ = turns`.
5. Initial cursor (`:68-80`): for `col` 0..columns_-1, for `row` 0..4: the first `board_[row][col]
   != 0` sets `selRow_ = row, selCol_ = col` and stops both loops.
6. `curX_ = curY_ = oldX_ = oldY_ = 0; touched_ = false; lastHighlight_ = -1; done_ = false;`.
7. Log `[hacking] begin cols=%d turns=%d iq=%d scrambles=%d`.

### 6.3 `fillBoard(scrambles)` (`src/HackingGame.cpp:133-180`), verbatim
```
do {
  board_ = 0; for k < columns_: board_[4][k] = board_[3][k] = board_[2][k] = k+1
  b = 0; mostRecentSrc[c] = mostRecentDest[c] = c  (c < 6)
  div = (255 + columns_) / columns_            // 86/64/52/43
  for (n = 0; n < scrambles; ++n):
    src = nextByte() / div; dst = nextByte() / div      // this draw order
    if (src == dst || src == b
        || (mostRecentDest[src] == dst && mostRecentSrc[dst] == src)
        || board_[4][src] == 0 || board_[0][dst] != 0) { --n; continue; }
    move the top block of src (smallest row with a value) to the lowest empty
      row of dst (scan 4 -> 0); clear the source cell
    mostRecentSrc[src] = dst; mostRecentDest[dst] = src; b = dst
} while (solved());          // the legacy recursion (:177-179), same RNG draws
```
Read `:153-174` while porting. The rejection conditions and the move loop are copied in the
legacy order. Self-check (Debug, `<cassert>`): after the loop, each color 1..columns_ occurs
exactly 3 times on the board.

`solved()`: every non-zero `board_[i][j]` over all 5x6 cells equals `j+1`.

### 6.4 `attemptToMove(n)` (`src/HackingGame.cpp:303-344`)
Port the loop **verbatim**, including the `n3`/`n4` stepping and the post-loop
`selectedColumn2 = (n4 - n3 + cols) % cols`. Summary: `c = (n + cols) % cols`, nothing if
`c == selCol_`. `n2` = lowest empty row of c. Not carrying: `selRow_ = (n2 == 4) ? 4 : n2 + 1;
selCol_ = c`. Carrying: step by `c - selCol_` until a column has space or the walk returns to the
source. On success the block moves and the cursor follows, else `selected_ = false`.

### 6.5 `handleAction(a)` (`src/HackingGame.cpp:182-301`)
Mapping: FIRE=`Use`, MENU=`Menu`, BACK=`BackKey`, AUTOMAP=`Automap`, LEFT/RIGHT=`TurnLeft/
TurnRight`, UP/DOWN=`Forward/Back` (`new_src/minigames/common/MiniGame.h`).
- Help: `Menu`/`BackKey` → `endGame(result_ == kPlaying ? 2 : result_ == kWon ? 1 : 0)`. `Use` →
  `screen_ = kScreenGame`. `Forward` scroll(-1), `Back` scroll(+1), `TurnRight` scroll(+16),
  `TurnLeft` scroll(-16).
- Game, `result_ == kPlaying`: `TurnLeft`/`TurnRight` → log `[sound] 1027`, then
  `attemptToMove(selCol_ ∓ 1)`. `Automap`/`BackKey` → `screen_ = kScreenHelp;
  help().reset()` (`selected_` is kept). `Menu` → `screen_ = kScreenConfirm`. `Use`:
  - carrying: `selected_ = false`. If `cameFrom_ != selCol_` then `--turnsLeft_`. Then
    `solved()` → `result_ = kWon`, else `turnsLeft_ == 0` → `result_ = kLost`. Log
    `[hacking] drop col=%d turns=%d result=%d`.
  - not carrying: if `board_[selRow_][selCol_] != 0` then `selected_ = true; cameFrom_ = selCol_`.
- Game, `kWon`: `Use`/`Automap` → `endGame(1)`. `kLost`: `Use`/`Automap` → `endGame(0)`.
- Confirm: `Use` → `confirmCursor_ == 1 ? endGame(2) : screen_ = kScreenGame`.
  `TurnLeft`/`TurnRight` → `confirmCursor_ ^= 1`. `BackKey` → `screen_ = kScreenGame`.
- Every other action is ignored. The first line is `if (done_) return;`.

### 6.6 Touch (`src/HackingGame.cpp:638-688`; ADR 0038)
**Sampling, at the top of `draw(ui)`**, with `in = ui.in()` and only when `in.cursorValid`
(`p = (in.cursorX, in.cursorY)`):
- `in.pressed`: `touched_ = true; cur = old = p` (touchStart).
- else `touched_ && in.down && p != cur`: `old = cur; cur = p` (touchMove).
- `in.released`: `touched_ = false; cur = p` (touchEnd; `old` kept).
If `!in.cursorValid && in.released`: `touched_ = false` only.

**Hover / drag-leave, in `draw`, only when `touched_ && screen_ != kScreenHelp`**, for
i < columns_ (`:382-400`):
- `columnRect(i).contains(cur)`: `model_.hoverColumn = i`. If `i != touchedColumn_` then
  `touchedColumn_ = i` and log `[sound] 1027` (soundClick).
- else if `columnRect(i).contains(old)`: `touchedColumn_ = -1; old = (0,0)`, and the frame
  returns `UiResult{UiAction::MiniGameTouch, kColumnTapBase + i}` instead of the view's result.
  The loop continues (the hover outline for other columns is still set).

**`handleTouch(id)`**, with `if (done_) return;` first:
- `id >= kColumnTapBase`: `attemptToMove(id - kColumnTapBase); handleAction(Use); return;`.
- touchEnd switch (`:661-688`):
  - Help: `id == 0` → `handleAction(Menu)`. Else `cur = old = (0,0); handleAction(Use)`. Return.
  - Game: `result_ != kPlaying` → `handleAction(Use)`. Else `id == 0` → `handleAction(Menu)`,
    `id == 1` → `handleAction(BackKey)`.
  - Confirm: nothing.
- Then `releasePass()`. It is the legacy first draw after touchEnd. It runs only if
  `!done_ && screen_ != kScreenHelp`:
  1. For i < columns_: if `columnRect(i).contains(old)`: `touchedColumn_ = -1;
     attemptToMove(i); handleAction(Use); old = (0,0)`. Stop if `done_`.
  2. If `screen_ == kScreenConfirm && !done_`: if Yes rect `{142,240,45,25}` contains `cur` →
     `confirmCursor_ = 1; handleAction(Use); cur = (0,0)`. Then, if still `!done_` and on the
     confirm screen, the No rect `{258,240,45,25}` contains `cur` → `confirmCursor_ = 0;
     handleAction(Use); cur = (0,0)` (`:466-498`, same sequential order).

**Kept quirk (faithful default):** a column tap on the confirm screen runs FIRE as the confirm
choice, and the drag-leave fires on the left column.

### 6.7 `draw(ui)` → model
After sampling (6.6):
- `model_ = HackingModel{}`. `exitLabel_` = Code 30 (kTextMain). `model_.buttons = { {0,
  {420,280,60,40}}, {1, {0,280,60,40}} }` (`src/HackingGame.cpp:95,98`).
- Button highlight sound: `hl = highlightedButtonId(buttons, 2, in)`. `if (hl != -1 && hl !=
  lastHighlight_)` log `[sound] 1027`. Then `lastHighlight_ = hl`. Same as
  `VendingMachine::draw`, `src/Button.cpp:145-155`.
- Help: `help().buildModel(164, 165, model_.help)`, `model_.help.background =
  &host_->assets().helpBackground`.
- Game/Confirm: copy board/columns/selection. `turns_` = Code 167, dehyphenate, then
  `append(turnsLeft_)`. `goal_` = Code 168, dehyphenate.
  `selColor`: `t = upTimeMs & 0x3FF`, `g = (t < 511) ? 160 + ((95*t) >> 9) : 255 - ((95*(t-511)
  + 511) >> 9)`, `selColor = 0xFF000000 | g*0x010101`. The second form is the portable spelling
  of the legacy `(-95*(t-511) >> 9) + 255`, an arithmetic shift that floors
  (`src/HackingGame.cpp:566-572`).
  - Game: `prompt_` = Code 166 / 169 / 170 by `result_`, then `wrapText(47, '\n')`. `backLabel_` =
    Menu 80 (`kTextIngame2`).
  - Confirm: `prompt_` = Code 189, `wrapText(47, '\n')`. `yes_` = Menu 197, `no_` = Menu 198
    (`kTextIngame2`, not dehyphenated). `yesHeld = touched_ && YesRect.contains(cur)`, and the
    same for `noHeld`. `osc = tables->oscCycle[(upTimeMs/100) % 4]` (guard `size() >= 4`, else 0,
    as vending does). `cursorX = (confirmCursor_ == 1 ? 136 : 252) + osc`.
- Hover/drag-leave pass (6.6), then `r = drawHacking(ui, model_, assets_)`. Return the pseudo
  column tap if one was emitted this frame, else `r`.

Text ids (strings file 0 = `kTextMain`, file 3 = `kTextIngame2`; HG §4): Code 30, 96 (footer,
inside `MiniGameHelp`), 164, 165, 166, 167, 168, 169, 170, 189, 236, 237. Menu 80, 197, 198.

### 6.8 `endGame(n)` (`src/HackingGame.cpp:619-636`)
`done_ = true;` log `[sound] %d` (`n == 1 ? 1043 : 1040`), then
`host_->evaluateResults(n); host_->finish(n);`. The legacy order is sound → evaluate →
`vars[7]` → Playing → resume. Results: **1 won, 0 lost (out of turns), 2 quit**.

**G3 acceptance:** builds (not wired yet). The reviewer diffs `fillBoard`/`attemptToMove`
against `src/HackingGame.cpp:133-180, 303-344` line by line.

## 7. G4 — host and debug wiring

### 7.1 `MiniGameHost`
- `Env` gains `Hud* hud = nullptr;` (forward-declare `class Hud;`).
- Public `void evaluateResults(int n);`. This is the shared `evaluateMiniGameResults`
  (`src/MiniGameManager.cpp:235-248`), and per ADR 0033 Consequences it moves here with its first
  user:
  ```
  if (n != 1) return;
  const int gained = env_.player->modifyStat(Enums::STAT_IQ, 2);
  std::string s = env_.loc->get(kTextMain, gained > 0 ? 236 : 237);
  if (gained > 0) { std::string a = std::to_string(gained); composeArgs(s, &a, 1); }
  if (env_.hud) env_.hud->addMessage(s, 0xAA000000, 3);   // addMessage(id, 3)
  ```
  Same call shape as `Player::usedChainsaw` (`new_src/domain/game/Player.cpp:340-352`).
  `composeArgs` is in `domain/game/Game.h:36`. Vending does NOT call it (it awards IQ at submit,
  ADR 0033).
- Member `HackingGame hacking_;`, include `minigames/hacking/HackingGame.h`. `init`:
  `hacking_.init(this)`. `loadAssets`: `& hacking_.loadAssets(store, read)`. `start`:
  `case kHacking: g = &hacking_; break;`. Update the default-case comment to "sentry bot: later".

### 7.2 `GameContext` / input
- `new_src/core/GameContext.cpp:106-116`: `mgEnv.hud = sys_.hud;`.
- `core/GameStates.h`: after `DebugVending` add
  `// DEBUG (removable, minigames): H starts the map00 hacking puzzle.` `DebugHacking,`.
- `core/UiInputCollector.cpp`, next to V: `case SDL_SCANCODE_H: a = Action::DebugHacking; break;
  // DEBUG (removable, minigames)`. H is unused today (scancodes in use: B C I K N O V, arrows,
  RETURN ESCAPE TAB BACKSPACE F12).
- `GameContext::tick` next to `DebugVending` (`:318-322`): `if (a == Action::DebugHacking) {
  minigames_.start(MiniGameHost::kHacking, 0, 3, 15, nullptr); continue; }` (map00 args, HG §1).

**G4 acceptance (user-visible, after reconfigure + build):**
1. In the world press **H**: the help screen appears ("Hacker", body text, "Tap Screen to
   Continue", "Exit" at the bottom right). UP/DOWN scroll if the body is longer than 16 lines.
2. ENTER: the game screen shows 3 columns of colored squares, 3 markers, "Turns: 15", "Goal"
   plus 3 bars, the prompt text, "Back" and "Exit". The selection box pulses gray about once per second.
3. LEFT/RIGHT move the box (wrapping). ENTER picks up (double box), LEFT/RIGHT carry the block,
   ENTER drops. The turn count drops only when the block lands in another column.
4. Solve → "Memory columns restored!...". ENTER → back to the world, HUD "Hack Successful!" with
   "+2 to I.Q." (stderr: `[sound] 1043`). Running out of turns → "...restore failed!", ENTER →
   world (`[sound] 1040`, no message).
5. ESC in the game → "Progress will be lost. Still exit?", gray Yes/No boxes, wobbling cursor at No.
   LEFT/RIGHT toggle. ENTER on No → back to the game. ENTER on Yes → world.
6. Mouse: clicking a column moves the cursor/block there and picks up/drops. Hovering while
   pressed draws a white column outline. Clicking Yes/No works. Clicking near (445,302) in the
   game → confirm screen, near (35,302) → help.
7. map00 secure storage terminal (tile 29,27, facing W, needs v16==0): answer Yes → the puzzle.
   A win → dialog 73 and the door unlocks. A quit or loss → dialog 74, and the terminal can be retried.
8. Vending (V) still works unchanged.

## 8. Deviations (all recorded)
- RNG seeded by time (project-wide, §2).
- Touch resolution moved into the tick. The letterbox-release pass and the stale-`current`
  confirm hit are dropped (ADR 0038).
- `b1 != 0` (custom puzzle) is logged and played as standard instead of `app->Error`.
- `scrambles` clamped to ≥ 1 (unreachable with shipped data).
- Sounds are logged to stderr only (`[sound] id`), as in vending.

## 9. Sentry bot (type 0): later, what it will reuse
`MiniGameHost::evaluateResults` and the `endGame` shape (sound → evaluate → `finish`),
`MiniGameHelp` + `drawMiniGameHelp` (ids 171/172), `MiniGameButtons.h`, the `loadUiTexture`
path, and `ItemPickup::nextInt`. Not shared: the board, rules, draw, and hacking's raw
press-sample touch model (sentry uses buttons, HG §6). It gets its own `minigames/sentrybot/`
folder and a `case kSentryBot` in `MiniGameHost::start`.

## 10. Groups (delegate one at a time, in order)
| Group | Files | Done when |
|---|---|---|
| G1 | `ui/UiAssets.h/.cpp`, `vending/VendingAssets.cpp`, `hacking/HackingAssets.h/.cpp` | §4 acceptance |
| G2 | `hacking/HackingModel.h`, `hacking/HackingView.h/.cpp` | §5 acceptance |
| G3 | `hacking/HackingGame.h/.cpp` | §6 acceptance |
| G4 | `common/MiniGameHost.h/.cpp`, `core/GameContext.cpp`, `core/GameStates.h`, `core/UiInputCollector.cpp` | §7 acceptance (user check) |
