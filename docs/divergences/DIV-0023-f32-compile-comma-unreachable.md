# DIV-0023: `COMPILE,` has no currently-reachable valid usage

- **Status:** open
- **Date:** 2026-07-26
- **Step:** F32 (conformance), docs/forth-plan-2.md
- **Authority diverged from:** Forth-2012 (`COMPILE,`'s own standard usage pattern); this project's
  own `machine/dictionary.hpp` (`COMPILE,`'s own immediate flag)

## What diverged

Forth-2012's own idiom for `COMPILE,` is: define an *immediate* helper word whose own body ends in
`['] TARGET COMPILE,` (or the `POSTPONE`-based equivalent); using that helper *inside a third
word's own definition* appends a call to `TARGET` into that third word, at the moment the helper
runs (which is while the third word is being compiled, since the helper is immediate). This only
works because `COMPILE,` **itself is not immediate** in Forth-2012: while the helper is being
*defined*, `COMPILE,` is compiled into the helper's own body (a deferred call) rather than executed
then and there; it only actually appends anything once the helper itself later runs. Verified
directly against real `gforth 0.7.3`: `: FOO ['] DUP COMPILE, ; IMMEDIATE : BAZ 5 FOO ; BAZ .s`
prints `<2> 5 5` (`BAZ` ends up behaving as `5 DUP`).

This project's own `COMPILE,` (`control_builtin::compile_comma_`) is installed as an *immediate*
control word (`machine/dictionary.hpp`'s own `immediate_control_words` table, step F27). Writing
the textbook idiom directly — `: DOUBLER ['] DBL COMPILE, ;` — underflows the stack: `[']`
compiles a *deferred* literal-push of `DBL`'s own execution token (it only takes effect once
`DOUBLER` itself later runs), but `COMPILE,`, being immediate, executes **right then**, while
`DOUBLER` is still being defined — at that moment the transient build-time data stack has nothing
on it for `COMPILE,` to pop. Confirmed directly: `build_session` on `": DOUBLER ['] DBL COMPILE, ;
"` alone (never even calling `DOUBLER`) fails with `"stack underflow"`.

## Why

**Discovered by F32's own conformance work**, not designed in: `core_suite_defining.test.cpp`
originally included the textbook idiom as a `T{ ... -> ... }T` assertion (mirroring the shape
`core_suite_defining.test.cpp`'s own doc comment lists among the execution-token surface this
project's earlier steps already exercise) and it failed to build at all. D14 leg (b)/(c) — the
gforth differential oracle and the constant-evaluation test battery — exist precisely to surface
gaps like this one.

**Not a simple reclassification.** Flipping `COMPILE,`'s own table entry from
`immediate_control_words` to `ordinary_control_words` is not sufficient by itself: `interp.hpp`'s
own `compile_comma_` case (`apply_control_word`) pops an execution-token index directly off
whatever data stack is *currently active* and calls `compile_entry` against `buf` (the interpreter's
own transient code space) immediately, in the same call. If `COMPILE,` were merely
non-immediate, the textbook helper word's own body would need this same "pop an xt, append to
whatever `buf` is active *right now*" action to happen **again, later, at the helper's own runtime**
— which means it needs a real compiled VM opcode with access to the *interpreter's own live
compile_buffer*, not merely the ordinary `machine::forth_state` the ordinary VM opcodes
(`EXECUTE`/`CREATE`/`DOES>`/`CATCH`/`THROW`) already operate on. None of those needs a live
`compile_buffer&` at their own VM-opcode runtime; `COMPILE,`'s textbook usage does. That is new
plumbing this step's own scope (conformance testing) does not extend to building.

## Consequences

- `core_suite_defining.test.cpp` does not test `COMPILE,`'s own textbook macro-defining-word usage;
  its own doc comment cites this DIV. `docs/conformance-exclusions.md` lists it under "not yet
  implemented" alongside the citation.
- `COMPILE,` remains installed and dispatchable (no test regresses); it is simply not
  *demonstrated* to have a working use case beyond what step F27/F28's own existing tests already
  exercise (none of which uses the textbook idiom either — `interp.hpp`'s own doc comment for
  `compile_comma_` has never had a passing example).
- A future step correcting this would need: (a) move `COMPILE,` to `ordinary_control_words`; (b) a
  real compiled form (a new `op::` opcode, or reuse of an existing "call through a live
  compile_bufer" mechanism) so a helper word's own *runtime* execution of `COMPILE,` can reach the
  buffer of whatever definition is actively being compiled at that moment; (c) new tests proving
  the textbook idiom now round-trips, mirroring this DIV's own gforth reference behavior.

## Revisit condition

Closed once a future step gives `COMPILE,` a real, non-immediate, runtime-reachable compiled form
and a passing test demonstrates the textbook `['] TARGET COMPILE,`-inside-an-immediate-helper
idiom.

**Orchestrator assignment: F36 owns this.** The record originally left the owner open ("not
currently named by any step"), which is how a filed bug quietly becomes a permanent one. This is
not an error-quality nicety — `COMPILE,` is a shipped word whose standard usage pattern cannot be
reached, verified against real gforth — so it belongs with the other correctness debts F36 is
carrying rather than with its documentation work. F36's list, as of this step:

- this record (`COMPILE,`'s immediate flag),
- DIV-0014's unknown-word negative-compile translation unit,
- DIV-0024 (`CATCH` cannot regrow the data stack below its saved depth), if no earlier step closes it,
- DIV-0021 (`EVALUATE`), if still deferred by then.

If F36 finds any of these too large to absorb, it must say so and propose a step rather than
letting the record lapse a second time.
