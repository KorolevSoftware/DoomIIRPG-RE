# 2026-11-04: Main-menu sub-screens (Options, Comic Book, Mini Games, Game Help, About, Credits)

## Hypotheses / questions
1. How does `MENU_COMIC_BOOK` 89 work, and which assets does it use?
2. Do the main-menu help leaves use separate ids? How do they differ from the in-game leaves?
3. What does About show?
4. How do the Credits from Game Help work, and can the intro-crawl code be reused?
5. What does `playFromMainMenu` do for the three minigames, and how does control come back?
6. What do the Options screens contain?

## Method
Read `src/MenuSystem.cpp` with `grep -a`, because the file is ISO-8859 and plain grep prints
nothing. Also read `src/ComicBook.cpp`, `src/IntroSequenceManager.cpp:163-245`,
`src/InputEventController.cpp:387-414`, `src/HackingGame.cpp`, `src/SentryBotGame.cpp` and
`src/VendingMachine.cpp`. Decoded the shipped `menus.bin` (header u16 menuDataCount, u16 itemsCount,
then LE u32 arrays, `src/MenuSystem.cpp:41-73`) and `strings.idx`/`stringsNN.bin`
(`src/Resource.cpp:169-207`) from the `.ipa` read-only, using a scratch script. Listed the `.ipa` for `ComicBook/`.

## Verdicts
1. CONFIRMED. 89 is a menu id that delegates paint, keys and touch to `app->comicBook`
   (`src/MenuSystem.cpp:721-724,2834-2836,4662-4665,4832-4833`). `initMenu` case 89 is empty (`:2276`). It
   needs 56 BMPs and all of them are in the `.ipa`. In the port only the 39 landscape frames are ever shown,
   because the orientation switch is dead (`src/ComicBook.cpp:67-76,124-132`). LEFT/RIGHT flip the frame,
   BACK/MENU → `back()` → MAIN.
2. CONFIRMED, the ids are separate: 5,6,7,9,10,11,12,20,21,22 (main) vs 43,44,45,38,39,40,41,59,60,61 (in-game).
   The resource numbers are the same; main About 8 → 11. The main leaves use `imgMainBG`, no logo, wrap 35
   with no re-wrap, paint rect (85,0,300,320), and the dial at (408,81) when the text overflows.
   Effects (main) appends one extra blank row.
3. CONFIRMED. `LoadHelpResource(11)` = customer service + copyright text.
4. CONFIRMED. `initScrollingText(2,0,false,16,5,500)` scrolls at 32 px/s on a black screen with no music.
   The end is `500*(lines)` ms. BACK/FIRE at any time pushes MAIN (index 3/4, which is the Mini Games row, a quirk)
   and opens MAIN_HELP with the cursor still on Credits.
5. CONFIRMED. All three set `callingThread = nullptr` → `gamePlayedFromMainMenu`. That gives IQ 0, no rewards and no
   script vars. Each game ends with `playSound(1071, loop)` + `setMenu(18)`. Vending starts on the help screen with
   4 tries.
6. CONFIRMED. The shipped row 17 has 0 items. All the content is `[GEC]` Sound/Video/Input.

Full facts with citations: `docs/original-code/front-end.md` §12.

## What new_src has / what is missing

### Comic Book (89)
| has | missing |
|---|---|
| Menu id `kMenuComicBook`; the row is disabled via `isUnportedMainScreen` (`new_src/core/MenuSession.cpp:273-276`) | The whole viewer: lazy loading of 39 frames (4-bit BMP), white clear, frame at `endPoint` plus neighbours at ±480, swipe/slide/snap logic, LEFT/RIGHT keys, "Done" button, BACK → MAIN |

### Game Help (4) + leaves
| has | missing |
|---|---|
| In-game leaves 38-41/43-45/59-61 with `helpResourceFor` (`new_src/core/MenuSession.cpp:224-238`) and the type-5 body loader (`:1040-1095`, wraps with `kHelpMaxChars`) | Main ids 4,5,6,7,9,10,11,12,20,21,22 (and 8). `helpResourceFor` needs main → res entries: 9→1, 10→2, 11→3, 12→7, 5→4, 6→5, 7→6, 20→8, 21→9, 22→10, 8→11. Wrap 35 and no re-wrap for main ids. Paint rect (85,0,300,320) on logo.bmp with no logo. Dial scrollbar at (408,81). Extra blank row on 6. MAIN_HELP list (type 6, rows 1..11, logo, dial) |

### About (8)
Same as the help leaves: one more `helpResourceFor` entry (8→11). BACK goes to MAIN.

### Credits (ST_CREDITS)
| has | missing |
|---|---|
| The intro-movie crawl (`new_src/core/IntroMovieSession.cpp:14-122`, `new_src/ui/IntroMovieView.cpp:44`) implements the same `drawScrollingText` math. `PrologueSession` is the paged story and does not apply here | A credits state. Parameters differ: text 2:0, spacing 16, msPerLine 500 (n=500), lines+5, end = n*lines (no `+screenH/spacing-2`), black background (no prolog band, fade or screen space), y = 250 - off on screen, "Continue" (0:43) at (216,288) anchor 2 once done, BACK/FIRE any time → MAIN_HELP. Wrap 42 is the same. The ChangeState row is disabled today (`new_src/core/MenuSession.cpp:553-554`) |

### Mini Games (18)
| has | missing |
|---|---|
| All three games: `new_src/minigames/{hacking,sentrybot,vending}`, started only via `MiniGameHost::start(type,b1,b2,b3,thread)` from EV_MINIGAME (`new_src/minigames/common/MiniGameHost.h:57-59`). Headers state "world branch only: no playFromMainMenu" (`HackingGame.h:16`, `SentryBotGame.h:18`, `VendingMachine.h:18`) | A `playFromMainMenu` entry for each game (random Hacking params; sentry botType 0; vending map 1, machine 1, help screen first, tries 4), the `fromMainMenu` branches (IQ 0, no evaluate/scriptVar/IQ bonus/tries/hack flag, sentry win screen 0:239/0:188 with no bot choice, vending 0:239/0:240), and an `endGame` tail that goes to menu 18 instead of resuming a thread. The MAIN_MINIGAME list (type 8, cursor on Hacker, Back soft key, BACK → MAIN) |

### Options (17)
| has | missing |
|---|---|
| In-game `kMenuOptions` 35 loads the shipped row "Controls" → 62 (disabled: no arrow-controls screen, `new_src/core/MenuSession.cpp:541-542`) + "Back". Note that `src/` replaces it with the `[GEC]` list | No original content exists (0 items). Of the `[GEC]` sub-menus, Sound is meaningless without audio; Video (window mode/VSync/resolution) and Input (bindings) are rewrite-specific choices. Minimal faithful option: an empty Options screen or keep the row disabled |

## Open questions
- None blocking. The comic viewer's slide speed rule for wrap-around (`endPoint ± 2*v8` at the 0↔38 boundary,
  `src/ComicBook.cpp:600-627`) was read but not simulated.
