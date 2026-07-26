# DIV-0019: effect-lint CFG design — instruction-level worklist, exit-agreement relaxation, LEAVE poisoning, and capacity/position choices

- **Status:** accepted-permanent
- **Date:** 2026-07-26
- **Step:** F30 (the effect lint), docs/forth-plan-2.md
- **Authority diverged from:** the deleted R1 elaborator's own `elaborator/stack_effect.hpp`
  (`analyze_body`/`check_exit_in_do_loops`/`analyze_colon_effect`), whose *behavior* this step
  reproduces per its own step text, not whose code it copies (the tree those functions walked no
  longer exists); DIV-0006 (`docs/divergences/DIV-0006-exit-not-flow-sensitive.md`), whose own gap
  this step was asked to close "if your design allows, and say so either way."

## What diverged

D20 describes the checker's *diagnoses* (branch/loop net disagreement, `>R`/`R>` imbalance, `EXIT`
inside `DO` without `UNLOOP`, declared-vs-computed mismatch) and says it runs "over instructions,"
but does not prescribe an algorithm. This step's own `interpreter::effect_lint.hpp` (`instr_effect`,
`instruction_effect`, `instr_edges`, `instruction_successors`, `recover_loop_regions`,
`recover_basic_blocks`, `check_definition_effect`) makes five concrete design choices the plan's own
wording did not settle, plus two capacity/position choices ordinary to this codebase's own style
but worth recording:

