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

## Step F6 — Syntax tree

- Added `src/smd/forth/reader/syntax_tree.hpp` (+ `syntax_tree.test.cpp`),
  namespace `smd::forth::reader`, canonical include
  `<smd/forth/reader/syntax_tree.hpp>`.
- Node kinds (all exactly as specified): `syn_literal{cell, pos}`,
  `syn_word<MaxName>{name, pos}`,
  `syn_colon_def<MaxNodes,MaxBody,MaxName>{name, declared_effect, body, pos}`,
  `syn_if<...>{then_body, else_body, pos}`,
  `syn_begin_until<...>{body, pos}`,
  `syn_begin_while<...>{condition, body, pos}`,
  `syn_do_loop<...>{body, is_plus_loop, pos}`,
  `syn_variable<MaxName>{name, pos}`, `syn_constant<MaxName>{name, pos}`,
  `syn_create<MaxName>{name, pos}`, `syn_tick<MaxName>{name, pos}`. Every kind
  additionally carries a `foundation::source_pos pos` member (not explicitly
  named in the plan's node-kind shorthand, but covered by "attach source
  positions where sensible"), left default-constructed until F7's grammar
  populates it.
- **Design note for how D3 is satisfied without `fix`/`Box`:** the closed
  node type `syn_node<MaxNodes, MaxBody, MaxName>` is *forward-declared*
  before any of the composite node kinds (`syn_colon_def`, `syn_if`,
  `syn_begin_until`, `syn_begin_while`, `syn_do_loop`) are defined. Those
  composite kinds hold `arena_box<syn_node<...>, MaxNodes>` handles (via the
  `syn_box`/`syn_body` aliases) to their children — this compiles with only
  the forward declaration in scope because `foundation::arena_box<T,
  MaxNodes>` never stores a `T` (only an `int id_`), so it never needs `T`
  complete. `syn_node` itself is then defined afterward as a `std::variant`
  over all eleven node-kind structs. This is a different technique than the
  Scheme reference repo's `elaborated_core.hpp`/`datum_type.hpp` (which use
  `foundation::fix<F>` to close an open-recursive functor) — deliberately so,
  since `fix`/`Box` are barred from this project's compiled pipeline (D3).
  No `fix.hpp` equivalent exists or is needed in `smd::forth::reader`.
- `declared_effect` is a plain `foundation::source_span` (not
  `std::optional<source_span>`): the default-constructed span (`first ==
  last`, both `source_pos{0, 1, 1}`) *is* the "no stack-effect comment
  written" state, so no extra field or `std::optional` was needed to keep
  every node trivially destructible via plain aggregates. F7's grammar sets
  it to a real (non-empty) span when it captures a `( ... )` comment; F12
  re-slices the original source text through that span rather than the tree
  storing the comment text a second time.
  `foundation::source_span`/`source_pos` are unchanged from F3.
- Capacities are template parameters on every node-kind struct, `syn_node`,
  and `syntax_tree` (`MaxNodes`, `MaxBody`, `MaxName`), each defaulting to
  `1024`/`64`/`32` respectively (documented in the header); no hardcoded
  capacity constants anywhere in `reader/`.
- `syntax_tree<MaxNodes, MaxBody, MaxName>` bundles a
  `foundation::tree_arena<syn_node<...>, MaxNodes> arena` with a top-level
  `syn_body<...> program` (the sequence of top-level forms in source order —
  colon defs, `VARIABLE`/`CONSTANT`/`CREATE` declarations, and any top-level
  executable words). This `program` member is new relative to the plan's
  literal node-kind list; it exists because a whole Forth *program* is more
  than one node, and F7's grammar needs somewhere to record the top-level
  sequence. Not a divergence — no DIV filed, since D3 only constrains node
  representation, not whether a wrapper struct may also hold a top-level
  sequence.
