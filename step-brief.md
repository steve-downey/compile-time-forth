# step-brief.md — Step F33: The Sender Backend

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory. **F32 (conformance) ran concurrently with F30 and
will already be merged by the time you start — do not assume F32's own state
from this brief; read its own step-brief/checklist entry instead.**

## What F30 built, by anchor

`docs/compiler_architecture.org`'s Phase 14 section ("The Effect Lint")
covers this in full, with transcluded code anchors. Read it before opening
`interpreter/effect_lint.hpp` wholesale.

- **The CFG recovery you need is already built, and is D24's own explicit
  hand-off**: `interpreter::instruction_successors(instr const&, int index)
  -> instr_edges` (`effect_lint.hpp`) is the *one* edge relation over
  `machine::instr` — every opcode's own real successor(s) within an
  instruction range, `-1` meaning "no such edge." `interpreter::
  recover_basic_blocks<MaxBlocks>(program, entry, end_exclusive)` (no
  default for `MaxBlocks` — you must specify it explicitly at each call
  site) partitions a definition's own range into maximal straight-line runs
  using exactly this edge relation, returning
  `foundation::result<foundation::static_vector<basic_block, MaxBlocks>>`
  (`struct basic_block { int start; int end; }`, half-open). This *is*
  "one analysis, two clients" (D24's own words): F30's own checker walks
  the same edges at per-instruction granularity (a worklist, not blocks —
  simpler for a checker that only cares about levels); you want the
  block-partitioned view directly.
- **`instruction_successors` does *not* model `op::catch_mark`'s own
  resume-ip edge.** `catch_mark`'s own operand is a real jump target (the
  instruction right after the paired `prim catch_ok`, reached directly by
  a caught `THROW` rather than by falling through `catch_ok`'s own normal-
  completion path) — a genuine second control-flow edge out of that
  instruction. F30's own checker didn't need it (`catch_mark`'s own local
  *effect* is `unknown_effect` regardless of which edge is taken, so
  omitting the edge cost nothing for level-checking purposes), so
  `instruction_successors` only returns the default single successor
  (`index + 1`) for it. If your own sender lowering needs `CATCH`'s two
  real completions (normal vs. caught) as two separate wired
  continuations, you will need to add that edge yourself — either by
  special-casing `op::catch_mark` in your own block/edge walk, or by
  extending `instruction_successors` itself (the latter is probably
  cleaner: nothing in F30's own checker would be affected by `catch_mark`
  gaining a second, always-known edge, since the checker's own worklist
  already tolerates unknown-poisoned edges without needing them to be
  absent). Given D24's own context ("unstructured return-stack
  manipulation does not refunctionalize and falls back to the VM inside a
  single sender"), you may already plan to handle `CATCH`/`THROW` via a VM
  fallback rather than real sender continuations — if so, this gap may not
  block you at all, but it is worth confirming rather than discovering
  partway through.
- **The edge relation is orthogonal to the effect lattice's own `unknown`.**
  `instruction_successors` always returns real, structural edges regardless
  of whether `instruction_effect`'s own *data*-effect for that instruction
  is `known` or `unknown` (`LEAVE`/`EXECUTE`/`CATCH` all still have real,
  fully-modeled successors; only their *effect* is unknown). Do not
  conflate the two: for sender lowering, every edge `instruction_successors`
  reports is real control flow you must wire, independent of whether F30's
  own checker could verify anything about the data stack across it.
- **`machine::compiled_colon_word` gained four fields**: `effect_known`,
  `effect_inputs`, `effect_outputs`, `peak_depth` (`-1` unless
  `effect_known`) — the checker's own per-word, durable output. Not
  consumed by anything F30 itself wrote beyond `interpreter::
  instruction_effect`'s own `op::call` case (looks up a callee's stored
  effect by matching `entry_point`); available to you if useful, not
  required.
- **`compiled_program::required_stack_depth`/`::required_return_depth`**
  (DIV-0008's own `-1` placeholders) are now filled, but as a running
  maximum across every definition closed so far in a session whose own
  peak was computable — not a per-word number, and not computed at all for
  any definition touching `unknown` anywhere. Do not read these as a
  precise bound for any one word; use `compiled_colon_word::peak_depth`
  instead if you need a per-word figure, and expect `-1` on it often (any
  word using `EXECUTE`/`CATCH`/`?DUP`/a call to such a word).
- **Diagnoses fire at `;`,`interpreter::interpret`'s own compiling path**,
  positioned at the closing `;` itself (`machine::instr` still carries no
  source position of its own, D16 unchanged) — irrelevant to your own
  lowering, since by the time your code runs, every definition it sees has
  already passed this gate.

## Gotchas F33 needs and cannot get from its own pasted section

- **`recover_loop_regions<MaxCode, MaxWords, MaxRegions = 64>`** recovers
  `DO...LOOP`/`+LOOP` regions by matching `do_setup` to its own
  `loop_step`/`plus_loop_step` by dest — a purely structural, non-CFG
  helper (a plain scan, not using `instruction_successors` at all). If your
  own loop-as-repeat-until-composition lowering wants this pairing, it is
  already there; you do not need to re-derive it.
- **A definition's own per-analysis working set (`check_definition_effect`'s
  own `MaxSpan`, default 160) is independent of `MaxCode`** — if you add
  your own per-definition analysis, follow the same pattern (index by
  offset from `entry`, a small dedicated capacity template parameter, not
  `MaxCode` reused directly): an early F30 draft sized its own working set
  off `MaxCode`'s multi-thousand default and made every single `;` pay for
  the whole session's own worst case, turning a few dozen `interpret()`
  calls in one test file into several minutes of constant-evaluation time.
  `recover_basic_blocks` already takes its own explicit `MaxBlocks` for the
  same reason — do not give it a large default either.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.** Watch for codespell false positives on
  short local variable names that happen to collide with common words
  (a two-letter abbreviation for "flow node" was one F30 hit; renamed to
  the unabbreviated spelling).
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F30 used DIV-0019 (DIV-0020 is next).

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for **F34** (foreign function interface); DIV filed for any
deviation, using the number you are given.
