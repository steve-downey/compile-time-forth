# Compile-Time Forth — Agent Execution Plan, Revision 2 (True Forth)

This revision supersedes `docs/forth-plan.md` ("the R1 plan") in its thesis
framing (section 0), its decision records where the disposition table below
says so, its language scope (section 6), and its remaining step sequence:
steps F18a–F22 are **retired unexecuted** and replaced by F23–F36 below.
Everything else is inherited by reference, unchanged: rule and style
precedence (R1 §1), the orchestration protocol and worker duties (R1 §2), the
divergence-doc mechanism (R1 §3), the documentation workstream and blog
contract (R1 §8, `docs/blog/AGENTS.md`), and the canonical clean-agent
instruction (R1 §12). Workers still read the bounded three-tier set; the
orchestrator still pastes one self-contained step section per worker.

State at this revision: F0–F17 are complete and merged; blog Parts 0–11 are
published; the default dictionary holds 46 primitives; DIV-0001 through
DIV-0010 are filed.

---

# 0. Thesis and goals (revised)

The two goals stand, with the first enlarged and the second unchanged:

1. **The same compiled system runs at compile time and at runtime.** The
   artifact is no longer a compiled tree but a **session image**: the
   Forth-2012 text interpreter runs a whole session — interpreting,
   compiling, and executing immediate words — inside one constant-expression
   evaluation, and the residue (code space, final dictionary, data-space
   seed, captured output) is a trivially copyable literal that executes again
   at ordinary runtime. `compiled_forth<"...">` keeps its name, its NTTP
   keying, and its malformed-program-is-a-hard-compile-error contract.

2. **Forth's control vocabulary is exactly what structured concurrency can
   express.** `EXIT`, `LEAVE`, `CATCH`/`THROW` are one-shot, upward-only,
   dynamic-extent — the sender/receiver completion discipline. Unchanged
   from R1 §0 and D11.

What changes is the road to both. R1's D5 built a Forth with a fixed grammar
and no outer interpreter; that produced a working substrate (machine,
dictionary, instruction set, VM, one-shot API — all retained) under a front
end that is not Forth. This revision builds the real thing: the stateful
text interpreter, `STATE`, `IMMEDIATE`, `POSTPONE`, `[` `]`, user parsing
words. Forth metaprogramming is just programming, and the system must
exhibit that — immediate words are ordinary Forth executed by the same
machine during compilation, which itself happens inside the constant
evaluator.

The theoretical frame tightens rather than loosens: threaded code is CPS
with the continuation defunctionalized as the return stack (CPS being a
complete intermediate language, equivalent to SSA), and the sender lowering
is refunctionalization. R1's own emission machinery anticipated this —
codegen's `branch0`-with-sentinel back-patching and F17's `LEAVE` sentinel
scan are precisely what `IF`/`THEN`/`LEAVE` do as immediate words in a
classical Forth. The relocation of that machinery from a tree walker into
the compiling words is the heart of the pivot.

## What this project now is, and still is not

- It **is** aimed at Forth-2012 core conformance, with gforth as the
  practical differential oracle (the ccforth ordering: gforth compatibility
  subsumes ANS core). Divergences are declared system characteristics or
  DIV docs, not silent.
- It is **still not** an interactive system: no `QUIT` loop, no user input
  at runtime. A session is compiled whole from a source string; "runtime"
  runs the resulting image.
- Floating point, double-cell arithmetic, blocks/files, and locals remain
  out of scope (D12 as rescoped in section 3).

---

# 1. Process inheritance and deltas

Inherited verbatim from R1: §1 (precedence), §2 (orchestration protocol,
worker duties, blog-agent dispatch), §3 (divergence docs), §8
(documentation workstream), §12 (clean-agent instruction). Deltas:

- **Toolchain matrix cadence** moves to F26, F31, and F33 (was F7/F14/F18).
- **Transclusion integrity during retirement:** F26 deletes source files
  that `docs/compiler_architecture.org` transcludes. The step must move the
  affected phase sections to `docs/history/architecture-grammar-era.org`
  with transclusions expanded to static code blocks (one-time hand
  expansion is authorized for the history snapshot only), so the
  orchestrator's transclusion check stays green on the living doc.
