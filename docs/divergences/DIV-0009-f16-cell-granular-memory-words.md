# DIV-0009: memory words address data space in cells, not Forth-2012 address units

- **Status:** accepted-permanent (D21's own naming of this as a system characteristic, revisited
  at step F32 — see the addendum at the end of this file)
- **Date:** 2026-07-24
- **Step:** F16 (memory words end-to-end)
- **Authority diverged from:** Forth-2012

## What diverged

Forth-2012 (and every ANS/2012-conforming implementation) addresses its data
space in *address units* — conventionally bytes — with `ALLOT` reserving `n`
address units, and `CELLS`/`CELL+`/`CHARS`/`CHAR+` provided so a program can
convert between a cell count and the address-unit count `ALLOT`/`@`/`!`
actually expect (`10 CELLS ALLOT` reserves ten cells' worth of address
units on a byte-addressed system, not ten address units).

This project's `machine::data_space<MaxData>` (F10,
`src/smd/forth/machine/data_space.hpp`) is a cell-granular arena from the
start: `allot(int count)` reserves `count` whole cells, `addr` is an index
into that cell array (not a byte offset), and `fetch`/`store` read/write one
cell per `addr`. F16 wires `ALLOT`/`@`/`!`/`+!` as ordinary primitives
directly against this arena with no unit conversion anywhere, and does not
add `CELLS`/`CELL+`/`CHARS`/`CHAR+` (none exist in `default_dictionary`).
So `CREATE BUF 4 ALLOT` reserves four *cells* past `BUF`, and `BUF 3 + @`
reaches the fourth one directly — the address arithmetic a Forth-2012
program would write as `BUF 3 CELLS + @` on a byte-addressed system here
needs no `CELLS` at all, because one address unit already is one cell.

## Why

The cell-granular design was already made by F10, before F16 existed to
name it: `data_space`'s own `allot`/`fetch`/`store` have always operated in
cells, and `docs/forth-plan.md`'s Step F16 section and this step's own
`step-brief.md` describe the merge criterion (`CREATE BUF 4 ALLOT`, `BUF`
"usable as base address") entirely in cell terms, never mentioning
`CELLS`/`CELL+`/byte addressing, or asking for a conversion layer.
F16 is the first step that actually exposes `ALLOT`/`@`/`!`/`+!` as
runnable words, though, so this is the first point at which the
cell-granular choice becomes an externally observable divergence from
Forth-2012 rather than an internal implementation detail of an
as-yet-unreachable data structure — hence recording it now rather than
retroactively against F10.
Introducing byte-granular addressing (and the `CELLS`/`CELL+`/`CHARS`/
`CHAR+` words a Forth-2012 program needs to bridge it) was out of scope for
this step: nothing in the plan's F16 section or merge criterion asks for
it, and doing so would mean revisiting F10's own `data_space` type, not
just wiring primitives against it.

## Consequences

- Every `addr` value this project's compiler ever produces or a Forth
  program ever computes (`VARIABLE`/`CREATE`'s own address, or one built
  with `+`/`-` against it) is a cell index, not a byte offset. Forth-2012
  source that uses `CELLS`/`CELL+`/`CHARS`/`CHAR+` will not compile here
  (`elaborate_word_ref` diagnoses them as "unknown word"); source written
  against this project's own cell-granular convention (as the plan's own
  merge criterion is) works without them.
- F19 (foreign function interface) and F20 (optional `CREATE`/`DOES>`), if
  either ever needs to interoperate with externally-addressed (byte-level)
  memory, must account for this gap explicitly rather than assuming
  `addr` already behaves like a Forth-2012 byte pointer.
- F21 (error-quality and negative-compile pass) should not add a negative
  test expecting `CELLS`/`CELL+` to be recognized words; they are
  correctly "unknown word" under this project's own convention, not a bug.

## Revisit condition

Would be closed only by a future step deliberately switching `data_space`
to byte-granular addressing and adding `CELLS`/`CELL+`/`CHARS`/`CHAR+` —
not currently planned anywhere in `docs/forth-plan.md`. Until then this is
accepted as a permanent, documented simplification of the Forth-2012 memory
model.

## Addendum (step F32, docs/forth-plan-2.md, D21)

D21 names this file's own cell-granular design directly as a legal Forth-2012 system
characteristic and assigns its own explicit revisit: adding `CELLS`/`CELL+`/`CHARS`/`CHAR+` (plus
`C@`/`C!`) as words that are consistent *with* the cell-granular convention, not a switch away from
it. Step F32 does exactly that: `CELL+`/`CHAR+` alias the existing `one_plus` enumerator, `C@`/`C!`
alias `fetch`/`store`, and `CELLS`/`CHARS` share one new `identity` enumerator (`machine/
forth_state.hpp`) — `1 CELLS` is `1`, not a byte count, so a Forth-2012 program written against
byte addressing still diverges here, by design, exactly as this file's own "What diverged" section
already described. The underlying cell-granular characteristic itself remains permanent and
unrevisited; only the "these words don't exist yet" half of the original gap is closed. See
`docs/compiler_architecture.org`'s own Phase 14 section and `docs/conformance-exclusions.md` for
where this step's own tests exercise the new words.
