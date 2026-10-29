# Spec 2026-10-29 — Minigames: folder layout, common interface, `ST_MINI_GAME`, vending machine (G1-G6)

ADR: `adr/0033-minigames-folder-and-interface.md`.
Facts used: `docs/original-code/vending-machine.md` (VM), `docs/original-code/ui.md` §17b (menus
83-87), `docs/research/2026-10-29-vending-menus.md`, `docs/original-code/tile-events-vm.md:219`.
Namespace `newcore`, as everywhere else. Single-threaded: everything below runs on the GL loop
thread. Ticks go through `GameContext::tick`, drawing through `GameContext::render`.

## 0. Scope

- Framework: `StateId::MiniGame`, `MiniGame` interface, `MiniGameHost`, EV_MINIGAME start + script
  resume, touch routing, shared help screen, asset ownership.
- Vending machine, complete: terminal, help, hack game, buy menus 83-87 with art.
- Hacking (type 2) and sentry bot (type 0) are placeholders only (§9). With this spec,
  `EV_MINIGAME` for those types keeps logging and does not park the thread (current behaviour).
- Out of scope: `playFromMainMenu`, every `gamePlayedFromMainMenu` branch (there is no main menu),
  `forceWin` (`skipMinigames` is only ever false, `src/Game.cpp:62`), save/load of the vending
  fields (no save system yet, they are only Player members), and menu 88 (unreachable,
  ui.md §17b.9).

## 1. Folder layout

```
new_src/minigames/
  common/
    MiniGame.h              interface (§2)                      [session role]
    MiniGameHost.h/.cpp     dispatcher, thread, finish (§3)     [session role]
    MiniGameButtons.h       GetTouchedButtonID/Highlight port   [view role, header-only]
    MiniGameAssets.h/.cpp   shared sheet: gameHelpBG.bmp        [asset role]
    MiniGameHelp.h/.cpp     shared help scroll + model build    [session role]
    MiniGameHelpModel.h     help view model                     [view role]
    MiniGameHelpView.h/.cpp stateless help view                 [view role]
  vending/
    VendingMachine.h/.cpp   session: rules, input, touch, menu API
    VendingModel.h          view model (terminal / game)
    VendingView.h/.cpp      stateless views
    VendingAssets.h/.cpp    minigame-only sheets
  hacking/   (later, §9)
  sentrybot/ (later, §9)
```

Include rules per role (ADR 0033 point 1):
- **view role** (`*View.*`, `*Model.h`, `MiniGameButtons.h`): only `ui/`, `text/`, `render/api/`,
  own model. Must not include anything from `domain/`, `core/`, `io/`, or another minigame's session.
- **session role**: may include `domain/`, `io/`, `text/`, `ui/`, `core/GameStates.h`, own
  view/model/assets, `minigames/common/*`. Must not include `core/GameContext.h` or
  `core/MenuSession.h`.
- **asset role**: `render/api/`, `ui/UiAssets.h` (for `loadUiTexture` + `ResourceReader`).

Include paths are rooted at `new_src` (existing convention): `#include "minigames/vending/VendingMachine.h"`.

**CMake:** `new_src/CMakeLists.txt` uses `GLOB_RECURSE` over `*.h`/`*.cpp` (lines 13-19). The new
folder is picked up with no edit to the file, but **every group that adds files needs a
reconfigure**: `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`, then
`cmake --build build_new -j 8`. The `list(FILTER ... EXCLUDE REGEX "/render/(api|sokol)/")` lines
do not match `minigames/`, so it lands in the executable target. Nothing else to change.

## 2. The common interface — `minigames/common/MiniGame.h`

```cpp
#include "core/GameStates.h"   // Action
#include "ui/UiTypes.h"        // UiResult

namespace newcore {
class Ui;

// The lifecycle all three legacy minigames share (ADR 0033 point 2).
class MiniGame {
public:
	virtual ~MiniGame() = default;
	// initGame. Called by MiniGameHost::start AFTER it latched the calling
	// thread and switched to StateId::MiniGame. b1..b3 are the raw signed
	// EV_MINIGAME byte args 2..4 (src/ScriptThread.cpp:1292-1295); each game
	// derives its own initGame parameters from them.
	virtual void begin(int b1, int b2, int b3) = 0;
	// handleInput(action): every queued key action while StateId::MiniGame.
	virtual void handleAction(Action a) = 0;
	// touchEnd after GetTouchedButtonID (src/Button.cpp:277-287): the legacy
	// button id released on, -1 = released on no button.
	virtual void handleTouch(int buttonId) = 0;
	// updateGame. Session side: may advance draw-time clocks and read ui.in()
	// (drags), then builds its model and calls its stateless view. Returns the
	// view's intent (UiAction::MiniGameTouch or None).
	virtual UiResult draw(Ui& ui) = 0;
};
}
```