- **Effect-gate suspension window:** between F26 and F30 there is no
  stack-effect checking (the R1 checker dies with the elaborator; its
  replacement lands at F30). Declared-effect comments are still captured
  and stored; they are simply not verified in the window. This is a known,
  bounded regression, recorded in the F26 DIV, not a silent one.

---

# 2. Disposition of R1 decision records

| # | Disposition | Notes |
|---|---|---|
| D1 | **Amended** | Components become `foundation`, `parser`, `interpreter`, `machine`, `sender`. `reader/` and `elaborator/` retire at F26 (the token-scanning parts of `reader/` move under `interpreter/` or `parser/` — worker's choice at F24, recorded in the architecture doc). |
| D2 | Stands | Import-by-copy provenance discipline unchanged. |
| D3 | **Stands, narrowed** | The principle (no heap-backed `fix`/`Box`; literal types only in the compiled pipeline) is untouched. The word "tree" narrows: after F26 the flat structures are code space, dictionary, and data space. Structural recursion over arena trees is no longer the compute model; the interpreter loop and the VM are. |
| D4 | Stands | C++26, GCC16 + clang-21, Beman Execution vendored; Beman Task pre-authorized for F33's trampoline. |
| D5 | **Overturned** | The pivot. F23 files the superseding divergence doc (next DIV number) marking DIV-0001 superseded: the structural grammar was a valid substrate-building strategy and is not the destination. |
| D6 | **Moot** | There is no grammar for the combinators to be. Their real role is D19. DIV-0005's hand-written productions are deleted with the grammar. |
| D7 | Stands | Machine model, diagnosed errors, never UB. |
| D8 | Stands | Case folding at scan time. |
| D9 | **Replaced** | Resolution happens when the interpreter meets the word (D13); effect checking becomes a lint over emitted code (D20). Program-order discipline is preserved — it becomes structural instead of simulated. |
| D10 | **Stands, amended** | Memory/output model unchanged. `,` (comma) becomes a real word (F28). Address units per D21. `DOES>` moves from optional to required (F28). |
| D11 | Stands verbatim | The thesis. |
| D12 | **Rescoped** | Moving *into* scope: the compilation-semantics wordset (`IMMEDIATE POSTPONE [ ] LITERAL STATE`), `WORD`/`PARSE`, `S" ." ABORT"`, `DEFER`/`IS`, `VALUE`/`TO`, `EVALUATE` (optional, F32). Remaining out: floating point, double-cell, `PICK`/`ROLL` (add only when a criterion demands, per the 1+ policy), blocks/files, locals, `MARKER`/`FORGET`, environmental queries, `>NUMBER`. |

---

# 3. New decision records

Overturning one of these requires a divergence doc and orchestrator
sign-off, as before.

**D13 — The text interpreter is the architecture.**
The Forth-2012 §3.4 loop is the top of the system: parse a word; find it;
if found, execute when interpreting or when immediate, else compile; else
try number per `BASE`, pushing or compiling a literal per `STATE`; else a
diagnosed error with source position. `STATE`, `SOURCE`, `>IN`, `BASE` are
machine state in `forth_state`. There is no reader phase, no syntax tree,
no elaboration phase. A word resolves against the dictionary as it exists
at the moment the interpreter meets it — R1's program-order discipline made
structural. Compile-only words (`;`, `EXIT`, control words, `POSTPONE`,
`LITERAL`, `DOES>`, `RECURSE`) diagnose use while interpreting, per
standard.

**D14 — One semantics; external oracles.**
A defined word means its compiled code, executed by the VM; interpreting it
executes the same code a compiled call executes. There is no reference
tree-walker. Correctness rests on: (a) the same `compiled_program` value
agreeing with itself at compile time and runtime (the F14 proof pattern,
retained); (b) gforth as differential oracle; (c) the Forth-2012 core test
suite (Hayes tester) under constant evaluation as `static_assert` gates.

**D15 — The artifact is a session image.**
`compiled_forth<Source>` runs the whole session in one constant-expression
evaluation; its result bundles code space, the final dictionary, the
data-space high-water mark (the F16 seeding discipline generalized), and
captured output, as a trivially copyable literal. Compilation performed by
immediate words is compile-time work because the interpreter performing it
runs at compile time. A session cannot span constant-expression
evaluations; cross-instantiation persistent state is out of scope by
construction. If constant-evaluation cost ever forces it, a
prelude-image-feeds-later-sessions design is the sanctioned extension —
scheduled deliberately, not patched in.

**D16 — The instruction set and VM carry; emission relocates.**
The opcode enum, `instr`, `compiled_program`, and the VM loop are retained.
`execute` gains semantics at F28, `catch_mark`/`throw_op` at F31. The
tree-driven `codegen` entry points are deleted at F26; the emission
utilities (append an `instr`, sentinel-and-patch) survive as the compiling
words' toolkit behind `,`, `COMPILE,`, and the immediate control words.
Sequential compilation preserves F14's no-call-back-patching property: a
callee is always already compiled (program order) and `RECURSE`'s target is
the entry being built.

**D17 — Control-flow words are immediate words on the standard discipline.**
`IF ELSE THEN`, `BEGIN UNTIL`, `BEGIN WHILE REPEAT`, `DO LOOP +LOOP LEAVE
UNLOOP I J` are immediate (or interpreter-recognized compile-only) words
using Forth-2012 orig/dest control-flow-stack discipline, with the data
stack serving as the control-flow stack as the standard permits. `IF` emits
`branch0` with a sentinel and pushes the orig; `THEN` resolves it. `LEAVE`
keeps F17's sentinel-scan, driven by `DO`'s dest marker. The `+LOOP`
boundary-crossing termination rule and `LOOP` equality rule carry verbatim
from F17.

**D18 — Every word has an execution token.**
Dictionary entries converge to the classical header: folded name, flags
(`IMMEDIATE`, compile-only), an XT, and a does-field. Primitives get a
uniform XT encoding (a one-instruction stub in code space, or a tagged XT
naming the primitive — decided once at F28, recorded in the architecture
doc). `'`, `[']`, `EXECUTE`, `DEFER` are uniform over every binding kind;
the F9 variant-of-kinds dissolves into the header. `variable_word.address`
keeps its `addr` type.

**D19 — Combinators below the word; the input stream is machine state.**
`parser<F>` combinators own scanning and classification: maximal
non-whitespace, number per `BASE`, comment forms, string payloads. They
operate over the machine's input source (`SOURCE`/`>IN`) and report
consumption the interpreter commits. Parsing words (`PARSE`, `WORD`,
`CHAR`, `S"`) consume the same stream, which is what makes user-defined
parsing words possible. No combinator is ever recursive in itself; the
F7/DIV-0005 obstruction has no analogue because there are no recursive
productions.

**D20 — Effect checking is a lint over emitted code, at `;`.**
When `;` closes a definition, an abstract interpreter walks the definition's
instruction range, recovering basic blocks and computing net effect, minimum
entry depth, and (where paths permit) peak depth. Diagnoses carried from
F12/F17, now over instructions: declared-effect mismatch, branch-arm net
disagreement, loop-body net violations including `DO`'s two-cell entry cost
and `+LOOP`'s net-+1 body, `>R`/`R>` imbalance, bare `EXIT` inside `DO`
without a preceding `UNLOOP`. `EXECUTE`, deferred words, and parsing-word
effects are `unknown` lattice values; the checker is **advisory by
default**, promoted to a hard gate for a definition exactly when a declared
`( ... -- ... )` effect is present. Peak-depth results, where computed,
finally fill `compiled_program`'s `-1` capacity fields (DIV-0008's second
cut). The recovered CFG is shared with F33's sender lowering — one
analysis, two clients.

