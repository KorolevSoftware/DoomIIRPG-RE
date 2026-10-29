# ADR 0033 — Minigames live in `new_src/minigames/<name>/` behind one small `MiniGame` interface, dispatched by `MiniGameHost`

Date: 2026-10-29
Status: accepted
Spec: `specs/2026-10-29-minigames-layout.md`

## Context

The original has three minigames: `src/VendingMachine.{h,cpp}`, `src/HackingGame.{h,cpp}`,
`src/SentryBotGame.{h,cpp}`. All three are singletons on `App` (`src/App.cpp:90-92`) and are
entered from script opcode `EV_MINIGAME` (53). Byte 1 selects the game: 0 sentry bot, 2 hacking,
4 vending (`src/ScriptThread.cpp:1290-1349`). The calling thread parks with `unpauseTime = -1`.
They all run in `ST_MINI_GAME` (12), with `stateVars[0]` as the game selector
(`src/Canvas.cpp:450-466`, `src/InputEventController.cpp:334-349`,
`src/TouchController.cpp:106-116, 238-248, 316-330`).

What the three classes really have in common:

- Entry: `initGame(callingThread, ...)` latches the thread, switches to `ST_MINI_GAME`, sets the
  sub-screen in `stateVars[1]` (0 = help everywhere), resets the shared help scroll and clears the
  soft keys (`src/VendingMachine.cpp:39-51`, `src/HackingGame.cpp:36-51`,
  `src/SentryBotGame.cpp:33-42`).
- Per frame: `handleInput(action)` (keys), `updateGame(graphics)` (draws AND mutates animation
  state), `touchStart/Move/End`. `touchEnd` always resolves to "which button id was released on"
  through `fmButtonContainer::GetTouchedButtonID` (`src/Button.cpp:277-287`), followed by a
  per-sub-screen switch.
- Exit: `endGame(n)` writes `scriptStateVars[7] = n`, calls `setState(ST_PLAYING)` and then
  `callingThread->run()` (`src/VendingMachine.cpp:850-864`, `src/HackingGame.cpp:619-636`,
  `src/SentryBotGame.cpp:806-824`).
- The help screen: one shared scroll state plus a renderer in `MiniGameManager`
  (`src/MiniGameManager.cpp:250-345`). The title/body ids differ per game (195/196, 164/165,
  171/172). The background `gameHelpBG.bmp` is one image shared by all three
  (`src/LoadingManager.cpp:87,105`).

What is NOT shared: board state, rules, sub-screens beyond help, draw code, button layouts.
Hacking and sentry play a result sound and call `evaluateMiniGameResults` (IQ +2) inside
`endGame`; vending does neither and awards IQ at submit time instead. Only vending reaches the
in-game menu (menus 83-87, `docs/original-code/ui.md` §17b). Hacking's column drag reads raw
press coordinates during draw (`src/HackingGame.cpp:385-397`).

The rewrite has no `ST_MINI_GAME`. `EV_MINIGAME` is a logging stub
(`new_src/domain/game/ScriptVM.cpp:1355-1360`). Screens follow a session + model + stateless view
split (`LootSession`/`LootView`, `MenuSession`/`MenuView`, ADR 0012), and `ui/` includes nothing
from `domain/`.

## Decision

1. **New top-level folder `new_src/minigames/`**, with `common/` plus one folder per game:
   `vending/`, later `hacking/` and `sentrybot/`. A minigame is a vertical slice (rules + input +
   model + view + its own sheets), so it gets its own folder instead of being spread over `core/`
   and `ui/`. The existing layering rule applies **per file role** inside the folder:
   - `*View.{h,cpp}`, `*Model.h`, `common/MiniGameButtons.h`: same rules as `ui/`. They include
     only `ui/`, `text/`, `render/api/` and their own model. No `domain/`, `core/` or `io/`.
   - Session classes (`VendingMachine`, `MiniGameHelp`, `MiniGameHost`) may include `domain/`,
     `io/`, `core/GameStates.h`, `ui/` and their own view. They never include
     `core/GameContext.h` or `core/MenuSession.h`. They reach the state machine only through
     `StateHost` and the `openMenu` callback.
   - `core/` includes `minigames/common/MiniGameHost.h` (GameContext) and
     `minigames/vending/VendingMachine.h` (MenuSession, vending menus only). `domain/game/ScriptVM.cpp`
     reaches the host through `env_.ctx->minigames()`, the same coupling it already uses for
     `cinematic()` and `actions()`.
2. **The common interface is exactly four virtuals** (`minigames/common/MiniGame.h`):
   `begin(b1,b2,b3)`, `handleAction(Action)`, `handleTouch(int buttonId)`, `draw(Ui&) -> UiResult`.
   This is the whole shared lifecycle. `draw` is session-side: it may advance animation clocks and
   read `ui.in()` for drags, as the originals do inside `updateGame`. It then builds the model and
   calls the game's stateless view.
