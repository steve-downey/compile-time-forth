- [Overview](#org4eace6f)
- [Blog Posts](#org04694a2)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#orgacb196d)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#org266888f)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#orgf31c0cd)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#org87bbdad)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#org2e815fa)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#orgbfad51c)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#orgf7b2ae8)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#org6bc2b67)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#orga4d302b)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#orga4e7e09)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#orgd2cca9e)
  - [[Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)](#org5d4b0b0)
  - [[Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)](#orgfd9bbcd)
  - [[Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)](#org961af41)
  - [[Part 14 — Correct by Accident](post-14-correct-by-accident.md)](#orgc128150)
  - [[Part 15 — The Cut](post-15-the-cut.md)](#org46fdb74)
  - [[Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)](#orgf5171c1)
  - [[Part 17 — Something to Point At](post-17-something-to-point-at.md)](#org45b9237)
- [Table of Contents](#orgdf8055f)



<a id="org4eace6f"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &#x2014; `EXIT`, `LEAVE`, `CATCH/THROW` &#x2014; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="org04694a2"></a>

# Blog Posts


<a id="orgacb196d"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="org266888f"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &#x2014; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="orgf31c0cd"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &#x2014; and the one CPO that stopped being callable as a result.


<a id="org87bbdad"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="org2e815fa"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="orgbfad51c"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="orgf7b2ae8"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="org6bc2b67"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &#x2014; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="orga4d302b"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="orga4e7e09"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="orgd2cca9e"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &#x2014; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="org5d4b0b0"></a>

## [Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)

`DO LOOP +LOOP I J LEAVE UNLOOP` land in both backends, loop parameters and all &#x2014; and the static checker turns out to have been wrong about `DO`'s own cost since two entries back, in a way only a nested loop could ever expose.


<a id="orgfd9bbcd"></a>

## [Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)

Part 0's refusal of Forth's outer interpreter doesn't survive a reread: the back-patching it worried about was already the project's own target representation. No code changed this entry &#x2014; only the argument for what has to change next.


<a id="org961af41"></a>

## [Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)

The Forth-2012 outer text interpreter, in interpret state only &#x2014; and a design choice that pays off nowhere in this entry: `>IN` is a bare offset sitting in the open, not a cursor buried inside the scanner.


<a id="orgc128150"></a>

## [Part 14 — Correct by Accident](post-14-correct-by-accident.md)

`:` and `;` land, and with them a session image that outlives the constant evaluation that built it &#x2014; and a first draft that passed every test for a reason that had nothing to do with being right.


<a id="org46fdb74"></a>

## [Part 15 — The Cut](post-15-the-cut.md)

Deleting the R1 pipeline outright &#x2014; reader, elaborator, direct evaluator, batch codegen, close to 6300 lines &#x2014; and retargeting the public API onto a session built once. What it costs: Part 7's oracle, the two evaluators that had to agree, and nothing yet stands fully in its place.


<a id="orgf5171c1"></a>

## [Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)

`IF ELSE THEN`, `BEGIN UNTIL`, and the whole `DO LOOP` family land as immediate words on Forth-2012's own orig/dest discipline &#x2014; and a depth check that looked obviously right gets caught wrong by one of my own tests before it ever reached anyone else's code.


<a id="org45b9237"></a>

## [Part 17 — Something to Point At](post-17-something-to-point-at.md)

`'`, `[']`, `EXECUTE`, `CREATE`, `DOES>`, `VALUE~/~TO`, and `DEFER~/~IS` all get a real execution token &#x2014; and paying down a debt from Part 13 turns out to be what makes any of it possible.


<a id="orgdf8055f"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#org4eace6f)
2.  [Blog Posts](#org04694a2)
3.  [Table of Contents](#orgdf8055f)
