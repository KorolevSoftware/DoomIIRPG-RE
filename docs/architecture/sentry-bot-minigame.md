# Spec — Sentry bot minigame "Matrix Skip" (`new_src/minigames/sentrybot/`)

Date: 2026-10-30
Facts: `docs/original-code/sentry-bot-minigame.md` (cited below as **SB §n**), raw log
`docs/research/2026-10-30-sentry-bot.md`. Ground truth `src/SentryBotGame.cpp`.
Framework: ADR 0033, `new_src/minigames/common/*`. Pattern: `new_src/minigames/hacking/*`
(spec `hacking-minigame.md`). Touch rule: ADR 0038 (rule changes only in the tick).
Decision record: ADR 0040 (the reward without the familiar system).

## 1. Goal and what the user sees

Script `EV_MINIGAME type=0 (botType, -1, -1)` (map01 terminal, map02/03 ×2-3 with botType 0;
map04/05/06/08 with botType 1, SB §1) opens "Matrix Skip". First comes the shared help screen
("Matrix Skip", rules, "Exit" bottom right). FIRE or a tap opens the board: `matrixSkip_BG`,
a 4x4 grid of digits 1..6, a blinking cursor (it starts on a marker left of the grid),
"Hint: Skip Code: N" on top, a 4-slot guess bar, "Enter your code", "Tap number to pick a
number", "Back"/"Exit" labels, and the Delete (left) / Submit (right) buttons. After 4 digits
FIRE submits. A correct code shows "Code Accepted!" plus the bot choice (Shooting / Exploding,
wobbling cursor, info panel). FIRE picks the bot: the player gets the bot weapon (it becomes the
selected weapon, with a bobbing bot view sprite), HUD "+2 to I.Q.", and the script unlocks the
terminal. A wrong code shows "Code Denied!" / "Hack Failed!" (botType 1 adds a red WARNING box),
and any of FIRE / TAB / BACKSPACE returns to the world.

**Out of scope (ADR 0040):** the familiar system (deploying and controlling the bot, SB §8).
FIRE with a bot weapon logs `[familiar] deploy deferred` and does nothing.
Debug: **J** starts a botType 0 game, **Shift+J** a botType 1 game, with no script thread.

## 2. Reuse / no-change list

- `MiniGame`, `MiniGameHost::start/finish/evaluateResults`, `latchTouch`, the action queue:
  unchanged API (ADR 0033). `evaluateResults(n)` is exactly `evaluateMiniGameResults`
  (SB §6, `src/SentryBotGame.cpp:812`).
- Help: `MiniGameHelp` (`reset`, `scroll`, `buildModel(171, 172, m)`) + `drawMiniGameHelp`, as
  hacking does (`new_src/minigames/hacking/HackingGame.cpp` draw, `HackingView.cpp` `drawHelp`).
  Help background = `host_->assets().helpBackground`.
- Buttons: `common/MiniGameButtons.h` (`touchedButtonId`, `highlightedButtonId`, `touchRelease`).
- Texture loading: `loadUiTexture` (`new_src/ui/UiAssets.h:82`), no fix callback.
- RNG: `ItemPickup::nextInt()` (= `app->nextInt()`, SB §2), same project-wide decision as hacking.
- Reward: `Player::give(1, w, 1)` already ports the sentry-bot pre-step (ammo slot 7 = 100, clear
  bits 0x78, set bit w, `selectWeapon` because new) — `new_src/domain/game/Player.cpp:237-271`,
  legacy `src/Player.cpp:970-1006`. No change to `give`.
- Labels: `fontRenderMode(2)` dimming is a no-op (established in hacking spec §2); soft keys are
  never drawn in ST_MINI_GAME (`src/SoftKeyController.cpp:70-75`), so `setLeftSoftKey(0,30)` and
  `clearSoftKeys` are dropped.
- World branch only (ADR 0033 point 7): no `playFromMainMenu`, no `forceWin`, no `wasTouched`
  latch. `gamePlayedFromMainMenu` is always false here — in particular a `nullptr` thread
  (debug key) does NOT mean "main menu" (legacy derives it from the thread, `:46`).
- Sounds are logged to stderr only (`[sound] id`), as in hacking/vending.

## 3. Files

| File | Group | Kind |
|---|---|---|
| `new_src/domain/game/Player.h/.cpp`, `domain/game/PlayerState.cpp` | G1 | modify: `lastSkipCode` |
| `new_src/domain/game/ScriptVM.cpp` | G1 | modify: stale-v7 fix |
| `new_src/minigames/sentrybot/SentryBotAssets.h/.cpp` | G2 | new |
| `new_src/minigames/sentrybot/SentryBotModel.h` | G2 | new |
| `new_src/minigames/sentrybot/SentryBotView.h/.cpp` | G2 | new |
| `new_src/minigames/sentrybot/SentryBotGame.h/.cpp` | G3 | new |
| `new_src/minigames/common/MiniGameHost.h/.cpp` | G4 | modify |
| `new_src/core/GameStates.h`, `core/UiInputCollector.cpp`, `core/GameContext.cpp` | G4 | modify (debug J) |
| `new_src/core/PlayerActions.cpp` | G5 | modify: bot FIRE intercept |
| `new_src/ui/ViewWeapon.h/.cpp`, `core/GameContext.cpp` (`buildViewWeaponModel`) | G5 | modify: bot view sprite |

