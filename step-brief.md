# step-brief.md — Step F28: execution tokens and defining words

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites (most relevantly D18, "header
unification"); this brief does not attempt to restate that section from
memory — F27 was not given F28's own plan text either, only checklist.md's
title ("execution tokens and defining words") and D18's own context
paragraph, so treat the goal/merge-criterion section below as orientation,
not authority.

## What F27 built, by anchor

`docs/compiler_architecture.org`'s Phase 10 section ("Immediacy and Control
Flow") covers this in prose, with three transcluded code anchors. Read it
before opening any source below wholesale.

- **Every control word is a real dictionary entry now**: `IF ELSE THEN`,
  `BEGIN UNTIL`, `BEGIN WHILE REPEAT`, `DO LOOP +LOOP LEAVE UNLOOP I J`,
  `LITERAL`, `POSTPONE`, `IMMEDIATE`, `[`, `]`, `COMPILE,` — a new
  `machine::dictionary_binding` alternative, `control_word` (a
  `control_builtin` tag, `machine/dictionary.hpp`, anchor
  `0d3b390a-1051-4f76-867c-b4f161264827`), dispatched by
  `interpreter::apply_control_word` (`interp.hpp`, anchor
  `d1a4f7b2-6c8e-4a3d-9b5f-2e7c4a9d1b6e`). `dictionary_entry` also gained a
  `bool immediate` field (D18's own narrow slice: "you need an IMMEDIATE
  flag; you do not need the full unification" — F27 took exactly that and
  no more) and `dictionary::mark_last_immediate`/`define_control`.
- **D13's "execute when interpreting or immediate, else compile" is now one
  rule, not a special case per binding kind**: `interpreter::execute_entry`
  and `interpreter::compile_entry` (`interp.hpp`, both just above the
  `d1a4f7b2` anchor) are the two halves; `interpret`'s own loop calls
  `execute_entry` unconditionally while interpreting, and while compiling
  calls `execute_entry` for an immediate entry or `compile_entry` for an
  ordinary one. If F28's own execution-token/header work adds a new binding
  kind, extending these two functions (rather than re-special-casing inside
  `interpret` itself) is almost certainly the right shape to follow.
- **`:`/`;`/`EXIT`/`RECURSE`/`VARIABLE`/`CREATE`/`CONSTANT` are still direct-
  name special cases**, deliberately not folded into the new dispatch (D13's
  own rule already covered them correctly as-is; folding them was optional,
  and F27 left them alone rather than touching working code without a
  reason). If F28's defining words need to change how `VARIABLE`/`CREATE`
  install their bindings (e.g. `CREATE ... DOES>`), this is the code to
  read first — `interp.hpp`'s own `interpret()`, the `VARIABLE`/`CREATE`
  branch inside the `STATE == 0` half.
- **`POSTPONE` of a C++-native control word only works when it is the
  *entire* body of the definition** (`: ENDIF POSTPONE THEN ; IMMEDIATE`):
  the word being closed becomes a plain `control_word` alias, not a
  `compiled_colon_word`, because a control word has no VM entry point to
  compile a call to. Mixing a postponed control word with any other code in
  the same definition is diagnosed, not supported — see DIV-0015 for the
  full rationale and its own revisit condition (closes if F28's own header
  unification gives control words a representation that composes with
  ordinary compiled code).
- **`COMPILE,` treats a dictionary index as an execution token** — the same
  convention `op::push_xt` already documented (`instruction.hpp`, since
  F14, never wired to anything before now): pop a `machine::cell`, treat it
  as a `dictionary::entry_at` index, append that entry's compiled form. F28
  is where this convention presumably gets a real producer (`'`/`tick`,
  `EXECUTE`); nothing before this step ever pushed a genuine execution
  token onto the data stack from Forth source (F27's own `COMPILE,` test
  drives it directly in C++, not through source text, for exactly that
  reason — see `interp.test.cpp`'s `CompileCommaAppendsAnEntrysCompiledForm`).
- **`default_dictionary`'s entry count is 67 now** (47 primitives + 20
  control words), not 47. Every hand-sized `dictionary<MaxWords, ...>` or
  `build_session<..., MaxWords, ...>` call built close to the old count with
  no headroom needs auditing before adding more default-dictionary entries
  — DIV-0015's own Consequences section names the two call sites F27 itself
  had to fix (`vm.test.cpp`, `session.test.cpp`, both `MaxWords` 64 → 96).

## Gotchas F28 needs and cannot get from its own pasted section

- **The data stack doubles as the control-flow stack while compiling**
  (D17, Phase 10): `IF`/`BEGIN`/`WHILE`/`DO` push orig/dest markers onto the
  *live* `st.machine().data()`, popped by their own closing word. F28's own
  work should not assume the data stack is untouched during compilation of
  a definition that uses control flow — it is, between control-flow words,
  but not always exactly at every token boundary.
- **`compiling_context<MaxName>` (`interp.hpp`) is the per-definition
  compile-time bookkeeping struct** (name/entry/effect/has_effect/
  loop_depth/has_postponed_alias/postponed_target) that replaced four loose
  locals at F27. Extend it, do not reintroduce loose locals, if F28 needs
  more per-definition state (e.g. tracking a `CREATE ... DOES>` word's own
  does-field entry point).
- **An unresolved `IF`/`BEGIN` (no matching `THEN`/`REPEAT`) is *not*
  diagnosed at `;`** — only an unresolved `DO` is (via `loop_depth`). DIV-
  0015 records why a general data-stack-depth balance check was tried and
  reverted (it rejects legitimate immediate-word side effects, like
  `: DOUBLE-IT 21 ; IMMEDIATE` leaving real data behind on purpose). Do not
  reintroduce that check without re-reading DIV-0015 first.
- **`interp.test.cpp`/`forth.test.cpp` now carry direct control-flow merge-
  criterion coverage** (`InterpTest - AbsMergeCriterion` etc.,
  `ForthTest - AbsControlFlowMergeCriterion` etc.) — the full corpus in
  `interpreter/control_flow_corpus.hpp` passes through both `interpret()`
  and `compiled_forth<Source>` now. `SPIN` still cannot go through
  `compiled_forth<Source>` (a program that never terminates is a hard
  compile error under D13/D14 regardless of control flow existing), and
  that is correct, not a gap to close.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F27 used DIV-0015.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for F29; DIV filed for any deviation, using the number you are
given.
