# step-brief.md — Step F25: the colon compiler and the session image

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory, only to hand you what F24 learned building the
interpreter that your own pasted section will not tell you.

## Goal (from the checklist title and F24's own forward pointer; verify against your pasted step section)

`checklist.md` names this step "colon compiler and session image." F24's own
step text (docs/forth-plan-2.md §6) previewed it as: D15's eventual artifact
is a session image — code space, dictionary, data-space seed, and output, a
trivially copyable literal built in one constant-expression evaluation and
runnable again at runtime — and that lands at F25/F26. F24 explicitly did
not build this; `compiled_forth<Source>` (`forth.hpp`) is untouched and still
the R1 one-shot API. Confirm the precise merge criterion against your own
pasted section rather than this paraphrase.

## What F24 built, by anchor

`docs/compiler_architecture.org`'s new **Phase 7** section (after Phase 6)
covers all of this in prose, transcluding both files in full:

- `smd::forth::interpreter::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut,
  MaxName>` (`src/smd/forth/interpreter/interp.hpp`, anchor
  `aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62` covers `interpret` itself) —
  **composes** a `machine::forth_state` (accessor: `.machine()`) with a new
  `input_source` (`.source()`), `BASE` (`.base()`/`.set_base()`, default 10),
  and `STATE` (`.state()`/`.set_state()`, default 0). See DIV-0012 for why
  this is composition, not an in-place edit of `machine::forth_state` —
  every existing R1-pipeline consumer of that narrower type is unaffected.
  **This shape is temporary; do not treat it as settled.** D13 says
  `SOURCE`/`>IN`/`BASE`/`STATE` *are* machine state, and F26 is scheduled to
  fold them down into `machine::forth_state` once the R1 consumers that
  forced the wrapper are deleted — otherwise F29's parsing words cannot be
  primitives (`apply_primitive` only ever sees the narrow type) and D18's
  uniform XT/`EXECUTE` cannot reach the input stream. Read DIV-0012's
  orchestrator amendment before designing anything around `.machine()`.
  Build F25 against the composed shape as it stands — do not do F26's fold
  early — but keep the seam thin and add nothing that makes the fold harder.
- `input_source` (`src/smd/forth/interpreter/input_source.hpp`, anchor
  `6a8b2e4f-1c9d-4a3e-8f5b-2d7c9e1a4b6f`) — `SOURCE`/`>IN` as a
  `std::string_view` plus a plain `int` offset, per D19: `>IN` is real
  machine state, not a cursor hidden in the scanner. `cursor_at_in()`
  rebuilds a `parser::cursor` by replaying from the start of the text.
- `interpret(state, dict, fuel)` — the outer loop, interpret state only: no
  `:`, so every dictionary hit it can reach is a `machine::primitive`,
  executed through the unchanged `machine::apply_primitive`. A non-primitive
  binding is diagnosed (`"word is not executable yet (F24: primitives
  only)"`), not silently skipped — **this is the branch F25 replaces** with
  real `colon_word` execution once `:` can produce one.
- Number classification per `BASE`: `is_number_token_in_base`/
  `token_to_cell_in_base` (same file), agreeing with the decimal-only
  `reader::is_number_token`/`token_to_cell` at base 10, generalized to
  2..36. `BASE` is plumbed and tested directly (`set_base`/`base()`); no
  `HEX`/`DECIMAL` word exists yet — add one only if your own step section
  asks for it.
- `consume_interp_fuel` — D22 fuel for the outer loop itself, once per token,
  independent of `machine::consume_vm_fuel`/`machine::consume_fuel`.

## Gotchas F25 needs and cannot get from its own pasted section

- **`machine::dictionary`'s existing `colon_word` binding (`dictionary.hpp`)
  is an R1 type and is very likely the wrong shape for you.** Its one field,
  `core_id`, is a handle into the elaborator's arena
  (`elaborator::compiled_unit`) — machinery step F26 deletes. A true-Forth
  colon definition compiles directly as immediate words run (D13/D24:
  threaded code, defunctionalized CPS, the return stack as the
  continuation), with no elaborator arena to hold a `core_id` into. Whether
  you repurpose `colon_word`, add a new binding variant, or change what
  `core_id` means is your call — but do not assume the existing struct
  already fits; it was built for a pipeline this pivot retired.
- **There is no code space yet.** `machine::instruction.hpp`/`codegen.hpp`/
  `vm.hpp`'s `compiled_program<MaxCode, MaxWords>` is a fixed, pre-sized
  array produced by one batch `codegen` call over an already-complete
  elaborated tree. A colon compiler that compiles words one `:`/`;` pair at
  a time as the interpreter meets them needs an incrementally-appendable
  code space, most likely living on `interpreter::forth_state` itself
  (D15's "code space" is part of the session image). Whether that reuses
  `instr`/`op` from `instruction.hpp` or needs its own representation is
  yours to decide.
- **`interpret`'s unknown-word and non-primitive-binding branches are your
  extension points**, not `apply_primitive`. Do not add compiling behavior
  inside `apply_primitive` (`machine/forth_state.hpp`) — that function is
  the pure-primitive substrate F8/F13/F16 already finished and other steps
  depend on its exact 47-opcode shape (`dictionary_test.cpp` asserts
  `dict.size() == 47`).
- **`STATE` is already a field, always 0 coming out of F24.** You do not
  need to add it to `interpreter::forth_state`; you need to make `interpret`
  (or a colon-compiler entry point layered over it) actually write and read
  it.
- **Fuel**: reuse `consume_interp_fuel`'s shape (per-something-processed,
  positioned diagnosis) rather than inventing a fourth fuel mechanism if
  compiling needs its own budget accounting.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks — a lint failure is real. **Run `make lint` as the very last thing
  before you commit, and commit whatever it reformats.** F24 ran it before a
  final edit and committed a tree that failed clang-format; the orchestrator
  caught it at the merge gate. clang-format is pinned to v21.1.2 by
  `.pre-commit-config.yaml` and is stricter than a system clang-format.
- Nothing is deleted until F26: the R1 pipeline (reader/elaborator/
  eval_direct/codegen/vm, all reachable via `forth.hpp`'s `compiled_forth`)
  must keep building and keep passing its own tests. F24 touched none of it.
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F24 used DIV-0012.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`smoke.sh` ends `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for F26 (the cut); DIV filed for any deviation, using the number
you are given.
