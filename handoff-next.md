# Next step: Step F12 — stack-effect analysis

Step F11 (elaborated core and resolution) is done in worktree `wt-f11` /
branch `step/f11`. This file is a full rewrite for F12 — see `handoff.md`'s
"Step F11 — Elaborated core and resolution" section (and everything above
it) for the complete historical record; this file only summarizes what F12
needs to start.

## What F12 is

Read `docs/forth-plan.md` section "Step F12 — Stack-effect analysis" for the
authoritative spec. In short: `src/smd/forth/elaborator/stack_effect.hpp`
(+ test), run **as part of** `elaborate` (D9) — abstract interpretation over
the elaborated core, computing each definition's net data-stack effect and
minimum entry depth, tracking the return stack separately. Diagnosed:
- `IF`/`ELSE` arms with unequal net effects.
- Loop bodies with nonzero net effect: a `DO` body must be net-zero; a
  `BEGIN ... UNTIL` body must be net-zero *after* the `UNTIL` flag pop; a
  `BEGIN ... WHILE ... REPEAT` condition must leave exactly the flag (net
  effect +1, consumed by `WHILE`).
- `>R`/`R>` imbalance across a control-structure boundary (return-stack
  depth must match going into and out of any `IF`/loop body).
- `EXIT` inside a `DO` loop without `UNLOOP` (F17 is what actually adds
  `UNLOOP`/counted-loop machinery; F12 only needs to *diagnose* this shape
  now, since `core_exit` and `core_do_loop` both already exist).
- Mismatch between a declared `( a b -- c )` effect comment and the
  computed effect.

Words with input-dependent effects (`?DUP`, anything reached only through
`EXECUTE`) get an `unknown` lattice value that suppresses checking
downstream rather than erroring. Record the lattice in the architecture doc.

Merge criteria: positive tests verifying a declared effect against the
computed one; one failure test per diagnosis listed above.

Deliverable: extend `docs/compiler_architecture.org`'s "Phase 4:
Elaboration" section (F11 already filled in the resolution half; F12 adds
the stack-effect-checking half) with a transclusion of the new checker.

## The elaborated-core API F12 consumes (Step F11)

See `handoff.md`'s "Step F11" section for full detail; summary:

- **`src/smd/forth/elaborator/elaborated_core.hpp`**: `core_node<MaxNodes=
  1024, MaxBody=64>` is the closed node variant (no `MaxName` — every name
  is already resolved to an index/addr/value by this point). Twelve
  alternatives: `core_push{cell,pos}`, `core_prim{machine::primitive,pos}`,
  `core_call{word_index,pos}`, `core_var{machine::addr,pos}`,
  `core_const{cell,pos}`, `core_push_xt{word_index,pos}`, `core_exit{pos}`,
  `core_if{then_body,else_body,pos}`, `core_begin_until{body,pos}`,
  `core_begin_while{condition,body,pos}`, `core_do_loop{body,
  is_plus_loop,pos}`, `core_seq{items,pos}` (a definition's whole body, the
  node `colon_word::core_id` points at). Every node kind carries its own
  `foundation::source_pos pos` — real positions are available for every
  diagnosis F12 needs to attach one to.
- **`compiled_unit<MaxNodes=1024, MaxBody=64, MaxName=32, MaxWords=256,
  MaxData=1024, MaxWarnings=64>`**: `.arena` (the core `tree_arena`),
  `.dictionary` (`machine::dictionary<MaxWords,MaxName>`, 37 primitives
  pre-installed plus every colon/variable/constant word the program
  defined), `.data_space` (a real `machine::data_space<MaxData>`),
  `.program` (`core_body<MaxNodes,MaxBody>`, the top-level executable
  body), `.warnings` (`warning_log<MaxWarnings>`, currently only
  redefinition notices).
- **`src/smd/forth/elaborator/elaborate.hpp`**:
  `elaborate<MaxNodes,MaxBody,MaxName,MaxWords,MaxData,MaxWarnings>(
  reader::syntax_tree<MaxNodes,MaxBody,MaxName> const&) ->
  foundation::result<compiled_unit<...>>`. Walks the syntax tree once, in
  program order, threading the dictionary forward.
- **`machine::stack_effect{int inputs=0, int outputs=0, bool known=false}`**
  (`machine/dictionary.hpp`, landed in F9) is the shape
  `machine::colon_word::effect` already has. F11 always installs
  `stack_effect{}` (i.e. `known = false`) for every colon word it defines
  — **F12 is the first step that ever computes or checks a real effect**.
  `dictionary` has no in-place mutation method for an already-inserted
  entry (only `insert`/`lookup`/`lookup_index`/`entry_at`, all
  append-or-read-only) — the natural integration seam is *inside*
  `elaborate_colon_def` (in `elaborate.hpp`), between "the body has been
  elaborated into a `core_seq`" and "`define_colon` is called": compute the
  effect there and pass the real `stack_effect` into the `colon_word` at
  the point it is first constructed, rather than trying to patch it in
  after the fact.
