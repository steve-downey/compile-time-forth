# step-brief.md — Step F31: CATCH and THROW

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory.

## What F29 built, by anchor

`docs/compiler_architecture.org`'s Phase 12 section ("Parsing Words and
Strings") covers this in full, with transcluded code anchors. Read it before
opening any source below wholesale.

- **`PARSE`/`WORD`/`CHAR`/`COUNT`/`TYPE`** landed as ordinary, non-immediate
  `machine::primitive` enumerators (`machine/forth_state.hpp`, anchor
  `13c745cb-5824-451b-8765-cbed0d5626b3` for the switch cases, `4beb6ab5-
  f28e-4b2f-8a21-3d4ba8575f55` for the enum). `S"`/`."`/`[CHAR]`/`ABORT"` and
  the reexpressed `(`/`\` are new `machine::control_builtin` tags, dispatched
  in `interpreter::apply_control_word` (`interp.hpp`, anchor
  `f73e653b-0bfe-4315-b0e9-d7ff2b4c044c`). `parser::scan_delimited`
  (`forth_chars.hpp`) is the one shared scan every one of them (including
  `(`/`\`) is built from.
- **`parser::skip_forth_space`/`forth_lexeme` are deleted.** `scan_word`'s
  own trailing skip is now plain `lexeme` (whitespace only). If F31 needs to
  scan raw text anywhere, do not reach for either name — they no longer
  exist. A comment can no longer appear between `:` and its own name (see
  DIV-0017 item 2); this is intentional, not a regression to work around.
- **`ABORT"` is a documented interim hard stop, not `THROW -2`** (DIV-0017,
  its own Revisit condition names this step by number). Its compiled form
  already does the right *shape* of work — store the message, push
  address/length, call a runtime primitive (`machine::primitive::
  abort_quote`, `forth_state.hpp`) — but that primitive currently prints the
  message (if the flag is nonzero) and returns a generic diagnosed
  `foundation::parse_error` that propagates all the way out of `interpret()`
  unconditionally. **Your own step should change `abort_quote` to `THROW -2`
  after printing**, once `THROW` exists, rather than leaving both
  mechanisms live side by side. `ABORT` itself (`-1 THROW`, this step's own
  deliverable per its step text) does not exist yet at all — F29 only built
  `ABORT"`.
- **`default_dictionary` is 89 entries now** (54 primitives + 35 control
  words), not 77. `MaxWords` values already in use (96, 256) still clear
  this with room; audit again once F31 adds `CATCH`/`THROW`/`ABORT`.
- **`foundation::parse_error::message` must be a static-lifetime string
  literal** (`foundation/parse_error.hpp`'s own doc comment) — it does not
  own or copy the string it points to. This is *why* `abort_quote` cannot
  attach its own dynamic message text to the diagnosed error value it
  returns (DIV-0017's own Why section spells this out). `THROW n` carries an
  integer, not a string, so this constraint should not bite your own step
  the same way — but if any part of your own design wants to carry a
  dynamic message through an error channel, this is the wall F29 hit.

## Gotchas F31 needs and cannot get from its own pasted section

- **`machine::op::catch_mark`/`op::throw_op` are reserved opcodes that
  already exist** in `machine::op` (`instruction.hpp`) and are already
  diagnosed by name ("exception opcode not implemented until F31") in
  `machine::run_from`'s own switch (`vm.hpp`, the `case op::catch_mark: case
  op::throw_op:` fallthrough near that function's end) — this is the one
  place in the VM's own fetch-execute loop your step needs to give real
  semantics, not a new case to add from scratch.
- **`machine::return_stack` (`stacks.hpp`) has no truncate/resize
  operation.** `THROW`'s own Forth-2012 semantics need to unwind the return
  stack back to whatever depth it was at the matching `CATCH`'s own handler
  frame, and restore the *data* stack to its depth at that same point (D11).
  Check `stacks.hpp`'s own `cell_stack`/`return_stack` API before assuming
  either capability exists; neither does yet.
- **`interpreter::compile_entry`/`execute_entry` are still the two dispatch
  points every new binding kind or control word needs a case in** (D13's
  "execute when interpreting or immediate, else compile" rule, unchanged in
  shape since F27). If `CATCH`/`THROW`/`ABORT` become new `control_builtin`
  tags (following the shape every defining/parsing word since F27 has used),
  extend these two functions; do not special-case further inside
  `interpret()`'s own loop body.
- **`interpreter/effect_lint.hpp`'s `primitive_data_effect` switch is
  exhaustive over `machine::primitive`** — add a case for any new primitive
  your step introduces, or the build warns (`-Wswitch`) under clang. F29's
  own `abort_quote` primitive is already there (`known(3, 0)`, with a
  comment noting the "may not return" fact is exactly what D20 defers to
  F30, not something this table decides) — the same reasoning applies to
  whatever primitive(s) `THROW`/`CATCH` become.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F29 used DIV-0017.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for **F30** (the effect lint — the orchestrator has reordered the
plan's own F30/F31 sequence so F30 runs *after* F31, informed by both F28's
and this step's own `unknown`-lattice cases, not before it); DIV filed for
any deviation, using the number you are given.
