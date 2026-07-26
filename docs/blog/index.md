- [Overview](#org67643dd)
- [Blog Posts](#org9253b7f)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#org9e021ae)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#orgb607b6b)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#org211b844)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#org65b30a5)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#org656c2c1)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#org6a5a04e)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#org78c9f79)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#org5501383)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#org79f1672)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#orgf933065)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#org29e5397)
  - [[Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)](#org69d623d)
  - [[Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)](#org2a0e9b3)
  - [[Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)](#orgd5095cc)
  - [[Part 14 — Correct by Accident](post-14-correct-by-accident.md)](#orge37b80e)
  - [[Part 15 — The Cut](post-15-the-cut.md)](#org366a365)
  - [[Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)](#orgef327dc)
- [Table of Contents](#org7fef9a1)



<a id="org67643dd"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &#x2014; `EXIT`, `LEAVE`, `CATCH/THROW` &#x2014; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="org9253b7f"></a>

# Blog Posts


<a id="org9e021ae"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="orgb607b6b"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &#x2014; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="org211b844"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &#x2014; and the one CPO that stopped being callable as a result.


<a id="org65b30a5"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="org656c2c1"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="org6a5a04e"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="org78c9f79"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="org5501383"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &#x2014; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="org79f1672"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="orgf933065"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="org29e5397"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &#x2014; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="org69d623d"></a>

## [Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)

`DO LOOP +LOOP I J LEAVE UNLOOP` land in both backends, loop parameters and all &#x2014; and the static checker turns out to have been wrong about `DO`'s own cost since two entries back, in a way only a nested loop could ever expose.


<a id="org2a0e9b3"></a>

## [Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)

Part 0's refusal of Forth's outer interpreter doesn't survive a reread: the back-patching it worried about was already the project's own target representation. No code changed this entry &#x2014; only the argument for what has to change next.


<a id="orgd5095cc"></a>

## [Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)

The Forth-2012 outer text interpreter, in interpret state only &#x2014; and a design choice that pays off nowhere in this entry: `>IN` is a bare offset sitting in the open, not a cursor buried inside the scanner.


<a id="orge37b80e"></a>

## [Part 14 — Correct by Accident](post-14-correct-by-accident.md)

`:` and `;` land, and with them a session image that outlives the constant evaluation that built it &#x2014; and a first draft that passed every test for a reason that had nothing to do with being right.


<a id="org366a365"></a>

## [Part 15 — The Cut](post-15-the-cut.md)

Deleting the R1 pipeline outright &#x2014; reader, elaborator, direct evaluator, batch codegen, close to 6300 lines &#x2014; and retargeting the public API onto a session built once. What it costs: Part 7's oracle, the two evaluators that had to agree, and nothing yet stands fully in its place.


<a id="orgef327dc"></a>

## [Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)

`IF ELSE THEN`, `BEGIN UNTIL`, and the whole `DO LOOP` family land as immediate words on Forth-2012's own orig/dest discipline &#x2014; and a depth check that looked obviously right gets caught wrong by one of my own tests before it ever reached anyone else's code.


<a id="org7fef9a1"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#org67643dd)
2.  [Blog Posts](#org9253b7f)
3.  [Table of Contents](#org7fef9a1)
