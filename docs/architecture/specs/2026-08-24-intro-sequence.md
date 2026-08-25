# Spec 2026-08-24 — Intro sequence (dialogs v2, cinematics, corpse loot)

Status: READY FOR IMPLEMENTATION
Goal: the map00 opening plays like the original — boot beats → help dialog on
spawn tile → new-game cinematic tail (camera fly-over, door ops between
handshakes) → control; NPC chain at tile (9,19) gives the pistol through styled
dialog boxes; corpse tutorial loots via the action key.
Sources of truth (normative where this file says "per doc"):
- `docs/original-code/dialog-system.md` (styles §1, text §2, geometry §3,
  input §4, threading §5, map00 recipe §6, PORT CHECKLIST)
- `docs/original-code/cutscenes-camera.md` (states §1, opcodes §2, maya cams
  §3, player-by-script §4, fades §5, boot trace §6, PORT CHECKLIST)
- `docs/original-code/loot-inventory.md` (corpses/lootsets/pickup/inventory)
Targets: extension of GameContext/ScriptVM/Hud + new files listed below.

---

## 0. Current foundation (do not re-do)

Phase 5 skeleton is live: GameContext states {Loading=7, Playing=3, Dying=13};
ScriptVM pool with EV_DIALOG-lite (gray panel), WAIT/-1 park protocol,
GIVEITEM, tile triggers (leave/FACE+enter/spawn-tile/staticFuncs); DoorAnim
ownerThread resume; lazy sprite textures; Hud demo weapon rendering +
showDialogMessage gray panel (to be REPLACED by real dialogs).

## 1. Module additions

| File | Responsibility |
|---|---|
| `domain/game/DialogSystem.{h,cpp}` (new) | Box styles table, composeText (%NN args), wrap/paging, typewriter clock, input handling, thread park/resume hooks. Owns no GL state — draws through Hud/Graphics2D. |
| `core/MayaCamera.{h,cpp}` (new) | Runtime for one camera: load from MapData::mayaCameras entry, `Update(activeKey, dt)` interpolation per cutscenes-camera.md §3 (7 channels, -2 inherit, 125 Hz tweens, shortest-arc angles), `Snap()`. |
| `core/GameContext.cpp/.h` | ST_CAMERA(18)/ST_INTER_CAMERA(4) states; setupCamera/Snap handshake; gotoThread park for GOTO; fade overlay member; dialog state routing. |
| `domain/game/ScriptVM.cpp` | New opcodes: STARTCINEMATIC(5), ADV_CAMERAKEY(18), CAMERA_STR(12), GOTO(15), TURN_PLAYER(69), FADEOP(32), MAKE_CORPSE(?id per loot doc), ASSIGN_LOOTSET(id per loot doc); DIALOG(13) upgraded to full system. |
| `ui/Hud.{h,cpp}` | drawDialog(style, title, lines, page info) replacing showDialogMessage lite; weapon bar wiring for owned pistol; keycard indicator. |

## 2. GROUP 1 — DialogSystem v2

Follow dialog-system.md PORT CHECKLIST items 1–8 exactly:
1. ST_DIALOG becomes a real state in GameContext: on enter clears soft keys;
   ALL input routes to DialogSystem::handleInput; close restores PRIOR state
   (INTER_CAMERA > CAMERA > PLAYING priority) and runs parked `thread->run()`.
2. Styles table verbatim fills: 1/6/14 blue `0xFF002864` (+tail arrows),
   2 gray-black + header strip `0xFF666666`, 8 green gradient `0xFF005617`
   (+portrait row = characterChoice−1 IF a portrait asset exists in Hud
   textures, else colored header only — documented fallback), 9 terminal
   black/green, 4 loot gold/green. EV_DIALOG operands: B,B → style = lo nibble,
   flags = hi nibble (src/ScriptThread.cpp:547-578).
3. Speaker/title = first `|`-line rendered as centered header (styles 2/16/9);
   style 4 shows dialogItem name bar.
4. Text pipeline: STRINGID `(map<<10)|idx` against kTextMap type; %NN args via
   a 50-slot arg pool (port addTextArg/composeText semantics, src/Text.cpp);
   wrapText at (W−2)/9 chars with retry (W−9)/9 (existing rewrite wrapText is
   the base — extend, don't duplicate); pages: viewLines {default 4, styles
   2/8→3}; typewriter reveal 25 ms/char.
5. Input keymap per §4: FIRE(E)=reveal-complete→page→close; UP/DOWN scroll or
   var4 choice cursor; LEFT/RIGHT page-jump (or choice toggle when flags&5);
   PASSTURN/AUTOMAP = skip-close; BACK swallowed. Movement keys never leak to
   gameplay while modal.
6. Threading: keep unpauseTime=−1 park; chained EV_DIALOG re-parks after each
   close; style-2 help FIFO (16 slots, auto-dequeue when playing & idle);
   queueAdvanceTurn side-effects per style table; sound ids logged only.
Acceptance G1: replaying evt[54] chain (str21 s1 → str22 s1/str23 s8 → str24 s8
→ str25 s1 + pistol give → str26 s8/s1) shows correct box colors/titles/pages
and ends with pistol owned.

## 3. GROUP 2 — Cinematics (ST_CAMERA + MayaCamera runtime)

