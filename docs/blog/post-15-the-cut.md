<div class="abstract" id="orgf113d92">
<p>
Part 12 argued that the tree was the only thing this project ever built that
actually conflicted with a stateful outer interpreter, and it closed with a
list of what would have to go once that argument was accepted. Tonight I did
the list: <code>reader/</code>, <code>elaborator/</code>, the direct evaluator, the batch codegen
pass, close to 6300 lines, deleted outright rather than left standing next to
their replacement. <code>compiled_forth&lt;Source&gt;</code> now builds one session, once,
inside its own <code>constexpr</code> initializer, and <code>.run()</code> stopped being something
you call and started being something you read. None of that is free. Part 7
built a whole entry out of two evaluators that had to agree on every program,
and that structure is gone tonight, not improved on.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 14 - Correct by Accident ←](post-14-correct-by-accident.md)

</nav>


# What's gone

`reader/syntax_tree.hpp` and `reader/read_program.hpp`: the arena-backed tree and the hand-written grammar productions that built it, `IF` and `BEGIN` and `DO` each its own recursive-descent function. The whole of `elaborator/`: the elaborated core, the pass that resolved every word against the dictionary before anything ran, the tree-walking stack-effect checker. `machine/eval_direct.hpp`, the tree-walking oracle Part 7 spent an entire entry building. `machine/codegen.hpp`, the batch pass that turned a resolved tree into an instruction array in one go, back-patches and all. Their tests went with them. Nothing here was refactored or folded into something else; it was cut, and the pipeline it fed doesn't exist in this codebase as of tonight.

What's left standing is the text interpreter Part 13 and Part 14 built next to all of that, doing the same job a different way: scan a word, look it up, run it or compile it, one token at a time, with no tree anywhere in the loop. `compiled_forth<Source>` used to compose three named stages over that tree. It composes one call into the interpreter now.

```cpp
template <source_literal Source, int MaxCode = 4096, int MaxWords = 256,
          int MaxData = 1024, int MaxOut = 4096, int MaxName = 32,
          int MaxStack = 64, int BuildDepth = 64, int BuildRDepth = 64,
          int Fuel = 100000>
inline constexpr auto compiled_forth =
    forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>{
        interpreter::build_session<MaxCode, MaxWords, MaxData, MaxOut, MaxName,
                                   BuildDepth, BuildRDepth, MaxStack>(
            Source.view(), Fuel)
            .value()};
```

Same name, same NTTP trick keying the type on the source text itself, same public surface. Underneath, `read -> elaborate -> codegen` is one call to `interpreter::build_session`, and what comes back is not a compiled program &#x2014; it's a whole session, run to completion, holding the code space, the finished dictionary, the data-space high-water mark, and the output &#x2014; the same object Part 14 built for a single `:` definition, the thing every public entry point returns now.


# `.run()` stops being a verb

The R1 `forth_program::run()` used to construct a fresh `machine::forth_state` and call `machine::run` against it, from scratch, every single time you called it &#x2014; three accessors, three re-executions, on purpose, because a `forth_program` carried no mutable state of its own to make a second call cheaper. That contract is gone, and not because it stopped being useful. There's nothing left to re-execute. Building the session inside `compiled_forth`'s own namespace-scope initializer already ran the whole program, top level included, exactly once. `.run()`, `.stack()`, and `.output()` are reads over what that one run left behind:

