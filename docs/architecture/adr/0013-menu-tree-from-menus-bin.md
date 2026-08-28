# ADR 0013 — The in-game menu tree is parsed from `menus.bin`, not hardcoded

Date: 2026-08-28
Status: accepted
Context spec: `specs/2026-08-28-menu.md` (GROUP 8 of `specs/2026-08-27-ui-layer.md`)

## Context

`docs/original-code/ui.md` §11 establishes that the original menu is
**data-driven**: `menus.bin` holds 72 menu rows and 426 item ints, and
`MenuSystem::initMenu` mostly just calls `loadMenuItems(menu, 0, -1)`
(`src/MenuSystem.cpp:2724-2727`) and then patches a few fields. The root
`MENU_INGAME` (id 29) is 11 rows of pure data: label string id, flags, action,
action parameter, help string id (`src/MenuSystem.cpp:4005-4035` decoder).

The rewrite had two options for GROUP 8:

* **A — parse `menus.bin`** in a new `io/` loader, the way `Tables`,
  `EntityDefs` and `Localization` are already parsed.
* **B — hardcode the in-game subset** as a C++ table of rows.

Facts that bear on the choice:

* The format is fully decoded and the file is tiny and uncompressed in the
  shipped archive (`menus.bin`, 1996 bytes, stored, verified by reading the
  `.ipa` central directory).
* The shipped `MENU_INGAME_OPTIONS` (id 35) row is only `Controls` + `Back`;
  every options *value* screen in `src/` is a `[GEC]` port addition
  (`docs/original-code/ui.md` §11.2, §17). Hand-writing menu rows is exactly
  what produced that divergence in the port we are porting from.
* There are **no save slots** — `ACTION_SAVE`/`ACTION_LOAD` are one implicit
  slot (`src/MenuSystem.cpp:3035-3050`) — so the data does not need any
  dynamic row generation for saves.
* `MENU_INGAME_RECIPES` (47) has no `menus.bin` row and an empty `initMenu`
  case (`src/MenuSystem.cpp:1710-1712`): dead in this build.
* Several screens are **not** in the file at all and are built in code:
  `MENU_INGAME_QUESTLOG` (46), `MENU_INGAME_LOAD` (49),
  `MENU_INGAME_RESTARTLVL` (52), `MENU_INGAME_SAVEQUIT` (53) and every
  `type 5` HELP leaf. Verified by decoding the shipped file: ids 46/49/52/53
  are absent from the 72 rows.

## Decision

**Option A: parse `menus.bin`** in `new_src/io/MenuData.{h,cpp}`.

Minimum viable subset of the loader:

1. Header `short menuDataCount, short menuItemsCount`, then `menuDataCount`
   LE int32 rows, then `menuItemsCount` LE int32 item words.
2. Row unpack: `id = bits 0..7`, `itemEnd = bits 8..23` (in ints),
   `type = bits 24..31`; `itemStart` = the previous row's `itemEnd`, 0 for
   row 0.
3. Item unpack, 2 ints per item: `label = (3, n7 >> 16)`,
   `flags = n7 & 0xFFFF`, `action = (n8 >> 8) & 0xFF`, `param = n8 & 0xFF`,
   `help = (3, (n8 >> 16) & 0xFFFF)`.
4. Lookup by menu id; a missing id yields an **empty** menu plus one
   `stderr` line (the original calls `Error(29)` and dies —
   `src/MenuSystem.cpp:4025`; a fatal exit is the wrong trade in a partial
   rewrite).

The loader does **not** model: the code-built screens above, `loadMenuItems`'s
`begItem`/`numItems` slicing (only the `0, -1` "all" case is used by the
in-game tree), vending screens, or main-menu screens.

## Consequences

* Every list screen the in-game tree reaches — root, `MENU_ITEMS` (72),
  `MENU_ITEMS_WEAPONS` (73), `STATUS` (30) and its three leaves (31/32/33),
  `GAME HELP` (37, 12 rows), `OPTIONS` (35), `EXIT` (42),
  `LOADNOSAVE` (50) — arrives at once, with the shipped English labels,
  their soft hyphens, their `NOSELECT`/`DIVIDER` flags and their per-row help
  string ids. Zero invented structure.
* `Localization` must load text type **3** (`FILE_MENUSTRINGS`,
  `src/MenuStrings.h:22`, called `kTextIngame2` in
  `new_src/io/Localization.h:19`) and, for HELP bodies, type **2**
  (`FILE_FILESTRINGS`, `kTextHelp`). Both are one `loadTextType` call in
  `Main.cpp`.
* The loader is necessary but **not sufficient**: the code-built screens
  (YES/NO confirms, notebook, help leaves) must still be produced by
  `MenuSession`, mirroring the `initMenu` cases they come from. That code is
  written against the same in-memory item struct the loader fills, so both
  sources look identical to the view.
* Golden values exist for a load-time self-check (decoded from the shipped
  file, and reproduced by the spec): `menuDataCount = 72`,
  `menuItemsCount = 426`, all 1996 bytes consumed, `id 29` type 1 with 11
  items whose actions are `1,1,4,1,9,1,1,1,1,1,1` and targets
  `72,46,0,49,6,30,37,35,52,53,42`.
* Cost: ~120 lines of loader plus one enum of menu ids. Bounded, and the
  parse is verifiable at boot rather than by eye.

## Rejected alternative (B — hardcoded table)

Rejected because:

* The 11 root rows are the smallest part of the job: the per-row `flags`,
  `helpField` and the disable/retarget rules of `initMenu`
  (`src/MenuSystem.cpp:1549-1573`) all operate **on data fields**, so a
  hardcoded table has to reproduce those fields anyway — at which point it is
  the file, transcribed by hand.
* It drifts silently. When a sub-screen's row set is wrong there is nothing to
  compare against; with the loader, a wrong row is a wrong byte offset and
  shows up as a failed golden check.
* Sub-screens would each need their own invented table, so "cheap" is only
  true for the first screen.
* The port's own `[GEC]` options body is the concrete precedent for what
  hand-written menu rows do to fidelity.
