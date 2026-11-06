# Sound engine (original `src/Sound.cpp`)

Verified facts about the legacy sound system. All claims cite `src/`.
Backend is OpenAL; the J2ME/iOS logic is a thin "10 slots of (buffer, source)" layer.

## 1. Resource mapping

- `resID` is an index into `Sounds::RESOURCE_FILE_NAMES` offset by 1000:
  `int index = (uint16_t)(resID - 1000);` (`src/Sound.cpp:180`), table at
  `src/Sounds.h:7-152` (entry `i` sits on line `i+8`). 145 entries, resID 1000..1144.
  No bounds check.
- File path: `InputStream::loadFile(name, LT_SOUND_RESOURCE)` prefixes
  `"Payload/Doom2rpg.app/Packages/sounds2/"` and reads the zip entry
  (`src/JavaStream.cpp:82-90`, `LT_SOUND_RESOURCE = 7` at `src/JavaStream.h:9`).
  Missing file -> `app->Error("Failed to open audio: %s")` (`src/Sound.cpp:272-274`),
  then "Sound resource not found" (`src/Sound.cpp:188`).
- The .ipa holds 148 wavs in `sounds2/`; 3 are never referenced by the table:
  `SpiderMastermind_pain.wav`, `SpiderMastermind_walk.wav`, `flames.wav`
  (diff of table vs `unzip -l`).
- Music = resID 1067..1071 (`Music_Boss`, `Music_General`, `Music_Level_End`,
  `Music_LevelUp`, `Music_Title`). The test is always
  `(unsigned)(resID - 1067) <= 4` (`src/Sound.cpp:341,421,431,489,520`).
- Special IDs: `-1` and `255` are no-ops; `1255` is also a no-op
  (`src/Sound.cpp:330-339`). Scripts pass `resID + 1000` from a ubyte
  (`src/ScriptThread.cpp:1078,1086,1090`), so script byte 255 -> 1255 = "none".

### WAV loader (`openAL_LoadAudioFileData`, `src/Sound.cpp:210-269`)

Fixed-layout parser, little-endian via `memcpy` (`src/JavaStream.cpp:11-17`):
`"RIFF"`, u32 size, `"WAVE"`, `"fmt "`, u32 fmtLen which must be 16 or 18
(`:234`), u16 formatTag must be 1/PCM (`:238`), u16 channels, u32 sampleRate,
u32 (byte rate; comment says block align), u16 (block align), u16 bits; if
fmtLen==18 skip one u16 (`:248-250`, only `SpiderMastermind_sight.wav`). Then
it ASSUMES the next chunk is `data`: skips the 4-byte id unchecked, reads u32
size, copies that many bytes (`:251-255`). Accepted formats: 1 or 2 channels,
8 or 16 bit -> `AL_FORMAT_{MONO,STEREO}{8,16}` (`src/Sound.cpp:145-166`);
anything else -> `Error("openAL: Error formatting")`. Sample rate is passed through.

Archive survey (all 148 files, every one has `data` right after `fmt `, all PCM):

| ch | rate | bits | count | examples |
|---|---|---|---|---|
| 1 | 11025 | 8 | 83 | pistol.wav |
| 1 | 22050 | 16 | 32 | |
| 2 | 44100 | 16 | 14 | all 5 Music_*, chime, loot, glass, vending_sale |
| 1 | 8000 | 8 | 13 | SpiderMastermind_sight (fmtLen 18) |
| 1 | 22050 | 8 | 4 | supershotgun_open/close/load, use_item |
| 1 | 8000 | 16 | 1 | impClaw |
| 2 | 48000 | 16 | 1 | vent |

### resID -> file table (`src/Sounds.h:8-152`)

