# 2026-08-26 — Combat Stage 1 fixes: facing probe, fire-target election, world viewport + view weapon

Delta spec on `specs/2026-08-26-combat-stage1.md`. Fixes three playtest defects
reported after combat stage 1 landed. No new research is needed: every number
below is already verified.

**Supersedes:**

* `specs/2026-08-26-combat-stage1.md` **§0.F** and its "one `traceMove` from
  `(destX,destY)` toward `viewStep` (one tile …)" rule at `:157-160` — the
  facing probe is a 6-tile ray (§1 here).
* `specs/2026-08-26-combat-stage1.md` **§9** deviation 1 ("single-hit ray"
  election) and deviation "§4 election of the closest hit" — the fire scan is an
  ordered walk over the sorted hit list (§2 here).
* `specs/2026-08-26-combat-stage1.md` **§6.2** anchors/source-rect numbers —
  replaced by §3 here (viewport-relative anchors, fixed 176-texel window).

**Sources (do not re-derive):**
`docs/original-code/combat.md` §1, §4.1, §7, §8;
`docs/original-code/rendering.md` §6;
`docs/research/2026-08-26-facing-entity-health-bar.md`;
`docs/research/2026-08-26-fire-target-acquisition-corpse-tile.md`;
`docs/research/2026-08-26-view-weapon-placement-audit.md`;
`docs/architecture/adr/0008-combat-module-and-monster-payload.md`;
new: `docs/architecture/adr/0009-legacy-world-viewport.md`
(read its **Amendment 2026-08-26**: deviation D1 "cinematic path unchanged" is
refuted — §5.2/§5.3 below now apply one viewport to both paths).

Threading: unchanged — single-threaded GL loop, everything below runs on the
game thread inside `GameContext::tick*`/`GameContext::render`.

---

## 0. Shared helper: the view forward vector

Both defect-1 and defect-2 rays need the legacy forward vector
`(-view[2], -view[6])` (14.14). The rewrite has no TinyGL view rows at tick
time, but `Camera3D`/`GameContext::render` already derive the same vector from
the sin table (`new_src/core/GameContext.cpp:1265-1268`: the 160-unit pull-back
subtracts `160*cos` from x and adds `160*sin` to y), i.e.

```
fwdX =  sinTable[(a + 256) & 0x3FF]      // cos, 16.16 (65536 = 1.0)
fwdY = -sinTable[a & 0x3FF]              // -sin
a    = player.viewAngle & 0x3FF
```

Add ONE private helper to `GameContext` (used by groups 1 and 3):

```cpp
// Forward vector of the current player view in 16.16 (legacy
// -view[2]/-view[6], src/MovementController.cpp:38, src/PlayingInputHandler.cpp:218).
void viewForward(int& fwdX, int& fwdY) const;   // reads sys_.tables->sinTable
```

Scaling a distance `d` in world units: `dx = (d * fwdX) >> 16`. This is exactly
the legacy `d * (-view[2]) >> 14` with a 16.16 table instead of 14.14.

---

## 1. Defect 1 — monster health bar only shows for an adjacent monster

Root cause: `GameContext::updateFacingProbe` sweeps ONE tile along the discrete
`viewStep` (`new_src/core/GameContext.cpp:1120-1164`), while the original casts a
6-tile ray (`src/MovementController.cpp:38`, combat.md §7.1). Second cause: our
probe only runs when the `facingDirty` latch is set
(`new_src/core/GameContext.cpp:337-340`), the original recomputes it on every
rendered frame while `ST_PLAYING` (`src/Hud.cpp:735-742`, combat.md §7.1).

### GROUP 1 — the ported probe (`new_src/core/GameContext.{h,cpp}`)

Rewrite `updateFacingProbe()`. **Reuse `Game::traceMove` + `Game::lastTraceHits()`
and `Game::entityDistFrom` — do NOT add a second trace mechanism.**

Geometry (combat.md §7.1, `src/MovementController.cpp:38-40`):

| quantity | current | target |
|---|---|---|
| origin | `(destX, destY)` | `destX + (28*fwdX>>16)`, `destY + (28*fwdY>>16)` — tile centre pushed 28 units forward |
| end | `dest + viewStep` (64 u) | `destX + (384*fwdX>>16)`, `destY + (384*fwdY>>16)` — 6 tiles |
| direction | discrete `viewStep` (8-way) | `viewForward()` (§0) |
| mask | 21741 | 21741 (unchanged) |
| radius | 2 | 2 (unchanged) |
| skipEnt | `playerEntity()` | `playerEntity()` (unchanged; legacy passes `nullptr` but PLAYER bit 1 ∉ 21741) |
| z check | none | none (legacy tests Z only when zoomed in — no zoom system) |

