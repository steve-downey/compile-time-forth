# DIV-0012: F24's interpreter state is composed, not an in-place edit of `machine::forth_state`; the D19 token-layer move is deferred to F26

- **Status:** accepted-permanent (composition), open (deferred move, closes at F26)
- **Date:** 2026-07-25
- **Step:** F24 (the interpreter, interpret state only), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F24's own step text and D19)

## What diverged

`docs/forth-plan-2.md`'s F24 step text says "`forth_state` grows an input
source ... `BASE` ... `STATE`", which reads as an in-place edit of the
existing `machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>` class
(`src/smd/forth/machine/forth_state.hpp`).
That class is untouched.
Instead, `src/smd/forth/interpreter/interp.hpp` defines a new type,
`smd::forth::interpreter::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut,
MaxName>`, that *composes* a `machine::forth_state` as a private member
(`machine()` accessor) alongside a new `input_source` member, `BASE`, and
`STATE`.

Separately, D19 (pasted with this step's own brief) says the token layer
(`src/smd/forth/reader/forth_chars.hpp`) "either moves under `interpreter/`
or stays in `parser/`" — offering this step the choice of a *destination*,
not the choice of *whether* to move it this step.
`forth_chars.hpp` was left exactly where it was: still at
`src/smd/forth/reader/forth_chars.hpp`, in the `smd::forth::reader`
namespace, consumed by the new `interpreter/` component through its
existing canonical include path.

## Why

**Composition, not an in-place edit.** `machine::forth_state` is consumed
today by `machine::run`, `machine::eval_direct.hpp`'s `eval_program`, and
`forth.hpp`'s `forth_program` — all part of the R1 pipeline that
`docs/forth-plan-2.md` §8's own durable-invariants block and DIV-0011 both
say stays buildable and tested until step F26's cut.
Editing `machine::forth_state` in place to add an `input_source` member
would make `machine/` (a layer below `reader/`, `parser/`, and the new
`interpreter/` in every existing include direction) depend on a type this
step defines above it, which is a layering violation the codebase does not
have anywhere else; the alternative — defining `input_source` inside
`machine/` itself, disconnected from the `interpreter/` namespace the step
brief names for the two new files — does not read as "growing" so much as
"duplicating," and still touches a type four other files depend on for no
behavioral gain.
Composing the wider `interpreter::forth_state` around the narrower
`machine::forth_state` gets the same net effect the plan text describes
(an interpreter-level Forth machine state with `SOURCE`/`>IN`/`BASE`/
`STATE` on top of the existing stacks/data-space/output substrate) with a
strictly smaller, strictly additive diff: nothing in `machine/`,
`elaborator/`, or `reader/` changes at all, matching this step's own
"nothing is deleted, nothing else may break" instruction.

**The token-layer move is out of scope for F24, not merely deferred by
accident.** `forth_chars.hpp` has no dependency on anything else under
`reader/` (only `foundation/` and `parser/`), so relocating it is
mechanical whenever it happens — but doing it now means renaming its
namespace to match wherever it lands and re-pointing every current
includer (`read_program.hpp`, the reader grammar's own test suite), which
is strictly more than this step's own merge criterion needs and risks the
"nothing else may break" instruction for no F24 benefit. The step brief
itself anticipates this choice ("if you choose to leave it in place for
now, say so explicitly").

## Consequences

- Every existing consumer of `machine::forth_state` (`machine::run`,
  `machine::eval_program`, `forth.hpp`'s `forth_program`,
  `machine/*.test.cpp`) is unaffected; none of their signatures or
  behavior changed.
- `smd::forth::interpreter::forth_state::machine()` is the seam: any later
  step that needs to hand an `interpreter::forth_state`'s stacks/data-space/
  output to R1-pipeline machinery goes through this accessor.
- **Step F26 inherits the token-layer move as unfinished business**, folded
  into "the cut" rather than a separate step: relocate
  `src/smd/forth/reader/forth_chars.hpp`, re-namespace it, and re-point its
  two current includers (this step's own `interp.hpp` among them). F24's
  own non-binding recommendation, recorded in
  `docs/compiler_architecture.org`'s Phase 1 section, is `parser/` over
  `interpreter/`, since D19 frames the token layer as combinator-library
  machinery several consumers sit on top of, not as something the
  interpreter component itself owns.

## Revisit condition

The composition decision is permanent: nothing about F25's colon compiler
or F26's cut requires `machine::forth_state` itself to grow these fields
rather than continuing to be wrapped.
The deferred-move item closes at step F26, when `reader/` is deleted and
`forth_chars.hpp` must land somewhere by construction.
