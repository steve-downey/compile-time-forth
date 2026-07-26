# DIV-0014: The cut — public-API reshape, the effect-gate suspension window, and F16's memory words moved into the interpreter

- **Status:** accepted-permanent (the effect-gate suspension window closes at
  F30; everything else here is a permanent design decision, not a temporary
  gap)
- **Date:** 2026-07-26
- **Step:** F26 (the cut), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F26's own step text and
  step-brief leave the exact shape of the `compiled_forth<Source>` retarget,
  and the mechanics of the F16 memory-word merge criterion, to this step's
  own discretion); Forth-2012 (CONSTANT's own argument-passing convention,
  narrowed — see below)

## What diverged

**`compiled_forth<Source>`'s own return shape changed, not just its
implementation.** The step brief resolved *whether* to retarget the public
API this step (yes) but not *how* `.run()`/`.stack()`/`.output()` should
behave once "compile" and "run the top level" stop being separable acts.
R1's `forth_program::run()` returned `foundation::result<machine::forth_state>`
— success or a runtime-recoverable failure (budget exhaustion, for
instance), computed fresh on every call against a caller-chosen fuel. The
retargeted `forth_program::run()` (`forth.hpp`) instead returns
`interpreter::session const&` directly, unconditionally: the whole program,
top level included, already ran exactly once, at `compiled_forth`'s own
namespace-scope `constexpr` initialization, and any failure from that run
already surfaced as a hard *compile* error via `.value()`. There is no
runtime-recoverable failure left for `.run()` to report, because there is no
second, later execution of the top level to fail — building the session *is*
running it. `.stack()`/`.output()` similarly became thin accessors over the
already-built session's own `stack`/`output` fields rather than independent
fresh re-executions.

**The old "runtime error vs. compile error" distinction does not survive
the retarget.** `forth.test.cpp`'s own `RunPropagatesRuntimeError` test
exercised exactly this distinction with `SPIN` (`BEGIN FALSE UNTIL`) under a
deliberately small fuel: a program that "compiles fine but does not
terminate within its fuel budget" used to be an ordinary, recoverable
runtime failure. Under the retargeted API, the same program's budget
exhaustion happens *during* the one build-time evaluation and is therefore a
hard compile error like any other diagnosed failure. This test (and its
static_assert twin) is removed, not adapted — the capability it exercised no
longer exists in this API, by construction, not by oversight. The concept
itself is not lost: `interpreter::build_session`/`interpret` still return a
recoverable `foundation::result`, so a caller working one layer down (as
`session.test.cpp` and `interp.test.cpp` already do) still gets a
recoverable failure channel; only the convenience one-shot API's own
contract changed.

**`interpreter::session` gained a field D15 did not ask for.**
`session::stack` (a `foundation::static_vector<machine::cell, MaxStack>`,
bottom-to-top) snapshots the build-time `forth_state`'s own data stack when
`build_session`'s own `interpret` call finishes. D15's own field list
(code space, dictionary, data-space high-water mark, captured output) does
not mention a stack; it was written before the `forth.hpp` retarget had to
answer "what does `.stack()` return now" concretely. The field is additive
(a new field with a default member initializer breaks no existing
designated-initializer call site) and follows the same reasoning D15 already
applied to `output`: a build-time side effect worth preserving in the
artifact.

**`VARIABLE`/`CREATE`/`CONSTANT` are now interpreter-recognized defining
words**, added to `interpreter::interpret` (`interp.hpp`) by the same
direct-name-comparison technique F25 already used for `:`/`;`/`EXIT`/
`RECURSE`. `docs/forth-plan-2.md`'s own step text for F26 requires this
implicitly: its merge criteria say "the F16 memory-word merge criteria pass
through the new `compiled_forth`," and F16's own merge criterion (recorded
in `docs/history/architecture-grammar-era.org`'s Phase 4/"Memory words
end-to-end" section) is `VARIABLE X ... CONSTANT LUCKY ...` and
`CREATE BUF 4 ALLOT ...`. Under D13 these are no longer grammar productions
(R1's `reader::syn_variable`/`syn_create`, resolved by the elaborator at
`elaborate_variable`/`elaborate_create`/`elaborate_constant`) — there is no
grammar phase left to own them, so the interpreter itself must recognize
them, exactly the way it already recognizes `:`.

