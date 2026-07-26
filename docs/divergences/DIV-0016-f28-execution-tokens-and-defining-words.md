# DIV-0016: Execution-token encoding, CREATE/DOES> dictionary access from inside a compiled body, and two data-space-backed binding kinds

- **Status:** accepted-permanent, except the `'`/`[']` scope cut named below (see Revisit condition)
- **Date:** 2026-07-26
- **Step:** F28 (execution tokens and defining words), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (F28's own step text names D18's
  header unification and leaves the primitive-XT encoding, the `CREATE`/`DOES>`
  mechanism, and `VALUE`/`DEFER`'s own binding shape open)

## What diverged

D18 says dictionary entries converge to a classical header (name, flags, an
XT, a does-field) and names the primitive-XT encoding as a decision this step
must make once and record: "a one-instruction stub in code space, or a
tagged XT naming the primitive." Neither `docs/forth-plan-2.md` nor D18 says
which, or how `CREATE`/`DOES>` are supposed to reach the dictionary when
invoked from inside another word's own compiled body (the defining-word
pattern the plan's own merge criterion, `: CONSTANT2 CREATE , DOES> @ ;`,
requires). This step had to make four concrete choices.

**1. Execution-token encoding: a code-space instruction index, guarded by a
branch when emission is needed.** An execution token is the instruction
index of a `ret`-terminated code-space location; `machine::op::execute` pops
one and jumps to it exactly like `op::call` would (push a return address,
then jump) — no dictionary lookup at the VM level, ever.
`interpreter::resolve_execution_token` (`interp.hpp`) is what produces one:
a `compiled_colon_word`'s own entry point already qualifies (`ret`-terminated
by construction); a primitive, `variable_word`, `constant_word`, or
`value_word` has no standing code-space location, so a small stub is built
for it — but always behind a leading, unconditional `machine::op::branch`
that jumps past the stub, patched to land just after it. The branch is
"needed" precisely because `resolve_execution_token` may run with the code
space positioned *inside* a still-open colon definition's own body (`'`/
`[']` used inside `[ ... ]` while compiling something else, D13's own
bracket-interpreting state): appending a stub's instructions inline,
unguarded, would be silently executed as part of the enclosing definition's
own body the next time it runs (its own `ret` would return early,
corrupting control flow) — the same hazard `apply_control_word`'s own
`IF`/`WHILE` sentinel-and-patch discipline already exists to avoid.

**2. `CREATE`/`DOES>` need the dictionary reachable from inside `run_from`'s
own bytecode loop, not only from the interpreter's own C++ dispatch.**
`machine::run_from`, `machine::run`, and `interpreter::call_word` all gain a
new, nullable, non-owning `dictionary<DictWords, DictName> *dict = nullptr`
parameter (template parameters defaulted, so no caller before this step
needs to change). Two new opcodes, `machine::op::create_word` and
`machine::op::does_enter`, consult it: `create_word` (operand: cells to
allot, 0 for `CREATE`) scans a name off `state`'s own `SOURCE`/`>IN` (the
DIV-0012 fold below is what makes this possible) and installs a
`variable_word`; `does_enter` (no operand — the code right after it, at
runtime, is always the instruction its own index+1 names, since compilation
is sequential) calls the new `dictionary::attach_does` to install a
does-field on the most recently defined entry, then behaves exactly like
`op::ret`. `interpreter::machine::create_here` (`vm.hpp`) is the one shared
implementation of `CREATE`'s own action, called both from `run_from`'s own
`op::create_word` case and from `interpreter::apply_control_word`'s own
`create_` case (`CREATE` met directly while interpreting) — not duplicated.

**3. `VALUE`/`DEFER` are data-space-backed, not dictionary-mutated.**
`machine::value_word`/`machine::defer_word` each hold one `addr` into the
data space, not a value or target stored on the dictionary entry itself.
This means a *compiled* reference to either, from inside another word's own
body, needs no dictionary access at runtime at all — it compiles to
ordinary, already-existing opcodes (`push address`; `prim fetch`; and, for a
deferred word, `execute`). `TO` is immediate (its target's address is
resolved once, at the moment `TO NAME` is met, regardless of `STATE`) and
either stores directly (interpreting) or compiles the equivalent
push-address/store sequence (compiling). `IS` pops an execution token and
stores it straight into the target's own cell — no dictionary mutation,
unlike `DOES>`.

**4. `CREATE` moves off the F26 direct-name list; `VARIABLE`/`CONSTANT`
stay.** F26 recognized `VARIABLE`/`CREATE`/`CONSTANT` by direct name
comparison in `interpret()`'s own token loop, before any dictionary lookup.
`CREATE` cannot stay there: it must also be reachable from inside another
word's own compiled body, which a direct-name special case in the outer
loop's own token scan can never reach. It becomes an ordinary,
non-immediate `control_word` (tag `create_`) instead, found via the same
`dict.lookup` every other word goes through. `VARIABLE`/`CONSTANT` are left
exactly where F26 put them: nothing requires either to work from inside a
running colon word yet, and migrating them needlessly enlarges this step's
own diff for no merge-criterion benefit.

## Why

