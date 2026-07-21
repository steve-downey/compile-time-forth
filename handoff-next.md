# Next step: Step F15 — Public one-shot API

Step F14 (stack-machine codegen and VM) is done in worktree `wt-f14` /
branch `step/f14`. This file is a full rewrite for F15 — see `handoff.md`'s
"Step F14 — Stack-machine codegen and VM" section (and everything above it)
for the complete historical record; this file only summarizes what F15
needs to start.

## What F15 is

Read `docs/forth-plan.md` section "Step F15 — Public one-shot API" (around
line 661) for the authoritative spec. In short: replace the placeholder
`src/smd/forth/forth.hpp`/`forth.cpp`/`forth.test.cpp` (currently a
`forth::forth()` free function returning the string `"Steve"` — a bootstrap
placeholder from before the real pipeline existed, never touched since) with
the project's real public one-shot entry point:

- A `source_literal<N>` NTTP (non-type template parameter) char-array
  wrapper — a template parameter that can carry a whole Forth source string
  as part of a type, so `compiled_forth<"...">` can be a variable template
  keyed on the literal source text itself.
- `template <source_literal Source> inline constexpr auto compiled_forth =
  /* read -> elaborate -> codegen, .value() */;` — one `constexpr` variable
  template that runs the *entire* pipeline (F5/F7 parse, F11/F12 elaborate,
  F14 codegen) over `Source`'s text and stores the resulting
  `machine::compiled_program`. A failed parse/elaboration/codegen is a
  **hard compile error**: calling `.value()` on a `foundation::result` that
  holds an error, inside a `constexpr` initializer, is not a core constant
  expression (`std::get<T>` on the wrong variant alternative throws), so the
  whole translation unit fails to compile — exactly the discipline
  `eval_direct.test.cpp`'s and `vm.test.cpp`'s own namespace-scope
  `constexpr` programs already rely on for their *known-good* inputs; F15's
  own job is to make that same discipline the **public** contract, not an
  internal test convenience.
- `compiled_forth<Source>.run() -> result<forth_state>` — runs the compiled
  program (at compile time via `static_assert`, or at ordinary runtime) and
  returns the resulting machine state or the first runtime error (stack
  underflow, division by zero, budget exhaustion, etc.) — this is `vm::run`
  under the hood, called against `compiled_forth<Source>`'s own stored
  `compiled_program`.
- Convenience accessors `.stack()` and `.output()`: the plan names them but
  does not specify their exact return shape (a full stack snapshot? just the
  top cell? see "Known open items" below) — this step's own design call,
  same as F14 had open design calls for `compiled_program`'s word-table
  shape and the deferred-opcode question.

`src/examples/hello.cpp` (currently `std::println("Hello, {}!",
forth::forth())`, printing `"Hello, Steve!"`) gets updated to run a small
program through `compiled_forth` and print its output instead; keep the
example's own test green (`hello` is built and run by `smoke.sh`, see
`.claude/skills/run-compile-time-forth/smoke.sh`). Add a Godbolt
single-file-extraction example under `src/examples/` — check whether an
existing single-file-extraction convention/script already exists in this
repo or its sibling `compile-time-scheme` before inventing one from
scratch.

The old name-returning placeholder API (`forth::forth()`) is **deleted**
outright, not deprecated alongside the new one; `forth.test.cpp` becomes the
public-API test (its own bootstrap `TEST_CASE("forth returns Steve", ...)`
goes away with the rest of the placeholder). `compile-time-forth.org` (the
top-level literate-programming doc, not `docs/compiler_architecture.org`)
has four UUID transclusions that currently point at the placeholder's own
anchors — `44cc988c-7353-43aa-a7d3-8840f92371a6` (`forth.hpp`),
`a66dec0e-e5cc-44b5-b9f1-bbed787c3d44` (`forth.cpp`),
`03013d1f-bcc1-4d3e-9701-3ed1a15c6370` (`forth.test.cpp`),
`710c39c6-c7e1-403f-a9f0-9f8ecf890dc9` (`hello.cpp`) — these all need
updating to real anchors once the placeholder is replaced (or the org file
gains an explicit TODO note deferring the update to F22, documentation
consolidation — the plan explicitly leaves this choice to the worker,
"record it in handoff" either way).

