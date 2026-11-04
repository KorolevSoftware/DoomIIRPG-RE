# HUD bottom-bar touch buttons (armor, health, keys, weapon tap, weapon-select overlay) — spec

Date: 2026-11-04
ADR: [0045](adr/0045-hud-bar-buttons-state-and-touch-only-actions.md)
Facts: `docs/original-code/hud-buttons.md` (cited as **HB §n**), raw log
`docs/research/2026-11-04-hud-bar-taps.md`, item use `docs/original-code/ui.md` §17a.
Supersedes Deviation D3 of `specs/2026-08-27-ui-layer.md` (the comment at
`new_src/ui/HudView.cpp:122-127` and the "Deliberately no NextWeapon" note at
`new_src/ui/UiTypes.h:36-38`).

## 0. Scope and what exists

| Legacy button (HB §1) | Rect x,y,w,h | Release does | Rewrite pieces reused |
|---|---|---|---|
| 4 shield/armor | 49,258,79,44 | `ACTION_ITEMS_DRINKS` -> menu 75 | `Action::ItemsDrinks` arm (`new_src/core/PlayerActions.cpp:456-460`) |
| 5 health | 133,258,79,44 | `ACTION_ITEMS` -> menu 72 | `Action::Items` arm (`new_src/core/PlayerActions.cpp:451-455`) |
| 6 keys | 375,258,55,44 | `ACTION_QUESTLOG` -> menu 46 | `kMenuQuestLog` screen (`new_src/core/MenuSession.cpp:481`, id `new_src/io/MenuData.h:27`) |
| 2 weapon, short | 268,258,102,44 | `ACTION_NEXTWEAPON` | `Player::selectNextWeapon` (`new_src/domain/game/Player.cpp:332-352`) |
| 2 weapon, long | same | weapon-select overlay | `Player::selectWeapon(i, def)` (`new_src/domain/game/Player.cpp:354-363`) |

All four `*_Active` sheets are already loaded (`new_src/ui/UiAssets.h:32-35`). Nothing is drawn
differently unless a button is held or the overlay is open, so the golden frames stay identical.
**No key binding changes**: the two new actions (`QuestLog`, `HudNextWeapon`) are touch-only;
`Z`/`X` keep feeding `Action::NextWeapon`/`PrevWeapon`, which still only act while zoomed.

Out of scope: `selectPrevWeapon` (keys only, HB §4), the game's 3D-view touch controls/swipe
(the rewrite has none, so "controls are not highlighted while the overlay is open" is moot),
sound.

## 1. Design summary (ADR 0045)

- **View** (`ui/HudView.cpp`): stateless as today. Hit-tests and highlights buttons 2/4/5/6 and,
  when the model says so, draws the overlay and hit-tests its icons. Reports one `UiResult`.
- **Retained state** (`core/HudButtons.{h,cpp}`, NEW): the legacy `Hud::isInWeaponSelect`,
  `Hud::weaponPressTime` (`src/Hud.cpp:94-96`) and the touch-start origin + `canvas->touched`
  drag latch (`src/TouchController.cpp:23,128-134`). It post-filters the view's `UiResult`
  (gating, 300 ms tap/long-press split, overlay open/close) before `applyUiAction`.
  `UiState` stays untouched (its "nothing else may be added" rule, `new_src/ui/UiState.h:13-17`).
- **Actions**: `UiAction::{ItemsDrinks, Items, QuestLog, WeaponTap, WeaponSelect}`;
  `Action::{QuestLog, HudNextWeapon}`. `WeaponSelect` is applied directly in
  `GameContext::applyUiAction` (legacy calls `selectWeapon` straight from the touch handler, ahead
  of the ST_COMBAT gate, `src/Hud.cpp:1281-1288`); everything else goes through the queue.
- **Swallow**: `PlayerActions::handleAction` returns early while the overlay is open
  (`src/PlayingInputHandler.cpp:67-69`).

Per-frame flow inside the existing HUD block of `GameContext::render`
(`new_src/core/GameContext.cpp:1404-1415`):

