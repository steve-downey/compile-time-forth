# DIV-0010: F17 adds the `1+` primitive and refines DO-loop stack-effect analysis

- **Status:** accepted-permanent
- **Date:** 2026-07-24
- **Step:** F17 (counted loops)
- **Authority diverged from:** docs/forth-plan.md and Forth-2012

## What diverged

Implementing F17's counted loops (`DO LOOP +LOOP I J LEAVE UNLOOP`) surfaced
three things not spelled out by `docs/forth-plan.md`, resolved here:

1. **The `1+` primitive did not exist.** F17's first merge-criterion program is
   verbatim `: SUMTO 0 SWAP 1+ 0 DO I + LOOP ;`, but the default dictionary had
   only `1-` (`primitive::one_minus`, itself added by DIV-0007 for the same kind
   of reason in F13) -- no `1+`. This step adds `primitive::one_plus`
   (`( a -- a+1 )`) alongside `one_minus`: an enumerator in `machine::primitive`
   (`forth_state.hpp`), a runtime arm in `apply_primitive`, the `{"1+", ...}`
   entry in `default_dictionary` (`dictionary.hpp`, array size 42 -> 43), and a
   `known(1, 1)` entry in `primitive_data_effect` (`stack_effect.hpp`). This is
   an addition to the `primitive` enum, which the F17 step-brief otherwise asked
   this worker to leave alone (F16 edits it concurrently) -- but the caution was
   specifically about the *control words* `I`/`J`/`LEAVE`/`UNLOOP`, which are
   kept as dedicated core nodes and are **not** in the enum. `1+` is an ordinary
   pure-arithmetic primitive with no control semantics; the only cost is a
   trivial textual merge with F16 in three files, which the orchestrator's
   rebase-F17-onto-F16 step absorbs.

2. **F12's `core_do_loop` stack-effect model did not account for `DO` consuming
   `( limit start )`.** Before this step, the analysis modelled a
   `DO ... LOOP`'s own contributed effect as `known(body.inputs, body.inputs)`
   -- net zero -- ignoring the two cells `DO` pops. That is invisible for a
   single top-level loop (a colon body has no net-zero requirement), but it made
   **every nested counted loop fail** diagnosis 2: the inner loop's own
   `limit start` are pushed *inside* the outer loop's body, so the outer body
   came out net `+2` and was rejected as "DO loop body must have a net-zero
   stack effect." This step models the two consumed cells:
   `item_eff = known(body.inputs + 2, body.inputs)`, so a nested loop's inner
   `limit start DO ... LOOP` contributes net `-2` and the surrounding body
   balances. This is a refinement of an imprecise F12 model, not a relaxation of
   any check.

3. **`+LOOP` bodies are net `+1`, not net `0`.** A `+LOOP` body leaves the loop
   increment on the data stack for `+LOOP` itself to consume, so requiring a
   net-zero body (as the plain-`LOOP` rule does) wrongly rejected
   `: SUMEVEN 0 10 0 DO I + 2 +LOOP ;`. The `core_do_loop` analysis arm now
   requires net `+1` for `is_plus_loop` bodies (diagnosed as "+LOOP body must
   leave exactly the increment (net +1)") and net `0` otherwise; the whole-loop
   contributed effect is `known(body.inputs + 2, body.inputs)` either way, since
   `+LOOP` consumes the extra output.

Additionally, F17 **completes** (rather than diverges from) the F12 stub that
`stack_effect.hpp` itself flagged: diagnosis 4 ("EXIT inside a DO loop requires
UNLOOP") was unconditional in F12 because `UNLOOP` did not exist. Now that
`UNLOOP` is real, `check_exit_in_do_loops` treats an `UNLOOP` seen earlier in a
body sequence as clearing the requirement for `EXIT`s after it (and anything
they recurse into), so `... UNLOOP EXIT ...` and `... IF UNLOOP EXIT THEN ...`
are accepted while a bare `EXIT` inside `DO` is still diagnosed. This matches
the "F17 should relax this" note the F12 code left behind.

## Design choices worth recording (not divergences, but non-obvious)

- **`I`/`J`/`LEAVE`/`UNLOOP` are dedicated core nodes** (`core_loop_index` with
  a `level` field for I=0/J=1, `core_leave`, `core_unloop`), recognized in
  `elaborate_word_ref` before dictionary lookup exactly like `EXIT`/`RECURSE`,
  never dictionary entries or primitives. This is the path DIV-0008 anticipated
  and keeps F17 clear of F16's `primitive`-enum edits.
- **Return-stack loop frame layout:** `DO` pushes `limit` then `index` (index on
  top), so `I` reads return-stack offset 0 and `J` offset 2; frames nest above
  the caller's `call`/`ret` return address and are torn down before the matching
  `ret`. `push_index` reads offset `2*level`.
- **`LEAVE`** is one-shot dynamic-extent nonlocal control (D11): in the direct
  evaluator a new `eval_signal::left` propagates up (like `exited`) to the
  nearest `core_do_loop`, which discards the frame and falls out; in the VM it is
  a `leave` opcode emitted with a `-1` sentinel operand, back-patched by the
  enclosing loop to the exit instruction (inner loops patch their own leaves
  first, so an outer loop's back-patch scan only finds its own).
- **`LEAVE` poisons the containing definition's stack effect to `unknown`** (like
  `?DUP`), which is also what lets `... IF <push something> LEAVE THEN ...`
  (e.g. `FIND5`'s `IF I LEAVE THEN`) type-check despite the LEAVE arm and the
  empty else-arm differing in net shape.
- **`+LOOP` termination** uses the Forth-2012 boundary-crossing rule, not a
  simple comparison: with `before = index - limit` and `after = before + n`, the
  loop terminates when `(before ^ after) < 0` (the sign of `index - limit`
  flips). Plain `LOOP` uses the Forth-2012 equality test (`index + 1 == limit`).

## Known limitation (shared with DIV-0006)

Neither the frame teardown on `LEAVE`/loop-termination nor the `EXIT`-in-`DO`
diagnosis is fully flow-sensitive with respect to `>R`/`R>` interleaved inside a
loop body: the teardown assumes the return-stack top is the loop frame at the
transfer point, which holds for well-formed programs (the ones F12's own
per-body return-stack-balance check already enforces) but is not verified across
an early `LEAVE`. No merge criterion mixes `>R` with `LEAVE`/`I` in one body,
and this is the same class of early-exit flow-insensitivity DIV-0006 records for
`EXIT`.

## Revisit condition

Revisit the `1+`/`1-` situation only if the plan later defines a canonical
primitive set that omits them (it currently does not enumerate `1+`).
Revisit the DO-loop analysis model if a future step adds real peak-depth
analysis (DIV-0008) or full flow-sensitive early-exit reconciliation (DIV-0006),
either of which would subsume the conservative `unknown`-poisoning `LEAVE` gives.