New files ⇒ re-run `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (GLOB) before building.
Layering (ADR 0033 point 1): `SentryBotModel.h` / `SentryBotView.*` include only `ui/`, `text/`,
`render/api/`, `minigames/common/MiniGameButtons.h`, `MiniGameHelpModel.h`, `MiniGameHelpView.h`
and their own assets header. `SentryBotGame.*` may use `domain/`, `io/`, `core/GameStates.h`.

## 4. G1 — `lastSkipCode` and the stale-v7 fix

### 4.1 `Player::lastSkipCode`
- `Player.h`: public `uint8_t lastSkipCode = 0;   // src/Player.h:70; SentryBotGame skip-code memory`
  (next to `chainsawStrengthBonusCount`).
- `Player::reset()`: `lastSkipCode = 0;` right after `chainsawStrengthBonusCount = 0;`
  (`new_src/domain/game/Player.cpp:141`, legacy `src/Player.cpp:520`).
- `PlayerState.cpp` save: `w.writeByte(p.lastSkipCode);` replacing `w.writeByte(0); // lastSkipCode`
  (`:81`, legacy `src/Player.cpp:1381`). Load: `tp.lastSkipCode = r.readByte();` replacing
  `r.readByte(); // lastSkipCode` (`:140`, legacy `src/Player.cpp:1298`). The byte position is
  unchanged, so old saves stay compatible.
- `docs/architecture/save-load.md` row is updated by the architect (done with this spec).

### 4.2 ScriptVM `EV_MINIGAME` stale result
`new_src/domain/game/ScriptVM.cpp:1380-1385`: in the `else` branch (start returned false), add
`vars[MiniGameHost::kResultVar] = 2;` before the log, and change the log to
`[script] MINIGAME type=%d not implemented, v7=2 (quit)`. Rationale: every caller's next op is
`EVAL v7 == 1` (or `v7 == 0` on botType 1 terminals); 2 = quit matches neither branch, so the
terminal stays retryable instead of acting on a stale win (SB §1, research "new_src state").
`MiniGameHost.h` is already reachable from ScriptVM via `env_.ctx->minigames()`; include it if the
constant is not visible (`minigames/common/MiniGameHost.h`).

**G1 acceptance:** builds; `grep -n lastSkipCode new_src/domain/game/*.cpp` shows reset, save,
load; a save/load round trip still works (Continue after a save).

## 5. G2 — assets, model, stateless view

### 5.1 `SentryBotAssets.h/.cpp`
```cpp
// Sheets only the sentry bot minigame draws (src/LoadingManager.cpp:88-92;
// matrixSkip_button.bmp is loaded in initGame, src/SentryBotGame.cpp:90-92).
struct SentryBotAssets {
	Texture background;   // matrixSkip_BG.bmp (imgMatrixSkip_BG)
	Texture game;         // sentryGame.bmp (imgGameAssets)
	Texture pressed[3];   // matrixSkip_grid_pressed.bmp, _2.bmp, _3.bmp (imgButton, imgButton2, imgButton3)
	Texture button;       // matrixSkip_button.bmp (legacy loads it 3x as imgSubmit/imgUnk1/imgDelete: one copy suffices)
	bool load(TextureStore& store, const UiAssets::ResourceReader& read);
};
```
`load`: `if (!read) return false;` then `ok &= loadUiTexture(...)` for the six files in the order
above (same shape as `HackingAssets::load`).

### 5.2 `SentryBotModel.h`
```cpp
// Per-frame view model, filled by SentryBotGame::draw. Pointers borrowed for one frame.
struct SentryBotModel {
	enum class Screen { Help, Board, Success, Failure };   // stateVars[1]==0 / [2] 0,1,2
	Screen screen = Screen::Help;

	MiniGameHelpModel help;            // Help
	const Text* exitCode = nullptr;    // Help: Code 30 at (445,302) anchor 3

	// Board (drawGameScreen, :489-727)
	uint8_t board[16] = {};            // 1..6
	int cursor = -1;                   // displayed player_cursor; -1 = start marker
	bool cursorVisible = true;         // blink applied (drawCursor b=true)
	int pressedCell = -1;              // touched over this cell: pressed-cell image
	const Text* digits[16] = {};       // number text per cell (session-owned)
	const Text* hint = nullptr;        // Code 173 + 174 + skipCode
	const Text* enterCode = nullptr;   // Code 175
	const Text* tapHint = nullptr;     // Code 176
	const Text* back = nullptr;        // literal "Back"
	const Text* exit = nullptr;        // literal "Exit"
	const Text* del = nullptr;         // literal "Delete"
	const Text* submit = nullptr;      // literal "Submit"
	uint8_t submitAlpha = 255, deleteAlpha = 255;   // Board and Success

	// Guess bar (drawPlayersGuess, :729-747) — Board, Success, Failure
	const Text* guess[4] = {};         // nullptr = slot empty (usersGuess 0)
	int guessY = 249;                  // 249 board, 26 result screens
	int answerCursor = -1;             // -1 = no guess cursor (result screens)

	// Success (script mode, :410-471) and Failure (:323-370)
	const Text* title = nullptr;       // 177 / 178 / 179 at (240,15)
	const Text* subtitle = nullptr;    // Failure: 240 at (240,71)
	const Text* select = nullptr;      // Success: 180 at (240,95)
	const Text* shooting = nullptr;    // 181 at (60,140)
	const Text* exploding = nullptr;   // 182 at (420,140)
	int selCursorX = 0;                // xPos + OSC wobble, y 140, anchor 10
	const Text* shootingHead = nullptr, *shootingBody = nullptr;     // "Shooting Bot:", 183 wrapped 50
	const Text* explodingHead = nullptr, *explodingBody = nullptr;   // "Exploding Bot:", 184 wrapped 50
	bool warning = false;              // Failure, botType 1: red box + Code 186
	const Text* warningLabel = nullptr;
	const Text* message = nullptr;     // Failure: 187 or 188 wrapped 22
	int messageY = 139;                // 139 (botType 0) / 209 (botType 1)

	MiniGameButton buttons[4];         // ids 0 Exit, 1 Back, 2 Submit, 3 Delete (list order)
	int buttonCount = 4;
};
```