```
hudButtons_.trackPointer(ui->in());                 // drag latch (before the view)
buildHudModel(hud, ...);                            // + hit rects, overlay fields
UiResult r = drawHud(*ui, hud);
r = hudButtons_.resolve(r, *ui, hud, frame);        // gating + 300 ms logic
applyUiAction(r.action, r.index);
```

## 2. Constants (all at canvas 480x320, `screenRect = {0,0,480,320}`, `src/Canvas.cpp:85-119`
with `n2 = 320-70 = 250`, `screenRect[3] = 250+70 = 320`)

| Name | Value | Source |
|---|---|---|
| `kWeaponHit` | {268,258,102,44} | `src/Hud.cpp:73` (w = `imgWeaponNormal->width` = 102) |
| `kShieldHit` | {49,258,79,44} | `src/Hud.cpp:79` (Hud_Shield_Normal 79x44, HB §1) |
| `kHealthHit` | {133,258,79,44} | `src/Hud.cpp:82` |
| `kKeysHit` | {375,258,55,44} | `src/Hud.cpp:85` (`imgKeyNormal->width` = 55) |
| `kLongPressMs` | 300 | `src/Hud.cpp:1329`, `:1370` |
| drag box | press origin ±3: inside iff `x0-3 <= x < x0+3 && y0-3 <= y < y0+3` | `src/TouchController.cpp:131-134`, `pointInRectangle` `src/Utils.cpp:52-54` |
| overlay dim | `panel({0,20,480,300}, 0x80000000)` | `FMGL_fillRect(0, imgPanelTop->height = 20, 480, 320, 0,0,0, 0.5f)` `src/Hud.cpp:1187` (h clipped to the canvas; 0.5 -> 0x80) |
| overlay row origin x | `(480 - 16 - 4*102) / 2 = 28` | `src/Hud.cpp:1184` |
| overlay first row y | `320 - 108 - 4 = 208` | `src/Hud.cpp:1185`, `posY = -4` `:1193` |
| overlay steps | x `+ 102 + 4 = 106` per icon, 4 per row; row `y -= 48` | `src/Hud.cpp:1198-1208` |
| overlay icon cell | 102x44 (touch area = draw rect) | `src/Hud.cpp:1206` |
| icon ammo digits | `(x + 51 + 6, y + 8)`, space 1, shown unless weapon 1, slash mode for 13 | `src/Hud.cpp:1053-1124` via `drawWeapon` `:1205` |
| bottom panel repaint | `panelBottom` at `(0, 320 - h)` after the dim | `src/TouchController.cpp:544-545` runs after `drawWeaponSelection` (`src/Hud.cpp:772-780`) |

Overlay slots for up to 15 owned weapons: rows y = 208, 160, 112, 64; columns x = 28, 134, 240,
346. Slot k (0-based count of owned weapons in id order 0..14): `x = 28 + 106*(k%4)`,
`y = 208 - 48*(k/4)`.

## 3. Groups

Build note: G1 adds `core/HudButtons.{h,cpp}` -> reconfigure
(`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`), CMake uses GLOB.

### G1 — armor / health / keys taps, highlight, gating skeleton

**`new_src/ui/UiTypes.h`**
- `UiAction`: append `ItemsDrinks, Items, QuestLog` (HUD buttons 4/5/6, `src/Hud.cpp:1346-1353`).
  Replace the "Deliberately no NextWeapon/PrevWeapon ... (Deviation D3)" comment with a pointer
  to this spec. (`HudSwitchLeft/Right` stay unused.)

**`new_src/core/GameStates.h`**
- `Action`: append `QuestLog` with comment: ACTION_QUESTLOG; touch-only in the rewrite (legacy
  `AVK_PDA` P stays unmapped, `new_src/core/UiInputCollector.cpp:117-118`).

**`new_src/core/PlayerActions.cpp`**
- Familiar early-return (`:182`): add `a == Action::QuestLog`.
- New arm after `ItemsDrinks`, mirroring it (`src/PlayingInputHandler.cpp:179-188`):
  ```cpp
  case Action::QuestLog:                    // src/PlayingInputHandler.cpp:179-188
      if (inTP) { env_.practice->exit(); break; }
      env_.hud->clearMessages();            // msgCount = 0
      if (env_.openMenu) env_.openMenu(kMenuQuestLog);
      break;
  ```

