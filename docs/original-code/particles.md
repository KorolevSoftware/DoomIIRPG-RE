# Particle system (`src/ParticleSystem.cpp/.h`)

Verified facts about the original particle core. Every claim cites `src/`.
Related: `combat.md` (call sites), `cutscenes-camera.md` (ST_CAMERA clip), `rendering.md`.

## 1. Data layout

### ParticleEmitter (4 instances, `src/ParticleSystem.h:13-32`)

| Field | Type | Meaning |
|---|---|---|
| `systems` | `ParticleSystem*` | back pointer, set in `startup()` (`src/ParticleSystem.cpp:154-156`) |
| `pos[3]` | `short` | emitter origin in **world<<4** units: `(short)(x << 4)` etc. (`src/ParticleSystem.cpp:389-391`) — same scale as the TinyGL view (`src/Canvas.cpp:1344` passes `(viewX<<4)+8`) |
| `gravity` | `int` | per-emitter gravity (`-196` default, `0` for type 0) (`src/ParticleSystem.cpp:269,279,386`) |
| `gibType` | `uint8_t` | index into `imgGibs[]`; spawn code stores `int8_t -1` → 255 for "no gibs" (`src/ParticleSystem.h:21`, `src/ParticleSystem.cpp:272,387`) |
| `color` | `int` | ARGB used by `setColor` for dot particles (`src/ParticleSystem.cpp:385,103`) |
| `startTime` | `int` | `app->gameTime` at spawn (`src/ParticleSystem.cpp:388`) |

### ParticleSystem (`src/ParticleSystem.h:38-74`)

| Field | Meaning |
|---|---|
| `systemIdx` | round-robin next emitter, `(idx+1) % 4` (`src/ParticleSystem.cpp:381-382`) |
| `particleEmitter[4]` | the 4 emitters |
| `imgGibs[3]` | `[0]="woodgibs_24.bmp"`, `[1]="Stonegibs_24.bmp"`, `[2]="Skeletongibs_24.bmp"` (`src/ParticleSystem.cpp:161-163`) |
| `monsterColors` | `uint8_t*` from table 8 (see below) |
| `particleNext[64]` | singly-linked lists (index 255 = end) |
| `particleStartX[64]`, `particleStartY[64]` | `uint8_t`, read back as `(char)` = signed start offset (`src/ParticleSystem.cpp:108-109`) |
| `particleVelX[64]`, `particleVelY[64]` | `int16_t` velocities |
| `particleSize[32]` | packed nibbles, 2 particles per byte: particle `i` → `particleSize[i>>1] >> ((i&1)*4)` (`src/ParticleSystem.cpp:105,410-411`) |
| `clipRect[4]` | screen-space rect for rendering (`src/ParticleSystem.cpp:204-220`) |

**Pool/list layout** (`src/ParticleSystem.cpp:183-193`): indices 0..3 are the
list heads of emitters 0..3 (no particle data used); index 63 is the head of the
free list; indices **4..62 = 59 usable particles**. `linkParticle(head, i)`
inserts `i` right after `head` (`src/ParticleSystem.cpp:178-181`);
`unlinkParticle(head)` removes and returns the element right after `head`, or
255 if empty (`src/ParticleSystem.cpp:169-176`). Free list is LIFO.

**Size nibble** (`src/ParticleSystem.cpp:105-107`): bit 3 = gib flag, bits 0..2 =
dot radius factor (dots) or gib frame row (gibs).

### Static tables (`src/ParticleSystem.h:43-45`)

* `levelColors[7] = {0xFFFFFFFF, 0xFF3A332E, 0xFF626262, 0xFF7F7F7F, 0xFFD1CF11, 0xFFFD8918, 0xFF20B00D}` —
  used only by script opcode `EV_SPAWN_PARTICLES` (color index = low 3 bits of arg) (`src/ScriptThread.cpp:931,934`).
* `rotationSequence[4] = {0, 4, 2, 6}` — gib sprite rotate modes (`src/ParticleSystem.cpp:129`).
* `GIB_BONE_MASK = 0x619` — monster eSubType bits 0,3,4,9,10 = ZOMBIE, IMP, SAW_GOBLIN,
  REVENANT, ARCH_VILE (`src/Enums.h:61-70`) get bone gibs when gibbed (`src/ParticleSystem.cpp:244`).

