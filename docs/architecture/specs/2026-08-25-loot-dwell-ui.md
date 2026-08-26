# Spec 2026-08-25 — Interactive loot dwell + loot menu UI (ST_LOOTING)

Status: DESIGN (ready to implement). Normative sources:
`docs/original-code/loot-inventory.md` §2 + **§2.6** (port-ready facts),
`docs/research/2026-08-25-camera-pitch-loot.md` (crouch/stand pose — already implemented),
raw disassembly of the UI path in `src/LoothingSystem.cpp` (quoted below with file:line).
Current-code citations are `new_src/<file>:<line>`.

---

## 1. Goal (user-facing)

`E` on a lootable corpse → 500 ms crouch with camera pitch-down (**done**) → the game
**waits**, showing the red top-bar loot list ("Looted Items:") over the frozen crouch pose
→ `FIRE` pages / closes per the legacy rule → grant happens **on close** → 500 ms stand-up →
`advanceTurn()` at stand-up expiry. An empty corpse shows exactly one line (common str 228
"None found!") and one press closes it. Looting consumes one turn.

## 2. Legacy contract being replicated (all verbatim from §2.6 + source)

### 2.1 Session timeline

`setState(ST_LOOTING)` **then** `poolLoot(corpsePos)` in that order
(`src/PlayingInputHandler.cpp:374-378`) — corpse(s) marked looted and the display list built
at ENTRY, not at close. Phase A crouch lerp 500 ms (`src/LoothingSystem.cpp:42-51`);
at settle sound 1055 plays once per session (latch `field_0xac5_`, set in
`onEnterLooting` `:32`, checked `:62-65`). Then the **dwell**: the settled crouch pose is
rewritten every frame (`:66-72`) and `drawLootingMenu` paints the list (`:121-150`).
Close (input) → `giveLootPool()` runs **before** standing up, `crouchingForLoot=false`,
`lootingTime=app->time` restarts the clock with zero extra delay (`:89-103`). Phase B
stand-up is another 500 ms; on expiry snap + `setState(ST_PLAYING)` + `advanceTurn()`
(`:74-80`).

### 2.2 Pooling (`poolLoot`, `src/LoothingSystem.cpp:154-278`)

Walk the whole tile chain at the faced corpse's tile (`findMapEntity … nextOnTile`);
process **every** `eType == 9` entity on it (not just the faced one):

- prop (`monster == nullptr`): `param != 0` → skip entity; else `++param`.
- monster corpse: `flags & 0x800` → skip; else `|= 0x800`.
- `info |= 0x400000` on every newly-marked entity (`:179`).
- Copy entries into `lootPool[3]`, stopping at the first zero `lootSet[i]` slot:
  - class 6 (flavor string): deduped by `entry & 0xFFF` against pooled class-6 entries;
    occupies a pool slot; **never granted**. Legacy quirk preserved verbatim: the dedupe
    loop tests the class bit on `entity->lootSet[j]` (the SOURCE entity) where
    `lootPool[j]` was meant (`src/LoothingSystem.cpp:186-194`; documented harmless quirk,
    loot-inventory.md §2.4).
  - class 0 idx 24 → `credits += cnt`; idx 25 → `credits += cnt*100` (folded, no slot).
  - others: merge by `entry >> 6` equality into an existing slot, saturating the count
    `(existing & 0x3F) + cnt) & 0x3F`; else push a new slot. `numLootItems` counts each
    non-class-6 entry pre-merge (stat only).
- Build `lootText` (one buffer, `|` separators) per pool entry:
  - class 6: `'\x88'` + map-string `entry & 0xFFF` (text-type `loadMapStringID`;
    map00 ⇒ type 4 = `kTextMap`, `src/LoadingManager.cpp:310-311`).
  - class 1 (weapon): args `{'\x88', longName}` → common str **91** `%01%02|`.
  - everything else: args `{'\x88', cnt, longName}` → common str **90** `%01%02x %03|`.
  - credits (if `credits != 0`): args `{'\x88', credits, str157}` → str **90**
    ⇒ `<icon> N x UAC Credits`. Name is **type-1** string 157
    (`addTextArg((short)1,(short)157)`, `src/Text.cpp:269-274`).
  - no entries and no credits: compose common str **228** alone.
