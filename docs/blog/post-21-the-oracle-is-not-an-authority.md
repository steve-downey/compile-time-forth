<div class="abstract" id="orgb29ee33">
<p>
Part 7 built a tree-walking evaluator whose only job was to agree with the
compiled machine on every program. Part 15 deleted it and admitted, in the
same entry, what was left standing in its place: one interpreter watching
itself run twice, plus two legs of a promised replacement (gforth as a
differential check, the Forth-2012 core word-set suite) that did not
exist yet. Tonight they do: John Hayes' own conformance tester, adapted as
actual Forth text and compiled by this project's own interpreter, gating
six batteries on what the session's own output actually says at
constant-evaluation time, plus a harness that runs the same programs
through real <code>gforth</code> and diffs the stacks. It found things immediately.
One of them wasn't a bug. Two of them were, and neither gets fixed
tonight.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 20 - Just Edges ←](post-20-just-edges.md)

</nav>


# Actual Forth text, not an imitation of it

I could have written a C++ harness that imitates `T{ 1 2 + -> 3 }T`: parse the expected stack, run the program, compare. It would have been shorter, and it would have been testing nothing about this project except the parts I already trust. The whole reason `T{ ... -> ... }T` exists as a Forth convention rather than an external test format is that it exercises the outer interpreter's own parsing words and defining words the same way any other program does: `T{` and `->` and `}T` are just colon definitions, and the suite that uses them is nothing but more source text.

So that is what I built: John Hayes' public-domain conformance tester, trimmed of the parts this project has no way to reach (there is no floating point here, and no `ENVIRONMENT?` to select a branch that assumes otherwise) and pasted, whole, into the same interpreter that compiles `COUNTDOWN` or `SUMTO`. The macro trick is unglamorous: adjacent string literals are one literal as far as the compiler is concerned, so a shard's own assertions get concatenated straight onto the tester's own definitions at the same point `compiled_forth<Source>` deduces a template argument's length.

```cpp
#define SMD_FORTH_TTESTER_SOURCE \
    "VARIABLE ACTUAL-DEPTH "                                                  \
    "CREATE ACTUAL-RESULTS 20 CELLS ALLOT "                                   \
    "VARIABLE START-DEPTH "                                                   \
    "VARIABLE ERROR-XT "                                                      \
    ": ERROR ERROR-XT @ EXECUTE ; "                                           \
    ": EMPTY-STACK "                                                          \
    "    DEPTH START-DEPTH @ < IF "                                          \
    "        DEPTH START-DEPTH @ SWAP DO 0 LOOP "                            \
    "    THEN "                                                              \
    "    DEPTH START-DEPTH @ > IF "                                          \
    "        DEPTH START-DEPTH @ DO DROP LOOP "                             \
    "    THEN ; "                                                            \
    ": ERROR1 "                                                              \
    "    TYPE CR "                                                           \
    "    EMPTY-STACK ; "                                                     \
    "' ERROR1 ERROR-XT ! "                                                   \
    ": T{ "                                                                  \
    "    DEPTH START-DEPTH ! ; "                                             \
    ": -> "                                                                  \
    "    DEPTH DUP ACTUAL-DEPTH ! "                                          \
    "    START-DEPTH @ > IF "                                                \
    "        DEPTH START-DEPTH @ - 0 DO ACTUAL-RESULTS I CELLS + ! LOOP "     \
    "    THEN ; "                                                            \
    ": }T "                                                                  \
    "    DEPTH ACTUAL-DEPTH @ = IF "                                         \
    "        DEPTH START-DEPTH @ > IF "                                      \
    "            DEPTH START-DEPTH @ - 0 DO "                                \
    "                ACTUAL-RESULTS I CELLS + @ "                            \
    "                <> IF S\" INCORRECT RESULT: \" ERROR LEAVE THEN "        \
    "            LOOP "                                                     \
    "        THEN "                                                         \
    "    ELSE "                                                              \
    "        S\" WRONG NUMBER OF RESULTS: \" ERROR "                         \
    "    THEN ; "
```

