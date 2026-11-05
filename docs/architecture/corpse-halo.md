# Lootable-corpse pulsate halo — spec (G1)

Date: 2026-11-05. ADR: `adr/0048-corpse-pulsate-halo.md`.
Facts: `docs/original-code/corpse-outline.md` (curated), `docs/research/2026-11-05-corpse-outline.md` (raw).

## 0. Summary

The original marks a still-lootable corpse with a second copy of its sprite. The copy is 18/16
the size (17/16 for sentinels, Boss Pinky, Mastermind, Arachnotron; none for lost souls,
cacodemons, VIOS) and is drawn BEFORE the body with
`RENDER_FLAG_PULSATE` (512). **Revised 2026-11-05:** the target is the TinyGL software path, which
is what the user's legacy build runs (`isInit` comes from the config file, `src/Game.cpp:2036`;
corpse-outline.md "Which path runs (CORRECTED)"). Every non-transparent halo texel is written
**opaque** as flat grey `RGB(g,g,g)`, with no blending and no fog. `g` is computed as
`tri=(time>>2)&0x1FF; if(tri&0x100) tri=511-tri; g=(tri>>1)+127`, giving 127..254 with a
2048 ms period (`src/Render.cpp:1968-1979`, `src/Span.cpp:59-63`). The body is drawn on top, so
only a pulsing rim shows.

Rewrite design (one group, no new files):

| File | Change |
|---|---|
| `render/api/Scene3D.h` | + `virtual void setPulsate(float a) = 0;` |
| `render/sokol/SgScene3D.h/.cpp` | implement `setPulsate`: tracker `pulsateA_`; `flush` uses the AlphaBlend pipeline, colour mod 1 and fog forced to full fog colour `(g,g,g)` |
| `render/World3D.h/.cpp` | + `drawBSP` parameter `spriteHalo`, member `halo_`, `pulsateGrey()` helper, halo pre-draw in the billboard branch of `drawSprite` and in `drawCharacter`'s `kManimDead` case |
| `render/SceneRenderer.h/.cpp` | + `drawWorld` parameter `cameraState`, per-sprite `spriteHalo_` bytes computed in the classification loop |
| `core/GameContext.cpp` | pass `state_ == StateId::Camera` to `drawWorld` |

No new files, so no CMake reconfigure. Just rebuild.

## 1. The per-sprite halo byte (SceneRenderer)

`SceneRenderer` already walks every entity each frame to build `spriteCharClass_` /
`spriteHasEntity_` (`new_src/render/SceneRenderer.cpp:159-175`). Add a third parallel vector:

```cpp
std::vector<uint8_t> spriteHalo_; // 0 = no halo, else halo scale numerator (18 or 17), /16
```

Assign it next to the other two: `spriteHalo_.assign(env_.map->numSprites, 0);`. Inside the
loop, after the existing `if (!ent.def || si < 0 || si >= numSprites) continue;`, add one
block. Let `spriteInfo = env_.map->mapSpriteInfo[si]` and `tile = spriteInfo & 0xFF` (already
computed). Conditions (corpse-outline.md §1-2):

```
if (!cameraState) {
  if (ent.monster == nullptr) {
    // Placed corpse props (src/Render.cpp:1714-1717); TILE-flagged (+257) sprites excluded.
    if ((spriteInfo & 0x400000) == 0 &&
        (tile == TILENUM_OBJ_SCIENTIST_CORPSE /*137*/ || tile == TILENUM_OBJ_CORPSE /*138*/ ||
         tile == TILENUM_OBJ_OTHER_CORPSE /*139*/) &&
        ent.param == 0 && !ent.hasEmptyLootSet())
      spriteHalo_[si] = 18;
    // Dropped sentry-bot corpse (src/Render.cpp:1719-1722): no loot-set test.
    else if ((spriteInfo & 0x400000) == 0 &&   // TILE sprites take src/Render.cpp:1620 instead
             (tile == 18 /*RED_SENTRY_BOT*/ || tile == 19 /*SENTRY_BOT*/) &&
             env_.game->db.isDropped(&ent) && ent.param == 0)
      spriteHalo_[si] = 18;
  } else if ((ent.info & Entity::kInfoCorpse) != 0 &&
             ((spriteInfo >> 8) & Enums::MANIM_MASK) == Enums::MANIM_DEAD &&
             (ent.monster->flags & Enums::MFLAG_LOOTED) == 0 && ent.param == 0 &&
             !ent.hasEmptyLootSet()) {
    // Dead monster (src/Render.cpp:3470-3474 / :3528-3532 / :3600-3604 / :3753-3758).
    spriteHalo_[si] = deadHaloScale(tile);   // 0 / 17 / 18, see below
  }
}
```

