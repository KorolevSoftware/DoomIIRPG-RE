# 2026-10-28 — Items menu flow up to `Player::useItem`

## Hypothesis / questions

1. How are `MENU_ITEMS` (72) / `MENU_ITEMS_DRINKS` (75) populated (code vs
   `menus.bin`), row text, filtering, sorting, params, disabled rules, help text?
2. Exact `ACTION_CONFIRMUSE` → `MENU_ITEMS_CONFIRM` (77) construction; what YES
   / NO do.
3. Other actions; does a menu use close the menu / consume a turn?
4. What `new_src/core/MenuSession` already has; gap list.

## Method

Read `src/MenuSystem.cpp` (`initMenu` cases `:1930-2059`, `select`
`:2971-3210`, `back` `:568-611`, `SetYESNO` `:3611-3660`,
`loadMenuItems`/`addItem`/`getLastArgString` `:3997-4098`),
`src/Player.cpp:801-960` (`useItem`), `src/Game.cpp:2391-2450`
(`snapMonsters`), `src/Combat.cpp:165`, `src/PlayingInputHandler.cpp:159-178`,
`src/Canvas.cpp:1033-1035`. Decoded `UnPackGameData/menus.bin` +
`strings.idx/strings0N.bin` with a throwaway scratch script (format per
`src/MenuSystem.cpp:4005-4035`, `src/Resource.cpp:169-205`,
`src/Text.cpp:179-211`). Note: `src/MenuSystem.cpp` is ISO-8859 + CRLF, plain
`grep` treats it as binary — use `grep -a`.

## Verdict

CONFIRMED with corrections to the existing rewrite comment: 77 is fully
code-built; YES runs `useItem(menuParam)` and on success goes **back to the
rebuilt list** (menu stays open), not `returnToGame`. Curated facts:
`docs/original-code/ui.md` §17a.

## Evidence (key quotes)

- 72 order: health 16..17 with lazy "HEALTH" divider → holy water (param 22)
  → armor 11..12 with lazy "AR-MOR" divider → menus.bin row 1 → "Na-no Drinks"
  GOTO 75 if `hasANanoDrink()` → menus.bin row 2..end ("Weapons" GOTO 73) →
  credits (action 16) (`src/MenuSystem.cpp:1930-1986`).
  `this->addItem(STRINGID(FILE_ENTITYSTRINGS, find->name), this->getLastArgString(), 32, 24, n5, STRINGID(FILE_ENTITYSTRINGS, find->description));` (`:1944`)
- Holy water: `if (app->player->ammo[3] >= 25 && (app->player->weapons & 0x4) != 0x0)` … `find(6, 2, 3)` … `32, 24, 22` (`:1947-1953`).
- 75: `for (int n13 = 0; n13 < 11; ++n13)` with `inventory[n13] > 0`, else `"[None]"` flags 9 (`:2024-2045`).
- CONFIRMUSE: `this->menuParam = this->items[i].param; this->gotoMenu(Menus::MENU_ITEMS_CONFIRM);` (`:3195-3196`).
- 77: `if (this->peekMenu() == 75) { this->SetYESNO((short)184, 1, 20, this->menuParam); break; } this->SetYESNO((short)184, 1, 21, this->menuParam);` (`:2054-2058`); string 3/184 = `"Use %01?"`; name arg from `composeText(FILE_ENTITYSTRINGS, find7->name)` where `find7 = find(6, 0, menuParam)` (`:2050-2053`).
- SetYESNO 4-arg → `SetYESNO(text, i2, i3, i4, 2, 0)` (`:3617`): YES = action A param menuParam, NO = `ACTION_BACK` 0, preselect YES (`:3650-3659`).
- YES (`:3149-3179`): `bool useItem = app->player->useItem((short)this->menuParam); if (app->game->animatingEffects != 0) { this->returnToGame(); break; } app->game->snapMonsters(true); app->game->snapLerpSprites(-1); if (app->canvas->state != Canvas::ST_MENU) break; if (useItem) { this->back(); break; }` then failure screens 82 / 79 / 80 / 81 by param.
- Failure screens' "Ok" = action 26 `ACTION_BACKTWO`: `popMenu(...)` + `back()` (`:3203-3207`).
- `useItem` tail: `playSound(1124)`, `if (b2) --inventory[n]; if (b) app->game->advanceTurn(); return true;` (`src/Player.cpp:952-959`); `b = false` only for item 8 (`:884-892`); `b2 = false` only for 22 (`:935-944`).
- Monster turn resolved in-menu: `snapMonsters` → `monsterAI()` when `monstersTurn != 0` (`src/Game.cpp:2431`), and `performAttack` → `setState(ST_COMBAT)` (`src/Game.cpp:2445-2447`, `src/Combat.cpp:165`).
- `back()` with empty stack returns to game for `MENU_ITEMS` and `MENU_ITEMS_DRINKS` (`src/MenuSystem.cpp:604-607`); play-mode keys open 72/75 directly via `setMenu` (`src/PlayingInputHandler.cpp:159-178`).

