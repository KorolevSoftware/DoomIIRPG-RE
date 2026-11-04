# Gameplay sound call sites (original `src/`) and their `new_src/` equivalents

Date: 2026-11-04. Scope: gameplay code only (combat, player, entities, monsters,
doors, loot, items, scripts, map load, HUD bar, familiar). Menus, front-end,
mini-games, dialogs are out of scope (separate inventory). Engine semantics of
`playSound` (slots, flags, priorities) are in `docs/original-code/sound.md`.

## Hypothesis / question

List every `playSound` / `playCombatSound` / `stopSound` / `soundStop` /
`isSoundPlaying` call in gameplay code with resID, flags, priority, trigger, and
the matching `new_src/` location; explain how monster sounds are chosen.

## Method

`grep -n "sound->\|Sound\."` over every gameplay `.cpp` in `src/`, then read each
call site. Files with ZERO sound calls: `CombatEntity.cpp`, `EntityMonster.cpp`,
`EntityDef.cpp`, `ParticleSystem.cpp`, `Hud.cpp` (direct calls; buttons use
`fmButton`, see below), `MovementController.cpp`, `AutomapController.cpp`,
`TravelMapManager.cpp`, `LerpSprite.cpp`. Monster table dumped from
`UnPackGameData/tables.bin` (table 13) with a throw-away reader that follows
`Resource::beginTableLoading/seekTable/loadByteTable` (`src/Resource.cpp:225-293`:
80-byte header of 20 LE int offsets, then per table an int count + bytes).

## Verdict

CONFIRMED (complete for the listed files; every direct call is in the tables below).

Signature: `playSound(int16_t resID, uint8_t flags, int priority, bool a5)`
(`src/Sound.h:91`); `playCombatSound(resID, flags, prio)` is a plain forward with
`a5=false` (`src/Sound.cpp:535-537`). `flags` bit0 = loop, bit1 = stop all slots
first (`src/Sound.cpp:389-403,439`). Priority 6 = do-not-restart-if-playing
(`src/Sound.cpp:367-372,389`). resID 255 / -1 / 1255 are no-ops (`src/Sound.cpp:330-339`).

## Call-site table

Prio = priority argument. "Fl" = flags. `new_src` column: the file:line of the
existing `[sound]` placeholder, or the ported function without a placeholder, or
"not ported".

### Combat (player attacks, monster attacks, projectiles)

| src | resID (name) | Fl | Prio | Trigger | new_src |
|---|---|---|---|---|---|
| Combat.cpp:307 | per weapon (switch :260-305): wpn 0,8 -> 1014 chaingun; 1 -> 1015 chainsaw; 2 -> 1045 HolyWaterPistol; 3,5,9 -> 1086 pistol; 7 -> 1117 supershotgun; 10 -> 1087 plasma; 11 -> 1101 rocketLauncher; 12 -> 1009 BFG; 13 -> 1114 Soulcube; 14 (thrown item) -> characterChoice==1 ? 1137 Weapon_Toilet_Throw2 : 1136 Weapon_Toilet_Throw; 4,6 -> none | 0 | 4 | `playerSeq` stage 0, player fires (`playCombatSound`) | `new_src/domain/game/Combat.cpp:676-691` (`[combat] fire sound`), matches table |
| Combat.cpp:522 | `attackerWeaponId==17` (WP_M_PUNCH) and miss -> 1100 Revenant_swing; `==28` (WP_M_PLASMA_GUN) -> 1087 plasma; else `getMonsterSound(sub, parm, ATTACK2 if weapon == monster field 1 else ATTACK1)` (:479-485) | 0 | 4 | `monsterSeq` stage 0, each monster attack loop | `new_src/domain/game/Combat.cpp:276-311` (`[sound] %d`) |
| Combat.cpp:944 | 1037 gib | 0 | 3 | `explodeOnMonster`, targetType 9 (ET_CORPSE) gibbed | `new_src/domain/game/Combat.cpp:836-837` branch deferred, no placeholder |
| Combat.cpp:1297 | 1032 explosion | 0 | 4 | `updateProjectile` impact, proj 4 (rocket); with shake 500/4/500 | not ported (only proj 7 branch exists) |
| Combat.cpp:1330 | 1034 fireball_impact | 0 | 4 | `updateProjectile` impact, proj 7 (fire) | `new_src/domain/game/Combat.cpp:992` |
| Combat.cpp:1345 | 1135 Weapon_Toilet_Smash | 0 | 4 | `updateProjectile` impact, proj 13 (thrown item) | not ported |
| Combat.cpp:473/476 | (J2ME `Sound.playSound(15/5)`) | - | - | inside `#if 0`, dead | n/a |

