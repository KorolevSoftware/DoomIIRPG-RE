# Particle system — spec (G1-G3)

Date: 2026-11-05. ADR: `adr/0047-particle-system.md`.
Facts: `docs/original-code/particles.md` (core), `docs/research/2026-11-04-particle-call-sites.md`
(call sites C1..C16, clears). Every formula below cites `src/`; read those docs for the full
derivations.

## 0. Summary

The original has no particle tick. A spawn writes up to 15 dots + 5 gibs into one of 4
round-robin emitters (64-entry linked-list pool, 59 usable), rolling the shared game RNG 5 times
per particle. Each frame the renderer computes every particle's position analytically from
`gameTime - startTime`, frees the ones that left the ±65536 box, and draws dots (filled
circles) and gib sprites over the world/weapon/fade, under the HUD.

Rewrite layout (one design):

| New / changed | Role |
|---|---|
| `domain/game/ParticleSystem.h/.cpp` (new) | pool + 4 emitters, `spawnParticles` x2, `spawnMonsterBlood`, `freeAllParticles`. Plain member `Game::particles`. |
| `render/ParticleRenderer.h/.cpp` (new) | `drawParticles(...)`: billboard projection, column cull, aging, reaping, dots + gibs. |
| `render/Camera3D` | + `mvpTglInt()` (legacy `TinyGL::mvp`, without the GLES negations). |
| `render/ColumnOcclusion` | + `column(i)` read accessor. |
| `render/World3D` | + `occlusion()` read accessor. |
| `render/Graphics2D` | + `fillCircle`. |
| `ui/UiAssets` | + `Texture gibs[3]`. |
| `core/GameContext.cpp` | draw call after `drawFade`; CinematicCamera wiring. |
| `core/Main.cpp` | `game.particles.init(...)`. |
| `core/LoadingManager.cpp`, `core/CinematicCamera.h/.cpp` | the two clears. |
| `domain/game/Combat.cpp`, `Game.h/.cpp`, `ScriptVM.cpp` | call sites (G3). |

Threading: everything runs on the single GL/game loop thread. New files → re-run the CMake
configure step (GLOB).

## 1. `domain/game/ParticleSystem.h/.cpp` (G1)

### 1.1 Interface

