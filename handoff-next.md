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

## F2 complete

Step F2 (vendor Beman Execution) is done in worktree `wt-f2` / branch
`step/f2`. See `handoff.md` section "Step F2 — Vendor Beman Execution" for
the full facts (submodule URL + pinned commit, `BEMAN_USE_MODULES OFF` note,
`sender/vocab.hpp` details). `make compile`, `make test`, `make lint`, and
`smoke.sh` are green on gcc-16 and clang-21. Not merged by this worker — the
orchestrator merges. F3 (parallel, separate worktree) is unaffected; its
section above is unchanged by this note.

## F3 complete — what F4/F6/F8 need to know about the foundation API

F3 landed `src/smd/forth/foundation/` (namespace `smd::forth::foundation`,
canonical includes `<smd/forth/foundation/*.hpp>`). All 50 imported/authored
tests pass on `gcc-16` and `clang-21`; `make compile`, `make test`,
`make lint` and both `smoke.sh` runs are green. See `handoff.md`'s
"Step F3 — Import foundation" section for the full list of what was
imported, what was left out (`fix.hpp`, `version.hpp`), and DIV-0002 (a
`clang-21`-only constexpr-portability fix in `parse_error::operator==`, no
behavior change).

The API surface later steps build on:

- `static_vector<T, Capacity>`: fixed-capacity, constexpr-friendly vector.
  `push_back`, `size`, `empty`, `operator[]`, iteration via `begin`/`end`,
  structural `operator==`. `Capacity` has no default — always specify it.
- `source_pos{offset, line, column}` and `source_span{first, last}`: plain
  structs, defaulted equality, no behavior beyond that. F5's lexer will
  produce these; F7's grammar will attach them to parse errors and to
  captured stack-effect comment spans.
- `parse_error{where, message}`: `message` must be a string literal or other
  static-lifetime `char const *`. Equality now only ever compares pointers
  against `nullptr` (DIV-0002) — if you add new fields or a new equality
  operator anywhere in the imported tree, check whether it compares two
  possibly-distinct pointers to each other in a `constexpr`/`static_assert`
  context, not just against `nullptr`; `clang-21` will reject that in a
  constant expression even when `gcc-16` accepts it silently.
- `result<T>`: discriminated union of `T` or `parse_error`, built from
  `std::variant`. `has_value()`/`value()`/`error()`. This is the error type
  D7 machine primitives (F8) and D9 elaboration (F11) both use — `apply_primitive`
  and `elaborate` should return `result<...>` the same way the parser will.
- `arena_box<T, MaxNodes = 1024>`: a typed integer handle (`id_`, `-1` =
  null, `explicit operator bool`). `tree_arena<T, MaxNodes = 1024>`: a
  bump-allocator arena (`allocate`, `get` by `int` or by `arena_box`).
  `make_arena_box(arena, args...)` constructs in place and returns a handle.
  **Both template parameters now default to 1024** (added a default to
  `tree_arena` during F3 — it had none upstream, unlike `arena_box`); still
  always specify `MaxNodes` explicitly at each real use site the way the
  Scheme repo's own call sites do (they never relied on a default either).
  This is the D3 substrate: F6's syntax tree, F11's elaborated core, and
  F14's instruction program are each one `tree_arena` of a trivially
  destructible node type, referenced by `arena_box` handles — no `fix`/`Box`
  anywhere in that path.
- `functor<Impl>`/`fmap`, `applicative<Impl>`/`invoke`, `alternative<Impl>`/
  `alt`/`empty`: CRTP typeclass bases plus CPOs, dispatching through the
  `..._typeclass<T>` template-variable lookup. F4's parser combinators
  register `parser<...>` types against these three typeclasses (that's the
  "typeclass-object (`parser_v` CPO) machinery" the plan's import inventory
  mentions); no Forth-specific type registers against them yet. Note:
  `functor.hpp`/`applicative.hpp`/`alternative.hpp` had **no dedicated
  upstream tests** — F3 wrote new ones using small test-local instance types
  (not exported); F4 is the first step that registers a real, exported
  production type against these typeclasses, so its tests are the first
  place a typeclass-law regression would actually be caught project-wide.

CMake shape to match: foundation's headers are additional `FILES` in the
already-declared `forth_forth_headers` `FILE_SET` on the
`compile-time-forth.forth` target (no separate `compile-time-forth.foundation`
target); its tests build as the `foundation_test` executable, wired from
`src/smd/forth/foundation/CMakeLists.txt`, descended into via
`add_subdirectory(foundation)` in `src/smd/forth/CMakeLists.txt`. F4's
`parser/` directory should follow the same pattern unless it has a concrete
reason not to (e.g. a genuine need to link foundation as a distinct
compiled unit, which nothing here has needed so far since everything in
`foundation/` is header-only).

Dependencies satisfied by this merge: F3 -> {F4, F6, F8} per the plan's
parallelism summary. F4, F6, and F8 can each start once F3 is merged to
main; they are mutually parallel (separate worktrees), same as F2/F3 were.

## F4 complete — what F5/F7 need to know about the parser combinator API

Step F4 (import parser combinators) is done in worktree `wt-f4` / branch
`step/f4`. `src/smd/forth/parser/` (namespace `smd::forth::parser`,
canonical includes `<smd/forth/parser/*.hpp>`) is imported from
`~/src/compile-time-scheme/main/src/smd/smdscheme/parser/`. All 89
imported/authored tests pass on `gcc-16` and `clang-21`; `make compile`,
`make test`, both `smoke.sh` runs are green. See `handoff.md`'s "Step F4 —
Import parser combinators" section for the full import inventory, what was
left behind (Scheme char predicates, `test_neg_parser_concept.cpp`), and
DIV-0003 (the parser typeclass-object now derives from
`smd::forth::foundation`'s functor/applicative/alternative CRTP bases and
registers against foundation's own typeclass variables, rather than
reimplementing a separate local typeclass mechanism the way the Scheme
reference did).