Every `DO` loop in there is guarded by an `IF` first, which looks like caution and is actually a requirement: Forth-2012's own `DO` does not skip its body when the loop's own limit already equals its starting index, so a bare `count 0 DO ... LOOP` with `count` at zero does not do nothing, it loops until a 64-bit cell wraps all the way around. I confirmed that the straightforward way, by writing the unguarded version and watching it burn through a thousand steps of interpreter fuel without finishing. Real `gforth` does the identical thing. Hayes knew this in 1995; I found out tonight by nearly reproducing his own mistake and being caught by the fuel budget before I noticed I hadn't guarded a loop.


# Six shards, and a static\_assert that means something

The tester alone proves nothing. What proves something is a battery of assertions run through it, and I split that battery into six translation units (arithmetic, stack manipulation, memory, control flow, defining words, strings), each one its own `compiled_forth<...>` built from the tester's own text glued to that shard's own tests. Splitting them wasn't about organization. A single translation unit holding the tester plus every assertion this project makes would ask one `constexpr` evaluation to do far more interpreting than any compiler's own default operation ceiling expects, and I would rather bound that cost per shard, deliberately, than find out where the ceiling actually sits by hitting it.

Five of the six shards gate on the same fact: the tester never prints anything when every assertion inside it passes, so an empty captured output **is** the check.

```cpp
static_assert(arithmetic_suite.output().size() == 0);

TEST_CASE("CoreSuiteArithmeticTest - AllAssertionsPass") {
    CHECK(arithmetic_suite.output().size() == 0);
}
```

That `static_assert` is not decoration on top of "this compiled." Building `arithmetic_suite` at all means the interpreter ran thirty-nine assertions against its own arithmetic, comparison, and logic words, at compile time, and the only way this program builds is if every one of them agreed with what it was told to expect. A shard that fails to build has failed a real executed check, not a syntax check on a test file that never ran.

The strings shard is the one exception, and it earns being one: `TYPE` and `."` and a caught `ABORT"` are supposed to print, by design, so "output is empty" is the wrong gate for it. It checks instead that the tester's own two failure messages never show up, and that every word's own expected text actually did.


# Honest words, not apologies

Somewhere back in this project's memory-word history I declared a simplification and left it there: one address unit is one cell, full stop, no byte addressing anywhere in this data space. It was an honest declaration then, and it left a gap that only mattered once something needed to cross it: `CELLS`, `CELL+`, `CHARS`, `CHAR+`, `C@`, `C!`, the whole family Forth-2012 gives a program for talking about address units without hardcoding what one is.

I could have shipped these as apologies: comments explaining that they don't really do anything here, or worse, silently leaving them out of the suite's own coverage. I didn't. They're real dictionary entries with real behavior, honest about what that behavior reduces to under this system's own declared convention. `CELL+` and `CHAR+` are `1+`, `C@` and `C!` are `@` and `!`, nothing more; `CELLS` and `CHARS` get one new primitive between them, because nothing already on hand meant "give this cell straight back."

```cpp
// Step F32 (docs/forth-plan-2.md), D21: the address-unit words,
// DIV-0009's own revisit. `CELL+`/`CHAR+` alias `one_plus` directly
// (one address unit is one cell, so advancing by one cell *is*
// advancing by one address unit); `C@`/`C!` alias `fetch`/`store`
// directly (a "character" is one cell here too). Neither pair gets
// a new primitive enumerator -- both dictionary names below just
// point at the same opcode `1+`/`@`/`!` already have.
{"CELL+", primitive::one_plus},
{"CHAR+", primitive::one_plus},
{"C@", primitive::fetch},
{"C!", primitive::store},
// `CELLS`/`CHARS` have no existing identity primitive to alias, so
// both share the one new enumerator this step adds.
{"CELLS", primitive::identity},
{"CHARS", primitive::identity},
```

The new primitive still pops and re-pushes its one argument instead of doing nothing at all, which sounds like a distinction without a difference until you call it on an empty stack: a true no-op would let that through silently, and this project diagnoses every other empty-stack call as underflow. `1 CELLS` is `1` here, not a byte count, and a Forth-2012 program written assuming otherwise gets a different number. That's the declared convention working as declared, not a bug hiding behind a word that happens to exist.


