<div class="abstract" id="orga5fc788">
<p>
Part 13 spent most of an entry defending a choice with no payoff that night:
<code>&gt;IN</code> as a bare integer sitting in the open on the machine state, not a
cursor tucked inside the scanner, because a parsing word would eventually
need to reach it the same way the outer loop does. Part 17 folded that state
onto <code>machine::forth_state</code> itself, for reasons that had nothing to do with
parsing words &#x2014; <code>EXECUTE</code> and <code>DOES&gt;</code> needed it first. Tonight both bills
come due at once. <code>PARSE</code>, <code>WORD</code>, <code>CHAR</code>, <code>[CHAR]</code>, <code>S"</code>, <code>."</code>, <code>ABORT"</code>,
<code>COUNT</code>, and <code>TYPE</code> land, built on one shared scan of the input stream, and
one small definition does the thing Part 0 refused to allow: a word written
in this Forth that reaches past its own definition and takes an argument
that was never inside it.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 17 - Something to Point At ←](post-17-something-to-point-at.md)

</nav>


# Two debts, one payoff

Part 13 put it plainly: a cursor would have been less code, and would have done everything that entry's own outer loop asked of it that night. The `int` won on an argument about a word that didn't exist yet &#x2014; something that reads a run of input and advances `>IN` itself, through the identical interface the outer loop uses, not through some separate mechanism the loop has and a Forth-level word can't reach. I wrote that down as an expectation, not a result, and said plainly it might be the entry that turns out to have been wrong about why.

Part 17 didn't touch that argument at all. It folded `SOURCE`, `>IN`, `BASE`, and `STATE` off the wrapper Part 13 built and onto `machine::forth_state` itself, because `EXECUTE` and `DOES>` needed the input reachable from inside the VM's own fetch-execute loop, with no interpreter-level watcher standing over it. Parsing words weren't the reason for that fold. They just needed it to have already happened.

Tonight is where both land on the same demonstration.


# ECHO-WORD

```cpp
static_assert([] {
    auto out = compiled_forth<": ECHO-WORD 32 WORD COUNT TYPE ;  "
                             "ECHO-WORD FOO">.output();
    return out.size() == 3 &&
           std::string_view{out.begin(),
                            static_cast<std::size_t>(out.size())} == "FOO";
}());

TEST_CASE("ForthTest - UserDefinedParsingWordMergeCriterion") {
    auto out = compiled_forth<": ECHO-WORD 32 WORD COUNT TYPE ;  "
                             "ECHO-WORD FOO">.output();
    REQUIRE(out.size() == 3);
    CHECK(std::string_view{out.begin(), static_cast<std::size_t>(out.size())} ==
          "FOO");
}
```

`ECHO-WORD`'s own definition is three words: `32 WORD COUNT TYPE`. `FOO` appears nowhere in it. `FOO` shows up only after `ECHO-WORD` is called, and what prints is `FOO` anyway &#x2014; `ECHO-WORD` reached into the input stream while it was running and took the next word for itself. That's a user-defined parsing word: a word that extends how the text after it gets read, written in ordinary Forth, with nothing in the interpreter that knows `ECHO-WORD`'s name.

Nothing does know it. `WORD` is an ordinary, non-immediate primitive, no different in kind from `DUP` or `@`: a colon definition that calls it compiles a plain `op::prim`, D13's dispatch rule from several entries back, unchanged. When `ECHO-WORD` later runs, that instruction fires and consumes whatever text `SOURCE` and `>IN` happen to be sitting on at that exact moment &#x2014; which, at the call site above, is `FOO`. Being ordinary and non-immediate is the entire trick. An immediate word runs at compile time, against whatever the source looks like while `ECHO-WORD` itself is being defined; `WORD` runs at `ECHO-WORD`'s own call time, against whatever the source looks like then. Nothing about `interpret`'s own loop had to change for this to work, because from the loop's point of view `ECHO-WORD` is a word like any other, and calling a word like any other is all `interpret` has ever done.

