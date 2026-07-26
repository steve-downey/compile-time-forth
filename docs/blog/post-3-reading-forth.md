<div class="abstract" id="org24e92b0">
<p>
Two layers before there is a grammar: a lexical layer that turns source text into
words and numbers, and a syntax tree those words get assembled into. The lexer
has to know that <code>-1</code> is a number and <code>1-</code> is a word, and the tree has to be
recursive without a heap. The second is the interesting constraint &#x2014; the nodes
hold each other by integer handle, which closes the recursion without ever
needing a complete type or an allocation.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 2 - Parser Combinators ←](post-2-parser-combinators.md)

</nav>


# What counts as a word

Forth's lexical rule is almost aggressively simple: a word is a run of non-whitespace, and whitespace is the only separator. There are no operators to tokenize, no punctuation with its own meaning. `DUP` is a word. `+` is a word. `2DUP` is a word. `1-` is a word. This is the part of Forth that makes people either relax or twitch.

The lexer built on Part 2's combinators does four things. It scans a word as maximal non-whitespace. It folds that word to uppercase, because Forth names are case-insensitive and I would rather normalize once at read time than compare case-insensitively forever after. It skips comments &#x2014; `\` to end of line, and `(` &#x2026; `)` inline. And it decides, for each word, whether the characters spell a number or a name.

That last decision has an edge that is pure Forth:

-   `-1` is a number. Leading minus, then digits.
-   `1-` is a word. Digit, then minus &#x2014; not a number, and in standard Forth it is the core word that subtracts one.
-   `-` is a word. A bare minus is subtraction, not a number.
-   `2DUP` is a word. Starts with a digit, but isn't all digits.

So "starts with a digit or minus" is not the test. The test is "the whole run, after an optional leading minus, is digits, and there is at least one digit." Everything else is a name, and names are resolved later against the dictionary. Getting this wrong doesn't crash &#x2014; it just quietly turns `1-` into a number literal and then wonders why decrement stopped working. So the number recognizer is small and its tests are not.


# Stack-effect comments are not thrown away

One kind of comment is special. `( n -- n n )` after a word is a stack-effect comment, and in ordinary Forth it is documentation the machine ignores. Here I capture it. When a colon definition carries a stack-effect comment, the reader keeps the text of it on the node, because a later stage is going to want to check the definition's real effect against what the author claimed. The lexer doesn't interpret it &#x2014; it just refuses to drop it on the floor with the other comments. Whether anything useful comes of that is a problem for the entry where I build the effect checker.


# A recursive tree without a heap

Now the shape problem. A syntax tree is recursive: an `IF` contains a body, and a body can contain another `IF`. The Scheme project solved recursion like this with a fixpoint type, `Fix<CompF>`, backed by a heap box. I am deliberately not bringing that across. Under `constexpr` there is no heap that outlives the evaluation, and I decided early that every tree in this project is a flat arena, not a web of pointers.

An arena is one `static_vector` of nodes. A node that needs a child doesn't hold a pointer to it &#x2014; it holds an integer index into the arena. That index is an `arena_box`: a handle, not an address.

```c++
// src/smd/forth/reader/syntax_tree.hpp  (shape, not verbatim)
struct syn_node;   // forward declared: the handle needs only the name

struct syn_if {
    arena_box<syn_node> when_true{};    // an index into the arena
    arena_box<syn_node> when_false{};   // ... not a pointer
};

using syn_node = std::variant<
    syn_literal, syn_word, syn_colon_def,
    syn_if, syn_begin_until, syn_begin_while, syn_do_loop,
    syn_variable, syn_constant, syn_create, syn_tick>;
```

The trick is in the forward declaration. `arena_box<syn_node>` stores an `int`, so it is a complete type as soon as the *name* `syn_node` exists &#x2014; it never needs the definition. That breaks the cycle the language would otherwise complain about: `syn_if` can contain a handle to `syn_node` before `syn_node` is defined, because the handle is just an index and an index is just an `int`. No completeness requirement, no `fix`, no allocation. To follow a child you ask the arena for the node at that index.

Every node kind Forth's structure needs is in that variant: a literal, a word reference, a colon definition, the four control structures (`IF`, the two `BEGIN` forms, `DO`), and the definition words (`VARIABLE`, `CONSTANT`, `CREATE`, and `'` for a word's execution token). The whole tree is one array of these, wired together by handle.


# What the reader does not do

The reader assembles shapes. It does not resolve a single word &#x2014; it does not know whether `DUP` is a primitive, a colon definition, or undefined, and it does not care. A `syn_word` holds a name. Turning that name into something with meaning is elaboration's job, an entry or two from now. Keeping the reader ignorant is deliberate: it means the grammar can be tested purely on shape, and every semantic question lives in exactly one later place instead of leaking backward into the parse.

That is the whole front end in miniature: scan words, know a number from a name, build a tree of handles. The tree is ready. Assembling the control structures out of it is where I find out whether the Part 0 plan survives the compiler.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 2 - Parser Combinators](post-2-parser-combinators.md) | [Part 4 - The Grammar That Couldn't Be a Combinator →](post-4-grammar.md)

</nav>


# References
