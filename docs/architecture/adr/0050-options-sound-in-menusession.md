# 0050 — Options → Sound as code-built `MenuSession` screens with producer-side slider input

Date: 2026-11-05. Spec: [`options-sound.md`](../options-sound.md). Narrows FE-D2 (ADR 0043/0044).

## Context

The user asked for sound settings in Options. The shipped `menus.bin` row 17 has 0 items; the
legacy port fills 17/35 and the Sound sub-menus -3/55 with `[GEC]` code-built rows: two volume
sliders (image slider on the main skin, flat-rect slider in game), a keyboard edit mode
(select toggles, LEFT/RIGHT ±10) and touch press/drag mapping x to 0..100
(`docs/research/2026-11-05-sound-options-menu.md`). FE-D2 refused Options outright.
`Sound::setSoundFxVolume/setMusicVolume` and config v11 persistence already exist (ADR 0046).

## Decision

1. The four screens are code-built `MenuSession` screens in `initMenu` (labels as literal rows on
   the legacy `ARGUMENTk` ids, ADR 0013 pattern); Options lists only "Sound" (Video/Input not
   ported). The `[GEC]` flags `ITEM_PADDING / ITEM_SCROLLBAR / ITEM_SCROLLBARTWO` and the full
   `getMenuItemHeight` rule are ported into `itemHeight`.
2. Slider state crosses to the view as four plain fields on `MenuRow` (`slider` kind, shown value,
   edit flag, held-pointer x). `MenuView` / `MainMenuView` draw them right after the row label,
   exactly where `paint` calls `drawCustomScrollbar`. No new view, no new UiId.
3. Slider touch input is resolved in the producer (`MenuSession::updateDrag`, which already reads
   the frame's pointer for scroll drags), with touch rects computed from the row tops. The view
   only draws. Keyboard edit mode is one `editSlider_` value (0 / 1 SFX / 2 Music) checked at the
   top of `handleAction`.
4. Deliberate deviations: edit/drag state cleared on every screen change; config saved on leaving
   any of the four screens (Back) and on Resume.

## Consequences

- No new files; FE-D2 now covers only Other Games plus the omitted Video/Input rows (OPT-D1).
- The slider touch geometry lives in `MenuSession` (constants 99/238/65, 92/296/21) and the draw
  geometry in the views — the same literals twice, each cited to `src/MenuSystem.cpp:5361-5455`.
- Volume is read live from `Sound` every frame, so no `setMenu(menu)` refresh calls are needed.
- Config is written a few extra times per Options visit (cheap, one small file).

## Rejected alternatives

- **View-side slider hit testing with a new `UiAction::Slider`**: needs per-frame drag state in
  the stateless view or `UiState`, plus a new action route through `GameContext`; the producer
  already owns pointer drags.
- **A separate `OptionsSession`/view**: duplicates menu stack, cursor, soft keys and skins that
  `MenuSession` already provides; legacy is one `MenuSystem` too.
- **Legacy two-flag edit state (`changeSfxVolume` + `changeValues`)**: the cursor cannot move in
  edit mode, so one value is equivalent and cannot desynchronise.
- **Save only on Back from 17/35 (verbatim)**: Resume would silently lose changes.