```cpp
// Step F29 (docs/forth-plan-2.md), D19/D21: parsing words and strings.
// Every one of these five reads or writes @p state's own @ref source
// and/or @ref data_space directly -- no dictionary access, so each is a
// real primitive rather than another `interpret()`-level special case,
// exactly as D19 calls for. `PARSE`/`WORD`/`CHAR` are ordinary
// (non-immediate) words: compiled as an @ref op::prim like `DUP` inside
// a colon definition, so a user-defined parsing word that calls one of
// them consumes whatever `SOURCE`/`>IN` are *when that word later runs*,
// not at the point it was defined -- this is the whole demonstration
// D19 exists for. `S"`/`."`/`ABORT"` (`interpreter::apply_control_word`,
// interp.hpp) are immediate control words instead, since each must
// consume its own string literal at the moment it is met; `TYPE`/
// `COUNT` are what they, and any user-defined word, read the result
// back with.
parse,       ///< `PARSE` ( char "ccc<char>" -- c-addr u ) Copies the
                 ///< text up to the next occurrence of @c char (or end of
                 ///< input) into freshly allotted data-space cells
                 ///< (D21: one cell per character) and advances `>IN` past
                 ///< the delimiter, if found.
word,        ///< `WORD` ( char "<chars>ccc<char>" -- c-addr ) Like
                 ///< @ref parse, but first skips leading occurrences of
                 ///< @c char, and stores the result as a counted string (the
                 ///< character count in the first cell, the characters
                 ///< following) rather than as a separate address/length
                 ///< pair.
char_,       ///< `CHAR` ( "<spaces>name" -- char ) Skips leading
                 ///< whitespace, reads the next blank-delimited name, and
                 ///< pushes the character code of its first character.
count,       ///< `COUNT` ( c-addr1 -- c-addr2 u ) Reads the count cell a
                 ///< @ref word-built counted string starts with; @c c-addr2
                 ///< is @c c-addr1 `+ 1` and @c u is that count.
type_,       ///< `TYPE` ( c-addr u -- ) Prints @c u characters, read as
                 ///< cells (D21) starting at @c c-addr, via
                 ///< @ref forth_state::emit_char.
abort_quote, ///< `ABORT"` ( flag c-addr u -- ) Compiled form of
                 ///< `ABORT"`'s runtime action (see
                 ///< `interpreter::apply_control_word`'s own `abort_quote_`
                 ///< case, which compiles the message text ahead of a call
                 ///< to this primitive): if @c flag is nonzero, prints the
                 ///< @c u characters at @c c-addr (exactly like @ref type_)
                 ///< and then diagnoses -- a hard stop of the whole
                 ///< interpretation, not yet the `THROW -2` Forth-2012
                 ///< actually calls for (DIV-0017: `THROW`/`CATCH` do not
                 ///< exist until F31). If @c flag is zero, does nothing.
