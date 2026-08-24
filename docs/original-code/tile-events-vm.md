# Tile Events / Map Bytecode VM (`tileEvents`, `mapByteCode`, `ScriptThread`)

> Status: COMPLETE — every claim cites `src/<file>:<line>`; map00 claims additionally verified against
> `tmp_map00.bin` (repo root, parsed read-only; method in `docs/research/2026-08-23-tile-events-vm.md`).
> Purpose: give the rewrite everything needed for a faithful interpreter.
> Focus use case: scripted door unlocking on map00 (EV_ITEM_COUNT inventory check → EV_DOOROP unlock/open of red/blue doors).

## 1. DATA LAYOUT

### 1.1 Where the data lives in `mapXX.bin`

Map header counts (`src/LoadingManager.cpp:383-384`):

```cpp
render->numTileEvents = (int)app->resource->shiftShort();
render->mapByteCodeSize = (int)app->resource->shiftShort();
```

Allocations (`src/LoadingManager.cpp:450-451`): `tileEvents = new int[numTileEvents * 2]`, `mapByteCode = new uint8_t[mapByteCodeSize]`.

Tail block order (`src/LoadingManager.cpp:542-553`), each block preceded by a 4-byte marker that the reader skips without validating (`Resource::readMarker`, `src/Resource.cpp:84-90`):

1. `staticFuncs`: **12 × u16 LE** (`readUShortArray(..., render->staticFuncs, 0, 12)`) — `src/LoadingManager.cpp:542`.
2. `tileEvents`: **numTileEvents × 2 × i32 LE** (`src/LoadingManager.cpp:544`).
3. `mapByteCode`: mapByteCodeSize bytes (`src/LoadingManager.cpp:552`).

All multi-byte values in the map file are little-endian (`shiftShort/shiftUShort/shiftInt`, `src/Resource.cpp:136-152`). After loading, every referenced tile gets flagged:

```cpp
for (int i = 0; i < render->numTileEvents; i++) {
    int index = render->tileEvents[i << 1] & 0x3FF;
    render->mapFlags[index] |= 0x40;
}
```
(`src/LoadingManager.cpp:546-549`). Flag `0x40` = "tile has events"; it gates all execution (`src/ScriptThread.cpp:66`, `src/Game.cpp:3312`). Map grid is fixed 32×32; linear tile index = `(y * 32) + x` (`src/ScriptThread.cpp:65`, `src/Game.cpp:3311`); x/y outside [0,32) aborts silently (`src/ScriptThread.cpp:60-63,96-98`).

File-walk caveat verified against `tmp_map00.bin`: there is **no marker between `nodeChildOffset1` and `nodeChildOffset2`** in the actual files, while `src/LoadingManager.cpp:474` reads one. `tools/map_to_obj.py:593-595` reads them back-to-back with a single following marker and produces valid output, so the tool layout matches the files; treat the extra `readMarker` at `src/LoadingManager.cpp:474` as a port-side discrepancy when parsing original maps.

### 1.2 tileEvents pair structure

Each event = two i32:

**word0** (`tileEvents[i*2]`):
- bits 0–9 (`& 0x3FF`): tile linear index — used by lookup and mapFlags marking (`src/Render.cpp:191,203`, `src/LoadingManager.cpp:547`).
- bits 16–31: **bytecode entry point** — `this->IP = (tileEvents[n] & 0xFFFF0000) >> 16` (`src/ScriptThread.cpp:156`).
- bits 10–15: no reader anywhere in `src/`; reserved.

**word1** (`tileEvents[i*2+1]`) — trigger mask, constants `EVFL_*` at `src/Enums.h:523-547`:

| Bits | Mask | Meaning | Evidence |
|---|---|---|---|
| 0–3 | `0xF` | exec-type: 1=ENTER, 2=EXIT, 4=TRIGGER(attack/use), 8=FACE | `src/Enums.h:523-527`; filter `src/ScriptThread.cpp:75` |
| 4–11 | `0xFF0` | direction modifiers: 16=E,32=NE,64=N,128=NW,256=W,512=SW,1024=S,2048=SE | `src/Enums.h:529-537`; filter `src/ScriptThread.cpp:75` |
| 12–14 | `0x7000` | attack mods: 4096=MELEE,8192=RANGED,16384=EXPLOSION | `src/Enums.h:538-541`; filter `src/ScriptThread.cpp:75` |
| 16 | `0x10000` | BLOCKINPUT — thread `type & 0x10000` blocks input (`isInputBlockedByScript`) | `src/Enums.h:543`, `src/Game.cpp:3452` |
| 17 | `0x20000` | EXIT_GOTO (named in Enums; not read from this word in src/) | `src/Enums.h:544` |
| 18 | `0x40000` | SKIP_TURN: sets `skipAdvanceTurn=true; queueAdvanceTurn=false` instead of running | `src/ScriptThread.cpp:76-79,106-109` |
| 19 | `0x80000` | DISABLED: skipped entirely; toggled by EV_EVENTOP; persisted in saves | `src/ScriptThread.cpp:75,808-816`; `src/Game.cpp:1683-1693,1838-1849` |

Direction semantics: the modifier points **from the event tile back toward the actor** (entering moving E records W; attacking a tile N of you records S). Derived from `eventFlagForMovement(-dx,-dy)` for enter (`src/Game.cpp:974-981`) and `flagForFacingDir(4)` adding 512° to the view angle for attack triggers (`src/MovementController.cpp:214-224`).

### 1.3 Event lookup (index math)

Linear scan over pairs, relying on sorted-by-tile storage for continuation:

```cpp
int Render::findEventIndex(int n) {           // n = tile linear index
    for (int i = 0; i < numTileEvents; ++i)
        if ((tileEvents[i*2] & 0x3FF) == n) { lastTileEvent = i | (n << 16); return i*2; }
    return lastTileEvent = -1;
}
int Render::getNextEventIndex() {             // continue while next pair has same tile
    ... if (n2 < numTileEvents && (tileEvents[n2*2] & 0x3FF) == n) { ... return n2*2; }
    return lastTileEvent = -1;
}
```
(`src/Render.cpp:182-209`). The iterator yields word0's array offset `i`; word1 lives at `i+1` (`src/ScriptThread.cpp:70,103`). Multiple events per tile all run in stored order (`src/ScriptThread.cpp:68-84`).

