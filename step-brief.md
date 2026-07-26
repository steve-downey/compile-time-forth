# step-brief.md — Step F29: parsing words and strings

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory.

## What F28 built, by anchor

`docs/compiler_architecture.org`'s Phase 11 section ("Execution Tokens and
Defining Words") covers this in full, with several transcluded code anchors.
Read it before opening any source below wholesale.

- **DIV-0012's fold is done.** `machine::forth_state` (`machine/
  forth_state.hpp`) now carries `SOURCE`/`>IN` (`source()`, an `input_source`
  relocated to `machine/input_source.hpp`), `BASE`, and `STATE` directly —
  `interpreter::forth_state`, the composed wrapper every prior step used, no
  longer exists. Every function that took it now takes `machine::forth_state`
  directly. **This is exactly what makes F29 possible**: `PARSE`/`WORD`/
  `CHAR` only need `state.source()` and `state.data_space()`, both plain
  fields of `machine::forth_state` now — they can be real `machine::
  primitive` enumerators, dispatched through `apply_primitive` exactly like
  `DUP`/`@`/`ALLOT`, with no dictionary access and no new opcode needed. Do
  not reach for the `CREATE`/`DOES>`-style `dictionary`-pointer-through-
  `run_from` machinery (next bullet) unless a specific parsing word turns out
  to need the dictionary itself, not just the input stream.
- **Two new VM opcodes exist for the one case that *does* need dictionary
  access mid-body**: `machine::op::create_word`/`op::does_enter`
  (`machine/instruction.hpp`), consulted via a new nullable, non-owning
  `dictionary<DictWords, DictName> *dict = nullptr` parameter on
  `machine::run_from`/`run` and `interpreter::call_word` (defaulted, so no
  existing call site needed to change). `machine::create_here` (`vm.hpp`) is
  the shared "scan a name, install a variable_word" action both the VM
  opcode and `interpreter::apply_control_word`'s own `create_` case call.
  Reuse this exact pattern (a shared free function in `machine/`, called
  from both an interpreting-time control-word case and a VM opcode case) if
  F29 needs a word usable both directly and from inside a compiled body.
- **`interpreter::resolve_execution_token`** (`interp.hpp`, anchor
  `1c9e6a4f-7b3d-4e8a-9c2f-6a1d8b4e3f7c`) is D18's primitive-XT encoding
  decision made concrete: an execution token is a code-space instruction
  index; `'`/`[']` produce one by returning a colon word's own entry point
  directly, or by building a small `ret`-terminated stub *guarded by a
  leading unconditional branch* for anything else (primitive/variable/
  constant/value_word) — the guard is required because the code space may be
  positioned inside a still-open colon definition's own body when this runs.
  If F29's own `S"` needs to store a string literal and later produce
  something callable from it, this guard discipline is the model to copy;
  do not emit unguarded instructions into `buf` from any context that might
  run mid-definition.
- **New binding kinds**: `machine::value_word`/`machine::defer_word`
  (`dictionary.hpp`) both hold a single data-space `addr` rather than a
  value stored on the entry itself, specifically so a *compiled* reference
  needs no dictionary access at runtime — reuse this shape if F29 needs to
  store a parsed string's own address/length as a dictionary binding (e.g. a
  `S"`-defined constant string).
- **`default_dictionary` is 77 entries now** (48 primitives + 29 control
  words), not 67. `MaxWords` values already in use (96, 256) still clear
  this with room; audit again if F29 adds enough new entries to threaten a
  smaller `MaxWords` somewhere.
- **`,` is primitive 48** (`machine::primitive::comma`, `( x -- )`): reserves
  one data-space cell and stores into it. `interpreter/effect_lint.hpp`'s
  `primitive_data_effect` switch is exhaustive over `machine::primitive` —
  add a case there for any new primitive F29 introduces, or the build warns
  (`-Wswitch`) under clang.

## Gotchas F29 needs and cannot get from its own pasted section

- **`CREATE`, uniquely among F26/F28's defining words, is a real dictionary
  entry (`control_word` tag `create_`) reachable from inside a compiled
  body; `VARIABLE`/`CONSTANT` are still direct-name special cases** in
  `interpret()`'s own interpreting branch, unchanged since F26. If F29's own
  parsing words need similar "usable both top-level and inside a running
  colon word" behavior, `CREATE`'s shape (non-immediate `control_word`,
  `compile_entry` case emitting a real opcode, `execute_entry`/
  `apply_control_word` case running the interpreting-time action directly)
  is the template — not a new direct-name special case.
- **`interpreter::compile_entry`/`execute_entry` are the two dispatch
  points every new binding kind or control word needs a case in**, per
  D13's "execute when interpreting or immediate, else compile" rule,
  unchanged in shape since F27. Extend these two functions; do not
  special-case further inside `interpret()`'s own loop body.
- **The guarded-branch stub pattern in `resolve_execution_token` costs one
  wasted instruction whenever emission happens somewhere already safe to
  append inline** (true top-level, most of the time) — this is deliberate
  (uniform safety over minimizing instruction count); do not try to detect
  "is this actually safe without a guard" and skip it.
- **`'`/`[']` cannot currently produce an execution token for a
  `control_word`, `foreign_word`, or `defer_word` target** (diagnosed: "word
  has no execution token") — DIV-0016's own Revisit condition names
  `defer_word` as the one case that could be added later if a merge
  criterion needs it; nothing before F29 does.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F28 used DIV-0016.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for F30; DIV filed for any deviation, using the number you are
given.