Legacy action ↔ rewrite `Action` mapping that every game uses (the dialog precedent,
`new_src/core/GameStates.h:23-26`; legacy values `src/Enums.h:993-1005`): UP=`Forward`,
DOWN=`Back`, LEFT=`TurnLeft`, RIGHT=`TurnRight`, FIRE=`Use`, MENU=`Menu`, AUTOMAP=`Automap`,
BACK=`BackKey`. Every other action is ignored.

## 3. `MiniGameHost` — `minigames/common/MiniGameHost.h/.cpp`

```cpp
#include <cstdint>
#include <functional>
#include "core/GameStates.h"
#include "minigames/common/MiniGameAssets.h"
#include "minigames/common/MiniGameHelp.h"
#include "minigames/vending/VendingMachine.h"
#include "ui/UiAssets.h"   // ResourceReader
#include "ui/UiTypes.h"

namespace newcore {
class EntityDefs; class Game; class Localization; class Player; class ScriptVM;
class Tables; class TextureStore; class Ui; struct ScriptThread;

class MiniGameHost {
public:
	// EV_MINIGAME byte 1 == legacy stateVars[0] selector
	// (src/ScriptThread.cpp:1296-1347, src/Canvas.cpp:451-465).
	static constexpr int kSentryBot = 0, kHacking = 2, kVending = 4;
	// Script result slot written by endGame (src/VendingMachine.cpp:853).
	static constexpr int kResultVar = 7;

	struct Env {
		ScriptVM* vm = nullptr;
		StateHost* host = nullptr;
		Game* game = nullptr;               // combat.loadMapID (vending machine id)
		Player* player = nullptr;
		const Tables* tables = nullptr;     // energyDrinkData (table 10), oscCycle
		const Localization* loc = nullptr;
		const EntityDefs* defs = nullptr;
		const int64_t* upTimeMs = nullptr;  // legacy app->time
		std::function<void(int)> openMenu;  // GameContext: nextMenuId_ = id; setState(Menu)
	};
	void init(const Env& env);              // also inits help_ and every game
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);

	// EV_MINIGAME. False = type not implemented: nothing changed, the VM must
	// not park the thread. True = state is MiniGame and the game has begun.
	bool start(int type, int b1, int b2, int b3, ScriptThread* t);

	void latchTouch(int buttonId) { touchButton_ = buttonId; }
	void handleAction(Action a);    // MiniGameTouch -> active_->handleTouch(touchButton_), else handleAction(a)
	UiResult draw(Ui& ui);          // active_ ? active_->draw(ui) : UiResult{}

	// The shared endGame tail (src/VendingMachine.cpp:850-864,
	// src/HackingGame.cpp:619-636, src/SentryBotGame.cpp:806-824), world
	// branch only: vm->vars[7] = result; requestState(Playing);
	// vm->resumeThread(t); clear thread + active. Order is legacy (the
	// resumed script sees ST_PLAYING).
	void finish(int result);

	const Env& env() const { return env_; }
	MiniGameHelp& help() { return help_; }
	const MiniGameAssets& assets() const { return assets_; }
	VendingMachine& vending() { return vending_; }   // menu hooks (§7)

private:
	Env env_;
	ScriptThread* callingThread_ = nullptr;
	MiniGame* active_ = nullptr;
	int touchButton_ = -1;
	MiniGameAssets assets_;
	MiniGameHelp help_;
	VendingMachine vending_;
};
}
```

`start` algorithm:
```
switch (type) { case kVending: g = &vending_; break; default: return false; }
callingThread_ = t; active_ = g; touchButton_ = -1;
env_.host->requestState(StateId::MiniGame);
g->begin(b1, b2, b3);
return true;
```
`finish(result)`: copy `t = callingThread_`, null out `callingThread_` and `active_`, then
`env_.vm->vars[kResultVar] = (short)result; env_.host->requestState(StateId::Playing);
if (t) env_.vm->resumeThread(t);`. `resumeThread` already ignores freed threads
(`new_src/domain/game/ScriptVM.cpp:216-219`). We clear before resuming because the resumed script
may start another minigame synchronously.

The calling thread and `active_` **survive `StateId::Menu`**: the vending terminal → menu 83 →
back round trip (§7) relies on this. Only `finish` clears them.

## 4. State machine and ScriptVM integration (all G1)

### 4.1 `core/GameStates.h`
- `StateId::MiniGame = 12` (legacy `ST_MINI_GAME`, `src/Canvas.h:79`).
- `Action::MiniGameTouch`, appended at the end. Comment: no key produces it; it is the touch
  release latched through `MiniGameHost::latchTouch` (the `ListRow` idiom).

