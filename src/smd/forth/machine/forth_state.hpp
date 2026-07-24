// src/smd/forth/machine/forth_state.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_FORTH_STATE_HPP
#define SRC_SMD_FORTH_MACHINE_FORTH_STATE_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/data_space.hpp>
#include <smd/forth/machine/stacks.hpp>

#include <cstdint>
#include <utility>
#include <variant>

namespace smd::forth::machine {

/// The bundled Forth machine state (D7, D10).
///
/// Holds both stacks, a @ref data_space "data_space" arena (bounds-checked
/// @c allot/@c fetch/@c store over a typed @ref addr, see
/// `data_space.hpp`), and a fixed-capacity output buffer with Forth
/// number-formatting helpers. A literal type: usable as a local in a
/// @c constexpr function, or exercised wholesale in a @c static_assert.
///
/// @tparam MaxDepth  Data stack capacity, in cells.
/// @tparam MaxRDepth Return stack capacity, in cells.
/// @tparam MaxData   Data space capacity, in cells.
/// @tparam MaxOut    Output buffer capacity, in characters.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
class forth_state {
  public:
    constexpr forth_state() = default;

    /// The data stack.
    [[nodiscard]] constexpr auto data() -> data_stack<MaxDepth> &;
    /// The data stack.
    [[nodiscard]] constexpr auto data() const -> data_stack<MaxDepth> const &;

    /// The return stack.
    [[nodiscard]] constexpr auto returns() -> return_stack<MaxRDepth> &;
    /// The return stack.
    [[nodiscard]] constexpr auto returns() const
        -> return_stack<MaxRDepth> const &;

    /// The data space: a bounds-checked cell arena (D10). See
    /// `data_space.hpp` for @c allot/@c fetch/@c store and the @ref addr
    /// type.
    [[nodiscard]] constexpr auto data_space() -> machine::data_space<MaxData> &;
    /// The data space: a bounds-checked cell arena (D10). See
    /// `data_space.hpp` for @c allot/@c fetch/@c store and the @ref addr
    /// type.
    [[nodiscard]] constexpr auto data_space() const
        -> machine::data_space<MaxData> const &;

    /// The output accumulated so far by @ref emit_char / @ref emit_cell.
    [[nodiscard]] constexpr auto output() const
        -> foundation::static_vector<char, MaxOut> const &;

    /// Appends a single character to the output buffer (D10).
    /// Diagnoses overflow if the buffer is already at @ref MaxOut.
    constexpr auto emit_char(char value) -> status;

    /// Appends the decimal rendering of @p value followed by a trailing
    /// space -- Forth's conventional number-then-space output format
    /// (D10), e.g. `emit_cell(-42)` appends `"-42 "`.
    /// Diagnoses overflow if the buffer cannot hold the full rendering.
    constexpr auto emit_cell(cell value) -> status;

