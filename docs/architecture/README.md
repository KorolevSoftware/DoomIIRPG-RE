# new_src architecture

_Audit date: 2026-08-22. All citations are `new_src/<file>:<line>`. 58 source files,
~5,260 LOC across `core/ domain/ graphics/ io/ platform/ render/ text/ ui/`.
Namespace for everything: `newcore`._

## Module map

### core/
| File | Responsibility |
|---|---|
| `Main.cpp` | Entry point (`main()` :36): data-archive init + loader smoke logs, constructs subsystems and `GameContext`, then `AppContext::run()`. (Ad-hoc inline loop removed by phase 5 — spec `specs/2026-08-23-phase5-skeleton.md`.) |
| `AppContext.h/.cpp` | Composition root: Window, RenderBackend, InputSystem, ZipArchive (:41-44); `readResource()` applies archive prefix `Payload/Doom2rpg.app/Packages/` (AppContext.h:22, AppContext.cpp:57-59); `run()` delegates to `GameLoop::run`. |
| `GameLoop.h/.cpp` | Fixed-step driver: accumulates clamped dt (≤125 ms), consumes 15 ms quanta calling `GameContext::tick()`, renders once per frame (revived in phase 5, ADR 0003). |
| `GameContext.h/.cpp` | **(planned, ADR 0043)** real `backToMain_()` (teardown, loadMapID 0, `Player::reset`, `MENU_MAIN`), world pass skipped while `loadMapID == 0`, front-end state dispatch/render, `screenTouchId_` latch. Legacy-*Canvas* analog (phase 5, ADR 0003): state machine (`StateId{Playing=3,Loading=7,Dying=13}`, setState semantics with stateVars[9] reset + old/new hooks), clocks (`upTimeMs`, `gameTime`, `blockInputTime`), pending input actions, per-state ticks (two-phase Loading ordered per game-flow §3.3; Playing tick per GameStateRunner order incl. finishMovement FACE+ENTER+advanceTurn), render orchestration. Loot dwell (ADR 0006, spec `specs/2026-08-25-loot-dwell-ui.md`): ST_LOOTING crouch/dwell/stand phases on one clock, `handleLootingAction` (FIRE pages/closes, PASSTURN/BACK close, arrows scroll; guard drops input outside the dwell window) and `drawLootingMenu` overlay (red body + black title bar str227, 3×16 px line slots, shared scrollbar); grant runs at close before stand-up, `advanceTurn` at stand-up expiry. |
| `GameStates.h` | **(planned, ADR 0045)** touch-only `Action::QuestLog`, `Action::HudNextWeapon`. **(planned, ADR 0044)** `StateId::Credits = 15`. **(planned, ADR 0043)** `StateId::CharacterSelect = 22`, `Intro = 9`, `Logo = 26` (`IntroMovie = 2` research-gated), `Action::ScreenTouch`. **(planned, ADR 0041)** `StateId::BotDying = 25`, `Action::BotDiscard` (key B; the debug coordinate overlay moves to F3), `Action::DebugGiveBot` (F5). **(planned, ADR 0010 / spec `specs/2026-08-26-decomposition.md` P1-G1)** `StateId` + `Action` enums moved out of `GameContext.h`, plus the `StateHost` interface (`state()` / `requestState()`) — the only thing a module may know about the state machine. |
| `CinematicCamera.h/.cpp` | **(planned, P1-G2)** owns `MayaCamera` + the cinematic clock: `startCinematic`, `advanceCameraKey` parking, key boundaries/Snap, skip, `renderPose()` (nullptr = no cinematic owns the view — the single source of truth for fov / cockpit overlay / view-weapon suppression). |
| `LootSession.h/.cpp` | **(planned, P1-G3)** ST_LOOTING slice: crouch/dwell/stand pose clock, loot-pool session state, `handleAction`, loot menu overlay. |
| `MenuSession.h/.cpp` | **(planned, ADR 0044 / spec `main-menu-subscreens.md` G1-G4b)** main help leaves 5-12/20-22 + About 8 (wrap 35, rect 85,0,300,320, dial 408,81), MAIN_HELP 4 and MINIGAME 18 lists, `ACTION_CHANGESTATE` 15 + `returnFromCredits()`, `ACTION_MINIGAME` → `Env::playMiniGame`, 18/89 → MAIN on an empty stack, menu id 89 with 0 rows + `menuId()`; only Options refuses (FE-D2). **(planned, ADR 0043 / spec `front-end.md` G2-G3)** main-menu screens 3/13/14/16 (Continue row, `(0,2)+(3,-1)` slice, confirms), actions 7/8/10/22, refusals for the out-of-scope main rows, main geometry (162-px items, no padding, rect 138,126,204,176, snapped scroll, dial knob) and `MenuViewModel::mainSkin`. **(planned, ADR 0013 / spec `specs/2026-08-28-menu.md` G3+)** the whole `ST_MENU` slice: retained menu state (current menu id/type, items copy, `selectedIndex`/`scrollIndex`, the 3-value nav stack of depth 10), `initMenu` per screen (file rows via `MenuData` + the code-built confirm/help/notebook bodies), `moveDir` wrap/clamp, page steps, `select`/`back`/`returnToGame`, and the per-frame `MenuViewModel` build (row composition, cursor oscillation, scroll pixels, scrollbar thumb). **(planned, spec `specs/2026-10-28-use-item.md`)** `Env::game`, `menuParam_`, item-use confirm 77 (`setYesNo` with `%01` args), actions 20/21/24/26, refusal screens 79-82, Nano Drinks list 75, cursor fix-up in `back()`. |
| `LoadingManager.h/.cpp` | **(planned, ADR 0043 / spec `front-end.md` G2)** `boot(skipFrontEnd)` (capture/`--newgame` bypass), `startGame(bool) -> bool` (new game → `ST_CHARACTER_SELECTION`, Continue failure reported), `startNewGameMap()`, save-exit `0x04` → `Env::backToMain`. **(planned, ADR 0035 / spec `specs/2026-10-29-map-change.md`)** port of `src/LoadingManager.cpp`'s map-change half, owned by `GameContext` (`loading()`): `loadMap(id, skipSave, newGame)` gateway, `EV_CHANGE_MAP` effects (`changeMap`), `ST_SAVING` (16) tick, var-15 travel direction, Restart Level (`loadState(2)`), load-tail hooks (`onEntitiesLoaded`/`onPlayerSpawned`/`onLoadFinished`); owns `WorldState`. Flow state: `lastMapID`, `nextMap`, `numLevelLoads[10]`, `activeLoadType`, `isLoaded`, `TM_NewGame`. **(planned, ADR 0037 / spec `save-load.md` G4b)** owns `SaveGame` instead of `WorldState`; adds `boot(forceNewGame)`, `startGame`, `loadState(type)`, `recentLoadType()`, `requestSave(type)`; `tickSaving` writes all files of the save type (MD2 withdrawn); `numLevelLoads` moves to `SaveGame`. |
| `SaveGame.h/.cpp` | **(planned, ADR 0037 / spec `save-load.md` G4a)** port of the save half of `src/Game.cpp`: the only disk access for saves (`<exe dir>/Doom2rpg.app/`, legacy file names, CRC-32 trailer, 1 MiB cap), config v11 (133 B), `hasSavedState`, `removeState`, `saveState(type, worldMap, header)`, `saveLevelSnapshot`, `loadPlayer`, `loadWorld`; owns the `WorldState` codec and the config-file state (`numLevelLoads`, `recentBriefSave`, `hasSeenIntro`, `totalPlayTime`). |
| `TravelMapSession.h/.cpp` | **(planned, spec `specs/2026-10-29-map-change.md` G5a)** `ST_TRAVELMAP` (21) phase machine (`TravelMapManager` port minus the dead star-field page): `begin(load,last)`, per-frame `buildModel(TravelMapModel&)`, `handleAction` (MENU skips, FIRE at the prompt). |
| `UiInputCollector.h/.cpp` | **(planned, ADR 0012, G3)** the single SDL -> `UiInput` normalizer (cursor in canvas coords, press/release edges, `Nav`, wheel) plus the moved keyboard->`Action` switch; keys and mouse both end up in `GameContext::pendingActions_`. |
| `PlayerActions.h/.cpp` | **(planned, P1-G7)** playing input (move/turn/use/fire commit); **(planned, ADR 0016)** the `Use` chain hoists the fire-target election above the front-tile script so the crate open (elect → crate → script → door → fire) sits where the original has it, and the elected hit + distance are computed once and reused, arrival hooks `finishMovement`/`finishRotationFired`, `flagForFacingDir`, `spawnPlayer`, the script GOTO handshake fields. |
| `ZoomController.h/.cpp` | **(planned, ADR 0039 / spec `target-practice.md` G1)** port of `src/ZoomController.cpp`: `initZoom`/`zoomOut`/`forceRemove`/state-entry `drop`, `handle(Action)` (zoomed ST_PLAYING input: yaw/pitch nudges, Z/X FOV steps 190→102, BACK/MENU zoom out, every 7th nudge advances the turn), `renderPose` (sway, FOV lerp, recoil kick). Data lives on `Game::zoom`. |
| `TargetPractice.h/.cpp` | **(planned, ADR 0039 / spec `target-practice.md` G4)** in-playing session for `EV_START_TARGETPRACTICE`: enter (save view, snap, help 17, strip, fade), `assessShot` (zones → 30/20/10, msgs 230-232/68), exit (restore, forceRemove + restoreInventory, accuracy reward 233/234/235, thread resume), `layerFrames` for the tile-149 sprite. |
| `HudButtons.h/.cpp` | **(planned, ADR 0045 / spec `hud-bar-buttons.md` G1-G3)** retained HUD-bar touch state owned by `GameContext`: weapon-select flag, 300 ms press timer, touch origin + 3 px drag latch; `resolve()` filters the HUD view's `UiResult` (blockInputTime, drag, close-on-other-button, tap vs long press). |
| `CharacterSelectSession.h/.cpp` | **(planned, ADR 0043 / spec `front-end.md` G4b)** `ST_CHARACTER_SELECTION` (22): highlighted constant (order 1/3/2), keyboard confirm sub-mode, touch ids 0-4, `choose()` = characterChoice + `Player::reset` + `ST_INTRO`; owns the Text buffers of `CharacterSelectModel`. |
| `CreditsSession.h/.cpp` | **(planned, ADR 0044 / spec `main-menu-subscreens.md` G2)** `ST_CREDITS` (15): 2:0 wrapped at 42, 16 px lines, 500 ms/line, end `500*(lines+5)`, 32 px/s on black, "Continue" once done; BACK/FIRE → `MenuSession::returnFromCredits`. |
| `ComicBookSession.h/.cpp` | **(planned, ADR 0044 / spec `main-menu-subscreens.md` G4a)** the viewer behind menu id 89: 39 landscape frames, drag / slide / snap-back, Done button, LEFT/RIGHT slide (FE-D7); routed by `GameContext` while `menu_.menuId() == 89`. |
| `PrologueSession.h/.cpp` | **(planned, ADR 0043 / spec `front-end.md` G5)** `ST_INTRO` (9): 2:12 composed with the character's name, 45-char wrap, 13-line pages, Back/More/Skip/Continue cursor, `done()` → `LoadingManager::startNewGameMap`. |
| `Familiar.h/.cpp` | **(planned, ADR 0041 / spec `familiar.md` G5-G11)** sentry-bot control behaviour (state lives on `Player`): deploy (`attemptToDeploy`/`setFamiliar` via `PlayerActions::attemptMove`), `unsetFamiliar`/`returnsToPlayer` (B key, menu row 33, `EV_FORCE_BOT_RETURN`, walking into the body, monster-at-body latch consumed at tick end), self-destruct dialog, `ST_BOT_DYING` (`StateId::BotDying = 25`) entry/tick, `died` (corpse + thrown remains + explosion via `Combat::radiusHurtEntities`). |
| `Sound.h/.cpp` | **(planned, ADR 0046 / spec `sound.md` G1)** port of `src/Sound.cpp` semantics as the process-wide service `Sound::instance()`: 145-entry resID table (1000..1144), 10 channels (resID + priority), `playSound(id, flags, prio)` with loop/stop-all flags, priority stealing and priority-6 no-restart, first-slot `stopSound`/`isSoundPlaying`, `soundStop`, music 1067..1071 on `musicVolume`, 0..100 volumes (default 80, 0 = off), `allowSounds`/`allowMusics`/`areSoundsAllowed`/`inInitMap`, lazy PCM cache; started/stopped by `AppContext`. |

