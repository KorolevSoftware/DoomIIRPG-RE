# ADR 0012 — Immediate-mode UI layer with per-frame models, and no game logic in `ui/`

Date: 2026-08-27. Status: accepted.
Spec: [specs/2026-08-27-ui-layer.md](../specs/2026-08-27-ui-layer.md).

## Context

`new_src/ui/` is not a layer today, it is two classes. `Hud` owns ~24
textures, ~15 drawing methods, five timed text slots, the shake generator and
a set of demo fields fed by setters; `ViewWeapon` includes
`domain/game/Game.h` and `domain/game/Player.h` and reads the player and the
combat sequence directly from inside its draw call
(`new_src/ui/ViewWeapon.cpp:6-8,89,123`). Loot and dialog drawing live in
`core/LootSession.cpp` and `domain/game/DialogSystem.cpp`; the loot overlay
reaches into the dialog system just to borrow a scrollbar
(`new_src/core/LootSession.cpp:167-169`). The menu, the automap and the save
screens are still to come, so whatever shape we pick now will carry them.

Two properties of this codebase constrain the choice:

* The renderer is already immediate: `SpriteBatch` is cleared in `begin()`
  and everything is re-emitted every frame
  (`new_src/render/gl/SpriteBatch.cpp:124-140`).
* The legacy runtime this port follows re-reads every displayed value per
  frame. Every `repaintFlags &= ~bit` clear inside `Hud::draw` is commented
  out in the RE port (`src/Hud.cpp:731,739,748,772,801`;
  `docs/original-code/ui.md` §8), so the invalidate flags are vestigial and
  all widget values are live reads.

## Decision

**1. A small custom immediate-mode UI.** Screens are drawn by functions that
both draw and report what was pressed (`UiResult drawHud(Ui&, const
HudModel&)`, `drawLootList`, `drawDialog`, `drawMenu`). There is no retained
widget tree. A retained tree would have to be reconciled every frame against
state that is already recomputed every frame — pure cost, no benefit — and
its "dirty" bookkeeping would mirror exactly the invalidate flags the port
already found redundant.

**2. Immediate-mode still keeps state, and it is named and bounded.** One
`UiState` owns exactly three things:

* `activeId` — which widget received the press. Without it "released inside"
  and "released outside" are indistinguishable and the `*_Active` sheet
  highlight (`docs/original-code/ui.md` §1) has nothing to key off.
* per-region scroll offsets;
* the text wrap cache.

Nothing else is allowed in. Screen state — current menu section, selected
index, loot dwell timing — belongs to that screen's session struct, following
the existing `LootSession` precedent (`new_src/core/LootSession.h:39-64`).

**3. Explicit ids: `enum class UiId` per widget**, not hashed labels. We have
dozens of widgets, not thousands; explicit ids are greppable, cannot collide,
and give the scroll table a fixed array size.

**4. The view model is a per-frame struct, not a class with setters.**
`HudModel` is rebuilt from scratch by `GameContext` every frame. Evidence
that this is load-bearing rather than taste: `Hud` currently keeps
`weapon_ = 3`, `ammo_ = 30`, `shield_ = 75` as fields fed by setters
(`new_src/ui/Hud.h:166-172`) and those demo values were on screen for months,
because a field cannot tell you whether it was fed this frame or is stale. A
struct rebuilt per frame cannot go stale.

**5. UI receives values and returns intents — mechanically checked.**
`ui/` may format numbers, wrap text, pick a texture row, clamp a scroll
offset, compute the portrait row from a passed-in hp/maxHp pair
(`docs/original-code/ui.md` §4). It may not read `Player`, reach into
`Tables`, call `advanceTurn`, mutate inventory or change state. The check is
`grep -rn "domain/game" new_src/ui/` returning empty; `ui/` also may not
include `core/` (that edge would close a cycle:
`new_src/core/GameContext.h:14` includes `ui/ViewWeapon.h`).

**6. Intents feed the EXISTING action queue.** `Action` already has `Menu`,
`Automap`, `BackKey` (`new_src/core/GameStates.h:26-27`). A single
`GameContext::applyUiAction(UiAction, int)` translates UI intents into those
and pushes them into the same `pendingActions_` vector the keyboard uses
(`new_src/core/GameContext.h:68`), so mouse and keys cannot diverge in turn
semantics.

**7. One input normalizer, no focus graph.** SDL is turned into
`UiInput { cursorX, cursorY, pressed, released, down, Nav nav, wheel }` in
one place. The cursor drives hit-testing; `nav` moves the selected index that
the screen session owns — the same index the arrow keys move. Our UI is 1-D
lists plus a few fixed buttons; a focus graph would be machinery for a
problem we do not have. Buttons unreachable by a cursor get an explicit
shortcut. Escape hatch recorded on purpose: a stick-driven virtual cursor
feeds `cursorX/cursorY` and reuses the mouse hit-test path unchanged. Gamepad
support is minimal by intent — there is no `SDL_GameController` in `new_src/`
today and, unlike everything else here, it has no original to be faithful to
(the original was touch + soft keys).

