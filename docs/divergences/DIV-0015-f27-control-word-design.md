# DIV-0015: Control-word binding shape, POSTPONE-of-a-control-word alias, and a reverted balance check

- **Status:** accepted-permanent; the `POSTPONE` scope cut is resolved at F28
  (narrowed, not closed — see the F28 addendum below)
- **Date:** 2026-07-26
- **Step:** F27 (immediacy and control flow), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F27's own step text names D17's
  discipline but leaves the binding shape, `POSTPONE`'s exact mechanism, and the
  `;`-time diagnostics open)

## What diverged

D17 says the control words are immediate words on the standard orig/dest
discipline, "with the data stack serving as the control-flow stack as the
standard permits." It does not specify how a C++-native control word is
represented as a dictionary entry, how `POSTPONE` of one is supposed to work
given that representation, or exactly what `;` should diagnose as unresolved.
This step had to make three concrete choices docs/forth-plan-2.md leaves open.

**1. Control words are a new `dictionary_binding` alternative with no VM
entry point.** `machine::dictionary_binding` gains a sixth alternative,
`control_word { control_builtin which; }` (`dictionary.hpp`), where
`control_builtin` is a plain enum tag (`if_`, `else_`, `then_`, ...,
`compile_comma_`). Unlike a primitive (inlined as `op::prim` at every
reference site) or a `compiled_colon_word` (real instructions in the
interpreter's own code space), a control word's action needs direct,
mutating access to `interpreter::compile_buffer` — appending sentinel
instructions, back-patching operands — which is not expressible as VM
bytecode at all: the VM's own `run_from` (`vm.hpp`) only ever sees a
`machine::forth_state`, never the code space being built. `control_builtin`/
`control_word` therefore live in `machine/dictionary.hpp` as an inert data
tag (`machine/` has no reason to know about `compile_buffer`);
`interpreter::apply_control_word` (`interp.hpp`) is the one place that
actually dispatches on the tag, with `compile_buffer`/`dictionary` both in
scope.

Alongside this, `dictionary_entry` gains a `bool immediate` flag (default
`false`), and `dictionary` gains `define_control` (installs a `control_word`,
taking an explicit `immediate` argument since most control words must be
immediate from the moment they are installed) and `mark_last_immediate`
(what the `IMMEDIATE` word itself calls, flagging whichever entry was
defined most recently regardless of binding kind). D13's own rule —
"execute a word when interpreting, or when immediate; otherwise compile
it" — is implemented uniformly via two new free functions in `interp.hpp`:
`execute_entry` (run a primitive/colon-word/variable/constant/control-word's
own execution semantics against the live state) and `compile_entry` (append
the same four non-control-word cases' compiled form; a control word has none
and is diagnosed if reached this way). `interpret`'s own loop calls
`execute_entry` unconditionally while interpreting, and while compiling
calls `execute_entry` for an immediate entry or `compile_entry` for an
ordinary one — one dispatch rule, not a special case per binding kind.

**2. `POSTPONE` of a control word installs an alias binding, not compiled
code.** `POSTPONE name`'s Forth-2012 contract is "append `name`'s own
compilation semantics to the current definition" — which, for anything with
a real entry point (a primitive, a colon word, a variable/constant's
address/value), is exactly what `compile_entry` already does, immediate or
not. For a `control_word` target, no VM code can represent "run this word's
own C++ action later," so `apply_control_word`'s own `postpone_` case
instead records the target on `compiling_context` (`has_postponed_alias`/
`postponed_target`), and `interpret`'s own `;` handling, seeing that flag,
installs the word being closed as a plain `control_word` entry carrying the
same tag — `: ENDIF POSTPONE THEN ; IMMEDIATE` makes `ENDIF` genuinely
indistinguishable from `THEN` afterward, not a `compiled_colon_word` that
happens to produce the same effect. This only works when the postponed
control word is the *entire* body of the definition: a control word met
before or after it (`buf.here() != cctx.entry` at either `POSTPONE` itself
or at the closing `;`) is diagnosed, not silently dropped or miscompiled.

**3. `;`'s own unresolved-control-flow diagnosis checks `loop_depth`, not
data-stack depth.** An early draft of this step gave `compiling_context` a
`stack_depth_at_entry` field and had `;` diagnose "unresolved IF/BEGIN/DO"
whenever the data stack's depth at `;` did not match its depth at the
matching `:`. This is unsound: an immediate word's own body can legitimately
leave real data on that same stack when it runs at compile time — `: DOUBLE-
IT 21 ; IMMEDIATE` used inside another definition being compiled genuinely
leaves a `21` behind right then, exactly as `[ ... ] LITERAL`'s own
bracketed computation does — and a plain depth comparison cannot tell that
apart from a genuinely unresolved `IF`. Caught by this step's own
`ImmediateWordExecutesAtCompileTimeNotRuntime` test failing against the
draft. The final design drops the general check and keeps only a `loop_depth`
counter (`compiling_context`, private bookkeeping incremented by `DO` and
decremented by `LOOP`/`+LOOP`, never touching the data stack) checked at `;`
for an unresolved `DO`. `THEN`/`REPEAT` with no matching `IF`/`BEGIN` is
still diagnosed, just later and less precisely positioned: when the data
stack does not hold what they expect to pop (a stack-underflow
`foundation::parse_error`, D7) — which is exactly this step's own stated
merge criterion ("mismatched THEN without IF diagnosed via the orig/dest
discipline"), so nothing required by the plan is lost.

## Why

**Binding shape.** Adding a sixth `dictionary_binding` alternative rather
than, say, encoding control words as a special range of primitive opcodes
keeps `machine::primitive`/`apply_primitive` exactly what they already are
(pure VM-executable operations over a live `forth_state`) and keeps the
"needs `compile_buffer`" concern entirely in `interpreter/`, where the type
it needs already lives. Splitting the work into `execute_entry`/
`compile_entry` rather than inlining `if (immediate) ... else ...` at each of
`interpret`'s two dispatch sites (interpreting, compiling) means the same two
functions serve both sites and both are independently testable (this step's
own `CompileCommaAppendsAnEntrysCompiledForm` test drives `compile_entry`
indirectly, without going through `interpret`'s own token loop at all).

**POSTPONE alias.** The alternative — giving `compiled_colon_word` some way
to carry "and also run this control action" — would mean either a second,
parallel representation for "a colon word's body" (defeating D16's "the
opcode enum, `instr`, `compiled_program` ... are retained" and D14's "one
semantics"), or teaching the VM's own instruction set a new opcode whose
semantics are "reach outside my own `forth_state` and mutate a `compile_
buffer` I have no reference to," which cannot be given real content within
`run_from`'s own signature without a much larger redesign of the VM/
compile-buffer boundary. The alias mechanism keeps the scope to exactly what
this step's own stated merge criterion needs, is honest that a control word
still has no VM-representable form (the alias entry is a `control_word`,
not a `compiled_colon_word`, so nothing pretends otherwise), and diagnoses
rather than silently miscompiles anything past that one case.

**Reverted balance check.** Correctness over completeness: a check that
flags legitimate programs as errors is worse than a narrower check that
misses a rarer misuse. `loop_depth` is safe because it is bookkeeping this
step already owns exclusively (nothing else increments or decrements it),
so it can never collide with a value an immediate word's own execution
pushed for an unrelated reason.

## Consequences

- `machine::dictionary_binding` has six alternatives again, for a different
  reason than DIV-0013's own R1-era `colon_word`/`compiled_colon_word`
  overlap (closed permanently at F26): colon definitions still have exactly
  one binding kind (`compiled_colon_word`) between them; `control_word` is
  a wholly separate kind of thing.
- `default_dictionary`'s own entry count grew from 47 to 67 (47 primitives +
  20 control words). Every pre-existing test or example that built a
  `machine::dictionary<MaxWords, ...>` (via `default_dictionary<MaxWords,
  ...>`) sized close to 47 with no headroom for its own colon definitions
  needed `MaxWords` raised — `machine/vm.test.cpp` and `interpreter/
  session.test.cpp`'s own `build_session<64, 64, ...>` calls (`MaxWords`
  64) became `build_session<64, 96, ...>`; nothing else in the tree was
  affected. A future step growing `default_dictionary` further should
  expect to repeat this audit.
- `POSTPONE` of a control word freely mixed with other compiled code in the
  same definition (legal under full Forth-2012) is diagnosed as unsupported
  here, not implemented. See Revisit condition.
- `apply_control_word`'s own per-word compile-only diagnoses share one
  message ("control word is compile-only, used while interpreting") rather
  than each spelling its own word name — matches the existing
  `RECURSE`/`EXIT`/`;` diagnostic *shape* (a positioned `parse_error`) but
  not their per-word wording; any later step tightening error-message
  quality (F36, consolidation) should be aware every control word currently
  shares this one string.
- `interpreter::compiling_context<MaxName>` replaces the four loose locals
  (`compiling_name`/`compiling_entry`/`compiling_effect`/
  `compiling_has_effect`) `interpret` used through F25/F26; any later step
  adding more per-definition compile-time bookkeeping should extend this
  struct rather than reintroducing loose locals.

## Revisit condition

The binding shape, the `execute_entry`/`compile_entry` split, and the
`loop_depth`-only `;` diagnosis are accepted-permanent: they follow directly
from the VM/compile-buffer boundary D16 retains, not from a scope cut this
step chose to accept temporarily.

The `POSTPONE`-of-a-control-word scope cut (whole-body-only) is open: it
closes if a later step (most plausibly F28, which owns D18's unified
execution-token/header work) gives control words a uniform representation
that composes with ordinary compiled code in the same definition body — at
that point full Forth-2012 `POSTPONE` semantics become buildable and this
record's own scope cut should be revisited, not silently left in place.

## F28 addendum: resolved — narrowed, not closed, and cannot fully close

Step F28 gives three control words a real compiled form: `EXECUTE`
compiles to a bare `machine::op::execute`, `CREATE` to `machine::op::
create_word`, and `DOES>` to `machine::op::does_enter` — each exactly as
compilable as a primitive, once `interpreter::compile_entry` has a case for
it. `POSTPONE EXECUTE`/`CREATE`/`DOES>` therefore now go through
`compile_entry` like anything else with a compiled form, and compose freely
with other code in the same definition body: `apply_control_word`'s own
`postpone_` case narrows its "alias instead of compile" branch to fire only
for a `control_word` whose `which` is none of those three (see
`interp.test.cpp`'s `PostponeExecuteComposesWithOtherCode`). This is a real,
demonstrable narrowing of the scope cut this record originally filed.

It does not close, and cannot, for the remaining structural control words
(`IF`/`ELSE`/`THEN`/`BEGIN`/`UNTIL`/`WHILE`/`REPEAT`/`DO`/`LOOP`/`+LOOP`/
`LEAVE`/`UNLOOP`/`I`/`J`/`LITERAL`/`POSTPONE`/`IMMEDIATE`/`[`/`]`/
`COMPILE,`). This is not a gap D18's own header unification merely failed
to close: giving one of these words an execution-token slot would not
create a runtime action for `EXECUTE` to invoke, because the word's entire
action *is* the compile-time mutation of `compile_buffer` (orig/dest
sentinels, back-patched operands) — there is no "run `THEN`'s action later"
to represent as VM bytecode, unlike `EXECUTE`/`CREATE`/`DOES>`, whose
actions always *were* representable as a fixed sequence of opcodes once
`compile_entry` was taught to emit one. See DIV-0016
(`docs/divergences/DIV-0016-f28-execution-tokens-and-defining-words.md`) for
this step's own full record, including the execution-token encoding
decision that makes the composable cases possible.
