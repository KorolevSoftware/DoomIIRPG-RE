# Phase 5 skeleton — game-state machine + tileEvents bytecode interpreter

Date: 2026-08-23 · Status: **Approved for implementation** · Owner: architect
Related: [ADR 0003](../adr/0003-game-context-state-machine-and-script-vm.md),
`docs/original-code/game-flow.md`, `docs/original-code/tile-events-vm.md`,
`docs/original-code/doors.md` (§2 R3, §5).

## 0. Goal

Replace the ad-hoc `main()` loop in `new_src/core/Main.cpp` with a faithful
game-state machine (`Loading`, `Playing`, `Dying` stub) and add the
tileEvents bytecode interpreter, giving scripted door unlocking and
move-triggered events on map00. After this phase the binary boots straight
into map00 gameplay, scripts fire from tile triggers (visible on stderr and,
for messages, on screen), and the blue/red door unlock flows execute their
real bytecode.

Everything not listed in §11 (Out of scope) is out of scope even if the
original does it.

## 1. Ground truth (all claims verified against these sources)

| Topic | Source |
|---|---|
| Canvas state machine, setState mutator, 26 states | `docs/original-code/game-flow.md#2` (`src/Canvas.h:68-93`, `src/Canvas.cpp:1020-1217`) |
| Frame order, clocks, clamped dt ≤ 125 ms | `docs/original-code/game-flow.md#1` (`src/Main.cpp:73-142`, `src/Canvas.cpp:745-796`) |
| ST_PLAYING tick order | `docs/original-code/game-flow.md#6` (`src/Canvas.cpp:805-822`, `src/GameStateRunner.cpp:162-201`) |
| Two-phase ST_LOADING ordered pipeline | `docs/original-code/game-flow.md#3.3` (`src/LoadingManager.cpp:604-744`) |
| advanceTurn contract | `docs/original-code/game-flow.md#5` (`src/Game.cpp:1238-1281`) |
| Movement/arrival trigger sites | `src/MovementController.cpp:160-196,214-224,284-310,312-355,377-547`; `src/Game.cpp:974-1014`; `src/PlayingInputHandler.cpp:395-404,445-453` |
| tileEvents data layout + lookup + trigger filter | `docs/original-code/tile-events-vm.md#1,§4.5` (`src/Render.cpp:182-209`, `src/ScriptThread.cpp:59-118`) |
| Thread-pool model (20 threads, state/unpauseTime/-1) | `docs/original-code/tile-events-vm.md#2` (`src/Game.cpp:3246-3295,3324-3365`, `src/ScriptThread.cpp:120-177,2048-2081`) |
| Opcode table + encodings | `docs/original-code/tile-events-vm.md#3` (`src/Enums.h:399-497`, switch `src/ScriptThread.cpp:243-2038`, arg readers `:2095-2119`) |
| EV_DOOROP / setLineLocked | `docs/original-code/tile-events-vm.md#3.3` (`src/ScriptThread.cpp:747-780`, `src/Game.cpp:2477-2498`) |
| map00 traces (blue evt@4161/4768/5192, staticFuncs 0/6) | `docs/original-code/tile-events-vm.md#5` |
| Item kinds IT_INVENTORY=0/IT_WEAPON=1/IT_AMMO=2 | `src/Enums.h:96-98` |
| scriptStateVars[128], refreshed slots | `src/Game.h:129`, `src/Game.cpp:3461-3471` |

## 2. Module layout

New files (**CMake uses GLOB_RECURSE — one `cmake -S . -B build_new`
reconfigure required**):

| File | Class(es) | Responsibility |
|---|---|---|
| `core/GameContext.{h,cpp}` | `GameContext`, `enum class StateId` | The legacy-*Canvas* analog: owns the state machine (`setState` semantics), clocks (`upTimeMs`, `gameTime`, `blockInputTime`), pending input actions, per-state ticks (`tickLoading/tickPlaying/tickDying`), playing-action handlers (move/turn/use), `finishMovement`, render orchestration. Non-owning pointers to all subsystems. |
| `domain/game/ScriptVM.{h,cpp}` | `struct ScriptThread`, `class ScriptVM` | Faithful tileEvents interpreter: 20-thread pool, `executeTile`/`executeStaticFunc`/`runScriptThreads`, big-endian dispatch loop, `scriptStateVars[128]`, event-index iterator, trigger filter. |

Modified files:

