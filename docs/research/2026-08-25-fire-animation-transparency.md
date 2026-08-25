# 2026-08-25 — Fire animation & transparency (fire underlay black-box bug)

## Hypothesis

(a) Fire/flame tiles don't animate in the rewrite. Candidates: AUTO_ANIMATE
sprite bit 0x80000, script ENTITY_FRAME ops, or media auto-cycle.
(b) Fire has an underlay/backing sprite whose BLACK background renders opaque;
find which tileNum and what makes it transparent in legacy (palette entry,
renderMode, blend).

## Method

1. Grepped `src/` for TILENUM_OBJ_FIRE/TILENUM_FIRE_BALL/SPRITE_FLAG_AUTO_ANIMATE
   → read `src/Render.cpp:1500-1730` (renderSpriteObject), `src/Game.cpp:340-500`
   (load-time entity setup), `src/Game.cpp:1327-1520` (gsprite anim),
   `src/Render.cpp:2420-2530` (postProcessSprites), `src/GLES.cpp:580-1120`
   (SetupTexture/CreateTextureForMediaID), `src/Span.cpp`, `src/Render.cpp:1885-2115`
   (setupPalette/setupTexture).
2. Decoded `tmp_map00.bin` sprite arrays (offsets verified by walking the
   DEADBEEF/CAFEBABE markers against `src/LoadingManager.cpp:459-569`) and
   `tmp_newMappings.bin`; decoded fire media palettes/texels with the
   tools/extract_textures.py algorithms (no PIL).
3. Cross-checked new_src state (`new_src/render/World3D.cpp:566-579`,
   `new_src/domain/game/Game.cpp:83-143`).

## Verdict

CONFIRMED (both mechanisms identified; both rewrite bugs explained).

1. **Animation** = AUTO_ANIMATE bit 0x80000 injected at map load for tiles
   130/234/136 (`src/Game.cpp:394-397`), frame count = mediaMappings range
   (4 for OBJ_FIRE/FIRE_BALL/ANIM_FIRE — forced constant 4 for 234,
   `src/Game.cpp:380-382`). Runtime cycle `(spriteIndex + time/100) % count`
   at `src/Render.cpp:1544-1546`. Raw map00 contains ZERO 0x80000 bits —
   the user's premise "map00 has none" is right, but the driver is still
   AUTO_ANIMATE; the injection just happens in code, not data. Dynamically
   spawned fire effects use a separate 200 ms gsprite path
   (`src/Game.cpp:1343,1490-1495`). ENTITY_FRAME (ScriptThread.cpp:669-687)
   is a cutscene frame-setter, not the ambient driver.
2. **Transparency**: magenta 0xF81F is the color key for general media
   (TinyGL palette scan src/Render.cpp:2093-2098 + span skip src/Span.cpp:59-67;
   GLES palette zeroing src/GLES.cpp:1047-1054). But fire art has NO magenta:
   decoded palettes of media 726-729 (OBJ_FIRE), 829-832 (FIRE_BALL),
   810-813 (ANIM_FIRE) contain no key and an OPAQUE near-black entry 0 that
   covers ~84% of each frame. The black "underlay/backing" is part of the
   fire texture itself; there is NO separate underlay tile. It is hidden
   purely by renderMode 3 = RENDER_ADD assigned per tile at load
   (`src/Render.cpp:2475-2477`): GL `glBlendFunc(GL_SRC_ALPHA,GL_ONE)`
   (`src/GLES.cpp:648-652`), TinyGL saturating add spans
   (`src/Span.cpp:23-31,171-180`). Adding black = no-op.

## Evidence

* `src/Game.cpp:379-397` — count from mappings; `n6==136||234||130` →
  `mapSpriteInfo |= (n7<<8 | 0x80000)`; 156→2, 236→3 special cases.
* `src/Render.cpp:1544-1546` — 100 ms auto-animate quote above.
* `src/Render.cpp:2468-2495` — postProcessSprites renderMode table
  ({208,234,130,242}→3 ADD, 212→7 SUB, 161→2 BLEND50, 244→4 ADD75).
* `src/GLES.cpp:867-893,938-943,1037-1058` — palette→RGBA5551, index-0 fill,
  magenta→alpha-0 passes. `src/Span.cpp:52-104,171-224`.
* tmp_newMappings.bin ranges: 130→726..730(4), 136→736..737(1),
  223→805..806(1), 234→810..814(4), 242→829..833(4), 193→798..799(1).
* tmp_map00.bin: spawn (19,4); OBJ_FIRE normal sprites tiles (1,16),(12,6),
  (13,4),(13,5) hi=0x00000000; FIRE_BALL z-sprites idx145-148 Z=6/118
  hi=0x00200000 (AUTOMAP_VISIBLE/NOENTITY) — static decorative flames,
  not in the auto-animate injection list; TORCHIERE instances incl. hi
  0x00200000 ones.
* Palette facts (decoded): OBJ_FIRE f0 entry0=(24,28,24,255), transparent
  entries none except stray idx126; FIRE_BALL f0 entry0=(16,24,33,255),
  zero magenta; ANIM_FIRE frames byte-identical to OBJ_FIRE frames;
  STATIC_FLAME(805) == ANIM_FIRE last frame (729).
* Rewrite gaps: `new_src/domain/game/Game.cpp:83-143 loadEntities` lacks the
  0x80000|count<<8 injection (bug a); `new_src/render/World3D.cpp:577-579`
  discards renderMode (bug b) though the runtime cycle exists at :569-571.

## Open questions

* Who calls `gsprite_allocAnim(234/242)` (combat effect spawns) — mechanism
  documented, call sites not enumerated here.
* Whether any map/script ever animates the static FIRE_BALL decorations
  (idx145-148) via ENTITY_FRAME; not observed in map00 bytecode in this pass.

Durable notes: `docs/original-code/fire-animation-transparency.md`.
