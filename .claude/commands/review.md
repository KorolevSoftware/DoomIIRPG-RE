---
description: Read-only review of the current change set against its spec and the original behavior.
argument-hint: [spec path or scope]
---

Delegate to the `reviewer` agent (Agent tool, `subagent_type: reviewer`) with the task description, the spec path and the change scope below (default: uncommitted changes — `git status --porcelain` / `git diff`):

$ARGUMENTS

Relay the PASS/FAIL verdict verbatim; on FAIL, hand the numbered items to the `coder` agent and re-review once fixed.
