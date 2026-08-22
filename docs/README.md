# Documentation index

Knowledge base for the Doom II RPG rewrite. Language: English.

| Location | Content |
|---|---|
| [`../PLAN.md`](../PLAN.md) | Roadmap, phases, verified low-level formats (single plan source) |
| [`status.md`](status.md) | Current focus, recent results, next steps |
| [`journal.md`](journal.md) | Append-only work log (date → what → result) |
| [`original-code/`](original-code/) | Verified facts about the original (`src/`), one file per topic |
| [`research/`](research/) | Dated raw investigation reports (hypothesis → verdict → evidence) |
| [`architecture/`](architecture/) | Design of `new_src/`: module map, ADRs |

## Conventions

1. Every factual claim cites a location: `src/File.cpp:123` for the original, `new_src/File.cpp:123` for the rewrite.
2. Prefer updating the right topic file over creating near-duplicates.
3. New significant design decision ⇒ new ADR `architecture/adr/NNNN-slug.md` + index line in `architecture/README.md`.
4. Finished unit of work ⇒ journal entry + `status.md` refresh (+ `PLAN.md` checkbox by the orchestrator).
