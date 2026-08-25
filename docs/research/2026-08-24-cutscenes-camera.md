# Research log — 2026-08-24 — Scripted cutscenes, Maya cameras, map00 intro

## Hypothesis
Scripted cutscenes (map00: NPC gun-give dialog → auto player move → tutorial) are driven
by `ScriptThread` opcodes + `MayaCamera` key tables; the rewrite can reproduce them by
reusing gameplay movement (`dest*` writes) and the camera-key interpolation.

## Method
1. Grepped `src/` for `ST_CAMERA|ST_INTER_CAMERA`; read `ScriptThread.cpp` (full),
   `MayaCamera.cpp`, `MovementController.cpp`, `GameStateRunner.cpp`,
   `DialogSystem.cpp`, `InputEventController.cpp`, `PlayingInputHandler.cpp`,
   `Game.cpp` (lerp/thread/camera sections), `LoadingManager.cpp`, `Resource.cpp`,
   `Render.cpp` fade section, `LerpSprite.cpp`.
2. Wrote a throwaway parser for `tmp_map00.bin` following the exact load order of
   `src/LoadingManager.cpp:304-569` + `src/Game.cpp:586-640` to extract tileEvents,
   staticFuncs, bytecode and the 14 maya cameras; wrote a bytecode disassembler from the
   switch in `src/ScriptThread.cpp:243-2046`.
3. Cross-checked decoder against raw bytes (hand decode) and against independent
   consumers (`tools/map_to_obj.py` parses the same file layout).

## Verdict
CONFIRMED (with two corrections found along the way):
- Map container is little-endian; **script bytecode operands are BIG-ENDIAN**
  (`getUShortArg/getIntArg`, `src/ScriptThread.cpp:2104`, `:2116`). First disassembly
  attempt used LE and produced garbage (e.g. "cam=17", "ev=13952") — fixed.
- There is no third sprite coord array; X(261)+Y(261)+infoLow(261) bytes are contiguous
  with a single marker AFTER them (`src/LoadingManager.cpp:488-512`) — an early parser
  inserted a marker in between and desynced.

## Key extracted facts (see docs/original-code/cutscenes-camera.md for full citations)
- States set by EV_STARTCINEMATIC (ST_CAMERA) / EV_START_INTERCINEMATIC (ST_INTER_CAMERA);
  end-of-keys `MayaCamera::Snap` → ST_PLAYING; dialogs restore prior state.
- Boot chain map00: spawnPlayer(4,19,E) → executeStaticFunc(0) → type-8 spawn-tile event
  (help dlg41) → new-game tail CALL@1889 = var5=1 + STARTCINEMATIC cam=4 + FADEOP OUT +
  door ops interleaved with ADV_CAMERAKEY resumes=9/3.
- Camera keys: 7 channels (x,y,z,pitch,yaw,roll,ms), -2 = inherit player pose,
  125 Hz sampleRate on map00, tween i8 deltas, formulas captured.
- Player move-by-script = EV_GOTO writes dest*, parks thread via `gotoThread`;
  interpolation is the normal updateView glide.

## Open questions / not fully resolved
- Which exact NPC sprite hands over the pistol: two GIVEITEM mode=1 sites (@4175 def111,
  @4782 def110) — entity-def table lookup needed to name the item (def ids are into
  entityDefManager, not readable without parsing that table).
- Exact semantic split between type nibbles 1/2/4/8 beyond what call sites show:
  1=step-on, 2=step-off, 4=fire-facing-tile, 8=facing-standing (inferred from
  `eventFlagsForMovement`/`flagForFacingDir` call sites).
- ST_INTER_CAMERA rendering path appears to keep player-view render while scripts run;
  original J2ME used it as "driving" state — kept as-is in doc, low confidence on intent.
- var15/var17 initial values come from save/config paths not traced here.

## Artifacts
- Parser/disassembler: ephemeral (heredoc scripts); outputs saved at /tmp/map00_disasm.txt,
  /tmp/map00_disasm2.txt (not part of repo).
- Curated doc: docs/original-code/cutscenes-camera.md