- `dehyphenate()` strips ALL `-` from the buffer (`src/Text.cpp:714-725`), then
  `lootPoolIndices[18]` records `<start,len>` per line splitting at `|`, last pair covers
  the tail (`:263-278`). `lootLineNum = 0`.

Verified strings (strings.idx + strings00.bin, loot-inventory.md §2.6):
`type0[90]="%01%02x %03|"`, `type0[91]="%01%02|"`, `type0[227]="Loot-ed Items:"`,
`type0[228]="None found!|"`, `type1[157]="UAC Cre-dits"`.

### 2.3 Input (`handleLootingEvents`, `src/LoothingSystem.cpp:85-117`)

Guard drops EVERYTHING outside the dwell window:
`crouchingForLoot && app->time > lootingTime + 500` (`:87`) — blocks during crouch
(clock not expired) and during stand-up (`crouchingForLoot` already false).
Line count `n = numPoolItems + (credits ? 1 : 0)`; scroll bound `max = max(n-3, 0)`.

| Legacy action | Effect |
|---|---|
| ACTION_FIRE | `lootLineNum >= max` → close (`lootingTime=now; crouchingForLoot=false; giveLootPool()`); else page `lootLineNum = min(lootLineNum+3, max)` |
| ACTION_PASSTURN / ACTION_BACK | close immediately, any page |
| ACTION_DOWN / ACTION_UP | `lootLineNum ± 1` clamped to `[0, max]` |
| ACTION_LEFT / ACTION_RIGHT | jump to `0` / `max` |

Any other action id is ignored. KEY_CLR reaches the handler as ACTION_BACK
(`AVK_CLR` mapping, `src/InputEventController.cpp:25`) ⇒ closes.

### 2.4 Menu geometry & paint (`drawLootingMenu`, `src/LoothingSystem.cpp:121-150`)

Drawn by the loot system itself — **not** DialogSystem (no style table; own rects/colors).
Paint order: 3D view → HUD → loot overlay (`src/Canvas.cpp:414-417,469-472`), drawn only
while dwelling (`:121-122`). On 480×320: `viewRect = {0,20,480,250}` (y=20 hardcoded,
`src/Canvas.cpp:124-127`), `SCR_CX = 240`.

```
dialogRect = { 0, 20+16, 480-0-1, 48 }                  ⇒ {0, 36, 479, 48}   (:123-127)
fillRect(body)   0xFF660000 dark red                                          (:128-129)
fillRect({x, y-18, w, 18}) 0xFF000000 black title bar                         (:130-131)
drawRect(title) + drawRect(body) 0xFFFFFFFF white border                      (:132-134)
title: compose str227 → dehyphenate → drawString(240, y-16, anchor HCENTER=1) (:135-139)
lines i=0..2: drawString(lootText, x+5, y+1+i*16, anchor 20=TOP|LEFT,
             offset=lineIndex[2*(i+lootLineNum)], len=lineIndex[…+1])          (:140-144)
scrollbar if n > 3: drawScrollBar(x+w, y+1, h-1, lootLineNum,
             min(lootLineNum+3, n), n, 3)                                      (:145-150)
```

Text is white (palette row 0); each line starts with glyph `\x88` rendered as one font
cell — 0x88 falls through `getCharIndices` to the default index
(`src/Graphics.cpp:583-604,637-659`). Scrollbar (`src/Canvas.cpp:1284-1314`): arrow caps =
imgUIImages regions `[60,0,7,7]` / `[60,7,7,7]`, track fill `0xFFB3AA93`, thumb
`0xFFE7CFAD`, black outlines; hidden when total ≤ page.

### 2.5 Grant (`giveLootPool`, `src/LoothingSystem.cpp:281-307`)

Per pool entry (class 6 skipped): `player->give(class, idx, count)`; weapon entries add
starter ammo `max(usage,10)` of `weapons[idx*9+AMMOTYPE]` (`:290-296`); credits via
`give(0,24,credits)`; `foundLoot(corpseTile…, numLootItems)` bumps the run stat
(`src/Game.cpp:3541-3543`); counters reset + buffer disposed. An empty corpse still runs
the whole close path (grants nothing, `+=0` stat, turn consumed via stand-up).

### 2.6 HUD / softkeys during ST_LOOTING

