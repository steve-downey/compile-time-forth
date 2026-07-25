<div class="abstract" id="org005e43f">
<p>
<code>VARIABLE</code>, <code>CONSTANT</code>, and <code>CREATE</code> have named an address since elaboration
first existed, and until now nothing ever read or wrote through one. This entry
wires <code>@</code>, <code>!</code>, <code>+!</code>, and <code>ALLOT</code> into both backends &mdash; the direct evaluator
and the compiled stack machine &mdash; so an address baked in at compile time
finally does something at runtime, and running off the end of it is a
diagnosed error, not undefined behavior. Getting there also meant admitting a
decision I had already made two steps back without noticing: this Forth's data
space has never had bytes in it, only cells, and I am not building the words a
standard Forth needs to tell the difference.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 9 - The One-Shot API, and the Summit Not Yet Reached ←](post-9-one-shot-api.md)

</nav>


# An address that has never done anything

`VARIABLE X` has worked, in the sense of compiling and elaborating without complaint, for several entries now. It resolves `X` to a dictionary entry carrying an address, and every place that address gets used &mdash; `X @`, `X !`, plain old `X` pushing itself &mdash; lowers to a literal push of that address at elaboration time. What it has never done is name anything real. There was no runtime storage behind it, no `@` or `!` to read or write through it, and `CREATE`'s cousin `ALLOT` did not exist as a word at all. A `VARIABLE` was a number with a good name.

This step gives the number somewhere to point.


# Four ordinary primitives

The mechanism is almost disappointingly plain, which is the best kind of plain. `@`, `!`, and `+!` are three new entries in `machine::primitive` &mdash; `fetch`, `store`, `plus_store` &mdash; named the same way every arithmetic primitive already is, plus `allot` for the fourth. All four dispatch through `apply_primitive` exactly like `+` or `DUP` always have:

```cpp
// Memory (D10): operate on @p state's own @ref data_space; step F16 is
// what first wires these (no runtime behavior existed for them before).
// `VARIABLE`/`CONSTANT`/`CREATE` addresses are cells on the data stack
// (D10), so @ref fetch/@ref store/@ref plus_store convert them back to
// @ref addr at the boundary rather than accepting an @ref addr directly.
fetch,      ///< `@`  ( a-addr -- x )   Reads the cell at @c a-addr.
store,      ///< `!`  ( x a-addr -- )   Writes @c x to the cell at
                ///< @c a-addr.
plus_store, ///< `+!` ( n a-addr -- )  Adds @c n to the cell at
                ///< @c a-addr.
allot       ///< `ALLOT` ( n -- ) Reserves @c n more cells past @ref
                ///< data_space::here.
```

There is no new node kind in the elaborated core for any of this and no new opcode in the instruction set. A memory access is, mechanically, just another primitive that happens to touch the data space instead of only the data stack:

```cpp
case primitive::fetch: {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        auto v = state.data_space().fetch(addr{a.value()});
        if (!v.has_value()) {
            return v.error();
        }
        return push_cell(v.value());
}
case primitive::store: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [value, address] = operands.value();
        return state.data_space().store(addr{address}, value);
}
case primitive::plus_store: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [delta, address] = operands.value();
        auto current = state.data_space().fetch(addr{address});
        if (!current.has_value()) {
            return current.error();
        }
        return state.data_space().store(addr{address}, current.value() + delta);
}
case primitive::allot: {
        auto count = pop_one();
        if (!count.has_value()) {
            return count.error();
        }
        auto r = state.data_space().allot(static_cast<int>(count.value()));
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
}
```

`fetch` and `store` take a cell off the data stack and convert it to an `addr` right at the boundary, because that is what an address has always been on the data stack &mdash; a plain cell, the same as any number. `+!` is written as a fetch and a store rather than a third arena operation, since that is what it is.

`ALLOT` is the one of the four I went back and forth on. The plan groups it with `@`, `!`, and `+!`, which reads as a hint, but hints are not decisions. I could have made it a new grammar production, parsed and handled the way `VARIABLE` and `CREATE` themselves are. Instead it is an ordinary primitive: it pops a cell count and calls the data space's own `allot` at runtime, against whichever backend happens to be executing. This is not a distinction without a difference. It means `CREATE BUF 4 ALLOT` works even though the elaboration of `CREATE` itself allots nothing for `BUF` &mdash; the four cells past it show up only when `ALLOT` actually runs. And it means `ALLOT`'s stack-effect entry in the Part 6 checker is the unremarkable `( n -- )`, not the input-dependent case `?DUP` needed. A word that reserves memory turns out to have a perfectly ordinary, statically known effect on the stack that asks for it.


