# Next step: Step F13 — Direct evaluator

Step F12 (stack-effect analysis) is done in worktree `wt-f12` / branch
`step/f12`. This file is a full rewrite for F13 — see `handoff.md`'s "Step
F12 — Stack-effect analysis" section (and everything above it) for the
complete historical record; this file only summarizes what F13 needs to
start.

## What F13 is

Read `docs/forth-plan.md` section "Step F13 — Direct evaluator" for the
authoritative spec. In short: `src/smd/forth/machine/eval_direct.hpp`
(+ test) — a structural-recursive reference interpreter over the elaborated
core:

- Primitives run via F8's `machine::apply_primitive` (already exists,
  imperative, operates on a `forth_state`).
- A call (`core_call`) recurses into the callee's own core (looked up via
  `compiled_unit::dictionary` + `compiled_unit::arena`, exactly the same
  `entry_at(word_index)` / `arena.get(colon_word.core_id)` pattern F12's
  `analyze_body` already uses for effect lookups).
- `core_if` / `core_begin_until` / `core_begin_while` / `core_do_loop`
  become real control flow this time (conditionally/repeatedly evaluating
  their bodies against the live `forth_state`, not just abstractly
  interpreted for depth).
- `core_exit` is an early-return signal threaded through the evaluator's own
  result channel (not a C++ exception — this project's nonlocal control is
  one-shot and dynamic-extent per `handoff.md`'s architectural invariants,
  and F18a's `CATCH`/`THROW` will want the same shape).
- Output words append to `forth_state::output()` (already exists, F8).
- Everything runs under a fuel/step budget: a nonterminating
  `BEGIN ... UNTIL` must be a *diagnosed* budget-exhaustion error via
  `foundation::result`, never a hung `constexpr` evaluation (the compiler
  will happily loop forever evaluating a `constexpr` function that never
  terminates — there is no other way to bound this at compile time).

Merge criteria (end-to-end `static_assert`s, whole pipeline
read → elaborate → eval — see the plan text for the exact four programs):
`SQUARED`, `ABS`, `COUNTDOWN` (stack effect *and* output buffer both
checked), plus a budget-exhaustion test (`SPIN`, `BEGIN FALSE UNTIL`, a
small fuel value) proving the evaluator diagnoses rather than hangs.

Deliverable: architecture-doc section (`docs/compiler_architecture.org`,
Phase 5 — "This section will transclude the direct evaluator once step F13
lands" is the placeholder sentence to replace); this is explicitly called
out in the plan as "the first full-pipeline milestone" — read → elaborate →
eval, all the way through, for the first time.

## The elaborated-core + stack-effect API F13 consumes (Steps F9–F12)

- **`src/smd/forth/elaborator/elaborated_core.hpp`**: `core_node<MaxNodes=
  1024, MaxBody=64>` — twelve alternatives, see `handoff.md`'s "Step F11"
  section for the full per-kind description. The three you'll spend the
  most time on: `core_if{then_body,else_body,pos}`,
  `core_begin_until{body,pos}`, `core_begin_while{condition,body,pos}`,
  `core_do_loop{body,is_plus_loop,pos}` (all `core_body<MaxNodes,MaxBody> =
  static_vector<core_box<MaxNodes,MaxBody>,MaxBody>`), and `core_seq{items,
  pos}` (a definition's whole body, what `colon_word::core_id` points at).
- **`compiled_unit<...>`**: `.arena` (the core `tree_arena`), `.dictionary`
  (`machine::dictionary<MaxWords,MaxName>` — every `colon_word` now carries
  a *real, analyzed* `machine::stack_effect` as of F12, not the F9/F11
  placeholder — you probably don't need to read `.effect` at all for
  evaluation, since you're actually running the code rather than predicting
  its depth, but it's there if a sanity-check assertion turns out useful),
  `.data_space` (`machine::data_space<MaxData>`, F10, real `allot`/`fetch`/
  `store`), `.program` (`core_body<MaxNodes,MaxBody>`, the top-level
  executable body in source order).
- **`src/smd/forth/elaborator/elaborate.hpp`**: `elaborate<MaxNodes,MaxBody,
  MaxName,MaxWords,MaxData,MaxWarnings>(reader::syntax_tree<...> const&,
  std::string_view source) -> foundation::result<compiled_unit<...>>`. **The
  signature grew a second parameter this step** (`source`, the exact program
  text the tree was parsed from) — if you write a `read_program(source)` →
  `elaborate(tree, source)` helper for F13's own tests, remember the second
  argument; several F11-era test helpers needed a one-line fix for exactly
  this when F12 landed (see `elaborate.test.cpp`'s `elaborate_source`).
- **`src/smd/forth/elaborator/stack_effect.hpp`** (new this step): you
  probably don't need anything from here directly — it is F12's own
  analysis machinery, already wired into `elaborate` itself (D9: elaboration
  fails if analysis fails, so by the time F13's evaluator ever sees a
  `compiled_unit`, every definition in it has already passed stack-effect
  checking). One thing worth knowing: `machine::stack_effect` gained an
  `operator==` this step (hidden friend, defaulted) if you want to assert
  anything about it in a test.
- **`machine::apply_primitive(machine::primitive, forth_state<...>&) ->
  machine::status`** (F8, `machine/forth_state.hpp`) — the actual runtime
  behavior for all 37 primitives; this is what a `core_prim` node should
  call. **F12 built a separate, purely declarative net-effect table**
  (`elaborator::primitive_data_effect`/`primitive_return_delta` in
  `stack_effect.hpp`) for its own abstract-interpretation purposes — do not
  confuse the two or try to reuse F12's table for actual evaluation; F13
  wants `apply_primitive`, the imperative one.
- **`machine::forth_state<MaxDepth,MaxRDepth,MaxData,MaxOut>`** (F8): `.data()`/
  `.returns()` (the two stacks), `.data_space()` (F10's real `data_space`),
  `.output()` (append-only char buffer, `emit_char`/`emit_cell` already
  exist). This is almost certainly the state object your evaluator threads
  through by reference.

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary (both
  were available and both verified green in this worker's sandbox for F12 —
  worth checking whether that holds for whoever runs F13 too, but don't
  block on it if only one toolchain is present).
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern); add matching `TEST_CASE`s for Catch2
  visibility.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3) — the evaluator walks the existing core arena; it
  should not need a new tree type, just a recursive walk plus whatever
  accumulator state (a fuel counter, an early-return/EXIT signal) the
  evaluation loop needs threaded through.
- All capacities are template parameters with defaults; no hardcoded
  capacity constants.
- All nonlocal control (`EXIT`, `LEAVE`, `CATCH`/`THROW`) is one-shot and
  dynamic-extent (`handoff.md`'s architectural invariants) — `core_exit`'s
  evaluation should follow this shape, most likely as a tagged result type
  the recursive walk checks after every nested call/body-evaluation and
  propagates upward without running anything further in the current body,
  the same "stop folding, don't visit what comes after" shape F12's
  `analyze_body` already used for `core_exit` (see `stack_effect.hpp`,
  `DIV-0006`) — except this time it's a real runtime early return, not an
  abstract-interpretation shortcut, so it needs to actually unwind back to
  the calling definition's own `EXIT`/fall-through boundary, not just the
  current nested body.
- Before handoff: `make compile`, `make test`, `make lint` green on
  `gcc-16` (and `clang-21` if available); both `smoke.sh` runs end
  `SMOKE OK`; `checklist.md` ticked; `handoff.md` appended (not rewritten);
  `handoff-next.md` rewritten for whatever comes next (F14, per the plan's
  ordering, unless it says otherwise); divergence docs filed for anything
  done differently than `docs/forth-plan.md` or Forth-2012 semantics (next
  free number: check `docs/divergences/` — **DIV-0006 is the latest open
  one**, filed this step for a documented gap in F12's `EXIT` handling —
  DIV-0004 and DIV-0005 are both `accepted-permanent`/resolved).

## Known open items going into F13

- **`EXIT`'s multi-return-path unsoundness** (DIV-0006,
  `docs/divergences/DIV-0006-exit-not-flow-sensitive.md`): F12's *static*
  stack-effect checker does not verify that an early-`EXIT` path and the
  eventual fall-through path leave the same stack depth — it is possible for
  a definition to pass F12's analysis with a computed effect that does not
  actually match every runtime path through it (`: F DUP 0< IF EXIT THEN
  DROP ;` is the worked counterexample in the DIV). This is *not* F13's
  responsibility to fix, but F13's evaluator will actually execute both
  paths and observe the real depths at runtime — if a merge-criterion test
  ever exercises a definition like this, expect the *evaluated* result to
  disagree with whatever effect F12 computed for it; that is expected and
  matches the documented limitation, not a bug in F13.
- **`DO...LOOP` does not yet model limit/start-index consumption** at the
  core level — F12's `core_do_loop` handling (and, presumably, F13's own
  evaluator) has nothing to pop a loop's `(limit start)` pair or expose `I`/
  `J`, because that machinery is F17's own deliverable
  (`do_setup`/`I`/`J`/`LEAVE`/`UNLOOP`). F13's own merge criteria do not
  exercise `DO...LOOP` at all (only `BEGIN...UNTIL`, via `COUNTDOWN`), so
  this is very likely fine to leave exactly as-is for F13 and defer entirely
  to F17 — just don't be surprised that `core_do_loop` evaluation, if you
  implement it at all this step, has no loop-index behavior yet.
- **Fuel/budget design is entirely F13's own to invent** — nothing upstream
  (F8–F12) has any notion of a step budget; the plan's own wording
  ("fuel/step budget") is the only guidance. A simple `int` decremented once
  per evaluated node (or once per loop iteration, or both) that turns into a
  diagnosed `foundation::parse_error` when it hits zero is probably the
  minimal correct shape — this doesn't need to be elaborate, just present
  and tested (the `SPIN` merge criterion).
- The recurring `make lint` `clang-format`/`gersemi` tooling-version drift
  on a shifting subset of pre-existing files (currently
  `machine/{CMakeLists.txt,data_space.hpp}` and
  `reader/forth_chars.test.cpp` in this worker's sandbox; different files
  were flagged at every step since F6) — still unresolved as a standing
  environment issue, not specific to any one step. Whoever eventually does a
  dedicated formatting-hygiene pass should treat it as one item, not
  something to chase file-by-file each step.
