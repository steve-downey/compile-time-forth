# handoff.md — ARCHIVED (retired 2026-07-22, not on any agent read path)

> **This is the retired cumulative handoff log (F0–F15).** It was demoted because it
> grew with every step and was re-read in full by every worker, dominating context
> cost. Agents no longer read this file. The runtime reading contract is the
> three-tier model in `AGENTS.md`; durable cross-step invariants now live in
> `docs/compiler_architecture.org` (in place, by anchor); the per-step handoff is
> `step-brief.md` (forward-only, bounded). This file is kept only as historical
> reference; the same narrative is recoverable from `git log` and the per-step
> `--no-ff` merge commits. **Do not append to it and do not read it as part of a step.**

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
## Step F5 — Forth lexical layer

- Added `src/smd/forth/reader/forth_chars.hpp` (+ `forth_chars.test.cpp`),
  namespace `smd::forth::reader`, canonical include
  `<smd/forth/reader/forth_chars.hpp>`. Built entirely on the F4 combinators
  (`smd::forth::parser::{cursor,parser,satisfy,map,some,char_p,
  skip_intertoken_space}`) — no hand-rolled scanning loop for word/number
  tokens.
- `fold_char(char) -> char`: uppercase-folds one ASCII letter (D8); other
  characters pass through unchanged.
- `is_word_char(char) -> bool`: true for anything that is not ASCII
  whitespace — the plan's literal "a word is any run of non-whitespace
  characters" definition. `is_digit(char) -> bool`: ASCII decimal digit.
- `skip_forth_space(cursor) -> cursor`: Forth's own intertoken-space skip —
  loops skipping plain ASCII whitespace (via
  `parser::skip_intertoken_space`), `\` line comments (backslash to end of
  line or end of input), and `( ... )` comments, until none of the three
  remain. Distinct from `parser::skip_intertoken_space`, which only knows
  about plain whitespace. An unterminated `(` comment stops the skip in
  place (does not silently consume to end of input) so the next parse step
  fails in context.
- `scan_paren_comment(cursor) -> parse_result<source_span>`: the
  comment-capture primitive (D9) — requires `cur` to already be positioned
  at `(`; on success returns a `source_span` covering from the opening `(`
  through the closing `)` inclusive, so re-slicing the original source text
  through the span reproduces the comment text verbatim, e.g.
  `( a b -- c )`. `skip_forth_space` calls this internally and discards the
  span when it only needs to skip, not capture. This is what F7 should call
  right after parsing a colon-definition's name to capture
  `syn_colon_def.declared_effect` (`src/smd/forth/reader/syntax_tree.hpp`,
  from F6) when the next token is a `(` comment.
- `forth_lexeme(P) -> parser`: the Forth-aware analogue of
  `parser::alt::lexeme`, wrapping any parser so it skips `skip_forth_space`
  (not just plain whitespace) on both sides.
- `token_text<MaxName = 32> = foundation::static_vector<char, MaxName>`:
  alias for scanned token text.
- `scan_word<MaxName = 32>(cursor) -> parse_result<token_text<MaxName>>`:
  the token scanner — `forth_lexeme(map(some<MaxName>(satisfy(is_word_char,
  ...)), <fold each char>))`. Scanning `"dup"` yields `"DUP"` (merge
  criterion). Skips surrounding `skip_forth_space` (so it composes across
  comments, not just plain whitespace) via `forth_lexeme`.
- `is_number_token(std::string_view) -> bool`: true iff the text is an
  optional leading `-` followed by one or more decimal digits and nothing
  else. `-1` is a number; `1-` is a word (the `-` isn't the first
  character); `-` alone is a word (no digits after the sign) — merge
  criterion, all three cases have a dedicated `static_assert`.
- `token_to_cell(std::string_view) -> std::int64_t`: converts a token
  already confirmed by `is_number_token` into its signed decimal value
  (precondition, not checked). This is what F7 will call to build a
  `syn_literal.cell` once it has recognized a number token via
  `is_number_token`.
- Every public function has a compile-time (`static_assert`,
  immediately-invoked-lambda pattern where the check needs a parser
  invocation) exercise in `forth_chars.test.cpp`, plus a small number of
  matching `TEST_CASE`s for runtime/Catch2 coverage, per house style. No
  new capacity constants: `MaxName` is a template parameter defaulting to
  `32`, matching `syn_name<MaxName>`'s default from F6.
- CMake: `forth_chars.hpp` folds into the existing `forth_forth_headers`
  `FILE_SET` on `compile-time-forth.forth`; `forth_chars.test.cpp` is a new
  source on the existing `reader_test` executable
  (`src/smd/forth/reader/CMakeLists.txt`, alongside F6's `syntax_tree.hpp`/
  `syntax_tree.test.cpp`) — no new target, no new subdirectory.
- No divergence filed: every design choice above (the `forth_lexeme`
  wrapper, the inclusive-span convention for `scan_paren_comment`, folding
  during the same scan pass rather than as a separate post-pass) is an
  implementation detail satisfying the plan's literal Step F5 spec and D8/
  D9, not a deviation from `docs/forth-plan.md` or Forth-2012 semantics.
- Verified on `gcc-16` only (no `clang-21` available in this worker's
  sandbox): `make compile`, `make test` (113/113 passed), `make lint`, and
  `.claude/skills/run-compile-time-forth/smoke.sh gcc-16` (`SMOKE OK`) are
  all green. `clang-21` was not re-verified for this step — nothing added
  is toolchain-specific, but confirm on `clang-21` before or at the next
  merge point if that toolchain is available to whoever does it.
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

## Step F9 — Dictionary

- Landed `src/smd/forth/machine/dictionary.hpp` (+ `dictionary.test.cpp`),
  namespace `smd::forth::machine`, canonical include
  `<smd/forth/machine/dictionary.hpp>`. Headers fold into
  `compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`
  (no separate compiled target); tests build into the existing
  `machine_test` executable (new source added to
  `src/smd/forth/machine/CMakeLists.txt`'s existing `FILES`/test-source
  lists, per F8's stated plan — no new subdirectory).
- `word_name<MaxName>` is an alias for
  `foundation::static_vector<char, MaxName>` (default `MaxName = 32`,
  matching F6's `syn_name`). Two free helpers: `fold_upper(char) -> char`
  (ASCII-only uppercase fold) and `make_word_name<MaxName>(std::string_view)
  -> word_name<MaxName>` (folds every character while building). Unlike F6's
  `syn_name`/`make_syn_name` (which assume pre-folded input and document
  that choice), `dictionary.hpp` folds internally on both insertion and
  lookup — this is the header's documented choice for satisfying the F9
  merge criterion "case-folded lookup: `dup` finds `DUP`" directly, so
  entries are always stored already-uppercase regardless of what case a
  caller passes to `define_*`, and `lookup` folds its query the same way
  before comparing.
- The dictionary-entry binding is `dictionary_binding = std::variant<
  primitive, colon_word, variable_word, constant_word, foreign_word>`
  exactly as the plan specifies, plus:
  - `stack_effect{inputs = 0, outputs = 0, known = false}` — the minimal
    effect summary the plan calls for; F12 owns refining or replacing it.
  - `colon_word{core_id = -1, effect = stack_effect{}}` — `core_id` is a
    bare `int` (handle into F11's not-yet-built elaborated-core arena);
    F9 cannot name a typed handle to a type it doesn't know.
  - `variable_word{addr = cell{0}}` — **DIV-0004**
    (`docs/divergences/DIV-0004-dictionary-addr-placeholder.md`): the plan's
    D10 calls for `addr` to be a distinct type convertible to/from `cell`,
    but that type is F10's deliverable (F10 runs in parallel off the same
    F8 baseline, so F9 has no dependency on it); `addr` is a plain `cell`
    here until F10 lands and a follow-up retypes this field.
  - `constant_word{value = cell{0}}`, `foreign_word{index = -1}` — as
    specified.
- `dictionary<MaxWords, MaxName = 32>` wraps one
  `foundation::static_vector<dictionary_entry<MaxName>, MaxWords> entries_`.
  Five typed `define_primitive`/`define_colon`/`define_variable`/
  `define_constant`/`define_foreign` methods (rather than one overloaded
  `define`) each fold the name via `make_word_name` and append via a private
  `insert`, returning `status`; `insert` diagnoses `"dictionary full"` via
  `foundation::parse_error` before ever calling `static_vector::push_back`
  once `entries_.size() >= MaxWords` (`push_back`'s own bounds check is a
  debug-only `assert`, same reasoning as F8's `cell_stack`). Redefinition is
  never rejected — `insert` always appends, never overwrites or removes.
- `lookup(std::string_view) const -> dictionary_entry<MaxName> const *`
  scans `entries_` from `size() - 1` down to `0` (newest-first) and returns
  the first case-folded match, or `nullptr`. Because insertion never
  removes a shadowed entry, `size()` after redefining a name is larger by
  one per redefinition, and the shadowed entry is still reachable by index
  (just not by `lookup`) — this is what lets a cross-compiling Forth keep
  already-elaborated bodies bound to the old definition while new bodies
  see the new one (the plan's own note on this: "later words see the new
  definition, earlier resolutions keep the old one — static binding falls
  out of F11's program-order resolution").
- `default_dictionary<MaxWords = 256, MaxName = 32>() -> dictionary<MaxWords,
  MaxName>` installs all 37 F8 primitives (one `std::array<std::pair<
  std::string_view, primitive>, 37>` literal, looped over with
  `define_primitive`), under exactly the Forth spellings the plan lists:
  `+ - * / MOD NEGATE ABS MIN MAX AND OR XOR INVERT LSHIFT RSHIFT 0= 0< =
  <> < > <= >= TRUE FALSE DUP DROP SWAP OVER ROT ?DUP NIP TUCK DEPTH >R R>
  R@` — a one-to-one mapping onto every `enum class primitive` enumerator
  from F8's `forth_state.hpp` (verified by count: 37 words, 37
  enumerators). `MaxWords` default of 256 leaves headroom beyond the 37
  primitives for a program's own colon/variable/constant/foreign
  definitions.
- Every binding alternative, the closed `dictionary_binding` variant, one
  `dictionary_entry<32>`, and a representative `dictionary<256, 32>` are all
  checked `std::is_trivially_destructible_v` in a `detail` namespace at the
  bottom of `dictionary.hpp` (same pattern as F6's `syntax_tree.hpp`).
- Merge-criterion `static_assert`s (immediately-invoked-lambda pattern) live
  at the top of `dictionary.test.cpp`, each also mirrored as a runtime
  `TEST_CASE` for Catch2 visibility: lookup finds an installed word
  (`default_dictionary<>().lookup("DUP")` holds `primitive::dup`);
  shadowing (`define_constant("X", 1)` then `define_constant("X", 2)`,
  `lookup("X")` returns the value-`2` entry, `size() == 2`); case-folded
  lookup (`lookup("dup")` also finds `DUP`). Additional `TEST_CASE`s cover
  colon/variable/foreign round-trip through `lookup` +
  `std::get<...>(entry->binding)`, dictionary-full diagnosis
  (`dictionary<1>`, second `define_constant` fails), and a missing-word
  lookup returning `nullptr`.
- Verified on `gcc-16` (only toolchain available in this worker's sandbox):
  `make compile`, `make test` (114/114, up from 68/68 at F8), `make lint`
  (all hooks Passed, including `clang-format` and `gersemi` CMake
  linting — no repeat of the F6/F8 pre-existing `clang-format` drift on
  `foundation/{applicative,functor,parse_error,source_pos,static_vector}.hpp`
  et al.; that drift is not present in this checkout), and
  `smoke.sh gcc-16` all green/`SMOKE OK`. `clang-21` was not independently
  re-verified this step (not installed in this worker's sandbox); nothing
  in `dictionary.hpp` is toolchain-specific.
- DIV-0004 filed (see above) for the `variable_word::addr` placeholder.
  No other divergence from `docs/forth-plan.md` or Forth-2012 semantics.
## Step F10 — Data space

- Added `src/smd/forth/machine/data_space.hpp` (+ `.test.cpp`), namespace
  `smd::forth::machine`, canonical include
  `<smd/forth/machine/data_space.hpp>`. Header folds into
  `compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`
  (matching F8's pattern, no separate compiled target); test source added to
  the existing `machine_test` executable via
  `src/smd/forth/machine/CMakeLists.txt`.
- `addr` (untemplated, plain class in `smd::forth::machine`) is the distinct
  typed cell index (D10): a private `cell index_{}` plus a default
  constructor, `explicit addr(cell)`, `explicit operator cell() const`, and a
  defaulted hidden-friend `operator==`. No arithmetic or implicit-conversion
  operators are provided on purpose -- an `addr` only ever moves between "a
  `cell` on the data stack" and "an index `data_space` accepts", both via
  explicit casts at the boundary; nothing in F10 needs `addr + 1` etc. (F16's
  `CREATE`/`ALLOT` interaction can add address arithmetic then, backed by the
  same `explicit operator cell()`, if it turns out to need it).
- `data_space<MaxData = 1024>` is a bump allocator: `allot(count) ->
  result<addr>` reserves `count` contiguous zero-initialized cells and
  returns the address of the first; `fetch(addr) const -> result<cell>` and
  `store(addr, cell) -> status` read/write one cell. The backing
  `foundation::static_vector<cell, MaxData>` is pre-filled to `MaxData` at
  construction (same trick as F8's `cell_stack`, since `static_vector` has no
  shrink/pop operation); a separate `int high_` tracks the allotted
  high-water mark (Forth's `HERE`), exposed as `size() -> int` and
  `here() -> addr`.
- **Bounds-check design decision** (not a DIV -- an implementation choice
  within the plan's "bounds-checked" requirement, not a deviation from it):
  `fetch`/`store` diagnose an address as out-of-bounds when it is negative
  **or when it has not yet been returned by `allot`** (`index >= high_`), not
  merely when it exceeds the physical `MaxData` capacity. Every real address
  in this design originates from `allot`, so this is a strictly *stronger*
  safety net than bounds-checking against raw capacity alone -- it catches
  stray/uninitialized `addr` values (e.g. `addr{5}` conjured without ever
  calling `allot(6)`) as errors rather than silently handing back a
  zero-initialized cell that was never actually reserved.
  `allot(count)` itself diagnoses both a negative `count` and exhaustion
  (`count > MaxData - high_`, computed to avoid signed-overflow on the
  addition).
- `forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>`'s `data_space()`
  accessor (mutable + const) now returns `machine::data_space<MaxData> &` /
  `const &` instead of the F8 placeholder
  `foundation::static_vector<cell, MaxData> &`; the private member changed
  from a bare `static_vector` to a `machine::data_space<MaxData>`. No other
  `forth_state` member changed signature. The pre-existing
  `forth_state.test.cpp` assertion `state.data_space().size() == 0` still
  compiles and passes unchanged, since `data_space::size()` was deliberately
  named/typed to match `static_vector::size()`'s `-> int` meaning ("count of
  cells in active use so far").
- Merge-criterion `static_assert`s (immediately-invoked-lambda pattern) live
  in `src/smd/forth/machine/data_space.test.cpp`: allot/fetch/store
  round-trip (single-cell and multi-cell/contiguous-run cases), out-of-bounds
  fetch/store diagnosis (both "past `here()`" and "negative index" cases),
  allot exhaustion diagnosis (both "request exceeds remaining capacity" and
  "negative count" cases), and `addr` <-> `cell` explicit-conversion
  round-trip (plus structural equality).
- Verified on `gcc-16` (only toolchain available in this worker's sandbox):
  `make compile`, `make test` (114/114 passed), `make lint`
  (`pre-commit run -a`) all green, `smoke.sh gcc-16` ends `SMOKE OK`. Notably,
  the `clang-format` drift on pre-existing F2/F3 files documented at F6/F8
  (`foundation/{applicative,functor,parse_error,source_pos,
  static_vector}.hpp`, `foundation/applicative.test.cpp`,
  `sender/vocab.test.cpp`) was **not** reproduced in this run -- `make lint`
  passed clean repo-wide with zero unrelated files touched (confirmed via
  `git status --porcelain` before/after). Whoever tracked that drift as an
  open item can likely close it; it may have been an environment/tooling
  version that has since resolved itself, or a prior worker's `make lint`
  run already normalized those files upstream of this worktree's base.
  `clang-21` was not re-verified this step (not installed in this sandbox);
  nothing added is toolchain-specific.
- No DIV filed for F10 -- every design choice above (the allotted-high-water
  bounds check, `addr`'s narrow explicit-conversion-only interface, the
  `size()`/`here()` convenience accessors) is an implementation detail
  within the plan's literal spec, not a deviation from `docs/forth-plan.md`
  or Forth-2012 semantics.

## Step F7 — Grammar

- Added `src/smd/forth/reader/read_program.hpp` (+ `read_program.test.cpp`),
  namespace `smd::forth::reader`, canonical include
  `<smd/forth/reader/read_program.hpp>`. Header folds into
  `compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`
  (matching F5/F6's pattern, no separate compiled target); test source added
  to the existing `reader_test` executable via
  `src/smd/forth/reader/CMakeLists.txt`.
- **Entry point:** `template <int MaxNodes = 1024, int MaxBody = 64, int
  MaxName = 32, int MaxDepth = 32> constexpr auto
  read_program(std::string_view source) -> foundation::result<syntax_tree<
  MaxNodes, MaxBody, MaxName>>`. `MaxDepth` is new relative to F6's
  `syntax_tree` capacities -- it bounds the grammar's own recursive-descent
  nesting (see DIV-0005 below), checked before `IF`/`BEGIN`/`DO` ever
  recurse one level deeper, diagnosed as `"max nesting depth exceeded"`
  rather than either compiling forever or accepting unbounded nesting.
- Grammar implemented exactly as specified (`docs/forth-plan.md` Step F7):
  `program := item* eof`; `item := colon-def | variable | constant |
  create | body-item`; `colon-def := ':' name effect-comment? body-item*
  ';'`; `body-item := literal | tick | if | begin-until | begin-while |
  do-loop | word`; `if`/`begin-until`/`begin-while`/`do-loop`/`tick`/
  `variable`/`constant`/`create` all as given. `VARIABLE`/`CONSTANT`/
  `CREATE` are legal only at the top level (the `item` production), matching
  the grammar's literal shape -- encountering one of them while parsing a
  body diagnoses `"VARIABLE/CONSTANT/CREATE not allowed inside a
  definition"` rather than silently treating it as a bare word (Forth-2012
  itself does not allow defining words to run inside colon-compilation
  either).
- **Token-level scanning is built entirely from F4/F5's free-function
  combinators** (`scan_word`, `skip_forth_space`, `scan_paren_comment`,
  `is_number_token`, `token_to_cell`, `parser::some`, `parser::satisfy`),
  per D6. Two small helpers layer directly on top: `scan_token<MaxName>
  (cursor) -> parse_result<scanned_token<MaxName>>` pairs a `scan_word`
  result with the token's own starting position (which `scan_word` alone
  does not preserve); `scan_token_no_trailing_skip<MaxName>` is a second
  variant used **only** for a colon-definition's name, because `scan_word`
  wraps its scan in `forth_lexeme`, which skips trailing Forth intertoken
  space -- and `( ... )` comments count as intertoken space (D9) -- so an
  ordinary `scan_token` call would silently consume a stack-effect comment
  immediately following the name before `parse_colon_def` ever got a chance
  to inspect it. `scan_token_no_trailing_skip` only performs the *leading*
  skip, so its returned cursor sits exactly at the first character after
  the name's own text, letting `parse_colon_def` peek (via plain
  `parser::skip_intertoken_space`, not `skip_forth_space`, so it stops at a
  `(` rather than consuming the comment) for an immediately-following `(`
  before anything else has a chance to eat it. **This was the one real bug
  caught during this step's own verification** (the `abs_ok`
  merge-criterion static_assert initially failed because the effect
  comment had already been swallowed) -- worth flagging to whoever next
  writes a "scan X, then look at what comes right after" grammar production
  against these combinators.
