# DIV-0024: `CATCH` cannot restore `i*x` if the caught word itself popped below it

- **Status:** open
- **Date:** 2026-07-26
- **Step:** F32 (conformance), docs/forth-plan-2.md
- **Authority diverged from:** Forth-2012 (`CATCH`'s own stack notation: `( i*x xt -- j*x 0 |
  i*x n )` — `i*x` is restored verbatim on a throw, regardless of what the caught execution did to
  it internally)

## What diverged

Forth-2012's own `CATCH` signature promises `i*x` back, unchanged, on the thrown path — the
caller's own pre-existing stack items survive a throw exactly as if the caught word had never run.
Nothing in the standard requires the caught word to leave those items *alone*; a real, common
pattern is a word that takes an argument as (implicitly) part of `i*x`, consumes it, and may throw
after doing so (`5 ' SQRT CATCH`, where `SQRT` reads its own argument off the stack before ever
deciding whether to throw on a negative input, is exactly this shape).

This project's `perform_throw` (`vm.hpp`) restores `i*x` by calling `cell_stack::truncate(int)`
with the *data*-stack depth `CATCH` recorded when it began (`op::catch_mark`). `truncate` itself
(`machine/stacks.hpp`) explicitly refuses to *grow* the stack back up: `if (new_depth < 0 ||
new_depth > depth_) { return ...error...; }`. If the caught word's own execution popped anything
that was already present below the `xt` before throwing (exactly the `SQRT`-argument shape above),
the depth at throw time is now *lower* than the depth `CATCH` recorded, and `truncate` diagnoses
`"stack truncate: depth out of range"` instead of restoring `i*x` — the whole build fails (a hard
compile error under `compiled_forth<Source>`, D15) rather than the throw succeeding.

Confirmed directly: `": CHK ABORT\" BOOM\" ; : TRYCHK 1 ['] CHK CATCH ;"` (the caller pushes `1`
as `CHK`'s own leading flag, `CHK`'s own `ABORT"` pops that same `1` as its condition before
throwing `-2`) fails to build with exactly this message. Reshaping the same test into the pattern
`interp.test.cpp`'s own `AbortQuoteCaughtByCatch` test already uses instead —
`": CHECK ABORT\" BOOM\" ; : RUN -1 CHECK 999 ;"`, where `RUN` pushes its *own* flag internally,
consumed entirely within `RUN`'s own execution, never dipping into whatever was below `RUN`'s own
`xt` when `CATCH` began — works fine, because nothing below the recorded depth is ever touched.

## Why

**A real gap, not a scope cut or a declared characteristic.** D12's own scope cuts do not mention
`CATCH`/`THROW`, and D14 leg (b)/(c) (the gforth differential oracle and constant-evaluation test
battery) exist precisely to surface a gap like this one — this is exactly that mechanism working.

**Not fixed in this step.** `cell_stack::truncate`'s own shrink-only guard is deliberate (F31's own
design, DIV-0018) — nothing in this step's own scope (conformance testing) asks for a redesign of
`perform_throw`'s own restoration mechanism, and doing so correctly needs either (a) snapshotting
`i*x`'s own values (not just the depth) when `CATCH` begins, so genuinely popped values can be
replayed back rather than merely un-truncated, or (b) documenting that a caught word must never pop
below its own call boundary — a real design decision, not a testing-step's call to make.

## Consequences

- `core_suite_strings.test.cpp`'s own `ABORT"`/`CATCH` interaction test uses the `RUN`-style
  "push-your-own-argument-internally" shape (matching `interp.test.cpp`'s own already-passing
  `AbortQuoteCaughtByCatch`), not the "caller pre-supplies the callee's own argument" shape that
  triggers this gap.
- `docs/conformance-exclusions.md` records this gap under its own citation; any future test written
  against the "caller pre-supplies an argument the callee pops before throwing" shape is expected
  to fail for this reason, not by accident.
- A future step wanting to close this must choose between the two designs in "Why" above and
  extend `perform_throw`/`CATCH`'s own frame accordingly.

## Revisit condition

Closed once a future step either snapshots `i*x`'s own actual values at `CATCH` time (not merely
its depth) or documents and diagnoses "a caught word popped below its own call boundary" as its own
distinct, intentional error rather than a generic stack-underflow-shaped `"depth out of range"`
message. Not currently named by any step in docs/forth-plan-2.md.