### 5.3 `SentryBotView.h/.cpp`
`UiResult drawSentryBot(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art);` — draws the
screen, then `return touchRelease(m.buttons, m.buttonCount, ui.in());` (hacking shape).
Constants: `n = 240` (`screenRect` half width), `n2 = 50` (board top). Anchors: 3 = HCENTER|VCENTER,
20 = TOP|LEFT, 10 = RIGHT|VCENTER. Rotate modes = legacy `Image` modes (`src/Image.cpp:120-150`,
same numbering as `Graphics2D::drawRegion`: 2 = 180°, 4 = mirror X, 6 = 180°+mirror = flip Y,
8 = flip Y). All `ui.label` calls use the default line height.

Helpers (file-local):
- `drawCursor(ui, art, x, y, visible)`: if visible, `ui.imageRegion(art.game, 95, 91, 22, 22, x + 1, y, 3)`
  (`:749-754`).
- `drawGuess(ui, m, art, y)` (`:729-747`): `imageRegion(art.game, 0, 91, 95, 33, 145, y, 20, 0)` and
  `(…, 240, y, 20, 4)`. For i in 0..3: `x = 173 + 45*i`, `ty = y + 16`; if `m.guess[i]` →
  `ui.label(*m.guess[i], x, ty, 3)`; underline `ui.panel(UiRect{x - 6, ty + 8, 13, 1}, 0xFF6C834A)`
  (legacy `drawLine(x-6, ty+8, x+6, ty+8)` is endpoint-inclusive = 13 px); if `m.answerCursor == i`
  → `drawCursor(x, ty, true)`.
- `drawButton(ui, art, b, alpha)`: `ui.imageAlpha(art.button, b.touch.x, b.touch.y, alpha)`
  (fmButton::Render draws the image top-left at the touch rect, `src/Button.cpp:195-201`).

**Board** (`:489-727`), in this order:
1. `ui.image(art.background, 0, 0, 0)`.
2. Grid quarters, region (53,0,91,91), anchor 20: (149,50) rot 0; (240,50) rot 4; (149,141) rot 6;
   (240,141) rot 2.
3. If `m.cursor == -1`: `imageRegion(art.game, 144, 0, 41, 37, 120, 58, 20, 0)` (start marker).
4. For i in 0..15: `n3 = 240 + (i%4)*42`, `n4 = 50 + (i/4)*44`.
   - If `i == m.pressedCell`: pressed image `art.pressed[img]` (full texture via
     `imageRegion(tex, 0, 0, tex.width(), tex.height(), n3 + dx, n4 + dy, 20, rot)`) from this
     table (`:518-633`, img 0 = `_pressed`, 1 = `_2`, 2 = `_3`):

     | i | img | rot | dx | dy |   | i | img | rot | dx | dy |
     |---|---|---|---|---|---|---|---|---|---|---|
     | 0 | 1 | 4 | -84 | 6 | | 8 | 2 | 0 | -84 | 3 |
     | 1 | 0 | 0 | -85 | 6 | | 9 | 0 | 0 | -86 | 3 |
     | 2 | 0 | 0 | -84 | 6 | | 10 | 0 | 0 | -84 | 3 |
     | 3 | 1 | 0 | -84 | 6 | | 11 | 2 | 4 | -83 | 3 |
     | 4 | 2 | 6 | -84 | 4 | | 12 | 1 | 2 | -84 | 2 |
     | 5 | 0 | 0 | -86 | 4 | | 13 | 0 | 0 | -86 | 2 |
     | 6 | 0 | 0 | -84 | 4 | | 14 | 0 | 0 | -84 | 2 |
     | 7 | 2 | 2 | -83 | 4 | | 15 | 1 | 8 | -83 | 2 |
   - `ui.label(*m.digits[i], n3 - 63, n4 + 27, 3)`.
   - If `i == m.cursor`: `drawCursor(n3 - 63, n4 + 26, m.cursorVisible)`.
5. If `m.cursor == -1`: `drawCursor(138, 76, m.cursorVisible)`.
6. `drawGuess(y = 249)` with `m.answerCursor`.
7. Labels anchor 3: `hint` (240,30); `enterCode` (240,241); `tapHint` (240,294); `back` (35,310);
   `exit` (445,310).
8. `drawButton(buttons[2], submitAlpha)`, `drawButton(buttons[3], deleteAlpha)`; then `del` at the
   centre of button 3 and `submit` at the centre of button 2, anchor 3
   (`x = r.x + r.w/2`, `y = r.y + r.h/2`, `:703-726`).

**Help:** `drawMiniGameHelp(ui, m.help)`; `ui.label(*m.exitCode, 445, 302, 3)` (`:475-487`).

**Success** (`:373-473`, script mode): background; `drawGuess(26)` (no cursor); `title` (240,15);
`select` (240,95); `drawButton` 2 then 3 with their alphas; `shooting` (60,140), `exploding`
(420,140), anchor 3; `ui.glyph('\x8A', m.selCursorX, 140, 10)` (legacy `graphics->drawCursor(x,y,10)`
= the 3-arg overload, `b = true` → `'\x8A'`, `src/Graphics.cpp:670-686`; SB §4 says 0x84, which is the
`b=false` overload and not the one called); `ui.panel(UiRect{12, 170, 456, 140}, 0xFF464C43)`;
`shootingHead` (22,178), `shootingBody` (22,197), `explodingHead` (22,243), `explodingBody`
(22,262), all anchor 20.