**`new_src/ui/HudModel.h`**: add caller-constant hit rects, set always (view uses them only in
the non-familiar branch): `UiRect weaponHit, shieldHit, healthHit, keysHit;`

**`new_src/core/GameContext.cpp`**
- Next to `kPortraitHit` (`:1016`): `kWeaponHit, kShieldHit, kHealthHit, kKeysHit` per §2, each
  with its `src/Hud.cpp` line.
- `buildHudModel`: assign the four rects.
- `applyUiAction`: `ItemsDrinks -> Action::ItemsDrinks`, `Items -> Action::Items`,
  `QuestLog -> Action::QuestLog`.
- Own `HudButtons hudButtons_;` (member in `GameContext.h`), wire the render flow of §1 with
  `HudButtons::Frame` filled as: `upTimeMs`, `blockInput = blockInputTime != 0` (legacy tests the
  raw latch, `src/Hud.cpp:1238`), `combat = sys_.game->combat.active` (the rewrite's ST_COMBAT,
  `new_src/core/PlayerActions.cpp:183-193`), `zoomed = sys_.game->zoom.isZoomedIn`.

**`new_src/ui/HudView.cpp`** (non-familiar branch; replace the D3 comment `:122-127`).
Hit test BEFORE the blit (press-frame highlight, the portrait idiom `:149-155`):
```cpp
if (m.interactive && ui.buttonRect(UiId::HudShield, m.shieldHit)) res.action = UiAction::ItemsDrinks;
ui.image(ui.isActive(UiId::HudShield) ? art.shieldActive : art.shieldNormal, kShieldX, kShieldY, kTopLeft);
```
Same for keys (`UiId::HudKeys`, `QuestLog`, `ui.keys(isActive ? art.keyActive : art.keyNormal, ...)`)
and health (`UiId::HudHealth`, `Items`). Digits unchanged. Legacy art swap:
`src/Hud.cpp:685-691`, `:1174-1178`.

**`new_src/core/HudButtons.h/.cpp`** (NEW, namespace `newcore`), complete interface (G2/G3 only
fill in bodies):
```cpp
// Retained state of the HUD bottom-bar buttons: legacy Hud::isInWeaponSelect /
// weaponPressTime (src/Hud.cpp:94-96) and TouchController's touch-start origin +
// canvas->touched drag latch (src/TouchController.cpp:23, :128-134).
class HudButtons {
public:
    struct Frame {
        int64_t upTimeMs = 0;
        bool blockInput = false;   // GameContext::blockInputTime != 0
        bool combat = false;       // Game::combat.active
        bool zoomed = false;       // ZoomState::isZoomedIn
    };
    void trackPointer(const UiInput& in);   // before drawHud
    UiResult resolve(UiResult r, const Ui& ui, const HudModel& m, const Frame& f);  // after drawHud
    bool weaponSelectOpen() const { return open_; }
private:
    bool open_ = false;
    int64_t pressTime_ = 0;      // 0 = no pending press (legacy sentinel)
    int pressX_ = 0, pressY_ = 0;
    bool dragged_ = false;
};
```
`trackPointer`: on `in.pressed` store cursor in `pressX_/pressY_`, `dragged_ = false`; otherwise,
when `in.down || in.released`, set `dragged_ = true` if the cursor is outside the drag box (§2).

`resolve` in G1 (G2/G3 extend it, order matters):
1. `if (!m.interactive || m.familiar) { open_ = false; pressTime_ = 0; }` (deviation HB-D1, §4).
2. `if (f.blockInput) return {};` (`src/Hud.cpp:1238-1239`).
3. `if (r.action == UiAction::None) return r;`
4. `if (dragged_ && !m.familiar) return {};` — the familiar branch has no `touched` test
   (`src/Hud.cpp:1241-1266` vs `:1276`).
5. `open_ = false;` for every surviving action except `WeaponTap`/`WeaponSelect`
   (`src/Hud.cpp:1299-1301`).
