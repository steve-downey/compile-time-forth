# step-brief.md — Step F27: immediacy and control flow

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.
Your orchestrator pastes your own step section from `docs/forth-plan-2.md`
plus the decision records it cites; this brief does not attempt to restate
that section from memory, only to hand you what F26 learned cutting the R1
pipeline that your own pasted section will not tell you.

## What F26 built, by anchor

`docs/compiler_architecture.org`'s Phase 9 section ("The Cut") covers this
in prose; Phase 6/Phase 8 were rewritten in place to describe the
post-cut state directly. Read those before opening any source below wholesale.

- **The R1 pipeline is gone.** `reader/`, `elaborator/`,
  `machine/eval_direct.hpp`, `machine/codegen.hpp` no longer exist. There is
  no tree, no elaboration phase, no second evaluator. The full R1-era
  narrative (grammar, syntax tree, elaboration, two of three codegen
  backends) is frozen at `docs/history/architecture-grammar-era.org` —
  not on your read path, archival only.
- **`compiled_forth<Source>` (`forth.hpp`) now builds a D15 session
  image** (`interpreter::build_session`) once, at namespace-scope
  `constexpr` initialization; `.run()` returns that session directly
  (`interpreter::session const&`, anchor `bb43d007-…`), `.stack()`/
  `.output()` are thin accessors over its `stack`/`output` fields. There is
  **no runtime-recoverable failure channel on the public API any more** —
  building the session *is* running the top level, so any failure (unknown
  word, budget exhaustion, stack underflow, ...) is a hard compile error.
  `interpreter::build_session`/`interpret` still return a recoverable
  `foundation::result`, one layer down, if you need that shape. See
  DIV-0014 for the full reasoning.
- **`interpreter::interpret` (`interp.hpp`, anchor `aa1d6f83-…`) recognizes
  `VARIABLE`/`CREATE`/`CONSTANT` by direct name comparison**, the same
  technique it already used for `:`/`;`/`EXIT`/`RECURSE` — added at F26
  because that step's own merge criterion needed the F16 memory-word
  programs working again, ahead of your own generalized immediate-word
  dispatch. **Do not re-add these as a second mechanism**; fold them into
  whatever generalized dispatch you build, or leave the direct-name checks
  in place if your own design still special-cases a handful of words.
  `interpret` still has **no control flow at all** — `IF`/`ELSE`/`THEN`,
  `BEGIN`/`UNTIL`/`WHILE`/`REPEAT`, `DO`/`LOOP`/`+LOOP` are unknown words to
  it. That is your step's job.
- **The VM already has every opcode control flow needs** —
  `branch`/`branch0` (unconditional/conditional jump), `do_setup`/
  `loop_step`/`plus_loop_step`/`push_index`/`leave`/`unloop` (counted
  loops) — carried over from R1 unchanged (D16, anchors `5b9e9c1a-…`/
  `c520a0cc-…`/`e2a7c9f4-…`/`3b356d6c-…`, Phase 9). Nothing about the VM
  itself needs to change; you are teaching the *interpreter* to emit
  (while compiling) or run (while interpreting) these opcodes, not
  inventing new ones. `compile_buffer::emit`'s own back-patch pattern is
  the one thing you'll need that doesn't exist yet as a helper: a forward
  branch's own operand isn't known until its `THEN`/`REPEAT`/`LOOP` is
  reached, so you'll want to record the `emit`-returned index and mutate
  `buf.program().code[index].operand` later (`compile_buffer::program()`
  already exposes a mutable reference for exactly this).
- **The stack-effect lattice moved to `interpreter/effect_lint.hpp`**
  (anchor `3fd1b4b2-…`), pure and tree-independent, for F30 — not wired to
  anything yet. Do not consume it; it is not your step's job (D20's own gate
  lands at F30).
- **The F13/F16/F17 program battery is preserved verbatim** in
  `interpreter/control_flow_corpus.hpp`: `abs_program`, `countdown_program`,
  `spin_program`, `upto3_program`, `exit_boundary_program`, `sumto_program`,
  `find5_program`, `tens_program`, `sumeven_program`, `first_program`, plus
  the two memory-word programs for completeness. Each has a doc comment
  recording its expected stack/output. **This is your own merge criterion's
  source of truth** — every one of these should pass through `interpret`
  (and, once wired, `compiled_forth`) once control flow exists. Consume the
  `std::string_view` constants directly; there is no pre-built expectation
  type to match, on purpose (F26 did not want to guess your own assertion
  shape).
- **`machine::dictionary_binding` has five alternatives, not six**:
  `colon_word` (R1's own binding, tied to the deleted elaborator arena) and
  its `stack_effect` payload type are gone. `compiled_colon_word` is the
  only colon-definition binding. If you add execution tokens or a new
  binding kind, `dictionary.hpp` is where it lives.
- **`interpreter::session` gained a `stack` field** (F26, not D15's
  original list) — a bottom-to-top snapshot of the build-time data stack.
  If you add new session-building entry points, remember to populate it the
  same way `build_session` does, or downstream `.stack()` callers get an
  empty snapshot silently.

## Gotchas F27 needs and cannot get from its own pasted section

- **DIV-0012's fold is still open, deferred to F28,** not F27: don't feel
  obligated to fold `interpreter::forth_state`'s `input_source`/`BASE`/
  `STATE` into `machine::forth_state` as part of adding control flow. See
  DIV-0012's own F26 addendum if you want the full reasoning; it's F28's
  job (uniform execution tokens need it), not yours, unless you find a
  concrete reason control flow itself needs it — file that against D13 if
  so, don't fold silently.
- **`parser::forth_chars.hpp`** (moved from `reader/` at F26) is where
  `scan_word`/`skip_forth_space`/`scan_paren_comment`/`is_number_token`/
  `token_to_cell` live now, namespace `smd::forth::parser`. If you add new
  token-layer scanning (e.g. for `IF`/`THEN` as ordinary words — they need
  no new scanning, just new dictionary/interpret-loop recognition), this is
  the header.
- **`compile_buffer`'s own `MaxWords` is still decoupled** from
  `machine::dictionary`'s (interp.test.cpp's
  `RedefinitionShadowsButDoesNotErase` exercises this); unrelated to control
  flow but easy to trip over if you resize capacities together without
  checking.
- **`vm.test.cpp` no longer has a source-text `compile()` helper** — every
  program there is hand-built via `machine::emit`. If you add VM-level
  tests for a *new* opcode, follow that pattern; if you're testing through
  `interpret()`, that's `interp.test.cpp`'s job, not `vm.test.cpp`'s.

## Standing constraints

- `TOOLCHAIN=gcc-16` for `make compile|test`; `make lint` runs clean, all
  hooks. **Run `make lint` as the very last thing before you commit, and
  commit whatever it reformats.**
- Every compiled structure stays flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred (D3).
- Compile-time tests use the immediately-invoked-lambda `static_assert`
  pattern; every public constexpr API gets one.
- Do not pick your own DIV number; the orchestrator allocates it at
  dispatch. F26 used DIV-0014.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green; `make lint` green;
`make check-transclusions` green; `smoke.sh gcc-16` and `smoke.sh clang-21`
both end `SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place, by anchor; `step-brief.md`
rewritten for F28; DIV filed for any deviation, using the number you are
given.
