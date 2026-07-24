# step-brief.md — Step F18a: Execution tokens and exceptions

Forward-only brief for the next clean agent. Bounded; not a log. Prior-step
narrative lives in `git log` / `docs/history/handoff-archive.md`; architecture
lives in `docs/compiler_architecture.org` — consult those only where pointed, by
section/anchor.

## Goal

Wire `EXECUTE` (run the execution token on the stack) and `CATCH`/`THROW`
(Forth-2012 exception handling) through **both** evaluators — `machine::
eval_direct` and `machine::codegen`/`machine::run`. The three reserved opcodes
`execute`, `catch_mark`, `throw_op` (`instruction.hpp`, per DIV-0008) are the
last three still diagnosed-not-implemented; F18a gives them real behavior.
Authoritative spec: `docs/forth-plan.md` section "Step F18a — execution tokens
and exceptions" (the orchestrator pastes it; do not open the full plan).

## Merge criterion

Taken verbatim from the plan's F18a section (the orchestrator pastes it). Expect
at minimum: `'` / `EXECUTE` round-trips an xt through the data stack and runs the
right word; `THROW` non-locally unwinds to the matching `CATCH`, which returns
the thrown code (0 when the protected word completed normally) and restores the
data- and return-stack depths `CATCH` recorded. Prove it through **both**
backends as `static_assert`s plus Catch2 mirrors, exactly as F13/F14/F17 tests
do (same `constexpr compiled_program` at namespace scope run twice).

## What already exists that F18a builds on (pointers, not narration)

- **`core_push_xt` already exists** (F11, `elaborated_core.hpp` anchor
  `8a9a3c73-...`): `' NAME` elaborates to it and it pushes the word's *dictionary
  index* as a `cell` (see `eval_direct.hpp`'s `core_push_xt` arm and `op::push_xt`
  in the VM). An xt **is** a dictionary index. `EXECUTE` maps that index back to
  something runnable: for a `colon_word`, do what `core_call` does
  (`eval_direct.hpp` recurses into `unit.arena.get(cw->core_id)`'s `core_seq`; the
  VM `call`s `entry_points[index]`); for a `primitive`, `apply_primitive`; other
  binding kinds are a diagnosed error. `compiled_program::entry_points` is already
  the xt→entry-point table the VM needs (index → instruction index, `-1` for
  non-colon).
- **Nonlocal control is one-shot, dynamic-extent (D11), threaded through the
  result channel, never a C++ exception.** F13's `EXIT` uses
  `eval_signal::exited` (absorbed at a `core_call`); F17's `LEAVE` added
  `eval_signal::left` (absorbed at a `core_do_loop`). F18a's `THROW` is the same
  shape: add a signal carrying the throw code, absorbed at the nearest `CATCH`.
  In the VM, `EXIT`→`ret` and `LEAVE`→back-patched `leave` already unwind via the
  return stack; `CATCH`/`THROW` need a handler frame (what `op::catch_mark`
  reserves).
- **Return-stack frame layout is now shared three ways** (D7, DIV-0010):
  `call`/`ret` push a return address; a `DO` loop pushes a two-cell
  `(limit index)` frame (index on top) *above* the caller's return address, torn
  down before the matching `ret`; F17's `UNLOOP` discards a loop frame so a
  following `EXIT` pops the real return address. **F18a's `CATCH` must record
  enough to restore the return stack** (and data stack) to the pre-`CATCH` depth
  on `THROW`, and must coexist with call frames *and* loop frames sitting on that
  same stack between the `CATCH` and the `THROW`. Decide and document the
  handler-frame layout and how a `THROW` finds the innermost handler.
- Reserved opcodes and the "diagnose, don't UB" discipline: `vm.hpp`'s `switch`
  still has the `execute`/`catch_mark`/`throw_op` arms diagnosing "not
  implemented until F18a" — replace those three. `codegen.hpp` currently never
  emits them.

## Files this step will touch

