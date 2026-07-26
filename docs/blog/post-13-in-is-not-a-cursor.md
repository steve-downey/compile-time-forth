<div class="abstract" id="org5ebb826">
<p>
Part 12 argued that the outer interpreter I refused to build in Part 0 was
never actually in conflict with anything this project needed, and left it at
that &#x2014; no code changed, the tree was still there. Tonight there's something
on the other side of the argument to check it against: <code>interpreter::forth_state</code>,
which adds <code>SOURCE</code>, <code>&gt;IN</code>, <code>BASE</code>, and <code>STATE</code> to the machine state from Part
5, and <code>interpret</code>, the Forth-2012 section 3.4 outer loop itself, running
over it. There's no <code>:</code> yet, so <code>STATE</code> never leaves 0 today. The one design
choice I want to defend before anyone else sees it: <code>&gt;IN</code> is a bare integer
offset sitting in the open, not a cursor buried inside the scanner, and
tonight that looks like more code for no visible benefit.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 12 - The Patch Stream Was Already There ←](post-12-the-patch-stream-was-already-there.md)

</nav>


# The loop that runs

Feed `interpret` the text `1 2 + .` and what comes back is a state with an empty data stack and the string `"3 "` sitting in its output buffer. That's the whole outer interpreter, doing the whole job an outer interpreter does: scan one word, decide what it is, act on it, and go around again.

The deciding is three questions, asked in order, against a dictionary searched newest-first so a redefinition shadows whatever it replaces. Is the word a primitive already in the dictionary? Run it, through the same `apply_primitive` that has been there since the machine substrate landed &#x2014; unchanged, because an outer interpreter calling a primitive and a tree-walking pass calling the same primitive are, from the primitive's own point of view, indistinguishable callers. Is it a number in the current `BASE`? Push it. Neither? Diagnose it, at the position where the word itself starts, not the position of whatever ran before it.

```cpp
/// The Forth-2012 section 3.4 outer text interpreter, interpret state only
/// (D13): scans one word at a time from @p st's own @ref input_source,
/// looks it up in @p dict (newest-first, so redefinition shadows), and
/// either runs it (a primitive, via @ref machine::apply_primitive), pushes
/// it (a number per @p st's own `BASE`), or diagnoses it (an unknown word,
/// positioned at the word's own start) -- repeating until @p st's source is
/// exhausted or the first diagnosed error, whichever comes first.
///
/// `\` line comments and `( ... )` comments are ordinary intertoken space to
/// this loop (@ref reader::skip_forth_space, invoked here via @ref
/// reader::scan_word's own leading skip): they are consumed without ever
/// becoming a token. There is no `:` yet (F25), so every dictionary hit this
/// step can actually reach is a @ref machine::primitive; a non-primitive
/// binding (impossible from @ref machine::default_dictionary today, but not
/// impossible for a caller-supplied @p dict) is diagnosed rather than
/// silently skipped, per D7.
///
/// Every stack/data-space misuse a primitive's own @ref
/// machine::apply_primitive can diagnose (stack underflow/overflow,
/// division by zero, an out-of-bounds `@`/`!`, ...) is returned here
/// unchanged -- the same @ref foundation::parse_error value @ref
/// machine::apply_primitive itself would have produced, not a re-diagnosed
/// or repositioned one, since the primitive's own diagnosis already carries
/// whatever position information it is ever going to carry (D7's machine
/// substrate has no notion of source position of its own).
///
/// @tparam MaxDepth  @p st's data stack capacity, in cells.
/// @tparam MaxRDepth @p st's return stack capacity, in cells.
/// @tparam MaxData   @p st's data space capacity, in cells.
/// @tparam MaxOut    @p st's output buffer capacity, in characters.
/// @tparam MaxWords  @p dict's capacity, in entries.
/// @tparam MaxName   Maximum word-name/token length, in characters; shared
///                    between @p st and @p dict (see @ref forth_state).
/// @param  st   The interpreter state to run against; mutated in place --
///              its stacks, output buffer, and `>IN` all advance as the loop
///              runs, even on the run that ends in a diagnosed error.
/// @param  dict The dictionary to resolve words against.
/// @param  fuel The interpreter loop's own step budget (@ref
///              consume_interp_fuel): decremented once per token processed;
///              exhaustion is a diagnosed error, never a hang.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxWords,
          int MaxName = 32>
constexpr auto
interpret(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName> &st,
          machine::dictionary<MaxWords, MaxName> const &dict, int fuel = 100000)
    -> machine::status {
    for (;;) {
        auto pre = st.source().cursor_at_in();
        auto token_start = reader::skip_forth_space(pre);
        if (token_start.empty()) {
            // Nothing left but whitespace/comments: a clean end of source,
            // not an error.
            return std::monostate{};
        }

        auto budget = consume_interp_fuel(fuel, token_start.position());
        if (!budget.has_value()) {
            return budget;
        }

        auto scanned = reader::scan_word<MaxName>(pre);
        if (!scanned.has_value()) {
            // Unreachable in practice: token_start non-empty means at least
            // one is_word_char run exists at that position, so scan_word's
            // own some<> cannot fail here. Diagnosed defensively, per D7,
            // rather than assumed away.
            return scanned.error();
        }

        auto const &token = scanned.value().value;
        st.source().set_in(scanned.value().rest.position().offset);

        std::string_view text{token.begin(),
                              static_cast<std::size_t>(token.size())};

        auto const *entry = dict.lookup(text);
        if (entry != nullptr) {
            auto const *op = std::get_if<machine::primitive>(&entry->binding);
            if (op == nullptr) {
                return foundation::parse_error{
                    token_start.position(),
                    "word is not executable yet (F24: primitives only)"};
            }
            auto r = machine::apply_primitive(*op, st.machine());
            if (!r.has_value()) {
                return r;
            }
            continue;
        }

        if (is_number_token_in_base(text, st.base())) {
            auto r = st.machine().data().push(
                token_to_cell_in_base(text, st.base()));
            if (!r.has_value()) {
                return r;
            }
            continue;
        }

        return foundation::parse_error{token_start.position(), "unknown word"};
    }
}
```

