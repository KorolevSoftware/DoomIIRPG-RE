# Dialog System (`src/DialogSystem.cpp`, `src/ScriptThread.cpp` EV_DIALOG)

> Status: COMPLETE. Every claim cites `src/<file>:<line>`; map00 script claims verified
> against `tmp_map00.bin` bytecode (method + full disasm: `docs/research/assets/map00_disasm.txt`,
> scanner `docs/research/assets/scan_loot_map00.py`; raw report `docs/research/2026-08-24-dialog-system.md`).
> Scope: everything the rewrite needs to replace dialog-lite with faithful dialogs.

## 0. Overview / data flow

`Canvas::dialogSystem` (src/Canvas.h:355) owns dialogs and help popups.
`DialogSystem.h` state: `dialogIndexes[1024]` (pairs of <offset,len> per line),
`dialogViewLines`, `dialogLineStartTime`, `dialogStartTime`, `dialogTypeLineIdx`,
`dialogStyle`, `dialogType`, `dialogFlags`, `dialogResumeScriptAfterClosed`,
`dialogResumeMenu`, `dialogClosing`, `dialogThread`, `numDialogLines`,
`currentDialogLine`, help FIFO 4×16 (`helpMessageTypes/Ints/Objs/Threads`,
`numHelpMessages`) — src/DialogSystem.h:14-33.

Pipeline: `EV_DIALOG` (or C++ caller) → `startDialog(thread, mapStringID, strId,
style, flags, resume=true)` → `Localization::composeText` into a large buffer →
`prepareDialog`: word-wrap into `canvas->dialogBuffer`, split into lines at `'|`
into `dialogIndexes[]` → `canvas->setState(ST_DIALOG)` (src/DialogSystem.cpp:737-747).
Render each frame via `Canvas::backPaint` → `dialogState(graphics)`
(src/Canvas.cpp:447-449). Input routed to `handleDialogEvents`
(src/InputEventController.cpp:331-333 → src/Canvas.cpp:1252).
Close → `closeDialog` → back to previous state, parked script thread resumed by
`thread->run()` (src/DialogSystem.cpp:559-563).

## 1. BOX STYLES

### 1.1 Where styles come from

`EV_DIALOG` carries two bytes: `strId = uByteArg5`, packed `uByteArg6` with
**style = arg & 0xF**, **flags/type = arg >> 4**
(src/ScriptThread.cpp:545-578: `startDialog(this, loadMapStringID, uByteArg5, n32, n31, true)` where
`n31 = uByteArg6 >> 4`, `n32 = uByteArg6 & 0xF`). Opcode id 13 (src/Enums.h:415).
C++ callers pass style/flags directly (`composeLootDialog` uses style 4 flags 0,
src/ScriptThread.cpp:2213; help dequeue uses style 2 flags 0, src/DialogSystem.cpp:770,830-834).

### 1.2 Style table (fill color `n2`, from the switch at src/DialogSystem.cpp:137-214)

Defaults before switch: border/text white `0xFFFFFFFF`, header strip `color2=0xFF666666`,
text x `n = dialogRect[0]+1` (src/DialogSystem.cpp:136-139).

