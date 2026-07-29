<div class="abstract" id="orgcb10f27">
<p>
Every session now opens on thirty-five tokens of Forth I never typed: a short
prelude, compiled before whatever program I actually asked for, defining three
ordinary words and three aliases of <code>IF</code>, <code>ELSE</code>, and <code>THEN</code>. I wrote the
commit for the second half as though a piece of the compiler had moved out of
C++ and into Forth. It hadn't. <code>WHEN</code>, <code>OTHERWISE</code>, and <code>ENDIF</code> still call
straight into the same three cases <code>IF</code>, <code>ELSE</code>, and <code>THEN</code> always called; the
vocabulary got wider and nothing underneath it got smaller. What's true, and
narrower, is still worth having: the trick that let one word borrow another's
entire compiled behavior isn't a <code>THEN</code>-shaped accident. It reaches every
structural word in the control vocabulary, one at a time, which means a Forth
program can rename all of it. Not rebuild it. Rename it.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 22 - The Call Stack Was the Continuation ←](post-22-the-call-stack-was-the-continuation.md)

</nav>


# A prelude, before anything you typed

I gave the session builder a second job tonight: before it looks at a single character of the program I actually handed it, it compiles a short piece of Forth I wrote myself and shipped inside the compiler. Same interpreter, same dictionary, same code space: the prelude's own words end up sitting in both before the caller's text gets its turn. It's the same discipline the conformance suite already leaned on two entries back, when the standard's own test corpus turned out to be more honestly represented as Forth text my interpreter compiles than as C++ pretending to know what it means: if I want to claim a definition works, the definition should be real source, not a description of one.

```forth
inline constexpr std::string_view prelude_source =
    ": NIP SWAP DROP ; "
    ": TUCK SWAP OVER ; "
    ": ?DUP DUP IF DUP THEN ; "
    ": WHEN POSTPONE IF ; IMMEDIATE "
    ": OTHERWISE POSTPONE ELSE ; IMMEDIATE "
    ": ENDIF POSTPONE THEN ; IMMEDIATE ";
```

Six words, thirty-five tokens. `NIP`, `TUCK`, and `?DUP` are ordinary colon definitions, nothing about them needs saying twice: `?DUP`'s own body reaches for `IF` and `THEN` exactly the way any other word's body would, as dictionary entries it looks up and either runs or compiles, immediate or not. Nothing had to be taught to let a user-defined word call an immediate one from inside itself; that rule has been sitting there since the text interpreter went in, doing its job on words I happened to write myself instead of ones I'd predicted somebody else would.

`WHEN`, `OTHERWISE`, and `ENDIF` are a different shape entirely: each one's entire body is a single `POSTPONE` of `IF`, `ELSE`, or `THEN`. That's the part of tonight that needs the rest of this entry.


# Three words move out, and leave a shadow behind

`NIP`, `TUCK`, and `?DUP` used to be C++, installed straight into the dictionary as primitives the way `DUP` and `SWAP` still are. Tonight they moved into the prelude as compiled colon words instead, and the dictionary's own entry count dropped from ninety-eight to ninety-five. What didn't move: the three primitives' own enum tags and their cases in the VM's dispatch are still sitting exactly where they were, compiled into the binary, reachable by nothing at all now that no dictionary entry names them. I didn't delete them. Deleting them would have meant touching a switch and a primitive-effect table for a benefit nobody asked this step for, and Forth itself doesn't care: a colon word and a primitive are indistinguishable from the outside, so a caller building a session the ordinary way can't tell `NIP` apart from what it used to be. The C++ underneath just became an orphan nothing points at any more, instead of something erased.

I could have moved `1+` and `1-` the same way; they're just as derivable (`: 1+ 1 + ;`) as the three that did move. I left them alone. An older battery of accepted programs (the running acceptance test this project has carried since the early control-flow work) reaches `1+` straight through the raw dictionary, with no prelude anywhere in the path, and moving the word would have meant deciding whether "the battery still passes" is allowed to mean "passes once you also change what's running it." That's a real question and I didn't want to answer it by accident on the way to a smaller primitive count, so `1+` and `1-` stayed right where they've always been.


# The alias reaches all three