### domain/game/
| File | Responsibility |
|---|---|
| `Entity.h` | World entity record: `EntityDef*`, monster ptr, tile linked-list ptrs, packed sprite index in `info` low bits (:39-40), flag bits (:22-26). |
| `Enums.h` | Legacy constants: entity types (:10-25), trace masks (:28-37), stat slots (:40-48), doors (:51-56), sprite flags (:63-72), monster anim/flags (:75-106); phase-5 additions: EV_* opcode ids, EVAL_* terms, EVFL_* trigger masks, SCR_* static-func indices. |
| `CombatEntity.h/.cpp` | Battle-stat block (8 slots + weapon); clamped set/add, XP calc (:52-54); Stage-1 combat adds verbatim `calcCombat/calcHit/calcDamage` taking `Combat&` (spec `specs/2026-08-26-combat-stage1.md`). |
| `Combat.h/.cpp` | Combat subsystem instance on `Game::combat` (ADR 0008): weapon-field constants + `tileDistances`, monsterTemplates[51], `performAttack` → two-stage hitscan `playerSeq` timer (`tick()`), `explodeOnMonster` subset, `getWeaponTileNum`; Env wired from Main. **(planned, ADR 0017 / spec `specs/2026-08-31-weapon-branch.md` G1)** the degenerate projectile pair `launchProjectile()` / `updateProjectile()` (+ `exploded` / `missileAnim`) replaces the inlined stage-0 hit application, unblocking every `PROJTYPE -1` weapon (chainsaw, sentry bots); `flagForWeapon` (G4); real missiles are G5. **(planned, ADR 0028 / spec `specs/2026-09-22-monster-attacks.md` G2-G4, G7)** `curAttacker` selects `monsterSeq` vs `playerSeq` in `tick()` (= `runFrame`), `performMonsterAttack`, `explodeOnPlayer`, `playerPain`/`playerPainEvent` (presentation half of `Player::pain`/`painEvent`), `getMonsterField`; missiles 6/7 + `activeMissiles[8]` (G7, supersedes weapon-branch G5.1). |
| `EntityMonster.h` | Monster payload struct (ce, ring links, nextAttacker/target, frameTime, flags, goal fields) pooled [80] on Game with active/inactive circular rings via `Game::activate/deactivate` (ADR 0008). |
| `Player.h/.cpp` | **(planned, ADR 0041 / spec `familiar.md` G1)** familiar state (`isFamiliar`, `familiarType`, `inventoryCopy`, status/buff copies, `playerEntityCopyIndex`, `save*`, latches), `give(..., forceReal)` routing into the bot bag, familiar arms of `getHealth`/`addHealth`/`pain`/`selectWeapon`/`addStatusEffect`. Player state: stats, inventory[26]/ammo[9]/weapon bitmask, XP; discrete grid movement via `kViewStepValues` 8-dir table (:8-11). **(planned, spec `specs/2026-08-31-weapon-branch.md` G1-G2)** `fireWeapon` refuses only `PROJTYPE > 0`; `modifyStat` + `usedChainsaw` (strength-growth counter, screen shake, msg 241). **(planned, spec `specs/2026-09-22-monster-attacks.md`)** `pain(n)` state half returning the health-crossing band, `addArmor`, static `calcDamageDir`, `viewRoll` + death counters (G8a). The player entity `entities[1]` is linked at its destination tile by `Game::relinkPlayer` (ADR 0027). **(planned, ADR 0032 / spec `specs/2026-10-28-use-item.md`)** `useItem` → `UseResult`, `addHealth(n, showMessage)` with msg 111 via `setMessaging(Hud*, Localization*)`, bool `addArmor`; `statusEffects[54]`/`buffs[30]` + add/remove/update/translate, `updateStats`, `advanceTurn` (countdown + regen), `turnTime`. |
| `Game.h/.cpp` | **(planned, ADR 0042 / spec `familiar.md` G2, G4)** 16 drop entities appended at spawn, `spawnDropItem` x2, `throwDropItem`, `spawnPlayerEntityCopy`, `spawnSentryBotCorpse`, `attemptToDiscardFamiliar`. Simulation subset: 32x32 entityDb lists (Game.h:69), door anims (6 slots), faced-door use `useDoorFacing` (ADR 0001), faithful swept-capsule move trace `traceMove` per ADR 0002 / spec `specs/2026-08-23-faithful-player-collision.md`, turn-advance auto-close with tile-granular occupancy, linked-state door solidity. Phase-5 additions: `setLineLocked` (tileNum bit0 flip + def re-lookup by tileNum+257), `advanceTurn` subset, eventFlags movement masks, `findEntityBySprite`, blocking-door-open thread resume via `DoorAnim::ownerThread`. **(planned, ADR 0015/0016 / spec `specs/2026-08-30-blocking-crates-shelf-pickup.md`)** `loadEntities` becomes the verbatim legacy spawn rule (entity for every sprite with a def + def-less sprite-wall fallback + oriented link-tile nudge + per-family `initspawn` marks, hidden sprites spawned-but-unlinked), plus `openCrate()` (arm `param`, unlink immediately) and the crate frame stepper inside `update()` on the `SpriteLerps` clock; `numDestroyableObj` / `lootSource` counters. **(planned, ADR 0018 / spec `specs/2026-08-31-weapon-branch.md` G3-G4)** `entityPain` / `entityDied` dispatchers on `eType` with the prop and corpse arms (`painProp` / `diedProp` / `diedCorpse`) living here, the monster arms delegating to `MonsterSystem`. Corpse loot (ADR 0006): `poolLootCorpse` marks + pools all eType-9 entities on a tile and composes the display lines (`Game::LootPool`: entries/credits/`Text` + line table), `giveLootPool` grants on UI close (replaced the interim auto-grant `lootCorpse`). |
| `ParticleSystem.h/.cpp` | **(planned, ADR 0047 / spec `particles.md` G1)** port of `src/ParticleSystem.cpp` state + spawning as `Game::particles`: 4 round-robin emitters, 64-entry linked-list pool (59 usable), `spawnParticles(type,color,sprite | x,y,z)`, `spawnMonsterBlood`, `freeAllParticles` (map load, cinematic skip); 5 `std::rand()&0xFF` rolls per particle; `kLevelColors[8]` ([7] = white); `DOOM2RPG_NO_PARTICLES` switch. |
| `Targeting.h/.cpp` | **(planned, ADR 0010 / spec `specs/2026-08-26-decomposition.md` P1-G4)** view forward vector, facing probe (health-bar feed), ordered fire-target election. |
| `ZoomState.h` | **(planned, ADR 0039)** plain sniper-zoom data (the 15 legacy `Canvas` zoom fields) on `Game::zoom` + `ZoomPose`. |
| `ShotZones.h/.cpp` | **(planned, ADR 0039)** `shotZone`: `getImageFrameBounds(tile,3,2,0)` + head/body/leg box test, shared by target practice and the monster scope crit. |
| `TraceHit.h` | **(planned, ADR 0011)** typed trace result: `kind{None,World,Ent}` + entity + frac + resolved eType; `startsInside()` names the legacy `frac == -1` own-tile convention, `isWorld()` replaces `def == nullptr`. |
| `TraceSystem.h/.cpp` | **(planned, P2-GA)** capsule/line + capsule/circle traces, entityDb broadphase, world-line pass, `distFrom`, trace scratch state — moved out of `Game`. **(planned, ADR 0027 / spec `specs/2026-09-22-monster-attacks.md` G1, G6)** `setPlayerDest`: the `ET_PLAYER` hit and `distFrom(player)` use the destination; lerping monsters are traced at their goal tile. |
| `DoorSystem.h/.cpp` | **(planned, P2-GB)** door use/open/close/anim/auto-close, `DoorAnim` pool. |
| `MonsterSystem.h/.cpp` | **(planned, P2-GC)** monster payload pool + rings, activate/deactivate, pain/died/XP, corpsify. |
| `MonsterAI.h/.cpp` | **(planned, ADR 0028 / spec `specs/2026-09-22-monster-attacks.md` G2, G6)** per-monster brain and legs: `think` (goal validity/choice), `weaponForTarget`, `isAttackValid`, the one-step DFS path search (`goalMove`/`calcPath`, verbatim incl. the `checkLineOfSight` quirk), step-lerp start/finish (`finishLerp` is called by `SpriteLerps` on completion), static monster-clip bitset `baseVisitedTiles` (+ `EV_MARKTILE`). `MonsterSystem` gains the collective side: pending-attack queue (`attack`/`undoAttack`), `monsterAI`/`monsterLerp`/`updateMonsters`/`snapMonsters`, `onMonsterAttackDone` (the `combatState` monster arm), `renderActivate` (sight wake, ADR 0029). |
| `GameSprites.h/.cpp` | **(planned, ADR 0030 / spec `specs/2026-09-22-monster-attacks.md` G7)** 48 runtime sprites in the custom slots appended to `MapData` (missiles, impact anims, claw overlays): `alloc`/`allocAnim`/`destroy`/`clear`/`update`, `activePropogators`. |
| `ViewVisibility.h` | **(planned, ADR 0029, G5)** plain data written once per frame by `GameContext::render` from `SceneRenderer`/`World3D::drawBSP`: drawn sprite list + visible-tile bitset + **(ADR 0036)** `seenLines` (automap line candidates); read next tick by sight wake and the on-screen step test. |
| `SpriteLerps.h/.cpp` | **(planned, P2-GD)** script sprite-lerp pool (LERP* opcodes), `vecToDir`, `spriteZBias`. **(planned, spec `specs/2026-10-28-secret-doors.md` G1)** two-phase secret push-wall lerp `startSecretWall` (SECRET_OPEN -> re-arm SECRET_HIDE in `freeLerpSprite`, unlink + HIDDEN at end), `secretActive()`; `Game::awardSecret` + `mapSecretsFound` (G2). |
| `Automap.h/.cpp` | **(planned, ADR 0036 / spec `automap.md` G2, G4a)** free functions on `MapData`: `markSeen` (frame's `ViewVisibility` → line bit 8 / sprite `0x200000`, gated like legacy `updateAutomap`), `uncover` (tile `0x80`, 3x3), `giveMap` (`EV_GIVE_AUTOMAP`), `buildModel` (per-frame `AutomapModel`, legacy draw order + cursor blink). |
| `WorldState.h/.cpp` | **(planned, ADR 0034 / spec `specs/2026-10-29-map-change.md` G2a)** per-map world state: ten in-memory blobs in the legacy brief `saveWorldState` layout (`saveLevel`/`restoreLevel`), level-entry `Player` snapshot for Restart Level, `levelBlob()` for the future save system. **(planned, ADR 0037 / spec `save-load.md` G2, G4b)** becomes a pure codec: `save(bool brief)` / `restore(bytes) -> Applied/Rejected/Corrupt`; slots and the `Player` snapshot are deleted. |
| `PlayerState.h/.cpp` | **(planned, ADR 0037 / spec `save-load.md` G3)** player-record codec: `Header` (34 B) + `Player::saveState` body (271 B, +108 with status effects); `load` validates and commits into the `Player` only when the whole record is valid. |
| `EntityState.h/.cpp` | **(planned, G2b)** entity records of the world blob: port of `saveEntityStates`/`getSaveHandle`/`Entity::saveState`/`loadState`/`restoreBinaryState`, brief variant. **(planned, spec `save-load.md` G2)** full variant (`save(..., brief)`: live monsters keep `monsterEffects`/health/goal) and `linkIndex` range guard. |
| `CorpseLoot.h/.cpp` | **(planned, P2-GE)** loot-set defaults, corpse pooling/composition, grant pass. |
| `ItemPickup.h/.cpp` | **(planned, ADR 0014 / spec `specs/2026-08-29-world-item-pickup.md` G2)** world-item touch slice: `touched`/`touchedItem` per item class (inventory/food/ammo/weapon rolls, caps, starter ammo), HUD messages 83-87/223, `EntityDb::removeEntity` + SCR_ITEM_PICKUP hook; driven by `Game::touchTile` from `PlayerActions::finishMovement` only. |
| `EntityDb.h/.cpp` | **(planned, P2-GF)** entity vector + 32x32 tile lists, link/unlink/find, world & player slots. **(planned, ADR 0042 / spec `familiar.md` G2)** drop ring: `firstDropIndex`/`dropIndex`/`lastDropEntIndex`, `getFreeDropEnt`, `isDropped`. |
| `WeaponTable.h` | **(planned, ADR 0011, P3-C2)** typed row views over `tables->weaponData` (stride 9, `WeaponField` enum) and `tables->weaponInfo` (stride 6, `WeaponPose`), replacing `weapons[id*9+field]`. |
| `ScriptVM.h/.cpp` | Faithful tileEvents interpreter (phase 5, ADR 0003 / spec `specs/2026-08-23-phase5-skeleton.md` §7): 20-thread pool (`ScriptThread`: IP/FP/stackPtr/unpauseTime/type/flags/state), big-endian dispatch with the legacy post-opcode `++IP` contract, trigger filter, `executeTile`/`executeStaticFunc`/`runScriptThreads`, `scriptStateVars[128]`; TIER-A opcodes functional (EVAL/JUMP/CALL/RETURN/ITEM_COUNT/DOOROP/EVENTOP/GIVEITEM/WAIT/ABORT_MOVE/MESSAGE/TILE_EMPTY…); **(planned, spec `specs/2026-08-30-blocking-crates-shelf-pickup.md`)** `EV_GIVEITEM` mode 0 = sprite-touch grant through `EntityDb::findEntityBySprite` + `ItemPickup::touched` (the shelf/"look + Enter" pickup), and `EV_GIVELOOT` composes/grants the script loot dialog, TIER-B logged no-ops (incl. parse-only EV_CHANGE_MAP, non-pausing EV_DIALOG). |
| `DialogSystem.h/.cpp` | Faithful port of Canvas::dialogSystem (ADR 0004 / spec `specs/2026-08-24-intro-sequence.md` GROUP 1): styled bottom boxes, %NN text-arg composition, wrap + paging, typewriter reveal, style-2 help FIFO, ST_DIALOG input dispatch. `drawScrollBar` is the shared Canvas::drawScrollBar port; public since ADR 0006 so the loot menu overlay can reuse it. **(planned, G4 of spec `specs/2026-08-30-blocking-crates-shelf-pickup.md`)** `startDialogText(thread, Text&, style, flags, resume)` — the composed-buffer entry point `EV_GIVELOOT` needs. |

### domain/world/
| File | Responsibility |
|---|---|
| `MapData.h` | **(planned, ADR 0042)** `numSprites` = file sprites + 48 custom + 16 drop slots, `firstDropSprite()`. Parsed mapXX.bin container: header (:21-38), media (:41-49), BSP geometry (:52-62), sprites (:64-70), events/bytecode/flags (:73-76), maya cams (:79-86), decoded `Polygon`s (:96-104); **planned (spec `specs/2026-08-26-decomposition.md` P1-G1)**: `heightAt(x,y)` terrain accessor so modules stop calling back into `GameContext::getHeight`. |
| `MapParser.h/.cpp` | Static parser mirroring legacy `LoadingManager::loadMapData` (v3); `decodePolys()` expands leaf nodePolys into quads incl. 2-vert edge expansion (:206-298). |

### graphics/
| File | Responsibility |
|---|---|
| `Image.h` | Palette-indexed image from BMP loading (dims, indices, RGB565 palette, texture slot). |

### io/
| File | Responsibility |
|---|---|
| `BmpImageLoader` | 4/8-bpp BMP decoder replicating legacy transform exactly (nibble expand, row flip, RGB565, 0xF81F transparency) (header :12-17). |
| `DataReader/.cpp` | Sequential LE reader; `readCoord()` = byte\*8 (:35-36). **(planned, spec `save-load.md` G1)** `overrun()` flag set by any read past the end. |
| `DataWriter/.cpp` | Growable LE writer, atomic flush (:31) — for future saves. |
| `EntityDefs/.cpp` | Parses entities.bin → `EntityDef` (tileIndex/name/eType/eSubType/parm/touchMe); find by type or tileIndex. |
| `Localization` | strings.idx + stringsNN.bin chunks; `(type<<10)|index` ids; `titleOf()` splits on `\|`. |
| `Media/.cpp` | `MediaMappings::load` newMappings.bin (:10-31); `MediaLoader::registerMedia/finalize` loads palettes + rolling texels files (:41-153). |
| `MenuData/.cpp` | **(planned, ADR 0013, G1)** `menus.bin` parser: 72 rows (`id` / `type` / item span) + 426 item ints unpacked into `MenuItemDef{labelId,flags,action,param,helpId}`; lookup by legacy menu id, absent id = empty menu + one log line. |
| `Resources.h` | File-name constants for all archive entries. |
| `Tables/.cpp` | tables.bin offset table → 18 typed vectors (combat, keys, sin table, sky A/B...) (:25-68). |
| `ZipArchive/.cpp` | Minimal zip reader: central dir, case-insensitive lookup, stored + raw-deflate. |

### platform/
| File | Responsibility |
|---|---|
| `FileSystem` | Base/save paths via SDL_GetBasePath, archive search, whole-file read, atomic write. **(planned, spec `save-load.md` G1)** `readFile` size cap + `ftell` guard, `fileExists`, `removeFile`. |
| `InputSystem` | SDL event pump → single callback (:5-15); action-mapping pipeline is future work. |
| `AudioOutput` | **(planned, ADR 0046 / spec `sound.md` G1)** SDL audio callback device (S16 stereo 44100 Hz, own `SDL_InitSubSystem(SDL_INIT_AUDIO)`, failure = silent) + 10-voice mixer with linear gain; `decodeWav` via `SDL_LoadWAV_RW` + `SDL_AudioCVT`. All methods lock the device. |
| `Window` | SDL2 window + GL 3.3 core context (Apple) / compatibility (:42-50); letterbox viewport (:159-169); canvas fixed 480x320 (:25-26). **(planned, ADR 0012, G1)** `screenToCanvas` (:171-176, no callers) replaced by `windowToDrawable`; the canvas half of the inverse lives in `RenderBackend`. |

### render/
| File | Responsibility |
|---|---|
| `Camera3D` | Faithful 14.14 fixed view/projection/MVP via 1024-entry sin table (:15-62); GLES BeginFrame projection tweaks (:83-92); float + int matrix accessors. **(planned, ADR 0031)** also the legacy `mvp2D` (yaw-only view × projection(fov+|pitch|)) for `ColumnOcclusion`. |
| `ColumnOcclusion` | **(planned, ADR 0031, spec `specs/2026-09-22-monster-attacks.md` §6.5)** CPU integer port of TinyGL's 2D column-occlusion cull (`transform2DVerts`/`clipLine`/`projectVerts`, `occludeClippedLine`, `clippedLineVisCheck`) + `Render::cullBoundingBox`. Used by `World3D::walkNode` only to fill `ViewVisibility` (legacy-visible leaves/sprites, `checkTileVisibilty` tile bits); never culls the GPU draw. |
| `Graphics2D` | Canvas-space 2D over SpriteBatch: rects/lines, blits with rotateModes 0-8, anchors, scaled draws, `drawString` with `^N` colors/buff icons; clip recorded but not applied (:20-24) — **(planned, ADR 0012, G1)** `setClip`/`clearClip` become a real GL scissor through `SpriteBatch::setScissorCanvas`, single level (nesting is `Ui::pushClip`'s job). |
| `ParticleRenderer` | **(planned, ADR 0047 / spec `particles.md` G2)** `drawParticles`: legacy billboard projection through `Camera3D::mvpTglInt`, whole-emitter column cull via `World3D::occlusion()`, render-time aging + reaping, dots (`Graphics2D::fillCircle`) and gib sprites (`UiAssets::gibs`), placed in the world band (raster + (0,6)), clipped to box ∩ band; drawn after `drawFade`. Also adds `Camera3D::mvpTglInt`, `ColumnOcclusion::column`, `World3D::occlusion`, `Graphics2D::fillCircle`. |
| `RenderBackend` | Frame owner: beginFrame clear + letterbox viewport + batch begin (:37-47); endFrame flush+swap (:49-52); `setCanvasViewport/restoreCanvasViewport` scope the 3D band — ONE world rect (1,7,478,248) for gameplay and cinematics alike (ADR 0009 + its 2026-08-26 amendment: `src/GLES.cpp:119-127` hardcodes the GL y for both, the cinematic letterbox is the cockpit overlay at canvas y = 42, not a viewport); both flush the sprite batch first; **(planned, G1)** latches the letterbox rect into `SpriteBatch` and exposes its inverse (`letterboxRect`, `drawableToCanvas`) for cursor mapping. |
| `SceneRenderer` | **(planned, ADR 0010, P1-G6)** the world pass: viewport band, camera setup (player view or cinematic `MayaPose`), screen shake, per-sprite sort-bias/character classification, `drawSky`+`drawBSP`; owns `Camera3D`. **(planned, ADR 0048 / spec `corpse-halo.md`)** per-sprite lootable-corpse halo byte (0 / 18 / 17) + `drawWorld(..., cameraState)`. |
| `World3D` | **(planned, ADR 0019 / spec `specs/2026-09-01-blend-modes.md`)** the full 14-row `RENDER_*` blend table (blend func + `uColorMod` modulation uniform + per-mode fog) behind `applyBatchState`'s flush-compare-assign cache, the tile-136 torchiere glow (extra tile-193 `ADD50` quad drawn before the lamp body, `drawTorchiereGlow`), the tile-212 `SUB` back-end override and the per-tile geometry mode in `drawPoly`. GL 3.3 world renderer: palette-LUT textures (:202+), sky (:399-454), polys (:329+), BSP walkNode painter's algorithm with per-leaf sprites (:788-881), billboard/wall/flat/slip-door sprites + RLE (:100+, :507-746), eye-space fog (:180-200), time animation (:78), per-sprite sort-bias hook on `drawBSP`. Stacked characters (ADR 0005): NPC branches + monster-family ATTACK deltas (ADR 0007, spec `specs/2026-08-26-monsters-stack-flicker.md`); floater/special-boss families still excluded. **(planned, ADR 0048)** lootable-corpse pulsate halo (enlarged pre-draw under `Scene3D::setPulsate`) in the billboard branch and `kManimDead`, fed by `drawBSP(..., spriteHalo)`. |

### render/api/
**(planned, ADR 0020 / spec `specs/2026-09-02-render-backend-split.md`)** target
`dr_render_core`: the backend-neutral render library. No GL, no SDL, no game deps.

| File | Responsibility |
|---|---|
| `TextureId.h` | Opaque `TextureId` (0 = invalid) + creation-time `TextureFlags{TransparentKey, Tiled}`. |
| `TextureStore.h` | Texture creation interface: `createIndexed(indices, w, h, palette565, count, flags)`, `createRgba`, `destroy`, `query`, `textureBytes`. Indexed data stays the currency because every shipped image is palette-indexed (`docs/original-code/image-formats.md` §0). |
| `Texture.h/.cpp` | Move-only RAII handle every caller keeps (`uploadIndexed(store, ...)`); caches w/h for the anchored blits. Replaces `render/gl/Texture.h` for `ui/`, `text/`, `render/World3D`. |
| `Draw2D.h` | 2D device: `drawQuad`, `fillQuad`, `setRenderMode`, `setClipCanvas`/`clearClip`, `flush`. Derived from the actual `Graphics2D`/`Ui`/`Font` call inventory. |
| `Scene3D.h` | 3D device: `beginScene(SceneView{mvp, view})`, `setTexture`, `setRenderMode`, `submitTriangles(WorldVertex*, n)`, `setFog`, `drawSky(tex, uOffset)`, `endScene`. **(planned, ADR 0048)** `setPulsate(a)`: legacy `RENDER_FLAG_PULSATE` state, software path: flat opaque grey on non-transparent texels via forced fog colour, no shader change. No shaders, no matrices as uniforms, no depth buffer. |
| `RenderBackend.h/.cpp` | Abstract frame/device owner (same method names as the old concrete class) + `requestCapture(path)`, `draw2d()`, `scene3d()`, `textures()`, `name()`. |
| `RenderModes.h/.cpp` | The 14-row legacy `RENDER_*` table in neutral `BlendFactor` enums + `clampRenderMode`/`renderModeNeedsWarning` (moved out of `World3D.cpp:145-201`, ADR 0019). |
| `PixelConvert.h/.cpp` | RGB565 -> RGBA8 bit replication, palette expansion with the `0xF81F` key, indexed -> RGBA expansion (shared by the GL LUT and the SDL store). |
| `QuadUV.h/.cpp` | Legacy `rotateMode` 0..8 -> quad UV permutation, extracted from `SpriteBatch::draw` so backends cannot drift. |
| `CanvasViewport.h/.cpp` | Letterbox rect + canvas<->drawable mapping (moved from `Window::computeViewport` / `RenderBackend::drawableToCanvas`), shared so input mapping is bit-identical across backends. |
| `BmpWriter.h/.cpp` | 24-bit BMP dump for F12 frame capture (replaces the unreferenced `saveIndexedBmp` debug helper). |

### render/sdl/ — **RETIRED (ADR 0024, spec `specs/2026-09-11-sokol-gfx-backend.md` group G1)**
Deleted in full: the directory, the `dr_render_sdl` target, `BackendKind::SdlRender`,
`GraphicsApi::SdlRender`, `Window::sdlRenderer()`, the CPU vertex pipeline, the
object-space UV tile split, the per-vertex fog/haze pass and the G7.1 adaptive
tessellation. Reason: 59.1 MB of texture memory against the indexed path's 15.1 MB,
and nothing it did was reusable on Metal/D3D11. Kept below for the record of what
existed and why we know what we know.

_Was:_ **(ADR 0021)** target `dr_render_sdl`: `SdlTextureStore` (measured 2026-09-02:
SDL2 rejects `INDEX8` on every driver, so it expands once via an INDEX8 surface into a
runtime-negotiated 32-bit format — ~40 MB for the whole world texture set; SDL3 supports
`INDEX8` + `SDL_SetTexturePalette` natively and would keep the 9.7 MB indexed pipeline,
which is why the interface never speaks RGBA — see spec §4.2.0-§4.2.3 for the fork and
the required palette-swap measurement), `SdlDraw2D` (everything through
`SDL_RenderGeometry` + a 1x1 white texture for fills, canvas-space clip rect),
`SdlScene3D` (CPU vertex pipeline: near clip, NDC -> canvas viewport, object-space UV
tile split because SDL textures are CLAMP_TO_EDGE, batching by texture+mode,
per-vertex fog + haze pass; **planned, ADR 0023 / spec
`specs/2026-09-07-sdl-tessellation.md`**: adaptive `n x n` tessellation of the
near-clipped triangles against the affine warp — criterion = peak affine displacement in
canvas pixels, cut by averaging clip-space vertices, `n = ceil(sqrt(M/2px))` capped at 8,
downstream of the tile split; env knobs `DOOM2RPG_SDL_TESS` / `DOOM2RPG_GFX_STATS`), `SdlBlendModes` (14 rows -> `SDL_BlendMode`, custom mode
for `RENDER_SUB`, `RENDER_NONE` = skip), `SdlRenderBackend`.

### render/sokol/
**(planned, ADR 0024/0025/0026 / spec `specs/2026-09-11-sokol-gfx-backend.md`)** target
`dr_render_sokol`: the ONE graphics backend. GPU API chosen at build time by
`DOOM2RPG_SOKOL_BACKEND` = `metal` (Apple default) | `d3d11` (Windows default) |
`glcore` (elsewhere, and permanently configurable everywhere for the F12 diff, ADR 0026).

| File | Responsibility |
|---|---|
| `SgCommon.h` | The only include of `sokol_gfx.h` (declarations); asserts exactly one `SOKOL_*` define. |
| `SokolGfxImpl.cpp` / `.mm` | The single `SOKOL_IMPL` translation unit; Objective-C++ on Apple (`sokol_gfx.h:83`). |
| `SgEnvironment.h` | Everything sokol refuses to do: `create` device, `environment()` for `sg_setup`, `acquireSwapchain()` **per frame** (`sg_swapchain`, `.invalid` on failure), `present()`, `resize()`, `readPixels()`, `apiName()`; plus `sokolGraphicsApi()`. |
| `SgEnvironmentGl.cpp` | Context already made by `Window`; `gl.framebuffer = 0`; `SDL_GL_SwapWindow`; the only `readPixels` that works (ADR 0026). |
| `SgEnvironmentMetal.mm` | `SDL_Metal_CreateView` + `SDL_Metal_GetLayer` + `MTLCreateSystemDefaultDevice`; `nextDrawable` per frame via `(__bridge const void*)`; `present()` does **nothing** — sokol calls `presentDrawable` in `sg_commit` (`sokol_gfx.h:17207-17209`). |
| `SgEnvironmentD3D11.cpp` | Device + swap chain on the SDL `HWND`, one RTV, no depth-stencil view. |
| `SgShaders.h` | Includes the two shdc-generated headers from the build tree. |
| `SgPipelines.h/.cpp` | The 14-row `RENDER_*` table (`render/api/RenderModes.cpp`) deduplicated into **4** `sg_blend_state`s x 4 programs = a 16-entry pipeline cache. `mod[4]`/`fog` stay uniforms. Mode 7's alpha factors are split to `(ZERO, ONE_MINUS_SRC_ALPHA)` for D3D11 portability (visible RGB identical). |
| `SgTexture.h/.cpp` | Per texture: `SG_PIXELFORMAT_R8` index image + view, RGBA8 256x1 palette image + view, one of two shared samplers. R8 support verified on GL/GLES3/Metal/D3D11/WGPU/Vulkan (spec §0.1). |
| `SgTextureStore.h/.cpp` | `TextureStore`; transliteration of `GlTextureStore` (slot vector, free list, slot 0 never handed out, stable `lookup()`). `textureBytes()` must equal the GL store's number. |
| `SgFrame.h/.cpp` | **ADR 0025**: the per-frame command list (`Viewport`/`Scissor`/`Draw`) + one 4 MB write-transient vertex buffer shared by both strides. `replay()` is the only code in the program that calls `sg_apply_*`/`sg_draw`. |
| `SgDraw2D.h/.cpp` | `Draw2D`; port of `GlDraw2D`, same batching and clip semantics. Scissor arithmetic copied **verbatim** from `GlDraw2D::applyScissor` (`lroundf`, not `CanvasViewport::canvasSubRect`'s truncation) so the F12 diff stays clean. The 1x1 white texture disappears (`fillQuad` uses the `quad_color` program). |
| `SgScene3D.h/.cpp` | `Scene3D`; port of `GlScene3D`. Per-pixel fog on every backend (**ADR 0022 has no subject left**). `depth_fix` uniform remaps clip z `[-w,w] -> [0,w]` on Metal/D3D11 and is the exact identity on GL (spec §0.4) — without it the near half of the world vanishes on Metal. |
| `SgRenderBackend.h/.cpp` | `RenderBackend`: `sg_setup` with enlarged pools (8192 images / 8192 views, spec §5.1), letterbox, one pass per frame (`sg_begin_pass` -> `SgFrame::replay` -> `sg_end_pass` -> `sg_commit` -> `env_->present`), capture. **No depth buffer anywhere** (spec §0.3). |
| `shaders/quad2d.glsl` | Hand-written annotated GLSL, 3 programs (`quad_indexed`/`quad_rgba`/`quad_color`) on one vertex shader; `uCanvasSize` becomes `vec4 canvas_size`, `uColorMod` moves into the `fs2d_params` block. |
| `shaders/world.glsl` | Hand-written annotated GLSL, program `world` (also draws the sky with an identity MVP). Verified to compile for glsl410/glsl300es/metal_macos/metal_ios/metal_sim/hlsl5/wgsl/spirv_vk. |

### render/gl/ — **reference until spec `2026-09-11-sokol-gfx-backend.md` group G8**
Kept deliberately through the whole sokol migration as the pixel reference:
`--backend=gl` vs `--backend=sokol` built with `DOOM2RPG_SOKOL_BACKEND=glcore`,
compared with F12 captures, is a byte-level diff on the same driver (ADR 0026).
Deleted only after the user signs off on G5 (byte-identical) and G6 (Metal looks right).

| File | Responsibility |
|---|---|
| `GlCommon.h` | Platform GL header selection (:4-11). |
| `Shader` | RAII program wrapper, uniform setters incl. mat4. |
| `GlDraw2D` | **(planned, ADR 0020, G2)** the renamed `SpriteBatch` implementing `Draw2D`; `GlTextureStore` owns the `GlTexture` slots behind `TextureId`; `GlScene3D` gets the world shader/VAO/VBO/fog/sky moved out of `World3D.cpp`; `GlRenderBackend` is the old concrete `RenderBackend`. Target `dr_render_gl`. |
| `SpriteBatch` | Batched quad renderer (4096 max, :18-19); three programs: indexed-palette, RGBA, flat color (:64-66). **(planned, ADR 0012, G1)** canvas-space scissor (`setLetterbox`/`setScissorCanvas`/`clearScissor`) following the `setBlendMode` compare-flush-assign precedent (:145-149); GL y measured from the bottom. |
| `Texture` | Move-only texture: indexed R8 + RGBA8 palette LUT with 0xF81F kill, optional GL_REPEAT (:33), plain RGBA8 (:37). |

### text/
| File | Responsibility |
|---|---|
| `Font` | Font.bmp atlas: 12x16 glyphs, advance 9 (:18-20), `^N` color table (:23-26), char→glyph mapping (:43). |
| `Text` | Mutable text buffer port of legacy Text: append/find/edit, wrap with break char (:58-62), width measure (:67-69). |

### ui/
| File | Responsibility |
|---|---|
| `ViewWeapon` | First-person weapon quad + muzzle flash: legacy viewport-relative anchors magnified by the live projection, hand-clipped to the world band by hand (quad + source sub-rect — scissor cannot reproduce it). **(planned, ADR 0012, G6)** takes a `ViewWeaponModel` instead of reading `Player`/`Combat`; the `flashDone` latch moves to `Combat::tick`. |
| `Hud` | **(planned, spec `familiar.md` G6-G7)** brighten ramp state, `drawFamiliarOverlay` (tint + scanlines + white flash), sentry top/bottom panel overlays, `damage_bot` vignette. Loads ~24 BMP textures (`startup()` :72-106); status bars, cockpit, weapon select (:211-233), bubbles, vignette (:136-169), arrows, monster bar (:180-209), messages, bottom bar (:533-565). Demo fields still exist (`Hud.h:166-172`). **(planned, ADR 0012, G4)** all `draw*` move to `HudView`; `Hud` keeps only the non-drawing producer runtime (shake, monster-bar drain, cockpit toggle). **(planned, spec `specs/2026-09-22-monster-attacks.md` G4, G8a)** real damage state (`damageDir`/`damageTime`/`damageCount`, demo API removed) + `drawDamageEffects` (arrow, vignette), `RockView` (applied by `SceneRenderer`), `stopShake`, screen fade. |
| `UiTypes.h` | **(planned, ADR 0012, G2)** `UiId` / `UiAction` / `Nav` enums, `UiInput`, `UiRect`, `UiResult`. |
| `UiState.h/.cpp` | **(planned, G2)** the ONLY retained UI state: `activeId`, per-region scroll offsets, text wrap cache (`<start,len>` tables). Nothing else may be added. |
| `Ui.h/.cpp` | **(planned, G2)** immediate-mode primitives (`panel`, `image`, `number3`, `label`, `textRows`, `textBlock`, `button`, `softKey`, `listHit`, `scrollBar`, `face`, `weaponIcon`, `keys`, `pushClip`) + the press/release hit-test rule. All constants come from the caller. `scrollBar` is the moved `DialogSystem::drawScrollBar`. **(planned, spec `specs/2026-08-28-menu.md` G2)** `+ glyph(char,...)` (the legacy `drawCursor`) and `+ scrollBarMenu(barRect, thumbOffset, thumbLen)` — the second scrollbar style (`gameMenu_ScrollBar` + three sliders), which is a different widget from `scrollBar`. |
| `UiAssets.h/.cpp` | **(planned, G2)** owner of the UI BMP sheets (moved `Hud::startup`), loaded through an injected `ResourceReader` so `ui/` stops including `core/AppContext.h`. |
| `HudModel.h` | **(planned, G4)** `HudModel` / `TextSlot` / `MonsterBarModel` — per-frame structs rebuilt by `GameContext`, never stored. |
| `HudView.h/.cpp` | **(planned, ADR 0045 / spec `hud-bar-buttons.md` G1-G3)** hit test + `*_Active` swap for weapon/shield/health/keys (`WeaponTap`/`ItemsDrinks`/`Items`/`QuestLog`), weapon-select overlay (50 % dim from y=20, 102x44 icons 4 per row from (28,208), rows -48, `HudWeaponSel0..14` -> `WeaponSelect`, `panelBottom` repaint). **(planned, G4)** `UiResult drawHud(Ui&, const HudModel&)`; bottom-bar geometry per `docs/original-code/ui.md` §1-§6. |
| `LootView.h/.cpp` | **(planned, G5)** `drawLootList` — moved verbatim from `LootSession::draw`; 3×16 px rows, no scissor. |
| `DialogView.h/.cpp` | **(planned, G7)** `drawDialog` — the drawing half of `DialogSystem` only. |
| `LoadingView.h/.cpp` | **(planned, spec `specs/2026-10-29-map-change.md` G3)** `drawLoading` — the `ST_SAVING`/`ST_LOADING` box (FabricBG tile, caption, "Just a moment!", bar + fire pacifier). |
| `TravelMapModel.h`, `TravelMapView.h/.cpp` | **(planned, G5a/G5b)** per-frame travel-map model and its view (background, BLEND50 grid, closeup, path reveal + label, ship, crosshair lines, locator box, prompt). |
| `AutomapModel.h`, `AutomapView.h/.cpp` | **(planned, ADR 0036 / spec `automap.md` G4a/G4b)** ST_AUTOMAP (6) model (ordered rect/line prims, cursor, soft-key labels) and its view (gameHelpBG, prims, `Automap_Cursor.bmp` 4x4 frame, Menu/Leave soft keys). |
| `MenuModel.h` | **(planned, spec `specs/2026-08-28-menu.md` G3)** `MenuRow` + `MenuViewModel`: menuRect/clip/itemWidth geometry, ALL rows with their resolved heights, `scrollPx`, `selectedRow`, cursor offset, scrollbar thumb, chrome flags and the two soft-key labels. No menu id, no `StateId`. |
| `ComicBookModel.h`, `ComicBookView.h/.cpp` | **(planned, ADR 0044 / G4a)** white clear, current frame at `endPoint` with neighbours at ±480, blue "Done" box (10,10,60,30); frames in `UiAssets::comicFrames[39]` (resident). `IntroMovieView` also gains `drawCredits` + `CreditsModel` (G2). |
| `MainMenuView.h/.cpp` | **(planned, ADR 0043 / spec `front-end.md` G3)** `drawMainMenu(Ui&, const MenuViewModel&)`: logo.bmp + logo2, 204x38 plates (25 % idle / `_on` held), dial + knob, Switch_Left Back key; same model as `MenuView`. |
| `CharacterSelectModel.h`, `CharacterSelectView.h/.cpp` | **(planned, G4a)** per-frame model and view of `ST_CHARACTER_SELECTION` (slots, avatar, stats panel, Back/Yes, confirm cursor). |
| `PrologueModel.h`, `PrologueView.h/.cpp` | **(planned, G5)** per-frame model and view of `ST_INTRO` (gameHelpBG, page slice, Back/More/Skip/Continue). |
| `MenuView.h/.cpp` | **(planned, G3+)** `UiResult drawMenu(Ui&, const MenuViewModel&)`: opaque background + bottom panel + health/shield readout + soft keys, then the clipped row walk (296x32 plates at 25 %/100 % alpha, `'\x8A'` cursor glyph, right-aligned value column) and the menu's own 4-sheet scrollbar. |

### minigames/ — **(planned, ADR 0033 / spec `specs/2026-10-29-minigames-layout.md`)**
One folder per minigame plus `common/`. The layering rule applies per file role: `*View`/`*Model`/`MiniGameButtons.h` follow the `ui/` rules (no `domain/`, `core/`, `io/`); session classes may use `domain/`/`io/`/`core/GameStates.h` but never `core/GameContext.h` or `core/MenuSession.h`.
| File | Responsibility |
|---|---|
| `common/MiniGame.h` | The shared lifecycle interface: `begin(b1,b2,b3)`, `handleAction(Action)`, `handleTouch(buttonId)`, `draw(Ui&) -> UiResult`. |
| `common/MiniGameHost.h/.cpp` | `ST_MINI_GAME` (12) dispatcher owned by `GameContext`: `start(type,...)` from `EV_MINIGAME`, holds the calling `ScriptThread*` across the vending↔menu round trip, `finish(result)` = vars[7] → Playing → resumeThread; owns the shared help state and sheets plus the concrete games. **(planned, ADR 0044 / G3a)** `playFromMainMenu(type)` + `fromMainMenu()` flag (not `thread == nullptr`): no thread, IQ 0, no rewards; `finish` main tail = sound 1071 + `openMenu(18)`. |
| `common/MiniGameButtons.h` | Verbatim `GetTouchedButtonID` (inclusive rects, list order), highlight and release → `UiAction::MiniGameTouch`. |
| `common/MiniGameAssets`, `MiniGameHelp`, `MiniGameHelpModel.h`, `MiniGameHelpView` | Shared `gameHelpBG.bmp`, plus the `MiniGameManager` help screen port (scroll, 16-line window, scrollbar). |
| `vending/VendingMachine`, `VendingModel.h`, `VendingView`, `VendingAssets` | Vending terminal / help / 4-digit hack game; menu API (prices, buy, `returnFromBuying`, `endGame`) used by `MenuSession` for menus 83-87. |
| `hacking/HackingGame`, `HackingModel.h`, `HackingView`, `HackingAssets` | **(planned, ADR 0038 / spec `hacking-minigame.md`)** `EV_MINIGAME` type 2 column-sort puzzle: board scramble (IQ-scaled), help → game → confirm, raw press-sample touch resolved in the tick; `MiniGameHost` gains `evaluateResults` (IQ +2 HUD message 236/237) and `Env::hud`. |
| `sentrybot/SentryBotGame`, `SentryBotModel.h`, `SentryBotView`, `SentryBotAssets` | **(planned, ADR 0041 / spec `familiar.md` G4)** award discards the owned bot as a world drop + forced help 16. **(planned, ADR 0040 / spec `sentry-bot-minigame.md`)** `EV_MINIGAME` type 0 "Matrix Skip": IQ-scaled skip code (`Player::lastSkipCode`), 4x4 board, help → board → success (bot choice) / failure, draw-time cell/bot selection applied in the tick (ADR 0038), reward = `give(1, w, 1)` bot weapon; familiar system deferred. |

## Wiring (startup → frame)

**Startup** (`main()`, Main.cpp:36): `AppContext::initialize(archive)` (window/backend/input/zip)
→ data loaders (Tables, EntityDefs, Localization, MediaLoader, MapParser→MapData,
Font, Hud, World3D + textures + sky) → construct `GameContext` with pointers to all
subsystems → `app.run()`.

**Per frame** (GameLoop): input poll → accumulate clamped dt → per 15 ms quantum:
clock advance (gameTime only in Playing) → input gate (blockInputTime /
script-block) → action dispatch → globals (`setPlayerPos`, `runScriptThreads`)
→ state tick (Loading two-phase / Playing order / Dying stub) → render once:
camera from player view (+160-unit pull-back), `drawSky`+`drawBSP`, HUD messages, swap.
Movement decisions live in the Playing tick (`traceMove` before commit; leave-events
before the trace; FACE+ENTER+advanceTurn on arrival); E = faced-tile TRIGGER event
first, then door use.

## External dependencies & build

- CMake 3.22, target `DoomIIRPG`, C++17, sources via **GLOB_RECURSE** (reconfigure on add/remove).
- Link: SDL2, ZLIB, OpenGL (+ Apple OpenGL.framework). No GLEW/GLAD.
- Audio: SDL2's audio device only (ADR 0046) — no OpenAL / SDL_mixer.
- All shaders are inline string literals (World3D.cpp:57-95, SpriteBatch.cpp:9-59) and stay **inside the backend** after the split (ADR 0020).
- **(spec `specs/2026-09-02-render-backend-split.md`)** four targets: `dr_render_core` (interfaces + neutral helpers), `dr_render_gl`, `dr_render_sdl`, `DoomIIRPG` (glob filtered with `list(FILTER ... EXCLUDE REGEX "/render/(api|gl|sdl)/")`); backend chosen at startup by `--backend=gl|sdl`.
- **(planned, ADR 0024 / spec `specs/2026-09-11-sokol-gfx-backend.md`)** `dr_render_sdl` is deleted and `dr_render_sokol` joins (glob filter becomes `"/render/(api|gl|sokol)/"`; note `*.mm` is not in the GLOB patterns and must be listed explicitly). New vendored trees `third_party_libs/sokol` (header) and `third_party_libs/sokol-tools-bin` (`sokol_shaders.cmake` + `bin/**`, which must stay next to each other) — **bump both commits together**, each carries a `VERSION.txt`. Shaders are compiled at build time by `sokol_shader()` into `${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/`; **no generated code ever lands in `new_src/`**, and a host without an shdc binary fails at configure time on purpose. The GL context request rises to **4.1 core on every platform** (shdc's lowest desktop target is `glsl410`; macOS caps at exactly 4.1), the compatibility-profile branch and `SDL_GL_DEPTH_SIZE` go away. Apple also links `Metal` + `QuartzCore`.
- zlib only for zip raw-deflate (`inflateInit2 -15`, ZipArchive.cpp:128-140).
- BMP/font hand-rolled; no SDL_image/SDL_ttf.

## Known incomplete spots

1. ~~Dead code path: `GameLoop` never called~~ — resolved 2026-08-23 (phase 5: GameLoop revived as the fixed-step driver; see spec `specs/2026-08-23-phase5-skeleton.md`, ADR 0003).
2. `AppContext::startup()` empty stub (AppContext.cpp:53-55).
3. ~~`Game::useDoorNear` declared but never defined~~ — resolved 2026-08-23 (replaced by `useDoorFacing`, see spec `specs/2026-08-23-fix-doors-sprite-placement.md`, ADR 0001).
4. Debug helper `saveIndexedBmp` unreferenced (World3D.cpp:16-55).
5. Hardcoded test values: fog (Main.cpp:207-209), FOV 290 (:225-227/:380-382), HUD demo fields (Hud.h:111-118), demo weapons mask (Hud.cpp:217-218), fake timestep `+=15` (Main.cpp:283).
6. `Graphics2D::setClip` records but scissor never applied (Graphics2D.cpp:20-24) — callers must clip by hand (`ViewWeapon`); scheduled as GROUP 1 of spec `specs/2026-08-27-ui-layer.md` (ADR 0012). `ViewWeapon`'s hand clipping stays on purpose (deviation D1).
7. Spawn math duplicated for camera and player (Main.cpp:213-228 vs :234-246).
8. `SDL_WINDOW_ALWAYS_ON_TOP` leftover (Window.cpp:55-56).

## ADRs

_See [adr/](adr/):_

- [0001 — Faced-door use without a trace system](adr/0001-faced-door-use-without-trace.md) (2026-08-23)
- [0002 — Faithful swept-capsule collision trace](adr/0002-faithful-player-collision-trace.md) (2026-08-23; amends 0001)
- [0003 — GameContext state machine + standalone ScriptVM](adr/0003-game-context-state-machine-and-script-vm.md) (2026-08-23)
- [0004 — Dialog module and cinematic states](adr/0004-dialog-module-and-cinematic-states.md) (2026-08-24)
- [0005 — Character detection & stacked-billboard rendering boundary](adr/0005-character-detection-stacked-billboards.md) (2026-08-25)
- [0006 — Loot dwell UI without a LootingSystem module](adr/0006-loot-ui.md) (2026-08-25)
- [0007 — Monsters join the entity-driven stacked-character path](adr/0007-monsters-stacked-character-path.md) (2026-08-26; amends 0005)
- [0008 — Combat module placement and monster payload structs](adr/0008-combat-module-and-monster-payload.md) (2026-08-26)
- [0009 — Restore the legacy world viewport (canvas 1,7,478,248)](adr/0009-legacy-world-viewport.md) (2026-08-26; amended 2026-08-26 — deviation D1 refuted, cinematics share the same viewport, aspect 163)
- [0010 — Decompose `GameContext` and `Game` into peer modules with narrow `Env` injection](adr/0010-module-decomposition-and-injection.md) (2026-08-26)
- [0011 — Typed trace result (`TraceHit`) and named legacy encodings](adr/0011-typed-trace-result-and-named-encodings.md) (2026-08-26)
- [0012 — Immediate-mode UI layer with per-frame models, and no game logic in `ui/`](adr/0012-immediate-mode-ui-layer.md) (2026-08-27)
- [0013 — The in-game menu tree is parsed from `menus.bin`, not hardcoded](adr/0013-menu-tree-from-menus-bin.md) (2026-08-28)
- [0014 — World item pickup as a peer subsystem, entities stay passive](adr/0014-world-item-pickup-subsystem.md) (2026-08-29)
- [0015 — Spawn an entity for every sprite with a def (drop the family whitelist)](adr/0015-full-legacy-entity-spawn-rule.md) (2026-08-30)
- [0016 — Crate opening is simulation state, not renderer state](adr/0016-crate-opening-in-the-simulation.md) (2026-08-30)
- [0017 — The projectile system is introduced degenerate-first (`launchProjectile`/`updateProjectile` from day one)](adr/0017-projectile-model-degenerate-first.md) (2026-08-31)
- [0018 — `Entity::pain`/`Entity::died` become one dispatcher pair on `Game`](adr/0018-entity-pain-died-dispatch.md) (2026-08-31)
- [0019 — Blend modes are one table; color modulation is a per-draw `uColorMod` uniform](adr/0019-blend-mode-table-and-color-mod-uniform.md) (2026-09-01)
- [0020 — The graphics backend is a library behind two need-shaped interfaces (`Draw2D`/`Scene3D`)](adr/0020-render-backend-two-interfaces.md) (2026-09-02)
- [0021 — SDL_Render is a full second backend, 3D view included (affine mapping accepted)](adr/0021-sdl-render-second-backend-with-3d.md) — **historical**, superseded by 0024
- [0022 — Fog is a backend-dependent effect (per-pixel on GL, per-vertex on SDL)](adr/0022-fog-is-backend-dependent.md) — **historical**, superseded by 0024 (fog is per-pixel on every backend again)
- [0023 — The affine warp is fought by adaptive tessellation, measured in screen space and cut in clip space (SDL only)](adr/0023-sdl-clip-space-adaptive-tessellation.md) (2026-09-07) — **historical**, superseded by 0024 (sokol gives hardware perspective correction)
- [0024 — sokol_gfx is the single graphics backend; SDL2 stays the window, input and swapchain provider](adr/0024-sokol-gfx-single-backend.md) (2026-09-11; supersedes 0021/0022/0023)
- [0025 — The sokol backend records one frame command list; the GPU is touched only between `sg_begin_pass` and `sg_commit`](adr/0025-frame-command-list.md) (2026-09-11)
- [0026 — F12 frame capture stays OpenGL-only, and `DOOM2RPG_SOKOL_BACKEND=glcore` stays configurable forever](adr/0026-frame-capture-is-gl-only.md) (2026-09-11)
- [0027 — The player is a real, linked entity (`entities[1]`) positioned at its destination tile](adr/0027-player-is-a-linked-entity.md) (2026-09-22)
- [0028 — Monster behaviour is a peer module `MonsterAI`; monster attacks run on `Combat` inside `ST_PLAYING`; death is detected from health](adr/0028-monster-ai-module-and-attack-sequencing.md) (2026-09-22)
- [0029 — The renderer reports what it saw (`ViewVisibility`); the simulation consumes it on the next tick](adr/0029-render-visibility-feedback.md) (2026-09-22)
- [0030 — Runtime sprites use 48 custom slots appended to the map sprite arrays, owned by `GameSprites`](adr/0030-runtime-sprite-slots.md) (2026-09-22)
- [0031 — Visibility bookkeeping runs a CPU port of TinyGL's column-occlusion cull; the GPU draw list is untouched](adr/0031-cpu-column-occlusion-for-visibility.md) (2026-10-28)
- [0032 — Item use: `Player` owns the item switch, effect planes and msg 111; the menu owns turn sequencing](adr/0032-item-use-ownership.md) (2026-10-28)
- [0033 — Minigames live in `new_src/minigames/<name>/` behind one small `MiniGame` interface, dispatched by `MiniGameHost`](adr/0033-minigames-folder-and-interface.md) (2026-10-29)
- [0034 — Per-map world state is an in-memory byte blob in the legacy brief world-record layout](adr/0034-per-map-world-state-in-memory-legacy-layout.md) (2026-10-29)
- [0035 — Map transitions run through a `LoadingManager` port; the old map stays resident until Loading phase 1; `MapData` is swapped in place](adr/0035-map-transition-flow-loadingmanager.md) (2026-10-29)
- [0036 — Automap: seen marks flow through `ViewVisibility` and are applied by `GameContext`; the world pass keeps running under the map; `Automap` is a sibling of `Playing` in the input path](adr/0036-automap-seen-marking-and-state.md) (2026-10-30)
- [0037 — Save files live on disk in the legacy payload layouts plus a CRC trailer; disk is the only store; no main menu yet](adr/0037-save-files-on-disk.md) (2026-10-30)
- [0038 — Hacking column taps are resolved in the tick (release pass in `handleTouch`, drag-leave as pseudo button ids), not in draw](adr/0038-hacking-touch-resolved-in-tick.md) (2026-10-30)
- [0039 — Sniper zoom data on `Game::zoom`, behaviour in `core/ZoomController`; target practice is an in-playing session, not a minigame](adr/0039-zoom-state-in-game-target-practice-in-playing.md) (2026-10-30)
- [0040 — The sentry bot minigame grants a real bot weapon, but the familiar system (deploy/control) is deferred](adr/0040-sentry-bot-reward-without-familiar.md) (2026-10-30; decisions 2, 3, 5 superseded by 0041)
- [0041 — Familiar: state on `Player`, behaviour in `core/Familiar`, `ST_BOT_DYING` as a real state, deferred monster return through one latch](adr/0041-familiar-state-on-player-behaviour-in-core.md) (2026-11-02)
- [0042 — Drop-entity pool: 16 legacy drop entities + 16 sprite slots after the 48 custom ones; `EntityDb` owns the ring, `Game` owns the spawners](adr/0042-drop-entity-pool.md) (2026-11-02)
- [0043 — Front end: main menu in `MenuSession`, character select and prologue as peer sessions, real `backToMain`, capture runs bypass the front end](adr/0043-front-end-flow-and-capture-bypass.md) (2026-11-03; supersedes 0037 §6)
- [0044 — Main-menu sub-screens: help leaves in `MenuSession`, Credits as a peer session, explicit main-menu minigame flag, Comic Book as a delegate of menu id 89](adr/0044-main-menu-subscreens.md) (2026-11-04; extends 0043)
- [0045 — HUD bar buttons: retained state in `core/HudButtons`, touch-only `QuestLog` / `HudNextWeapon`, direct overlay selection](adr/0045-hud-bar-buttons-state-and-touch-only-actions.md) (2026-11-04; retires ui-layer D3)
- [0046 — Sound: own SDL mixer under the legacy channel rules, `Sound::instance()` service, lazy PCM cache](adr/0046-sound-sdl-mixer-singleton-service.md) (2026-11-04)
- [0047 — Particle system: domain pool on `Game`, render-time aging in `render/ParticleRenderer`, world-band placement](adr/0047-particle-system.md) (2026-11-05)
- [0048 — Corpse pulsate halo: per-sprite halo byte from `SceneRenderer`, `Scene3D::setPulsate` device state](adr/0048-corpse-pulsate-halo.md) (2026-11-05)

## Specs

- [2026-08-23 — Fix "doors work incorrectly" + "sprites slightly shifted"](specs/2026-08-23-fix-doors-sprite-placement.md)
- [2026-08-23 — Faithful player collision (swept-capsule trace)](specs/2026-08-23-faithful-player-collision.md)
- [2026-08-23 — Phase 5 skeleton: game-state machine + tileEvents ScriptVM](specs/2026-08-23-phase5-skeleton.md)
- [2026-08-24 — Intro sequence (dialogs v2, cinematics, corpse loot)](specs/2026-08-24-intro-sequence.md)
- [2026-08-25 — Character animation (stacked-billboard humans, walk cycles, squad)](specs/2026-08-25-character-animation.md)
- [2026-08-25 — Interactive loot dwell + loot menu UI (ST_LOOTING)](specs/2026-08-25-loot-dwell-ui.md)
- [2026-08-26 — Monsters join the stacked path + walk-flicker fix (delta on 2026-08-25 character animation)](specs/2026-08-26-monsters-stack-flicker.md)
- [2026-08-26 — Combat Stage 1 (fire pipeline, damage math, monster payload, HUD feed)](specs/2026-08-26-combat-stage1.md)
- [2026-08-26 — Cinematic camera key-0 fix: legacy tri-state startup (delta on 2026-08-24 intro sequence)](specs/2026-08-26-camera-key0.md)
- [2026-08-26 — Combat Stage 1 fixes: facing probe, fire-target election, world viewport + view weapon (supersedes parts of the combat-stage1 spec)](specs/2026-08-26-combat-stage1-fixes.md)
- [2026-08-26 — Decomposition: GameContext + Game split, typed TraceHit, named legacy encodings](specs/2026-08-26-decomposition.md)
- [2026-08-27 — UI layer: immediate-mode framework (`Ui`/`UiState`/models) + migration of HUD, loot list, dialogs, view weapon](specs/2026-08-27-ui-layer.md)
- [2026-08-28 — In-game menu (`ST_MENU`): `menus.bin` loader, `MenuSession`, `MenuView`, the menu scrollbar and the cursor glyph (GROUP 8 of the UI layer)](specs/2026-08-28-menu.md)
- [2026-08-29 — World item pickup: item entity spawn, `Game::touchTile`, `ItemPickup::touched/touchedItem`, `Player::give` legacy semantics](specs/2026-08-29-world-item-pickup.md)
- [2026-08-30 — Shelf pickup (`EV_GIVEITEM` mode 0), full blocking-sprite spawn, crates (open/animate/unlink), `EV_GIVELOOT`](specs/2026-08-30-blocking-crates-shelf-pickup.md)
- [2026-09-01 — Blend-mode table, `uColorMod` modulation, torchiere glow](specs/2026-09-01-blend-modes.md)
- [2026-09-02 — Render backend split: `dr_render_core` + GL and SDL_Render implementations (`Draw2D`/`Scene3D`, backend flag, frame capture)](specs/2026-09-02-render-backend-split.md)
- [2026-09-07 — SDL path: adaptive triangle tessellation against the affine warp (G7.1-G7.3)](specs/2026-09-07-sdl-tessellation.md) — **historical** (the SDL backend is retired, ADR 0024)
- [2026-09-11 — sokol_gfx becomes the graphics backend; the SDL_Render backend is retired (G1-G8)](specs/2026-09-11-sokol-gfx-backend.md)
- [2026-09-22 — Monster attacks: player entity, turn pipeline, attack sequence, player damage + feedback, sight wake, movement, missiles 6/7, death (G1-G8b)](specs/2026-09-22-monster-attacks.md)
- [2026-10-28 — Secret doors (push walls), awardSecret, EV_AWARDSECRET (G1-G3)](specs/2026-10-28-secret-doors.md)
- [2026-10-28 — Use inventory items: confirm 77, `Player::useItem`, status effects/buffs + consumers, Nano Drinks list, HUD buff box (G1a-G3)](specs/2026-10-28-use-item.md)
- [2026-10-29 — Minigames: folder layout, `MiniGame` interface, `ST_MINI_GAME`, `EV_MINIGAME` start/resume, vending machine terminal/hack game/menus 83-87 (G1-G6)](specs/2026-10-29-minigames-layout.md)
- [2026-10-29 — Map change: `LoadingManager` gateway, per-map world state + restart snapshot, loading box, level-stats menu, travel map (G1a-G5b)](specs/2026-10-29-map-change.md)
- [2026-10-30 — Automap: seen data, ST_AUTOMAP, drawing, script ops (G1-G4b)](automap.md)
- [2026-10-30 — Save / load to disk: config, player and world files, Continue/Load/Restart, menu save actions (G1-G5)](save-load.md)
- [2026-10-30 — Sniper scope zoom + target practice: zoom core, scope HUD, zoomed fire trace, `TargetPractice`, target sprite layers, score bar, monster scope crit (G1-G7)](target-practice.md)
- [2026-10-30 — Sentry bot minigame "Matrix Skip": lastSkipCode, stale-v7 fix, assets/model/view, session, host + debug J, bot FIRE intercept + view sprite (G1-G5)](sentry-bot-minigame.md)
- [2026-11-02 — Familiar (sentry-bot control): state + save, drop-entity pool, discard/pick-up, deploy/return, tint/bob, bot HUD, return triggers + menu row, bot combat + view weapon, splash damage, bot death/self-destruct (G1-G11)](familiar.md)
- [2026-11-03 — Front end: difficulty/character plumbing, main menu screens + skin, real backToMain, character select, prologue, logo; intro movie research-gated (G1-G6b)](front-end.md)
- [2026-11-04 — Main-menu sub-screens: Game Help + About, Credits, Mini Games from the menu, Comic Book; Options stays refused (G1-G4b)](main-menu-subscreens.md)
- [2026-11-04 — Sound: SDL mixer + `Sound` service, config volumes, call-site wiring by area (G1-G9)](sound.md)
- [2026-11-05 — Particle system: pool + spawning, renderer, call sites C2/C3/C9-C11/C15/C16 (G1-G3)](particles.md)
- [2026-11-05 — Lootable-corpse pulsate halo (G1)](corpse-halo.md)
