# DIV-0013: The colon compiler's binding shape and word-invocation mechanism

- **Status:** open — both close at F26/F28 (see Revisit condition)
- **Date:** 2026-07-25
- **Step:** F25 (the colon compiler and the session image), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F25's own step text left both
  choices explicit but open)

## What diverged

F25's own step text and its step-brief both flag two design questions the
plan leaves to this step rather than specifying: what a compiled colon
definition's own dictionary binding looks like, and how interpreting a
defined word actually invokes the VM against a *live*, already-running
`forth_state` rather than a fresh one.

**Binding shape.** `machine::dictionary`'s existing `colon_word` binding
(`dictionary.hpp`) has one field, `core_id` — an index into
`elaborator::compiled_unit`'s own arena, a pipeline the colon compiler does
not participate in and that step F26 deletes outright. Rather than
repurposing `colon_word` (reinterpreting `core_id` as something else) or
editing it in place, this step adds a **new**, sixth alternative to the
`dictionary_binding` variant: `compiled_colon_word { int entry_point;
foundation::source_span effect_span; bool has_effect; }`. `colon_word`
itself, and every one of its existing consumers (`elaborate.hpp`,
`eval_direct.hpp`, `codegen.hpp`, `stack_effect.hpp`), is untouched.

**Word invocation.** D14 says "interpreting a defined word runs its code on
the VM against the live forth_state — one semantics." The VM's own existing
entry point, `machine::run` (`vm.hpp`), always seeds the running state's data
space from `compiled_program::data_space_size` before executing — a
batch-codegen concern (F16) that makes sense once, for a whole program, but
would be wrong to repeat on every single word call from the interpreter
(each call would `allot` `data_space_size` more cells, without bound).

An earlier draft of this step avoided touching `vm.hpp` at all:
`interpreter::compile_buffer::call_word` pushed a permanent internal "halt
landing pad" instruction's own index as a return address, pointed the
buffer's own (otherwise-unused, always-zero-`data_space_size`)
`compiled_program::program_entry` at the word being called, and called
`machine::run` completely unmodified — relying on `compile_buffer` never
setting `data_space_size` above its default (0) to make `run`'s own seed
step (`allot(0)`) a documented no-op. That draft was rejected on review
before this record was ever committed: it is a correctness bug waiting to
happen, not merely inelegant. It is correct only by an *incidental*
invariant (this step's own code space happens never to raise
`data_space_size`), not by construction — a future step that lets
`compile_buffer` track a real data-space high-water mark (F26's cut, or any
step after it) would silently reintroduce unbounded reseeding on every call,
with nothing at this call site to catch it. It also does not generalize:
F27's immediate words execute *during* compilation, and F31's `THROW` unwinds
out of a partially-executed program — both need to invoke the VM against a
program that is not the one live top-level run `machine::run`'s own contract
assumes, and mutating a program's own fields to fake an entry point does not
extend to either.

This step therefore adds `machine::run_from` (`vm.hpp`) instead: an entry
point that executes from a caller-supplied instruction index against an
already-live `forth_state`, *without* `run`'s own data-space seeding step.
`machine::run` is reimplemented in terms of it — seed, then delegate to
`run_from(program, state, program.program_entry, fuel)` — so `run`'s own
signature and every observable behavior for an ordinary top-level call are
unchanged; the R1 pipeline and every one of its own tests are unaffected.
`interpreter::compile_buffer::call_word` now pushes the halt-pad return
address and calls `run_from` directly, at the word's own entry point,
without ever touching `program_entry`.

`vm.hpp`'s existing `docs/compiler_architecture.org` Phase 5 prose and its
transcluded anchor (`3b356d6c-c4c1-4676-b16a-48e975b5d46b`) previously
covered `run`'s *whole* function body (seed step plus fetch-execute loop).
This step split that anchor's content: the fetch-execute loop itself moved,
byte-for-byte, under a new anchor (`e2a7c9f4-5d1b-4e8a-9c3f-7b2d6a4e1f8c`) on
`run_from`, and `3b356d6c` now covers only `run`'s own short seed-then-
delegate body. Phase 5's prose was updated in place (not left to
desynchronize) to transclude both anchors and explain the split; see
`docs/compiler_architecture.org`'s Phase 5 and Phase 8 sections.