**D21 — One address unit is one cell, declared and conforming.**
The F16 data space is cell-granular; that is a legal Forth-2012 system
characteristic, so declare it: `CELLS` is identity, `CELL+` is `1+`,
`CHARS` identity, `CHAR+` `1+`; `C@`/`C!` alias `@`/`!`; strings store one
char per cell. Observable divergence from byte-addressed gforth is recorded
once as a system-characteristic DIV, not per word.

**D22 — Budgets are architecture.**
The session model concentrates constant-evaluation work. Fuel covers the
interpreter loop, not only the VM. The conformance suite is sharded across
translation units; CI records step counts so cost regressions are visible
before they are failures.

**D23 — gforth is the differential oracle; core is the scope.**
Core word set first, core-ext opportunistically, in gforth's direction. A
runtime harness feeds identical programs to the session image (runtime
side) and to gforth and diffs stacks and output. The Hayes `ttester` and
core suite run under constant evaluation as merge-gating batteries.
Divergences: standard citation, D21-style system characteristic, or DIV.

**D24 — The sender lowering is refunctionalization.**
Lowering partitions a word's instruction range into D20's basic blocks, one
sender per block, block completions wired as continuations, `THROW` on the
error channel so `CATCH` has a receiver's dynamic extent, `EXIT`/`LEAVE`
as one-shot upward completions, loops as repeat-until composition.
Unstructured return-stack manipulation does not refunctionalize and falls
back to the VM inside a single sender — a documented finding locating the
CPS/SSA boundary, not a failure, but F33's gate names constructs that must
lower natively so the fallback cannot silently become the implementation.
R1 F18's pre-authorizations carry: the constexpr trampoline may become a
Beman Task coroutine driver (`vendor/task`, no DIV needed, record in the
architecture doc); if `sync_wait` is not constexpr-capable, compile-time
coverage comes from the VM and the sender backend is the runtime path —
acceptable and documented.

