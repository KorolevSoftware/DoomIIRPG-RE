# DoomIIRPG-RE — agent handbook

Reverse-engineering rewrite of **Doom II RPG** (J2ME) as modern C++ / OpenGL 3.3.

## Layout

| Path | Role |
|---|---|
| `src/` | Legacy C++ RE port of the original game (TinyGL software rasterizer + GLES GL-path). READ-ONLY reference — the ground truth for gameplay/render behavior. |
| `Doom 2 RPG Java/` | Original J2ME `.class` files. Not used; ignore entirely. |
| `new_src/` | The rewrite target. ALL development happens here. |
| `tools/` | Python verification tools (e.g. `map_to_obj.py`). |
| `build_new/` | Build directory for `new_src`. |

## Build & run

- Configure (once, or after adding/removing files — CMake uses GLOB): `cmake -S . -B build_new`
- Build: `cmake --build build_new -j 8`
- Run: `cd build_new/new_src && ./DoomIIRPG`
- Game archive expected at `build_new/new_src/Doom 2 RPG.ipa`.

## Ground rules

- NEVER modify anything under `src/` or `Doom 2 RPG Java/`.
- Canvas 480x320 (letterbox), GL 3.3 core + shaders, SDL2 window/input, zlib for the `.ipa`.
- Fixed-point heritage: original uses 14.14 fixed point; key conversions are documented in `PLAN.md`.
- The user is the "eyes": nobody inspects rendered images/screenshots with scripts. After building, ask the user what is visible on screen.
- Do not git-commit unless explicitly asked.
- Documentation and code comments: English.

## Subagent reliability (unstable network — keep!)

The backend intermittently kills subagent runs (empty/truncated results, 429
rate limits), write-capable agents more often than read-only ones.

- **NEVER relaunch a failed / empty / truncated subagent task from scratch.**
  Every task returns a `task_id`: re-invoke the SAME agent type with that
  `task_id` (+ a short "continue from where you stopped" prompt) to WAKE it
  with its full prior context (messages read, partial edits, plan).
- Still empty after a wake → run a tiny probe task (e.g. one scratch file),
  then immediately wake the real task again.
- Split big implementations into small per-group tasks; verify landed work
  yourself (`grep`, `cmake --build`) after EVERY delegation — reports can be
  lost even when edits landed.
- Do not use agents without file-write tools for edit work.

## Documentation system (keep it alive)

| File | Content | Updated by |
|---|---|---|
| `PLAN.md` | Roadmap/phases checklist + verified low-level formats | orchestrator |
| `docs/status.md` | Current focus, recent results, next steps | orchestrator |
| `docs/journal.md` | Append-only work log (date → what → result) | whoever completes a unit of work |
| `docs/original-code/` | Curated verified facts about `src/`, per topic, every claim cites file:line | researcher |
| `docs/research/` | Raw dated investigation reports (hypothesis → verdict → evidence) | researcher |
| `docs/architecture/` | How `new_src/` is designed: module map, specs, ADRs in `adr/` | architect |

Rule: every factual claim cites a location — `src/File.cpp:123` for the original,
`new_src/File.cpp:123` for the rewrite.
