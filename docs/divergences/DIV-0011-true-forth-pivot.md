# DIV-0011: the true-Forth pivot — D5 overturned, the text interpreter is the architecture

- **Status:** accepted-permanent
- **Date:** 2026-07-25
- **Step:** F23 (revision governance and the pivot record), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan.md (D5, D6, D9, D12; and D1, D3, D10 as amended)

## What diverged

D5 of `docs/forth-plan.md` chose a structural grammar over a stateful outer text interpreter, and DIV-0001 recorded that choice as the project's foundational, permanent divergence from Forth-2012.
That choice is overturned.
`docs/forth-plan-2.md` makes the Forth-2012 §3.4 text interpreter the top of the system: parse a word, find it, execute it when interpreting or when it is immediate, otherwise compile it; failing that, try it as a number per `BASE` and push or compile a literal per `STATE`; failing that, a diagnosed error with source position.
`STATE`, `SOURCE`, `>IN`, and `BASE` become machine state in `forth_state`.
`IMMEDIATE`, `POSTPONE`, `[`, `]`, `LITERAL`, and user-defined parsing words come into scope, and the control-flow words become immediate words on the standard orig/dest discipline rather than grammar productions.
There is no reader phase, no syntax tree, and no elaboration phase after step F26.

The artifact widens with it.
`compiled_forth<Source>` keeps its name, its NTTP keying, and its malformed-program-is-a-hard-compile-error contract, but its result is no longer a compiled tree: it is a session image — code space, final dictionary, data-space high-water mark, and captured output — produced by running a whole Forth session inside one constant-expression evaluation, as a trivially copyable literal that executes again at ordinary runtime.

## Why

The R1 architecture produced a working substrate under a front end that is not Forth.
The machine, the dictionary, the instruction set, the VM, the fuel discipline, and the one-shot API are all sound and are all retained; what sat on top of them was a fixed grammar with no outer interpreter, and a fixed grammar is precisely the thing a Forth programmer does not have.
Forth metaprogramming is just programming, and a system that cannot exhibit that is demonstrating something else.

DIV-0001's stated objection was that "an outer loop that lays down control structure imperatively does not produce a tree, it produces a stream of patches."
That identified the real tension and drew the wrong conclusion from it.
The patch stream was never a problem to be avoided — it was already the project's own target representation.
Codegen emits `branch0` with a placeholder and back-patches it; the `LEAVE` implementation emits `-1` sentinels and scans them per loop.
That is exactly what `IF`, `THEN`, and `LEAVE` do as immediate words in a classical Forth.
The artifact in conflict with the outer interpreter was the tree, and only the tree.
The instruction array, the dictionary, and the machine never were.

DIV-0001's second argument — that an outer interpreter is orthogonal to the sender/receiver thesis — is also reversed.
The thesis is unchanged and the pivot makes it more direct, not less: threaded code is CPS with the continuation defunctionalized as the return stack, CPS being a complete intermediate language equivalent to SSA, and the sender lowering is refunctionalization of exactly that.
Relocating the emission machinery from a tree walker into the compiling words is the heart of the pivot, and it moves the project toward the bet rather than away from it.

The full deliberation that produced this revision is archived at `docs/history/forth-replan.org`.
The revision itself, with its decision records D13–D24, its disposition inventory, and its step sequence, is `docs/forth-plan-2.md`.

## Consequences

- **DIV-0001 is superseded by this document.** Its "no `IMMEDIATE`, no `POSTPONE`, no `[` `]`, no `STATE`, no user-defined parsing words, anywhere in the project" consequence list is void from step F23 onward. The structural grammar was a valid substrate-building strategy and is not the destination.
- **DIV-0005 is retired with its subject.** Its hand-written mutually recursive productions (`parse_body_until`, `parse_body_item_from_token`, `parse_if`, `parse_begin`, `parse_do`) are deleted at step F26 along with the grammar they implement. Its underlying finding — a `parser<F>` cannot be recursive in itself, and there is no constexpr type-erased callable to escape with — stands as a fact about the combinator library and is simply no longer load-bearing: a true Forth has no recursive grammar productions, so decision D19 puts the combinators below the word, owning scanning and classification over the machine's input source, which is the layer at which they always held.
- **DIV-0006 is retired with its subject, and its concern transfers.** `stack_effect.hpp`'s `analyze_body` dies with the elaborator at step F26. The gap it recorded — an early `EXIT` path whose depth disagrees with the fall-through path is not diagnosed — is inherited by step F30's effect lint (D20), which recovers basic blocks from the emitted instruction range and therefore sees both paths structurally rather than by folding an item list. F30 either closes it or re-files it against the new checker; the retirement of DIV-0006 must not be read as the gap having been fixed.
- **Steps F18a–F22 of `docs/forth-plan.md` are retired unexecuted** and replaced by F23–F36 of `docs/forth-plan-2.md`. Their content is not lost: F18a's `CATCH`/`THROW` semantics become F31, F18's sender backend becomes F33, F19's FFI becomes F34, F20's `CREATE`/`DOES>` becomes required rather than optional in F28, and F21/F22 fold into F36.
- **A bounded regression is scheduled, not accidental.** Between F26 and F30 there is no stack-effect checking at all: the R1 checker dies with the elaborator and its replacement lands at F30. Declared-effect comments are still captured and stored in that window, simply not verified. The step F26 divergence doc records the window; this document records that it was planned.
- **The durable invariants change.** The stable-tier block in `AGENTS.md` is replaced wholesale by `docs/forth-plan-2.md` §8. In particular, "source is parsed structurally by applicative combinators (DIV-0001); there is no interpretive outer loop and no IMMEDIATE" is deleted and replaced by the D13/D15 statements, and D3's "every tree is a flat arena" narrows to "every compiled structure is flat" — after F26 the flat structures are code space, the dictionary, and data space, and structural recursion over arena trees is no longer the compute model.
- **The scope target moves from a demonstration language to core conformance.** D23 makes gforth the differential oracle and the Forth-2012 core word set the scope, with the Hayes `ttester` suite running under constant evaluation as a merge gate (step F32). Divergences from here on are standard citations, declared system characteristics (D21's one-address-unit-is-one-cell), or DIV docs — never silence.
- **The blog series continues rather than restarts.** F23 is Part 12, one post per step thereafter. The pivot is itself the entry: the Part 0 refusal, what the stream-of-patches objection got right and what it missed, and the carry/delete inventory as the author's own accounting.

## Revisit condition

None.
This supersedes DIV-0001 as the project's foundational statement about its own front end.
Overturning D13 or D15 in turn would require a further divergence doc and orchestrator sign-off, as `docs/forth-plan-2.md` §3 requires of every decision record it installs.
