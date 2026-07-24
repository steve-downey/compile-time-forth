<div class="abstract" id="org1e26849">
<p>
The front end produces trees; the machine is what those trees eventually run on.
This entry builds the substrate: a cell, two stacks, the primitives, the
dictionary that names them, and the data space they read and write. The data
space has a distinct address type so a raw index can't be mistaken for a number
on the stack. The dictionary needed that type before it existed, so for now it
holds a placeholder.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 4 - The Grammar That Couldn't Be a Combinator ←](post-4-grammar.md)

</nav>


# A cell and two stacks

Forth's memory model is famously blunt. Everything is a cell, and a cell is a machine word. Here it is a `std::int64_t` &mdash; signed, because Forth arithmetic is signed by default and I would rather match that than fight it.

```cpp
using cell = std::int64_t;

/// The Forth truth value pushed by comparison words: all bits set (D7).
inline constexpr cell flag_true = -1;

/// The Forth truth value pushed by comparison words: no bits set (D7).
inline constexpr cell flag_false = 0;
```

A Forth true is `-1`, all bits set, not `1`. That is not decoration &mdash; it makes `AND` and `OR` double as bitwise and logical operators, which is the sort of economy the whole language is built out of.

Two stacks. The data stack is where computation happens; the return stack holds return addresses and the occasional stashed value. Both are `static_vector`, so neither reallocates, and both are bounds-checked on every push and pop &mdash; an overflow or underflow is a diagnosed error, not undefined behavior. That check is not optional under `constexpr`: reading past the end of a `static_vector` during constant evaluation is a hard compile error anyway, so better to catch it as a real Forth stack fault with a message than as a raw `constexpr` diagnostic.

`forth_state` bundles the whole runtime world: the data stack, the return stack, the data space, and an output buffer for the words that print. The primitives &mdash; arithmetic, comparison, stack shuffling &mdash; are an `enum`, and `apply_primitive` is one switch that executes a primitive against a `forth_state`. Nothing here is clever. It is the flat, obvious core the fast machine will later be checked against.


# The dictionary

A Forth dictionary is a list of named things, and "thing" is not one kind. A name can bind to a primitive, to a colon definition, to a variable, to a constant, or to a foreign function. So a dictionary entry is a variant over those binding kinds:

```c++
// src/smd/forth/machine/dictionary.hpp  (shape, not verbatim)
struct colon_word    { int core_id; stack_effect effect; };
struct variable_word { cell addr = 0; };          // placeholder, see below
struct constant_word { cell value; };
struct foreign_word  { int index; };

using dictionary_entry = std::variant<
    primitive_opcode, colon_word, variable_word, constant_word, foreign_word>;
```

The dictionary is append-only, and lookup scans it newest-first. That single choice gives Forth its redefinition semantics for free: define `FOO`, then define `FOO` again, and a later reference finds the newer one while the older one stays buried and reachable by anything compiled before the redefinition. There is no mutation, no deletion &mdash; just a search order. `default_dictionary()` builds the starting vocabulary of primitives that every program begins with.

Actually resolving a name in a program &mdash; deciding *which* entry a `syn_word` refers to &mdash; is not the dictionary's job. The dictionary stores and finds; elaboration resolves. Keeping that split is the same discipline as the reader: one question, one place.


# The data space, and a real address type

`VARIABLE` and `CREATE` carve out cells in a data space, and programs read and write them with `@` and `!`. The data space is another cell arena with bounds-checked `allot`, `fetch`, and `store`. The part worth stopping on is the address.

A data-space address is an index. It would be trivial to represent it as a `cell` &mdash; it already is one, underneath. But then an address and an ordinary number are the same type, and nothing stops a program's machinery from putting a raw index where a value belongs, or doing arithmetic on an address as if it were data. So the address is its own type, convertible to and from `cell` only when you say so:

```cpp
class addr {
  public:
    constexpr addr() = default;

    /// Constructs an address from a raw cell index. Explicit: an address
    /// is never implicitly manufactured from an arbitrary cell.
    constexpr explicit addr(cell index);

    /// Converts back to the raw cell index. Explicit for the same reason.
    [[nodiscard]] constexpr explicit operator cell() const;

    // HIDDEN FRIEND
    friend constexpr auto operator==(addr const &, addr const &)
        -> bool = default;

  private:
    cell index_{};
};

constexpr addr::addr(cell index) : index_(index) {}

constexpr addr::operator cell() const { return index_; }
```

Both conversions are `explicit`. You can turn a `cell` into an `addr` and back, but never by accident, and never silently in the middle of an expression that thought it was doing arithmetic. It is a small amount of type safety bought cheaply, and it exists to keep addresses and numbers from getting confused on a stack that has no idea which is which.


# The placeholder I owe

Here is a debt I am recording now so it is on the books before it gets paid. Look again at `variable_word`:

```c++
struct variable_word {
    cell addr = 0;   // wants to be `addr`, holds a plain `cell` for now
};
```

That field wants to be an `addr`. It holds a `cell`. When I built the dictionary, the distinct `addr` type didn't exist yet &mdash; it belongs to the data space, and the dictionary is not the data space's owner. Rather than invent a second, possibly-conflicting address type inside the dictionary header just to name the field, I let the field hold the only representation that existed: a plain `cell` index. Nothing in the dictionary itself puts a `variable_word`'s address on the stack, so the missing type safety costs nothing yet. But it is not the guarantee the `addr` type is there to give, and any code that builds a `variable_word` right now is passing an unprotected index.

The first place that actually constructs a `variable_word` is elaboration, when it processes a `VARIABLE` declaration. So that is where the field gets its real type &mdash; and that is the next entry's problem.

The machine has a heartbeat: cells, stacks, a vocabulary, and somewhere to store things. Nothing can run yet, because nothing has resolved a single word.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 4 - The Grammar That Couldn't Be a Combinator](post-4-grammar.md) | [Part 6 - Elaboration and the Effect Checker →](post-6-elaboration.md)

</nav>


# References
