- [Overview](#orgf5cfb2f)
- [Blog Posts](#org982aa93)
  - [[Part 0 — The Pitch](post-0-the-pitch.md)](#org02c70a7)
  - [[Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)](#org86efd85)
  - [[Part 2 — Parser Combinators](post-2-parser-combinators.md)](#orgce7c80d)
  - [[Part 3 — Reading Forth Text](post-3-reading-forth.md)](#org2a3f788)
  - [[Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)](#orgb984bc3)
  - [[Part 5 — The Machine](post-5-the-machine.md)](#org7f66daa)
  - [[Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)](#orgfed15a5)
  - [[Part 7 — The Oracle](post-7-the-oracle.md)](#orgc4ee535)
  - [[Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)](#orgbc47b8d)
  - [[Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)](#orgfbe854f)
  - [[Part 10 — The Address Was Always a Cell](post-10-memory-words.md)](#org157033e)
  - [[Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)](#org0278c28)
  - [[Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)](#orgb110aa3)
  - [[Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)](#org1234925)
  - [[Part 14 — Correct by Accident](post-14-correct-by-accident.md)](#org91b7896)
  - [[Part 15 — The Cut](post-15-the-cut.md)](#orge89d1c7)
  - [[Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)](#org8ab5c4f)
  - [[Part 17 — Something to Point At](post-17-something-to-point-at.md)](#org00ddcef)
  - [[Part 18 — What >IN Was For](post-18-what-in-was-for.md)](#org0f36ee2)
  - [[Part 19 — The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)](#orgb604bab)
  - [[Part 20 — Just Edges](post-20-just-edges.md)](#orgb068394)
  - [[Part 21 — The Oracle Is Not an Authority](post-21-the-oracle-is-not-an-authority.md)](#org95f6bb8)
  - [[Part 22 — The Call Stack Was the Continuation](post-22-the-call-stack-was-the-continuation.md)](#org2923ed0)
  - [[Part 23 — Renamed, Not Reimplemented](post-23-renamed-not-reimplemented.md)](#orgc7aaf7a)
  - [[Part 24 — Nothing Downstream Notices](post-24-nothing-downstream-notices.md)](#org73e3ac8)
- [Table of Contents](#org6d91f24)



<a id="orgf5cfb2f"></a>

# Overview

A working diary of building a Forth compiler and interpreter that runs entirely inside the C++26 constant evaluator. The same compiled program is meant to run at compile time and at runtime, and the reason I picked Forth is a guess I am trying to make good on: that Forth's control words &mdash; `EXIT`, `LEAVE`, `CATCH/THROW` &mdash; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver structured concurrency already enforces.

These are entries, not chapters. Each was written as the work landed, so an earlier one does not know what a later one found out. Where the plan met the compiler and lost, I have left the loss in.


<a id="org982aa93"></a>

# Blog Posts


<a id="org02c70a7"></a>

## [Part 0 — The Pitch](post-0-the-pitch.md)

Why build a Forth at compile time at all, what the pipeline is meant to look like, and the one thing this is deliberately not: a stateful outer interpreter with immediate words.


<a id="org86efd85"></a>

## [Part 1 — Standing on the Scheme Repo](post-1-standing-on-scheme.md)

The C++26 baseline, vendoring Beman Execution, and importing the `foundation` vocabulary by copy &mdash; where a two-line equality fast path turned out to compile on GCC and not on Clang.


<a id="orgce7c80d"></a>

## [Part 2 — Parser Combinators](post-2-parser-combinators.md)

Applicative combinators over immutable cursors, wired into the `foundation` typeclass machinery the Scheme reference left them disconnected from &mdash; and the one CPO that stopped being callable as a result.


<a id="org2a3f788"></a>

## [Part 3 — Reading Forth Text](post-3-reading-forth.md)

The lexical layer (`-1` is a number, `1-` is a word) and an arena-backed syntax tree whose nodes hold each other by integer handle, closing the recursion without a heap.


<a id="orgb984bc3"></a>

## [Part 4 — The Grammar That Couldn't Be a Combinator](post-4-grammar.md)

The plan said the combinator library *was* the parser. Nested control structures said otherwise, for a reason about types, not effort.


<a id="org7f66daa"></a>

## [Part 5 — The Machine](post-5-the-machine.md)

Cells, two stacks, the dictionary, and the data space. Building the dictionary before the data space had a real address type, and paying for it with a placeholder.


<a id="orgfed15a5"></a>

## [Part 6 — Elaboration and the Effect Checker](post-6-elaboration.md)

Resolving words in program order, retyping that placeholder (and tripping a warning doing it), and a stack-effect checker that is honest about the one thing it cannot see across `EXIT`.


<a id="orgc4ee535"></a>

## [Part 7 — The Oracle](post-7-the-oracle.md)

A direct evaluator to be the reference the compiled machine gets checked against &mdash; and the acceptance test that could not pass until I added two words the plan forgot to list.


<a id="orgbc47b8d"></a>

## [Part 8 — The Program That Survives to Runtime](post-8-survives-to-runtime.md)

Flattening the tree to a linear instruction array, back-patching branches, and one `constexpr` value that runs the same at compile time and at runtime. Eight opcodes of seventeen do real work; the rest are reserved on purpose.


<a id="orgfbe854f"></a>

## [Part 9 — The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

`compiled_forth<"...">` as the public surface, a hard compile error for a bad program, and the honest accounting: the thesis from Part 0 is still not executable, because the part that would prove it is not built.


<a id="org157033e"></a>

## [Part 10 — The Address Was Always a Cell](post-10-memory-words.md)

`VARIABLE~/~CONSTANT~/~CREATE` finally get something to read and write through, in both backends at once &mdash; and wiring it up means admitting this Forth's memory was cell-granular from several steps back, with none of Forth-2012's `CELLS~/~CELL+` words to say otherwise.


<a id="org0278c28"></a>

## [Part 11 — The Two Cells the Checker Never Saw](post-11-counted-loops.md)

`DO LOOP +LOOP I J LEAVE UNLOOP` land in both backends, loop parameters and all &mdash; and the static checker turns out to have been wrong about `DO`'s own cost since two entries back, in a way only a nested loop could ever expose.


<a id="orgb110aa3"></a>

## [Part 12 — The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md)

Part 0's refusal of Forth's outer interpreter doesn't survive a reread: the back-patching it worried about was already the project's own target representation. No code changed this entry &mdash; only the argument for what has to change next.


<a id="org1234925"></a>

## [Part 13 — >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md)

The Forth-2012 outer text interpreter, in interpret state only &mdash; and a design choice that pays off nowhere in this entry: `>IN` is a bare offset sitting in the open, not a cursor buried inside the scanner.


<a id="org91b7896"></a>

## [Part 14 — Correct by Accident](post-14-correct-by-accident.md)

`:` and `;` land, and with them a session image that outlives the constant evaluation that built it &mdash; and a first draft that passed every test for a reason that had nothing to do with being right.


<a id="orge89d1c7"></a>

## [Part 15 — The Cut](post-15-the-cut.md)

Deleting the R1 pipeline outright &mdash; reader, elaborator, direct evaluator, batch codegen, close to 6300 lines &mdash; and retargeting the public API onto a session built once. What it costs: Part 7's oracle, the two evaluators that had to agree, and nothing yet stands fully in its place.


<a id="org8ab5c4f"></a>

## [Part 16 — Nothing to Point At](post-16-nothing-to-point-at.md)

`IF ELSE THEN`, `BEGIN UNTIL`, and the whole `DO LOOP` family land as immediate words on Forth-2012's own orig/dest discipline &mdash; and a depth check that looked obviously right gets caught wrong by one of my own tests before it ever reached anyone else's code.


<a id="org00ddcef"></a>

## [Part 17 — Something to Point At](post-17-something-to-point-at.md)

`'`, `[']`, `EXECUTE`, `CREATE`, `DOES>`, `VALUE~/~TO`, and `DEFER~/~IS` all get a real execution token &mdash; and paying down a debt from Part 13 turns out to be what makes any of it possible.


<a id="org0f36ee2"></a>

## [Part 18 — What >IN Was For](post-18-what-in-was-for.md)

`PARSE`, `WORD`, `CHAR`, `S"`, `."`, and `ABORT"` land, and a definition three words long &mdash; `: ECHO-WORD 32 WORD COUNT TYPE ;` &mdash; reaches past its own call for an argument that was never inside it. Two entries' worth of debt about `>IN` finally cash out on the same example.


<a id="orgb604bab"></a>

## [Part 19 — The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)

`CATCH`, `THROW`, and `ABORT` land, and `ABORT"` stops being a hard stop and becomes a real `THROW -2` &mdash; but the entry is about what `THROW` had to do to unwind a return stack holding an arbitrary mix of call frames, loop frames, and stray `>R` values, and how that answers, without fully closing, the teardown gap Part 11 admitted to eight entries back.


<a id="orgb068394"></a>

## [Part 20 — Just Edges](post-20-just-edges.md)

A real stack-effect checker returns, as an abstract interpreter over the instructions a colon definition actually compiled instead of a tree that no longer exists &mdash; and Part 11's loop-teardown gap closes for free, because a loop's back edge turns out to need the same agreement any other join does. The first version of it was also wrong, caught not by a test I wrote but by a conformance suite landing on the same tree the same week.


<a id="org95f6bb8"></a>

## [Part 21 — The Oracle Is Not an Authority](post-21-the-oracle-is-not-an-authority.md)

The other two legs of the oracle Part 15 promised finally exist: John Hayes' own conformance tester, adapted as real Forth text and compiled by this project's own interpreter, gating six batteries on a session's own captured output; and a harness that runs the same programs through real `gforth` and diffs the stacks. One disagreement turns out not to be a bug at all &mdash; Forth-2012 leaves it ambiguous on purpose &mdash; but two others are, and neither gets fixed tonight.


<a id="org2923ed0"></a>

## [Part 22 — The Call Stack Was the Continuation](post-22-the-call-stack-was-the-continuation.md)

A second executor lowers the same compiled instruction stream the machine has run since Part 5 down to Execution26 senders, and agrees with it, block by block, on every control construct that matters &mdash; a call and a return stop pushing an address and become an ordinary recursive C++ call, exactly the refunctionalization Part 12 predicted. Two real bugs turned up along the way, one of them sitting quietly in already-shipped code for two entries, and the one place the theory doesn't reach &mdash; the constant evaluator &mdash; stays open.


<a id="orgc7aaf7a"></a>

## [Part 23 — Renamed, Not Reimplemented](post-23-renamed-not-reimplemented.md)

A prelude of Forth, compiled before every program from now on, defines three ordinary words and three aliases of `IF~/~ELSE~/~THEN` under new names. I first wrote this up as a full replacement of those three in Forth and had to walk it back: the C++ underneath is untouched, and the aliases dispatch into the same cases the originals always did. What holds is narrower and still real &mdash; the whole-body alias trick Part 16 found isn't specific to `THEN`; it reaches every structural control word, one at a time.


<a id="org73e3ac8"></a>

## [Part 24 — Nothing Downstream Notices](post-24-nothing-downstream-notices.md)

A foreign word &mdash; a plain C++ function taking the same machine state a primitive gets &mdash; lands with a dictionary entry and an execution token like any other word, and not one existing mechanism needed an FFI-aware case to reach it. What did need designing: where the function pointer actually lives, since no dictionary entry can name its type; and a toolchain surprise where comparing a function's address to null turns out not to be a constant expression at all under this project's own sanitizer.


<a id="org6d91f24"></a>

# Table of Contents


# Table of Contents

1.  [Overview](#orgf5cfb2f)
2.  [Blog Posts](#org982aa93)
3.  [Table of Contents](#org6d91f24)