**Failure** (`:323-370`): background; `drawGuess(26)`; `title` (240,15); `subtitle` (240,71); if
`warning`: panels `(187,113,107,52) 0xFF5C000B`, `(190,116,101,46) 0xFF343A33`,
`(200,126,80,26) 0xFF5C000B`, `(207,133,66,12) 0xFF343A33`, then `warningLabel` (240,111) anchor 3;
`message` (240, m.messageY) anchor 3.

**G2 acceptance:** builds (no caller yet); layering grep: no `domain/`, `core/`, `io/` include in
`SentryBotModel.h` / `SentryBotView.*`.

## 6. G3 — `SentryBotGame` session

### 6.1 Header
```cpp
// The sentry bot "Matrix Skip" session (src/SentryBotGame.cpp), world branch only.
class SentryBotGame : public MiniGame {
public:
	void init(MiniGameHost* host) { host_ = host; }
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);
	// EV_MINIGAME type 0: b1 = botType (src/ScriptThread.cpp:1290-1303); b2/b3 unused.
	void begin(int b1, int b2, int b3) override;   // initGame (:44-120)
	void handleAction(Action a) override;           // -> input(legacy id)
	void handleTouch(int buttonId) override;        // touchEnd (:839-892)
	UiResult draw(Ui& ui) override;                 // updateGame (:296-321)
private:
	// Legacy action ids (src/Enums.h:993-1012); 64/65 come only from touch.
	static constexpr int kUp = 1, kDown = 2, kLeft = 3, kRight = 4, kMenu = 5, kFire = 6,
		kAutomap = 7, kPassTurn = 14, kBack = 15, kMatrixLeft = 64, kMatrixRight = 65;
	static constexpr int kBoard = 0, kWon = 1, kLost = 2;   // stateVars[2]

	void generate();              // :53-81
	void input(int action);       // handleInput (:127-294)
	void commitTouch();           // ADR 0038: apply draw-time selections in the tick
	void award();                 // awardSentryBot stub (ADR 0040)
	void endGame(int n);          // :806-824 world branch

	MiniGameHost* host_ = nullptr;
	SentryBotAssets assets_;
	SentryBotModel model_;
	int botType_ = 0;
	int skipCode_ = 1;
	uint8_t board_[16] = {}, solution_[4] = {}, guess_[4] = {};
	int answerCursor_ = 0, playerCursor_ = -1, botCursor_ = 0;
	int64_t cursorMoveTime_ = 0;      // timeSinceLastCursorMove
	bool help_ = true;                // stateVars[1] == 0
	int result_ = kBoard;             // stateVars[2]
	// Touch samples (touchStart/Move/End, :826-843) and the draw-time
	// selections waiting for the tick (-1 = none).
	bool touched_ = false;
	int curX_ = 0, curY_ = 0;
	int touchCell_ = -1, touchBot_ = -1;
	int lastHighlight_ = -1;          // button highlight edge -> sound 1027
	Text exitCode_, back_, exit_, del_, submit_, hint_, enterCode_, tapHint_;
	Text digits_[16], guessText_[4];
	Text title_, subtitle_, select_, shooting_, exploding_;
	Text shootingHead_, shootingBody_, explodingHead_, explodingBody_, warning_, message_;
};
```
`failedEarly` is not stored: the J2ME early-fail is `#if 0` (`:195-203`), so it is always false and
the title is always 178 (SB §3).

### 6.2 `begin(b1, b2, b3)` (`:44-81`)
1. `botType_ = (b1 == 0 || b1 == 1) ? b1 : 0;` (`:48-51`).
2. `help_ = true; result_ = kBoard; host_->help().reset();` (setupGlobalData, `:33-42`).
3. `generate()`.
4. `answerCursor_ = 0; botCursor_ = 0; playerCursor_ = -1; guess_[0..3] = 0;
   cursorMoveTime_ = *host_->env().upTimeMs;` (`:75-81`).
5. `touched_ = false; curX_ = curY_ = 0; touchCell_ = touchBot_ = -1; lastHighlight_ = -1;`
6. Log `[sentrybot] begin botType=%d iq=%d skip=%d solution=%d%d%d%d` (the solution in the log is
   a debug aid, removable).

### 6.3 `generate()` — verbatim (`:53-74`, SB §2)
```
Player& p = *host_->env().player;
const int iq = p.ce.getIQPercent();
const int n4 = (10000*2 + (6-2)*(100-iq)*(100-iq)) / 10000;          // :56, iq 0 -> 6, >=51 -> 2
do skipCode_ = ItemPickup::nextInt() % n4 + 1; while (skipCode_ == p.lastSkipCode);   // :58-60
p.lastSkipCode = (uint8_t)skipCode_;
const bool allowSkip = iq < 50;                                       // :61 (menu term dropped)
do {
	for (int i = 0; i < 16; ++i) {                                    // :62-67
		board_[i] = (uint8_t)(ItemPickup::nextInt() % 6 + 1);
		if (board_[i] == skipCode_ && !allowSkip) --i;
	}
	board_[0] = (uint8_t)(skipCode_ % 6 + 1);                        // :68
} while (orbitAllSkip());                                             // DEVIATION D2, see below
int cur = -1;                                                         // :70-74
for (int j = 0; j < 4; ++j) {
	do cur = (cur + skipCode_) % 16; while (board_[cur] == skipCode_);
	solution_[j] = board_[cur];
}
```
`orbitAllSkip()` (file-local lambda or helper): true iff every index `(k*skipCode_ - 1) mod 16`
for k = 1..16 has `board_[idx] == skipCode_`. The legacy walk loops forever in that case (possible
only with `allowSkip` and an even skip code, e.g. skip 4 → cells 3/7/11/15 all 4, about 1/1296).
`shortcut: regenerate the board instead of hanging; unreachable for iq >= 50`.
Self-check under `#ifndef NDEBUG` (as `HackingGame::fillBoard`): `assert(board_[0] != skipCode_)`;
for j: `assert(solution_[j] != skipCode_)`; if `!allowSkip`, no cell equals `skipCode_`;
`assert(skipCode_ >= 1 && skipCode_ <= n4)`.