Nothing hidden; `hud->repaintFlags |= 0x22` re-armed per frame
(`TOP_BAR|HUD_OVERDRAW`, `src/Hud.h:21,25`, `src/LoothingSystem.cpp:38`); soft keys
cleared on entry (`:28`), restored by `setState(ST_PLAYING)`.

---

## 3. Design decisions

### A. Where the menu drawing lives → GameContext-private method (NOT Hud, NOT DialogSystem)

`void GameContext::drawLootingMenu(Graphics2D& g)` called from `GameContext::render()`
after the message/dialog overlays, gated internally by the legacy dwell predicate.

Why: legacy paints from `LootingSystem` itself with its own rects/colors
(`src/LoothingSystem.cpp:121-150`) — neither a Hud widget nor a DialogSystem style. The
rewrite's LootingSystem analog *is* the loot block in GameContext (ADR 0006), so the
painter lives there too. Text rendering reuses the proven machinery, not a new renderer:
one `Text` buffer + `<start,len>` line table drawn via
`Graphics2D::drawString(font, text, x, y, flags, 16, strBeg, strEnd)` — exactly how
DialogSystem draws its wrapped buffer (`new_src/domain/game/DialogSystem.cpp:497-526`,
sub-range support at `new_src/render/Graphics2D.h:68-69`). The scrollbar is NOT
duplicated: `DialogSystem::drawScrollBar` is the ported `Canvas::drawScrollBar`
(`new_src/domain/game/DialogSystem.cpp:579-595` — identical constants `0xFFB3AA93` /
`0xFFE7CFAD`, imgUIImages caps regions, thumb formula) and legacy loot called that same
shared Canvas implementation (`src/LoothingSystem.cpp:146-150`), so we merely make it
**public** and call `sys_.dialogs->drawScrollBar(...)`.

### B. Input routing → Looting branch in the pendingActions_ loop (mirror Dialog)

In `GameContext::tick()`'s indexed action loop (`new_src/core/GameContext.cpp:149-163`),
insert after the `StateId::Camera` branch and before the `blocked || state != Playing`
break:

```cpp
if (state == StateId::Looting) { handleLootingAction(a); continue; }
```

This mirrors the Dialog dispatch (`:151-153`) — ST_DIALOG consumes queued keys the same
way, movement never leaks. Routing sits *before* the blocked/break check like Dialog, so
the queue-drain semantics stay uniform; the legacy dwell-window guard is enforced inside
the handler (§4.3), reproducing `src/LoothingSystem.cpp:87` exactly (drops during both
transition windows). Action mapping (keyboard already wired in
`new_src/core/GameLoop.cpp:39-47`, same mapping Dialog uses):

| Action enum | Legacy loot action | Effect |
|---|---|---|
| `Action::Use` (E) | ACTION_FIRE | page ×3 / close on last page |
| `Action::Forward` (W/↑) | ACTION_UP | `topLine = max(topLine-1, 0)` |
| `Action::Back` (S/↓) | ACTION_DOWN | `topLine = min(topLine+1, max)` |
| `Action::TurnLeft` (A/←) | ACTION_LEFT | `topLine = 0` |
| `Action::TurnRight` (D/→) | ACTION_RIGHT | `topLine = max` |
| `Action::Passturn` (TAB) | ACTION_PASSTURN | close + grant |
| `Action::BackKey` (BACKSPACE) | ACTION_BACK (AVK_CLR) | close + grant |
| `Menu` / `Automap` / `None` | — | ignored |

### C. State model → dwell is a phase of the existing loot clock, no new enum

Keep `lootCrouch_` as the phase selector and let the **dwell be the settled crouch**:
`lootCrouch_ == true && upTimeMs > lootTime_ + kLootPhaseMs` (strictly `>`, like legacy).
This one predicate drives the menu draw, the input guard and the pose hold — no extra
"dwellActive" flag to desync.

Changes to `tickLooting()` (`new_src/core/GameContext.cpp:438-496`):

- Crouch lerp branch: unchanged.
- **Settle branch** (today: grants + flips to stand-up): keep writing the settled pose
  every tick (already does), play sound 1055 once via a session latch
  `lootSettleSfx_` (analog of `field_0xac5_`, set false in `enterState_`), log
  `[loot] sound 1055` — and **do nothing else**. `lootCrouch_` stays true; the clock is
  NOT restarted.