| resID | file | resID | file | resID | file |
|---|---|---|---|---|---|
| 1000 | Arachnotron_active.wav | 1049 | Imp_active.wav | 1098 | Revenant_punch.wav |
| 1001 | Arachnotron_death.wav | 1050 | Imp_death1.wav | 1099 | Revenant_sight.wav |
| 1002 | Arachnotron_sight.wav | 1051 | Imp_death2.wav | 1100 | Revenant_swing.wav |
| 1003 | Arachnotron_walk.wav | 1052 | Imp_sight1.wav | 1101 | rocketLauncher.wav |
| 1004 | Archvile_active.wav | 1053 | Imp_sight2.wav | 1102 | rocket_explode.wav |
| 1005 | Archvile_attack.wav | 1054 | item_pickup.wav | 1103 | secret.wav |
| 1006 | Archvile_death.wav | 1055 | loot.wav | 1104 | Sentinel_attack.wav |
| 1007 | Archvile_pain.wav | 1056 | LostSoul_attack.wav | 1105 | Sentinel_death.wav |
| 1008 | Archvile_sight.wav | 1057 | Maglev_arrive.wav | 1106 | Sentinel_pain.wav |
| 1009 | BFG.wav | 1058 | Maglev_depart.wav | 1107 | Sentinel_sight.wav |
| 1010 | block_drop.wav | 1059 | malfunction.wav | 1108 | SentryBot_activate.wav |
| 1011 | block_pick.wav | 1060 | Mancubus_attack.wav | 1109 | SentryBot_death.wav |
| 1012 | Cacodemon_death.wav | 1061 | Mancubus_death.wav | 1110 | SentryBot_discard.wav |
| 1013 | Cacodemon_sight.wav | 1062 | Mancubus_pain.wav | 1111 | SentryBot_pain.wav |
| 1014 | chaingun.wav | 1063 | Mancubus_sight.wav | 1112 | SentryBot_return.wav |
| 1015 | chainsaw.wav | 1064 | menu_open.wav | 1113 | slider.wav |
| 1016 | ChainsawGoblin_attack.wav | 1065 | menu_scroll.wav | 1114 | Soulcube.wav |
| 1017 | ChainsawGoblin_death.wav | 1066 | monster_pain.wav | 1115 | SpiderMastermind_death.wav |
| 1018 | ChainsawGoblin_pain.wav | 1067 | Music_Boss.wav | 1116 | SpiderMastermind_sight.wav |
| 1019 | ChainsawGoblin_sight.wav | 1068 | Music_General.wav | 1117 | supershotgun.wav |
| 1020 | chime.wav | 1069 | Music_Level_End.wav | 1118 | supershotgun_close.wav |
| 1021 | claw.wav | 1070 | Music_LevelUp.wav | 1119 | supershotgun_load.wav |
| 1022 | Cyberdemon_death.wav | 1071 | Music_Title.wav | 1120 | supershotgun_open.wav |
| 1023 | Cyberdemon_sight.wav | 1072 | no_use.wav | 1121 | switch.wav |
| 1024 | demon_active.wav | 1073 | no_use2.wav | 1122 | switch_exit.wav |
| 1025 | demon_pain.wav | 1074 | Pinkinator_attack.wav | 1123 | teleport.wav |
| 1026 | demon_small_active.wav | 1075 | Pinkinator_death.wav | 1124 | use_item.wav |
| 1027 | Dialog_Help.wav | 1076 | Pinkinator_pain.wav | 1125 | vending_sale.wav |
| 1028 | door_close.wav | 1077 | Pinkinator_sight.wav | 1126 | vent.wav |
| 1029 | door_close_slow.wav | 1078 | Pinkinator_spawn.wav | 1127 | VIOS_attack1.wav |
| 1030 | door_open.wav | 1079 | Pinky_attack.wav | 1128 | VIOS_attack2.wav |
| 1031 | door_open_slow.wav | 1080 | Pinky_death.wav | 1129 | VIOS_death.wav |
| 1032 | explosion.wav | 1081 | Pinky_sight.wav | 1130 | VIOS_pain.wav |
| 1033 | fireball.wav | 1082 | Pinky_small_attack.wav | 1131 | VIOS_sight.wav |
| 1034 | fireball_impact.wav | 1083 | Pinky_small_death.wav | 1132 | weapon_pickup.wav |
| 1035 | footstep1.wav | 1084 | Pinky_small_pain.wav | 1133 | Weapon_Sniper_Scope.wav |
| 1036 | footstep2.wav | 1085 | Pinky_small_sight.wav | 1134 | Weapon_Toilet_Pull.wav |
| 1037 | gib.wav | 1086 | pistol.wav | 1135 | Weapon_Toilet_Smash.wav |
| 1038 | glass.wav | 1087 | plasma.wav | 1136 | Weapon_Toilet_Throw.wav |
| 1039 | hack_clear.wav | 1088 | platform_start.wav | 1137 | Weapon_Toilet_Throw2.wav |
| 1040 | hack_fail.wav | 1089 | platform_stop.wav | 1138 | zombie_active.wav |
| 1041 | hack_failed.wav | 1090 | player_death1.wav | 1139 | zombie_death1.wav |
| 1042 | hack_submit.wav | 1091 | player_death2.wav | 1140 | zombie_death2.wav |
| 1043 | hack_success.wav | 1092 | player_death3.wav | 1141 | zombie_death3.wav |
| 1044 | hack_successful.wav | 1093 | player_pain1.wav | 1142 | zombie_sight1.wav |
| 1045 | HolyWaterPistol.wav | 1094 | player_pain2.wav | 1143 | zombie_sight2.wav |
| 1046 | HolyWaterPistol_refill.wav | 1095 | Revenant_active.wav | 1144 | zombie_sight3.wav |
| 1047 | hoof.wav | 1096 | Revenant_attack.wav |  |  |
| 1048 | impClaw.wav | 1097 | Revenant_death.wav |  |  |

