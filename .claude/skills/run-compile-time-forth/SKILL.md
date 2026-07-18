---
name: run-compile-time-forth
description: Build, test, and run compile-time-forth — the smd::forth C++ library, its Catch2 tests, and the hello example — via the top-level Makefile with TOOLCHAIN/CONFIG. Use when asked to run, build, test, or smoke-check this project or verify a change works.
---

# Run compile-time-forth

C++ library (`compile-time-forth.forth`, namespace `forth`) with a Catch2
test binary and a `hello` example executable. **The top-level Makefile is
the single interface to the build** — it wraps CMake+ninja and is
parameterized by exactly two variables: `TOOLCHAIN` (compiler) and
`CONFIG` (flag set). Never invoke a compiler directly; drive everything
through `make`. All paths below are relative to the repo root.

## Prerequisites

- `uv` on PATH (the Makefile runs cmake/ctest through `uv run` in a local
  `.venv` it creates automatically). `cmake` and `ninja` come from the venv.
- Compilers on PATH with versioned names (`g++-16`, `clang++-21`, …).
  One toolchain file per compiler exists under `etc/`; system `c++` is the
  default when `TOOLCHAIN` is unset.

## Run (agent path)

The smoke driver builds, runs ctest, runs the `hello` example, and runs
the test binary directly — all through the Makefile:

```bash
.claude/skills/run-compile-time-forth/smoke.sh                    # system c++, Asan
.claude/skills/run-compile-time-forth/smoke.sh gcc-16             # gcc-16, Asan
.claude/skills/run-compile-time-forth/smoke.sh clang-21 RelWithDebInfo
```

Success ends with `SMOKE OK: .build/build-<name> CONFIG=<config>`;
non-zero exit means build, test, or example failed.

Binary locations (the pattern the driver computes):

```
.build/build-${TOOLCHAIN:-system}/src/examples/${CONFIG}/hello
.build/build-${TOOLCHAIN:-system}/src/smd/forth/${CONFIG}/forth_test
```

`CONFIG` defaults to `Asan`. Available configs: `RelWithDebInfo`, `Debug`,
`Tsan`, `Asan`, `Gcov` (all pre-generated in one multi-config build tree).

## Build / test directly

```bash
make                                   # system c++, Asan: configure + build + ctest
make TOOLCHAIN=gcc-16 CONFIG=RelWithDebInfo
make ctest                             # re-run tests on current build, no rebuild
```

A fresh `TOOLCHAIN` value configures a new `.build/build-<toolchain>/`
tree on first use (Catch2 is provisioned automatically); subsequent
builds are incremental.

## Verifying a code change (direct invocation)

Most changes touch `src/smd/forth/forth.{hpp,cpp}`. Exercise them by
adding a Catch2 `TEST_CASE` to `src/smd/forth/forth.test.cpp` (or a call
in `src/examples/hello.cpp`), then run the smoke driver. Do **not**
side-compile a scratch file with ad-hoc compiler flags — new sources get
wired into `target_sources` in the corresponding `CMakeLists.txt` so the
whole build stays coherent.

## Project policy — extending the build

- The Makefile stays the primary interface. New files and capabilities
  must work *with* it, not around it.
- All files that are compiled are always compiled; everything in a build
  is built the same way, consistently.
- A capability needing different flags (coverage — already present as
  `Gcov` via `make coverage` — or benchmarking, etc.) is a **new
  `CONFIG`**, not per-file flag tweaks.

## Gotchas

- The no-`TOOLCHAIN` build tree is named `build-system`, not `build-c++`
  — hence `.build/build-system/...` for default builds.
- Default `CONFIG` is `Asan`: binaries are address-sanitized and slow.
  Use `CONFIG=RelWithDebInfo` for anything timing-sensitive.
- Each `make` run relinks `compile_commands.json` at the repo root to
  point at the last-built tree — building with a different toolchain
  changes what clangd sees.
- `make` with no target already runs ctest; there is no separate "test"
  step needed after a default build.
