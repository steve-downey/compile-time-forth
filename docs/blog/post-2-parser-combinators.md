<div class="abstract" id="orgc2413d8">
<p>
The Forth grammar is going to be built out of applicative parser combinators &#x2014;
small callables over immutable cursors that compose into bigger ones. I brought
the combinator layer across from the Scheme project and wired it into the
<code>foundation</code> typeclass machinery the reference had left it disconnected from.
That was the right move, and it cost me one CPO that stopped being callable the
way you would expect.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 1 - Standing on the Scheme Repo ←](post-1-standing-on-scheme.md)

</nav>


# A parser is a callable

A parser here is a callable that takes an immutable cursor and returns either a value paired with a new cursor, or a failure. The cursor is a `string_view` and an offset; you never mutate it, you produce a fresh one advanced past what you consumed. That immutability is what makes backtracking free and the whole thing `constexpr`-safe &#x2014; there is no position to reset because you never moved.

```c++
// src/smd/forth/parser/  (shape, not verbatim)
// a cursor is a view plus an offset; copies are cheap and independent
struct cursor { std::string_view text; int offset = 0; };

// a parser<F> wraps a callable  cursor -> parse_result<T>
template <typename F>
struct parser { F run; };
```

The primitives are the usual few &#x2014; `satisfy` a predicate on the next character, `some` for one-or-more, a `map` to transform a result, a `lift2` to combine two, and an ordered choice `operator|` that tries the left parser and, only if it fails without consuming, tries the right. Out of those you build a number parser, a name scanner, a comment skipper, and &#x2014; this is the plan from Part 0 &#x2014; eventually the entire Forth grammar. One uniform way to build every production, atoms and control structures alike.


# Plugging into the foundation

The combinators compose like a functor, an applicative, and an alternative, because that is exactly what they are: `map` is `fmap`, `lift2` is applicative combination, `operator|` is the alternative's choice. The `foundation` layer already defines those three as CRTP bases with associated typeclass variables and free CPOs &#x2014; `fmap`, `invoke`, `alt` &#x2014; that dispatch to any registered type.

The reference combinators never actually registered. They carried their own local `ParserApplicative` and `ParserAlternative` layers and a private parser-only typeclass lookup, sitting right next to `foundation`'s own layer and never touching it. Two mechanisms for one idea.

So I wired them together. `parser<F>` registers against `foundation`'s own `functor_typeclass`, `applicative_typeclass`, and `alternative_typeclass`, and `foundation::fmap`, `foundation::invoke`, and `foundation::alt` now dispatch to a parser the same way they dispatch to anything else. There is no separate parser-only path.

The payoff is not tidiness for its own sake. It makes `parser<F>` the first real, exported type to exercise `foundation`'s typeclass laws. Up to now those CRTP bases were only ever instantiated by small test-local doubles. Now a law regression in `foundation` &#x2014; a broken `fmap` identity, an `alt` that isn't associative &#x2014; shows up through an actual consumer that ships, not just through a synthetic test type built to pass. If I am going to trust these laws in the grammar, I want them broken by the grammar's own building blocks when they break.


# The empty that can't be reached

Here is what unification cost. `foundation::alternative` is stricter than the reference's local parser typeclass: it requires not just `alt` (the choice) but `empty` (the identity element for that choice &#x2014; the parser that always fails without consuming, the unit that `alt` leaves alone). The reference only ever supplied `alt`. So I added `empty`:

```c++
// src/smd/forth/parser/parser_ops.hpp  (shape, not verbatim)
struct parser_alternative_impl {
    // a parser of value type T that always fails, consuming nothing
    template <typename T>
    static constexpr auto empty() -> parser</* ... */> { /* always fails */ }
};
```

That member is fine. The trouble is reaching it through the free CPO. The generic `foundation::empty<T>()` is a zero-argument call: it names the identity by its result type `T` and expects to find the registered instance from that alone. For an ordinary container type keyed on its element that is enough. But `parser<F>` is not one type &#x2014; it is a family, one distinct type per wrapped callable `F`. A zero-argument `empty<T>()` has `T` and nothing else, and there is no `F` to deduce from anywhere, so it cannot pick out which `parser<F>` you meant. There isn't one; there's a family.

The instance member has no such problem, because you name it on a concrete instance and hand it `T` directly:

```c++
auto never = parser_v.empty<int>();        // fine: T is explicit, the family is fixed
// auto q  = foundation::empty<int>();      // can't: nothing to deduce F from
```

So the rule for the rest of the project is: when you want an always-failing parser, call `parser_v.empty<T>()` on the typeclass instance, not the free `foundation::empty<T>()`. It is written on the member's doc comment and pinned by a test, so the next person to reach for the free function finds the wall with a note on it instead of a deduction failure with no explanation.

I could have kept the reference's separate parser typeclass and never met this edge. I think the edge is worth it. One mechanism, laws exercised by a real consumer, and a single documented exception is a better trade than two parallel typeclass systems that agree only by coincidence.

The combinators build and compose. Now they have to read actual Forth.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 1 - Standing on the Scheme Repo](post-1-standing-on-scheme.md) | [Part 3 - Reading Forth Text →](post-3-reading-forth.md)

</nav>


# References