# A second oracle, invited from outside

The suite above only ever checks this project against itself: does this interpreter's own idea of "correct" match this interpreter's own idea of "correct," recorded once in a `T{ ... -> ... }T` line. That's real, and it's also circular in a way that should make anyone nervous. So the other half of tonight's work talks to something outside this codebase entirely: real `gforth`, invoked as a subprocess, fed the same short programs this project's own runtime session runs, with both sides' final stacks and printed output compared line for line. It records `gforth`'s own version string before it runs anything, on the theory that a disagreement ought to be traceable to a specific build of the thing it disagreed with, since nothing stops that build from changing out from under this project later. If `gforth` isn't on the machine at all, this harness says so loudly rather than quietly doing nothing &#x2014; a test that vanishes without a trace is worse than one that fails.

I did not expect this to be interesting. A differential check against a mature, decades-old implementation felt, going in, like a formality: run the battery, watch it agree, move on. It agreed on almost everything. Then it didn't, twice, and once more in a way that looked like disagreement and wasn't.


# A disagreement that isn't a bug

`-7 2 /` gives `-3` in this project. It gives `-4` in `gforth`. My first reaction, looking at that side by side, was that the differential harness had done its job and caught a real bug: two systems computing division and landing on different numbers is the shape a diff tool exists to flag.

It hadn't caught a bug. Forth-2012 mandates a specific rounding convention only for the two words that name one explicitly in their own spelling (`FM/MOD` for floored, `SM/REM` for symmetric), precisely because plain `/` and `MOD` are left an ambiguous condition a conforming system may resolve either way ({Forth 200x Standardisation Committee}, 2014). This project's own division has always truncated toward zero, the same way C++'s own `/` does, since before there was a conformance suite to check it against. `gforth` floors instead. Neither is wrong. They're both legal answers to a question the standard declines to answer.

```forth
-7 2 /    \ this project: -3 (truncating)
-7 2 /    \ gforth:       -4 (floored)
```

The lesson I want to keep is not "division is fine, nothing to see." It's that an oracle that disagrees with you is not, by itself, telling you who's wrong. A differential harness is exactly as good as the thing it's diffing against, and a real Forth-2012 implementation is a witness, not a verdict. The move a disagreement actually buys you is a trip to the standard, not a trip straight to the debugger. I made that trip, found the ambiguous-condition language sitting right there, and left this project's own convention as it was. The alternative would have been changing behavior every existing arithmetic test in this project already depends on, to chase an agreement the standard itself says I don't owe anyone.


# `COMPILE,` is genuinely broken

