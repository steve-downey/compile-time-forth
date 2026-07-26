<div class="abstract" id="org748cd2e">
<p>
Part 16 ended on a control word with nothing to point at: <code>THEN</code>'s whole
action is mutating the code being built, so there is no address <code>EXECUTE</code>
could ever jump to for it. Tonight most other words get that address.
<code>'</code>, <code>[']</code>, and <code>EXECUTE</code> land, along with <code>CREATE</code>, <code>DOES&gt;</code>, <code>VALUE</code> and
<code>TO</code>, and <code>DEFER</code> and <code>IS</code> &#x2014; and getting there meant finally paying
down a debt from Part 13: <code>SOURCE</code>, <code>&gt;IN</code>, <code>BASE</code>, and <code>STATE</code> move off the
wrapper I built to hold them and onto the machine state itself, because the
alternative was a VM that still could not see the input it needed to read.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 16 - Nothing to Point At ←](post-16-nothing-to-point-at.md)

</nav>


# The debt comes due

Part 13 built `interpreter::forth_state` as a wrapper: the old type, `machine::forth_state`, holding the two stacks and the data space, with `SOURCE`, `>IN`, `BASE`, and `STATE` composed on top of it in a wider type that exposed the old one through a `machine()` accessor. I said at the time that it wasn't where this ends up &#x2014; `apply_primitive` only ever sees the narrower type, which meant no primitive could read or write `>IN`, and the whole argument for `>IN` being a plain offset instead of a cursor was that a parsing word would eventually need to reach it exactly the way the outer loop does. I called that debt and left it standing, because the old R1 pipeline still needed the narrower type to stay narrow and folding it in would have meant the bottom of the dependency stack depending on something defined above it.

The old pipeline is gone &#x2014; Part 15 deleted it &#x2014; so that reason is gone too, and tonight's two central pieces of work both needed the fold directly. `EXECUTE` can jump to any word's own code, and a defining word's body can run `CREATE` or `DOES>` from inside a colon definition that is now pure VM bytecode, with no interpreter-level loop watching it run. Both of those paths go through `machine::run_from`, and `run_from` only ever takes a `machine::forth_state`. A wrapper sitting one layer up is invisible to code running one layer down. So `interpreter::forth_state` is deleted outright, `input_source` relocates into `machine/`, and `SOURCE`, `>IN`, `BASE`, and `STATE` are fields on `machine::forth_state` itself now, next to the stacks and the data space they always should have been sitting beside.

Every accessor name survived the move &#x2014; `source()`, `base()`, `set_base()`, `state()`, `set_state()` &#x2014; so every caller of the old wrapper's own spelling kept working, minus a `.machine()` that used to sit in the middle for no reason a caller could see. That's the entire visible cost of paying down a debt I named four entries ago: a rename, not a redesign.


# An execution token is a place in the code

Forth-2012 says `'` takes a word and produces an execution token; `EXECUTE` takes that token back and runs whatever it names ({Forth 200x Standardisation Committee}, 2014). The standard doesn't say what the token **is**, only what it has to do, and that left a real choice sitting open: tag a value with enough bits to say "this is a primitive, this is a colon word, run it accordingly," or make every resolvable word answer to one uniform representation and let the token be completely opaque about which kind of word it names.

I took the second one, for a reason that turned out to be more concrete than I expected going in: a `CONSTANT`'s value can be any 64-bit cell, and any tagging scheme that reserves bits for a kind discriminator loses precision somewhere in that range. An execution token here is a plain code-space instruction index, and `EXECUTE` pops one and jumps to it exactly the way `op::call` already does &#x2014; push a return address, then jump. No dictionary lookup happens at the VM level, ever; by the time a token exists, the dictionary has already done its job.

A colon word already has a real address to hand back: its own entry point, `ret`-terminated by construction since compilation first started emitting whole words. A primitive, a variable, a constant, or a `VALUE` has no such address &#x2014; nothing about them lives in code space at all &#x2014; so producing a token for one means building a small stub that does: push whatever the word would push, or run the one primitive it names, then `ret`.

