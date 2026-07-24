# step-brief.md — Step F18a: Execution tokens and exceptions

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log` / `docs/history/handoff-archive.md`; architecture lives
in `docs/compiler_architecture.org` — consult those only where pointed, by section.

## Status this brief is written from

F16 (memory words end-to-end) just finished and is what this brief hands off
from. **F17 (counted loops) was being implemented concurrently in a separate
worktree (`wt-f17`) as of F16's own completion and had not yet merged.**
Per `docs/forth-plan.md`'s own parallelism summary (referenced, not
re-narrated, in F16's own prior brief), F15/F16/F17 are mutually parallel
after F14; confirm with the orchestrator whether F17 has merged before
starting F18a, and whether F18a's own dependency line in the plan names F17
as a prerequisite (this agent does not have that section memorized and did
not re-open the full plan to check, per the reading-contract rule).

## Goal

Authoritative spec: `docs/forth-plan.md` section "Step F18a — execution
tokens and exceptions" (the orchestrator pastes it; do not open the full
plan). This agent does not have that section's exact text or merge
criterion — only what is inferable from already-landed code (below).

## What already exists that F18a builds on (pointers, not narration)

- `machine::op::execute`, `::catch_mark`, `::throw_op` (`machine/instruction.hpp`)
  are reserved enumerators with no codegen or VM behavior yet; `vm.hpp`'s
  `run` diagnoses any of the three as `"execution-token/exception opcode not
  implemented until F18a"` if ever encountered (defensive only — codegen
  never emits them yet).
- `elaborator::core_push_xt` / `codegen`'s `op::push_xt` already produce a
  runnable execution token today: `' NAME` resolves (F11's
  `elaborate_tick`) to the referenced word's plain dictionary index, pushed
  as a cell (`eval_direct.hpp`'s `core_push_xt` case; `codegen.hpp`'s
  `op::push_xt` case) — identical runtime representation to `op::push` today,
  kept as a distinct opcode only for documentation/future extensibility. An
  `EXECUTE` implementation almost certainly needs to interpret this same
  index against `unit.dictionary`/`compiled_program.entry_points`, the way
  `core_call`/`op::call` already do.
- `elaborator::stack_effect.hpp`'s own doc comment on `unknown_effect` says
  explicitly: once `EXECUTE` exists as a `machine::primitive` (or however
  F18a represents it), its `primitive_data_effect` table entry belongs
  there as `unknown_effect` — anything reached via `EXECUTE` is not
  statically known, the same reasoning already applied to `?DUP`.
- `machine::default_dictionary` now installs **46** primitives (not 42): F16
  added `@`/`!`/`+!`/`ALLOT` (`fetch`, `store`, `plus_store`, `allot` in the
  `primitive` enum, `machine/forth_state.hpp`). Any new F18a primitive
  changes this count again; anywhere the count is hardcoded (grep for `46`
  across `src/smd/forth/machine/` and `src/smd/forth/elaborator/`) needs the
  same lockstep update F16 did for 42→46 (`dictionary.hpp`/`.test.cpp`,
  `codegen.test.cpp`'s `squared_index`, `elaborated_core.hpp`'s doc comment,
  `stack_effect.hpp`'s doc comment, `elaborate.test.cpp`'s dictionary-size
  assertion).
- `docs/compiler_architecture.org`'s new "Memory words end-to-end (step
  F16)" subsection (end of Phase 5) documents F16's own design choices in
  full, including where data-space seeding lives (`eval_program`/`run`,
  each calling `data_space::allot` once at entry) — not directly relevant to
  F18a's own EXECUTE/CATCH/THROW work, but the pattern ("reuse an existing
  primitive-level mechanism rather than adding a second") is the same shape
  this project has used for every backend-parity step so far.
- `docs/divergences/DIV-0009-f16-cell-granular-memory-words.md`: this
  project's data space is cell-granular (no `CELLS`/`CELL+`/`CHARS`/
  `CHAR+`), not Forth-2012's byte-granular convention. Not expected to
  interact with F18a, but worth knowing if `EXECUTE`/`CATCH`/`THROW` ever
  need to reason about memory addresses.

## Standing constraints

- Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
  Baseline `gnu++26` on `gcc-16` primary / `clang-21` secondary — both
  verified green for F16 (`make compile`/`test` on each, plus both
  `smoke.sh` runs, `SMOKE OK`).
- `make lint` caveat (unchanged from F15/F16): `markdownlint-cli`/`gitleaks`/
  `checkmake` pre-commit hook environments cannot provision in a
  network-restricted sandbox (TLS cert verification fails fetching a
  prebuilt Node/tool). Run the individual hooks that do not need network
  provisioning instead (`clang-format`, `gersemi`, `codespell`,
  `mbake-validate`, `shellcheck`, plus the built-in
  whitespace/yaml/large-file hooks) and note the gap if `make lint` itself
  cannot run end to end — this is an environment limit, not a code problem.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` (D3); capacities are template parameters with defaults (no
  hardcoded constants); nonlocal control (`EXIT`/`LEAVE`/`CATCH`/`THROW`) is
  one-shot, dynamic-extent (D11) — `CATCH`/`THROW` per F18a's own scope will
  need to fit this same invariant, presumably threaded through the
  evaluator's/VM's own result channel the way `core_exit`/`op::ret` already
  are, not a real C++ exception.
- Next free divergence number: **DIV-0011** (DIV-0009 is F16's own; DIV-0010
  is reserved for the concurrent F17 worker — confirm it has actually been
  used/merged before assuming DIV-0011 is truly next).

## Before handoff

`make compile`, `make test` green on `gcc-16` (and `clang-21` if available);
`make lint` green or the individual-hook fallback above; both `smoke.sh`
runs end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place; `step-brief.md` rewritten for the
next open step; DIV docs filed for any deviation.
