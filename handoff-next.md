# Next step: Step F14 — Stack-machine codegen and VM

Step F13 (direct evaluator) is done in worktree `wt-f13` / branch
`step/f13`. This file is a full rewrite for F14 — see `handoff.md`'s "Step
F13 — Direct evaluator" section (and everything above it) for the complete
historical record; this file only summarizes what F14 needs to start.

## What F14 is

Read `docs/forth-plan.md` section "Step F14 — Stack-machine codegen and VM"
(around line 627) for the authoritative spec. In short:
`src/smd/forth/machine/{instruction,codegen,vm}.hpp` (+ a `.test.cpp` each):
the classic stack machine.

- **`instruction.hpp`**: `enum class op` (`push, prim, call, ret, branch,
  branch0, do_setup, loop_step, plus_loop_step, push_index, leave, unloop,
  push_xt, execute, catch_mark, throw_op, halt`) and `struct instr { op code;
  cell operand; }` — one instruction, an opcode plus one immediate `cell`
  operand. `compiled_program` bundles a flat `static_vector<instr, MaxCode>`
  (the program itself) with the dictionary's word table (an entry-point
  instruction index per word), the data-space size, and required stack
  capacities. **`compiled_program` must be a literal type, trivially
  copyable** — the plan's own words: "it is THE artifact that survives to
  runtime." All capacities are template parameters, no hardcoded constants
  (the standing project-wide rule, D2).