### 4.2 `ui/UiTypes.h`
- `UiAction::MiniGameTouch`, appended. `index` = legacy button id or -1.
- No new `UiId` (ADR 0033, rejected alternative 5).

### 4.3 `core/GameContext.h/.cpp`
- `#include "minigames/common/MiniGameHost.h"`; member `MiniGameHost minigames_;`; accessor
  `MiniGameHost& minigames() { return minigames_; }` next to `cinematic()`/`actions()`.
- `init`: fill `MiniGameHost::Env` (`vm, this, game, player, tables, loc, defs, &upTimeMs`) and
  `openMenu = [this](int id) { nextMenuId_ = id; setState(StateId::Menu); }` (the same lambda
  as `actEnv.openMenu`, `new_src/core/GameContext.cpp:93`).
- `enterState_`: `case StateId::MiniGame: pendingActions_.clear(); break;`. Legacy entry from
  `initGame` clears the soft keys only. Clearing the queue is the rewrite's "events" analog used
  for Dialog/Menu, so keys pressed in the world never leak into the minigame.
- `tick()` input loop: insert right after the `Menu` line, before the blocked-drop check:
  `if (state_ == StateId::MiniGame) { minigames_.handleAction(a); continue; }`. Legacy dispatches
  ST_MINI_GAME at `src/InputEventController.cpp:334-349`, which is before the block test at `:449`.
- `tick()` per-state switch: `case StateId::MiniGame: break;` with a comment: nothing ticks
  (`src/Canvas.cpp:827-828` excludes ST_MINI_GAME); no scripts (Playing/Camera only); `gameTime`
  is frozen (clock gate unchanged); animations run on `upTimeMs` inside `draw`.
- `render()`: add after the Menu block and before `ui->endFrame()`:
  ```
  if (state_ == StateId::MiniGame && ui != nullptr) {
      const UiResult r = minigames_.draw(*ui);
      applyUiAction(r.action, r.index);
  }
  ```
  `gameplayView` does not list MiniGame, so there is no HUD and no view weapon. The world pass
  above still runs. Every minigame screen starts with an opaque 480x320 blit (VM §4; hacking
  `src/HackingGame.cpp:380`, sentry `src/SentryBotGame.cpp:495`), so it is covered.
- `applyUiAction`: `case UiAction::MiniGameTouch: minigames_.latchTouch(index); queued = Action::MiniGameTouch; break;`
- `main()` (`core/Main.cpp`), right after `ctx.init(...)`:
  `ctx.minigames().loadAssets(app.renderer().textures(), <same reader lambda as uiAssets>);` (G2).

### 4.4 `domain/game/ScriptVM.cpp` — `EV_MINIGAME` (replace the stub at :1355-1360)
```
case Enums::EV_MINIGAME: {
	int type = readByte(t), b1 = readByte(t), b2 = readByte(t), b3 = readByte(t);
	if (env_.ctx->minigames().start(type, b1, b2, b3, t)) {   // src/ScriptThread.cpp:1296-1347
		t->unpauseTime = -1;
		n = 2;
	} else {
		std::fprintf(stderr, "[script] MINIGAME type=%d not implemented\n", type);
	}
	break;
}
```
Leave `writeVar7` alone: the legacy `b` stays true for EV_MINIGAME, so the generic tail writes
`vars[7] = n2 (0)` and the IP advances before the loop exits (VM §1, `src/ScriptThread.cpp:2040-2043`;
rewrite tail `new_src/domain/game/ScriptVM.cpp:1530`). The parked thread resumes at the next opcode
when `finish` calls `resumeThread`. `EV_ENDMINIGAME` (54) stays a no-op ("No implementado",
`src/ScriptThread.cpp:1351-1354`).

## 5. Shared pieces (G2)

### 5.1 `MiniGameButtons.h` (header-only, view role)
```cpp
struct MiniGameButton { int id; UiRect touch; bool enabled = true; };  // fmButton drawButton
// fmButtonContainer::GetTouchedButtonID (src/Button.cpp:277-287): first enabled button in
// list order with x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h (INCLUSIVE), else -1.
inline int touchedButtonId(const MiniGameButton* b, int n, int x, int y);
// HighlightButton(x, y, true) analog: the button touchedButtonId would return while the pointer is down.
inline int highlightedButtonId(const MiniGameButton* b, int n, const UiInput& in);  // -1 unless in.down && in.cursorValid
// Release edge on the canvas -> {MiniGameTouch, touchedButtonId(...)}; else {}.
inline UiResult touchRelease(const MiniGameButton* b, int n, const UiInput& in);
```
`touchRelease` requires `in.released && in.cursorValid`. A release in the letterbox bars is
dropped, as `UiInput::cursorValid` documents.