## 2. Channels and `playSound(resID, flags, priority, a5)`

- 10 channels `SoundStream channel[10]` (`src/Sound.h:62`), each owns one AL
  buffer + one AL source, created once in `openAL_LoadAllSounds`
  (`src/Sound.cpp:280-295`; source pos 0,0,0, `AL_REFERENCE_DISTANCE 5000000`
  = no attenuation). `startup()` sets every channel `resID=-1, priority=1`
  (`src/Sound.cpp:42-45`).
- A channel keeps its `resID` (and its loaded buffer) after the sound ends or is
  stopped — nothing ever resets `channel.resID` to -1 except `startup()`. So a
  channel is a 1-entry cache; WAVs are decoded lazily when a channel is (re)assigned
  (`src/Sound.cpp:414-420`), never preloaded.
- `a5` is never read (`src/Sound.cpp:306-442`). `playCombatSound(r,f,p)` =
  `playSound(r,f,p,false)` (`src/Sound.cpp:535-537`).

### Flags
- bit0 (`1`) = loop: `openAL_PlaySound(src, flags & 1)` -> `AL_LOOPING`
  (`src/Sound.cpp:439,169-175`).
- bit1 (`2`) = stop ALL channels before playing (`alSourceStop`, priority=1,
  fadeInProgress=0; `resID` kept) (`src/Sound.cpp:396-403`).
- No other bits are read. Script `EV_PLAYSOUND` packs `flags = args>>4`,
  `priority = args&0xF` (`src/ScriptThread.cpp:1086`).

### Gates (in order, `src/Sound.cpp:330-351`)
1. resID -1/255/1255 -> return. `field_0x10` (=100 from `startup`, `:36`) == 0 -> return;
   `field_0x4` true -> return (never set anywhere, memset 0).
2. Music (1067..1071) requires `allowMusics`; everything else requires `allowSounds`.
3. `if (this->resID == resID) return;` — `this->resID` is a "deferred sound" field
   that is never written outside `startFrame` (see section 4); stays 0 from memset,
   so effectively dead.