| File | Change |
|---|---|
| `core/Main.cpp` | Slimmed: archive/data init (keep loader smoke logs) → construct subsystems → construct `GameContext` → `app.run()` → `app.shutdown()`. Deletes the entire ad-hoc loop (current lines ~252-480) and the spawn/camera placement (moves into Loading tick / render). |
| `core/GameLoop.{h,cpp}` | Revived from dead skeleton: fixed-step driver (15 ms quanta, accumulated dt clamped ≤ 125 ms) calling `ctx.tick()` + `ctx.render(app)`; ESC/SDL_QUIT handling; FPS log kept. |
| `domain/game/Enums.h` | Add: full `EV_*` opcode id table verbatim (`src/Enums.h:399-497`), `EVAL_*` term codes (`:512-522`), `EVFL_*` trigger-mask constants (`:523-547`), `SCR_*` static-func indices + `SCR_NOT_DEFINED=65535` (`:498-511`). |
| `domain/game/Game.{h,cpp}` | Add: `setLineLocked`, `advanceTurn`, `eventFlagsForMovement`/`eventFlagForDirection` + `eventFlags_[2]`, `findEntityBySprite`, `touchTile` stub, fields `monstersTurn/queueAdvanceTurn/skipAdvanceTurn/abortMove/spawnParam`, `const EntityDefs* defs_` (set in `loadEntities`), `DoorAnim::ownerThread` + thread param on `performDoorEvent` (blocking-open resume). |
| `ui/Hud.{h,cpp}` | Add `drawMessages(Graphics2D&, const Font&)`: draws only center-message + important-banner (works while cockpit/HUD stays hidden). |

Unchanged but referenced: `AppContext::run()` → `GameLoop::run(*this)`
(already wired, `AppContext.cpp:61-63`) becomes the live path.

Ownership & lifetime: all subsystems remain stack objects in `main()`;
`GameContext` receives them by pointer in an init struct and never owns them
(single-threaded GL loop; context declared *after* the systems it references
so it dies first). `ScriptVM` likewise holds an env struct of raw pointers.

## 3. State machine

```cpp
// core/GameContext.h
enum class StateId : int {
    Playing = 3,   // legacy ST_PLAYING  (src/Canvas.h:68-93 numbering kept)
    Loading = 7,   // legacy ST_LOADING
    Dying   = 13,  // legacy ST_DYING (stub)
};
```

`Menu/Intro/Combat/Dialog/Automap/Camera/TravelMap/...` are **documented
out-of-scope stubs**: not enumerated, unreachable; when a script requests one
(EV_DIALOG, EV_STARTCINEMATIC…) the request is logged and execution continues
(see §7 TIER-B rule).

`setState(StateId)` mirrors `Canvas::setState` (`src/Canvas.cpp:1020-1217`):

1. `stateChanged = true` (cleared at end of the same tick, `src/Canvas.cpp:987`).
2. Zero `stateVars[9]`.
3. Exit hooks: none needed for the subset; keep an empty `exitState_()` slot
   with comments naming the legacy hooks (AUTOMAP/MENU/CAMERA).
4. `oldState = state; state = s;`
5. Enter hooks:
   - `Playing`: clear pending action queue; `lastTurnTime = upTimeMs`
     (`src/Canvas.cpp:1101-1106` analog).
   - `Dying`: record `deathTimeMs = upTimeMs` (unused this phase).
   - `Loading`: arm loading-phase counter to 0.

## 4. Frame loop and clocks (`GameLoop::run`)

Fixed timestep (task constraint; legacy is variable-rate ~66 fps ceiling —
deliberate deviation, see ADR-0003):

```
acc += min(now - last, 125 ms);  last = now            # clamp (src/Main.cpp:133-135)
while acc >= 15 ms:                                    # one quantum per iteration
    acc -= 15 ms
    tick():
      1. upTimeMs += 15                                # legacy app->upTimeMs/app->time
      2. if (!pauseGameTime && state == Playing) gameTime += 15
                                                       # legacy freezes only menus (src/Canvas.cpp:769-771);
                                                       # subset freezes Loading/Dying too (deviation C5)
      3. input gate: if (game.blocked() || gameTime <= blockInputTime)
              drop pendingActions                      # (src/InputEventController.cpp:449-478)
      4. runInputEvents: dispatch queued actions → handlePlayingAction (Playing only)
      5. globals: game.setPlayerPos(player.viewX, player.viewY);
                  vm.runScriptThreads(gameTime)        # (src/Canvas.cpp:791-796; resumes only in Playing)
      6. switch (state): tickLoading / tickPlaying / tickDying
      7. stateChanged = false
render(app): beginFrame → world (sky+BSP+camera-from-player-view) →
             hud.drawMessages → endFrame(swap)
```

`UpdatePlayerVars` maps to the existing `Game::setPlayerPos`;
`gsprite_update` has no rewrite counterpart (particles absent) — omitted.
`isInputBlockedByScript()` = any inuse thread with `type & EVFL_FLAG_BLOCKINPUT`
(`src/Game.cpp:3447-3459`); dormant unless authored data sets bit 16.

## 5. Loading state (two-phase, gameplay-relevant order)

Media/IO stays eager in `main()` (pragmatic cut, sanctioned by scope item 3);
the Loading tick carries the legacy *ordered tail* split over two frames so
future media streaming has insertion points:

