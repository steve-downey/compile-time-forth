<div class="abstract" id="org1754b3c">
<p>
One call now compiles a Forth program from its source text and hands back
something ready to run: <code>compiled_forth&lt;"..."&gt;</code>, keyed on the text itself, with a
malformed program rejected as a hard C++ compile error. That is a real result.
It is also not the result I set out for. The thing that would prove the bet from
Part 0 &mdash; Forth control compiled into senders &mdash; is the one part I have not
built, and this entry is where I say so plainly instead of dressing the gap as a
conclusion.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 8 - The Program That Survives to Runtime ←](post-8-survives-to-runtime.md)

</nav>


# One call

Everything up to now has been reachable only by naming the pipeline stages by hand: read, then elaborate, then codegen, then run. Every test wrote that sequence out as a little local helper. This step promotes the sequence to the one thing an outside caller is meant to touch.

The key is that the *source text* is a template argument. `source_literal<N>` is a struct that stores a null-terminated copy of the text in a fixed char array, which makes it usable as a non-type template parameter &mdash; so a program's content, not just its address or length, can drive template argument deduction.

```cpp
template <std::size_t N>
struct source_literal {
    char text[N]{}; ///< Null-terminated copy of the source text.

    /// Copies all @p N characters (including the trailing null) out of
    /// @p input.
    constexpr source_literal(char const (&input)[N]) {
        std::copy_n(input, N, text);
    }

    /// The stored text as a @c std::string_view, excluding the trailing
    /// null terminator.
    [[nodiscard]] constexpr auto view() const -> std::string_view {
        return {text, N - 1};
    }
};
```

That literal is the key to keying a variable template on the program's text &mdash; the whole pipeline runs once, at namespace-scope `constexpr` initialization, and the result is stored in a `forth_program` that offers `.run()`, `.stack()`, and `.output()`:

```cpp
template <source_literal Source, int MaxCode = 4096, int MaxNodes = 1024,
          int MaxBody = 64, int MaxName = 32, int MaxDepth = 32,
          int MaxWords = 256, int MaxData = 1024, int MaxWarnings = 64,
          int StackDepth = 64, int RStackDepth = 64, int MaxOut = 4096>
inline constexpr auto compiled_forth =
    forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData, MaxOut>{
        detail::compile_program<MaxCode, MaxNodes, MaxBody, MaxName, MaxDepth,
                                MaxWords, MaxData, MaxWarnings>(Source.view())
            .value()};
```

So the source text is the template argument, and using it is one line:

```c++
inline constexpr auto sq = compiled_forth<": SQUARED DUP * ;  7 SQUARED">;
static_assert(sq.stack().back() == 49);
```

One quirk I chose and documented rather than hid: a `forth_program` carries no mutable state &mdash; it can't, since `compiled_forth<Source>` is a `constexpr` value &mdash; so `run`, `stack`, and `output` each construct a fresh state and run the whole program from scratch. Call all three on one object and you have run the program three times. For the size of program this is meant for that is the right trade, and it is the only option that keeps the object stateless. It is a real cost, stated, not an accident to trip over later.


# A bad program is a compile error

This is the part I find genuinely satisfying. A `Source` that fails to parse, or names a word that doesn't resolve, or fails the stack-effect check, or overruns a capacity, is not a runtime error you check for. It is a hard compile error in the translation unit that wrote it.

The mechanism is almost too plain. `compile_program` returns a `result`, and `compiled_forth` calls `.value()` on it. Calling `.value()` on a `result` holding an error is not a core constant expression &mdash; it reaches into the wrong variant alternative, which throws, and you cannot throw during constant evaluation. So a namespace-scope `constexpr` initializer that hits an error fails the whole compile, rather than quietly producing a runtime-checkable failure.

```c++
// SQUARED is never defined here: elaboration diagnoses an unknown word,
// .value() on that error is not a constant expression, the TU does not compile.
inline constexpr auto bad = compiled_forth<"7 SQUARED">;
```

There is a negative-compile test that exercises this failing side on purpose. The compiler rejects a malformed Forth program at C++ compile time, with a diagnostic pointing at the offending line. A Forth error and a C++ error are now the same error.

Cleaning up around this, the placeholder entry point that used to return the string `"Steve"` is gone, and the library is header-only &mdash; nothing to link, just include and instantiate.


# What I did not build

Now the honest accounting, because the abstract promised it.

Go back to Part 0. The bet was that Forth's control words &mdash; `EXIT`, `LEAVE`, `CATCH/THROW` &mdash; are the one-shot, upward-only, dynamic-extent discipline that sender/receiver already enforces, and that I could compile Forth control *into* senders and let the sender machinery carry the discipline (Dominiak, Micha{\\l} and others, 2024)(Kiselyov, Oleg, 2012). That backend does not exist. The sender vocabulary I vendored in Part 1 is still just aliases waiting for a consumer. The opcodes for `EXECUTE`, `CATCH`, and `THROW` are reserved in the instruction set and fault if reached. `DO` &hellip; `LOOP` is still diagnosed, not run.

What is here is a Forth that reads its own source, resolves it, checks its stack effects, compiles it to a flat program, and runs that program both inside the constant evaluator and at ordinary runtime, from one call, with a malformed program caught at compile time. That stands on its own, and I am not going to pretend it is small. But it is the substrate for the bet, not the bet.

What proving the bet would take, as best I can see it from here: partition the elaborated core into basic blocks, emit one sender per block, wire each block's completion into the next, and route `THROW` onto the sender error channel so that `CATCH` is a handler with exactly the dynamic extent a receiver already has. If Forth's control flow really is the sender discipline, the reserved opcodes fill in without a fight and the sender graph enforces the one-shot rule for free. If it isn't, that is where I find out &mdash; and finding out is the point.

So the bet is still a bet. I have built the thing that can carry the experiment and not yet run the experiment. That is an unsatisfying place to stop a series, and it is the true one, which is the only kind of stopping place these entries were ever going to have. The interesting part is the part that's left.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 8 - The Program That Survives to Runtime](post-8-survives-to-runtime.md)

</nav>


# References

Dominiak, Micha{\\l} and others (2024). *P2300: std::execution*.

Kiselyov, Oleg (2012). *An argument against call/cc*.
