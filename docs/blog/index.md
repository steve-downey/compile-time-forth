- [Overview](#org10e3932)
- [Blog Posts](#orgc4aaa99)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#org7f221eb)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#orgcca5aaf)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#orgb450bfa)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#org0b99c8e)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#orge9e3c4c)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#orgb3c8c3f)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#org4e21c7c)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#orgfa2f931)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#org1898e2d)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#org0bf40a6)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#orgd3aa294)
  - [[Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)](#org420f4d1)
- [Table of Contents](#org91f2f61)



<a id="org10e3932"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &#x2014; `EXIT`, `LEAVE`, `CATCH/THROW` &#x2014; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="orgc4aaa99"></a>

# Blog Posts


<a id="org7f221eb"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="orgcca5aaf"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &#x2014; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="orgb450bfa"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &#x2014; and the one CPO that stopped being callable as a result.


<a id="org0b99c8e"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="orge9e3c4c"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="orgb3c8c3f"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="org4e21c7c"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="orgfa2f931"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &#x2014; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="org1898e2d"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="org0bf40a6"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="orgd3aa294"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &#x2014; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="org420f4d1"></a>

## [Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)

`DO LOOP +LOOP I J LEAVE UNLOOP` land in both backends, loop parameters and all &#x2014; and the static checker turns out to have been wrong about `DO`'s own cost since two entries back, in a way only a nested loop could ever expose.


<a id="org91f2f61"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#org10e3932)
2.  [Blog Posts](#orgc4aaa99)
3.  [Table of Contents](#org91f2f61)
