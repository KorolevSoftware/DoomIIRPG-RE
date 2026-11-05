# 2026-11-05 Fire view kick vs weapon-sprite recoil

**Hypothesis (user):** on fire the original moves the camera, not the weapon sprite.

**Verdict: PARTIAL.** The camera kick is real (`Render::rockView`, 2 units
back, rocket 6, cosine return over SHOTHOLD*10 ms, mask WP_NORECOIL excludes
chainsaw/holy water/plasma/soul cube — `src/Combat.cpp:326-332`,
`src/Render.cpp:2248-2253,2962-2968`). But the original ALSO moves the
weapon sprite (atk pose -> idle 16.16 lerp, `src/Combat.cpp:735-767`). The
rewrite has the sprite lerp; it lacks only the rock
(`new_src/domain/game/Combat.cpp:736` comment "rockView recoil skipped").

**Method:** grep `shake|kick|recoil` in `src/` -> `WP_NORECOIL = 9222`
(`src/Enums.h:162`) = 0x2406 -> literal mask at `src/Combat.cpp:326` ->
`rockView` -> its consumer in `Render::render`. Cross-checked against the
two other rockView callers (`src/PlayingInputHandler.cpp:485`,
`src/Combat.cpp:1015`) and the shake path (`src/Canvas.cpp:1000-1018`,
`src/MovementController.cpp:381-389`, `src/Render.cpp:2265-2270`) — shake
is not on the fire path except rocket impact (`src/Combat.cpp:1296`).

**Key arithmetic:** `n8 = sinTable[elapsed*256/dur + 256] >> 8` =
`256*cos(pi/2*t/dur)`; `view += n8*(target-view) >> 8` (render units,
world<<4). Instant full offset, quarter-cosine ease back. Clock upTimeMs.

Curated: `docs/original-code/fire-view-kick.md`.
