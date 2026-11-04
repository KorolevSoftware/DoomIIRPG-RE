# 2026-11-04 — Original sound engine

**Question:** how does `src/Sound.cpp` map resIDs to files, select channels, handle
flags/priority/fades/volume, and is there per-frame logic or suspend handling?

**Method:** read `src/Sound.cpp` (all 602 lines), `src/Sound.h`, `src/Sounds.h`,
`src/JavaStream.cpp:68-90`; grepped every `sound->` user (with `LC_ALL=C grep -a`,
since `src/MenuSystem.cpp`, `src/Graphics.cpp`, `src/Input.cpp` are ISO-8859 and plain
grep skips them as binary). Diffed the name table against `unzip -l` of the .ipa
and surveyed all 148 WAV headers (read-only, Python `zipfile`).

**Verdict:** CONFIRMED / documented in `docs/original-code/sound.md`.

**Key evidence**
- `index = (uint16_t)(resID - 1000)` into 145-entry table (`src/Sound.cpp:180`,
  `src/Sounds.h:7-152`); path prefix `Payload/Doom2rpg.app/Packages/sounds2/`
  (`src/JavaStream.cpp:83`). 3 archive WAVs unused (SpiderMastermind_pain/walk, flames).
- Music = 1067..1071 (`src/Sound.cpp:341`). No-op IDs -1, 255, 1255 (`:330-339`).
- flags bit0 loop (`:439`), bit1 stop-all (`:396-403`); `a5` unused.
- 10 slots; same sound playing -> duplicate in another slot unless priority 6
  (`:353-385`); `getFreeSlot` steals lowest priority `< p` and `< 6` (`:445-472`).
- Fades are started (`:493,581-587`) but never advanced: `endFrame` empty (`:577-579`).
- `startFrame` deferred play gated by `field_0x162`, never set (`:30,564`) -> dead.
- Volume -> `AL_MAX_GAIN = vol/100` (`:112`); `updateVolume` derives allow flags from
  volume != 0 (`:530-531`); defaults 80/80 (`:34-35`); persisted in config v11
  (`src/Game.cpp:1922-1924,1985-1988`).
- Suspend/resume functions never called (`src/Sound.cpp:115-127`,
  `src/CAppContainer.cpp:63-69`).

**Open questions:** none blocking. The rewrite must decide whether to replicate the
duplicate-overlap behaviour and the effectively-ignored saved `allowSounds` bool.