---

# 4. Disposition inventory

## Carries unchanged (do not touch except as a step directs)

`foundation/` entire; `parser/` entire; `machine/cell.hpp`,
`machine/stacks` and `forth_state` (grows fields per steps),
`machine/data_space.hpp`, `machine/instruction.hpp`, `machine/vm.hpp`
(grows opcodes per steps), `apply_primitive` and the 46-primitive set;
`sender/vocab.hpp`; `vendor/execution`; the Makefile/smoke/CMake build
machinery; negative-compile test infrastructure (F15).

## Relocates

| What | From | To | Step |
|---|---|---|---|
| Word scanning, case fold, number/word discrimination, comment scanning | `reader/forth_chars.hpp` | `interpreter/` (or stays in `parser/` — worker's choice, once) | F24 |
| Emission + sentinel/back-patch utilities | `machine/codegen.hpp` | `machine/emit.hpp` (toolkit for compiling words) | F25 |
| Stack-effect lattice + composition rules | `elaborator/stack_effect.hpp` | `interpreter/effect_lint.hpp`, retargeted to `instr` ranges | F30 |
| Declared-effect comment capture | grammar | interpreter's `:` handling | F25 |

## Deleted at F26

`reader/syntax_tree.hpp`, `reader/read_program.hpp` (grammar productions
only), `elaborator/` entire (`elaborated_core`, `elaborate`,
`stack_effect` after its lattice is extracted), `machine/eval_direct.hpp`,
`machine/codegen.hpp` (after F25 extracts emission), and their tests.
Architecture-doc phases 2–5 sections describing them move to
`docs/history/architecture-grammar-era.org` per §1.

---

# 5. Language scope (revised word table)

| Group | Words | Step |
|---|---|---|
| Carried, already live | all 46 primitives incl. memory/output words; `: ; RECURSE EXIT` semantics | F24–F25 re-home |
| Interpreter state | `STATE BASE >IN SOURCE` (as words) | F24/F25 |
| Compilation semantics | `IMMEDIATE POSTPONE [ ] LITERAL COMPILE,` | F27 |
| Control flow | `IF ELSE THEN BEGIN UNTIL WHILE REPEAT DO LOOP +LOOP LEAVE UNLOOP I J` as immediate words | F27 |
| Tokens & defining | `' ['] EXECUTE , CREATE DOES> VARIABLE CONSTANT VALUE TO DEFER IS` | F28 |
| Parsing & strings | `( \ PARSE WORD CHAR [CHAR] S" ." ABORT" COUNT TYPE` | F29 |
| Exceptions | `CATCH THROW ABORT` | F31 |
| Address units | `CELLS CELL+ CHARS CHAR+ C@ C!` per D21 | F32 |
| Optional | `EVALUATE` | F32 |
| FFI | foreign word registration | F34 |

---

# 6. Steps

Verify always means `make compile`, `make test`, `make lint` green plus the
R1 §2 orchestrator checks. Capacities are template parameters.

## Step F23 — Revision governance and the pivot record

Install this plan as `docs/forth-plan-2.md`; mark R1 superseded per its
preamble (banner atop `docs/forth-plan.md`, not a rewrite — append-only
history). File the superseding divergence doc for D5/DIV-0001 recording the
pivot rationale (section 0 above). Update `checklist.md` with section 8
below (retire F18a–F22 lines as "retired unexecuted, see forth-plan-2");
replace the durable-invariants block per section 9 in `AGENTS.md`/the
architecture doc's stable tier. No code changes.
Merge criteria: verify passes; markdownlint/codespell clean on new docs;
the DIV cross-references DIV-0001, DIV-0005, DIV-0006 as superseded or
retired-with-subject.
Dependencies: none. **Blog: Part 12 — the pivot entry.**

## Step F24 — The interpreter, interpret state only

`src/smd/forth/interpreter/{input_source,interp}.hpp` (+ tests).
`forth_state` grows an input source (a view + `>IN` offset), `BASE`
(default 10), and `STATE` (0 = interpreting). The outer loop per D13,
interpret state only: scan a word via the relocated token layer (D19), look
it up (primitives execute via `apply_primitive`), else number per `BASE`
(signed; `-1` number, `1-` word — the F5 tests move here intact), else a
positioned unknown-word diagnosis. `\` and `( ... )` consume per the
existing comment scanners. No `:` yet.
Merge criteria: static_asserts — `1 2 + .` yields output `"3 "` and empty
stack; `HEX FF .`-style `BASE` handling if `BASE`-changing words are
included, else `BASE` plumbed and tested directly; unknown-word error
carries position; underflow through the interpreter is the same diagnosed
error `apply_primitive` already produces.
Dependencies: F23.

## Step F25 — The colon compiler and the session image

`interpreter/{compilebuf,session}.hpp`, `machine/emit.hpp` (+ tests).
Extract emission utilities from `codegen.hpp` into `machine/emit.hpp`
(relocation, not rewrite; `codegen.hpp` still builds until F26). `:` reads
a name, opens a header, captures a following `( ... -- ... )` comment span
as the declared effect (stored, unverified until F30), sets `STATE`=1;
in compile state, found words emit `call`/`prim`, numbers emit `push`;
`;` emits `ret`, closes the header, clears `STATE`. `EXIT` compiles `ret`;
`RECURSE` compiles a self-`call` (header recorded before body, F14's
discipline). Interpreting a defined word runs its code on the VM against
the live `forth_state` — one semantics (D14).
`session.hpp` defines the session image type (D15): code space, dictionary,
data-space high-water mark, output. `compiled_forth<Source>` is **not**
retargeted yet.
Merge criteria: static_asserts — `: SQUARED DUP * ;  4 SQUARED` leaves
`[16]` via the interpreter; `: QUAD SQUARED SQUARED ; 3 QUAD` leaves `[81]`;
compile-only misuse (`;` while interpreting, `EXIT` while interpreting)
diagnosed; a session image round-trips: build at constexpr, run its
top-level again at runtime from the same object.
Dependencies: F24.

## Step F26 — The cut

Retarget `compiled_forth<Source>` to run a session (D15) and return the
image with `run/stack/output` semantics preserved; delete the tree pipeline
per section 4's deletion list; move architecture-doc phases 2–5 to
`docs/history/architecture-grammar-era.org` per §1; rewrite the living
architecture doc's pipeline overview (source → text interpreter →
{code space, dictionary, data space} → VM | sender). File the F26 DIV
recording the effect-gate suspension window (§1).
Merge criteria: verify on **both** toolchains (matrix point); transclusion
check green on the living doc; `SQUARED`/`QUAD` and the F16 memory-word
merge criteria pass through the new `compiled_forth` (no control flow yet);
the F15 negative-compile test still fails compilation for an unknown word;
the survives-to-runtime proof re-established on a session image at
namespace scope.
Dependencies: F25.

## Step F27 — Immediacy and control flow

`IMMEDIATE` (flags the latest definition), `[` `]`, `LITERAL`, `COMPILE,`,
`POSTPONE` (correct dual behavior for immediate vs ordinary targets); the
control words per D17, implemented as C++-installed immediate words over
`machine/emit.hpp`: orig/dest on the data stack, `branch0` sentinels,
`LEAVE` sentinel-scan bounded by `DO`'s dest, loop opcodes and termination
rules carried verbatim from F17.
Merge criteria: the complete F13/F16/F17 merge-criteria program set —
`ABS`, `COUNTDOWN`, `SPIN` budget exhaustion, memory words, `SUMTO`,
`EVENS`, `FIND5`, `TENS`, `SUMEVEN`, `FIRST` — passes **verbatim** through
the interpreter path, compile time and runtime; a user-defined immediate
word built with `POSTPONE` (e.g. `: ENDIF POSTPONE THEN ; IMMEDIATE`)
works; mismatched `THEN` without `IF` diagnosed via the orig/dest
discipline.
Dependencies: F26.

## Step F28 — Execution tokens and defining words

`'`, `[']`, `EXECUTE` (the `execute` opcode goes live), `,` (comma),
`CREATE`, `DOES>`, `VARIABLE`, `CONSTANT` (pops the live stack at
interpret time — replacing R1 F11's constant-folding of the preceding
push; strictly standard, less machinery), `VALUE`/`TO`, `DEFER`/`IS`.
Header unification per D18, including the primitive-XT encoding decision.
Merge criteria: `: CONSTANT2 CREATE , DOES> @ ;  42 CONSTANT2 LIFE  LIFE`
leaves `[42]` (the R1 F20 classic, now required); `' SQUARED EXECUTE` ≡
`SQUARED` at compile time and runtime; `DEFER`red word before `IS`
diagnosed on execution; redefinition shadowing still observable
(dictionary tests carried).
Dependencies: F27. Parallel with F29.