## Why

**Binding shape.** A new binding kind is strictly additive and keeps the two
pipelines (R1's elaborator/codegen/eval_direct, and F25's own colon
compiler) fully decoupled until F26 actually deletes the first one — exactly
what "nothing is deleted this step" requires. Reusing `colon_word` would
have meant either changing what its one existing field means (breaking
every R1 consumer's own assumption that `core_id` is an arena index) or
adding a second, differently-interpreted field to a struct four other files
already depend on for a meaning that would never apply to their own
instances of it.

**Word invocation.** `machine::run`'s data-space seeding step exists for a
batch-compiled top-level program, executed once. F25's interpreter calls
`call_word` potentially many times in a single session (any top-level
reference to a previously defined word), so reusing `run`'s seeding
unconditionally would silently grow the live state's data space on every
call — a correctness bug, not just a performance one. Making that safe by
construction — rather than by an incidental fact about what this step's own
code space happens never to do — needs a real entry point that does not
seed at all, so `run_from` was added and `call_word` calls it directly.
Reimplementing `run` in terms of `run_from` (seed, then delegate) keeps
`run`'s own signature and behavior exactly as they were for every existing
caller, satisfying the constraint that D16's retained VM loop and `run`'s
own existing entry-point behavior must not change. Editing `vm.hpp` for this
is explicitly in scope for this step: D16 says the VM loop is *retained*,
not frozen, and already anticipates further opcodes landing in `vm.hpp` at
F28 and F31 — this step's own addition is a new function alongside the
existing ones, not a change to any existing opcode's semantics or to `run`'s
own contract.

## Consequences

- `machine::dictionary_binding` now has six alternatives, not five. The one
  place that visits it exhaustively via `std::visit`
  (`elaborate.hpp::elaborate_word_ref`) already has a catch-all `else` arm
  (originally written for `foreign_word`); a `compiled_colon_word` falls
  into that same arm if it is ever encountered there, which cannot happen
  in practice today (nothing feeds an F25-built dictionary into the R1
  pipeline's own `elaborate`).
- `interpreter::compile_buffer::halt_pad()` (always instruction index 0) is
  now a load-bearing invariant: any future step that appends directly to a
  `compile_buffer`'s own underlying `compiled_program` without going through
  `compile_buffer::emit` must not overwrite instruction 0.
- F28 (execution tokens and defining words, D18) inherits the question of
  whether `compiled_colon_word` survives as its own binding kind or folds
  into D18's unified header/XT shape — this step deliberately did not
  attempt to anticipate that shape beyond "do not paint F28 into a corner,"
  per its own step-brief.
- F26 ("the cut") is where `colon_word`, `elaborator::compiled_unit`, and
  every R1-pipeline consumer of the old binding actually get deleted; until
  then, `machine::dictionary_binding` legitimately carries both the old and
  the new colon-definition binding shapes side by side.
- `vm.hpp` now exports `run_from` alongside `run`; both are public API of
  the `machine` namespace. Any later step invoking the VM against a program
  that is not a fresh top-level run (F27's immediate-word execution during
  compilation, F31's `THROW` unwind) should reach for `run_from`, not
  reinvent a second way to start execution mid-program.

## Revisit condition

The binding-shape half closes at F26, when `colon_word` and its own R1
consumers are deleted and `compiled_colon_word` is the only colon-definition
binding left standing (or is itself renamed/folded, at F26's own
discretion).
The invocation half closes at F28 if D18's unified execution-token/header
work finds a reason `call_word`'s own "push a halt-pad return address, call
`machine::run_from` at the callee's entry point" mechanism does not
generalize — that reason supersedes this record and must be filed against
D18 rather than silently reworked.
