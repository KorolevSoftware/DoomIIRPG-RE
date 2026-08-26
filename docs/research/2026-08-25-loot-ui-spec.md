# Loot dwell + menu UI — interaction flow, timing, pixel spec

Date: 2026-08-25 · Status: **CONFIRMED** (all claims verified against `src/`; curated port
spec appended to `docs/original-code/loot-inventory.md` §2.6)

## Hypothesis / task

User report: in the original, after the loot crouch settles the game WAITS for input and shows an
ITEM LIST DIALOG AT THE TOP ("красный диалог вверху") that pages through picked-up items, and only
then stands up. Our `new_src` version auto-grants at settle and auto-stands. Questions:
(1) exact interaction flow of `handleLootingEvents` incl. key-by-key and empty-corpse behavior;
(2) timing of `giveLootPool`/`advanceTurn` relative to UI close; (3) pixel-exact menu rendering
(position, who draws it, fill colors incl. red bar, scrollbar, text composition, string ids);
(4) HUD/state details; (5) delta list vs `new_src/core/GameContext.cpp`.

## Method

- Read `src/LootingSystem.cpp/.h` in full; traced every call site: `Canvas.cpp` (backPaint order,
  tick state machine, setState hooks, drawScrollBar, viewRect setup), `InputEventController.cpp`
  (routing + KEY_CLR interception + getKeyAction), `TouchController.cpp` (touch synthesis),
  `Hud.h/Hud.cpp` (repaint flag meanings, arrow-control gating), `Graphics.cpp/.h`
  (drawString anchors, glyph pipeline), `Text.cpp` (composeText %NN parsing, dehyphenate,
  loadTextFromIndex), `Resource.cpp/.h` (strings.idx format, byte-table loader),
  `PlayingInputHandler.cpp:315-380` (entry), `Game.cpp:3536-3543` (foundLoot).
- Asset verification: extracted `Packages/strings.idx`, `strings00.bin`, `tables.bin` from
  `build_new/new_src/Doom 2 RPG.ipa` (read-only) into a temp dir and parsed them per the loader
  code. strings.idx: u16 count=15, then 5-byte records (u8 chunk, i32 LE offset; 0xFF records only
  close previous length), final 5-byte tail closes last length — matches
  `src/Resource.cpp:169-207`. Blobs are NUL-split per table (`src/Text.cpp:179-212`); type0 =
  common @0, type1 = entity strings @9470, … (15 single-language entries).
  tables.bin: first 80 bytes = 20 LE i32 offsets, all offsets RELATIVE TO BYTE 80
  (`beginTableLoading` skips the header, `src/Resource.cpp:225-234`); table 5 =
  TBL_CANVAS_KEYSNUMERIC → `[5,9,1,10,3,6,4,12,2,14]` (= MENU, STRAFELEFT, UP, STRAFERIGHT,
  LEFT, FIRE, RIGHT, NEXTWEAPON, DOWN, PASSTURN for keys 1..0).
  Gotcha noted: initial parse forgot the 80-byte header skip and produced garbage sizes.
- Compared with `new_src/core/GameContext.cpp` (setState hook :87-102, tick :438-496,
  action dispatch :149-163, handlePlayingAction Use :760-774) and
  `new_src/domain/game/Game.cpp::lootCorpse` (:724-818).

## Verdict

CONFIRMED on all five questions. The original has an explicit **dwell phase**: after the 500 ms
crouch the game holds the pose, plays sound 1055 once, draws a top-of-screen loot list (dark-red
body 0xFF660000 + black title bar "Looted Items:"), and accepts FIRE/PASSTURN/BACK/UP/DOWN/
LEFT/RIGHT. Closing (last-page FIRE or PASSTURN/BACK anywhere) runs `giveLootPool()` immediately
and starts stand-up with no extra delay; `advanceTurn()` fires at stand-up expiry.

Key evidence anchors (full citations in loot-inventory.md §2.6):

- Input guard + handler: `src/LootingSystem.cpp:85-117` (guard `:87`; FIRE close/page `:89-98`;
  PASSTURN/BACK `:99-103`; UP/DOWN `:104-109`; LEFT/RIGHT `:110-115`; `max` calc `:88`).
- Draw gating + geometry + colors: `src/LoothingSystem.cpp:119-152`
  (gate `:122`; rect `:123-127`; 0xFF660000 body `:128-129`; black bar+white borders `:130-134`;
  str227 title `:135-139`; 3 lines `:140-144`; scrollbar `:145-150`).
- Paint z-order: `src/Canvas.cpp:414-417` (HUD) → `:470-472` (loot overlay) ⇒ over 3D view AND HUD.
- viewRect y=20 hardcoded: `src/Canvas.cpp:124-127` ⇒ dialogRect {0,36,479,48} at 480×320.
- Scrollbar internals/colors: `src/Canvas.cpp:1284-1314` (track 0xFFB3AA93 `:1307-1308`,
  thumb 0xFFE7CFAD `:1309-1310`, formulas `:1301-1304`, black outlines `:1311-1313`).
- Entry order (pool BEFORE any input possible): `src/PlayingInputHandler.cpp:374-378`.
- Grant-before-standup, timer restart: `src/LoothingSystem.cpp:89-103`;
  stand-up snap + advanceTurn: `:73-81`.
- Empty corpse: str228 line `src/LoothingSystem.cpp:259-261`; still marked looted `:163-179`;
  foundLoot += 0 `src/Game.cpp:3541-3543`.
- KEY_CLR swallow: `src/InputEventController.cpp:164-184`; AVK_CLR→ACTION_BACK `:25`;
  routing `:432-434`; touch→FIRE `src/TouchController.cpp:29-31` + keys_numeric[5]=6.
- Strings decoded from .ipa: type0[90]=`%01%02x %03|`, type0[91]=`%01%02|`,
  type0[227]=`Loot-ed Items:`, type0[228]=`None found!|`, type1[157]=`UAC Cre-dits`.
  Credits name resolves via entity TABLE 1 (`addTextArg((short)1,(short)157)`,
  `src/LootingSystem.cpp:256` + `src/Text.cpp:269-274`) — not table 0.
- dehyphenate deletes every '-': `src/Text.cpp:714-725`.

## new_src deltas (verified lines)

1. Auto-grant at settle + immediate stand-up: `new_src/core/GameContext.cpp:469-485` — must become
   dwell + grant-on-close.
2. Input dropped entirely in Looting: loop breaks on non-Playing `:161` (comment admits it,
   `:442-443`) — needs a Looting action handler.
3. No pooling at entry: Use branch only stores `pendingLootCorpse_` `:768-773`; legacy pools +
   marks looted at entry (`src/PlayingInputHandler.cpp:374-376`), over ALL eType-9 entities on the
   tile (`src/LoothingSystem.cpp:154-224`).
4. Toast instead of list UI: `new_src/domain/game/Game.cpp:791-818` — replace with the
   drawLootingMenu overlay; keep the grant pass but call it from close.
5. Mark-looted unification deviation (param vs monster flag 0x800):
   `new_src/domain/game/Game.cpp:730-734` vs `src/LoothingSystem.cpp:163-179`.
6. advanceTurn placement already correct (`new_src/core/GameContext.cpp:486-495`).

## Open questions

- Exact visual identity of font glyph `\x88` (default-mapped font cell index 103;
  `src/Graphics.cpp:637-659`) — cosmetic; check a screenshot against device footage if needed.
- `lootPoolIndices[18]` holds ≤9 lines while pool merging can theoretically exceed that with many
  stacked corpses on one tile (original overflow risk, not reproduced here).
