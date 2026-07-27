# DIV-0025: `recover_basic_blocks` degenerated every block to one instruction

- **Status:** accepted-permanent (fixed in place)
- **Date:** 2026-07-26
- **Step:** F33 (sender backend), docs/forth-plan-2.md; defect originates in F30 (effect lint)
- **Authority diverged from:** docs/forth-plan-2.md / D24, via F30's own already-merged
  `interpreter::recover_basic_blocks` (`effect_lint.hpp`) — its own doc comment's contract ("One
  maximal straight-line run of instructions: no jump target lands inside it, and only its own last
  instruction can be a branch/loop/return")

## What diverged

`recover_basic_blocks`'s own leader-computation loop called `add_leader` on *both* edges of
`interpreter::instruction_successors` for *every* instruction in range, unconditionally — including
`edges.a` for an ordinary, non-branching instruction, which `instruction_successors`'s own "default"
case always sets to `index + 1` (pure bookkeeping for `check_definition_effect`'s own worklist
advance, not a real control-flow fact). That made *every* instruction's own physical successor a
leader, regardless of whether any branch was anywhere nearby — so every recovered block came out
exactly one instruction long, unconditionally, silently contradicting the function's own "maximal
straight-line run" doc comment. The correct `is_terminator` check immediately below it (adding
`i + 1` as a leader only after a genuine terminator) was already right, and already redundant with
the defect above for the cases it covered — it just never got a chance to matter, since the
unconditional edge-leader loop had already split everything first.

Confirmed directly: a hand-built four-instruction straight-line program (`push`, `push`, `prim +`,
`ret`, no branch anywhere) recovered as four separate one-instruction blocks before this fix, and as
the single block `[0, 4)` after it. The existing F30 unit test
(`EffectLintTest`'s own `recover_basic_blocks` case) does not distinguish the two behaviors: its one
example already has a leading `branch0`, whose own two real edges (`{index + 1, operand}`) force
three single-instruction blocks either way, so the defect produced the same answer as the fix would
have for that specific input.

## Why

**F30 had no real consumer of block *granularity*.** `check_definition_effect` never called
`recover_basic_blocks` at all — D20's own doc comment on `basic_block` is explicit that the type
existed only as "the reusable CFG product D24 asks this step to leave behind for F33's own sender
lowering." F30's own checker walks `instruction_successors` directly, per instruction, in a
worklist that assigns a data-stack level to every instruction individually; it does not care where
one block ends and the next begins, so a leader-computation bug that only affects block *extent*
(never which edges exist, and never any instruction's own computed level) was invisible to every
test that existed before this step. **This is the strongest form D20's own "one analysis, two
clients" claim can take**: the shared analysis had exactly one client for two steps running, and
only broke visibly the moment a second, genuinely different client (F33's own block-granularity
sender lowering, which needs one composed sender *per recovered block* to mean something) tried to
consume the same product for what it was actually built for.

## Consequences

- Fixed in place in `interpreter::recover_basic_blocks` (`effect_lint.hpp`): the edge-leader step
  now only runs for instructions whose own opcode is a genuine branch
  (`branch`/`branch0`/`loop_step`/`plus_loop_step`/`leave`/`catch_mark`); an ordinary instruction's
  physical adjacency to whatever follows needs no separate leader entry. The existing unit test is
  unaffected (same three-block answer, for the reason given above); a new case was added asserting
  a four-instruction straight-line program now recovers as one block.
- `interpreter::instruction_successors` also gained a second, real edge for `op::catch_mark`
  (its own resume-ip, previously unmodeled per F30's own step-brief note) as part of this same step
  — a distinct, additive change (not itself a defect fix), needed so `CATCH`'s own two real
  completions land as two separately-recoverable leaders. See `docs/compiler_architecture.org`'s own
  Phase 15 section for how F33's sender lowering consumes both.
- Every later step reading `recover_basic_blocks`'s own output for block *extent* (not just edge
  existence) can now trust its own doc comment. Nothing downstream of F30 before this step ever
  relied on block extent, so no other code needed to change.

## Revisit condition

None — this is a defect fix restoring the function's own already-documented contract, not a new
design decision. Closed.
