# Familiar (sentry-bot control): presentation and gameplay integration (original RE port)

What the player sees, and how other systems act, while `player->isFamiliar` is set. State, deploy, return, death,
self-destruct, discard and save are in `familiar-core.md`; the minigame that awards the bot is in
`sentry-bot-minigame.md`. Strings are from `strings00.bin` file 0 (split on NUL; 30 = "Exit", 52 = "Menu" as checks).

familiarType from the bot weapon (`src/Player.cpp:2405-2424`): w3 -> 1 (green shooting), w4 -> 2 (green exploding),
w5 -> 3 (red shooting), w6 -> 4 (red exploding). Shooting = types 1/3, exploding = 2/4 (`src/Combat.cpp:695-696`).

## 1. Rendering

### 1.1 Tint and scanlines (GL overlay, not a pixel pass)
- `setFamiliarType` -> `calcViewMode` sets `render->postProcessMode` = 0 (type 0), 1 (types 1/2), 2 (types 3/4)
  (`src/Player.cpp:2495-2518`).
- `postProcessMode != 0` makes `Canvas::renderScene` call `Render::postProcessView` after the world, portal and view
  weapon (`src/Canvas.cpp:1344-1353`). Cinematic camera frames do the same, after the cockpit overlay
  (`src/MayaCamera.cpp:316-321`). So the tint covers the view weapon but not the HUD (HUD is drawn later).
- `postProcessView` overlay, only `if (isFamiliar)` (`src/Render.cpp:2642-2659`), with function statics
  `static float update = 0.f; static int y = 0;` (`:2593-2594`):
  1. if `brightenPostProcess`: white rect (0,0,480,320) alpha `max/2226.0` (`:2644-2646`; `max` from the brighten ramp `:2619-2640`).
  2. per frame: `update += 0.05f; y = (y + 1) % 30;` (`:2647-2648`).
  3. `alpha = sin(update) * 0.05 + 0.35` -> pulses 0.30..0.40, one cycle every ~126 frames (`:2649`).
  4. `(uint16_t)(familiarType - 1) > 1u` (types 3/4, also 0) -> red `(1,0,0,alpha)`, else green `(0,1,0,alpha)`;
     rect `(0, 0, 480, 256)` (`:2650-2655`; the comment `/*320*/` shows the height was cut to the 3D view).
  5. scanlines: `for (i = 0; i < 256; i += 10) fillRect(0, i + y/3, 480, 5, black, alpha 0.15)` (`:2656-2658`):
     26 black 5-px bands every 10 px, scrolling down 0..9 px (y/3) over 30 frames.
- `Graphics::FMGL_fillRect` is plain alpha blending: `a < 1` -> `GL_BLEND`, `glBlendFunc(GL_SRC_ALPHA,
  GL_ONE_MINUS_SRC_ALPHA)`, alpha test `> 0`, texture off, `glColor4f(r,g,b,a)`; rect clipped to `graphClipRect`
  (`src/Graphics.cpp:104-229`).
- The J2ME per-pixel greyscale+channel post-process (mode 1 green-ish, mode 2 red-ish, every 4th row dimmer) is kept
  only as `#if 0` (`src/Render.cpp:2661-2689`). The world itself is NOT greyscaled. Only particles are: when
  `postProcessMode != 0` the particle colour goes through `convertToGrayscale` (`src/ParticleSystem.cpp:100-102`,
  formula `src/Render.cpp:2692-2696`).

### 1.2 Camera
- Eye height: `if (isFamiliar) viewZ += (sinTable[time >> 2 & 0x3FF] >> 11) + 128;` (`src/Render.cpp:2271-2273`), in
  render units (1/16 world unit, since `renderScene` passes `(viewZ << 4) + 8`, `src/Canvas.cpp:1344`): +8 world units
  higher, hover bob +-2 units (sinTable is 16.16), period 4096 ms. Applied after the shake (`:2265-2270`).
- FOV, pitch, turn and step speed: no familiar-specific code (only the dying state uses fov 290, see familiar-core.md §4).
- Portals are off: `checkPortalVisibility` sets `portalState = PORTAL_DNE` and returns false (`src/Render.cpp:2702-2705`).
- Deploy and return each `startFade(400, 2)` (`src/Player.cpp:2104, 2162`); deploy saves player fog, return reloads it
  (`:2103`, `:2160-2161`).

