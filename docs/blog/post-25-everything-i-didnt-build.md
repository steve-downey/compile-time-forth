<div class="abstract" id="org741a94e">
<p>
Tonight's step didn't add a word to the language. It wrote down, in one
place, everything this Forth deliberately doesn't do: floating point,
double-cell arithmetic, blocks and files, locals, a dictionary
un-definition word, and a handful of others, next to the full log of
every place I built something other than what Forth-2012 or the plan said,
thirty entries long. Two other documents got the same treatment: the
project's own presentation file, which had been transcluding a function
that stopped existing fifteen entries ago, and the README, which still opened
by describing a different, much smaller project. Writing all of that down
turned out not to be pure bookkeeping. Building a table of every kind of
diagnostic this compiler raises, message and source position both, found a
real bug: a program that left more on the stack than its caller had room
for didn't get told so, it hit an assertion. That's fixed now, with a test
that pins the fix down. And, for what may be the first time, none of the
work needed a new entry in that log at all. The four slots I'd kept open
for surprises went unused, which is itself worth noticing.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 24 - Nothing Downstream Notices ←](post-24-nothing-downstream-notices.md)

</nav>


# Two placeholders, still there

This project has two documents that describe it to a stranger, and until tonight both of them were wrong in the specific way old documentation is wrong: not false, exactly, just describing something that used to be true.

The presentation file at the repository root still opened on a TODO. It used to transclude a placeholder API: a free function called `forth::forth()` that returned the literal string "Steve". That function stopped existing the day the real public API landed, Part 9 of this series. Fifteen entries have shipped since then, and the file at the root of the repository, the one meant to be handed to someone who wants a tour, was still describing a function nobody could build against.

The README had the same problem in a different key. It opened, until tonight, "This repo is my current set of best practices for C++ projects," followed by a timestamp and a description of a library that returns a name. That was true once. It stopped being true somewhere in the last twenty-odd entries, and nothing had gone back to say so.

Both got rewritten onto what actually exists, and the presentation file's new opening transcludes the real thing instead of describing it:

```cpp
/// A compile-time string literal usable as a non-type template parameter
/// (an NTTP).
///
/// Storing a null-terminated copy of the source text in a fixed-size @c
/// char array lets the *content* of a Forth program participate in
/// template argument deduction: `compiled_forth<"...">` is a variable
/// template keyed on the literal source text itself, not merely on its
/// address or length.
///
/// @tparam N The literal's length including its trailing null terminator
///           (deduced from the array-reference constructor argument).
// 0f3a01ca-389f-4ca6-b09c-6b6423482246
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
// 0f3a01ca-389f-4ca6-b09c-6b6423482246 end
```

```cpp
template <source_literal Source, int MaxCode = 4096, int MaxWords = 256,
          int MaxData = 1024, int MaxOut = 4096, int MaxName = 32,
          int MaxStack = 64, int BuildDepth = 64, int BuildRDepth = 64,
          int Fuel = 100000>
inline constexpr auto compiled_forth =
    forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>{
        interpreter::build_session_with_prelude<MaxCode, MaxWords, MaxData,
                                                MaxOut, MaxName, BuildDepth,
                                                BuildRDepth, MaxStack>(
            Source.view(), Fuel)
            .value()};
```

That's the same shape Part 9 described: a Forth program as a template argument, interpreted once, handed back as a value a caller can inspect or re-run. What's changed since then is mostly capacities nobody has to think about (`MaxStack`, `BuildDepth`, `BuildRDepth`, `Fuel`) &mdash; each with a default, each grown in only because some later entry needed the room. The two that matter to anyone actually hitting a limit are `MaxStack` and `BuildDepth`, and they are not the same capacity: `BuildDepth` bounds the transient stack while the program is being built, `MaxStack` bounds what the returned session is allowed to remember afterward. Shrink the wrong one and you get a confusing error about the wrong thing. I know this because I nearly did it myself, a few paragraphs from now.


# Where the line gets drawn

The presentation file and the README describe what this project is. There was nowhere that plainly said what it isn't, and "isn't" had been accumulating for a while: a scope cut decided early on, a system characteristic (data-space addresses are cells, not bytes, Part 10's own subject) declared once and then assumed everywhere after, and thirty separate places across the series where I built something other than what the plan or Forth-2012 itself specified, each written down at the time it happened and never collected anywhere since.

