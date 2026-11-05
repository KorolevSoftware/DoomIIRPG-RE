# 2026-11-04 — Original particle system core

**Question**: exact data layout, spawn/aging/render behaviour of `src/ParticleSystem.cpp/.h`
for a 1:1 port (layout, startup/free, spawn overloads, RNG order, aging clock,
projection/occlusion/layering, sounds).

**Method**: read `src/ParticleSystem.cpp:1-414`, `src/ParticleSystem.h:1-76` fully; followed
`TinyGL::transform3DVerts/projectVerts/occludeClippedLine` (`src/TinyGL.cpp:261-276,516-524,720-745`),
`Graphics::fillCircle/drawRegion/clipRect/setScreenSpace` (`src/Graphics.cpp:38-86,308-394,688-753`),
`Canvas::backPaint` (`src/Canvas.cpp:381-424`), `renderScene` (`src/Canvas.cpp:1330-1352`),
gameTime advance (`src/Canvas.cpp:769-771`), RNG (`src/App.cpp:506-512`), table load
(`src/App.cpp:366,381,397`) and all call sites (grep `spawnParticles|spawnMonsterBlood`).

**Verdict**: CONFIRMED / documented in `docs/original-code/particles.md`.

**Key findings**
- 4 emitters round-robin, list heads 0..3, free head 63, 59 usable particles (`src/ParticleSystem.cpp:183-193`).
- 5 `std::rand()` calls per particle via shared `nextByte()` — affects game RNG (`src/ParticleSystem.cpp:399-409`, `src/App.cpp:506-512`).
- No update tick: positions are analytic in `t = (gameTime-startTime)*1024/1000`; particles die only when leaving the ±65536 billboard box (`src/ParticleSystem.cpp:39-40,108-112`).
- Emitter = 32x128 world-unit view-facing billboard; whole emitter culled by near-clip and a single centre-column `columnScale` test (`src/ParticleSystem.cpp:41-68`).
- Dots `fillCircle(size*min(W,1024)>>11)`; gibs 48x48 cells of `*gibs_24.bmp` with rotationSequence {0,4,2,6} (`src/ParticleSystem.cpp:117-130`).
- Drawn in `backPaint` after the 3D view+weapon, before HUD (`src/Canvas.cpp:404-416`).
- Quirks: grayscale written back into emitter color each frame (darkens) (`src/ParticleSystem.cpp:100-101`, `src/Render.cpp:2692-2696`); port `fillCircle` ignores transX/clip (`src/Graphics.cpp:58-62`).
- No sounds inside ParticleSystem; 1037 is played by `Combat::explodeOnMonster` right after the gib spawn (`src/Combat.cpp:942-944`).

**Open**: number of entries in table 8 and the actual palette indices (needs the .ipa table data; not read).