6. `return r;` (combat needs no filter here: the queued actions hit the combat gate,
   `new_src/core/PlayerActions.cpp:187-193`, which drops everything but turning — the
   `ST_COMBAT` switch skip of `src/Hud.cpp:1303`.)

Note: steps 2 and 4 also apply to the existing soft keys and portrait (legacy gates them the same
way). Keyboard input is not routed through `resolve`.

**Acceptance G1**
- Build clean; golden frames identical.
- In play, holding the mouse on the shield plate swaps it to `Hud_Shield_Button_Active`; releasing
  on it opens the Nano Drinks list (menu 75). Health plate -> inventory root (menu 72). Keys
  panel (even with no keys) -> quest log / notebook (menu 46). Releasing outside the plate does
  nothing (highlight drops).
- In target practice each of the three exits the range instead.
- Press, drag 3+ px right (or 4+ px left/up), release on the same plate: nothing happens.
- While a script lockout runs (`blockInputTime` set, e.g. right after a level start with
  input blocked) taps do nothing; during a monster attack the menus do not open.
- In sentry-bot (familiar) mode these plates are not drawn and nothing new reacts.
- Keys I / O still open 72 / 75; no key opens 46.

### G2 — weapon short tap (NextWeapon)

**`new_src/ui/UiTypes.h`**: append `UiAction::WeaponTap` (hud button 2 release,
`src/Hud.cpp:1324-1342`).

**`new_src/core/GameStates.h`**: append `Action::HudNextWeapon` — "the HUD weapon tap's
ACTION_NEXTWEAPON. A separate touch-only action so key Z keeps its zoom-only meaning (ADR 0045)."

**`new_src/ui/HudView.cpp`**: replace the weapon blit:
```cpp
if (m.interactive && ui.buttonRect(UiId::HudWeapon, m.weaponHit)) res.action = UiAction::WeaponTap;
ui.weaponIcon(ui.isActive(UiId::HudWeapon) ? art.weaponActive : art.weaponNormal,
              kWeaponX, kWeaponY, kWeaponRowH, m.weapon);
```
(`src/Hud.cpp:1114-1119` swaps the sheet; ammo digits unchanged.)

**`new_src/core/HudButtons.cpp`** `resolve`, insert:
- After step 2: press stamp — `if (ui.in().pressed && ui.isActive(UiId::HudWeapon)) pressTime_ = f.upTimeMs;`
  (`src/Hud.cpp:1291-1295`).
- Replace step 5/6 tail with:
  ```cpp
  if (r.action != UiAction::WeaponTap && r.action != UiAction::WeaponSelect) open_ = false;  // :1299-1301
  if (f.combat && r.action != UiAction::WeaponSelect) return {};                            // :1303
  if (r.action == UiAction::WeaponTap) {
      if (f.zoomed) return r;                      // :1325-1327 -> unzoom in PlayerActions
      const bool quick = pressTime_ != 0 && f.upTimeMs - pressTime_ < kLongPressMs;  // :1329
      pressTime_ = 0;
      if (!quick) return {};                       // long press already handled / expired
      if (open_) { open_ = false; return {}; }     // :1333-1335 tap closes the overlay
      return r;                                    // :1337
  }
  return r;
  ```

**`new_src/core/GameContext.cpp`** `applyUiAction`: `WeaponTap -> Action::HudNextWeapon`.

**`new_src/core/PlayerActions.cpp`**
- Just before `if (zoomed && env_.zoom->handle(a)) return;`:
  `if (zoomed && a == Action::HudNextWeapon) a = Action::BackKey;` — legacy routes the zoomed
  weapon tap to `handleZoomEvents(ACTION_BACK)` (`src/Hud.cpp:1325-1327`), which is
  `ZoomController::handle(BackKey)` -> `zoomOut` (`new_src/core/ZoomController.cpp:79-82`).
- Monster-gate exemption: add `a != Action::HudNextWeapon` to the TurnLeft/TurnRight test
  (`src/PlayingInputHandler.cpp:72` exempts NEXT/PREVWEAPON). The animation gate still applies
  (`src/PlayingInputHandler.cpp:25-27`).
