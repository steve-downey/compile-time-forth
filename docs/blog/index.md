- [Overview](#org832bf0c)
- [Blog Posts](#org11878a6)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#org640c600)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#org58bfe61)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#orgb12c637)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#org5d39f45)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#org2860f69)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#org9c9dd8a)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#org30db0df)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#orgab14242)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#orgc73e947)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#org71aea42)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#org0e39e94)
- [Table of Contents](#orgdc05f9b)



<a id="org832bf0c"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &mdash; `EXIT`, `LEAVE`, `CATCH/THROW` &mdash; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="org11878a6"></a>

# Blog Posts


<a id="org640c600"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="org58bfe61"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &mdash; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="orgb12c637"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &mdash; and the one CPO that stopped being callable as a result.


<a id="org5d39f45"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="org2860f69"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="org9c9dd8a"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="org30db0df"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="orgab14242"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &mdash; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="orgc73e947"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="org71aea42"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="org0e39e94"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &mdash; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="orgdc05f9b"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#org832bf0c)
2.  [Blog Posts](#org11878a6)
3.  [Table of Contents](#orgdc05f9b)