- `src/smd/forth/elaborator/elaborated_core.hpp` — likely a core node for
  `CATCH`/`THROW` (mirror `core_exit`/`core_leave`); `EXECUTE` may be a primitive
  or a core node — decide. `core_push_xt` already exists.
- `src/smd/forth/elaborator/elaborate.hpp` — recognize `EXECUTE`/`CATCH`/`THROW`
  (if core nodes, in `elaborate_word_ref` alongside `EXIT`/`RECURSE`/I/J/LEAVE/
  UNLOOP; if primitives, they resolve through the dictionary instead).
- `src/smd/forth/elaborator/stack_effect.hpp` — `EXECUTE` is input-dependent →
  `unknown_effect` (the F12 table doc already anticipates this); add analysis
  arms for any new core nodes.
- `src/smd/forth/machine/eval_direct.hpp` — `EXECUTE` and `CATCH`/`THROW`
  behavior; add a throw signal to `eval_signal` and let it propagate (see gotcha
  below).
- `src/smd/forth/machine/codegen.hpp` + `vm.hpp` — emit/execute `execute`,
  `catch_mark`, `throw_op`; the handler frame on the return stack.
- Possibly `forth_state.hpp`/`dictionary.hpp` if `EXECUTE`/`THROW`/`CATCH` are
  primitives (touches the `primitive` enum).

## Gotchas discovered this step (F17) that F18a must respect

- **`eval_body` and every loop/`IF` arm now propagate *any* non-`normal`
  signal** (`if (r.value() != eval_signal::normal) return r.value();`), not just
  `exited`. A new `thrown` signal is carried up by that generic propagation for
  free — but make the `core_do_loop` and `core_call` arms do the right thing with
  it: a `THROW` must unwind *past* a loop, tearing down the loop's return-stack
  frame, rather than being swallowed by the loop's `left`/normal handling.
- **Return-stack sizing:** loops now consume 2 return cells per nesting level on
  top of call frames. Tests use `RStackDepth = 8`; a `CATCH` handler frame plus
  nested loops plus calls add up. Size test `forth_state` return stacks with
  headroom (still no computed bound — DIV-0008's `required_return_depth` is `-1`).
- **`1+` now exists** as `primitive::one_plus` (DIV-0010); the default dictionary
  has **43** primitives now (was 42) — any test asserting a dictionary size or a
  first-colon-word index of 42 must use 43 (F17 fixed the existing ones).
- **`LEAVE` poisons a definition's stack effect to `unknown`** (DIV-0010); mixing
  `EXECUTE` (also `unknown`) with loops yields an `unknown` whole-definition
  effect — that suppresses depth checking, it is not an error.

## Standing constraints

- Makefile is the single build interface. **Use `TOOLCHAIN=gcc-16`** — the bare
  `make` default picks system gcc-13, which rejects `gnu++26` and fails to
  configure. `make TOOLCHAIN=gcc-16 compile|test`; secondary `clang-21`.
- **`make lint` caveat:** the sandbox cannot provision the node/go-based hooks
  (`markdownlint`, `gitleaks`, `checkmake`) — they fail with an SSL/TLS
  certificate error while downloading their env. Run the rest individually
  (`.venv/bin/pre-commit run clang-format --all-files`, plus `gersemi`,
  `codespell`, `check-yaml`, `shellcheck`, `mbake-validate`,
  `trailing-whitespace`, `end-of-file-fixer`) — all pass. Environment limit, not
  a code problem. **clang-format will reflow** touched files; let it.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` (D3); capacities are template parameters (no hardcoded constants);
  nonlocal control is one-shot, dynamic-extent (D11).
- **Divergence number: use DIV-0011** for F18a (DIV-0010 is F17's; DIV-0009 was
  F16's).

## Before handoff

`make TOOLCHAIN=gcc-16 compile`, `... test` green (and `clang-21` if available);
lint green or the individual-hook fallback above; both `smoke.sh` runs end
`SMOKE OK`; `checklist.md` ticked; durable facts recorded in
`docs/compiler_architecture.org` in place (by anchor); `step-brief.md` rewritten
for the next open step; DIV docs filed for any deviation.