- New arm (`src/PlayingInputHandler.cpp:122-136`):
  ```cpp
  case Action::HudNextWeapon: {
      const int before = p.ce.weapon;
      if (before == Enums::WP_ITEM) break;                       // weapon 14: no-op, no help bit
      p.selectNextWeapon();
      if (p.ce.weapon != before && env_.loc != nullptr && p.activeWeaponDef != nullptr) {
          env_.hud->addMessage(env_.loc->get(kTextIngame, p.activeWeaponDef->longName),
                               0xAA000000, Hud::kMsgFlagForce);   // addMessage(1, longName, 1)
      }
      p.helpBitmask |= 0x100;
      break;
  }
  ```
  `composeText(1, longName)` is the verbatim string of text group 1 (`src/Hud.cpp:150-157`,
  `src/Text.cpp:281-300`), hence `get` without `titleOf`. No `advanceTurn`: no turn is spent.
- Update the `default:` shortcut comment (`:462-463`): selectNextWeapon IS ported now; only
  `selectPrevWeapon` and the key bindings remain unported.

**Acceptance G2**
- With 2+ usable weapons: a quick click on the weapon panel highlights it while held and switches
  to the next weapon on release; the HUD message shows its name; monsters do not get a turn.
  Weapons with no ammo are skipped (except usage 0 and weapon 2); wraps to the lowest.
- Holding the button 300 ms or longer and releasing: no weapon change (G3 makes it open the
  overlay).
- Zoomed (scoped rifle): a tap only unzooms, weapon unchanged.
- Holding the bot item (weapon 14): tap does nothing.
- Z / X unchanged (zoom in/out only while scoped, nothing otherwise).

### G3 — long-press weapon-select overlay

**`new_src/ui/UiTypes.h`**
- `UiId`: append after `HudSoftRight` (inside the HUD block) `HudWeaponSel0 ... HudWeaponSel14`
  (15 ids, one per weapon id, legacy `m_weaponsButtons` `src/Hud.cpp:88-92`).
- `UiAction`: append `WeaponSelect` — index = weapon id (`src/Hud.cpp:1281-1288`).

**`new_src/ui/HudModel.h`**
```cpp
bool weaponSelect = false;        // legacy Hud::isInWeaponSelect
int  weaponSelectMask = 0;        // ~disabledWeapons & weapons, bits 0..14 (src/Hud.cpp:1183)
int  weaponSelectAmmo[15] = {};   // ammo[ammoType(i)] per weapon, for the icon digits
```

**`new_src/core/GameContext.cpp`**
- `buildHudModel`: `m.weaponSelect = hudButtons_.weaponSelectOpen() && !p.isFamiliar;`
  `m.weaponSelectMask = p.weapons & ~p.disabledWeapons & 0x7FFF;` ammo per `i < 15` with the same
  `tables->weaponDef(i).ammoType` guard used for `m.ammo` (`:1045-1048`).
