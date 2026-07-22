# Next step: Step F16 — Memory words end-to-end

Step F15 (public one-shot API) is done in worktree `wt-f15` / branch
`step/f15`. This file is a full rewrite for F16 — see `handoff.md`'s "Step
F15 — Public one-shot API" section (and everything above it) for the
complete historical record; this file only summarizes what F16 needs to
start.

## Parallelism note

Per docs/forth-plan.md's own "Parallelism summary": F15, F16, and F17 are
all mutually parallel after F14 (separate worktrees), since each depends
only on F14. If F17 (counted loops) is being worked by a different agent
concurrently with F16, both touch data that F18a (execution tokens) will
later also touch (the return stack, for F17 only) — F16 itself (memory
words) does not touch the return stack at all, so F16 and F17 should not
conflict with each other structurally, but merge order still matters if
both land before either merges to main: rebase whichever lands second.

## What F16 is

Read `docs/forth-plan.md` section "Step F16 — Memory words end-to-end"
(around line 686) for the authoritative spec. In short: wire
`VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT`/`@`/`!`/`+!` through **both**
evaluators (F13's `machine::eval_direct` and F14's `machine::codegen`/
`machine::run`), with data-space initialization threaded from the
`elaborator::compiled_unit` that already exists.

Merge criteria (from the plan, verbatim):

```forth
VARIABLE X  5 X !  X @ 3 + X !  X @   \ stack [8]
7 CONSTANT LUCKY  LUCKY LUCKY +       \ stack [14]
CREATE BUF 4 ALLOT                    \ BUF usable as base address
```

as `static_assert`s through both backends; an out-of-bounds store is a
diagnosed error, never UB (D7).

## What already exists (F9–F14) that F16 builds on

- **`machine::data_space<MaxData>`** (F10, `src/smd/forth/machine/
  data_space.hpp`): a bounds-checked cell arena with `allot(n)`,
  `fetch(addr)`, `store(addr, value)` — F16 is the step that actually
  wires `@`/`!`/`+!` as *primitives* against a `forth_state`'s own
  `data_space()`, not a new data-space type.
- **`elaborator::elaborate_variable`/`elaborate_create`/`elaborate_constant`**
  (F11, `src/smd/forth/elaborator/elaborate.hpp`) already elaborate
  `VARIABLE`/`CREATE`/`CONSTANT` declarations at the *elaboration* level:
  `VARIABLE` allots one cell and binds the name to its address
  (`machine::variable_word`); `CREATE` binds the name to the current
  data-space top without allotting anything (`ALLOT` is what F16 itself
  adds real behavior for — extending storage *past* a `CREATE`d address);
  `CONSTANT` constant-folds the immediately preceding top-level literal
  into the dictionary entry. All three already exist and are tested
  (`elaborate.test.cpp`) — F16 does **not** need to add elaboration-level
  support for the declarations themselves, only for `ALLOT` as a body-item
  (see "What's actually missing" below) and for wiring `@`/`!`/`+!` as
  primitives.
- **`elaborate_word_ref`**'s `core_var`/`core_const` cases (F11,
  `elaborate.hpp`): a `variable_word`/`constant_word` reference already
  elaborates to `core_var{.address = ...}` / `core_const{.value = ...}`,
  and F14's own `codegen_emit_node` already compiles both to a plain
  `op::push` of the address/value — this is why the plan's own
  `compiled_program::data_space_size` field (F14) already exists and is
  already populated (`unit.data_space.size()` at codegen time), even
  though nothing has consumed it as a runtime data space yet. F16 is what
  finally makes `forth_state::data_space()` get initialized from that
  size (or otherwise reconciled with it) before a program that reads/
  writes it runs.
- **`machine::forth_state::data_space()`** (F8/F13,
  `src/smd/forth/machine/forth_state.hpp`): already exists as an accessor
  returning `machine::data_space<MaxData>&`, but nothing currently reads
  or writes through it at runtime — F8 built the type, F16 is the first
  step to actually exercise it end-to-end.