Merge criteria (from the plan, verbatim): `compiled_forth<": SQUARED DUP *
;  4 SQUARED">.stack()` static_asserts to `[16]`; the `hello` example
prints the program's output; a negative-compile test proves a syntax error
fails compilation (this needs the project's existing negative-compile-test
mechanism — check how earlier steps, if any, have proven "this does not
compile" already, e.g. via a separate CMake target that is expected to
fail, or a documented manual-verification note; F15 is likely the first
step that actually needs one for real, so this may be new infrastructure).

## A close reference implementation already exists

The sibling project `~/src/compile-time-scheme/main` (see `docs/forth-
plan.md`'s own "Reuse as pattern only (reimplement, don't copy)" table,
which names exactly this) already has a working `source_literal<N>` +
one-shot variable-template pattern at
`~/src/compile-time-scheme/main/src/smd/smdscheme/smdscheme.hpp`:

```cpp
template <std::size_t N>
struct source_literal {
    char text[N]{};
    constexpr source_literal(char const (&input)[N]) {
        std::copy_n(input, N, text);
    }
    [[nodiscard]] constexpr auto view() const -> std::string_view {
        return {text, N - 1};
    }
};

template <source_literal Source>
inline constexpr auto compiled_closure =
    closure::compile_to_closure(Source.view()).value();
```

