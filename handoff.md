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

## Step F4 — Import parser combinators

- Imported from `~/src/compile-time-scheme/main/src/smd/smdscheme/parser/`
  into `src/smd/forth/parser/`, renamespaced `smd::smdscheme::parser` ->
  `smd::forth::parser`: `cursor.hpp`, `parser.hpp`, `alt.hpp`,
  `parser_ops.hpp`, plus their `.test.cpp` files. `test_neg_parser_concept.cpp`
  was **not** imported — this project has no negative-compile-test harness
  yet (F3 didn't bring one in either; none of `foundation`'s
  `test_neg_*.cpp` files were imported), and F4's merge criteria don't call
  for one; F21 (error-quality and negative-compile pass) is where that
  infrastructure and pattern arrive.
- Scheme char predicates left behind per the plan: `cursor.hpp` dropped
  `is_initial_symbol_char`, `is_symbol_char`, and `is_delimiter` (all
  s-expression/symbol-token-specific). Kept: the `cursor` class itself,
  `is_space` (generic ASCII whitespace, not Scheme-specific), and
  `skip_intertoken_space` (generic algorithm built on `is_space`, used by
  `alt.hpp`'s `lexeme`). Forth's own word/number/comment predicates arrive
  in F5's `forth_chars.hpp`.
- `parser.hpp` and `alt.hpp` ported unchanged apart from renamespacing and
  include-path updates: `parser_like`, `parse_state<T>`, `parse_result<T>`,
  `parser<F>` (+ deduction guide), `pure`, `satisfy`, `char_p`, `map`,
  `lift2`, `sequence_left`, `sequence_right`, `operator|` (from
  `parser.hpp`); `alt`, `many<Capacity>`, `some<Capacity>`, `optional`,
  `lexeme` (from `alt.hpp`). No hardcoded capacities were found to
  parameterize — `many`/`some`'s `Capacity` was already an NTTP with no
  default upstream, and stays that way (call sites specify it explicitly,
  same as upstream).
- `parser_ops.hpp` is the one file with a real behavior change beyond
  renamespacing: **DIV-0003**
  (`docs/divergences/DIV-0003-parser-foundation-typeclass.md`). The Scheme
  reference's `parser_ops.hpp` reimplemented its own local
  `ParserApplicative`/`ParserAlternative` CRTP layers and its own local
  `parser_typeclass<T>` lookup variable, never actually plugging into
  `smd::smdscheme::foundation`'s functor/applicative/alternative machinery
  even though that machinery already existed alongside it. This project's
  `parser_ops` instead derives directly from
  `smd::forth::foundation::functor<parser_functor_impl>`,
  `foundation::applicative<parser_applicative_impl>`, and
  `foundation::alternative<parser_alternative_impl>`, and registers
  `parser<F>` against foundation's own `functor_typeclass`,
  `applicative_typeclass`, and `alternative_typeclass` variable templates
  (by reopening `namespace smd::forth::foundation` inside `parser_ops.hpp`).
  `foundation::fmap`/`foundation::invoke`/`foundation::alt` (the free CPOs)
  now dispatch to `parser<F>` exactly as they would to any other
  registered type — `parser<F>` is the first production, exported type to
  register against `foundation`'s CRTP bases (F3's own tests for those
  three headers only used small test-local instance types). The global
  default instance is `smd::forth::parser::parser_v` (a `parser_ops`), same
  name as upstream.
- Consequence of deriving from `foundation::alternative<Impl>`: it requires
  both `alt` and `empty` (an identity element for `alt`), where the Scheme
  reference's local parser typeclass provided only `alt`. `empty<T>()` is
  added as a parser of value type `T` that always fails without consuming;
  because `parser<F>` is a family of types (parameterized by the wrapped
  callable) rather than one concrete container type,
  `foundation::empty_fn`'s zero-argument calling convention
  (`tc_type{}.empty()`) cannot deduce `T`, so the generic free function
  `foundation::empty<T>()` does not work for `parser<F>` — call
  `parser_v.empty<T>()` directly instead (documented on
  `parser_alternative_impl::empty`'s doc comment and exercised by
  `parser_ops.test.cpp`'s `AlternativeEmptyIsIdentity` test). Full details
  and consequences are in DIV-0003.
- The typeclass object's `discard_first`/`discard_second` (inherited from
  `foundation::applicative<Impl>`) cover the same ground as the Scheme
  reference's `parser_v.sequence_left`/`sequence_right` (`discard_first`
  keeps the second argument's value, `discard_second` keeps the first's);
  `parser_ops` does not re-add `sequence_left`/`sequence_right` names to
  avoid two names for the same typeclass-level operation. The free
  functions `sequence_left`/`sequence_right` in `parser.hpp` (Layer 0, not
  the typeclass object) are untouched and still exist under their original
  names.
- `parser_ops` also carries `many<Capacity>`/`some<Capacity>`/`optional`/
  `lexeme` as direct member-template wrappers over the `alt.hpp` free
  functions (foundation has no counterpart for these; they are
  parser-specific repetition/whitespace combinators, not
  functor/applicative/alternative primitives).
- Merge-criterion `static_assert` (immediately-invoked-lambda pattern)
  lives in `src/smd/forth/parser/parser_ops.test.cpp`: builds a digit
  parser via `satisfy` over a local `is_digit` predicate, folds one-or-more
  digits (`some<8>`) into an `int` with `map`, and parses `"42"` — composed
  entirely from the generic primitives in this test, not the Scheme atom
  parser. `parser_ops.test.cpp` also has functor-law tests (identity,
  composition) and exercises `foundation::fmap`/`invoke`/`alt` dispatching
  to `parser<F>` directly, per house style's "law-focused tests before
  performance tests" rule.
- CMake: headers fold into the existing `forth_forth_headers` `FILE_SET` on
  `compile-time-forth.forth` (no separate `compile-time-forth.parser`
  target — same pattern as `foundation`); tests build as `parser_test`,
  wired from `src/smd/forth/parser/CMakeLists.txt`, descended into via
  `add_subdirectory(parser)` in `src/smd/forth/CMakeLists.txt`.
- Verified on both `gcc-16` and `clang-21`: `make compile`, `make test`
  (89/89 passed), `make lint` all green for the `parser/` addition itself.
  Note: `make lint` (`pre-commit run -a`) currently fails on a pre-existing
  `clang-format` drift in F2/F3 files (`foundation/{applicative,functor,
  parse_error,source_pos,static_vector}.hpp`, `applicative.test.cpp`,
  `sender/vocab.test.cpp`) that reproduces on the clean F3-merged baseline
  *before* any F4 change (verified by stashing all F4 edits and re-running
  `make lint`) — this is unrelated to F4 and out of its scope to fix
  (touching those files would violate "no unrelated files changed"); no
  file under `src/smd/forth/parser/` is ever reformatted by the same
  `clang-format` run. `smoke.sh gcc-16` and `smoke.sh clang-21` both end
  `SMOKE OK`.
