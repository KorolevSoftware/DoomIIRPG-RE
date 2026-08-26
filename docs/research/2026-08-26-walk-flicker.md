# 2026-08-26 — Walk-cinematic flicker: BSP leaf-straddler drop in rewrite

## Hypothesis (as given)

Rewrite attaches sprites to a BSP leaf **once at load** (`getNodeForPoint` →
`spriteLeaf[]`); a lerping sprite that leaves its stored leaf keeps drawing from
the old list (or vanishes when filtered by `spriteLeaf[i]!=leaf`) →
painter-order pops = flicker. Legacy supposedly solves straddlers via
`splitSprites` (`src/Render.cpp:894-921`).

## Verdict

**PARTIAL — root cause CONFIRMED, premise REFUTED.**

* REFUTED: binding is **not** load-time static. The rewrite re-runs
  `getNodeForPoint` for *every* sprite *every frame* from live positions
  (`new_src/render/World3D.cpp:1277-1292`), and its lerp tick writes X/Y/Z per
  game tick (`new_src/domain/game/Game.cpp:1051-1065`). Membership tracks
  movement just like legacy.
* CONFIRMED: the rewrite ports `getNodeForPoint`'s **internal-node band
  early-out** verbatim (`new_src/render/World3D.cpp:1229` =
  `src/Render.cpp:2422-2424`) but **never ports the consumer** — the
  split-sprite rescue pipeline. `walkNode` queues only leaves
  (`World3D.cpp:1250-1253`), the draw loop filters `spriteLeaf[i] != leaf`
  over leaves only (`World3D.cpp:1311-1312`), so any sprite classified onto an
  **internal node** (or −1) matches nothing and is **never emitted**. Grep for
  `splitSprite|addSplit` in `new_src/`: zero hits.
* Result: a walker whose lerped position lands within the band alternates
  between drawn (leaf state) and not-drawn (internal state) frame-by-frame →
  exactly the reported flicker. Static sprites resting on a plane are
  permanently invisible (≥6 found on map00).

## Q1 — Legacy straddler mechanism (exact)

Two cooperating layers. There is **no geometric splitting** — "split" means
*dual/multi-leaf listing*: the quad is submitted once, under one chosen visible
leaf, per frame.

### Layer A — membership rule (which node owns the sprite)

* `Render::relinkSprite(n[,x,y,z])` unlinks from the old `nodeSprites` chain
  and re-links via `getNodeForPoint(x<<4, y<<4, z<<4)`
  (`src/Render.cpp:2380-2398`).
* `getNodeForPoint` descends from the root. While descending an **internal**
  node, if the classify value satisfies `> -128 && < 128` (and the sprite is
  not water `tn==240`, and it is not an exact-on-plane decal — `c==0 &&
  info&0xF000000` picks a child by the `0x9000000` bit instead,
  `src/Render.cpp:2411-2424`), it returns the **internal node index**, not a
  leaf (`src/Render.cpp:2422-2424`). Only a full descent reaches the leaf
  bounds test (`:2434-2441`, −1 if outside).
* Band scale: relink feeds coords `<<4`, normals are 16384-fixed
  (`src/LoadingManager.cpp:467`), so ±128 classify-units = **±8 world units =
  ⅛ tile** (`MAPTILE_SIZE`=64, `src/Render.h:46`).
* When relinks run: once for every sprite at load after `postProcessSprites`
  bakes terrain height into Z (`src/Render.cpp:2459-2497`), then **every lerp
  tick** (`src/Game.cpp:2889-2896`; parabola variant with explicit coords
  `:2889-2891`) and again at completion (`src/Game.cpp:3086-3094`) unless
  `LS_FLAG_S_NORELINK`. So membership follows the walker continuously.

### Layer B — per-frame rescue (how internal-node sprites get drawn)

* `renderBSP` resets `numSplitSprites=0` each frame
  (`src/Render.cpp:1745`), then `walkNode(0)` (`:1747`).
* `walkNode`: culls bbox (`:1053`); a **leaf** is pushed to `nodeIdxs`
  (`:1057-1060`); an **internal** node snapshots `numVisibleNodes` *before*
  recursing (`:1085`), walks the view-near side then far side
  (`:1086-1093`), and *afterwards* feeds every sprite in its own
  `nodeSprites[n]` list to `addSplitSprite(snapshot, sprite)`
  (`:1094-1096`).