### 5.2 `MiniGameAssets.h/.cpp`
`struct MiniGameAssets { Texture helpBackground; bool load(TextureStore&, const UiAssets::ResourceReader&); };`
loads `gameHelpBG.bmp` (`src/LoadingManager.cpp:87,105`). Export the loader that is file-private
today in `ui/UiAssets.cpp:11-31` as
`bool loadUiTexture(TextureStore&, const UiAssets::ResourceReader&, const char* name, Texture& out);`
declared in `ui/UiAssets.h`. Same body, still used by `UiAssets::load`. `MiniGameHost::loadAssets` =
`assets_.load(...) & vending_.loadAssets(...)`. Textures are move-only `Texture` handles owned by
these structs. They live as long as `GameContext`, which `Main.cpp` destroys before
`app.shutdown()`, just like `UiAssets`.

### 5.3 `MiniGameHelp` (session role) + model + view
Port of `MiniGameManager` help (`src/MiniGameManager.cpp:250-345`). `screenRect = {0,0,480,320}`
(`docs/original-code/cutscenes-camera.md:408`).
```cpp
class MiniGameHelp {
public:
	void init(const Localization* loc);
	void reset() { scroll_ = 0; }                 // initMiniGameHelpScreen (:250-252)
	void scroll(int delta);                       // handleMiniGameHelpScreenScroll (:327-350), verbatim
	// Composes title/body/footer into own Text buffers; body wrapText(49, '\n'),
	// numLines_ = line count; fills the model (window = 16 lines from scroll_).
	void buildModel(int titleId, int bodyId, MiniGameHelpModel& m);
private:
	const Localization* loc_ = nullptr;
	int scroll_ = 0, numLines_ = 0;
	Text title_, body_, footer_;
};
```
`scroll(d)`: `d<0` → `scroll_ = max(0, scroll_+d)`. `d>0` → `scroll_ = min(max(0, numLines_-16), scroll_+d)`.
Keys (each game calls these from its own help handler): UP -1, DOWN +1, RIGHT +16, LEFT -16
(VM §3 table, `src/VendingMachine.cpp:245-259`).

`MiniGameHelpModel { const Texture* background; const Text* title; const Text* body; const short* lineIndex?; int firstLine, numLines; const Text* footer; }`.
Use whatever line-table form the existing `Ui::textRows`/`textBlock` primitives need. Text ids are
file 0: vending 195/196, hacking 164/165, sentry STRINGID(0,171/172) = 171/172. Footer = (0, 96).

`drawMiniGameHelp(Ui&, const MiniGameHelpModel&)` (`src/MiniGameManager.cpp:254-325`):
background at (0,0); title dehyphenated at (240, 25) anchor 3; body window of 16 lines at (18, 45)
anchor 20 (0x14); `ui.scrollBar(462, 45, 256, firstLine, min(firstLine+16, numLines), numLines, 16)`;
footer dehyphenated at (240, 300) anchor 3. The per-game "Exit" label (0,30) at (445, 302) anchor 3
is drawn by each game's view after this call (`src/VendingMachine.cpp:620-632`).
`setFontRenderMode(2)` (the "dim unless highlighted" call around these labels) is a **no-op in the
original**: `Canvas::fontRenderMode` is written (`src/App.cpp:515`, `src/Canvas.cpp:179`) and never
read anywhere in `src/` (grep). Draw the label plainly.

## 6. Vending machine session — `minigames/vending/VendingMachine.h/.cpp`

### 6.1 Public surface
```cpp
class MiniGameHost;
class VendingMachine : public MiniGame {
public:
	void init(MiniGameHost* host);
	bool loadAssets(TextureStore&, const UiAssets::ResourceReader&);

	// MiniGame
	void begin(int b1, int b2, int b3) override;   // initGame(loadMapID, (loadMapID-1)*2 + b1) (src/ScriptThread.cpp:1306)
	void handleAction(Action a) override;          // :172-187 dispatch on screen_
	void handleTouch(int buttonId) override;       // :879-895 -> per-screen touch handlers
	UiResult draw(Ui& ui) override;                // updateGame :393-419

	// Menu API (MenuSession, G5) — legacy names, src/VendingMachine.cpp:805-827, :118-130
	bool  drinkInThisVendingMachine(int n) const;
	short getDrinkPrice(int n) const;
	int   getSnackPrice() const;                   // hacked ? 8 : 10
	bool  buyDrink(int n);
	void  returnFromBuying();
	void  endGame(int n);                          // -> host_->finish(n)
	int   currentItemQuantity = 1;                 // written by the menu (actions 28, UP/DOWN)
	int   currentItemPrice = 0;                    // written by initMenu 86
private:
	...
};
```

