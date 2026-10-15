# DIV-0030: F34's "static_assert via VM and sender backends" is delivered as a static_assert via the VM plus a runtime equivalence check via senders

- **Status:** accepted-permanent
- **Date:** 2026-07-29
- **Step:** F34 (foreign function interface), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md §6 F34's own merge criteria

## What diverged

F34's merge criteria read, verbatim: "static_assert computing through a constexpr
foreign word via VM and sender backends."

Taken literally that asks for a `static_assert` whose evaluation drives the sender
backend. That is impossible, and known to be: `beman::execution26::sync_wait` is not
constexpr-capable -- its own implementation uses `run_loop`, `std::exception_ptr`, and
`throw`/`rethrow_exception`, none usable in a constant expression -- which step F33
confirmed directly and recorded in `docs/compiler_architecture.org`'s own Phase 16
("The constexpr capability split: a positive D24 outcome, not a gap").

This step therefore reads the criterion as two halves:

- **The `static_assert` half runs through the VM.** `machine/foreign.test.cpp`,
  `interpreter/interp.test.cpp`, and `forth.test.cpp` each carry
  immediately-invoked-lambda `static_assert`s that compute through a `constexpr`
  foreign word -- at the raw `op::foreign` level, through `interpret` (interpreted,
  compiled, and via `'`/`[']`/`EXECUTE`), and through the public
  `compiled_forth_with` entry point respectively.
- **The sender half is a runtime equivalence check.**
  `sender/lower_foreign.test.cpp` compiles a word containing a foreign call once and
  runs that same instruction range through both executors --
  `interpreter::call_word` (the VM) and `sender::run_from_via_senders` -- asserting
  D14's own "identical final states" via `sender::testing::states_agree`, which is
  precisely the shape F33 built `sender/run_and_compare.hpp` for.

## Why

D24's final paragraph pre-authorizes exactly this: "if `sync_wait` is not
constexpr-capable, compile-time coverage comes from the VM and the sender backend is
the runtime path -- acceptable and documented." F33 established the fact; this step is
the first whose own written criterion collides with it, so this record exists to make
the reading explicit rather than leaving a criterion that reads as unmet.

Nothing weaker is being claimed. The point of "via VM *and* sender backends" is that a
foreign word is not a VM-only feature -- that the second executor dispatches it too,
and reaches the same state. That is exactly what the runtime equivalence check
verifies, on the same compiled instructions the `static_assert` half evaluates at
compile time.

## Consequences

- Any later step whose own merge criterion names a `static_assert` "through the sender
  backend" should be read the same way, without needing a further record: this one
  covers the pattern, not just this instance.
- `sender/lower_foreign.test.cpp` is a new shard in the `sender_test` binary,
  following DIV-0026's own per-translation-unit discipline (one capacity combination,
  two programs). Measured cost, taken the same way and in the same session as an
  existing shard for comparability: 37.7 s wall / 710 MB peak `cc1plus` RSS, against
  `lower_if.test.cpp`'s 34.6 s / 728 MB. The new shard is not an outlier.
- The second program in that shard puts the foreign call inside a `CATCH`-protected
  region, which also discharges the DIV-0028-category risk the F34 step-brief named
  (an enclosing sender-level `handler_depth()` corrupting a nested dispatch): both
  executors produce the same caught `-4`.

## Revisit condition

Closes if `sync_wait` (or a constexpr-capable equivalent driver) ever becomes usable in
a constant expression, at which point the criterion could be met literally. Nothing in
this project is waiting on that.