* `addSplitSprite` (`src/Render.cpp:896-910`): scans the visible-leaf list
  from the snapshot forward (i.e., leaves discovered inside this node's
  subtree, near-subtree order first) and picks the **first** leaf whose
  byte-bounds (`<<3`, raw map units) overlap the sprite center box
  `[x−8, x+8]×[y−8, y+8]` (test `:905`; `SPLIT_SPRITE_BOUNDS`=8
  `src/Render.h:122`); records `(leaf<<16)|sprite` into `splitSprites[]`,
  capped at `MAX_SPLIT_SPRITES`=8 per frame (`src/Render.h:121`, cap checked
  `:905`). One leaf per sprite per frame; overflow silently dropped.
* Draw pass (`src/Render.cpp:1752-1763`): iterate `nodeIdxs` **reversed**
  (far→near painter order). Per leaf: `drawNodeGeometry` (leaves only,
  `:939-941`), then `addNodeSprites(leaf)` (`:912-924`) which adds the leaf's
  own sprite chain via `addSprite` **and then** consumes split pairs whose
  high word equals this leaf (`:917-921`, entry cleared after use).
  `addSprite` computes the depth key once per sprite — mvp row dot product +
  bias chain (`:838-876`) — and inserts into the sorted `viewSprites` list
  (`:880-893`); `renderSpriteObject` then draws them head→tail (farthest
  first, `:1757-1760`).
* If the overlapping leaf is culled/invisible, no pair forms and the sprite
  simply isn't drawn that frame (correct: it was occluded/off-screen anyway).

### Billboard vs WALL/FLAT vs CHARACTER

Membership, splitting and sorting are **per map-sprite index** and identical
for all kinds; kind only selects the emitter inside `renderSpriteObject`.
Character stacks (`renderSpriteAnim`, `src/Render.cpp:3144+`) emit
shadow/legs/torso/head as several `renderSprite` quads (`:3202-3252` idle,
`:3263-3290` walk) **inside one `renderSpriteObject(n)` call** — the whole
stack shares one sort key (entity anchor x,y,z) and one leaf listing. Parts
are never split individually.

Backend note: this pipeline lives in `renderBSP`, driven by `Render::render()`
(`src/Render.cpp:2345`) for both TinyGL and GLES — it is core, not a backend
detail.

## Q2 — Rewrite gap confirmation

All in `new_src/render/World3D.cpp` unless noted:

* `drawBSP` recomputes `spriteLeaf` **per frame** from live positions
  (`:1277-1292`) — load-static premise false. (Comment `:1273-1274` claims
  legacy does it once at load — wrong about legacy, see Layer A.)
* `getNodeForPoint` keeps the band early-out returning internal nodes
  (`:1224-1233`, early-out `:1229`).
* `walkNode` pushes **only leaves** into `nodeIdxs_` (`:1250-1253`).
* Draw loop: `for i … if (spriteLeaf[i] != leaf) continue;` over `leaf ∈
  nodeIdxs_` (`:1311-1312`). Internal-node attachments (and −1) match
  nothing → **no per-frame reassignment rescue exists anywhere**
  (grep `splitSprite|addSplit|SPLIT_SPRITE` over `new_src/`: 0 hits).
* Non-BSP helper `drawSprites` (would ignore leaves, `World3D.cpp:565-609`)
  has **zero callers**; `GameContext.cpp:1023` is the only world-draw call and
  runs in every camera state including cinematics.

### Measured impact (probe: `docs/research/assets/probe_bsp_bands.py`)

Replicates `getNodeForPoint` + `postProcessSprites` Z-bake byte-exactly over
`tmp_map00.bin` (layout per `src/LoadingManager.cpp:463-541`; coord = byte·8,
`src/Resource.cpp:154-156`):