### 1.4 staticFuncs — 12 built-in script slots

12 u16 entries; value = raw bytecode IP; sentinel `65535` = SCR_NOT_DEFINED, absent (`src/Enums.h:511`; checked `src/Game.cpp:3345,3356`). Slot names `src/Enums.h:498-510`; call sites:

| Idx | Constant | Fired when | Site |
|---|---|---|---|
| 0 | SCR_INIT_MAP | after map/state load; `scriptStateVars[12]=difficulty` first | `src/LoadingManager.cpp:692-693` |
| 1 | SCR_END_GAME | after INIT_MAP if `player->gameCompleted` | `src/LoadingManager.cpp:696-697` |
| 2 | SCR_BOSS_75 | boss pain crosses 3/4 max HP (not during combat finish) | `src/Entity.cpp:293-299` |
| 3 | SCR_BOSS_50 | crosses 1/2 | `src/Entity.cpp:310-312` |
| 4 | SCR_BOSS_25 | crosses 1/4 | `src/Entity.cpp:323-325` |
| 5 | SCR_BOSS_DEAD | boss `died()` | `src/Entity.cpp:487-489` |
| 6 | SCR_PER_TURN | each `advanceTurn()` after auto door-close pass | `src/Game.cpp:1271-1279` |
| 7 | SCR_ATTACK_NPC | returning to ST_PLAYING from combat vs NPC (eType 3) | `src/Canvas.cpp:1072-1073` |
| 8 | SCR_MONSTER_DEATH | no call site in `src/` (superseded by entityDeathFunctions) | — |
| 9 | SCR_MONSTER_ACTIVATE | monster activated with MFLAG_TRIGGERONACTIVATE (`flags & 0x2`), flag cleared | `src/Game.cpp:800-803` |
| 10 | SCR_CHICKEN_KICKED | no call site in `src/` | — |
| 11 | SCR_ITEM_PICKUP | item pickup succeeds; `scriptStateVars[11] = def->tileIndex` first | `src/Entity.cpp:124-126` |

Dispatchers: `Game::executeStaticFunc(n)` runs synchronously on a fresh thread (`alloc(ip)` + `run()`) (`src/Game.cpp:3342-3351`); `Game::queueStaticFunc(n)` queues instead (thread `flags |= 0x2`, `unpauseTime=0`, `blockInputTime = gameTime+1`, returns 2) (`src/Game.cpp:3353-3365`). Static threads get `type=0, flags=1` (`src/ScriptThread.cpp:169-177`).

map00 actuals: `staticFuncs[0] = 0` (INIT_MAP present), `[6] = 253` (PER_TURN), rest 65535.

### 1.5 Bytecode addressing

- Addresses are absolute byte offsets into `mapByteCode`; u16/s16/i32 operands are big-endian inline (`getUByteArg/getByteArg/getUShortArg/getShortArg/getIntArg`, `src/ScriptThread.cpp:2095-2119`).
- Dispatch: `switch (mapByteCode[this->IP])`, `++this->IP` after every opcode (`src/ScriptThread.cpp:243,2043`). Arg readers pre-increment IP, so jumps are relative to the IP **after** operands (`EV_JUMP`: `IP += getUShortArg()`, `src/ScriptThread.cpp:314-318`; EV_EVAL false-jump `IP += uByteArg`, `306-310`). Loop ends when `IP >= mapByteCodeSize` or an opcode sets `n == 2` (`src/ScriptThread.cpp:238`).
- run() codes: `0` = lerp alloc failed (retry next tick), `1` = finished, `2` = paused (`src/ScriptThread.cpp:230-246,2027-2045`).

## 2. EXECUTION MODEL

### 2.1 Thread pool and lifecycle

