# 2026-08-24 — intro (map00 boot) cinematic: LERPSPRITE family & ship/platform descent

Date: 2026-08-24
Status: COMPLETE

## Hypothesis

The map00 boot cinematic (player's ship descending into the shaft on a platform) is implemented
with the script `LERP*` opcode family (`EV_LERPSPRITE`, `EV_LERPSCALE`, `EV_LERPSPRITEOFFSET`,
maybe parabola variants) moving large sprites.

## Method

1. Read all EV_LERP* handlers in `src/ScriptThread.cpp` (ops 4/58/59/61/75/96 + EV_TOGGLE_OVERLAY).
2. Read `src/Game.cpp` `updateLerpSprite`/`updateLerpSprites`/`allocLerpSprite`/`freeLerpSprite`
   (2855-3243), `src/LerpSprite.{h,cpp}`, flag constants `src/Enums.h:285-305`.
3. Parsed `tmp_map00.bin` with `tools/map_to_obj.py::parse_map` (read-only, stdout-only scripts);
   disassembled the full bytecode (operand sizes per handler; variable-length ops 0/4/35/41/59/
   61/66/70/75/83/96/255 handled). Bytecode parses cleanly to 8372/10015 B (op 107 tail = post-
   table data / parse gap in the last 1.6 KB — does not affect the intro region).
4. Decoded `MayaCamera` keyframes by re-walking the file with `tools/map_to_obj.py`'s own Reader
   (channel-major 7×i16 per key — matches `src/Game.cpp:586-640`).
5. Identified sprite art: mapSpriteInfo low byte = tile type; `SPRITE_FLAG_TILE` (0x400000 in
   bits 16-31) makes `renderSpriteObject` render `type+257` as a WALL texture
   (`src/Render.cpp:1515-1517,1620-1622`). Verified art via `output/*.png` and a manual
   palette/texels decode of media 696/799/801/802/804 (`UnPackGameData/new*.bin`).
6. Cockpit: `src/Hud.cpp:24-26,620-625`, `src/Hud.h:56-57`, `src/App.cpp:418`,
   `src/MayaCamera.cpp:302-324`, `src/Render.cpp:2934-2939`, `src/ScriptThread.cpp:1710-1714`.

## Verdict

CONFIRMED (with refinement). The boot cinematic is driven by LERP* ops moving large sprites,
but the "descent into the shaft" motion itself is the MayaCamera key path (camera 0 z 948→682);
the lerps animate the environment around it. The "platform" is sp150/151 (type 202 metal-grate
slab) settling in camera 1; the "ship" in the boot shot is implied by the cockpit overlay +
camera 0 + a white flash streak (sp137); the actual ship sprite (sp152, type 198 thruster art)
belongs to the later landing cinematic (camera 9, trigger tiles 581-645).

## Evidence highlights

* Opcode encodings: `src/ScriptThread.cpp:344-398` (LERPSPRITE), `:1388-1441` (OFFSET),
  `:1453-1498` (SCALE), `:1653-1708`/`:1962-2019` (PARABOLA[+_SCALE]), `:1381-1385` (FOG),
  `:1710-1714` (TOGGLE_OVERLAY); flags `src/Enums.h:285-305`; arg readers `:2095-2119` (BE).
* Tick math LINEAR `p=(elapsed<<16)/(travel<<8)`, `v=src+(p*(dst-src<<8)>>16)`:
  `src/Game.cpp:2876-2882`; parabola `sinTable` arc `:2883-2893`; completion → `freeLerpSprite`
  `:2868-2870,3078-3243`; thread resume `:2998-3013`; pool 16 + ERR 36 `:3028-3066`.
* S_NORELINK: only door/secret/entity lerps set it (`Game.cpp:1129,1186`, `Entity.cpp:866`);
  script LERPs never do — their ticks relink every frame (`Game.cpp:2889-2896`).
* Boot flow: `staticFuncs[0]=0`; new-game gate `EVAL v17==0` at IP 0/221 (EVAL semantics
  `ScriptThread.cpp:287-310`, loop `++IP` `:2043`, CALL `IP=ip-1` `:422-431`); chain
  CALL 1913 → 1942/2084/2328 (bytecode IPs 1913-1922).
* Camera 0 keys (map00): (1124,98,948)→(416,453,812)→hover (209,448,792) with roll wobble →
  descend z 792→782→719→682 (keys 9-11, ~3.4 s) — the shaft descent.
* Cockpit: ON at IP 1945 / OFF at 2062 (only around camera 0); drawn in `MayaCamera::Render`
  (`MayaCamera.cpp:316-318`) via `Hud::drawOverlay` (`Hud.cpp:620-625`, cockpit.bmp ×2 into
  cinRect); `Render::drawRGB` suppressed while set (`Render.cpp:2937-2939`); init false
  (`Hud.cpp:24-26`).

## Intro-animated sprite table (map00, art-verified)

| sprites | type (low byte) | art (verified) | flags hi16 | role / motion |
|---|---|---|---|---|
| 135, 136 | 85 (+TILE → wall tex 342) | hazard-striped shaft wall (`output/wall_342.png`) | 0x08F2/0x08F0 (WEST,2-sided,solid) | shaft-wall panels @(248,416)/(248,480); LERPSPRITEOFFSET zrel 32→96, 2000 ms ASYNC (IP 1973/1980) during camera 0 descent |
| 137 | 209 | white flash burst (media 804) | 0x20F0 | engine flare streak (240,447)→(255,447), 1000+1000 ms, then HIDE (IP 2034/2046/2053) |
| 150, 151 | 202 | metal grate platform (media 802 = `output/item_202.png`) | 0x0C30 (EAST+WEST, 2-sided) | THE elevator platform @(128,1376); settle zrel 32→96 / −32→32 1500 ms, bounces 64/0 & 96/32 700 ms + shake/particles/fog (IP 2084-2327) |
| 19 | 69 | npc_bob | — | tiny distant walker (104,383)→(104,575) 3000 ms (IP 2010) |
| 46 | 23 | imp media f0 | — | tiny distant walker (104,352)→(104,544) 3000 ms (IP 2017) |
| 7 / 9 / 10 | 68 / 66 / 72 | npc_major / npc_riley / npc_sarge | — | squad walk-in, camera 5; placement branched on `scriptStateVars[14]` (character choice) |
| 15 / 16 | 23 / 71 | imp / civilian | — | imp-attack demo (PARABOLA h=48 600 ms @2715) |
| 152 | 198 | ship/thruster machinery (media 801) | 0x20F4 | THE SHIP — later landing cinematic (camera 9, IP 3703-3862), not the boot shot |
| 153,154,155,205,206 | 52/194/178/72/72 | misc gear pieces | TILE/2-sided | landing gear rising from below floor zrel −40→…, 800 ms, then HIDE |
| 0 | 130 | obj_fire | — | pad fire, LERPSCALE 128/64 (IP 3820) |

## Cockpit condition (summary)

Draw cockpit.bmp over the 3D view iff `hud->cockpitOverlayRaw` (toggled only by EV_TOGGLE_OVERLAY;
boot intro: ON before STARTCINEMATIC 0, OFF after camera 0 ends). Rendered from MayaCamera::Render
only (i.e., during cinematics/ST_CAMERA).

## Open questions

* Bytecode tail: op 107 at IP 8372 halts the linear disassembly (last 1.6 KB unverified; likely
  data referenced by GOTO/call targets beyond the table, or an opcode variant missing from the port).
* Exact in-game appearance of sp135/136 (wall-tex sprites rising past a descending camera) not
  eyeballed; user confirmation welcome.
* Whether shipped `.ipa` map00 matches `tmp_map00.bin` (regenerated) — same caveat as prior docs.