```cpp
#ifndef NEW_DOMAIN_GAME_PARTICLESYSTEM_H
#define NEW_DOMAIN_GAME_PARTICLESYSTEM_H
#include <cstdint>
namespace newcore {
class Entity; class MapData; class MediaLoader; class Tables;

// Port of src/ParticleSystem.cpp minus rendering (render/ParticleRenderer).
class ParticleSystem {
public:
	struct Env {
		const MapData* map = nullptr;        // &g_map (stable address, ADR 0035)
		const MediaLoader* media = nullptr;  // &g_media, monster palettes
		const Tables* tables = nullptr;      // monsterColors (table 8)
		const int64_t* gameTime = nullptr;   // &ctx.gameTime
	};
	void init(const Env& env);               // freeAllParticles() + reads DOOM2RPG_NO_PARTICLES

	static constexpr int kNumEmitters = 4;   // src/ParticleSystem.h:52
	static constexpr int kPoolSize = 64;     // heads 0..3, particles 4..62, free head 63
	static constexpr int kFreeHead = 63;
	static constexpr uint8_t kEnd = 0xFF;
	static constexpr int kGibBoneMask = 0x619;                 // src/ParticleSystem.cpp:244
	static constexpr uint32_t kLevelColors[8] = {              // src/ParticleSystem.h:43
		0xFFFFFFFF, 0xFF3A332E, 0xFF626262, 0xFF7F7F7F,
		0xFFD1CF11, 0xFFFD8918, 0xFF20B00D,
		0xFFFFFFFF };  // [7]: out of bounds in src (7 entries, index & 7); defined white (ADR 0047 §5)

	void freeAllParticles();                                          // src/ParticleSystem.cpp:183-193
	void spawnParticles(int type, uint32_t color, int sprite);        // :252-255
	void spawnParticles(int type, uint32_t color, int x, int y, int z); // :257-414, world (canvas) units
	void spawnMonsterBlood(const Entity* e, bool gib);                // :228-250

	struct Emitter {
		int16_t pos[3] = {0, 0, 0};   // world << 4 (:389-391)
		int gravity = 0;
		uint8_t gibType = 0;          // 0 wood, 1 stone, 2 bone, 255 none
		uint32_t color = 0;           // ARGB, never mutated by the renderer
		int64_t startTime = 0;        // gameTime at spawn
	};
	Emitter emitters[kNumEmitters];
	// Pool (public: the renderer walks and reaps it). size: bit 3 = gib,
	// bits 0..2 = dot radius factor or gib row (src nibble packing dropped).
	uint8_t next[kPoolSize] = {};
	int8_t startX[kPoolSize] = {}, startY[kPoolSize] = {};
	int16_t velX[kPoolSize] = {}, velY[kPoolSize] = {};
	uint8_t size[kPoolSize] = {};

	// Renderer reap: moves the particle right after `prev` to the free list
	// (src/ParticleSystem.cpp:110-111: linkParticle(63, unlinkParticle(n2))).
	void freeAfter(int prev) { link(kFreeHead, unlink(prev)); }

private:
	int unlink(int head);          // :169-176
	void link(int head, int i);    // :178-181
	void freeSystem(int n);        // :195-199
	Env env_;
	int systemIdx_ = 0;
	bool disabled_ = false;        // DOOM2RPG_NO_PARTICLES set
};
}
#endif
```

### 1.2 Algorithms (verbatim)

* `unlink(i)`: `i2 = next[i]; if (i2 != kEnd) { next[i] = next[i2]; next[i2] = kEnd; } return i2;`
* `link(i, i2)`: `next[i2] = next[i]; next[i] = i2;`
* `freeAllParticles()`: `next[63] = kEnd; for i in 0..62: i < 4 ? next[i] = kEnd : link(63, i);`
  Emitter fields are not reset (src). Self-check: `assert` that walking the free list from 63
  visits exactly 59 entries (debug build only, cheap).
* `freeSystem(n)`: `for (i = unlink(n); i != kEnd; i = unlink(n)) link(63, i);`
* RNG: file-static `static int nextByte() { return std::rand() & 0xFF; }` — identical to
  `Combat::nextByte()` (`new_src/domain/game/Combat.cpp:48-50`) and to `Applet::nextByte`
  (`src/App.cpp:506-512`): the SAME global `std::rand()` stream the rest of the rewrite uses.

`spawnParticles(type, color, x, y, z)` (`src/ParticleSystem.cpp:257-414`):

1. `if (disabled_) return;` (verification switch, ADR 0047 §8 — no RNG, no state change).
2. Defaults: `sx=[-64,64) sy=[-64,64) vx=[-128,128) vy=[-32,32) size=[4,7) gravity=-196 dots=15 gibs=0 gibType=255`.
   Per-type overrides — the table in `docs/original-code/particles.md` §3 "Per-type parameters"
   (types 0..8; any other type keeps the defaults). Port the `switch` literally from
   `src/ParticleSystem.cpp:273-379` (note: types 1,2,3,4,8 use `>>= 2` on the sx/sy bounds,
   5/7 `>>= 3`, 6 `>>= 1` on sx only).
3. `idx = systemIdx_; systemIdx_ = (systemIdx_ + 1) % 4; freeSystem(idx);`
4. `e.color = color; e.gravity = gravity; e.gibType = gibType; e.startTime = *env_.gameTime;`
   `e.pos = {(int16_t)(x << 4), (int16_t)(y << 4), (int16_t)(z << 4)}`.