- Fixed pool of **20** threads in `Game::scriptThreads`, wired to `app` once (`src/Game.h:109`, `src/Game.cpp:47`).
- `allocScriptThread()`: first `!inuse` slot; `init()` sets stackPtr=0, IP=FP=0, unpauseTime=0, **state=2**, throwAwayLoot=false (`src/ScriptThread.cpp:2048-2056`); `inuse=true`, `numScriptThreads++`; error 40 when exhausted (`src/Game.cpp:3270-3284`).
- `freeScriptThread()` → `reset()` (inuse=false + init), count-- (`src/Game.cpp:3286-3295`).
- Start modes:
  - Tile event: `alloc(eventIndex, flagsArg, blockInput)` — IP from word0 high half; pushes initial frame `[-1, 0]`; `type = flagsArg` (caller's trigger mask); `flags = blockInput ? 1 : 0` (`src/ScriptThread.cpp:155-167`).
  - Raw IP: `alloc(ip)` — same but `type=0, flags=1` (`src/ScriptThread.cpp:169-177`).
- `Game::executeTile(x,y,flags,b)` always takes a fresh thread; under ST_DIALOG it queues instead (`queueTile` + `unpauseTime=0`); result != 2 frees immediately (`src/Game.cpp:3324-3340`). `queueTile` performs the same filter as `executeTile` but ends with `flags |= 0x2; return 2` (`src/ScriptThread.cpp:95-118`).

### 2.2 Per-frame tick vs synchronous

Synchronous part: `ScriptThread::executeTile` → `run()` executes everything that doesn't pause, inside the caller's context (move finish, attack, map load…).

Resumption per frame: `Game::runScriptThreads(gameTime)` from `Canvas::update` whenever state != ST_MENU (`src/Canvas.cpp:791-796`):

```cpp
if (inuse && state == 2) {
    if (isInputBlockedByScript() && canvas->state == ST_AUTOMAP) setState(ST_PLAYING);
    if (canvas->state == ST_PLAYING || canvas->state == ST_CAMERA) attemptResume(gameTime);
}
if (state != 2 || stackPtr == 0) freeScriptThread(thread);
```
(`src/Game.cpp:3246-3268`). `attemptResume(n)` (`src/ScriptThread.cpp:2063-2072`):
- `stackPtr == 0` → 1 (dead);
- `unpauseTime == -1 || n < unpauseTime` → 2 (still waiting; `-1` = "external resume will call run()");
- else `unpauseTime = 0; run()`.

External resumers call `thread->run()` directly: dialog close (`src/DialogSystem.cpp:559-562`), non-async lerp completion collected into `callThreads[]` by `updateLerpSprites` / `snapLerpSprites` (`src/Game.cpp:2985-3005,2960-2982`), goto-move end (`gotoThread->run()`, `src/MovementController.cpp:164-166,298-301`), minigames (`callingThread->run()`: `src/HackingGame.cpp:629`, `src/VendingMachine.cpp:857`, `src/SentryBotGame.cpp:817`), armor repair (`src/ArmorRepairSystem.cpp:50`), camera keys (`src/GameStateRunner.cpp:36`), explosion follow-up (`src/PlayingInputHandler.cpp:424`).

### 2.3 Pause mechanics (`unpauseTime`, `evWait`, "no input pause")

- `evWait(ms)` (`src/ScriptThread.cpp:120-137`): if `game->skippingCinematic` → return 1 (never wait). Else `unpauseTime = gameTime + ms`; **iff thread `flags & 1`** (the `b=true` executeTile arg — used by all movement/combat sites): unless ST_CAMERA, `canvas->blockInputTime = unpauseTime`; forces ST_AUTOMAP/ST_MENU → ST_PLAYING; clears soft keys when playing. Returns 2 → run() exits.
- "No input pause" idiom (e.g. EV_DOOROP op-bit2, async lerps): the opcode simply does not attach the thread/block input (`n43=0` passes `nullptr` thread into performDoorEvent, `src/ScriptThread.cpp:751,760`).
- `unpauseTime == -1` = waiting on external completion (dialog, blocking lerp/door lerp, camera, minigame).
- Input pipeline honors `blockInputTime` (`src/InputEventController.cpp:449-478`): while `gameTime <= blockInputTime` queued input is dropped/cleared. `isInputBlockedByScript()` also true for any thread with `type & 0x10000` (`src/Game.cpp:3447-3459`), clearing input per frame outside dialogs/menus (`src/Canvas.cpp:777-779`); no current `executeTile` site passes bit 0x10000, so that path is dormant unless authored data uses EVFL_FLAG_BLOCKINPUT.

### 2.4 Multiple threads / re-entrancy

Up to 20 live threads; each `executeTile` takes its own slot (a single arrival can spawn FACE + ENTER threads sequentially). Pool exhaustion = fatal error 40 (`src/Game.cpp:3282`). Systems reference threads by pool index via `getIndex()` (`src/ScriptThread.cpp:2074-2081`; help dialog enqueue `src/ScriptThread.cpp:564-567`; LerpSprite binding `src/LerpSprite.cpp:76`).

### 2.5 Canvas-state interactions

| State | Effect |
|---|---|
| ST_MENU | `runScriptThreads` not called (`src/Canvas.cpp:791-796`) |
| ST_DIALOG | new tile events queued, not run (`src/Game.cpp:3329-3331`) |
| ST_AUTOMAP | no resumption (`src/Game.cpp:3259`); evWait/EV_DOOROP force ST_PLAYING (`src/ScriptThread.cpp:129-131,751`) |
| ST_CAMERA | evWait skips blockInputTime; threads still resume (`src/ScriptThread.cpp:126-128`, `src/Game.cpp:3259`) |

## 3. OPCODE TABLE

Opcode ids `src/Enums.h:399-497`; decode = single switch in `ScriptThread::run()` (`src/ScriptThread.cpp:243-2038`). Encoding column: `B`=u8, `b`=s8, `S`=u16BE, `s`=s16BE, `I`=i32BE. "var" = `game->scriptStateVars[]` (shorts).

| # | Name | Enc | Effect (decode site line) |
|---|---|---|---|
| 0 | EV_EVAL | cnt:B, cnt×term:B, off:B | term: bit7 → push var[t&0x7F]; bit6 → push sign-ext 14-bit const `((t&0x3F)<<8\|next)<<18>>18`; else op AND/OR/LTE/LT/EQ/NEQ/NOT. If final pop==0 → `IP += off` (244-312) |
| 1 | EV_JUMP | S | `IP += u16`, forward-only (314-318) |
| 2 | EV_RETURN | – | pop frame; ret IP==-1 → end (return 1) (321-327; frame pop 139-153) |
| 3 | EV_MESSAGE | S | HUD msg str `v&0x7FFF` of map string table; bit15 → style 3 (329-342) |
| 4 | EV_LERPSPRITE | B,B,B [+B z][+B t] | pack: sprite=(a>>14)&0xFF, dst=((a>>9)&0x1F,(a>>4)&0x1F), flags=a&0xF (ASYNC=1,BLOCK=2,NO_TIME=4,DEFAULT_Z=8, `src/Enums.h:285-290`); z default 32 else byte−48; time = byte*100 unless NO_TIME; blocking+time → evWait; sync lerp pauses thread via unpauseTime=-1 (344-398) |
| 5 | EV_STARTCINEMATIC | b | setupCamera(idx), canvas→ST_CAMERA, skipAdvanceTurn (400-412) |
| 6 | EV_SETSTATE | B,s | `var[b] = s16` (414-420) |
| 7 | EV_CALL_FUNC | S | push ret IP, push old FP; FP=stackPtr before pushes; `IP = ip − 1` (compensates ++IP) (422-431) |
| 8 | EV_ITEM_COUNT | S,B | cls=S&0x1F idx=(S>>5)&0x1F dst=B: cls0 → `player->inventory[idx]`; cls1 → bit test `player->weapons & 1<<idx`; cls2 → `player->ammo[idx]`; result → `var[dst]` (433-454) |
| 9 | EV_TILE_EMPTY | S,B | 1 iff tile (S&0x1F,(S>>5)&0x1F) holds only entities with eType==12 or `1<<eType & 0x6240` (456-475) |
| 10 | EV_WEAPON_EQUIPPED | B | `var[B&0x7F] = ce->weapon` (477-482) |
| 11 | EV_CHANGE_MAP | B,S | nextMap=B&0xF, fade/showStats=B&0x80, spawnDir=(B>>4)&7, spawnTile=S&0x3FF → `spawnParam=(dir<<10)\|tile` (consumed `src/Game.cpp:958-962`); stats screen iff nextMap >= current; snapAllMovers (484-517) |
| 12 | EV_CAMERA_STR | S,S | subtitle/title str `v&0x3FFF`; bit15 title, bit14 hides cin player; time ms (519-543) |
| 13 | EV_DIALOG | B,B | startDialog(strId, style=B&0xF, type=B>>4); style 2 → help queue; pauses (unpauseTime=-1, ret 2) (545-585) |
| 14 | EV_WAIT | B | `evWait(B*100 ms)` (587-591) |
| 15 | EV_GOTO | S | teleport: dst=((S>>5)&0x1F, S&0x1F), face=(S>>10)&0xF (15=keep); bit14 animate turn/walk (pauses via gotoThread), bit15 advanceTurn; relink, clearEvents(1), gotoTriggered=true (593-661) |
| 16 | EV_ABORT_MOVE | – | `canvas->abortMove = true` (663-667) |
| 17 | EV_ENTITY_FRAME | B,B,B | frame bits 8–15 of mapSpriteInfo[sprite]; optional wait B*100 (669-688) |
| 18 | EV_ADV_CAMERAKEY | B | camera key advance; pauses (690-702) |
| 19 | EV_DAMAGEMONSTER | B,b | pain(dmg); died(false,nullptr) if lethal (704-724) |
| 20 | EV_DAMAGEPLAYER | b,b,b | hp dmg (neg heals), armor delta, hit dir; painEvent/damage flash (726-745) |
| 21 | EV_DOOROP | S | see §3.3 (747-780) |
| 22 | EV_MONSTERFLAGOP | B,B | op=(B2>>6)&3 ADD/REMOVE/SET, mask `1 << (B2&0x3F)` on monster flags (782-806) |
| 23 | EV_EVENTOP | S | `word1[event[S&0x7FFF]] = (clear bit19) \| ((S>>15)&1)<<19` — enable/disable other events (808-816) |
| 24 | EV_HIDE | B | hide sprite; loot/corpse bookkeeping per eType (818-844) |
| 25 | EV_DROPITEM | S,B | spawnDropItem at (S&0x1F,(S>>5)&0x1F) height (S>>10)&0x1F, def id B (846-860) |
| 26 | EV_PREVSTATE | B | `--var[B]` (862-867) |
| 27 | EV_NEXTSTATE | B | `++var[B]` (869-874) |
| 28 | EV_WAKEMONSTER | B | activate sleeping monster, frameTime=0 (876-892) |
| 29 | EV_SHOW_PLAYERATTACK | B | cinematic weapon anim in ST_CAMERA (894-909) |
| 30 | EV_MONSTER_PARTICLES | B | blood on monster sprite (911-919) |
| 31 | EV_SPAWN_PARTICLES | B,S,B | kind=(B>>3)&0xF color=B&7; bit7 → explicit tile pos else sprite/z param (921-936) |
| 32 | EV_FADEOP | S | bit15 → fade-out over S&0x7FFF ms else fade-in S ms; skip-aware (938-958) |
| 33 | EV_GIVEITEM | B,B,b | mode B3==0 → touch sprite (B1<<8\|B2); else lookup def by **tileIndex** B1 (`EntityDefManager::lookup`, `src/EntityDef.cpp:76-83`), qty=(s8)B2 <0 = silent-fail variant, >0 spawns drop + touched() pickup; failure sets n2=1 → var7 (960-1008) |
| 34 | EV_NAMEENTITY | B,B | `entity.name = B2 \| loadMapStringID<<10` (1010-1021) |
| 35 | EV_DROPMONSTERITEM | S,B[,B],B | drop def at sprite's tile (bit15 → relocate whole entity instead) (1023-1057) |
| 36 | EV_SETDEATHFUNC | B,s | bind death-script ip to sprite (-1 unbinds) (1059-1075; exec `src/Game.cpp:3523-3534`) |
| 37 | EV_PLAYSOUND | B,B | `playSound(B+1000, B2>>4, B2&0xF, 0)` — vol/priority nibbles (1077-1092) |
| 38 | EV_NPCCHAT | S | NPC chat param (S>>14)&3 on sprite S&0x3FFF (1094-1121) |
| 39 | EV_STOCKSTATION | – | nop in port (1123-1126) |
| 40 | EV_LERPFLAT | B,S | consumed and ignored (1128-1133) |
| 41 | EV_GIVELOOT | B,cnt×S | loot dialog; entries class nibble: 6=text str, 5=quest, else give(class,idx,count) incl. credits merge; pauses (1135-1150, compose 2121-2224) |
| 42 | EV_MARKTILE | S | automap entrance(bit5)/exit(bit4)/ladder(bit1)/monsterclip(bit0)/keep-pitch((fl&0xC)<<2) on tile (1152-1181) |
| 43 | EV_UPDATEJOURNAL | B,B | updateQuests(quest,state); state 0 → showHelp(5) (1183-1193) |
| 44 | EV_BRIBE_ENTITY | – | nop (1195-1198) |
| 45 | EV_PLAYER_ADD_STAT | B | modifyStat((B>>5)&7, sign-ext 5-bit delta) (1200-1205) |
| 46 | EV_PLAYER_ADD_RECIPE | – | nop (1207-1210) |
| 47 | EV_RESPAWN_MONSTER | S,B,B | resurrect dead monster at tile; impossible → n2=-1 → var7 (1212-1226) |
| 48 | EV_SCREEN_SHAKE | S | dur=(v>>14&3)+1, dx=(v>>7&0x7F)+1<<4, dy=(v&0x7F)+1<<4 (1228-1245) |
| 49 | EV_SPEECHBUBBLE | s,B | showSpeechBubble(texId, color) (1247-1254) |
| 50 | EV_AWARDSECRET | – | awardSecret(false) (1256-1260) |
| 51 | EV_AIGOAL | S,B | setAIGoal(sprite S&0xFFF, goal (S>>12)&0xF, param B) (1262-1274) |
| 52 | EV_ADVANCETURN | – | snap monsters, undo attacks, endMonstersTurn, clearEvents(1), advanceTurn (1276-1288) |
| 53 | EV_MINIGAME | B,B,B,B | type 0 sentry-bot / 2 hacking / 4 vending; pauses (1290-1349) |
| 54 | EV_ENDMINIGAME | – | nop (1351-1354) |
| 55 | EV_ENDROUND | – | nop (1356-1359) |
| 56 | EV_PLAYERATTACK | S | scripted attack weapon=(S>>12)&0xF on sprite S&0xFFF; evWait(1) (1361-1373) |
| 57 | EV_SET_FOG_COLOR | I | buildFogTables(argb) (1375-1379) |
| 58 | EV_LERP_FOG | I | startFogLerp(from v&0x7FF, to (v>>11)&0x7FF, ((v>>22)&0xFF)*100 ms) (1381-1386) |
| 59 | EV_LERPSPRITEOFFSET | B,B,I | pixel-offset lerp; dx=(v>>11)&0x7FF dy=v&0x7FF flags=(v>>22)&3 dz=((v>>24)&0xFF)-48; ms=B*100 (1388-1441) |
| 60 | EV_DISABLED_WEAPONS | s | player->disabledWeapons=s16; switch away if masked (1443-1451) |
| 61 | EV_LERPSCALE | S,S,B | sprite=(S1>>4) flags=S1&0xF, ms=S2, dstScale=B<<1 (1453-1498) |
| 62 | EV_GIVEAWARD | – | nop (1500-1503) |
| 65 | EV_STARTMIXING | – | nop (1505-1508) |
| 66 | EV_DEBUGPRINT | B,… | mode0: zero-terminated inline chars; mode1: var value; chains while next cmd is 66 (1510-1531) |
| 67 | EV_GOTO_MENU | B | menuSystem->setMenu(B) (1533-1537) |
| 68 | EV_START_INTERCINEMATIC | B | inter-level camera (B&0x7F) (1539-1545) |
| 69 | EV_TURN_PLAYER | B | angle=(B&7)<<7 (45° units); bit3 animated (pauses) else instant (1547-1574) |
| 70 | EV_STATUS_EFFECT | B[,B] | bit7 remove (B&0x7F) else add(id, mag, per-id duration) (1576-1603) |
| 71 | EV_JOURNAL_TILE | B,B,B | setQuestTile(B1, B2&0x1F, B3&0x1F) (1605-1612) |
| 72 | EV_MAKE_CORPSE | S,B,B | corpsifyMonster(sprite, tile (B1,B2)) (1614-1625) |
| 73 | EV_INVENTORY_OP | B | 0 strip-for-vios / 1 strip-target-practice / 2 restore (1627-1643) |
| 74 | EV_END_GAME | – | gameCompleted; canvas→ST_EPILOGUE (1645-1651) |
| 75 | EV_LERPSPRITEPARABOLA | I,S | arc: sprite=(v>>22)&0x3FF dst=((v>>17)&0x1F,(v>>12)&0x1F) h=((v>>4)&0xFF)-48 flags=v&0xF \|LS_FLAG_PARABOLA (1653-1708) |
| 76 | EV_TOGGLE_OVERLAY | – | hud cockpit overlay ^= 1 (1710-1714) |
| 77 | EV_FOG_AFFECTS_SKYMAP | b | render->fogAffectsSkyMap (1716-1720) |
| 78 | EV_ENABLE_HELP | b | player->enableHelp (1722-1726) |
| 79 | EV_SET_MM_RENDER_HACK | b | render->useMastermindHack (1728-1732) |
| 80 | EV_START_ARMORREPAIR | – | armor UI; pauses (1734-1742) |
| 81 | EV_FORCE_BOT_RETURN | – | familiar returns (1744-1751) |
| 82 | EV_UNMARKTILE | S | inverse of 42 (1753-1780) |
| 83 | EV_ASSIGN_LOOTSET | S,B,n×S | entSprite=S&0xFFF, B entries overwrite lootSet (rest zeroed) (1782-1804) |
| 84 | EV_START_TARGETPRACTICE | S | enterTargetPractice((S>>8)&0x1F,(S>>3)&0x1F,S&7); pauses (1806-1813) |
| 85 | EV_GIVE_AUTOMAP | – | givemap(0,0,32,32) (1815-1819) |
| 86 | EV_ANGER_VIOS | b | game->angryVIOS (1821-1825) |
| 87 | EV_UNHIDE_AUTOMAP | B,B,B,B | rect clear of automap-hidden flag (1827-1839) |
| 88 | EV_HIDE_AUTOMAP | B,B,B,B | rect set (1841-1853) |
| 89 | EV_PORTAL_EVENT | B | portal state=B&0xF prev=(B>>4)&0xF; 4 → portalScripted=false (1855-1868) |
| 90 | EV_PITCH_CONTROL | S | keep-pitch tiles add/remove (1870-1899) |
| 91 | EV_USED_CHAINSAW | – | usedChainsaw(false) (1901-1905) |
| 92 | EV_ENTITY_BREATHES | B,B | entity info bit 0x20000000 breathe fx toggle (1907-1923) |
| 93 | EV_DESTROY_PLAYER | B | kill player (noclip/familiar rules) (1925-1947) |
| 94 | EV_START_TREADMILL | – | canvas→ST_TREADMILL (1949-1953) |
| 95 | EV_STOPSOUND | B | stopSound(B+1000) (1955-1960) |
| 96 | EV_LERPSPRITEPARABOLA_SCALE | I,S,B | parabola + dstScale=B<<1 (1962-2019) |
| 97 | EV_SET_CALDEX_RENDER_HACK | b | render->useCaldexHack (2021-2025) |
| 255 | EV_END | – | assert stackPtr==0; return 1 (2027-2032) |

Unknown opcode → fatal `Error("Cannot handle event: %d")` (`src/ScriptThread.cpp:2034-2037`). Post-op default: `scriptStateVars[7] = n2` records give/spawn failures (`src/ScriptThread.cpp:240,2040-2042`).

### 3.1 EV_EVAL notes

- Comparisons pop `a` (top) then `b` and push `b <= a` / `b < a` — left operand compares against right (275-297). EQ/NEQ symmetric; results are 0/1 ints.
- AND/OR evaluate both sides (terms are pre-pushed; no short-circuit) (267-274).
- Constants are **14-bit sign-extended**: `(((t&0x3F)<<8 | next) << 18) >> 18` (259-264).
- The false-jump offset is unsigned byte added AFTER consuming it (306-310).

### 3.2 Runtime script vars

Refreshed at every `run()` entry (`updateScriptVars`, `src/Game.cpp:3461-3471`; also `src/Player.cpp:2094,2099,2171`):
`var0=statusEffects[33]`, `var1=hp`, `var2=viewX>>6`, `var3=viewY>>6`, `var8=inventory[24] (credits)`, `var12=difficulty` (set at load, `src/LoadingManager.cpp:692`), `var14=characterChoice`, `var16=isFamiliar`. Written by scripts: var7 (cmd fail flag), var11 (last picked item tileIndex, `src/Entity.cpp:125`), others free.

### 3.3 EV_DOOROP encoding (door-critical)

`args:S` — sprite = `args & 0x3FF`, `op = args >> 10` (values ≥4 seen in real data = quiet variants) (`src/ScriptThread.cpp:749-752`):

- `interactive = (op & 4) == 0 && canvas->state != ST_AUTOMAP`.
- action = `op & 3`:
  - **0 OPEN** — `performDoorEvent(0, interactive?this:nullptr, entity, interactive, false)`; if lerp started and interactive → `unpauseTime=-1; return 2` (resumed when the 750ms door lerp finishes, §2.2).
  - **1 UNLOCK+OPEN** — `setLineLocked(entity,false)` first (iff eType==ET_DOOR), then like 0.
  - **2 LOCK** — `setLineLocked(entity,true)` + sound 1065 if interactive.
  - **3 UNLOCK** — `setLineLocked(entity,false)` + sound 1065 if interactive.
- Sprite without live entity (`S_ENT == -1`) → nothing (`src/ScriptThread.cpp:753-779`).

`setLineLocked(entity,b)` (`src/Game.cpp:2477-2498`): flips **bit0 of `mapSpriteInfo[sprite] & 0xFF`** (unlock = set), then re-looks-up the EntityDef via `entityDefManager->lookup(tileNum + 257)` (by tileIndex!, `src/EntityDef.cpp:76-83`), syncing `entity->name = def->name | 0x400` when they matched.

Door def families (from `tmp_entities.bin`; consistent with `docs/research/2026-08-23-doors.md:27-30` and range check `b3 = n4>=271 && n4<281` at `src/Game.cpp:1058` where `n4 += 257` iff spriteInfo bit22 set):

| base tileNum (sprite low byte) | locked def | unlocked def | family |
|---|---|---|---|
| 14 / 15 | 271 | 272 | red-blue keycard A ("red") |
| 16 / 17 | 273 | 274 | keycard B ("blue") |
| 18 / 19 | 275 | 276 | normal |
| 20 / 21 | 277 | 278 | level exit |

(`defs: tileIndex 271..278, eType=5 ET_DOOR, eSubType 1=locked / 2=unlocked; parm 1/0/2`.) Sprite info low byte stores the CURRENT tileNum (bit0 set = unlocked); bit22 marks the special door class.

## 4. TRIGGERS — all executeTile / thread-spawn sites

### 4.1 Movement (enter/leave with direction bits)

Direction modifier from movement vector (`src/Game.cpp:983-1014`): (+x,−y)=NE(32), (+,+)=SE(2048), (+,0)=E(16), (−,−)=NW(128), (−,+)=SW(512), (−,0)=W(256), (0,+y)=S(1024), (0,−y)=N(64). Screen Y grows downward.

`eventFlagsForMovement(viewX,viewY,newX,newY)` (`src/Game.cpp:974-981`):
- `eventFlags[0] = 2 \| dir(old→new)` — LEAVE mask for source tile,
- `eventFlags[1] = 1 \| dir(new→old)` — ENTER mask for destination tile (points back the way you came).

Per step (`MovementController::attemptMove`, `src/MovementController.cpp:326-349`): compute masks → **leave-event BEFORE collision/move** (`executeTile(viewX>>6, viewY>>6, eventFlags[0], true)`); the handler may `abortMove` (EV_ABORT_MOVE) which cancels (`328-331`). On arrival (`finishMovement`, `160-184`): FACE then ENTER on destination, both with `b=true`; then `touchTile`.

`flagForFacingDir(i)` = `i \| 1 << (((destAngle & 0x3FF) >> 7) + 4)`; `i==4` looks 512° backward (`src/MovementController.cpp:214-224`). Rotation-in-place fires FACE alone (`src/MovementController.cpp:307`); post-EV_GOTO re-fires synthetic ENTER+FACE (`503-509`, masks recomputed from (-1,-1)); GOTO itself doesn't fire enter events and does `clearEvents(1)` (`src/ScriptThread.cpp:653-657`). Cinematic FIRE re-runs pending ENTER (`src/PlayingInputHandler.cpp:560-567`).

### 4.2 Attack / use triggers

- Fire button vs adjacent tile: `executeTile(aheadX, aheadY, flagForFacingDir(4), true)` → TRIGGER + reverse-facing dir (`src/PlayingInputHandler.cpp:395-404`); nonzero result consumes the turn (unless skipAdvanceTurn).
- Explosive barrel destroyed: `executeTile(x,y, 0xFF4 \| flagForWeapon(weapon), true)` — TRIGGER + ALL dirs + MELEE/RANGED/EXPLOSION mod (`src/Combat.cpp:372-378`; `flagForWeapon` `src/MovementController.cpp:198-212`).
- Plain door use bypasses the VM: `performDoorEvent(0, entity, …)` (`src/PlayingInputHandler.cpp:451,462`); scripted unlocking goes through EV_DOOROP inside scripts.

### 4.3 Map init

Fresh map: `executeStaticFunc(0)`, optional `executeStaticFunc(1)`, then `executeTile(spawnX, spawnY, 4081, 1)` — 4081 = 0xFF1 = ENTER + all dirs ("entered from anywhere") (`src/LoadingManager.cpp:692-706`).

### 4.4 Others

All static-func sites in §1.4; bound death scripts via `Game::executeEntityFunc` (`src/Game.cpp:3523-3534`).

### 4.5 Trigger filter (exact predicate)

An event runs iff ALL hold (`src/ScriptThread.cpp:75`; same in `doesScriptExist` `src/Game.cpp:3316`):

```cpp
(word1 & 0x80000) == 0                    // enabled
&& (word1 & flags & 0xF)   != 0           // exec-type intersect
&& (word1 & flags & 0xFF0) != 0           // direction intersect
&& ( ((word1 & 0x7000) == 0 && (flags & 0x7000) == 0)
     || (word1 & flags & 0x7000) != 0 )   // attack mods: both empty or intersect
```

Consequences: an event with NO direction bits never fires from real triggers (all sites pass dir bits); attack-modded events need a compatible attack; SKIP_TURN events swap turn-advance for skip-flag.

## 5. RED/BLUE DOOR END-TO-END ON MAP00 (verified against tmp_map00.bin)

Parsed with a faithful re-implementation of the `src/LoadingManager.cpp` walk (minus the childOffset marker discrepancy, §1.1). Header: `numTileEvents=167`, `mapByteCodeSize=10015`, spawn `(4,19)` facing 0(E).

Door sprites (positions from sprite coord arrays; tileNum/info from mapSpriteInfo bytes):

| sprite | tile | low byte | info | identity |
|---|---|---|---|---|
| 22 | (10,19) | 16 | 0x0C500010 | **BLUE door, locked** (16+257=273) |
| 65 | (16,19) | 14 | 0x0C50000E | **RED door, locked** (14+257=271) |
| 37 | (13,19) | 19 | 0x0C500013 | normal door, unlocked; INIT_MAP locks it (@61 `EV_DOOROP sprite=37 op=2`) |
| 132 | (29,26) | 18 | 0x0C500012 | normal door, locked |

Key items: def with `tileIndex=110` = ET_ITEM parm 19 (**red keycard**, `INV_OTHER_RED_KEY` `src/Enums.h:222`); `tileIndex=111` = parm 20 (**blue keycard**, `src/Enums.h:223`). `EV_GIVEITEM` resolves defs by tileIndex (`src/EntityDef.cpp:76-83`), so `give def=110/111` grants these cards. No keycard sprites are placed on the map; the **blue keycard is carried by searchable corpse sprite 29** at (13,2) — `ET_CORPSE` (eType 9, subtype 17) with `EV_ASSIGN_LOOTSET entSprite=29 lootSet=[class0 idx20 x1]` (@581), i.e. looting the body yields `inventory[20]=1`. No map00 lootset carries the red card (it enters the inventory only on later maps; inventory persists across maps).

### 5.1 Blue door (sprite 22 @ (10,19)) — three cooperating scripts

1. **Intro/story trigger — EVT tile 617 (9,19), w1=0xFF4 TRIGGER-all-dirs** (fires when the player, standing west of the door, attacks/uses toward it; also covers approach):
   - long cinematic: NPCs walk to the door (`EV_LERPSPRITE` sprites 7/10/19), camera 7, then `func@3333`: dialog str(26), bubble, `EV_HIDE sprite=203` (grenade prop), **@3365 `EV_DOOROP sprite=22 op=3(UNLOCK)`** (sound 1065) → **@3368 `op=0(OPEN)`** (blocking; thread resumes on lerp end);
   - later in the same event: **@3324 `op=1(UNLOCK+OPEN)`** (NPC steps out) and **@3327 `op=6(quiet LOCK)`** — the door is quietly re-locked behind them.
2. **First-entry cinematic — EVT tile 618 (10,19), w1=0xFF1 ENTER-all-dirs** (@4223): disables itself (`EV_EVENTOP event[56] DISABLE`), plays camera 10 sequence (scientist/Klingon scene at the door tile).
3. **Player-with-key trigger — EVT tile 618, w1=0xFF4 TRIGGER-all-dirs** (@4161):
   ```
   4161: EV_ITEM_COUNT inventory[20] -> var27        ; blue key count
   4165: EV_EVAL [var27 != 0] iffalse -> 4190        ; have key?
   4172:   EV_EVENTOP event[60] DISABLE              ; retire this trigger
   4175:   EV_GIVEITEM def=111 qty=1                 ; (re-)grant blue keycard
   4179:   EV_DOOROP sprite=22 op=3(UNLOCK)          ; setLineLocked(false)+sound 1065
   4182:   EV_WAIT 300ms
   4184:   EV_DOOROP sprite=22 op=0(OPEN)            ; blocking open
   4187:   EV_JUMP -> 4220 (EV_RETURN)
   4190: (no key) character-dependent sounds 1073/1072 or 1111,
   4219:   EV_MESSAGE str(127) style3                ; "locked" HUD message
   ```
   So after looting the corpse, using/attacking toward the blue door unlocks and opens it; afterwards the plain door-use path (`performDoorEvent` from `src/PlayingInputHandler.cpp:451,462`) keeps working because the def was swapped to the unlocked variant.

### 5.2 Red door (sprite 65 @ (16,19))

1. **Story unlock — EVT tile 593 (17,18), w1=0x404 TRIGGER+dir S** (attack/use from (17,19) northward): cinematic ending **@5192 `EV_DOOROP sprite=65 op=1(UNLOCK+OPEN)`** (non-quiet, blocking), followed by camera 13 + GOTO walk-through.
2. **Player-with-key trigger — EVT tile 624 (16,19), w1=0xFF4** (@4768): identical shape to blue:
   ```
   4768: EV_ITEM_COUNT inventory[19] -> var49        ; red key count
   4772: EV_EVAL [var49 != 0] iffalse -> 4797
   4779:   EV_EVENTOP event[66] DISABLE
   4782:   EV_GIVEITEM def=110 qty=1                 ; (re-)grant red keycard
   4786:   EV_DOOROP sprite=65 op=3(UNLOCK)
   4789:   EV_WAIT 300ms
   4791:   EV_DOOROP sprite=65 op=0(OPEN)
   4797: (no key) sounds 1073/1072/1111,
   4826:   EV_MESSAGE str(126) style3,
   4829:   once-per-game journal hint: EVAL var50==0 → UPDATEJOURNAL quest=128 state=0,
           EV_JOURNAL_TILE quest=128 tile(16,19), EV_NEXTSTATE var50
   ```

### 5.3 Supporting static funcs on map00

- **INIT_MAP @0**: entity naming (`EV_NAMEENTITY`), fog setup (`EV_SET_FOG_COLOR`/-`LERP_FOG`), calls func@384 (bulk `EV_MONSTERFLAGOP`s + NPC chats) and func@508 (17 × `EV_SETDEATHFUNC` bindings), decorative `EV_LERPSPRITEOFFSET`/`EV_LERPSCALE`s, locks normal door sprite 37, `EVAL var17==0` gate around the tail (new-game vs reload behavior), `EV_MARKTILE`s.
- **PER_TURN @253**: `EVAL var28==1` → `EV_ITEM_COUNT inventory[20] -> var27` → if ==1: `EV_UPDATEJOURNAL quest=37 state=1` + `NEXTSTATE var28` (one-shot "got blue key" journal poller); VIOS sub-state machine (vars 24/25, `CALL func@4564`); corpse-loot checks with `EV_TILE_EMPTY` (21,28)/(21,27) + `EV_HIDE` + `EV_LERPSPRITE`.

## 6. EDGE CASES

- **Missing lists/data**: out-of-range coords → thread dies silently (`state=0`) (`src/ScriptThread.cpp:60-63,96-98`); tile without flag 0x40 → no-op (`66,87-88`); findEventIndex miss → -1; staticFunc sentinel 65535 → return 0 without allocating (`src/Game.cpp:3345-3347,3356-3358`).
- **Re-entrancy**: scripts can trigger nested executeTile (fresh slots); exhaustion fatal (error 40). EV_GOTO/EV_ADVANCETURN snap monsters and cancel pending attacks first (`src/ScriptThread.cpp:593-601,1276-1284`).
- **EV_ABORT_MOVE** only meaningful in the leave-event window (leave runs before trace; `abortMove` checked right after) — `src/MovementController.cpp:328-331`; reset each `attemptMove`.
- **Loot-mode threads**: `throwAwayLoot` switches EV_GIVEITEM/EV_GIVELOOT into toast mode (`src/ScriptThread.cpp:965-969`; set by `src/Game.cpp:3523-3531`).
- **Save/load**: all 20 slots saved verbatim — unpauseTime stored relative to gameTime (unless 0/-1), state byte, IP, FP, FP stack ints (`src/ScriptThread.cpp:30-57`); load restores and `inuse = stackPtr > 0` (`src/Game.cpp:1862-1872`). Event disable bits (word1 bit19) persist as ceil(N/32) i32 bitmask (`save src/Game.cpp:1680-1693` / `load 1838-1849`). Door state survives via mapSpriteInfo/entity defs in the regular entity-state save; `watchLine` too (`src/Game.cpp:1720-1724`).
- **Marker discrepancy**: files lack the childOffset marker the loader reads (§1.1) — relevant to anyone re-implementing the parser from `src/` alone.

## PORT CHECKLIST

Minimum viable faithful interpreter (doors + move events scope):

1. **Data**: parse `numTileEvents`(u16LE), `mapByteCodeSize`(u16LE); tail blocks staticFuncs[12]:u16LE, tileEvents[N]:{i32,i32}LE, mapByteCode[size]; OR 0x40 into mapFlags per event; remember the missing childOffset marker (§1.1).
2. **State**: `ScriptThread { IP, FP, stackPtr, unpauseTime, type, flags, state, inuse, scriptStack[16] }`, pool of 20; `scriptStateVars` shorts refreshed per run (§3.2).
3. **Trigger dispatch**: `executeTile(x,y,mask,blockInput)` implementing §4.5 predicate + findEventIndex/getNextEventIndex iteration (all matches run in order); wire leave-before-move, face+enter-on-arrival, attack-trigger (`flagForFacingDir(4)`), spawn-tile 4081, static funcs 0/6/11.
4. **Dispatch loop**: big-endian inline args, `++IP` after opcode, returns {0 retry, 1 done, 2 paused}; per-frame `attemptResume` gated on ST_PLAYING/ST_CAMERA; `unpauseTime==-1` external-resume protocol (dialog/lerp/goto/minigame call `run()` directly); `blockInputTime` handling in `evWait`.
5. **Opcode subset sufficient for map00 doors/moves**: EV_EVAL (full grammar incl. 14-bit signed consts), EV_JUMP, EV_RETURN, EV_CALL_FUNC, EV_END, EV_SETSTATE/PREVSTATE/NEXTSTATE, EV_ITEM_COUNT, EV_DOOROP (+ performDoorEvent + setLineLocked def-swap by tileIndex+257), EV_MESSAGE, EV_PLAYSOUND, EV_STOPSOUND, EV_WAIT, EV_GOTO, EV_ABORT_MOVE, EV_TURN_PLAYER, EV_EVENTOP, EV_FADEOP, EV_CHANGE_MAP, EV_GIVEITEM, EV_GIVELOOT (subset), EV_UPDATEJOURNAL, EV_JOURNAL_TILE, EV_TILE_EMPTY, EV_NAMEENTITY, EV_SETDEATHFUNC, EV_HIDE, EV_LERPSPRITE/EV_LERPSPRITEOFFSET (or stubbed), EV_DIALOG (stub ok initially), EV_ADVANCETURN, EV_MARKTILE/EV_UNMARKTILE, EV_SCREEN_SHAKE, EV_SPEECHBUBBLE, EV_NPCCHAT, EV_WAKEMONSTER, EV_MONSTERFLAGOP, EV_ASSIGN_LOOTSET, EV_ENTITY_FRAME, EV_ENTITY_BREATHES, EV_MINIGAME (stub), EV_STARTCINEMATIC/EV_ADV_CAMERAKEY (camera system).
