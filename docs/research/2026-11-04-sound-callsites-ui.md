# Sound call sites in the original UI code (inventory)

Date: 2026-11-04
Question: list every sound call (playSound / stopSound / soundStop / isSoundPlaying /
soundClick / volume helpers) in the original UI/state paths of `src/`, with the
rewrite's equivalent in `new_src/`.
Verdict: CONFIRMED (inventory complete for the files listed in "Method").

## Method

- `grep -a` (MenuSystem.cpp, Graphics.cpp, Input.cpp are ISO-8859 + CRLF; plain grep
  treats MenuSystem.cpp as binary and silently misses it) for
  `playSound|stopSound|soundStop|isSoundPlaying|soundClick|volumeUp|volumeDown|musicVolume*|updateVolume|allowSounds|allowMusics`
  over `src/*.cpp`, plus every `new fmButton(..., soundResID)` / `new fmScrollButton(...)`.
- Read each call site's control flow. TravelMapManager, LoadingScreenController,
  AutomapController, Hud.cpp (except button ctor IDs), MiniGameManager (except button ctor
  IDs), SoftKeyController, Text: **no direct sound calls**.
- new_src: `grep -rn "\[sound\]|\[dialog\] sound|\[script\] sound|\[loot\] sound"`.
- Map music: `tools/disasm_map_scripts.py tmp_map00.bin` (only map00 is extracted locally).

## Sound API semantics (needed to read the table)

| Fact | Evidence |
|---|---|
| Signature `playSound(int16 resID, uint8 flags, int priority, bool a5)`; `a5` unused in body | `src/Sound.h:91`, `src/Sound.cpp:306-441` |
| resID `-1`/`255` = no-op | `src/Sound.cpp:330-334` |
| **Music group = resID 1067..1071** (`(resID-1067) <= 4`), gated by `allowMusics` and uses `musicVolume`; everything else gated by `allowSounds`, uses `soundFxVolume` | `src/Sound.cpp:341-349`, `:421-424` |
| flags bit0 = loop (`AL_LOOPING`); bit0 also means "do not restart if this resID already playing" | `src/Sound.cpp:389-393`, `:438`, `:169-175` |
| flags bit1 = stop all 10 channels first | `src/Sound.cpp:395-402` |
| priority 6 = also "do not restart if already playing"; otherwise priority picks the channel to steal (`getFreeSlot`: lowest priority < requested) | `src/Sound.cpp:369-374`, `:389`, `:445-472` |
| 10 channels | `src/Sound.cpp:353`, `:475` |
| `soundStop()` stops all 10 channels (music included) | `src/Sound.cpp:474-480` |
| `stopSound(resID, fade)`: first channel with resID; fade = 500 ms | `src/Sound.cpp:482-504` |
| `isSoundPlaying(resID)`: first channel with resID, OpenAL playing state | `src/Sound.cpp:506-513` |
| Volume 0..100 -> `AL_MAX_GAIN = v/100` | `src/Sound.cpp:108-113` |
| `updateVolume()`: re-applies volume to non-fading channels, then `allowMusics = musicVolume!=0`, `allowSounds = soundFxVolume!=0` | `src/Sound.cpp:515-533` |
| `volumeUp/Down(n)` = soundFxVolume +-n clamped 0..100 then updateVolume; `musicVolumeUp/Down` same for musicVolume ([GEC]) | `src/Sound.cpp:547-561`, `:589-603` |
| Defaults: allowSounds/allowMusics = true, soundFxVolume = musicVolume = 80 | `src/Sound.cpp:32-35` |
| Config persists allowSounds, soundFxVolume, musicVolume (bytes) and calls updateVolume on load | `src/Game.cpp:1922-1924`, `:1985-1988`, `:2050`, `:2327-2329` |
| `MenuSystem::soundClick()` = `playSound(1027, 0, 5, false)` (1027 = **Dialog_Help.wav**) | `src/MenuSystem.cpp:5874-5876` |
| `fmButton::SetHighlighted(true)` on a false->true edge plays `soundResID` (flags 0, pri 5) unless `-1` or already playing | `src/Button.cpp:145-158` |
| `fmScrollButton` drag inside bar plays `soundResID` (flags 0, pri 5) unless already playing | `src/Button.cpp:519-526` |

