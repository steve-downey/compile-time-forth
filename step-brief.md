# step-brief.md — after Step F36: Consolidation

Forward-only brief. Bounded; not a log. Prior-step narrative lives in
`git log`; architecture lives in `docs/compiler_architecture.org` —
consult it only where pointed, by anchor.

## F36 is the last step in checklist.md

There is no successor step to hand off to. `checklist.md`'s "True Forth
revision" section is now fully checked (F23 through F36); the only
remaining unchecked line in the whole file is "Blog: F36 consolidation
(Part 25)", which is the blog agent's own job, not a worker's — see
`docs/blog/AGENTS.md`. If you are a worker reading this file expecting a
next step to implement, there isn't one: read `docs/forth-plan-2.md`'s own
revision-table framing, and if new work is wanted it needs a new plan
section (and a checklist line) before anything is dispatched against it,
not an inference from this file.

## What F36 did, by anchor

`docs/compiler_architecture.org`'s new Phase 19 section ("Consolidation")
covers this step's own work in full. Its two `**` subsections (the merge
criteria are the second) name what changed and why; this brief does not
restate them.

- **`docs/forth-limitations.md` is new** — the roll-up the plan's own step
  text asks for: rescoped D12, D21's cell-granularity characteristic, and
  every DIV-0001 through DIV-0030 (each superseded entry marked as such).
  DIV-0031 through DIV-0034 were allocated to this step and went unused.
- **`compile-time-forth.org` (repo root) is rewritten**, transcluding a
  working subset of `compiler_architecture.org`'s own anchors instead of
  the retired R1-era placeholder it carried since step F15. If Part 25's
  blog post wants a code excerpt to pin, the anchors it reuses are:
  `18245977-b4d2-4011-bac7-a36f7680aeb4`/`9affe4fc-3d72-4f41-8fd0-d2338f9ab603`
  (`forth.hpp`, the public API), `aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62`
  (`interp.hpp`, the interpreter loop), `c6e9f1b3-8a2d-4c7e-9f1a-3b6d8e2c5a7f`
  (`session.hpp`, the session image), `3fd1b4b2-6cf1-4b6e-9b6b-3f2c0a8f7d21`
  (`effect_lint.hpp`, the lattice), `e7f4b8a2-1c6d-4e3f-9a5b-2d7c8e1f4a6b`
  (`sender/lower.hpp`), `c46f1cb1-63a2-4d0e-9e2c-6a45f2c9d5b1`
  (`prelude.hpp`, the Forth-source prelude itself — the most quotable one,
  since it is Forth, not C++), and `bf3c6a19-4e27-4d5a-9c81-7a2f6b0d4e93`
  (`foreign.hpp`). All eight already existed in `compiler_architecture.org`
  before this step; none is new.
- **`README.md`'s opening is rewritten** off the R1-era "returns my name"
  placeholder description, onto what the project actually is, with
  pointers to the architecture doc, the limitations doc, the plan, and the
  presentation.
- **The error-quality pass**: `interpreter/diagnostics.test.cpp` (new) is a
  7-case table-driven positioned-diagnostic battery — message *and*
  `source_pos` (offset/line/column) — covering unresolved-name,
  unterminated-construct, control-word-misuse, stack-fault, and
  declared-effect-mismatch diagnosis kinds; three cases are also
  static_assert-checked. `test_neg_effect_mismatch.cpp` and
  `test_neg_capacity_overflow.cpp` (new) extend
  `test_neg_syntax_error.cpp`'s own `EXCLUDE_FROM_ALL`+`WILL_FAIL` CTest
  pattern, unchanged, to a declared-effect mismatch and a data-stack
  capacity overflow respectively.

## A defect found and fixed, not merely documented

