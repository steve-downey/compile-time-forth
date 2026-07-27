# DIV-0026: `word_sender::run` must not be templated on its own connecting receiver

- **Status:** accepted-permanent
- **Date:** 2026-07-26
- **Step:** F33 (sender backend), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md / D24 (no specific mechanism was named for how
  `word_sender` should implement the Execution26 `sender`/`connect`/`operation_state` protocol; this
  step's own first draft chose the ordinary, textbook shape and it does not scale)

## What diverged

`sender::word_sender<...>` is a hand-written Execution26 sender (`vendor/execution/examples/
sender_demo.cpp`'s own pattern: a member `connect(Receiver&&) &&` returning an `operation_state`
whose `start()` drives the real work). The textbook version of that pattern makes the work function
— here, `word_sender::run` — a member template over the connecting receiver's own concrete type,
exactly like `operation_state` itself is. This step's first working draft did that, and it does not
compile to completion on this machine for any nontrivial word: a single `sender::run_from_via_
senders` call over the simplest possible corpus program (`ABS`, one `IF`, no `CATCH`, no nested
calls) drove `cc1plus` to 42 GB resident before the kernel OOM-killed it. A follow-up run under an
explicit `ulimit -v` cap of roughly 11 GB failed the same way after 2m26s of wall time, denied by a
single 6.7 GB allocation request partway through — confirming this is unbounded growth, not merely
"expensive," since a hard cap an order of magnitude below the original failure point was not enough
to let it finish either.

Fixed by type-erasing `word_sender`'s own receiver: `abstract_receiver<Value, Error>` (a three-
method interface, `value`/`error`/`stopped`) and `receiver_adapter<Value, Error, Receiver>` (a
trivial one-time wrapper from a concrete Execution26 receiver to that interface) replace the direct
receiver-templated `run`. `word_sender::run` is now `auto run(abstract_receiver<block_outcome,
error_type>&) -> void` — templated only on `word_sender`'s own class-level dimensions (`MaxCode`,
..., `DictName`), never on whatever connects to it. `op_state<Receiver>::start()` builds one
`receiver_adapter` and calls `run` through it.

Measured after the fix: the same `ABS` case now compiles in roughly 2–6 seconds using 330–360 MB
peak RSS (`/usr/bin/time -v`, measured three times, including once under a 4 GB `ulimit -v` cap for
safety); a seven-scenario comparison battery (`ABS`, `COUNTDOWN`, `TENS`, native `CATCH`/`THROW`,
the `>R`-fallback `CATCH`/`THROW`, an uncaught `THROW`, and fuel exhaustion) in one translation unit
compiles in roughly 5 seconds at 395 MB peak RSS — down from the original 42 GB OOM kill for even
the single-scenario case.

## Why

**Local lambdas inside a function template get a distinct closure type per instantiation of the
enclosing template, even when textually identical — and `run`'s own body needs to define some.**
`run`'s own `CATCH` handling composes a nested `word_sender` with real `then`/`upon_error`
combinators (D24's own "error-to-value adapter," not a hand-rolled if/else); the callables passed to
`then`/`upon_error` are ordinary lambdas written directly in `run`'s own source. If `run` is itself a
member template parameterized by the connecting receiver's own type, each instantiation of `run`
mints its *own* copies of those lambda types — not because the code differs, but because a closure
type's identity is tied to its enclosing scope's own template instantiation, per the language rule
(each instantiation of an enclosing template is a distinct point of definition for any lambda
textually inside it). Composing the nested `word_sender` with those lambdas produces a receiver type
that embeds them, and connecting the nested `word_sender` to *that* receiver type requires
instantiating `run` again — for a **new**, never-before-seen receiver type, which mints **its own**
new lambda types, requiring yet another instantiation, without bound. There is no fixed point:
nothing in this composition converges to a receiver type `run` has already been instantiated for,
because every instantiation's own lambdas are unique to it by construction. `call`/`execute`'s own
recursive step does not have this problem (it drives a nested `word_sender` through @ref drive,
which always connects to the single, fixed `drive_receiver<block_outcome, error_type>` type,
regardless of the enclosing receiver) — it is specifically `CATCH`'s own genuine sender-combinator
composition, the part of this design closest to the literal Execution26 idiom, that triggers it.

**Type erasure is the right fix, not a narrower one.** The alternative of avoiding local lambdas
inside `run` (hoisting them to free functions, or writing hand-rolled functor types with no
captures) would only reduce the *rate* of type proliferation, not eliminate the missing fixed point
— any construct that lets `run`'s own instantiation-specific state flow into a type that reconnects
to `run` reproduces the same shape. Type-erasing the one boundary that actually needs to vary
(`word_sender`'s own outward-facing receiver) removes the recursion at its root: `run` becomes a
single, closed function per class-level instantiation, and every internal composition (the `CATCH`
adapter chain, a nested call's own `drive`) connects through the cheap, non-recursive
`receiver_adapter` instead of ever feeding back into a new instantiation of `run` itself.

**A general lesson for Execution26-style recursive lowering, not a local quirk.** Any component that
(a) writes a custom sender whose own work function recurses into *itself* through further sender
composition, and (b) implements that sender via the ordinary receiver-templated `connect`/
`operation_state` pattern, will hit this same unbounded-instantiation wall the moment step (a)'s own
composition uses a local lambda (which most idiomatic Execution26 code does, routinely). F34 (the
FFI) is a plausible next place this could recur if it composes senders recursively across a foreign
call boundary; this record exists so that step does not have to rediscover the wall by OOM-killing a
machine first.

## Consequences

- `sender/lower.hpp` gains `abstract_receiver<Value, Error>` and `receiver_adapter<Value, Error,
  Receiver>` (both immediately above `word_sender`'s own forward declaration); `word_sender::run`'s
  own signature and its out-of-line definition both changed accordingly. `inline_sender` (the
  per-block micro-sender `word_sender::run` itself builds and drives once per recovered block) was
  *not* changed: it is only ever connected once, at a single fixed call site (`drive<block_outcome,
  error_type>`), so its own generic `auto rec2` receiver parameter never accumulates distinct types
  and never needed type erasure.
- Component tests for this step must be sharded by translation unit (one or two corpus programs per
  `.test.cpp`, mirroring `src/smd/forth/conformance`'s own `core_suite_*` discipline) regardless of
  this fix: even after type erasure, instantiating the sender machinery once per distinct
  `forth_state`/capacity combination still costs 330–400 MB measured, not zero. `docs/
  compiler_architecture.org`'s own Phase 16 section records per-shard wall-clock and peak-RSS
  numbers as the regression baseline this establishes — a memory baseline, not only a timing one,
  since peak RSS is the binding constraint this step found, not wall-clock.
- Any future step adding a new Execution26 custom sender whose own work function recurses through
  further composition should check this record before choosing the receiver-templated textbook
  shape by default.

## Revisit condition

None. Type erasure at this one boundary is a correct, general fix for the specific structural cause
(no fixed point under receiver-templated recursion), not a workaround for a narrower symptom that a
later step might outgrow.
