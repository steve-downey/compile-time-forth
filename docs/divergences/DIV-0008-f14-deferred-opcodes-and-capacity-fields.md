# DIV-0008: F14 reserves nine opcodes without behavior and leaves "required stack capacity" uncomputed

- **Status:** accepted-permanent
- **Date:** 2026-07-20
- **Step:** F14 (stack-machine codegen and VM)
- **Authority diverged from:** docs/forth-plan.md

## What diverged

`docs/forth-plan.md`'s Step F14 section gives `op`'s full seventeen-enumerator
shape verbatim (`push, prim, call, ret, branch, branch0, do_setup, loop_step,
plus_loop_step, push_index, leave, unloop, push_xt, execute, catch_mark,
throw_op, halt`) and says `compiled_program` "also carries the dictionary's
word table (entry point per word), data-space size, and required stack
capacities," without stating which opcodes must have real codegen/VM
behavior at this step or how "required stack capacities" is computed.
This step:

- Implements real codegen and VM semantics for eight opcodes only: `push`,
  `push_xt`, `prim`, `call`, `ret`, `branch`, `branch0`, `halt`. The
  remaining nine (`do_setup`, `loop_step`, `plus_loop_step`, `push_index`,
  `leave`, `unloop` -- all `DO...LOOP` machinery, step F17's own deliverable;
  `execute`, `catch_mark`, `throw_op` -- all step F18a's) exist in the `op`
  enum (`instruction.hpp`) but are never emitted by `codegen()` and are
  diagnosed with a `foundation::parse_error` if the VM (`vm.hpp`) ever
  encounters one. `codegen` diagnoses `core_do_loop` itself as "not
  implemented until F17" rather than emitting any of the six
  counted-loop-shaped opcodes with placeholder semantics -- mirroring F13's
  own identical choice for `eval_direct.hpp` (see `eval_direct.hpp`'s
  `core_do_loop` case), per handoff-next.md's F14 briefing, which named this
  as the more conservative of two live options and this worker's suggested
  default.
- Adds `compiled_program::required_stack_depth` and
  `::required_return_depth` fields (defaulting to `-1`, "not computed")
  rather than computing a real whole-program peak-depth bound. F12's own
  stack-effect analysis (`elaborator/stack_effect.hpp`) computes each colon
  definition's *net* data-stack effect and *minimum entry depth*, not a
  running peak across a definition's own body (e.g. `DUP DUP DROP DROP` has
  net effect zero but a peak two cells deeper than its entry) -- there is no
  existing analysis this step could read a true bound from without writing
  a new one, and the plan does not otherwise specify the algorithm.
  `compiled_program::data_space_size` (real, read from
  `compiled_unit::data_space::size()`) and `::entry_points` (real, the
  dictionary word table) are computed as the plan describes; only the two
  stack-capacity fields are placeholders.

## Why

Both cuts keep F14's own scope to what its own merge criteria (the F13 test
set: `SQUARED`, `ABS`, `COUNTDOWN`, `SPIN`'s budget exhaustion, running
through codegen+VM at compile time and runtime on the same `constexpr`
program object) demonstrably require, rather than speculatively building
machinery for later steps that no test yet exercises:

- None of F13's own merge criteria exercise `DO...LOOP`, `EXECUTE`,
  `CATCH`/`THROW`, or anything reachable only through them; implementing
  placeholder VM semantics for opcodes nothing calls risks encoding a
  guessed-at design (loop-parameter frame shape, handler-frame shape) that
  F17/F18a would likely have to revise anyway once they do the real design
  work the plan assigns them.
- A correct whole-program peak-stack-depth analysis is a nontrivial
  addition (it needs to track *maximum* depth reached, not just net change,
  through every control-flow path, including loops) that the plan does not
  ask for in F12 and does not describe the shape of in F14; inventing one
  under this step's own time budget risked either an incorrect bound (worse
  than no bound: a false sense of safety) or scope creep well past "codegen
  and VM," so this step records the gap explicitly instead.

## Consequences

- Every `compiled_program` caller (F15's public one-shot API, F16's memory
  wiring, F17, F18a) must still size its own `forth_state` generously by
  hand, exactly as F13's own tests already do (`StackDepth = 64` and
  similar headroom figures chosen by inspection, not by a computed bound) --
  `required_stack_depth`/`required_return_depth` being `-1` is not itself a
  runtime hazard (VM stack operations stay bounds-checked via
  `cell_stack::push`/`pop`, D7), it only means the field cannot yet answer
  "how big does my `forth_state` need to be."
- F17, when it gives `do_setup`/`loop_step`/`plus_loop_step`/`push_index`/
  `leave`/`unloop` real codegen and VM semantics, and F18a, when it does the
  same for `execute`/`catch_mark`/`throw_op`, do not need to touch `op`'s
  own enumerator list (already reserved by this step) -- only `codegen.hpp`'s
  `core_do_loop` case (replacing the diagnosed error) and new VM cases in
  `vm.hpp`'s `switch` (replacing the diagnosed "not implemented" arms).
- Whichever future step first needs a real stack-capacity bound (most
  likely whichever of F15/F16/F17 first wants to auto-size a `forth_state`
  from a `compiled_program` rather than a hand-chosen constant) should
  either extend F12's analysis to track peak depth alongside net effect, or
  add a dedicated codegen-time walk computing it directly from the flattened
  instruction array; either replaces this divergence's `-1` placeholders
  with a real, checked number.

## Revisit condition

Closed for the opcode-deferral half once F17 (counted loops) and F18a
(execution tokens and exceptions) land and give every reserved opcode real
codegen and VM behavior.
Closed for the capacity-field half once some step computes a real
whole-program peak stack/return-stack depth bound and populates
`required_stack_depth`/`required_return_depth` with it instead of `-1`.