- **DIV-0005** (`docs/divergences/DIV-0005-grammar-recursive-descent.md`):
  the grammar's nested control-structure productions (`if`/`begin-until`/
  `begin-while`/`do-loop`, each recursively containing `body-item*`) are a
  small set of mutually recursive plain `constexpr` function templates
  (`parse_body_until`, `parse_body_item_from_token`, `parse_if`,
  `parse_begin`, `parse_do`), not a chain of F4 combinator composition
  (`map`/`lift2`/`operator|`) -- a self-recursive grammar production has no
  finite `parser<F>` type to compose it from, and assembling tree nodes
  needs a mutable `tree_arena` threaded alongside the cursor, which the
  combinator calling convention (`cursor -> parse_result<T>`) has no slot
  for. Every one of these functions still calls down into the F4/F5 free
  functions for its own token-level work -- only the tree-shaped recursion
  and arena-threading are hand-written. **F11 (and any later step with a
  genuinely self-recursive node kind) should expect to reach for this same
  plain-mutually-recursive-function-template shape**, not try to force it
  through `functor`/`applicative`/`alternative`-style combinators.
- **Error catalog** (every one has a dedicated failure test in
  `read_program.test.cpp`, checking the exact `foundation::parse_error`
  message and source position, via `parse_error`'s own `operator==`):
  - `"unterminated definition (no ;)"` -- colon-def body ran out of input
    before `;`; position is where the failed token scan landed (effectively
    end-of-input). The same "ran out of input before the expected
    terminator" pattern produces `"unterminated IF (missing THEN)"`,
    `"unterminated IF (missing THEN after ELSE)"`, `"unterminated BEGIN
    (missing UNTIL or WHILE)"`, `"unterminated BEGIN...WHILE (missing
    REPEAT)"`, and `"unterminated DO (missing LOOP or +LOOP)"` for the
    other four body-sequence productions (only the colon-def case has a
    dedicated test, since all five share the same `parse_body_until`
    implementation path).
  - `"ELSE without IF"`, `"THEN without IF"`, `"UNTIL without BEGIN"`,
    `"WHILE without BEGIN"`, `"REPEAT without BEGIN...WHILE"`, `"LOOP
    without DO"`, `"+LOOP without DO"` -- any closing keyword encountered
    as a body-item head when it isn't the *current* context's own
    terminator is diagnosed by name, at that token's position (tested:
    `ELSE`/`THEN`/`UNTIL`, the rest share the identical dispatch branch in
    `parse_body_item_from_token`).
  - `"stray ;"` -- a `;` encountered anywhere it isn't the current
    colon-def's own terminator (including a bare `;` at the top level, no
    open colon-def at all -- the required test case).
  - `"nested : inside definition"` -- a `:` encountered as a body-item head
    (only ever possible inside an already-open colon-def, since a
    top-level `:` is instead the start of a new colon-def).
  - `"reserved word cannot be redefined"` -- a colon-def's name (after case
    folding) matches the reserved set `: ; IF ELSE THEN BEGIN UNTIL WHILE
    REPEAT DO LOOP +LOOP VARIABLE CONSTANT CREATE '`, checked only for
    colon-def names per the plan's literal wording, position at the name's
    own start.
  - `"VARIABLE/CONSTANT/CREATE not allowed inside a definition"` -- an
    implementation-level extension beyond the plan's four explicitly-listed
    error cases (not a divergence -- the grammar's `body-item` production
    never included these three keywords as alternatives in the first
    place; this diagnoses the otherwise-unreachable case cleanly instead of
    treating the token as an ordinary, permanently-unresolvable word
    reference).
  - `"max nesting depth exceeded"` -- `MaxDepth` reached before `IF`/
    `BEGIN`/`DO` recurses another level; tested with `MaxDepth = 0` against
    a single level of nesting.