**Execution-token encoding.** The alternative D18 names — "a tagged XT
naming the primitive" — was seriously considered (encode primitive-vs-colon
distinction directly in the pushed cell, decoded by `op::execute` without
any code-space involvement at all). It was rejected because a `CONSTANT`'s
value can be any 64-bit `cell`, and any tag scheme that reserves bits for a
kind discriminator loses precision somewhere in the value range — a real,
if narrow, correctness cost the code-space-stub encoding does not pay
anywhere. The code-space encoding also reuses `op::call`'s own semantics for
`op::execute` verbatim (this is genuinely "the same opcode with a
runtime-supplied target," not new VM machinery), keeping D16's "the VM loop
is retained" as literal as possible.

**Guard branch, always, not only when provably needed.** A cheaper design
would skip the guard when `resolve_execution_token` is known to run at true
top level (outside any open definition) — true for `'`, always. It was
rejected: `'`/`[']` can both run while a definition is open, through
`[ ... ]` bracket-interpreting, and distinguishing "safe to emit inline"
from "must guard" by context would be exactly the kind of special-casing
this project has been removing (D13's own uniform dispatch, Phase 10). The
guard costs one wasted instruction in the common case; that is cheaper than
the risk of getting the special case wrong.

**Dictionary pointer through `run_from`, not a new re-entry mechanism.** The
alternative — some way for the VM to pause and hand control back to the
interpreter's own C++ loop mid-body — would be a materially larger change to
the VM/interpreter boundary than this step's own scope, and nothing else in
the plan calls for it. A nullable pointer, defaulted to `nullptr`,
threading unchanged through every existing call site, is the minimal
addition that makes `CREATE`/`DOES>` possible without disturbing `run_from`'s
own contract for every caller that never reaches either opcode.

**Data-space-backed `VALUE`/`DEFER`.** Storing a `VALUE`'s value or a
`DEFER`'s target directly on the dictionary entry would need the same
"reach the dictionary from inside a compiled body" machinery `CREATE`/
`DOES>` need, for a case where it is avoidable: a data-space cell is
already reachable from any VM context via ordinary primitives, so `TO`
compiling a fixed address (resolved once, at the point `TO` is met) plus a
`store` is both simpler and needs no new opcode.

## Consequences

- `machine::dictionary_binding` has eight alternatives now, not six:
  `value_word` and `defer_word` join `control_word` (F27) alongside the five
  from before it.
- `machine::run_from`/`run`, and `interpreter::call_word`, all gained two
  new template parameters (`DictWords`, `DictName`, both defaulted) and one
  new function parameter (`dict`, defaulted to `nullptr`). Every call site
  before this step continues to compile and behave identically.
- `default_dictionary`'s own entry count grew from 67 to 77: 48 primitives
  (47 + `,`) plus 29 control words (20 from F27 + 9 from this step). Every
  pre-existing test or example sized close to 67 with no headroom needed
  auditing — none needed raising this time (existing `MaxWords` values of 96
  and 256 both still clear 77 comfortably), but a future step growing
  `default_dictionary` further should expect to repeat this audit
  (DIV-0015 already names the same pattern for F27's own growth).
- `'`/`[']` do not support taking an execution token of a `control_word`,
  `foreign_word`, or `defer_word` target; see Revisit condition.
- `interpreter::compile_entry` gained three new `control_word` cases
  (`execute_`, `create_`, `does_`) alongside its four pre-existing binding
  kinds; `interpreter::execute_entry` gained two new binding-kind cases
  (`value_word`, `defer_word`).
- DIV-0012's own deferred fold (composed `interpreter::forth_state` versus
  `machine::forth_state`) closes at this step; DIV-0012's own record carries
  the closure. DIV-0013's own "invocation half" (whether `call_word`'s
  mechanism generalizes) closes too: it does, unchanged in its core shape,
  with one additional defaulted parameter. DIV-0015's own revisit condition
  (whether header unification lets `POSTPONE` of a control word compose)
  resolves as a qualified yes: it does for `EXECUTE`/`CREATE`/`DOES>`
  specifically (they now have real compiled forms), and provably cannot for
  the structural, orig/dest-patching control words (their action is not a
  runtime effect at all). Both records carry their own addenda.

## Revisit condition

The execution-token encoding, the `CREATE`/`DOES>` dictionary-pointer
mechanism, and the data-space-backed `VALUE`/`DEFER` design are all
accepted-permanent: none is a scope cut made to save time, and none is
expected to need revisiting as more of D18 is built out (deferred words,
foreign functions, and further defining words should all fit the same
shapes).

The one open scope cut: `'`/`[']` cannot currently produce an execution
token for a `control_word`, `foreign_word`, or `defer_word` target
(`resolve_execution_token` diagnoses "word has no execution token" for all
three). The first two are permanent by their own nature (no VM
representation exists or ever will for a bare control word; foreign
functions have no callable code-space location before F34 gives them one).
`defer_word` is the one that could, in principle, be added later — a
`resolve_execution_token` case for it would need the same guarded stub
pattern already used for primitives/variables/constants, just resolving
through the deferred word's own current target at the moment `'` runs. No
merge criterion in this step or the ones immediately after it needs this;
revisit if a later step's own merge criterion does.
