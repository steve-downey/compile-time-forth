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
