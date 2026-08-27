---
description: Ask the architect to design how to build something in new_src (spec + ADR).
argument-hint: <feature to design>
---

Delegate to the `architect` agent (Agent tool, `subagent_type: architect`). First check `PLAN.md`, `docs/status.md` and the relevant `docs/original-code/*.md` yourself; if factual questions about the original remain open, send `researcher` agents in parallel BEFORE the architect and attach their results.

Design the following for the new_src rewrite, producing a full implementation-ready specification split into small implementation groups (and an ADR if the decision is significant):

$ARGUMENTS

Then report the spec path and the group breakdown to the user.