All of that is one document now. Permanently out of scope, by design, not by oversight: floating point, double-cell arithmetic, `PICK=/=ROLL`, blocks and files, locals, `MARKER=/=FORGET` (there's no dictionary un-definition mechanism to hang them off of), environmental queries, and `>NUMBER`. None of those is a gap I ran out of time for. Each is a decision that a from-scratch, compile-time Forth doesn't need to make everything Forth-2012 lets a Forth make.

The other thirty entries are smaller and more particular: a fast path that wasn't portable across compilers, a placeholder type that outlived its reason for existing by four entries, a basic-block algorithm that degenerated every block to one instruction until a test caught it. Most of those already got a paragraph somewhere in this series as they happened. Rolling them into one table is the part that was missing: read the table top to bottom and you get the shape of thirty small corrections, not just the one you happen to remember.

What's notable about tonight's own contribution to that table is that there isn't one. I'd left room for a handful more entries, expecting that writing everything down honestly would force at least one decision I'd been fudging. It didn't. Everything this step touched already had somewhere to go.


# A diagnostic that proves itself

The rest of the step is smaller in scope and more mechanical: does this compiler actually tell you what's wrong, at the right place in your source, for every kind of wrong thing it can find?

There's been scattered coverage of this since early on: a test here that checks an unresolved word is reported at the right offset, a static\_assert there that a declared effect mismatch gets caught. What didn't exist was one table that walks every **kind** of diagnostic this compiler raises and checks both the message and the position for each: an unresolved name, an unterminated colon definition, a control word used outside the structure it needs (`LEAVE` outside a loop, `EXIT` inside a `DO` loop without `UNLOOP`), a runtime stack fault, a declared effect that disagrees with what a word actually computes, and `POSTPONE` naming a word that doesn't exist. Seven rows, seven programs, each captured by actually running it through the interpreter and reading back what it reported, never hand-computed, because a hand-computed byte offset is confidently wrong in the way a test like this exists to catch.

Two more tests extend a pattern this project has had since the syntax-error test at Part 9: a translation unit that is **supposed** to fail to compile, built by a dedicated CTest whose only passing condition is that the build fails. One pins a declared-effect mismatch: `": BADDECL ( n -- n n ) DUP DROP ;"` claims a net effect of one cell and computes zero. The other pins a capacity overflow: five pushes against a build-time stack sized for four. Neither of those tests, on its own, proves the compiler failed for the **right** reason. A WILL\_FAIL test only proves the build died, and the compiler's own output for a failed constexpr initializer never names the diagnostic. So each one has a positive twin in the table: the identical program, run through `interpret` where it can actually report something, asserting the exact message the failing test's own comment claims. Without the twin, a WILL\_FAIL test would keep passing even if the compiler started rejecting the program for a completely different, wrong reason.

I nearly got the capacity twin's own position wrong, the same way I'd already warned myself not to a few paragraphs earlier this session: I went to type in an offset before I'd actually run the program past it. The right answer turned out to be zero, not because the fifth push happens at the start of the source (it doesn't) but because a stack fault has no position to report at all. It's diagnosed by the stack itself, which has no notion of source text, and the interpreter passes that error through exactly as it received it, instead of trying to attach a location that was never computed. I'd rather have caught that by running the program, which I did, than by trusting what looked like an obvious guess.


# The bug the paperwork found

Writing that table meant reading through every path that can produce a diagnosis, and one of them turned out not to produce one. `build_session`, the function underneath `compiled_forth` that actually runs a program and hands back a session, snapshots the data stack it left behind so the session can remember it. That snapshot has its own capacity, separate from the transient stack the program ran on (the `MaxStack=/=BuildDepth` distinction from a few sections back), and nothing checked that a program's final depth actually fit.

