# ADR 0047 — Particle system: domain pool on `Game`, render-time aging in `render/ParticleRenderer`, world-band placement

Date: 2026-11-05. Status: accepted. Spec: `../particles.md`.
Facts: `docs/original-code/particles.md`, `docs/research/2026-11-04-particle-call-sites.md`.

## Context

The original's `ParticleSystem` (`src/ParticleSystem.cpp`) has 4 round-robin emitters and a
64-entry linked-list pool (59 usable particles). Each spawn rolls the shared global `std::rand()`
5 times per particle. There is no update tick: the renderer computes positions from
`gameTime - startTime`, frees the particles that leave the box, and draws filled-circle dots
and 48 px gib sprites inside a camera-facing billboard. A whole emitter is culled by the
TinyGL column-depth buffer. Callers sit in combat, entity pain, and two script opcodes. The
rewrite has the equivalent pieces: `Camera3D` (14.14 matrices), `ColumnOcclusion`
(ADR 0031), `Graphics2D` with a real clip, `UiAssets`, and the `gameTime` clock.

## Decision

1. **Placement.** State and spawning live in `domain/game/ParticleSystem`, a plain member
   `Game::particles`. This follows the `Game::combat` / `lerps` / `zoom` pattern, and every
   caller already holds `Game`. Drawing is the free function `drawParticles` in
   `render/ParticleRenderer`. It takes the pool non-const because it reaps dead particles, as
   the original does. The pool keeps the legacy linked lists (heads 0..3, free head 63, LIFO),
   so draw order and pool-exhaustion behaviour match the original. The 4-bit size packing is
   dropped: one `uint8_t` per particle.
2. **RNG.** The pool calls `std::rand() & 0xFF`, the same global stream as `Combat::nextByte`
   and `ItemPickup::nextInt` (`src/App.cpp:506-512`). Each particle makes 5 calls in src order
   (size, startX, startY, velX, velY), dots before gibs, and stops early when the pool is
   empty. Hooks sit at the original statement positions, so the call order matches the
   original.
3. **Screen placement (deviation from the port).** Particles are projected with the legacy
   TinyGL `mvp` (`Camera3D::mvpTglInt`, built before the GLES negations) and the gameplay
   viewport constants (1,1,478,248). The result is mapped to canvas with offset (0,+6), which
   is the world band (1,7,478,248) of ADR 0009. The clip is the billboard box intersected with
   that band, in every state. In the port, the GLES path moves the world to canvas y=7 but
   leaves 2D particles where the software path put them. Gibs land at raster+(1,21), which is
   15 px below their world point, and 59 px below it in `ST_CAMERA`, where the viewport moves
   to `cinRect`. Dots land at raster+(0,0), so dots and gibs of one burst are 21 px apart. That
   is a port artefact, not game behaviour, so we follow the world.
4. **Original bugs.**
   a. *Familiar grayscale write-back* (`src/ParticleSystem.cpp:100-103`): src stores
      `convertToGrayscale(color)` back into the emitter every frame, and gray-of-gray halves,
      so dots fade to black. **Decision:** apply the grayscale once at draw time and never
      mutate `color`. Dots are a constant gray while the bot view is on, and recover their
      color afterwards. The gate is `player->isFamiliar`, the same gate as the familiar
      overlay.
   b. *Port `fillCircle` ignores the translation and the clip* (`src/Graphics.cpp:58-62`).
      **Decision:** dots use the same offset and clip as gibs (`Graphics2D::fillCircle` emits
      clipped `fillRect` row spans).
   c. *Size shift*: the port uses `>> 11`, J2ME used `>> 10` (`src/ParticleSystem.cpp:117-124`).
      **Decision:** `>> 11`, because `src/` is the ground truth. Dot radius ≤ 3 px, gib column
      `min((W*3)>>11, 3)`.
5. **`levelColors[7]`.** `EV_SPAWN_PARTICLES` indexes a 7-entry table with `& 7`, so index 7 is
   out of bounds in src. **Decision:** it is white (`0xFFFFFFFF`), as the 8th table entry.
   map00 uses only indices 2/4/6.
6. **`fillCircle` as row spans** in `Graphics2D`, not a new triangle-fan device primitive. The
   radius never exceeds 3 px, and fillRect already respects the clip and batching.
7. **Overflow-safe intermediates.** The vertex transform sums, `t*t`, `vel*t` and `d*W` are
   computed in `int64_t`. Results are identical to src wherever src does not overflow. Where
   src overflows (very close emitters, long-lived type-0 bursts), src had undefined behaviour,
   and we produce the mathematically correct value.
8. **Verification switch.** `DOOM2RPG_NO_PARTICLES` makes every spawn a no-op (no RNG, no
   state). The golden route spawns intro particles (spec §3.4), so frames can legitimately
   change. Running with the switch must stay ALL IDENTICAL, which proves the rest of the hook
   work is inert.
9. **Draw gate.** Particles are drawn when the world pass ran and the state is
   Playing/Dialog/Camera/Dying/BotDying, or a cinematic owns the view. These are the runners
   that set `REPAINT_PARTICLES`. The slot is right after `drawFade` and before the letterbox
   and HUD.
10. **C3 corpse gib is ported now.** It needs only the hide bit, `spawnMonsterBlood`, a
    25-line `Game::spawnDropItem(Entity*)` and sound 1037, and its trigger (chainsaw on an
    adjacent corpse) is already elected by `Targeting`. C1, C6-C8, C12 and C13 wait for their
    gameplay paths; their one-line hooks are listed in the spec. C4, C5 and C14 are dead in
    src.

## Consequences

* Five intro bursts on map00 shift the global RNG by 340 calls. Golden frames will differ once
  G3 lands. The user reviews the diffs, then the frames are re-baselined. The existing Hud shake
  LCG is a separate, older RNG deviation and is not changed here.
* Dead particles are reaped only in drawn frames. If the rewrite draws in different frames than
  src, the free list can differ, but only when the pool is exhausted (more than 59 live
  particles).
* `gameTime` freezes in the menu, so bursts freeze with it, as in src.
* Gib sprites keep their native 48 px size per size column, with no distance scaling, as in src.
* `Camera3D` carries one more 4x4 matrix. The world MVP is unchanged.

## Rejected alternatives

* **Process-wide singleton** (like `Sound`, ADR 0046). The pool is per-game state with no
  device, and `Game` is already reachable from every caller.
* **3D billboards in `World3D` with GPU depth.** That changes cull semantics: src culls the
  whole burst by column depth, with no per-particle depth test. It would also touch the
  recorded world command list.
* **Verbatim port offsets** (raster+(1,21) for gibs, raw raster for dots). Faithful to a port
  bug, but bursts would visibly detach from barricades and crates, and in cinematics they
  would sit 59 px low.
* **A separate update tick.** src has none, and aging at draw time keeps the timing exact.