Promotion re-scan — replace the current 2-rule subset with the **full table** of
combat.md §7.1 (`src/MovementController.cpp:41-86`). Entered only when the
nearest hit `t0` ∈ {6 ITEM, 11 MONSTERBLOCK_ITEM, 12 SPRITEWALL,
10 ATTACK_INTERACTIVE, 14 DECOR_NOCLIP}; then walk `lastTraceHits()` from index 0
(sorted by frac asc, the world slot included) and for each entity type:

| encountered eType | action |
|---|---|
| 2 MONSTER | `if (t0 != ET_SPRITEWALL) hit = ent;` then `break` |
| 5 DOOR / 4 PLAYERCLIP / 0 WORLD | `break` |
| 12 SPRITEWALL with `map.mapFlags[ent->linkIndex] & 0x2` | `break` |
| 7 DECOR | `if (t0 == ET_SPRITEWALL) hit = ent;` then `break` |
| 14 DECOR_NOCLIP | `if (hit->def->eSubType != 6) hit = ent;` (no break) |
| 10 ATTACK_INTERACTIVE with eSubType ∈ {1,2,3} | `if (t0 != ET_ITEM) hit = ent;` (no break) |
| anything else | continue |

`mapFlags` is the unpacked 1024-entry tile table
(`new_src/domain/world/MapData.h:76`, filled at
`new_src/domain/world/MapParser.cpp:187-188`); `Entity::linkIndex`
(`new_src/domain/game/Entity.h:37`) is `tx + 32*ty`, the same index space
(cf. `new_src/domain/game/ScriptVM.cpp:146`). Guard `linkIndex >= 0 && < 1024`.

Distance gate (unchanged from the current code, keep it, cite `:88-93`):
non-monsters with `entityDistFrom(hit, viewX, viewY) > combat.tileDistances[2]`
(36864) become `nullptr`; **monsters are never distance-gated**. Note: legacy
measures from `destX/destY`; keep our `viewX/viewY` (identical while idle,
≤1 tile apart mid-lerp) and leave the existing comment.

Refresh / invalidation:

* Remove the latch-gated call at `new_src/core/GameContext.cpp:337-340`
  (block "4.5") from `tickPlaying`.
* Call `updateFacingProbe()` from `GameContext::render`, immediately **before**
  the health-bar feed block (`new_src/core/GameContext.cpp:1346-1361`), gated on
  `state == StateId::Playing` — this is literally the legacy call site
  (`src/Hud.cpp:737-739` runs it before `drawTopBar` while `ST_PLAYING`). During
  Dialog/Looting the last probe result persists, like legacy.
* Clear `sys_.game->facingDirty` there too; the field stays (many sites set it)
  but becomes advisory. Add a one-line comment saying so.
* Hard clears already ported: `Game::removeEntity`
  (`new_src/domain/game/Game.cpp:751`, legacy `src/Game.cpp:192`) and
  `Player::reset` (`new_src/domain/game/Player.cpp:104`, legacy
  `src/Player.cpp:497`). Nothing to add.
* Delete the `[face] probe …` per-frame `fprintf`
  (`new_src/core/GameContext.cpp:1161-1164`) — it would now spam every frame.

### GROUP 2 — bar geometry + feed (`new_src/ui/Hud.{h,cpp}`, feed call in `core/GameContext.cpp`)

Bar constants for the 480x320 canvas (combat.md §7.3, `src/Hud.cpp:861-899`).
Current code (`new_src/ui/Hud.cpp:220-259`) is already correct on segments/frame;
only the three missing branches and the target metadata are added.

