# Compile-Time Forth — Agent Execution Plan

This is the operational plan for building `smd/forth`, a compile-time and
runtime-capable Forth compiler in C++26 on GCC16.

The pipeline: constexpr applicative parser combinators turn source text into a
syntax tree, an elaboration stage turns the bare tree into a resolved,
stack-effect-checked core, and code generation produces both compile-time
execution and a compiled program that survives to runtime — executed by a
classic stack machine and, separately, by CPS over Sender/Receiver.
The foreign function interface is to native C++ via direct access to the
underlying value and stack representations.

This plan is written for an orchestrating agent that dispatches Sonnet worker
sub-agents, independently verifies their results, and maintains the handoff and
divergence documentation described below.

The reference implementation is the compile-time Scheme compiler at
`~/src/compile-time-scheme/main` (`smd::smdscheme`, mid-pivot to
`smd::smdlisp`). This plan imports its infrastructure by copy and its
architectural lessons by reference; there is no build-time coupling between the
repositories.

---

# 0. Thesis and goals

Two goals, one demonstration:

1. **The same compiled program runs at compile time and at runtime.**
   Parsing, elaboration, and code generation are all constexpr. The compiled
   program is a literal-type object — a flat instruction array plus
   dictionary — that can be baked into the binary as a `constexpr` variable or
   reached through an NTTP source string (`compiled_forth<"...">`), then
   executed again at runtime with runtime inputs.
2. **Forth's control vocabulary is exactly what structured concurrency can
   express.** Forth has no continuations. `EXIT`, `LEAVE`, and Forth-2012
   `CATCH`/`THROW` are all one-shot, upward-only, dynamic-extent transfers.
   That is precisely the completion discipline of the sender/receiver model
   (one completion per operation state, through exactly one of `set_value`,
   `set_error`, `set_stopped`). The Scheme sibling had to abandon `call/cc` to
   reach this soundness boundary; Forth starts inside it. The sender backend
   makes the correspondence executable: `THROW` completes on the error
   channel, `CATCH` is an adapter that restores machine state, `EXIT` is early
   completion of a definition's scope.

Forth is also the natural stress case for the front half: it is concatenative,
so the parser-combinator front end and the stack-effect elaboration carry more
of the analysis weight than they did for Scheme's s-expressions.

## What this project is not

- Not a standards-conforming Forth system. It is a structurally parsed,
  integer-only Forth-2012 core subset (decision records D5, D12).
- Not an interactive interpreter. There is no outer interpreter loop, no
  `QUIT`, no user input; a program is compiled whole from a source string.
- Not a continuation of the Scheme/CL blog series. Documentation is this
  repository's own architecture doc and presentation org file.

---

# 1. Rule and style precedence

Agents must read these files first, in order:

```txt
docs/codestyle.org
AGENTS.md
docs/CODING_RULES.md
CLAUDE.md
docs/forth-plan.md   (this file)
handoff.md
handoff-next.md
checklist.md
```

Rule precedence:

```txt
docs/codestyle.org > AGENTS.md > docs/CODING_RULES.md > CLAUDE.md > handoff/checklist files
```

Step F0 installs the governance files; until it completes, this plan plus the
existing repo conventions govern.

C++26 on GCC16 is the baseline (installed by F1; the scaffold is C++23 today).
Tests use Catch2. Do not introduce GTest.

**The Makefile is the single build interface.** It is parameterized by exactly
two variables: `TOOLCHAIN` and `CONFIG`. All files that are compiled are
always compiled; everything in a build is built the same way. A capability
needing different flags (coverage exists as `CONFIG=Gcov`; benchmarking would
be another) is a **new CONFIG**, never per-file flag tweaks and never a side
build. New sources are wired into `target_sources` in the owning
`CMakeLists.txt`. The smoke driver
`.claude/skills/run-compile-time-forth/smoke.sh [TOOLCHAIN] [CONFIG]` builds,
tests, and runs the example through the Makefile; workers may use it as a
one-command verification.

---

# 2. Orchestration protocol

The plan is executed by an **orchestrator** agent managing **Sonnet worker**
sub-agents.

## Orchestrator duties

- Dispatch one worker per step, giving it the canonical clean-agent
  instruction (section 12) plus the full text of the step's section from this
  plan. Steps are written to be self-contained at Sonnet scale; do not send
  the whole plan.
- Dispatch **independent steps in parallel** when the dependency notes permit
  it; each parallel worker gets its own git worktree.
- After each worker reports completion, **independently verify — do not trust
  the report**:
  - Run `make compile`, `make test`, `make lint` in the worker's tree
    (or `.claude/skills/run-compile-time-forth/smoke.sh`).
  - Review the diff; changes must be confined to the files the step declares
    plus its tests and CMake.
  - Confirm `checklist.md` was ticked, `handoff.md` updated with durable facts
    only, `handoff-next.md` rewritten for the next worker.
  - Confirm any deviation from this plan or from Forth-2012 semantics has a
    divergence doc (section 3).
  - Spot-check the architecture facts in section 11 still hold (trivially
    destructible tree nodes, no heap types in the compiled program, capacities
    parameterized).
- Merge to main with `--no-ff` only when all checks pass.
- If a worker is blocked, capture the blocker in `handoff-next.md` and either
  re-scope the step or file a divergence doc and move on.
- Periodically (at least at F7, F14, F18) run the full toolchain matrix:
  `smoke.sh gcc-16` and `smoke.sh clang-21`, plus `CONFIG=RelWithDebInfo` on
  one of them.

## Worker duties

- Work only the assigned step.
- Keep the step small and mergeable; do not continue into later steps.
- Do not leave vague TODOs.
- Run `make compile`, `make test`, `make lint` before declaring completion.
- Update `checklist.md`, `handoff.md`, `handoff-next.md`.
- File divergence docs for anything done differently than this plan specifies,
  and for any knowing deviation from Forth-2012 semantics beyond those already
  recorded.
