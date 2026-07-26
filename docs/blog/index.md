- [Overview](#org3f36149)
- [Blog Posts](#org14ad780)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#orgec0e50a)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#org61f3e66)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#orgd8d0618)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#orge5cae46)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#orgce9a377)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#org3e69a90)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#org20e4157)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#orgd1e6e0a)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#org31a4282)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#org6d3de2a)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#orgdc7960f)
  - [[Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)](#org49e4efa)
  - [[Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)](#org9633e0b)
  - [[Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)](#org93f352b)
  - [[Part 14 — Correct by Accident](post-14-correct-by-accident.md)](#orgcba7c11)
  - [[Part 15 — The Cut](post-15-the-cut.md)](#org5763c89)
  - [[Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)](#org710fa9b)
  - [[Part 17 — Something to Point At](post-17-something-to-point-at.md)](#orgf370001)
  - [[Part 18 — What >IN Was For](post-18-what-in-was-for.md)](#org231c304)
  - [[Part 19 — The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)](#org4c9c607)
  - [[Part 20 — Just Edges](post-20-just-edges.md)](#org4666d6b)
- [Table of Contents](#orgac3393b)



<a id="org3f36149"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &#x2014; `EXIT`, `LEAVE`, `CATCH/THROW` &#x2014; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="org14ad780"></a>

# Blog Posts


<a id="orgec0e50a"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="org61f3e66"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &#x2014; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="orgd8d0618"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &#x2014; and the one CPO that stopped being callable as a result.


<a id="orge5cae46"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="orgce9a377"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="org3e69a90"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="org20e4157"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="orgd1e6e0a"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &#x2014; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="org31a4282"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="org6d3de2a"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="orgdc7960f"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &#x2014; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="org49e4efa"></a>

## [Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)

`DO LOOP +LOOP I J LEAVE UNLOOP` land in both backends, loop parameters and all &#x2014; and the static checker turns out to have been wrong about `DO`'s own cost since two entries back, in a way only a nested loop could ever expose.


<a id="org9633e0b"></a>

## [Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)

Part 0's refusal of Forth's outer interpreter doesn't survive a reread: the back-patching it worried about was already the project's own target representation. No code changed this entry &#x2014; only the argument for what has to change next.


<a id="org93f352b"></a>

## [Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)

The Forth-2012 outer text interpreter, in interpret state only &#x2014; and a design choice that pays off nowhere in this entry: `>IN` is a bare offset sitting in the open, not a cursor buried inside the scanner.


<a id="orgcba7c11"></a>

## [Part 14 — Correct by Accident](post-14-correct-by-accident.md)

`:` and `;` land, and with them a session image that outlives the constant evaluation that built it &#x2014; and a first draft that passed every test for a reason that had nothing to do with being right.


<a id="org5763c89"></a>

## [Part 15 — The Cut](post-15-the-cut.md)

Deleting the R1 pipeline outright &#x2014; reader, elaborator, direct evaluator, batch codegen, close to 6300 lines &#x2014; and retargeting the public API onto a session built once. What it costs: Part 7's oracle, the two evaluators that had to agree, and nothing yet stands fully in its place.


<a id="org710fa9b"></a>

## [Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)

`IF ELSE THEN`, `BEGIN UNTIL`, and the whole `DO LOOP` family land as immediate words on Forth-2012's own orig/dest discipline &#x2014; and a depth check that looked obviously right gets caught wrong by one of my own tests before it ever reached anyone else's code.


<a id="orgf370001"></a>

## [Part 17 — Something to Point At](post-17-something-to-point-at.md)

`'`, `[']`, `EXECUTE`, `CREATE`, `DOES>`, `VALUE~/~TO`, and `DEFER~/~IS` all get a real execution token &#x2014; and paying down a debt from Part 13 turns out to be what makes any of it possible.


<a id="org231c304"></a>

## [Part 18 — What >IN Was For](post-18-what-in-was-for.md)

`PARSE`, `WORD`, `CHAR`, `S"`, `."`, and `ABORT"` land, and a definition three words long &#x2014; `: ECHO-WORD 32 WORD COUNT TYPE ;` &#x2014; reaches past its own call for an argument that was never inside it. Two entries' worth of debt about `>IN` finally cash out on the same example.


<a id="org4c9c607"></a>

## [Part 19 — The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)

`CATCH`, `THROW`, and `ABORT` land, and `ABORT"` stops being a hard stop and becomes a real `THROW -2` &#x2014; but the entry is about what `THROW` had to do to unwind a return stack holding an arbitrary mix of call frames, loop frames, and stray `>R` values, and how that answers, without fully closing, the teardown gap Part 11 admitted to eight entries back.


<a id="org4666d6b"></a>

## [Part 20 — Just Edges](post-20-just-edges.md)

A real stack-effect checker returns, as an abstract interpreter over the instructions a colon definition actually compiled instead of a tree that no longer exists &#x2014; and Part 11's loop-teardown gap closes for free, because a loop's back edge turns out to need the same agreement any other join does. The first version of it was also wrong, caught not by a test I wrote but by a conformance suite landing on the same tree the same week.


<a id="orgac3393b"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#org3f36149)
2.  [Blog Posts](#org14ad780)
3.  [Table of Contents](#orgac3393b)