`\` and `( ... )` comments never become tokens at all &#x2014; they're consumed as intertoken space by the same scan that skips ordinary blanks, so the loop above never has to know a comment happened. And every stack or data-space misuse a primitive can diagnose &#x2014; underflow, overflow, division by zero, an out-of-bounds `@` or `!` &#x2014; comes back out of `interpret` as the exact same error value `apply_primitive` would have produced calling it directly. Not re-diagnosed, not repositioned. The loop is a caller, and a boring one.

There's no `:` in this entry, which means `STATE` exists on the state object and has never once been anything but 0. I'm not going to pretend that's more than it is: a field added because Part 5's `SOURCE`, `>IN`, `BASE`, and `STATE` were meant to arrive together, sitting there unused until something writes to it.


# >IN, in the open

Here's the design choice I want to defend before it has a payoff. `input_source` is Forth's `SOURCE` and `>IN` pair ({Forth 200x Standardisation Committee}, 2014), and `>IN` is stored as a plain `int` offset into the program text, not as a `parser::cursor`.

```cpp
/// The input source an interpreter loop scans: @ref text is the whole
/// program text (Forth's `SOURCE`), and @ref in is the byte offset of the
/// next character not yet consumed (Forth's `>IN`).
class input_source {
  public:
    constexpr input_source() = default;

    /// Constructs an input_source positioned at the start of @p text.
    constexpr explicit input_source(std::string_view text) : text_{text} {}

    /// `SOURCE`: the whole program text this input_source was constructed
    /// over. Never mutated after construction.
    [[nodiscard]] constexpr auto text() const -> std::string_view;

    /// `>IN`: the byte offset, from the start of @ref text, of the next
    /// character not yet consumed.
    [[nodiscard]] constexpr auto in() const -> int;

    /// Sets `>IN` directly. What the interpreter loop does after consuming
    /// a token, and what a parsing word (`WORD`, `PARSE`, F29) does to claim
    /// or release input for itself.
    constexpr auto set_in(int value) -> void;

    /// A @ref parser::cursor positioned at `>IN`, built by replaying
    /// @ref parser::cursor::bump from the start of @ref text. `>IN` is
    /// stored as a bare offset rather than a cursor (see this header's own
    /// top comment), so this replay is what keeps a cursor's line/column
    /// bookkeeping correct for diagnostics without duplicating that
    /// bookkeeping in a second place here.
    [[nodiscard]] constexpr auto cursor_at_in() const -> parser::cursor;

  private:
    std::string_view text_{};
    int in_{0};
};

constexpr auto input_source::text() const -> std::string_view { return text_; }

constexpr auto input_source::in() const -> int { return in_; }

constexpr auto input_source::set_in(int value) -> void { in_ = value; }