### Entities, monsters, items

| src | resID (name) | Fl | Prio | Trigger | new_src |
|---|---|---|---|---|---|
| Entity.cpp:277 | 1054 item_pickup | 0 | 4 | `touchedItem` end, item picked up (after removeEntity) | `new_src/domain/game/ItemPickup.cpp:65` (TODO comment, no print) and `:74` (`touchedDrop`) |
| Entity.cpp:348 | `getMonsterSound(sub, parm, MSOUND_PAIN)` | 0 | 3 | `Entity::pain`, monster survives (hp after hit > 0) | `new_src/domain/game/MonsterSystem.cpp:370-372` (`[monster] pain sound`) |
| Entity.cpp:376 | 1038 glass | 0 | 3 | `Entity::pain`, ET_ATTACK_INTERACTIVE subtype 1 (breakable glass) | `new_src/domain/game/Game.cpp:666-671` comment only (`painProp`) |
| Entity.cpp:405 | `getMonsterSound(sub, parm, MSOUND_DEATH)` | 0 | 5 | `checkMonsterDeath(b, b2)` with b2: from `Entity::died` (Entity.cpp:496, always true) and script kill (ScriptThread.cpp:2265, arg b) | not ported; `new_src/domain/game/MonsterSystem.cpp:386` `diedMonster` has no sound, `:478` "checkMonsterDeath sound omitted" |
| Entity.cpp:736 | 1123 teleport | 0 | 1 | `aiWeaponForTarget`, eSubType 15 (BOSS_VIOS) teleports to a free random tile | not ported (no boss AI) |
| Render.cpp:3775 | `getMonsterSound(sub, parm, MSOUND_IDLE)` | 0 | 1 | `handleMonsterIdleSounds` from `addSprite` (Render.cpp:860) for every visible eType 2 monster sprite; fires when per-subtype timer `monsterIdleTime[sub]` elapsed AND dist < `tileDistances[3]`; timer re-armed to `time + 6000 + (nextByte%10)*1000` even if out of range (:3777); initial arm at map load `time + 12000 + (nextByte%10)*1000` (LoadingManager.cpp:736) | not ported |
| Game.cpp:806 | `getMonsterSound(sub, parm, MSOUND_ALERT1)` (random of 3 alerts) | 0 | 3 | `Game::activate(..., b3=true)` while `canvas->state == ST_PLAYING` | `new_src/domain/game/MonsterSystem.cpp:111-114` (`[monster] alert sound`) |
| Game.cpp:445-446 | `cacheCombatSound(ATTACK1/2)` | - | - | map entity setup; `cacheCombatSound` is an unimplemented stub returning true (Sound.cpp:538-541) | n/a |
| Game.cpp:2931 | 1047 hoof | 0 | 1 (`true`) | `updateLerpSprite`, BOSS_CYBERDEMON walk lerp, when the walk frame pair changes | not ported (`new_src/domain/game/SpriteLerps.cpp:259` "omitted") |
| Game.cpp:2934 | 1003 Arachnotron_walk | 0 | 1 | same, BOSS_MASTERMIND (any parm) | same |
| Game.cpp:2937 | 255 (no-op) | 0 | 1 | unreachable else | n/a |

### Doors, secrets, map/script