### 6.2 Fields (legacy `src/VendingMachine.h:14-60`, named)
`screen_` (stateVars[1]: 0 help, 1 game, 2 terminal), `result_` (stateVars[2]: 0 editing, 1 won,
2 wrong), `chevronClock_` (stateVars[3]). stateVars[4] is written but never read → drop it.
Also: `mainTerminalCursor_`, `gameCursor_`, `gameCursor2_` ([GEC] column selector), `solution_[4]`,
`sliderPositions_[4]`, `playersGuess_[4]`, `minimums_[4]`, `maximums_[4]`,
`chevronAnimationOffset_[4]`, `correctSum_`, `triesLeft_`, `currentMapNumber_`,
`currentMachineNumber_`, `machineHasBeenHacked_`, `machineJustHacked_`, `iqBonusAwarded_`,
`widthOfContainingBox_` (= 96 at W=480, VM §1), `VendingAssets assets_`, `VendingModel model_`,
own `Text` buffers for every composed string. No `callingThread` (the host owns it), no
`gamePlayedFromMainMenu` (always false), no `stateVars` pointer.

### 6.3 Behaviour (verbatim, world branch only)
- `begin`: VM §1 "initGame". `currentMapNumber_ = clamp(loadMapID, 1, 9)`,
  `currentMachineNumber_ = clamp((loadMapID-1)*2 + b1, 1, 18)` with `loadMapID = game->combat.loadMapID`
  (`new_src/domain/game/Combat.h:92`). Then `screen_=2, result_=0, chevronClock_=*upTimeMs`,
  `host->help().reset()`, tries/hacked from Player (§6.4), `randomizeGame()` if
  `machineCanBeHacked()`, cursors = 0, `iqBonusAwarded_=0`. b2/b3 are unused (scripts pass -1).
- `randomizeGame`: VM §3 "Puzzle setup", verbatim. Random = `ItemPickup::nextInt()`
  (`new_src/domain/game/ItemPickup.h:49`): **make it public**, do not duplicate it.
  IQ = `player->ce.getIQPercent()`.
- Input: VM §3 "Input per state" table, row by row (`src/VendingMachine.cpp:189-374`). FIRE on
  "Buy" (terminal, cursor 0) → `host->env().openMenu(83)`.
- Touch: VM §3 "Touch" paragraph (`:897-992`). Help: 10 → BackKey, else Use. Game: a result
  screen → Use; 0 → Use; 9 → BackKey; 10 → Menu; 1..8 → slider `(id-1)/2` up (odd ids, clamp 9) or
  down (even ids, clamp 0), with `gameCursor2_ = (id-1)/2`. Terminal: 9 → BackKey, 10 → Menu,
  else Use. Each touch path calls the same handler as the keys.
- Column drag (`:754-768`): in `draw`, before building the model, while `ui.in().down` and the
  cursor is inside column i's rect `(x+5..x+27, y+104..y+214)`, set
  `sliderPositions_[i] = (y+214-py)/11`. Same frame order as legacy (mutation inside updateGame).
- `draw`: clock first. `if (upTimeMs - chevronClock_ > 100) { chevronClock_ = upTimeMs; ++offset %11 for all 4 }`.
  Then, by `screen_`: terminal / game / help. In the game screen, `playersGuess_ = sliderPositions_`
  every frame before drawing (`:637-639`). This is a model-build side effect and stays in the session.
- `returnFromBuying` (`:118-130`): `host->env().host->requestState(StateId::MiniGame)`, then
  `screen_=2, result_=0, machineJustHacked_=false`, **`chevronClock_ = 0`** (legacy
  `setState` zeroed stateVars[3] and `returnFromBuying` does not restore it). Then
  `if (!machineCanBeHacked()) endGame(2)`.
- `endGame(n)` → `host_->finish(n)`. Vending plays no sound and runs no `evaluateMiniGameResults`
  (`:850-864`). Result 2 for every exit except MENU on the win screen (1) (VM §5).
- Sounds: log `[sound] N` with `std::fprintf(stderr, ...)` (existing precedent,
  `new_src/core/GameContext.cpp` Dying entry). Ids per VM §3 (1043 win, 1040 fail, click).
- Terminal Exit soft key (`src/VendingMachine.cpp:571-593`). This is NOT one of the 11 buttons.
  Legacy draws `hud->imgVendingSoftkeyNormal` at (362, 297-h) with h = img.h/2, using exclusive
  bounds `x in (362, 362+w)`, `y in (297-h, 297+h)`. While touched inside, it swaps to the
  Pressed image and sets `mainTerminalCursor = 2`. After the release (touched false, stale press
  inside), the next updateGame calls `handleInput(BACK)` → `endGame(2)`. touchEnd's FIRE on
  cursor 2 is a no-op (`:198-205`). Net effect: a release inside the soft-key image exits. Port:
  the terminal's button table appends a pseudo-button **id 11 = `kTerminalExit`** with that rect,
  AFTER ids 0-10 (list order keeps button 10 first where they overlap; both end in `endGame(2)`).
  `handleTouch` on the terminal: 9 → BackKey, 10 → Menu, 11 → BackKey, else Use. In `draw`, while
  `ui.in().down` and the cursor is inside the rect, set `mainTerminalCursor_ = 2` and draw the
  Pressed image. Deviation: inclusive instead of exclusive edges (1 px), and the exit happens on
  the release instead of one frame later.

