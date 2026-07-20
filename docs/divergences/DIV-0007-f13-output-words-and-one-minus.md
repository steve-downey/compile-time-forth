# DIV-0007: F13 adds output-word and `1-` primitives the plan's word table omits

- **Status:** accepted-permanent
- **Date:** 2026-07-20
- **Step:** F13 (direct evaluator)
- **Authority diverged from:** docs/forth-plan.md

## What diverged

`docs/forth-plan.md`'s Step F13 merge criteria spell three words that had no
runtime behavior and, in two cases, no dictionary entry at all before this
step:

- `.` (used by the `COUNTDOWN` merge criterion: `BEGIN DUP . 1- DUP 0= UNTIL
  DROP`). Section 6's word table does separately assign `. .S EMIT CR` to
  "F13 (buffered per D10)", so wiring these four is not itself a divergence
  from the plan's own word-scoping table -- it is F13 literally doing what
  that table already asked for. What *is* new is that no `machine::primitive`
  enumerator, `apply_primitive` case, or `default_dictionary` entry existed
  for any of the four before this step; F8 (machine substrate) only ever
  implemented the arithmetic/comparison/stack-manipulation primitives, never
  the output ones, even though D10's prose ("Output words (`.`, `.S`, `EMIT`,
  `CR`) append to a fixed-capacity character buffer in `forth_state`") reads
  as though the machinery should already exist by the time a Forth program
  can use it.
- `1-` (also used by the same `COUNTDOWN` merge criterion). Unlike the output
  words, `1-` is **not** listed anywhere in section 6's word-scoping table --
  neither the arithmetic/logic row (`+ - * / MOD NEGATE ABS MIN MAX AND OR
  XOR INVERT LSHIFT RSHIFT`) nor any other row assigns it to a step. It is
  simply absent from the plan's own inventory of words, despite being used
  verbatim in the F13 merge-criterion text itself, and again later in F17's
  own worked example (`: SUMTO 0 SWAP 1+ 0 DO I + LOOP ;`, which uses the
  sibling word `1+`, likewise absent from the table).

## Why

The F13 merge criteria cannot be satisfied by static_assert against the
`docs/forth-plan.md` text as written: `3 COUNTDOWN` cannot produce output
`"3 2 1 "` without a working `.`, and cannot decrement without a working
`1-`, but neither had an implementation to call through
`machine::apply_primitive`, and `1-` additionally had no primitive
enumerator or dictionary entry at all (elaboration would diagnose it
`"unknown word"` before evaluation ever started). Rather than rewrite the
merge criterion's source text to avoid these two words (which would silently
drift from the plan's own literal example), this step adds:

- Four new `machine::primitive` enumerators (`dot`, `dot_s`, `emit`, `cr`)
  wired through `apply_primitive` (`machine/forth_state.hpp`) and
  `default_dictionary` (`machine/dictionary.hpp`), each with a
  `elaborator::primitive_data_effect` table entry
  (`elaborator/stack_effect.hpp`) for F12's stack-effect checker: `.` and
  `EMIT` are `(1, 0)`, `.S` and `CR` are the identity effect `(0, 0)`.
- One new primitive enumerator, `one_minus` (`1-`, `( a -- a-1 )`), wired the
  same way, with a `known(1, 1)` stack effect -- the standard Forth-2012
  `1- ( n1|u1 -- n2|u2 )` core word.

The dictionary grew from 37 to 42 entries as a result (37 F8 primitives + 4
F13 output words + `1-`); every test and doc comment that counted dictionary
entries or primitives was updated alongside (`dictionary.test.cpp`,
`elaborate.test.cpp`, `elaborated_core.hpp`, `stack_effect.hpp`).

## Consequences

- F13's `COUNTDOWN` and budget-exhaustion merge criteria now match the
  plan's literal source text exactly, rather than a paraphrased substitute
  (e.g. spelling `1-` as `1 -`), which keeps the merge-criteria text in
  `eval_direct.test.cpp` directly comparable to `docs/forth-plan.md`'s own
  prose.
- `1+` (used only in F17's own later example, not by anything F13's merge
  criteria exercise) is deliberately **not** added by this step, to keep
  F13's own scope to what its own merge criteria demonstrably require.
  Whichever step first needs `1+` (most likely F17) should add it the same
  way this step added `1-`, or fold it into a dedicated cleanup of section
  6's word table.
- Every later step that counts "the primitives" or "the dictionary size" by
  a literal number (F14's codegen, F16's memory wiring, etc.) should use 42
  as the current baseline until a further step changes it again, not the
  37/39 figures recorded in earlier handoff.md sections (those remain
  historically accurate for the step they were written in, per handoff.md's
  own append-only, do-not-rewrite-history policy).
- `.S`'s output format (bottom-to-top, one `emit_cell`-rendered decimal cell
  per entry of the data stack, nondestructive) is this step's own choice --
  Forth-2012 leaves `.S`'s exact rendering implementation-defined -- and is
  not exercised by any of F13's own merge criteria; it is covered only by
  `forth_state.test.cpp`'s own direct `apply_primitive` tests.

## Revisit condition

Revisit `docs/forth-plan.md` section 6 itself (not this divergence) to add
`1-`/`1+` to the arithmetic/logic word row explicitly, closing the gap this
DIV records; until then, this divergence stays accepted-permanent as the
record of why `1-` exists as primitive 42 starting at step F13 rather than
whatever step section 6 would otherwise have assigned it to.