- `applyUiAction`, direct (not queued):
  ```cpp
  case UiAction::WeaponSelect:   // src/Hud.cpp:1281-1288: no ammo check, no message, no turn
      sys_.player->selectWeapon(index, sys_.defs != nullptr
          ? sys_.defs->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, index) : nullptr);
      return;
  ```
  (`selectWeapon` already ignores the call while familiar, `new_src/domain/game/Player.cpp:355`;
  legacy's zoomOut is moot, the overlay cannot be open while zoomed.)
- `PlayerActions::Env` gets `std::function<bool()> weaponSelectOpen;` wired as
  `[this] { return hudButtons_.weaponSelectOpen(); }` in the `actEnv` block (`:107-118`).

**`new_src/core/PlayerActions.cpp`**: right after the animation gate
(`if (!zoomed && gateBlocked) return;`):
`if (env_.weaponSelectOpen && env_.weaponSelectOpen()) return;` (`src/PlayingInputHandler.cpp:67-69`;
keyboard included, as in legacy — this state is only reachable by touch). The combat branch stays
first (legacy ST_COMBAT has its own handler, `src/InputEventController.cpp:364-372`).

**`new_src/core/HudButtons.cpp`** `resolve`, insert after the press stamp (legacy `Hud::update`,
`src/Hud.cpp:1365-1378`):
```cpp
if (pressTime_ != 0 && ui.isActive(UiId::HudWeapon) && ui.hover(m.weaponHit) &&
    f.upTimeMs - pressTime_ >= kLongPressMs) {
    if (!f.combat && !f.zoomed) open_ = true;
    pressTime_ = 0;
}
```
`hover` stands in for legacy `highlighted`, which `handleUserMoved` re-evaluates under the finger
(`src/Hud.cpp:1214-1235`). And before the step-5 close: `if (r.action == UiAction::WeaponSelect)
{ open_ = false; return r; }` (no combat gate, `src/Hud.cpp:1281-1288` precedes `:1303`).
A release on empty space returns `None`, so the overlay stays (HB §2).

**`new_src/ui/HudView.cpp`**: at the top of `drawHud`, after the `showBottomBar` early-out and
before the soft keys (legacy draws the overlay before `drawBottomBar`, `src/Hud.cpp:772-782`):
```cpp
if (m.weaponSelect) {
    ui.panel({ 0, 20, 480, 300 }, 0x80000000);                       // :1187
    int k = 0;
    for (int i = 0; i < 15; ++i) {
        if (((m.weaponSelectMask >> i) & 1) == 0) continue;
        const UiRect r{ 28 + 106 * (k % 4), 208 - 48 * (k / 4), 102, 44 };   // :1184-1209
        ++k;
        const UiId id = static_cast<UiId>(static_cast<int>(UiId::HudWeaponSel0) + i);
        if (m.interactive && ui.buttonRect(id, r)) { res.action = UiAction::WeaponSelect; res.index = i; }
        ui.weaponIcon(ui.isActive(id) ? art.weaponActive : art.weaponNormal, r.x, r.y, kWeaponRowH, i);
        if (i != 1) ui.number3(art.numbers, r.x + 57, r.y + 8, kDigitSpace,
                               m.weaponSelectAmmo[i], i == 13);
    }
    ui.image(art.panelBottom, 0, 320 - art.panelBottom.height(), kTopLeft);  // src/TouchController.cpp:544-545
}
```
Name the literals as `constexpr` next to the existing geometry block with the §2 citations.
The panel repaint reproduces the legacy order (dim, icons, then the bar background on top); the
HUD widgets that follow are unchanged.

**Acceptance G3**
- Hold the weapon panel ~0.3 s: the screen below the top bar darkens to 50 %, owned weapons
  appear as weapon-panel icons with ammo, 4 per row, the first row just above the bottom bar,
  further rows stacked upward, the row centred at x = 28. The bottom bar is NOT darkened.
- Release after the long press: overlay stays, weapon unchanged.
- Click an icon: it highlights while held, release equips that weapon (even with 0 ammo), no HUD
  message, overlay closes.
- Quick tap on the weapon panel while open: closes it, weapon unchanged.
- Tap shield/health/keys/portrait/soft keys while open: overlay closes and that button acts.
- Click on empty darkened area: nothing; arrow keys / Enter / C do nothing while open.
- Long press during a monster attack or while zoomed: no overlay.
- Golden frames identical.

## 4. Deviations

- **HB-D1** — the overlay is force-closed (and the press timer cleared) whenever the HUD is not
  interactive (menu, dialog, loot, automap, camera...) or the player is familiar. Legacy keeps
  `isInWeaponSelect` across such states and would redraw it on return; since every playing action
  is swallowed while it is open, only script-driven state changes can reach this case.
- **HB-D2** — the long-press poll runs in the render pass, right after the view, instead of in
  `Hud::update` of the playing tick (`src/GameStateRunner.cpp:196`); the overlay appears at most
  one frame later.
- **HB-D3** — while `blockInputTime` is set the pressed widget still shows its `*_Active` sheet
  (the `Ui` latch is set by the view); legacy also refuses the highlight (`src/Hud.cpp:1238`).
  No action fires.
- The drag latch and `blockInputTime` gate now also cover the soft keys and the portrait
  (legacy behaviour; previously absent).