* map00 tree: 695 nodes = 348 leaves + 347 internal; 261 sprites.
* **At load**, 6 sprites attach to internal nodes → invisible in the current
  rewrite: spr 1 (tile 107 @(160,992)), **spr 53 (zombie tile 20 @(992,96))**,
  **spr 54/55 (imps tile 23 @(992,160)/(992,224))**, spr 191/192 (tile 114
  @(1344,1824)/(1344,1888)). All with classify **exactly 0**: e.g. spr53
  worldX 15872 == node 205 plane offset 15872 (`normal=(-16384,0,0)`),
  verified numerically. Map data snaps props onto tile edges that the BSP
  compiler reused as split planes, so "dead-on plane" is common, not exotic.
* ±96-unit sweep around home: **62 of 85** entity-bearing sprites
  (monsters 18–25/52, NPCs 66–77, tile 85) enter the INTERNAL state somewhere
  in their neighborhood. The dbg-audited squad walkers: spr7 14%, spr9 17%,
  spr10 12% of sampled positions INTERNAL — each such position = a frame in
  which the rewrite drops the entire character.
* Walk speed crosses the ⅛-tile band in a few ticks → blink-blink through the
  whole walk, matching "flickers while moving" with clean anim bytes.
* Sprites parked in the map void (OOB, e.g. imp trio 185-187 @(224,1696))
  classify −1 in **both** engines and stay hidden until scripted into place —
  parity, not a bug (also noted in `docs/original-code/rendering.md`).

## Q3 — Alternative candidates ranked (all fail to explain the symptom)

1. **Sort-key instability within a leaf** — REFUTED. Keys are recomputed per
   frame from the same inputs as legacy (`World3D.cpp:1327-1346` ≡
   `src/Render.cpp:839-876`); a character is ONE sort entry whose parts emit
   consecutively with no depth test (`World3D.cpp:613-615,1054-1055`), so
   ties cannot make the body vanish, only genuine occlusion crossings.
   *Proof if ever needed:* log `d` per squad sprite/frame; flicker frames
   carry no key discontinuity.
2. **Texture-bind lazy-load flush** — REFUTED. `ensureSpriteTexture` caches
   per mediaId (`World3D.cpp:735-737`, decl `World3D.h:106-110`); a lazy
   upload happens at most once per art, then never again; `flush()` submits,
   drops nothing. *Proof:* stderr counter in `ensureSpriteTexture` during a
   walk (expect 0 after warm-up).
3. **Camera pull-back shifting part offsets** — REFUTED as flicker source.
   Part offsets derive continuously from yaw via sin table
   (`World3D.cpp:987-994`); produces smooth sway, not binary pop. *Proof:*
   dump the n17 offset per frame — continuous.
4. **`applyBatchState` flush between parts** — REFUTED. Called once per
   sprite before branching (`World3D.cpp:669-670`), renderMode constant
   during walks, flush only emits pending verts. *Proof:* log renderMode
   changes for squad sprites (expect none).
5. **Winner: leaf-straddler drop** — see Q2. *One-line runtime proof:* temp
   stderr in `drawBSP` when `charClass[i]!=0 && (nodeOffsets[spriteLeaf[i]] !=
   0xFFFF || spriteLeaf[i]<0)` — bursts coincide with perceived flicker.

## Q4 — Port recipe (follow the evidence: legacy re-evaluates per tick AND
per frame; the rewrite already has the per-tick half, missing the per-frame
rescue)

Minimal faithful port of Layers B into `World3D::drawBSP`. No allocations per
frame — everything hoisted to members.

New members (`new_src/render/World3D.h`, next to `nodeIdxs_` `:146`):

```cpp
std::vector<int> spriteLeaf_;                 // was a per-frame local (:1277)
std::vector<std::vector<int>> internalSprites_; // node -> sprite ids, sized numNodes once
std::vector<int> splitPairs_;                 // flat [leaf,sprite]*8, cap 2*MAX_SPLIT_SPRITES
std::vector<int> leafSprites_, leafDepth_;    // hoist locals (:1301-1302)
```

Changes in `new_src/render/World3D.cpp`:

1. `drawBSP`: clear + refill `spriteLeaf_` (existing loop `:1278-1292`);
   clear `splitPairs_` (≈ `src/Render.cpp:1745`); rebuild
   `internalSprites_` buckets: for each non-hidden `i`, let `nd =
   spriteLeaf_[i]`; if `nd >= 0 && map.nodeOffsets[nd] != 0xFFFF`
   → `internalSprites_[nd].push_back(i)`. (Water tn 240 can never be banded —
   `getNodeForPoint` bypass, `:1222,1229` ≡ `src/Render.cpp:2410,2422`.)
2. `walkNode`: after the leaf early-out, capture `size_t start =
   nodeIdxs_.size()` before recursing (≡ snapshot `src/Render.cpp:1085`);
   at the tail, if `internalSprites_[n]` non-empty, for each sprite `s` scan
   `k ∈ [start, nodeIdxs_.size())` and take the FIRST leaf passing the
   verbatim bounds test (`src/Render.cpp:899-905`, RAW map units:
   `sx,sy = map.mapSprites[...]` vs `(bounds&0xFF)<<3`, box ±8), pushing
   `{leaf, s}` into `splitPairs_` while `splitPairs_.size() < 16`.
3. Draw loop (`:1303-1360`): per leaf `L`, after collecting its own sprites
   into `leafSprites_` (order preserved — `addNodeSprites` adds own chain
   first, splits second, `src/Render.cpp:913-922`), append split pairs with
   `pairLeaf==L`; compute `d` for them with the SAME depth/bias expression
   (factor `:1315-1346` into a helper/lambda taking sprite index); stable
   sort descending; draw (unchanged `:1359`).
4. Leave `getNodeForPoint` untouched — the early-out IS legacy behavior;
   deleting it would stray from legacy and reintroduce far-side wall pops.

Known accepted deltas: ≤8 rescued sprites/frame like legacy (`MAX_SPLIT_SPRITES`,
`src/Render.h:121`); OOB (−1) stays undrawn in both engines (parity);
tie-break order for equal depth keys differs cosmetically (legacy head-insert
puts newer-equal first, `src/Render.cpp:884-887`; rewrite stable sort keeps
collection order).

Cost: bucket build O(#sprites) + split scans bounded by subtree leaf counts;
both trivial at map00 scale (≤256 visible leaves, few banded sprites).

## Open questions

* None blocking. Optional follow-ups: exact legacy tie-order parity; whether
  any other map ships sprites resting OOB in void that scripts later move
  (handled identically by both engines today).

## CODER CHECKLIST

- [ ] `World3D.h`: add members `spriteLeaf_`, `internalSprites_`,
      `splitPairs_`, `leafSprites_`, `leafDepth_` (resize once when map
      loads / first drawBSP).
- [ ] `World3D.cpp drawBSP`: replace local `spriteLeaf` with member; clear
      `splitPairs_` each frame (≈ `src/Render.cpp:1745`).
- [ ] Bucket internal-attached sprites: `spriteLeaf_[i]>=0 &&
      nodeOffsets[spriteLeaf_[i]]!=0xFFFF` → `internalSprites_[node]`.
- [ ] `walkNode`: `start = nodeIdxs_.size()` after the leaf return, before
      recursion; tail-loop `internalSprites_[n]` with verbatim bounds scan
      (`src/Render.cpp:899-905`, RAW units, box ±8, cap 8 pairs).
- [ ] Draw loop: merge `splitPairs_` for the leaf after its own sprites;
      shared depth/bias lambda; unchanged stable sort + draw.
- [ ] Pre-fix probe (temp stderr): `charClass[i]!=0 && (spriteLeaf_[i]<0 ||
      nodeOffsets[spriteLeaf_[i]]!=0xFFFF)` → expect burst logs during walks;
      post-fix expect none.
- [ ] Visual acceptance: walking squad renders every frame through corridor
      planes; previously-invisible statics appear — map00 expectations:
      spr 1 tile 107 @(160,992); spr 53 zombie @(992,96); spr 54/55 imps
      @(992,160)/(992,224); spr 191/192 tile 114 @(1344,1824)/(1344,1888).
- [ ] Do NOT "fix" OOB-in-void sprites (185-187 etc.) — both engines hide
      them until scripted.
- [ ] Remove probe stderr after sign-off; keep
      `docs/research/assets/probe_bsp_bands.py` for regression checks.
