# 2026-09-22 — Player receiving damage (feedback + death flow)

## Hypothesis / questions
1. `Player::pain` / `painEvent` / `calcDamageDir`: armor formula, health deduction,
   god mode, feedback (weapon brightred, damage flash, direction indicator, shake,
   face, sounds) with constants.
2. How the feedback is drawn.
3. `Player::died` and `ST_DYING`: what is shown, durations, continuation.
4. Other damage sources on the same path.

## Method
Read `src/Player.cpp:412-452,576-752,1933-1946`, `src/Combat.cpp:427-612,690-712,
948-1056,1110-1140,1290-1297,1375`, `src/CombatEntity.cpp:58-89,300-378`,
`src/Hud.cpp:493-620,636-822`, `src/Canvas.cpp:80-135,1000-1160`,
`src/MovementController.cpp:381-389`, `src/Render.cpp:1900-1960,2225-2270,2527-2587,
2962-2968`, `src/GLES.cpp:610-650`, `src/Graphics.cpp:300-452`, `src/Image.cpp:54-175`,
`src/GameStateRunner.cpp:20-60,160-175,257-300`, `src/InputEventController.cpp:410-425`,
`src/MenuSystem.cpp:35-75,3035-3111,4005-4035`, `src/LoadingManager.cpp:38-52,753-760`,
`src/ScriptThread.cpp:700-744,1925-1945`, `src/Entity.cpp:120-150`.
Asset checks (read-only, inline Python over the IPA, per the loader code):
BMP headers/pixels of `damage.bmp`, `damage_bot.bmp`, `Hud_Attack_Arrows.bmp`;
`menus.bin` rows 16 and 51 (format `src/MenuSystem.cpp:46-73,4005-4035`);
`strings.idx` + `strings00.bin` for string ids (format `src/Resource.cpp:169-207`,
`src/Text.cpp:179-212`).

Note: the shell `grep` in this environment is a wrapper that silently skipped
`src/Graphics.cpp`; `/usr/bin/grep` was used to re-verify all searches.

## Verdict
CONFIRMED (all four), with corrections to the premises:
* Armor absorption is not in `Player::pain`; it is in `CombatEntity::calcDamage`
  (`src/CombatEntity.cpp:356-359`) and applied via `addArmor(-totalArmorDamage)` in
  `Combat::explodeOnPlayer` (`src/Combat.cpp:981`).
* Monster hits cause NO screen shake in this port: `painEvent(…, true)` calls
  `startShake(0, 0, 150)` (vibration only, `[GEC]`, `src/Player.cpp:647-649`);
  the camera "rock-back" (`rockView(200, …)`, `src/Combat.cpp:1005-1016`) is the
  visible jolt.
* There is no full-screen red flash and no hurt-face portrait.

## Evidence (key quotes)
* `src/Player.cpp:654` `if (this->god) { return; }`
* `src/CombatEntity.cpp:357-358` `armorDamage = std::min(((171 * dmgStrMin >> 8) + 1) / 2, ce2->getStat(Enums::STAT_ARMOR)); damage = dmgStrMin - 2 * armorDamage;`
* `src/Combat.cpp:959-964` `damageTime = app->time + 150` (more shots) / `+ 1000`.
* `src/Combat.cpp:988` `this->totalDamage += (this->totalDamage >> 1) + loadMapID / this->weapons[this->attackerWeapon + 7];` (difficulty != 1).
* `src/Player.cpp:645` `app->canvas->startShake(500, 2, 150);`
* `src/Player.cpp:729` `app->canvas->startShake(350, 5, 500);`
* `src/Hud.cpp:509` arrow mask `(1 << this->damageDir & 0xC1)`; `:510` `y = screenRect[3] - 20 - 25`.
* `src/Hud.cpp:538` vignette `app->time < this->damageTime && this->damageCount >= 0 && app->combat->totalDamage > 0`.
* `src/Hud.cpp:580` bottom strip at hard-coded `(256 - width)`.
* `src/Combat.cpp:710-711` weapon `RENDER_FLAG_BRIGHTREDSHIFT`; `src/GLES.cpp:628` `glColor4f(1.0f, 0.5, 0.5, 1.0f)`.
* `src/GameStateRunner.cpp:261-279` dying: 750 ms fall, `startFade(2000, 1)`, at 2750 ms `setMenu(MENU_INGAME_DEAD)`.
* `src/Canvas.cpp:1024-1026` `stateVars[i] = 0` on every `setState` ⇒ `src/InputEventController.cpp:422` skip never fires.
* IPA: `damage.bmp` 16×16 8bpp, red dither dense at x=0 fading to magenta at right;
  `Hud_Attack_Arrows.bmp` 12×36 4bpp.
* `menus.bin` menu 51: type 6; items (str 169 flags 9), (0), (170, action 3), (171, action 12),
  (172, action 5) = "Try Again?", "", "Load Saved Game", "Restart Level", "Main Menu".
  Menu 16 difficulty params: 1 Normal, 2 Difficult, 4 Nightmare.
* Strings file 0: 69 "%01 missed!", 71 "%01 dam-age!", 72 "De-flect-ed!",
  73 "Wea-pon De-flect-ed!", 111 "Gain-ed %01 health", 112 "%01 did %02 dam-age!",
  113 "Near Death!", 114 "Low Health!", 131 "Re-flect-ed %01 dam-age!".
* Sounds (`src/Sounds.h` index = id − 1000, `src/Sound.cpp:180`): 1090-1092
  `player_death1..3.wav`, 1093/1094 `player_pain1/2.wav`, 1111 `SentryBot_pain.wav`,
  1032 `explosion.wav`.

Full curated write-up: `docs/original-code/player-damage.md`.

## Open questions
* During `ST_DYING` the HUD is not repainted after the first frame
  (`hud->repaintFlags = 32`, `src/GameStateRunner.cpp:260`). Whether the bars then
  show stale pixels or black depends on the back-buffer clearing policy of the GL
  port — needs the user's eyes on the legacy build.
* `recentBriefSave` semantics beyond `(saveType & 0x20) != 0`
  (`src/Game.cpp:2064`) were not traced.
* Exact on-screen orientation of rotated vignette tiles (rotateMode 1/3 under the
  y-down ortho) was not verified visually; base image is the left-edge strip.