resID -> file: `Sounds::RESOURCE_FILE_NAMES[resID-1000]` (`src/Sounds.h:7-153`). IDs used here:
1027 Dialog_Help, 1032 explosion, 1040 hack_fail, 1043 hack_success, 1054 item_pickup,
1064 menu_open, 1065 menu_scroll, 1067 Music_Boss, 1068 Music_General, 1069 Music_Level_End,
1070 Music_LevelUp, 1071 Music_Title, 1072 no_use, 1073 no_use2, 1086 pistol,
1090 player_death1, 1091 player_death2, 1092 player_death3,
1113 slider, 1121 switch, 1122 switch_exit, 1125 vending_sale.

## Inventory

Columns: src location | call | resID (name) | flags | pri | trigger | new_src equivalent.

### Boot, logo, intro movie, main menu, back-to-main

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/GameStateRunner.cpp:328-330` | `cacheSounds()` | - | - | - | ST_LOGO first frame if not loaded (logo itself is silent) | not ported (no audio backend) |
| `src/IntroSequenceManager.cpp:780` | playSound | 1068 Music_General | 1 (loop) | 6 | ST_INTRO_MOVIE, every frame while `stateVars[3]==0` (pri 6 -> no restart) | `new_src/core/IntroMovieSession.cpp:65` `[sound] 1068` |
| `src/IntroSequenceManager.cpp:872` | soundStop | all | - | - | `exitIntroMovie()` (skip or end of movie) | `new_src/core/GameContext.cpp:706` `[sound] stop` |
| `src/Canvas.cpp:1404` | playSound | 1071 Music_Title | 1 | 3 | `backToMain(false)` (after intro, char-select MENU, "back to main" menu action, etc.) | `new_src/core/GameContext.cpp:700` `[sound] 1071` |
| `src/Canvas.cpp:1386-1401` | (none) | - | - | - | `backToMain(true)` (skipIntro path) plays NO music | n/a |
| `src/MenuSystem.cpp:3056-3060` | playSound | 1068 Music_General | 1 | 3 | ACTION_TOGSOUND while menu == MENU_ENABLE_SOUNDS(25) -> ST_INTRO_MOVIE. Dead path: nothing ever `setMenu(25)` (only compared at `src/Canvas.cpp:791`, `src/InputEventController.cpp:174`) | not ported (dead) |

### MenuSystem (all menus)

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/MenuSystem.cpp:317` | soundClick | 1027 | 0 | 5 | `scrollDown()` when item not block-text (flag 0x100) | not ported (`MenuSession::moveDir` `new_src/core/MenuSession.cpp:1487` silent) |
| `src/MenuSystem.cpp:327` | soundClick | 1027 | 0 | 5 | `scrollUp()` same condition | not ported |
| `src/MenuSystem.cpp:336` | soundClick | 1027 | 0 | 5 | `scrollPageDown()` non-block-text | not ported (`new_src/core/MenuSession.cpp:1583`) |
| `src/MenuSystem.cpp:353` | soundClick | 1027 | 0 | 5 | `scrollPageUp()` non-block-text | not ported (`new_src/core/MenuSession.cpp:1572`) |
| `src/MenuSystem.cpp:539-541` | playSound | 1065 menu_scroll | 0 | 5 | touch-scroll list: scrollbar `field_0x48_` changed vs `old_0x48` | not ported |
| `src/MenuSystem.cpp:578-580` | isSoundPlaying+playSound | 1122 switch_exit | 0 | 5 | `back()` unless menu is MAIN_MORE_GAMES or MAIN, and not already playing | `new_src/core/MenuSession.cpp:1934` `[sound] 1122` (no isSoundPlaying guard, no MORE_GAMES case) |
| `src/MenuSystem.cpp:2852` | soundClick | 1027 | 0 | 5 | ACTION_MENU_ITEM_INFO on item with helpField (not in MAIN_OPTIONS) -> open help text | not ported |
| `src/MenuSystem.cpp:2983-2985` | playSound | 1125 vending_sale | 0 | 3 | `select()` in MENU_VENDING_MACHINE_CONFIRM (after disabled-check) | `new_src/core/MenuSession.cpp:1720` `[sound] 1125` |
| `src/MenuSystem.cpp:2990-2993` | soundStop | all | - | - | `select()` in MENU_END_RANKING -> ST_CREDITS | not ported (end ranking absent) |
| `src/MenuSystem.cpp:3013` | playSound | 1086 pistol | 0 | 5 | `select()` any action item, except LEVEL_STATS, MORE_GAMES, and type 5/7 (help/notebook) menus which return earlier | not ported (`MenuSession::select` `new_src/core/MenuSession.cpp:1705-1729` silent) |
| `src/MenuSystem.cpp:3087-3090` | playSound | 1071 Music_Title | 1 | 3 | ACTION_CHANGESTATE to ST_CREDITS when `loadMapID != 0` (in-game credits) | `new_src/core/MenuSession.cpp:1859` `[sound] 1071` |
| `src/MenuSystem.cpp:3209-3219` | soundStop | all | - | - | ACTION_MINIGAME param 2/0/4 (Hacker / MatrixSkip / Vending from main menu) before `playFromMainMenu()` | `new_src/core/MenuSession.cpp:1865` `[sound] stop` |
| `src/MenuSystem.cpp:3583` | soundStop | all | - | - | `startGame(b)` (New Game / Continue) | not ported in `new_src/core/LoadingManager.cpp:40` startGame (but loadMap stop below fires) |
| `src/MenuSystem.cpp:173,183,188,199,203,207,225,229,238,249,256` | fmButton ctor | 1027 | 0 | 5 | menu item rows, arrows, slider buttons 12/13/14/16/17, info buttons, vending arrows: highlight edge | not ported (`new_src/ui/MenuView.cpp:258` notes "Not ported ... 1027") |
| `src/MenuSystem.cpp:193,219` | fmButton ctor | 1122 switch_exit | 0 | 5 | menu back (11) / return-to-game (15) buttons: highlight edge | not ported |
| `src/MenuSystem.cpp:233` | fmScrollButton ctor | 1065 menu_scroll | 0 | 5 | menu scrollbar drag | not ported |

