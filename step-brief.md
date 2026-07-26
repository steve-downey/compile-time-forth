# step-brief.md — Step F24: The interpreter, interpret state only

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log`; architecture lives in
`docs/compiler_architecture.org` — consult it only where pointed, by anchor.

**Read this first: the plan changed.** `docs/forth-plan-2.md` is now the governing
plan and `docs/forth-plan.md` is superseded history. The project pivoted from a
structural grammar to the real Forth-2012 text interpreter. Your orchestrator
pastes your step section and the decision records it cites; do not open either
plan. The pivot rationale, if you need it, is
`docs/divergences/DIV-0011-true-forth-pivot.md`.

## Goal

Build the Forth-2012 §3.4 outer text interpreter, **interpret state only**, in
`src/smd/forth/interpreter/{input_source,interp}.hpp` (+ `.test.cpp` +
`CMakeLists.txt`). `forth_state` grows an input source (a source view plus a
`>IN` offset), `BASE` (default 10), and `STATE` (0 = interpreting).

The loop: scan a word; look it up in the dictionary (newest-first, so
redefinition shadows); if found and it is a primitive, run it through
`apply_primitive`; if not found, try it as a number per `BASE` and push it; if
that fails, a **positioned** diagnosed error naming the unknown word. `\` and
`( ... )` are consumed by the existing comment scanners. There is no `:` yet —
that is F25.

## Merge criterion

Per your pasted step section. At minimum, as `static_assert`s with Catch2
mirrors: `1 2 + .` yields output `"3 "` and an empty stack; `BASE` is plumbed and
tested (directly, or via `HEX`/`DECIMAL` if you include them); an unknown word
carries its source position; a stack underflow reached through the interpreter is
the same diagnosed error `apply_primitive` already produces.

## What already exists that F24 builds on (pointers, not narration)

- **The token layer is already written and tested.**
  `src/smd/forth/reader/forth_chars.hpp` has `fold_char`, `is_word_char`,
  `skip_forth_space`, `scan_word`, `scan_token<MaxName>`, `scan_paren_comment`,
  `is_number_token`, and `token_to_cell`. Architecture anchors
  `e9a1c6a1-9d3d-4a6a-9f2e-7d3f2b3c8a1c` and
  `c3d5e8b2-7f2a-4b9c-8d3e-4f6a8c2b1e9f`. **Do not rewrite these.** Decision D19
  (pasted with your step) says the token layer either moves under `interpreter/`
  or stays in `parser/` — **your choice, made once, recorded in
  `docs/compiler_architecture.org`.** Note `reader/` is deleted at F26, so
  "leave it in `reader/`" is not one of the options; if you choose to leave it in
  place for now, say so explicitly in your report and in the brief you write, so
  F26 knows it inherits the move.
- **`forth_state`** (`machine/forth_state.hpp`) is
  `template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>` — data stack,
  return stack, arena data space, and a fixed-capacity output buffer with
  `emit_char`/`emit_cell`. Your new fields go here, as more template-parameterized
  state, never hardcoded capacities.
- **`apply_primitive`** is in the same header and already covers all 47
  primitives including the memory words (`@ ! +! ALLOT`) and the output words
  (`. .S EMIT CR`). Interpreting a primitive **is** calling it. Every misuse it
  can see is already a diagnosed `foundation::result` error, never UB — your loop
  propagates those, it does not re-diagnose them.
- **The dictionary** (`machine/dictionary.hpp`) has append-only storage with
  newest-first lookup, which is what gives Forth redefinition semantics for free.
  `default_dictionary()` installs the primitives.
- **The F5 number/word tests move here intact.** The hard-won edges are that `-1`
  is a number and `1-` is a word, and that words fold to uppercase at scan time.
  Those tests are the specification for the number recognizer; carry them, do not
  re-derive them.
- **Fuel is architecture now, not just a VM concern** (D22): the interpreter loop
  itself carries a step budget, so a pathological source cannot run the constant
  evaluator out of steps undiagnosed. Model it on the VM's existing budget.

## Gotchas

- **`default_dictionary()` holds 47 primitives, not 46.** The array in
  `dictionary.hpp` is correct at 47; its own doc comment above it still says 46
  and omits `1+` from the prose word list. Fix that comment while you are there —
  the wrong number has already propagated into planning documents once.
- **Nothing is deleted in this step.** The reader grammar, the elaborator,
  `eval_direct`, and `codegen` all still build and all their tests still pass.
  The cut is F26. If your change makes any of them fail to compile, you have
  reached further than this step.
- **Diagnostics are a merge criterion, not a nicety.** The elaborator got
  positioned errors from a tree; you have to thread position through `>IN`
  yourself. Test the shape of the error, not just that one occurred.
- **`>IN` is an offset into the input source, and it is ordinary machine state.**
  That is the whole point — it is what makes user-defined parsing words possible
  at F29. Do not hide it inside the scanner as a local cursor.

## Standing constraints

- Makefile is the single build interface. **Use `TOOLCHAIN=gcc-16`** — bare `make`
  picks system gcc-13, which rejects `gnu++26` and fails to configure.
  `make TOOLCHAIN=gcc-16 compile|test`; secondary `clang-21`.
- **`make lint` runs clean, all hooks.** The previous brief's caveat that the
  node/go-based hooks (`markdownlint`, `gitleaks`, `checkmake`) cannot provision
  is **stale** — they provision and pass as of F23. Treat a lint failure as a
  real failure. **clang-format will reflow** touched files; let it. Note that
  clang-format is pinned to v21.1.2 by `.pre-commit-config.yaml` and is stricter
  than a system clang-format may be, particularly about the indentation of
  transclusion anchor comments.
- Every compiled structure is flat, trivially destructible, and
  capacity-parameterized; heap-backed `fix`/`Box` types are barred. All
  capacities are template parameters with defaults.
- Compile-time tests use the immediately-invoked-lambda `static_assert` pattern;
  every public constexpr API gets one.
- **Do not pick your own DIV number.** The orchestrator allocates it at dispatch.

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green (and `clang-21` if available);
lint green or the individual-hook fallback above; `smoke.sh` ends `SMOKE OK`;
`checklist.md` ticked; the D19 token-layer decision and any new durable fact
recorded in `docs/compiler_architecture.org` in place, by anchor;
`step-brief.md` rewritten for F25 (the colon compiler and the session image);
DIV filed for any deviation, using the number you were given.
