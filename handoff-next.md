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