### 1.3 The left-behind body
`Game::spawnPlayerEntityCopy(saveX, saveY)` (`src/Game.cpp:2725-2767`): a drop entity with def = `lookup(tile)` and
sprite tile/frame 0 = 72 (Sarge, default and characterChoice 2), 68 (choice 1 Major), 66 (choice 3 O'Connor); name
string (file 0) 226 "Sgt. Blazkowicz" / 224 "Major Morgan" / 225 "Dr. O'Connor"; z = floor + 32, render mode 0, scale
64, info `|= 0x400000`, linked on its tile. It is the ordinary NPC composite sprite of that tile (NPC tiles 65-80
composite legs/torso/head, `entities.md` §2); there is no animation code for it. Walking into it returns the bot
(`src/MovementController.cpp:333-336`, familiar-core.md §3).

### 1.4 The bot when not deployed
The held bot weapon (3-6) draws as view sprite tile 19 (w3/4) or 18 (w5/6), frame 2 then frame 3 offset down by 1 or
2 px, frame 3 mirrored (`SPRITE_FLAG_FLIP_HORIZONTAL = 0x20000`, `src/Enums.h:1232`) in phases 0, 4-7, 10-12 of a
16-step cycle `(time + tile*1337)/1024 & 0xF` (`src/Combat.cpp:797-813`). There is no "walking back" animation: return
is instant with a 400 ms fade and the camera snaps back (or, when returning by bumping the body, the player is placed
on the bot's tile facing the body; `src/Player.cpp:2133-2157`).

## 2. HUD

- Top bar: `HUD_Panel_top.bmp` then `HUD_Panel_top_sentrybot.bmp` (480x20) over it, and the bevel is skipped
  (`src/Hud.cpp:239-245`, load `:44`). Messages and monster health unchanged (`:246-...`).
- Bottom bar: `gameMenu_Panel_bottom.bmp` then `gameMenu_Panel_bottom_sentrybot.bmp` over it
  (`src/TouchController.cpp:542-548`, load `src/MenuSystem.cpp:119`). Soft-key switch buttons as usual.
- Bot block replaces weapon / armour / health / keys / face (`src/Hud.cpp:644-680`, else branch `:681-710`):
  - colour by `h = ammo[7]`: `> 80` 0xFF59A907, `> 60` 0xFF96DA0E, `> 40` 0xFFEDD703, `> 20` 0xFFFF8B00, else
    0xFFFF0C00 (`:646-662`).
  - HUD button 3 highlighted -> colour 0xFFFFFFFF and `HUD_sentry_active.bmp` (194x38) at (143,261) (`:664-667`).
  - face: `drawRegion(Hud_Sentry.bmp, 0, (type 3/4) ? 17 : 0, 29, 17, SCR_CX - (15+118)/2, 278, anchor 3)` (`:669`);
    `Hud_Sentry.bmp` is 29x34 (green row 0, red row 17). With SCR_CX = 240 (`src/Canvas.cpp:122`) and anchor 3 =
    HCENTER|VCENTER (`src/Graphics.cpp:349-367`) the face occupies x 160..188, y 270..286.
  - bar: `n5 = SCR_CX + face->width - 66` = 203; `segments = h*20/100`, `+1` if `< 20 && h != 0`; each
    `fillRect(n5 + 6i, 271, 4, 14, colour)` (`:671-679`). Full bar spans x 203..321.
  - images loaded in `src/App.cpp:452-453`.
- Damage vignette uses `damage_bot.bmp` (16x16) instead of `damage.bmp` (`src/Hud.cpp:568`, load `src/App.cpp:419-420`).
- `drawHudOverdraw` returns early while familiar (`src/Hud.cpp:627-634`) but its call is commented out (`:745`).
- Soft keys: left 52 "Menu", right 216 "Return"; when holding (not deployed) a bot: right 220 "Discard"; normal 55 "Map"
  (`src/SoftKeyController.cpp:91-98`).
- Touch while familiar (`src/Hud.cpp:1241-1264`): a press inside the active image rect (143..337, 262..300) counts as
  button 3; button 1 (right switch, x 428) -> `ACTION_BOT_DISCARD` (= return), button 3 -> `ACTION_PASSTURN`, button 0
  (left switch) -> `ACTION_MENU`. Weapon / armour / health / key buttons are unreachable.
- Messages: 190 "No room to deploy, step back.", 193 "Sentry Bot cannot open doors.", 194 "Self-destruct?",
  216/217/218/219/221/222 "No Bot Access", 192 "Your Sentry Bot has perished!", 191 "Received all the Sentry Bot's
  items." (call sites in familiar-core.md and below). Healing pickups show the usual 111 "+N health" (`src/Player.cpp:445-450`).

## 3. Combat

- Player attack with a shooting bot: `ce->weapon` stays 3 or 5 (`selectWeapon` returns while familiar,
  `src/Player.cpp:132-137`), so the shot uses weapon row 3/5 of table 2: 3 = STR 12..15, range 0..5, ammoType 7,
  ammoUsage 0, proj 0 (instant bullet), 1 shot, shotHold 35; 5 = STR 20..30, otherwise the same (parsed from
  `tables.bin`, loader per combat.md §10.2). AmmoUsage 0 -> firing does not drain `ammo[7]`; the low-ammo messages are
  skipped for bot weapons (`src/Combat.cpp:213`). Sound 1086 `pistol.wav` (`src/Combat.cpp:261-265`).
- Tile attack-event flag `flagForWeapon`: bot weapons give 0 unless the player is a shooting familiar (then 8192)
  (`src/MovementController.cpp:197-211`).
- View weapon while familiar (`src/Combat.cpp:694-724, 788-795`): exploding types draw nothing; shooting types draw
  weapon 0 (rifle tile 1, its flash frame 3 at wpinfo-0 flash +40/+40) with idle forced to (1,50) and attack
  `(1 + atkX - idleX, 50 + atkY - idleY)` = (6,44); scr bias of bot weapons (+12 y GL; software -4 x, +12 y) still
  applies because the switch keys on `ce->weapon` (`:637-650, :678-692`); then `y -= 35` (software path also x+8, y-7).
- Exploding types: FIRE (when nothing more specific handles it) -> `startSelfDestructDialog()`
  (`src/PlayingInputHandler.cpp:489-491`); details in familiar-core.md §5. Explosion damage weapon 4: 100..120,
  weapon 6: 140..155 (`src/Player.cpp:2264-2276`).
- Monsters: the player entity is ET_PLAYER whose position is `canvas->destX/destY` (`src/Entity.cpp:1378-1381`), i.e.
  the bot. Monster AI has no familiar code, so monsters chase and attack the bot. The body copy is never a target.
  A monster that ends a move orthogonally adjacent to the body (`distFrom <= tileDistances[0]` and exactly one axis
  equal) forces a return (`src/Game.cpp:3217-3225`, familiar-core.md §3).
- Damage to the bot: monster hit = `dmg*7/8` (types 1/2) or `dmg*5/8` (types 3/4), no armour, then the PLAYER's
  defense % still applies (`src/CombatEntity.cpp:356-372`); `pain` -> `addHealth(-n)` on `ammo[7]`
  (`src/Player.cpp:672-674`), pain sound 1111 instead of the hero grunt (`:631-633`). Fire/spike floors do nothing to
  the bot (`src/Entity.cpp:135`). Status effects cannot be added (`src/Player.cpp:1565-1567`).
- XP and kill stats go to the player as usual (`Entity::checkMonsterDeath`, `src/Entity.cpp:395-415`, no familiar check).

## 4. Movement and world interaction

- `attemptMove`: target tile with `mapFlags & 0x10` (and not the body tile) -> message 222 "No Bot Access", no move
  (`src/MovementController.cpp:322-325`). Otherwise normal: tile events run (`:328`), trace, step, relink.
  The engine suppresses no tile events for the bot; scripts decide via v16 (§6).
- Doors: `CanCloseDoor` returns false (`src/Game.cpp:1215-1220`), so open doors stay open. FIRE on a door entity
  (eType 5) as bot: sound 1111 + message 193 instead of opening (`src/PlayingInputHandler.cpp:446-458`).
- Wall push (eType 0/12): plays 1111 instead of the hero sound, rest unchanged (`:465-482`).
- Grabbing a throwable (eType 10 sub 3) is skipped (`:405-428`).
- Pickups: `give()` routes weapons/inventory/ammo to `weaponsCopy`/`inventoryCopy`/`ammoCopy` except ammo 6 (soul
  cube) and forced `b2` (`src/Player.cpp:995-1036`); food heals the bot by 40 or 20 up to 100
  (`src/Entity.cpp:196-207` -> `addHealth`, `src/Player.cpp:422-424`). Bot items refused (`src/Entity.cpp:240`).

## 5. Input

- `InputEventController` drops ITEMS, DRINKS and PDA keys while familiar (`src/InputEventController.cpp:79-91`).
  MENU still works.
- Weapon switch: `selectWeapon` no-ops (`src/Player.cpp:135-137`), so PREV/NEXT do nothing.
- `ACTION_BOT_DISCARD` (key B / right touch button): familiar -> `familiarReturnsToPlayer(false)`; not deployed with a
  bot weapon -> discard (`src/PlayingInputHandler.cpp:77-84`). Automap key is NOT blocked (`:86-99`); deploy itself is
  refused in the automap (`src/Player.cpp:2083-2085`).
- In-game menu while familiar (`src/MenuSystem.cpp:1551-1557`): item 0 Inventory, then inserted "Return to player"
  (menu strings 355/356, action 33 -> `familiarReturnsToPlayer(false)`, `:3256-3259`), then the rest; items 0
  (Inventory) and 6 (Status) disabled; Save is NOT disabled and the camera-active patch is skipped (cf. `:1559-1566`).
  Item list per `ui.md` §11.1.

## 6. Scripts

- v16 = isFamiliar via `updateScriptVars` (`src/Game.cpp:3469`) and `setFamiliar` (`src/Player.cpp:2086`); counts in
  familiar-core.md §8 (about 413 checks in map00-08: 262 `v16 == 0`, 142 `v16 == 1`, 9 compound).
- Typical pattern: a use/terminal event runs its body only on `v16 == 0`; the else branch calls a per-map helper that
  plays 1072/1073 (hero) or 1111 (bot) plus a MESSAGE, e.g. map00 `func_9586` @9586-9618, map01 `func_7946`, map02
  `func_8429` (disasm with `tools/disasm_map_scripts.py`).
- `EV_FORCE_BOT_RETURN` (`src/ScriptThread.cpp:1744-1750`): 4 sites, all before cinematics: map05 @2365 (SCR_BOSS_25,
  after FADE out 2000), map07 @4755, @4832, @5439.
- `EV_DESTROY_PLAYER` hits `ammo[7]` when familiar; arg 0 sets `noFamiliarRemains` (`src/ScriptThread.cpp:1925-1946`).
- Cinematics: a forced return requested during ST_CAMERA is deferred to camera end (`src/MayaCamera.cpp:386-389`).

## 7. Sounds

1108 SentryBot_activate (deploy, `src/Player.cpp:2125`), 1111 SentryBot_pain (pain, door/wall refuse, script
refusals; `:632`, `src/PlayingInputHandler.cpp:456,473`), 1112 SentryBot_return (`src/Player.cpp:2280`), 1109
SentryBot_death (`:746`), 1032 explosion (self-destruct end, `src/GameStateRunner.cpp:293`), 1086 pistol (shooting
bot fire). 1110 SentryBot_discard has no caller in `src/`. Names: `src/Sounds.h` index = id - 1000 (`src/Sound.cpp:180`).

## 8. new_src status (2026-11-02)

None of the presentation exists: no tint, no bob, no bot HUD, no soft-key 216/220, no touch remap, no menu rebuild
(`new_src/core/MenuSession.cpp:458-460`), deploy is a logged stub (`new_src/core/PlayerActions.cpp:339-342`),
vars[16] = 0 (`new_src/domain/game/ScriptVM.cpp:321`), `damage_bot.bmp` absent (`new_src/ui/Hud.cpp:183`). The
not-deployed bot view sprite exists (`new_src/core/GameContext.cpp:977`, `new_src/ui/ViewWeapon.h:31`).