`interpreter/session.hpp`'s `build_session` copied the build-time data
stack into the returned session's `stack` snapshot with no bound check of
its own: a program leaving more cells behind than the caller's own
`MaxStack` could hold reached `foundation::static_vector::push_back` past
capacity — an assertion failure (UB in a release build), not a
`foundation::result` a caller could observe. This violated the project's
own durable "misuse is a diagnosed error, never UB" invariant, so it is
fixed in place (`session.hpp` now diagnoses
`st.data().depth() > MaxStack` before building the snapshot;
`session.test.cpp`'s `BuildSessionDiagnosesFinalDepthExceedingMaxStack` is
the regression). This is an ordinary bug fix within F36's own remit, not a
plan or Forth-2012 divergence — it carries no DIV.

Note for the capacity-overflow negative-compile test specifically:
`compiled_forth`'s template parameters name two *different* stack
capacities — `MaxStack` (7th, the returned session's snapshot capacity,
the one the fix above guards) and `BuildDepth` (8th, the transient
build-time `forth_state`'s own data-stack capacity, enforced during
interpretation by `machine::cell_stack::push`'s existing "stack overflow"
diagnostic). `test_neg_capacity_overflow.cpp` pins `BuildDepth` down,
deliberately, because that path was already cleanly diagnosed before this
step and needed no fix to serve as a negative-compile test; the `MaxStack`
path needed the fix above precisely because it was not clean before it.

## Gotchas this step found, for whoever next touches this ground

- **Plain `make presentation` (no `TOOLCHAIN=`) fails on this machine, for a
  reason that predates this step and is not this step's to fix.** It builds
  against whatever `c++` resolves to (here GCC 15.2.0), and
  `conformance/core_suite_strings.test.cpp`'s own `static_assert(text_of(
  strings_suite.output()).find("BOOM") != ...)` fails GCC 15's own
  `constexpr` evaluation of `std::string_view::find` ("is not a constant
  expression"), reproduced identically on pristine `main` with a bare
  `c++ -std=gnu++26 -fsyntax-only` on that one file — nothing this step
  touched. `TOOLCHAIN=gcc-16` (the project's own baseline compiler,
  AGENTS.md) does not have this problem: `make TOOLCHAIN=gcc-16
  presentation` builds, tests (429/429), and exports both
  `compile-time-forth.html` and `compile-time-forth-slides.html` cleanly.
  If a future step's own gate says "make presentation succeeding" with no
  qualifier, read it as "under the project's baseline toolchain" and pass
  `TOOLCHAIN=gcc-16` explicitly rather than trusting the system default.
- **Compute expected diagnostic offsets by actually running the program,
  never by hand.** A throwaway `g++-16 -std=c++26 -Isrc` translation unit
  linking nothing but this project's own headers (no CMake, no test
  framework) is enough to print `r.error().message`/`.where.offset` for a
  candidate bad program directly — this is how every expected value in
  `diagnostics.test.cpp`'s table was obtained. Hand-computing a byte offset
  from a multi-line source string is exactly the kind of thing that is
  confidently wrong in a way `make test` alone will catch late.
- **`build_session_with_prelude`'s and `build_session`'s template parameter
  orders are not identical to `compiled_forth`'s own naming**, and it is
  easy to overshrink the wrong one. `compiled_forth<Source, MaxCode,
  MaxWords, MaxData, MaxOut, MaxName, MaxStack, BuildDepth, BuildRDepth,
  Fuel>` — position 7 is the *returned session's* stack-snapshot capacity,
  position 8 is the *transient build-time* stack capacity that
  interpretation itself pushes/pops against. A capacity-overflow test
  aimed at "stack overflow" (an interpretation-time diagnostic) needs
  position 8, not position 7.

## Standing constraints (carried forward, still true)

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. Run `make lint` as the very last thing before committing, and
  commit whatever it reformats.
- Watch `pgrep -af cc1plus` and its RSS before launching a new build if any
  step touches sender/Execution26 composition; memory, not wall clock, is
  the binding constraint. Sender-side tests stay sharded one or two
  programs per translation unit, one capacity combination per file.
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
  The `session.hpp` fix above is exactly this invariant's own enforcement,
  extended to a spot that had quietly slipped it.
- `src/smd/forth/foundation/` is a mirror of `~/src/compile-time-scheme`'s
  kit and is never hand-edited; `scripts/sync-kit.py --check` against the
  kit's main (the default `--scheme`) is the drift check.