| style | fill `n2` | extras | user-visible identity |
|---|---|---|---|
| default | `0xFF000000` black | — | plain black box |
| 3 | none | translucent fill `12800` (0x3200), box raised 10px, taller border; scroll-log layout (141-148) | scroll/message log |
| 16 | body black, header strip `0xFF000066` dark blue | title-bar layout like 2/9 (230) | comm-link variant |
| 4 | `0xFFB18A01` gold if flags&1 else `0xFF005A00` green | item-pickup box; 12px item-name bar when `dialogItem != nullptr` (154-161, 254-269) | loot popup |
| 11 | `0xFF800000` dark red | y=`hudRect[1]+20` if flags&2; VIOS malloc/delete choice (162-169) | VIOS terminal |
| 5 | `0xFF800000` dark red | speech-bubble tail icon (312-319); y shift if flags&2 (170-177) | NPC bubble |
| 8 | `Canvas::PLAYER_DLG_COLOR = 0xFF005617` green (src/Canvas.h:112) | vertical gradient lines (281-289), corner icon imgUIImages[30,0,15,9] (290), **hero portrait left** from `imgPortraitsSM` row `characterChoice-1` (291-310), text starts after portrait width (309-310) | **GREEN hero speech** |
| 14 | `0xFF002864` navy | falls through into case 1/6; rect y -= 20; tail icon [45,0,15,9] (183-186, 326-328) | comm-link variant |
| 1, 6 | `0xFF002864` navy | tail arrow imgUIImages[10,0,10,6] above box for style 1 (320-322) | **BLUE comm-link** (strings literally say "COMM LINK:", e.g. map00 str46-48) |
| 9 | `0xFF000000` black | title-bar layout; body text drawn with `currentCharColor = 2` = `0xFF00FF00` GREEN (249-251, 349-351; palette src/Graphics.h:25-28) | terminal/log/email viewer (green on black) |
| 10 | `0xFF2E0854` purple | y=`hudRect[1]+20`; tail icon [20,6] (197-201, 323-325) | special |
| 12, 13 | `0xFFB18A01` gold | familiar self-destruct confirm / armor repair confirm (202-209) | choice boxes |

### 1.3 Title-bar layouts (speaker name!)

Styles **2, 16, 9** draw an 18px header strip ABOVE the box filled with
`color2` (0xFF666666 gray) with a white 1px border, and draw
`dialogIndexes[0..1]` — i.e. **the first `|`-separated line of the buffer** —
centered in it as the speaker/title (src/DialogSystem.cpp:230-252).
So "speaker resolution" for these is textual: strings embed their own header,
e.g. map00 `"RE-MIND-ER|You can save..."`, `" --Mixom Glae-ven-scope--||..."`,
`"Email:|To: ..."` (tmp_map00 strings04, decoded in docs/research/2026-08-24-dialog-system.md §4).

Style 4 draws the same kind of 12px name bar only when `dialogItem != nullptr`
(item longName composed by the loot path) centered with anchor 3
(src/DialogSystem.cpp:259-269).

Styles 1/5/8/14 have no name bar: speaker identity comes from content,
speech bubbles (`EV_SPEECHBUBBLE` → Hud bubble text composed from
`loadMapStringID`, src/Hud.cpp:908) or the hero portrait (style 8 only).

### 1.4 Portraits

Only style 8 draws one: `app->hud->imgPortraitsSM`, height/3 rows, row =
characterChoice 1→0, 2→1, 3→2, at `(dialogRect[0]+2, dialogRect[1]+3)`
(src/DialogSystem.cpp:291-308). No NPC portraits anywhere in dialogState.

## 2. TEXT PIPELINE

### 2.1 String ids and tables

- `STRINGID(map,index) = map<<10 | index`; 5-bit map, 10-bit index
  (src/Text.h:81-83). Map/table indices used: 0 = common strings,
  1 = entity strings (`MenuStrings.h:21`), 3 = menu strings (`MenuStrings.h:23`),
  **per-map tables = 4 + (mapNameID-1)** set into `canvas->loadMapStringID` at map
  load (src/LoadingManager.cpp:310). EV_DIALOG always composes from
  `loadMapStringID` + byte operand (src/ScriptThread.cpp:578).