- **Stand-up expiry branch**: unchanged (snap home, `setState(Playing)`,
  `advanceTurn()`) — `new_src/core/GameContext.cpp:486-495` already correct.

New `closeLootSession()` implements the legacy close ordering
(`src/LoothingSystem.cpp:89-103`): grant → `lootCrouch_ = false` → `lootTime_ = upTimeMs`
(stand-up starts immediately, no extra delay). Grant-on-close ordering is therefore
structural: the pool is granted exactly once, from the input handler, before any
stand-up tick runs.

Field changes in `GameContext.h` (replace the `pendingLootCorpse_` block,
`:184-191`):

```cpp
static constexpr int kLootPhaseMs = 500;  // LOOTING_CROUCH_TIME (src/Canvas.h:46);
                                          // promoted from tickLooting's local constant
// Loot dwell session (legacy LootingSystem field analogs).
bool lootSettleSfx_ = false;              // field_0xac5_: sound 1055 once per session
Game::LootPool lootPool_;                 // pooled entries + lootText + lineIndex + lootLineNum
```

Delete `Entity* pendingLootCorpse_` (its "granted at crouch settle" contract is obsolete;
pooling happens at entry, granting from the pool struct). `GameContext.h` gains
`#include "domain/game/Game.h"` (needed for the nested `LootPool` by value; no include
cycle — Game.h is self-contained).

### D. Pooling/marking → `Game::poolLootCorpse`; `lootCorpse` becomes grant-only and is replaced

Game owns entities/tile-lists/defs (`new_src/domain/game/Game.h:249-253`), so the tile-chain
walk and mark-looted writes belong there. Because the legacy `poolLoot` also composes
`lootText` (it had app access), and the rewrite keeps that pairing, `poolLootCorpse`
returns the fully populated display struct; GameContext stays presentation + input.

```cpp
// Game.h — replaces Game::lootCorpse entirely.
struct LootPool {
    static constexpr int kMaxLines = 9;      // lootPoolIndices[18] / 2 pairs
    int entries[Entity::kMaxCorpseLoot] = { 0, 0, 0 }; // packed u16 (cls<<12|idx<<6|cnt)
    int numEntries = 0;                      // numPoolItems (incl. class-6 flavor lines)
    int numItems   = 0;                      // numLootItems (stat only, counts pre-merge)
    int credits   = 0;                       // lootPoolCredits
    Text text;                               // lootText: '|'-separated lines
    short lineIndex[2 * kMaxLines] = { 0 };  // lootPoolIndices: <start,len> per line
    int topLine = 0;                         // lootLineNum (scroll pos, reset by pool)
    static int lineCount(const LootPool& p) { return p.numEntries + (p.credits != 0); }
};

// Mark-looted + pool + compose the loot list for ALL eType==9 entities on tile
// (tx,ty) (src/LoothingSystem.cpp:154-278). Marks BEFORE reading loot sets,
// per entity: prop ++param (skip when already != 0), monster flag 0x800
// (unified into ++param — see Deviations #1), info |= kInfoActivated.
void poolLootCorpse(int tx, int ty, const Localization& loc, LootPool& out);

// Grant pass (src/LoothingSystem.cpp:281-307): give() per non-class-6 entry,
// weapon starter ammo max(usage,10) of tables.weaponData[idx*9+4], credits
// give(0,24,credits), foundLoot stderr stub, resets pool counters + text.
void giveLootPool(LootPool& pool, Player& player, const Tables* tables);
```

`Game::lootCorpse` (`new_src/domain/game/Game.cpp:727-820`, decl `Game.h:211`) is
**deleted** together with its center-message/toast tail and its `Hud&` parameter — the
list UI replaces the toast, and legacy gives no post-close HUD message on corpse loot.
The file-local helpers `composeArgs` (`Game.cpp:700-715`) and `itemLongName`
(`Game.cpp:717-722`) survive and are reused by the composer. Callers updated:
only `GameContext.cpp:484` references `lootCorpse` today.

