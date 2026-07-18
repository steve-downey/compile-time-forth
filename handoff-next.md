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