- **Declared stack-effect capture (D9):** the first `( ... )` comment
  immediately after a colon-def's name is captured into
  `syn_colon_def.declared_effect` *only if* its text contains `--`
  (substring search over the re-sliced comment span); a comment that
  exists but doesn't qualify is left alone and simply gets skipped later as
  ordinary intertoken space, exactly like any other comment -- no special
  handling needed for "wrong-shaped comment present." Merge-criterion test
  slices `declared_effect` back out of the *original* source text (not a
  duplicated copy) and checks it equals `"( n -- n )"` verbatim, per F6's
  documented span convention.
- **Merge criteria, both as `static_assert` (immediately-invoked-lambda)
  and mirrored `TEST_CASE`s in `read_program.test.cpp`:**
  - Round-trip `: ABS ( n -- n ) DUP 0< IF NEGATE THEN ;` -- verifies the
    colon def's name, the declared-effect span text, the three-item body
    (`DUP`, `0<` as a *word* -- not a number, since `<` fails `is_digit` --
    and the `IF` node), and the `IF` node's `then_body`/`else_body` shape.
  - Two-level nesting `: F BEGIN DUP IF DROP THEN UNTIL ;` -- `IF` nested
    inside `BEGIN ... UNTIL` inside a definition, verifying the full nested
    structure walks back correctly.
- Verified on both `gcc-16` and `clang-21` (both available in this
  worker's sandbox): `make compile`, `make test` (138/138 passed, up from
  113/113 at F5/F6's baseline -- F8/F9/F10 landed in parallel in the
  interim, hence the jump), `make lint`, and both `smoke.sh gcc-16` /
  `smoke.sh clang-21` all green/`SMOKE OK`.
- **`make lint` note:** `clang-format`/`gersemi` reformatted several
  pre-existing, untouched files on every run in this sandbox
  (`machine/{CMakeLists.txt,data_space.hpp,dictionary.hpp,
  dictionary.test.cpp}`, `reader/forth_chars.test.cpp`) -- the same kind of
  tooling-version drift flagged at F6/F8/F9/F10 (sometimes present,
  sometimes not, depending on the sandbox's installed `clang-format`/
  `gersemi` versions), reproducible with zero F7 changes present (confirmed
  via `git checkout --` on exactly those files and re-running `make
  lint`). This step left those files untouched (reverted after each `make
  lint` run) per "no unrelated files changed"; this step's own new files
  (`read_program.hpp`/`read_program.test.cpp`) and its one edit
  (`reader/CMakeLists.txt`) were independently confirmed
  `clang-format`-clean via a targeted `pre-commit run clang-format --files
  ...`. Whoever next does a dedicated formatting-hygiene pass should treat
  this as the same open item F6/F8/F9/F10 already flagged, not a new one.
- Filed DIV-0005 (see above); no other divergence from `docs/forth-plan.md`
  or Forth-2012 semantics.

## Step F11 — Elaborated core and resolution

- Added `src/smd/forth/elaborator/{elaborated_core,elaborate}.hpp` (+
  `.test.cpp` each), namespace `smd::forth::elaborator`, canonical includes
  `<smd/forth/elaborator/*.hpp>`. New directory, new component (this project's
  first `add_subdirectory` placed alphabetically *before* `foundation` in
  `src/smd/forth/CMakeLists.txt`); headers fold into
  `compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`
  (matching every prior step's pattern, no separate compiled target); tests
  build as the new `elaborator_test` executable, wired from
  `src/smd/forth/elaborator/CMakeLists.txt`.
- **Core node layout** (`elaborated_core.hpp`): eleven node kinds exactly as
  the plan specifies, plus one more the plan's parenthetical anticipated
  ("or bodies as static_vector of arena_box -- follow the F6 syntax-tree
  pattern") — `core_seq{items, pos}`. `core_seq` is what a colon
  definition's elaborated body actually becomes; `machine::colon_word::
  core_id` (a bare `int` since F9) is the arena index of one of these, the
  only way to give that bare `int` somewhere concrete to point at.
  `core_push{cell,pos}`, `core_prim{primitive,pos}`,
  `core_call{word_index,pos}`, `core_var{addr,pos}`, `core_const{cell,pos}`,
  `core_push_xt{word_index,pos}`, `core_exit{pos}` are simple (non-recursive)
  leaf kinds; `core_if{then_body,else_body,pos}`,
  `core_begin_until{body,pos}`, `core_begin_while{condition,body,pos}`,
  `core_do_loop{body,is_plus_loop,pos}`, `core_seq{items,pos}` are the
  composite kinds, each `core_body<MaxNodes,MaxBody>` a
  `static_vector<core_box<MaxNodes,MaxBody>,MaxBody>` -- same
  handle-sequence shape as `reader::syn_body`. `core_node<MaxNodes,MaxBody>`
  (forward-declared before the composite kinds, exactly like F6's
  `syn_node`) is the closed `std::variant` over all twelve; every node kind
  carries its own `foundation::source_pos pos` (F12 needs these for its own
  diagnostics). Unlike `reader::syn_node`, the core has no `MaxName`
  parameter at all -- every name has already been resolved away into an
  `int` index, an `addr`, or a `cell` by the time a node reaches the core.
  Every node kind and the closed variant are `static_assert`-checked
  trivially destructible (D3), matching F6/F9's pattern.
- **`compiled_unit<MaxNodes=1024, MaxBody=64, MaxName=32, MaxWords=256,
  MaxData=1024, MaxWarnings=64>`** bundles: `arena` (the elaborated-core
  `tree_arena`), `dictionary` (`machine::dictionary<MaxWords,MaxName>`,
  pre-populated with all 37 F8 primitives via `machine::default_dictionary`
  before elaboration starts), `data_space`
  (`machine::data_space<MaxData>` -- a real F10 instance, not a bare
  counter: `VARIABLE`'s elaboration calls its `allot(1)` directly, so
  `compiled_unit` reuses F10's own bounds-checked API rather than
  reimplementing a redundant high-water-mark counter; `data_space.size()`
  *is* "the data-space size consumed by declarations" the plan's prose
  asks for), `program` (`core_body<MaxNodes,MaxBody>`, the top-level
  executable body in source order -- `VARIABLE`/`CONSTANT`/`CREATE`/colon
  defs contribute nothing here, only bare top-level executable forms do),
  and `warnings` (see below). `MaxName` is reused as-is from whatever
  `syntax_tree`/`read_program` instantiation produced the input tree (the
  same value parameterizes both `elaborate`'s input and `compiled_unit`'s
  dictionary).
- **Warning channel:** `warning_log<MaxWarnings> = static_vector<
  foundation::parse_error, MaxWarnings>` -- reuses `parse_error`'s
  `{where, message}` shape rather than inventing a second identical struct
  (the only difference between a warning and an error is which channel it
  travels through). `push_warning(unit, pos, message)` appends, silently
  dropping anything past `MaxWarnings` capacity (a warning is never itself
  a reason to fail elaboration, so there is no error path for "too many
  warnings" -- this is a documented, deliberate choice, not an oversight).
  Redefining any name (colon def, `VARIABLE`, `CONSTANT`, or `CREATE`)
  triggers exactly one `"redefined word"` warning at the new declaration's
  own position; the elaboration itself proceeds normally (matches the
  plan's "warn, don't error, on redefinition").
- **`elaborate<MaxNodes=1024, MaxBody=64, MaxName=32, MaxWords=256,
  MaxData=1024, MaxWarnings=64>(reader::syntax_tree<MaxNodes,MaxBody,
  MaxName> const&) -> foundation::result<compiled_unit<MaxNodes,MaxBody,
  MaxName,MaxWords,MaxData,MaxWarnings>>`** -- takes an already-parsed
  syntax tree (not source text); walks `tree.program` in program order
  with an explicit index loop (not a range-for), because `CONSTANT`'s
  constant-folding needs one-token lookahead (see below). Reuses the
  syntax tree's own `MaxNodes`/`MaxBody`/`MaxName` for the core arena's
  capacities, per DIV-0005's follow-up note in the F7->F11 handoff.
- **Resolution dispatch** (`elaborate_word_ref`, used for every bare
  `syn_word` reference, at top level and inside colon-def bodies alike):
  `EXIT` and `RECURSE` are checked *before* any dictionary lookup (neither
  is ever installed as a dictionary entry) -- `EXIT` becomes `core_exit`;
  `RECURSE` becomes `core_call{word_index = self_index}`, where
  `self_index` is threaded down through every recursive elaboration call
  as "the dictionary index of the colon definition currently being
  compiled, or -1 at the top level". Both are diagnosed
  (`"EXIT outside a definition"` / `"RECURSE outside a definition"`) when
  `self_index < 0` -- **this project's documented choice** for the plan's
  explicitly-open "EXIT at top level is an error or ignore" question:
  diagnosed as an error, for both `EXIT` and `RECURSE` symmetrically (not
  just `EXIT`). Any other name resolves via `dictionary::lookup`
  (`nullptr` -> `"unknown word"` at the reference's own position) and then
  branches on the resolved binding: `primitive` -> `core_prim`;
  `colon_word` -> `core_call{word_index}` (the index comes from the new
  `dictionary::lookup_index`, see below); `variable_word` -> `core_var`;
  `constant_word` -> `core_const`; `foreign_word` -> diagnosed
  (`"foreign words not yet callable"` -- nothing installs one until F19,
  this branch exists only so the `std::visit` stays exhaustive).
  `' NAME` (`elaborate_tick`) is simpler and uniform: no `EXIT`/`RECURSE`
  special-casing (ticking either is just "unknown word", since neither is
  a dictionary entry), no branching on binding kind -- `core_push_xt{
  word_index}` regardless of what `NAME` turns out to be bound to, since
  `EXECUTE` (F18a) is what later decides what to do with the token.
- **`RECURSE`'s self-index trick:** F7's grammar disallows nested `:` and
  `VARIABLE`/`CONSTANT`/`CREATE` inside a body-item sequence, so *no new
  dictionary entry can ever be created while a colon definition's own body
  is being elaborated*. That means `unit.dictionary.size()`, read once
  right before a colon def's body is elaborated, is exactly the index that
  definition will occupy once `define_colon` appends it afterward -- no
  dictionary mutation/update method was needed to let `RECURSE` inside the
  body resolve to "myself" before "myself" formally exists as an entry.
- **`CONSTANT` constant-folding** (`elaborate`'s top-level loop): rather
  than eagerly pushing every top-level literal into `unit.program` and
  then trying to "unpush" it if a `CONSTANT` follows (`static_vector` has
  no pop), the loop peeks one token ahead: when `tree.program[i]` is a
  `syn_literal` and `tree.program[i+1]` is a `syn_constant`, the pair is
  consumed together (`elaborate_constant`, using the literal's `.cell`
  directly -- no arena node for the literal is ever built), and the loop
  advances by two. Any other `syn_constant` encountered directly (no
  immediately preceding literal that survived to be looked at -- either
  nothing precedes it, or the preceding top-level form was some other,
  non-literal body-item already elaborated and pushed) is diagnosed:
  `"CONSTANT requires a preceding integer literal"` at the `CONSTANT`
  token's own position -- this is the plan's "a non-constant initializer
  is a diagnosed error" case.
- **`VARIABLE`** calls `unit.data_space.allot(1)` and installs
  `machine::variable_word{.address = <the returned addr>}`.
  **`CREATE`** installs `machine::variable_word{.address =
  unit.data_space.here()}` -- allotting *zero* cells, per the plan's
  literal text ("CREATE just records the address"); `CREATE` and
  `VARIABLE` share the same dictionary binding kind (there is no separate
  "created word" binding -- `dictionary_binding`'s five alternatives are
  unchanged from F9), documented on `variable_word` itself. F16's `ALLOT`
  is what later actually extends storage past a `CREATE`d address.
- **DIV-0004 resolved** (`docs/divergences/DIV-0004-dictionary-addr-
  placeholder.md`, updated in place, not superseded by a new number):
  `machine::dictionary`'s `variable_word::addr` field is now
  `machine::addr address{}` (was a placeholder plain `cell addr`). The
  field was also *renamed* `addr` -> `address`: `addr addr{};` does not
  compile in C++ once the field's type is itself named `addr` (GCC:
  `-Wchanges-meaning`, a hard error -- the member declaration's own name
  shadows the type name mid-declaration). `dictionary.hpp` gained
  `#include <smd/forth/machine/data_space.hpp>`; `dictionary.test.cpp` was
  updated (`variable_word{addr{3}}`, `.address` instead of `.addr`). No
  other F9/F10 call site referenced this field.
- **`dictionary.hpp` API extension** (additive, non-breaking, needed
  because `core_call`/`core_push_xt` must store a `word_index`, and F9's
  `lookup` only ever returned a pointer): added
  `lookup_index(std::string_view) const -> int` (same newest-first scan as
  `lookup`, returns the index instead of a pointer, `-1` if not found) and
  `entry_at(int) const -> dictionary_entry<MaxName> const&`. Both have
  their own `static_assert`/`TEST_CASE` coverage in `dictionary.test.cpp`.
  `lookup` itself is untouched.
- **Defensive-only branch:** `elaborate_body_item`'s `std::visit` has an
  `else` arm for `syn_colon_def`/`syn_variable`/`syn_constant`/
  `syn_create` appearing *inside* a body-item sequence -- F7's grammar
  already makes this unreachable (none of the four are part of the
  `body-item` production), but `std::visit` must stay exhaustive; it
  diagnoses `"declaration not allowed inside a body"` rather than being
  silently impossible-but-unchecked.
- **Merge criteria** (`static_assert`, immediately-invoked-lambda pattern,
  each mirrored as a `TEST_CASE` for Catch2 visibility, in
  `elaborate.test.cpp`): `: SQUARED DUP * ; : QUAD SQUARED SQUARED ;` --
  `QUAD`'s elaborated body (a `core_seq` reached through its
  `colon_word::core_id`) has exactly two `core_call` items, both with
  `word_index == lookup_index("SQUARED")`; unknown word
  (`: F NOPE ;`) diagnosed `"unknown word"` at `source_pos{4,1,5}`;
  forward reference (`: A B ; : B ;`) diagnosed identically (B is simply
  not yet in the dictionary when A is elaborated -- "use before
  definition" *is* "unknown word", no separate diagnosis needed);
  `VARIABLE`/`CONSTANT`/tick each independently exercised, plus
  redefinition-warns-not-errors, `RECURSE`-calls-self, and
  `EXIT`/`RECURSE`-outside-a-definition-is-diagnosed.
- Verified on `gcc-16` (only toolchain available in this worker's
  sandbox): `make compile`, `make test` (155/155 passed, up from 138/138
  at F7's baseline -- F9/F10 had already landed in the interim, hence part
  of the jump), `make lint`, and `smoke.sh gcc-16` all green/`SMOKE OK`.
  `clang-21` was not re-verified this step (not installed in this
  sandbox); nothing added is toolchain-specific.
- **`make lint` note:** the same recurring pre-existing `clang-format`/
  `gersemi` tooling-version drift flagged at every step since F6
  (`machine/{CMakeLists.txt,data_space.hpp}`,
  `reader/forth_chars.test.cpp` in this run) reproduced again, confirmed
  via `git checkout --` + rerun with zero F11 changes present; this
  step's own new/touched files (`elaborator/*`, `machine/dictionary.hpp`,
  `machine/dictionary.test.cpp`, `src/smd/forth/CMakeLists.txt`) were
  independently confirmed clean via targeted `pre-commit run clang-format`/
  `gersemi --files ...` runs. Still an open, standing environment item,
  not specific to F11.
- Updated `docs/compiler_architecture.org`'s "Phase 4: Elaboration"
  section with real prose (marked `DRAFT -- pending author revision`) plus
  three UUID transclusions from `elaborated_core.hpp` (leaf kinds,
  composite kinds, closed variant) and one from `elaborate.hpp` (the
  `elaborate` entry point), plus a paragraph on redefinition/static-binding
  semantics matching traditional cross-compiling-Forth behavior.
- No new DIV filed: every choice above either directly implements the
  plan's own explicitly-delegated wording ("your documented choice" for
  `EXIT`/`RECURSE` outside a definition; "document this" for `CREATE`
  reusing the `variable_word` binding), or is a non-surprising
  implementation necessity for the plan's own literal requirements
  (`word_index` needs a dictionary index-returning lookup; `RECURSE` needs
  the self-index trick; `CONSTANT` folding needs one-token lookahead).
  DIV-0004 is resolved (not superseded) in place, per its own "Revisit
  condition".