Entry gate unchanged: `handlePlayingAction`'s Use branch keeps
`findLootableCorpseFacing` (unlooted + linked + `hasLootSet`) and just calls
`setState(StateId::Looting)` — the pool happens in the `enterState_` hook, preserving the
legacy `setState` → `poolLoot` order (`src/PlayingInputHandler.cpp:374-378`).

### E. Strings — all from Localization; icon glyph needs zero new code

- `io/Tables.cpp` stores **numeric** tables only (`new_src/io/Tables.cpp:7-47`) — no
  strings live there; the "entity-table" type-1 strings arrive through the same
  `Localization` store (`strings.idx` + `stringsNN.bin`, loaded in
  `new_src/core/Main.cpp:86-92`: `kTextMain`, `kTextIngame`, `kTextMap(+mapId)` all
  loaded at boot). No loader work.
- Sources: `loc.get(kTextMain, 90/91/227/228)`, `loc.get(kTextIngame, 157)`;
  item names via the existing pattern
  `Localization::titleOf(loc.get(kTextIngame, def->longName))` after
  `defs.find(Enums::ET_ITEM /*=6*/, cls, idx)` (`Game.cpp:717-722`). Class-6 flavor:
  `loc.get(kTextMap, entry & 0xFFF)` (same convention as ScriptVM MESSAGE/DIALOG,
  `new_src/domain/game/ScriptVM.cpp:370`).
- Glyph `\x88`: `Font::getCharIndices` falls through to the default index
  (`new_src/text/Font.cpp:12-77` → cell 103, in-range) — the identical fall-through the
  legacy relies on (`src/Graphics.cpp:583-604,637-659`); `Graphics2D::drawString` renders
  it as a plain 9px-advance char (`new_src/render/Graphics2D.cpp:204-216`), and
  `Text::getStringWidth` counts it 9px (`new_src/text/Text.cpp:257-258`). Append the raw
  byte `'\x88'` as the first compose argument; no special casing.
- Composition: build each line into a scratch `std::string` via the existing
  `composeArgs` (format strings end in `|`, `%NN` eats two digits — `Game.cpp:700-715`),
  append to `LootPool::text`, then ONE `text.dehyphenate()` over the whole buffer, then
  split into `lineIndex` exactly like `DialogSystem::prepareDialog`'s splitter
  (`DialogSystem.cpp:164-180`). Order matters: dehyphenate BEFORE recording offsets
  (legacy order, `src/LoothingSystem.cpp:260-278`).

### F. Data flow

```
E (Use) — handlePlayingAction (Playing only, idle-gated)
  └─ Game::findLootableCorpseFacing(viewX,viewY,stepX,stepY) != nullptr
       └─ setState(Looting)
            ├─ enterState_: cache pose/facing/steps, lootTime_=now, lootCrouch_=true,
            │   lootSettleSfx_=false
            └─ Game::poolLootCorpse(facedTile) → lootPool_     [marks ALL eType-9 on tile]

tick (Looting, one 15 ms quantum)
  ├─ t < 500 ms           : crouch lerp pose (unchanged formulas)
  ├─ settle (t ≥ 500, crouching): hold settled pose + "[loot] sound 1055" once (latch)
  │                          → DWELL: menu drawn, input live
  └─ t ≥ 500, standing    : mirror lerp → snap → setState(Playing) → advanceTurn()

input — pendingActions_ loop: state==Looting → handleLootingAction(a)
  ├─ guard fails (crouch/stand-up windows) → drop
  ├─ Use:   topLine < max ? page +3 (clamp max) : closeLootSession()
  ├─ Passturn / BackKey: closeLootSession()
  └─ Up/Down: ±1 clamp [0,max];  Left/Right: 0 / max

closeLootSession(): Game::giveLootPool(lootPool_) → lootCrouch_=false → lootTime_=now

render(): world → overlays → drawLootingMenu(g)
  └─ dwell-guard → red body {0,36,479,48} + black title bar {0,18,479,18} + white
     borders + centered str227 + 3 line slots (sub-ranges of lootPool_.text) +
     DialogSystem::drawScrollBar when lineCount > 3
```

---

## 4. File-by-file change list

No new files ⇒ no CMake reconfigure needed (GLOB project; adding files would require
`cmake -S . -B build_new`).

### GROUP 1 — domain (do first; GROUP 2 compiles against it)

**`new_src/domain/game/Game.h`**

