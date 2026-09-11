# step-brief.md — Step F36: Consolidation

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory.

F34 (foreign function interface) and F35 (bootstrap prelude) are both
merged. F36 is the last unchecked step in `checklist.md`; the retired R1
steps it absorbs are F21 (error-quality and negative-compile pass) and F22
(documentation consolidation) — but take the goal and merge criteria from
the §6 section your orchestrator pastes, not from those retired titles.

## What F34 built, by anchor

`docs/compiler_architecture.org`'s Phase 18 section ("The Foreign Function
Interface") covers this in full, with transcluded code anchors. Read it
before opening `machine/foreign.hpp` or the threading diff wholesale.

- **A foreign word is an ordinary word.** `machine::foreign_fn` is
  `status (*)(forth_state<...> &)`; `machine::foreign_vocabulary` is the flat
  registry of those pointers; `machine::foreign_word` (the dictionary
  binding) carries an index into it plus D20's optional declared effect;
  `machine::foreign_dictionary` bundles word list and registry so one
  `with_foreign` call appends to both. One new opcode, `machine::op::foreign`
  (operand: a registry index, *not* a code-space address). Nothing downstream
  distinguishes a foreign word: `'`, `[']`, `EXECUTE`, `CATCH`, `POSTPONE`,
  and the effect lint all reach one with no FFI-aware case anywhere.
- **A fifth threaded parameter, exactly like F28's `dict`.** `machine::
  run_from`/`run`, `interpreter::call_word`/`execute_entry`/
  `apply_control_word`/`interpret`/`build_session`/
  `build_session_with_prelude`/`call_defined_word`, and `sender::
  run_word_via_vm`/`word_sender`/`run_from_via_senders`/`run_via_senders` each
  gained one nullable `foreign_vocabulary const *` (defaulted `nullptr`) and
  one defaulted `MaxForeign` template parameter. Every pre-F34 call site
  compiles and behaves identically. `build_session` also gained a `vm_fuel`
  parameter, defaulted to the value `interpret` already used, so the new
  `vocabulary` parameter could precede it without behavior change.
- **Public surface:** `forth::compiled_forth_with(text, vocabulary, fuel)`
  (`forth.hpp`) — a *function*, not a variable template, because the
  vocabulary is a value carrying function pointers rather than an NTTP. It
  returns a `foundation::result`, so `.value()` in a namespace-scope
  `constexpr` initializer keeps `compiled_forth<Source>`'s own "malformed
  program is a hard compile error" contract. `compiled_forth<Source>` itself
  is untouched.
- **A session image carries foreign headers, never function pointers.** Any
  runtime re-run must be handed the same vocabulary alongside the
  `forth_state`, and that state must use the same four capacities the
  vocabulary's function pointers are typed on. `src/examples/ffi_gcd.cpp`
  shows the whole shape (compile-time call out, an `is_constant_evaluated`-
  guarded runtime-printing foreign word, and the reverse embedding).

## Gotchas F36 could not get from its own brief

- **Function addresses are not constant expressions under UBSan.** Under
  GCC-16 with `-fsanitize=undefined` (this project's default `Asan` config),
  `fn == nullptr` for a function pointer makes any enclosing `static_assert`
  fail with "is not a constant expression" — while *calling* through the same
  pointer constant-evaluates fine. This is why `foreign_vocabulary::add` does
  not diagnose a null implementation and `foreign_vocabulary::call` does,
  spelled `!std::is_constant_evaluated() && fn == nullptr` so the comparison
  is never evaluated in a constant expression (DIV-0029). If F36's
  error-quality work reaches for a null-check on any function pointer in a
  constexpr path, write it that way; a bare comparison hits the same wall,
  so bisect with `-fsanitize=undefined` alone before assuming a code defect.
- **A foreign word must not use the return stack as data.** D24's fallback
  trigger `sender::word_uses_return_stack_data` scans *instructions*, and a
  foreign call is opaque to it, so such a word would be lowered natively by
  the sender backend and could silently disagree with the VM. Documented
  constraint, not a defect; the fix if ever needed is a flag on
  `foreign_word` the trigger consults (DIV-0029).
- **`resolve_execution_token` still diagnoses two binding kinds**,
  `control_word` (permanently — a structural control word has no runtime
  action an XT could name, DIV-0015's F28 addendum) and `defer_word`
  (possible, unneeded so far, DIV-0016's revisit condition). If F36's
  error-quality pass audits diagnostics, "word has no execution token" is the
  one message covering both, and those two are the only remaining cases.
- **`run_and_compare.hpp` grew a `vocabulary` parameter** after `fuel`;
  `compile_and_run_both` now forwards `fuel` as `interpret`'s `vm_fuel` too
  (identical at every existing call site, all of which use the default).
- **Advisory effect-lint diagnostics have a collection point nobody reads:**
  `compile_buffer::program().diagnostics` (DIV-0019). If F36 is the
  error-quality step, that list is the obvious thing to surface, and it is
  already populated.

## Files F34 touched (so F36 knows what is new)

New: `src/smd/forth/machine/foreign.hpp`, `machine/foreign.test.cpp`,
`sender/lower_foreign.test.cpp`, `src/examples/ffi_gcd.cpp`,
`docs/divergences/DIV-0029-*.md`, `DIV-0030-*.md`.
Changed: `machine/dictionary.hpp`, `machine/instruction.hpp`,
`machine/vm.hpp`, `interpreter/compilebuf.hpp`, `interpreter/interp.hpp`,
`interpreter/effect_lint.hpp`, `interpreter/session.hpp`,
`interpreter/prelude.hpp`, `forth.hpp`, `sender/lower.hpp`,
`sender/run_and_compare.hpp`, the three CMakeLists that register the new
files, `interpreter/interp.test.cpp`, `forth.test.cpp`,
`docs/compiler_architecture.org` (Phase 18),
`docs/divergences/DIV-0016-*.md` (F34 addendum), `checklist.md`.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Watch `pgrep -af cc1plus` and its RSS before launching a new build if any
  step touches sender/Execution26 composition; memory, not wall clock, is the
  binding constraint. Never stack a second build against a first that appears
  stuck; diagnose memory before retrying. Sender-side tests are sharded one
  or two programs per translation unit, one capacity combination per file;
  measure each shard (`/usr/bin/time -v`) rather than assuming cost —
  `lower_foreign.test.cpp` measured 37.7 s / 710 MB peak RSS against
  `lower_if.test.cpp`'s 34.6 s / 728 MB taken the same way.
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one — but the sender backend is a
  **runtime-only** executor (`sync_wait` is not constexpr-capable, F33;
  DIV-0030 records how a merge criterion naming a `static_assert` "via the
  sender backend" is to be read, and that reading covers the pattern, not
  just F34's instance).
- Do not pick your own DIV number; the orchestrator allocates it at dispatch.
  F34 was allocated DIV-0029 through DIV-0032 and used 0029 and 0030; 0031
  and 0032 were **not** used and are free. Next free is DIV-0031.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for whatever the plan names after F36 (F36 is the last step in
`checklist.md` — if nothing follows, say so in the brief rather than
inventing a successor); DIV filed for any deviation, using the number you
are given.