5. `while (gibs > 0 || dots > 0)`: `p = unlink(63); if (p == kEnd) return;`
   * `if (dots > 0) { s = (lo + ((hi - lo) * nextByte() >> 8)) & 7; --dots; }`
     `else { s = (nextByte() % 4) | 8; --gibs; }` (size lo/hi = 4/7 → 4..6)
   * then in this order: `startX[p] = (int8_t)(sxLo + ((sxHi - sxLo) * nextByte() >> 8))`,
     `startY[p]` likewise, `velX[p] = (int16_t)(vxLo + ((vxHi - vxLo) * nextByte() >> 8))`, `velY[p]` likewise.
     Plain signed `int` arithmetic (arithmetic `>>`) gives the same values as src's unsigned
     product for negative spans (`docs/original-code/particles.md` §3).
   * `size[p] = s; link(idx, p);`
   Exactly 5 `nextByte()` per particle, dots before gibs.
6. One stderr line per spawn: `[particles] type=%d color=%08X at (%d,%d,%d) emitter=%d`.

`spawnParticles(type, color, sprite)`: `x = map.mapSprites[sprite]`,
`y = map.mapSprites[map.numSprites + sprite]`, `z = map.spriteZ(sprite)` (legacy baked `S_Z`,
`new_src/domain/world/MapData.h:73-76`), forward. Guard `0 <= sprite < numSprites` else return
(no RNG; src never passes a bad index).

`spawnMonsterBlood(e, gib)` (`src/ParticleSystem.cpp:228-250`):
```
if (!e || !e->def) return;
s     = e->getSprite();
tile  = map.mapSpriteInfo[s] & 0xFF;
mid   = media.mappings().mappings[tile];            // getPalette(tile, 0, 0), src/Render.cpp:2039-2041
pal   = media.paletteIndexFor(mid);
ci    = def->eSubType;
color = 0;                                           // unresolved palette / table index -> black (never in src)
if (pal >= 0 && ci < tables.monsterColors.size()) {
    p = media.palette(pal).colors[tables.monsterColors[ci]];
    color = ((p & 0x7E0) << 5) | ((p & 0xF800) << 8) | ((p & 0x1F) << 3);   // alpha 0, ignored by fillCircle
}
dz   = (def->eType == ET_CORPSE) ? -26 : 0;
type = (gib && ci < 32 && (kGibBoneMask & (1 << ci))) ? 4 : 1;
spawnParticles(type, color, X(s), Y(s), map.spriteZ(s) + dz);
```

### 1.3 Wiring

* `domain/game/Game.h`: `#include "domain/game/ParticleSystem.h"`; member
  `ParticleSystem particles;   // src app->particleSystem (ADR 0047); wired in Main` next to `zoom`.