**8. `Graphics2D::setClip` becomes a real GL scissor** in `SpriteBatch`,
mirroring the `setBlendMode` precedent (compare, `flush()`, assign —
`new_src/render/gl/SpriteBatch.cpp:145-149`); without the flush,
already-batched quads would be clipped retroactively. The window→canvas
inverse transform is exposed for the cursor from the same latched letterbox
rect. **Boundary: scissor is for the new UI, not for the migrated legacy
screens.** The loot list and the dialogs keep drawing whole 16 px rows
(`kLineH = 16`, 3 visible loot rows, `new_src/core/LootSession.cpp:155-165`)
so their behaviour stays bit-identical; a "nearly the same" loot list is the
worst possible outcome.

**9. Reuse, do not rewrite.** The wrap representation is the one already
proven in the loot path — one `Text` plus `<start,len>` pairs
(`new_src/domain/game/CorpseLoot.h:60`, filled at
`new_src/core/LootSession.cpp:155-165`) — and the row primitive is the
existing `Graphics2D::drawString(..., lineH, strBeg, strEnd)`. The scrollbar
is moved, not rewritten, out of `DialogSystem::drawScrollBar`
(`new_src/domain/game/DialogSystem.cpp:579-595`) into `Ui::scrollBar`.

## Why "no game logic in UI" is a hard rule here

A side effect invoked from the draw path lands mid-frame: after some systems
have ticked and before others. That is exactly the ordering class that
produced two defects in this project already — the cinematic fov, the cockpit
overlay and the view-weapon gate keyed off three different notions of "a
camera is active" (fixed by making `CinematicCamera::renderPose()` the single
source of truth, `new_src/core/GameContext.cpp:456-478`), and an extra
`hud->update` on cinematic-skip frames. The live example of the same hazard
is `new_src/ui/ViewWeapon.cpp:135`, which latches `Combat::flashDone` from
inside a draw call; the spec moves that latch into `Combat::tick`.

Values-in / intents-out removes the whole class: a draw pass cannot change
what a later system reads, and an intent is consumed by the queue at the
start of the next tick, where the keyboard's intents are consumed too.

## Consequences

* `ui/` grows real files: `Ui.{h,cpp}`, `UiTypes.h`, `UiState.{h,cpp}`,
  `UiAssets.{h,cpp}`, `HudModel.h`, and one view pair per screen. `Hud`
  shrinks to the non-drawing producer runtime (shake, health-bar drain,
  cockpit toggle).
* `GameContext` gains model builders and `applyUiAction`. It grows; that is
  where the frame's knowledge already lives.
* Texture loading moves to `UiAssets::load(ResourceReader)`, an injected
  functor, so `ui/` stops including `core/AppContext.h`.
* Migration is incremental and every step is eye-checkable, because every
  screen being migrated is user-confirmed working. Groups are ordered so
  exactly one file pair changes where possible.
* One behaviour change accepted knowingly: `Combat::flashDone` flips on a
  15 ms quantum boundary instead of at draw time.
* One rewrite-vs-original fix: the weapon-13 ammo readout returns to the
  original "N/5" (`new_src/ui/Hud.cpp:577-593` vs
  `docs/original-code/ui.md` §2).

## Rejected alternatives

* **A retained widget tree (Qt/ImGui-docking style).** Rejected: the values
  are recomputed every frame anyway (point 2 of Context), so the tree adds
  reconciliation and lifetime questions with nothing to gain. It also invites
  widgets holding references to `Player`, which is the failure we are fixing.
* **Dear ImGui or another third-party UI.** Rejected: the game must render
  the original's exact BMP sheets, 12×16 bitmap font, J2ME anchors and pixel
  coordinates on a 480×320 canvas through the existing `SpriteBatch`; a
  foreign UI would have to be fought into that, and it would add a dependency
  where the project deliberately hand-rolls BMP and font handling.
* **Hashed string ids (`ui.button("Menu")`).** Rejected: silent collisions,
  ungreppable, and no fixed-size arrays.
* **Keeping setters on `Hud` and adding "did anyone feed me?" asserts.**
  Rejected: it detects the stale-field bug instead of making it impossible.
* **A generic focus/navigation graph.** Rejected as machinery for a problem
  we do not have (1-D lists + fixed buttons); the virtual-cursor escape hatch
  covers gamepads if they ever land.
* **Using the new scissor to clip the migrated loot list and dialogs.**
  Rejected: those screens' row geometry is user-confirmed working and the
  original never clipped them; clipping would risk half-rows and partial
  glyphs for zero benefit.
* **Letting views push into the state machine directly (`requestState`).**
  Rejected: it re-creates the mid-frame side effect the whole ADR is about;
  intents go through the one queue instead.