## Step F29 — Parsing words and strings

`PARSE`, `WORD`, `CHAR`, `[CHAR]`, `S"`, `."`, `ABORT"` (message stored
per D21 cell-per-char; `COUNT`/`TYPE`), with `(` and `\` re-expressed as
ordinary immediate/parsing words over the same stream.
Merge criteria: `: GREET ." HELLO" CR ;  GREET` outputs correctly both
worlds; a user-defined parsing word (reads its own argument via `PARSE` or
`WORD`) defined in Forth works at constexpr — the demonstration D19
exists for; `S"` string round-trips through `COUNT TYPE`.
Dependencies: F27. Parallel with F28.

## Step F30 — The effect lint

`interpreter/effect_lint.hpp` per D20, run at `;`: CFG recovery over the
definition's instruction range, the F12/F17 diagnosis set retargeted,
declared-effect gating restored (ending the §1 suspension window),
`unknown` for `EXECUTE`/deferred/parsing-dependent effects, peak-depth
fill of `compiled_program` capacity fields where computable.
Merge criteria: every F12 positive and negative effect test reproduced
over the new path (the old test programs are the spec); the F17
`DO`-cost and `+LOOP`-net-+1 corrections hold; a declared-effect mismatch
is a compile error, an undeclared imbalance is a collected diagnostic;
one program's `required_stack_depth` is a real number asserted against a
hand computation.
Dependencies: F27 (F28 informs the `unknown` cases; schedule after F28).