| src | resID (name) | Fl | Prio | Trigger | new_src |
|---|---|---|---|---|---|
| Game.cpp:1137 | 1028 door_close | 0 | 3 | `performDoorEvent` n==1 (close) | not ported (`new_src/domain/game/DoorSystem.cpp:52` no sound; `ScriptVM.cpp:571` comment) |
| Game.cpp:1144 | 1030 door_open | 0 | 3 | `performDoorEvent` n==0 and !b (normal open; secret-wall b=true path silent here) | not ported (same) |
| Game.cpp:3480 | 1103 secret | 0 | 3 | `awardSecret(b=true)` (push wall, PlayingInputHandler.cpp:463) | `new_src/domain/game/Game.cpp:764` |
| Game.cpp:3483 | 1020 chime | 0 | 3 | `awardSecret(b=false)` (script, ScriptThread.cpp:1258) | `new_src/domain/game/Game.cpp:764` |
| Game.cpp:2512 | `soundStop()` | - | - | `skipCinematic` (allowSounds forced false during fast-forward, restored :2534) | `new_src/core/CinematicCamera.cpp:196` no placeholder |
| ScriptThread.cpp:768,774 | 1065 menu_scroll | 0 | 3 | EV_DOOROP lock (n44==2) / unlock (else) when interactive (`n43`: flag bit 4 clear and not ST_AUTOMAP) | `new_src/domain/game/ScriptVM.cpp:598,601` |
| ScriptThread.cpp:1086/1090 | operand `resID + 1000` | `args >> 4` | `args & 0xF` | EV_PLAYSOUND (2 ubyte operands); during `inInitMap && areSoundsAllowed` it forces `allowSounds = true` around the call | `new_src/domain/game/ScriptVM.cpp:978-983` (`[script] PLAYSOUND`, flags/prio dropped) |
| ScriptThread.cpp:1958 | `stopSound(resID + 1000, false)` | - | - | EV_STOPSOUND (1 ubyte) | `new_src/domain/game/ScriptVM.cpp:985-988` |
| LoadingManager.cpp:70 | `soundStop()` | - | - | map load (`loadMap` path) | `new_src/core/LoadingManager.cpp:139` |
| Canvas.cpp:889 | 1069 Music_Level_End | 1 (loop) | 3 | after save, `saveType & 0x10` -> MENU_LEVEL_STATS (a5 = `saveType & 8`, unused) | `new_src/core/LoadingManager.cpp:202` |
| Canvas.cpp:990 | `(nextByte & 0xF) + 1000` | 0 | 3 | `sysSoundDelayTime > 0` — field is never assigned anywhere in `src/` (only read here), dead | n/a |

### Player

| src | resID (name) | Fl | Prio | Trigger | new_src |
|---|---|---|---|---|---|
| Player.cpp:350-351 | `soundStop()` then 1070 Music_LevelUp | 0 | 5 | `addLevel` when `canvas->state != ST_MENU` | not ported (`new_src/domain/game/Player.cpp:804-806` says sound/dialog deferred) |
| Player.cpp:632 | 1111 SentryBot_pain | 0 | 3 | `painEvent` when `isFamiliar` | `new_src/domain/game/Combat.cpp:425` |
| Player.cpp:636 | 1094 player_pain2 | 0 | 3 | `painEvent`, characterChoice == 1 | `new_src/domain/game/Combat.cpp:425` — BUG: always logs 1093 (comment :422 admits voice split absent) |
| Player.cpp:639 | 1093 player_pain1 | 0 | 3 | `painEvent`, other characters | same |
| Player.cpp:721/724/726 | 1092 / 1091 / 1090 player_death3/2/1 | 0 | 3 | `Player::died`, characterChoice 1 / 2 / 3 | `new_src/core/GameContext.cpp:285` |
| Player.cpp:746 | 1109 SentryBot_death | 0 | 3 | `familiarDying(false)` (not self-destruct); shake 350/5/500 | `new_src/core/Familiar.cpp:171` |
| Player.cpp:952 | 1124 use_item | 0 | 3 | `useItem` success (every consuming case; case 23 returns before) | `new_src/domain/game/Player.cpp:585` |
| Player.cpp:1572 | 1034 fireball_impact | 0 | 3 | `addStatusEffect(13 = STATUS_EFFECT_FIRE)` unless `buffs[9]` (anti-fire) and not familiar | `new_src/domain/game/Player.cpp:715` |
| Player.cpp:1988 | 1020 chime | 0 | 3 | `showAchievementMessage` (secret-complete n=1 Game.cpp:3487; kill-type n=2 Entity.cpp:417) | `new_src/domain/game/Game.cpp:775` (n=1 path only) |
| Player.cpp:2125 | 1108 SentryBot_activate | 0 | 3 | `setFamiliar` end | `new_src/core/Familiar.cpp:104` |
| Player.cpp:2280 | 1112 SentryBot_return | 0 | 3 | `familiarReturnsToPlayer` start | `new_src/core/Familiar.cpp:143` |
| GameStateRunner.cpp:293 | 1032 explosion | 0 | 3 | `familiarDyingState`, self-destruct, 1500 ms after `familiarDeathTime` | `new_src/core/Familiar.cpp:192` |

### Player input / interaction (PlayingInputHandler, loot, armor repair)