1. Add `#include "text/Text.h"`.
2. Add `struct LootPool` (public, nested in `Game`) per §3-D.
3. Replace the `lootCorpse` declaration + doc comment (`Game.h:202-212`) with:

```cpp
void poolLootCorpse(int tx, int ty, const Localization& loc, LootPool& out);
void giveLootPool(LootPool& pool, Player& player, const Tables* tables);
```

**`new_src/domain/game/Game.cpp`**

4. Delete `Game::lootCorpse` (`Game.cpp:724-820`). Keep `composeArgs`, `itemLongName`.
5. Implement `poolLootCorpse` — walk `findMapEntity(tx, ty)` chain; for each
   `isCorpse()` entity apply §2.2 exactly (mark-before-read, `info |=
   Entity::kInfoActivated`, first-zero-slot break, class-6 dedupe with the verbatim
   `entity->lootSet[j]` quirk, credit folds before the merge scan, saturated merges).
   Reset `out` fields + `out.text.setLength(0)` + `out.topLine = 0` FIRST (legacy
   `:158-162`). Compose lines (§3-E), `out.text.dehyphenate()`, zero `lineIndex`, split
   at `|` recording `<start,len>` pairs + tail pair. Log one summary:
   `std::fprintf(stderr, "[loot] pooled tile=%d,%d entries=%d items=%d credits=%d\n", …)`
   (keeps `[loot]` convention).
6. Implement `giveLootPool` — §2.5 verbatim; starter-ammo lookup copies the proven block
   (`Game.cpp:771-776`: `tables->weaponData[idx*9+4]` AMMOTYPE, `[idx*9+5]` AMMOUSAGE,
   `player.give(2, ammoType, std::max(usage, 10))`); per-grant log
   `[loot] give class=%d idx=%d cnt=%d` + `[loot] credits=%d` +
   `[loot] foundLoot items=%d` stub; finish with
   `pool.numEntries = pool.numItems = pool.credits = 0; pool.text.setLength(0);`
   (dispose analog). `tables` may be null (skips starter ammo), like before.

### GROUP 2 — core + ui

**`new_src/ui/DialogSystem.h`**