| quantity | value | status |
|---|---|---|
| segments `n` | 25 | ok |
| segment pitch `n4` | `2*(480<<8)/128>>8` = 7, then `isBoss ? ++n4 : (n4&1 ? ++n4 : n4)` = **8** | add the boss branch (same result at 480 — fidelity + comment) |
| frame | `w = 2 + 8*25 = 202`, `h = n4*2+1 = 17`, `x = 240 - 101 = 139` | ok |
| frame `y` | `viewRect[1] (20) + n3` | ok |
| `n3` | 6 default; **50** when `eSubType == 5 (PINKY) && parm == 0`; `+20` when zoomed (no zoom system → skip, comment) | add the pinky branch |
| segments | `w = n4-1 = 7`, `h = n4*2-2 = 14`, first at `x+2 = 141`, `y+2`, pitch 8 | ok |
| fill / frame colours | `0xFF000000` / `0xFFAAAAAA` | ok |
| segment colour | `n2 <= n/4 (≤6)` red `0xFFFF0000`; `n - n2 <= n/4 (n2 ≥ 19)` green `0xFF00FF00`; else orange `0xFFFF8800` | ok |
| count | `n2 = ceil(25*shown/max)`, forced to ≥1 when `shown > 0` | ok |
| drain | 250 ms linear from the previous displayed value; `> 250 ms` snaps | ok |

Signature change:

```cpp
// id = faced sprite index (-1 clears). lowBar = PINKY eSubType 5 with parm 0
// (n3 = 50, src/Hud.cpp:866-868); boss = Entity::isBoss() (n4 += 1, :874).
void feedMonsterHealth(int id, int hp, int maxHp, bool lowBar = false, bool boss = false);
```

Store `lowBar_`/`boss_` next to `monsterId_` and consume them in
`drawMonsterHealth`. Caller
(`new_src/core/GameContext.cpp:1346-1361`) fills them from the faced entity:
`lowBar = (fe->def->eSubType == 5 && fe->def->parm == 0)`,
`boss = Game::isBossDef(fe->def)` (`new_src/domain/game/Game.h:311`).
Keep the existing live-monster gate (`monster != nullptr`, `isMonster()`,
`info & kInfoActive`, `hp > 0`) — it is the port of `src/Hud.cpp:826-835`.

No change to `drawTopBar`'s `drawMonsterHealth(g, 240, 20)` call: `viewRect[1]`
stays 20 in HUD math even after the world viewport change (§3).

---

## 2. Defect 2 — standing on a corpse blocks the shot

Root cause: our fire path takes the single closest hit of a **one-tile** ray
(`new_src/core/GameContext.cpp:1030-1060`); a corpse on the player's own tile
returns frac −1 and sorts first, is classified as "no outcome" and the shot
becomes an air shot. The original walks the whole sorted list with per-type
accept/skip/break rules, and its corpse branch neither matches at distance 0 nor
breaks (combat.md §8, `src/PlayingInputHandler.cpp:279`,
`docs/research/2026-08-26-fire-target-acquisition-corpse-tile.md` §7).

### GROUP 3 — election walk (`new_src/core/GameContext.{h,cpp}`)

Extract the election into a private method so the `Action::Use` body stays
readable:

```cpp
// Ordered target election over the sorted fire-trace hit list — port of
// src/PlayingInputHandler.cpp:218-368 (docs/original-code/combat.md §8).
// Returns the elected entity (nullptr = nothing elected -> air shot) and
// writes the hit fraction of that entity to outFrac.
Entity* electFireTarget(int weapon, int* outFrac);
```

Probe (replaces the one-tile ray; keep the `!combat.active` and `weapon >= 0`
guards where they are):

| quantity | current | target |
|---|---|---|
| origin | `(viewX, viewY)` | unchanged (`viewX/viewY`, `src/PlayingInputHandler.cpp:221`) |
| length | 1 tile via `viewStep` | `n7 = 6` tiles = 384 units along `viewForward()`; `n7 = 1` (64 u) when melee |
| melee test | absent | `weapon == 1` (WP_CHAINSAW is the only member of WP_MELEEMASK 2, `src/Enums.h:137,155`) |
| mask | `CONTENTS_WEAPONSOLID` (13997) | `13997 \| (weapon == 2 ? 0x4100 : 0) \| (melee ? 0x10 : 0)` (`:203-215`) |
| radius | 2 | 2 |
| skipEnt | `playerEntity()` | unchanged (legacy passes `nullptr`; PLAYER bit ∉ 13997) |

Election: iterate `sys_.game->lastTraceHits()` in order (index 0 = lowest frac,
own-tile entities have frac −1 and come first). State: `Entity* entity = nullptr;`
`int frac = 16384;` (`n4`), plus `Entity* melee13 = nullptr;` (`entity2`).
For every hit compute `dist = entityDistFrom(ent, viewX, viewY)` (Chebyshev², the
existing helper) and apply, **in this exact order**
(`src/PlayingInputHandler.cpp:229-354`):