- **`machine::primitive`** enum (F8, `forth_state.hpp`): has no
  `fetch`/`store`/`plus_store` (`@`/`!`/`+!`) enumerators yet — these are
  new enumerators F16 adds, following the existing naming convention
  (trailing underscore only where the bare spelling collides with a C++
  keyword or another enumerator; `@`/`!`/`+!` have no such collision, so
  something like `fetch`, `store`, `plus_store` should read cleanly,
  matching `plus`/`minus`/`star` for `+`/`-`/`*`).
- **`ALLOT` is not yet a body-item at all**: F7's grammar (`src/smd/forth/
  reader/read_program.hpp`) has no `ALLOT` keyword; `CREATE BUF 4 ALLOT`
  needs `ALLOT` to parse as an ordinary word (it takes one stack argument,
  the cell count, so it can almost certainly be treated as a primitive
  word rather than a new grammar production — check whether the grammar's
  existing "ordinary word" path already covers this, or whether `ALLOT`
  needs special-casing the way `VARIABLE`/`CONSTANT`/`CREATE` do; the plan
  groups `ALLOT` with `@`/`!`/`+!` in its own wording, not with
  `VARIABLE`/`CONSTANT`/`CREATE`, which is a hint it is meant to be an
  ordinary primitive-like word, not a new declaration keyword).

## The API F16 will touch

- `src/smd/forth/machine/forth_state.hpp` (`primitive` enum, `apply_primitive`):
  add `@`/`!`/`+!` (and decide `ALLOT`'s home — primitive vs. elaboration-level
  special form; see above).
- `src/smd/forth/machine/eval_direct.hpp` (F13): needs its own handling for
  `core_var`/`core_const` bodies that call the new primitives (`core_prim`
  should already cover `@`/`!`/`+!` once they are primitives — verify
  nothing about `eval_direct.hpp`'s own `core_prim` case needs to change,
  since it already delegates uniformly to `apply_primitive`).
- `src/smd/forth/machine/codegen.hpp`/`vm.hpp` (F14): same expectation —
  `codegen_emit_node`'s `core_prim` case and `vm.hpp`'s `op::prim` case
  should already handle new primitives uniformly, *if* `@`/`!`/`+!` are
  ordinary primitives. The real F14-side gap is `compiled_program::
  data_space_size` (already populated, per above) needing to actually
  initialize a `forth_state`'s `data_space()` before `run()` starts — check
  whether `machine::run` itself should grow a `data_space_size` parameter,
  or whether a caller (F15's own `forth_program`, `vm.test.cpp`'s existing
  test helpers) is expected to do this initialization step itself before
  calling `run`. This is a real design call this step has to make and
  document.
- `src/smd/forth/elaborator/elaborate.hpp` (F11/F12): `ALLOT`'s elaboration
  path, wherever it ends up living.
- **`compiled_forth<Source>` (F15, `src/smd/forth/forth.hpp`)**: once F16
  wires data-space initialization end-to-end, `compiled_forth`'s own
  `detail::compile_program` pipeline and `forth_program::run` may need a
  one-line addition to actually initialize a fresh `forth_state`'s data
  space from `compiled_program::data_space_size` before calling
  `machine::run` — check `forth.hpp`'s own `forth_program::run`/`stack`/
  `output` methods (each constructs a `state_type state{};` and calls
  `machine::run(program_, state, fuel)` directly) once F16 decides where
  data-space initialization belongs; if it belongs in `machine::run`
  itself, no F15-side change is needed; if it belongs in the caller, F15's
  own `forth_program` methods need updating to match — flag this
  explicitly either way in whichever `handoff.md` section documents F16's
  own design call.

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary — both
  confirmed available and green in this worker's sandbox for F15
  (`make compile`, `make test` at **202/202** on both toolchains,
  `make compile-headers` clean on `gcc-16`, `smoke.sh gcc-16`/
  `smoke.sh clang-21` both `SMOKE OK`).
- **`make lint` could not be run to completion in this worker's sandbox**:
  `pre-commit run -a` fails while provisioning the `markdownlint-cli` and
  `gitleaks` hook environments (`ssl.SSLCertVerificationError`, a
  sandbox-wide TLS/network restriction on new hook-environment downloads,
  unrelated to any file content). `clang-format`, `gersemi`,
  `trailing-whitespace`, `end-of-file-fixer`, `check-yaml`, `codespell`,
  and `check-added-large-files` were all verified individually instead and
  are clean on every file F15 touched. If this worker's own sandbox has
  network access, run `make lint` in full before handoff; if it hits the
  same TLS error, this is an environment limitation, not a code problem —
  verify the same individual hooks instead and note it in `handoff.md`,
  the same way F15 did.
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern) plus a matching `TEST_CASE`.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3); `compiled_program` (F14) stays flat by
  construction (a `static_vector<instr, MaxCode>`); `forth_program` (F15)
  is a thin wrapper around `compiled_program`, not a new tree type.
- All capacities are template parameters with defaults; no hardcoded
  capacity constants — `compiled_forth<Source>` (F15) parameterizes every
  pipeline-stage capacity plus three new runtime-state capacities
  (`StackDepth`, `RStackDepth`, `MaxOut`); F16 should follow the same
  discipline for anything new it introduces.
- All nonlocal control (`EXIT`, `LEAVE`, `CATCH`/`THROW`) is one-shot and
  dynamic-extent (`handoff.md`'s architectural invariants) — unaffected by
  F16, which touches only memory words, not control flow.
- Before handoff: `make compile`, `make test` green on `gcc-16` (and
  `clang-21` if available); `make lint` green if the sandbox allows it,
  otherwise the individual-hook fallback documented above; both
  `smoke.sh` runs end `SMOKE OK`; `checklist.md` ticked; `handoff.md`
  appended (not rewritten); `handoff-next.md` rewritten for whatever comes
  next (F17, counted loops, is the other F14-only-dependent step still
  open per the plan's "Parallelism summary" — mention explicitly in
  whichever `handoff-next.md` is written second if F16 and F17 are being
  worked concurrently); divergence docs filed for anything done
  differently than `docs/forth-plan.md` or Forth-2012 semantics (next free
  number: **DIV-0009** — DIV-0008 is F14's, unchanged by F15, which filed
  no new DIV).

## Known open items going into F16

- **Where `ALLOT` parses**: an ordinary primitive word (taking a cell
  count off the stack) vs. a new grammar production alongside
  `VARIABLE`/`CONSTANT`/`CREATE`. The plan's own wording groups it with
  `@`/`!`/`+!`, suggesting "ordinary word," but `ALLOT`'s *effect*
  (growing the data space, which lives on `compiled_unit`/`forth_state`,
  not the data stack) is unlike any existing primitive's effect (pure
  stack-to-stack or stack-to-output) — check whether `apply_primitive`'s
  own signature (`(primitive, forth_state&) -> status`) is already
  sufficient (it has access to `state.data_space()`, so an `ALLOT`
  primitive can pop a count and call `state.data_space().allot(n)`
  directly) before assuming a grammar change is needed.
- **Where data-space initialization belongs**: `machine::run` itself
  (taking `compiled_program::data_space_size` as an implicit
  precondition, or as an explicit step at the top of `run`) vs. every
  caller's own responsibility (F15's `forth_program`, `vm.test.cpp`'s
  helpers, any future caller) initializing a fresh `forth_state`'s data
  space before calling `run`. Whichever is chosen has a direct, mechanical
  consequence for `src/smd/forth/forth.hpp`'s own `forth_program::run`/
  `stack`/`output` — see "The API F16 will touch" above.
- **Out-of-bounds store diagnosis**: `machine::data_space::store` (F10)
  already diagnoses out-of-range addresses via `foundation::result`/
  `status` (D7) — confirm this at F16 time rather than assuming; if it
  does, F16's own merge-criterion diagnosis requirement may already be
  satisfied by F10's existing bounds checking, needing only a test that
  exercises it through a real compiled program rather than new production
  code.