### Slot selection (`src/Sound.cpp:352-437`)
Single loop over slots 0..9:
- If slot holds this resID: look at the FIRST slot holding it; if it is not
  playing -> reuse that slot (`v10`), stop scanning. If it is playing and
  `priority == 6` -> return (do not restart). Otherwise keep scanning.
- `FreeSlot` = first slot with `resID == -1` seen during the scan.
After the scan:
- `priority == 6 || loop` and the reuse slot is playing -> return (`:390-394`;
  unreachable in practice since the reuse slot is by construction not playing).
- Reuse path: set volume (music vs sfx), `alSourcePlay` again with the cached
  buffer; slot `priority` is NOT updated (`:428-437`).
- New-slot path: `FreeSlot`, else `getFreeSlot(priority)`; -1 -> sound dropped.
  Sets `resID`, `priority`, stops source, detaches buffer, reloads WAV into the
  slot buffer, attaches, sets volume (`:406-426`).

Consequence: if the same non-priority-6 sound is already playing, a SECOND copy
starts in another slot (overlapping duplicates are allowed, incl. looping music
re-requested without flag 2). Priority 6 = "play only if not already playing".

### `getFreeSlot(p)` (`src/Sound.cpp:445-472`)
Scan 0..9; return the first slot that is empty (`resID==-1`) or neither playing nor
paused. If all 10 are busy, steal the busy slot with the lowest `priority` that is
strictly `< p` and `< 6` (first one wins on ties); none -> -1. Higher number =
more important; priority-6 slots are never stolen; stopped slots revert to priority 1.

Priorities used: 1 (Render/Entity ambient monster sounds `src/Render.cpp:3775`,
`src/Entity.cpp:736`), 3 (most game sounds, music), 4 (combat:
`src/Combat.cpp:307,522,1297`), 5 (UI: `soundClick` = 1027
`src/MenuSystem.cpp:5874-5876`, buttons `src/Button.cpp:153`), 6 (intro
`playSound(1068, 1, 6)` `src/IntroSequenceManager.cpp:780`).
Music is started with flags 1, priority 3, e.g. 1071 title
(`src/Canvas.cpp:1404`, `src/MenuSystem.cpp:3089`), 1069 level end
(`src/Canvas.cpp:889`), 1068 general (`src/MenuSystem.cpp:3058`).
Callers usually stop the previous track explicitly via `soundStop()` first
(e.g. `src/IntroSequenceManager.cpp:143-145`).

## 3. Stop, query, fades, volume

- `soundStop()`: every channel `alSourceStop`, priority=1, fadeInProgress=false;
  `resID` kept (`src/Sound.cpp:474-480`). Called on map load
  (`src/LoadingManager.cpp:70`), cinematic skip (`src/Game.cpp:2512`), player
  death (`src/Player.cpp:350`), credits/help (`src/InputEventController.cpp:391,410`).
- `stopSound(resID, fadeOut)`: acts on the FIRST slot with that resID only.
  `fadeOut=false` -> stop, priority=1, fade off. `fadeOut=true` and no fade
  running -> `StartFade(curVol, 0, 500)` (`src/Sound.cpp:482-504`). Only caller is
  script `EV_STOPSOUND` with `fadeOut=false` (`src/ScriptThread.cpp:1958`).
- `isSoundPlaying(resID)`: AL_PLAYING state of the FIRST slot with that resID
  (`src/Sound.cpp:506-513`).
- Fades: `StartFade` only stores `volume`, `fadeBeg`, `fadetime=app->gameTime`,
  `fadeVolume=(fadeBeg-volume)/fadeEnd` (per-ms slope) and sets
  `fadeInProgress` (`src/Sound.cpp:581-587`). NOTHING advances or ends a fade
  (`endFrame` is empty, `src/Sound.cpp:577-579`; no other reader of these fields).
  A faded sound would keep playing and be skipped by `updateVolume`. Dead path.