```cpp
/// A built D15 session image (@ref interpreter::session), bundled with the
/// same three-accessor convenience surface step F15 established
/// (`run`/`stack`/`output`).
///
/// Unlike the R1-era @c forth_program this class replaces, @ref
/// forth_program carries no separate "compiled program" distinct from the
/// session it wraps, and none of its three accessors re-executes anything:
/// the whole program -- every top-level word and every `:` ... `;`
/// definition -- already ran once, when the @ref session_type this class
/// wraps was built (@ref compiled_forth below is the one place that
/// happens). `run`/`stack`/`output` are cheap reads over that
/// already-finished result.
///
/// @tparam MaxCode  Code-space instruction-array capacity (@ref
///                  interpreter::session).
/// @tparam MaxWords Dictionary/code-space word-table capacity, likewise.
/// @tparam MaxData  Data-space capacity, likewise.
/// @tparam MaxOut   Output-buffer capacity, likewise.
/// @tparam MaxName  Maximum word-name length, likewise.
/// @tparam MaxStack Final-data-stack snapshot capacity, likewise (F26's own
///                  addition to @ref interpreter::session).
template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
class forth_program {
  public:
    /// The session type this class wraps.
    using session_type = interpreter::session<MaxCode, MaxWords, MaxData,
                                              MaxOut, MaxName, MaxStack>;

    /// Wraps an already-built @p session; performs no interpretation of its
    /// own.
    constexpr explicit forth_program(session_type session);

    /// The whole built session image: code space, final dictionary,
    /// data-space high-water mark, captured output, and final data stack.
    /// Unlike the R1-era @c forth_program::run, this never fails at the
    /// call site -- any failure already happened, at compile time, when
    /// @ref compiled_forth built this object's own session (see this
    /// header's own top-of-file comment).
    [[nodiscard]] constexpr auto run() const -> session_type const &;

    /// The final data stack @ref run's own session left behind, bottom cell
    /// first and top cell last -- the same bottom-to-top order @ref
    /// machine::primitive::dot_s prints in. A thin accessor over @ref
    /// session_type::stack; see that field's own doc comment
    /// (`interpreter/session.hpp`) for how it is populated.
    [[nodiscard]] constexpr auto stack() const
        -> foundation::static_vector<machine::cell, MaxStack>;

    /// The output @ref run's own session accumulated while it was built. A
    /// thin accessor over @ref session_type::output.
    [[nodiscard]] constexpr auto output() const
        -> foundation::static_vector<char, MaxOut>;

  private:
    session_type session_;
};

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName,
                        MaxStack>::forth_program(session_type session)
    : session_(session) {}

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr auto
forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>::run()
    const -> session_type const & {
    return session_;
}

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr auto
forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>::stack()
    const -> foundation::static_vector<machine::cell, MaxStack> {
    return session_.stack;
}

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr auto
forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>::output()
    const -> foundation::static_vector<char, MaxOut> {
    return session_.output;
}
```

No fuel parameter on any of the three. No `foundation::result` wrapping `.run()`'s own return value, either &#x2014; it returns the session by reference, unconditionally, because any way that program could have failed already failed, at compile time, in the `.value()` call inside `compiled_forth`'s own initializer. Under R1, a program that compiled clean but never terminated inside its fuel budget was a normal, recoverable runtime failure, distinct from a bad parse. I had a test for that specific shape: `SPIN` (`BEGIN FALSE UNTIL`) under a fuel of ten, checked to fail `.run()` without failing the build. That test is deleted, not adapted, because the thing it tested &#x2014; a later, separately-fueled execution that a compiled artifact could still fail at &#x2014; doesn't exist at this layer any more. Compiling `SPIN` and running out of fuel are the same event now, and that event is a hard compile error like any other. The program itself wasn't thrown away with the test; it's sitting in a new corpus header, `SPIN` and all, waiting for the day this interpreter has a `BEGIN` to run it with. A caller working one layer down, against `interpreter::build_session` directly instead of through `compiled_forth`, still gets an ordinary recoverable `foundation::result` back. Only the convenience API's own contract narrowed.


# `CONSTANT` gets better on the way out

`VARIABLE`, `CREATE`, and `CONSTANT` were grammar productions under R1 &#x2014; `elaborate_variable`, `elaborate_create`, `elaborate_constant`, each its own function in the elaborator, each triggered by a shape the parser recognized syntactically. There is no grammar left for them to be productions of, so they moved into the interpreter, the same direct-name-comparison trick already used for `:`: check the token against a fixed set of names before ever touching the dictionary, and act.

```cpp
if (text == "CONSTANT") {
        // Forth-2012 CONSTANT semantics directly, not R1's
        // elaborate_constant (which required a syntactically
        // preceding literal token and constant-folded it): pop
        // whatever the data stack holds at this point, whatever
        // pushed it there -- `7 CONSTANT LUCKY` and
        // `3 4 + CONSTANT SEVEN` are equally valid under an
        // interpreter that has already executed the "7" (or "3 4 +")
        // before meeting CONSTANT.
        auto value = st.machine().data().pop();
        if (!value.has_value()) {
            return value.error();
        }
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{
                token_start.position(),
                "expected a name after CONSTANT"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        std::string_view name_text{
            folded_name.begin(),
            static_cast<std::size_t>(folded_name.size())};
        auto def_r = dict.define_constant(
            name_text, machine::constant_word{value.value()});
        if (!def_r.has_value()) {
            return def_r;
        }
        continue;
}
```