**`make lint` note for whoever merges this**: `pre-commit run -a` currently
fails with a `clang-format` drift on a handful of F2/F3 files
(`foundation/{applicative,functor,parse_error,source_pos,static_vector}.hpp`,
`foundation/applicative.test.cpp`, `sender/vocab.test.cpp`) — reproduces on
the clean F3-merged baseline with none of F4's changes present (verified by
stashing this step's diff and re-running `make lint`), so it predates F4
and is out of this step's scope to fix (fixing it would mean reformatting
files this step never touched). No file under `src/smd/forth/parser/` is
ever touched by that same `clang-format` run. Whoever picks up formatting
hygiene next (or the orchestrator, at merge time) should decide whether to
take a dedicated small step to re-run `clang-format` repo-wide and commit
the result, or pin down why the resolved `clang-format` version drifted
from whatever produced the currently-committed formatting.

The API surface later steps build on:

- **Layer 0 (free functions, this is what F5/F7 will mostly use, per D6 —
  "the combinator library is the production parser"):**
  - `cursor` (`src/smd/forth/parser/cursor.hpp`): immutable input view +
    `foundation::source_pos`; `empty()`, `peek()`, `bump()`, `position()`,
    `remaining()`. Plus `is_space(char)` and
    `skip_intertoken_space(cursor) -> cursor` (generic ASCII whitespace
    only — no Forth-specific predicates here; those are F5's job in
    `forth_chars.hpp`, and go in `src/smd/forth/reader/`, not
    `src/smd/forth/parser/`).
  - `parser.hpp`: `parser_like` concept, `parse_state<T>{value, rest}`,
    `parse_result<T> = foundation::result<parse_state<T>>`, `parser<F>`
    (callable wrapper + CTAD deduction guide), `pure(value)`,
    `satisfy(pred, expected) `, `char_p(char)`, `map(pa, f)`,
    `lift2(pa, pb, f)`, `sequence_left(pa, pb)`, `sequence_right(pa, pb)`,
    `operator|` (ordered choice — succeeds with `pa` unless `pa` fails
    *without consuming*, in which case tries `pb`; if `pa` consumed input
    before failing, the error propagates without trying `pb` — this is the
    backtracking-prevention rule F7's grammar must respect when composing
    productions).
  - `alt.hpp`: `alt(pa, pb)` (thin `operator|` alias),
    `many<Capacity>(p)` / `some<Capacity>(p)` (collect into
    `foundation::static_vector<V, Capacity>`; `many` always succeeds,
    `some` requires >=1 match), `optional(p)` (wraps in `std::optional`,
    always succeeds), `lexeme(p)` (strips surrounding
    `skip_intertoken_space`). `Capacity` has no default anywhere in this
    file (matches upstream); always specify it at each real call site —
    same convention as `arena_box`/`tree_arena`'s `MaxNodes` from F3.
- **Layer 1/2 (typeclass-object, `parser_v`):** `parser_ops` (in
  `parser_ops.hpp`) derives from
  `smd::forth::foundation::{functor,applicative,alternative}<Impl>`, and
  `parser<F>` is registered against foundation's own
  `functor_typeclass`/`applicative_typeclass`/`alternative_typeclass`
  (DIV-0003). `smd::forth::parser::parser_v` is the global instance;
  `foundation::fmap`/`foundation::invoke`/`foundation::alt` all dispatch to
  any `parser<F>` through the normal CPO path, same as any other
  registered type. `parser_v.empty<T>()` (explicit template argument)
  builds an always-failing parser of value type `T` — `foundation::empty<T>()`
  does **not** work for `parser<F>` (see DIV-0003 for why: `parser<F>` is a
  type family, not one container type, and `empty_fn`'s zero-argument
  calling convention has nothing to deduce `T` from). This layer exists
  mainly to prove the typeclass laws hold for a real production type and
  to let generic (non-parser-specific) code written against
  `foundation::fmap`/`invoke`/`alt` also work over parsers; F5/F7 are not
  expected to need it for ordinary grammar-writing — reach for the Layer 0
  free functions first, matching D6.
- `src/smd/forth/parser/parser_ops.test.cpp` has the F4 merge-criterion
  `static_assert`: an integer parser built from `satisfy` (digit
  predicate) + `some<8>` + `map`, parsing `"42"`, entirely from primitives
  — this is the shape F5's number recognition and F7's literal-parsing
  grammar production should follow (a digit-class predicate composed with
  `satisfy`/`some`/`map`, not a hand-rolled scanning loop).

Dependencies satisfied by this merge: F4 -> F5 per the plan's parallelism
summary (Track A: F4 -> F5 -> F7, F6 joins F7). F5 (Forth lexical layer,
`src/smd/forth/reader/forth_chars.hpp`) can start once this merges to main.

## F5 complete — what F7 needs to know about the lexical API

Step F5 (Forth lexical layer) is done in worktree `wt-f5` / branch
`step/f5`. `src/smd/forth/reader/forth_chars.hpp` (+ `forth_chars.test.cpp`),
namespace `smd::forth::reader`, canonical include
`<smd/forth/reader/forth_chars.hpp>`. All 113 tests pass on `gcc-16`
(`clang-21` not available in this worker's sandbox — not re-verified, see
`handoff.md`); `make compile`, `make test`, `make lint`, and
`smoke.sh gcc-16` are all green. No divergence filed. See `handoff.md`'s
"Step F5 — Forth lexical layer" section for the full facts.

