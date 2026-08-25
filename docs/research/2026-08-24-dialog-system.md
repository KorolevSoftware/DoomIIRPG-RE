# Research: full DialogSystem (styles, text pipeline, input, threading, map00 intro)

Date: 2026-08-24. READ-ONLY investigation of `src/`; curated result:
`docs/original-code/dialog-system.md`.

## Hypothesis

The rewrite's dialog-lite (gray HUD panel, E to step) can be replaced faithfully
if we document: (1) style/color variants and how EV_DIALOG operands select them,
(2) the string/args/wrap/paging pipeline, (3) box geometry + fonts + HUD
interplay, (4) the full dialog keymap, (5) thread park/resume protocol, and
(6) the actual map00 intro sequence with string ids/styles/speakers.

## Method

1. Read `src/DialogSystem.{h,cpp}` in full (908 lines) — styles, geometry,
   input, close/resume, help queue.
2. Read `src/ScriptThread.cpp:545-585` (EV_DIALOG decode) and `:2121-2224`
   (loot dialog), `src/Game.cpp:3461-3471` (script vars), 
   `src/InputEventController.cpp` (keymap), `src/Canvas.cpp` startup/state/
   paint paths, `src/Hud.cpp` suppression, `src/Graphics.{h,cpp}` +
   `src/Image.cpp:192` (font palette, drawString typewriter semantics),
   `src/Text.{h,cpp}` (Localization buffers, composeText, wrapText),
   `src/Resource.cpp:169-207` (strings.idx format).
3. Map00 replay: reused the trusted scanner approach from
   `docs/research/assets/scan_loot_map00.py` against `tmp_map00.bin`
   (`tools/map_to_obj.py::parse_map`); existing disasm
   `docs/research/assets/map00_disasm.txt`; re-walked tileEvents tail block
   manually (staticFuncs pattern search → marker → 167×8 bytes pairs) to map
   bytecode IPs to tile triggers; extracted `strings.idx` +
   `strings00..02.bin` from `build_new/new_src/Doom 2 RPG.ipa`
   (Payload/Doom2rpg.app/Packages) and decoded strings04 (map00) + common
   labels with a throwaway script in /var/folders/.../opencode (not committed).
4. Cross-checks: EV_DIALOG decode vs docs/original-code/tile-events-vm.md §3
   (opcode 13 row, independently produced); loot popup style vs
   `composeLootDialog`; help dequeue style=2 vs EV_DIALOG style-2 enqueue path;
   charColors[2]=0xFF00FF00 vs "green terminal" hypothesis (refuted my initial
   "gray" guess — gray is the header strip of style 2, body text is white).

## Verdict

CONFIRMED on all six points, with two notable corrections to prior assumptions:

- The user-remembered "gray tutorial boxes" are **style 2** (black fill,
  0xFF666666 header strip, white body): src/DialogSystem.cpp:230-237. Style 9
  is green-on-black (currentCharColor=2 → charColors[2]=0xFF00FF00,
  src/DialogSystem.cpp:250, src/Graphics.h:25-28). "Green hero speech" =
  style 8 (0xFF005617 + portrait, Canvas.h:112). "Blue comm-link" = styles
  1/6/14 (0xFF002864).
- Speaker names are not resolved from entities inside dialogState; they are
  the first `|`-line of the composed buffer for title-bar styles
  (src/DialogSystem.cpp:252), or come from EV_NAMEENTITY-set entity names only
  indirectly via loot/bubble composition (Hud.cpp:290-305, 908).

## Evidence highlights (see curated doc for ~80 citations)

- EV_DIALOG operand split: src/ScriptThread.cpp:547-549,578.
- Page sizes & wrap budgets: src/DialogSystem.cpp:607-618,
  src/Canvas.cpp:89-92.
- Typewriter 25ms/char: src/DialogSystem.cpp:338-344.
- Keymap: src/DialogSystem.cpp:29-112; BACK swallowed:
  src/InputEventController.cpp:167-184.
- Park/resume: src/ScriptThread.cpp:582-584; src/DialogSystem.cpp:559-563;
  skipDialog gate src/ScriptThread.cpp:555-557.
- queueAdvanceTurn rule: src/DialogSystem.cpp:530-532.
- Help FIFO: src/DialogSystem.cpp:749-908.
- map00 intro chain: INIT_MAP@0 → func@1913 → {1942,2084,2328}; dialogs at
  evt[54] tile(9,19) w1=0xFF4 ip=2993 (str21 s1, str22 s1, str23 s8, str24 s8,
  str25 s1+GIVEITEM def1, str26 s8/s1, bubble str27); pistol skipped verbatim
  when characterChoice==3 (@3048 EVAL v14 NEQ c3 iff->+31).

## Open questions / caveats

- Why characterChoice==3 skips the pistol give in evt[54] (data-level branch;
  maybe compensated elsewhere — not found on map00; left as documented quirk).
- Common str30/31 ("Exit"/"Play") as flags&4 button labels look odd but are
  exactly what `composeText(0,30/31)` resolves to in strings00.
- `dialogResumeMenu` has no producer among call sites read; kept for parity.
- Touch handling documented only at routing level (TouchController.cpp:378-419);
  desktop port needs keyboard-only equivalence, which §4 covers.
