# step-brief.md — Step F34: The Foreign Function Interface

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory.

## What F33 built, by anchor

`docs/compiler_architecture.org`'s Phase 16 section ("The Sender Backend")
covers this in full, with transcluded code anchors. Read it before opening
`sender/lower.hpp` wholesale.

- **The sender backend is real and green**: `sender::word_sender<...>`
  (`sender/lower.hpp`) is a second executor of the same `machine::instr`
  stream `vm.hpp`'s own `run_from` runs, verified bit-for-bit against it
  (`sender::testing::states_agree`, `sender/run_and_compare.hpp`) for `IF`,
  both `BEGIN` forms, `DO`/`LOOP`/`+LOOP`/`LEAVE`, `EXIT`, `CATCH`/`THROW`,
  `EXECUTE`, and `CREATE`/`DOES>`. Public entry points: `sender::
  run_from_via_senders(program, state, entry, fuel, dict)` (mirrors `vm.hpp`'s
  own `run_from`) and `sender::run_via_senders(program, state, fuel)` (mirrors
  `run`, seeds the data space first).
- **`sync_wait` is not constexpr-capable** (`beman::execution26::sync_wait`'s
  own implementation uses `run_loop`, `std::exception_ptr`, and `throw`/
  `rethrow_exception` internally — none of which is usable in a constant
  expression). The sender backend is therefore a **runtime-only** executor;
  compile-time Forth-2012 coverage remains exclusively `vm.hpp`'s own
  `run_from`. If your own step reaches for Execution26 senders at all
  (plausible for an FFI boundary — composing a call out to foreign code and
  back is exactly the shape senders are for), expect the same split: build
  the composition with `constexpr`-marked combinators (`just`/`then`/
  `let_value`/`connect`/`start` all are), but the actual driving call
  (`sync_wait`, or your own equivalent) will not be, and nothing downstream
  of it can be constant-evaluated either.
- **If you write a custom Execution26 sender whose own work function
  recurses into itself through further sender composition, do not make that
  work function a member template over the connecting receiver's own
  concrete type** — the ordinary, textbook `connect`/`operation_state`
  shape. `word_sender`'s own first draft did exactly this and OOM-killed the
  build machine at 42 GB (a follow-up run under an 11 GB cap also failed).
  Cause: local lambdas inside a function template get a distinct closure
  type per instantiation of the *enclosing* template, so a receiver-
  templated recursive work function has no fixed point — instantiating it
  for one receiver mints new lambda types, composing them into a new
  receiver type, requiring another instantiation, unboundedly. Fix (already
  built and reusable in spirit, not literally importable since it is
  `lower.hpp`-private): type-erase the sender's own receiver
  (`abstract_receiver<Value, Error>` + a trivial `receiver_adapter<Value,
  Error, Receiver>` one-time wrapper) so the work function is templated only
  on the sender's own fixed dimensions, never on whatever connects to it.
  Full record, including the measured before/after numbers: DIV-0026.
- **Component tests that instantiate Execution26 sender machinery are
  expensive per distinct capacity combination even after that fix** —
  330–400 MB peak `cc1plus` RSS measured per `forth_state`/capacity
  combination, not per test case (multiple test cases sharing one
  combination in one TU are nearly free; a *new* combination is not). If F34
  composes senders at all, shard its own tests the same way this step's
  `sender/lower_*.test.cpp` files do (one or two programs per translation
  unit, one capacity combination per file) from the start, and measure each
  shard (`/usr/bin/time -v` against a standalone compile) rather than
  assuming cost. `docs/compiler_architecture.org`'s own Phase 16 table is
  the baseline to compare against, not just to imitate the shape of.
- **`run_word_via_vm`'s own handler-hiding pattern** (`sender/lower.hpp`) is
  the template for any future case where sender-composed code calls into a
  region whose own error handling was not itself written in terms of
  Execution26's error channel: save the ambient signal that would otherwise
  leak across the boundary, run the foreign region with it hidden, then
  translate whatever comes back into the shape the sender side expects
  before restoring it. If F34's FFI boundary can be called from inside a
  `CATCH`-protected region (a foreign function invoked via an xt that a
  Forth word passes to `CATCH`), the same category of bug this step found
  (DIV-0028 — an enclosing sender-level `handler_depth()` corrupting a
  nested VM-driven dispatch loop) is worth checking for explicitly, not
  assumed away by analogy.
- **`sender::word_body_end`** (`sender/lower.hpp`) is a real, useful, but
  imprecise pattern if you need a compiled word's own instruction range
  post-hoc (after compilation, not at `;`-time when `buf.here()` is free):
  `compiled_program` stores no per-word end, only `entry_points` (starts),
  so this scans for the smallest later-recorded entry point as a safe
  (possibly loose) upper bound. Reusable as-is if you need the same thing;
  do not assume it is tight.
- **`sender::run_and_compare.hpp`** (`compile_and_run_both`, `states_agree`)
  is a small, reusable "compile a word, run it through both the VM and the
  sender backend, compare final data-stack/output state" test helper.
  Reusable if F34 wants the same VM-vs-second-executor comparison shape for
  its own tests, with one caveat: it always starts both runs from a
  *fresh* `forth_state` with caller-supplied stack arguments pushed. A word
  depending on data-space *content* established at compile time (`CREATE`/
  `DOES>`-style — this step hit exactly this in `lower_defining.test.cpp`'s
  own `DoesEnter` test) needs both runs to start from a *copy* of the
  already-interpreted `forth_state` instead, not from the shared helper;
  see that test file for the direct pattern.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Watch `pgrep -af cc1plus` and its RSS before launching a new build if any
  step touches sender/Execution26 composition — see the compile-cost note
  above. Never stack a second build against a first that appears stuck;
  diagnose (memory, not just time) before retrying.
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one — but see the constexpr-
  capability note above before assuming a *sender*-composed API can.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F33 used DIV-0025 through DIV-0028 (DIV-0028 is next).

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for **F35** (bootstrap prelude, if reached next) or whatever the
plan names after F34; DIV filed for any deviation, using the number you are
given.