3. **`MiniGameHost` owns the shared parts.** It holds the calling `ScriptThread*` and the active
   game. `start(type, b1, b2, b3, thread)` returns false for a type it does not have, and the VM
   then does not park. `finish(result)` is the shared `endGame` tail: `vars[7] = result`, then
   `requestState(Playing)`, then `resumeThread`, in that order. The host also owns the single
   shared help state (`MiniGameHelp`) and the shared sheet (`MiniGameAssets::helpBackground`).
   The concrete games are plain members of the host. There is no registry and no factory.
4. **Touch goes through the one action queue.** Views hit-test the release point with a verbatim
   port of `GetTouchedButtonID` (inclusive rects, list order, `common/MiniGameButtons.h`). They
   return `UiResult{UiAction::MiniGameTouch, id}`, where id -1 means a release on no button.
   `GameContext::applyUiAction` latches the id and queues `Action::MiniGameTouch` (the `ListRow`
   precedent). The tick routes every action in `StateId::MiniGame` to the host before the
   input-blocked check (legacy dispatch order, `src/InputEventController.cpp:334-349` comes before
   `:449`).
5. **Sub-state is owned by each game**, in named fields (`screen_` = legacy `stateVars[1]`,
   `result_` = `stateVars[2]`, ...), not in `GameContext::stateVars`. The legacy re-initialises
   every `stateVars` slot it reads on each `setState(ST_MINI_GAME)` (`initGame`,
   `returnFromBuying`). The one exception, the chevron clock `stateVars[3]`, is reproduced
   explicitly (spec §6.3).
6. **Asset ownership:** sheets drawn by `ui/` views (the vending menu art, including
   `vendingBG.bmp`, which both the terminal and menus 83-87 draw) go into `UiAssets`. Sheets drawn
   only by a minigame go into that game's `*Assets` struct (`VendingAssets`, ...). The shared help
   background goes into `MiniGameAssets`. All of them are loaded once at startup, as
   `LoadingManager::loadMiniGameImages` does (`src/App.cpp:105`, `src/LoadingManager.cpp:81-110`).
   The BMP→texture helper that is file-private in `ui/UiAssets.cpp` today is exported as
   `loadUiTexture`, so there is one loader.
7. **Dropped as unreachable/dead:** `playFromMainMenu` and every `gamePlayedFromMainMenu` branch
   (the rewrite has no main menu), and `forceWin` (`game->skipMinigames` is only ever set to false,
   `src/Game.cpp:62`). They are not part of the interface.

## Consequences

- Adding hacking or sentry bot means a new folder, one member plus one `case` in `MiniGameHost`,
  and a `loadAssets` call. GameContext, ScriptVM and the action plumbing stay untouched.
- `evaluateMiniGameResults` (IQ +2 message, `src/MiniGameManager.cpp:235-248`) has two users
  (hacking, sentry). It moves into `MiniGameHost` when the first of them is implemented, not before.
- The vending ↔ menu round trip (terminal → `setMenu(83)` → back → `returnFromBuying`) keeps the
  host's calling thread and active game alive across `StateId::Menu`. Only `finish()` clears them.
- One deliberate deviation: the vending menus' touch "Exit" soft key goes through
  `VendingMachine::endGame(2)` instead of the legacy `returnToGame()`. The legacy call leaves the
  script thread parked forever (`docs/original-code/ui.md` §17b.1).
- A new top-level directory is picked up by `GLOB_RECURSE` but needs a CMake reconfigure.

## Rejected alternatives

- **`core/minigames/` + `ui/minigames/` split:** this follows the existing layer folders, but the
  user asked for one folder per minigame, and each game would end up in two places. The layering
  rule is kept per file role instead.
- **No base class, a `switch (type)` at each dispatch site:** that makes four switches (start,
  input, touch, draw) in GameContext and ScriptVM, which have to stay in sync. The four virtuals
  are exactly the shared surface.
- **A fatter interface** (`forceWin`, `playFromMainMenu`, `tick`, `loadAssets`, `onResult`):
  the first two are dead, nothing ticks in `ST_MINI_GAME` (`src/Canvas.cpp:827-828` skips it), and
  asset loading and the result side effects differ per game. Speculative.
- **Minigame sub-state in `GameContext::stateVars`** (literal port): this couples every game to
  the state machine's array and its zero-on-`setState` rule for no behavioural gain.
- **Using `Ui::button`/`UiId` slots for minigame buttons:** `Ui::hitTest` needs press and release
  on the same widget. The legacy fires on the release position only, so a press-drag-release onto
  a button would differ.