### Options / Sound menu ([GEC])

| src | What | new_src |
|---|---|---|
| `src/MenuSystem.cpp:1381-1382`, `:1391-1392` | Options lists have a "Sound" entry -> MENU_MAIN_OPTIONS_SOUND (-3) / MENU_INGAME_OPTIONS_SOUND | not ported (`new_src/core/MenuSession.cpp:318` comment: no MenuId) |
| `src/MenuSystem.cpp:2304-2343` | Sound menu items: title "Sound Options"; "SoundFx Volume:" scrollbar (ACTION_CHANGESFXVOLUME=15, param 1); "Music Volume:" scrollbar (ACTION_CHANGEMUSICVOLUME=37, param 2). Main menu uses ITEM_SCROLLBARTWO, in-game ITEM_SCROLLBAR. `items[3]` DISABLEDTWO if `!isUserMusicOn()` (always true, `:5814-5816`) | not ported |
| `src/MenuSystem.cpp:3120-3125`, `:3276-3281` | Selecting the row toggles `changeSfxVolume` / `changeMusicVolume` and `changeValues` (edit mode) | not ported |
| `src/InputEventController.cpp:186-212` | In edit mode: RIGHT/LEFT -> `volumeUp/Down(10)` or `musicVolumeUp/Down(10)` + soundClick(1027) + refresh | not ported |
| `src/InputEventController.cpp:214-271` | Same edit mode for alpha (+-10), vibration (+-10), deadzone (+-5): each step soundClick(1027) | not ported |
| `src/MenuSystem.cpp:5818-5867` | Touch/drag slider buttons 12 (sfx) / 13 (music): `value = 100*(x - x0) / (w)`, clamped 0..100, then `updateVolume()`; x0/w differ main vs in-game (`:5847-5855`). Called from `:4779`, `:4838`, `:4881` | not ported |
| `src/MenuSystem.cpp:5312`, `:5322` | Slider shows 0 when `!allowSounds` / `!allowMusics`, else the volume | not ported |
| `src/InputEventController.cpp:277-295` | iOS hardware volume keys 27/28 -> volumeUp/Down(10): inside `#if 0` (dead) | n/a |
| `src/MenuSystem.cpp:3556-3562` | debug SYS_SOUND / SYS_SOUND_DELAY toggle `sysSoundDelayTime`; `src/Canvas.cpp:989-992` then plays random `(nextByte()&0xF)+1000`, fl 0, pri 3 periodically | not ported (debug) |

