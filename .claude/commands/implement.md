---
description: Full orchestration cycle for a task — research, architecture, coding, review, docs update.
argument-hint: <task>
---

Run the complete development cycle for this task, acting as the orchestrator described in `CLAUDE.md` (research → architecture → implementation → review → docs update):

$ARGUMENTS

Sequence:
1. Restate the goal; read `PLAN.md`, `docs/status.md` and relevant `docs/original-code/*.md` — facts may already exist.
2. Open factual questions about the original → `researcher` agents in parallel.
3. Design → `architect` agent, attaching research results or doc paths.
4. Implementation → `coder` agent, ONE spec group per delegation. Verify landed work yourself (`grep`, `cmake --build build_new -j 8`) after every delegation.
5. Review → `reviewer` agent (task, spec path, change scope). On FAIL send items back to the coder and re-review.
6. Close the loop yourself: tick `PLAN.md`, append a dated entry to `docs/journal.md`, refresh `docs/status.md`.
7. Ask the user what is visible on screen for anything visual — you are not the eyes.