### `monsterColors` (table 8, TBL_MONSTER_COLORS)

Allocated with `getNumTableBytes(8)` bytes and filled by `loadByteTable(...,8)`
(`src/App.cpp:366,381,397`). One byte per monster **`def->eSubType`**: it is a
**palette index** into the monster sprite's palette (`src/ParticleSystem.cpp:235-236`):
```
palette = render->getPalette(mapSpriteInfo[sprite] & 0xFF, 0, 0);  // tile, frame 0, palette 0
p565    = palette[monsterColors[def->eSubType]];
color   = ((p&0x7E0)<<5) | ((p&0xF800)<<8) | ((p&0x1F)<<3);   // RGB565 -> 0x00RRGGBB, alpha byte 0
```
`getPalette(n,n2,n3) = mediaPalettes[mediaPalColors[mediaMappings[n]+n2] & 0x3FFF][n3]`
(`src/Render.cpp:2039-2041`). Alpha 0 is harmless: `fillCircle` ignores alpha (`src/Graphics.cpp:45-47,80`).

## 2. Startup / free

* `startup()` (`src/ParticleSystem.cpp:150-167`): set emitter back pointers,
  `freeAllParticles()`, load the 3 gib images. Called from `src/App.cpp:117`.
* `freeAllParticles()` (`src/ParticleSystem.cpp:183-193`): `next[63]=0xFF`;
  heads 0..3 = 0xFF; links 4..62 into free list in increasing order (each inserted after 63,
  so the free list ends up 62,61,...,4). Emitter fields are NOT reset.
  Called on map load (`src/LoadingManager.cpp:665`) and on load-state finish (`src/Game.cpp:2537`).
* `freeSystem(n)` (`src/ParticleSystem.cpp:195-199`): moves all particles of emitter n to the free list.

## 3. Spawning

### Overloads
* `spawnParticles(type, color, sprite)` (`src/ParticleSystem.cpp:252-255`): uses
  `mapSprites[S_X/S_Y/S_Z + sprite]` (world units, no offset) and forwards.
* `spawnParticles(type, color, x, y, z)` (`src/ParticleSystem.cpp:257-414`): world units.
* `spawnMonsterBlood(entity, gib)` (`src/ParticleSystem.cpp:228-250`): null → return;
  color from `monsterColors` (above); `z += -26` if `def->eType == ET_CORPSE` (9);
  type **4** (bone gibs) if `gib && GIB_BONE_MASK & (1<<eSubType)`, else type **1**;
  position = sprite S_X, S_Y, S_Z+z.

`color` is an ARGB int; `-1` = `0xFFFFFFFF` white (no special meaning).

### Per-type parameters (defaults `src/ParticleSystem.cpp:259-272`)

Defaults: startX ∈ [-64,64), startY ∈ [-64,64), velX ∈ [-128,128), velY ∈ [-32,32),
dot size [4,7), gravity -196, dots=15, gibs=0, gibType=-1.

| type | startX range | startY range | velX range | velY range | dots | gibs | gibType | gravity | src |
|---|---|---|---|---|---|---|---|---|---|
| 0 | -64..64 | -64..64 | 0 | 0 | 15 | 5 | 0 wood | **0** | 274-283 |
| 1 | -16..16 | -16..16 | -128..128 | 96..110 | 15 | 0 | - | -196 | 284-292 |
| 2 | -16..16 | -16..16 | -32..32 | 64..78 | 15 | 0 | - | -196 | 329-341 |
| 3 | -16..16 | -16..16 | -32..32 | 118..128 | 15 | 5 | 1 stone | -196 | 316-328 |
| 4 | -16..16 | -16..16 | -32..32 | 64..78 | 15 | 5 | 2 bone | -196 | 342-354 |
| 5 | -8..8 | -8..8 | -128..128 | -128..128 | **8** | 0 | - | -196 | 304-315 |
| 6 | -32..32 | -64..-40 | 0 | 0..-128 | 15 | 0 | - | -196 | 355-366 |
| 7 | -8..8 | -8..8 | -128..128 | -128..128 | 15 | 0 | - | -196 | 293-303 |
| 8 | -16..16 | -15..15 | 0..-128 | 0..-128 | **8** | 0 | - | -196 | 367-379 |