- Report back: what was built, what deviated (with DIV numbers), what the next
  step needs to know.

---

# 3. Divergence issue docs

Divergence docs live in `docs/divergences/`, one file per issue, named
`DIV-NNNN-short-slug.md`, numbered sequentially.
Step F0 installs `docs/divergences/TEMPLATE.md` (copy the Scheme repo's,
adjusting the authority line to `Forth-2012 | docs/forth-plan.md`).

File a divergence doc when:

1. The implementation knowingly deviates from Forth-2012 semantics beyond the
   scope cuts already recorded in D5/D12.
2. A step is implemented differently than this plan specifies (the plan sketch
   didn't survive contact with the compiler).
3. An imported-infrastructure file (D2) needs semantic changes beyond
   renamespacing and the debt fixes D2 authorizes.

Each doc records: what diverged, from what authority, why, the consequences
for later steps, and whether it is permanent or should be revisited.
Divergence docs are append-only history; supersede, don't rewrite.

---

# 4. Decision records

These decisions are made now so workers don't re-litigate them.
Overturning one requires a divergence doc and orchestrator sign-off.

**D1 — Layout.**
New code goes in `src/smd/forth/<component>/`, namespace
`smd::forth::<component>`, one sub-namespace per directory. Components:
`foundation`, `parser`, `reader`, `elaborator`, `machine`, `sender`.
The existing placeholder (`src/smd/forth/forth.hpp` returning a name string)
is replaced by the public API in F15; until then it stays green.
Canonical includes: `#include <smd/forth/reader/read_program.hpp>`.

**D2 — Infrastructure imports by copy, with debt paid at the door.**
`foundation/` and `parser/` are adapted by copy from
`~/src/compile-time-scheme/main/src/smd/smdscheme/`, renamespaced
`smd::smdscheme::foundation` → `smd::forth::foundation` (likewise `parser`),
tests brought along, file prologs updated with a provenance line. Both repos
are Apache-2.0 WITH LLVM-exception; no license issue.
Known debt is fixed during the copy, without divergence docs:
hardcoded capacities become template parameters with the old value as default;
anything Scheme-flavored (char predicates, datum references) is left behind.
`foundation::fix` is **not** imported (see D3). `src/smd/fixpoint/` is not
imported unless a later optional step needs it.
There is no build dependency on the Scheme repository, ever.

**D3 — Flat arenas only; no `fix`/`Box` in the compiled pipeline.**
The Scheme project established (docs/cps-direction.md there) that
heap-backed `fix<F>`/`Box<A>` are not literal types under C++26 constexpr
rules, which bars them from `static_assert` tests and from surviving to
runtime. Every tree in this project — syntax tree, elaborated core,
instruction program — is a flat `tree_arena` of trivially destructible nodes
referenced by integer-ID `arena_box` handles. Recursion over trees is
structural recursion on node IDs. Compile-time tests use the
immediately-invoked-lambda `static_assert` pattern.

**D4 — Baseline and dependencies.**
C++26 (`gnu++26`), GCC16 primary, clang-21+ secondary, both already in `etc/`.
Beman Execution is the sender backend, vendored as a git submodule at
`vendor/execution`, integrated with `add_subdirectory` — no FetchContent, no
vcpkg, no install-time discovery. Beman Task may be vendored at
`vendor/task` under the same rules, only when a step needs coroutines.

**D5 — Structural grammar, not an interpretive outer text interpreter.**
Classical Forth parses with a stateful outer interpreter: immediate words run
at compile time and lay down control-flow structure themselves. This project
instead parses source structurally with constexpr applicative parser
combinators: colon definitions and control structures
(`IF`/`ELSE`/`THEN`, `BEGIN`/`UNTIL`, `BEGIN`/`WHILE`/`REPEAT`,
`DO`/`LOOP`/`+LOOP`) are grammar productions producing a nested tree.
Consequences: no `IMMEDIATE`, no `POSTPONE`, no `[` `]`, no `STATE`, no
user-defined parsing words. This is the project's foundational, permanent
divergence from Forth-2012; F0 seeds it as DIV-0001.

**D6 — The combinator library is the production parser.**
The Scheme reader hand-wrote recursive descent and used its combinators only
for atoms. Here the combinators are the parser: the grammar of F7 is built
from `parser`/`alt`/`many`/`lexeme` composition. If constexpr depth or
compile-time cost forces a hand-written region for nested control structures,
that is a divergence doc, not a silent fallback.

**D7 — Machine model: two stacks, cells, diagnosed errors.**
`cell` is `std::int64_t`. Data stack and return stack are fixed-capacity
`static_vector<cell, Max>` with template-parameter capacities. Stack
underflow, overflow, return-stack imbalance, division by zero, and control
misuse are **diagnosed errors** carried through `foundation::result` — never
UB, at compile time or runtime. The machine state (`forth_state`) bundles both
stacks, the data space, and an output buffer (D10), and is a literal type.

**D8 — Words are case-insensitive; fold to uppercase.**
Word names fold to uppercase at scan time and are stored folded, mirroring
traditional Forth. Numbers are signed decimal only.

**D9 — Elaboration owns resolution and stack-effect checking.**
Elaboration walks the tree in program order, threading a dictionary: each
colon definition is resolved against the words defined before it (use before
definition is an error; `RECURSE` names the definition being compiled, per
Forth). Elaboration computes each definition's static stack effect and
diagnoses: `IF`/`ELSE` arms with different net effects, unbalanced loop
bodies, `EXIT` from inside a `DO` loop without `UNLOOP`, return-stack
imbalance from `>R`/`R>` across control boundaries. If the first `( ... )`
comment after a definition's name has stack-effect shape (`( a b -- c )`),
it is a **declaration**, and the computed effect must match it.

**D10 — Memory and output.**
`VARIABLE`, `CONSTANT`, and `CREATE`/`ALLOT` allocate from an arena data space
of cells; an address is a typed cell index, not a native pointer; `@` and `!`
bounds-check. `DOES>` is optional (F20) and may be deferred by divergence doc.
Output words (`.`, `.S`, `EMIT`, `CR`) append to a fixed-capacity character
buffer in `forth_state`, so output is testable in `static_assert` and
printable at runtime; there is no direct I/O in the machine.

**D11 — One-shot dynamic-extent control only (the thesis).**
`EXIT` returns from the current definition. `LEAVE` exits the innermost `DO`
loop. `' word` (tick) yields an execution token as a cell; `EXECUTE` invokes
it; `CATCH` (`xt CATCH — 0 | err` with Forth-2012 stack restoration) and
`THROW` are the dynamic-extent exception pair; `ABORT` is `-1 THROW`.
All are one-shot and upward-only. A dead exit cannot be expressed (there are
no continuation values), and the sender backend maps: `THROW` → error
channel, `CATCH` → error-channel adapter restoring machine state,
`EXIT`/`LEAVE` → early completion of the enclosing scope's sender.

**D12 — Out of scope.**
Floating point, double-cell arithmetic, strings beyond comments
(`S"`, `."`, `ABORT"`), `PICK`/`ROLL`, block and file wordsets, locals,
`DEFER`/`IS`, `EVALUATE`, `>NUMBER`, `WORD`/`PARSE`, environmental queries,
`MARKER`/`FORGET`, and the whole compilation-semantics wordset (per D5).
The consolidation step (F22) records these in one limitations doc rather than
one divergence doc each.

---

# 5. Import inventory

What `~/src/compile-time-scheme/main` contributes, by disposition.

## Adapt by copy in F3/F4 (renamespace, parameterize, keep tests)

| Component | Files | Notes |
|---|---|---|
| `foundation/` | `result`, `parse_error`, `static_vector`, `source_pos`/`source_span`, `arena_box` (with `tree_arena`), `functor`, `applicative`, `alternative` | Language-agnostic. Do **not** import `foundation/fix.hpp` (D3). |
| `parser/` | `cursor`, `parser`, `alt`, `parser_ops` | Generic applicative/alternative combinators plus the typeclass-object (`parser_v` CPO) machinery. Leave the Scheme char predicates behind; Forth predicates arrive in F5. |

## Reuse as pattern only (reimplement, don't copy)

| Pattern | Source | Forth use |
|---|---|---|
| `sender_v.hpp` vocabulary aliases over Beman Execution | `sender/sender_v.hpp` | F2 recreates a minimal `smd::forth::sender::vocab` header. |
| `source_literal<N>` NTTP + `compiled_closure<Source>` one-shot API | `smdscheme.hpp` | F15's `compiled_forth<Source>`. |
| `foreign_function` as plain function pointer + span-of-values signature, `is_constant_evaluated` guard for runtime-only I/O | `closure/value.hpp`, `examples/godbolt_ffi.cpp` | F19's foreign words, adapted to stack access. |
| Immediately-invoked-lambda `static_assert` pattern | `docs/cps-direction.md` | All compile-time tests. |
| Negative compile tests matching specific diagnostics | `test_neg_*.cpp` | F21. |

## Lessons imported as constraints (not code)

- Structural recursion over flat arenas; `fix<F>` is not a literal type (D3).
- Tree nodes never embed value/closure types or anything non-trivially
  destructible; that discipline is what lets compiled programs persist to
  namespace-scope constexpr variables.
- Arena elements captured **by value** in any lambda that outlives its source.
- The sender backends that `sync_wait` per subexpression defeated the purpose
  of senders; F18 composes one sender per basic block and drives control flow
  explicitly (never `sync_wait` inside evaluation).
- Hardcoded capacities (the Scheme `env<Core,16>`) metastasize; every capacity
  here is a template parameter from day one (D2).

---

# 6. Language scope

The baseline word set, grouped by the step that lands it.

| Group | Words | Step |
|---|---|---|
| Arithmetic/logic | `+ - * / MOD NEGATE ABS MIN MAX AND OR XOR INVERT LSHIFT RSHIFT` | F8 (primitives), F13 (wired) |
| Comparison | `0= 0< = <> < > <= >= TRUE FALSE` | F8/F13 |
| Data stack | `DUP DROP SWAP OVER ROT ?DUP NIP TUCK DEPTH` | F8/F13 |
| Return stack | `>R R> R@` | F8/F13 (elaboration rules in F12) |
| Definitions | `: ; RECURSE` | F7 (parse), F11 (resolve) |
| Conditionals | `IF ELSE THEN` | F7/F13 |
| Indefinite loops | `BEGIN UNTIL`, `BEGIN WHILE REPEAT` | F7/F13 |
| Counted loops | `DO LOOP +LOOP I J LEAVE UNLOOP` | F17 |
| Memory | `VARIABLE CONSTANT CREATE ALLOT @ ! +!` | F10 (space), F16 (wired) |
| Output | `. .S EMIT CR` | F13 (buffered per D10) |
| Execution tokens | `' EXECUTE` | F18a |
| Exceptions | `CATCH THROW ABORT` | F18a |
| Early exit | `EXIT` | F13 (definition), F17 (`UNLOOP` interaction) |
| FFI | foreign word registration (no Forth-side syntax) | F19 |
| Defining words | `DOES>` | F20 (optional) |

---

# 7. Layout and namespace

```txt
src/smd/forth/CMakeLists.txt
src/smd/forth/forth.hpp                    (public one-shot API, F15)
src/smd/forth/foundation/                  (F3)
src/smd/forth/parser/                      (F4)
src/smd/forth/reader/                      (F5–F7: chars, syntax tree, grammar)
src/smd/forth/elaborator/                  (F11–F12)
src/smd/forth/machine/                     (F8–F10, F13–F14, F16–F17, F18a)
src/smd/forth/sender/                      (F2 vocab, F18)
src/examples/                              (hello evolves; Godbolt + FFI examples)
vendor/execution                           (F2, git submodule)
```

Namespace `smd::forth`, one sub-namespace per directory
(`smd::forth::reader`, etc.). All existing style rules apply: file prologs
with repo-relative path + Emacs mode line, SPDX, classical include guards,
canonical angle-bracket includes, co-located `.test.cpp`, constexpr-first,
every public constexpr API exercised in a `static_assert`, UUID anchors on
code destined for prose.

CMake: targets with `target_sources` + `FILE_SET HEADERS`; each
`CMakeLists.txt` lists only its own directory's files; new directories are
added with `add_subdirectory`. The existing installed target name
(`compile-time-forth.forth`) and install/packaging wiring stay intact.

---

# 8. Documentation workstream

Lighter than the Scheme repo's blog pipeline; two artifacts:

- `docs/compiler_architecture.org` — created at F6, updated by every step that
  adds a pipeline stage (F7, F11, F12, F13, F14, F15, F18, F19), transcluding
  live code via UUID anchors, never hand-copied blocks.
- `compile-time-forth.org` (repo root, the presentation) — rewritten at F22 to
  transclude the real compiler instead of the scaffold placeholder.

Prose is drafted by agents, one sentence per line, marked
`DRAFT — pending author revision`; the author finalizes.

---

# 9. Steps

Each step states goal, key files, sketch where the design is novel, merge
criteria, and dependencies. "Verify" always means `make compile`, `make test`,
`make lint` green, plus the orchestrator checks in section 2. Capacities in
sketches are template parameters with the shown values as defaults.

## Step F0 — Governance install

Copy and adapt from `~/src/compile-time-scheme/main`: `docs/codestyle.org`,
`docs/CODING_RULES.md`, `AGENTS.md` (drop the Copier-template rule and the
Scheme-specific architecture facts; namespace examples become `smd::forth`;
required commands are this repo's `make compile/test/lint` and the smoke
driver). Create repo `CLAUDE.md` pointing at `AGENTS.md`. Create
`checklist.md` with the section from section 10 of this plan, `handoff.md`
seeded from section 11, `handoff-next.md` for F1,
`docs/divergences/TEMPLATE.md`, and `DIV-0001-structural-parse.md` recording
D5 as accepted-permanent.
Merge criteria: verify passes (governance files are lint-clean; markdownlint
and codespell run on them).
Dependencies: none.

## Step F1 — C++26 baseline

Update `etc/gcc-flags.cmake` and `etc/clang-flags.cmake` to
`CMAKE_CXX_STANDARD 26` / `-std=gnu++26`, matching the Scheme repo's settings.
Confirm the scaffold builds and tests pass on `TOOLCHAIN=gcc-16` and
`TOOLCHAIN=clang-21` via the smoke driver.
Merge criteria: `smoke.sh gcc-16` and `smoke.sh clang-21` both end `SMOKE OK`.
Dependencies: F0.

## Step F2 — Vendor Beman Execution

Add `vendor/execution` as a git submodule (Beman Execution), integrate with
`add_subdirectory` from the top-level `CMakeLists.txt` behind the existing
project options, and create `src/smd/forth/sender/vocab.hpp` (+ test) aliasing
`just`, `then`, `let_value`, `when_all`, `sync_wait` into
`smd::forth::sender`. The test `sync_wait(then(just(20), [](int x){ return x + 22; }))`
passing 42 proves the toolchain digests the vendored tree under gnu++26.
Update `.update-submodules` flow (the Makefile already runs
`git submodule update --init --recursive`).
Merge criteria: verify passes on gcc-16 and clang-21; submodule documented in
`handoff.md`.
Dependencies: F1.

## Step F3 — Import foundation

Copy from the Scheme repo per D2:
`src/smd/forth/foundation/{static_vector,result,parse_error,source_pos,arena_box,functor,applicative,alternative}.hpp`
plus their tests (renamed `.test.cpp` where needed), renamespaced to
`smd::forth::foundation`, prologs updated with provenance, capacities
parameterized. Do not import `fix.hpp` (D3). Wire a
`compile-time-forth.foundation` CMake target (or fold into the main library —
worker's choice, but file sets and verify-interface-header-sets stay on).
Merge criteria: imported tests pass; a `static_assert` builds a small
`tree_arena` of a local test node type and reads it back by `arena_box`
handle.
Dependencies: F1. Parallel with F2.

## Step F4 — Import parser combinators

Copy `src/smd/forth/parser/{cursor,parser,alt,parser_ops}.hpp` + tests,
renamespaced `smd::forth::parser`, Scheme char predicates left behind (the
cursor keeps only generic char/position machinery). Includes the
functor/applicative/alternative typeclass-object layering (`parser_v` CPO).
Merge criteria: imported combinator and typeclass-law tests pass;
`static_assert` parses `"42"` with `integer_p`-style composition rebuilt from
primitives (not the Scheme atom parser).
Dependencies: F3.

## Step F5 — Forth lexical layer

`src/smd/forth/reader/forth_chars.hpp` (+ test): a word is any run of
non-whitespace; case folding to uppercase (D8); intertoken space skipping
including `\` line comments and `( ... )` comments; number recognition
(optional `-`, decimal digits); comment-capture support that preserves the
text of a `( ... )` comment when asked (F7 uses this for stack-effect
declarations, D9).
Merge criteria: static_asserts for folding (`dup` → `DUP`), comment skipping
(both kinds), number/word discrimination (`-1` number, `1-` word, `-` word).
Dependencies: F4.

## Step F6 — Syntax tree

`src/smd/forth/reader/syntax_tree.hpp` (+ test): arena-backed tree per D3.
Node kinds: `syn_literal{cell}`, `syn_word{name}`,
`syn_colon_def{name, declared_effect, body}`, `syn_if{then_body, else_body}`,
`syn_begin_until{body}`, `syn_begin_while{condition, body}`,
`syn_do_loop{body, is_plus_loop}`, `syn_variable{name}`,
`syn_constant{name}`, `syn_create{name}`, `syn_tick{name}`.
Bodies are `static_vector<arena_box<...>, MaxBody>`. Names are
`static_vector<char, MaxName>` (folded). `declared_effect` stores the raw
comment text span or empty. All nodes trivially destructible; capacities are
template parameters.
Also create `docs/compiler_architecture.org` with the pipeline diagram and the
first UUID anchors.
Merge criteria: static_assert hand-builds `: SQUARED DUP * ;` as a tree and
walks it back.
Dependencies: F3. Parallel with F4/F5.

## Step F7 — Grammar

`src/smd/forth/reader/read_program.hpp` (+ test): the production parser, built
from the F4 combinators per D6.

```txt
program   := item* eof
item      := colon-def | variable | constant | create | body-item
colon-def := ':' name effect-comment? body-item* ';'
body-item := literal | tick | if | begin-until | begin-while | do-loop | word
if        := 'IF' body-item* ('ELSE' body-item*)? 'THEN'
begin-until := 'BEGIN' body-item* 'UNTIL'
begin-while := 'BEGIN' body-item* 'WHILE' body-item* 'REPEAT'
do-loop   := 'DO' body-item* ('LOOP' | '+LOOP')
tick      := ''' name
```

Control-structure keywords and `:`/`;` are recognized after case folding and
are reserved (a colon definition may not redefine them — diagnosed error).
The first `( ... )` comment after a definition's name is captured as the
declared stack effect if it contains `--`. Errors carry source positions:
unterminated definition, `ELSE`/`THEN` without `IF`, stray `;`, nested `:`.
Recursion over nesting is bounded; depth is a template parameter.
Merge criteria: static_asserts round-trip
`: ABS ( n -- n ) DUP 0< IF NEGATE THEN ;` and a two-level nesting
(`IF` inside `BEGIN ... UNTIL` inside a definition); failure tests for each
error case with position checks.
Deliverable: architecture-doc section for reader + grammar.
Dependencies: F5 and F6.

## Step F8 — Machine substrate

`src/smd/forth/machine/{cell,stacks,forth_state}.hpp` (+ tests):
`cell = std::int64_t`; `data_stack<MaxDepth>` / `return_stack<MaxDepth>` over
`static_vector` with `push`/`pop`/`peek` returning `result` (underflow and
overflow diagnosed, D7); `forth_state<MaxDepth,MaxRDepth,MaxData,MaxOut>`
bundling both stacks, the data space (sized, wired in F10), and the output
buffer `static_vector<char, MaxOut>` with `emit_char`/`emit_cell` formatting
helpers (D10). The primitive opcode enum for the section-6 arithmetic,
comparison, and stack-manipulation words, plus
`constexpr auto apply_primitive(prim, forth_state&) -> result<void>` for the
pure-stack primitives, including division-by-zero diagnosis.
Merge criteria: static_asserts for each primitive's stack behavior and for
underflow/overflow/div-zero errors; `emit_cell(-42)` yields `"-42 "`.
Dependencies: F3. Parallel with F5–F7.

## Step F9 — Dictionary

`src/smd/forth/machine/dictionary.hpp` (+ test): arena-backed word list.
Entry: folded name plus a variant of
`{primitive_opcode, colon_word{core_id, effect}, variable_word{addr},
constant_word{cell}, foreign_word{index}}`.
Lookup is linear, newest-first, so redefinition shadows (traditional Forth
behavior); redefinition is legal and not an error, but the elaborator warns
via a collected-diagnostics channel (later words see the new definition,
earlier resolutions keep the old one — static binding falls out of F11's
program-order resolution and matches how a cross-compiling Forth behaves; this
is worth an architecture-doc paragraph, not a divergence).
`default_dictionary()` installs the primitives.
Merge criteria: static_asserts for lookup, shadowing, and case-folded lookup
(`dup` finds `DUP`).
Dependencies: F8.

## Step F10 — Data space

`src/smd/forth/machine/data_space.hpp` (+ test), wired into `forth_state`:
a cell arena with `allot(n) -> result<addr>`, `fetch(addr)`, `store(addr,
cell)`, all bounds-checked; `addr` is a distinct typed index (D10) —
convertible to/from `cell` explicitly so addresses can live on the data stack.
Merge criteria: static_asserts for allot/fetch/store round-trip and
out-of-bounds diagnosis.
Dependencies: F8.

## Step F11 — Elaborated core and resolution

`src/smd/forth/elaborator/{elaborated_core,elaborate}.hpp` (+ tests):
the core tree (arena, D3) with node kinds `core_push{cell}`,
`core_prim{opcode}`, `core_call{word_index}`, `core_var{addr}`,
`core_const{cell}`, `core_push_xt{word_index}`, `core_if`,
`core_begin_until`, `core_begin_while`, `core_do_loop`, `core_exit`,
`core_seq`, plus a `compiled_unit` holding the core arena, the dictionary,
the data-space size consumed by declarations, and the top-level body.
`elaborate(syntax, ...) -> result<compiled_unit>` walks in program order per
D9: definitions extend the dictionary as encountered; word references resolve
against the dictionary at their position; unknown word is an error with
position; `RECURSE` resolves to the definition being compiled; `VARIABLE`
allots one cell, `CONSTANT` pops nothing at elaboration (its value is the
preceding literal — Forth's `5 CONSTANT FIVE` means the elaborator constant-
folds the immediately preceding push; a non-constant initializer is a
diagnosed error, recorded in the architecture doc).
Merge criteria: static_asserts resolving `: SQUARED DUP * ; : QUAD SQUARED
SQUARED ;`; error tests for unknown word and forward reference.
Dependencies: F7, F9, F10.

## Step F12 — Stack-effect analysis

`src/smd/forth/elaborator/stack_effect.hpp` (+ tests), run as part of
`elaborate` per D9: abstract interpretation over the core computing, per
definition, net data-stack effect and minimum entry depth, tracking the
return stack separately.
Diagnosed: `IF` arms with unequal net effects, loop bodies with nonzero net
effect (`DO` body must be net-zero; `BEGIN ... UNTIL` body net-zero after the
`UNTIL` flag pop; `WHILE` condition leaves exactly the flag), `>R`/`R>`
imbalance across a control boundary, `EXIT` inside a `DO` loop without
`UNLOOP`, and mismatch between a declared `( a b -- c )` effect and the
computed one.
Words with input-dependent effects (`?DUP`, `EXECUTE`-reached code) get an
`unknown` effect lattice value that suppresses checking downstream rather
than erroring — record the lattice in the architecture doc.
Merge criteria: positive tests for declared-effect verification; one failure
test per diagnosis listed above.
Deliverable: architecture-doc section for elaboration + analysis.
Dependencies: F11.

## Step F13 — Direct evaluator

`src/smd/forth/machine/eval_direct.hpp` (+ tests): structural-recursive
reference interpreter over the core: primitives via `apply_primitive`, calls
by recursion into the callee's core, `IF`/`BEGIN` control, `EXIT` as an
early-return signal in the result channel, output words appending to the
state buffer, all under a fuel/step budget so a nonterminating
`BEGIN ... UNTIL` is a diagnosed budget-exhaustion error, not a hung
constexpr evaluation.
Merge criteria (end-to-end static_asserts, whole pipeline
read → elaborate → eval):

```forth
: SQUARED DUP * ;  4 SQUARED          \ stack [16]
: ABS DUP 0< IF NEGATE THEN ;  -7 ABS \ stack [7]
: COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN
                                      \ stack [], output "3 2 1 "
```

plus a budget-exhaustion test for `: SPIN BEGIN FALSE UNTIL ; SPIN` with a
small budget.
Deliverable: architecture-doc section; this is the first full-pipeline
milestone.
Dependencies: F11 (F12 lands before or with it; order F12 first).

## Step F14 — Stack-machine codegen and VM

The classic stack machine. `src/smd/forth/machine/{instruction,codegen,vm}.hpp`
(+ tests):

```cpp
// One instruction: opcode + immediate operand. The program is a flat
// static_vector<instr, MaxCode> in a compiled_program that also carries
// the dictionary's word table (entry point per word), data-space size,
// and required stack capacities. compiled_program is a literal type and
// trivially copyable: it is THE artifact that survives to runtime.
enum class op : std::uint8_t {
    push, prim, call, ret, branch, branch0,   // branch0: pop flag, branch if zero
    do_setup, loop_step, plus_loop_step, push_index, leave, unloop,
    push_xt, execute, catch_mark, throw_op, halt
};
struct instr { op code; cell operand; };
```

`codegen(compiled_unit) -> result<compiled_program>` flattens the core:
control structures become `branch`/`branch0` with resolved instruction
indices (back-patching inside codegen); definitions become entry points;
`EXIT` becomes `ret`.
`run(compiled_program const&, forth_state&, fuel) -> result<void>` is the VM:
an explicit loop over `ip` with a call-return stack (the return stack per
Forth: calls and loop parameters share it), fully constexpr, same code
executing at runtime.
Merge criteria: the entire F13 test set passes through codegen+VM at compile
time via static_assert **and** at runtime via Catch2 `REQUIRE` on the same
program object declared `constexpr` at namespace scope — the
survives-to-runtime proof.
Deliverable: architecture-doc section for the instruction set.
Dependencies: F13.

## Step F15 — Public one-shot API

Replace the placeholder `src/smd/forth/forth.hpp`: `source_literal<N>` (NTTP
char-array wrapper, pattern per section 5) and

```cpp
template <source_literal Source>
inline constexpr auto compiled_forth = /* read→elaborate→codegen, .value() */;
// compiled_forth<"...">.run() -> result<forth_state> at compile time or runtime;
// convenience accessors: .stack(), .output().
```

A failed parse/elaboration is a hard compile error (non-constant `.value()`).
Update `src/examples/hello.cpp` to run a small program through
`compiled_forth` and print its output; keep the example's test green. Add a
Godbolt single-file extraction example under `src/examples/`.
The old name-returning placeholder API is deleted; `forth.test.cpp` becomes
the public-API test. The org transclusion anchors in `compile-time-forth.org`
that referenced the placeholder are updated to real anchors (or the org file
gains a TODO note for F22 — worker's choice, recorded in handoff).
Merge criteria: `compiled_forth<": SQUARED DUP * ; 4 SQUARED">.stack()`
static_asserts to `[16]`; hello example prints program output; a
negative-compile test proves a syntax error fails compilation.
Dependencies: F14.

## Step F16 — Memory words end-to-end

Wire `VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT`/`@`/`!`/`+!` through both
evaluators (F13's and F14's): data-space initialization from the
`compiled_unit`, addresses on the stack as cells (D10).
Merge criteria:

```forth
VARIABLE X  5 X !  X @ 3 + X !  X @   \ stack [8]
7 CONSTANT LUCKY  LUCKY LUCKY +       \ stack [14]
CREATE BUF 4 ALLOT                    \ BUF usable as base address
```

as static_asserts through both backends; out-of-bounds store diagnosed.
Dependencies: F14 (parallel with F15).

## Step F17 — Counted loops

`DO LOOP +LOOP I J LEAVE UNLOOP` through elaboration (F12 rules), direct
evaluator, and VM (loop parameters on the return stack; `LEAVE` as a one-shot
forward transfer to loop exit; `+LOOP` crossing-the-boundary termination per
Forth-2012).
Merge criteria:

```forth
: SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO      \ stack [15]
: EVENS 0 BEGIN DUP 10 < WHILE DUP . 2 + REPEAT DROP ; EVENS
                                                 \ output "0 2 4 6 8 "
: FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5  \ stack [5]
```

through both backends, plus nested-loop `J` and the `EXIT`-without-`UNLOOP`
diagnosis from F12 firing on a real program.
Dependencies: F14 (parallel with F15/F16).

## Step F18a — Execution tokens and exceptions

`' word`/`EXECUTE`/`CATCH`/`THROW`/`ABORT` per D11, in elaborator, direct
evaluator, and VM. `CATCH` semantics per Forth-2012: save data-stack depth
and a handler mark; on `THROW n` (n≠0), restore data-stack depth (contents
unspecified — here: truncate), unwind the return stack to the handler, push
n; `xt CATCH` pushes 0 on normal completion. Uncaught `THROW` is a diagnosed
error carrying n. In the VM this is a handler frame on the return stack; in
the direct evaluator it is an error-channel value intercepted at the `CATCH`
site.
Merge criteria:

```forth
: BOOM 1 THROW ;
: SAFE ' BOOM CATCH ;  SAFE           \ stack [1]
: OK 42 ;  : TRY ' OK CATCH ;  TRY    \ stack [42 0]
```

through both backends; uncaught-throw diagnosis; stack-depth restoration
test.
Dependencies: F14 (schedule after F17 to avoid rebasing the return-stack
machinery).

## Step F18 — Sender/Receiver CPS backend

`src/smd/forth/sender/` (+ tests): the thesis made executable (section 0,
D11), executing the **same `compiled_program`** the VM runs.

Design (sketch — the details are the step's discovery, deviations get
divergence docs):

- Partition the instruction stream into basic blocks (straight-line runs
  ending in branch/call/ret/throw/halt) — a small analysis over `instr`s.
- A basic block compiles to one composed sender:
  `just(state) | then(instr₁) | then(instr₂) | …` — never `sync_wait` inside
  evaluation (section 5 lesson).
- Control transfer is an explicit constexpr trampoline: run a block's sender,
  its value completion carries `(state, next_block)`; loop until `halt`.
  If expressing the trampoline over raw senders fights the type system,
  vendor Beman Task (`vendor/task`, per D4) and write the driver as a
  coroutine `task<result<forth_state>>` — that choice is pre-authorized,
  record it in `handoff.md`, no divergence doc needed.
- Channel mapping per D11: `THROW` completes the block's sender on the error
  channel with `(n, state-at-throw)`; `CATCH` runs the protected region under
  an adapter that converts an error completion back to a value completion
  with the Forth-mandated stack restoration; `EXIT`/`ret` and `LEAVE` are
  ordinary value completions to the trampoline (upward, one-shot). A test
  demonstrates stop-channel usage for fuel exhaustion.
Merge criteria: the full F13/F16/F17/F18a merge-criteria program set passes
through the sender backend with identical final states; the CATCH/THROW tests
demonstrably route through `set_error`; constexpr where Beman Execution
permits, runtime otherwise (record which in the architecture doc — if
`sync_wait` isn't constexpr-capable, compile-time coverage comes from the VM
and the sender backend is the runtime path; that split is acceptable and
documented, not a divergence).
Deliverable: architecture-doc section mapping Forth control to completion
channels.
Dependencies: F14 and F18a; F2 for the vendored tree.

## Step F19 — Foreign function interface

Per D11/section 5: `foreign_word` — a plain function pointer
`result<void>(*)(forth_state&)` (direct access to the underlying stacks,
data space, and output buffer, as requested) registered into the dictionary
by a builder API before compilation:

```cpp
// C++ side: pop two, push one; constexpr-capable, runtime I/O guarded.
constexpr auto gcd_word(forth_state& s) -> result<void>;
auto dict = default_dictionary().with_foreign("GCD", gcd_word);
```

plus the reverse direction: a `compiled_program` is a value; an example
embeds one in a C++ function, pushes arguments from C++ variables, runs,
and reads results off the exposed stack — Forth as an embedded constexpr
scripting engine.
Foreign words are opaque to stack-effect analysis unless registered with a
declared effect (extend `with_foreign` with an optional effect argument;
undeclared → `unknown` lattice value per F12).
Merge criteria: a static_assert computing with a constexpr foreign word
through both VM and sender backends; a runtime example whose foreign word
prints via `is_constant_evaluated` guard; Godbolt FFI example under
`src/examples/`.
Deliverable: architecture-doc FFI section.
Dependencies: F14; sender coverage after F18.

## Step F20 — `CREATE`/`DOES>` (optional)

`DOES>` gives created words colon-defined behavior over their data address —
the one defining-word idiom worth keeping (D10). Elaborate `DOES>` inside a
colon definition as: subsequent code becomes the behavior attached to the
most recent `CREATE`d word at that definition's execution. If this doesn't
fit the structural model cleanly, defer with a divergence doc; `VARIABLE`/
`CONSTANT`/`CREATE`/`ALLOT` already cover the data-structure story.
Merge criteria: the classic

```forth
: CONSTANT2 CREATE , DOES> @ ;   \ requires , (comma) — add it here
```

or a documented deferral.
Dependencies: F16, F18a.

## Step F21 — Error-quality and negative-compile pass

Every diagnosed error carries a source position and a stable message; a
table-driven test walks representative bad programs and checks both. Negative
compile tests (`test_neg_*.cpp` pattern per section 5) for: syntax error in
`compiled_forth` NTTP source, stack-effect declaration mismatch, capacity
overflow. Match specific diagnostics.
Merge criteria: verify passes; each negative test fails compilation for the
stated reason.
Dependencies: F15; best scheduled after F18a so the error space is complete.

## Step F22 — Documentation consolidation

`docs/forth-limitations.md` rolling up D5/D12 and all divergence docs;
`docs/compiler_architecture.org` completed and internally consistent;
`compile-time-forth.org` (presentation) rewritten to transclude the real
compiler via UUID anchors; README.md rewritten from scaffold-description to
project-description (keep the build-workflow section, it is accurate);
final `handoff.md` rewrite describing the finished state.
Merge criteria: verify passes; every divergence doc is referenced from the
limitations doc; `make presentation` renders if emacs is available (orchestrator
runs it; workers must not require emacs).
Dependencies: everything else.

## Parallelism summary

- Track A (front end): F4 → F5 → F7 (F6 joins F7).
- Track B (machine): F8 → F9/F10.
- Track C (spine): F11 → F12 → F13 → F14 → {F15, F16, F17} → F18a → F18 → F19.
- F0 → F1 → {F2, F3}; F3 → {F4, F6, F8}; A and B run in parallel; C needs
  both.
- F15/F16/F17 are mutually parallel after F14 (separate worktrees; F17 and
  F18a touch the return stack — keep them serial with each other).
- F20–F22 slot in as dependencies allow; F21 after F18a; F22 last.

---

# 10. Checklist section (F0 installs as `checklist.md`)

```markdown
# compile-time-forth checklist

## Forth compiler (docs/forth-plan.md)

- [ ] Step F0: governance install
- [ ] Step F1: C++26 baseline
- [ ] Step F2: vendor Beman Execution
- [ ] Step F3: import foundation
- [ ] Step F4: import parser combinators
- [ ] Step F5: Forth lexical layer
- [ ] Step F6: syntax tree
- [ ] Step F7: grammar
- [ ] Step F8: machine substrate
- [ ] Step F9: dictionary
- [ ] Step F10: data space
- [ ] Step F11: elaborated core and resolution
- [ ] Step F12: stack-effect analysis
- [ ] Step F13: direct evaluator
- [ ] Step F14: stack-machine codegen and VM
- [ ] Step F15: public one-shot API
- [ ] Step F16: memory words end-to-end
- [ ] Step F17: counted loops
- [ ] Step F18a: execution tokens and exceptions
- [ ] Step F18: sender/receiver CPS backend
- [ ] Step F19: foreign function interface
- [ ] Step F20: CREATE/DOES> (optional)
- [ ] Step F21: error-quality and negative-compile pass
- [ ] Step F22: documentation consolidation
```

---

# 11. `handoff.md` seed (F0 installs)

Durable facts to record:

```txt
This is smd/forth, a compile-time and runtime Forth compiler in C++26 on GCC16; plan in docs/forth-plan.md.
The Makefile is the single build interface, parameterized by TOOLCHAIN and CONFIG; new flag-sets are new CONFIGs; all compiled files are always compiled.
The smoke driver is .claude/skills/run-compile-time-forth/smoke.sh [TOOLCHAIN] [CONFIG].
foundation/ and parser/ are adapted by copy from compile-time-scheme (smd::smdscheme); provenance lines in file prologs; no build coupling to that repo.
Every tree (syntax, core, instruction program) is a flat tree_arena of trivially destructible nodes referenced by integer arena_box handles; heap-backed fix/Box types are barred from the pipeline (docs/forth-plan.md D3).
Compile-time tests use the immediately-invoked-lambda static_assert pattern; every public constexpr API has one.
All capacities are template parameters with defaults; no hardcoded capacity constants.
Words fold to uppercase at scan time; source is parsed structurally by applicative combinators (DIV-0001); there is no interpretive outer loop and no IMMEDIATE.
The machine is two fixed-capacity stacks plus an arena data space and an output buffer; all misuse is a diagnosed error via foundation::result, never UB.
All nonlocal control (EXIT, LEAVE, CATCH/THROW) is one-shot and dynamic-extent; the sender backend maps THROW to the error channel.
Beman Execution is vendored as a git submodule at vendor/execution, integrated by add_subdirectory; Beman Task at vendor/task only if F18 needed it.
```

---

# 12. Canonical clean-agent instruction

```txt
Please read AGENTS.md, docs/codestyle.org, docs/CODING_RULES.md, CLAUDE.md, handoff.md, handoff-next.md, and checklist.md, then the step section of docs/forth-plan.md given below.

Work only the step named below, using its plan section as the specification, in the git worktree you were given.

Finish only that step.
Keep the change small and mergeable.
Do not leave vague TODOs.

Run:

make compile
make test
make lint

(.claude/skills/run-compile-time-forth/smoke.sh runs build+test+example in one command.)

When everything is green, update checklist.md, update handoff.md with durable facts only, rewrite handoff-next.md for the next agent, and file docs/divergences/DIV-NNNN docs for anything done differently than the plan or Forth-2012 specifies.

Report: what you built, every deviation (with DIV numbers), and what the next step needs to know.
Do not continue into the following step; if blocked, document the blocker in handoff-next.md and report it.
```