```cpp
/// Builds a @ref session by interpreting @p text from a fresh @ref
/// forth_state seeded with @ref machine::default_dictionary (D15: "a session
/// cannot span constant-expression evaluations" -- this function is the one
/// boundary a session is ever built across, called once, wholesale, whether
/// at constexpr or ordinary runtime).
///
/// @tparam MaxDepth  The transient build-time @ref forth_state's data stack
///                    capacity; also @ref session::stack's own snapshot
///                    capacity ceiling (@p MaxStack must be at least as
///                    large as whatever depth the program actually leaves
///                    behind, same discipline as every other capacity here).
/// @tparam MaxRDepth Likewise, the build-time return stack capacity.
/// @tparam MaxStack  @ref session::stack's own capacity (F26).
/// @tparam MaxForeign @p vocabulary's own registry capacity (F34).
///
/// @param vocabulary Step F34's own addition (D18): a @ref
///                   machine::foreign_dictionary -- @ref
///                   machine::default_dictionary already extended with one or
///                   more `with_foreign` registrations -- whose word list
///                   *replaces* the default one this function would otherwise
///                   build, and whose registry backs every @ref
///                   machine::foreign_word in it. `nullptr` (the default)
///                   builds exactly the prelude-free, foreign-free session
///                   every caller before that step already got.
/// @param vm_fuel    The VM's own step budget (D22), forwarded to
///                   @ref interpret; previously left at @ref interpret's own
///                   default, which is still this parameter's default.
template <int MaxCode = 4096, int MaxWords = 256, int MaxData = 1024,
          int MaxOut = 256, int MaxName = 32, int MaxDepth = 64,
          int MaxRDepth = 64, int MaxStack = 64, int MaxForeign = 16>
constexpr auto build_session(
    std::string_view text, int fuel = 100000,
    machine::foreign_dictionary<MaxWords, MaxName, MaxForeign, MaxDepth,
                                MaxRDepth, MaxData, MaxOut> const *vocabulary =
        nullptr,
    int vm_fuel = 100000)
    -> foundation::result<
        session<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>> {
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> st{text};
    auto dict = vocabulary == nullptr
                    ? machine::default_dictionary<MaxWords, MaxName>()
                    : vocabulary->words;
    compile_buffer<MaxCode, MaxWords> buf;

    auto r = interpret(st, dict, buf, fuel, vm_fuel,
                       vocabulary == nullptr ? nullptr : &vocabulary->foreigns);
    if (!r.has_value()) {
        return r.error();
    }

    // Snapshot the build-time data stack bottom-to-top (F26): same order
    // machine::primitive::dot_s prints in, and the same convention
    // forth.hpp's own (now-superseded) R1-era forth_program::stack used.
    //
    // F36 error-quality pass: the final depth is diagnosed against @p
    // MaxStack here, rather than left to @ref foundation::static_vector's
    // own capacity assertion below. Before this check, a @p text that left
    // more cells behind than @p MaxStack could hold reached
    // `stack_snapshot.push_back` past capacity -- an assertion failure (UB
    // in a release build), not a `foundation::result` a caller could
    // observe, in violation of this project's own "misuse is a diagnosed
    // error, never UB" invariant. `session.test.cpp`'s own
    // `BuildSessionDiagnosesFinalDepthExceedingMaxStack` is the regression.
    if (st.data().depth() > MaxStack) {
        return foundation::parse_error{
            foundation::source_pos{},
            "final data stack depth exceeds session MaxStack capacity"};
    }
    foundation::static_vector<machine::cell, MaxStack> stack_snapshot{};
    for (int offset = st.data().depth() - 1; offset >= 0; --offset) {
        stack_snapshot.push_back(st.data().peek(offset).value());
    }

    return session<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>{
        .code = buf,
        .dictionary = dict,
        .data_space_high_water = st.data_space().size(),
        .output = st.output(),
        .stack = stack_snapshot,
    };
}
```

Before the check that's now in there, a program that left more cells behind than its caller's own `MaxStack` could hold ran straight into the fixed-capacity stack's own bound check on the push: an assertion failure, undefined behavior in a build that doesn't keep assertions. Nobody who hit this would have gotten a diagnosed error back. They'd have gotten a crash, or worse, silence, in the situation this project has said since early on that it refuses to allow: misuse is a diagnosed result, never UB. It's fixed in place now, with a regression test that pins a two-cell capacity against a program that leaves three cells behind.

I like this finding better than most of the ones this series has recorded, because nobody asked the question "does this defend itself" on purpose. The table asked a completely different question (does every kind of diagnostic report the right message at the right place) and answered it by first checking that there **was** a diagnostic everywhere the compiler could plausibly need one. A test written to document existing behavior found behavior that didn't exist yet.


# What's still open

The plan I've been working from is done: this is its last step. That's a fact about the plan, not a claim about the compiler.

The limitations document this step wrote isn't a list of things I intend to get back to. Most of it is permanent by design. But some of it is genuinely open, and I'd rather say so than let "consolidation" imply everything got tied off. `COMPILE,` still doesn't have anywhere useful to be called from: the textbook idiom for it, an immediate helper word whose body ends in `['] TARGET COMPILE,`, needs `COMPILE,` itself to be **not** immediate, and this project's own `COMPILE,` is immediate, so the one usage Forth-2012 actually demonstrates for it underflows the stack instead. `CATCH`, landed back at Part 19, still can't put back what a caught word already popped from below its own call boundary before throwing: restoring the stack on a throw only ever shrinks it, never regrows it, so a word that consumes an argument the caller supplied and then throws loses that argument for good. And the bootstrap prelude from Part 23 only runs in front of a program built through the one public entry point; every other way of building a session in this project's own test suite still gets the prelude-free dictionary it always did.

None of those got closed tonight, and none of them needed to be for this step to be done. What changed is that they're written down in the same place as everything else this project doesn't do, next to the things it never intended to do in the first place, instead of scattered across whichever entry happened to find them.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 24 - Nothing Downstream Notices](post-24-nothing-downstream-notices.md)

</nav>


# References
