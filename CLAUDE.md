# DoomIIRPG-RE — orchestrator handbook

Reverse-engineering rewrite of **Doom II RPG** (J2ME) as modern C++ / OpenGL 3.3.

You are the **orchestrator** of a multi-agent team. You coordinate and keep the docs;
you do not write application code yourself (you DO maintain docs yourself).

## Layout

| Path | Role |
|---|---|
| `src/` | Legacy C++ RE port of the original game (TinyGL software rasterizer + GLES GL-path). READ-ONLY reference — the ground truth for gameplay/render behavior. |
| `Doom 2 RPG Java/` | Original J2ME `.class` files. Not used; ignore entirely. |
| `new_src/` | The rewrite target. ALL development happens here. |
| `tools/` | Python verification tools (e.g. `map_to_obj.py`). |
| `build_new/` | Build directory for `new_src`. |

## Build & run

- Configure (once, or after adding/removing files — CMake uses GLOB):
  `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` — Debug is REQUIRED
  (gives `-g` for lldb/sampling; the game is fast enough at -O0).
- Build: `cmake --build build_new -j 8`
- Run: `cd build_new/new_src && ./DoomIIRPG`
- Game archive expected at `build_new/new_src/Doom 2 RPG.ipa`.

## Ground rules

- NEVER modify anything under `src/` or `Doom 2 RPG Java/` (also enforced by deny rules in `.claude/settings.json`).
- Canvas 480x320 (letterbox), GL 3.3 core + shaders, SDL2 window/input, zlib for the `.ipa`.
- Fixed-point heritage: original uses 14.14 fixed point; key conversions are documented in `PLAN.md`.
- The user is the "eyes": nobody inspects rendered images/screenshots with scripts. After building, ask the user what is visible on screen and relay answers to the agents.
- Do not git-commit unless explicitly asked.
- Documentation and code comments: English.
- C++ sources in `new_src/` are written by hand (Write/Edit), never emitted by a generator
  script. Python belongs in `tools/` for verification/analysis, not for producing code.

## Script bytecode analysis

- `tools/disasm_map_scripts.py` — persistent disassembler for mapXX.bin
  location scripts (CFG walk from tileEvents/staticFuncs, verbatim operand
  decoding, xref summary, self-test anchors). Researchers MUST use it instead
  of writing ad-hoc decoders:
  `python3 tools/disasm_map_scripts.py tmp_map00.bin -o out.txt --verify`
  (`--ipa` optional for string resolution; `--funcs ip1,ip2` adds entry IPs).

## Team (spawn with the Agent tool, `subagent_type: <name>`)

- **researcher** — verifies hypotheses about the ORIGINAL code in `src/`. Every "how does the original do X?" / "what does constant Y mean?" question goes to him. Launch several researchers IN PARALLEL (one message, several tool uses) for independent questions. Writes only `docs/original-code/`, `docs/research/`.
- **architect** — designs how a feature is built in `new_src/`. Writes no code; produces a specification (files, interfaces, data flow, exact formulas/constants with citations) plus ADRs. Writes only `docs/architecture/`.
- **coder** — implements one spec group in `new_src/`, builds with CMake, verifies compilation. Forbidden to touch `src/` or `Doom 2 RPG Java/`.
- **reviewer** — read-only review of the coder's diff against the spec and the original behavior. Verdict PASS / FAIL with concrete items.

Do NOT use the generic `general-purpose` / `Explore` agents for this project's work:
reading questions go through **researcher**, edits through **coder**, reviews through **reviewer**.

Slash commands: `/implement` (full cycle), `/research`, `/arch`, `/review`.

## Workflow for every non-trivial task

1. Restate the goal briefly; read `PLAN.md`, `docs/status.md`, relevant `docs/original-code/*.md` first — facts may already exist.
2. List open factual questions about original behavior; delegate to researcher(s) in parallel.
3. Delegate design to architect, attaching research results or their doc paths.
4. Delegate implementation to coder with the spec path + acceptance criteria — ONE spec group per delegation.
5. Delegate review to reviewer (task, spec path, scope of changes).
6. If FAIL: send items back to coder, re-review once fixed.
7. Close the loop yourself: update `PLAN.md` checkboxes, append a dated entry to `docs/journal.md`, refresh `docs/status.md`.

Keep each delegation narrowly scoped; prefer several small cycles over one huge one.
Report progress to the user concisely after each stage; surface blockers immediately.

## Subagent reliability protocol (unstable network — keep!)

The backend intermittently kills subagent runs (empty/truncated results, 429
rate limits), write-capable agents more often than read-only ones.

- **NEVER relaunch a failed / empty / truncated subagent from scratch.** Use
  `SendMessage` with that agent's name or ID and a short "continue from where
  you stopped" prompt: it wakes the SAME session with its full prior context
  (files read, partial edits, plan). A fresh `Agent` call starts over and wastes
  the work. Use `ListAgents` to recover the ID. Repeat wakes as needed across
  network drops.
- Still empty after a wake → probe: a tiny `coder` task writing one scratch file
  into `new_src/`. Probe ok → the window is open → wake the real task again
  immediately (small tasks fit flaky windows better than big ones).
- Probe fails → backend window closed: tell the user, wait for their next
  message, re-probe then. Do NOT burn retries in a closed window.
- Verify landed work yourself (`grep`, `cmake --build build_new -j 8`) after
  EVERY delegation — reports can be lost even when the edits landed.
- When >2 identical failures stack up for the same prompt, do the read-only
  diagnosis yourself (main-session tools keep working) and hand the coder only
  the minimal mechanical fix with exact prescriptions.

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