### 6.4 `handleAction(a)`
Map to the legacy id and call `input`: Forward→kUp, Back→kDown, TurnLeft→kLeft, TurnRight→kRight,
Menu→kMenu, Use→kFire, Automap→kAutomap, Passturn→kPassTurn, BackKey→kBack; anything else is ignored.

### 6.5 `input(action)` — port of `handleInput` (`:127-294`), SB §3
First `commitTouch()`. Then:
0. `if (result_ == kLost && (action == kAutomap || action == kBack || action == kFire)) { endGame(0); return; }`
   (`:130-133`; the `kWon && fromMainMenu` half is dead in the world branch).
1. `help_` (`:135-177`):
   - kMenu / kBack: `result_ == kBoard` → `endGame(2)`; `kWon` → `award(); endGame(1)`;
     `kLost` → `endGame(0)` (only kBoard is reachable; ported for shape, as hacking).
   - kFire: `help_ = false`.
   - kUp/kDown/kRight/kLeft: `host_->help().scroll(-1 / +1 / +16 / -16)`.
2. Board (`!help_ && result_ == kBoard`, `:179-269`):
   - kAutomap / kBack: `help_ = true; host_->help().reset();`
   - kMenu: `endGame(2)`.
   - kFire: if `answerCursor_ < 4` { if `playerCursor_ > -1` { `guess_[answerCursor_] = board_[playerCursor_]; ++answerCursor_;` } }
     else { `result_ = (all 4 guess_[i] == solution_[i]) ? kWon : kLost;` log
     `[sentrybot] submit %d%d%d%d -> %s` }. (`playerHasWon`, `:756-763`, order matters.)
   - kLeft: if `playerCursor_ > -1` { `playerCursor_ = (playerCursor_ + 15) % 16; cursorMoveTime_ = now;` log `[sound] 1027` }.
   - kRight: `playerCursor_ = (playerCursor_ + 1) % 16; cursorMoveTime_ = now;` log `[sound] 1027`
     (from -1 it lands on 0).
   - kPassTurn / kMatrixLeft (Delete, `:245-258`): if `answerCursor_ > 0` { if `answerCursor_ <= 3`
     `guess_[answerCursor_] = 0;` `--answerCursor_; guess_[answerCursor_] = 0;` }.
   - kMatrixRight (`:259-268`): if `playerCursor_ > -1 && answerCursor_ <= 3` {
     `guess_[answerCursor_] = board_[playerCursor_]; ++answerCursor_;` }.
   - `playerCursor_` is never reset after an entry (`:210,253,265`).
3. Success (`!help_ && result_ == kWon`, `:271-292`): kFire → `award(); endGame(1);`
   kLeft / kRight → `botCursor_ = (botCursor_ + 1) % 2;`. kMenu/kBack/kAutomap do nothing
   (faithful: the player must pick a bot).
4. Failure: only rule 0.

### 6.6 Touch (ADR 0038 applied)
Legacy sets `player_cursor` (cell under the held press, `:509-515`) and `bot_selection_cursor`
(highlighted button 3 → 0, button 2 → 1, `:424-426,438-440`) inside drawing. Here `draw` only
records them in `touchCell_` / `touchBot_`; `commitTouch()` (called first by `input` and by
`handleTouch`) applies them:
```
if (touchCell_ != -1) { playerCursor_ = touchCell_; touchCell_ = -1; }   // no blink reset (legacy)
if (touchBot_  != -1) { botCursor_   = touchBot_;  touchBot_  = -1; }
```
The value the legacy would have stored during the frames before the release is therefore in place
before the release is handled; the release over a cell enters that cell (MATRIX_RIGHT).

`handleTouch(buttonId)` = `commitTouch()` then the `touchEnd` switch (`:839-892`):
- `help_`: `buttonId == 0` → `input(kMenu)`, else `input(kFire)`.
- `result_ == kBoard`: 0 → kMenu; 1 → kBack; 2 → kFire only if `answerCursor_ > 3`;
  3 → kMatrixLeft; anything else (-1) → kMatrixRight.
- `kWon`: `input(kFire)` (the `bot_selection_cursor != -1` test is always true).
- `kLost`: `input(kMenu)` — **faithful quirk D3**: rule 0 ignores MENU, so a tap on the failure
  screen does nothing; FIRE (Enter), AUTOMAP (Tab) or BACK (Backspace) leave it. Esc does nothing
  there either (legacy MENU). Recorded, not fixed.

### 6.7 `draw(ui)` → model
1. Touch samples (`:826-843`): if `in.cursorValid` and `in.pressed` → `touched_ = true`, cur = pos;
   else if `touched_ && in.down && in.cursorValid` → cur = pos. Do not clear `touchCell_` /
   `touchBot_` here: only `commitTouch()` (the tick) consumes them. The release (`in.released`) is applied
   **after** step 4, so a click whose press and release land in one frame still selects its cell
   (the release position counts as the last held position).
