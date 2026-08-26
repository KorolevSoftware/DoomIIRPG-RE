# LERP* script opcodes & the LerpSprite system (verified facts)

All claims cite `src/` (original RE port). Map-data claims cite the parsed
`tmp_map00.bin` (regenerated asset; see docs/research/2026-08-23-tile-events-vm.md for
layout provenance). Operand streams are BIG-endian (`src/ScriptThread.cpp:2095-2119`).

## Opcode IDs

`src/Enums.h`: EV_LERPSPRITE=4 (:406), EV_LERPFLAT=40 (:442), EV_SET_FOG_COLOR=57 (:459),
EV_LERP_FOG=58 (:460), EV_LERPSPRITEOFFSET=59 (:461), EV_LERPSCALE=61 (:463),
EV_LERPSPRITEPARABOLA=75 (:475), EV_TOGGLE_OVERLAY=76 (:476),
EV_LERPSPRITEPARABOLA_SCALE=96 (:496).

## EV_LERPSPRITE (op 4) — `src/ScriptThread.cpp:344-398`

Payload: 3 raw bytes `a = b0 | b1<<8 | b2<<16` (:346), then optional bytes:
- `sprite = (a>>14)&0xFF`, `dstTileX = (a>>9)&0x1F`, `dstTileY = (a>>4)&0x1F`, `flags = a&0xF` (:347-350)
- flag 8 = SCRIPT_LS_DEFAULT_Z (`src/Enums.h:289`): if clear, read `z` byte, `dstZrel = z-48`; if set, `dstZrel = 32` (:351)
- flag 4 = SCRIPT_LS_NO_TIME (:288): if clear, read `t` byte, `time = t*100` ms; else 0 (:352)
- flag 2 = SCRIPT_LS_FLAG_BLOCK (:286): thread does `evWait(time)` up-front (:353-355) and the
  LerpSprite gets LS_FLAG_ANIMATING_EFFECT (blocks monster auto-activation, `Game.cpp:3071-3074`,
  consumed at `src/Render.cpp:1578`)
- flag 1 = SCRIPT_LS_FLAG_ASYNC (:285): fire-and-forget (no thread pause)
- Destination: `dstX = 32+(dstTileX<<6)`, `dstY = 32+(dstTileY<<6)` (tile centers, :361-362);
  `dstZ = getHeight(dstX,dstY) + dstZrel` (:370). `getHeight = heightMap[(y>>6)*32+(x>>6)]<<3`
  (`src/Render.cpp:2444-2451`).
- src pos/scale taken from current mapSprites; `travelTime = time`; `flags = scriptFlags & 3` (:371-377).
- If `time==0`: single immediate `updateLerpSprite` (:379-386). Else if not ASYNC: sets
  `skipAdvanceTurn`, returns n=2 (thread pauses until lerp completes) (:388-394).

## EV_LERPSPRITEOFFSET (op 59) — `src/ScriptThread.cpp:1388-1441`

Payload `B sprite, B time(hundreds of ms), I packed`:
- `dstX = (I>>11)&0x7FF`, `dstY = I&0x7FF` — ABSOLUTE fixed coords, NOT tile-relative (:1393-1394)
- `flags = (I>>22)&3` (bit0 ASYNC, bit2 BLOCK→evWait (:1428-1429)), `dstZrel = ((I>>24)&0xFF)-48` (:1395-1396)
- `dstZ = getHeight(dstX,dstY) + dstZrel` (:1410)
- Marks linked entity `info |= 0x400000` and `monster->flags |= 0x4000` (MFLAG_LERP_SHADOW,
  `src/Enums.h:342,358`) (:1403-1409). Parabola variants instead CLEAR that monster bit (:1672,1982).

## EV_LERPSCALE (op 61) — `src/ScriptThread.cpp:1453-1498`

Payload `S packed, S timeMs, B scale`:
- `sprite = S>>4`, `flags = S&0xF` (bit0 ASYNC, bit1 BLOCK→evWait) (:1455-1457,1485-1486)
- `dstScale = scale<<1` (mapSprites scale is 64=1.0; max 510) (:1468)
- Position/Z held constant; entity `info |= 0x400000` (:1469-1472). time is in ms directly (no *100).

## EV_LERPSPRITEPARABOLA (op 75) / _SCALE (op 96) — `src/ScriptThread.cpp:1653-1708,1962-2019`

Payload `I packed, S timeMs` (+`B scale` for 96):
- `sprite=(I>>22)&0x3FF`, `dstTileX=(I>>17)&0x1F`, `dstTileY=(I>>12)&0x1F`,
  `height=((I>>4)&0xFF)-48` (arc peak), `flags=I&0xF` (:1657-1661)