| src | resID (name) | Fl | Prio | Trigger | new_src |
|---|---|---|---|---|---|
| PlayingInputHandler.cpp:411 | 1046 HolyWaterPistol_refill | 0 | 3 | use on toilet (eType 10 sub 3, 1 tile, ammo[8]==0), holding weapon 2 with ammo[3] < 100 -> refill | not ported |
| PlayingInputHandler.cpp:427 | 1134 Weapon_Toilet_Pull | 0 | 3 | same toilet, other weapon, STR >= 11 -> pick up toilet as weapon 14 | not ported |
| PlayingInputHandler.cpp:437 | 1046 HolyWaterPistol_refill | 0 | 3 | use on sink (eType 14 sub 7) with weapon 2 and ammo[3] < 100 | not ported |
| PlayingInputHandler.cpp:456 | 1111 SentryBot_pain | 0 | 3 | familiar tries to open a door (eType 5) | `new_src/core/PlayerActions.cpp:301` |
| PlayingInputHandler.cpp:473 | 1111 | 0 | 3 | "no use" on wall/sprite-wall (eType 0/12) as familiar, not zoomed | `new_src/core/PlayerActions.cpp:353` |
| PlayingInputHandler.cpp:476 | 1073 no_use2 | 0 | 3 | same, characterChoice == 1 | `new_src/core/PlayerActions.cpp:355` |
| PlayingInputHandler.cpp:479 | 1072 no_use | 0 | 3 | same, characterChoice 2..3 | `new_src/core/PlayerActions.cpp:355` |
| LootingSystem.cpp:64 | 1055 loot | 0 | 3 | loot crouch reaches dwell, once per session (`field_0xac5_` latch) | `new_src/core/LootSession.cpp:85` (`[loot] sound 1055`) |
| ArmorRepairSystem.cpp:34/37 | 1073 / 1072 | 0 | 3 | `startArmorRepair` with < 5 armor shards; characterChoice 1 / 2..3 | `new_src/domain/game/ScriptVM.cpp:1706` |

### HUD bar buttons (fmButton highlight)

`fmButton::SetHighlighted` plays `soundResID` with flags 0, prio 5 on the
rising edge of highlight if `!isSoundPlaying(soundResID)` (`src/Button.cpp:150-155`;
scroll-button twin at `src/Button.cpp:521-525`).

| src | resID (name) | Trigger | new_src |
|---|---|---|---|
| Hud.cpp:67-85 | 1121 switch | HUD bottom-bar buttons 0..6 (switch L/R, weapon, face, shield, health, key) get highlighted by touch | `new_src/core/HudButtons.cpp` — no placeholder |
| Hud.cpp:90 | 1027 Dialog_Help | weapon-grid buttons | same, no placeholder |
| Canvas.cpp:241 | 1113 slider | sniper scope dial scroll button | `new_src/core/ZoomController.cpp:120,129` |

Out of scope but seen: `Canvas.cpp:1404` (1071 Music_Title, backToMain),
`Canvas.cpp:327-357` dialog buttons 1027, `DialogSystem.cpp:575-716`,
`IntroSequenceManager.cpp`, `InputEventController.cpp:391,410`.

## Monster sound selection — `Game::getMonsterSound(eSubType, parm, soundType)` (`src/Game.cpp:3771-3845`)

Slots (`src/Enums.h:85-95`): ALERT1=0, ALERT2=1, ALERT3=2, ATTACK1=3, ATTACK2=4,
IDLE=5, PAIN=6, DEATH=7; `MSOUND_TYPES=8`; `MSOUND_NONE=255`.

Rule, in order:
1. `rand = (soundType == ALERT1) ? nextByte() % 3 : 0` (:3776-3778). Only ALERT1
   is ever requested (Game.cpp:805), so this picks one of the three alert slots.
2. eSubType 5 (PINKY) with parm 0 -> hard-coded small-pinky set: alerts 1085,
   attacks 1082, idle 1026, pain 1084, death 1083 (:3780-3800).
3. eSubType 14 (BOSS_MASTERMIND) with parm 1 -> alerts 1116, idle 1024, pain 1025,
   death 1115; ATTACK1/2 fall through to the table (:3804-3824).
4. Otherwise `v = monsterSounds[eSubType*8 + soundType + rand]`; result
   `v != 255 ? v + 1000 : 255` (:3826-3831). 255 is then ignored by `playSound`.