## Step F31 — CATCH and THROW

Per D11 and R1 F18a's semantics, unchanged in substance: handler frames on
the return stack (`catch_mark`), `THROW n` restores data-stack depth
(truncate), unwinds to the handler, pushes n; `xt CATCH` pushes 0 on
normal completion; `ABORT` is `-1 THROW`; uncaught `THROW` diagnosed
carrying n; machine faults (underflow, bounds, budget) mapped to standard
THROW codes where sensible.
Merge criteria: R1 F18a's `BOOM`/`SAFE`/`TRY` programs verbatim, both
worlds; depth-restoration test; nested `CATCH`; interaction battery mixing
`>R`, `DO` frames, `LEAVE`, and `CATCH` on one return stack — this is
where Part 11's recorded unverified-teardown gap gets its stress test
(tagged-frame debug mode or exhaustive battery — worker decides, DIV if
tagged frames change layout).
Dependencies: F28. **Toolchain matrix point.**

## Step F32 — Conformance

Hayes `ttester` compiled by the session itself; the Forth-2012 core test
battery under constant evaluation, sharded per D22; the D21 address-unit
words; `EVALUATE` if the suite demands it (else deferred with DIV); the
gforth differential harness (runtime CI job: same programs through the
image and gforth, diff stacks/output); a written exclusion list with a
standard citation or DIV per exclusion.
Merge criteria: core suite green minus the exclusion list; harness runs in
CI and currently-known divergences are all listed; step-count telemetry
recorded per shard.
Dependencies: F31 (rolls in continuously from F27; gates here).