## Step F12 — Stack-effect analysis

- Added `src/smd/forth/elaborator/stack_effect.hpp` (+ `stack_effect.test.cpp`),
  namespace `smd::forth::elaborator`, canonical include
  `<smd/forth/elaborator/stack_effect.hpp>`. Header folds into
  `compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`
  (matching every prior elaborator/machine/reader step); test source added
  to the existing `elaborator_test` executable via
  `src/smd/forth/elaborator/CMakeLists.txt`. Runs **as part of** `elaborate`
  per D9 — a program failing stack-effect analysis fails elaboration; there
  is no separate opt-in checking pass.
- **The lattice** (`effect{bool known; int inputs; int outputs;}`,
  `unknown_effect`, `identity_effect`, `known(inputs,outputs)`): a `known`
  value's `inputs` doubles as the "minimum entry depth" the plan asks for —
  there is no separate field for it, it *is* that number. `unknown` is the
  lattice's top value; it is produced only by `?DUP` today (the plan's other
  named source, "anything reached via `EXECUTE`," has no `machine::primitive`
  enumerator yet — F18a is what adds one, and its net-effect table entry
  belongs in `primitive_data_effect` as `unknown_effect` once it exists,
  documented on that function). Once a computation touches `unknown`, every
  downstream combination (`combine_sequential`, `combine_branch`) stays
  `unknown` — this is the concrete mechanism behind "suppresses checking
  downstream rather than erroring."
- **Sequential composition** (`combine_sequential`) is the standard "stack
  level" derivation: combined-entry-requirement = first operand's own input
  requirement, plus however much more the second operand needs beyond what
  the first operand's own output already leaves behind; combined-output
  follows from combined-input plus the sum of both operands' net changes
  (`outputs - inputs`). `identity_effect` (0,0) is the identity element on
  both sides (verified by `static_assert`).
- **`IF`/`ELSE` arms are compared by *net effect* (the depth delta), not the
  full `(inputs,outputs)` pair** — this is the one real design decision
  handoff-next.md (F11→F12) flagged as ambiguous in the plan's wording
  ("unequal net effects"), resolved here because the alternative (full-pair
  equality) would reject the canonical Forth-2012 idiom
  `: ABS DUP 0< IF NEGATE THEN ;` (`NEGATE` is `(1,1)`, the implicit empty
  `ELSE` is `(0,0)` — different shapes, same net effect `0`). `combine_branch`
  computes the `IF` node's own contributed effect from two already-verified
  same-net arms using the same "whichever arm demands more at entry" logic as
  sequential composition. Verified directly by the `AbsIfNoElseTypeChecks`
  test/`static_assert` in `stack_effect.test.cpp` — this also de-risks F13's
  own planned `ABS` merge criterion, which depends on `elaborate` succeeding
  for exactly this program.
- **The primitive net-effect table** (`primitive_data_effect`,
  `primitive_return_delta`) — did not exist before this step (F8's
  `apply_primitive` is imperative runtime code, not a declarative mapping).
  35 of 37 primitives have a fixed data-stack effect; `?DUP` is
  `unknown_effect`; `>R`/`R>`/`R@` report only their *data*-stack view from
  `primitive_data_effect` (`>R`: `(1,0)`, `R>`: `(0,1)`, `R@`: `(0,1)`) —
  their *return*-stack contribution (`+1`/`-1`/`0` respectively) is a
  separate table, `primitive_return_delta`, since the return-stack side is
  tracked as a plain running `int`, never a lattice value (only `?DUP` is
  ever input-dependent, and it never touches the return stack, so return-
  stack tracking is always exactly known regardless of whether the data-stack
  side has gone `unknown`).
- **`RECURSE`'s self-effect fixed-point question** (flagged open by F11's
  handoff-next.md) — resolved as option (a)+(b) combined, exactly as that
  file's own two options described: if the definition being compiled wrote a
  declared `( ... )` effect comment, `RECURSE` call sites use it as ground
  truth (`analyze_colon_effect` computes `self_effect` once, up front, before
  walking the body); otherwise `RECURSE` resolves to `unknown_effect`,
  identical treatment to `?DUP`. No fixed-point solving is attempted. Tested
  by `RecurseUsesDeclaredEffect` (`: LOOP1 ( n -- n ) RECURSE ;`) and by the
  pre-existing F11 `RecurseCallsSelf` test continuing to pass unchanged (no
  declared effect there, so `RECURSE` is `unknown`, and the whole definition
  ends up with `stack_effect{known = false}` — same as before F12 existed).