| eType | rule | loop |
|---|---|---|
| 0 WORLD / 12 SPRITEWALL / 4 PLAYERCLIP | `if (entity == nullptr) { entity = ent; frac = f; }` | **break always** |
| 10 ATTACK_INTERACTIVE | if `((1 << eSubType) & 1) == 0` (eSubType ≠ 0) **or** `weapon == 1`: `if (!lootElected) { entity = ent; frac = f; }` and **break**. Otherwise fall through to `continue` | break only when the condition holds |
| 13 NONOBSTRUCTING_SPRITEWALL | `if (melee) melee13 = ent;` | continue |
| 3 NPC | `if (dist >= 8192) { if (!lootElected) { entity = ent; frac = f; } break; }` | break only then |
| 2 MONSTER | `entity = ent; frac = f;` (clears any loot election) | **break** |
| 5 DOOR | `if (entity == nullptr) { entity = ent; frac = f; }` | **break** |
| 9 CORPSE | only when `dist == combat.tileDistances[0]` (**exact equality**, 4096): chainsaw (`weapon == 1`) takes the highest `linkIndex` of the pile (`if (entity == nullptr \|\| entity->def->eType != 9 \|\| entity->linkIndex < ent->linkIndex) { entity = ent; frac = f; }`); other weapons elect a LOOT target — already handled by `findLootableCorpseFacing` before the fire branch, so here just **skip** and add a comment citing `:318-334` | **never breaks** — this is the whole point of the defect |
| 8 ENV_DAMAGE | `if (eSubType == 1 && weapon == 2 && player.ammo[3] >= 2) { entity = ent; frac = f; break; }` | break only then |
| 7 DECOR | `if ((map.mapSpriteInfo[ent->getSprite()] & 0xFF) == 0x95 /* 149 TILENUM_PRACTICE_TARGET */) { entity = ent; frac = f; break; }` | break only then |
| 14 DECOR_NOCLIP | `if (eSubType == 7 && dist == tileDistances[0]) { entity = ent; frac = f; break; }` | break only then |
| anything else (not 7, not 6) | `if (entity == nullptr) { entity = ent; frac = f; }` | continue |

Post-walk pruning to port (all cited, all cheap):

1. eType 10 out of weapon range: `if (entity && eType == 10 &&
   ((1 << eSubType) & 1) == 0 && combat.worldDistToTileDist(dist2) >
   weaponData[weapon*9 + kFieldRangeMax]) entity = nullptr;` (`:379-381`;
   `dist2 = entityDistFrom(entity, viewX, viewY)`, `tileDistances[9]` when
   `entity == nullptr`).
2. melee promotion: `if (melee13 && (entity == nullptr ||
   (entity->def->eType != 2 && entity->def->eType != 9))) entity = melee13;`
   (`:383-385`).
3. Do NOT port `:387-393` (barricade unlink), `:405-431` (sentry-bot pickup),
   `:433-447` (water spout) — log-free skips, mention them in a comment.

Outcome mapping in `Action::Use` (keep the existing three-outcome shape and the
behaviours that already work):

* elected `eType == 2 / 3 / 9(chainsaw) / 7 / 10 / 8 / 13` → `kElected`,
  `p.fireWeapon(combat, entity, spriteX, spriteY)` — unchanged code.
* elected `eType == 0 || 12` with `dist2 <= tileDistances[0]` → `kWallPush`
  (existing push-log, no turn consumed, `:467-487`).
* everything else (including `entity == nullptr` and far walls) → `kAirShot`
  into `worldEntity()` at the player position — unchanged.
* Keep the zoom-entry early-out and the chainsaw gib / corpse-loot paths as
  they are.
* Downgrade the `[fire] election …` debug line to a single summary print
  (elected type + frac + dist2); keep it for this round of playtesting.

Explicitly unchanged: loot preemption via `findLootableCorpseFacing`, the
tile-event → door → fire ordering in `Action::Use`, `Player::fireWeapon`,
`Combat::performAttack` and everything downstream.

---

## 3. Defect 3 — view weapon too high and too small

Two confirmed causes (view-weapon audit, combat.md §4.1):
(a) we blit the whole 256x256 media into the 176x176 quad instead of the fixed
top-left 176x176 texel window (`src/GLES.cpp:539-542`, rendering.md §6.2);
(b) the legacy weapon anchors are relative to the 3D GL viewport
`(1, 7, 478, 248)` in canvas space, ours are absolute canvas with a full-canvas
world render (rendering.md §6.1).