Two entries back I wrote down a trick and its own limit in the same sitting. The trick: a control word like `THEN` has no address, nothing a `call` could ever mean, because its whole output is a mutation of the code being built, finished by the time it returns. So `POSTPONE` of one doesn't compile anything: it just records the target and lets the closing `;` install the new word as a plain alias, indistinguishable from the word it names, provided the postponed control word is the **entire** body of the definition doing the postponing, nothing before it and nothing after. `: ENDIF POSTPONE THEN ; IMMEDIATE` was the proof, and it worked the first time I tried it.

One entry later, `EXECUTE`, `CREATE`, and `DOES>` picked up real compiled forms of their own and escaped that limit, composing with ordinary code in the same definition instead of only standing alone as the whole body. I wrote at the time that the rest of the control words (`IF`, `THEN`, `DO`, and everything like them) couldn't do that and wouldn't, because there is no later moment in their execution to represent as bytecode in the first place. I still believe that. It hasn't moved.

What I hadn't actually checked was narrower, and turned out to be a different question from that limit: does the whole-body alias trick that worked for `THEN` work for every structural word the same way, or did I just get lucky that `THEN` happens to be the one control word simple enough for it? The honest answer meant looking at what `IF` and `ELSE` each actually do. `IF` pushes one marker onto the discipline the standard calls orig/dest and leaves a branch to be patched later. `ELSE` pops that marker, emits its own branch, and pushes a new one. `THEN` pops and patches. None of the three reads anything the other two left behind beyond what that shared bookkeeping already carries by design ({Forth 200x Standardisation Committee}, 2014); each one is as self-contained an action as `THEN` turned out to be. Nothing in the mechanism that makes `POSTPONE` install a whole-body alias singles `THEN` out either; it applies to whichever control word shows up the same way.

So: `WHEN` is `POSTPONE IF`, `OTHERWISE` is `POSTPONE ELSE`, `ENDIF` is `POSTPONE THEN`, each one immediate, each one the entire body of its own definition. Used together they reproduce `IF ... ELSE ... THEN` end to end, under new names:

```forth
\ (shape, not verbatim)
: SIGN DUP 0< WHEN DROP -1 OTHERWISE DROP 1 ENDIF ;
-7 SIGN
```

That leaves `-1` on the stack, by way of a definition that never once says `IF`, `ELSE`, or `THEN`.


# What I got wrong writing that down

I called this a full `IF~/~ELSE~/~THEN` replacement, and left the word "replacement" to carry more than it should have. Read uncharitably, the way I'd read it if somebody else had written it, that sentence says `IF` got reimplemented in Forth. It didn't, and it couldn't have: nothing about tonight gave any of the three words a second way to reach the branch-patching logic underneath them. `IF`, `ELSE`, and `THEN` are still installed exactly as they were, their three cases untouched. `WHEN`, `OTHERWISE`, and `ENDIF` are three additional dictionary entries, each one dispatching into one of those same three cases. The C++-installed control vocabulary is the same size it was before tonight. The session's own dictionary is three names bigger, not the compiler underneath it three cases smaller.

Once I'd written that plainly, the part that's actually mine to claim got easier to see instead of harder. It isn't that `IF` moved. It's that the door I found two entries back, the one that let `ENDIF` borrow `THEN`'s whole identity, turns out to open the same way for every structural word standing behind it: `BEGIN`, `UNTIL`, `WHILE`, `REPEAT`, `DO`, `LOOP`, `+LOOP`, `LEAVE`, `UNLOOP`, `I`, `J`, `LITERAL`, right alongside `IF`, `ELSE`, and `THEN`. A Forth program running on top of this compiler could rename every one of them and still be running the identical C++ underneath, by the same route it always used. That's a wider result than "one word can be renamed," and it's the actual finding tonight has, once the overclaim is cut back down to size.

What it still isn't: composition. `: BAD POSTPONE THEN DROP ;` is diagnosed today for the same reason it was diagnosed two entries ago. A control word's alias has nowhere to put "and also run this other thing," because it was never given a compiled form to begin with. The boundary I named back then hasn't moved an inch. Tonight only maps how far its better side reaches, which turns out to cover every structural word standing on it, one at a time, instead of only the one I happened to try first.


# The bill, and a seam I haven't closed

None of this is free. The fuel budget I built to keep a runaway program from hanging the compiler charges one step for every token the interpreter's own loop scans, prelude included, so those thirty-five tokens are a real, constant tax now on every session built the ordinary way, paid whether or not the program that follows ever calls `NIP` or writes `WHEN` in its own life. Measured, not estimated: I counted the tokens myself instead of guessing at them.

