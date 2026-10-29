# 2026-10-29 — Vending menus 83-88

## Hypothesis / questions

How are the vending menus (83 `MENU_VENDING_MACHINE` .. 88 `_DETAILS`) built,
what do their actions do (currency, item grant, limits, CANT_BUY reasons), how
are the confirm/details screens laid out, and how does the flow return to the
vending minigame or the game?

## Method

Read `src/MenuSystem.cpp` (initMenu cases `:2062-2274`, select cases
`:3223-3254`, `fillVendingMachineSnacks` `:4100-4164`, key handling
`:2840-2955`, touch `:4690-4830`, softkeys `:5179-5288`, settings
`:4192-4219`, `updateTouchButtonState` `:4430-4627`), `src/VendingMachine.cpp`
(`:33-134`, `:186-221`, `:805-864`), `src/Player.cpp:970-1031,1147-1159`,
`src/GameStateRunner.cpp:203-253`. Decoded the shipped `UnPackGameData/menus.bin`
(format `src/MenuSystem.cpp:45-73,4005-4035`), `strings.idx`/`stringsNN.bin`
(`src/Resource.cpp:169-207`, `src/Text.cpp:179-212`) and `tables.bin` table 10
(`src/Resource.cpp:295-311`, `src/App.cpp:383,399`) with a throwaway scratch
script (no persistent tool exists for these formats).

Note: `src/MenuSystem.cpp` is ISO-8859 + CRLF; the session's `grep` shell
function silently returned nothing on it — use `command grep -a`.

## Verdict: CONFIRMED (with corrections to the caller's guesses)

- Snacks are **only** inventory slot 16 (Snack, `addHealth(20)`); 17 is never
  sold. Snack names are 12 cosmetic menu strings per map group; the row param
  is the composite string id `3<<10|s` (>= 11 distinguishes snack from drink).
- Drinks go to `inventory[0..10]` via `give(0, n, 1, false)`.
- Currency: `inventory[24]` decremented per unit.
- No max-count limit: `give` clamps the slot to 999 and silently drops excess
  (credits still deducted). The only CANT_BUY reason is insufficient credits.
- No stock concept; with shipped data every machine sells all 11 drinks
  (mapMask 0x03FE).
- 88 DETAILS is unreachable (no caller; `showDetailsMenu` is a stub).
- Touch "Exit" calls `returnToGame()` directly and bypasses
  `VendingMachine::endGame`, leaving the `EV_MINIGAME` script thread paused
  (port artifact; key path goes through `returnFromBuying`).

## Evidence (key quotes)

- Drink rows: `this->addItem(Localization::STRINGID(Strings::FILE_ENTITYSTRINGS, entDef->name), this->getLastArgString(), 32, 28, n16, ...)` — `src/MenuSystem.cpp:2102`.
- Prices: `return this->energyDrinkData[(n - 0) * 3 + (this->machineHasBeenHacked ? 1 : 0)];` — `src/VendingMachine.cpp:810`; `return this->machineHasBeenHacked ? 8 : 10;` — `:826`.
- Buy drink: `app->player->inventory[24] -= (short)this->currentItemPrice; app->player->give(0, n, 1, false);` — `src/VendingMachine.cpp:819-820`.
- Buy snack: `app->player->inventory[24] -= (short)app->vendingMachine->getSnackPrice(); app->player->give(0, 16, 1, false);` — `src/MenuSystem.cpp:3249-3250`.
- Qty up: `if (app->player->inventory[24] >= app->vendingMachine->currentItemPrice * (app->vendingMachine->currentItemQuantity + 1))` — `src/MenuSystem.cpp:2861`.
- 83 back: `else */if (this->menu == Menus::MENU_VENDING_MACHINE) { app->vendingMachine->returnFromBuying(); }` — `src/MenuSystem.cpp:2927-2928`.
- Touch exit: `else if (TouchedButtonID == 15) { this->returnToGame(); }` — `src/MenuSystem.cpp:4788-4790`.
- Clamp: `else if (n5 > 999) { n5 = 999; }` — `src/Player.cpp:1017-1019`.

## Decoded data

menus.bin (type 9 for all): 83 = "Ven-ding Ma-chine"(344,f73) / blank(f9) /
"Na-no Drinks"(178, GOTO 84) / "Snacks"(352, GOTO 85); 84 = "Na-no Drinks" f73 /
blank; 85 = "Snacks" f73 / blank; 86, 87 = "Con-firm Or-der"(345) f73 / blank;
88 absent.

Menu strings: 346 "Press UP/DOWN", 347 "to change quan-ti-ty", 348 "Tap item to
buy", 349 "Press back to go back  ", 350 "%01 x %02", 351 "To-tal Cost: %01",
354 "You can't af-ford this!", snacks 358-390. Code strings: 197 "Cre-dits:
%01", 96 "Tap Screen to Con-tin-ue", 202 "Buy", 30 "Exit".

tables.bin table 10 (`[price, hacked, mapMask]` x 11): 20/15, 25/20, 30/20,
50/40, 70/60, 80/70, 100/80, 100/80, 200/150, 400/300, 666/400; mask 0x03FE
for all.

Full curated write-up: `docs/original-code/ui.md` §17b.

## Open questions

- Whether the paused script thread after a touch "Exit" is ever recovered
  (e.g. by a later map reload) was not traced.
- `moveDir` type-9 look-ahead was read but not exercised against real list
  lengths/scroll pixel offsets.
