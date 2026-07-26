<div class="abstract" id="org71e1849">
<p>
Part 0 committed to a strong line: the combinator library is the parser, control
structures and all. Building the grammar is where that line broke. Not because
combinators are too weak in general, but because a production that recursively
contains itself needs a combinator whose own C++ type mentions itself, and there
is no such finite type. The nested control structures are hand-written mutually
recursive functions. The token scanning underneath them is still combinators.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 3 - Reading Forth Text ←](post-3-reading-forth.md)

</nav>


# The commitment, and where it holds

I said the combinator library would *be* the parser, not just a tool for atoms. For the token layer that is exactly what happened. Scanning a word, recognizing a number, capturing a comment, matching a specific keyword &#x2014; all of it is built from the Part 2 free functions: `scan_word`, `skip_forth_space`, `scan_paren_comment`, `satisfy`, `some`. A `scan_token` primitive sits on top of `scan_word`, and every production in the grammar calls down through it. That half of the commitment held without a fight.

The other half did not. Colon definitions and the four control structures &#x2014; `IF/ELSE/THEN`, `BEGIN/UNTIL`, `BEGIN/WHILE/REPEAT`, `DO/LOOP` &#x2014; are not combinators. They are a small set of plain, mutually recursive `constexpr` functions. This is a real divergence from the Part 0 plan, and the reason is worth getting exactly right, because it is about the type system, not about how hard I was willing to work.


# Why the type can't close

A combinator you build with `map`, `lift2`, and `operator|` has a concrete, finite type. It is `parser<F>` for some particular `F` &#x2014; the closure type of everything you composed. That is the whole appeal: the composition *is* the type, and the type is complete and known.

Now write the body of a colon definition as a production. A body is a sequence of body-items. A body-item can be a word, a number, or a control structure. A control structure &#x2014; an `IF` &#x2014; contains a body. So the production "parse a body" is defined in terms of "parse a control structure," which is defined in terms of "parse a body." The grammar is recursive, which is fine; grammars are.

The trouble is what that recursion does to the type. If "parse a body" is a combinator `parser<F>`, then `F` has to mention the parser for control structures, which has to mention the parser for bodies, which is `parser<F>` again. The type would have to contain itself. There is no finite `parser<F>` instantiation for "the parser that parses a body," because `F` is defined partly in terms of `F`. The compiler cannot name the type, because the type has no bottom.

```c++
// what a body-item production would need its own type to be:
//   parser< /* mentions the control-structure parser, which */
//           /* mentions the body parser, which is this type again */ >
// there is no finite F that closes this.
```

The standard escape is type erasure: hide the recursive parser behind a `std::function`-shaped wrapper so the recursion goes through a runtime indirection instead of the type. That is not available here. A type-erased callable needs the heap to hold arbitrary captured state, and this project bars heap-backed types from the compiled pipeline. There is no `constexpr`-friendly type-erased callable in the C++26 standard library to reach for instead. The one tool that would let a combinator be recursive is the one tool `constexpr` takes away.


# The other reason: the arena has nowhere to ride

Even setting recursion aside, there is a second obstruction, and it would bite a perfectly flat production too. Building a syntax-tree node means allocating it in the `tree_arena` from Part 3 &#x2014; and the arena is mutated as you go, threaded alongside the cursor so each production can drop its node in and hand back a handle. A `parser<F>`'s signature is `cursor -> parse_result<T>`. There is no slot in that signature for a shared, mutated-in-place arena. To compose tree construction out of combinators I would have to widen the calling convention of every parser in the project to carry an arena, whether it builds a node or not.

So even the non-recursive productions have a reason not to be combinators. The recursion is the reason they *can't* be; the arena is the reason they wouldn't want to be.


# Functions recurse fine

Plain function templates do not have the type problem, because the recursion is in the *calls*, not in the type. `parse_body_until` forward-declares `parse_body_item_from_token`; that function calls `parse_if`, `parse_begin`, `parse_do`; those call back into `parse_body_until`. C++ has supported mutually recursive function templates forever. Nothing here is a self-referential type &#x2014; it is a self-referential set of ordinary calls, and each function has an ordinary, complete signature that happens to take the cursor and the arena both.

```c++
// src/smd/forth/reader/read_program.hpp  (shape, not verbatim)
// mutually recursive plain functions; each threads cursor AND arena
constexpr auto parse_body_until(cursor, tree_arena&, /* terminators */, int depth)
    -> parse_result</* body handle */>;

constexpr auto parse_if(cursor c, tree_arena& a, int depth) {
    if (depth >= max_depth) return error("max nesting depth exceeded");
    // parse the true-arm body, maybe an ELSE arm, then THEN,
    // each by calling back into parse_body_until(..., depth + 1)
}
```

The recursion is bounded by a `MaxDepth` template parameter, checked before any of `parse_if/parse_begin/parse_do` descends a level, and diagnosed as `"max nesting depth exceeded"` rather than either looping forever or silently swallowing arbitrarily deep nesting. That bound is doing double duty: it caps the nesting a program is allowed, and because `MaxDepth` is fixed at the call site, it also caps the *compile-time* cost of the recursion regardless of how deep any particular program actually goes.


# What I'll admit

The clean version of the story is that combinators build the whole grammar. That version is wrong, and I would rather say so here than let Part 0 stand as if it had come true. The token layer is combinators. The tree-shaped recursion, and the arena threading, is hand-written. Only the parts that genuinely can't be a finite type are the parts that aren't.

I expect to meet this again. The next time I walk or rebuild a structure that can contain itself &#x2014; and elaboration is going to rebuild the tree &#x2014; the same wall is there for the same reason, and the same plain-mutually-recursive-function shape is the answer. A `constexpr` type-erased callable in some future standard would reopen it. Today it is closed, and pretending otherwise would just make the next person rediscover the bottom of a type that has none.

The grammar parses. Now the words in it need to mean something.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 3 - Reading Forth Text](post-3-reading-forth.md) | [Part 5 - The Machine →](post-5-the-machine.md)

</nav>


# References
