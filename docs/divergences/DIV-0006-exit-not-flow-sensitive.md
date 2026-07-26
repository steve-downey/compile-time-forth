# DIV-0006: stack-effect checker is not flow-sensitive across EXIT

- **Status:** superseded by DIV-0011 (retired with its subject at step F26; the gap itself is inherited by step F30's effect lint, not closed)
- **Date:** 2026-07-19
- **Step:** F12 (stack-effect analysis)
- **Authority diverged from:** Forth-2012

## What diverged

Forth-2012 allows `EXIT` anywhere inside a colon definition's body, including
nested inside `IF`/`ELSE` arms, and a fully correct stack-effect checker
would verify that *every* return path -- an early `EXIT` and the eventual
fall-through to the end of the definition -- leaves the stack at a depth
consistent with the definition's own effect.
`stack_effect.hpp`'s `analyze_body` does not do this: once a `core_exit`
node is encountered while folding a body's item list, the remaining items in
that *same* body are treated as unreachable and are never visited (a sound
simplification for an unconditional trailing `EXIT`, since code after it
never runs), but the checker never reconciles an early-return path's own
depth against the depth computed by continuing past the control structure
that contains the `EXIT`.

Concretely: `: F DUP 0< IF EXIT THEN DROP ;` has two genuinely different
real effects depending on which path executes (the early-exit path leaves
one cell; the fall-through path leaves zero, since `DROP` runs), but the
checker computes a single effect for the whole definition and does not flag
the inconsistency.

## Why

Modeling every return path correctly requires either a set-of-possible
return effects threaded through the whole walk, or a dedicated "terminal"
lattice value with its own combination rules distinct from the plain
`known`/`unknown` lattice already in place -- a materially larger feature
than the five diagnoses `docs/forth-plan.md`'s Step F12 section literally
asks for (unequal `IF`/`ELSE` net effect; nonzero-net-effect loop bodies;
`>R`/`R>` imbalance; `EXIT` inside `DO` without `UNLOOP`; declared-vs-computed
mismatch -- none of which mention reconciling multiple return paths).
Building it now would have expanded this step well past a small, mergeable
change for a case the plan's own diagnosis list does not call out.

## Consequences

- Every diagnosis the plan does specify for F12 fires correctly and is
  covered by `stack_effect.test.cpp`; this gap is additional exposure beyond
  the plan's literal ask, not a missed merge criterion.
- A definition using `EXIT` only as an unconditional trailing form (as in
  F11's own `ExitBecomesCoreExit` test, `: E DUP EXIT DROP ;`) is analyzed
  correctly, since there is only one reachable path.
- A definition using `EXIT` conditionally with a genuinely different
  early-return depth than its fall-through depth will not be diagnosed by
  F12, even though it is a real Forth-2012 stack-effect defect.
- Step F17 (counted loops, which also touches `EXIT`/`UNLOOP` semantics) and
  Step F21 (error-quality and negative-compile pass) are the natural places
  to either accept this gap permanently (documented, not silently) or close
  it with a proper multi-path effect model.

## Revisit condition

Revisit if a future step needs to diagnose a real program whose `EXIT` paths
disagree in depth -- at that point, either a set-of-return-effects model or a
dedicated terminal lattice value (distinct from plain `unknown`) would need
to replace the current "stop folding at EXIT" simplification in
`analyze_body`.