## Step F33 — The sender backend

R1 F18 carried to the new representation per D24, consuming F30's CFG:
basic blocks over the instruction stream; one composed sender per block
(never `sync_wait` inside evaluation); explicit constexpr trampoline for
control transfer, with the Beman Task coroutine driver pre-authorized;
`THROW` → error channel with `(n, state)`, `CATCH` as the error-to-value
adapter with Forth stack restoration; `EXIT`/`ret`/`LEAVE` as value
completions; stop channel demonstrated for fuel exhaustion; VM-in-a-sender
fallback for non-refunctionalizable return-stack use, with the boundary
documented.
Merge criteria: the F27/F28/F31 program batteries pass through the sender
backend with identical final states; CATCH/THROW demonstrably routes
through `set_error`; native (non-fallback) lowering shown for `IF`, both
`BEGIN` forms, `DO` with `LEAVE`, `EXIT`, `CATCH/THROW`; one program on
each side of the fallback boundary, named in the architecture doc; the
constexpr-capability split (if any) recorded per D24.
Dependencies: F30, F31. **Toolchain matrix point.**

## Step F34 — Foreign function interface

R1 F19 carried: `foreign_word` as `result<void>(*)(forth_state&)` with
direct stack/data-space/output access, registered pre-session
(`default_dictionary().with_foreign("GCD", gcd_word)` shape, now a D18
header with an XT like any word); optional declared effect (undeclared →
`unknown`); the reverse embedding example (push args from C++, run,
read the stack); `is_constant_evaluated`-guarded runtime I/O example;
Godbolt FFI example.
Merge criteria: static_assert computing through a constexpr foreign word
via VM and sender backends; runtime printing example; foreign word callable
via `'`/`EXECUTE` like any other.
Dependencies: F28; sender coverage after F33.

## Step F35 — Bootstrap prelude (stretch)

A Forth prelude source, compiled by every session before user source,
defining derived words in Forth — and, as far as `POSTPONE` reaches,
control-flow words themselves, shrinking the C++-installed immediate set.
The demonstration that metaprogramming is just programming.
Merge criteria: at least the derived arithmetic/stack words and one control
word (`ENDIF`-class at minimum, a full `IF` replacement if it holds)
defined in the prelude, with the F27 battery still green; session
step-count delta recorded.
Dependencies: F29. Optional; deferral is a DIV.

## Step F36 — Consolidation

`docs/forth-limitations.md` rolled up from the rescoped D12, D21, and all
DIVs; the living architecture doc completed and consistent (the history
snapshot cross-referenced, not duplicated); `compile-time-forth.org`
(presentation) rewritten to transclude the real system; README updated;
error-quality pass: table-driven positioned-diagnostic tests over
representative bad programs, negative-compile tests for syntax error,
declared-effect mismatch, capacity overflow (extending the F15/F26
infrastructure).
Merge criteria: verify; every DIV referenced from the limitations doc;
each negative test fails for its stated reason; presentation renders where
emacs is available.
Dependencies: everything else.

## Parallelism summary

