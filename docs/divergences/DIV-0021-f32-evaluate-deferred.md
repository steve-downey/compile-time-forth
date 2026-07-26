# DIV-0021: EVALUATE deferred, not built

- **Status:** accepted-permanent (until a future step's own criterion demands `EVALUATE`)
- **Date:** 2026-07-26
- **Step:** F32 (conformance), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F32's own step text: "`EVALUATE` if the suite
  demands it (else deferred with DIV)")

## What diverged

`EVALUATE` (Forth-2012 core: `( i * x c-addr u -- j * x )`, interpret the given string as Forth
source using the current input source's own state, temporarily) is not implemented. F32's own
governing step text names this exact choice explicitly and defers the decision to whichever
worker actually builds the conformance suite: build it only if the suite demands it.

## Why

Neither the adapted Hayes ttester (`ttester_corpus.hpp`, DIV-0020) nor any `core_suite_*.test.cpp`
shard this step adds calls `EVALUATE` anywhere. Upstream's own `ttester.fs` does not use it either
(confirmed by inspection of `/usr/share/gforth/0.7.3/test/ttester.fs`). D12 (rescoped) already
lists `EVALUATE` among the words this project treats as optional rather than committed scope.
Building it speculatively, with no test in this step that exercises it, would be exactly the kind
of unrequested feature this project's own house style disallows ("write code that tools can
normalize cleanly" cuts against dead, untested surface area, not for it).

## Consequences

- `docs/conformance-exclusions.md` lists `EVALUATE` under "deferred per the orchestrator's own 1+
  policy," citing this DIV.
- A future step (most plausibly F33's sender backend, if it needs to interpret a string produced
  at runtime, or F36 consolidation, if a later conformance pass adds a test that genuinely needs
  nested-source-interpretation) should build `EVALUATE` only once it has a concrete consumer, not
  ahead of one.

## Revisit condition

Closed the moment any step's own merge criterion names a test, program, or consumer that actually
calls `EVALUATE` — at that point, build it there rather than continuing to defer.