  private:
    data_stack<MaxDepth> data_{};
    return_stack<MaxRDepth> returns_{};
    machine::data_space<MaxData> data_space_{};
    foundation::static_vector<char, MaxOut> output_{};
};

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::data()
    -> data_stack<MaxDepth> & {
    return data_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::data() const
    -> data_stack<MaxDepth> const & {
    return data_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::returns()
    -> return_stack<MaxRDepth> & {
    return returns_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::returns() const
    -> return_stack<MaxRDepth> const & {
    return returns_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::data_space()
    -> machine::data_space<MaxData> & {
    return data_space_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::data_space() const
    -> machine::data_space<MaxData> const & {
    return data_space_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::output() const
    -> foundation::static_vector<char, MaxOut> const & {
    return output_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::emit_char(char value)
    -> status {
    if (output_.size() >= MaxOut) {
        return foundation::parse_error{foundation::source_pos{},
                                       "output buffer overflow"};
    }
    output_.push_back(value);
    return std::monostate{};
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::emit_cell(cell value)
    -> status {
    bool const negative = value < 0;
    // Negate through std::uint64_t rather than through cell itself: the
    // magnitude of std::numeric_limits<cell>::min() has no representation
    // in cell, but unsigned negation (0 - x, wrapping mod 2^64) is always
    // well-defined and yields the correct magnitude bit pattern.
    std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(0) -
                                             static_cast<std::uint64_t>(value)
                                       : static_cast<std::uint64_t>(value);

    char digits[20]{};
    int count = 0;
    if (magnitude == 0) {
        digits[count++] = '0';
    } else {
        while (magnitude > 0) {
            digits[count++] =
                static_cast<char>('0' + static_cast<int>(magnitude % 10));
            magnitude /= 10;
        }
    }

    if (negative) {
        if (auto r = emit_char('-'); !r.has_value()) {
            return r;
        }
    }
    for (int i = count - 1; i >= 0; --i) {
        if (auto r = emit_char(digits[i]); !r.has_value()) {
            return r;
        }
    }
    return emit_char(' ');
}

/// The section-6 arithmetic, comparison, stack-manipulation, and output
/// primitive opcodes (D7, D10). Enumerator names spell the Forth word, with
/// a trailing underscore where the bare spelling would collide with a C++
/// keyword or another enumerator (`mod_`, `abs_`, `min_`, `max_`, `and_`,
/// `or_`, `xor_`, `true_`, `false_`), and an underscore standing in for `.`
/// (`dot`, `dot_s`).
enum class primitive {
    // Arithmetic and logic.
    plus,      ///< `+`      ( a b -- a+b )
    minus,     ///< `-`      ( a b -- a-b )
    star,      ///< `*`      ( a b -- a*b )
    slash,     ///< `/`      ( a b -- a/b ), diagnoses division by zero.
    mod_,      ///< `MOD`    ( a b -- a%b ), diagnoses division by zero.
    negate,    ///< `NEGATE` ( a -- -a )
    abs_,      ///< `ABS`    ( a -- |a| )
    min_,      ///< `MIN`    ( a b -- min(a,b) )
    max_,      ///< `MAX`    ( a b -- max(a,b) )
    and_,      ///< `AND`    ( a b -- a&b )
    or_,       ///< `OR`     ( a b -- a|b )
    xor_,      ///< `XOR`    ( a b -- a^b )
    invert,    ///< `INVERT` ( a -- ~a )
    lshift,    ///< `LSHIFT` ( a n -- a<<n )
    rshift,    ///< `RSHIFT` ( a n -- a>>n ), logical (unsigned) shift.
    one_minus, ///< `1-`     ( a -- a-1 ), see DIV-0007.

    // Comparison.
    zero_equal,    ///< `0=`  ( a -- flag )   a == 0
    zero_less,     ///< `0<`  ( a -- flag )   a < 0
    equal,         ///< `=`   ( a b -- flag )
    not_equal,     ///< `<>`  ( a b -- flag )
    less,          ///< `<`   ( a b -- flag )
    greater,       ///< `>`   ( a b -- flag )
    less_equal,    ///< `<=`  ( a b -- flag )
    greater_equal, ///< `>=`  ( a b -- flag )
    true_,         ///< `TRUE`  ( -- flag_true )
    false_,        ///< `FALSE` ( -- flag_false )

    // Data stack manipulation.
    dup,          ///< `DUP`   ( a -- a a )
    drop,         ///< `DROP`  ( a -- )
    swap,         ///< `SWAP`  ( a b -- b a )
    over,         ///< `OVER`  ( a b -- a b a )
    rot,          ///< `ROT`   ( a b c -- b c a )
    question_dup, ///< `?DUP`  ( a -- 0 | a a )
    nip,          ///< `NIP`   ( a b -- b )
    tuck,         ///< `TUCK`  ( a b -- b a b )
    depth,        ///< `DEPTH` ( -- n )

    // Return stack.
    to_r,    ///< `>R` ( a -- ) ( R: -- a )
    r_from,  ///< `R>` ( -- a ) ( R: a -- )
    r_fetch, ///< `R@` ( -- a ) ( R: a -- a )

    // Output (D10): append to forth_state's own output buffer; step F13 is
    // what first wires these (no runtime behavior existed for them before).
    dot,   ///< `.`   ( n -- )  Prints @c n via @ref forth_state::emit_cell.
    dot_s, ///< `.S`  ( -- )    Prints the data stack, bottom to top,
           ///< nondestructively, one cell per @ref forth_state::emit_cell.
    emit,  ///< `EMIT` ( c -- ) Prints the character whose code is @c c.
    cr,    ///< `CR`  ( -- )    Prints a newline.

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
};

/// Applies a pure-stack, output, or memory @ref primitive to @p state.
///
/// Every stack-underflow, stack-overflow, and division-by-zero condition is
/// diagnosed through the returned @ref status rather than triggering
/// undefined behavior (D7). Division (`/`) and remainder (`MOD`) use
/// symmetric (C++ truncating) division, not floored division -- see
/// individual opcode comments in @ref primitive. The four output opcodes
/// (`.`, `.S`, `EMIT`, `CR`) append to @p state's own output buffer (D10)
/// via @ref forth_state::emit_char / @ref forth_state::emit_cell rather than
/// performing any direct I/O -- an output-buffer overflow is diagnosed the
/// same way a stack overflow is. The four memory opcodes (`@`, `!`, `+!`,
/// `ALLOT`, step F16) operate on @p state's own @ref data_space, converting
/// each address cell to @ref addr at the boundary; an out-of-bounds fetch or
/// store, or a full arena, is diagnosed through @ref status exactly like
/// every other opcode here, never UB (D7).
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
apply_primitive(primitive op,
                forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state)
    -> status {
    auto pop_one = [&state]() -> foundation::result<cell> {
        return state.data().pop();
    };

    auto pop_two = [&state]() -> foundation::result<std::pair<cell, cell>> {
        auto b = state.data().pop();
        if (!b.has_value()) {
            return b.error();
        }
        auto a = state.data().pop();
        if (!a.has_value()) {
            return a.error();
        }
        return std::pair<cell, cell>{a.value(), b.value()};
    };

    auto push_cell = [&state](cell value) -> status {
        auto r = state.data().push(value);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    };

    auto unary = [&](auto &&fn) -> status {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        return push_cell(fn(a.value()));
    };

    auto binary = [&](auto &&fn) -> status {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        return push_cell(fn(a, b));
    };

    auto compare = [&](auto &&fn) -> status {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        return push_cell(fn(a, b) ? flag_true : flag_false);
    };

    switch (op) {
    case primitive::plus:
        return binary([](cell a, cell b) { return a + b; });
    case primitive::minus:
        return binary([](cell a, cell b) { return a - b; });
    case primitive::star:
        return binary([](cell a, cell b) { return a * b; });
    case primitive::slash: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        if (b == 0) {
            return foundation::parse_error{foundation::source_pos{},
                                           "division by zero"};
        }
        return push_cell(a / b);
    }
    case primitive::mod_: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        if (b == 0) {
            return foundation::parse_error{foundation::source_pos{},
                                           "division by zero"};
        }
        return push_cell(a % b);
    }
    case primitive::negate:
        return unary([](cell a) { return -a; });
    case primitive::abs_:
        return unary([](cell a) { return a < 0 ? -a : a; });
    case primitive::min_:
        return binary([](cell a, cell b) { return a < b ? a : b; });
    case primitive::max_:
        return binary([](cell a, cell b) { return a > b ? a : b; });
    case primitive::and_:
        return binary([](cell a, cell b) { return a & b; });
    case primitive::or_:
        return binary([](cell a, cell b) { return a | b; });
    case primitive::xor_:
        return binary([](cell a, cell b) { return a ^ b; });
    case primitive::invert:
        return unary([](cell a) { return ~a; });
    case primitive::lshift:
        return binary([](cell a, cell b) {
            return static_cast<cell>(static_cast<std::uint64_t>(a) << (b & 63));
        });
    case primitive::rshift:
        return binary([](cell a, cell b) {
            return static_cast<cell>(static_cast<std::uint64_t>(a) >> (b & 63));
        });
    case primitive::one_minus:
        return unary([](cell a) { return a - 1; });
    case primitive::zero_equal:
        return unary([](cell a) { return a == 0 ? flag_true : flag_false; });
    case primitive::zero_less:
        return unary([](cell a) { return a < 0 ? flag_true : flag_false; });
    case primitive::equal:
        return compare([](cell a, cell b) { return a == b; });
    case primitive::not_equal:
        return compare([](cell a, cell b) { return a != b; });
    case primitive::less:
        return compare([](cell a, cell b) { return a < b; });
    case primitive::greater:
        return compare([](cell a, cell b) { return a > b; });
    case primitive::less_equal:
        return compare([](cell a, cell b) { return a <= b; });
    case primitive::greater_equal:
        return compare([](cell a, cell b) { return a >= b; });
    case primitive::true_:
        return push_cell(flag_true);
    case primitive::false_:
        return push_cell(flag_false);
    case primitive::dup: {
        auto a = state.data().peek(0);
        if (!a.has_value()) {
            return a.error();
        }
        return push_cell(a.value());
    }
    case primitive::drop: {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        return std::monostate{};
    }
    case primitive::swap: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        if (auto r = push_cell(b); !r.has_value()) {
            return r;
        }
        return push_cell(a);
    }
    case primitive::over: {
        auto a = state.data().peek(1);
        if (!a.has_value()) {
            return a.error();
        }
        return push_cell(a.value());
    }
    case primitive::rot: {
        auto c = pop_one();
        if (!c.has_value()) {
            return c.error();
        }
        auto b = pop_one();
        if (!b.has_value()) {
            return b.error();
        }
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        if (auto r = push_cell(b.value()); !r.has_value()) {
            return r;
        }
        if (auto r = push_cell(c.value()); !r.has_value()) {
            return r;
        }
        return push_cell(a.value());
    }
    case primitive::question_dup: {
        auto a = state.data().peek(0);
        if (!a.has_value()) {
            return a.error();
        }
        if (a.value() != 0) {
            return push_cell(a.value());
        }
        return std::monostate{};
    }
    case primitive::nip: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        (void)a;
        return push_cell(b);
    }
    case primitive::tuck: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [a, b] = operands.value();
        if (auto r = push_cell(b); !r.has_value()) {
            return r;
        }
        if (auto r = push_cell(a); !r.has_value()) {
            return r;
        }
        return push_cell(b);
    }
    case primitive::depth:
        return push_cell(static_cast<cell>(state.data().depth()));
    case primitive::to_r: {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        auto r = state.returns().push(a.value());
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case primitive::r_from: {
        auto a = state.returns().pop();
        if (!a.has_value()) {
            return a.error();
        }
        return push_cell(a.value());
    }
    case primitive::r_fetch: {
        auto a = state.returns().peek(0);
        if (!a.has_value()) {
            return a.error();
        }
        return push_cell(a.value());
    }
    case primitive::dot: {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        return state.emit_cell(a.value());
    }
    case primitive::dot_s: {
        // Bottom to top, nondestructively: peek(offset) counts down from the
        // top (offset 0), so the bottom-most cell is at offset depth() - 1.
        for (int offset = state.data().depth() - 1; offset >= 0; --offset) {
            auto a = state.data().peek(offset);
            if (!a.has_value()) {
                return a.error();
            }
            if (auto r = state.emit_cell(a.value()); !r.has_value()) {
                return r;
            }
        }
        return std::monostate{};
    }
    case primitive::emit: {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        return state.emit_char(static_cast<char>(a.value()));
    }
    case primitive::cr:
        return state.emit_char('\n');
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
    }
    return foundation::parse_error{foundation::source_pos{},
                                   "unknown primitive opcode"};
}

} // namespace smd::forth::machine

#endif