- `dst = 32+(dx<<6), 32+(dy<<6)`; `dstZ = getHeight(dst) + (srcZ - getHeight(src))` (relative height
  preserved, :1678); `dstScale = scale<<1` (96 only, :1990); `flags = (I&3) | LS_FLAG_PARABOLA` (:1684-1685).

## EV_LERP_FOG (op 58) — `src/ScriptThread.cpp:1381-1385`

`I`: `fogMin = v&0x7FF`, `fogRange = (v>>11)&0x7FF`, `time = ((v>>22)&0xFF)*100` →
`Render::startFogLerp` (`src/Render.cpp:1801-1821`).

## EV_TOGGLE_OVERLAY (op 76) — `src/ScriptThread.cpp:1710-1714`

`hud->cockpitOverlayRaw ^= 1`. Only writer in the codebase.

## LerpSprite runtime — `src/LerpSprite.h:13-28`, `src/Game.cpp`, `src/LerpSprite.cpp`

- Pool of 16 (`Game.cpp:3028`); exhaustion → `app->Error(36)` ERR_MAX_LERPSPRITES (`Enums.h:1050`).
  `hSprite` stores `sprite+1`; 0 = free slot (`Game.cpp:3030,3041`). Re-allocating the same sprite
  reuses its slot (`Game.cpp:3030-3031`).
- `allocLerpSprite(thread, sprite, block)`: block → `LS_FLAG_ANIMATING_EFFECT` + `animatingEffects++`
  (`Game.cpp:3071-3074`); `thread==nullptr` → `LS_FLAG_ASYNC` (:3068-3070).
- Flags (`src/Enums.h:293-305`): 0x1 ASYNC, 0x2 ANIMATING_EFFECT, 0x4 PARABOLA, 0x8 TRUNC,
  0x10 ENT_NORELINK, 0x20 S_NORELINK, 0x40/0x80 DOOROPEN/CLOSE, 0x100/0x200 SECRET_OPEN/HIDE,
  0x400 CHICKEN_KICK, 0x800 AUTO_FACE. NOTE: script LERP ops write `flags = scriptBits&3`
  (bit0=ASYNC, bit1=ANIMATING_EFFECT); ENT_NORELINK/S_NORELINK are only set by door/secret/entity
  lerps (`Game.cpp:1129,1186`, `src/Entity.cpp:866`) — never by the script LERP ops.
- Tick `Game::updateLerpSprite` (`Game.cpp:2855-2956`): LINEAR, no easing.
  `p = (elapsed<<16)/(travelTime<<8)` (:2876-2878); then for X/Y/Z/scale:
  `value = src + (p*(dst-src<<8)>>16)` (:2879-2882). Parabola adds
  `z += (sinTable[(p*2)&0x3FF]>>8) * (height<<8) >> 16` (TRUNC: `p = p*6/8`) (:2883-2893).
  Monsters get walk-anim frames `1 + (p*dist>>12) & 3` and footstep sounds for bosses (:2903-2944).
  Per-tick `relinkSprite` unless S_NORELINK (:2889-2896).
- Completion (`elapsed >= travelTime`, :2868): `freeLerpSprite` (`Game.cpp:3078-3243`) snaps
  dstX/Y/(Z unless TRUNC)/scale into mapSprites, relinks sprite unless S_NORELINK, relinks entity
  unless ENT_NORELINK, then handles DOORCLOSE/DOOROPEN/SECRET/CHICKEN special cases and frees the
  slot. Returns 3.
- Driver `Game::updateLerpSprites` (`Game.cpp:2985-3021`, called from `src/GameStateRunner.cpp:25,184`
  and `Game.cpp:1874`): on completion (return bit0) resumes the owning non-ASYNC ScriptThread via
  `callThreads[]`; bit1 → `updateFacingEntity`, bit2 → `invalidateRect`.
- `Game::snapLerpSprites(n)` (`Game.cpp:2958-2983`) force-completes (startTime=travelTime=0) and
  resumes threads — used on cinematic skip.
- Save/load: `LerpSprite::saveState/loadState` (`src/LerpSprite.cpp:17-81`) persists travelTime,
  elapsed, current+src+dst XYZ, height, scales, flags, owning thread index.

## map00 boot cinematic usage (new-game path)