```
phase 0 (frame A):
  [unload-slot: game.unloadMapData()/render.unloadMap() equivalents — no-op on first boot]
  zero scriptStateVars except vars[15]                  (src/LoadingManager.cpp:631-635)
  phase = 1; return                                     # bar frame in legacy (src/LoadingManager.cpp:610-616)

phase 1 (frame B):                                      # (src/LoadingManager.cpp:617-744)
  vm.resetPool()                                        # 20 threads freed (src/Game.cpp:332-360)
  game.loadEntities(map, defs)                          (src/LoadingManager.cpp:658)
  [loadWorldState slot — fresh entry: no-op]            (src/LoadingManager.cpp:670)
  spawnPlayer():                                        (src/Game.cpp:941-972)
     if game.spawnParam != -1: x=sp&0x1F y=(sp>>5)&0x1F dir=(sp>>10)&7
     else: x=map.spawnIndex%32, y=map.spawnIndex/32, dir=map.spawnDir
     view=dest=(tile*64+32), z=getHeight+36, angle=dir<<7&0x3FF, relink
  vars[12] = kDifficultyDefault (2)                     (src/LoadingManager.cpp:692)
  vm.executeStaticFunc(0)                               # SCR_INIT_MAP (src/LoadingManager.cpp:691-693)
  [prevX/Y = view snap — fields not ported (save-only); comment]
  vm.executeTile(x, y, 4081, true)                      # entrance event, ENTER|all-dirs (src/LoadingManager.cpp:700-706)
  player.finishRotation()                               # finishRotation(false) analog (src/LoadingManager.cpp:707)
  game.monstersTurn = 0                                 # endMonstersTurn (src/LoadingManager.cpp:708)
  [uncoverAutomap stub]                                 (src/LoadingManager.cpp:709)
  setState(Playing); pauseGameTime = false;
  blockInputTime = gameTime + 200                       (src/LoadingManager.cpp:712-720)
```

`staticFunc(1)` (completed-game) has no caller in the subset — slot commented.

## 6. Playing state tick (fixed order)

Port of `src/Canvas.cpp:805-822` → `src/GameStateRunner.cpp:162-201`,
restricted to today's features:

```
tickPlaying():
  1. [held-button auto-repeat — n/a, keyboard discrete]
  2. deferred turn: if (queueAdvanceTurn) { queueAdvanceTurn=false; advanceTurn(); }
  3. death check: if (player.getHealth() <= 0) { setState(Dying); return; }   (:170-173)
  4. monster phase: if (monstersTurn != 0) monstersTurn = 0;                   # empty-world
     updateMonsters placeholder — sits at the legacy position                  (:181-183)
  5. game.update(15)                                  # door lerps ≈ updateLerpSprites (:184)
  6. updateView:                                                                 (:377-547)
       b  = (viewX==destX && viewY==destY)          # position idle at frame start
       b2 = (viewAngle==destAngle)                  # angle idle at frame start
       player.updateView()                          # interpolate X/Y/Z/angle
       if (!b && viewX==destX && viewY==destY) finishMovement();                 (:510-512)
       if (!b2 && viewAngle==destAngle) finishRotationFired();                   (:514-516)
  7. camera pull-back + scene draw happen in the render phase (unchanged visuals)
```

`finishRotationFired()` = `player.finishRotation()` (recompute step vectors)
then `vm.executeTile(destX>>6, destY>>6, flagForFacingDir(8), true)` — the
rotation-arrival FACE event (`src/MovementController.cpp:284-310, :307`).

Screen shake / Maya cameras / knockback / zoom / automap snapping are absent
from both sides of the port — positions reserved by order only.

## 7. ScriptVM (faithful interpreter)

### 7.1 Structures

```cpp
struct ScriptThread {                       // layout per tile-events-vm.md PORT CHECKLIST 2
    int IP = 0, FP = 0, stackPtr = 0;
    int unpauseTime = 0;    // ms in gameTime space; -1 = waiting on external resume
    int type = 0;           // trigger mask of the spawning event
    int flags = 0;          // bit0 = block-input capable (alloc b=true), bit1 = queued
    int state = 2;          // 2 alive, 0 dead (out-of-range coords / aborted)
    bool inuse = false;
    int scriptStack[16];    // src/ScriptThread.cpp stack (pop/push :2083-2093)
};

class ScriptVM {
public:
    struct Env {  // non-owning
        MapData* map; EntityDefs* defs; Game* game; Player* player;
        Localization* loc; Hud* hud;
        GameContext* ctx;           // abortMove, message routing
        const int64_t* gameTime;
    };
    short vars[128];                // scriptStateVars (src/Game.h:129)

    void init(const Env&);
    void resetPool();                                   // loadMapEntities-time clear
    int  executeTile(int x, int y, int mask, bool blockInput);
    int  executeStaticFunc(int idx);                    // sentinel 65535 → 0
    void runScriptThreads(int64_t gameTime);
    bool isInputBlockedByScript() const;
    void resumeThread(ScriptThread*);                   // external-resume protocol
private:
    ScriptThread threads_[20];
    int  lastTileEvent_ = -1;                           // Render::lastTileEvent cache
    ...
};
```

