# Next steps: Step F2 and Step F3 (run in parallel, separate worktrees)

## What F1 did

Raised the C++ baseline to `gnu++26` on both toolchains:

- `etc/gcc-flags.cmake`: `CMAKE_CXX_STANDARD` 23 -> 26, `-std=gnu++23` ->
  `-std=gnu++26`.
- `etc/clang-flags.cmake`: no change needed — it already read
  `CMAKE_CXX_STANDARD 26` / `-std=gnu++26`, matching
  `~/src/compile-time-scheme/main/etc/clang-flags.cmake` exactly.

Verified on both toolchains: `make compile`, `make test`, `make lint` all
green; `.claude/skills/run-compile-time-forth/smoke.sh gcc-16` and
`.claude/skills/run-compile-time-forth/smoke.sh clang-21` both end
`SMOKE OK`. No divergence from `docs/forth-plan.md`; no DIV filed.

## F2 and F3 run in parallel, in separate worktrees

Per the plan's parallelism summary (`docs/forth-plan.md` "Parallelism
summary"): `F1 -> {F2, F3}`. Cut two worktrees off this merged state, one per
step. Do not let one step's worker touch the other's files.

## Step F2 — Vendor Beman Execution

Read `docs/forth-plan.md` section "Step F2 — Vendor Beman Execution" for the
full spec. In short:

1. Add `vendor/execution` as a git submodule (Beman Execution).
2. Integrate it with `add_subdirectory` from the top-level `CMakeLists.txt`,
   behind the existing project options.
3. Create `src/smd/forth/sender/vocab.hpp` (+ test) aliasing `just`, `then`,
   `let_value`, `when_all`, `sync_wait` into `smd::forth::sender`.
4. The test `sync_wait(then(just(20), [](int x){ return x + 22; }))`
   returning 42 proves the toolchain digests the vendored tree under
   `gnu++26`.
5. Update the submodule flow — the Makefile already runs
   `git submodule update --init --recursive` via `.update-submodules`; make
   sure that keeps working with the new submodule added.

Merge criteria: verify passes on gcc-16 and clang-21 (`make compile`, `make
test`, `make lint`, plus the smoke driver); the submodule is documented in
`handoff.md`.

Constraints from `AGENTS.md`: do not use `FetchContent`, `vcpkg`, or
`find_package` for Beman Execution; do not vendor by git subtree; integrate
only via `add_subdirectory`.

Dependencies: F1 (satisfied by this merge).

## Step F3 — Import foundation

Read `docs/forth-plan.md` section "Step F3 — Import foundation" for the full
spec. In short:

1. Copy from `~/src/compile-time-scheme/main` per divergence D2:
   `src/smd/forth/foundation/{static_vector,result,parse_error,source_pos,arena_box,functor,applicative,alternative}.hpp`
   plus their tests (renamed `.test.cpp` where needed).
2. Renamespace to `smd::forth::foundation`.
3. Update prologs with provenance lines (per `AGENTS.md`: files adapted by
   copy carry an additional provenance line after SPDX, naming the source
   file and repository).
4. Parameterize capacities — no hardcoded capacity constants (project-wide
   rule).
5. Do **not** import `fix.hpp` (per divergence D3 — heap-backed `fix`/`Box`
   types are barred from the compiled pipeline).
6. Wire a `compile-time-forth.foundation` CMake target, or fold into the main
   library — worker's choice — but keep file sets and
   `CMAKE_VERIFY_INTERFACE_HEADER_SETS` on.

Merge criteria: imported tests pass; a `static_assert` builds a small
`tree_arena` of a local test node type and reads it back by `arena_box`
handle.

Dependencies: F1 (satisfied by this merge). Runs in parallel with F2.

## Standing constraints (both steps)

- The Makefile is the single build interface, parameterized only by
  `TOOLCHAIN` and `CONFIG`; do not add per-file flags or side builds.
- Baseline is C++26 (`gnu++26`) on gcc-16 primary / clang-21 secondary — this
  is now in place; no fallback paths for older standards or other compilers.
- Tests use Catch2; do not introduce GTest.
- Before handoff: run `make compile`, `make test`, `make lint`; run
  `smoke.sh gcc-16` and `smoke.sh clang-21`; update `checklist.md` (tick the
  step); append durable facts to `handoff.md`; rewrite `handoff-next.md` for
  whatever comes next (F4 depends on F3; consult the plan's parallelism
  summary for what can start once both F2 and F3 land).
- File a divergence doc (`docs/divergences/DIV-NNNN-*.md`) for anything done
  differently than the plan specifies, or any knowing deviation from
  Forth-2012 semantics beyond the scope cuts already recorded.

## Known open items

None outstanding from F1. `docs/forth-plan.md` is committed and tracked as of
the F0 merge; it should stay that way.
