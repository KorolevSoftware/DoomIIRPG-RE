# 2026-08-31 — Is the water spout (tile 134) animated in the original?

## Hypothesis / question
User reports the converted fountain (tile 134) is NOT animated in the rewrite.
Questions: (1) the exhaustive list of load-time animation injections in the legacy map
loader; (2) whether tile 134 is in it, and if not, whether it animates by some other
mechanism; (3) how many media frames tile 134 has; (4) whether the converted spout differs
from a map-placed one; (5) if it is static, say so.

## Method
- Read `src/Game.cpp:328-500` (`Game::loadMapEntities`) in full; enumerated every
  `mapSpriteInfo` reference in `src/Game.cpp` via grep to prove the list is complete.
- Read `src/Render.cpp:1498-1729` (`renderSpriteObject`) plus `renderSprite`
  (`:426-442`) and `setupTexture` (`:2043-2047`) to see how the frame reaches the media
  index.
- Grepped every `0x80000` write/read across `src/`.
- Parsed `tmp_newMappings.bin` (little-endian `short[512]`, layout per
  `src/LoadingManager.cpp:333-352`) for frame counts.
- Cross-checked the conversion site `src/ArmorRepairSystem.cpp:56-63`.

## Verdict: REFUTED (the spout IS animated in the original) — prior claim corrected

### 1. Complete load-time injection list (`src/Game.cpp:374-397`)
Five tiles, three `if` blocks, nothing else:
- `:380-382` tile 234 → `n7 = 4` (count forced; written by the branch below)
- `:383-387` tile 156 → `n7 = 2`, `info &= 0xFFFF00FF; info |= (n7<<8 | 0x80200)`
- `:388-392` tile 236 → `n7 = 3`, `info &= 0xFFFF00FF; info |= (n7<<8 | 0x80300)`,
  `mapSprites[S_RENDERMODE + n5] = 3`
- `:394-397` tiles 136 / 234 / 130 → `info &= 0xFFFF00FF; info |= (n7<<8 | 0x80000)`
  with `n7 = mediaMappings[n6+1] - mediaMappings[n6]` (`:379`) unless forced above.

The only other frame-field write in the loop is `info |= 0x200` for tiles 1..12
(`src/Game.cpp:405-407`) — a static frame index for doors, not an animation.

### 2. Tile 134 is NOT in that list, but has its own animator
`src/Render.cpp:1648-1653`:
```cpp
if (n3 == Enums::TILENUM_WATER_SPOUT) {
    int n15 = app->time / 128;
    this->renderSprite(x, y, z, n3, (n15 & 0x1), n2, renderMode, scaleFactor, n10);
    return;
}
```
Frame = `(time / 128) & 1` → 2 frames, 128 ms each (~7.8 fps), global phase, and it
`return`s so the generic `renderSprite` at `:1727` never runs. `renderSprite` passes the
frame straight through (`src/Render.cpp:442`) to `setupTexture`, where
`mediaIdx = mediaMappings[tileNum] + frame` (`src/Render.cpp:2047`).
Reachability: the branch sits in the `else` of `if ((n2 & 0x400000) != 0x0)`
(`:1618`); `0x400000` in mapSpriteInfo means "+257 tile" (`:1513-1515`), so it is
necessarily clear whenever `n3 == 134`. No `EV_ENTITY_FRAME` script, no `param`
timer and no mediaMappings-range animator is involved.

### 3. Media frame count
`tmp_newMappings.bin`: `mediaMappings[134] = 733`, `[135] = 735` → **2 images** for tile
134, exactly matching the `& 1`. (For reference: 123 toilet = 2, 127 sink = 2, 130 = 4,
136 = 1, 156 = 3 in data but forced to 2, 234 = 4, 236 = 3, 240 = 1.)

### 4. Converted vs. pre-placed spout — identical
Conversion (`src/ArmorRepairSystem.cpp:61`) keeps the upper bits of the fixture's
mapSpriteInfo, but tiles 123/127 receive no load-time injection (§1), so no `0x80000` and
no frame count are inherited; and even if they were, the tile-134 branch overrides the
frame. The pre-placed map00 spout (sprite 44 at (14,10), `info 0x00000086`) has no upper
bits either. **Both animate the same way.** The entity-side `info |= 0x400000` set by the
conversion is a different field (persisted "spout used/on" flag,
`src/Entity.cpp:1466-1471,1816`) and does not affect rendering.

### 5. Direct answer
The user's expectation is correct: the original spout animates. The earlier statement in
`docs/research/2026-08-31-water-spout.md` §7 ("static tile-134 sprite … never
auto-animated") is wrong — it only checked the generic `0x80000` path
(`src/Render.cpp:1544-1546`) and missed the dedicated branch at `:1648-1653`.

## Open questions
- Rewrite side (out of scope here): tiles 156 (`0x80200`) and 236 (`0x80300` +
  `S_RENDERMODE = 3`) load-time injections are still missing per `docs/status.md`, and the
  tile-134 / 240 / 156 / 136 hard-coded render branches need porting.
- Implicit invariant worth guarding in the rewrite: `0x80000` with a zero frame-count field
  divides by zero at `src/Render.cpp:1545`.