Pool rules are verbatim ports: `allocThread` = first free slot, `init()`
sets `stackPtr/IP/FP=0, unpauseTime=0, state=2` (`src/ScriptThread.cpp:2048-2056`);
free = `reset()`; exhaustion logs `ERR_MAX_SCRIPTTHREADS (40)` and returns
nullptr — callers treat nullptr as "nothing ran" (deviation: legacy fatals;
bring-up must survive malformed data, see Conflicts/deviations table).

### 7.2 Event lookup + trigger filter

`findEventIndex(tile)/getNextEventIndex()` linear-scan port including the
`lastTileEvent` packed cache (`src/Render.cpp:182-209`). All matching events
of a tile run in stored order (`src/ScriptThread.cpp:68-84`).

Filter predicate, exact (`src/ScriptThread.cpp:75`):

```cpp
bool eventMatches(int w1, int flags) {
    int n6 = w1 & flags;
    return (w1 & 0x80000) == 0                       // enabled (EVFL_FLAG_DISABLE)
        && (n6 & 0xF)    != 0                        // exec-type intersect
        && (n6 & 0xFF0)  != 0                        // direction intersect
        && (((w1 & 0x7000) == 0 && (flags & 0x7000) == 0) || (n6 & 0x7000) != 0);
}
// SKIP_TURN (0x40000): skipAdvanceTurn=true; queueAdvanceTurn=false; still runs.
```

`executeTile(x,y,flags,b)`: coords outside [0,32) → thread `state=0`, return 0
(`:60-63`); tile `mapFlags & 0x40` missing → return 0 (`:66,87-88`);
otherwise iterate matches, `allocFromEvent(i, flags, b)` (IP =
`(word0 & 0xFFFF0000)>>16`, push frame `[-1,0]`, `type=flags`,
`flags = b ? 1 : 0`, `src/ScriptThread.cpp:155-167`) and `run()` each;
return the last run code.

### 7.3 Dispatch loop — the off-by-one contract

```cpp
uint32_t ScriptVM::run(ScriptThread* t) {
    updateScriptVars();                    // refresh shared vars[] (src/Game.cpp:3461-3471)
    if (t->stackPtr == 0) return 1;
    int n = 1;
    while (t->IP < map.mapByteCodeSize && n != 2) {
        int n2 = 0;                        // command fail flag → vars[7]
        switch (bc[t->IP]) { /* cases below; arg readers pre-increment IP */ }
        vars[7] = n2;                      // (src/ScriptThread.cpp:2040-2042)
        ++t->IP;                           // after EVERY opcode (src/ScriptThread.cpp:2043)
    }
    return n;                              // 1 finished, 2 paused
}
```

Arg readers are big-endian, pre-incrementing (`src/ScriptThread.cpp:2095-2119`):
`u8: bc[++IP]`, `s8: (int8_t)bc[++IP]`, `u16: bc[IP+1]<<8|bc[IP+2]; IP+=2`,
`s16` sign-extended ditto, `i32` four bytes. Consequently **every inline
operand is consumed before the trailing `++IP`, so relative jump offsets and
`CALL_FUNC`'s `IP = target − 1` are relative to "next instruction address − 1"
exactly as in the original** — do not "fix" this.

Per-frame resumption (`runScriptThreads`, `src/Game.cpp:3246-3268` +
`attemptResume :2063-2072`): for each inuse thread with `state==2`:
if `stackPtr==0` → free; else if `unpauseTime==-1 || gameTime < unpauseTime`
→ still waiting; else `unpauseTime=0; run()`. Threads whose `state!=2` are
freed. External resume (`unpauseTime == -1`) happens only by direct `run()`
calls — in this phase the sole producer is a **blocking door open**
(§7.5 EV_DOOROP op0/op1 with `interactive`): `DoorAnim` gains
`ScriptThread* ownerThread`, and `Game::updateDoors` calls
`ownerThread->run()` once when the open animation completes (mirrors
`updateLerpSprites` collecting `callThreads[]`, `src/Game.cpp:2985-3005`),
then clears the pointer.

`evWait(t, ms)` (`src/ScriptThread.cpp:120-137`): `unpauseTime = gameTime + ms`;
if `t->flags & 1` → `blockInputTime = unpauseTime` (ST_CAMERA/AUTOMAP
branches n/a in subset); return 2.

`evReturn(t)` (`:139-153`): pop while `FP < stackPtr-2`; `FP=pop(); ip=pop();`
`ip == -1` → script complete; else `IP = ip`.

`updateScriptVars()` writes **only** the refreshed slots each `run()` entry:
`vars[0]=0` (status effects n/a), `[1]=player health`, `[2]=viewX>>6`,
`[3]=viewY>>6`, `[8]=inventory[24]`, `[14]=0` (character choice), `[16]=0`;
`vars[12]` stamped at load only (`src/Game.cpp:3461-3471`,
`src/LoadingManager.cpp:692`). All other slots persist across scripts.

### 7.4 Opcode tiers

**TIER-A — fully functional.**

