# ADR 0044 — Main-menu sub-screens: help leaves in `MenuSession`, Credits as a peer session, explicit main-menu minigame flag, Comic Book as a delegate of menu id 89

Date: 2026-11-04. Status: accepted. Spec: `../main-menu-subscreens.md`. Facts:
`docs/original-code/front-end.md` §12. Extends ADR 0043 (retires most of its point 1 refusals,
deviation FE-D2).

## Context

ADR 0043 shipped the main menu with Options, Comic Book, Mini Games, Game Help and About
refusing on select. Research (FE §12) shows: the main help leaves are separate code-built ids
(5-12, 20-22, About 8) laid out on `logo.bmp` with their own rect/wrap/dial; Credits is a
separate canvas state `ST_CREDITS` (15) using the scrolling-text code of the intro crawl; the
three minigames have a `playFromMainMenu` path keyed on `callingThread == nullptr`; the Comic
Book is menu id 89 with zero rows whose paint, keys and touch are handed to a `ComicBook`
object; Options ships with zero rows and only `[GEC]` content.

## Decision

1. **Help leaves and About are `MenuSession` screens.** The main ids join `helpResourceFor`;
   `loadHelpResource` wraps them at 35 without the in-game re-wrap; the main-skin branch of
   `buildViewModel` gives them rect (85,0,300,320), no logo and the dial at (408,81).
   `MainMenuView` already draws label rows, so it is reused unchanged.
2. **Credits is a peer session** `core/CreditsSession` with `StateId::Credits = 15`, drawn by a
   second function `drawCredits` in the existing `ui/IntroMovieView` (shared file, separate
   model). The return to the menu is `MenuSession::returnFromCredits()`, called from the Menu
   entry hook when the previous state was Credits, so the legacy `pushMenu` + `setMenu` with the
   kept cursor is reproduced inside the session that owns the stack.
3. **Main-menu minigames use an explicit `MiniGameHost::fromMainMenu_` flag**, set only by
   `MiniGameHost::playFromMainMenu(type)`, instead of the legacy `thread == nullptr` test,
   because the rewrite's debug keys already start games with no thread and expect the world
   tail. The host's `finish()` gets the main-menu tail (sound 1071, `openMenu(18)`), each game
   guards its IQ / tries / hack-bit / reward writes with the flag.
4. **Comic Book stays menu id 89 with zero rows**, and `GameContext` routes paint and keys to a
   `core/ComicBookSession` while `menu_.menuId() == 89` — the legacy delegation. The stack, the
   cursor restore and the BACK sound come from `MenuSession` unchanged. The 39 landscape frames
   are loaded once at startup into `UiAssets` (39 × 480×320 R8 = 5.99 MB GPU) and stay resident.
5. **Options (17) stays refused**: there is no original content to port.

## Consequences

* No new view kinds and no new module layers; three new file pairs (`CreditsSession`,
  `ComicBookSession`, `ComicBookView`) plus one model header.
* `MenuSession` gains main-id knowledge in a handful of places (`helpResourceFor`, the main
  view-model branch, `returnFromCredits`, `back()`'s empty-stack rule); the in-game behaviour is
  untouched.
* The comic frames cost 6 MB of GPU memory for the whole run and a few ms of startup decode;
  there is no "Loading" screen (deviation FE-D8).
* LEFT/RIGHT on the comic slide at 8 px/frame instead of the legacy one-frame flip
  (deviation FE-D7, the flip came from a coordinate-scaling bug).

## Rejected alternatives

* **Comic Book as its own `StateId`**: no legacy state exists; returning to MAIN with the cursor
  on the Comic Book row would need a second stack or a special re-entry like Credits.
* **Lazy per-frame comic loading with the "Loading" screen**: needs runtime access to the
  texture store and the archive reader from a core session plus a destroy path on both exit
  routes, for a cosmetic screen that only existed to hide iPhone decode time.
* **`thread == nullptr` as the main-menu test**: breaks the V/H/J debug games, which would
  suddenly return to menu 18 with no map.
* **A credits mode inside `IntroMovieSession`**: that session is a phase machine around the
  table camera; the credits need only a start time and a done flag, and different end/offset
  constants. A separate 40-line session is clearer than mode branches in every phase.
* **Porting the `[GEC]` Options sub-menus**: not original content (FE §12.6).
