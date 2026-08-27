# 2026-08-27 — `Entity::info` bit meanings (0x20000000 / 0x200000 / 0x4000000)

## Hypothesis under test

"The entity `info` word reuses the `mapSpriteInfo` `SPRITE_FLAG_*` layout, so
`0x20000000` = FLAT, `0x200000` = AUTOMAP_VISIBLE/NOENTITY, `0x4000000` = EAST,
and `0x4000000` is a highlight marker."

## Method

1. Exhaustive grep of the flat `src/` tree (`find . -type d` → single dir, so
   root-level `*.cpp *.h` greps are complete) for each literal.
2. Extracted every `info <op> 0x...` occurrence via
   `grep -rhoE "info *[&|^]=? *~?0x[0-9A-Fa-f]+" *.cpp *.h | sort | uniq -c`
   to build a complete bit census, including composite masks
   (`0x420000`, `0x1020000`, `0xF6FEFFFF`, ...).
3. Read every setter and tester call site; separated the entity word from the
   `mapSpriteInfo` word and from the third bitfield with colliding values, the
   `getSaveHandle` result.
4. Cross-checked meanings against `Entity::resurrect`'s clear mask
   `info &= 0xF6FEFFFF` (`src/Entity.cpp:1359`) — it clears exactly
   corpse | raise-reserved | removed, which agrees with the independently
   derived meanings of `0x1000000`, `0x8000000`, `0x10000`.

## Verdicts

| bit | verdict | meaning |
|---|---|---|
| `0x20000000` | CONFIRMED | idle breathing/bob suppressed (set = do not breathe) |
| `0x200000` | UNKNOWN | write-only in the entire port; no tester exists |
| `0x4000000` | UNKNOWN | write-only; "highlight marker" REFUTED as unsupported |
| `0x400000` (bonus) | CONFIRMED | dirty flag: full save record required |
| shares `mapSpriteInfo` layout? | REFUTED for all of them | see evidence below |

## Evidence

### 0x20000000 — breathing suppression

Setter, `EV_ENTITY_BREATHES` (`src/ScriptThread.cpp:1907-1920`):

```
short n38 = ...mapSprites[S_ENT + uByteArg9];
if (n38 != -1) {
    Entity* entity5 = &this->app->game->entities[n38];
    if (uByteArg10 == 1)      entity5->info &= ~0x20000000;
    else if (uByteArg10 == 0) entity5->info |=  0x20000000;
```

Note the inversion: opcode arg 1 (breathe = yes) CLEARS the bit.

Testers — `Render::renderMonster` (`src/Render.cpp:3193-3199`):

```
int n20 = (app->time + n * 1337) / 1024 & 0x1;
if ((entity->info & 0x20000000) != 0x0) { n20 = 0; }
int n21 = n20 * 26;
```

and `Render::renderFearEyes` (`src/Render.cpp:3046-3057`), which zeroes the
matching eye Z offset (`n11`, 26 px or `<<4` for cacodemon/sentinel/lost soul).
So the bit freezes the 2-frame, 1024 ms idle bob and its eye follow-up.

### 0x200000 — no reader

Setters only: `src/Combat.cpp:67` (attacker), `:70` (target) in
`Combat::performAttack`, and `src/Combat.cpp:1149` on a monster taking splash
damage. There is no `info & 0x200000` test anywhere: the only `& 0x200000`
tests in `src/` are on `mapSpriteInfo` (`src/AutomapController.cpp:142`) or on
the save handle `n` (`src/Entity.cpp:1514,1655,1907`). Since bit 21 lies inside
the persisted byte (`info >> 16 & 0xFF`, `src/Entity.cpp:1521`; restored at
`:1681`) it survives saves, still unread. Any name would be invention →
UNKNOWN.

### 0x4000000 — transient, no reader

Set only at `src/Combat.cpp:229`, inside `if ((this->crFlags & 0x1007) != 0x0)`
for `targetType == 2` (ET_MONSTER), i.e. "the roll hit". Cleared at
`src/Combat.cpp:373` (`this->curTarget->info &= 0xFBFFFFFF`) when the attack
animation leaves stage 1. No tester in `src/`. Bit 26 is outside the persisted
byte, so it is purely runtime. "Highlight marker" is not supported by any code
path: nothing in `Render`/`Hud` reads it.

### No shared layout with `mapSpriteInfo`

Low halves hold different data: entity `info & 0xFFFF` is sprite index + 1
(`src/Entity.cpp:115`; `src/Hud.cpp:286`; `src/Combat.cpp:1389,1456,1476`),
whereas `mapSpriteInfo & 0xFF` is the tile number (`src/Entity.cpp:1450`) and
bits 8-15 the anim/frame byte (`src/Entity.cpp:1524`). Bits with equal value
carry unrelated meanings (`0x400000`: SPRITE_FLAG_TILE `src/Enums.h:1238` vs
entity dirty flag `src/Entity.cpp:1816`). `src/Entity.cpp:1591-1593` handles
both words in the same statement pair, sprite-hidden vs entity-linked, which
would be impossible if they were the same layout.

### The `0xFFFF00FF | (v << 8)` idiom is `mapSpriteInfo`-only

All ~35 occurrences in `src/` target `app->render->mapSpriteInfo[...]`
(`src/Combat.cpp:157,438,529,548,576,1634,1640,1643`,
`src/Entity.cpp:351,359,374,463,1177,1627,1664,1889`,
`src/Game.cpp:385,390,395,774,1151,1494,2943,3062,3107,3110,3122,3203`,
`src/Render.cpp:1604,1616`, `src/ScriptThread.cpp:675,887`,
`src/DialogSystem.cpp:626`, `src/Player.cpp:2214`). Note
`src/Entity.cpp:463` uses local `n3`, which is the sprite-info value written
back at `src/Entity.cpp:526` (`app->render->mapSpriteInfo[sprite] = n3;`).
The entity word is never touched with that mask; its only bits-8-15 content is
the sprite index. Same is true of the rewrite: every `0xFFFF00FF` site in
`new_src/` holds a `mapSpriteInfo` value in a local named `info`
(`new_src/domain/game/ScriptVM.cpp:472-477`,
`new_src/domain/game/MonsterSystem.cpp:203-204`,
`new_src/domain/game/SpriteLerps.cpp:228`,
`new_src/domain/game/Game.cpp:50`), so `SpriteInfo::kAnimByteMask` is correctly
applied there.

## Full bit inventory

Moved into curated docs: `docs/original-code/entities.md` §5.

## Open questions

* What consumed `info & 0x200000` and `info & 0x4000000` in the J2ME original?
  Both may be dead remnants (the port has other write-only leftovers), or the
  readers may live in code paths this port dropped. Not answerable from `src/`.
* `0x80000` ("gib on death") has no runtime setter in `src/`; it only enters
  `info` from the save handle (`src/Entity.cpp:1715`). Its producer is unclear.