| Op | Name | Enc | Behavior (cite) |
|----|------|-----|-----------------|
| 0 | EV_EVAL | `cnt:B, cnt×term:B, off:B` | term bit7→`push vars[t&0x7F]`; bit6→`push (((t&0x3F)<<8\|next)<<18)>>18` (14-bit sign-ext); else AND0/OR1/LTE2/LT3/EQ4/NEQ5/NOT6 — LTE/LT compute `under <= top` / `under < top`, AND/OR require both `==1` (no short-circuit). Final `pop()==0` → `IP += off` (`src/ScriptThread.cpp:244-312`) |
| 1 | EV_JUMP | `S` | `IP += u16` forward-only (`:314-318`) |
| 2 | EV_RETURN | – | evReturn (`:139-153,321-327`) |
| 7 | EV_CALL_FUNC | `S` | `ip=u16; fp=FP; FP=stackPtr; push(IP); push(fp); IP = ip-1` (`:422-431`) |
| 255 | EV_END | – | warn if `stackPtr != 0`; `n=1` (`:2027-2032`) |
| 6 | EV_SETSTATE | `B,s` | `vars[s8] = s16` (`:414-420`) |
| 26/27 | EV_PREV/NEXTSTATE | `B` | `--vars[B]` / `++vars[B]` (`:862-874`) |
| 8 | EV_ITEM_COUNT | `S,B` | `cls=S&0x1F idx=(S>>5)&0x1F dst=B`; cls0→`inventory[idx]`, cls1→`(weapons>>idx)&1`, cls2→`ammo[idx]`; → `vars[dst]` (`:433-454`) |
| 21 | EV_DOOROP | `S` | §7.5 (`:747-780`) |
| 23 | EV_EVENTOP | `S` | `tileEvents[(S&0x7FFF)*2+1] = (w & ~0x80000) | (((S>>15)&1)<<19)` (`:808-816`) |
| 33 | EV_GIVEITEM | `B,B,b` | §7.6 (`:960-1008`) |
| 14 | EV_WAIT | `B` | `evWait(B*100)` (`:587-591`) |
| 16 | EV_ABORT_MOVE | – | `ctx.abortMove = true` (`:663-667`) |
| 3 | EV_MESSAGE | `S` | `text = loc.get(kTextMap /*4+(mapID-1)*/, S&0x7FFF)`; bit15 → important banner else center message; stderr log. Deviation: legacy posts through the hud message-id queue (`:329-342`) |
| 9 | EV_TILE_EMPTY | `S,B` | tile `(S&0x1F,(S>>5)&0x1F)` empty iff every linked entity has `eType==12 (ET_SPRITEWALL)` or `0x6240 & (1<<eType)` (bits: ET_ITEM 6, ET_CORPSE 9, ET_ATTACK_INTERACTIVE 10, ET_DECOR_NOCLIP 13… decode literally from `1<<eType & 0x6240`); result → `vars[B]` (`:456-475`) |
| 52 | EV_ADVANCETURN | – | `game.advanceTurn()` (snap-monsters/endMonstersTurn parts are placeholders) (`:1276-1288`) |
| 24 | EV_HIDE | `B` | `mapSpriteInfo[B] |= 0x10000`; if an entity is bound: unlink + `kInfoActivated`; foundLoot/destroyedObject/corpseify side effects logged only (`:818-844`) |
| 66 | EV_DEBUGPRINT | `B,…` | mode 0: zero-terminated inline chars; mode 1: `vars[…]`; chained while next opcode is 66; output → stderr (`:1510-1531`) |
| 17 | EV_ENTITY_FRAME | `B,B,B` | `info[sprite] = (info[sprite]&0xFFFF00FF)\|(frame<<8)` (frame bits 8–15); entity bound via S_ENT analog → `kInfoActivated` (+ legacy monster anim-freeze deferred until EntityMonster exists); legacy `canvas->staleView` n/a (renderer re-reads info per frame); `time>0` → `evWait(time*100)` (`src/ScriptThread.cpp:669-688`, `new_src/domain/game/ScriptVM.cpp:516-535`) |

**TIER-B — parsed, logged, no-op (args consumed exactly; thread continues).**
Rationale per case: pausing or mutating without the owning system would
corrupt timing or leak threads.