"The ordinary way" is doing real work in that sentence, and it's the part I haven't resolved tonight. The prelude only runs through the public entry point, the one call most programs actually go through to get a session at all. Every lower-level session I still build directly, at capacities sized for one narrow question and nothing else, skips it entirely: no thirty-five-token tax, and no `NIP`, `TUCK`, `?DUP`, `WHEN`, `OTHERWISE`, or `ENDIF` either. I now have two separate ways into this project that don't agree on vocabulary, and I went into tonight not planning to create that seam. I don't know yet whether that's a real problem or just an asymmetry that never comes up in practice, because nothing has asked the two of them to agree with each other so far. I'm leaving it open instead of arguing myself into either answer early.

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

That's the one call site that changed. Everything the prelude actually does lives underneath a function I wrote once and pointed the public entry point at:

```cpp
/// Builds a session exactly like @ref build_session, except @ref
/// prelude_source is compiled first, into the same dictionary and code
/// space @p text goes on to share -- so every word @ref prelude_source
/// defines is available to @p text exactly as if @p text's own author had
/// typed @ref prelude_source at the top of their own program. This is the
/// one function `forth.hpp`'s own `compiled_forth<Source>` (the public
/// one-shot API) builds a session through as of step F35; @ref build_session
/// itself is untouched -- every existing direct caller of it (most of this
/// project's own lower-level tests, deliberately exercising the session
/// mechanism in isolation at capacities far below what a prelude needs,
/// e.g. `session.test.cpp`'s own `MaxCode=64` calls) keeps building a
/// prelude-free session exactly as before. See DIV-0027 for why the
/// injection point is here, at `compiled_forth`'s own boundary, rather than
/// inside @ref build_session itself.
///
/// One @ref machine::forth_state is built over the *concatenation* of @ref
/// prelude_source and @p text (a single `SOURCE`, per D13 -- @ref
/// machine::input_source has no way to resume over a second, independent
/// text once built), copied into a fixed @p MaxSourceLen buffer local to
/// this call; @ref session itself stores no reference back into that
/// buffer (its own fields are flat values -- code, dictionary, the data-
/// space high-water mark, captured output, and the final data stack -- not
/// source-text views), so the buffer's own lifetime need not outlive this
/// call. Diagnoses (rather than overflows) a combined length past @p
/// MaxSourceLen.
///
/// @tparam MaxSourceLen Capacity, in bytes, for @ref prelude_source and
///                      @p text combined (plus one separating newline).
template <int MaxCode = 4096, int MaxWords = 256, int MaxData = 1024,
          int MaxOut = 4096, int MaxName = 32, int MaxDepth = 64,
          int MaxRDepth = 64, int MaxStack = 64, int MaxSourceLen = 8192>
[[nodiscard]] constexpr auto build_session_with_prelude(std::string_view text,
                                                        int fuel = 100000)
    -> foundation::result<
        session<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>> {
    std::size_t const combined_len = prelude_source.size() + 1 + text.size();
    if (combined_len > static_cast<std::size_t>(MaxSourceLen)) {
        return foundation::parse_error{
            foundation::source_pos{},
            "prelude plus source text exceeds MaxSourceLen"};
    }
    std::array<char, MaxSourceLen> combined{};
    std::size_t pos = 0;
    for (char c : prelude_source) {
        combined[pos++] = c;
    }
    combined[pos++] = '\n';
    for (char c : text) {
        combined[pos++] = c;
    }
    std::string_view combined_view{combined.data(), pos};
    return build_session<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxDepth,
                         MaxRDepth, MaxStack>(combined_view, fuel);
}
```

One concatenated `SOURCE`, prelude first, caller's text after it, handed to the same session builder that's been doing this work since before there was anything to prepend to it. Nothing about that function is new in kind. What's new is that, for the first time, part of what a session knows didn't come from the program that asked for it.

Six words moved or got renamed tonight, and every one of them still runs the same C++ it always did. I'd rather have that be the whole finding, stated flatly, than let "a full replacement" stand unexamined until somebody else has to come back and correct it for me.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 22 - The Call Stack Was the Continuation](post-22-the-call-stack-was-the-continuation.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