**Decision (user, final — do not re-open):** restore the legacy world viewport.
See `adr/0009-legacy-world-viewport.md`.

### GROUP 4 — bottom HUD panel (`new_src/ui/Hud.{h,cpp}`) — land BEFORE group 5

After the viewport change the world band ends at canvas y = 254; the original
covers y 256…319 with `gameMenu_Panel_bottom.bmp` (480x64 at y = 256,
`src/TouchController.cpp:544-545`, rendering.md §6.1). Without it group 5 leaves
a 65 px black strip. Landing it first is harmless (it paints over the world
exactly like legacy).

* Load `gameMenu_Panel_bottom.bmp` (verified 480x64 in the ipa) into a new
  `Texture imgPanelBottom_` in `Hud::startup` next to
  `imgPanelTop_` (`new_src/ui/Hud.cpp:75`).
* New `void Hud::drawBottomPanel(Graphics2D& g)`: `g.drawImage(imgPanelBottom_,
  0, 256, 0)` — background only, **no widgets** (`drawBottomBar`'s
  shield/health/weapon readouts still run on demo fields `Hud.h:159` and are out
  of scope).
* Call it from `GameContext::render` for `state ∈ {Playing, Looting, Dialog}`,
  right after `drawTopBar` (`new_src/core/GameContext.cpp:1362-1366`).

### GROUP 5 — world viewport, aspect, weapon placement

Files: `new_src/render/gl/SpriteBatch.h`, `new_src/render/RenderBackend.cpp`,
`new_src/core/GameContext.cpp`.

**5.1 Batch safety.** Move `void flush();` from the private to the public
section of `SpriteBatch` and call `batch_.flush()` as the first statement of
both `RenderBackend::setCanvasViewport` and `RenderBackend::restoreCanvasViewport`
(`new_src/render/RenderBackend.cpp:40,55`). Rationale: batched 2D quads
rasterize at flush time, so a viewport change must not retro-scale quads
recorded earlier.

**5.2 World viewport.** In `GameContext::render`, replace the current
"set cinematic viewport for the whole block, restore late" shape
(`new_src/core/GameContext.cpp:1206-1213` and `:1332-1336`) with a viewport that
wraps ONLY the world draw:

```cpp
// Overlay anchor only (cockpit letterbox art) — NOT a viewport. src/Canvas.cpp:151-154,
// src/Hud.cpp:623-624. See ADR 0009 amendment (D1 refuted).
static constexpr int kCinRect[4]   = { 0, 42, 480, 250 };
// The single world GL viewport, gameplay AND cinematics: src/GLES.cpp:119-127
// hardcodes posY = 65 for both rects. rendering.md §6.1.
static constexpr int kWorldRect[4] = { 1,  7, 478, 248 };
...
if (sys_.world->initialized() && sys_.map->numNodes > 0) {
    renderer.setCanvasViewport(kWorldRect[0], kWorldRect[1], kWorldRect[2], kWorldRect[3]);
    sys_.world->drawSky(camera_);
    ... drawBSP ...
    renderer.restoreCanvasViewport(app.window());
} else {
    g.fillRect(0, 0, 480, 320, 32, 32, 64);   // stays in canvas space
}
```

**No `cinematicView` branch on the viewport at all.** The cinematic band that
the player sees is produced by the cockpit overlay drawn afterwards in full
canvas space (`drawOverlay(g, 0, 42, 480)` — unchanged), exactly like the
original. `kCinRect` survives only as the source of that `42`.

This also removes an existing inconsistency: a maya camera can be active while
`state != StateId::Camera`, and that path already used the gameplay rect with
the cinematic fov, so the two cinematic entries disagreed. One rect makes them
agree by construction.

Then delete the old late `if (cinematicView) renderer.restoreCanvasViewport(...)`
(`:1332-1336`) — everything after the world block (weapon, cockpit overlay, HUD,
dialogs, loot menu) now draws in full canvas space. `drawSky` is an NDC quad
(`new_src/render/World3D.cpp:551-560`) so it fills exactly the world rect;
`drawBSP` is MVP-driven and viewport-agnostic.

**5.3 Projection aspect** (`new_src/core/GameContext.cpp:1276-1277`):