`EV_LERPSPRITE(4)` `B,B,B[,B][,B]` packed-fields reader ported for stride
correctness; `EV_LERPSPRITEOFFSET(59)`; `EV_STARTCINEMATIC(5)` `b`;
`EV_ADV_CAMERAKEY(18)` `B`; `EV_CAMERA_STR(12)` `S,S`;
**`EV_DIALOG(13)` `B,B` — must NOT pause** (legacy `unpauseTime=-1` would
deadlock without a dialog system; log + continue, documented deviation);
`EV_PLAYSOUND(37)`/`EV_STOPSOUND(95)` log `id±1000`;
`EV_FADEOP(32)` `S` log duration/direction; **`EV_CHANGE_MAP(11)` `B,S` —
parse-validate-only**: decode `nextMap=B&0xF, fade=B&0x80, dir=(B>>4)&7,
tile=S&0x3FF`, range-check, stderr log `"[EV_CHANGE_MAP] map=%d dir=%d
tile=%d fade=%d (parse-only)"`, mutate nothing (multi-map out of scope);
`EV_NAMEENTITY(34)`, `EV_SETDEATHFUNC(36)`, `EV_MONSTERFLAGOP(22)`,
`EV_WAKEMONSTER(28)`, `EV_DAMAGEMONSTER(19)`, `EV_DAMAGEPLAYER(20)`,
`EV_MARKTILE(42)`/`EV_UNMARKTILE(82)`, `EV_UPDATEJOURNAL(43)`,
`EV_JOURNAL_TILE(71)`, `EV_SCREEN_SHAKE(48)`, `EV_SPEECHBUBBLE(49)`,
`EV_NPCCHAT(38)`, `EV_GIVELOOT(41)` (count-prefixed entries consumed),
`EV_TURN_PLAYER(69)`, `EV_GOTO(15)` (full `S` consumed; log — teleport
contract needs goto-thread machinery), `EV_GIVE_AUTOMAP(85)`,
`EV_MINIGAME(53)`, `EV_DROPITEM(25)`.

**Anything else**: `fprintf(stderr, "[script] UNIMPLEMENTED opcode %d at
IP=%d\n")` and kill the thread (`state = 0`) — deliberate replacement of the
legacy fatal `Error("Cannot handle event: %d")` (`:2034-2037`) so a stray
opcode cannot end the process during bring-up.

### 7.5 EV_DOOROP (door-critical)

```
args = u16BE;  op = args >> 10;  sprite = args & 0x3FF
interactive = (op & 4) == 0          # state is always ST_PLAYING here
act = op & 3
ent = game.findEntityBySprite(sprite);  if (!ent) return (silent, :753-779)
act 0 OPEN:      if (performDoorEvent(0, ent, interactive ? thread : nullptr)
                     && interactive) { unpauseTime = -1; n = 2; }     # resumed by door-lerp end
act 1 UNLOCK+OPEN: if (ent->isDoor()) setLineLocked(ent,false);  then like 0
act 2 LOCK:        setLineLocked(ent,true);   log sound 1065 if interactive
act 3 UNLOCK:      setLineLocked(ent,false);  log sound 1065 if interactive
```
(`src/ScriptThread.cpp:747-780`.) `performDoorEvent` keeps its current
locked/eSubType refusal; the new third parameter only names the thread to
resume on open-completion (nullptr = fire-and-forget).

### 7.6 EV_GIVEITEM

```
B1, B2, b3 = bytes
if (b3 == 0):  log "sprite-touch give unsupported"; return        # not used by map00 doors
def = defs.lookup(B1)                                             # BY tileIndex (src/EntityDef.cpp:76-83)
if (!def): log Err109; n2 = 1; return
qty = (int8_t)B2
ok = player.give(def->eSubType, def->parm, qty)                   # IT_INVENTORY=0/IT_WEAPON=1/IT_AMMO=2
                                                                  # (src/Enums.h:96-98) match rewrite kinds 0/1/2
if (!ok) n2 = 1
```

Deviation: legacy `qty > 0` spawns a drop item + `touched()` pickup + loot
toast (`src/ScriptThread.cpp:996-1005`); skeleton gives directly (identical
net inventory effect for keycards `give(0, 19/20, ±1)`). Failure convention
`n2=1 → vars[7]` preserved.

### 7.7 setLineLocked (doors.md R3)

```cpp
void Game::setLineLocked(Entity* e, bool locked) {           // src/Game.cpp:2477-2498
    int sp = e->getSprite();
    int info = map_->mapSpriteInfo[sp];
    int tn = info & 0xFF;
    tn = locked ? (tn & 0xFFFFFFFE) : (tn | 0x1);            // bit0 flip: 271↔272, 273↔274 …
    map_->mapSpriteInfo[sp] = (info & 0xFFFFFF00) | tn;
    e->def = defs_->lookup(tn + 257);                        // def re-lookup by tileNum+257
    // entity->name sync (src/Game.cpp:2491-2494) omitted — Entity::name not ported
}
```

The renderer resolves textures from `info & 0xFF` (+257), so the locked↔
unlocked texture swap is automatic. Subsequent plain door use works because
`performDoorEvent` refuses on `def->eSubType == DOOR_LOCKED` and the def just
swapped.

## 8. Triggers (exact masks)

Helpers ported verbatim:

```cpp
int Game::eventFlagForDirection(dx, dy)      // 8-way table (src/Game.cpp:983-1014):
   dx>0: dy<0→NE32 dy>0→SE2048 else E16 · dx<0: dy<0→NW128 dy>0→SW512 else W256
   dx==0: dy>0→S1024 else N64
void Game::eventFlagsForMovement(x0,y0,x1,y1):            // (src/Game.cpp:974-981)
   eventFlags_[0] = 2 | dir(x1-x0, y1-y0);                // LEAVE mask for source tile
   eventFlags_[1] = 1 | dir(x0-x1, y0-y1);                // ENTER mask for destination tile
int flagForFacingDir(i):                                  // (src/MovementController.cpp:214-224)
   a = destAngle; if (i == 4) a += 512;
   return (i==4 || i==8) ? i | (1 << (((a & 0x3FF) >> 7) + 4)) : 0;
```

Wired sites:

| Site | Mask | Order contract |
|---|---|---|
| Move commit (forward/back) | leave `2\|dir` on source tile | BEFORE `traceMove`; then `abortMove` checked; then trace; commit sets dest (`src/MovementController.cpp:326-350`) |
| Arrival | FACE `8\|facing` then ENTER `1\|reverse-dir` on destination, then `advanceTurn` (unless `skipAdvanceTurn`) | `finishMovement` (`src/MovementController.cpp:160-196`) |
| Rotation arrival | FACE `8\|facing` on destination tile | `finishRotation(true)` (`:284-310, :507`) |
| Use/Fire (E key) | `flagForFacingDir(4)` on faced tile `(destX+stepX)>>6,(destY+stepY)>>6` | FIRST, before door use; if any thread ran (`result != 0`) and `!skipAdvanceTurn` → `advanceTurn()`, else fall through to door logic (`src/PlayingInputHandler.cpp:395-404,445-453`) |
| Map spawn | `4081` (ENTER\|all dirs) on spawn tile | Loading phase 1 (`src/LoadingManager.cpp:700-706`) |
| Per-turn | `executeStaticFunc(6)` | inside `advanceTurn` after door sweep (`src/Game.cpp:1271-1279`) |
| Init | `executeStaticFunc(0)` | Loading phase 1 |
| SCR_ITEM_PICKUP(11) | pickup system absent — site documented, no caller this phase |

Plain-door fallback under E: reuse `useDoorFacing`; extend it to return an
outcome `{None, Opened, Locked}` — locked prints the legacy refusal to stderr
(`hud->addMessage(44)` analog, `src/PlayingInputHandler.cpp:447-449`) and
opened consumes the turn via `advanceTurn()`.

## 9. advanceTurn (Game, subset of `src/Game.cpp:1238-1281`)

```cpp
void Game::advanceTurn() {
    queueAdvanceTurn = false;                    // (:1240)
    pushedWall = false;                          // field kept, no consumer
    // player->advanceTurn(): stats/effects ticking — placeholder (no status effects yet)
    // updateBombs(): no bombs — placeholder
    monstersTurn = 1;                            // arm (:1257-1264); tick step 4 disarms
    advanceTurnDoors();                          // existing auto-close sweep (:1271-1278)
    vm->executeStaticFunc(6);                    // PER_TURN (:1279)
    // startRotation(true) pitch refresh — no pitch in rewrite: no-op
}
```

`Game` gains a `ScriptVM* vm_` back-pointer set after construction
(forward-declared in `Game.h`, included only in the .cpp — no header cycle).

## 10. Per-file edit list (checklist)

1. `domain/game/Enums.h` — constants (EV_*, EVAL_*, EVFL_*, SCR_*).
2. `domain/game/ScriptVM.{h,cpp}` — new (threads, pool, dispatch §7).
3. `domain/game/Game.{h,cpp}` — `defs_`, `vm_`, `setLineLocked`, `advanceTurn`,
   `findEntityBySprite`, `eventFlags_*/helpers`, `abortMove`,
   `monstersTurn/queueAdvanceTurn/skipAdvanceTurn`, `spawnParam`,
   `DoorAnim::ownerThread` + `performDoorEvent(n, door, ScriptThread*)`
   (default param keeps existing callers compiling), resume-on-complete in
   `updateDoors`, `useDoorFacing` outcome enum.
4. `core/GameContext.{h,cpp}` — new (§3-§6, §8 handlers, render orchestration).
5. `core/GameLoop.{h,cpp}` — real loop (§4).
6. `core/Main.cpp` — slim (§2).
7. `ui/Hud.{h,cpp}` — `drawMessages`.
8. Optional debug aid (marked `// PHASE5 DEBUG`, removable): pressing `K`
   grants `inventory[19] = inventory[20] = 1` (red/blue keycards) so the
   positive unlock path is testable without the loot system.

Build note: after adding files run `cmake -S . -B build_new` once (GLOB).

## 11. Out of scope (explicit)

Menus/intro/character select/story screens; combat & dialog handlers
(EV_DIALOG logs only); monster AI/entities (death/familiar checks are
position-only placeholders); automap uncover; save/restore (BRIEF/FULL
files); TRAVELMAP; loot UI / touched()-based pickups / drop-item spawning;
sound playback (log-only); multi-map change (EV_CHANGE_MAP parse-only);
performance work; HUD cockpit/weapon-select re-enable; held-key auto-repeat;
zoom; screenshake/fades rendering; EV_GOTO teleport machinery.