- Spine: F23 → F24 → F25 → F26 → F27 → {F28 ∥ F29} → F30 → F31 → F32 →
  F33 → F36.
- F30 after F28 (informs `unknown` cases); F31 after F28; F33 after
  F30+F31; F34 after F28 (sender coverage post-F33); F35 after F29,
  anytime before F36.
- F28 and F29 in separate worktrees; both touch the dictionary header —
  F28 owns the header change, F29 rebases on it if dispatched second, or
  serialize them if the orchestrator prefers.
- Toolchain matrix at F26, F31, F33.

---

# 7. Checklist section (F23 appends to `checklist.md`)

```markdown
## True Forth revision (docs/forth-plan-2.md)

- [ ] Step F23: revision governance and pivot record
- [ ] Step F24: interpreter, interpret state only
- [ ] Step F25: colon compiler and session image
- [ ] Step F26: the cut
- [ ] Step F27: immediacy and control flow
- [ ] Step F28: execution tokens and defining words
- [ ] Step F29: parsing words and strings
- [ ] Step F30: effect lint
- [ ] Step F31: CATCH and THROW
- [ ] Step F32: conformance
- [ ] Step F33: sender backend
- [ ] Step F34: foreign function interface
- [ ] Step F35: bootstrap prelude (stretch)
- [ ] Step F36: consolidation

## Blog series continuation (one post per step; F23 = Part 12)

- [ ] Blog: F23 the pivot (Part 12)
- [ ] Blog: F24 (Part 13) … F36 (Part 25), one per step as merged
```

---

# 8. Durable invariants (replacement block)

Replaces the R1 §11 block wholesale; same stable-tier rules.

```txt
This is smd/forth, a compile-time and runtime true-Forth system in C++26; plan in docs/forth-plan-2.md (docs/forth-plan.md is the superseded R1 plan, kept as history).
The Makefile is the single build interface, parameterized by TOOLCHAIN and CONFIG; new flag-sets are new CONFIGs; all compiled files are always compiled.
The smoke driver is .claude/skills/run-compile-time-forth/smoke.sh [TOOLCHAIN] [CONFIG].
foundation/ and parser/ are adapted by copy from compile-time-scheme; provenance lines in file prologs; no build coupling to that repo.
The architecture is the Forth-2012 text interpreter (D13): STATE/SOURCE/>IN/BASE live in forth_state; words resolve when the interpreter meets them; immediate words execute during compilation; there is no syntax tree and no elaboration phase.
The artifact is a session image (D15): code space + dictionary + data-space seed + output as a trivially copyable literal built in one constant-expression evaluation and runnable again at runtime; compiled_forth<Source> is the one-shot API and a malformed program is a hard compile error.
A defined word has one semantics: its compiled code on the VM (D14); oracles are compile-time/runtime agreement of the same value, gforth differential testing, and the Forth-2012 core suite under consteval.
Every compiled structure is flat, trivially destructible, capacity-parameterized; heap-backed fix/Box types are barred (D3 as narrowed).
Words fold to uppercase at scan time; one address unit is one cell, declared as a system characteristic (D21).
The machine is two fixed-capacity stacks plus an arena data space and an output buffer; all misuse is a diagnosed error via foundation::result, never UB; every evaluation carries fuel (D22).
All nonlocal control (EXIT, LEAVE, CATCH/THROW) is one-shot and dynamic-extent; threaded code is defunctionalized CPS with the return stack as the continuation; the sender backend refunctionalizes it, mapping THROW to the error channel (D24).
Effect checking is an advisory lint over emitted code at ';', gating only on declared effects (D20).
Beman Execution is vendored at vendor/execution; Beman Task at vendor/task only if F33 needed it.
```

---

# 9. Blog mapping

F23 → Part 12, one post per step thereafter (F36 → Part 25 if F35 runs;
renumber down if a step is retired by DIV). The F23 post is the pivot
entry: the Part 0 refusal, what the stream-of-patches objection got right
(the tree fought the outer interpreter) and what it missed (the patch
stream was already the project's own target representation), and the
carry/delete inventory as the author's own accounting. The blog contract
(`docs/blog/AGENTS.md`) is unchanged and is built for exactly this entry.
