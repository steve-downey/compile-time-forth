# DIV-0028: the VM-fallback path must hide an enclosing sender-level `CATCH`

- **Status:** accepted-permanent
- **Date:** 2026-07-26
- **Step:** F33 (sender backend), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md / D24 (the VM-in-a-sender fallback is
  pre-authorized in general terms; this step's own first draft implementation of it — a bare call to
  `vm.hpp`'s own `run_from` — was wrong in a way only direct testing surfaced)

## What diverged

`sender::run_word_via_vm` (the D24-permitted VM-in-a-sender fallback for a word whose own body uses
`>R`/`R>`/`R@`, `sender::word_uses_return_stack_data`'s own trigger) originally called `vm.hpp`'s own
`run_from` directly against the shared `forth_state`, unmodified. Verified directly against the VM
using the exact `GUARD`/`DEEP` program already in `interp.test.cpp`'s own `CATCH` corpus (`: DEEP 42
>R 10 0 DO 5 THROW LOOP R> DROP ; : GUARD ['] DEEP CATCH ;` — `DEEP` uses `>R`/`R>`, so it falls
back; `GUARD`'s own `CATCH` around it does not, since `GUARD` itself touches neither), this produced
the wrong final value: the VM leaves `5` on the stack (the thrown code, correctly caught); the
sender backend, before this fix, first underflowed the return stack outright (a bare `run_from` call
at a word's own entry point has nothing to pop for that word's own trailing `ret`, exactly the
`interpreter::call_word` convention exists to supply and this fallback path was not yet using), and
after fixing that specific underflow, produced `-256` (this component's own defensive "a raw
diagnosis reached an active handler unexpectedly" sentinel, `word_sender::run`'s own `CATCH` case)
instead of `5`.

Two fixes to `run_word_via_vm`, both confirmed against the VM afterward:

1. **Push a manufactured return address before calling `run_from`.** Entering a word this way is not
   a `vm.hpp` `op::call`/`op::execute` (this lowering's whole point is that neither one pushes a
   return address any more, D24's own refunctionalization), so nothing supplies the address the
   fallback word's own trailing `ret` needs. `interpreter::compile_buffer::call_word`'s own
   convention — push the reserved halt pad, instruction `0`, always an `op::halt` by construction —
   is replayed verbatim.
2. **Hide `state.handler_depth()` for the duration of the call (save, set to `-1`, restore
   afterward).** `vm.hpp`'s own `perform_throw` treats `handler_depth()` as the base of a *real*,
   VM-materialized 3-cell frame sitting on `state.returns()` at that exact depth. This lowering's own
   native `CATCH` (`word_sender::run`'s own `op::catch_mark` case) never materializes that frame — it
   keeps the equivalent bookkeeping (saved data depth, saved return depth, the previous handler) as
   plain C++ locals in its own stack frame instead. If an enclosing sender-level `CATCH` had already
   set a real, non-negative `handler_depth()` (exactly the `GUARD`/`DEEP` shape: `GUARD`'s own native
   `CATCH` sets it before invoking `DEEP` as its own protected callee), a `THROW` inside the
   VM-fallback-executed `DEEP` would reach `perform_throw`, which would set `ip` to whatever
   `handler_depth()` currently names — a resume point meaningful only to the *sender*-level
   trampoline, not to `run_from`'s own dispatch loop — and let `run_from` keep fetching instructions
   from it, interpreting the caller's own unrelated code as more of `DEEP`'s own body. Hiding the
   outer value turns an escaping `THROW` into `run_from`'s own standard "uncaught" diagnosis instead
   (produced with `handler_depth() < 0` from the fallback call's own point of view, exactly as if it
   were the outermost execution); `word_sender::run`'s own caller recognizes that specific message
   text and re-numbers it into a proper `control_error{numbered = true, n = ...}`, so the *enclosing*
   sender-level `CATCH` — restored to visibility the moment the fallback call returns — still
   catches it correctly, indistinguishably from a natively-lowered `THROW` reaching the same
   handler.

## Why

**The two executors' own `CATCH` bookkeeping are incompatible representations of the same logical
state, and the fallback boundary is exactly where that stops being able to stay implicit.** D14
holds within each executor alone (the VM's own `catch_mark`/`perform_throw` agree with each other;
this lowering's own native `catch_mark` case and its `upon_error` adapter agree with each other) —
but `handler_depth()` is a single shared register on `forth_state`, and the two executors disagree
about what a non-negative value of it *means* on the return stack (a real, indexable 3-cell frame
for the VM; nothing at all, by design, for this lowering). Whenever execution crosses from one
executor's own protected region into the other's, that disagreement becomes live. This step's own
merge criteria already require "one program on each side of the fallback boundary" specifically
because the boundary is exactly where a design that is correct on each side separately can still be
wrong at the seam — this is that seam, found by running the boundary program, not by inspecting
either side in isolation.

**Why not make native `CATCH` also push a real 3-cell frame (matching the VM's own layout), instead
of hiding `handler_depth()` in the fallback?** Tried and rejected during this step, not merely
considered: even with a real frame in place, `perform_throw`'s own resume-ip write is not confined
to fixing up the frame — it also mutates the *caller's own local* instruction pointer (`vm.hpp`'s own
`run_from` dispatch loop variable) and lets that same `run_from` call keep running from the new `ip`.
A resume point meaningful to `word_sender::run`'s own trampoline is not a valid instruction index for
`run_from`'s *own*, separate dispatch loop to keep interpreting — pushing a matching frame does not
fix that mismatch, since the failure is about which dispatch loop consumes the jump, not about
whether the frame's own three cells are shaped correctly. Hiding the outer handler for the fallback
call's own duration sidesteps the mismatch entirely, by construction: `perform_throw` is never
invoked against a resume point it cannot correctly act on, because the only frames it can ever see
belong to a `CATCH` that is *itself* running inside the same `run_from` call (real, VM-native, and
therefore correctly actionable by `perform_throw`).

## Consequences

- `run_word_via_vm` pushes the halt-pad return address and saves/restores `handler_depth()` around
  every fallback call, unconditionally — cheap (two register operations and one stack push/pop) and
  correct whether or not an enclosing `CATCH` is actually active (`state.handler_depth()` is simply
  already `-1` in the no-`CATCH` case, so the save/restore is a no-op in substance).
- `word_sender::run`'s own fallback-dispatch branch recognizes `run_from`'s exact "uncaught THROW
  (code in foundation::parse_error::where.offset)" message text and re-numbers it into a
  `control_error{numbered = true, ...}`; any other diagnosis from the fallback call passes through
  as a raw (`numbered = false`) diagnosis unchanged, exactly as a native machine fault with no active
  handler would.
- The `GUARD`/`DEEP` program is this step's own named fallback-boundary merge-criterion program
  (`docs/compiler_architecture.org`'s own Phase 16 section); `TRY`/`BOOM` (`: BOOM 42 THROW ; : TRY
  ['] BOOM CATCH ;`, no `>R`/`R>`/`R@` anywhere) is the paired native-side program, both verified
  bit-for-bit against `machine::run_from`/`interpreter::call_word`.
- Any future step extending the fallback boundary (a new trigger condition, or a different
  VM-in-a-sender use) must re-derive this same handler-hiding discipline if the fallback's own
  protected region can be nested inside a sender-level `CATCH` — it is not specific to `>R`/`R>`/
  `R@`, only first found through that trigger.

## Revisit condition

None. The two executors' own `CATCH` representations are deliberately different (DIV-0018's own
scalar-register design for the VM predates this step; this lowering's own C++-local design is
DIV-0026's sibling decision), so isolating them at every crossing is the correct permanent shape, not
a placeholder for eventually unifying the two representations.