- Volume model: `soundFxVolume`, `musicVolume` in 0..100, default 80/80
  (`src/Sound.cpp:34-35`). Applied per source as
  `alSourcef(src, AL_MAX_GAIN, vol/100.0)` clamped at 100 (`src/Sound.cpp:108-113`)
  — music slots use `musicVolume`, all others `soundFxVolume`.
  `updateVolume()` (only if `soundsLoaded`) re-applies to non-fading slots, then
  `allowMusics = musicVolume != 0`, `allowSounds = soundFxVolume != 0`
  (`src/Sound.cpp:515-533`). So volume 0 == off; there is no independent toggle.
- Steps: `volumeUp/Down`, `musicVolumeUp/Down` add/subtract and clamp 0..100,
  then `updateVolume` (`src/Sound.cpp:547-561,589-603`); keyboard menu uses
  step 10 (`src/InputEventController.cpp:189-209`). Touch slider:
  `value = 100*(x - origin)/trackWidth` clamped 0..100, then `updateVolume`
  (`src/MenuSystem.cpp:5823-5865`). Menu shows 0 when the allow flag is false
  (`src/MenuSystem.cpp:5312,5322`).
- System volume: `openAL_SetSystemVolume` (listener `AL_GAIN`) exists but its only
  call is commented out (`src/Sound.cpp:77,101-106`); `field_0x10=100` is used
  only as the gate above.
- Persistence: config file v11 writes `bool allowSounds, u8 soundFxVolume,
  u8 musicVolume` right after `difficulty` (`src/Game.cpp:1920-1924`, same in
  `saveEmptyConfig` `:2327-2329`). `loadConfig` reads them back, copies
  `allowSounds` to `canvas->areSoundsAllowed`, then calls `updateVolume()`
  (`src/Game.cpp:1985-1988,2050`). Boot order: `sound->startup()` then
  `game->loadConfig()` (`src/App.cpp:115-120`); since `soundsLoaded` is still
  false there, the effective `updateVolume` happens in `cacheSounds()` from
  `GameStateRunner::logoState` (`src/GameStateRunner.cpp:328-330`,
  `src/Sound.cpp:298-304`), which overwrites `allowSounds/allowMusics` from the
  volumes (the saved bool is effectively ignored).
- Temporary mute: map loading saves `allowSounds`, sets false, restores at the end
  (`src/LoadingManager.cpp:619-621,721`); same around `skipCinematic`
  (`src/Game.cpp:2510-2534`). During `inInitMap` scripts force `allowSounds=true`
  for their own `EV_PLAYSOUND` if `canvas->areSoundsAllowed`
  (`src/ScriptThread.cpp:1083-1088`).
- `MENU_ENABLE_SOUNDS` Yes/No both run `ACTION_TOGSOUND` which sets
  `allowSounds=true`, plays 1068 looped and goes to the intro movie
  (`src/MenuSystem.cpp:1545,3055-3060`).
- `cacheCombatSound` returns true, `freeMonsterSounds` is empty — stubs
  (`src/Sound.cpp:539-545`), called from `src/Game.cpp:445-446,684`.

## 4. Per-frame logic, rate limiting, suspend

- `CAppContainer::DoLoop`: `sound->startFrame(); canvas->run(); sound->endFrame();`
  (`src/CAppContainer.cpp:55-61`).
- `startFrame` plays a deferred sound (`resID/flags/priority` members) when
  `field_0x162 != 0`, then clears it (`src/Sound.cpp:563-575`). `field_0x162` is
  set to 0 in `startup` and never set anywhere else -> dead. `endFrame` is empty.
- No per-frame dedup or rate limiting exists beyond the slot rules in section 2
  (priority 6 / 10-slot stealing). Callers that want "don't overlap" check
  `isSoundPlaying` themselves (`src/Button.cpp:152`, `src/ComicBook.cpp:734`,
  `src/MenuSystem.cpp:578`).