2. Static texts: compose once per frame like hacking (`setLength(0)` + `compose`): `exitCode_` =
   Code 30; literals `back_` "Back", `exit_` "Exit", `del_` "Delete", `submit_` "Submit",
   `shootingHead_` "Shooting Bot:", `explodingHead_` "Exploding Bot:" (literal in the original,
   `:651,661,712,722,461,471`). Buttons: `kButtons[4]` = `{0, {428,290,54,30}}`,
   `{1, {0,290,54,30}}`, `{2, {368, 142 - bh/2, bw, bh}}`, `{3, {8, 142 - bh/2, bw, bh}}` with
   `bw/bh = assets_.button.width()/height()` (`:101-119`).
3. Highlight: `hl = highlightedButtonId(buttons, 4, in)`; `if (hl != -1 && hl != lastHighlight_)`
   log `[sound] 1027`; `lastHighlight_ = hl` (fmButton highlight sound, as hacking).
4. Screen:
   - Help: `buildModel(171, 172, model_.help)`, `help.background = &host_->assets().helpBackground`.
   - Board: if `touched_` and cell rect `UiRect{n3 - 84, n4 + 6, 42, 42}` (half-open,
     `pointInRectangle`, `src/Utils.cpp:52-54`) contains cur for some i (first match): `pressedCell = i`;
     `shown = touchCell_ != -1 ? touchCell_ : playerCursor_`; if `i != shown` log `[sound] 1027`
     (soundClick, `:512`); `touchCell_ = i`. `model_.cursor = touchCell_ != -1 ? touchCell_ : playerCursor_`.
     `cursorVisible = ((now - cursorMoveTime_) & 0x200) == 0` (`:751`). Digits: `digits_[i]` =
     `append((int)board_[i])`. `hint_` = compose 173, compose 174, `append(skipCode_)`, `dehyphenate()`;
     `enterCode_` 175, `tapHint_` 176 (each dehyphenated). Alphas (`:685-701`, render mode 1 =
     `glColor4f(1,1,1,.25)` modulate, `src/Image.cpp:172-175` → alpha 64; mode 0 → 255;
     highlighted → highlightRenderMode 0 → 255): `submitAlpha = (hl == 2) ? 255 : (answerCursor_ > 3 ? 64 : 255)`;
     `deleteAlpha = (hl == 3) ? 255 : (answerCursor_ > 0 ? 64 : 255)` — **faithful quirk D4**: the
     "active" button is the faded one. `guessY = 249`, `answerCursor = answerCursor_`.
   - Success: `if (hl == 3) touchBot_ = 0; else if (hl == 2) touchBot_ = 1;` (`:424-426,438-440`);
     `shownBot = touchBot_ != -1 ? touchBot_ : botCursor_`. `title_` 177; `select_` 180; `shooting_`
     181; `exploding_` 182 (dehyphenated). `w = label.getStringWidth()` of the selected label;
     `xPos = shownBot == 0 ? 60 - w/2 - 5 : 420 - w/2 - 5`; `selCursorX = xPos + osc` with
     `osc = tables->oscCycle[(now / 100) % 4]` (guarded on size ≥ 4, as hacking). Bodies 183 / 184:
     dehyphenate, `wrapText(50, '\n')`. Button alphas as on the board formula (answerCursor_ is 4
     here, so 64 unless highlighted). `guessY = 26`, `answerCursor = -1`.
   - Failure: `title_` 178; `subtitle_` 240; `warning = botType_ == 1`, `warning_` 186;
     `message_` = `botType_ == 1 ? 187 : 188`, `wrapText(22, '\n')`; `messageY = botType_ == 1 ? 209 : 139`
     (`:340-367`). `guessY = 26`, `answerCursor = -1`.
   - Guess texts (all non-help screens): `guessText_[i]` = `append((int)guess_[i])` when non-zero,
     `model_.guess[i] = guess_[i] ? &guessText_[i] : nullptr`.
5. `if (in.released) touched_ = false;` (also when `!cursorValid`).
6. `return drawSentryBot(ui, model_, assets_);`

`draw` changes no rules state: only samples, `touchCell_`/`touchBot_` (applied by the tick), and
sound logs (ADR 0038 point 4).

### 6.8 `award()` — stub of `awardSentryBot` (`:784-804`), ADR 0040
```
const int w = botType_ == 0 ? (botCursor_ == 0 ? 3 : 4) : (botCursor_ == 0 ? 5 : 6);   // :276-282
Player& p = *host_->env().player;
if (p.hasASentryBot())
	std::fprintf(stderr, "[familiar] discard of owned bot deferred (give evicts it), help 16 skipped\n");
	// shortcut: attemptToDiscardFamiliar drops the old bot as a world item and showHelp(16)
	// explains it (src/Player.cpp:2427-2437); give() below just evicts the bit. Upgrade with
	// the familiar system.
p.give(1, w, 1);   // ammo[7] = 100, bits 0x78 cleared, bit w set, selectWeapon(w)
std::fprintf(stderr, "[sentrybot] awarded bot weapon %d\n", w);
```
`showWeaponHelp(w)` inside `give` is not ported (existing `give` comment), nothing to add.

### 6.9 `endGame(n)` (`:806-824`)
`log [sound] (n == 1 ? 1043 : 1040)`; `host_->evaluateResults(n)`; `host_->finish(n)`.
(`award` runs before `endGame(1)`, legacy order.)