### Character select, prologue, credits, comic book, help

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/IntroSequenceManager.cpp:646` | soundClick | 1027 | 0 | 5 | char select stage 0 (`stateVars[8]==0`), RIGHT changes character | `new_src/core/CharacterSelectSession.cpp:53` |
| `src/IntroSequenceManager.cpp:655` | soundClick | 1027 | 0 | 5 | stage 0, LEFT changes character | `new_src/core/CharacterSelectSession.cpp:53` (shared) |
| `src/IntroSequenceManager.cpp:662` | playSound | 1086 pistol | 0 | 5 | stage 0 FIRE -> confirm stage | `new_src/core/CharacterSelectSession.cpp:67` |
| `src/IntroSequenceManager.cpp:672` | soundClick | 1027 | 0 | 5 | stage 1 LEFT/RIGHT toggles Yes/No | `new_src/core/CharacterSelectSession.cpp:75` |
| `src/IntroSequenceManager.cpp:685` | playSound | 1086 pistol | 0 | 5 | stage 1 FIRE (after start game or backToMain) | `new_src/core/CharacterSelectSession.cpp:77` |
| `src/IntroSequenceManager.cpp:691` | playSound | 1122 switch_exit | 0 | 5 | stage 1 MENU -> back to stage 0 | `new_src/core/CharacterSelectSession.cpp:83` |
| `src/IntroSequenceManager.cpp:37,43,45,47` | fmButton ctor | 1027 | 0 | 5 | char-select / story buttons highlight edge | not ported |
| `src/IntroSequenceManager.cpp:698-750` | (none) | - | - | - | prologue story input: silent | n/a |
| `src/InputEventController.cpp:387-391` | soundStop | all | - | - | ST_CREDITS with `endingGame`: FIRE after text done (or key 18) -> MENU_END_ | not ported (endingGame not ported) |
| `src/InputEventController.cpp:408-410` | soundStop | all | - | - | ST_CREDITS not ending, BACK/FIRE, `loadMapID != 0` branch -> INGAME_HELP. Main-menu branch (`loadMapID==0`) does NOT stop: Music_Title keeps playing | `new_src/core/GameContext.cpp:455` `[sound] stop` |
| `src/ComicBook.cpp:734-736` | isSoundPlaying+playSound | 1064 menu_open | 0 | 5 | comic touch hits a button (`ButtonTouch`) and 1064 not playing | `new_src/core/ComicBookSession.cpp:86` (no isSoundPlaying guard) |
| `src/DialogSystem.cpp:715-717` | playSound | 1027 Dialog_Help | 0 | 3 | `startDialog` with dialogStyle == 2 (help dialog) | `new_src/domain/game/DialogSystem.cpp:169` `[dialog] sound 1027` |
| `src/Canvas.cpp:327,330,336,342,348` | fmButton ctor | 1027 | 0 | 5 | dialog buttons (choices, page up/down/OK) highlight edge | not ported |
| `src/Canvas.cpp:355,357` | fmButton ctor | 1027 | 0 | 5 | soft-key buttons 19/20 highlight edge | not ported |

### Loading, level end, epilogue, level-up, dying

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/LoadingManager.cpp:70` | soundStop | all | - | - | `loadMap()` every map change (kills any music) | `new_src/core/LoadingManager.cpp:139` `[sound] stop` |
| `src/LoadingManager.cpp:619-621`, `:721` | allowSounds=false .. restore | - | - | - | map init muted while `inInitMap` | not ported |
| `src/ScriptThread.cpp:1083-1090` | playSound | script byte + 1000 | `args>>4` | `args&0xF` | EV_PLAYSOUND; during initMap forced audible only if `canvas->areSoundsAllowed` (init false `src/Canvas.cpp:188`, set from config `src/Game.cpp:1986`) | `new_src/domain/game/ScriptVM.cpp:978-983` log only |
| `src/ScriptThread.cpp:1955-1959` | stopSound(id+1000, false) | script | - | - | EV_STOPSOUND | `new_src/domain/game/ScriptVM.cpp:985-988` log only |
| `src/Canvas.cpp:888-890` | playSound | 1069 Music_Level_End | 1 | 3 | ST_SAVING done with saveType & 0x10 (level-end save) -> MENU_LEVEL_STATS | `new_src/core/LoadingManager.cpp:202` `[sound] 1069` |
| `src/IntroSequenceManager.cpp:143,145` | soundStop; playSound | 1069 Music_Level_End | 1 | 3 | `disposeEpilogue()` (end of epilogue, `src/Canvas.cpp:844`, `src/InputEventController.cpp:328`) -> MENU_LEVEL_STATS | not ported (no epilogue) |
| `src/Canvas.cpp:1153-1156` | allowSounds save/restore | - | - | - | ST_EPILOGUE entry: assigns and immediately restores (net no-op) | n/a |
| `src/MenuSystem.cpp:2996-3001` | (none) | - | - | - | LEVEL_STATS select -> loadMap (whose `:70` soundStop ends 1069) | n/a |
| `src/Player.cpp:350-351` | soundStop; playSound | 1070 Music_LevelUp | 0 | 5 | level up, when state != ST_MENU | not ported |
| `src/Player.cpp:720-727` | playSound | 1092 / 1091 / 1090 | 0 | 3 | `died()`: characterChoice 1 -> 1092, 2 -> 1091, 3 -> 1090 | `new_src/core/GameContext.cpp:285` |
| ST_DYING / MENU_INGAME_DEAD ("Try Again") | (none) | - | - | - | no sound calls on these paths besides the generic menu select pistol `:3013` / back `:578` | covered by MenuSession rows |
| `src/Game.cpp:2510-2534` | allowSounds=false; soundStop; restore | all | - | - | `skipCinematic()` | not checked |