Any other type id uses the defaults. All ranges are `lo + ((hi-lo)*r >> 8)`, r = 0..255,
so the upper bound is exclusive. (`nextByte()` returns `uint32_t`, so the product is
unsigned; for negative spans the logical shift + truncation to 8/16 bits yields the same
value as a signed arithmetic shift — `src/App.cpp:510-512`, `src/ParticleSystem.cpp:406-409`.)

### Spawn algorithm (`src/ParticleSystem.cpp:381-413`)
1. `idx = systemIdx; systemIdx = (systemIdx+1)%4;` `freeSystem(idx)` — the oldest emitter is recycled.
2. Store color, gravity, gibType, `startTime = app->gameTime`, `pos = (x<<4, y<<4, z<<4)` as short.
3. Loop while dots>0 or gibs>0: take a particle from the free list; **if empty → return** (remaining
   particles and RNG calls skipped). Dots are emitted first, then gibs.
4. RNG calls per particle, in this order (all `app->nextByte()`):
   * dot: `size = (4 + (3*r >> 8)) & 7` → 4..6; gib: `size = (r % 4) | 8` → row 0..3, gib flag;
   * `startX`, `startY`, `velX`, `velY`.
   → exactly 5 calls per particle.
5. Write size nibble, `linkParticle(idx, p)` (LIFO into the emitter list).

**RNG**: `nextByte() = nextInt() & 0xFF`, `nextInt() = std::rand() & INT32_MAX`
(`src/App.cpp:506-512`) — the **same global C `rand()`** used by gameplay
(e.g. `app->nextInt()` in `src/Entity.cpp:79,161,480`). Spawning particles therefore
advances the game RNG by 5 per spawned particle (up to 100 for types 0/3/4).

## 4. Aging / update

There is **no update tick**. Each emitter is evaluated analytically at render time from
`t = ((app->gameTime - startTime) << 10) / 1000` (seconds in 1/1024 units)
(`src/ParticleSystem.cpp:39-40`). `gameTime` advances by `time - lastTime` each
`Canvas::run` unless `game->pauseGameTime` or state == ST_MENU (`src/Canvas.cpp:769-771`).

Per particle (`src/ParticleSystem.cpp:108-109`):
```
t2 = t*t >> 10
dx = ((char)startX << 10) + velX * t
dy = ((char)startY << 10) - (velY * t + t2 * gravity)     // screen-y down; +velY = up, gravity -196 pulls down
```
**Death**: only when `|dx| > 65536` or `|dy| > 65536` → particle moved to free list, not drawn
(`src/ParticleSystem.cpp:110-112`). There is no lifetime: type 0 (no velocity, no gravity) stays
until its emitter is recycled by a later spawn or `freeAllParticles()`. Dead particles are removed
only while being rendered (`renderSystems` must run).

## 5. Rendering

### `renderSystems(graphics)` (`src/ParticleSystem.cpp:201-226`)
`clipRect = cinRect` in `ST_CAMERA`, else `viewRect`; then inset `x+1, y+1, w-2, h-2`.
For each emitter 0..3: `setScreenSpace(clipRect)` (sets clip rect and `transX/transY = clipRect[0..1]`,
`src/Graphics.cpp:732-739`), `emitter.render(graphics, i)`, `resetScreenSpace()` (back to `displayRect`,
`src/Graphics.cpp:750-753`).

