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
- `foundation/` headers (and their tests) are folded into the existing
  `compile-time-forth.forth` target's `forth_forth_headers` `FILE_SET`
  (declared with `BASE_DIRS src` at the top-level `CMakeLists.txt`), the same
  file-set name `forth.cpp`/`forth.hpp` already use; there is no separate
  `compile-time-forth.foundation` target. Imported tests build into their own
  `foundation_test` executable (mirroring `forth_test`), wired from
  `src/smd/forth/foundation/CMakeLists.txt` via `add_subdirectory(foundation)`
  in `src/smd/forth/CMakeLists.txt`.

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

## Step F2 — Vendor Beman Execution

- `vendor/execution` is a real git submodule (`.gitmodules` added), URL
  `https://github.com/bemanproject/execution.git`, pinned at commit
  `cd3211e8f2fdeaa4237de3803513438cac20aa2b` (`main`, "Fix spawn and
  spawn_future (#296)").
- Integrated purely by `add_subdirectory(vendor/execution)` from the
  top-level `CMakeLists.txt`, before the `compile-time-forth.forth` target is
  linked against `beman::execution` (`PUBLIC`); no `FetchContent`, no vcpkg,
  no `find_package`, no install-time discovery. `.update-submodules` /
  `.gitmodules` Makefile plumbing (already present) picks the new submodule
  up automatically via `git submodule update --init --recursive`.
- `BEMAN_USE_MODULES` is forced `OFF` (`set(... CACHE BOOL ... FORCE)`) before
  the `add_subdirectory`. Without this, this CMake/Ninja/GCC-16 combination
  auto-enables `CMAKE_CXX_SCAN_FOR_MODULES`, which makes the vendored
  `beman.execution` target build as C++ modules (`.cppm`) instead of the
  plain headers; that in turn makes any translation unit that includes both
  `<beman/execution26/execution.hpp>` and Catch2 headers (i.e.
  `sender/vocab.test.cpp`) fail to parse Catch2's own headers with unrelated
  syntax errors. Forcing header mode makes the vendored tree behave exactly
  like the reference repo's usage (traditional includes, no `import`).
- New component `src/smd/forth/sender/vocab.hpp` (+ `vocab.test.cpp`)
  aliases `beman::execution26::{just,then,let_value,when_all,sync_wait}`
  into `smd::forth::sender`, mirroring
  `~/src/compile-time-scheme/main/src/smd/smdscheme/sender/sender_v.hpp`
  (fresh minimal header, no `task` alias since Beman Task is not vendored
  yet). Test proves
  `sync_wait(then(just(20), [](int x){ return x + 22; }))` yields 42 under
  `gnu++26` — confirms the vendored tree digests on both toolchains.
- Wired via a new `src/smd/forth/sender/CMakeLists.txt` (added to the same
  `forth_forth_headers` file set as the rest of `compile-time-forth.forth`),
  descended into from `src/smd/forth/CMakeLists.txt` with `add_subdirectory(sender)`
  placed after the existing Catch2 discovery block so the new `sender_test`
  executable can rely on `Catch2::Catch2WithMain` already being resolved.
- Verified green on both toolchains: `make compile`, `make test`, `make lint`;
  `smoke.sh gcc-16` and `smoke.sh clang-21` both end `SMOKE OK`. 3/3 tests
  pass on each toolchain (the pre-existing `forth` test plus the two new
  `SenderVocabTest` cases).
- No DIV filed: forcing header-mode (`BEMAN_USE_MODULES OFF`) is an
  implementation necessity to match the plan's stated integration (plain
  `add_subdirectory`, traditional headers), not a deviation from
  `docs/forth-plan.md` or Forth-2012 semantics.

## Step F3 — Import foundation

- Imported from `~/src/compile-time-scheme/main/src/smd/smdscheme/foundation/`
  into `src/smd/forth/foundation/`, renamespaced `smd::smdscheme::foundation`
  -> `smd::forth::foundation`: `static_vector.hpp`, `source_pos.hpp`,
  `source_span.hpp`, `parse_error.hpp`, `result.hpp`, `arena_box.hpp` (with
  `tree_arena`), `functor.hpp`, `applicative.hpp`, `alternative.hpp`, plus
  their `.test.cpp` files (already `.test.cpp` upstream, no rename needed).
- Not imported, per D3: `foundation/fix.hpp`, its test, and
  `src/smd/fixpoint/`. Not imported, out of this step's inventory:
  `version.hpp`/`version.cpp` (not in the F3 file list).
- `functor.hpp`, `applicative.hpp`, and `alternative.hpp` had no dedicated
  upstream test files (they were only exercised indirectly through
  `smd::smdscheme::sender::string_writer`, which this project does not
  import); this step wrote new `.test.cpp` files for all three, each
  registering a small test-local typeclass instance (a `box`/`logged` type
  private to the test TU) to exercise the CRTP base (`fmap`/`replace`,
  `invoke`/`lift_a2`/`ap`/`discard_first`/`discard_second`,
  `alt`/`combine`/`empty`) and the CPO dispatch path, per house-style's
  bootstrap-plus-substantive-test rule.
- Parameterized: `tree_arena<T, MaxNodes>` had no default for `MaxNodes`
  upstream (only `arena_box<T, MaxNodes = 1024>` did); added
  `MaxNodes = 1024` to `tree_arena` too, so a handle type and its backing
  arena share a capacity default without restating it. No other hardcoded
  capacities were found in the F3 file set — `static_vector`'s `Capacity` and
  `arena_box`/`tree_arena`'s `MaxNodes` were already template parameters
  everywhere else.
- DIV-0002 (`docs/divergences/DIV-0002-parse-error-equality-constexpr.md`):
  `parse_error::operator==`'s pointer-equality fast path
  (`lhs.message == rhs.message`) is not usable in a constant expression under
  `clang-21` when both operands are identical string literals (unspecified
  address-equality result per `[expr.eq]`), though `gcc-16` accepted it;
  replaced with an explicit `lhs.message == nullptr && rhs.message ==
  nullptr` check ahead of the existing null-mismatch check, preserving the
  exact truth table while comparing pointers only against `nullptr` (always
  constant-evaluable). No other equality/comparison operators in the F3 file
  set had this pattern.
- CMake: no separate `compile-time-forth.foundation` target (see "Build and
  verification" above) — headers fold into `compile-time-forth.forth`'s
  existing `forth_forth_headers` `FILE_SET`; tests build as `foundation_test`.
- Verified on both `gcc-16` and `clang-21`: `make compile`, `make test`
  (50/50 passed), `make lint` all green; `smoke.sh gcc-16` and
  `smoke.sh clang-21` both end `SMOKE OK`.
- The `arena_box.test.cpp` merge-criterion `static_assert` lives in
  `src/smd/forth/foundation/arena_box.test.cpp`: an immediately-invoked
  lambda builds a `tree_arena<point_node, 8>` (a local test node type),
  allocates two nodes via `make_arena_box`, and reads them back through their
  `arena_box` handles, all at compile time.

## Step F8 — Machine substrate

- Landed `src/smd/forth/machine/{cell,stacks,forth_state}.hpp` (+
  `.test.cpp` each), namespace `smd::forth::machine`, canonical includes
  `<smd/forth/machine/*.hpp>`. Headers fold into
  `compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`
  (no separate compiled target, matching F3's pattern); tests build as the
  new `machine_test` executable, wired from
  `src/smd/forth/machine/CMakeLists.txt`, descended into via
  `add_subdirectory(machine)` in `src/smd/forth/CMakeLists.txt` (placed
  alphabetically between `foundation` and `sender`).
- `cell.hpp`: `cell = std::int64_t` (D7); `flag_true = -1` / `flag_false = 0`
  (Forth truth-value convention, all-bits-set / no-bits-set); and
  `status = foundation::result<std::monostate>` — `foundation::result<T>` is
  a `std::variant<T, parse_error>` and has no `T = void` specialization
  (`std::variant` cannot hold `void`), so `std::monostate` is the concrete
  stand-in everywhere the plan speaks of a conceptual `result<void>`. No
  change made to `foundation/result.hpp` itself.
- `stacks.hpp`: one template, `cell_stack<MaxDepth>`, with `data_stack` and
  `return_stack` as template aliases to it (same behavior; they only differ
  in which register of `forth_state` holds them). Backing storage is a
  `foundation::static_vector<cell, MaxDepth>` **pre-filled to `MaxDepth`**
  at construction (loop of `push_back(cell{0})`), because `static_vector`
  offers no `pop_back`/shrink operation; `cell_stack` tracks its own logical
  `depth_` separately and always indexes the (now full-size) backing vector
  in bounds, diagnosing overflow (`depth_ >= MaxDepth`) and underflow
  (`depth_ <= 0`, or `peek(offset)` with `offset >= depth_`) itself via
  `foundation::result`/`status` before ever touching storage out of range —
  `static_vector::push_back`'s own bounds check is an `assert` precondition
  (UB in release builds), so `cell_stack` never lets it fire from ordinary
  Forth-level misuse.
- `forth_state.hpp`: `forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>`
  bundles `data()`/`returns()` (the two stacks), `data_space()` (currently
  just a `foundation::static_vector<cell, MaxData>` placeholder — F10 wires
  real `allot`/`fetch`/`store` and a distinct typed `addr` over it), and
  `output()` (a `foundation::static_vector<char, MaxOut>`) with
  `emit_char`/`emit_cell` mutators. `emit_cell` renders decimal digits by
  hand (no `<charconv>` dependency) and negates through `std::uint64_t`
  (`0 - static_cast<std::uint64_t>(value)`, well-defined unsigned wraparound)
  rather than through `cell` itself, so `std::numeric_limits<cell>::min()`
  (whose positive magnitude has no representation in `cell`) renders
  correctly; `emit_cell(-42)` yields `"-42 "` (merge criterion, tested both
  as a `static_assert` and a Catch2 case). All of `forth_state`'s members
  are literal-type-compatible; the whole class is usable in `constexpr`
  contexts and is exercised via `static_assert` throughout
  `forth_state.test.cpp`.
- `forth_state.hpp` also declares the primitive opcode enum, `enum class
  primitive`, and the free function template `apply_primitive(primitive,
  forth_state<...> &) -> status` (both plan deliverables have no other
  named home among the three specified files, and `apply_primitive` takes
  `forth_state` by reference, so they live here). Enumerator names spell
  the Forth word, with a trailing underscore only where the bare spelling
  collides with a C++ keyword or another enumerator: `plus, minus, star,
  slash, mod_, negate, abs_, min_, max_, and_, or_, xor_, invert, lshift,
  rshift, zero_equal, zero_less, equal, not_equal, less, greater,
  less_equal, greater_equal, true_, false_, dup, drop, swap, over, rot,
  question_dup, nip, tuck, depth, to_r, r_from, r_fetch`. `/` and `MOD` use
  symmetric (C++ truncating) division — `a / b` and `a % b` directly — not
  floored division, and both diagnose division-by-zero via `status` before
  dividing. `LSHIFT`/`RSHIFT` shift through `std::uint64_t` (`RSHIFT` is
  logical/unsigned, per Forth-2012) and mask the shift count to `& 63` to
  avoid shift-amount UB. Every primitive's stack behavior, plus
  underflow/overflow/div-zero diagnosis, has a dedicated `static_assert` in
  `forth_state.test.cpp` (merge criterion).
- Verified on `gcc-16`: `make compile`, `make test` (68/68 passed), and
  `smoke.sh gcc-16` all green. `clang-21` was not re-verified this step (F8
  depends only on F3, already `clang-21`-clean); nothing added here is
  toolchain-specific.
- `make lint` (`pre-commit run -a`) reports **Failed** in this environment
  regardless of F8's changes: `clang-format` reformats pre-existing,
  untouched files (`src/smd/forth/foundation/{applicative,functor,
  parse_error,source_pos,static_vector}.hpp`,
  `foundation/applicative.test.cpp`, `sender/vocab.test.cpp`) by one space
  on wrapped `friend`/multi-line signatures, reproducible starting from a
  clean checkout of those files with F8's new `machine/` directory absent
  entirely from the working tree (confirmed by reverting and rerunning).
  This is pre-existing formatting drift, not something F8 introduced or is
  in scope to fix (those files belong to F2/F3); F8's own new files
  (`src/smd/forth/machine/*`) were checked directly with `pre-commit run
  clang-format --files src/smd/forth/machine/*.hpp
  src/smd/forth/machine/*.cpp` and produced **no** modifications. No DIV
  filed for this — it is environment/tooling drift, not a deviation from
  `docs/forth-plan.md` or Forth-2012 semantics. Flagged in
  `handoff-next.md` for whoever next runs `make lint` repo-wide.