### Travel map

No sound calls in `src/TravelMapManager.cpp` (grep). Music is already stopped by
`src/LoadingManager.cpp:70` before ST_SAVING/ST_TRAVELMAP. new_src `TravelMapSession.cpp`: none (matches).

### HUD buttons

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/Hud.cpp:67,70,73,76,79,82,85` | fmButton ctor | 1121 switch | 0 | 5 | bottom-bar buttons (switch L/R, weapon, face, armor, health, keys) highlight edge | not ported (`new_src/core/HudButtons.cpp` silent) |
| `src/Hud.cpp:90` | fmButton ctor | 1027 | 0 | 5 | weapon grid buttons highlight edge | not ported |
| `src/Canvas.cpp:241` + `src/ZoomController.cpp:129,147` | fmScrollButton 1113 / playSound(1113,0,5) | 1113 slider | 0 | 5 | sniper scope dial | `new_src/core/ZoomController.cpp:120,129` |

### Minigames

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/HackingGame.cpp:221,225` | soundClick | 1027 | 0 | 5 | Hacker play: LEFT/RIGHT column move | `new_src/minigames/hacking/HackingGame.cpp:208` |
| `src/HackingGame.cpp:389` | soundClick | 1027 | 0 | 5 | touch-hover enters a new column (draw path) | `new_src/minigames/hacking/HackingGame.cpp:408` |
| `src/HackingGame.cpp:95,98` | fmButton ctor | 1027 | 0 | 5 | exit/back buttons highlight edge | `new_src/minigames/hacking/HackingGame.cpp:342` |
| `src/HackingGame.cpp:622` | playSound | 1043 win / 1040 lose | 0 | 3 | `endGame(n)` | `new_src/minigames/hacking/HackingGame.cpp:293` |
| `src/HackingGame.cpp:633` | playSound | 1071 Music_Title | 1 | 3 | endGame when played from main menu | `new_src/minigames/common/MiniGameHost.cpp:81` (shared) |
| `src/SentryBotGame.cpp:234,242` | soundClick | 1027 | 0 | 5 | MatrixSkip cursor LEFT (if cursor > -1) / RIGHT | `new_src/minigames/sentrybot/SentryBotGame.cpp:202,208` |
| `src/SentryBotGame.cpp:513` | soundClick | 1027 | 0 | 5 | touch-hover onto a different cell | `new_src/minigames/sentrybot/SentryBotGame.cpp:337` |
| `src/SentryBotGame.cpp:101,104,107,114` | fmButton ctor | 1027 | 0 | 5 | exit/back/submit/delete highlight edge | `new_src/minigames/sentrybot/SentryBotGame.cpp:305` |
| `src/SentryBotGame.cpp:809` | playSound | 1043 / 1040 | 0 | 3 | `endGame(n)` | `new_src/minigames/sentrybot/SentryBotGame.cpp:271` |
| `src/SentryBotGame.cpp:821` | playSound | 1071 | 1 | 3 | endGame from main menu | `new_src/minigames/common/MiniGameHost.cpp:81` |
| `src/VendingMachine.cpp:335` / `:343` | playSound | 1043 / 1040 | 0 | 3 | 4th digit entered: win / lose | `new_src/minigames/vending/VendingMachine.cpp:282` / `:288` |
| `src/VendingMachine.cpp:353,361,368,372` | soundClick | 1027 | 0 | 5 | slider UP/DOWN (always, even at limit), cursor LEFT/RIGHT | `new_src/minigames/vending/VendingMachine.cpp:297,303,309,312` |
| `src/VendingMachine.cpp:597-599` | soundClick | 1027 | 0 | 5 | main terminal cursor changed to a valid row (draw path) | `new_src/minigames/vending/VendingMachine.cpp:555` |
| `src/VendingMachine.cpp:81,99,107,110` | fmButton ctor | 1027 | 0 | 5 | submit/arrows/back/exit highlight edge | `new_src/minigames/vending/VendingMachine.cpp:384` |
| `src/VendingMachine.cpp:861` | playSound | 1071 | 1 | 3 | endGame from main menu | `new_src/minigames/common/MiniGameHost.cpp:81` |
| `src/MiniGameManager.cpp:36,42` | fmButton ctor | 1027 | 0 | 5 | target practice (PowerUp) buttons highlight edge; no other sound calls | `new_src/core/TargetPractice.cpp`: none (not ported) |
| `src/MenuSystem.cpp:3209-3219` | soundStop | all | - | - | before minigame from main menu (see MenuSystem table) | `new_src/core/MenuSession.cpp:1865` |