Built entirely from the F4 combinators (`cursor`, `satisfy`, `map`, `some`,
`char_p`, `skip_intertoken_space`) per D6 — no hand-rolled scanning loop for
word/number tokens; the only two places that iterate directly over `cursor`
are `skip_forth_space` (a fixpoint loop over three alternatives: plain
whitespace / `\` comment / `( ... )` comment — not naturally a `satisfy`-
style single-predicate scan) and `scan_paren_comment` (which needs to record
a span while scanning, not just a value), matching the precedent already set
by F4's own `cursor::skip_intertoken_space`.

The API surface F7's grammar (`src/smd/forth/reader/read_program.hpp`)
builds on, all in `smd::forth::reader`:

- `fold_char(char) -> char`: uppercase-folds one ASCII letter (D8); passes
  everything else through unchanged. Used internally by `scan_word`; F7
  should not need to call it directly except to fold a literal spelling by
  hand (e.g. matching a reserved word like `IF`/`ELSE`/`THEN` against
  already-folded text — those keyword spellings are already uppercase in
  source, so no folding call is needed for them either).
- `is_word_char(char) -> bool` / `is_digit(char) -> bool`: character-level
  predicates, exported in case F7 wants to compose its own `satisfy`-based
  scanners directly rather than going through `scan_word`.
- `skip_forth_space(cursor) -> cursor`: **use this, not
  `parser::skip_intertoken_space`**, everywhere in the grammar that needs to
  skip between tokens — it also skips `\` line comments and `( ... )`
  comments (D9), which plain `parser::skip_intertoken_space` does not know
  about. An unterminated `(` comment leaves the cursor positioned at the
  unclosed `(` rather than consuming to end of input, so a subsequent parse
  step will fail there with a sensible position.
- `forth_lexeme(P) -> parser`: wraps any parser (not just `scan_word`) to
  skip `skip_forth_space` on both sides — the Forth-aware analogue of
  `parser::lexeme`. Reach for this when building new token-level
  productions (e.g. a `tick` production wrapping `char_p('\'')`) instead of
  `parser::lexeme`, so comments between tokens are handled uniformly.
- `token_text<MaxName = 32> = foundation::static_vector<char, MaxName>` and
  `scan_word<MaxName = 32>(cursor) -> parse_result<token_text<MaxName>>`:
  the word/token scanner. Already folds to uppercase and already skips
  surrounding `skip_forth_space` (via `forth_lexeme`) — this is almost
  certainly what F7's `name`, keyword-matching, and bare-word productions
  should be built from. To match a specific reserved word (e.g. `IF`), scan
  with `scan_word`, then compare the resulting `token_text` against the
  literal uppercase spelling (or convert to `std::string_view` via
  `std::string_view{tok.begin(), static_cast<std::size_t>(tok.size())}` —
  see `forth_chars.test.cpp`'s local `view_of` helper for the exact
  pattern) — there is no dedicated `keyword_p("IF")` combinator in F5;
  build it in F7 from `scan_word` + comparison, or via `map`/`satisfy` over
  the same primitives, whichever reads better in the grammar.
- `is_number_token(std::string_view) -> bool`: call this on a `scan_word`
  result's text to decide whether a token is a number or a word (D8):
  `-1` is a number, `1-` and `-` are words. This is how F7's `body-item`
  production should distinguish `literal` from `word` — scan one token with
  `scan_word`, then branch on `is_number_token` of its text, rather than
  trying to parse a number and a word as separate alternatives (that would
  require careful ordering/backtracking around `operator|`'s no-backtrack-
  after-consuming rule; branching after a single scan avoids the issue
  entirely).
- `token_to_cell(std::string_view) -> std::int64_t`: converts a token
  already confirmed by `is_number_token` (precondition, not checked) into
  its signed decimal value — build `syn_literal{token_to_cell(text), pos}`
  from this once a token is recognized as a number.
- `scan_paren_comment(cursor) -> parse_result<foundation::source_span>`:
  the comment-capture primitive for D9. Requires `cur` to already be
  positioned at `(` — call this (not `skip_forth_space`) at the exact point
  in the grammar where a declared stack-effect comment may appear, i.e.
  right after scanning a colon-definition's name, when you want to *test*
  whether the next non-whitespace token is `(` and, if so, capture its
  span into `syn_colon_def.declared_effect` rather than discarding it. The
  returned span covers the opening `(` through the closing `)` inclusive
  (`first` = position of `(`, `last` = position just past `)`), so
  `source.substr(span.first.offset, span.last.offset - span.first.offset)`
  reproduces the comment text verbatim, e.g. `"( a b -- c )"` — F12 (not
  F7) is the step that actually parses that text for `--` and the
  before/after stack-effect items; F7 only needs to capture the span
  faithfully. Per the plan (Step F7 section), the *first* `( ... )` comment
  after the name is the one to capture; if none is present, leave
  `declared_effect` at its default-constructed empty span (`first == last`)
  as F6 already documented.
- Every function above is `constexpr` and has a `static_assert` (mostly the
  immediately-invoked-lambda pattern, since most checks need to invoke a
  parser) in `forth_chars.test.cpp`, including the three literal merge
  criteria from the plan's Step F5 section: folding (`scan_word` on `"dup"`
  yields `"DUP"`), comment skipping (both `\` and `( ... )` kinds, alone
  and interleaved), and number/word discrimination (`-1`/`1-`/`-`).

CMake shape to match: `forth_chars.hpp` folds into
`compile-time-forth.forth`'s existing `forth_forth_headers` `FILE_SET`;
`forth_chars.test.cpp` is a new source on the existing `reader_test`
executable, both wired from `src/smd/forth/reader/CMakeLists.txt` (same
directory and executable as F6's `syntax_tree.hpp`/`.test.cpp` — no new
target, no new subdirectory). F7's `read_program.hpp`/`.test.cpp` should
follow the same pattern: new `FILES`/test-source entries in this same
`CMakeLists.txt`, same `reader_test` executable, unless there's a concrete
reason to split — nothing here has needed that so far.

Dependencies satisfied by this merge: per the plan's parallelism summary,
Track A is F4 -> F5 -> F7, with F6 (already merged, separate track) joining
at F7. F7 (grammar, `src/smd/forth/reader/read_program.hpp`) can start once
this merges to main — both of its dependencies (F5's lexical layer, F6's
syntax tree) are now satisfied.

## F6 complete — what F7/F11 need to know about the syntax-tree API

Step F6 (syntax tree) is done in worktree `wt-f6` / branch `step/f6`. See
`handoff.md`'s "Step F6 — Syntax tree" section for the full facts. Summary
for F7 (grammar: parses source into this tree) and F11 (elaborator: consumes
this tree):

- New component: `src/smd/forth/reader/syntax_tree.hpp` (+
  `syntax_tree.test.cpp`), namespace `smd::forth::reader`, canonical include
  `<smd/forth/reader/syntax_tree.hpp>`. CMake: headers fold into
  `compile-time-forth.forth`'s `forth_forth_headers` `FILE_SET`; tests build
  as `reader_test`, wired via `add_subdirectory(reader)`.
- The closed node type is `syn_node<MaxNodes, MaxBody, MaxName>`, a
  `std::variant` over eleven node-kind structs: `syn_literal` (untemplated —
  just `{cell: std::int64_t, pos}`), `syn_word<MaxName>`,
  `syn_colon_def<MaxNodes,MaxBody,MaxName>`, `syn_if<...>`,
  `syn_begin_until<...>`, `syn_begin_while<...>`, `syn_do_loop<...>`,
  `syn_variable<MaxName>`, `syn_constant<MaxName>`, `syn_create<MaxName>`,
  `syn_tick<MaxName>`. Every kind carries a `foundation::source_pos pos`
  member for F7 to populate; it is default-constructed for now.
- `syn_node` is *forward-declared*, then the composite kinds (which hold
  `arena_box<syn_node<...>, MaxNodes>` handles to children, via the
  `syn_box<MaxNodes,MaxBody,MaxName>`/`syn_body<MaxNodes,MaxBody,MaxName>`
  aliases) are defined against the forward declaration, and only then is
  `syn_node` itself defined as the closing `std::variant`. This works
  because `foundation::arena_box<T, MaxNodes>` never needs `T` complete (it
  only stores an `int`). **This is how D3 (no `fix`/`Box`) is satisfied** —
  it is a different, simpler technique than the Scheme reference repo's
  `foundation::fix`-based open recursion (`elaborated_core.hpp`,
  `datum_type.hpp`); do not import `fix.hpp` when building F7's grammar or
  F11's elaborated core, follow this same forward-declare-the-closed-type
  pattern instead. F11's elaborated-core node type will need the identical
  trick if it has recursive node kinds (it will — `core_if`, function
  bodies, etc.).
- `syn_colon_def.declared_effect` is a plain `foundation::source_span`
  (default = empty, `first == last`), not `std::optional`. F7's grammar:
  when you parse a `( ... )` comment immediately after a colon-definition
  name, set `declared_effect` to the span covering it (start-of-`(` to
  end-of-`)` or whatever convention you pick — document it in the header
  when you land it); leave it default (empty) when there is no comment.
  F12's stack-effect checker re-slices the *original source text* through
  that span when it needs the declared-effect text — the tree does not
  duplicate the comment's characters.
- `syn_variable`/`syn_constant`/`syn_create`/`syn_tick` are name-only per the
  plan's literal spec (no value field on `syn_constant` — Forth-2012's
  `<value> CONSTANT <name>` value comes from whatever the preceding body
  form(s) leave on the stack; F7 decides how that value threads through,
  e.g. as a preceding `syn_literal` sibling in the enclosing body — nothing
  in `syn_constant` itself carries it).
- `syntax_tree<MaxNodes = 1024, MaxBody = 64, MaxName = 32>` bundles
  `arena` (the `tree_arena<syn_node<...>, MaxNodes>`) with `program` (a
  `syn_body<...>` — the top-level forms in source order: colon defs,
  `VARIABLE`/`CONSTANT`/`CREATE`, and any top-level executable words). F7's
  grammar should build one `syntax_tree` per compiled program and
  `push_back` each top-level form's handle onto `.program` as it parses.
- Helpers available: `make_syn_name<MaxName>(std::string_view) ->
  syn_name<MaxName>` and `syn_name_equals<MaxName>(syn_name<MaxName> const&,
  std::string_view) -> bool`. Both assume the input text is **already
  case-folded to uppercase** — F5's lexer folds case at scan time (per
  `handoff.md`'s architectural invariants), so F7's grammar should feed
  already-folded spellings into `make_syn_name`, not raw source text.
- All capacities (`MaxNodes`, `MaxBody`, `MaxName`) are template parameters
  on every node-kind struct, on `syn_node`, and on `syntax_tree`, defaulting
  to `1024`/`64`/`32`. F7's grammar and F11's elaborator should thread these
  same three capacities through rather than inventing new ones, unless a
  concrete reason (e.g. the elaborated core needing a different `MaxBody`
  for argument lists vs. Forth's flatter bodies) argues otherwise.
- Merge-criterion test: `src/smd/forth/reader/syntax_tree.test.cpp` hand
  builds `: SQUARED DUP * ;` in a `syntax_tree<8, 4, 16>` and walks it back
  (name + body-word checks) inside a `static_assert`, all at compile time.
  Use the same aggregate-initialization + `make_arena_box` pattern shown
  there when writing F7's grammar productions — no additional factory
  functions were added beyond `make_syn_name`/`syn_name_equals`, since the
  aggregate-init pattern is already terse and F7 will likely want its own
  grammar-shaped construction helpers anyway.
- Added `docs/compiler_architecture.org` (pipeline diagram + prose, `DRAFT —
  pending author revision`, one sentence per line). Only "Phase 3: The
  Syntax Tree" transcludes real code today (three UUID-anchored
  `#+transclude` blocks from `syntax_tree.hpp`). **Whoever lands F7 should
  fill in "Phase 2: Applicative Parser Combinators"'s transclusion (once F4
  is merged) and add the grammar's own transclusion to "Phase 3"** (or a new
  phase, if it doesn't fit); whoever lands F11 should fill in "Phase 4:
  Elaboration". Keep one sentence per line and the `DRAFT` marker until an
  author does a real editing pass.
- Verified on `gcc-16` only (no `clang-21` available in this worker's
  sandbox): `make compile`, `make test` (54/54), `smoke.sh gcc-16` all
  green/`SMOKE OK`. **`clang-21` was not re-verified for this step** —
  nothing added is toolchain-specific, but confirm on `clang-21` before or
  at the next merge point if that toolchain is available to whoever does
  it.
- **`make lint` environment caveat (not a DIV, see `handoff.md` for the full
  writeup):** this worker's sandbox has a `clang-format` (21.1.8) that
  reformats seven *pre-existing, untouched* F1–F3 files
  (`foundation/{applicative,functor,parse_error,source_pos,
  static_vector}.hpp`, `foundation/applicative.test.cpp`,
  `sender/vocab.test.cpp`) on every `make lint` run — confirmed present even
  with zero F6 changes applied (stash-and-recheck). This step left those
  seven files untouched rather than fold in an unrelated reformat; every
  file F6 actually added/changed is independently confirmed clean under
  `clang-format --style=file -n` and passes `make lint`'s CMake-linting
  hook. **Whoever merges next (or whoever lands F7/F8) will hit this same
  `make lint` "Failed" on the same seven files** regardless of what they
  touch — it is not step-specific. Decide once, centrally, whether to
  normalize those seven files' formatting (a small unrelated-looking commit)
  or reconcile the project's expected `clang-format` version, rather than
  having every subsequent worker rediscover and route around it
  independently.

No DIV filed for F6: every design choice above (the forward-declared closed
node type instead of `fix`, the empty-span convention for `declared_effect`,
the `program` field on `syntax_tree`) is an implementation detail that
satisfies D3 and the plan's literal node-kind spec, not a deviation from
`docs/forth-plan.md` or Forth-2012 semantics.
## F8 complete — what F9/F10 need to know about the machine API

Step F8 (machine substrate) is done in worktree `wt-f8` / branch `step/f8`.
See `handoff.md`'s "Step F8 — Machine substrate" section for the full
facts. `make compile`, `make test` (68/68) and `smoke.sh gcc-16` are green.
Not merged by this worker — the orchestrator merges.

The API surface F9 (dictionary) and F10 (data space) build on, all in
namespace `smd::forth::machine`, canonical includes
`<smd/forth/machine/{cell,stacks,forth_state}.hpp>`:

- `cell = std::int64_t`; `flag_true = -1` / `flag_false = 0`; `status =
  foundation::result<std::monostate>` (the concrete stand-in for a
  conceptual `result<void>` — `foundation::result<T>` cannot hold `T =
  void`). All three live in `cell.hpp`.
- `cell_stack<MaxDepth>` (`stacks.hpp`) is the single implementation behind
  both `data_stack<MaxDepth>` and `return_stack<MaxRDepth>` (template
  aliases to it — same type, same behavior, they just occupy different
  registers of `forth_state`). API: `push(cell) -> status`, `pop() ->
  result<cell>`, `peek(int offset = 0) const -> result<cell>` (0 = top),
  `depth() const -> int`. Overflow/underflow are always diagnosed, never
  UB — see `handoff.md` for why the backing `static_vector` is pre-filled
  to capacity at construction (it has no `pop_back`/shrink operation).
- `forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>` (`forth_state.hpp`)
  bundles everything: `data()`/`returns()` (mutable + const overloads)
  return the two stacks; `data_space()` (mutable + const) currently returns
  a bare `foundation::static_vector<cell, MaxData> &` — **this is the
  placeholder F10 replaces**: F10 should wire real `allot(n) ->
  result<addr>` / `fetch(addr)` / `store(addr, cell)` bounds-checked over
  this same storage (or a wrapping type that owns it), and per D10 give
  `addr` its own distinct type, explicitly convertible to/from `cell` so
  addresses can live on the data stack — do not just hand out raw
  `static_vector` indices as `cell`s without that conversion boundary.
  `output()` (const-only) exposes the accumulated
  `foundation::static_vector<char, MaxOut>`; `emit_char(char) -> status`
  and `emit_cell(cell) -> status` append to it (`emit_cell(-42)` yields
  `"-42 "`, D10's number-space-trailing format). `forth_state` is a literal
  type — usable in `constexpr` locals and in `static_assert`.
- `enum class primitive` (in `forth_state.hpp`, since `apply_primitive`
  needs `forth_state` and the plan gave no other file for it) covers all
  section-6 arithmetic/logic, comparison, data-stack, and return-stack
  words from the spec. Enumerator names spell the Forth word verbatim
  except for a trailing underscore where the bare spelling collides with a
  C++ keyword or another enumerator: `mod_, abs_, min_, max_, and_, or_,
  xor_, true_, false_`. Full list, in declaration order: `plus, minus,
  star, slash, mod_, negate, abs_, min_, max_, and_, or_, xor_, invert,
  lshift, rshift, zero_equal, zero_less, equal, not_equal, less, greater,
  less_equal, greater_equal, true_, false_, dup, drop, swap, over, rot,
  question_dup, nip, tuck, depth, to_r, r_from, r_fetch`.
- `apply_primitive(primitive, forth_state<...> &) -> status` (function
  template, deduces the four `forth_state` capacity parameters) implements
  every one of those as a pure stack operation. `/` and `MOD` are symmetric
  (C++ truncating) division/remainder, not floored, and both diagnose
  division-by-zero as a `status` error rather than dividing. `LSHIFT` /
  `RSHIFT` operate through `std::uint64_t` (`RSHIFT` is a logical/unsigned
  shift per Forth-2012) and mask the shift amount to `& 63`. F9's colon-word
  bodies and F11's elaborator will presumably call `apply_primitive` per
  `core_prim{opcode}` node when interpreting/evaluating (F13's direct
  evaluator) rather than compiling to native code directly — nothing in F8
  assumes how `primitive` values get chosen at a call site, only that
  callers hold a `forth_state<...> &` to apply them to.
- Every primitive's stack behavior, plus every underflow/overflow/div-zero
  path, has a dedicated `static_assert` in
  `src/smd/forth/machine/forth_state.test.cpp` (merge criterion); `stacks.
  test.cpp` covers the underlying `cell_stack` underflow/overflow
  directly; `cell.test.cpp` covers the flag constants and `status` alias.
- CMake shape: headers fold into `compile-time-forth.forth`'s existing
  `forth_forth_headers` `FILE_SET` (no separate compiled
  `compile-time-forth.machine` target — matches the `foundation`/`sender`
  pattern); tests build as `machine_test`, wired from
  `src/smd/forth/machine/CMakeLists.txt`, descended into via
  `add_subdirectory(machine)` in `src/smd/forth/CMakeLists.txt`. F9's
  `dictionary.hpp` and F10's `data_space.hpp` should each get their own
  `.hpp`/`.test.cpp` pair added to this same `machine/` directory and
  `CMakeLists.txt` (new `FILES`/test-source entries, same `machine_test`
  executable) rather than a new subdirectory, unless one of them has a
  concrete reason to be a separate compiled target — nothing in `machine/`
  has needed that so far.

## Known environment issue: `make lint` fails repo-wide, unrelated to F8

`make lint` (`pre-commit run -a`) currently reports **Failed** in this
environment even on a checkout with F8's new `machine/` files entirely
absent: the pinned `clang-format` hook (`v21.1.2` via the portable wheel,
per `.pre-commit-config.yaml`) reformats several pre-existing,
already-committed files by one space on wrapped `friend`/multi-line
function-signature continuation lines —
`src/smd/forth/foundation/{applicative,functor,parse_error,source_pos,
static_vector}.hpp`, `foundation/applicative.test.cpp`, and
`sender/vocab.test.cpp`. This reproduces on a clean `git checkout --` of
exactly those files with no other changes present, so it predates F8 and
is not caused by it; F8's own new files were verified independently
clean via `pre-commit run clang-format --files src/smd/forth/machine/*`
(no modifications). Likely cause: a `clang-format` micro-version mismatch
between whatever produced the currently-committed formatting and the
`v21.1.2` wheel this environment's pre-commit resolves. Not filed as a DIV
(it's tooling/environment drift, not a deviation from `docs/forth-plan.md`
or Forth-2012 semantics) — but whoever next needs a fully green `make
lint` repo-wide should either re-pin/re-vendor a matching `clang-format`
build, or run `clang-format -i` repo-wide once and commit the result as its
own small housekeeping change, separate from any feature step.

## F9 complete — what F11 needs to know about the dictionary API

Step F9 (dictionary) is done in worktree `wt-f9` / branch `step/f9`. See
`handoff.md`'s "Step F9 — Dictionary" section for the full facts.
`make compile`, `make test` (114/114, up from 68/68 at F8), `make lint`
(fully green — no repeat of the pre-existing `clang-format` drift F6/F8
flagged; that drift is not present in this checkout), and `smoke.sh gcc-16`
are all green. Not merged by this worker — the orchestrator merges. F5
(lexical layer) and F10 (data space), running in parallel in other
worktrees, are unaffected by this merge; `forth_state.hpp` was not touched.

New component: `src/smd/forth/machine/dictionary.hpp` (+
`dictionary.test.cpp`), namespace `smd::forth::machine`, canonical include
`<smd/forth/machine/dictionary.hpp>`. CMake: headers fold into
`compile-time-forth.forth`'s `forth_forth_headers` `FILE_SET`; tests build
into the existing `machine_test` executable (new `FILES`/test-source
entries in `src/smd/forth/machine/CMakeLists.txt`, no new subdirectory).

The API surface F11 (elaborated core and resolution) builds on:

- `dictionary<MaxWords, MaxName = 32>` is the word list F11 threads through
  program-order resolution. Construct one with `dictionary<MaxWords,
  MaxName> dict;` or start from `default_dictionary<MaxWords, MaxName>()`
  (default `MaxWords = 256`, enough for the 37 installed primitives plus
  headroom) to get every F8 primitive pre-installed under its Forth name.
- Five typed definer methods, each returning `status` (diagnoses
  `"dictionary full"` rather than overflowing, never UB):
  `define_primitive(name, primitive)`, `define_colon(name, colon_word)`,
  `define_variable(name, variable_word)`,
  `define_constant(name, constant_word)`, `define_foreign(name,
  foreign_word)`. F11 should call `define_colon` once per colon definition
  it elaborates, threading the dictionary forward through the program in
  source order (each subsequent top-level form's word references resolve
  against whatever the dictionary looks like *at that point*, including
  any earlier colon definitions in the same program) — this is what the
  plan's "static binding falls out of F11's program-order resolution" note
  refers to.
- `colon_word{core_id, effect}`: `core_id` is a bare `int` — F11 should
  treat it as the index/handle of the elaborated-core node it just built
  for this definition (into whatever arena F11's `tree_arena` for the
  elaborated core turns out to be); nothing in F9 interprets `core_id`
  beyond storing it. `effect` is a `stack_effect{inputs, outputs, known}`
  — F9 always installs `stack_effect{}` (i.e. `known = false`) for new
  colon words; F11 either leaves it unknown or fills in a real value if it
  computes one inline, but F12 is the step that actually owns stack-effect
  analysis end-to-end.
- `variable_word{addr}`, `constant_word{value}`: build these from whatever
  F11 resolves a `VARIABLE`/`CONSTANT` declaration to.
  **DIV-0004** (`docs/divergences/DIV-0004-dictionary-addr-placeholder.md`):
  `variable_word::addr` is currently a plain `cell`, not the distinct typed
  `addr` the plan's D10 calls for — that type is F10's deliverable, and F9
  has no dependency on F10 (both depend only on F8, run in parallel).
  **When F9 and F10 are both merged, retype `variable_word::addr` from
  `cell` to F10's `addr` type as a small follow-up** (update
  `dictionary.hpp`'s include and the field's type; nothing else in
  `dictionary.hpp` needs to change). Until then, treat any `variable_word`
  you build as holding a raw data-space index with no more type safety
  than a `cell` has.
- `foreign_word{index}`: a placeholder slot for F19 (foreign function
  interface); F11 has no reason to construct one yet.
- `dictionary::lookup(std::string_view name_text) const ->
  dictionary_entry<MaxName> const *` is linear, **newest-first** (scans
  from the last-inserted entry backward), and **folds `name_text` to
  uppercase internally** before comparing — pass either case, it doesn't
  matter (`lookup("dup")` and `lookup("DUP")` are equivalent). Returns
  `nullptr` if nothing matches. F11's word-reference resolution should
  call this once per `syn_word` it needs to resolve, then `std::visit` (or
  `std::get_if`) on `entry->binding` to decide what kind of reference it
  just resolved (primitive opcode vs. colon word vs. variable vs. constant
  vs. foreign word) and shape its elaborated-core node accordingly. A
  resolution failure (word not found at all) is `entry == nullptr`; F9
  does not itself produce a diagnostic for that case, since it has no
  notion of "the elaborator is currently resolving word X at source
  position Y" — F11 owns turning a `nullptr` lookup into a real
  `parse_error`/diagnostic with source position.
- Redefinition is legal, not an error: `define_*` never rejects a name
  already present, it just appends another entry. The plan calls for F11
  to *warn* (not error) on redefinition via a "collected-diagnostics
  channel" — F9 does not implement that channel itself (out of scope: F9
  only has to make redefinition legal and make `lookup` return the newest
  entry, which it does); F11 is expected to notice "this name already had
  an entry before I called `define_colon`/etc." (e.g. by calling `lookup`
  first and checking for a non-null result before defining) and route that
  fact into whatever collected-diagnostics mechanism F11 introduces.
- `dictionary_entry<MaxName>{name, binding}`: `name` is a `word_name<MaxName>
  = foundation::static_vector<char, MaxName>`, always stored already
  uppercase (`make_word_name` folds on construction, called internally by
  every `define_*`). `binding` is the closed
  `dictionary_binding = std::variant<primitive, colon_word, variable_word,
  constant_word, foreign_word>`.
- Every binding type, the closed variant, `dictionary_entry`, and
  `dictionary` itself are trivially destructible (D3-adjacent — the
  dictionary is one flat `foundation::static_vector`, never a
  destructor-owning heap container); F11's elaborated-core arena should
  follow the same discipline, per F6's precedent (forward-declare the
  closed node type, hold `arena_box` handles to children, no `fix`/`Box`).
- Merge-criterion tests live in `dictionary.test.cpp` (both as
  `static_assert`s and mirrored `TEST_CASE`s): lookup finds an installed
  word, shadowing (newest wins, old entry still counted in `size()`), and
  case-folded lookup (`dup` finds `DUP`).

Dependencies satisfied by this merge: F9 -> F11 (elaboration also depends
on F6 (syntax tree, already merged) and F5/F7 (lexical layer + grammar,
still in flight) for its input; F11 cannot fully start until F7 lands, but
the dictionary API it consumes is stable as of this merge).
## F10 complete — what F11/F16 need to know about the data-space API

Step F10 (data space) is done in worktree `wt-f10` / branch `step/f10`. See
`handoff.md`'s "Step F10 — Data space" section for the full facts.
`make compile`, `make test` (114/114), `make lint` (fully green, including
the `clang-format` hook — see below), and `smoke.sh gcc-16` are all green.
Not merged by this worker — the orchestrator merges. F9 (dictionary,
parallel worktree, a new `machine/dictionary.hpp`) did not touch
`forth_state.hpp` or `data_space.hpp`; this step did not touch
`dictionary.hpp`.

The API surface F11 (elaborated core and resolution) and F16 (memory words
end-to-end) build on, namespace `smd::forth::machine`, canonical include
`<smd/forth/machine/data_space.hpp>`:

- `addr`: a plain (untemplated) class wrapping a private `cell index_{}`.
  Default-constructible (converts to cell `0`). `explicit addr(cell)` and
  `explicit operator cell() const` are the *only* ways in or out — no
  arithmetic, no implicit conversions, no comparison beyond a defaulted
  hidden-friend `operator==`. This is deliberately narrow: an `addr` moves
  between "a `cell` sitting on the data stack" (what `VARIABLE X` / `X`
  push) and "an index `data_space` will accept" (what `@`/`!` consume), and
  every hop between those two roles is an explicit cast at the call site.
  If F16's `CREATE ... DOES>` or pointer-like address arithmetic needs
  `addr + n`, add that operator to `addr` then — nothing here precludes it,
  it just wasn't needed yet.
- `data_space<MaxData = 1024>`: a bump allocator over a
  `foundation::static_vector<cell, MaxData>` pre-filled to `MaxData` at
  construction (same trick F8's `cell_stack` uses, since `static_vector` has
  no shrink operation). API:
  - `allot(int count) -> result<addr>`: reserves `count` contiguous,
    zero-initialized cells, returns the address of the first. Diagnoses a
    negative `count` and exhaustion (`count > MaxData - high_water_mark`,
    checked to avoid signed overflow). **F11's `VARIABLE` allots exactly
    one cell at elaboration time** by calling `allot(1)` on the
    `data_space` threaded through `elaborate`; **`compiled_unit`'s
    "data-space size consumed by declarations" field should be the sum of
    every `allot` call size made during elaboration** — read it back via
    `data_space::size()` (below) after elaborating, or track it
    incrementally as each declaration allots.
  - `fetch(addr) const -> result<cell>` / `store(addr, cell) -> status`:
    read/write one cell. **Bounds-checked against the allotted high-water
    mark, not just raw `MaxData` capacity** — an `addr` that `allot` never
    handed out (even if numerically `< MaxData`) is diagnosed as
    out-of-bounds. This means F16's `@`/`!`/`+!` must only ever be handed
    addresses that trace back to a real `allot` call (which is already true
    for `VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT` — they all go through
    `elaborate`'s `data_space`) — do not synthesize an `addr` out of thin
    air and expect `fetch`/`store` to accept it.
  - `size() -> int`: count of cells allotted so far (matches
    `foundation::static_vector::size()`'s meaning/type, which is what F8's
    placeholder `data_space()` accessor originally exposed — kept the name
    so the one pre-existing test that called `.size()` needed no change).
  - `here() -> addr`: the address one past the last allotted cell, i.e.
    Forth's `HERE`. Useful if F16 wires `HERE` itself, or if F11 wants an
    `addr`-typed handle to "the next declaration's base" rather than an
    `int` count.
- `forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::data_space()`
  (mutable + const) now returns `machine::data_space<MaxData> &` / `const
  &` — this is the real thing, not the F8 placeholder
  (`foundation::static_vector<cell, MaxData> &`). Every other
  `forth_state` member (`data()`, `returns()`, `output()`, `emit_char`,
  `emit_cell`, `primitive`, `apply_primitive`) is byte-for-byte unchanged
  from F8 — F9's dictionary work and F11/F13's evaluator work should not
  need to touch anything about `forth_state` except this one accessor's
  return type.
- Merge-criterion tests live in `src/smd/forth/machine/data_space.test.cpp`
  (immediately-invoked-lambda `static_assert`s, per house style):
  allot/fetch/store round-trip (single- and multi-cell), out-of-bounds
  fetch/store (both "past `here()`" and "negative index"), allot
  exhaustion (both "exceeds remaining capacity" and "negative count"), and
  `addr` <-> `cell` explicit-conversion round-trip plus structural
  equality. Follow this same shape for F11's `elaborated_core.hpp` tests —
  `data_space` is exactly the kind of small, fully-bounds-checked component
  those merge criteria expect.
- CMake: `data_space.hpp`/`data_space.test.cpp` were added as additional
  `FILES`/test sources on the existing `compile-time-forth.forth` /
  `machine_test` targets, in `src/smd/forth/machine/CMakeLists.txt` — no
  new subdirectory or target, matching F8's/F9's pattern. F11's
  `elaborator/` directory should get its own new `CMakeLists.txt` +
  `add_subdirectory(elaborator)` (there is no existing `elaborator_test`
  target yet), following the same shape as `machine/`, `reader/`, etc.
- **`make lint` environment note, resolved (not carried forward as an open
  item):** the `clang-format` drift on pre-existing F2/F3 files documented
  at F6/F8 (`foundation/{applicative,functor,parse_error,source_pos,
  static_vector}.hpp`, `foundation/applicative.test.cpp`,
  `sender/vocab.test.cpp`) did **not** reproduce in this worker's sandbox —
  `make lint` (`pre-commit run -a`) passed fully clean, repo-wide, with
  zero files touched beyond this step's own edits (verified via `git status
  --porcelain`). Whoever next runs `make lint` should confirm this is still
  the case rather than assuming the old caveat still applies; if it's still
  green, that open item from F6/F8's handoff notes can be considered
  closed.
- No DIV filed for F10 — see `handoff.md` for why each design choice (the
  allotted-high-water-mark bounds check, `addr`'s narrow
  explicit-conversion-only surface, the `size()`/`here()` accessors) is
  within the plan's literal spec rather than a deviation from it.

Dependencies satisfied by this merge: F10 -> F11 (alongside F7, F9) per the
plan's parallelism summary. F11 needs F7 (grammar), F9 (dictionary), and
F10 (this step) all merged before it can start.