### `ParticleEmitter::render(graphics, n)` (`src/ParticleSystem.cpp:32-137`)
Skipped if list empty. Builds a **camera-facing billboard quad** in world<<4 units:
```
rx = 64*sinTable[viewAngle & 0x3FF] >> 12      // sinTable amplitude 16384 -> ±256 = 16 world units
ry = 64*sinTable[(viewAngle+256) & 0x3FF] >> 12
v0 = (pos.x+rx, pos.y+ry, pos.z+1024)          // top,  +64 world units
v1 = (v0.x, v0.y, v0.z-2048)                    // bottom, -64
v2 = (v0.x-2rx, v0.y-2ry, v0.z)                 // other top corner
```
→ 32 world units wide × 128 tall, centered on `pos` (`src/ParticleSystem.cpp:41-54`).
`render->viewAngle` and `sinTable` from Render. Transformed with `tinyGL->transform3DVerts` (the
`mvp` = view×projection built in `TinyGL::setView`, `src/TinyGL.cpp:261-276,178-191`).

* **Near clip**: if any of the 3 verts has `w + z < 0` → whole emitter skipped (`src/ParticleSystem.cpp:56-58`).
* `projectVerts` (`src/TinyGL.cpp:516-524`): `x = XBias + x*XScale/w`, `y = YBias + y*YScale/w`,
  `z = 0x7FFFFF / w`; x/y are in **1/8 pixel**, viewport-relative (`XScale = vw<<2`,
  `XBias = ((vx + vw/2)<<3) - 4`, `src/TinyGL.cpp:141-144`; viewport = viewRect inset 1, `src/TinyGL.cpp:152-166`).
* **Occlusion — column buffer, whole emitter at once** (`src/ParticleSystem.cpp:60-68`):
  `col = ((v0.x>>3) + (v2.x>>3)) >> 1`; `depth = 0x7FFFFF / v0.z` (≈ clip w of v0);
  skip if `col < 0 || col >= tinyGL->screenWidth || depth > columnScale[col]`.
  `columnScale[]` is reset to `INT_MAX` per frame (`src/Render.cpp:2315-2317`) and lowered by
  wall lines in BSP traversal via `occludeClippedLine` (`src/TinyGL.cpp:720-745`, callers
  `src/Render.cpp:709,712,735`). No per-particle depth test, no GL depth buffer.
* Billboard screen box (px): `W = v1.x - v2.x`, `H = v1.y - v0.y` (1/8 px); `left = v2.x>>3`,
  `top = v0.y>>3`, size `W>>3 × H>>3`, clamped to `0..viewRect[2/3]` (note: viewRect even in camera
  mode); skip if negative; `graphics->clipRect(left, top, w, h)` intersects the clip
  (`src/ParticleSystem.cpp:69-97`, `src/Graphics.cpp:688-714`).
* Center: `cx = (v1.x + v2.x) >> 4`, `cy = (v0.y + v1.y) >> 4` (px) (`src/ParticleSystem.cpp:98-99`).
* Color: if `render->postProcessMode != 0` → `this->color = convertToGrayscale(color)` — **written back
  into the emitter every frame**, and gray-of-gray halves (`0xFF000000 | v>>1 ...`,
  `src/Render.cpp:2692-2696`), so dots darken to black over successive frames while the familiar view
  is active (quirk; `postProcessMode` set by familiar type, `src/Player.cpp:2500-2516`)
  (`src/ParticleSystem.cpp:100-103`).
* Per particle screen position: `px = cx + (dx * W >> 20)`, `py = cy + (dy * H >> 20)`
  (`src/ParticleSystem.cpp:73-74,115-116`; `a = (W<<7)/128 = W`). So ±65536 (the death bound)
  maps exactly to the billboard half-width / half-height: horizontally 1 world unit = 4096 dx
  units, vertically 1 world unit = 1024 dy units.
* **Dot** (bit3 = 0): `fillCircle(px, py, size * min(W,1024) >> 11)` (port; J2ME used `>> 10`)
  (`src/ParticleSystem.cpp:117-119`). `fillCircle` = 16-vertex GL triangle fan, opaque, current color
  (`src/Graphics.cpp:38-86`). **Port quirk**: `fillCircle` does not add `transX/transY` and does not
  honor `graphClipRect` (`src/Graphics.cpp:58-62`), unlike `drawRegion`.
