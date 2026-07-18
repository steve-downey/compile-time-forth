# Next step: Step F1 — C++26 baseline

## What F0 did

Installed the governance files this repository was missing:
`docs/codestyle.org`, `docs/CODING_RULES.md`, `AGENTS.md`, `CLAUDE.md`,
`checklist.md`, `handoff.md`, this file, `docs/divergences/TEMPLATE.md`, and
`docs/divergences/DIV-0001-structural-parse.md`. Read `AGENTS.md` first; it
points at everything else in the required order.

`docs/forth-plan.md` (the full operational plan) did not exist in this
worktree before F0 — it existed only as an untracked file in the main working
copy. F0 copied it in verbatim (unchanged) so the governance files that name
it (`AGENTS.md`, `CLAUDE.md`, `checklist.md`) resolve; F0 did not author its
content and made no edits to it.

## Your job: Step F1 — C++26 baseline

Read `docs/forth-plan.md` section "Step F1 — C++26 baseline" for the full
spec. In short:

1. Update `etc/gcc-flags.cmake` and `etc/clang-flags.cmake` to
   `CMAKE_CXX_STANDARD 26` / `-std=gnu++26`, matching
   `~/src/compile-time-scheme/main`'s settings for the same files.
2. Confirm the scaffold builds and tests pass on `TOOLCHAIN=gcc-16` and
   `TOOLCHAIN=clang-21` via the smoke driver:

   ```sh
   .claude/skills/run-compile-time-forth/smoke.sh gcc-16
   .claude/skills/run-compile-time-forth/smoke.sh clang-21
   ```

3. Merge criteria: both smoke runs end `SMOKE OK`.

## Standing constraints

- The Makefile is the single build interface, parameterized only by
  `TOOLCHAIN` and `CONFIG`.
- Do not add fallback paths for older C++ standards or other compilers.
- Tests use Catch2; do not introduce GTest.
- Before handing off: run `make compile`, `make test`, `make lint`; update
  `checklist.md` (tick Step F1), append durable facts to `handoff.md`,
  rewrite this file for Step F2 (vendor Beman Execution) and Step F3 (import
  foundation), which may run in parallel per the plan's parallelism summary.
- File a divergence doc for anything done differently than the plan
  specifies.

## Known open item

`docs/forth-plan.md` is new to this worktree as of F0 (copied in, not
previously tracked here). Confirm it is staged/committed alongside the rest
of F0's governance files when this step merges, so `docs/forth-plan.md`
never appears as an untracked file again.