menus.bin decode (shipped file): 72 = blank / "IN-VEN-TO-RY" (73) / "Weap-ons"
GOTO 73 help 176; 73 = blank / "WEAP-ONS"; 75 = blank / "NA-NO DRINKS";
77 absent; 79-82 = two message lines + blank + "Ok" (flags 8, action 26).
Strings 3/112 "HEALTH", 3/113 "AR-MOR", 3/178 "Na-no Drinks", 3/179 "Your
na-no drinks", 3/184 "Use %01?", 3/197 "Yes", 3/198 "No ", 3/1 "[None]".

## Rewrite status and gap list (`new_src/core/MenuSession.cpp`)

Already present:
- `buildItemsScreen()` reproduces the 72 row order, params and value column
  (`new_src/core/MenuSession.cpp:550-636`), including holy water param 22.
- `setYesNo(strId, preselect, yesAction, yesParam, noAction=2, noParam=0)` with
  the verbatim `\n` split and preselect (`new_src/core/MenuSession.cpp:714-748`).
- `gotoMenu`/`back`/stack with cursor restore after rebuild
  (`new_src/core/MenuSession.cpp:1405-1420`, `:1350` onward).
- `kActionUseItemWeapon` (18) path (`new_src/core/MenuSession.cpp:1322-1334`).

Missing:
1. IDs: `kMenuItemsConfirm = 77`, 79/80/81/82 not in `MenuId`
   (`new_src/io/MenuData.h:25`); actions 20, 21, 26 (`ACTION_BACKTWO`) not in
   `MenuAction` (`new_src/io/MenuData.h:42-48`). 79-82 load through the
   existing `addParsedRows(menu,0,-1)` default path once reachable.
2. Drinks screen body (75): `loadMenuItems(75,0,-1)` + drink loop + "[None]";
   the GOTO 75 is refused at `new_src/core/MenuSession.cpp:437-441`.
3. `menuParam` state (set by CONFIRMUSE, read by 77 and by actions 20/21).
4. `setYesNo` cannot substitute `%01`: it appends the raw string and logs on
   `%` (`new_src/core/MenuSession.cpp:716-720`). 77 needs string 184 composed
   with the entity-def name of `find(6,0,menuParam)`.
5. `ACTION_CONFIRMUSE` refused (`new_src/core/MenuSession.cpp:464-468`);
   select cases 20/21/24/26 absent.
6. `Player::useItem` absent; `Player::addArmor` returns `void`
   (`new_src/domain/game/Player.h:132`) but useItem needs the legacy bool
   "armor was below max"; no `statusEffects`/`addStatusEffect`/
   `translateStatusEffects` on the rewrite Player (drinks 0-10, 22 need them;
   health 16/17 and armor 11/12 do not).
7. Post-use sequencing: `animatingEffects` check → `returnToGame`;
   `snapMonsters(true)` + `snapLerpSprites(-1)`; leave if state changed
   (monster attack → combat state); else `back()`.
8. `back()` empty-stack return list lacks `kMenuItemsDrinks`
   (`new_src/core/MenuSession.cpp:1417`); no gameplay entry for
   `ACTION_ITEMS`/`ACTION_ITEMS_DRINKS` (the rewrite only opens
   `MENU_INGAME`/`MENU_INGAME_DEAD`, `new_src/core/GameContext.cpp:176-177,578`).
9. Credits row `ACTION_SHOWDETAILS` — legacy stub, nothing to port.

## Open questions

- Paint of `textField2` (count column) and dividers is in §12.3 of ui.md; not
  re-verified here.
- After a successful use that empties a row, the restored `selectedIndex` is
  not clamped (`src/MenuSystem.cpp:599`); harmless in legacy (rows past
  `numItems` are blank action-0), but the rewrite's `select` bounds check
  differs — decide explicitly.
- `addStatusEffect` semantics for drinks are out of scope (not researched).