# The gap a fresh state doesn't know about

Wiring the four primitives was not the part that made me stop and think. This was:

A `forth_state` built fresh &mdash; which is every `forth_state`, since neither backend keeps one around between runs &mdash; starts with a data space whose high-water mark is zero. But a `VARIABLE X`'s address was computed during elaboration, against the compiled unit's own data space, which had already allotted a cell for `X` by the time elaboration finished. Run the program and that address is a literal sitting in the compiled output, pointing at a cell that, as far as the fresh runtime data space knows, was never reserved. The address is real. The storage behind it, from the new state's point of view, is not yet.

Both backends close that gap the same way, and it is one line each. The direct evaluator seeds its state before it evaluates a single node:

```cpp
/// Evaluates @p unit's whole top-level program body (F13's entry point):
/// builds a fresh @ref forth_state, seeds its data space from @p unit's own
/// (F16, D10 -- every `VARIABLE`/`CREATE` address elaboration already baked
/// into `unit.program` is valid from the first instruction onward), runs
/// every top-level item through @ref eval_body under @p fuel, and returns
/// the resulting state (its data stack, return stack, data space, and output
/// buffer all reflect the program's full run) or the first diagnosed error
/// -- a stack/return-stack misuse, an out-of-bounds/exhausted data-space
/// access, a division by zero, an output-buffer overflow, or budget
/// exhaustion (@ref consume_fuel), any of which halts evaluation immediately
/// rather than continuing in an inconsistent state.
///
/// A bare top-level `EXIT` cannot actually reach here: elaboration itself
/// already diagnoses `"EXIT outside a definition"` (F11's
/// `elaborate_word_ref`), so no `core_exit` node can exist anywhere in
/// `unit.program`. @ref eval_signal::exited is still a possible return from
/// @ref eval_body in principle (an `EXIT` nested inside a top-level `IF`,
/// say -- elaboration only forbids a *bare* top-level `EXIT`, not one nested
/// inside a top-level control structure), and is simply treated the same as
/// @ref eval_signal::normal here: the top-level program has no further body
/// of its own to skip past either way.
///
/// @tparam StackDepth  Data stack capacity (@ref forth_state).
/// @tparam RStackDepth Return stack capacity (@ref forth_state).
/// @tparam MaxOut      Output buffer capacity (@ref forth_state).
/// @param  unit A successfully elaborated program (F11/F12): every
///              definition in it has already passed stack-effect analysis,
///              per D9 -- @ref eval_program never sees a `compiled_unit`
///              that failed elaboration.
/// @param  fuel The evaluation step budget: decremented once per core node
///              visited (@ref consume_fuel); exhaustion is a diagnosed
///              error, never a hang, even for a nonterminating
///              `BEGIN ... UNTIL`.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings, int StackDepth = 64, int RStackDepth = 64,
          int MaxOut = 256>
constexpr auto
eval_program(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                       MaxData, MaxWarnings> const &unit,
             int fuel = 100000)
    -> foundation::result<
        forth_state<StackDepth, RStackDepth, MaxData, MaxOut>> {
    forth_state<StackDepth, RStackDepth, MaxData, MaxOut> state{};
    // F16: seed state's own data space with the same high-water mark
    // unit.data_space reached during elaboration (every VARIABLE/CREATE
    // allotment), so core_var addresses already baked into unit.program are
    // valid for @/!/+! from the first instruction onward; a fresh state's
    // data_space always starts at high-water mark 0 (data_space.hpp), and
    // this reuses allot itself rather than adding a second way to advance
    // it, so it can never disagree with an ordinary runtime ALLOT's own
    // bookkeeping.
    auto data_init = state.data_space().allot(unit.data_space.size());
    if (!data_init.has_value()) {
        return data_init.error();
    }
    auto outcome = eval_body(unit, unit.program, state, fuel);
    if (!outcome.has_value()) {
        return outcome.error();
    }
    return state;
}
```

The VM does the same thing before its fetch-execute loop takes its first step:

```cpp
// F16: seed state's own data space with program.data_space_size -- the
// high-water mark the source compiled_unit's data space reached during
// elaboration (every VARIABLE/CREATE allotment) -- so the addresses
// codegen already inlined as push literals are valid for @/!/+! from the
// first instruction onward. Reuses allot itself (discarding the
// returned address) rather than adding a second way to advance the
// high-water mark, exactly like eval_direct.hpp's eval_program does for
// the direct evaluator.
auto data_init = state.data_space().allot(program.data_space_size);
if (!data_init.has_value()) {
        return data_init.error();
}
```

Both calls reuse `allot` itself, throwing away the address it returns and keeping only the side effect of moving the high-water mark. I could have written a second function that just sets the mark directly &mdash; it would have been one field assignment instead of a call through a result type &mdash; but then there would be two ways to advance the same number, and the day they disagree is a bug I would have written myself. Going through `allot` means a runtime `ALLOT` later in the same program extends the exact same arena from exactly where seeding left off, and `BUF` from a `CREATE` stays a valid base address for whatever a following `ALLOT` reserves past it, because there was never a separate bookkeeping path for either one to fall out of sync with.

```forth
VARIABLE X  5 X !  X @ 3 + X !  X @   \ leaves [8]
7 CONSTANT LUCKY  LUCKY LUCKY +       \ leaves [14]
```

Run both lines and the stack ends at `[8, 14]`, through the direct evaluator, through the compiled VM, and through the one-shot public API from last entry &mdash; `forth.hpp` itself needed no change at all, since seeding lives entirely inside the VM's own `run`, which every one of `forth_program`'s methods already calls for a fresh state.

```forth
CREATE BUF 4 ALLOT
10 BUF ! 20 BUF 3 + ! BUF @ BUF 3 + @ +   \ leaves [30]
```

`BUF` is a base address across all four cells `ALLOT` actually reserved for it, not just the one `CREATE` named.

And an out-of-bounds `@`, `!`, or `+!` &mdash; an address past what has actually been allotted, whether that shortfall happened at elaboration time or is only now showing up at runtime &mdash; is diagnosed through the same result channel the data space's own `fetch` and `store` have used since they were written, several entries ago. I added no new bounds-checking of any kind this step. I only added the first callers able to actually reach the checks that were already sitting there.


# The address unit I never had

Somewhere in the middle of writing the merge criterion, I went looking for `CELLS`. A standard Forth addresses its data space in address units &mdash; conventionally bytes &mdash; so `ALLOT` reserves address units, not cells, and a program has to say `10 CELLS ALLOT` to reserve ten cells' worth of storage on a byte-addressed system. `CELLS`, `CELL+`, `CHARS`, and `CHAR+` exist precisely to do that conversion.

There is nothing to convert here, and there never has been. The data space this Forth uses was written several steps ago as a cell-granular arena from the start: `allot(count)` reserves whole cells, an address is an index into that array of cells, and `fetch` and `store` read and write one cell per address. The cell-granular choice was made the moment that type was written; this step is only the first one that lets a Forth program actually observe it, because this step is the first time `ALLOT`, `@`, `!`, and `+!` are reachable words at all. Before now it was an implementation detail of a data structure nothing could reach. Now it is part of the language.

So `CREATE BUF 4 ALLOT` reserves four cells past `BUF`, full stop, and `BUF 3 + @` reaches the fourth one directly &mdash; the `BUF 3 CELLS + @` a byte-addressed Forth would require has no `CELLS` to write, because here one address unit already is one cell. I am not adding `CELLS`, `CELL+`, `CHARS`, or `CHAR+` this step. Nothing in the plan's own criterion asks for them, and adding them for their own sake would mean pretending my data space has a byte granularity it does not, then building four words whose entire job is apologizing for a pretense. If this thing ever has to talk to memory laid out by somebody else's rules instead of its own, the cell-versus-byte question comes back, and I don't know yet what answering it costs. I'm not going to guess before something forces the question.


# Where this leaves things

The default dictionary is now forty-six words instead of forty-two, and every place that counted primitives by that number &mdash; the effect table's own exhaustiveness comment, a handful of tests that hardcode a colon word's position past the last primitive &mdash; moved with it. That part is bookkeeping, not design, and it is the kind of bookkeeping I would rather do once, in one step, than discover piecemeal later.

The interesting part is smaller than the bookkeeping and more durable than the four primitives: a Forth program can now hold state across the whole run, not just on two stacks that unwind as soon as a word returns. That is a real capability this Forth did not have yesterday, and it did not need a single change to how programs are parsed or elaborated to get it.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 9 - The One-Shot API, and the Summit Not Yet Reached](post-9-one-shot-api.md)

</nav>


# References