**`CONSTANT`'s own semantics narrowed to ordinary Forth-2012, dropping an
R1-specific restriction.** R1's `elaborate_constant` required a *syntactically
preceding integer literal* token (constant-folded at elaboration time); a
`CONSTANT` with no immediately preceding literal was a diagnosed error, and
`3 4 + CONSTANT SEVEN` would not have worked. The interpreter-recognized
`CONSTANT` instead pops whatever the data stack holds at the point it is
met — the standard Forth-2012 reading — so `3 4 + CONSTANT SEVEN` now works
and R1's own syntactic restriction is gone. This is a narrowing of R1's own
scope cut back toward the standard, not a new divergence from Forth-2012
itself.

**`machine::dictionary_binding` lost `colon_word`, and `dictionary.hpp` lost
`stack_effect`/`define_colon`.** DIV-0013 anticipated this closure and left
the rename-or-fold choice for `compiled_colon_word` to this step's own
discretion; F26 keeps the `compiled_` prefix. See DIV-0013's own F26
addendum for the full record; not repeated here.

**The effect-gate suspension window (D20).** Between F26 and F30 there is no
stack-effect *checking*: the R1 checker (`elaborator::analyze_colon_effect`
and its own tree-walk) is deleted along with the rest of the elaborator, and
its replacement (`interpreter/effect_lint.hpp`, retargeted to `instr`
ranges) is F30's own job, not this one. A declared `( ... -- ... )` effect
comment is still captured and stored (`scan_colon_header`, unchanged since
F25) — it is simply not verified against anything in this window. This is a
known, bounded regression, not a silent one: the lattice itself
(`effect`/`combine_sequential`/`combine_branch`/`primitive_data_effect`/
`primitive_return_delta`/`has_declared_effect`/`parse_declared_effect`) is
relocated verbatim to `interpreter/effect_lint.hpp` specifically so F30 has
something concrete to retarget rather than having to reconstruct it.

**The DIV-0012 fold is deferred, not performed.** See DIV-0012's own F26
addendum for the full record; summarized here because it is part of this
step's own divergence footprint: `interpreter::forth_state` remains a
composed wrapper around `machine::forth_state` rather than folding
`input_source`/`BASE`/`STATE` into it directly, on the grounds that F26's
own scope (deletion, retarget, `VARIABLE`/`CREATE`/`CONSTANT`) was already
large enough, and no F26 merge criterion needs the fold — only F28/F29 do.

## Why

The public-API reshape follows directly from D13/D14: once "compile" and
"execute the top level" are the same act, there is no longer a meaningful
seam at which `.run(fuel)` could re-execute *only* the top level with a
caller-supplied fuel distinct from the one used to build the session in the
first place, without either re-running the whole build (parsing and all,
wastefully, and re-defining every word a second time into a discarded
dictionary) or inventing a notion of "just the top-level instructions,
separately callable" that D13 does not provide for. Returning the already-
built session directly is the honest shape: it says exactly what is true
(the program already ran; here is what it left behind), rather than
preserving a method signature (`foundation::result<forth_state>`,
runtime-parameterized fuel) whose own justification (repeatable, cheap
re-execution against a *compiled artifact* distinct from *running* it) no
longer holds once compiling and running are the same evaluation.

`VARIABLE`/`CREATE`/`CONSTANT` as direct-name-recognized words, rather than
waiting for F27's generalized immediate-word dispatch, follows the same
reasoning F25 already used for `:`: this step's own merge criteria need them
working now, and the generalized mechanism is explicitly scoped to F27 by
`docs/forth-plan-2.md`'s own plan text (Phase 8's own architecture-doc
section: "generalized immediate-word dispatch through the dictionary itself
is F27's own job"). Duplicating F27's own work early, or blocking this
step's own explicit merge criterion on F27 landing first, were both worse
than the small, targeted, direct-name addition this step makes instead.