- Suspend/resume: `openAL_Suspend/Resume` exist (`src/Sound.cpp:115-127`) but are
  never called; `CAppContainer::suspendOpenAL/resumeOpenAL` are empty and also
  uncalled (`src/CAppContainer.cpp:63-69`). No background handling.
- Shutdown: `~Sound` -> `soundStop` + `openAL_Close` (`src/Sound.cpp:15-18,84-99`).

## 5. Gameplay call sites and monster sound selection

Full per-call table (resID, flags, priority, trigger, `new_src` location):
`docs/research/2026-11-04-sound-callsites-gameplay.md`. Key facts:

- Almost all gameplay calls use flags 0, priority 3. Exceptions: combat fire /
  monster attack / projectile impacts prio 4 (`src/Combat.cpp:307,522,1297,1330,1345`),
  item pickup prio 4 (`src/Entity.cpp:277`), monster death prio 5 (`src/Entity.cpp:405`),
  level-up music prio 5 after `soundStop()` (`src/Player.cpp:350-351`), monster idle,
  VIOS teleport and boss footsteps prio 1 (`src/Render.cpp:3775`, `src/Entity.cpp:736`,
  `src/Game.cpp:2931-2934`), HUD button highlight prio 5 guarded by `isSoundPlaying`
  (`src/Button.cpp:150-155`), level-end music looped (flags 1, `src/Canvas.cpp:889`).
- EV_PLAYSOUND operand byte 2 = `flags << 4 | priority` (`src/ScriptThread.cpp:1078-1091`).
- Monster sounds: `Game::getMonsterSound` (`src/Game.cpp:3771-3845`) reads
  `monsterSounds[eSubType*8 + slot (+ nextByte()%3 for ALERT1)]` (+1000 unless 255)
  from byte table 13 of `tables.bin` (`src/App.cpp:386,402`), with hard-coded
  overrides for small pinky (sub 5 parm 0), Spider Mastermind (sub 14 parm 1),
  and `std::rand()` death variants for imp (1050/1051) and zombie (1139..1141).
  Slots: ALERT1..3, ATTACK1, ATTACK2, IDLE, PAIN, DEATH (`src/Enums.h:85-95`).
  Requested at: alert on activate (`src/Game.cpp:805`), attack (`src/Combat.cpp:479-522`),
  idle when visible + timer (`src/Render.cpp:3769-3778`), pain (`src/Entity.cpp:347`),
  death (`src/Entity.cpp:404`).

## 6. UI call sites, music per state, Sound options menu

Full per-call table (front end, MenuSystem, options, char select, credits, comic,
dialog, loading/level end, HUD buttons, minigames, `new_src` mapping):
`docs/research/2026-11-04-sound-callsites-ui.md`. Key facts:

- `MenuSystem::soundClick()` = `playSound(1027 Dialog_Help, 0, 5)` (`src/MenuSystem.cpp:5874-5876`);
  used for list scroll (`:317,327,336,353`), item-info (`:2852`), options value steps
  (`src/InputEventController.cpp:186-271`), char-select moves and minigame cursors.
- Menu `select()` plays 1086 pistol pri 5 (`src/MenuSystem.cpp:3013`); `back()` plays 1122
  switch_exit unless menu MAIN / MORE_GAMES (`:578-580`); touch list scroll 1065 (`:539-541`).
- fmButton highlight-edge sounds: menu rows/arrows/sliders/dialog/minigame/weapon-grid
  buttons 1027, menu back/return 1122 (`src/MenuSystem.cpp:193,219`), HUD bar 1121
  (`src/Hud.cpp:67-85`), scrollbar 1065 (`src/MenuSystem.cpp:233`).