### 6.4 Player additions — `domain/game/Player.h/.cpp` (G3)
VM §2, verbatim (`src/Player.cpp:506-518`, `:2451-2483`):
`int hackedVendingMachines = 0; int vendingMachineHackTriesLeft1 = 0, vendingMachineHackTriesLeft2 = 0;`
plus `bool vendingMachineIsHacked(int id) const`, `void setVendingMachineHack(int id)`,
`int getVendingMachineTriesLeft(int id) const`, `void removeOneVendingMachineTry(int id)`. Initialise
them where the rewrite does the legacy new-game reset (every 3-bit field = 4).

## 7. Vending menus 83-87 — `core/MenuSession.*`, `ui/MenuModel.h`, `ui/MenuView.cpp`, `ui/UiAssets.*`

Facts: ui.md §17b. Reuse list: §17b.12.
- `MenuSession::Env` gains `VendingMachine* vending = nullptr;` (set in `GameContext::init` to
  `&minigames_.vending()`). `MenuSession.cpp` includes `minigames/vending/VendingMachine.h`.
- Rows: 83/84/85 header (§17b.3), 84 drinks (§17b.4: `addEntityRow`-style label/help, price in the
  value column, flags 32, action 28, param slot), 85 snacks (§17b.5; the set index comes from
  `game->combat.loadMapID`, which is the legacy `canvas->loadMapID` argument at `:2123`), 86 confirm (§17b.6 table), 87 can't-buy (§17b.8 table). The "Cre-dits: N" row and
  the "%01 x %02" / "To-tal Cost" rows use `literalBuf_`/`hasLiteralLabel`.
- Actions: 28 `CONFIRMBUY`, 29 `BUYDRINK`, 30 `BUYSNACK`, verbatim (§17b.6-17b.7). Add the ids
  next to the existing `kAction*` constants. Sound 1125 on every select in 86 (`:2983-2985`).
- `handleAction` overrides: on 86, `Forward`/`Back` (UP/DOWN) change the quantity and rebuild
  (`:2859-2875`). On 83, `Menu`/`BackKey` → `env_.vending->returnFromBuying()` instead of `back()`
  (`:2927-2928`, `:2947-2948`).
- **Deviation (ADR 0033):** `Action::MenuResume` (right soft key "Exit") on any of 83-87 →
  `env_.vending->endGame(2)`, not `returnToGame()`. Legacy `returnToGame` (`:4788-4790`) leaves
  the `EV_MINIGAME` thread parked forever (§17b.1).
- `moveDir` type 9 extras (§17b.10 last paragraph, `:416-455`). This replaces the "minus type 9"
  omission noted at `new_src/core/MenuSession.h:135`.
- Info buttons on 84, 85 and the 86 action row (§17b.11). The cursor glyph is suppressed on 86/87
  (`:1064`).
