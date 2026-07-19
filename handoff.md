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