- Music (group 1067..1071): intro movie 1068 loop pri 6 (`src/IntroSequenceManager.cpp:780`);
  title 1071 loop on `backToMain(false)` (`src/Canvas.cpp:1404`), after main-menu minigames
  (`src/HackingGame.cpp:633`, `src/SentryBotGame.cpp:821`, `src/VendingMachine.cpp:861`) and
  in-game credits (`src/MenuSystem.cpp:3087-3090`); level stats 1069 loop
  (`src/Canvas.cpp:889`, `src/IntroSequenceManager.cpp:145`). No C++ picks in-level music:
  it comes from map scripts (map00: `PLAYSOUND id=1068` flags 0, later `STOPSOUND 1068`).
  `loadMap` always `soundStop()`s (`src/LoadingManager.cpp:70`).
- [GEC] Sound options menu (MAIN_OPTIONS_SOUND -3 / INGAME_OPTIONS_SOUND,
  `src/MenuSystem.cpp:2304-2343`): "SoundFx Volume" (ACTION 15, param 1) and "Music Volume"
  (ACTION 37, param 2) sliders. Select toggles edit mode; LEFT/RIGHT = -/+10 via
  `volumeDown/Up` / `musicVolumeDown/Up` + click; touch drag maps x to 0..100 then
  `updateVolume()` (`src/MenuSystem.cpp:5818-5867`). Volume 0 disables that group
  (`src/Sound.cpp:530-531`).

### 6.1 Options / Sound screens — implementation facts

Full row tables, pixel geometry and quirks: `docs/research/2026-11-05-sound-options-menu.md`.

- Labels are English literals pushed as text args and referenced as 3:241.. (`ARGUMENT1..`):
  17/35 = "Options" (title), "Sound", "Video", "Input" (`src/MenuSystem.cpp:1395-1413`);
  -3/55 = "Sound Options", "SoundFx Volume:", "Music Volume:" (`:2329-2337`). No string-table ids.
- 17: pad 14, title (NOSELECT|CENTER), 3 CENTER GOTO rows → -3/-2/-1, logo (`:1374-1384`).
  35: title (NOSELECT|CENTER|DIVIDER), 3 GOTO rows (flags 0) → 55/56/63 (`:1386-1393`).
  -3: pad 14, title, spacer, two `ITEM_SCROLLBARTWO` image sliders, logo (`:2304-2316`).
  55: title+divider, spacer, `ITEM_SCROLLBAR` SFX, spacer, `ITEM_SCROLLBAR` Music (`:2317-2327`).
  `type`/`background` are inherited from the previous menu (not reset, `:1226-1237`).
- Main slider (-3): `Menu_Option_SliderBar.bmp` 238x65 at (99, rowTop-4), knob 24x22
  `SliderOFF` at `99 + 221*v/100` (or `SliderON` at the finger while dragging), value as 3
  `Hud_Numbers.bmp` digits at x 344 (354 with "<" ">" in edit mode) (`:5409-5455`, `:5794-5809`).
  In-game slider (55): flat rects, track/touch area `(92, rowTop+17, 296, 21)`, 24-px knob at
  `92 + 272*v/100`, value text "N" / "< N >" right-aligned at x 384 (`:5361-5408`).
- Edit mode = `changeSfxVolume` / `changeMusicVolume` + `changeValues`, toggled by `select`
  (plays 1086) (`:3120-3125`, `:3276-3281`); while set, UP/DOWN/FIRE/Back only toggle it off
  (`:2960-2967`). Not editing, LEFT/RIGHT page-scroll; no volume change. Touch drag works in
  any mode, releases silently (`:4666-4673`).
- Persistence: `saveConfig()` only on `back()` out of 17 or 35 (`:583-585`, `:4041-4049`).
  Back from -3/55 pops to 17/35 without saving; "Resume" (button 15) from 35/55 never saves.
  Edit flags are never cleared by `back()`/`returnToGame()` (quirk).
- Menu 62 stays reachable via 35 → Input 63 → "Touch Controls" (`:2446-2448`, `:2474-2493`);
  the original 35 body ("Controls"/"Back") is `#if 0` (`:1618-1650`).