`staticFuncs = [0]=0 (SCR_INIT_MAP), [6]=253 (SCR_PER_TURN)` (tmp_map00.bin). At IP 0 and 221 the
init script tests `scriptStateVars[17]==0` (EVAL op4 = EQ, jump-if-false,
`src/ScriptThread.cpp:287-291,306-310`; jump target = offsetByteAddr+offset+1 because of the loop's
`++IP` at `src/ScriptThread.cpp:2043`; CALL_FUNC target is exact, `IP = ip-1`,
`src/ScriptThread.cpp:422-431`). New game falls through and `CALL_FUNC 1913` (IP 228); the 1913
function chains `CALL 1942` (camera 0), `CALL 2084` (camera 1), `CALL 2328` (camera 5).

- func@1942 — camera 0 "cockpit flyby & shaft descent": TOGGLE_OVERLAY (1945, cockpit ON),
  STARTCINEMATIC 0, subtitle strings 9/10/11/12 (op 12, 3000+4000+3000+3000 ms);
  LERPSPRITEOFFSET sp135/sp136 (shaft-wall panels, type 85 + SPRITE_FLAG_TILE → wall texture 342)
  zrel 32→96, 2000 ms ASYNC; sp19 (npc_bob) / sp46 (type 23) walk (104,383)→(104,575) /
  (104,352)→(104,544) 3000 ms; sp137 (type 209 = white flash media) (240,447)→(255,447)
  1000+1000 ms then HIDE (engine flare streak); TOGGLE_OVERLAY (2062, cockpit OFF);
  snap sp19/sp46 to tiles (9,20)/(14,16) scale 64.
- func@2084 — camera 1 "elevator platform settle": sp150/sp151 (type 202 = metal grate platform,
  EAST|WEST two-sided) at (128,1376): zrel 32→96 and -32→32 over 1500 ms, reset, fog pulses
  (LERP_FOG 65536/262176), SCREEN_SHAKE 40637, SPAWN_PARTICLES 172, PLAYSOUND 32/STOPSOUND 68,
  then two 700 ms bounces (zrel 64/0, 96/32) via CALL 2299; character title cards via op12
  16398/16400 (branch on `scriptStateVars[14]` = `player->characterChoice`, `Game.cpp:3468`).
- func@2328 — camera 5 "squad walk-in + imp demo": sp7/sp9/sp10 (npc_major/npc_riley_oconnor/
  npc_sarge, types 68/66/72) placed per character choice, walk 1500/1700/2000 ms, doors open
  (DOOROP), then a scripted imp attack (sp15 type 23, LERPSPRITEPARABOLA h=48 600 ms) killing
  sp16 (type 71) with shake/particles.
- Later "lift crashes into the shaft" cinematic (trigger tile 610=(18,19),
  event[48] IP 3703-3872, camera 9 = parked at (160,1248,Z=484) inside the
  shaft): sp152 (type 198 wall art via +257) to (160,1248); car side sp153
  (type 52+257=309) drops dz −40 then HIDE; debris sp205/sp206 HIDE;
  **sp155 = TILENUM_GLASS pane (`src/Enums.h:792`) is NOT hidden** — it is
  re-parked at (127,1248) and switched to its broken frame by
  `ENTITY_FRAME 155 frame=1` @3797 (`src/ScriptThread.cpp:669-687`);
  `NEXTSTATE 22` @3706 makes every later map load re-run the restorer
  function @1028-1069 whose `EVAL v22==1` gate falls through to
  `ENTITY_FRAME 155 frame=1` @1065 — i.e. whole glass before the crash,
  broken glass after it, persisted purely via script var v22.
  (Full decode: docs/research/2026-08-25-elevator-glass.md §2.)

## Cockpit overlay condition (for the rewrite)

- `hud->cockpitOverlayRaw` (`src/Hud.h:57`), initially false (`Hud::Hud` memset, `src/Hud.cpp:24-26`).
- Set ONLY by EV_TOGGLE_OVERLAY (map00: ON at IP 1945 before STARTCINEMATIC 0, OFF at 2062).
- Drawn from `MayaCamera::Render` (`src/MayaCamera.cpp:316-318`) → `Hud::drawOverlay`
  (`src/Hud.cpp:620-625`): draws `cockpit.bmp` (loaded `src/App.cpp:418`) twice into
  `canvas->cinRect` (left edge and right edge, source offset 24,4 for the right copy).
- `Render::drawRGB` suppresses the normal view border/overdraw while set (`src/Render.cpp:2937-2939`).
- So: draw the cockpit frame during ST_CAMERA whenever `cockpitOverlayRaw` is true; the boot intro
  enables it only for camera 0.
