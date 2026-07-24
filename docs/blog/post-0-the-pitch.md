<div class="abstract" id="orgda756d8">
<p>
I have a compile-time Scheme that works, and a suspicion I want to test: that
Forth's control words are the same discipline the sender/receiver model already
enforces. So I am going to build a Forth that reads, compiles, and runs entirely
inside the C++26 constant evaluator, and write down what happens as it happens.
This entry is the bet, not the result &mdash; including the one thing I am refusing
to build.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md)

</nav>


# The bet

Here is the sentence the whole project hangs on. Forth's control vocabulary &mdash; `EXIT`, `LEAVE`, `CATCH/THROW` &mdash; is one-shot, upward-only, and dynamic-extent. You leave a word once. You leave a loop outward, never back into it. A `CATCH` handler is live exactly as long as the call under it is running, and not a moment after.

That is the same shape a sender's completion signal has. A sender completes once, on one of three channels, and the receiver downstream of it never runs twice (Dominiak, Micha{\\l} and others, 2024). The control flow Forth spells with stack words is the control flow structured concurrency spells with senders and receivers. If that is true, I should be able to compile Forth's control structures *into* senders, and have the sender machinery enforce the discipline that Forth only documents.

I don't know yet that it's true. I think it's true. That gap is why this is a series and not a paper.


# Why Forth, and why at compile time

The compile-time part is the easy half to justify. Modern C++ keeps moving the line between what runs during compilation and what runs after it, and by C++26 the constant evaluator is a real, if hostile, programming environment. No heap that outlives the evaluation. No virtual dispatch worth having. No pointer casts. I want to know where the wall is, and the honest way to find a wall is to walk into it carrying something heavy.

Forth is the something heavy, and it is a good choice for a reason beyond the bet. A Forth system is small enough to hold in your head and structured enough to have real phases &mdash; a reader, a dictionary, a compiler, a virtual machine. It is the canonical example of a language that is its own compiler. Building one at compile time means every one of those phases has to survive `constexpr`, and each phase that survives tells me something specific about the wall.

I am not doing this because anyone needs a Forth embedded in C++ template argument deduction. I am doing it to find out exactly what the constant evaluator can be made to carry, and to check the bet.


# The pipeline I think I'm building

The shape I am starting with, subject to revision the first time it meets a compiler:

```
source text
  → parser combinators        (scan words, recognize numbers, skip comments)
  → syntax tree               (arena-backed, control structures as nodes)
  → elaboration               (resolve words against the dictionary,
                               check stack effects)
  → codegen
      ├─ direct evaluator     (walk the tree; the reference oracle)
      └─ stack-machine VM      (flatten to instructions; the artifact
                               that survives to runtime)
      └─ sender backend        (the bet, made executable)
```

Two commitments in there are load-bearing, and I want them on the record now so that if they break, the break is visible.

The first: the combinator library *is* the parser, not just a tool for atoms. Numbers and names and comments are combinators, and so are colon definitions and `IF/THEN` and `BEGIN/UNTIL`. I would rather have one uniform way to build the grammar than a tidy combinator layer with a hand-rolled parser bolted on top.

The second: there is one elaborated core, and every backend reads it. The direct evaluator and the stack machine and &mdash; eventually &mdash; the sender backend all consume the same resolved tree. The direct evaluator exists to be the oracle: slow, obvious, structurally recursive, the thing I check the fast machine against. If the VM and the evaluator ever disagree, the evaluator is right until proven otherwise.

I am reusing a good deal of the Scheme project's foundation to get here &mdash; the arena, the `result` type, the combinator scaffolding. Standing on that is the point of having built it.


# What this is not

Classical Forth does not have a grammar. It has an outer interpreter: a stateful loop that reads a word, and either executes it now or compiles it, depending on a `STATE` variable that the words themselves can flip. Control structure is not parsed &mdash; `IF` is an immediate word that runs at compile time and lays down a branch by hand. The language extends its own syntax as it goes.

I am not building that. No `IMMEDIATE`, no `POSTPONE`, no `[` and `]`, no `STATE`, no user-defined parsing words. The grammar is a fixed, closed set of productions, and a Forth program cannot reach in and change how the next word is read.

This is a real divergence from Forth-2012, and I want to be plain that it is a choice, not an oversight. The bet is about control words and senders. A stateful outer interpreter with self-modifying syntax is orthogonal to that bet &mdash; it would dominate the reader and the elaborator and teach me nothing about compiling control flow into completion signals. It also fights the other foundational choice: every tree here is a flat arena built by structural recursion, and an outer loop that lays down control structure imperatively does not produce a tree, it produces a stream of patches. Giving up the outer interpreter is what makes the rest of the design possible.

So this is a Forth in the sense that it has a data stack, a return stack, colon definitions, and the core words. It is not a Forth you could hand an existing program and expect to run.

That's the bet, and the boundary around it. The rest of these entries are what it cost.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [Next: Part 1 - Standing on the Scheme Repo →](post-1-standing-on-scheme.md)

</nav>


# References

Dominiak, Micha{\\l} and others (2024). *P2300: std::execution*.