```cpp
/// Resolves @p entry to an execution token (D18): a code-space instruction
/// index that @ref machine::op::execute can later jump to exactly like a
/// call, given only @p entry's own binding -- the shared machinery behind
/// both `'` (@ref apply_control_word's own `tick_` case, interpreting) and
/// `[']` (its `bracket_tick_` case, compiling).
///
/// A @ref machine::compiled_colon_word's own entry point already is one (it
/// is `ret`-terminated by construction, F14's discipline): returned directly,
/// no emission. Every other resolvable kind -- @ref machine::primitive, @ref
/// machine::variable_word, @ref machine::constant_word, @ref
/// machine::value_word -- has no standing code-space location of its own, so
/// this function builds a small `ret`-terminated stub for it, unconditionally
/// guarded by a leading @ref machine::op::branch that jumps past the stub
/// (patched to land just after it): @p buf may be positioned *inside* a
/// still-open colon definition's own body when this runs (`'`/`[']` used
/// inside a `[ ... ]` bracket while compiling something else, D13's own
/// bracket-interpreting state) -- appending a stub's own instructions inline,
/// unguarded, would be silently executed as part of that enclosing
/// definition's own body the next time it runs (its own `ret` would return
/// early, corrupting control flow), the same hazard @ref apply_control_word's
/// own `IF`/`WHILE` sentinel-and-patch discipline exists to avoid. The guard
/// costs one extra instruction in the (common) case where @p buf actually was
/// at a safe append point; it is never wrong to pay it.
///
/// Diagnoses if @p entry is a @ref machine::control_word, @ref
/// machine::foreign_word, or @ref machine::defer_word: none has a stable
/// code-space location an XT can usefully name yet (a control word's whole
/// point is that it has no VM-representable form at all; a not-yet-`IS`sed
/// deferred word's *target* does not exist yet either) -- a documented,
/// narrower scope than full Forth-2012 `'`/`[']` (DIV-0016 records this and
/// its own revisit condition).
template <int MaxCode, int MaxBufWords, int MaxName>
[[nodiscard]] constexpr auto
resolve_execution_token(machine::dictionary_entry<MaxName> const &entry,
                        compile_buffer<MaxCode, MaxBufWords> &buf,
                        foundation::source_pos pos) -> foundation::result<int> {
    using machine::cell;
    using machine::op;

    if (auto const *cw =
            std::get_if<machine::compiled_colon_word>(&entry.binding)) {
        return cw->entry_point;
    }

    auto skip = buf.emit(op::branch, cell{-1}, pos);
    if (!skip.has_value()) {
        return skip.error();
    }
    int const stub = buf.here();

    if (auto const *p = std::get_if<machine::primitive>(&entry.binding)) {
        auto r = buf.emit(op::prim, static_cast<cell>(*p), pos);
        if (!r.has_value()) {
            return r.error();
        }
    } else if (auto const *vw =
                   std::get_if<machine::variable_word>(&entry.binding)) {
        auto r = buf.emit(op::push, static_cast<cell>(vw->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        if (vw->does_entry >= 0) {
            auto r2 =
                buf.emit(op::call, static_cast<cell>(vw->does_entry), pos);
            if (!r2.has_value()) {
                return r2.error();
            }
        }
    } else if (auto const *cn =
                   std::get_if<machine::constant_word>(&entry.binding)) {
        auto r = buf.emit(op::push, cn->value, pos);
        if (!r.has_value()) {
            return r.error();
        }
    } else if (auto const *vl =
                   std::get_if<machine::value_word>(&entry.binding)) {
        auto r = buf.emit(op::push, static_cast<cell>(vl->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        auto r2 = buf.emit(op::prim,
                           static_cast<cell>(machine::primitive::fetch), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
    } else {
        return foundation::parse_error{pos, "word has no execution token"};
    }

    auto ret_r = buf.emit(op::ret, cell{0}, pos);
    if (!ret_r.has_value()) {
        return ret_r.error();
    }

    buf.program().code[skip.value()].operand = static_cast<cell>(buf.here());
    return stub;
}
```

The branch in front of every stub is not defensive programming for its own sake. `'` and `[']` can both run in the middle of an unrelated definition's own body, through `[ ... ]` bracket-interpreting &#x2014; Part 13's own `STATE`-toggling mechanism &#x2014; which means the code space can be positioned **inside** a colon word that is still open when a stub needs to be built. Appending the stub's instructions there, unguarded, would make them part of that enclosing definition: its own `ret` would return early the first time it ran, and control flow would be quietly wrong in a way nothing would catch until much later. That is the exact hazard `IF` and `WHILE` already manage with their own sentinel-and-patch discipline, and the fix is the same shape: jump around what you just wrote. It costs one wasted instruction whenever the stub was going to be safe to fall into anyway, which is most of the time. I didn't try to detect the safe case and skip the guard. Getting that detection wrong once is worse than paying for it every time.

`' SQUARED EXECUTE` agrees with calling `SQUARED` directly, at compile time and at runtime both, because both roads end at the same call:

```forth
\ (shape, not verbatim)
: SQUARED DUP * ;  5 ' SQUARED EXECUTE
```

leaves `25`, same as `5 SQUARED` would.


# CREATE and DOES> need a dictionary the VM never had

The classic defining-word idiom is the real target this step had to hit:

```forth
\ (shape, not verbatim)
: CONSTANT2 CREATE , DOES> @ ;  42 CONSTANT2 LIFE  LIFE
```

leaves `[42]`. `CONSTANT2` has to scan a **new** name and install a **new** dictionary entry every time it runs, not once when `CONSTANT2` itself is defined &#x2014; and once `CONSTANT2`'s body is compiled, it runs as ordinary VM bytecode, through `run_from`, with no interpreter-level loop watching each instruction go by. Which meant the VM needed the dictionary reachable from inside its own fetch-execute loop, something it had never once needed before tonight.

That's the one deliberate exception this project now has to "the VM only ever sees a forth\_state": `run_from`, `run`, and `interpreter::call_word` all gained a new, nullable, non-owning dictionary pointer, defaulted to `nullptr` so that not one caller from before this step had to change. Two new opcodes consult it. `create_word` scans a name off the newly-folded `SOURCE` and `>IN` and installs a variable; `does_enter` attaches whatever code follows it to the most recently defined word and then behaves like `ret` itself &#x2014; the defining word's own execution ends right there, so the code after `DOES>` never runs while `CONSTANT2` is being defined, only later, as the word it just created.

```cpp
/// `CREATE`'s own action (step F28, D18/D10): scan the next name off @p
/// state's own SOURCE/>IN, allot @p cells_to_allot cells past the current
/// data-space HERE, and define the scanned name in @p dict as a @ref
/// variable_word at that address, with no does-field yet (@ref
/// variable_word::does_entry defaults to -1; `DOES>` is what later attaches
/// one, via @ref dictionary::attach_does).
///
/// Shared between @ref run_from's own @ref op::create_word case (`CREATE`
/// invoked from inside another word's own compiled body -- a defining word
/// like `: CONSTANT2 CREATE , DOES> @ ;`, once per invocation) and
/// `interpreter::apply_control_word`'s own `create_` case (`CREATE` met
/// directly by the text interpreter, interpreting): both need the identical
/// action, and this is the one place it is written down.
template <int MaxWords, int MaxName, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut>
constexpr auto
create_here(dictionary<MaxWords, MaxName> &dict,
            forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
            int cells_to_allot) -> status {
    auto scanned = parser::scan_word<MaxName>(state.source().cursor_at_in());
    if (!scanned.has_value()) {
        return scanned.error();
    }
    auto const &folded = scanned.value().value;
    if (folded.empty()) {
        return foundation::parse_error{foundation::source_pos{},
                                       "expected a name after CREATE"};
    }
    state.source().set_in(scanned.value().rest.position().offset);
    std::string_view name_text{folded.begin(),
                               static_cast<std::size_t>(folded.size())};
    auto address = state.data_space().allot(cells_to_allot);
    if (!address.has_value()) {
        return address.error();
    }
    return dict.define_variable(
        name_text, variable_word{.address = address.value(), .does_entry = -1});
}
```

`create_here` is shared, verbatim, between the opcode above and the interpreting-time `CREATE` that runs when the text interpreter meets the word directly, outside any definition. Both need the identical action, and I wrote it down once rather than twice &#x2014; the same instinct that already gave `IF` and `THEN` one execute path and one compile path instead of two of each.

The address a defining word installs still needs a does-field to point somewhere, and that's a small, honest addition to the binding it already had:

```cpp
struct variable_word {
    addr address{};
    int does_entry = -1;
};
```

`-1` means an ordinary `VARIABLE` or bare `CREATE`: push the address and stop. Anything else is an instruction index `DOES>` installed, and executing the word pushes the address, then calls straight into that code &#x2014; as if the address had been pushed by hand immediately before an ordinary call. Nothing about calling into a does-field is new machinery; it's `op::call` again, reached from one more place.


# VALUE, DEFER, and cells that don't need the dictionary at all

`VALUE` and `DEFER` could have used the same dictionary-threading trick `CREATE` needed, and I decided against it on purpose. Both bind to one data-space cell instead &#x2014; a plain address, not the word itself:

```cpp
/// A `VALUE`-defined word's binding (step F28): the data-space cell holding
/// its current value. Unlike @ref variable_word, executing a @ref value_word
/// pushes the *contents* of @ref address (`interpreter::execute_entry`'s own
/// case for it), not the address itself -- `TO NAME` is what later changes
/// that content (`interpreter::apply_control_word`'s own `to_` case).
struct value_word {
    addr address{};
};

/// A `DEFER`-defined word's binding (step F28): the data-space cell holding
/// its current target, as an execution token (D18) -- an instruction index
/// in the interpreter's own code space, or `-1` if `IS` has never been used
/// on this word yet. Storing the target in data space rather than on the
/// dictionary entry itself (contrast @ref variable_word::does_entry, which
/// `DOES>` does mutate on the entry directly) means an ordinary compiled
/// reference to a deferred word (`interpreter::compile_entry`'s own case for
/// it: push @ref address, `@`, `EXECUTE`) needs no dictionary access at
/// runtime at all -- executing the unset `-1` sentinel through `EXECUTE`
/// diagnoses cleanly as an out-of-range instruction pointer (D7), and
/// `interpreter::execute_entry`'s own case gives the friendlier "deferred
/// word has no action" diagnosis for the common case of invoking a deferred
/// word directly by name.
struct defer_word {
    addr address{};
};
```

The point of the cell is that a **compiled** reference to a `VALUE` or a word defined with `DEFER`, from inside another word's own body, needs nothing new at all: push the address, `@`, and for a deferred word, `EXECUTE` the result. Three opcodes that already existed, in a sequence the compiler could already emit. `TO` resolves its target's address once, at the moment it's met, and either stores directly while interpreting or compiles the equivalent push-address-and-store while compiling; `IS` pops an execution token and stores it straight into the target's cell, no dictionary touched at all.

Calling a deferred word before `IS` has ever set it is diagnosed, not a silent no-op:

```forth
\ (shape, not verbatim)
DEFER FOO  FOO
```

fails with a message naming the deferred word as having no action yet. A **compiled** call to the same unset word takes a different, less specific road to the same conclusion &#x2014; the sentinel address reaches `EXECUTE` and gets diagnosed as an out-of-range instruction pointer instead. Same guarantee, worse bedside manner, and I'm leaving that asymmetry written down rather than pretending both paths give the same message. Neither one is undefined behavior; that's the part that actually matters.


# The postpone boundary, pinned down

Part 16 ended by naming a limit: `POSTPONE` of a control word worked, but only when the postponed word was the **entire** body of the definition, because a control word has no address for anything to alias. I guessed, at the time, that giving control words a uniform header might close that gap once this step's header-unification work landed. It closes for exactly three of them.

`EXECUTE`, `CREATE`, and `DOES>` all compile to a single, real instruction now &#x2014; `op::execute`, `op::create_word`, `op::does_enter` &#x2014; through the same `compile_entry` every primitive already goes through. Which means `POSTPONE` of any of the three now composes with ordinary code in the same definition, not just stands alone as the whole body:

```forth
\ (shape, not verbatim)
: SQUARED DUP * ;  : RUN-XT DUP POSTPONE EXECUTE ;
6 ' SQUARED RUN-XT
```

`RUN-XT` is an ordinary compiled colon word here, not the control-word alias Part 16 described &#x2014; `DUP` sits in front of `POSTPONE EXECUTE` in the same body, which the whole-body-only version could never have allowed.

The rest of the control words &#x2014; `IF`, `THEN`, `DO`, `LOOP`, and everything else that patches its own branch operand into place &#x2014; still can't do this, and won't. I want to be precise about why, because it isn't that the header work stopped short of reaching them. Giving `THEN` an execution-token slot wouldn't create anything for `EXECUTE` to invoke, because `THEN` doesn't have a runtime action separate from the mutation it performs while compiling. `EXECUTE`, `CREATE`, and `DOES>` always had an action a fixed sequence of opcodes could name &#x2014; that's what made them compilable the moment something bothered to write the case. `THEN`'s entire job is rewriting an operand in the buffer being built, before there is a running program to have deferred the mutation into. There's no later moment to represent as bytecode, because the whole point already happened. That's a structural fact about what these words are, not an unfinished corner of this step's own work, and I'd rather say that plainly than leave it looking like a queue I just haven't gotten to.


# What's still open

Nothing about stack effects changed tonight, same as it didn't last time: the comment after a colon header is still captured off the source text and checked against nothing. `: BAD ( -- ) DROP ;` still compiles clean and still underflows the moment anything calls it. There is still no `PARSE`, no `WORD`, no `S"`, no `."` &#x2014; nothing that reads a run of raw text out of the input stream the way a real parsing word would, even though tonight is the entry that finally gave the input stream a home a primitive could reach. No `CATCH`, no `THROW`. Nothing has been tested against gforth, and there is no Forth-2012 conformance suite anywhere near this tree. All of that was true when Part 16 landed and it's true again tonight; nothing in this entry touches any of it.

What tonight did change: almost everything in this dictionary now has something to point at. The thing that doesn't is no longer a gap waiting to be closed &#x2014; it's the shape of what a control word is.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 16 - Nothing to Point At](post-16-nothing-to-point-at.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