- `RECURSE` resolves to `core_call{word_index = self_index}` where
  `self_index` is the *currently-being-compiled* definition's own eventual
  dictionary index (see `handoff.md`'s "RECURSE's self-index trick" note
  for why this is knowable before the word is actually defined). **A
  `core_call` back to the definition currently being analyzed is a real
  design problem for F12's abstract interpretation**: the callee's net
  effect is exactly the thing being computed, so it is not yet known when
  the interpreter reaches the `RECURSE` call site. Two ways out, neither
  implemented yet: (a) require a declared `( ... )` effect comment on any
  recursive definition and trust it as the assumed effect of the `RECURSE`
  call site (verify everything else against it), or (b) reuse the same
  `unknown` lattice value the plan already specifies for `?DUP`/
  `EXECUTE`-reached code, treating a self-call as effect-unknown and
  suppressing the net-effect check for that definition (still checking
  everything checkable, e.g. `IF`-arm-balance elsewhere in the same body).
  This project's plan text does not pick one — it is F12's call, documented
  wherever F12 documents its lattice.
- **F11 does *not* thread the original source text through `elaborate`** —
  `elaborate`'s only parameter is the `syntax_tree`, and
  `reader::syn_colon_def::declared_effect` is a `foundation::source_span`
  that is meaningless without the original source string it indexes into.
  Since F11 never needed to read a declared effect's *text* (only F12
  does), this was never threaded through. **F12 will need the source text
  to slice `declared_effect` into e.g. `"( a b -- c )"` for comparison
  against what it computes.** The natural fix, since F12's own checker
  is meant to run *as part of* `elaborate` (D9) and `elaborate_colon_def`
  already has the `syn_colon_def` — including its `declared_effect` span
  — in scope: add a `std::string_view source` parameter to `elaborate`
  (and thread it down to wherever the effect check runs), rather than
  inventing a second pass that re-walks the syntax tree separately. This
  is a real signature change to `elaborate.hpp`'s public entry point, not
  an addition — expect to touch `elaborate.test.cpp`'s call sites too.
- **Primitive net effects are not tabulated anywhere yet.** F8's
  `apply_primitive` (`machine/forth_state.hpp`) is imperative runtime code,
  not a declarative effect table — F12 needs to build its own mapping from
  `machine::primitive` to a static `(inputs, outputs)` pair (35 of the 37
  primitives have one; `?DUP` is the one genuinely input-dependent
  primitive and gets the `unknown` lattice value; `>R`/`R>`/`R@` move
  values between stacks and need special-casing in the return-stack
  tracking rather than an ordinary data-stack `(inputs,outputs)` pair).

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary.
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern where a parser/elaboration call is
  needed); add matching `TEST_CASE`s for Catch2 visibility.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3) — the abstract interpreter walks the existing
  core arena; it should not need a third tree type of its own, just
  accumulator state (net effect, min depth, return-stack depth) threaded
  through the same mutually-recursive-function-template walk shape F7/F11
  both used (DIV-0005's pattern — expect to reach for it a third time).
- All capacities are template parameters with defaults; no hardcoded
  capacity constants.
- Before handoff: `make compile`, `make test`, `make lint` green on
  `gcc-16` (and `clang-21` if available); both `smoke.sh` runs end
  `SMOKE OK`; `checklist.md` ticked; `handoff.md` appended (not rewritten);
  `handoff-next.md` rewritten for whatever comes next (F13, per the plan's
  ordering, unless it says otherwise); divergence docs filed for anything
  done differently than `docs/forth-plan.md` or Forth-2012 semantics
  (next free number: check `docs/divergences/` — DIV-0005 is the latest
  open one; DIV-0004 is resolved as of F11).

## Known open items going into F12

- **The declared-effect/source-text gap** above — `elaborate`'s signature
  likely needs a `std::string_view source` parameter added; not yet done
  by anyone.
- **The `RECURSE`-self-effect fixed-point question** above — needs a
  documented design decision (declared-effect-as-ground-truth vs.
  `unknown`-lattice suppression); not yet made by anyone.
- **The primitive net-effect table** — does not exist yet; F12 builds it.
- The recurring `make lint` `clang-format`/`gersemi` tooling-version drift
  on a shifting subset of pre-existing files (currently
  `machine/{CMakeLists.txt,data_space.hpp}` and
  `reader/forth_chars.test.cpp` in this worker's sandbox; different files
  were flagged at every step since F6) — still unresolved as a standing
  environment issue, not specific to any one step. Whoever eventually does
  a dedicated formatting-hygiene pass should treat it as one item, not
  something to chase file-by-file each step.