7. Move `drawScrollBar` from `private:` to `public:` (one-line relocation with comment:
   "shared Canvas::drawScrollBar port; also used by the loot overlay, like legacy
   `src/LoothingSystem.cpp:146-150`"). No `.cpp` change.

**`new_src/core/GameContext.h`**

8. `#include "domain/game/Game.h"`.
9. Promote `static constexpr int kLootPhaseMs = 500;` to the public constants block
   (next to `kTickMs`); delete the local constant in `tickLooting`.
10. Replace `Entity* pendingLootCorpse_` with `bool lootSettleSfx_ = false;` +
    `Game::LootPool lootPool_;` (§3-C comment block).
11. Add private methods:

```cpp
void handleLootingAction(Action a);  // src/LoothingSystem.cpp:85-117
void closeLootSession();             // grant + stand-up restart (:89-103)
void drawLootingMenu(Graphics2D& g); // src/LoothingSystem.cpp:121-150
```

(`Graphics2D` is forward-declared already via render includes; add `class Graphics2D;`
to the fwd-decl block if not present.)

**`new_src/core/GameContext.cpp`**

12. `enterState_` case `Looting` (`:87-102`): keep everything; add
    `lootSettleSfx_ = false;` and after the pose cache:

```cpp
int tx = (lootDestX_ + lootStepX_ * 64) >> 6;
int ty = (lootDestY_ + lootStepY_ * 64) >> 6;
sys_.game->poolLootCorpse(tx, ty, *sys_.loc, lootPool_);
```

13. Tick loop (`:149-163`): insert the Looting dispatch (§3-B) between the Camera branch
    and the `blocked || state != Playing` break.
14. `tickLooting` (`:438-496`): use `kLootPhaseMs`; settle branch becomes
    pose-write + `if (!lootSettleSfx_) { lootSettleSfx_ = true; std::fprintf(stderr,
    "[loot] sound 1055\n"); }` — remove the grant + `lootCrouch_/lootTime_` flip.
    Stand-up branch untouched. Update the stale header comment ("minus the loot-list UI
    dwell" → describe the dwell).
15. `handlePlayingAction` Use branch (`:760-774`): drop `pendingLootCorpse_ = corpse;`;
    keep the gate + `setState(StateId::Looting)`; refresh the comment (pooling in
    `enterState_`, grant on close, turn at stand-up expiry).
16. New `handleLootingAction` / `closeLootSession` per §3-B/§3-C:

```cpp
void GameContext::handleLootingAction(Action a) {
    if (!lootCrouch_ || upTimeMs <= lootTime_ + kLootPhaseMs) return;  // (:87)
    int maxLine = std::max(Game::LootPool::lineCount(lootPool_) - 3, 0);
    switch (a) {
    case Action::Use:                                   // ACTION_FIRE
        if (lootPool_.topLine >= maxLine) closeLootSession();
        else lootPool_.topLine = std::min(lootPool_.topLine + 3, maxLine);
        break;
    case Action::Passturn:
    case Action::BackKey:                closeLootSession(); break;
    case Action::Forward:  lootPool_.topLine = std::max(lootPool_.topLine - 1, 0); break;
    case Action::Back:     lootPool_.topLine = std::min(lootPool_.topLine + 1, maxLine); break;
    case Action::TurnLeft:  lootPool_.topLine = 0; break;
    case Action::TurnRight: lootPool_.topLine = maxLine; break;
    default: break;                                     // other ids ignored
    }
}

void GameContext::closeLootSession() {
    sys_.game->giveLootPool(lootPool_, *sys_.player, sys_.tables);
    lootCrouch_ = false;
    lootTime_ = upTimeMs;                 // stand-up starts now, zero extra delay
}
```

17. New `drawLootingMenu(Graphics2D&)` per §3-A + §2.4 geometry, with file-local
    `fillArgb`/`rectArgb` helpers (mirror `DialogSystem.cpp:36-42`):

```cpp
void GameContext::drawLootingMenu(Graphics2D& g) {
    if (!(lootCrouch_ && upTimeMs > lootTime_ + kLootPhaseMs)) return; // (:121-122)
    if (lootPool_.text.length() == 0 || sys_.font == nullptr) return;
    constexpr int kViewY = 20;            // viewRect[1] (src/Canvas.cpp:124-127)
    constexpr int kScrCx = 240;           // Canvas::SCR_CX
    const int dx = 0, dy = kViewY + 16, dw = 480 - 1, dh = 48;   // dialogRect (:123-127)
    fillArgb(g, dx, dy, dw, dh, 0xFF660000u);                    // body (:128-129)
    fillArgb(g, dx, dy - 18, dw, 18, 0xFF000000u);               // title bar (:130-131)
    rectArgb(g, dx, dy - 18, dw, 18, 0xFFFFFFFFu);               // (:132-133)
    rectArgb(g, dx, dy, dw, dh, 0xFFFFFFFFu);                    // (:134)
    Text title;                                                  // (:135-139)
    title.append(sys_.loc->get(kTextMain, 227));
    title.dehyphenate();
    g.drawString(*sys_.font, title, kScrCx, dy - 16, Graphics2D::kAnchorHCenter, 16);
    for (int i = 0; i < 3; ++i) {                                // (:140-144)
        int line = i + lootPool_.topLine;
        if (line < 0 || line >= Game::LootPool::kMaxLines) continue;
        g.drawString(*sys_.font, lootPool_.text, dx + 5, dy + 1 + i * 16,
            Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft, 16,
            lootPool_.lineIndex[line * 2], lootPool_.lineIndex[line * 2 + 1]);
    }
    int total = Game::LootPool::lineCount(lootPool_);
    if (total > 3)                                               // (:145-150)
        sys_.dialogs->drawScrollBar(g, dx + dw, dy + 1, dh - 1, lootPool_.topLine,
            std::min(lootPool_.topLine + 3, total), total, 3);
}
```

18. `render()` (`:944-947` area): after the Dialog overlay draw, add:

```cpp
// Loot list overlay during the dwell window — paints OVER world+HUD
// (src/Canvas.cpp:414-417,469-472).
if (state == StateId::Looting) drawLootingMenu(g);
```

---

## 5. Deviations (deliberate, all documented here)

1. **Monster flag 0x800 unified into `++param`** — `EntityMonster` is not ported; the
   rewrite's established unification (`Game.cpp:688-691`) is kept. Marker timing moves
   from grant-time to pool-time (this spec), matching legacy semantics for re-open
   prevention; a future monster-struct port splits them again
   (delta list item 6, loot-inventory.md §2.6).
2. **`repaintFlags 0x22` / REPAINT_HUD|VIEW3D: no-op** — the rewrite renders
   immediate-mode GL every frame and has no dirty-flag system; the top/bottom HUD bars
   are not wired into `render()` yet either (only `drawMessages`/cinematic overlay).
   Nothing to repaint; the overlay draws over whatever is on screen.
3. **Soft-key clear/restore: no-op** — no soft keys exist.
4. **Sound 1055 → stderr `[loot] sound 1055`** at crouch-settle, latched once per
   session (moves from the per-grant log at `Game.cpp:789`). No audio system yet.
5. **`foundLoot` run stat → stderr stub** (run counters absent, `Game.cpp:819` note kept).
6. **Legacy class-6 dedupe quirk preserved verbatim** (tests the class bit on the source
   entity's `lootSet[j]`, `src/LoothingSystem.cpp:186-194`) — fidelity over cleanliness.
7. **Pickup-toast strings removed from the loot flow** (str 84/85/86 were the previous
   stand-in feedback in `lootCorpse`). Legacy corpse looting has NO post-close toast —
   the list is the feedback. Those strings remain for future `touchedItem` pickup ports.
8. **FOV not widened by \|pitch\| during the crouch** — pre-existing single-projection
   deviation (`GameContext.cpp:882-885`); unchanged by this spec.
9. **Corpse sparkle not ported** (would need World3D changes — out of scope per
   constraints). Marker-at-pool-time gives a future sparkle port the correct predicate.
10. **Touch synthesis** (`keys_numeric` table → synthetic FIRE) not applicable —
    keyboard-only input.

## 6. Acceptance criteria

Code-level (reviewer):
- `grep -n lootCorpse new_src` returns nothing; `pendingLootCorpse_` gone.
- Grant executes exactly once per session, only from `closeLootSession`, and always
  before `lootTime_` restart; `advanceTurn` only at stand-up expiry.
- Marking happens in `poolLootCorpse` before any grant and for every `eType==9` entity
  on the tile, including ones whose entries were skipped.
- Input dropped during both 500 ms windows (guard predicate identical to legacy `:87`).

User-visible (user is the eyes; map00 fixtures from loot-inventory.md §6):

1. `cmake --build build_new -j 8` clean; run from `build_new/new_src`.
2. Walk to the **blue-keycard skeleton sp29 @(13,2)**, face it, press E:
   crouch+pitch-down (unchanged), then the red panel appears near the top of the screen:
   black bar with centered **"Looted Items:"**, thin white borders around bar and body,
   one white line starting with a small symbol glyph followed by **"1x Blue Key-card"**
   (wording per strings.bin; hyphens stripped). No scrollbar. Press E again → panel
   disappears during the 500 ms stand-up → back in control.
3. Walk to **imp corpse sp121 @(27,23)**: THREE lines — snake venom, caffeine block
   (flavor/named items), and **"36x UAC Credits"**; stderr shows
   `[loot] pooled tile=27,23 entries=3 … credits=36` at entry and
   `[loot] give …` ×2 + `[loot] credits=36` at close. One E closes (max=0).
4. **Flavor-only corpse** (e.g. sp72/76 family): list shows the map-string line(s),
   closing grants nothing (`[loot]` logs confirm), turn still consumed.
5. Pressing E again on any looted corpse does NOT reopen (marker at pool time) — it
   falls through to TRIGGER/door use as before.
6. W/S/A/D/arrows during the dwell shift the list one line / jump ends (no visual change
   with ≤3 lines — expected; paging+scrollbar need >3 lines which map00 cannot produce,
   so those paths are review-verified only).
7. Input during the crouch and stand-up windows (E/W/S/A/D/TAB/BACKSPACE) does nothing;
   TAB or BACKSPACE during the dwell closes+grants immediately.
8. Blue door flow intact: loot sp29 → open the blue door (keycard was really granted).
9. Existing `[script]`/`[load]` log streams unchanged; no new spam beyond the
   per-session `[loot]` lines listed above.