## Consequences

- **Orchestrator note on the negative-compile merge criterion.** F26's own
  criterion reads "the F15 negative-compile test still fails compilation for
  an *unknown word*." The retargeted `test_neg_syntax_error.cpp` instead
  fails on an unterminated colon definition — the nearest analogue of the
  syntax error F15's test actually exercised, now that there is no grammar
  left to produce one. The criterion's intent (a malformed program is a hard
  compile error, proven by a test whose passing condition is that the build
  fails) is met, and the unknown-word path itself is covered as a recoverable
  diagnosis one layer down (`SessionTest -
  BuildSessionDiagnosesMalformedProgram`, `"1 2 + BOGUS"`). What is *not*
  covered is an unknown word specifically as a negative-compile translation
  unit. That is a real, small gap against the criterion as literally worded,
  accepted here rather than expanded into F26's scope at its merge gate.
  **F36 owns closing it** — its own step text already calls for
  "negative-compile tests for syntax error, declared-effect mismatch,
  capacity overflow," and an unknown-word TU belongs in that set.
- `forth.test.cpp`'s `RunSucceeds`/`RunPropagatesRuntimeError` tests are
  replaced: `RunReturnsTheBuiltSessionImage` covers the successful case
  against the new return shape; the runtime-error case has no replacement
  (see "What diverged," above) — its *program* (`SPIN`) and expected
  behavior (budget exhaustion) survive verbatim in
  `interpreter/control_flow_corpus.hpp::spin_program` for F27.
- Any future step adding a public accessor that needs a genuinely
  *re-executable* notion of "run this again, with different capacities or
  fuel" should build it against `interpreter::call_defined_word` (calling a
  specific defined word by name against a fresh state), not by trying to
  resurrect a `.run(fuel)`-shaped API over the whole top level — the whole
  top level is not re-executable without re-building the session from
  scratch, and `compiled_forth`'s own contract is that the session is built
  exactly once.
- `machine/vm.test.cpp` no longer has a source-text `compile()` helper (it
  built one from the now-deleted `read_program`/`elaborate`/`codegen`
  sequence); its own remaining tests hand-build `compiled_program` values
  directly via `machine::emit`. A future step adding new VM-level tests
  should follow this same pattern rather than reintroducing a source-text
  pipeline at this layer — source-text-driven testing belongs at
  `interp.test.cpp`/`session.test.cpp`/`forth.test.cpp` now.
- F30 must retarget `interpreter/effect_lint.hpp`'s tree-walking half
  (which this step does *not* provide — only the lattice moved) to `instr`
  ranges over `interpreter::compile_buffer`, and must account for
  `VARIABLE`/`CREATE`/`CONSTANT` bodies (they have none — they are not
  colon definitions) alongside `compiled_colon_word` bodies.
- F27 must consume `interpreter/control_flow_corpus.hpp` (source text plus
  expected results for `ABS`, `COUNTDOWN`, `SPIN`, `UPTO3`,
  `exit_boundary`, `SUMTO`, `FIND5`, `TENS`, `SUMEVEN`, `FIRST`, plus the
  two non-control-flow memory-word programs for completeness) as its own
  merge-criterion program set.
- F28 (uniform execution tokens) and F29 (parsing words as primitives)
  inherit the deferred DIV-0012 fold; see that record's own F26 addendum.

## Revisit condition

The public-API reshape, the `session::stack` field, the `CONSTANT`
narrowing, and the `colon_word` removal are permanent — nothing plans to
revisit them.
The effect-gate suspension window closes at F30, by construction (that step
retargets the lattice this step relocated).
The DIV-0012 fold's own revisit condition lives on DIV-0012 itself, not
here.