- **`elaborate`'s signature changed** (additive, but a real breaking change
  to the entry point, not a new overload): `elaborate(tree, source)` now
  takes a second `std::string_view source` parameter — the exact program
  text `tree` was parsed from. F11 never needed this (it never read a
  declared effect's *text*, only its span); F12 needs it to re-slice
  `syn_colon_def::declared_effect` inside `analyze_colon_effect`. Threaded
  through `elaborate_colon_def` (which also gained a `source` parameter);
  every other `elaborate_*` helper is unchanged. `elaborate.test.cpp`'s call
  sites were updated (`elaborate<...>(tree.value(), source)`); one of its
  existing merge-criterion `static_assert`s (`SQUARED`/`QUAD`) also gained an
  assertion that `colon_word::effect` is now a real, analyzed `(1,1)` rather
  than the F9/F11 `stack_effect{}` placeholder.
- **Integration seam**, exactly where F11's handoff-next.md predicted:
  `elaborate_colon_def` calls `analyze_colon_effect(unit, body.value(),
  word_index, cd, source)` right after `elaborate_body` succeeds and right
  before building the `core_seq`/calling `define_colon` — the computed
  `machine::stack_effect` is passed directly into the new `colon_word`
  at construction, no in-place dictionary mutation needed.
- **Diagnoses 1-3** (`analyze_body`, recursive, one function handles `IF`,
  `BEGIN...UNTIL`, `BEGIN...WHILE...REPEAT`, `DO...LOOP`, and plain
  sequencing): `IF`/`ELSE` net-effect mismatch; `DO`/`BEGIN...UNTIL`/
  `BEGIN...WHILE...REPEAT` bodies with the wrong net effect (`DO`/`UNTIL`
  bodies must net to 0 after the loop-terminator's own flag pop is accounted
  for; `WHILE`'s condition must leave exactly the flag, net `+1`); return-
  stack imbalance across any single control-structure body (checked
  independently for each nested body — `then_body`, `else_body`, `DO`'s
  body, `BEGIN...UNTIL`'s body, `BEGIN...WHILE`'s condition and its body —
  each must net to `r_delta == 0` on its own). All diagnoses carry the
  *enclosing control node's own* position (e.g. the `BEGIN`'s `pos` for both
  its condition-flag-check and its body-net-zero-check), not a more granular
  inner position — simple and stable, per the merge-criteria requirement to
  check exact message + position.
- **Diagnosis 4** (`check_exit_in_do_loops`) is a separate, purely lexical
  recursive scan — independent of `analyze_body`'s depth tracking — because
  `UNLOOP` is not a primitive yet (F17 adds real counted-loop machinery), so
  there is no way to leave a `DO` loop's parameters consistent before an
  early `EXIT` today; it fires unconditionally on any `core_exit` lexically
  reachable while inside `>=1` enclosing `core_do_loop`, at any nesting
  depth. **F17 should relax this** once `UNLOOP` is real (documented on the
  function itself).
- **Diagnosis 5** (declared-vs-computed mismatch, in `analyze_colon_effect`):
  `has_declared_effect`/`parse_declared_effect` slice `cd.declared_effect`
  out of `source`, strip the `(`/`)` delimiters, and count whitespace-
  delimited tokens either side of a `--` token (reusing
  `reader::is_word_char` for tokenization — no new char-predicate
  invented). Comparison is full-pair equality (`inputs` and `outputs` both),
  unlike the net-only `IF`-arm comparison — a declared effect states an
  exact shape, not just a net change. Skipped entirely when the computed
  effect is `unknown` (an `unknown` result cannot contradict anything; the
  declared effect, if present, is simply trusted and stored).
- **`EXIT` handling is a deliberate, documented simplification, not full
  flow-sensitivity** — see **DIV-0006**
  (`docs/divergences/DIV-0006-exit-not-flow-sensitive.md`): once a
  `core_exit` is folded, remaining items in that same body list are treated
  as unreachable and never visited (sound for an unconditional trailing
  `EXIT`), but the checker does not reconcile an early-`EXIT` path's depth
  against the fall-through depth when `EXIT` sits inside a conditional. Every
  diagnosis the plan literally asks for is unaffected by this gap; it is
  additional exposure the plan's own diagnosis list does not call for.
- **`DO...LOOP` does not yet model the limit/start-index consumption** that
  real Forth-2012 `DO` pops at entry — not a DIV, since no core node
  represents that consumption yet (F17's `do_setup`/`I`/`J` machinery is
  what will add it); F12's `core_do_loop` contribution to the enclosing
  scope is purely "whatever the body needs, returned unchanged" once the
  body's own net-zero check passes. Flagged for F17, not filed as a
  divergence, since it directly follows the plan's own literal ordering
  (F12's own spec text only asks for "DO body must be net-zero").
- `machine::stack_effect` (`machine/dictionary.hpp`, F9) gained a defaulted
  `operator==` (hidden friend) — needed for `stack_effect.test.cpp`'s own
  assertions and the new `elaborate.test.cpp` assertion; no other change to
  that struct.
- **Merge criteria** (`static_assert`, immediately-invoked-lambda pattern,
  each mirrored as a `TEST_CASE`, in `stack_effect.test.cpp`): positive —
  `SQUARED`'s and `ABS`'s declared effects verified against their computed
  ones; a correct multi-word program (`DOUBLE`/`QUADRUPLE`, no declared
  effect, composed through two `core_call` lookups); `RECURSE` trusting a
  declared effect. Negative — one failing `static_assert` per diagnosis 1-5
  (`IF`/`ELSE` mismatch; `DO` body nonzero; `BEGIN...UNTIL` wrong flag count;
  `BEGIN...WHILE` condition wrong flag count; return-stack imbalance both
  across a control boundary and at a definition's end; `EXIT` inside `DO`;
  declared-vs-computed mismatch) — each checks the exact message and
  position. Plus pure-lattice `static_assert`s for `combine_sequential`,
  `combine_branch`, the primitive tables, and `parse_declared_effect`,
  independent of the full read→elaborate pipeline.
- Updated `docs/compiler_architecture.org`'s "Phase 4: Elaboration" section
  with the stack-effect-checking half (marked `DRAFT — pending author
  revision`, one sentence per line): the lattice paragraph, the
  sequential/branch composition paragraphs (including the net-effect-not-
  full-pair design note), the `RECURSE`/`EXIT` handling notes, and three UUID
  transclusions from `stack_effect.hpp` (the lattice + composition
  functions, `analyze_body`, `analyze_colon_effect`).
- Verified on both `gcc-16` and `clang-21` (both available in this worker's
  sandbox): `make compile`, `make test` (168/168 passed, up from 155/155 at
  F11's baseline), `make lint`, and `smoke.sh gcc-16`/`smoke.sh clang-21`
  both green/`SMOKE OK` on each toolchain.
- **`make lint` note:** the same recurring pre-existing `clang-format`/
  `gersemi` tooling-version drift flagged at every step since F6
  (`machine/{CMakeLists.txt,data_space.hpp}`, `reader/forth_chars.test.cpp`
  in this run) reproduced again, confirmed via `git checkout --` + rerun
  with zero F12 changes present; this step's own new/touched files
  (`elaborator/stack_effect.{hpp,test.cpp}`, `elaborator/elaborate.hpp`,
  `elaborator/elaborate.test.cpp`, `elaborator/CMakeLists.txt`,
  `machine/dictionary.hpp`) were independently confirmed clean via a
  targeted `pre-commit run clang-format --files ...`. Still an open,
  standing environment item, not specific to F12.
- DIV-0006 filed (see above); no other divergence from `docs/forth-plan.md`
  or Forth-2012 semantics.

## Step F13 — Direct evaluator

- Added `src/smd/forth/machine/{eval_direct.hpp,eval_direct.test.cpp}`,
  namespace `smd::forth::machine` (not `smd::forth::elaborator`), canonical
  include `<smd/forth/machine/eval_direct.hpp>` — this is the plan's own
  placement, not a mistake: `eval_direct.hpp` depends on
  `elaborator::compiled_unit` the same direction every `elaborator/*.hpp`
  header already depends on `machine/*.hpp` for `dictionary`/`forth_state`,
  and both directories fold into the one single build target
  (`compile-time-forth.forth`), so this is a documented one-way layering
  choice, not a new library boundary or a cycle. Header folds into the
  existing `forth_forth_headers` `FILE_SET`; test source added to the
  existing `machine_test` executable via `src/smd/forth/machine/
  CMakeLists.txt`. This is the pipeline's first full read → elaborate → eval
  milestone, per the plan's own framing.
- **The evaluator is a structural-recursive reference interpreter over the
  elaborated core, not a codegen+VM** — it walks `core_node`/`core_body`
  directly by mutual recursion (`eval_body`/`eval_node`, forward-declared
  exactly like `elaborate_body`/`elaborate_body_item` (F11) and
  `analyze_body` (F12) before them), never flattening anything into an
  instruction array. This is deliberate: F14's stack-machine codegen+VM is
  checked *against* F13's own results (the plan's own F14 merge criterion
  runs the same F13 test programs through both backends and requires
  agreement), so F13 has to exist as an independently-derived oracle before
  F14's flattening/back-patching logic has anything to be checked against.
- **`core_prim` runs through `machine::apply_primitive`** (F8's imperative
  implementation, the one that actually mutates a `forth_state`) — **never**
  through F12's `elaborator::primitive_data_effect` table, which exists
  purely for abstract interpretation and has no runtime behavior at all. This
  is called out explicitly in the header's own doc comment because the two
  tables/functions look similar enough (both keyed on `machine::primitive`)
  to invite confusion.
- **`core_call` recursion**: `unit.dictionary.entry_at(word_index)` to get
  the `colon_word`, `std::get_if<colon_word>` to confirm the binding kind (a
  defensive check — every `core_call`'s `word_index` was resolved against a
  `colon_word` binding at elaboration time, but the evaluator does not take
  that on faith), then `unit.arena.get(colon_word.core_id)` and
  `std::get_if<core_seq<...>>` to reach the callee's own item list, which
  `eval_body` then walks — exactly the same lookup chain F12's `analyze_body`
  already used for the same purpose (finding a call target), just fetching
  the actual body instead of a precomputed effect.
- **`eval_signal` is the runtime early-return channel** (`enum class
  eval_signal { normal, exited }`, returned inside `foundation::result<
  eval_signal>` from both `eval_body` and `eval_node`): `core_exit` produces
  `exited` directly; every control-structure handler (`core_if`'s two arms,
  `core_begin_until`'s and `core_begin_while`'s loop bodies) checks its
  nested `eval_body` call's returned signal and, on `exited`, immediately
  returns `exited` itself without evaluating anything else in its own body —
  this is what makes the signal unwind through arbitrarily many nested
  control structures within one definition. `core_call`'s handling is where
  the signal is absorbed: whatever the callee's own top-level `eval_body`
  call returned (`normal` or `exited`, it does not matter which), the caller
  always reports `normal` for the call itself, since a call is its own fresh
  definition boundary and the callee's `EXIT` must never unwind past it. This
  is **not** a C++ exception — the whole thing is threaded through the
  evaluator's own `foundation::result` return channel, per the project's
  one-shot/dynamic-extent nonlocal-control invariant. A bare top-level `EXIT`
  cannot reach `eval_program` at all (F11's `elaborate_word_ref` already
  diagnoses `"EXIT outside a definition"` at elaboration time), but `EXIT`
  nested inside a top-level `IF`/`BEGIN` is legal and does reach
  `eval_program`'s own top-level `eval_body` call — it is simply treated the
  same as `normal` there, since the top-level program has nothing further of
  its own to skip past either way.
- **`core_if`/`core_begin_until`/`core_begin_while` are real control flow**:
  `core_if` pops the flag itself (Forth's own `IF` semantics) and evaluates
  exactly one arm; `core_begin_until` evaluates its body, pops the flag
  UNTIL's own semantics expect, and loops until that flag is true (nonzero);
  `core_begin_while` evaluates its condition body first (every iteration,
  including the first), pops the flag, stops the loop on false, otherwise
  evaluates the loop body and repeats. Verified as real repetition (not just
  depth prediction) by `EvalDirectTest - WhileLoopIsRealRepetition`
  (`: UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ;`, actually counts to 3) and by
  the `COUNTDOWN` merge criterion itself.
- **`core_do_loop` is diagnosed, not evaluated** — deferred to F17 exactly as
  handoff-next.md's F13 briefing predicted: there is no core-level node that
  pops a `DO` loop's `(limit start)` pair or exposes `I`/`J` yet (F17's own
  `do_setup`/`I`/`J`/`LEAVE`/`UNLOOP` deliverable), so `eval_node` returns a
  diagnosed error (`"DO...LOOP evaluation is not implemented until F17"`)
  rather than silently running the body some undefined number of times. Not
  a new DIV — F12's own handoff section already declined to file one for the
  same underlying gap ("not a DIV, since no core node represents that
  consumption yet"), and F13 follows the same reasoning. Note a top-level
  `DO...LOOP` is not even stack-effect-checked (F12's `analyze_colon_effect`
  only runs for colon-definition bodies, never the top-level program body),
  so a program like `5 0 DO LOOP` elaborates successfully and only fails at
  evaluation time — exercised by `EvalDirectTest -
  DoLoopIsDiagnosedNotImplemented`.
- **Fuel design**: a single decrementing `int` (`consume_fuel(int &fuel,
  foundation::source_pos pos)`), called once per `eval_node` invocation —
  i.e. once per core node *visited*, so a loop that revisits the same node
  every iteration spends fuel every time, not just once. Exhaustion is a
  diagnosed `foundation::parse_error{pos, "evaluation budget exhausted"}`,
  carrying whichever node's own position was being evaluated when the budget
  ran out — never a hang, verified by the `SPIN` merge criterion (`: SPIN
  BEGIN FALSE UNTIL ; SPIN` with `fuel = 10`) actually terminating with a
  diagnosed error rather than looping forever. `eval_program`'s own `fuel`
  parameter defaults to `100000`, generous enough for every merge-criterion
  program at its default; every test that needs a tight budget passes one
  explicitly.
- **D10's output words (`.`, `.S`, `EMIT`, `CR`) and `1-` did not exist as
  runtime primitives before this step** — F8 never wired them despite D10's
  prose describing their behavior, and `1-` was missing from
  `docs/forth-plan.md`'s own word-scoping table entirely, even though the
  plan's own F13 `COUNTDOWN` merge-criterion text uses both `.` and `1-`
  verbatim. This step adds five new `machine::primitive` enumerators (`dot`,
  `dot_s`, `emit`, `cr`, `one_minus`), wires all five through
  `apply_primitive` (`machine/forth_state.hpp`) and `default_dictionary`
  (`machine/dictionary.hpp`), and gives each a
  `elaborator::primitive_data_effect` table entry (`elaborator/
  stack_effect.hpp`) so F12's stack-effect checker still handles every
  primitive exhaustively. **DIV-0007** records this in full
  (`docs/divergences/DIV-0007-f13-output-words-and-one-minus.md`, status
  `accepted-permanent`). The dictionary grew from 37 to 42 entries; every
  test/doc comment that counted dictionary size or "the N primitives" was
  updated alongside (`machine/dictionary.test.cpp`, `elaborator/
  elaborate.test.cpp`, `elaborator/elaborated_core.hpp`, `elaborator/
  stack_effect.hpp`). `.S`'s exact rendering (bottom-to-top, one
  `emit_cell`-formatted decimal per stack cell, nondestructive) is this
  step's own implementation-defined choice per Forth-2012, not exercised by
  any F13 merge criterion, only by `forth_state.test.cpp`'s own direct
  `apply_primitive` tests. `1+` is deliberately **not** added this step
  (nothing F13 needs uses it) — left for whichever step first needs it
  (most likely F17, per its own `SUMTO` example in the plan).
- **DIV-0006's predicted divergence is now directly observable at runtime**:
  `: F DUP 0< IF EXIT THEN DROP ;` elaborates successfully (F12's checker
  computes one effect for the whole definition without reconciling the
  early-`EXIT` path against the fall-through path, exactly as DIV-0006
  documents), and F13 actually executes both paths and observes the
  differing depths DIV-0006 predicts: `-3 F` leaves one cell (`-3`, `EXIT`
  fires before `DROP`), `7 F` leaves zero cells (`DROP` runs on the
  fall-through path) — both exercised directly by
  `EvalDirectTest - ExitUnwindsOnlyToItsOwnCallBoundary`'s sibling
  static_asserts. This is expected behavior matching the documented
  limitation, not a new bug; DIV-0006 itself was not modified (its own
  "Revisit condition" is unchanged by this observation).
- **Merge criteria** (`static_assert`, immediately-invoked-lambda pattern,
  each mirrored as a `TEST_CASE`, in `eval_direct.test.cpp`, run through the
  whole read → elaborate → eval pipeline via a local `run_program(source,
  fuel)` helper): `SQUARED` (`: SQUARED DUP * ;  4 SQUARED` → stack `[16]`);
  `ABS` (`: ABS DUP 0< IF NEGATE THEN ;  -7 ABS` → stack `[7]`); `COUNTDOWN`
  (`: COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN` → stack
  `[]`, output `"3 2 1 "`, both checked); budget exhaustion (`SPIN`, `fuel =
  10`, diagnosed rather than hung). Plus additional coverage beyond the
  plan's own four: EXIT unwinding to exactly its own call boundary (both the
  DIV-0006 pair above and a two-definition `INNER`/`OUTER` case proving a
  callee's `EXIT` does not escape past the call that invoked it),
  `BEGIN...WHILE...REPEAT` as real repetition, and `DO...LOOP`'s diagnosed-
  not-evaluated status.
- Verified on both `gcc-16` and `clang-21` (both available in this worker's
  sandbox): `make compile`, `make test` (**178/178** passed, up from 168/168
  at F12's baseline), `make lint`, and `smoke.sh gcc-16`/`smoke.sh clang-21`
  all green/`SMOKE OK` on each toolchain.
- **`make lint` note**: unlike every prior step since F6, this run reported
  **zero** pre-existing drift files (`machine/{CMakeLists.txt,
  data_space.hpp}`, `reader/forth_chars.test.cpp` were flagged at F11/F12
  and every step before) — `clang-format` did reformat this step's own
  touched files on the first `make lint` run (mechanical only: e.g.
  `default_dictionary`'s word-list array got reflowed to two columns after
  growing to 42 entries), and a second run then reported clean. The
  standing tooling-version-drift item flagged in every handoff-next.md since
  F8 appears to have resolved itself, most likely because `c9bbacb reformat
  via make lint` (the commit immediately preceding this step, per `git log`)
  already normalized the whole tree; still worth re-checking at the next
  step rather than assuming it stays resolved.
- Updated `docs/compiler_architecture.org`'s "Phase 5: Codegen and the Three
  Backends" section, replacing the placeholder sentence with real prose
  (marked `DRAFT — pending author revision`) plus two UUID transclusions
  from `eval_direct.hpp` (the `eval_signal` type plus the mutually recursive
  `eval_body`/`eval_node` walk; the `eval_program` entry point).
- DIV-0007 filed (see above); DIV-0006 unmodified but its predicted
  divergence is now directly observable at runtime (see above); no other
  divergence from `docs/forth-plan.md` or Forth-2012 semantics.

## Step F14 — Stack-machine codegen and VM

Done in worktree `wt-f14` / branch `step/f14`.
Three new files under `src/smd/forth/machine/`: `instruction.hpp`
(`op`/`instr`/`compiled_program`), `codegen.hpp` (`codegen`, flattens an
`elaborator::compiled_unit` into a `compiled_program`), `vm.hpp` (`run`,
executes a `compiled_program` against a `forth_state`) — plus a `.test.cpp`
for each, wired into the existing `machine_test` executable via
`src/smd/forth/machine/CMakeLists.txt`.

- **`op`/`instr`**: `enum class op : std::uint8_t` with all seventeen
  enumerators the plan names verbatim (`push, prim, call, ret, branch,
  branch0, do_setup, loop_step, plus_loop_step, push_index, leave, unloop,
  push_xt, execute, catch_mark, throw_op, halt`); `struct instr { op code;
  cell operand; }`, a plain trivially-copyable aggregate. Only eight
  opcodes have real codegen+VM behavior this step: `push`, `push_xt`,
  `prim`, `call`, `ret`, `branch`, `branch0`, `halt`. The other nine
  (six `DO...LOOP`-shaped opcodes reserved for F17; `execute`/
  `catch_mark`/`throw_op` reserved for F18a) exist in the enum but are
  never emitted by `codegen` and are diagnosed (a `foundation::parse_error`,
  never UB per D7) if the VM ever encounters one — **DIV-0008** records this
  choice (`docs/divergences/DIV-0008-f14-deferred-opcodes-and-capacity-
  fields.md`, status `accepted-permanent`), picking option (a) from
  handoff-next.md's F14 briefing (diagnose `core_do_loop` at codegen time,
  the conservative F13-consistent choice) over option (b) (emit placeholder
  semantics now). `branch0` pops a flag and branches when it is **zero**
  (Forth false) — the natural encoding for both `IF`'s "skip THEN's arm
  when false" and `BEGIN...UNTIL`'s "loop back while still false."
- **`compiled_program<MaxCode, MaxWords>`**: a literal type, trivially
  copyable and trivially destructible (checked by `static_assert` against
  the header's own default instantiation, mirroring `elaborated_core.hpp`'s
  and `dictionary.hpp`'s identical checks) — "THE artifact that survives to
  runtime," per the plan's own wording. Fields: `code`
  (`foundation::static_vector<instr, MaxCode>`, the flat program);
  `entry_points` (`foundation::static_vector<int, MaxWords>`, one slot per
  dictionary entry in dictionary-index order — a colon word's slot holds
  its body's starting instruction index, every other binding kind's slot
  holds `-1` since primitives/variables/constants are inlined at each
  reference site rather than called); `program_entry` (instruction index
  where the top-level program body starts, run after every colon
  definition's own code); `data_space_size` (real, `unit.data_space.size()`
  at codegen time — not yet consumed by the VM itself, F16 wires
  `@`/`!`/`+!`); `required_stack_depth`/`required_return_depth` (both
  default `-1`, "not computed" — see DIV-0008, F12 computes per-definition
  net effect and minimum entry depth, not a whole-program peak, so this
  step does not invent a new analysis to populate these). `MaxWords`
  deliberately reuses the source `compiled_unit`'s own `MaxWords` template
  parameter rather than introducing a second capacity — `entry_points`
  needs exactly one slot per dictionary entry, and the dictionary is
  already capped at `MaxWords`, so no extra parameter was needed.
- **`codegen(compiled_unit) -> result<compiled_program>`**: walks
  `unit.dictionary` in index order (not the arena/tree structure directly),
  emitting each colon word's own body before moving to the next dictionary
  entry, then the top-level program body, then a trailing `halt`. Recording
  `entry_points[i]` **before** emitting word `i`'s own body (not after) is
  what makes both ordinary calls and `RECURSE` self-calls resolve without
  any back-patching: F11's static-binding discipline guarantees a
  `core_call`'s `word_index` is always either an earlier dictionary entry
  (already emitted, since dictionary order is walked forward) or the
  current entry itself (`RECURSE` — already in the table by the time its
  own self-reference is reached). This is a **one-pass** design: unlike
  `eval_direct.hpp`'s lazy structural recursion (which only ever visits a
  callee when actually called), codegen unconditionally emits code for
  every colon word in the dictionary, reachable or not, in one linear walk.
  Back-patching (the plan's own words: "computed by back-patching inside
  codegen") is reserved for what genuinely needs it — a control structure's
  own *forward* branch (`IF`'s `branch0`/skip-`ELSE` `branch`,
  `BEGIN...WHILE`'s exit `branch0`): emit a placeholder operand of `0`,
  remember the instruction's own index, then once the real target
  instruction has actually been emitted, index directly into `out.code` to
  overwrite the placeholder. Backward branches
  (`BEGIN...UNTIL`'s/`BEGIN...WHILE`'s own jump back to the loop's start)
  never need this, since the loop-start instruction was already emitted
  before the branch that targets it. `core_exit` compiles straight to `ret`
  — no signal-propagation machinery needed on the codegen side at all,
  unlike `eval_direct.hpp`'s `eval_signal`. `core_do_loop` is diagnosed
  ("DO...LOOP codegen is not implemented until F17"), mirroring F13's
  identical choice for the direct evaluator.
  `codegen_emit_body`/`codegen_emit_node` are the mutually-recursive walk
  pair, directly modeled on `eval_direct.hpp`'s `eval_body`/`eval_node` —
  same closed `core_node` variant, `std::visit`/`if constexpr` dispatch,
  same defensive `core_seq`-is-unreachable arm — just emitting `instr`s
  into a flat array instead of executing side effects immediately.
- **`run(compiled_program const&, forth_state&, fuel) -> status`**: an
  explicit loop over an instruction pointer (`ip`), starting at
  `program.program_entry`, using `forth_state::returns()` as the call/return
  stack exactly as Forth convention calls for (the same register F17 will
  later reuse for loop parameters). `call` pushes `ip + 1` (the return
  address) onto the return stack and jumps; `ret` pops a return address and
  jumps to it — this is the entire runtime shape of `EXIT`, and it needed
  **no propagation logic of its own anywhere else** in the loop, unlike
  `eval_direct.hpp`'s `eval_signal` threading through every nested control
  handler. This is exactly the simplification handoff-next.md's F14
  briefing predicted ("should end up simpler than F13's eval_signal
  propagation... not harder, since the call/return stack already does the
  unwinding for free"). `branch`/`branch0` set `ip` directly (`branch0`
  first popping a flag, branching only when it is zero). `push`/`push_xt`
  both push `instr::operand` as a plain `cell` (identical runtime behavior,
  kept as distinct opcodes per the plan's own enum, for documentation/
  extensibility — `push_xt` is **not** one of DIV-0008's deferred opcodes;
  it already has real behavior this step, matching `core_push_xt`'s
  existing F11/F13 treatment of an execution token as "just a cell," with
  `EXECUTE` itself, the opcode that actually *acts* on that cell, deferred
  to F18a). `prim` calls `apply_primitive` exactly as `eval_direct.hpp`'s
  `core_prim` case does. The nine reserved opcodes each return a diagnosed
  `foundation::parse_error` if reached (unreachable in practice, since
  codegen never emits them). **Fuel**: a single decrementing `int`
  (`consume_vm_fuel`), consumed once per instruction *fetched* — the VM-
  level analogue of `eval_direct.hpp`'s "once per core node visited"
  (`instr` carries no `source_pos`, unlike a core node, so the diagnosed
  exhaustion error carries a default/empty `source_pos` rather than a real
  one — a real, if minor, diagnostic-quality gap relative to F13's own
  fuel-exhaustion errors, not filed as a DIV since the plan does not
  require positional fuel diagnostics and no merge criterion checks the
  position). `run`'s own `fuel` parameter defaults to `100000`, matching
  `eval_program`'s own default.
- **The survives-to-runtime proof** (`vm.test.cpp`): every one of F13's own
  merge-criterion programs (`SQUARED`, `ABS`, `COUNTDOWN`, `SPIN`'s budget
  exhaustion) plus two more for parity with `eval_direct.test.cpp`'s wider
  coverage (`UPTO3`'s `BEGIN...WHILE...REPEAT`, and the `INNER`/`OUTER`
  EXIT-unwinds-to-its-own-call-boundary pair) is compiled once into a
  `constexpr compiled_program` **declared at namespace scope**
  (`squared_program`, `abs_program`, `countdown_program`, `spin_program`,
  `upto3_program`, `exit_boundary_program`), via a local `compile(source)`
  helper mirroring `eval_direct.test.cpp`'s `run_program` (parse → elaborate
  → codegen, `.value()` at every stage — a hard compile error, not a
  runtime failure, if any stage fails for one of these known-good programs).
  Each namespace-scope program is then run through `run()` twice against
  the *same* object: once inside a `static_assert` (an immediately-invoked
  lambda constructing a fresh `forth_state` and calling `run`) at compile
  time, and once inside a `TEST_CASE` at ordinary runtime — proving the
  literal compiled artifact works both ways, not just that the same source
  text happens to compile identically twice. This is F14's own merge
  criterion, satisfied verbatim.
- **`codegen.test.cpp`** additionally checks codegen's own output shape
  directly (not just end-to-end results through the VM): a colon
  definition's code ends with `halt` only at the very end of the whole
  program (colon bodies end with `ret`), a `call` instruction's operand
  equals the callee's own recorded `entry_points` slot, primitive/
  variable/constant dictionary entries have `entry_points[i] == -1`, `IF`
  compiles to a genuinely forward `branch0` (`operand > i`), and
  `DO...LOOP` is diagnosed at codegen time exactly as `eval_direct.hpp`
  diagnoses it at evaluation time.
- **Word-table shape** (an open design call per handoff-next.md's F14
  briefing): a plain `foundation::static_vector<int, MaxWords>` indexed by
  dictionary `word_index`, exactly the minimal choice handoff-next.md
  anticipated, mirroring `machine::dictionary`'s own flat, index-addressed
  design.
- **`required_stack_depth`/`required_return_depth` are not computed** (see
  DIV-0008 above) — a real scope cut relative to the plan's literal "also
  carries... required stack capacities" wording, recorded rather than
  silently approximated. Every caller (F15's public API, F16, F17, F18a)
  must still size its own `forth_state` generously by hand, exactly as
  F13's own tests already do.
- Verified on both `gcc-16` and `clang-21` (both available in this worker's
  sandbox): `make compile`, `make test` (**194/194** passed, up from
  178/178 at F13's baseline — 16 new tests across the three new test
  files), `make lint` (clean on the **first** run, no reformatting, tree
  stayed unmodified — the tooling-version-drift item first resolved at F13
  stayed resolved), and `smoke.sh gcc-16`/`smoke.sh clang-21` both
  `SMOKE OK`.
- Updated `docs/compiler_architecture.org`'s "Phase 5: Codegen and the
  Three Backends" section (F14 extends the existing section, per
  handoff-next.md's own guidance — it does not get a new heading) with a
  new "The classic stack machine (step F14)" subsection and five UUID
  transclusions: the `op`/`instr` pair and `compiled_program` (both from
  `instruction.hpp`), `codegen_emit_node` and `codegen` itself (both from
  `codegen.hpp`), and `run` (from `vm.hpp`).
- DIV-0008 filed (see above); no other divergence from
  `docs/forth-plan.md` or Forth-2012 semantics. DIV-0006 and DIV-0007
  unmodified, both still relevant background (DIV-0006's `EXIT`-inside-`IF`
  gap is directly observable through the VM too, by construction — the VM
  runs the same elaborated core's semantics as F13, just flattened first;
  not re-verified by a dedicated F14 test since F13's own `eval_direct.
  test.cpp` already covers the same source text and the VM/direct-evaluator
  agreement is exactly what `vm.test.cpp`'s shared expected values are
  checked against).

## Step F15 — Public one-shot API

Done in worktree `wt-f15` / branch `step/f15`. Replaced the placeholder
`src/smd/forth/forth.hpp`/`forth.cpp`/`forth.test.cpp` (F0's bootstrap
`forth::forth()` returning `"Steve"`) with the real public one-shot entry
point: `smd::forth::source_literal<N>`, `smd::forth::forth_program<...>`,
and `smd::forth::compiled_forth<Source, ...>`, all in `forth.hpp`.
`forth.cpp` is **deleted**, not replaced — the new API is entirely
header-only (see "Library target is now header-only" below).

- **`source_literal<N>`**: reimplemented (not imported) under `smd::forth`
  from the pattern at `~/src/compile-time-scheme/main/src/smd/smdscheme/
  smdscheme.hpp` (docs/forth-plan.md section 5, "reuse as pattern only") —
  same `char text[N]{}` + `constexpr source_literal(char const (&)[N])` +
  `.view()` (`{text, N - 1}`, dropping the trailing null) shape, this
  project's own file-prolog/guard/namespace conventions.
- **`detail::compile_program<MaxCode, MaxNodes, MaxBody, MaxName, MaxDepth,
  MaxWords, MaxData, MaxWarnings>(source) -> result<compiled_program<...>>`**
  (an internal helper, `smd::forth::detail`, not part of the public
  surface): composes `reader::read_program` -> `elaborator::elaborate` ->
  `machine::codegen`, threading `source` to both `read_program` and
  `elaborate` (F12's declared-effect re-slicing needs the original text a
  second time). Short-circuits on the first stage that fails, returning
  its `parse_error` unchanged.
- **`forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
  MaxOut>`**: a new public class (not a modification to F14's own
  `machine::compiled_program`, which stays a plain data-only struct) that
  wraps an already-compiled `machine::compiled_program` and adds the
  `.run()`/`.stack()`/`.output()` surface the plan names. Its constructor
  is `explicit`, takes a `machine::compiled_program<MaxCode, MaxWords>` by
  value, and does no compilation of its own — `compiled_forth<Source>` is
  what actually runs the pipeline and constructs one.
  - **`.run(fuel = 100000) -> result<forth_state<...>>`**: constructs a
    fresh `forth_state`, calls `machine::run` against it, and returns
    either the resulting state or the propagated `parse_error`. This is
    the plan's own literal signature (`.run() -> result<forth_state>`).
  - **`.stack(fuel = 100000) -> static_vector<cell, StackDepth>`** and
    **`.output(fuel = 100000) -> static_vector<char, MaxOut>`**: the two
    "open design call" convenience accessors (handoff-next.md's own
    framing). Chosen shape: a `foundation::static_vector` snapshot, **not**
    wrapped in `result<...>` — unlike `.run()`, a runtime failure does not
    prevent a result: both call `machine::run` against a fresh state and
    discard its own `status`, then read back whatever the state's data
    stack/output buffer holds regardless of whether execution completed.
    This is a deliberate, documented choice (see the doc comments in
    `forth.hpp`): `machine::run` mutates `state` in place even when it
    returns an error partway through, so "best effort" is a real,
    inspectable answer, not a lie — a caller that needs to distinguish
    "completed" from "stopped early with an error" uses `.run()` itself.
    `.stack()`'s cell order is bottom-to-top (index 0 is the deepest
    cell), matching `machine::primitive::dot_s`'s own printed order
    (`machine/forth_state.hpp`) — chosen so a caller who already knows
    Forth's `.S` convention gets the same order here, not its reverse.
    `compiled_forth<": SQUARED DUP * ;  4 SQUARED">.stack()` therefore
    reads as a one-element `static_vector` equal to `{16}` — the plan's
    own merge-criterion wording ("static-asserts to `[16]`"), verified via
    `.size() == 1 && s[0] == 16` in both a `static_assert` and a matching
    `TEST_CASE` (`forth.test.cpp`).
  - **Each of `.run()`/`.stack()`/`.output()` runs the whole program from
    scratch**, every single call: `forth_program` carries no mutable
    execution state of its own (it cannot — `compiled_forth<Source>` is a
    namespace-scope `constexpr` *value*), so calling more than one
    accessor on the same object re-executes the program that many times.
    Documented as a deliberate choice in `forth.hpp`'s own doc comments,
    matching handoff-next.md's own "Known open items" framing exactly:
    correct and cheap for the plan's own merge-criterion-sized programs,
    not something a caller running a large program in a hot loop should
    do without noticing.
- **`compiled_forth<Source, MaxCode = 4096, MaxNodes = 1024, MaxBody = 64,
  MaxName = 32, MaxDepth = 32, MaxWords = 256, MaxData = 1024, MaxWarnings
  = 64, StackDepth = 64, RStackDepth = 64, MaxOut = 4096>`**: the public
  one-shot variable template. **Capacity-parameterization decision**: every
  pipeline-stage capacity is exposed as its own template parameter, in
  keeping with D2 ("all capacities are template parameters with
  defaults") — no DIV filed, since this is the D2-compliant choice, not a
  divergence from it. The first eight defaults after `Source` are exactly
  `reader::read_program`'s, `elaborator::elaborate`'s, and
  `machine::codegen`'s own existing defaults (1024/64/32/32/256/1024/64,
  plus codegen's own 4096 for `MaxCode`); `StackDepth`/`RStackDepth`/
  `MaxOut` are new to this step (no prior default existed for a
  `forth_state`'s own runtime capacities anywhere in the codebase — only
  ad hoc small test-local values), chosen generously (64/64/4096) for a
  one-shot caller who has not sized a `forth_state` by hand.
  `forth.test.cpp`'s `CapacitiesAreOverridable` test instantiates
  `compiled_forth<"...", /*MaxCode=*/512>` to prove overriding is real, not
  merely declared.
  A `Source` that fails to parse, fails elaboration, or overflows any
  capacity is a **hard compile error**: `detail::compile_program(...)
  .value()` on a failed `result` throws inside a `constexpr` initializer
  (`std::get` on the wrong `std::variant` alternative), so the whole
  translation unit fails to compile. This is the same discipline
  `eval_direct.test.cpp`'s and `vm.test.cpp`'s own namespace-scope
  `constexpr` programs already exploited for *known-good* inputs (F13/F14);
  F15 is the first step to exploit the *failure* side on purpose, as the
  public contract (see the negative-compile test below).
- **Library target is now header-only**: F15 deletes `forth.cpp`, the
  library's *only* remaining `.cpp` (every other component under
  `src/smd/forth/` was already header-only). This left
  `compile-time-forth.forth` — declared `add_library(... STATIC "")` at the
  top-level `CMakeLists.txt` — with zero compiled sources, which CMake
  cannot link (`"CMake can not determine linker language for target"`).
  Fixed with `set_target_properties(compile-time-forth.forth PROPERTIES
  LINKER_LANGUAGE CXX)` immediately after the `add_library` call, rather
  than reclassifying the target as `INTERFACE` (AGENTS.md reserves
  `INTERFACE` libraries for "truly header-only code," but this target is
  the project's own stable home for a real compiled source the moment one
  reappears — F18's sender/receiver driver or F19's foreign-function glue
  are both plausible candidates) or reintroducing a dummy `.cpp` (adds a
  translation unit with no purpose). This is a top-level `CMakeLists.txt`
  change outside `src/smd/forth/`'s own directory, but a forced,
  documented consequence of deleting `forth.cpp` as instructed, not scope
  creep.
- **Negative-compile-test infrastructure (new)**: `src/smd/forth/
  test_neg_syntax_error.cpp` is a full, syntactically valid C++
  translation unit whose own `compiled_forth<": UNCLOSED DUP *">`
  instantiation is expected to fail (the source never closes its colon
  definition, so `reader::read_program` diagnoses "unterminated
  definition (no ;)", and `.value()` on that failure is the hard-compile-
  error path described above). Checked first: neither this repository nor
  `~/src/compile-time-scheme/main` had a *working* "must fail to compile"
  mechanism to import — compile-time-scheme's own `test_neg_*.cpp` files
  (`foundation/test_neg_source_literal.cpp`,
  `foundation/test_neg_static_vector.cpp`,
  `parser/test_neg_parser_concept.cpp`) are unwired snippets, not attached
  to any CMake target and not even valid standalone translation units
  (each is a dangling `return 0; }` with no enclosing function) — so this
  is genuinely new infrastructure, introduced ahead of its originally
  planned home (docs/forth-plan.md Step F21 names the same `test_neg_*.cpp`
  pattern for its own three negative tests, one of which — "syntax error
  in `compiled_forth` NTTP source" — is exactly this test) because F15's
  own merge criterion needs a working mechanism now. Mechanism: an
  `EXCLUDE_FROM_ALL` executable target (`forth_test_neg_syntax_error`,
  `src/smd/forth/CMakeLists.txt`) that never participates in `make compile`
  /`make all`'s own `all` target, plus a CTest test
  (`forth.negative_compile_syntax_error`) whose `COMMAND` is
  `${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
  forth_test_neg_syntax_error --config $<CONFIG>` and whose `WILL_FAIL
  TRUE` property inverts the usual pass/fail sense: the CTest test *passes*
  exactly when that build *fails*. Verified by hand (not just via CTest):
  building the target directly reproduces the expected diagnostic,
  `error: uncaught exception of type 'std::bad_variant_access'; 'what()':
  'std::get: wrong index for variant'` at `forth.hpp`'s own
  `compiled_forth` initializer, instantiated from `test_neg_syntax_error.
  cpp`'s own line — exactly the failure mode the doc comments describe.
  Not attempted: matching the diagnostic's exact text via CTest's
  `PASS_REGULAR_EXPRESSION`/`FAIL_REGULAR_EXPRESSION` (AGENTS.md's own "match
  specific diagnostics in negative compile tests" rule) — the actual
  compiler-emitted text for "`.value()` threw in a constexpr context" is a
  `std::bad_variant_access` diagnostic chain, not `forth`'s own
  `"unterminated definition (no ;)"` message, and its exact wording is
  compiler-version-specific (gcc-16 and clang-21 do not word it
  identically); regex-matching it would overfit to one toolchain's message
  text rather than testing the real contract ("this does not compile").
  Documented here as a considered, deliberate gap for F21 (or a future
  worker) to revisit if per-diagnostic matching becomes valuable enough to
  justify the toolchain-specific brittleness.
- **Examples**: `src/examples/hello.cpp` now runs `": SQUARED DUP * ;  4
  SQUARED  ."` through `compiled_forth` and prints `.output()`
  (`"Hello, Forth! SQUARED(4) prints: 16 "` — still starts with `"Hello,"`,
  matching `smoke.sh`'s own `case "$out" in Hello,*)` check unmodified).
  Added `src/examples/godbolt_forth.cpp`, the Godbolt single-file
  extraction example the plan asks for: modeled directly on
  `~/src/compile-time-scheme/main/src/examples/godbolt_arithmetic.cpp`'s
  own shape (one `compiled_*` namespace-scope constant, one `main` that
  inspects the result) — the only pattern reference available, since this
  repository had none yet. "Single-file extraction" here means literally
  that: `forth.hpp` is header-only as of this step, so this file's own
  contents plus the small number of headers it transitively includes under
  `src/smd/forth/` are the whole extraction, with no `forth.cpp` needed
  alongside it. Wired as an ordinary example target plus a CTest
  `PASS_REGULAR_EXPRESSION "SQUARED\\(6\\) = 36"` check
  (`src/examples/CMakeLists.txt`), the same convention
  `godbolt_arithmetic`'s own sibling `examples_godbolt_arithmetic` CTest
  entry uses.
- **`compile-time-forth.org`'s four transclusion anchors** (`44cc988c-...`
  in `forth.hpp`, `a66dec0e-...` in `forth.cpp`, `03013d1f-...` in
  `forth.test.cpp`, `710c39c6-...` in `hello.cpp`) pointed at the deleted
  placeholder. **Chosen: TODO note, not a four-anchor remap** — explicitly
  offered as a choice by docs/forth-plan.md's own Step F15 text. The old
  section assumed a declaration/definition/test/example four-way split
  that no longer exists now that `forth.hpp` is header-only (there is no
  `forth.cpp` "definition" to point a second anchor at), so forcing the old
  shape onto the new design would have meant either inventing an artificial
  split or dropping one of the four anchors silently. Instead,
  `compile-time-forth.org`'s "Examples of Transclusion" section now
  explains the removal, names all four dead anchors and why they no longer
  resolve, records a `TODO(F22, documentation consolidation)` (matching
  docs/forth-plan.md Step F22's own deliverable, "rewritten to transclude
  the real compiler via UUID anchors"), and keeps exactly one *live*
  transclusion (`source_literal`'s own real anchor,
  `18245977-b4d2-4011-bac7-a36f7680aeb4`) so the file is not left with zero
  working examples until F22 lands. No anchor pointing at nonexistent code
  remains in the file.
- **`docs/compiler_architecture.org`**: added a new "Phase 6: The Public
  One-Shot API (step F15)" top-level section (the existing "Phase 5"
  section is scoped to the three backends by its own heading, "Codegen and
  the Three Backends," so F15's public-API layer — which sits above all
  three backends, not inside them — gets its own phase rather than being
  squeezed into Phase 5). Three live transclusions: `source_literal`,
  `forth_program`, and `compiled_forth` (all three real anchors from
  `forth.hpp`).
- Every public constexpr API (`source_literal`, `compiled_forth`,
  `forth_program::run`/`stack`/`output`) has both an immediately-invoked-
  lambda `static_assert` and a matching Catch2 `TEST_CASE` in
  `forth.test.cpp`, per AGENTS.md's own rule — eight `static_assert`/
  `TEST_CASE` pairs in total, covering: the plan's own `[16]` merge
  criterion (`SquaredMergeCriterion`), a multi-cell stack snapshot proving
  bottom-to-top order (`StackIsBottomToTopSnapshot`), `.output()`
  (`OutputMergeCriterion`), `.run()` success (`RunSucceeds`), `.run()`
  propagating a genuine runtime failure rather than a hard compile error
  (`RunPropagatesRuntimeError`, budget exhaustion), and capacity
  overriding (`CapacitiesAreOverridable`).
- No DIV filed: the one project-wide rule this step could plausibly have
  diverged from (D2, "all capacities are template parameters") was instead
  *followed* by parameterizing every pipeline capacity on `compiled_forth`
  itself; every other design call above (accessor return shape, run-every-
  call semantics, negative-compile mechanism, org-anchor handling) is an
  open design choice the plan explicitly left to this step, not a
  divergence from a documented decision (D-something) or from Forth-2012
  semantics.
- Verified on both `gcc-16` and `clang-21` (both available in this
  worker's sandbox): `make compile` and `make test` (**202/202** passed on
  both toolchains, up from 194/194 at F14's baseline — 8 new `forth_test`
  cases, one new negative-compile CTest test, one new
  `examples_godbolt_forth` CTest test), `make compile-headers` green on
  `gcc-16` (all header sets, including `forth.hpp` itself, verify as
  self-contained), and `smoke.sh gcc-16`/`smoke.sh clang-21` both `SMOKE
  OK`. `git status` after all builds shows no unrelated files touched.
- **`make lint` could not be run to completion in this worker's sandbox**:
  `pre-commit run -a` aborts while provisioning the `markdownlint-cli` and
  `gitleaks` hook environments (`nodeenv`/Go binary downloads), both
  failing identically with `ssl.SSLCertVerificationError: ... Basic
  Constraints of CA cert not marked critical` — a sandbox-wide TLS/network
  restriction on installing new pre-commit hook environments, unrelated to
  any file this step touched (the same failure reproduces on a clean
  `pre-commit run -a` with no relation to which files changed). Verified
  individually instead, against every file this step added or changed:
  `clang-format` (clean after one auto-format pass, then clean on rerun),
  `gersemi` (clean after one auto-format pass on the three `CMakeLists.txt`
  files this step touched, then clean on rerun — no unrelated file was
  reformatted), `trailing-whitespace`, `end-of-file-fixer`, `check-yaml`,
  `codespell`, and `check-added-large-files` all pass clean. `checkmake`,
  `mbake-validate`, and `shellcheck` were not touched by this step's own
  changes (no Makefile or shell-script edits) and were not separately
  verified, since their own hook environments hit the same network
  restriction. A future worker with network access should re-run `make
  lint` in full to confirm; nothing above suggests this step's own files
  would fail those checks.