The move exposed a restriction that had nothing to do with Forth and everything to do with how the old elaborator worked. `elaborate_constant` needed a literal token sitting immediately in front of `CONSTANT` in the source text, so it could constant-fold it at elaboration time &#x2014; so `3 4 + CONSTANT SEVEN` was a diagnosed error under R1, because there is no literal immediately before `CONSTANT` there, only an addition. The interpreter has no such requirement, because by the time it meets `CONSTANT` it has already executed everything before it: `CONSTANT` just pops whatever the data stack holds and binds a name to it, which is what Forth-2012 actually says. `3 4 + CONSTANT SEVEN` works now. Nobody set out to fix that; deleting the thing that broke it was enough.


# No checker at all, and I want that said plainly

The stack-effect checker died with the elaborator, because it was a pass over the elaborated tree and the tree is gone. Its replacement &#x2014; an abstract interpretation over the instructions a definition actually emitted, run when `;` closes it &#x2014; is not written. A `( ... -- ... )` comment after a colon header is still scanned and stored on the finished definition, the same as the moment Part 14 added it, and it is checked against nothing at all. `: BAD ( -- ) DROP ;` compiles clean tonight and underflows the stack the first time anything calls it. That's not a rounding error in the bookkeeping; it's a real gap, open right now, and closing it is later work I haven't done.


# The programs stay even though the tests don't

Deleting `machine/vm.test.cpp`'s old tests and `machine/eval_direct.test.cpp` outright was the right call &#x2014; they tested a pipeline that no longer exists. But those files also held the accumulated program battery this project has been building since Part 7: `ABS`, `COUNTDOWN`, `SUMTO`, `FIND5`, `TENS`, `SUMEVEN`, `FIRST`, the `+LOOP` case, `UNLOOP EXIT`. Losing the tests loses nothing; losing the programs would have lost the one thing that will actually prove control flow works once this interpreter has any, because every one of those programs uses `IF` or `BEGIN` or `DO`, and none of that exists here tonight. I lifted the source text and its expected result, as a doc comment, into a small header before the tests that held them went away, so the corpus survives the pipeline that used to carry it.


# The oracle, owed a reckoning

Part 7 made a whole entry out of a specific structural fact: a tree-walking evaluator and a compiled machine, independently arriving at the same answer on every program, or one of them was wrong. That was never a convenience. It was a second, differently-shaped way of being right, and disagreement between the two was a bug report that cost nothing to generate. It's gone tonight, and I don't think it comes back in the shape it had.

Not because I stopped valuing it. There is no second evaluator to disagree with in a true Forth, and that isn't a corner I cut &#x2014; it's the design. Interpreting a word that's already defined runs the exact code a call to it from inside another definition runs; one code path, checked once, at compile-and-execute time, rather than compiled twice into two representations that could drift apart from each other. Part 14 already leaned on that fact to make `:` and `;` work at all. Tonight is when the alternative it replaced stops existing to compare against.

What's standing in the oracle's place is thinner, and I want it named as thin rather than dressed up as equivalent. The same session, agreeing with itself: built once inside one namespace-scope `constexpr` evaluation, checked inside a `static_assert` from that object, then checked again from a Catch2 test against the identical object at ordinary runtime, both leaving 16 on the stack for the same `SQUARED` definition. That's Part 8's proof &#x2014; one value, both times &#x2014; carrying something considerably larger than the single compiled program it used to carry, but it's one evaluator watching itself run twice, not two evaluators built two different ways. The other two legs the oracle's replacement is supposed to eventually stand on &#x2014; gforth run as a differential check against the same programs, and the Forth-2012 core word-set test suite run as a battery instead of one program I happened to think of &#x2014; don't exist in this codebase. I'm naming them because I said I would, not because either is built.


# What's still not here

Nothing in this entry taught the interpreter a new word. There is still no `IF`, no `DO`, no `BEGIN` anywhere `interpret` recognizes &#x2014; they're unknown words tonight, exactly as they were before Part 13, and the public API still cannot run a single one of the programs the corpus header is holding onto. The tree is gone, the grammar is gone, the direct evaluator is gone, and what replaced them can define a word and run one, and nothing more than that. The cut was supposed to be irreversible, and it is: there's no old pipeline left to fall back on if the interpreter turns out not to be able to do everything the tree could. That's the bet this whole project has been making since Part 12, paid for in full tonight, with the harder half of it &#x2014; teaching this interpreter to compile control flow the way `IF` and `LEAVE` already patch their own branches &#x2014; still ahead of me.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 14 - Correct by Accident](post-14-correct-by-accident.md)

</nav>


# References
