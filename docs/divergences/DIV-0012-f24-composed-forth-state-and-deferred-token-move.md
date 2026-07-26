# DIV-0012: F24's interpreter state is composed, not an in-place edit of `machine::forth_state`; the D19 token-layer move is deferred to F26

- **Status:** open — both items close at F26 (see the orchestrator's amendment below)
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

## Orchestrator amendment (filed at F24 merge)

F24's step author proposed the composition as permanent. It is accepted for
F24 and F25, but **not** as permanent, and the "Why" above understates one
thing.

D13 does not say the interpreter needs `SOURCE`/`>IN`/`BASE`/`STATE` nearby;
it says they *are* machine state in `forth_state`. That wording is
load-bearing for two later steps:

- **F29 (parsing words).** `PARSE`, `WORD`, `CHAR`, and `S"` are ordinary
  Forth words that read the input stream. Primitives execute as
  `machine::apply_primitive(op, machine::forth_state&)` — `interp.hpp` calls
  it exactly that way, via `st.machine()`. A primitive therefore cannot see
  an `input_source` that lives only on the wrapper. Under the composed shape,
  no parsing word can be a `machine::primitive`.
- **F28/D18 (every word has an execution token).** `'`, `[']`, and `EXECUTE`
  are uniform over every binding kind, and an XT is executed by the VM
  against machine state. A parsing word reachable through `EXECUTE` needs the
  input stream reachable from whatever the VM runs against.

The layering objection that motivated composition is real but is entirely an
artifact of the R1 pipeline: `machine::forth_state` has four consumers today
(`machine::run`, `eval_program`, `forth.hpp`, and the machine tests) only
because `eval_direct`/`codegen` still exist. **Those consumers die at F26.**
The alternative the "Why" dismisses as "duplicating" — defining
`input_source` under `machine/` — is not duplication; it is what D13 already
calls these fields. It was simply not worth doing while the old pipeline was
still standing.

So the composed shape is correct for now and wrong to freeze. F26 already
inherits the token-layer move; it inherits this too, and the two are the same
piece of work: once `reader/` and the elaborator are gone, fold
`input_source`, `BASE`, and `STATE` down into `machine::forth_state` and let
`interpreter::forth_state` collapse into it. If F26 finds a concrete reason
that fold cannot happen, that reason supersedes this amendment and must be
filed against D13 rather than settled silently.

## Revisit condition

Both items close at step F26.
The deferred token-layer move closes because `reader/` is deleted and
`forth_chars.hpp` must land somewhere by construction.
The composition closes because `machine::forth_state`'s R1-era consumers are
deleted in the same step, removing the layering objection that justified
wrapping rather than growing it — see the orchestrator amendment above for
why leaving it wrapped would obstruct F28 and F29.