## 12. Implementation groups (each ends build-green)

**G1 — VM core (no behavior change yet).** Items 1, 2, 3.
Checkpoint: builds; existing binary behaves identically (Main untouched);
a temporary self-test in Main (behind `#ifdef`, deleted in G3) decodes
map00 evt@4161 and prints the expected opcode walk
(EVAL/ITEM_COUNT/EVAL/EVENTOP/GIVEITEM/DOOROP/WAIT/DOOROP/JUMP/MESSAGE…).

**G2 — machine + loop swap.** Items 4, 5, 6, 7.
Checkpoint: boots Loading→Playing with identical visuals; movement, turns,
door use/auto-close, collision unchanged (regression gate); ESC quits.

**G3 — trigger wiring + logging polish.** Item 8 + remove the G1 self-test;
wire §8 masks into the movement/use paths; entrance event + staticFunc(0).
Checkpoint: stderr shows script traffic while walking; acceptance pass §13.

**G4 — acceptance & docs.** Full §13 checklist with the user; journal/status
updates by implementer.

## 13. Acceptance criteria + manual verification (user is the eyes)

| # | Criterion | How to verify |
|---|---|---|
| A1 | Boots directly into the map00 3D view (Loading→Playing), no crash | launch; screen shows the familiar corridor at spawn (4,19) facing E |
| A2 | Boot stderr shows, in order: `[load] staticFunc(0)`, `[script] executeTile(tile=4,19 mask=0xFF1)`, `[load] -> ST_PLAYING (blockInput 200ms)` | read stderr |
| A3 | First ~200 ms ignores movement input, then walks | tap arrows immediately after launch |
| A4 | Movement/turn/collision/doors behave exactly as before the change | walk around, open a plain door, step away, watch auto-close |
| A5 | Walking onto/off event tiles logs `[script] executeTile(...)` with plausible masks | cross tiles near (13,19)/(10,19) and read stderr |
| A6 | Approaching the BLUE door (10,19) and pressing E toward it fires the TRIGGER script: stderr shows `ITEM_COUNT inv[20]=0`, then the locked branch: `MESSAGE` with the game's locked string shown on screen (center message) | stand west of the blue door, face it, press E |
| A7 | Same at the RED door (16,19): `inv[19]=0` → locked message (str 126) | stand west of the red door, press E |
| A8 | With debug keys (K): E at blue door runs the unlock flow — stderr `EVENTOP disable event[60]`, `GIVEITEM def=111`, `setLineLocked sprite=22 -> unlocked`, `WAIT 300`, `DOOROP open (blocking)`; door visibly splits open; afterwards plain E opens/closes it | K, then E at the blue door |
| A9 | Red door via debug keys behaves the same (sprite 65, def 110) | K, E at red door |
| A10 | Story-unlock tile (17,18) facing N fires evt@5192 ending in `DOOROP sprite=65 op=1` **if reachable**; if the INIT_MAP-locked normal door at (13,19) blocks the route, the block is reported, not worked around | attempt walk east along row 19; read stderr |
| A11 | EV_CHANGE_MAP / EV_DIALOG / camera ops encountered in cinematic chains are logged and skipped without hanging the game | attack-use toward door tiles to trip story events |
| A12 | ESC exits cleanly (no GL/SDL errors on exit) | press ESC |
| A13 | 20-thread pool never exhausts (no `ERR_MAX_SCRIPTTHREADS` in stderr during a session) | play several minutes, trigger events repeatedly |

## 14. Flagged documentation conflicts (found during this spec's research)

- **C1 (doc bug, no code fix):** `docs/original-code/tile-events-vm.md` §1.1
  states `src/LoadingManager.cpp:474` reads an extra childOffset marker that
  real files lack. Actual source (`src/LoadingManager.cpp:473-475`) reads
  childOffset1/childOffset2 back-to-back with a single following marker —
  same as the files and `tools/map_to_obj.py`. `new_src/domain/world/
  MapParser.cpp:67-71` therefore **already parses correctly; no parser fix**.
  Researcher should amend the doc sentence.
- **C2 (stale audit note):** architecture README / doors.md R2 describe the
  rewrite's door search as "nearest within 3 tiles"; current code is
  own-tile + faced-tile only (`Game.cpp:80-96`). This spec's §8 keeps the
  faced-tile behavior.
- **C3:** legacy loop is variable-rate with a 15 ms floor; the task mandates
  a fixed 15 ms timestep — accepted deviation (ADR-0003).
- **C4:** rewrite `Player::give(kind,…)` numbering coincides with legacy
  `IT_INVENTORY/IT_WEAPON/IT_AMMO` (0/1/2) — relied upon by EV_GIVEITEM;
  keep the comment in ScriptVM.cpp warning against renumbering.
- **C5:** legacy freezes `gameTime` only in menus; this phase freezes it in
  Loading/Dying as well (sanctioned simplification; revisit when menus land).