This is explicitly a **pattern to reimplement, not copy** (`docs/forth-
plan.md`'s own distinction between "adapt by copy" (F3/F4's parser
foundation) and "reuse as pattern only" (this)) — F15's own `source_literal`
should live under `smd::forth` (not import the Scheme one), follow this
project's own file-prolog/guard/namespace conventions, and `compiled_forth`
should compose `read_program` -> `elaborate` -> `codegen` (three stages, not
Scheme's single `compile_to_closure`) rather than copy Scheme's own
single-function pipeline shape. Note the Scheme version returns a reference
into a `constinit` storage location, not a `constexpr` value directly
(`inline constexpr auto compiled_closure = ...` — actually it *is*
`constexpr`, matching what F15's own plan text asks for); read this file in
full before starting, it answers most of the NTTP-mechanics questions before
they come up (deduction guide needs, `char const (&)[N]` constructor,
`.view()`'s `N - 1` to drop the null terminator).

## The API F15 consumes (Steps F5–F14)

- **`reader::read_program<MaxNodes, MaxBody, MaxName, MaxDepth>(source) ->
  result<syntax_tree<...>>`** (F7, `src/smd/forth/reader/read_program.hpp`)
  — stage one of the pipeline `compiled_forth` composes.
- **`elaborator::elaborate<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
  MaxWarnings>(tree, source) -> result<compiled_unit<...>>`** (F11/F12,
  `src/smd/forth/elaborator/elaborate.hpp`) — stage two; note it takes
  `source` a second time (to re-slice declared stack-effect comments, see
  F12's own handoff section), so `compiled_forth`'s own pipeline helper
  needs `Source.view()` twice, once for `read_program`, once for
  `elaborate`.
- **`machine::codegen<MaxCode, MaxNodes, MaxBody, MaxName, MaxWords,
  MaxData, MaxWarnings>(unit) -> result<compiled_program<MaxCode,
  MaxWords>>`** (F14, `src/smd/forth/machine/codegen.hpp`) — stage three,
  the new stage this step's own predecessor added. Every template parameter
  above is a capacity with a project-standard default (1024/64/32/256/1024/
  64/4096) — `compiled_forth<Source>` will need to decide whether to expose
  these as its own template parameters (letting a caller override
  capacities per-program) or hardcode the defaults for the one-shot API's
  first cut; either is defensible, but whichever is chosen should be
  recorded in `handoff.md`, and per the project's standing rule (D2, "all
  capacities are template parameters with defaults") the former is more in
  keeping with everything upstream of it.
- **`machine::run<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
  MaxOut>(compiled_program const&, forth_state&, fuel = 100000) -> status`**
  (F14, `src/smd/forth/machine/vm.hpp`) — `compiled_forth<Source>.run()`'s
  own implementation constructs a fresh `forth_state`, calls this, and
  either returns it wrapped in a `result` or returns the propagated error;
  `.stack()`/`.output()` most likely call `.run()` internally (or share a
  memoized/recomputed state — see "Known open items" below on whether
  `compiled_forth<Source>` itself is stateless, since a namespace-scope
  `inline constexpr auto compiled_forth<Source>` is a *value* — the
  `compiled_program`, not a `forth_state` — so `.stack()`/`.output()` must
  each do their own fresh `run()` unless a different design is chosen).
- **`machine::compiled_program<MaxCode, MaxWords>`** (F14,
  `src/smd/forth/machine/instruction.hpp`) — what `compiled_forth<Source>`
  itself stores; trivially copyable and a literal type by construction (F14's
  own `static_assert`s already prove this), so storing one as a `constexpr`
  variable-template value is exactly the shape F14 built it for.
- **`machine::forth_state<StackDepth, RStackDepth, MaxData, MaxOut>`** (F8,
  extended F13's D10 output words) — `.data()` (the data stack — note **not**
  named `.stack()`; F15's own `.stack()` accessor is new naming at the
  `compiled_forth` level, not a rename of `forth_state`'s own `.data()`),
  `.returns()`, `.data_space()`, `.output()`. `.data()` returns a
  `data_stack<MaxDepth>` (a `cell_stack` alias) with `.depth()`/`.peek(int)`
  — there is no "give me the whole stack as a container" accessor on
  `cell_stack` itself yet (`stacks.hpp`), so `compiled_forth<Source>.stack()`
  returning something like `[16]` (the plan's own merge-criterion wording)
  likely needs either a small conversion into a `static_vector<cell, N>`
  built by looping `peek(i)` from `depth()-1` down to `0` (bottom to top,
  matching `apply_primitive`'s own `dot_s` convention in
  `machine/forth_state.hpp`), or a new `cell_stack` accessor — this step's
  own call.
- **`foundation::result<T>`/`foundation::parse_error`**: unchanged, same
  discipline. `.value()` on an error result inside a `constexpr` initializer
  is what makes a bad `compiled_forth<Source>` a **hard** compile error —
  this is not new machinery, it is the same "throws in constexpr, which
  means it isn't a constant expression, which means the initializer fails"
  mechanism `eval_direct.test.cpp`'s and `vm.test.cpp`'s own namespace-scope
  `constexpr` programs already exploit for known-good inputs; F15 exploits
  the *failure* side of the same mechanism on purpose, for its own negative-
  compile-test merge criterion.

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary — both
  were available and both verified green in this worker's sandbox for F14
  (`make compile`, `make test` at 194/194, `make lint` clean on the first
  run with no reformatting, and `smoke.sh` on both toolchains ending
  `SMOKE OK`); worth re-confirming both are still present for whoever runs
  F15, but don't block on it if only one is.
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern); add matching `TEST_CASE`s for
  Catch2 visibility. F15's own merge criterion is explicit about a
  **negative**-compile test this time (a syntax error must fail
  compilation) — check whether this repository already has a documented
  mechanism for "this file must fail to compile" as part of the normal test
  suite (a separate CMake target expected to fail, guarded so it does not
  break the main build) before inventing one; if none exists, this is new
  infrastructure this step adds, worth calling out explicitly in
  `handoff.md`.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3); `compiled_program` (F14) is flat by
  construction instead (a `static_vector<instr, MaxCode>`, not a tree) —
  unchanged by F15, which only consumes `compiled_program`, never builds a
  new tree type of its own.
- All capacities are template parameters with defaults; no hardcoded
  capacity constants — applies to whatever capacities `compiled_forth<
  Source>` itself exposes (see "The API F15 consumes" above on whether to
  parameterize or hardcode the pipeline's own capacities).
- All nonlocal control (`EXIT`, `LEAVE`, `CATCH`/`THROW`) is one-shot and
  dynamic-extent (`handoff.md`'s architectural invariants) — unchanged,
  F15 does not add new control-flow machinery, only a public entry point
  onto the existing pipeline.
- Before handoff: `make compile`, `make test`, `make lint` green on
  `gcc-16` (and `clang-21` if available); both `smoke.sh` runs end
  `SMOKE OK`; `checklist.md` ticked; `handoff.md` appended (not rewritten);
  `handoff-next.md` rewritten for whatever comes next (F16, memory words
  end-to-end, per the plan's own ordering — F16 and F15 are marked as
  parallelizable with each other in the plan's "Parallelism summary," both
  depending only on F14, so if F15 and F16 are being worked by different
  agents concurrently, say so explicitly in whichever `handoff-next.md` is
  written second); divergence docs filed for anything done differently than
  `docs/forth-plan.md` or Forth-2012 semantics (next free number: check
  `docs/divergences/` — **DIV-0008 is the latest**, `accepted-permanent`,
  filed this step (F14) for two related scope cuts: the nine F17/F18a-
  shaped opcodes reserved in `op` but never given real behavior, and
  `compiled_program`'s `required_stack_depth`/`required_return_depth`
  fields defaulting to `-1`, "not computed," rather than a real whole-
  program peak-depth bound).

## Known open items going into F15

- **`.stack()`/`.output()`'s exact return shape is unspecified by the
  plan** beyond the merge criterion's own worked example
  (`compiled_forth<"...">.stack()` static-asserts to something that reads
  as `[16]` — almost certainly a small fixed-capacity container of `cell`s,
  not a single scalar, since a general Forth program's data stack can hold
  more than one cell at the end of a run). This worker needs to pick a
  concrete representation (most likely a `foundation::static_vector<cell,
  N>` snapshot, built by draining `forth_state::data()` via repeated
  `.peek(i)` calls bottom-to-top or top-to-bottom — pick one and document
  it, since it is directly observable in `[16]`-style test assertions) and
  document it here or in `docs/compiler_architecture.org`'s eventual F15
  section.
- **Whether `compiled_forth<Source>` re-runs the whole program on every
  `.stack()`/`.output()` call, or runs once and caches**: since
  `compiled_forth<Source>` itself is a `constexpr`-initialized *value* (the
  `compiled_program`, produced once by codegen), but `.run()` needs a fresh
  mutable `forth_state` each time it is invoked (running the same
  `compiled_program` object twice against two different `forth_state`s is
  exactly F14's own "survives-to-runtime proof" pattern), the natural
  reading is that `.run()`, `.stack()`, and `.output()` are each
  independent calls that construct a fresh `forth_state` and run the
  program from scratch — meaning `.stack()` followed by `.output()` runs
  the whole program *twice*. This is almost certainly fine for the plan's
  own merge criteria (small test programs, no side effects observable
  across runs since each run gets a fresh `forth_state`), but is worth a
  deliberate choice and a doc comment either way, not an accidental
  quadratic surprise for a future large program.
- **Whether the negative-compile-test merge criterion needs new CMake/test
  infrastructure**: see "Standing constraints" above — check first whether
  this repository or `compile-time-scheme` already has a "must fail to
  compile" test pattern (a `try_compile`-based CMake test, a separate
  target excluded from `all` but built by a dedicated CI/test step, etc.)
  before inventing one. `~/src/compile-time-scheme/main` is the same
  sibling project the `source_literal`/`compiled_closure` pattern came
  from, so check there first.
- **Capacity parameterization of `compiled_forth<Source>`**: see "The API
  F15 consumes" above — expose the pipeline's own capacities as further
  template parameters (in keeping with D2's "all capacities are template
  parameters, no hardcoded constants" project-wide rule) or default them
  for a first cut and revisit later. If defaulted without a way to
  override, that is arguably a real, if minor, divergence from D2 worth a
  DIV entry; if parameterized, no divergence, just more verbose call
  sites (`compiled_forth<"...">` vs. `compiled_forth<"...", 2048>`, etc.,
  depending on parameter order/defaults chosen).
- **The Godbolt single-file-extraction example**: the plan asks for one
  under `src/examples/` but does not specify its shape (a script that
  concatenates headers? a genuinely single-translation-unit example that
  happens to need no other files because it only includes already-
  self-contained project headers?). Check whether `compile-time-scheme`
  already has one to use as a pattern reference, the same way
  `smdscheme.hpp` served as the pattern reference for `source_literal`
  above.
- **DIV-0006's gap (F12's stack-effect checker is not flow-sensitive across
  `EXIT`) and DIV-0008's deferred-opcode/uncomputed-capacity gaps are both
  still open** and still not F15's responsibility to fix — flagged again
  here only because F15's `compiled_forth<Source>` is a third consumer
  (after F13's tests and F14's tests) of the same elaborate/codegen
  pipeline, and any F15-authored example program that happens to hit either
  gap should not be mistaken for a new F15 bug.
