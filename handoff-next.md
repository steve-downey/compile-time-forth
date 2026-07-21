# Next step: Step F16 — Memory words end-to-end

Step F15 (public one-shot API) is done in worktree `wt-f15` / branch
`step/f15` — see `handoff.md`'s "Step F15 — Public one-shot API" section
(and everything above it) for the complete historical record. This file is
a full rewrite for F16 — the next unchecked step in `checklist.md`.

## Parallelism note

`docs/forth-plan.md` marks F16 "parallel with F15" and F17 "parallel with
F15/F16" — both F16 and F17 depend only on F14 (done), not on each other or
on F15. If another agent is concurrently working F17 (counted loops) or
F18a (execution tokens/exceptions) in a different worktree, note the shared
files both steps are likely to touch: `src/smd/forth/machine/forth_state.hpp`
(the `primitive` enum and `apply_primitive`'s `switch`) and
`src/smd/forth/machine/dictionary.hpp` (`default_dictionary`'s word table).
Both are append-only in spirit (new enumerators, new table rows), but a
concurrent merge could still conflict on adjacent lines — worth checking
`git log`/other worktrees before assuming a clean merge.

## What F16 is

Read `docs/forth-plan.md`'s "Step F16 — Memory words end-to-end" section
(around line 685) for the authoritative spec. In short: wire
`VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT`/`@`/`!`/`+!` through **both**
existing evaluators — F13's `eval_direct.hpp` and F14's `vm.hpp` — so that
data-space addresses (already a distinct `machine::addr` type, D10) can
actually be fetched from and stored to at runtime, not just pushed onto the
stack as inert values (which is all that happens today).

Merge criteria, verbatim from the plan:

```forth
VARIABLE X  5 X !  X @ 3 + X !  X @   \ stack [8]
7 CONSTANT LUCKY  LUCKY LUCKY +       \ stack [14]
CREATE BUF 4 ALLOT                    \ BUF usable as base address
```

as `static_assert`s through both backends; an out-of-bounds store must be
diagnosed (never undefined behavior, D7).

## What already exists (do not re-invent)

- **`machine::addr`** (`src/smd/forth/machine/data_space.hpp`): a distinct
  type wrapping `cell`, explicitly convertible both ways. DIV-0004 (F11)
  already resolved "should this be its own type or a bare cell" in favor of
  its own type — this is settled, not open.
- **`machine::data_space<MaxData>`** (same file): a bump-allocated cell
  arena with `allot(count) -> result<addr>`, `fetch(addr) const ->
  result<cell>`, `store(addr, cell) -> status`, `size()`, `here()`. Every
  operation is already bounds-checked against the high-water mark
  (`allot`'d range) — "out-of-bounds store diagnosed" (the plan's own merge
  criterion) is **already true of `data_space` itself**; F16 does not need
  to add bounds-checking, only to make `@`/`!`/`+!` actually reach this
  type at runtime (see the gap below).
- **`elaborator::compiled_unit::data_space`** (`elaborated_core.hpp`): a
  `machine::data_space<MaxData>`, already populated **at elaboration time**
  — `elaborate.hpp`'s `elaborate_variable` (line ~359) calls
  `unit.data_space.allot(1)` for every `VARIABLE`, and `elaborate_create`
  (~line 380) calls it with 0 cells for `CREATE` (per F11's own documented
  reading: "`CREATE NAME` installs `NAME` at the current data-space top
  with no cells allotted... F16's `ALLOT` is what later actually extends
  storage past a `CREATE`d address" — see `dictionary.hpp`'s
  `variable_word` doc comment). `machine::dictionary`'s own
  `variable_word{ .address }` binding already stores the resolved `addr`.
- **`core_var`/`core_const`** (`elaborated_core.hpp`): elaborated-core node
  kinds that push a variable's address / a constant's value. **Both
  `eval_direct.hpp` and `codegen.hpp`/`vm.hpp` already handle these two
  node kinds** (`eval_direct.hpp` lines ~167/173; `codegen.hpp`'s
  `codegen_emit_node`, the `core_var`/`core_const` arms, emit a plain
  `op::push`). `VARIABLE X` and `7 CONSTANT LUCKY` already push the right
  values through both backends today — this half of the merge criterion is
  **already working**; what's missing is only `@`/`!`/`+!` themselves.
- **`elaborate_constant`** (`elaborate.hpp`, ~line 399) is the pattern for
  "a top-level form that consumes the immediately preceding top-level
  item" — `N CONSTANT NAME` folds the literal `N` into the new dictionary
  entry at elaboration time, not at runtime. `CREATE BUF 4 ALLOT` almost
  certainly needs the same shape (`ALLOT` consuming an immediately
  preceding literal *and* the immediately preceding `CREATE`, extending
  that same data-space allotment) — read `elaborate_constant` and
  `elaborate_create` together before designing `elaborate_allot`.

## The real gap: runtime data-space allotment is never wired

This is the one non-obvious piece, worth reading carefully before writing
any code. `elaborator::compiled_unit::data_space` (elaborate-time) and
`machine::forth_state::data_space()` (runtime, what `eval_direct.hpp`'s
`eval_program` and `vm.hpp`'s `run` actually operate on) are **two
different `data_space<MaxData>` objects**. Elaboration allots cells in the
first one (to compute addresses); nothing today ever allots the
*same* cells in the second one. A fresh `forth_state` starts with
`data_space().size() == 0` (nothing allotted yet), so `X @`/`X !` against
an address F11 already resolved will immediately diagnose "data space
address out of bounds" from `data_space::fetch`/`store`'s own existing
bounds check — not because the address is wrong, but because the *runtime*
arena was never told how many cells to reserve.

`machine::compiled_program::data_space_size` (F14) already carries the
right number (`unit.data_space.size()` at codegen time, copied verbatim —
see `instruction.hpp`) but nothing consumes it yet (F14's own handoff.md
section says so explicitly: "not yet consumed by the VM itself, F16 wires
`@`/`!`/`+!`"). `eval_direct.hpp`'s `eval_program` has the equivalent
number available too (`unit.data_space.size()`, the `compiled_unit` it is
already given).

F16 needs to decide **where** the runtime `forth_state::data_space()` gets
its cells allotted before any `@`/`!`/`+!` runs. Two live options:

1. **Inside `eval_program`/`vm::run` themselves**, as their first action —
   `state.data_space().allot(unit.data_space_size)` (or the equivalent
   field/parameter each function already has access to) before evaluating
   anything else. This keeps every caller's contract simple: hand `run` a
   fresh `forth_state`, it does the right thing.
2. **Left to the caller** (test helpers, and — see below —
   `compiled_forth_program::run()`, F15) to allot before calling
   `run`/`eval_program`.

Option 1 is very likely the better default (it matches "a fresh
`forth_state` just works," the same expectation F15's own
`compiled_forth_program::run()` already has for every other aspect of
running a program), but it is F16's own design call, not dictated by the
plan.

**Cross-file dependency to check**: `src/smd/forth/forth.hpp`'s
`compiled_forth_program::run()` (F15, this step) currently does:

```cpp
state_type state{};
auto status = machine::run(program_, state, fuel);
```

with no data-space setup of its own. If F16 chooses option 1 above, this
keeps working unchanged. If F16 chooses option 2, **this line needs a
one-line update** (`state.data_space().allot(program_.program().data_space_size)`
or similar, before calling `machine::run`) or every `VARIABLE`/`CREATE`-using
program run through the public `compiled_forth<Source>` API will spuriously
fail with a data-space bounds error despite compiling and elaborating
correctly. Check this file when F16 is done, even if F16's own merge
criteria are satisfied purely through `eval_direct.test.cpp`/`vm.test.cpp`
(the plan's own literal wording, "through both evaluators," does not
mention `compiled_forth` — F16 is not obligated to add `forth.test.cpp`
coverage, but should not silently leave the public API broken for memory
words either).

## What F16 most likely needs to add

- **New `primitive` enumerators** (`forth_state.hpp`): something like
  `fetch_op` (`@`), `store_op` (`!`), `plus_store_op` (`+!`) — trailing
  `_op` (or similar) to dodge C++ keyword/operator-name collisions, matching
  this file's existing convention (`mod_`, `abs_`, `dot_s`, etc. — see the
  enum's own doc comment for the naming rule already in place).
- **New `apply_primitive` cases** (`forth_state.hpp`): `@` pops an address
  cell, converts to `addr` (`static_cast<addr>` — check `addr`'s own
  explicit-conversion constructor), calls `state.data_space().fetch`,
  pushes the result (propagating a diagnosed error, e.g. out-of-bounds, the
  same way every existing primitive already propagates `pop`/`push`
  failures). `!` pops a value then an address, calls `store`. `+!` pops a
  delta then an address, fetches, adds, stores back (`ADD-TO`, standard
  Forth-2012 semantics for `+!`).
- **New `default_dictionary` rows** (`dictionary.hpp`): `{"@",
  primitive::fetch_op}`, `{"!", primitive::store_op}`, `{"+!",
  primitive::plus_store_op}` (adjust names to whatever the enum ends up
  using) — and update the `std::array<..., 42>` size literal and the
  function's own doc comment ("42" appears in both the array type and the
  `@tparam MaxWords` doc note; both need to grow by 3, or however many new
  primitives are added).
- **`elaborate_allot`** (`elaborate.hpp`), most likely: a new elaborate-time
  form recognizing `ALLOT` immediately following a numeric literal
  immediately following a `CREATE` (or, per Forth-2012, `ALLOT` more
  generally consuming a preceding literal and extending the *most
  recently created* word's own allotment — check Forth-2012's actual
  semantics before assuming `CREATE`-then-`ALLOT` must be textually
  adjacent with nothing between them; the plan's own merge criterion
  example, `CREATE BUF 4 ALLOT`, is adjacent, but real Forth allows
  `CREATE BUF 4 ALLOT` in the same sense `CREATE` conventionally works
  with intervening code before `ALLOT` in some dialects — decide and
  record whichever reading this step adopts, and file a DIV if this
  project's own choice narrows Forth-2012's actual grammar).
- **Reader-level check**: confirm `@`, `!`, `+!` scan as ordinary
  `body-item` `word` tokens already (they are not in
  `read_program.hpp`'s own `is_reserved_word` list, so they should already
  parse as generic words with no grammar changes needed — only
  `elaborate.hpp`'s name resolution and the dictionary need to know about
  them). Confirm `ALLOT` similarly, unless F16's own design makes it a
  *reserved* word requiring grammar support the way `VARIABLE`/`CONSTANT`/
  `CREATE` already are (`is_reserved_word` already lists `CREATE`; check
  whether `ALLOT` needs the same top-level-form treatment those three get,
  which would mean touching `read_program.hpp`'s grammar too, not just
  `elaborate.hpp`).

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary — both
  verified green in this worker's sandbox for F15 (`make compile`, `make
  test` at 198/198, `make compile-headers`, `make lint` clean after one
  reformat-then-reverify cycle, `smoke.sh gcc-16`/`smoke.sh clang-21` both
  `SMOKE OK`).
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern) plus a matching `TEST_CASE`. The
  plan's own F16 merge criteria are `static_assert`s "through both
  backends" — add them to `eval_direct.test.cpp` and `vm.test.cpp`
  (existing files, existing per-file capacity constants and `compile()`/
  `run_program()` helpers already in place; follow their existing style).
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3); `data_space<MaxData>` is already flat by
  construction (a `static_vector<cell, MaxData>`, not a tree) — unchanged.
- All capacities are template parameters with defaults; no hardcoded
  capacity constants.
- This project now has **one** piece of negative-compile-test
  infrastructure (`src/smd/forth/CMakeLists.txt`'s `try_compile()` call
  checking `neg_compile_syntax_error.cpp`, added this step, F15) — narrowly
  scoped to one file/one claim, not a general harness (F21 is still where
  general infrastructure is planned). F16 does not need one of its own
  unless it specifically wants to prove some *new* class of compile error
  (unlikely — F16 is wiring runtime primitives, not new grammar/elaboration
  failure modes beyond what already exists).
- Before handoff: `make compile`, `make test`, `make lint` green on
  `gcc-16` (and `clang-21` if available); both `smoke.sh` runs end
  `SMOKE OK`; `checklist.md` ticked; `handoff.md` appended (not rewritten);
  `handoff-next.md` rewritten for whatever comes next (F17, counted loops,
  per `checklist.md`'s own ordering, unless F17 or F18a land first in a
  concurrent worktree — check `checklist.md` again at handoff time, not
  just at start time); divergence docs filed for anything done differently
  than `docs/forth-plan.md` or Forth-2012 semantics (next free number:
  **DIV-0009 is the latest**, `accepted-permanent`, filed this step (F15)
  for `compiled_forth`'s own default-capacity choices, the new narrowly-
  scoped negative-compile-test mechanism, and the `compile-time-forth.org`
  anchor-update choice).
- **CRITICAL LINT PROTOCOL** (learned the hard way by at least two prior
  steps' agents, called out explicitly in this step's own task briefing):
  `make lint` runs `clang-format`/`gersemi` and **will silently reformat
  files**. After all edits, run `make lint`; if `git status` shows any
  modified files afterward, restage and run `make lint` again; repeat
  until a **fresh** `make lint` leaves `git status --porcelain` completely
  empty. Only then commit. This step (F15) hit exactly one reformat cycle
  (clang-format normalized spacing this step's own hand-written new files
  did not initially match) — a second, fresh `make lint` run afterward
  left the tree unmodified, confirmed via `git status --porcelain`.

## What F15 leaves behind that F16 can build on

- **`smd::forth::compiled_forth<Source, ...>`** (`src/smd/forth/forth.hpp`):
  the public one-shot API — `read_program -> elaborate -> codegen` run at
  compile time, `.value()` at every stage (hard compile error on
  parse/elaboration failure), stored as a `compiled_forth_program` wrapper
  with `.run()` (`-> result<forth_state<...>>`), `.stack()` (data stack
  snapshot, bottom to top), `.output()` (accumulated output buffer). Every
  pipeline capacity is a further template parameter with a documented
  default (see DIV-0009). F16 does not need to touch this file unless it
  chooses "option 2" for the runtime-data-space-allotment gap above (see
  that section for exactly what would need to change).
- **`src/smd/forth/neg_compile_syntax_error.cpp`** +
  `src/smd/forth/CMakeLists.txt`'s `try_compile()` block: the pattern to
  follow if any future step needs its own negative-compile check (F16
  almost certainly does not).
- **`src/examples/hello.cpp`** and **`src/examples/godbolt_forth.cpp`**:
  both now run real Forth programs through `compiled_forth` and print real
  output/results (no longer the placeholder's hardcoded string). Neither
  needs changes for F16 unless F16 wants to add a memory-words example
  (not required).
