<div class="abstract" id="org8e88979">
<p>
Elaboration is where a tree of names becomes a tree of meanings: every word
resolved against the dictionary in program order, the placeholder address from
Part 5 finally given its real type, and a stack-effect checker run over each
definition. Retyping the placeholder tripped a warning I didn't see coming. The
effect checker works &#x2014; with one blind spot I can describe exactly and haven't
yet watched misbehave.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 5 - The Machine ←](post-5-the-machine.md)

</nav>


# Resolving in program order

The reader left every `syn_word` holding a name and no opinion about what it means. Elaboration forms the opinion. It walks the syntax tree in program order, carrying the dictionary as it goes, and turns each name into a resolved core node: a primitive reference, a call to a colon word, a variable, a constant. Order matters because the dictionary grows as elaboration proceeds &#x2014; a colon definition adds its own entry before the definitions that follow it are elaborated, so a word can call anything defined earlier, and `RECURSE` names the definition currently being built without needing its name to be in scope yet. An unknown word is a diagnosed error here, with a position, not a mystery deferred to runtime.

The output is the elaborated core: the same tree shape, still arena-backed, still handles-not-pointers, but every reference now points at something real. This is the one intermediate form every backend will read &#x2014; the direct evaluator, the stack machine, and eventually the sender backend all consume this and nothing earlier.


# The placeholder, and the warning

Part 5 left a debt: `variable_word` held a plain `cell` where it wanted the distinct `addr` type, because the dictionary was built before that type existed. Elaboration is the first code that actually constructs a `variable_word` &#x2014; it happens when a `VARIABLE` declaration is elaborated &#x2014; so this is where the debt comes due. The data space's `addr` type is in place now. Retype the field, add the include, done.

Except the obvious spelling doesn't compile:

```c++
struct variable_word {
    addr addr{};     // error: -Wchanges-meaning
};
```

Naming a data member the same as its own type turns out to be ill-formed. Once the declaration introduces the member name `addr`, that name hides the type name `addr` for the rest of the declaration &#x2014; the very declaration that still needs the type name to finish. GCC calls it `-Wchanges-meaning`: the name `addr` changes meaning midway through its own declaration. The field simply cannot keep its old spelling once its type becomes `addr`. So it is renamed:

```cpp
struct variable_word {
    addr address{};
};
```

The construction sites move to `variable_word{addr{3}}` &#x2014; explicit, matching `addr`'s explicit constructor &#x2014; and read back `.address`. It is a trivial change with a non-trivial cause, and the cause is the kind of thing you only find by doing the rename and watching the compiler object to a name colliding with itself. The placeholder is paid off, and it is real type safety now, not a `cell` wearing a comment.


# Stack effects

Every Forth word has a stack effect: how many cells it consumes and how many it leaves. `+` is `( n n -- n )`, two in, one out. `DUP` is `( n -- n n )`. A colon definition's effect is the composition of its body's effects, and a definition whose two branches leave the stack at different depths is almost always a bug. The effect checker computes the effect of each definition and checks it.

The lattice is small. An effect is either `known` with a specific input and output count, or `unknown` when something in the body defeats the analysis.

```cpp
struct effect {
    bool known = true; ///< False means "unknown" (the lattice's top value).
    int inputs = 0;    ///< Cells required present at entry. Only meaningful
                       ///< when @ref known is true.
    int outputs = 0;   ///< Cells left behind, from the same entry point.
                       ///< Only meaningful when @ref known is true.

    /// The net change in stack depth (`outputs - inputs`). Only meaningful
    /// when @ref known is true -- callers must check that first.
    [[nodiscard]] constexpr auto net() const -> int { return outputs - inputs; }

    friend constexpr auto operator==(effect const &, effect const &)
        -> bool = default;
};
```

On top of it, the five diagnoses the checker actually issues: an `IF` and `ELSE` arm whose *net* effects differ; a loop body with a nonzero net effect (it would grow or shrink the stack every iteration); a `>R/R>` imbalance; an `EXIT` inside a `DO` without an `UNLOOP`; and a definition whose computed effect disagrees with the effect its author declared in the stack-effect comment &#x2014; which is why the reader bothered to capture those comments back in Part 3. `IF/ELSE` is checked by net effect so that a balanced conditional passes and a lopsided one is caught. `RECURSE` is handled by using the definition's declared effect, or `unknown` if there isn't one.


# The blind spot I can name

`EXIT` is where the checker is honest but incomplete, and I want the gap on the record now, before anything downstream leans on it.

A fully correct checker would verify *every* return path out of a definition &#x2014; an early `EXIT` and the eventual fall-through past the end &#x2014; and confirm they all leave the stack at a consistent depth. This one does not. When it reaches an `EXIT` while folding a body, it treats the rest of that body as unreachable and stops &#x2014; which is sound for an unconditional trailing `EXIT`, since code after it never runs. But it never reconciles an early-return path's depth against the depth it computes by continuing past the control structure the `EXIT` sits inside.

Concretely:

```forth
: F  DUP 0< IF EXIT THEN DROP ;
```

This definition has two genuinely different real effects. Take the `IF` arm and you `EXIT` with one cell left. Fall through and `DROP` runs, leaving zero. Those are different, and a Forth-2012 checker should say so. Mine computes a single effect for the whole definition and says nothing.

I am not fixing it in this step, and the reason is proportion. Modeling every return path correctly needs either a set of possible return effects threaded through the whole walk, or a dedicated terminal lattice value with its own combination rules &#x2014; a materially larger feature than the five diagnoses I set out to build, for a case none of those five call out. Building it now would blow this step well past a small, checkable change. So the checker stops folding at `EXIT`, and this early-return-depth disagreement goes undiagnosed.

The gap is real Forth-2012 exposure, not just a theoretical nicety. I expect it to be observable the moment I have something that actually *runs* a definition like `F` down each path and can watch the depths disagree. That something is the next entry. I have written down what should happen. Next I get to see whether it does.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 5 - The Machine](post-5-the-machine.md) | [Part 7 - The Oracle →](post-7-the-oracle.md)

</nav>


# References