- Storage: 15 types/language; `strings.idx` = u16 count then 5-byte records
  (u8 chunk 0xFF-terminated, i32 LE offset; offset also closes the previous
  string's length), final record ends last length
  (src/Resource.cpp:169-207; chunks `strings00..02.bin` src/Resource.h:15).
  Per-type blob loaded by `loadTextFromIndex`, split into a NUL-offset textMap
  (src/Text.cpp:179-212). Type counts hardcoded in startup
  (src/Text.cpp:34-48), e.g. type0=253 strings, type4 (map00)=186.
- `composeText(map, idx, out)` decodes: `\%`→`%`, `\n`→newline, `%NN` →
  substitution of text-arg NN-1 (0-based, first arg is `%01`)
  (src/Text.cpp:281-326).

### 2.2 Text args

Args are a flat byte pool `dynamicArgs` with cumulative-end offsets in
`argIndex[50]`; `addTextArg(char/int/Text/substring/stringid)` append to it;
max 50 args (fatal error otherwise) (src/Text.h:17-18,74-80,
src/Text.cpp:222-275). Callers must `resetTextArgs()` before composing a new
parameterized string (e.g. loot line `<bullet><name>x<count>|`:
common str90 = `"\x88<arg0>x <arg2>|"`, args pushed at
src/ScriptThread.cpp:2165-2174). In-dialog usage: prepareDialog appends common
str50 (`"||"` spacing) when flags&4 or flags&1 (src/DialogSystem.cpp:637-647);
YES/NO/MALLOC/DELETE button labels are composed fresh without args
(src/DialogSystem.cpp:361-370, 422-439).

### 2.3 Wrap + page split

Char budget constants (480px canvas ⇒ displayRect[2]=480):
`dialogMaxChars = scrollMaxChars = (W-2)/9 = 53`,
`dialogWithBarMaxChars = scrollWithBarMaxChars = (W-9)/9 = 52`
(src/Canvas.cpp:89-92). 9 px/char, 16 px line height
(src/Graphics.cpp:574-580, 554; drawString h=16 src/Graphics.cpp:470-475).

`prepareDialog` (src/DialogSystem.cpp:601-718):
- lines per page `dialogViewLines`: style 3 → 4, style 8 → 3, style 2 → 3,
  everything else → 4 (607-618).
- Wrap with `Text::wrapText(maxChars)` (default splitter `'|'`,
  src/Text.cpp:744-745; wrap rules documented in that function: breaks on
  spaces/hyphens/explicit `'|'`). If wrapped line count exceeds the page size,
  re-wrap narrower with the `-9` variants to make room for the scrollbar:
  style 3: `scrollMaxChars` → `scrollWithBarMaxChars` (670-677); others:
  `dialogMaxChars` → `dialogWithBarMaxChars` (679-691); style 2/16/9 exclude the
  title line from the count (`--numLines`, 682-685). Style 8 tries normal width
  first, retries bar width if > 3 lines (648-668).
- Line table build: scan buffer for `'|'`, emit `dialogIndexes[i]={start,len}`
  pairs, `numDialogLines` total; `currentDialogLine=0`,
  `dialogLineStartTime=dialogStartTime=app->time`, `dialogTypeLineIdx=0`,
  `dialogFlags/dialogStyle` stored (692-712). NOTE: `'|'` chars remain in the
  buffer as line separators for drawString (src/Graphics.cpp:548-555).
- Style 2 plays sound 1027 on open (715-717).

### 2.4 Typewriter reveal

Per-line progressive reveal in dialogState (src/DialogSystem.cpp:333-354):
line i shows `len` chars once passed; current line
(`n12 == dialogTypeLineIdx`) shows `(app->time - dialogLineStartTime) / 25`
chars (**25 ms/char = 40 cps**); when complete, `++dialogTypeLineIdx` and reset
start time. FIRE skips the animation (sets typeLineIdx to page end),
see §4. Rendering itself is `graphics->drawString(dialogBuffer, n, n11, 0,
offset, visibleLen)` — strEnd limits the drawn char count
(src/Graphics.cpp:483-546).

## 3. RENDERING GEOMETRY (480x320 canvas)

Base rect (src/DialogSystem.cpp:130-134):
`x = -screenRect[0]`, `w = hudRect[2]` (=480), `h = viewLines*16+8`,
`y = 320 - h - 1`. With 4 view lines ⇒ 72px tall box at bottom of screen;
style 8 shifts y -= 64 (taller hero box, 179), style 14 y -= 20 (185),
styles 11/5/10 with flags&2 pin y = hudRect[1]+20 near top (165,173,199),
style 3 raises y by 10 more and pads ±10 (143-147).

Draw order in dialogState: hide touch buttons 1-7 (122-128) → box fill/border
per style (140-329) → optional YES/NO widgets → text lines loop (333-354,
x=n, y=rect.y+2, 16px steps) → blinking cursor `OSC_CYCLE[time/200%4]` next to
selected choice (355, 389, 402, 434, 448) → buttons/scrollbar:

- flags&2 (vertical YES/NO, left side): two 32px-high gray buttons
  (`0xFF4A4A4A`, highlighted `0xFF8A8A8A`) at `y=screenRect[3]-214` and `+64`,
  labels common str141 "Yes"/str140 "No", or str214 "MALLOC"/str215 "DELETE"
  when flags&8 (src/DialogSystem.cpp:357-406). Selection index =
  `game->scriptStateVars[4]`.
- flags&4 or flags&1 (bottom NO/YES pair on last page): two 96px-wide buttons
  at x=96/x=288 inside the box, labels str30 "Exit"/str31 "Play" (flags&4) or
  str140 "No"/str141 "Yes" (flags&1), highlighted tint `n2+0x333333`
  (src/DialogSystem.cpp:408-451). Buttons 3/4 carry the touch areas.
- Scrollbar via `canvas->drawScrollBar` when more pages exist; page-up/down/OK
  icons are fmButtons 5/6/7 using `imgPageUP/DOWN/OK_Icon.bmp` at x=390
  (src/Canvas.cpp:330-350, src/DialogSystem.cpp:460-489).
- Loot icon decorations when `lootingSystem.specialLootIcon != -1` (245-248).

HUD interaction: entering ST_DIALOG keeps the HUD painted
(`repaintFlags |= REPAINT_HUD`, hud->repaintFlags=47, soft keys cleared,
events cleared; zoom-out forced; src/Canvas.cpp:1084-1112). backPaint draws the
HUD for any non-menu state (src/Canvas.cpp:414-417); only the arrow controls /
sniper scope are suppressed while ST_DIALOG (src/Hud.cpp:747-771). The tick
keeps updating lerp sprites + 3D view under the box
(src/Canvas.cpp:920-926). Player "pause" is purely input-routing —
`Player::unpause(n)` called on close is an empty stub (src/Player.cpp:1419-1423,
called src/DialogSystem.cpp:527).

Fonts: all dialog text draws through `Graphics::drawString(canvas->imgFont,...)`
with 9px advance/16px lines (src/Graphics.cpp:470-480, 574-580);
`currentCharColor` selects a palette row applied per char (src/Image.cpp:192).

## 4. INPUT (`handleDialogEvents`, src/DialogSystem.cpp:29-112)

Key→action mapping: `getKeyAction` (src/InputEventController.cpp:47-141,
table 24-44). All ST_DIALOG keys go here (src/InputEventController.cpp:331-333);
movement is impossible — no movement actions exist in this handler. KEY_CLR/BACK
(18) is swallowed with no effect during dialogs (src/InputEventController.cpp:167-184).

Let `cur=currentDialogLine`, `last=numDialogLines-viewLines`:

- **ACTION_FIRE**: if typewriter still revealing → finish current page
  (`dialogTypeLineIdx = viewLines`); elif `cur < last` → next page
  (`cur += viewLines`, restart typewriter; clamp so final page stays full when
  flags&4 or flags&1, lines 42-44); else `closeDialog(false)` (34-49).
- **ACTION_UP**: line up (clamped at 0); when on last page AND flags&2, moves
  the YES/NO cursor instead — but only if `scriptStateVars[4]==0`, else it
  decrements var4 (51-67).
- **ACTION_DOWN**: line down clamped to `last`; when on last page AND flags&2,
  increments var4 up to 1 instead (69-89). Scrolling down mid-text sets
  `dialogLineStartTime=now, dialogTypeLineIdx=viewLines-1` so the newly shown
  line types out (86-88).
- **ACTION_LEFT/RIGHT** with (flags&5) and on last page: toggle var4 bit0
  (choice flip) (91-93). Otherwise LEFT falls through to the ACTION_MENU branch
  below and RIGHT pages forward (97-107).
- **ACTION_PASSTURN / ACTION_AUTOMAP**: `closeDialog(true)` — skip-close
  (94-96).
- **ACTION_MENU / ACTION_LEFT**: page back (`cur -= viewLines`, clamp 0)
  (97-102).
- **ACTION_RIGHT**: page forward (`cur += viewLines`, clamp to max(last,0))
  (103-108).
- Tail: if we ended back in ST_PLAYING with monsters idle, immediately dequeue
  the next queued help dialog (109-111).

Touch equivalents route through m_dialogButtons ids 0-8; buttons 3/4 write
var4=0/1 then synthesize FIRE (src/TouchController.cpp:378-419).

## 5. THREADING / LIFECYCLE

### 5.1 Scripted dialogs (EV_DIALOG, src/ScriptThread.cpp:545-585)

Decode: style=arg2&0xF, flags=arg2>>4. If automap → force ST_PLAYING first
(551-554). If `game->skipDialog` → whole op skipped (555-557). Styles
6/7/1 clear `player->inCombat` (558-560). Style **2 does NOT open a dialog** —
it enqueues a HELP message bound to this thread's pool index
(`enqueueHelpDialog(loadMapStringID, strId, threadIdx)`, scanning the 20-thread
pool, 561-573). Everything else: `player->prevWeapon = weapon` saved when
style==4, then
`canvas->startDialog(this, loadMapStringID, strId, style, flags, resume=true)`
(574-579). Always: `skipAdvanceTurn=true; queueAdvanceTurn=false;
unpauseTime=-1; return 2` — thread parks awaiting external resume
(580-584). `unpauseTime==-1` semantics: docs/original-code/tile-events-vm.md §2.2.

### 5.2 Resume protocol

`startDialog(...,resume=true)` stores `dialogThread` (src/DialogSystem.cpp:741-743).
`closeDialog(skip)`: clears loot icon, disposes buffer, then restores state in
priority order oldState==ST_INTER_CAMERA → ST_CAMERA (if camera active) →
ST_COMBAT (if combat not done) → ST_PLAYING; `dialogResumeMenu` forces
ST_MENU after (src/DialogSystem.cpp:520-557). Finally, iff
`dialogResumeScriptAfterClosed`: `game->skipDialog = skip; dialogThread->run();
skipDialog=false` (559-563). The resumed thread continues AFTER the EV_DIALOG;
a following EV_DIALOG chains the next box; if skip was requested
(PASSTURN/AUTOMAP close), subsequent EV_DIALOGs in this run() become no-ops
(via the skipDialog check) until run() returns and the flag resets.

`queueAdvanceTurn` side effects on close (530-532): true when
`(style 3 || style 4 || (style 2 && dialogType==1) || (style 12 && var4==1 && !skip))`
and no help messages pending — consumed later by the Canvas turn machinery
(docs/research/2026-08-23-game-flow.md). Style 11 + var4 records
VIOS malloc/angry state (533-540). Styles 12/13 trigger familiar self-destruct /
armor-repair purchases based on var4 (564-597). Repaint VIEW3D at end (598).

### 5.3 Help-message queue (16 slots FIFO)

Producers: `enqueueHelpDialog(EntityDef*)` type 1 (item picked up,
src/DialogSystem.cpp:890-908), `(int map,int idx,int threadIdx)` type 2
(EV_DIALOG style 2, 843-863), `(Text*, int type)` type ≥2 (865-888).
All reject when `!player->enableHelp` (EV_ENABLE_HELP opcode) or dying; error 41
when full. Consumer `dequeueHelpDialog(b)` runs only when not ST_DIALOG/not
closing, state is ST_PLAYING/ST_INTER_CAMERA (unless forced b), no secret
active (753-768). Composition: type 1 →
`"<longName>|<description>[ <extra hint str 32/34/35/36 by def class>]"`
(776-807); type 2 → compose(map,idx) (808-811); else raw Text (812-819).
Then FIFO shift, and if help enabled:
`startDialog(nullptr|thread, buffer, style=2, flags=0, resume = thread!=-1)`
(828-835). `dialogType` = original queue type; used by closeDialog's
queueAdvanceTurn rule (`dialogStyle==2 && dialogType==1` = item pickup popups
advance the turn) (773, 530-532).

### 5.4 Timeouts

None. There is no auto-close timer anywhere; `dialogStartTime` exists only for
the (stub) player unpause accounting, `dialogLineStartTime` only feeds the
typewriter (src/DialogSystem.h:17-18, src/DialogSystem.cpp:339,707-709,
src/Player.cpp:1419-1423).

### 5.5 Other entry points

- Loot: `ScriptThread::composeLootDialog` builds `"<source text><taken items>"`
  and calls `startDialog(this, buffer, 4, 0, true)` (src/ScriptThread.cpp:2121-2223).
- Menu-driven dialogs use `dialogResumeMenu` (set false by every startDialog
  call site found; menu system re-enters ST_MENU on close,
  src/DialogSystem.cpp:742,555-557).

## 6. MAP00 INTRO (new game), replay recipe

Verified against tmp_map00.bin (bytecode walk per tile-events-vm.md §1.1;
disasm docs/research/assets/map00_disasm.txt). Strings quoted from strings04
chunk (see raw report §4 for decode tooling). Hyphens below are the source's
soft hyphens; `|` = explicit line break.

Boot sequence (src/LoadingManager.cpp:692-706): INIT_MAP staticFunc@0 → spawn-tile event → play.

1. **INIT_MAP @0** (disasm 0-252): names the squad NPCs via EV_NAMEENTITY —
   sprite7→str42 "Major Morgan", sprite10→str43 "Sgt. Blazkowicz",
   sprite9→str44 "Dr. O'Connor"; signs sprite2 "Docking Elevator"(112),
   96 "To Cichus Base"(111), 207 "Maintenance Access"(125), 211 "ATM"(138),
   71 "Armor Repair Station"(160). Fog setup; locks door sprite37; NPCCHAT.
   New-game gate `EVAL v17 == 0` (@221) → CALL func@1913, then `v17 = 1`.
2. **func@1913** calls 1942, 2084, 2328 in order.
   - @1942 (camera 0, dropship landing): cockpit overlay toggled off, fade-in,
     subtitles str9 "Cap-tain: Eve-ry-one, bu-ckle up!" (3000ms), str10
     "Cap-tain: We're land-ing at Ty-cho Sta-tion." (4000ms), explosion FX,
     shake, str11 "Rough land-ing. Sor-ry about that!" (3000ms), str12
     "What the… what was that?!" (3000ms).
   - @2084 (camera 1, crash site): subtitle str13/str14 "Ma-jor: Some-thing
     strange is go-ing on here." vs str15/str16 "Eve-ry-one, be on guard."
     chosen by characterChoice (v14) (@2230-2290).
   - @2328: squad fans out (lerps), camera 5, EV_GOTO teleports player to tile
     (5,19) facing E (@2424 `OP15 275`), quiet door ops, grenade-prop gag,
     subtitle str17 "HELP! There's…" / str18 "Die hell spawn!" / str19
     "Fol-low me! HUR-RY!!", rename sprite19 → str20 "Cal-dex" (@2877),
     NPCCHAT(@2933).
3. **Intro dialog scene = tile event evt[54], tile 617 = (9,19), trigger mask
   0xFF4 (TRIGGER + all dirs) → IP 2993.** Fires when the player stands east of
   it and attacks/uses toward it (trigger semantics:
   docs/original-code/tile-events-vm.md §4.2). Sequence:
   - disables 3 events (EVENTOPs), then **DIALOG str21, style 1** (comm-link,
     navy): "It's about time you got here! We need all the as-sis-tance we can
     get. Hur-ry, let's get mov-ing!"
   - characterChoice==1: WAIT500, **DIALOG str23 style 8** (hero, green):
     "Some-one needs to stay here … I'll stay. The rest of you move out."
     Otherwise: LERP offset, **DIALOG str22 style 1** ("Some-one needs to stay
     here to meet the rest of our squad."), WAIT500, **DIALOG str24 style 8**
     (hero): "I'll stay."
   - if characterChoice != 3 (@3048 `EVAL v14 NEQ c3`, false-jump over the
     block when v14==3): WAIT500, **DIALOG str25 style 1** (the GUN GIVE):
     "Al-right, you'll need this then. With luck, we'll meet up soon."; NPCCHAT;
     prop lerp; CALL @3401 → **GIVEITEM def=1** (pistol, eType6/sub1/parm0) +
     difficulty ammo (def90 sub2parm3 easy / def40 armor medium / def10 pistol
     hard), EV_ENABLE_HELP(1); MESSAGE common-style 32891 = map str133 style3
     "You equip-ped your wea-pon."; WAIT1000. NOTE: characterChoice==3 skips
     this give verbatim in the data.
   - movement/camera choreography, camera 7 (@3250), quiet unlock+open of blue
     door area (DOOROP 5134/1046/6158/6166 = sprites 14/22 quiet ops), squad
     leaves; func@3333 (called twice mid-scene): WAIT500,
     **DIALOG str26 style 8 if v14==1 else style 1**: "We'll stay in touch via
     our comm link. Stay alert!", SPEECHBUBBLE str27 "Let's go!",
     DOOROP sprite22 op3 UNLOCK then op0 OPEN (blocking; thread resumes on door
     lerp — same external-resume protocol as dialogs).
4. Follow-up scenes reuse the same machinery: str28-35 (stay-behind NPC
   exchange incl. "Search the body and get the key-card…", styles 1/8),
   str41 LOOTING tutorial (style 2), comm-link death scene str45-50 (style 8
   hero + str49 "*hisss*" style 14), VIOS/terminal logs str141-158 (styles 9/3,
   green-on-black / log), emails str93-105 (style 9). Tutorial REMINDER/HINT
   popups str2,3,5,6,7,8 open with **style 2** (funcs @1619-1689, each preceded
   by EVENTOP to fire once).

Speaker summary: hero lines = style 8 (portrait + green); squad/NPC comm-link
lines = style 1 (navy + tail); tutorial/help/loot = styles 2/4 (gray/black +
header strip, first buffer line is the title); logs/email/terminals = style 9
(green text) or 3 (scroll log).

## PORT CHECKLIST (faithful minimum)

1. **State + routing**: ST_DIALOG state; all input → dialog handler; restore
   prior state on close (INTER_CAMERA/CAMERA/COMBAT/PLAYING priority); clear
   soft keys/events on enter (§0, §3, §5.2).
2. **Styles**: implement the §1.1 table at least for styles 1, 2, 3, 4, 8, 9,
   14 (+flags): exact fills 0xFF002864 / 0xFF666666 header / 0xFF005A00-0xFFB18A01
   / 0xFF005617 gradient / 0xFF000000 + green text; hero portrait row =
   characterChoice-1; tail arrows for 1/5/14.
3. **Speaker/title**: first `|`-line as centered header for styles 2/16/9;
   item-name bar for style 4 with dialogItem.
4. **Text pipeline**: STRINGID packing; per-map table 4+(mapID-1); composeText
   escapes (\%, \n, %NN) with a 50-slot arg pool; wrapText (space/hyphen/|
   breaking) at (W-2)/9 chars, retry (W-9)/9 when lines overflow;
   viewLines {3:4, 8:3, 2:3, else 4}; dialogIndexes line table.
5. **Typewriter + paging**: 25ms/char reveal; FIRE completes page → advances
   page → closes; scrollbar + PageUp/PageDown/OK icons.
6. **Input keymap**: §4 exactly (FIRE/UP/DOWN/LEFT/RIGHT/PASSTURN/AUTOMAP/
   MENU; var4 choice cursor with flags&2; LEFT/RIGHT toggle choices with
   flags&5 on last page; PASSTURN/AUTOMAP = skip-close).
7. **Choices**: flags&2 vertical Yes/No (or MALLOC/DELETE with flags&8);
   flags&4/&1 bottom Exit/Play or No/Yes on last page; selection stored in
   `scriptStateVars[4]`; blinking OSC_CYCLE cursor.
8. **Threading**: EV_DIALOG B,B decode (style=lo nibble, flags=hi nibble);
   park unpauseTime=-1; resume via thread->run() with skipDialog flag;
   chained dialogs; style 2 = help enqueue (FIFO 16, type 1/2 composition,
   auto-dequeue when playing & monsters idle); queueAdvanceTurn rules on close;
   sound 1027 for style 2; no timeouts.