`deadHaloScale(tile)` is a static helper next to `isFloaterTile` in SceneRenderer.cpp:
- 17: `TILENUM_MONSTER_SENTINEL..SENTINEL3` (renderFloaterAnim sentinel sub-switch, dead halo
  `src/Render.cpp:3528-3532`, returns at `:3537`), `TILENUM_BOSS_PINKY` (`:3600-3604`),
  `TILENUM_BOSS_MASTERMIND`, `TILENUM_MONSTER_ARACHNOTRON` (other special-boss branch,
  `:3753-3758`).
- 0: `LOST_SOUL..LOST_SOUL3`, `CACODEMON..CACODEMON3` (shared floater MANIM_DEAD draws the body
  only, `:3568-3571`), `BOSS_VIOS..VIOS5` (the VIOS branch `:3619-3659` has no MANIM_DEAD case).
- 18: every other tile (renderSpriteAnim, `:3470-3474`).

Notes for the coder:
- Tile constants: add `TILENUM_OBJ_SCIENTIST_CORPSE = 137`, `TILENUM_OBJ_CORPSE = 138`,
  `TILENUM_OBJ_OTHER_CORPSE = 139`, `TILENUM_MONSTER_RED_SENTRY_BOT = 18`,
  `TILENUM_MONSTER_SENTRY_BOT = 19` to `new_src/domain/game/Enums.h` only if they are missing
  (cite `src/Enums.h:666-667,754-756`). Grep for them first.