This one is a bug, and the oracle needed almost no coaxing to find it. Forth-2012's own idiom for `COMPILE,` is a small piece of misdirection that only works if you get the immediacy backwards on purpose: define an **immediate** helper word whose own body ends in `['] TARGET COMPILE,`, then use that helper inside some third definition. Because the helper is immediate, it runs while the third word is still being compiled, and because `COMPILE,` itself is **not** immediate, writing it inside the helper's own body doesn't execute it there; it compiles a deferred call into the helper, one that only actually appends `TARGET`'s own behavior once the helper later runs, live, during someone else's compilation.

That's the whole trick, and it depends entirely on `COMPILE,` being the one non-immediate piece in an otherwise-immediate sandwich. This project installed `COMPILE,` as immediate a few steps back. Write the textbook helper here and `COMPILE,` fires the moment the helper itself is being defined, before `[']`'s own deferred literal-push has ever taken effect, against a data stack that has nothing on it yet to pop: an underflow, every time, on a word this project ships and calls compilable.

```forth
: FOO ['] DUP COMPILE, ; IMMEDIATE
: BAZ 5 FOO ;
BAZ .s        \ real gforth: <2> 5 5
              \ here: underflow, while defining FOO itself
```

I ran the `gforth` line to be sure I wasn't misreading the standard's own convention, and it works as advertised there. It doesn't here, and nothing about this step fixes that: the fix needs `COMPILE,` to stop being immediate, which in turn needs a real compiled form that can reach whatever definition is actively being built at the moment a helper word runs, not merely the ordinary stack machinery every other word this project ships already gets by with. That's a real piece of new plumbing, and building it wasn't what tonight was for. `COMPILE,` stays installed, dispatchable, and unable to do the one thing Forth-2012 actually wrote it for.


# `CATCH` can't give back what's gone

The second bug is smaller to describe and, I think, harder to fix cleanly. Forth-2012's own `CATCH` promises the caller's pre-existing stack items back exactly as they were if the caught word throws &#x2014; `i*x`, restored, regardless of what happened underneath the hood while the caught word ran. Nothing in the standard says the caught word has to leave those items alone first. A word that reads its own argument off the stack and only later decides to throw is an ordinary shape.

This project's own `CATCH` restores the stack by recording a depth when it starts and truncating back to that depth on a throw:

```cpp
template <int MaxDepth>
constexpr auto cell_stack<MaxDepth>::truncate(int new_depth) -> status {
    if (new_depth < 0 || new_depth > depth_) {
        return foundation::parse_error{foundation::source_pos{},
                                       "stack truncate: depth out of range"};
    }
    depth_ = new_depth;
    return std::monostate{};
}
```

Read that guard closely: it refuses to grow the stack back up. That's deliberate, from further back than tonight, and it's correct for the overwhelmingly common case, where the caught word never dips below its own call boundary. It stops being correct the moment a caught word legitimately pops something that was sitting there before it was ever called:

```forth
: CHK ABORT" BOOM" ;
1 ['] CHK CATCH    \ CHK's own ABORT" consumes the 1 as its condition,
                    \ then throws -- the depth CATCH recorded is now
                    \ *higher* than the depth at throw time
```

`truncate` can shrink a stack back to a shallower depth it remembers perfectly well. It has no way to regrow one back up to a depth it never kept the actual values for: the depth is a number, not a snapshot, and a number can't hand back what a word already threw away before deciding to throw. Fixing this for real means either storing the values themselves when `CATCH` starts, not merely their count, or deciding outright that a caught word popping below its own call boundary is a distinct, diagnosed error, not an ordinary underflow wearing a confusing message. Both are real design decisions. Neither is one a conformance suite gets to make on its own, so this stays open too.


# What a suite that only agreed with me would have been worth

I want to be plain about the shape of tonight, because it would be easy to write this entry as a scorecard (six shards green, one harness green, ship it) and let two open bugs sit as a footnote. They're not a footnote. A conformance suite whose only outcome, ever, is confirming what I already believed about my own interpreter would have cost real effort and told me nothing I didn't already think I knew. The suite earned its keep tonight specifically by disagreeing with me twice in ways I hadn't gone looking for, on words this project has been shipping as if they worked.

Neither disagreement is fixed. Both are written down, plainly, as things this interpreter currently gets wrong, on purpose, not smoothed over by a test that quietly avoids the shape that breaks them. That's a worse evening than watching six shards pass clean and calling it done. It is also the only version of tonight that was actually worth having.


# The bill for asking

None of this was free to run. The largest shard, arithmetic, takes about seventeen seconds to compile on its own &#x2014; the tester's own definitions plus thirty-nine assertions, genuinely interpreted, not merely parsed, at constant-evaluation time. A compiler's own default ceiling on how much work a single `constexpr` evaluation is allowed to do exists to catch runaway template metaprogramming, not a real interpreter loop this project already bounds with its own fuel budget, so I raised that ceiling explicitly for this suite instead of shrinking the suite to fit a limit that was never really about this. Raised, not removed: it's still a number, not infinity, which means a future step that makes this interpreter meaningfully more expensive to run per assertion fails a build against that number instead of quietly eating however much time it wants. I'd rather find out about a cost regression from a build failure than from noticing, one day, that compiling this project got slow and having no idea when that started.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 20 - Just Edges](post-20-just-edges.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
