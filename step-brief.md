# step-brief.md — Step F16: Memory words end-to-end

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log` / `docs/history/handoff-archive.md`; architecture lives
in `docs/compiler_architecture.org` — consult those only where pointed, by section.

## Goal

Wire `VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT`/`@`/`!`/`+!` through **both** evaluators
— F13 `machine::eval_direct` and F14 `machine::codegen`/`machine::run` — with
data-space initialization threaded from the `elaborator::compiled_unit` that already
exists. Authoritative spec: `docs/forth-plan.md` section "Step F16 — Memory words
end-to-end" (the orchestrator pastes it; do not open the full plan).

## Merge criterion (from the plan, verbatim)

```forth
VARIABLE X  5 X !  X @ 3 + X !  X @   \ stack [8]
7 CONSTANT LUCKY  LUCKY LUCKY +       \ stack [14]
CREATE BUF 4 ALLOT                    \ BUF usable as base address
```

as `static_assert`s through **both** backends; an out-of-bounds store is a diagnosed
error, never UB (D7).

## Files this step will touch

- `src/smd/forth/machine/forth_state.hpp` — add `@`/`!`/`+!` to `primitive` +
  `apply_primitive`; decide `ALLOT`'s home (primitive vs. elaboration-level form).
- `src/smd/forth/machine/eval_direct.hpp` (F13) — `core_var`/`core_const` bodies;
  verify `core_prim` already delegates uniformly to `apply_primitive`.
- `src/smd/forth/machine/codegen.hpp` / `vm.hpp` (F14) — verify `core_prim` /
  `op::prim` handle new primitives uniformly; wire `compiled_program::data_space_size`
  into `forth_state` init before `run()`.
- `src/smd/forth/elaborator/elaborate.hpp` (F11/F12) — `ALLOT`'s elaboration path.
- `src/smd/forth/forth.hpp` (F15) — `forth_program::run`/`stack`/`output` may need a
  one-line data-space init depending on the design call below.

## What already exists that F16 builds on (pointers, not narration)

F8–F14 already built the substrate; read the named source files and the matching
sections of `docs/compiler_architecture.org` ("Phase 4: Elaboration", "Phase 5:
Codegen and the Three Backends", "The classic stack machine (step F14)") rather than
re-deriving. Concretely:

- `machine::data_space<MaxData>` (F10, `machine/data_space.hpp`): bounds-checked cell
  arena with `allot`/`fetch`/`store`; `store` already diagnoses OOB via
  `foundation::result` (D7). F16 wires `@`/`!`/`+!` as primitives against
  `forth_state::data_space()`; it does not add a new data-space type.
- `elaborator::elaborate_variable`/`elaborate_create`/`elaborate_constant` (F11,
  `elaborator/elaborate.hpp`): declarations already elaborate and are tested. A
  `core_var`/`core_const` reference already lowers to `op::push` of the address/value
  (F14). F16 adds elaboration only for `ALLOT` as a body-item, plus the `@`/`!`/`+!`
  primitives.
- `compiled_program::data_space_size` (F14) is already populated from
  `unit.data_space.size()` but nothing consumes it as a runtime data space yet — F16
  is the step that finally initializes `forth_state::data_space()` from it.
- `forth_state::data_space()` (F8/F13, `machine/forth_state.hpp`) exists but is never
  read/written at runtime yet. `primitive` has no `fetch`/`store`/`plus_store` yet —
  add them following the existing naming (`plus`/`minus`/`star` → so `fetch`/`store`/
  `plus_store`). `ALLOT` is not yet a body-item in F7's grammar.

## Open design decisions this step must make and document

1. **Where `ALLOT` parses** — ordinary primitive taking a cell count off the stack
   (its effect touches data space via `apply_primitive`'s `(primitive, forth_state&)`
   signature, which already has `state.data_space()`) vs. a new grammar production.
   The plan groups it with `@`/`!`/`+!`, hinting "ordinary word."
2. **Where data-space init belongs** — inside `machine::run` (consume
   `data_space_size` at the top) vs. each caller's responsibility. This has a direct
   mechanical consequence for `forth.hpp`'s `forth_program` methods.
3. **OOB store diagnosis** — likely already satisfied by F10's `store`; confirm and
   add a test through a real compiled program rather than new production code.

## Standing constraints

- Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only). Baseline
  `gnu++26` on `gcc-16` primary / `clang-21` secondary.
- **`make lint` caveat:** F15's sandbox could not provision `markdownlint-cli`/
  `gitleaks` hook envs (TLS/network restriction), so `pre-commit run -a` failed on
  those two only. If your sandbox has network, run full `make lint`; if it hits the
  same TLS error, verify the other hooks individually and note it — this is an
  environment limit, not a code problem.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` (D3); capacities are template parameters with defaults (no hardcoded
  constants); nonlocal control (`EXIT`/`LEAVE`/`CATCH`/`THROW`) is one-shot,
  dynamic-extent — unaffected by F16.
- Next free divergence number: **DIV-0009** (DIV-0008 is F14's).

## Parallelism note

Per the plan's "Parallelism summary", F15/F16/F17 are mutually parallel after F14
(separate worktrees). F16 (memory words) does not touch the return stack; F17
(counted loops) does — they should not conflict structurally, but if both land before
either merges, rebase whichever lands second. If F17 is being worked concurrently,
say so in whichever `step-brief.md` is written second.

## Before handoff

`make compile`, `make test` green on `gcc-16` (and `clang-21` if available); `make
lint` green or the individual-hook fallback above; both `smoke.sh` runs end
`SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place; `step-brief.md` rewritten for the next open
step (F17 counted loops is the other F14-only-dependent step still open); DIV docs
filed for any deviation.