- Presentation (§17b.11): `MenuViewModel` gains `bool vendingSkin = false;` (a bool, per the
  model's "no menu id" rule at `ui/MenuModel.h:43-46`) and `bool showQtyArrows = false;` (86 only).
  `MenuView` with `vendingSkin`: bottom panel, then `vendingBG` at (0,0) instead of the menu
  background, no status line, `vending_button_large` row art, `vending_button_help` info buttons,
  scrollbar with `barRect.x = 350` and the vending scrollbar sheets, vending soft keys (left at
  (56,277), "Back" at (67,307) anchor 36; right at (362,277), literal "Exit" at (411,307) anchor
  40). Quantity arrows on 86: up at (320,130), down at (320, arrowUp.h + 150), glow sheets. A
  release on them returns `UiAction::ScrollUp`/`ScrollDown`, which `applyUiAction` already maps to
  `Forward`/`Back` = the same UP/DOWN path as the keys (`:4744-4763` does the same checks then
  `setMenu(86)`). 83 shows only the right soft key (`:4617-4627`).
- `UiAssets` gains (loaded in `UiAssets::load`; `vendingBackground` (`vendingBG.bmp`) and the two
  soft keys already exist from G3): `vendingButtonLarge`, `vendingButtonHelp`, `vendingScrollBar`, `vendingScrollTop/Mid/Bottom`,
  `vendingArrowUpGlow/DownGlow`. File names per §17b.11 and
  `src/Hud.cpp:55-56`. `vendingBackground` is ALSO what the vending terminal draws: the vending
  view reads `ui.art().vendingBackground`, not a copy (ADR 0033 point 6).

## 8. Vending assets and view

`VendingAssets` (minigame-only, `src/LoadingManager.cpp:93-102`): `vendingGame` (atlas `vendingGame.bmp`),
`gameBackground` (`vending_BG.bmp`), `submit`, `submitPressed`, `arrowUp`, `arrowUpPressed`,
`arrowDown`, `arrowDownPressed`, `buttonSmall` (`vending_button_small.bmp`). The terminal Exit art
is `hud->imgVendingSoftkeyNormal/Pressed` (`src/VendingMachine.cpp:571-581`), i.e.
`vending_softkey_{normal,pressed}.bmp`. The vending menus need those too, so they live in
`UiAssets::vendingSoftkeyNormal/Pressed` (loaded in G3, together with `vendingBackground`), and the
view reads them from `ui.art()`. **`fixImage` on `vending_arrow_down.bmp`** (`src/LoadingManager.cpp:109`,
`src/Utils.cpp:164-172`): when the indices' MD5 matches, palette indices 1 and 2 become 0. Port it
as "remap index 1,2 → 0 before upload", applied unconditionally to that one file (the shipped file
is the one the hash names; there is no other variant to protect).

`VendingModel.h`: one struct, filled by `VendingMachine::draw` into its own buffers:
`enum class Screen { Help, Game, Terminal } screen;` plus the borrowed `const Text*`s and numbers
each screen needs (VM §4 "Main terminal" steps 2-7, "Hack game" steps 1-5), a `MiniGameHelpModel help`,
and `MiniGameButton buttons[12]; int buttonCount;` — the button table rebuilt each frame from VM §1's
table (ids 0, 1..8 with rects from `drawGameMiddleBar` (game screen only), 9 `(0,290,54,30)`,
10 `(428,290,54,30)`), plus pseudo-id 11 on the terminal (§6.3).

`VendingView.h`: `UiResult drawVending(Ui& ui, const VendingModel& m, const VendingAssets& art);`.
It draws per VM §4 (every coordinate/anchor there carries its `src/VendingMachine.cpp` line). The
highlight state comes from `highlightedButtonId`. It ends with `return touchRelease(m.buttons, m.buttonCount, ui.in());`.
The view takes `VendingAssets` as an argument because they are not in `UiAssets`. It still reads no
game state.

## 9. Placeholders: hacking and sentry bot (not implemented now)

They fit the same shape without touching GameContext or ScriptVM:
- `minigames/hacking/HackingGame.{h,cpp}`, `HackingModel.h`, `HackingView.{h,cpp}`,
  `HackingAssets.{h,cpp}` (`hackerBG.bmp`, `blockGameColors.bmp` + its fixImage,
  `src/LoadingManager.cpp:85-86,108`). b1 == 1 (custom puzzle) is a legacy
  `app->Error` (`src/ScriptThread.cpp:1317-1318`). `MiniGameHost::start` returns false for it
  (logged, thread not parked) and never calls `begin`. b1 == 0 → `initGame(b2, b3 == -1 ? 30 : b3)`
  (`src/ScriptThread.cpp:1332-1339`, `src/HackingGame.cpp:53-55`). The column drag reads `ui.in()`
  in `draw` (`src/HackingGame.cpp:385-397`). Buttons 0 `(420,280,60,40)`, 1 `(0,280,60,40)`.
- `minigames/sentrybot/SentryBotGame.{h,cpp}` + model/view/assets (`matrixSkip_*.bmp`,
  `sentryGame.bmp`, `src/LoadingManager.cpp:88-92`; `matrixSkip_button.bmp` is loaded per
  `initGame` in legacy, `src/SentryBotGame.cpp:98-100`, but loaded once here). `begin(b1,...)` →
  `initGame(b1)` (`src/ScriptThread.cpp:1297`). Touch-only `ACTION_MATRIX_LEFT/RIGHT` stay internal
  to `handleTouch` (`src/SentryBotGame.cpp:878-882`), so no new `Action`.
- Both: `endGame` = sound (1043 win / 1040 otherwise) + `evaluateMiniGameResults(n)` + `finish(n)`
  (`src/HackingGame.cpp:619-636`). Move `evaluateMiniGameResults` (`src/MiniGameManager.cpp:235-248`)
  into `MiniGameHost` with the first of the two.
- Host change per game: one member, one `case` in `start`, one call in `loadAssets`.

## 10. Implementation groups

Each group builds on its own (reconfigure when files are added) and keeps the game runnable.

| G | Files | Content | Verify |
|---|---|---|---|
| **G1** framework | `core/GameStates.h`, `ui/UiTypes.h`, `minigames/common/MiniGame.h`, `minigames/common/MiniGameButtons.h`, `minigames/common/MiniGameHost.h/.cpp` (no games yet: `start` returns false for every type, no `vending_` member yet), `core/GameContext.h/.cpp`, `domain/game/ScriptVM.cpp` | §2, §3, §4, §5.1 | Build passes. map00 vending tile (27,29) still logs `MINIGAME type=4 not implemented` and the script continues as before. |
| **G2** shared help + assets | `minigames/common/MiniGameAssets.h/.cpp`, `MiniGameHelp.h/.cpp`, `MiniGameHelpModel.h`, `MiniGameHelpView.h/.cpp`, `ui/UiAssets.h/.cpp` (export `loadUiTexture`), `core/Main.cpp` (`loadAssets` call) | §5.2, §5.3 | Build passes. Startup log shows no missing-resource line for `gameHelpBG.bmp`. Nothing visible yet. |
| **G3** vending: terminal + help + exit | `domain/game/Player.h/.cpp` (§6.4), `domain/game/ItemPickup.h` (public `nextInt`), `minigames/vending/VendingMachine.h/.cpp`, `VendingModel.h`, `VendingView.h/.cpp`, `VendingAssets.h/.cpp`, `MiniGameHost` (add `vending_`, `case kVending`), `ui/UiAssets.h/.cpp` (`vendingBackground`, `vendingSoftkeyNormal/Pressed`) | §6 without the game screen and the menu, §8 terminal + help. Temporary: help FIRE (→ game) and terminal "Buy" log `[vending] <x>: G4/G5` and do nothing. | User: walk to map00 tile (27,29) and use it. The vending terminal shows ("Vending Machine" title, Credits, prompt, Buy/Hack buttons, cursor wobble, Exit). LEFT/RIGHT moves the cursor. "Hack" opens the help screen (scrollable text, scrollbar, "Exit"). Back returns to the terminal. Exit/Esc returns to the world and the script continues (`[script]` log after MINIGAME). |
| **G4** vending hack game | `VendingMachine.cpp`, `VendingModel.h`, `VendingView.cpp`, `VendingAssets.cpp` (+ arrow-down fix) | §6.3 game input/touch/drag/submit/results, VM §3 "Submit", VM §4 "Hack game"; IQ bonus via `modifyStat(7,2)` (port it if missing, `src/Player.cpp:236-238`) | User: help → FIRE shows 4 sliders with chevrons, a pulsing yellow selected slider, the top-bar sums and "Hack Goal Sum". Arrows/keys move the sliders. OK submits: a wrong code shows "Wrong code! Tries left: N"; 4 failures lock the machine (reopen: no Hack option). A win shows "Hack Successful! +2 to I.Q." and the terminal then says prices dropped. |
| **G5a** vending menus: lists | `core/MenuSession.h/.cpp`, `core/GameContext.cpp` (Env::vending) | §7: 83/84/85 rows + header, 83 back → `returnFromBuying`, `MenuResume` → `endGame(2)`, type 9 `moveDir`, info buttons 84/85; terminal "Buy" now opens 83 | User: Buy → "Vending Machine / Credits: N / Nano Drinks / Snacks"; Nano Drinks lists 11 drinks with prices (hacked prices after a win); Snacks lists 12 names at 10 (8). Back on 83 returns to the terminal (or straight to the world if the machine can no longer be hacked). |
| **G5b** vending menus: confirm + buy | `core/MenuSession.cpp` | §7: action 28 → 86, UP/DOWN quantity, 29/30 buy, 87 can't afford, sound 1125 | User: picking a drink shows "Confirm Order / <name> x 1 / Total Cost"; UP raises the quantity only while affordable; selecting buys it (credits drop, item appears in Items → Nano Drinks) and returns to the list; with too few credits "You can't afford this!" → tap/Use returns to the list. |
| **G6** vending menu art | `ui/UiAssets.h/.cpp`, `ui/MenuModel.h`, `ui/MenuView.cpp`, `core/MenuSession.cpp` (set `vendingSkin`/`showQtyArrows`) | §7 presentation | User: menus 83-87 draw on the vending background with the large vending buttons, vending soft keys (Back/Exit), the vending scrollbar at x=350, and arrows on the confirm screen; "Exit" leaves the machine and the script continues. |

Reviewer checklist for every group: view-role files include nothing from `domain/`/`core/`/`io/`
(grep the includes); no session includes `core/GameContext.h`; `finish` order is
vars[7] → Playing → resume.

## 11. Acceptance (whole feature)

On map00 the vending machine behind tile (27,29) (VM §8) opens the terminal. Every path out
(Exit soft key, Esc/Back on the terminal, MENU on a result screen, Back on menu 83 when the machine
is exhausted, Exit on any vending menu) returns to the world with the walk/script resuming. Credits
and the bought drinks/snacks persist in the player inventory. A hacked machine shows the hacked
prices on every later visit in the same session.