**G3 acceptance:** builds; with a temporary call from G4 the stderr log of `begin` shows a
skip code in 1..n4 different from the previous game's.

## 7. G4 — host and debug wiring

### 7.1 `MiniGameHost`
- Include `minigames/sentrybot/SentryBotGame.h`; member `SentryBotGame sentry_;`.
- `init`: `sentry_.init(this);`. `loadAssets`: `& sentry_.loadAssets(store, read)`.
- `start`: `case kSentryBot: g = &sentry_; break;`; default comment → `// unknown type`.
- `SentryBotGame::loadAssets` logs `[sentrybot] button %dx%d` (the button size drives two rects).

### 7.2 Debug key
- `core/GameStates.h` after `DebugHacking`:
  `// DEBUG (removable, minigames): J / Shift+J start the sentry bot game, botType 0 / 1.`
  `DebugSentryBot, DebugSentryBotRed,`
- `core/UiInputCollector.cpp` after H:
  `case SDL_SCANCODE_J: a = (ev.key.keysym.mod & KMOD_SHIFT) ? Action::DebugSentryBotRed : Action::DebugSentryBot; break; // DEBUG (removable, minigames)`.
  J is free (in use: A B C D H I K N O S V W X Z, arrows, RETURN KP_ENTER ESCAPE TAB BACKSPACE F12).
- `GameContext::tick` next to `DebugHacking` (`new_src/core/GameContext.cpp:358-362`):
  `if (a == Action::DebugSentryBot || a == Action::DebugSentryBotRed) { minigames_.start(MiniGameHost::kSentryBot, a == Action::DebugSentryBotRed ? 1 : 0, -1, -1, nullptr); continue; }`

**G4 acceptance (user screen test, after reconfigure + build):**
1. In the world press **J**: help screen "Matrix Skip" with the rules text and "Exit" bottom right.
   UP/DOWN scroll if long. Esc here → back to the world, nothing granted (`[sound] 1040`).
2. Press J, then ENTER: green/grey 4x4 grid of digits 1..6, a blinking cursor (≈1 s period) on a
   marker left of the grid, "Hint: Skip Code: N" on top, the guess bar with 4 underlines and a
   steady cursor on slot 1, "Enter your code", "Tap number to pick a number", "Back"/"Exit" at the
   bottom, the Delete button on the left and Submit on the right (both opaque).
3. RIGHT moves the cursor onto cell 0 and then along the row-major order, wrapping 15 → 0; LEFT
   goes back (LEFT does nothing while on the start marker). Each move clicks (`[sound] 1027`).
4. ENTER writes the digit under the cursor into the next guess slot; the Delete button becomes
   faded (D4). C (pass turn) deletes the last digit. After 4 digits the Submit button is faded too.
5. Solve: start at index -1, step N cells (row-major, wrapping at 16) skipping cells equal to N, take
   4 digits (the stderr `begin` line shows the expected solution). ENTER → "Code Accepted!", the
   4 digits on top, "Select a bot type", "Shooting" left / "Exploding" right with a wobbling cursor,
   grey info panel with both descriptions. LEFT/RIGHT toggle. Esc/Tab/Backspace do nothing here.
   ENTER → world: HUD "Hack Successful! +2 to I.Q." (or "I.Q. is at Max."), stderr
   `[sentrybot] awarded bot weapon 3` (or 4), `[sound] 1043`; the bot is now the selected weapon.
6. Wrong code → "Code Denied!", "Hack Failed!", "You are being logged out. Good day." (J) or the red
   WARNING box + "Sentry Bot initiating defensive action." (Shift+J). Clicking does nothing (D3);
   ENTER/Tab/Backspace → world, `[sound] 1040`.
7. Mouse: pressing on a cell shows the pressed-cell art under the finger and moves the cursor
   there; releasing enters that digit. Clicking Delete deletes; clicking Submit with 4 digits
   submits (with fewer it does nothing). "Exit" (bottom right) quits, "Back" (bottom left) → help.
   On the help screen any click outside "Exit" starts the board. On "Code Accepted!" pressing the
   right button area selects Exploding, the left Shooting, and the release picks the bot.
