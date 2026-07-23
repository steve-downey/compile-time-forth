# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

The governing rules for this repository live in `AGENTS.md`. Read `AGENTS.md`
first; it defines a bounded **three-tier reading contract** — Tier 1 rules pack
(`docs/codestyle.org` authoritative style, `docs/CODING_RULES.md`, this file), Tier 2
this step (`step-brief.md`, `checklist.md`), Tier 3 on demand only
(`docs/compiler_architecture.org` by anchor, `docs/forth-plan.md` as the
orchestrator's DAG, `git log`). Nothing on the read path grows per step;
`docs/forth-plan.md` is an orchestrator/design document that workers do not read
whole, and the retired cumulative log lives at `docs/history/handoff-archive.md`
(archival, not read).

Rule precedence:

```txt
docs/codestyle.org > AGENTS.md > docs/CODING_RULES.md > CLAUDE.md > step-brief/checklist files
```

Do not duplicate rules here. If this file and `AGENTS.md` ever disagree,
`AGENTS.md` wins; fix this file.