* **Gib** (bit3 = 1) (`src/ParticleSystem.cpp:121-130`):
  `scale = min((W*3) >> 11, 3)` (J2ME `>> 10`); drawn only if `scale >= 1`;
  `rot = (((i<<6) + t) >> 8) & 3`, mirrored `rot = 3-rot` if `velX < 0`;
  `drawRegion(imgGibs[gibType], srcX=(3-scale)*48, srcY=row*48, 48, 48, px, py, anchor 3 (center), rotationSequence[rot], 0)`.
  Gib sheets: 3 size columns (col 0 = largest) × 4 rows of 48×48. `drawRegion` adds `transX/Y`
  and clips to `graphClipRect` (`src/Graphics.cpp:351-394`).

### Layering (`src/Canvas.cpp:381-424`)
Per frame the state runner calls `renderScene` → 3D world, portal, **weapon** (`combat->drawWeapon`),
post-process (`src/Canvas.cpp:1344-1352`); then `flushGraphics → backPaint` (`src/Canvas.cpp:375-379`):
1. 3D fade / SW `drawRGB` (`REPAINT_VIEW3D`) (`src/Canvas.cpp:390-402`)
2. **particles** if `REPAINT_PARTICLES` (32), flag cleared (`src/Canvas.cpp:404-407`)
3. swipe area (ST_COMBAT/ST_PLAYING) (`src/Canvas.cpp:409-411`)
4. HUD if `REPAINT_HUD` and not ST_MENU (`src/Canvas.cpp:413-416`)
5. target-practice score, intro movie (`src/Canvas.cpp:418-424`)

So particles draw **over the weapon and post-process, under the HUD**. `REPAINT_PARTICLES` is set
each frame by playing/camera/dying runners (`src/GameStateRunner.cpp:82,192,268,275,291,309,316`,
`src/MovementController.cpp:393,521`, `src/Canvas.cpp:923`, `src/Combat.cpp:1283`).

## 6. Sounds

`ParticleSystem.cpp` plays no sounds. Sounds at call sites:

| Call site | Particles | Sound |
|---|---|---|
| `Combat::explodeOnMonster`, targetType 9 gib (`src/Combat.cpp:942-944`) | `spawnMonsterBlood(target, isGibbed=true)` | 1037 after `spawnDropItem` |
| `Entity::pain` ATTACK_INTERACTIVE subtype 1 (`src/Entity.cpp:375-376`) | type 2, white | 1038 |
| Entity crate/furniture (`src/Entity.cpp:381`) | type 2, `0xFF7F7F7F` (-8421505) | none here |
| Entity pickup → water spout (`src/Entity.cpp:384`) | type 1, white | none |
| Boss VIOS (eSubType 15) teleport in `aiWeaponForTarget` (`src/Entity.cpp:735-743`) | type 7 at old and new sprite pos | 1123 between them |
| `Combat::updateProjectile` case 13 (`src/Combat.cpp:1344-1345`) | type 1, white | 1135 |

Other callers (no sound): `spawnMonsterBlood(...,false)` on reflected damage (`src/Combat.cpp:444`),
normal hit unless weapon 14 (`src/Combat.cpp:909`), `Combat::hurtEntityAt` corpse / dynamite kill with `true`
(`src/Combat.cpp:1146,1179`); thorn trail type 1 `0xFF81603D` (`src/Combat.cpp:1277`), projectile
case 9 type 1 `0xFF81603D` at z+16 (`src/Combat.cpp:1365`); resurrect type 1 `-161512`=`0xFFFD8918`
(`src/Entity.cpp:1368`); `raiseCorpses` type 1 `-3355444`=`0xFFCCCCCC` at floor+48 (`src/Game.cpp:3578`);
scripts `EV_MONSTER_PARTICLES` → `spawnMonsterBlood(ent,false)` (`src/ScriptThread.cpp:911-918`) and
`EV_SPAWN_PARTICLES` (`src/ScriptThread.cpp:921-935`): byte `b`: type = `(b>>3)&0xF`, color =
`levelColors[b&7]`; ushort `u`; byte `zo-48`. If `b&0x80`: position = tile center
`(((u>>11)&0x1F)<<6)+32, (((u>>6)&0x1F)<<6)+32`, z = floor height + zo; else `u` is a sprite index
(zo ignored).