constexpr auto input_source::cursor_at_in() const -> parser::cursor {
    parser::cursor cur{text_};
    for (int i = 0; i < in_ && !cur.empty(); ++i) {
        cur = cur.bump();
    }
    return cur;
}
```

A cursor would have been less code and would have done everything `interpret` itself asks of it tonight: `cursor_at_in` exists only to rebuild one, by replaying `bump` from the start of the text whenever a diagnostic needs a line and column, which is a worse way to get a cursor than just keeping one. Locally, this is the wrong call.

The reason isn't local. `>IN` being an ordinary, readable, writable `int` on the state &#x2014; instead of a cursor's private bookkeeping, reachable only from inside whatever function currently holds it &#x2014; is what a user-defined parsing word is going to need: something that reads a run of input and advances `>IN` itself, through the identical interface the outer loop above uses, not through some separate mechanism the loop has and a Forth-level word can't reach. Nothing in this codebase is that word yet. I'm writing this down as an expectation I'm building toward, not a result I have. If it doesn't pan out, this is the entry that was wrong about why.


# Numbers, per BASE

`FF .` prints `255` once `BASE` is set to 16. `-A .` prints `-10` at the same `BASE`, the sign handled the same way it always has been. Both are the old decimal-only number recognizer from several entries back, generalized: `is_number_token_in_base` and `token_to_cell_in_base` treat `A` through `Z` as digits 10 through 35, so any base from 2 to 36 falls out of the same two functions instead of needing one apiece.

Letters work as digits without a separate lowercase branch because every token reaching this classifier has already been folded to uppercase by the scanner, a decision made for entirely different reasons a long way back. That's the kind of thing I like finding: two pieces of the lexical layer built for unrelated purposes turn out to compose for free.

The old adversarial cases move over intact and stay adversarial: `-1` is a number, `1-` is a word, `-` is neither, all checked by `static_assert` against the original decimal-only functions at base 10 so the new classifier can't quietly disagree with the old one on the cases that made it worth writing carefully the first time.


# A budget of its own

`interpret` takes its own fuel argument, spent one unit per token, separate from the VM's budget and separate from the direct evaluator's. Neither of those two would ever notice a source that is nothing but an unbroken run of valid tokens &#x2014; none of them ever touching a stack hard enough to trip either budget, all of them perfectly legal, forever. Something has to notice that on its own, or the constant evaluator just runs past whatever its own limits are with no diagnosis pointing at why. Fuel has been unconditional in this project since the machine substrate arrived; today it just has one more place to live.


# What's unchanged

None of this replaced anything. The tree, the grammar, the elaborator, the direct evaluator &#x2014; all of it still builds, all of it still passes, 237 tests deep. `interpreter` is a new front end growing in beside the old one, not instead of it, and `machine::apply_primitive` is the one piece of machinery both fronts run through unmodified. Whatever gets decided about the old pipeline is a decision for later entries, and this one isn't making it.


# A wrapper I don't expect to keep

One more thing worth being straight about, because it's a real seam and not a stylistic one. The state object above isn't `machine::forth_state` with three new fields on it. It's a new, wider type that wraps the old one as a private member and exposes it through a `machine()` accessor. I built it that way on purpose: `machine::forth_state` is still what the VM, the direct evaluator, and the one-shot API all consume today, and none of those three sit above the interpreter in this codebase's own layering &#x2014; they sit below it. Growing `machine::forth_state` in place to hold an input source would mean the bottom of the stack depending on a type defined above it, which nothing else here does, anywhere.

That's a real reason, and it's also not where this ends up, and I'd rather say so tonight than let the wrapper quietly harden into the design. Every primitive runs through `apply_primitive`, and `apply_primitive` only ever sees the narrower `machine::forth_state` &#x2014; never the wrapper, never the input source riding along beside it. Which means, as written, no primitive can ever read or write `>IN`. And the entire argument two sections up for why `>IN` is a bare offset instead of a cursor was that a parsing word needs to reach it the same way this loop does. A parsing word that can't be a primitive can't keep that promise. `SOURCE`, `>IN`, `BASE`, and `STATE` belong down in the machine layer, not composed on top of it; they're wrapped tonight because the old pipeline still needs that layer to stay narrow, and they'll want folding down once it doesn't. I'm recording that here as debt I can already see, not as a decision I've made peace with.


# What I haven't done

No `:`, no immediate words, no `POSTPONE`. `STATE` is a field that has never held anything but 0. The old pipeline is untouched, on purpose, and I don't yet know what an abstract interpretation over emitted instructions &#x2014; if that's even the shape it takes &#x2014; will need to recover what the tree-pass checker could already see. Part 12 argued the refusal from Part 0 was never load-bearing. Tonight is the first entry with anything on the other side of that argument to point at, and what's on the other side is one loop, running one word at a time, in a language that still can't define a word of its own.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 12 - The Patch Stream Was Already There](post-12-the-patch-stream-was-already-there.md) | [Part 14 - Correct by Accident →](post-14-correct-by-accident.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
