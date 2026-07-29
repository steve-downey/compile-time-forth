- [Overview](#org37f6f34)
- [Blog Posts](#org93112fd)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#orgd8ce660)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#org0948ca9)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#org27e29a5)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#org86a2576)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#org42d62a1)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#orga5043fc)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#org3f8f547)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#org7674ac2)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#orgd96e753)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#org0aef87b)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#orgf2af9e7)
  - [[Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)](#orgb303a78)
  - [[Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)](#orgd4bb212)
  - [[Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)](#org3a371ca)
  - [[Part 14 — Correct by Accident](post-14-correct-by-accident.md)](#orgc825ada)
  - [[Part 15 — The Cut](post-15-the-cut.md)](#orgc635f8e)
  - [[Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)](#orgf3f9c12)
  - [[Part 17 — Something to Point At](post-17-something-to-point-at.md)](#orgef70cd3)
  - [[Part 18 — What >IN Was For](post-18-what-in-was-for.md)](#org0b0d66c)
  - [[Part 19 — The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)](#org3f15769)
  - [[Part 20 — Just Edges](post-20-just-edges.md)](#orgf840a4f)
  - [[Part 21 — The Oracle Is Not an Authority](post-21-the-oracle-is-not-an-authority.md)](#orge966571)
  - [[Part 22 — The Call Stack Was the Continuation](post-22-the-call-stack-was-the-continuation.md)](#org42ed5c1)
  - [[Part 23 — Renamed, Not Reimplemented](post-23-renamed-not-reimplemented.md)](#org2bf72f7)
- [Table of Contents](#orgcc98997)



<a id="org37f6f34"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &#x2014; `EXIT`, `LEAVE`, `CATCH/THROW` &#x2014; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="org93112fd"></a>

# Blog Posts


<a id="orgd8ce660"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="org0948ca9"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &#x2014; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="org27e29a5"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &#x2014; and the one CPO that stopped being callable as a result.


<a id="org86a2576"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="org42d62a1"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="orga5043fc"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="org3f8f547"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="org7674ac2"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &#x2014; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="orgd96e753"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="org0aef87b"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="orgf2af9e7"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &#x2014; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="orgb303a78"></a>

## [Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)

`DO LOOP +LOOP I J LEAVE UNLOOP` land in both backends, loop parameters and all &#x2014; and the static checker turns out to have been wrong about `DO`'s own cost since two entries back, in a way only a nested loop could ever expose.


<a id="orgd4bb212"></a>

## [Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)

Part 0's refusal of Forth's outer interpreter doesn't survive a reread: the back-patching it worried about was already the project's own target representation. No code changed this entry &#x2014; only the argument for what has to change next.


<a id="org3a371ca"></a>

## [Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)

The Forth-2012 outer text interpreter, in interpret state only &#x2014; and a design choice that pays off nowhere in this entry: `>IN` is a bare offset sitting in the open, not a cursor buried inside the scanner.


<a id="orgc825ada"></a>

## [Part 14 — Correct by Accident](post-14-correct-by-accident.md)

`:` and `;` land, and with them a session image that outlives the constant evaluation that built it &#x2014; and a first draft that passed every test for a reason that had nothing to do with being right.


<a id="orgc635f8e"></a>

## [Part 15 — The Cut](post-15-the-cut.md)

Deleting the R1 pipeline outright &#x2014; reader, elaborator, direct evaluator, batch codegen, close to 6300 lines &#x2014; and retargeting the public API onto a session built once. What it costs: Part 7's oracle, the two evaluators that had to agree, and nothing yet stands fully in its place.


<a id="orgf3f9c12"></a>

## [Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)

`IF ELSE THEN`, `BEGIN UNTIL`, and the whole `DO LOOP` family land as immediate words on Forth-2012's own orig/dest discipline &#x2014; and a depth check that looked obviously right gets caught wrong by one of my own tests before it ever reached anyone else's code.


<a id="orgef70cd3"></a>

## [Part 17 — Something to Point At](post-17-something-to-point-at.md)

`'`, `[']`, `EXECUTE`, `CREATE`, `DOES>`, `VALUE~/~TO`, and `DEFER~/~IS` all get a real execution token &#x2014; and paying down a debt from Part 13 turns out to be what makes any of it possible.


<a id="org0b0d66c"></a>

## [Part 18 — What >IN Was For](post-18-what-in-was-for.md)

`PARSE`, `WORD`, `CHAR`, `S"`, `."`, and `ABORT"` land, and a definition three words long &#x2014; `: ECHO-WORD 32 WORD COUNT TYPE ;` &#x2014; reaches past its own call for an argument that was never inside it. Two entries' worth of debt about `>IN` finally cash out on the same example.


<a id="org3f15769"></a>

## [Part 19 — The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)

`CATCH`, `THROW`, and `ABORT` land, and `ABORT"` stops being a hard stop and becomes a real `THROW -2` &#x2014; but the entry is about what `THROW` had to do to unwind a return stack holding an arbitrary mix of call frames, loop frames, and stray `>R` values, and how that answers, without fully closing, the teardown gap Part 11 admitted to eight entries back.


<a id="orgf840a4f"></a>

## [Part 20 — Just Edges](post-20-just-edges.md)

A real stack-effect checker returns, as an abstract interpreter over the instructions a colon definition actually compiled instead of a tree that no longer exists &#x2014; and Part 11's loop-teardown gap closes for free, because a loop's back edge turns out to need the same agreement any other join does. The first version of it was also wrong, caught not by a test I wrote but by a conformance suite landing on the same tree the same week.


<a id="orge966571"></a>

## [Part 21 — The Oracle Is Not an Authority](post-21-the-oracle-is-not-an-authority.md)

The other two legs of the oracle Part 15 promised finally exist: John Hayes' own conformance tester, adapted as real Forth text and compiled by this project's own interpreter, gating six batteries on a session's own captured output; and a harness that runs the same programs through real `gforth` and diffs the stacks. One disagreement turns out not to be a bug at all &#x2014; Forth-2012 leaves it ambiguous on purpose &#x2014; but two others are, and neither gets fixed tonight.


<a id="org42ed5c1"></a>

## [Part 22 — The Call Stack Was the Continuation](post-22-the-call-stack-was-the-continuation.md)

A second executor lowers the same compiled instruction stream the machine has run since Part 5 down to Execution26 senders, and agrees with it, block by block, on every control construct that matters &#x2014; a call and a return stop pushing an address and become an ordinary recursive C++ call, exactly the refunctionalization Part 12 predicted. Two real bugs turned up along the way, one of them sitting quietly in already-shipped code for two entries, and the one place the theory doesn't reach &#x2014; the constant evaluator &#x2014; stays open.


<a id="org2bf72f7"></a>

## [Part 23 — Renamed, Not Reimplemented](post-23-renamed-not-reimplemented.md)

A prelude of Forth, compiled before every program from now on, defines three ordinary words and three aliases of `IF~/~ELSE~/~THEN` under new names. I first wrote this up as a full replacement of those three in Forth and had to walk it back: the C++ underneath is untouched, and the aliases dispatch into the same cases the originals always did. What holds is narrower and still real &#x2014; the whole-body alias trick Part 16 found isn't specific to `THEN`; it reaches every structural control word, one at a time.


<a id="orgcc98997"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#org37f6f34)
2.  [Blog Posts](#org93112fd)
3.  [Table of Contents](#orgcc98997)