5. Overrides AFTER the lookup: IMP DEATH -> `1050 + std::rand()%2`; ZOMBIE DEATH
   -> `1139 + std::rand()%3` (:3836-3842). Note `std::rand`, not `app->nextByte`.

Table source: `game->monsterSounds`, byte table index 13 of `tables.bin`
(`src/App.cpp:371,386,402`), 136 bytes = 17 rows x 8.

Dump (`UnPackGameData/tables.bin`, resID; `-` = 255 none):

| sub | monster | ALERT1 | ALERT2 | ALERT3 | ATTACK1 | ATTACK2 | IDLE | PAIN | DEATH |
|---|---|---|---|---|---|---|---|---|---|
| 0 | ZOMBIE | 1142 zombie_sight1 | 1143 zombie_sight2 | 1144 zombie_sight3 | 1100 Revenant_swing | - | 1138 zombie_active | 1066 monster_pain | 1139 (overridden) |
| 1 | (unused) | - | - | - | - | - | - | - | - |
| 2 | LOST_SOUL | 1056 | 1056 | 1056 | 1021 claw | 1056 LostSoul_attack | 1024 demon_active | 1025 demon_pain | 1034 fireball_impact |
| 3 | IMP | 1052 Imp_sight1 | 1053 Imp_sight2 | 1053 | 1048 impClaw | 1033 fireball | 1049 Imp_active | 1066 | 1050 (overridden) |
| 4 | SAW_GOBLIN | 1019 | 1019 | 1019 | 1016 ChainsawGoblin_attack | - | 1049 Imp_active | 1018 | 1017 |
| 5 | PINKY (parm != 0) | 1081 | 1081 | 1081 | 1079 Pinky_attack | 1079 | 1024 | 1025 | 1080 |
| 6 | CACODEMON | 1013 | 1013 | 1013 | 1021 claw | 1033 fireball | 1024 | 1025 | 1012 |
| 7 | SENTINEL | 1107 | 1107 | 1107 | 1104 | - | 1095 Revenant_active | 1106 | 1105 |
| 8 | MANCUBUS | 1063 | 1063 | 1063 | 1060 | - | 1138 zombie_active | 1062 | 1061 |
| 9 | REVENANT | 1099 | 1099 | 1099 | 1096 Revenant_attack | 1098 Revenant_punch | 1095 | 1066 | 1097 |
| 10 | ARCH_VILE | 1008 | 1008 | 1008 | 1005 | 1037 gib | 1004 | 1007 | 1006 |
| 11 | SENTRY_BOT | 1108 | 1108 | 1108 | 1014 chaingun | - | 1112 | 1111 | 1109 |
| 12 | CYBERDEMON | 1023 | 1023 | 1023 | 1101 rocketLauncher | - | 1024 | 1025 | 1022 |
| 13 | BOSS_PINKY | 1077 | 1077 | 1077 | 1074 | - | 1024 | 1076 | 1075 |
| 14 | MASTERMIND (parm 0 = arachnotron) | 1002 | 1002 | 1002 | 1014 chaingun | - | 1000 | 1025 | 1001 |
| 15 | VIOS | 1131 | 1131 | 1131 | 1127 VIOS_attack1 | 1128 VIOS_attack2 | - | 1130 | 1129 |
| 16 | (copy of VIOS) | 1131 | 1131 | 1131 | 1127 | 1128 | - | 1130 | 1129 |

ATTACK1 vs ATTACK2: ATTACK2 when `attackerWeaponId == getMonsterField(def, 1)`
(the monster's second weapon), else ATTACK1 (`src/Combat.cpp:479-485`), with the
1100/1087 weapon overrides of Combat.cpp:513-520. "Walk" sounds exist only for the
two bosses (hoof / Arachnotron_walk, Game.cpp:2931-2934); there is no per-monster
walk slot.

`new_src` port: `Combat::monsterSound` (`new_src/domain/game/Combat.cpp:462-471`)
implements step 4 only (ATTACK slots); the pinky-parm-0 and mastermind-parm-1
overrides (steps 2-3) are missing, so a small pinky's attack resolves to 1079
instead of 1082 — note for when audio lands.

## Open questions

- Table dump came from `UnPackGameData/tables.bin`, assumed identical to the copy
  in `Doom 2 RPG.ipa`; not byte-compared.
- Row 1 (eSubType 1) is all 255; no MONSTER_ constant for 1 in `src/Enums.h:60-81`.