```

Every one of these reads or writes `state`'s own `source()` and `data_space()` directly, both plain fields since Part 17's fold, so each is a real `machine::primitive` enumerator dispatched through `apply_primitive` &#x2014; no dictionary lookup, no new opcode, nothing `ECHO-WORD`'s own caller had to set up in advance.


# One scan underneath all of them

`PARSE`, `WORD`, and the reexpressed `(~/~S"~/`."`/~ABORT"` below all read through the same function, differing only in the delimiter, whether leading delimiters get skipped first, and what each does with the text once it has it:

```cpp
/// The result of @ref scan_delimited: the delimited run's own raw text
/// (sliced directly out of the scanned cursor's underlying view, no
/// case-folding, unlike @ref scan_word) and the cursor positioned just past
/// it -- past the trailing delimiter if @ref found_delim, or at end of
/// input otherwise. This is Forth-2012 `PARSE`'s own `>IN` update either
/// way, and every parsing word this project defines computes it the same
/// way (D19).
struct delimited_scan {
    std::string_view text{};  ///< The scanned run, excluding the delimiter.
    cursor rest{cursor{""}};  ///< Positioned just past the run (see above).
    bool found_delim = false; ///< False if input ran out before @p delim.
};

/// Scans @p cur for a run of characters delimited by @p delim -- Forth-2012
/// `PARSE`'s own definition, and D19's shared "below the word" scanning
/// primitive every parsing word this project defines is built from. If
/// @p skip_leading, first skips any leading occurrences of @p delim (`WORD`'s
/// own extra step over plain `PARSE`); then collects every character up to
/// the next occurrence of @p delim, or to the end of input, whichever comes
/// first.
///
/// Never fails: running out of input before finding @p delim is an ordinary
/// outcome for `PARSE`/`WORD` (@ref delimited_scan::found_delim reports it,
/// for callers -- `(`, `S"`, `."`, `ABORT"` -- that choose to diagnose it as
/// an unterminated form instead).
///
/// `PARSE`, `WORD`, `S"`, `."`, `ABORT"`, and the reexpressed `(` (step F29,
/// D19's own demonstration that a parsing word needs nothing but the input
/// stream) all consume the same stream through this one scan, differing
/// only in @p delim, @p skip_leading, and what each does with the resulting
/// @ref delimited_scan::text afterward -- which is what makes a user-defined
/// parsing word written in ordinary Forth, calling `PARSE` or `WORD`
/// directly, work identically to any of this project's own built-in ones.
[[nodiscard]] constexpr auto scan_delimited(cursor cur, char delim,
                                            bool skip_leading)
    -> delimited_scan {
    if (skip_leading) {
        while (!cur.empty() && cur.peek() == delim) {
            cur = cur.bump();
        }
    }
    auto const text_begin = cur;
    int len = 0;
    while (!cur.empty() && cur.peek() != delim) {
        cur = cur.bump();
        ++len;
    }
    bool const found = !cur.empty();
    std::string_view text =
        text_begin.remaining().substr(0, static_cast<std::size_t>(len));
    return delimited_scan{text, found ? cur.bump() : cur, found};
}
```

`PARSE` calls it with no leading skip and hands the raw text back as an address and a length &#x2014; Forth-2012's own `( -- c-addr u )` shape ({Forth 200x Standardisation Committee}, 2014). `WORD` skips leading delimiters first and stores a **counted** string instead, the count in the first cell and the characters after it, because that's what `COUNT` expects to turn back into an address/length pair. Both copy their result into freshly allotted data-space cells, one character per cell &#x2014; this project's declared address-unit convention since the data space first existed &#x2014; rather than some fixed scratch buffer. That means a loop that calls `WORD` a hundred times allocates a hundred times, bounded by the same data-space capacity that already bounds `CREATE` or a bare `,`, and diagnosed the same way if it runs out. No new capacity, no new failure mode, just the one this project already had.


# ( and \\ stop being special

Through the last several entries, `(` and `\` were never words at all. `skip_forth_space`, down in the scanner, swallowed a comment as part of skipping ordinary whitespace, before anything got far enough to ask what kind of token it was looking at. Tonight that function is gone, deleted outright, not left in place as an unused alternative. `(` and `\` are ordinary dictionary entries now, immediate and not compile-only, each scanning forward from `SOURCE` through the shared function above &#x2014; `)` or end of line as the delimiter &#x2014; and advancing `>IN` past what it finds, exactly like any other parsing word this entry adds.

There's a real cost to this, and I'd rather say it than let it hide in a changelog: a comment can no longer sit between `:` and the name it's defining. `: ( oops ) NAME` used to skip the parenthetical and define `NAME`; now it defines a word called `(`. That is what a raw, undecorated name-parse does in any Forth-2012 system, so this is conformance, not a regression against the standard &#x2014; but it's a real behavior change against what this project did as recently as last entry, and no amount of "that's what the standard says" makes existing source using the old habit keep working.


# ABORT" is honestly incomplete

`ABORT"` compiles a message, and if the flag on the stack when it runs is non-zero, it's supposed to print that message and unwind to the nearest exception handler &#x2014; Forth-2012's own `THROW -2`. There is no `THROW` in this system yet, and there is no handler for one to unwind to. So `ABORT"`'s runtime primitive prints the message, the same way `TYPE` would, and then returns a diagnosed error that propagates all the way out of the interpreter. The whole program stops. That is a hard stop standing in for an unwind, not an unwind, and I don't want to describe it as more finished than it is. The message is real, printing it is a real, testable side effect, and interpretation does stop rather than silently continuing past a condition that was supposed to abort something &#x2014; but "something" was supposed to be nameable, and right now it's everything. That's a deliberate placeholder with a name and a reason, not a corner I forgot.


# What's still open

Nothing here touches stack-effect checking, which has had none of any kind since the tree that removed the old checker along with the rest of the R1 pipeline. `: BAD ( -- ) DROP ;` still compiles clean and still underflows the instant anything calls it. There's still no `CATCH`, no `THROW`, no sender/receiver backend, no differential testing against gforth, and no Forth-2012 conformance suite anywhere near this tree. All of that was true last entry and is true again tonight.

What's different is that the thing Part 0 refused to build &#x2014; a word defined in this Forth that changes how the text after it gets read &#x2014; now has one working example. `ECHO-WORD` isn't a demonstration wired up to look like the real thing. It's ordinary compiled Forth, calling an ordinary primitive, that happens to read the input stream because that's what the primitive does. Part 13's bet was that keeping `>IN` in the open would matter to something that didn't exist yet. Tonight it did.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 17 - Something to Point At](post-17-something-to-point-at.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
