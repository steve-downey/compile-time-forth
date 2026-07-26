# step-brief.md — Step F26: the cut

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory, only to hand you what F25 learned building the
colon compiler that your own pasted section will not tell you.

## What F25 built, by anchor

`docs/compiler_architecture.org`'s new **Phase 8** section (after Phase 7)
covers this in prose, transcluding the new files in full. Read it before
opening any of the source below wholesale.

- `machine::emit` relocated verbatim from `codegen.hpp` to `machine/emit.hpp`
  (anchor `c2f6a5d1-3b9e-4a7c-8d2f-6e1b9c4a7f3d`); `codegen.hpp` now includes
  it. A pure relocation — no behavior changed.
- `machine::dictionary_binding` (`machine/dictionary.hpp`) gained a sixth
  alternative, `compiled_colon_word { int entry_point; source_span
  effect_span; bool has_effect; }` — **not** a reuse of the existing
  `colon_word` (whose `core_id` is an elaborator-arena index; still used,
  unchanged, by `elaborate.hpp`/`eval_direct.hpp`/`codegen.hpp`/
  `stack_effect.hpp`). See DIV-0013 for why a new alternative was added
  instead of repurposing `colon_word`, and for the one exhaustive
  `std::visit` this touches (`elaborate.hpp::elaborate_word_ref`'s own
  catch-all arm absorbs it harmlessly).
- `interpreter::compile_buffer` (`interpreter/compilebuf.hpp`, anchor
  `f7a3c9e1-6d4b-4a2f-8c1e-9b5d7a3f2e6c` covers `call_word`) — the
  incrementally-appendable code space, wrapping `machine::compiled_program`
  directly. Instruction 0 is a permanent reserved `halt` landing pad
  (`halt_pad()`, always 0) — load-bearing; do not overwrite it if you ever
  append to a `compile_buffer`'s own `.program()` directly instead of
  through `.emit()`.
- `call_word` (same file) is how interpreting a defined word runs the VM
  (D14): pushes `halt_pad()` as a return address, points the buffer's own
  `compiled_program::program_entry` at the callee, calls `machine::run`
  **completely unmodified**. `vm.hpp` has zero lines changed by F25 — see
  DIV-0013 for why a `run_at`-style loop extraction was drafted and then
  reverted (it would have silently invalidated Phase 5's existing prose,
  which transcludes `vm.hpp::run`'s current anchor verbatim).
- `interpreter::session` (`interpreter/session.hpp`, anchors
  `b4d8e2a6-7c1f-4e3a-9d5b-2a8f6c1e4d9b` covers `call_defined_word`,
  `c6e9f1b3-8a2d-4c7e-9f1a-3b6d8e2c5a7f` covers `build_session`) — D15's
  session image: a `compile_buffer`, a `machine::dictionary`, the
  data-space high-water mark, and captured output. `build_session(text,
  fuel)` is the one boundary a session is built across.
  **`forth.hpp`'s `compiled_forth<Source>` is NOT retargeted onto this —
  that is your own step's job.**
- `interpret` (`interp.hpp`, anchor `aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62`)
  now branches on `st.state()` and writes it: `:`/`;`/`EXIT`/`RECURSE` are
  recognized by direct name comparison before dictionary lookup (same
  technique the elaborator already uses for `I`/`J`/`LEAVE`/`UNLOOP`) —
  generalized immediate-word dispatch through the dictionary is F27's job,
  not done here.

## Gotchas F26 needs and cannot get from its own pasted section

- **F25's own `interpret` has no control flow at all** — no `IF`/`ELSE`/
  `THEN`, no `BEGIN`/`UNTIL`/`WHILE`, no `DO`/`LOOP`. Those words are simply
  unknown to it (diagnosed as "unknown word"); F27 ("immediacy and control
  flow") is what adds them. **If you retarget `compiled_forth<Source>` onto
  `build_session` this step, every existing `forth.test.cpp`/`hello.cpp`/
  `godbolt_forth.cpp` program that uses control flow will stop compiling**
  until F27 lands, since a `Source` that fails is a hard compile error
  (D15's own contract, unchanged). Decide deliberately whether "the cut"
  retargets the public API this step or leaves `compiled_forth` on a stub/
  interim shape until F27 — F25's own step-brief was told not to guess this
  for you, and neither should you guess it silently; if the plan text you
  are pasted does not resolve this, it is a real open question to raise,
  not paper over.
- **`interpreter::forth_state` is still a composed wrapper**, not a fold-in
  (DIV-0012). DIV-0012's own revisit condition names this step as where
  both halves close: relocate `reader/forth_chars.hpp` (D19's own
  recommendation is under `parser/`), and fold `input_source`/`BASE`/
  `STATE` down into `machine::forth_state` directly, once `reader/` and the
  elaborator are deleted and the layering objection that justified
  composing rather than growing disappears. Read DIV-0012's own
  "Orchestrator amendment" section before starting — it explains why this
  matters for F28 (D18's uniform XT) and F29 (parsing words as primitives)
  specifically, not just as tidiness.
- **`compile_buffer`'s own `MaxWords` template parameter is decoupled from
  `machine::dictionary`'s.** `interpret`'s own template parameter list uses
  `MaxWords` for the dictionary and a separate `MaxBufWords` for the code
  buffer's (currently unused) word table — they do not have to match. If
  you fold `compile_buffer`/`session` capacities together elsewhere, keep
  this decoupling or re-verify nothing depended on it (a small custom test
  dictionary paired with a normal-sized code buffer is exactly the case
  this was needed for; `interp.test.cpp`'s
  `RedefinitionShadowsButDoesNotErase` exercises it).
- **`machine::default_dictionary` needs at least 47 slots.** Any
  `MaxWords`/session capacity you choose for a real program must exceed 47
  (primitives) plus however many colon words the program itself defines —
  `default_dictionary`'s own `(void)dict.define_primitive(...)` calls
  discard overflow silently rather than propagating it, so an
  under-sized dictionary fails *later*, confusingly, at the first user
  definition rather than at `default_dictionary()` itself.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F25 used DIV-0013.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`smoke.sh` ends `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for F27; DIV filed for any deviation, using the number you are
given.