**1. A uniform instruction-level worklist, not a per-construct tree fold.** Every reachable
instruction is assigned a *level* — a `(data-stack, return-stack)` depth pair relative to the
definition's own entry — via a fixed-point worklist requiring every edge reaching the same
instruction to agree. A branch's two arms and a loop's back edge are both just edges under this one
mechanism, so the F17 corrections (`DO`'s own two-cell entry cost, `+LOOP`'s own net-+1 body) and
the F17 residual (`>R` unbalanced across a loop boundary, Part 11's own recorded gap) all fall out
of the *same* join-consistency check, rather than needing their own per-construct code paths the
way the deleted elaborator's `analyze_body` had one `if constexpr` arm per node kind. This is also
what let this step recover a *real* CFG (D24: "shared with F33's sender lowering") instead of a
tree-shaped analysis with no independent existence outside this one checker.

**2. Multiple reachable exit points (`ret`/`does_enter`/`halt`) are not required to agree with each
other.** DIV-0006 named `: F DUP 0< IF EXIT THEN DROP ;` as a genuinely different-shaped definition
(an early `EXIT` leaves a different depth than the fall-through) the deleted elaborator silently
assumed consistent rather than checked; this project's own `interpreter::corpus::
exit_boundary_program` (`INNER`) is exactly this shape and is deliberately, validly present in the
acceptance corpus. A first draft of this checker required every exit to agree, diagnosing this case
as an error — which would have rejected `INNER`, a program this step's own instructions explicitly
say must keep passing. The design settled on: multiple *known* exits disagreeing makes the
definition's own net effect `unknown` (stored, not guessed at) — never itself an error. A *declared*
effect changes this (D20's own "hard gate exactly when declared"): every reachable *known* exit is
checked against the declaration individually. This is what actually closes DIV-0006 for the case
that matters — a written contract can no longer silently disagree with an early `EXIT`, whichever
arm the old checker happened to fold last — while an *undeclared* multi-shaped word is accepted
exactly as Forth-2012 permits.

**3. `LEAVE`'s own local effect is `unknown_effect`, not `identity_effect`.** This is a direct port
of the deleted elaborator's own `core_leave` case ("LEAVE poisons to unknown \[...\] lets `... IF
<stuff> LEAVE THEN ...` type-check despite the LEAVE arm and the empty else-arm differing in
shape"), not an independent rediscovery — but it was rediscovered independently before this
divergence record was written, by hand-tracing `interpreter::corpus::find5_program` (`10 0 DO I 5 =
IF I LEAVE THEN LOOP`) against a first draft that gave `LEAVE` `identity_effect`: `LEAVE`'s own
target (the loop's own exit point) is a genuine join with the loop's normal-exhaustion path, and the
`LEAVE` arm here leaves one extra cell (`I`) the exhaustion path never pushes. A known, no-op
`LEAVE` would make this a hard, always-on diagnosed conflict at that join — exactly the corpus
regression this step's own instructions warned against ("an advisory lint must not start rejecting
corpus programs that legitimately have unknown or unbalanced-looking effects"). Unlike `EXIT`
(diagnosis 2, above), `LEAVE`'s target is a real shared continuation, not a separate terminal exit,
so it cannot use the same "disagreement is fine" treatment; `unknown` is the correct lattice value
for exactly the reason the deleted code already recorded.

**4. A declared effect's own gate is skipped wholesale once any reachable instruction's own local
effect is unknown, not checked per-exit against a possibly-undercounted `required_depth`.** A first
draft checked `required_depth` (this checker's own computed minimum entry depth) against a declared
effect's own inputs unconditionally whenever a declaration was present. This is unsound: an
`EXECUTE`/`CATCH`/`?DUP`-reached instruction's own local effect is `unknown_effect` (`inputs`/
`outputs` meaningless per the lattice's own contract), so it contributes `0` to `required_depth`'s
own running maximum — an *undercount*, not a sound bound. A declared `: RUNIT ( xt -- ) EXECUTE ;`
genuinely needs one cell (to pop the token), but this checker cannot see that, and a naive
comparison would reject a *correct* declaration because its own computed `required_depth` (0) does
not match the true, unverifiable requirement (1). The fix: track whether any reachable instruction's
own local effect was ever unknown (`any_unknown_reached`); the whole declared-effect comparison
(both the input side and, out of caution, the output side too) is skipped once it is, and the
declaration is trusted wholesale instead — exactly the deleted elaborator's own precedent
(`declared_present && declared.known && summary.eff.known` gated the comparison there too; an
unknown-touching definition's declared effect was always trusted, never partially argued with).

**5. One genuine behavioral widening versus the deleted elaborator's own per-construct rules:**
`: OKWHILE BEGIN DUP DUP WHILE DROP ... REPEAT ;`-shaped code — a `BEGIN...WHILE` condition that
leaves *more* than the one flag `WHILE` consumes, but whose body then removes the surplus before
`REPEAT` rejoins `BEGIN`'s own dest — is accepted here. The deleted elaborator's own
`core_begin_while` case checked the *condition alone* ("BEGIN...WHILE condition must leave exactly
one flag for WHILE"), independent of the body, and rejected this shape unconditionally. This
checker instead verifies whole-cycle consistency at the loop's own back edge — the same uniform
join mechanism diagnosis 1 describes — which this specific combination happens to satisfy (the
condition's own surplus and the body's own compensating `DROP` cancel exactly). This is strictly
*more* permissive, never unsafe: every reachable instruction's own minimum entry depth is still
verified unconditionally, and a body that does *not* compensate (`interp.test.cpp`'s own
`BeginWhileConditionWrongFlagCountDiagnosed`, an empty body) is still diagnosed via the same
mechanism. `interp.test.cpp`'s own
`WhileConditionLeavingExtraCellsIsAcceptedWhenTheCycleIsConsistent` records this directly, since the
program it accepts would have been one of F12's own negative tests under the deleted per-construct
rule.

**6. Diagnoses carry the position of the closing `;`, not a per-node position.** `machine::instr`
has no source-position field of its own (D16's own retained opcode/instr/VM shape, unchanged by
this step) — codegen only ever threads a position through `machine::emit`'s own diagnosed-overflow
path, never storing one on the instruction itself. The deleted elaborator's own checker positioned
every diagnosis at the offending tree node's own span; this checker cannot, and does not attempt to
retrofit position-carrying instructions (a materially larger change touching `instr`/`emit`/every
opcode's own compiled-form emission, well past this step's own scope). Every diagnosis this checker
raises carries `diag_pos`, the position `interpreter::interpret`'s own `;` handling already has in
hand — coarser than the deleted checker's own positions, but still a real, located diagnosis (D7),
never a bare `foundation::source_pos{}`.

**7. Per-definition working-set capacity (`MaxSpan`) is independent of `MaxCode`.**
`check_definition_effect`'s own node table, worklist, and exit/`ret` lists are indexed by *offset
from* the definition's own entry, capacity `MaxSpan` (default 160), rather than by raw instruction
index bounded by `MaxCode` (`compiled_program`'s own instruction-array capacity, sized for the
*whole session's* ever-growing shared code space under D13, not any one definition). An early draft
sized these containers off `MaxCode` directly (`compiled_program`'s own default, 4096), which made
every single `;` — even for a two-instruction definition — pay constant-evaluation cost proportional
to the whole session's own worst case; `interp.test.cpp`'s own compile time was measured in single-
digit minutes for one file under this draft. `MaxSpan` (a genuinely independent template parameter,
not a repurposed one) fixes this without losing D7's own discipline: a definition whose own
instruction count exceeds it is diagnosed, not silently truncated or left to a container's own
precondition.

**8. Post-merge amendment: a data-stack join disagreement is advisory unless the definition declares
an effect; a return-stack join disagreement is a hard error always.** This is a design decision, not
a bugfix, made after merging F30 with F32 (conformance) exposed a fault neither branch showed alone:
F32's own Hayes ttester (`src/smd/forth/conformance/ttester_corpus.hpp`) is real, standard Forth-2012,
and it failed to compile under this step's own original design. `EMPTY-STACK`'s own body —
`DEPTH START-DEPTH @ < IF DEPTH START-DEPTH @ SWAP DO 0 LOOP THEN` — pads the data stack by a count
known only at the moment it runs; the `DO 0 LOOP` body pushes one cell per iteration for a
runtime-determined trip count, so no static level assignment can ever be consistent across that back
edge, *by construction*, not by an error in this checker's own analysis. None of the ttester's
definitions declare an effect, and D20's own literal wording already covers exactly this case: the
checker is "advisory by default, promoted to a hard gate for a definition exactly when a declared
effect is present." Diagnosis 1 (above) had made every join-consistency check — data and return
stacks alike — unconditionally fatal; that was the actual defect this amendment corrects.

The two stacks are not interchangeable here, which is why this is a *split*, not a blanket
"make join-consistency advisory" change (a blanket change was considered and rejected: it would have
undone the F17 residual closure, since `: BAD 10 0 DO 5 >R LOOP ;` is *also* undeclared, and making
its own return-stack join disagreement advisory would silently re-open Part 11's own gap).

- **Data stack:** a loop body's own net data-stack effect can genuinely *depend on a runtime value*
  (the ttester's own padding count) in a way that is not itself a defect — Forth-2012 places no
  requirement on a `DO...LOOP` body's own net data-stack effect at all. A disagreement here is
  therefore a fact about this checker's own limits (it cannot see a shape that only exists at
  runtime), not a fact about the program. Undeclared, it is collected onto
  `definition_effect::diagnostics` (poisoning that join to `unknown_effect`, exactly like
  `EXECUTE`/`CATCH`/`LEAVE` already do) and the definition still installs. Declared, D20's own
  "promoted to a hard gate" applies literally: the same disagreement is a hard, unconditional error,
  because a human wrote down a specific claim this checker *can* check and found false.
- **Return stack:** Forth-2012 requires the return stack balanced before `LOOP`/`+LOOP`/`UNLOOP` runs,
  unconditionally — this is not implementation-defined, and no runtime trip count changes what the
  standard requires. A `>R` left unbalanced across a loop's own back edge is not "a shape this checker
  cannot see"; it is a definition that will corrupt its own loop frame the moment `LOOP` misidentifies
  which return-stack cell is its own saved index, regardless of how many iterations actually run. The
  justification for keeping this a hard, always-on error is the standard's own requirement, not this
  analysis's own confidence — so declaring an effect changes nothing about it.

Mechanically: `check_definition_effect` gains `MaxDiagnostics` (default 8) and
`definition_effect::diagnostics` (a `foundation::static_vector<foundation::parse_error,
MaxDiagnostics>`); a data-stack join mismatch checks `declared_effect_gate` (`has_declared &&
declared.known`) before deciding whether to return the hard error or collect an advisory one and
poison the join to unknown. The return-stack join-mismatch branch and the "unbalanced at the end of a
definition" check are untouched — no such gate exists there, by design. `machine::compiled_program`
gains its own `MaxDiagnostics` (default 32) and a `diagnostics` field; `interpreter::interpret`'s own
`;` handling copies each definition's own collected diagnostics onto it (capped, silently, at that
capacity — an advisory list running out of room must not itself become a compile failure). A caller
retrieves the whole session's own accumulated advisory diagnostics via
`compile_buffer::program().diagnostics` directly; nothing else surfaces them (no output, no separate
return value from `interpret`), so this field is the one place they are not silently absent.
`foundation::static_vector` gained a `capacity()` accessor (a small, additive, backward-compatible
change) so callers checking "is this list full before I push" do not need the capacity named
separately at the call site.

## Why

Every choice above was forced by a concrete test case this step's own corpus or the F12 battery
already contains — none is speculative. Diagnoses 2–5 were each discovered by hand-tracing a real
program (`INNER`/`OUTER`, `find5_program`, `RUNIT`, `OKWHILE`) against an earlier draft that got it
wrong, not designed in the abstract; DIV-0006's own text ("Close it if your design allows, and say
so either way") is answered directly by diagnosis 2. Diagnosis 1 (the uniform worklist) is the
design that makes 2–5 possible to state as one small set of rules rather than a growing pile of
per-construct special cases — the deleted elaborator's own tree fold could not have absorbed
diagnosis 2 without inventing a second, terminal-aware lattice value (DIV-0006's own revisit
condition names exactly this: "a set-of-return-effects model or a dedicated terminal lattice
value"); recovering a real CFG made that unnecessary; the level-consistency check already
distinguishes "two paths that must agree" (an internal join) from "two paths that need not" (two
separate terminal exits) structurally, for free.

## Consequences

- Every F12 positive and negative test this step's own step-brief names is reproduced, behaviorally,
  over instructions (`interp.test.cpp`'s own `EffectLintTest -` prefix) — but, per amendment 8,
  `BADCOND`/`BADLOOP`/`BADPLUSLOOP`/`BADUNTIL`/`BADWHILE`-shaped programs are *undeclared data-stack*
  disagreements, so they now install (advisory diagnostic collected, `effect_known` left `false`)
  rather than being rejected outright; `BADRET`/`BADRETEND` (return-stack) and `BADDECL`
  (declared-effect mismatch) and `DOEX` (`EXIT` inside `DO` without `UNLOOP`, a structural check
  independent of this amendment) are still hard-rejected exactly as before, and a *declared* variant
  of a data-stack disagreement (`DeclaredDoLoopBodyNonzeroIsAHardError`) is still hard-rejected too.
  `SQUARED`/`ABS2`-shaped declared-and-matching definitions still install. The F17 residual
  (`10 0 DO 5 >R LOOP`, a return-stack disagreement) is still diagnosed, unconditionally — closed,
  not merely narrowed, and not reopened by amendment 8.
- F32's own Hayes ttester (`SMD_FORTH_TTESTER_SOURCE`) and all six `core_suite_*` conformance shards
  now compile and pass, per amendment 8 — the concrete failure this amendment was written to fix
  (`compiled_forth<SMD_FORTH_TTESTER_SOURCE ...>` previously failed to constant-evaluate at all, with
  `EMPTY-STACK`'s own data-stack join disagreement an unconditional hard error).
- The full acceptance corpus (`interpreter::corpus`, `control_flow_corpus.hpp`) passes verbatim,
  including every program this divergence record's own diagnoses 2 and 3 were discovered against
  (`INNER`/`OUTER`, `find5_program`, `first_program`).
- `BEGIN...WHILE`-shaped code whose condition leaves extra cells a body then reabsorbs is now
  accepted where the deleted elaborator rejected it (diagnosis 5) — a documented widening, not a
  silent one; anyone relying on the old rejection (nothing in this project's own corpus did) would
  see a behavior change.
- Diagnoses are positioned at the closing `;`, coarser than the deleted checker's own per-node
  positions (diagnosis 6) — acceptable for a compile error (the offending definition is always
  named by the diagnostic's own surrounding context), but a real loss of precision worth knowing
  about if a future step wants finer positions.
- `check_definition_effect`'s own `MaxSpan` (diagnosis 7) must be raised explicitly by a caller
  compiling one definition larger than 160 instructions; every program in this project's own corpus
  and test suite fits comfortably under it.
- The CFG recovery itself (`instruction_successors`, `recover_basic_blocks`) is a genuinely reusable
  product, not a private detail of this checker — F33's own sender lowering (D24) is expected to
  consume `recover_basic_blocks` directly rather than rebuilding leader/block recovery from scratch.
- `compiled_program::required_stack_depth`/`::required_return_depth` (DIV-0008's own `-1`
  placeholders) are filled, but as a running maximum across every definition closed so far in one
  session whose own peak was computable, not a single whole-program bound — DIV-0008's own literal
  "whole-program peak-depth bound" no longer has a natural referent under D13's shared, ever-growing
  code space (F14's original one-`compiled_program`-is-one-executable-program assumption, which
  DIV-0008 was written against, is no longer this project's own architecture). `machine::
  compiled_colon_word`'s own new `effect_known`/`effect_inputs`/`effect_outputs`/`peak_depth` fields
  are the per-word-precise answer DIV-0008 was actually asking for; the two `compiled_program` fields
  are now best read as a coarse, whole-session safety margin, not a per-program guarantee.

## Revisit condition

Diagnosis 5 (the `BEGIN...WHILE` widening) should be revisited if a future conformance step (F32)
finds a Forth-2012 program whose correctness actually depends on the *old*, stricter per-construct
rejection — none is known to as of this step. Diagnosis 6 (position coarsening) should be revisited
if a future step (F36, error-quality) wants per-node positions for effect-lint diagnoses badly
enough to justify adding a position field to `machine::instr` and threading it through every opcode
emission site. Diagnosis 7's `MaxSpan` default (160) should be raised if a future step's own
definitions genuinely exceed it; nothing in F30 through F31's own corpus does.

Amendment 8's own split (data advisory-unless-declared, return always hard) should be revisited only
if a future step finds a *return*-stack shape that is legitimately runtime-trip-count-dependent the
way the ttester's own data-stack padding is — no such shape is known, and D20's own citation of
Forth-2012's own balanced-return-stack requirement is unconditional, so none is expected. `compiled_
program::diagnostics`' own `MaxDiagnostics` (32) and `check_definition_effect`'s own per-definition
`MaxDiagnostics` (8) should be raised if a future step's own session collects enough advisory
diagnostics to overflow either; nothing in this project's own test suite or conformance battery does
as of this amendment.
