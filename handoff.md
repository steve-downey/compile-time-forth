# handoff.md

Durable facts about `smd::forth`, recorded as steps land. Append; do not
rewrite history away — later facts may refine earlier ones, but do not delete
a fact just because a later step changed something adjacent.

## Project identity

- This is `smd/forth`, a compile-time and runtime Forth compiler in C++26 on
  GCC16; the operational plan is `docs/forth-plan.md`.
- The reference implementation for infrastructure and lessons is
  `~/src/compile-time-scheme/main` (`smd::smdscheme`); there is no build-time
  coupling to that repository.

## Build and verification

- The Makefile is the single build interface, parameterized by exactly two
  variables, `TOOLCHAIN` and `CONFIG`; new flag-sets are new `CONFIG`s, never
  per-file flag tweaks; all compiled files are always compiled.
- The smoke driver is
  `.claude/skills/run-compile-time-forth/smoke.sh [TOOLCHAIN] [CONFIG]`; it
  builds, tests, and runs the example through the Makefile in one command.

## Imported infrastructure

- `foundation/` and `parser/` are adapted by copy from compile-time-scheme
  (`smd::smdscheme` -> `smd::forth`); provenance lines are added to file
  prologs; there is no build coupling to that repository.

## Architectural invariants

- Every tree (syntax tree, elaborated core, instruction program) is a flat
  `tree_arena` of trivially destructible nodes referenced by integer
  `arena_box` handles; heap-backed `fix`/`Box` types are barred from the
  compiled pipeline (`docs/forth-plan.md` D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API has one.
- All capacities are template parameters with defaults; there are no
  hardcoded capacity constants.
- Words fold to uppercase at scan time; source is parsed structurally by
  applicative combinators (DIV-0001); there is no interpretive outer loop and
  no `IMMEDIATE`.
- The machine is two fixed-capacity stacks plus an arena data space and an
  output buffer; all misuse is a diagnosed error via `foundation::result`,
  never UB.
- All nonlocal control (`EXIT`, `LEAVE`, `CATCH`/`THROW`) is one-shot and
  dynamic-extent; the sender backend maps `THROW` to the error channel.
- Beman Execution is vendored as a git submodule at `vendor/execution`,
  integrated by `add_subdirectory`; Beman Task at `vendor/task` only if F18
  needs it.

## Step F0 — governance install

- Installed `docs/codestyle.org`, `docs/CODING_RULES.md`, `AGENTS.md`,
  `CLAUDE.md`, `checklist.md`, `handoff.md` (this file), `handoff-next.md`,
  `docs/divergences/TEMPLATE.md`, and
  `docs/divergences/DIV-0001-structural-parse.md`, adapted from
  `~/src/compile-time-scheme/main`.
- `docs/forth-plan.md` did not yet exist in this worktree (it existed only as
  an untracked file in the main working copy the worktree was cut from); F0
  copied it in verbatim so the governance files that reference it by name
  resolve. F0 did not author its content.

## Step F1 — C++26 baseline

- Baseline is `gnu++26` (`CMAKE_CXX_STANDARD 26`, `-std=gnu++26`) on
  `TOOLCHAIN=gcc-16` (primary) and `TOOLCHAIN=clang-21` (secondary), matching
  `~/src/compile-time-scheme/main/etc/{gcc,clang}-flags.cmake`.
- Only `etc/gcc-flags.cmake` needed the edit (was `gnu++23`/`CMAKE_CXX_STANDARD
  23`). `etc/clang-flags.cmake` already matched the reference repo exactly
  (already `gnu++26`), so it was left untouched.
- `make compile`, `make test`, `make lint` are green on both toolchains;
  `smoke.sh gcc-16` and `smoke.sh clang-21` both end `SMOKE OK`.
- No divergence from `docs/forth-plan.md` — no DIV filed for this step.
