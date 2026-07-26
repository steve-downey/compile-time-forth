# step-brief.md — Step F30: The Effect Lint

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory.

## What F31 built, by anchor

`docs/compiler_architecture.org`'s Phase 13 section ("CATCH and THROW")
covers this in full, with transcluded code anchors. Read it before opening
any source below wholesale.

- **The opcode enum is now fully populated.** Every enumerator
  `machine::op` reserved since F14/F17/F28 has real semantics in
  `machine::run_from` (`vm.hpp`) as of this step — `catch_mark`/`throw_op`
  are the last two. There is nothing left in `op` for any future step to
  give first semantics to.
- **`CATCH`/`THROW`/`ABORT` are new, non-immediate `machine::
  control_builtin` tags** (`catch_`, `throw_`, `abort_`,
  `machine/dictionary.hpp`), dispatched in `interpreter::compile_entry`
  (compiled form) and `interpreter::apply_control_word` (interpreting form,
  `interp.hpp`) — the same shape every control word since F27 has used.
  **`effect_lint.hpp`'s own lattice has no case for any control word at
  all**, only `primitive_data_effect`/`primitive_return_delta` over
  `machine::primitive`. If your own step's retargeting walks
  `compile_buffer`'s own `instr` ranges opcode-by-opcode (the plan's own
  words, from this file's own top comment: "retargeted to instr ranges over
  the interpreter's own compile_buffer instead of the deleted core tree"),
  you will need to give `op::catch_mark`/`op::throw_op` (and every other
  control-flow opcode: `branch`/`branch0`/`do_setup`/`loop_step`/etc.) their
  own treatment directly — the primitive table alone will not cover a
  compiled program's own control-flow opcodes.
- **`CATCH` is almost certainly `unknown_effect` in the lattice's own
  terms** (`interpreter/effect_lint.hpp`'s own doc comment: "produced by
  input-dependent primitives, or anything reached only through `EXECUTE`"):
  `xt CATCH` runs an arbitrary execution token whose own stack effect is not
  statically known at the `CATCH` call site, exactly the same shape
  `EXECUTE` already has. Whatever your own step decides for `EXECUTE`'s own
  lattice contribution, `CATCH` should almost certainly get the same
  answer, for the same reason.
- **`THROW`/`ABORT` never return to their own call site.** Their compiled
  form ends in `op::throw_op`, which either unwinds to an enclosing `CATCH`
  (control resumes *there*, not after the `THROW`) or diagnoses an uncaught
  throw. This is the same "may not return" fact D20 already flagged on
  `abort_quote`'s own primitive-effect entry (`interpreter/effect_lint.hpp`,
  step F29) — now sharper, since `THROW`/`ABORT` are real control words a
  compiled body can contain anywhere, not only inside one primitive's own
  fixed 3-cell contract. Whatever "may not fall through" treatment your own
  step gives `EXIT` (a bare `op::ret`, also never falling through) is the
  natural model for `THROW`/`ABORT` too.
- **`machine::primitive` gained one enumerator this step: `catch_ok`**
  (`CATCH`'s own normal-completion epilogue, `( -- 0 )`). Its
  `primitive_data_effect` case (`known(0, 1)`) is already added
  (`interpreter/effect_lint.hpp`) — nothing further needed there.
- **`default_dictionary` is 92 entries now** (54 primitives + 38 control
  words), not 89. Audit again if this step adds anything.

## Gotchas F30 needs and cannot get from its own pasted section

- **`machine::forth_state::handler_depth()`/`set_handler_depth`** (a new
  scalar register alongside `BASE`/`STATE`) is *runtime* state — how far a
  `CATCH` handler's own frame sits on the return stack, at the moment code
  actually runs. Effect linting is a *static* analysis over already-compiled
  `instr` ranges (D20's own "advisory lint over emitted code at `;`"); this
  register has no bearing on it and should not be consulted by anything
  your own step writes.
- **`cell_stack::truncate(int)`** is new (`machine/stacks.hpp`) — a runtime
  operation `THROW`'s own unwind uses. Also irrelevant to a static lint.
- **`CATCH`'s own compiled shape is exactly two instructions**
  (`op::catch_mark` then `op::prim primitive::catch_ok`), with
  `catch_mark`'s own operand a compile-time-known resume instruction index
  — no back-patching, unlike `IF`/`WHILE`. If your own step's instr-range
  walk needs to skip or specially treat a `CATCH` site as one unit, this is
  the exact shape to recognize.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F31 used DIV-0018 (DIV-0019 is next).

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for **F32** (conformance); DIV filed for any deviation, using the
number you are given.