- Two small helper free functions, `make_syn_name<MaxName>(std::string_view)`
  and `syn_name_equals<MaxName>(syn_name<MaxName> const&, std::string_view)`,
  ease building/reading `syn_name` values; both assume the input text is
  already case-folded (folding happens in F5's lexer, not here).
- Every node-kind struct plus the closed `syn_node` is verified trivially
  destructible via `static_assert(std::is_trivially_destructible_v<...>)` in
  a `detail` namespace at the bottom of `syntax_tree.hpp`, checked against
  the header's own default capacities as a representative instantiation.
- CMake: headers join `forth_forth_headers` `FILE_SET` on
  `compile-time-forth.forth` (no separate reader target, matching F3's
  pattern); tests build as `reader_test`, wired from
  `src/smd/forth/reader/CMakeLists.txt`, descended into via
  `add_subdirectory(reader)` in `src/smd/forth/CMakeLists.txt` (placed
  between `foundation` and `sender`, alphabetical).
- Added `docs/compiler_architecture.org`: pipeline diagram (mermaid) —
  source text → constexpr applicative parser combinators → syntax tree →
  elaboration (resolution + stack-effect checking) → codegen → {direct
  evaluator, stack-machine VM, sender/receiver CPS backend} — plus prose,
  one sentence per line, marked `DRAFT — pending author revision`. Only the
  "Phase 3: The Syntax Tree" section transcludes real code so far (three
  `#+transclude` blocks, by UUID anchor, from `syntax_tree.hpp`: the
  leaf-node kinds, the control-flow/composite node kinds, and the closed
  `syn_node` variant); the other phase sections are prose-only placeholders
  noting which future step (F4/F5/F7/F11/F12/F13/F14/F18) will add their
  transclusions, since transcluding code that doesn't exist yet isn't
  possible and per `docs/CODING_RULES.md` illustrative-but-noncompiling code
  is not allowed.
- The merge-criterion `static_assert` lives in
  `src/smd/forth/reader/syntax_tree.test.cpp`: an immediately-invoked lambda
  hand-builds `: SQUARED DUP * ;` (a `syn_word` "DUP", a `syn_word` "*", and
  a `syn_colon_def` "SQUARED" whose body holds both, all allocated in one
  small `syntax_tree<8, 4, 16>`), and a second immediately-invoked lambda
  walks it back through `arena.get`/`std::get<Kind>` and verifies the name
  and body structure, all at compile time; both are re-checked at runtime in
  a `TEST_CASE` for Catch2 visibility.
- Verified on `gcc-16` (the only toolchain available in this worker's
  environment): `make compile`, `make test` (54/54 passed, up from 50/50 at
  F3), `make lint` (see below), `smoke.sh gcc-16` all green/`SMOKE OK`.
  `clang-21` was not independently re-verified by this step (not installed
  in this worker's sandbox); nothing in `syntax_tree.hpp` uses anything
  `gcc-16`-specific, so no portability risk is expected, but a `clang-21`
  run is worth doing before/at the next merge point if that toolchain is
  available.
- **Environment note, not a DIV (no plan/Forth-2012 deviation, just a
  tooling-version observation):** `make lint`'s `clang-format` pre-commit
  hook, run with the `clang-format` installed in this worker's sandbox
  (Ubuntu clang-format 21.1.8), reformats seven **pre-existing, untouched**
  F1–F3 files on every run — `foundation/{applicative,functor,parse_error,
  source_pos,static_vector}.hpp`, `foundation/applicative.test.cpp`, and
  `sender/vocab.test.cpp` — shifting certain multi-line continuations by one
  space. Confirmed by stashing all F6 changes and re-running `clang-format
  --style=file -n` against the clean F3-merged tree: the same seven files
  already fail formatting with zero F6 changes present, so this is
  pre-existing drift (likely a `clang-format` minor-version difference from
  whatever produced the F1–F3-green `make lint` runs), not something F6
  introduced or fixed. This step left those seven files untouched
  (`git checkout --` after each `make lint` run) per the "no unrelated files
  changed" rule, so **`make lint` as run in this exact sandbox currently
  fails** on those pre-existing files even though every file this step
  actually touched is clang-format-clean and CMake-linting-clean (verified
  directly with `clang-format --style=file -n` against
  `reader/syntax_tree.hpp`/`syntax_tree.test.cpp`, and via `make lint`'s
  "CMake linting" hook passing on `reader/CMakeLists.txt`). Whoever merges
  next should decide whether to normalize those seven files' formatting in
  their own (unrelated) commit, or pin/reconcile the `clang-format` version
  the project expects.