- **`codegen.hpp`**: `codegen(compiled_unit) -> result<compiled_program>`
  flattens the elaborated core into a flat instruction array: control
  structures (`core_if`, `core_begin_until`, `core_begin_while`) become
  `branch`/`branch0` with resolved instruction indices, computed by
  back-patching inside codegen (emit a placeholder branch operand, remember
  its instruction index, patch it once the target is known); each colon
  definition becomes an entry point (an instruction index recorded in the
  program's word table); `core_exit` becomes `ret`; `core_call` becomes
  `call` with the callee's entry-point instruction index as its operand
  (**not** the dictionary `word_index` `core_call` itself carries — codegen
  has to translate dictionary index → instruction address via the word
  table it is simultaneously building).
- **`vm.hpp`**: `run(compiled_program const&, forth_state&, fuel) ->
  result<void>` — an explicit loop over an instruction pointer (`ip`) with a
  call/return stack (Forth convention: calls and loop parameters share the
  return stack — F13's `forth_state::returns()` is exactly this stack
  already), fully `constexpr`, the same code executing identically at
  compile time and runtime.

Merge criteria (from the plan, verbatim): **the entire F13 test set** passes
through codegen+VM (a) at compile time via `static_assert`, **and** (b) at
runtime via a Catch2 `REQUIRE` on **the same program object**, declared
`constexpr` at namespace scope — "the survives-to-runtime proof." Concretely:
declare something like `constexpr auto squared_program =
codegen(elaborate_source(...)).value();` at namespace scope, then both
`static_assert(run(squared_program, ...) gives stack [16])` **and** a
`TEST_CASE` that constructs a fresh `forth_state` at ordinary runtime, calls
`run(squared_program, state, fuel)`, and `REQUIRE`s the identical result —
proving the *exact same compiled object*, not just the same source text,
works both ways. Every F13 merge-criterion program (`SQUARED`, `ABS`,
`COUNTDOWN`, `SPIN`'s budget exhaustion) needs this treatment; `eval_direct.
test.cpp` is the literal list to work from.

Deliverable: an architecture-doc section (`docs/compiler_architecture.org`'s
existing "Phase 5" section already covers F13's reference evaluator — see
below — F14 extends that same section, it does not get a new one) describing
the instruction set. `docs/forth-plan.md` frames this as a normal
incremental step, not a second "first milestone" the way F13 was.

## The API F14 consumes (Steps F8–F13)

- **`elaborator::compiled_unit<...>`** (F11/F12) is codegen's own input, same
  as F13's: `.arena` (the elaborated-core `tree_arena`), `.dictionary`
  (`machine::dictionary`, every entry already stack-effect-analyzed),
  `.data_space` (real F10 `allot`/`fetch`/`store`, `.size()` is the data-
  space footprint a `compiled_program` needs to record), `.program`
  (`core_body<...>`, the top-level executable body — codegen's own entry
  point into the whole flattening walk, exactly like `eval_program`'s own
  `eval_body(unit, unit.program, ...)` call in `eval_direct.hpp`).
- **The elaborated-core node kinds** (`elaborator/elaborated_core.hpp`,
  F11): `core_push`, `core_prim`, `core_call`, `core_var`, `core_const`,
  `core_push_xt`, `core_exit` (leaves); `core_if`, `core_begin_until`,
  `core_begin_while`, `core_do_loop`, `core_seq` (composites, each holding
  one or two `core_body<MaxNodes,MaxBody>` child sequences). Codegen has to
  walk every one of these — the same closed `std::variant` `eval_direct.hpp`
  already pattern-matches over via `std::visit`/`if constexpr`, just
  producing `instr`s into a flat array instead of executing side effects
  immediately. **Read `src/smd/forth/machine/eval_direct.hpp` first** — its
  `eval_body`/`eval_node` pair is the closest existing analogue to what
  codegen's own recursive walk needs to do (minus the back-patching, which
  is new to this step), and its doc comments explain each node kind's
  runtime meaning in detail.
- **`machine::dictionary<MaxWords,MaxName>`** (F9, extended F11): `.entry_at
  (int) const -> dictionary_entry<MaxName> const &`, `.lookup_index(...)`.
  `core_call`/`core_push_xt` both carry a dictionary `word_index`; codegen
  needs its own word-index → instruction-address table (built as codegen
  visits each colon definition and records where its entry point landed),
  since `compiled_program`'s own `call`/`push_xt`-equivalent instructions
  need instruction addresses, not dictionary indices, at VM-execution time.
- **`machine::primitive`** (F8, extended F13: **42 enumerators now**, not
  37 — F13 added `dot`, `dot_s`, `emit`, `cr`, `one_minus`; see DIV-0007) and
  **`machine::apply_primitive(primitive, forth_state&) -> status`**: the
  `op::prim` instruction's operand is a `primitive` enumerator value (cast
  to/from `cell`); the VM's `op::prim` case should call `apply_primitive`
  exactly the way `eval_direct.hpp`'s `core_prim` case does — this part is
  nearly a direct port.
- **`machine::forth_state<MaxDepth,MaxRDepth,MaxData,MaxOut>`** (F8, extended
  F13's D10 output words): `.data()`/`.returns()` (the two stacks —
  `.returns()` is the call/return stack `run`'s own signature already names
  as shared between calls and loop parameters), `.data_space()`, `.output()`
  (still just append-only; F16 is what wires `@`/`!`/`+!` through both
  backends, not F14). The VM threads the *same* `forth_state` type F13
  already uses — no new state type needed.
- **`machine::eval_direct::eval_program`/`eval_body`/`eval_node`**
  (`eval_direct.hpp`, F13) — not a dependency F14's own code calls, but the
  *oracle* F14's own tests check against: every merge-criterion program's
  expected stack/output/error, as already worked out and verified in
  `eval_direct.test.cpp`, is the ground truth `codegen`+`run` must reproduce.
  Consider literally reusing `eval_direct.test.cpp`'s `run_program`-style
  helper's *expected values* (not its machinery) as the source of truth for
  F14's own assertions, so the two backends' test expectations do not
  quietly drift apart from hand-copying.
- **`foundation::result<T>`/`foundation::parse_error`/`foundation::
  source_pos`**: unchanged, same diagnosed-error discipline throughout.
  `foundation::static_vector<T,MaxN>`/`foundation::tree_arena<T,MaxN>`/
  `foundation::arena_box<T,MaxN>`: `compiled_program`'s own instruction array
  is a `static_vector<instr, MaxCode>`, not a `tree_arena` — instructions
  don't need arena-handle addressing the way tree nodes do, since the
  program is already flat and instructions reference each other by plain
  integer instruction index (the `branch`/`branch0`/`call` operand), not by
  `arena_box`.

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary — **both**
  were available and both verified green in this worker's sandbox for F13
  (`make compile`, `make test` at 178/178, `make lint`, and `smoke.sh` on
  both toolchains); worth re-confirming both are still present for whoever
  runs F14, but don't block on it if only one is.
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern); add matching `TEST_CASE`s for Catch2
  visibility. F14's own merge criterion is explicit about needing *both* a
  compile-time `static_assert` **and** a runtime Catch2 `REQUIRE` on the
  *same* `constexpr`-at-namespace-scope program object — this is stricter
  than the usual "mirror every static_assert as a TEST_CASE" pattern, so
  read the merge-criteria paragraph above carefully before structuring
  `codegen.test.cpp`/`vm.test.cpp`.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3) — the elaborated core (codegen's input) still
  follows this; the *compiled instruction program* (codegen's output) does
  not need to, since it is already flat by construction (a `static_vector`
  of `instr`, not a tree).
- All capacities are template parameters with defaults; no hardcoded
  capacity constants — applies to `MaxCode` (`compiled_program`'s
  instruction-array capacity) exactly as it does to every other capacity in
  the project.
- All nonlocal control (`EXIT`, `LEAVE`, `CATCH`/`THROW`) is one-shot and
  dynamic-extent (`handoff.md`'s architectural invariants). F13 already
  worked out `EXIT`'s runtime shape as a tagged result channel value
  (`eval_signal`) threaded through a tree-walk; F14's VM instead gets `ret`
  as a literal instruction — much closer to how a real machine would do it
  (pop a return address off the call/return stack, jump to it) — which
  should end up *simpler* than F13's `eval_signal` propagation-through-
  nested-calls logic, not harder, since the call/return stack already does
  the unwinding for free once `ret` is just "pop and jump."
- Before handoff: `make compile`, `make test`, `make lint` green on
  `gcc-16` (and `clang-21` if available); both `smoke.sh` runs end
  `SMOKE OK`; `checklist.md` ticked; `handoff.md` appended (not rewritten);
  `handoff-next.md` rewritten for whatever comes next (F15, public one-shot
  API, per the plan's ordering, unless F14's own work reveals a reason to
  reorder — record that reasoning if so); divergence docs filed for anything
  done differently than `docs/forth-plan.md` or Forth-2012 semantics (next
  free number: check `docs/divergences/` — **DIV-0007 is the latest**,
  `accepted-permanent`, filed this step for two words — `.`/`.S`/`EMIT`/`CR`
  output primitives and `1-` — the plan's own F13 merge-criterion text needed
  but the runtime/word-table never had; DIV-0006, open, is about F12's
  stack-effect checker not being flow-sensitive across `EXIT` — still open,
  not F14's concern to resolve, but relevant background if F14's codegen
  needs to reason about `EXIT` at all when emitting `ret`).

## Known open items going into F14

- **The instruction set's F17/F18a-shaped opcodes have no real semantics
  yet**: `do_setup`, `loop_step`, `plus_loop_step`, `push_index` (`I`/`J`),
  `leave`, `unloop` are all `DO...LOOP`-related (F17's own deliverable —
  F13's `core_do_loop` evaluation is itself just a diagnosed
  "not implemented until F17" error, see `eval_direct.hpp`); `execute`
  (`EXECUTE`), `catch_mark`, `throw_op` are all F18a's. **The plan's own
  `op` enum names all of them now, at F14**, which reads as "reserve the
  opcode space now, wire the behavior later" rather than "implement all of
  this at F14." This worker needs to decide (and document the decision,
  either inline or as a DIV if it's a real divergence from a literal plan
  instruction) what `codegen` does when it encounters a `core_do_loop` node
  — the two live options are (a) mirror F13's own choice and diagnose a
  "not implemented" `foundation::parse_error` at codegen time, keeping
  `codegen` from ever emitting `do_setup`/`loop_step`/etc. at all until F17
  gives them real meaning, or (b) emit the opcodes now with *some*
  placeholder semantics in the VM that F17 later replaces. Option (a) is
  the more conservative, F13-consistent choice and is this worker's
  suggested default, but it is genuinely this step's own call to make, not
  a foregone conclusion — F13's own merge criteria never exercise
  `DO...LOOP` either way, so nothing forces the answer.
- **`compiled_program`'s word table shape is unspecified by the plan beyond
  "entry point per word"** — this worker needs to pick a concrete
  representation (a plain `static_vector<int, MaxWords>` indexed by
  dictionary `word_index`, mapping to instruction address, is the obvious
  minimal choice, mirroring how `machine::dictionary` itself is a flat,
  index-addressed structure) and document it, since F15's `compiled_forth`
  public API and F16's memory-word wiring will both need to know this shape.
- **Fuel/budget at the VM level**: the plan's own `run` signature already
  names a `fuel` parameter (`run(compiled_program const&, forth_state&,
  fuel) -> result<void>`), so this is not an open design question the way it
  was for F13 (there, "fuel design is entirely F13's own to invent" — see
  `handoff.md`'s F13 section) — but the *type* of `fuel` and its exact
  accounting (once per instruction executed? once per branch taken?) is
  still this worker's call. F13's own `consume_fuel(int &fuel,
  foundation::source_pos pos)` (a plain decrementing `int`, once per core
  node visited) is a reasonable model to port directly: once per instruction
  executed is the natural VM-level analogue of "once per core node visited."
- **DIV-0006's gap (F12's stack-effect checker is not flow-sensitive across
  `EXIT`) is still open** and still not F14's responsibility to fix — flagged
  again here only because F14's codegen is the second consumer (after F13)
  of a `compiled_unit` that might contain a definition like `: F DUP 0< IF
  EXIT THEN DROP ;`, and should not be surprised if such a definition's
  `ret`-based codegen produces a VM program whose actual runtime behavior
  (checked against F13's already-correct `eval_direct` output) differs by
  path exactly as DIV-0006 and F13's own `EvalDirectTest -
  ExitUnwindsOnlyToItsOwnCallBoundary`-adjacent tests already demonstrate —
  that is expected, not a codegen bug, if the VM's numbers agree with F13's.
- **The recurring `make lint` `clang-format`/`gersemi` tooling-version-drift
  item, flagged at every step since F6, reported clean for the first time
  at F13** (see `handoff.md`'s F13 section) — worth re-checking whether that
  holds at F14 too rather than assuming it stays resolved.
