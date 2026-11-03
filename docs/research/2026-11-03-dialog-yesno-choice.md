# 2026-11-03 — Dialog Yes/No choice widgets (flags 0x1 / 0x4)

## Hypothesis
The sentry-bot self-destruct dialog (style 12, flags 1, str 194) shows no
Yes/No choice in the rewrite because the choice draw path of the original is
not ported; input (LEFT/RIGHT toggle of var4) works.

## Verdict: CONFIRMED

## Original (src/)
- prepareDialog, flags&4 || flags&1: `scriptStateVars[4] = (style==12) ? 0 : 1`
  and appends common str50 ("||") to the text (src/DialogSystem.cpp:637-647).
- Input: LEFT/RIGHT with `(dialogFlags & 0x5)` on the last page:
  `scriptStateVars[4] ^= 0x1` (src/DialogSystem.cpp:91-92). FIRE on last page
  closes (34-49). For flags&4/&1, paging clamps so the final page stays full (42-44).
- Draw (dialogState): `else if (this->dialogFlags != 0 && this->currentDialogLine >= this->numDialogLines - this->dialogViewLines)`
  (src/DialogSystem.cpp:408), labels n19/n20 = 30/31 ("Exit"/"Play"), or
  140/141 ("No"/"Yes") when flags&1 (409-414). Inside `if (flags&4 || flags&1)` (416):
  - `n21 = canvas->dialogRect[1] + (16 * dialogViewLines) - 14` (417); v75 = FONT_HEIGHT+2 (419), v113 = FONT_HEIGHT (420), FONT_HEIGHT=16.
  - Left: `fillRect(96, n21, 96, v75, hColor)`, `drawRect(96, n21, 96, v75, -1)`, `drawString(label n19, 144, n21 + (v75>>1) + 2, 3)` (426-428). Cursor if var4==0: `drawCursor((128 - (w>>1)) + b, n21 + (v113>>1) - 5, 0)` (433-435).
  - Right: same at x=288, label at 336, label n20 (440-442). Cursor if var4==1: `drawCursor((320 - (w>>1)) + b, n21 + (v113>>1) - 5, 4)` (447-449).
  - hColor = n2 (style box fill; style 12/13 = 0xFFB18A01, :202-209), or `(n2+0x333333)|0xFF000000` while the touch button is highlighted (425, 439).
  - `b = OSC_CYCLE[app->time / 200 % 4]` (355); 3-arg drawCursor draws glyph `'\x8A'` (src/Graphics.cpp:670-686).
- Touch buttons 3/4 enabled when flags&4||flags&1 (216-218); taps set var4
  directly (src/TouchController.cpp:383-452).
- Style 12 semantics: closeDialog with `attemptingToSelfDestructFamiliar`
  calls `familiarDying(true)` only when var4==1 && !skip (src/DialogSystem.cpp:563-568);
  queueAdvanceTurn also on style 12 var4==1 (530).

Worked numbers, style 12 (viewLines 4): rect.y = 320-72-1 = 247; n21 = 297;
buttons y 297..315; labels at y=308 (anchor 3 = HCENTER|VCENTER); cursor y=300.
Default var4=0 -> cursor on the LEFT "No" button; RIGHT flips to "Yes" —
matches the user's observation (RIGHT then ENTER explodes the bot).

## Rewrite (new_src/)
- new_src/domain/game/DialogSystem.cpp:118-123 — var4 default and str50 append: ported correctly.
- new_src/domain/game/DialogSystem.cpp:308-309 — LEFT/RIGHT toggle: ported.
- new_src/ui/DialogView.cpp:244-247 — the whole Yes/No widget block is a
  comment: "still deferred — the map00 intro chain uses only flagless styles 1/8".
  Nothing is drawn for flags&1/&2/&4 in ANY style.
- DialogViewModel (new_src/ui/DialogModel.h) has no var4 / choice-label / cursor fields;
  buildViewModel (new_src/domain/game/DialogSystem.cpp:348-413) fills none.

So the discrepancy is not style-12 specific: the map00 yes/no dialogs
str72 (style 0, flags 1, IP 7094) and str91 (style 3, flags 1, IP 8350)
(from tools/disasm_map_scripts.py on tmp_map00.bin, `DIALOG ... type=1`) also
draw no choice today (their default is var4=1 = "Yes").

## Fix prescription
1. DialogModel.h: add `bool showChoice; int choiceSel; const Text* choiceLeft; const Text* choiceRight; int cursorOsc;`.
2. DialogSystem::buildViewModel (after the rows loop, ~:392): if
   `(flags_ & 0x5) && currentDialogLine_ >= numDialogLines_ - viewLines_`:
   showChoice=true; choiceSel=env_.vm->vars[4]; compose str 140/141 (flags&1)
   else 30/31 into two producer-owned Text buffers (dehyphenated);
   cursorOsc = oscCycle[(upTimeMs/200)%4] (table as in new_src/core/MenuSession.cpp:1908-1913).
3. DialogView.cpp:244: replace the comment with: n21 = ry + 16*viewLines - 14;
   panel {96,n21,96,18} fill, frame white, label centred (144, n21+11) anchor
   HCenter|VCenter; same at 288/336; `ui.glyph('\x8A', 128 - wL/2 + osc, n21+3, kAnchorNone)` if sel==0,
   `ui.glyph('\x8A', 320 - wR/2 + osc, n21+3, kAnchorLeft)` if sel==1 (w = label width, 9px/char).
   Optional: buttonRect hits for touch areas (96, n21-40, 96, 58) / (288, ...) setting var4.
4. flags&2 vertical pair (357-406) is a separate deferred block (VIOS / NPC); not needed for this bug.

## Open
- Whether the font's anchor 0 vs 4 differ visually for a single glyph (both left-aligned in new Graphics2D: kAnchorNone=0 treated as top-left?) — check Graphics2D anchor semantics when implementing.