| path | current | target |
|---|---|---|
| gameplay | `camera_.setView(..., 290, (290<<14)/((480<<14)/320))` = aspect **193** | `(290<<14)/((478<<14)/248)` = aspect **150** (hfov 87.4°, horizon back at canvas y = 131) — `src/Render.cpp:2223`, rendering.md §6.1 |
| cinematic (`:1258-1259`) | `fov 315`, `(315<<14)/((480<<14)/320)` = 210 | `fov 315` unchanged, aspect `(315<<14)/((478<<14)/248)` = **163** — same viewport as gameplay (ADR 0009 amendment; `src/Render.cpp:2223`, rendering.md §6.1) |

Write both expressions literally (`(290 << 14) / ((478 << 14) / 248)` and
`(315 << 14) / ((478 << 14) / 248)`) with the citation, not the constants
150 / 163.

**5.4 Weapon anchors and source rects** (`GameContext::drawViewWeapon`,
`new_src/core/GameContext.cpp:611-695`). Keep every existing formula
(`wpinfo` indices, per-weapon `scrY` bias 3/10/12, `sy = -|sy|`,
`x = scrX + wpX + sx`, `y = scrY - (wpY + sy)`, flash gate masks 0x200/0x181,
flash offset +40/+40, `getWeaponTileNum`, animTime = SHOTHOLD×10). Change only:

| item | current | target | citation |
|---|---|---|---|
| `scrX` (`:620`) | 196 | **197** (= 196 + world viewport x 1) | rendering.md §6.2, combat.md §1 |
| `scrY` (`:621`) | 131 | **138** (= 131 + world viewport y 7) | same |
| gun src rect (`:693`) | `0,0,tex->width(),tex->height()` (256x256) | `0, 0, 176, 176` | `src/GLES.cpp:539-542` |
| gun dst size (`:693`) | `176,176` | unchanged | `src/Render.cpp:358-373` |
| flash src rect (`:674`) | `0,0,tex->width(),tex->height()` | `0, 0, 176, 176` | same UV window, scale 0x8000 |
| flash dst size (`:675`) | `88,88` | unchanged | `src/Combat.cpp:834` |

Expected result for the rifle idle, no shake: quad top-left canvas
`(197-13, 138-88) = (184, 50)`, 176x176, visible gun pixels canvas
(265,154)-(358,226) — the original numbers of the audit table.

Also delete the one-shot `[weapon] …` debug print (`:686-692`) — the placement is
the thing being accepted by eye, the log adds nothing.

**5.5 HUD geometry: no changes needed.** `viewRect = {0,20,480,250}` stays the
HUD's coordinate frame (health bar at y = 26, top panel 480x20 at y = 0, bottom
panel 480x64 at y = 256). Coverage after the change: world y 7…254; top panel
paints over 0…19; bottom panel over 256…319. **Expected and faithful:** 1 px
black seams at canvas `x = 0`, `x = 479` (world rows 20…254) and at row
`y = 255` — the original clears the same pixels to black
(`glViewport(1,65,478,248)` inside a cleared 480x320 framebuffer). Do not "fix"
them.

### GROUP 6 — cosmetic deviations (OPTIONAL, non-blocking)

Land only if groups 1-5 are green and the user still wants them.

1. **Recoil lerp clock** (`new_src/core/GameContext.cpp:653-656`): drop the
   `- c.flashTime` term — the original lerps from `animStartTime` and never
   subtracts `flashTime` (`src/Combat.cpp:747`). Visible error today: 1 ms of a
   500 ms recoil (0.2%). One-token change.
2. **Flash additive blend** (`:670-676`): the original draws the muzzle flash
   with `RENDER_ADD50` = `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` and
   `glColor4f(.5,.5,.5,1)` (`src/GLES.cpp:660-664`, `src/Combat.cpp:833`);
   we use a plain alpha blit at full tint. Implementation: add
   `void SpriteBatch::setBlendMode(int mode)` (0 = `SRC_ALPHA,
   ONE_MINUS_SRC_ALPHA`, 1 = `SRC_ALPHA, ONE`) that flushes on change and is
   honoured by `flush()`'s blend re-assert (`SpriteBatch.cpp:182-186`); expose
   it through `Graphics2D::setBlendMode`. Draw the flash with mode 1 and tint
   `(128,128,128,255)`, then restore mode 0. Nothing else in the frame may stay
   in mode 1.

---

## 4. Acceptance criteria (USER EYES)