### In-game dialog side effects (UI-adjacent)

| src | call | resID | fl | pri | Trigger | new_src |
|---|---|---|---|---|---|---|
| `src/DialogSystem.cpp:575` | playSound | 1054 item_pickup | 0 | 3 | armor-repair dialog (style 13) "yes" with >= 5 parts | not checked |
| `src/DialogSystem.cpp:588,591` | playSound | 1073 / 1072 | 0 | 3 | same, not enough parts; by characterChoice (1 -> 1073, 2..3 -> 1072) | not ported as dialog; `new_src/domain/game/ScriptVM.cpp:1705-1706` prints the 1073/1072 pair for EV_START_ARMORREPAIR with < 5 parts (= `src/ArmorRepairSystem.cpp:33-38`, not this DialogSystem path) |
| `src/GameStateRunner.cpp:293` | playSound | 1032 explosion | 0 | 3 | familiar self-destruct, 1500 ms after | `new_src/core/Familiar.cpp:192` |

## Music per state / per map — summary

- Title: 1071 looped after intro (`src/Canvas.cpp:1404`), after main-menu minigames
  (`src/HackingGame.cpp:633`, `src/SentryBotGame.cpp:821`, `src/VendingMachine.cpp:861`),
  and on in-game Credits (`src/MenuSystem.cpp:3089`).
- Intro movie: 1068 looped, pri 6 (`src/IntroSequenceManager.cpp:780`).
- Level stats: 1069 looped (`src/Canvas.cpp:889`, `src/IntroSequenceManager.cpp:145`).
- Level up jingle: 1070 one-shot after soundStop (`src/Player.cpp:350-351`).
- **No C++ level-music selection exists**: the only music IDs in `src/*.cpp` are the sites
  above (grep `106[789]|107[01]`). In-level music comes from map scripts via EV_PLAYSOUND
  (`src/ScriptThread.cpp:1077-1091`). map00 disassembly: `1951: PLAYSOUND id=1068 vol=0 pri=5`
  and `2119: STOPSOUND id=1068` (tool prints flags as `vol`; flags 0 = not looped).
  Music_Boss 1067 never appears in C++.
- Every `loadMap` stops all sounds (`src/LoadingManager.cpp:70`).

## Open questions

- Per-map music for map01..map10 not inventoried (only map00 extracted locally).
- `src/Game.cpp:2510-2534` skipCinematic and the DialogSystem armor-repair rows were not
  mapped to new_src.