* `core/Main.cpp`, right after `game.items.init(...)`:
  `game.particles.init({ &g_map, &g_media, &tables, &ctx.gameTime });`
  (src `startup()` at `src/App.cpp:117`; gib images are G2's `UiAssets`).
* Clear on map load — `core/LoadingManager.cpp` `onEntitiesLoaded()`, immediately before
  `env_.player->levelInit();`: `env_.game->particles.freeAllParticles();   // src/LoadingManager.cpp:665`.
* Clear on cinematic skip — `core/CinematicCamera.h` `Env` gains `ParticleSystem* particles = nullptr;`
  (forward-declare the class); `core/GameContext.cpp` init sets `cinEnv.particles = &sys_.game->particles;`;
  in `skipCinematicNow()`, after `snd.allowSounds = savedAllow;`:
  `if (env_.particles) env_.particles->freeAllParticles();   // src/Game.cpp:2537`
  (after the thread fast-forward, so bursts spawned while skipping are cleared, as in src).
  Update the stale "particles have no rewrite counterpart" comment there.

### 1.4 Acceptance (G1)

* Configure + build clean (Debug).
* Golden check ALL IDENTICAL (nothing spawns yet; the clears touch only the pool).
* Boot log unchanged except nothing; no `[particles]` lines.

## 2. Rendering (G2)

### 2.1 Small accessors

* `render/Camera3D.h/.cpp`: member `int mvpTgl_[16]`; in `setView`, right after
  `buildProjectionMatrix(viewFov, viewAspect, projection_)` and BEFORE the GLES negations:
  `multMatrix(view_, projection_, mvpTgl_);` Accessor
  `const int* mvpTglInt() const { return mvpTgl_; }   // TinyGL::mvp before gles::BeginFrame (src/TinyGL.cpp:188-190)`.
  Nothing else changes (world MVP, golden frames untouched).
* `render/ColumnOcclusion.h`: `int column(int i) const { return columnScale_[i]; }`.
* `render/World3D.h`: `const ColumnOcclusion& occlusion() const { return occl_; }` — the final
  buffer of the last `drawBSP` (filled every world pass, `new_src/render/World3D.cpp:1401-1402`).
* `render/Graphics2D.h/.cpp`:
  ```cpp
  // Legacy Graphics::fillCircle (src/Graphics.cpp:38-86: opaque 16-gon fan, current color).
  // Row spans through fillRect, so the active clip applies. r <= 0 draws nothing.
  void fillCircle(int cx, int cy, int r, uint8_t red, uint8_t green, uint8_t blue);
  ```
  Body: `if (r <= 0) return; for (dy = -r; dy < r; ++dy) { float yc = dy + 0.5f;
  int hw = (int)(std::sqrt((float)(r * r) - yc * yc) + 0.5f); if (hw > 0) fillRect(cx - hw, cy + dy, 2 * hw, 1, red, green, blue); }`
  (radius is at most 3 px here: `6 * 1024 >> 11`).
* `ui/UiAssets.h/.cpp`: `Texture gibs[3];   // [0] wood, [1] stone, [2] bone (src/ParticleSystem.cpp:161-163)`,
  loaded in `UiAssets::load` with `loadUiTexture(store, read, "woodgibs_24.bmp", gibs[0])`,
  `"Stonegibs_24.bmp"` → `gibs[1]`, `"Skeletongibs_24.bmp"` → `gibs[2]` (exact archive case;
  each 144x192 = 3 size columns x 4 rows of 48x48).

### 2.2 `render/ParticleRenderer.h/.cpp`

```cpp
namespace newcore {
class Camera3D; class ColumnOcclusion; class Graphics2D; class ParticleSystem; class Texture;
// Port of ParticleSystem::renderSystems + ParticleEmitter::render
// (src/ParticleSystem.cpp:32-137, :201-226). Mutates `ps`: dead particles
// are reaped here, as in src (there is no update tick).
void drawParticles(Graphics2D& g, ParticleSystem& ps, const Camera3D& cam,
                   const ColumnOcclusion& occl, int64_t gameTime, bool grayscale,
                   const Texture (&gibs)[3]);
}
```

Constants (TinyGL viewport = viewRect {0,20,480,250} inset 1 → (1,1,478,248),
`src/TinyGL.cpp:141-144,149-167`, `src/Canvas.cpp:124-127`):
```
kXScale = 478 << 2 (1912)   kXBias = ((1 + 239) << 3) - 4 (1916)
kYScale = 248 << 2 (992)    kYBias = ((1 + 124) << 3) - 4 (996)
kOffX = 0, kOffY = 6        // TinyGL raster -> canvas: world band is canvas (1,7,478,248) (ADR 0009); ADR 0047 §3
kBand = {1, 7, 478, 248}    // final clip
kRotationSequence[4] = {0, 4, 2, 6}   // src/ParticleSystem.cpp:129 / .h:44
```

Per emitter `i = 0..3` (`src/ParticleSystem.cpp:32-137`):
```
p = ps.next[i]; if (p == kEnd) continue;
t  = (int)(((gameTime - e.startTime) << 10) / 1000);
t2 = (int64)t * t >> 10;
yaw = cam.viewYaw(); sin = cam.sinTable();
rx = 64 * sin[yaw & 0x3FF] >> 12;  ry = 64 * sin[(yaw + 256) & 0x3FF] >> 12;
v0 = (pos0 + rx, pos1 + ry, pos2 + 1024); v1 = (v0.x, v0.y, v0.z - 2048);
v2 = (v0.x - 2*rx, v0.y - 2*ry, v0.z);
for each v: m = cam.mvpTglInt();                       // transform3DVerts, src/TinyGL.cpp:261-276
   X = (int)((int64)x*m[0] + (int64)y*m[4] + (int64)z*m[8]  >> 14) + m[12];  (Y: m1,m5,m9,m13;
   Z: m2,m6,m10,m14; W: m3,m7,m11,m15)
if (any W + Z < 0) continue;                           // near clip (:56-58); then W >= 256, no div-by-0
project (src/TinyGL.cpp:516-524): x = kXBias + X*kXScale/W; y = kYBias + Y*kYScale/W; z = 0x7FFFFF / W
depth = 0x7FFFFF / v0.z;  col = ((v0.x >> 3) + (v2.x >> 3)) >> 1;
if (col < 0 || col >= 480 || depth > occl.column(col)) continue;      // :60-68
W8 = v1.x - v2.x;  H8 = v1.y - v0.y;                    // 1/8 px
left = v2.x >> 3; top = v0.y >> 3; bw = W8 >> 3; bh = H8 >> 3;        // :69-97
if (left < 0) { bw += left; left = 0; }  if (top < 0) { bh += top; top = 0; }
if (left + bw > 480) bw = 480 - left;    if (top + bh > 250) bh = 250 - top;
if (bw < 0 || bh < 0) continue;
clip = intersect({left + kOffX, top + kOffY, bw, bh}, kBand);  g.setClip(clip);   // w/h <= 0 clips all
cx = (v1.x + v2.x) >> 4;  cy = (v0.y + v1.y) >> 4;      // :98-99
c  = grayscale ? toGray(e.color) : e.color;            // ADR 0047 §4a, no write-back
prev = i;
while (p != kEnd) {
   s = ps.size[p];
   dx = ((int64)ps.startX[p] << 10) + (int64)ps.velX[p] * t;
   dy = ((int64)ps.startY[p] << 10) - ((int64)ps.velY[p] * t + t2 * e.gravity);
   if (dx < -65536 || dx > 65536 || dy < -65536 || dy > 65536) {     // :110-112
       ps.freeAfter(prev);                     // prev stays
   } else {
       px = cx + (int)(dx * W8 >> 20) + kOffX;  py = cy + (int)(dy * H8 >> 20) + kOffY;   // :115-116 (a = W8)
       if (!(s & 8)) g.fillCircle(px, py, (s & 7) * std::min(W8, 1024) >> 11, R(c), G(c), B(c));   // :117-119
       else {
           scale = std::min((W8 * 3) >> 11, 3);                                            // :121-130
           if (scale >= 1 && e.gibType < 3) {
               rot = (((p << 6) + t) >> 8) & 3;  if (ps.velX[p] < 0) rot = 3 - rot;
               g.drawRegion(gibs[e.gibType], (3 - scale) * 48, (s & 7) * 48, 48, 48, px, py,
                            Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter, kRotationSequence[rot]);
           }
       }
       prev = p;
   }
   p = ps.next[prev];
}
```
After the 4 emitters: `g.clearClip()`.

`toGray(c)` = `Render::convertToGrayscale` (`src/Render.cpp:2692-2696`):
`n2 = (c & 0xFF) + (((c >> 16) & 0xFF) - (c & 0xFF) >> 1); n3 = n2 + (((c >> 8) & 0xFF) - n2 >> 1);
return 0xFF000000 | (n3 >> 1) << 16 | (n3 >> 1) << 8 | (n3 >> 1);` (signed `int` math).

Gibs are not grayscaled (src only recolors `setColor`).

### 2.3 Draw call — `core/GameContext.cpp` render

Directly after `sys_.hud->drawFade(g, upTimeMs);` (legacy slot: after REPAINT_VIEW3D/fade,
before swipe/HUD, `src/Canvas.cpp:391-417`; before the letterbox fills, which therefore cover
rows 0..41 / 292..319 in `ST_CAMERA`):
```cpp
// REPAINT_PARTICLES is set only by the playing/dialog/camera/dying runners
// (src/GameStateRunner.cpp:82,192,268,275,291,309,316, src/Canvas.cpp:923,
// src/MovementController.cpp:393,521).
if (sys_.game->loadMapID != 0 && sys_.world->initialized() &&
    (cinePose != nullptr || state_ == StateId::Playing || state_ == StateId::Dialog ||
     state_ == StateId::Camera || state_ == StateId::Dying || state_ == StateId::BotDying)) {
	drawParticles(g, sys_.game->particles, scene_.camera(), sys_.world->occlusion(), gameTime,
	              sys_.player->isFamiliar,   // postProcessMode != 0 (src/Player.cpp:2500-2516), same gate as drawFamiliarOverlay
	              sys_.ui->art().gibs);
}
```
(`sys_.ui` is non-null in the game; guard with `sys_.ui != nullptr` like `travelEnv.art`.)

### 2.4 Acceptance (G2)

* Build clean; golden ALL IDENTICAL (pool still empty → `drawParticles` returns per emitter
  without drawing; `mvpTgl_` is a pure addition).
* Boot log: no `UiAssets: missing resource` for the three gib sheets.

## 3. Call sites (G3)

All colors as `uint32_t` literals. Hooks in original order relative to neighbours (RNG order).

| Site | File / place | Code |
|---|---|---|
| C2 monster hit (`src/Combat.cpp:908-910`) | `domain/game/Combat.cpp` `explodeOnMonster`, right after `env_.game->entityPain(curTarget, totalDamage);` | `if (attackerWeaponId != 14) env_.game->particles.spawnMonsterBlood(curTarget, false);` — trim the "blood particles ... skipped" comment to knockback/splash/heal. |
| C3 corpse gib (`src/Combat.cpp:938-945`) | `Combat.cpp` corpse branch (replace the "deferred" log) | `const int s = curTarget->getSprite(); env_.map->mapSpriteInfo[s] \|= Enums::SPRITE_FLAG_HIDDEN; isGibbed = true; env_.game->particles.spawnMonsterBlood(curTarget, true); env_.game->spawnDropItem(curTarget); Sound::instance().playSound(1037, 0, 3);` Add `bool isGibbed = false;` to `Combat.h`; reset `isGibbed = false;   // :189` as the first line of the `stage == 0` block in `playerSeq` (`new_src/domain/game/Combat.cpp:642-643`, before `totalDamage = 0`). Guard `s` in range. |
| C3 helper | `domain/game/Game.h/.cpp` new overload `void spawnDropItem(Entity* e);` | port of `src/Game.cpp:2692-2723`, see §3.1 |
| C9 barricade (`src/Entity.cpp:374-377`) | `domain/game/Game.cpp` `painProp`, between the frame write and `playSound(1038…)` | `particles.spawnParticles(2, 0xFFFFFFFFu, sprite);` |
| C10 crate/furniture (`:380-382`) | `painProp` else-arm, before the PICKUP test | `if (sub == Enums::INTERACT_CRATE \|\| sub == Enums::INTERACT_FURNITURE) particles.spawnParticles(2, 0xFF7F7F7Fu, sprite);` |
| C11 pickup → spout (`:383-385`) | `painProp` PICKUP arm, before `turnEntityIntoWaterSpout(e)` | `particles.spawnParticles(1, 0xFFFFFFFFu, sprite);` — update the two stale "has no subsystem yet" comments. |
| C15 `EV_MONSTER_PARTICLES` (`src/ScriptThread.cpp:911-919`) | `domain/game/ScriptVM.cpp` case | `const int sprite = readUByte(t); if (Entity* ent = env_.game->db.findEntityBySprite(sprite)) env_.game->particles.spawnMonsterBlood(ent, false);` (findEntityBySprite = the house S_ENT analog) |
| C16 `EV_SPAWN_PARTICLES` (`:921-936`) | `ScriptVM.cpp` case | see §3.2 |

### 3.1 `Game::spawnDropItem(Entity* e)` (`src/Game.cpp:2692-2723`)

```cpp
void Game::spawnDropItem(Entity* e) {
	const bool b = e->hasLootSet;
	const bool b2 = e->monster == nullptr ? (b && e->param == 0)
	                                      : (b && (e->monster->flags & 0x800) == 0);
	if (!b2) return;
	const int n = e->lootSet[0];
	const int cls = (n >> 12) & 0xF;
	if (cls == 6) return;
	const int idx = (n & 0xFC0) >> 6, cnt = n & 0x3F;
	if (cnt <= 0) return;
	const EntityDef* def = defs_->find(Enums::ET_ITEM, cls, idx);
	if (def == nullptr) { std::fprintf(stderr, "[drop] Cannot find a def for the dropItem.\n"); return; }  // src Error 117
	if (def->tileIndex == 0) return;
	const int s = e->getSprite();
	spawnDropItem(map_->mapSprites[s], map_->mapSprites[map_->numSprites + s], def->tileIndex, def, cnt, false);
}
```

### 3.2 `EV_SPAWN_PARTICLES`

```cpp
case Enums::EV_SPAWN_PARTICLES: {
	// u8 packed: bit 7 = tile-position flag (mask 0x80), bits 3..6 type, bits 0..2
	// color index; u16 pos; u8 z+48 (src/ScriptThread.cpp:921-936).
	const int packed = readUByte(t);
	const int pos = readUShort(t);
	const int z = readUByte(t) - 48;
	const int type = (packed >> 3) & 0xF;
	const uint32_t color = ParticleSystem::kLevelColors[packed & 0x7];   // [7] = white, ADR 0047 §5
	if (packed & 0x80) {
		const int x = (((pos >> 11) & 0x1F) << 6) + 32;
		const int y = (((pos >> 6) & 0x1F) << 6) + 32;
		env_.game->particles.spawnParticles(type, color, x, y, z + env_.map->heightAt(x, y));
	} else {
		env_.game->particles.spawnParticles(type, color, pos);   // pos = sprite index; z ignored
	}
	break;
}
```
(Use whatever map pointer the VM already uses for `heightAt`; `MapData::heightAt` = legacy
`getHeight`.)

### 3.3 Deferred sites (their gameplay path is not ported) — the exact hook to add

| Site | When the path is ported, add |
|---|---|
| C1 reflection (`src/Combat.cpp:444`), at `new_src/domain/game/Combat.cpp:263` | `env_.game->particles.spawnMonsterBlood(curAttacker, false);` before `curAttacker`'s pain call |
| C6 thorn trail (`src/Combat.cpp:1277`), in-flight branch near `Combat.cpp:1018` | `env_.game->particles.spawnParticles(1, 0xFF81603Du, x, y, env_.map->heightAt(x, y));` |
| C7 item impact (`src/Combat.cpp:1344-1345`), near `Combat.cpp:1029` | `env_.game->particles.spawnParticles(1, 0xFFFFFFFFu, missileSprite); Sound::instance().playSound(1135, 0, 4);` |
| C8 thorn impact (`src/Combat.cpp:1365`), near `Combat.cpp:1029` | `env_.game->particles.spawnParticles(1, 0xFF81603Du, x, y, z + 16);` (x/y/z = missile pos, before `z += 48`) |
| C12 Vios teleport (`src/Entity.cpp:735,743`), `MonsterAI.cpp:501-504` | `particles.spawnParticles(7, 0xFFFFFFFFu, sprite);` before `playSound(1123, 0, 1)` and again after the relink (MonsterAI Env gains `ParticleSystem*`) |
| C13 resurrect (`src/Entity.cpp:1368`), last statement of a future `resurrect` | `particles.spawnParticles(1, 0xFFFD8918u, sprite);` (all four callers inherit it) |
| C4, C5, C14 | dead in src — never hook |

### 3.4 Golden route and RNG

The golden route fires C16 only. The map00 boot intro (init script `CALL_FUNC func_1913` at
IP 228 → `func_2084` camera 1, `func_2328` camera 5) runs `EV_SPAWN_PARTICLES` at IPs 2127
(type 5, color 4 yellow, tile (1,21)), 2656 (type 6, yellow), 2687 (type 1, grey), 2694 (type 7,
grey), 2784 (type 1, green), all at tile (5,19) for the last four
(`python3 tools/disasm_map_scripts.py tmp_map00.bin`). Total 8+15+15+15+15 = 68 particles =
340 extra `std::rand()` calls. C2/C3/C9-C11/C15 need player attacks or other scripts and are not
on the route.

Expected effect with G3: ticks 200/400/500 (intro) may show particles if a burst is alive
(about 1 s) at the capture tick; ticks 750/850 and menu ticks can differ only through
RNG-dependent state (the 340-call shift moves every later `std::rand()` roll). Both are
legitimate. Verification:
1. `DOOM2RPG_NO_PARTICLES=1` + the golden command → must be ALL IDENTICAL (proves every other
   G3 edit is inert).
2. Without it → list the differing ticks; the user reviews them; the orchestrator re-baselines
   `tests/golden/frames/` only after user approval.

### 3.5 Acceptance (G3)

* Build clean; check (1) above ALL IDENTICAL.
* stderr shows five `[particles] type=5/6/1/7/1 ...` lines during the map00 intro.
* User sees on screen:
  * intro, camera 1 after the explosion sound/screen shake: a small burst of yellow dots that
    flies out and falls; camera 5: yellow, grey and green bursts near the same spot;
  * in play: hitting a monster → a short upward spray of dots in the monster's blood color
    (red for most, other colors for some types); smashing a crate/furniture → grey dots;
    breaking a barricade → white dots; smashing a toilet/sink → white jet; chainsaw on a
    corpse → corpse vanishes, bone or blood spray with tumbling 48 px gib pieces (bone types:
    zombie, imp, saw goblin, revenant, arch-vile), gib sound 1037, possible loot drop;
  * dots/gibs are hidden behind walls (whole burst at once), drawn over the weapon, under the
    HUD, never outside the 3D view band; in sentry-bot view the dots are gray (constant, not
    fading to black).

## 4. Groups

| Group | Files | Acceptance |
|---|---|---|
| G1 core | `domain/game/ParticleSystem.h/.cpp` (new), `Game.h` member, `core/Main.cpp` init, `core/LoadingManager.cpp` clear, `core/CinematicCamera.h/.cpp` + `core/GameContext.cpp` (`cinEnv.particles`) clear | §1.4 |
| G2 render | `render/ParticleRenderer.h/.cpp` (new), `Camera3D`, `ColumnOcclusion.h`, `World3D.h`, `Graphics2D`, `UiAssets`, `core/GameContext.cpp` draw call | §2.4 |
| G3 hooks | `Combat.h/.cpp` (C2, C3, `isGibbed`), `Game.h/.cpp` (C9-C11, `spawnDropItem(Entity*)`), `ScriptVM.cpp` (C15, C16) | §3.5 |

G1 → G2 → G3 strictly in order (G2 needs the pool, G3 needs both). Each needs a CMake
reconfigure only when it adds files (G1, G2).