8. Two J games in a row never show the same skip code twice in a row (`lastSkipCode`).
9. **map01 terminal** (N once from map00 → map01; B shows coordinates): stand on tile **(24,13)**,
   face **north**, press ENTER (event 44, `v16 == 0 && v57 == 0`, IP 6154). Sound 1065, then the
   game. A win → sprites 199-201 (the terminal's lock) disappear, event 36 is disabled and the
   terminal no longer offers the game (`v57 = 1`). Quit (Esc on help/board) → nothing changes, the
   terminal can be retried. Loss → nothing changes either (botType 0 acts only on `v7 == 1`).
10. H (hacking) and V (vending) still work.

## 8. G5 — bot weapon safety (FIRE and view sprite)

### 8.1 FIRE intercept (`src/PlayingInputHandler.cpp:493-495`)
`new_src/core/PlayerActions.cpp`, in the fire block, at the top of the `else` branch that follows
`if (outcome == kWallPush) { ... }` — i.e. after the wall push and **before** the zoom entry, the
legacy order (`:467` push, `:493` bot, `:506` zoom):
```cpp
// Bot weapons do not attack: legacy attemptToDeploySentryBot (:493-495), whose
// familiar system is not ported (ADR 0040).
if (((1 << weapon2) & Enums::WP_SENTRY_BOT_MASK) != 0) {
	std::fprintf(stderr, "[familiar] deploy deferred\n");
	break;
}
```
No turn passes, no attack sequence starts (the legacy returns `true` without `advanceTurn`).
(Verify `break` leaves the same enclosing `switch` that `if (zoomed) break;` already uses there.)

### 8.2 View sprite (`src/Combat.cpp:797-813`)
`ViewWeaponModel` (`new_src/ui/ViewWeapon.h`) gains:
```cpp
// Sentry-bot weapons 3..6 (src/Combat.cpp:797-813): tile 19 (w 3/4) or 18 (w 5/6),
// frame 2 then frame 3 at +botDy, frame 3 mirrored on X when botMirror. -1 = not a bot.
int botTile = -1;
bool botMirror = false;
int botDy = 0;
```
Producer `GameContext::buildViewWeaponModel` (`new_src/core/GameContext.cpp:969-971`), after
`m.weaponTile = ...`:
```cpp
if (w >= 3 && w <= 6) {                                    // weaponIsASentryBot
	m.botTile = (w == 3 || w == 4) ? 19 : 18;
	const int t = (int)((upTimeMs + m.botTile * 1337) / 1024) & 0xF;   // app->time
	m.botMirror = t == 0 || (t >= 4 && t <= 7) || (t >= 10 && t <= 12);  // flags ^= 0x20000
	m.botDy = (t & 0x2) ? 1 : 2;
}
```
0x20000 is the horizontal mirror on the GL path: `Render::draw2DSprite` passes `flags ^ 0x20000`
(`src/Render.cpp:415`) and `DrawWorldSpaceSpriteLine` mirrors when `(flags ^ 0x60000) & 0x20000`
(`src/GLES.cpp:520`), so the net mirror is set exactly when the caller's flags have 0x20000.

View `ViewWeapon::draw` (`new_src/ui/ViewWeapon.cpp`): `drawWeaponQuad` gains a trailing
`bool mirror = false`. When mirrored, the source columns for the visible span are taken from the
other side — `srcX0' = kSrc - sx1`, `srcX1' = kSrc - sx0` — and `g.drawImage(..., rotateMode 4, ...)`
(mirror within the 176-texel art box, which is what the GL quad's swapped texcoords do). Then,
replacing the "Sentry-bot stack deferred" comment, before the underlay/weapon art:
```cpp
if (m.botTile >= 0) {   // (:797-813) replaces the gun art; no flash (mask 0x181)
	const Texture* f2 = env_.world->spriteTexture(*env_.media, m.botTile, 2);
	const Texture* f3 = env_.world->spriteTexture(*env_.media, m.botTile, 3);
	if (f2) drawWeaponQuad(g, *f2, x, y, 176, 255, gb, gb, magX, magY);
	if (f3) drawWeaponQuad(g, *f3, x, y + m.botDy, 176, 255, gb, gb, magX, magY, m.botMirror);
	return;
}
```
(`gb` = the existing bright-red tint; the legacy passes the same `renderFlags` to both frames.)
The existing per-weapon `scrY += 12` for weapons 3..6 (`ViewWeapon.cpp`) already matches `:679-693`.

**G5 acceptance (user screen test):** after winning a J game (Shooting): the bottom of the view
shows the bot sprite (two layers: body and a top part that bobs 1-2 px and flips left/right every
few seconds), not a gun. ENTER facing open space or a monster: no shot, no turn passes, stderr
`[familiar] deploy deferred`; facing a wall within one tile: the usual wall push. Z/X or the
weapon keys still switch to other weapons. Shift+J win + Exploding: same with the red bot (tile 18).
No crash on save/load with a bot weapon selected.

## 9. Deviations (all recorded)
- D1 World branch only: no main-menu mode, `forceWin`, `wasTouched`; a `nullptr` thread is the
  debug launch, not the main menu.
- D2 Board regenerated when the solution walk would never find a non-skip cell (legacy hang).
- D3 (faithful, kept) Touch on the failure screen does nothing (ACTION_MENU is unhandled there);
  keys FIRE/AUTOMAP/BACK leave it.
- D4 (faithful, kept) Submit/Delete are drawn at 25 % alpha when active and opaque when inactive.
- D5 Draw-time cell and bot selection are applied lazily in the tick (ADR 0038); a one-frame
  double cursor the legacy can draw on the first hover frame is not reproduced.
- D6 A press and release inside one frame still selects the cell (release counted as held).
- D7 Reward stub (ADR 0040): no `attemptToDiscardFamiliar`, no `showHelp(16)`, no deploy.
- D8 Sounds logged only. RNG seeded by time (project-wide).
- Research correction: the success-screen cursor glyph is `'\x8A'`, not 0x84 (§5.3).

## 10. Groups (delegate one at a time, in order)
| Group | Files | Done when |
|---|---|---|
| G1 | `domain/game/Player.h/.cpp`, `domain/game/PlayerState.cpp`, `domain/game/ScriptVM.cpp` | §4 acceptance |
| G2 | `minigames/sentrybot/SentryBotAssets.h/.cpp`, `SentryBotModel.h`, `SentryBotView.h/.cpp` | §5 acceptance |
| G3 | `minigames/sentrybot/SentryBotGame.h/.cpp` | §6 acceptance (builds) |
| G4 | `minigames/common/MiniGameHost.h/.cpp`, `core/GameStates.h`, `core/UiInputCollector.cpp`, `core/GameContext.cpp` (tick) | §7 user screen test |
| G5 | `core/PlayerActions.cpp`, `ui/ViewWeapon.h/.cpp`, `core/GameContext.cpp` (`buildViewWeaponModel`) | §8 user screen test |