- `ent.param == 0` in the monster branch is a rewrite addition. `CorpseLoot::poolLootCorpse`
  marks monster corpses with `++param` instead of the legacy `monster->flags |= 0x800`
  (`new_src/domain/game/CorpseLoot.cpp:110-114`, spec deviation #1). `MFLAG_LOOTED` is still
  tested because save restore sets it (`new_src/domain/game/EntityState.cpp:289`). Monster
  `param` is 0 until it is looted. Both markers mean "looted".
- The monster branch needs the same `kInfoCorpse` + `MANIM_DEAD` test that sets
  `spriteCharClass_[si] = 1` (`SceneRenderer.cpp:162-165`). That way a monster byte is only
  ever read by `drawCharacter`'s `kManimDead` case, and the billboard hook can never pick it up
  for a living floater. Prop and sentry bytes have `monster == nullptr` and tiles outside
  65-80, so they never take the stacked path.
- Include `domain/game/EntityMonster.h` (for `flags`) if `Game.h` does not already pull it in.
- `drawWorld` gets a trailing parameter `bool cameraState` (header and .cpp). Pass
  `spriteHalo_.data()` as the new last argument of `env_.world->drawBSP(...)`.

`core/GameContext.cpp:1360`:
`scene_.drawWorld(renderer, app.window(), cinePose, underDialog, zpp, state_ == StateId::Camera);`
`ST_CAMERA = 18` is `StateId::Camera` (`new_src/core/GameStates.h:23`). This is the exact
legacy test `canvas->state != 18`. Do NOT use `cinePose != nullptr` instead: a dialog inside a
cinematic runs in `StateId::Dialog`, and the original shows the halo there.

## 2. World3D

### 2.1 Interface

```cpp
void drawBSP(const MapData& map, const MediaLoader& media, const Camera3D& camera,
    const int* spriteSortBias = nullptr, const uint8_t* charClass = nullptr,
    const uint8_t* spriteHasEntity = nullptr, bool caldexHack = false,
    const uint8_t* spriteHalo = nullptr);   // per sprite: 0 or halo scale numerator (/16)
```
Private member: `const uint8_t* halo_ = nullptr; // drawBSP's spriteHalo, valid during drawBSP`.
In `drawBSP`, set `halo_ = spriteHalo;` at entry (next to `hasEntity_ = spriteHasEntity;`,
`World3D.cpp:~1400`) and set `halo_ = nullptr;` after the final `end();` (`World3D.cpp:~1569`).
`drawSprites` (the static path, `:532`) never sets it, so it draws no halo.

### 2.2 Grey helper (file-local in World3D.cpp)

Software path `src/Render.cpp:1968-1979`, time `app->time` = `timeMs_` (`upTimeMs`,
`new_src/render/SceneRenderer.cpp:46`). This replaces `pulsateAlpha` (`World3D.cpp:18-29`):

```cpp
// RENDER_FLAG_PULSATE flat grey of the software path (src/Render.cpp:1968-1979):
// triangle wave over (time>>2)&0x1FF (period 2048 ms), g = (tri>>1)+127 in 127..254.
constexpr int pulsateGrey(int timeMs) {
    int tri = (timeMs >> 2) & 0x1FF;
    if (tri & 0x100) tri = 511 - tri;
    return (tri >> 1) + 127;
}
static_assert(pulsateGrey(0) == 127 && pulsateGrey(400) == 177 && pulsateGrey(1020) == 254 &&
              pulsateGrey(1024) == 254 && pulsateGrey(2044) == 127 && pulsateGrey(2048) == 127,
              "pulsate triangle wave");
```

### 2.3 Billboard branch (props 137-139, dropped sentry bot)

In `drawSprite`, `if (!isWall)` block (`World3D.cpp:716-743`), right before the body
`drawBillboardPart(map, media, x, y, z, tileNum, mediaId, info, scaleFactor);` add:

```cpp
// Lootable-corpse halo BEFORE the body (src/Render.cpp:1714-1722, then :1728).
if (halo_ != nullptr && halo_[i] != 0) {
    scene_->setPulsate(pulsateGrey(timeMs_) / 255.f);
    drawBillboardPart(map, media, x, y, z, tileNum, mediaId, info, (halo_[i] * scaleFactor) >> 4);
    scene_->setRenderMode(clampRenderMode(renderMode));   // back to the sprite's own mode for the body
}
```
Same position, tile, frame (`mediaId`) and flags as the body. Only the scale changes. The
billboard geometry in `drawBillboardPart` already centres horizontally and grows upward from the
base anchor (port of `src/Render.cpp:455-487`), so the 1/16 side rims and the 2/16 top rim
follow from the scale with no extra code. The legacy halo ignores the sprite's own render mode
(`renderMode = 0`) and the pulsate state replaces it completely.

### 2.4 Stacked branch (dead monsters, incl. floaters / special bosses)

`drawCharacter`, `case kManimDead` (`World3D.cpp:1187-1190`). Replace the "deferred (§8)"
comment:

```cpp
case kManimDead: {
    // Single corpse quad, NO shadow. Lootable halo first (src/Render.cpp:3470-3474).
    if (halo_ != nullptr && halo_[i] != 0) {
        scene_->setPulsate(pulsateGrey(timeMs_) / 255.f);
        emitPart(13, x, y, zR, info, (halo_[i] * scaleFactor) >> 4);
        scene_->setRenderMode(clampRenderMode(map.mapSprites[i + 3 * n]));
    }
    emitPart(13, x, y, zR, info, scaleFactor);             // :3470-3475
    break;
}
```

Floaters and special bosses: the rewrite has no `renderFloaterAnim` / `renderSpecialBossAnim`
port. Their living poses stay on the billboard path (ADR 0007). Their corpses, though, already
take this `kManimDead` case through the `kInfoCorpse` clause (`SceneRenderer.cpp:162-165`),
which comes before the floater/boss exclusion. The legacy dead branches of those renderers draw
frame 13 at the same `(x,y,z)`; the 17/16 halo exists only for sentinels (`src/Render.cpp:3528-3532`),
Boss Pinky (`:3600-3604`) and Mastermind/Arachnotron (`:3753-3758`). Lost souls and cacodemons
get none (`:3568-3571`), and VIOS has no dead case at all (`:3619-3659`). `deadHaloScale` (§1)
encodes that per tile (17 / 0), so the byte covers all four legacy call sites and no floater or
boss renderer is needed.

## 3. Scene3D / SgScene3D

### 3.1 Interface (`render/api/Scene3D.h`, next to `setRenderMode`)

Keep the signature. Replace the comment with:

```cpp
// Legacy RENDER_FLAG_PULSATE, software path (src/Render.cpp:1968-1979, src/Span.cpp:59-63):
// every non-transparent texel is written opaque as flat (grey,grey,grey); transparent texels
// are skipped; no fog. Sticky until the next setRenderMode; flushes on change. grey in [0,1].
virtual void setPulsate(float grey) = 0;
```

### 3.2 SgScene3D (exact edits to the landed code)

No shader change and no new uniform. `fs_world` already has a colour-replace path: the fog mix
`col.rgb = mix(fog_color.rgb, col.rgb, f)` (`new_src/render/sokol/shaders/world.glsl:55-59`)
returns exactly `fog_color.rgb` when `f == 0`, and it leaves alpha alone. The palette alpha is
binary (0 only for the 0xF81F key, `new_src/render/World3D.cpp:320-324`). So under the
AlphaBlend pipeline, opaque texels overwrite the destination with `(g,g,g)` and key texels stay
invisible, which is the software trans span.

- `SgScene3D.cpp` `flush` (`:118-127`). When `pulsateA_ >= 0.f`:
  - `u.colorMod[i] = 1.f` for i = 0..3. This keeps the texture alpha untouched.
  - `u.fogColor = { pulsateA_, pulsateA_, pulsateA_, 1.f }`.
  - `u.fogParams = { kPulsateFogEnd - 1.f, kPulsateFogEnd, 1.f, 0.f }`, where
    `constexpr float kPulsateFogEnd = -1e9f;` sits in the anonymous namespace. Fog is "fully
    fogged" everywhere: `(end - depth) / 1 << 0` clamps `f` to 0. Comment it in one line:
    `// fog forced to f=0 so the shader writes fog_color = the flat pulsate grey (colour replace)`.
  - `cmd.pip = pipelines_->get(SgProgram::World, SgBlend::AlphaBlend)` replaces `SgBlend::Additive`.
  - Otherwise use the existing row and fog values, unchanged.
- `setPulsate` (`:168-174`): fog no longer matters. The body becomes
  `if (a == pulsateA_) return; flush(); pulsateA_ = a;`. Drop the `fogOn` / `currentFogOn_`
  lines. `setRenderMode` is unchanged: its `pulsateA_ < 0.f` test still forces the flush on exit.
- `SgScene3D.h:80` comment: `// < 0: pulsate state inactive, else the flat grey in [0,1]`.

`SgScene3D` is the only `Scene3D` implementation.

## 4. Draw order and caching

- Per corpse: halo batch (pulsate), then the body batch in the sprite's own mode. The mode
  switch flushes the halo first. The painter's order of the BSP walk does not change.
- `drawTorchiereGlow` stays as it is. Tiles 136 and 137-139 are disjoint.
- The legacy GL state cache keyed on `(renderMode, flags)` (`src/GLES.cpp:615`) cannot make two
  halos in one frame differ, because `timeMs_` is constant within a frame. Nothing to port.

## 5. Acceptance

Build: `cmake --build build_new -j 8`, with no warnings from the new code. The `static_assert`
in §2.2 is the unit check of the formula.

User check on screen (map00):
1. Gameplay after the intro: the corpse prop next to the spawn, sprite 105, tile 138 at tile
   (5,19) (`docs/journal.md:242`), has a solid flat grey-to-white rim
   (opaque, no texture detail) that pulses between mid grey (127) and near white (254) with a
   2 s period. The rim is slightly wider on both sides and taller on top, with nothing below the base.
2. Loot that corpse (use while facing it). The halo is gone when the loot screen opens and stays
   gone afterwards.
3. During the intro cinematic (`ST_CAMERA`) no halo is visible.
4. Kill a zombie (it has a loot set). Its corpse pulses until it is looted. Imp corpses show
   NO halo: the rewrite leaves non-table monsters with an empty loot set, because the joke-item
   table is unported (`new_src/domain/game/CorpseLoot.cpp:36-39`). That is a known divergence
   with the same root as the empty loot screen.
5. The torchiere glow and other blend-mode sprites look exactly as before.

## 6. Golden frames (`tools/compare_frames.py --golden`)

- `t0200`, `t0400`, `t0500` (intro cinematic, `StateId::Camera`): expected **byte-identical**.
  They test the camera gate directly. If one differs, the cinematic was in `StateId::Dialog` at
  that tick with the corpse in view, which the original also does. Confirm the halo is the only
  difference, then update.
- `t0750` (gameplay, player at (4,19) facing the corpse prop at (5,19)) and `t0850` (in-game
  menu over that view): expected to **differ**, only in a thin rim around the tile-138 corpse.
  The grey at tick 750 is deterministic: `upTimeMs = 11250`, `t = 2812`, `tri = 252`,
  `g = 253`. Tick 850: `tri = 115`, `g = 184`.
  The user confirms by eye, then runs `tools/compare_frames.py --update` for those two frames
  only.
- If `t0750` is identical, the corpse is out of view or not lootable at that tick. Then the
  on-screen check of §5.1 is the acceptance.

**Open question (2026-11-05, after G1 landed):** the golden run came out ALL IDENTICAL, t0750
and t0850 included, against the prediction above. Possible causes: the corpse is out of view or
hidden at those ticks, its loot set is empty, or the halo does not draw. Pending the user's
on-screen check of §5.1; do not update goldens until it is answered.