Per cutscenes-camera.md:
1. Add states Camera=18, InterCamera=4 (legacy numbering) to GameContext enum;
   transitions EXACTLY as §1 table (EV_STARTCINEMATIC → setupCamera() +
   ST_CAMERA; Snap() at end-of-keys → ST_PLAYING; dialogs restore prior).
2. MayaCamera runtime: constructor takes MapData::mayaCameras entry (already
   parsed — VERIFY parser covers keys[7]/tweenIndices/tweens fully; fix if
   not); `Update(activeKey, elapsedMs)` implements the 125 Hz tween sampling
   and channel interpolation incl. `-2`=inherit-player-pose sentinel and
   shortest-arc angle math (§3 formulas are normative); `Snap()` jumps to last
   key. Camera output feeds render the same way Camera3D does during ST_CAMERA
   (build MVP from maya pose; FOV per doc: 315 cinematic /290 gameplay).
3. Opcodes: STARTCINEMATIC(5) args→setupCamera(camId); ADV_CAMERAKEY(18)
   resume-count handshake (thread parks; Snap() resumes that many parked
   threads — port the callThreads[] collection order, src/Game.cpp:2985-3013);
   CAMERA_STR(12) subtitle packed u16 → HUD center text (documented deviation:
   same look as legacy subtitle if trivial, else center message); GOTO(15):
   write destX/Y/Z(+destAngle per operand), zero pitch/roll, park thread on
   `gotoThread`, resume + destination events + relink on arrival (reuse
   finishMovement path); TURN_PLAYER(69): destAngle ± operand then normal
   rotation glide. FADEOP(32): fullscreen fade overlay (color/alpha/duration
   per doc §5) drawn in GameContext::render — include if ≤ ~40 LOC else defer
   with a TODO comment (decide during implementation, document choice).
4. Skip semantics: action key after cinUnpauseTime (1000 ms) sets
   skipCinematic → snap lerps/camera keys, fast-forward thread (huge
   timestamp), clear subtitles, 500 ms fade-in (doc checklist item 7).
Acceptance G2: boot replays new-game tail (var5=1 → cam=4 fly-over, FADEOP OUT
500 ms, door ops at ADV_CAMERAKEY boundaries) then hands control at spawn room;
skip shortens it cleanly without softlocks.

## 4. GROUP 3 — Corpse + minimal loot

Per loot-inventory.md:
1. Opcode MAKE_CORPSE (id per doc §1): spawn/convert entity per its effects
   (sprite swap etc.), flagged lootable.
2. Opcode ASSIGN_LOOTSET: store set id on target entity (roll rules per doc).
3. Pickup: faced-entity action (E) on lootable corpse within legacy distance →
   roll/transfer entries into Player inventory/ammo/weapons via existing give()
   paths → HUD feedback line ("You got the key-card" analog via message queue)
   → consume/remove per legacy rules. Full ST_LOOTING UI deferred unless the
   intro needs a choice UI (it does not per doc — single-entry sets).
Acceptance G3: tutorial corpse ahead of spawn loots by pressing E once;
blue key-card lands in inventory (visible via later door unlock working).

## 5. GROUP 4 — HUD wiring + boot sanity

1. Weapon bar: given pistol sets weapons bit + activeWeaponDef → existing Hud
   weapon rendering shows it (remove dependence on demo cycling for this item;
   demo fields stay for other widgets).
2. Keycard indicator appears when red/blue card enters inventory (Hud already
   renders keys_ demo field — wire real inventory[19]/[20]).
3. Boot sanity: staticFunc(0) → spawn-tile event ordering unchanged; assert no
   double-fires across state changes (Loading→Playing→Camera→Playing).

## 6. Implementation order

G1 Dialogs → build; G2 Cinematics → build; G3 Loot → build; G4 HUD → final
build. Reconfigure once (`cmake -S . -B build_new`) after new files appear.
Each group: compile clean, smoke boot reaches ST_PLAYING (or through
cinematic), no regressions to doors/collision/turns.

## 7. Manual verification checklist (user = eyes)

- [ ] Boot: help dialog on spawn tile → cinematic fly-over (camera moves,
      subtitles) → control handed at spawn room; ESC skips cleanly.
- [ ] Walk east: NPC chain fires with GRAY/GREEN box colors matching the
      original, speaker titles shown, typewriter reveal, E steps pages.
- [ ] Pistol received: weapon bar updates.
- [ ] Corpse tutorial: E on corpse → loot feedback → blue key-card owned.
- [ ] Blue door still unlocks/opens/closes as before (regression).
- [ ] No turn freezes; no input deadlocks inside dialogs/cinematics.
- [ ] ESC exits cleanly from any state.

## 8. Out of scope (explicit)

Monsters/AI/combat; automap uncover; save/load; real EV_CHANGE_MAP travel;
PDA/journal UI; character selection; audio playback (ids logged); portraits if
asset missing (fallback documented); var4 Yes/No widgets UNLESS the map00 intro
chain requires them (check dialog-system.md §6 — current reading: it does not).

## 9. Known conflicts / notes

- Rewrite currently has NO ST_DIALOG state (lite was modal flag) — G1 replaces
  the flag with the real state; ensure ScriptVM's DIALOG site switches to
  DialogSystem API and dialog-lite code is deleted, not left dual-path.
- Maya parser completeness must be verified BEFORE G2 (cutscenes-camera.md §3
  layout vs MapParser.cpp mayaCameras block).
- characterChoice: rewrite has no selection screen — treat as 0 (first marine)
  for portrait row; document.
