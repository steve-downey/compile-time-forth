# DIV-0018: CATCH/THROW frame design, machine-fault mapping scope, and ABORT" closure

- **Status:** accepted-permanent
- **Date:** 2026-07-26
- **Step:** F31 (CATCH and THROW), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F31's own step text leaves the return-stack
  teardown mechanism -- "tagged-frame debug mode or exhaustive battery -- worker decides" -- and
  the machine-fault-to-THROW-code mapping scope open; DIV-0017's own revisit condition, for
  `ABORT"`)

## What diverged

F31's own merge criteria name the choice explicitly: a handler-frame design that survives an
"interaction battery mixing `>R`, `DO` frames, `LEAVE`, and `CATCH` on one return stack," with
either tagged frames (a runtime discriminant on every return-stack cell, scanned to find a
handler) or an exhaustive battery (no structural help, just tests). This step picks neither
literally: it adds a third return-stack-adjacent register, `machine::forth_state::handler_depth`,
that names the return-stack depth a `CATCH` handler's own frame begins at -- so `THROW` finds its
target by reading a scalar and truncating to a known depth, never by scanning or tagging anything
already on the stack. Consequences below list the concrete additions.

**1. `THROW` never scans the return stack; it truncates to a depth it already knows.**
`machine::forth_state` gains `handler_depth()`/`set_handler_depth(int)`: `-1` means no active
`CATCH`; otherwise it is the return-stack depth immediately below the innermost active handler's
own 3-cell frame (`op::catch_mark` sets it; `perform_throw`/`primitive::catch_ok` restore it from
the frame's own first cell). `THROW` (`op::throw_op`) reads this register, calls the new
`cell_stack::truncate(int)` once to discard everything above `handler_depth() + 3` -- whatever mix
of call frames, `DO`-loop frames, and `>R` values the caught execution built, all at once -- then
pops the frame's own 3 cells (resume ip, saved data-stack depth, previous handler depth), truncates
the *data* stack to the saved depth, pushes the thrown code, and jumps to the resume ip. This is
correct regardless of F17's own still-unverified assumption that a `DO`-loop's own teardown always
finds its own frame at the very top of the return stack (Part 11's own recorded gap): `THROW`'s own
unwind makes no assumption at all about what is between its own target depth and the current top --
it does not inspect it, only discards it.

**2. `CATCH`'s own compiled shape is two instructions, not a call/return pair the caller has to
interpret specially.** `xt CATCH` compiles to `op::catch_mark <RESUME>` followed by
`op::prim primitive::catch_ok`, where `RESUME` (the instruction index execution should be at once
`CATCH` is fully done, either way) is known immediately at compile time -- both instructions have
fixed length, so no back-patching is needed, unlike `IF`/`WHILE`'s own orig/dest discipline.
`catch_mark` pops the execution token, pushes the handler frame, pushes its own return address
(the `catch_ok` instruction's own index -- the same "push return address, jump" convention
`op::call`/`op::execute` already use), and jumps to the token. If the token's own execution
completes normally, its own final `ret` lands on `catch_ok`, which pops the frame, restores
`handler_depth()`, and pushes `0`. If a `THROW` inside it targets this exact frame instead,
`perform_throw` does the equivalent restoration itself and jumps straight to `RESUME`, skipping
`catch_ok` entirely (already having pushed the thrown code instead of `0`). Interpreting-time
`CATCH`/`THROW`/`ABORT` (met directly by the text interpreter, not compiled into any colon
definition) reuse the identical shape, appended to the live code space and run once via
`machine::run_from`, guarded by a leading unconditional branch exactly like
`interp.hpp`'s own `resolve_execution_token` already does for `'` -- the same "one semantics"
discipline (D14) `EXECUTE` already established. Interpreting `CATCH`'s own resume ip is
`compile_buffer::halt_pad` (instruction 0, always `op::halt`) rather than "one past this call's
own appended code" (nothing genuinely follows it there): both `catch_ok`'s own normal-completion
fallthrough and a caught `THROW`'s own jump need a trailing `branch halt_pad` (added explicitly)
to land on it cleanly.

**3. `catch_ok` is a new `machine::primitive`, not a third VM opcode.** `CATCH`'s own
normal-completion epilogue (pop the frame, restore the handler register, push `0`) needs no `ip`
manipulation of its own -- an ordinary primitive, dispatched through the existing `op::prim`, is
sufficient and cheaper than adding a third reserved opcode alongside `catch_mark`/`throw_op`.

**4. Machine faults are mapped to standard `THROW` codes only when a handler is already active,
by matching each diagnosis's own static message text; most are left unmapped.** `run_from`'s own
`op::prim` case, on any primitive failure, checks `state.handler_depth() >= 0` before attempting
`machine_fault_throw_code` (message text to code): `"stack overflow"` -> `-3`,
`"stack underflow"` -> `-4`, `"division by zero"` -> `-10`,
`"data space address out of bounds"` -> `-9`. With no handler active, every diagnosis anywhere in
this project keeps its original, more specific message verbatim -- unchanged from every test that
predates `CATCH`/`THROW`. Left unmapped, deliberately: `"data space exhausted"` and
`"output buffer overflow"` (no standard Forth-2012 code fits either cleanly, and neither is the
kind of ordinary runtime condition a Forth program is expected to guard against with `CATCH`), the
return-stack-specific overflow/underflow case (the same message text `cell_stack` uses for *both*
stacks, so this project cannot tell a data-stack fault from a return-stack one by message alone
without a larger plumbing change; mapping both to the data-stack codes -3/-4 was judged an
acceptable, documented imprecision rather than a reason to grow `parse_error` with a stack-identity
tag), and `"vm execution budget exhausted"` (D22's fuel: deliberately never catchable, since it
signals a runaway computation the machine itself gave up bounding, not a condition a user's own
`CATCH` should be able to swallow and keep going past).

**5. `ABORT"`'s own runtime primitive always routes through `THROW -2`, unconditionally.**
Unlike the general machine-fault mapping (item 4, gated on an active handler),
`machine::primitive::abort_quote`'s own "condition met" failure is recognized by
`is_abort_quote_condition` and always calls `perform_throw(state, -2, ip)`, whether or not a
handler is active -- `perform_throw` itself produces the correct uncaught diagnosis when
`handler_depth() < 0`. `ABORT"` is *defined* to be `THROW -2` (Forth-2012); it is not merely a
fault this project chooses to make catchable. `ABORT` itself compiles to `op::push -1` followed by
`op::throw_op` -- literally `-1 THROW`, per Forth-2012, with no dedicated opcode of its own.

**6. An uncaught `THROW`'s own diagnosed code rides in `foundation::parse_error::where.offset`,
not its message.** `parse_error::message` must stay a static-lifetime string literal (it does not
own or copy what it points to) -- the exact wall DIV-0017's own F29 finding named for `ABORT"`'s
message text, and the same wall a dynamic `THROW` code hits. `where.offset` (otherwise meaningless
for a condition with no real source position) carries it instead: a deliberate, documented reuse,
not a new field.

## Why

**A scalar handler-depth register over tagged return-stack frames.** Tagging every return-stack
cell (a call-frame return address, a `DO`-loop's own `limit`/`index` pair, a plain `>R` value, and
now a `CATCH` frame) would mean widening every one of those four kinds of pushes to carry a
discriminant, at every existing push site (`op::call`, `op::do_setup`, `primitive::to_r`) -- a much
larger, more invasive change than this step's own scope, and one that would need its own DIV for
changing three other frame kinds' shape at once (this step's own instructions call that out
explicitly: "if you change return-stack frame layout, that needs a DIV -- it is shared three ways
already"). A single scalar naming the innermost handler's own depth, restored via the frame's own
first cell on both paths, gives `THROW` a target it can jump to directly, with zero scanning and
zero new tag bits on any of the three existing frame kinds -- D24's own "a design that makes the
handler's extent explicit and findable" read as literally as possible.

**Message-text matching for machine-fault-to-code mapping, not a new discriminated fault-code
field.** Every existing diagnosis in this project is a bare `foundation::parse_error{pos,
"static message"}`; adding a fault-code enum to that struct (to make mapping exact rather than
string-matched) would touch every one of the roughly forty call sites that construct one, for a
step whose own merge criteria only need a handful of faults actually caught. Matching by message
text is a few lines, confined entirely to `vm.hpp`, and leaves every existing diagnosis untouched.

**`ABORT"` unconditional, general faults conditional.** `ABORT"` is a *defined* Forth-2012 word
whose entire meaning, per the standard, is `THROW -2`; there is no "uncaught-but-still-generic"
reading of it the way an ordinary stack underflow has one (a stack underflow's own pre-CATCH
message text, "stack underflow," already *is* the right uncaught diagnosis -- there was no reason
to invent a THROW-shaped one for it too).

## Consequences

- `machine::forth_state` gains a `handler_depth`/`set_handler_depth` pair and a private
  `handler_depth_` field (default `-1`); `machine::cell_stack` gains `truncate(int)`.
- `machine::primitive` gains `catch_ok`; `machine::control_builtin` gains `catch_`, `throw_`,
  `abort_` (none immediate: all three work identically interpreting or compiled, exactly like
  `EXECUTE`). `default_dictionary`'s own entry count grows from 89 to 92 (54 primitives + 38
  control words); `interpreter::effect_lint.hpp`'s own `primitive_data_effect` switch gains a case
  for `catch_ok` (`known(0, 1)`).
- `machine::op::catch_mark`/`op::throw_op` (reserved since F14/F28) gain real semantics in
  `machine::run_from`; no new `op` enumerator was added (see item 3 above for why `catch_ok` is a
  primitive, not an opcode).
- `machine::primitive::abort_quote`'s own failure message changes from the F29-era
  `"ABORT\" condition met (F31's THROW/CATCH will replace this hard stop)"` to the plain
  `"ABORT\" condition met"`, now unconditionally recognized by `vm.hpp`'s own
  `is_abort_quote_condition` and always routed through `THROW -2`.
- `interp.hpp` gains three new cases in both `compile_entry` (the compiled forms) and
  `apply_control_word` (the interpreting-time forms) for `catch_`/`throw_`/`abort_`; the latter
  three reuse `resolve_execution_token`'s own guarded-append discipline.
- DIV-0017's own revisit condition (`ABORT"` should `THROW -2` "when F31 lands") is resolved by
  this step; DIV-0017's own status line is updated in place to record it, rather than duplicating
  the record here.

## Orchestrator amendment: the F17 teardown gap is narrowed, and its residual has an owner

F31 was the step the plan named for paying Part 11's recorded unverified-teardown gap ("the gap
does not survive F31 unexamined"). It has been examined, and this step's design genuinely narrows
it: `THROW` no longer depends on the assumption at all, because it discards everything above a
recorded depth without inspecting any of it. That is a better outcome than verifying the
assumption would have been, and the interaction battery
(`CatchUnwindsThroughToRDoLoopAndCallFrames`, throwing from inside a `DO` loop nested inside a
`>R` and asserting the return stack returns to depth 0) is the stress test the merge criterion
asked for.

What remains is narrower and must not be dropped: **`LOOP`/`+LOOP`/`UNLOOP` teardown still assumes
its own two-cell frame sits at the top of the return stack.** Nothing in F31 changed that path.
A definition that pushes an unbalanced `>R` across a loop boundary — `: BAD 10 0 DO 5 >R LOOP ;` —
would tear down the wrong cells silently rather than diagnosing, which is in tension with D7 ("all
misuse is a diagnosed error, never UB"). Forth-2012 requires the return stack to be balanced before
`LOOP`, so this is a program error rather than a system bug; the objection is that the system does
not *say so*.

**Owner: F30.** D20's diagnosis list for the effect lint already names `>R`/`R>` imbalance and
loop-body net-effect violations, and F30 runs next against the now-final opcode set. A static lint
that rejects an unbalanced `>R` across a loop boundary is exactly the shape this residual needs,
and closes it at compile time rather than by adding a runtime check to the hot loop path. If F30
finds it cannot diagnose this case, it must say so explicitly rather than let the residual lapse.

## Revisit condition

None. This is accepted-permanent: the scalar handler-depth register is a strictly better fit for
this project's own C++-native VM (direct access to both stacks and a private register, no Forth-
level `HANDLER` variable or `SP@`/`RP@` primitives needed, unlike a classical Forth's own textbook
`CATCH`/`THROW` implementation) than either suggested alternative, and the machine-fault mapping
scope (item 4) is deliberately narrow rather than a placeholder for later widening -- a future step
that wants more faults caught should extend `machine_fault_throw_code`'s own table, not revisit the
mechanism.