* **Defect 1** — standing in a corridor with an imp 3-5 tiles ahead in plain
  line of sight: the segmented health bar appears under the top panel
  (frame 202x17 at x 139, y 26) and tracks that imp; it disappears when a wall,
  a closed door or a corner breaks the line, and reappears when the player turns
  back toward the monster. Damaging the imp drains segments smoothly over
  ~0.25 s and the colour walks orange → red as it drops below ~1/4.
* **Defect 2** — standing ON a corpse tile (e.g. after killing an imp and
  stepping onto its body) and shooting a monster ahead: the monster takes damage
  and its health bar drops, exactly as when standing on clean floor. Shooting
  along a 2-5 tile corridor now hits a monster that far away instead of firing
  into the air; the corpse one tile ahead still opens the loot list instead of
  being shot.
* **Defect 3** — the rifle sits low-right in the frame at its original size:
  gun pixels roughly from canvas x 265 to 358, top edge around y 154, bottom
  edge touching y 226 (i.e. it visually rests just above the bottom panel, not
  floating mid-screen), and the art is noticeably larger than before. The world
  now occupies the band between the top bar and the bottom panel; the bottom of
  the screen shows the HUD bottom panel image, not a black strip. The horizon /
  eye level is at canvas y ≈ 131 and the view feels slightly narrower
  vertically than before.

* **Cutscene framing (NEW eye-check required)** — the cinematic viewport is no
  longer separate: entering a cutscene (boot intro, in-map cutscenes) must NOT
  shift the world image vertically any more. Before this change the picture
  jumped ~36 px down at cutscene entry ("the viewport slides down"); after it the
  world band is the same canvas y 7…254 as gameplay, with a wider vertical FOV
  (aspect 163 instead of 210), so the framing of every cutscene shot looks
  different from the previously accepted key-0 screenshots — more of the scene
  visible vertically, horizon at canvas y ≈ 131. The cockpit letterbox overlay
  still starts at canvas y = 42, unchanged. **The old key-0 eye-validation does
  not carry over** (it was taken against a full-canvas gameplay render, ADR 0009
  amendment): the user must re-check the intro cutscene and confirm (a) no
  vertical step when the cutscene starts or ends, (b) the cockpit frame sits
  where it always did, (c) the shot compositions are acceptable. If a shot now
  looks badly framed, that is a *content* observation to report, not a reason to
  restore the two-viewport model.

## 5. Regression list (must all still behave)

1. Doors: use opens/closes, locked doors refuse with the message, auto-close on
   turn advance, slip-door sprite motion.
2. Corpse loot: dwell crouch, loot list text/scroll, grant on close, turn spent.
3. Dialogs: intro/help dialogs, styled boxes, typewriter, paging, choice
   branches.
4. TAB: "Turn Passed" centre message + turn advance.
5. Monsters: wake on sight, pain reaction, death animation → corpse, XP message,
   corpse looting afterwards; health bar clears when the target dies.
6. Cinematic camera: the boot intro and in-map cutscenes still start on key 0
   (the two-field active model of `specs/2026-08-26-camera-key0.md` must not
   regress). The cinematic **framing intentionally changes** this round (one
   shared viewport, aspect 163) — see acceptance criterion "Cutscene framing"
   in §4; only the key-0 trigger logic, the shot/camera timing and the cockpit
   overlay position must be unchanged.
7. Cockpit overlay during cinematics still lands at canvas (0,42) and the loot
   menu / message overlays still draw in full canvas space.

## 6. Build notes

New files: none (only the new ADR doc). No CMake reconfigure needed unless a
coder splits something into a new translation unit — the project GLOBs, so if a
file is added run
`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` before
`cmake --build build_new -j 8`.

## 7. Group order for delegation

1. GROUP 1 — facing probe (`core/GameContext.{h,cpp}`).
2. GROUP 2 — bar geometry + feed (`ui/Hud.{h,cpp}` + feed call).
3. GROUP 3 — fire election walk (`core/GameContext.{h,cpp}`).
4. GROUP 4 — bottom HUD panel (`ui/Hud.{h,cpp}` + call site).
5. GROUP 5 — world viewport + aspect + weapon anchors/src rects
   (`render/gl/SpriteBatch.h`, `render/RenderBackend.cpp`,
   `core/GameContext.cpp`).
6. GROUP 6 — optional cosmetics (recoil clock, additive flash).

Groups 1-3 and 4-5 are independent of each other; 4 must precede 5.
