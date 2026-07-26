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
#include <smd/forth/machine/input_source.hpp>
#include <smd/forth/machine/stacks.hpp>
#include <smd/forth/parser/forth_chars.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace smd::forth::machine {

// Step F28 (docs/forth-plan-2.md), DIV-0012's own deferred fold: SOURCE/>IN,
// BASE, and STATE join the stacks/data-space/output substrate here rather
// than staying on the composed interpreter::forth_state wrapper F24/F25/F26/
// F27 built this up through. D13 does not say the interpreter needs these
// nearby; it says they *are* machine state in forth_state -- the wrapper was
// accepted through F26 on scope grounds (the R1 pipeline's own four
// consumers of the narrower type were still alive, and machine/ depending on
// parser/ for @ref input_source would have been a new layering edge for no
// benefit those steps needed), but F26 deleted that pipeline and F28 is the
// step DIV-0012's own orchestrator amendment names as unable to defer this
// any further: @ref apply_primitive and @ref run_from only ever see a
// forth_state, so a word reached through `EXECUTE` (D18), or a defining word
// like `CREATE`/`DOES>` invoked from inside another word's own compiled body
// (both this step's own deliverables), can only reach the input stream and
// the dictionary-mutating machinery that scans it if forth_state itself
// carries `SOURCE`/`>IN`. `interpreter::forth_state`, the composed wrapper,
// is deleted by this same step; every one of its own accessor names
// (`source`/`base`/`set_base`/`state`/`set_state`) is preserved verbatim
// here, so every caller of the wrapper's own spelling keeps working, minus
// the now-gone `.machine()` indirection. See DIV-0012's own F28 addendum.

/// The bundled Forth machine state (D7, D10, D13).
///
/// Holds both stacks, a @ref data_space "data_space" arena (bounds-checked
/// @c allot/@c fetch/@c store over a typed @ref addr, see
/// `data_space.hpp`), a fixed-capacity output buffer with Forth
/// number-formatting helpers, and -- since step F28's own fold, see this
/// header's own top comment -- the Forth-2012 section 3.4 outer
/// interpreter's own three remaining pieces of state: an @ref input_source
/// (`SOURCE`/`>IN`), `BASE` (default 10), and `STATE` (0 = interpreting,
/// nonzero = compiling). A literal type: usable as a local in a
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

    /// Constructs a forth_state whose @ref source is positioned at the start
    /// of @p text, with `BASE` 10 and `STATE` 0 (interpreting).
    constexpr explicit forth_state(std::string_view text) : source_{text} {}

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

    /// `SOURCE`/`>IN` (D13): see @ref input_source.
    [[nodiscard]] constexpr auto source() -> input_source &;
    /// `SOURCE`/`>IN` (D13): see @ref input_source.
    [[nodiscard]] constexpr auto source() const -> input_source const &;

    /// `BASE`: the radix number-per-BASE classification and formatting use.
    /// Defaults to 10.
    [[nodiscard]] constexpr auto base() const -> int;
    /// Sets `BASE`. A caller-supplied value outside 2..36 simply makes every
    /// subsequent number classification fail, which is a diagnosed "unknown
    /// word" at the interpreter loop, not UB.
    constexpr auto set_base(int value) -> void;

    /// `STATE`: 0 while interpreting, nonzero while compiling (`:` sets it,
    /// `;` clears it).
    [[nodiscard]] constexpr auto state() const -> int;
    /// Sets `STATE`.
    constexpr auto set_state(int value) -> void;

    /// Step F31 (docs/forth-plan-2.md), D11: the return-stack depth *at
    /// which* the innermost active `CATCH` handler's own 3-cell frame begins
    /// (`vm.hpp`'s own `op::catch_mark`/`perform_throw`), or `-1` if no
    /// `CATCH` is currently active (the default). Not itself a stack slot --
    /// a scalar register alongside the stacks, exactly like `BASE`/`STATE` --
    /// so `THROW` never has to scan the return stack to find its target: it
    /// reads this value directly, truncates to it, and restores it from the
    /// frame it just consumed (D24: "a design that makes the handler's
    /// extent explicit and findable" rather than one requiring a scan).
    [[nodiscard]] constexpr auto handler_depth() const -> int;
    /// Sets the handler-depth register (see @ref handler_depth). Only
    /// `vm.hpp`'s own `op::catch_mark`/`perform_throw` and the `catch_ok`
    /// primitive ever call this.
    constexpr auto set_handler_depth(int value) -> void;

  private:
    data_stack<MaxDepth> data_{};
    return_stack<MaxRDepth> returns_{};
    machine::data_space<MaxData> data_space_{};
    foundation::static_vector<char, MaxOut> output_{};
    input_source source_{};
    int base_{10};
    int state_{0};
    int handler_depth_{-1};
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

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::source()
    -> input_source & {
    return source_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::source() const
    -> input_source const & {
    return source_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::base() const
    -> int {
    return base_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::set_base(int value) -> void {
    base_ = value;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::state() const
    -> int {
    return state_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::set_state(int value)
    -> void {
    state_ = value;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::handler_depth() const
    -> int {
    return handler_depth_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>::set_handler_depth(int value)
    -> void {
    handler_depth_ = value;
}

namespace detail {

// forth_state must stay a literal, trivially destructible type (D3): it is
// what a constexpr-evaluated session (interpreter::build_session) runs its
// own text interpreter against, and the R1-era static_assert this moved from
// (interpreter::forth_state, deleted this step -- see this header's own top
// comment) already checked exactly this for the composed wrapper; it must
// hold for the folded-in type too.
static_assert(std::is_trivially_destructible_v<forth_state<64, 64, 1024, 256>>);

} // namespace detail

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
    one_plus,  ///< `1+`     ( a -- a+1 ), see DIV-0010.

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

    // d3325ebc-173e-4f4a-b51f-cb9811d2993a
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
    allot,      ///< `ALLOT` ( n -- ) Reserves @c n more cells past @ref
                ///< data_space::here.
    // d3325ebc-173e-4f4a-b51f-cb9811d2993a end

    // Step F28 (docs/forth-plan-2.md), D10/D21: `,` reserves one cell past
    // @ref data_space::here and stores @c x there in the same step -- the
    // primitive `CREATE ... , DOES> ...` (interpreter::apply_control_word's
    // own `create_`/`does_` control words) uses to fill in the cell `CREATE`
    // itself leaves empty.
    comma, ///< `,` ( x -- ) Reserves one cell past @ref data_space::here and
           ///< stores @c x there.

    // 4beb6ab5-f28e-4b2f-8a21-3d4ba8575f55
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
                 ///< and then fails with a distinguished "condition met"
                 ///< diagnosis that `vm.hpp`'s own `op::prim` dispatch always
                 ///< recognizes and routes through `perform_throw(-2, ...)`
                 ///< (step F31, DIV-0018/DIV-0017's own revisit: `ABORT"` is
                 ///< `THROW -2`, per Forth-2012, whether or not a `CATCH` is
                 ///< active -- caught if one is, an uncaught diagnosed
                 ///< `THROW` carrying `-2` if not). If @c flag is zero, does
                 ///< nothing.
    // 4beb6ab5-f28e-4b2f-8a21-3d4ba8575f55 end

    // 6a1e9c4f-8b3d-4e2a-9f6c-1d8b3a7e5f2c
    // Step F31 (docs/forth-plan-2.md), D11/D18: CATCH's own "normal
    // completion" epilogue. See vm.hpp's own op::catch_mark for the other
    // half of CATCH's compiled shape (it cannot itself be a primitive: it
    // must jump to the popped execution token, which apply_primitive has no
    // way to do).
    catch_ok, ///< ( -- 0 ) Pops CATCH's own 3-cell handler frame off the
              ///< return stack (prev-handler, saved data-stack depth,
              ///< resume ip -- the last two unused on this, the *normal*
              ///< completion path; see vm.hpp's own perform_throw for the
              ///< path that does use them), restores the handler-depth
              ///< register from the frame's own prev-handler field, and
              ///< pushes 0 -- CATCH's own "no exception" result. Reached
              ///< only as the return address CATCH's own op::catch_mark
              ///< pushed before jumping to the caught execution token, once
              ///< that token's own body returns normally (never reached at
              ///< all if a THROW inside it unwound past this frame instead).
    // 6a1e9c4f-8b3d-4e2a-9f6c-1d8b3a7e5f2c end
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
    case primitive::one_plus:
        return unary([](cell a) { return a + 1; });
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
    // 29b353a0-fc5d-46fc-98e3-b9a47b8cd691
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
        // 29b353a0-fc5d-46fc-98e3-b9a47b8cd691 end
    case primitive::comma: {
        auto value = pop_one();
        if (!value.has_value()) {
            return value.error();
        }
        auto a = state.data_space().allot(1);
        if (!a.has_value()) {
            return a.error();
        }
        return state.data_space().store(a.value(), value.value());
    }
    // 13c745cb-5824-451b-8765-cbed0d5626b3
    case primitive::parse: {
        auto ch = pop_one();
        if (!ch.has_value()) {
            return ch.error();
        }
        auto scan = parser::scan_delimited(state.source().cursor_at_in(),
                                           static_cast<char>(ch.value()),
                                           /* skip_leading */ false);
        state.source().set_in(scan.rest.position().offset);
        auto a = state.data_space().allot(static_cast<int>(scan.text.size()));
        if (!a.has_value()) {
            return a.error();
        }
        for (std::size_t i = 0; i < scan.text.size(); ++i) {
            auto sr = state.data_space().store(
                addr{static_cast<cell>(a.value()) + static_cast<cell>(i)},
                static_cast<cell>(scan.text[i]));
            if (!sr.has_value()) {
                return sr;
            }
        }
        if (auto r = push_cell(static_cast<cell>(a.value())); !r.has_value()) {
            return r;
        }
        return push_cell(static_cast<cell>(scan.text.size()));
    }
    case primitive::word: {
        auto ch = pop_one();
        if (!ch.has_value()) {
            return ch.error();
        }
        auto scan = parser::scan_delimited(state.source().cursor_at_in(),
                                           static_cast<char>(ch.value()),
                                           /* skip_leading */ true);
        state.source().set_in(scan.rest.position().offset);
        auto a =
            state.data_space().allot(static_cast<int>(scan.text.size()) + 1);
        if (!a.has_value()) {
            return a.error();
        }
        auto count_r = state.data_space().store(
            a.value(), static_cast<cell>(scan.text.size()));
        if (!count_r.has_value()) {
            return count_r;
        }
        for (std::size_t i = 0; i < scan.text.size(); ++i) {
            auto sr = state.data_space().store(
                addr{static_cast<cell>(a.value()) + 1 + static_cast<cell>(i)},
                static_cast<cell>(scan.text[i]));
            if (!sr.has_value()) {
                return sr;
            }
        }
        return push_cell(static_cast<cell>(a.value()));
    }
    case primitive::char_: {
        auto scanned = parser::scan_bare_name(state.source().cursor_at_in());
        if (!scanned.has_value()) {
            return scanned.error();
        }
        state.source().set_in(scanned.value().rest.position().offset);
        return push_cell(static_cast<cell>(
            static_cast<unsigned char>(scanned.value().value.front())));
    }
    case primitive::count: {
        auto a = pop_one();
        if (!a.has_value()) {
            return a.error();
        }
        auto n = state.data_space().fetch(addr{a.value()});
        if (!n.has_value()) {
            return n.error();
        }
        if (auto r = push_cell(a.value() + 1); !r.has_value()) {
            return r;
        }
        return push_cell(n.value());
    }
    case primitive::type_: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [c_addr, u] = operands.value();
        for (cell i = 0; i < u; ++i) {
            auto ch = state.data_space().fetch(addr{c_addr + i});
            if (!ch.has_value()) {
                return ch.error();
            }
            if (auto r = state.emit_char(static_cast<char>(ch.value()));
                !r.has_value()) {
                return r;
            }
        }
        return std::monostate{};
    }
    case primitive::abort_quote: {
        auto operands = pop_two();
        if (!operands.has_value()) {
            return operands.error();
        }
        auto [c_addr, u] = operands.value();
        auto flag = pop_one();
        if (!flag.has_value()) {
            return flag.error();
        }
        if (flag.value() == 0) {
            return std::monostate{};
        }
        for (cell i = 0; i < u; ++i) {
            auto ch = state.data_space().fetch(addr{c_addr + i});
            if (!ch.has_value()) {
                return ch.error();
            }
            if (auto r = state.emit_char(static_cast<char>(ch.value()));
                !r.has_value()) {
                return r;
            }
        }
        return foundation::parse_error{foundation::source_pos{},
                                       "ABORT\" condition met"};
    }
        // 13c745cb-5824-451b-8765-cbed0d5626b3 end
    // 3c8f6a2d-1b7e-4d9a-8c3f-6b1e9a4d7c2f
    case primitive::catch_ok: {
        auto resume_ip = state.returns().pop();
        if (!resume_ip.has_value()) {
            return resume_ip.error();
        }
        auto saved_data_depth = state.returns().pop();
        if (!saved_data_depth.has_value()) {
            return saved_data_depth.error();
        }
        auto prev_handler = state.returns().pop();
        if (!prev_handler.has_value()) {
            return prev_handler.error();
        }
        state.set_handler_depth(static_cast<int>(prev_handler.value()));
        return push_cell(0);
    }
        // 3c8f6a2d-1b7e-4d9a-8c3f-6b1e9a4d7c2f end
    }
    return foundation::parse_error{foundation::source_pos{},
                                   "unknown primitive opcode"};
}

// Merge criteria (static_assert, immediately-invoked-lambda pattern), step
// F28's own fold: SOURCE/>IN/BASE/STATE are directly on forth_state, and `,`
// reserves and fills one data-space cell.

static_assert([] {
    forth_state<8, 8, 8, 32> st{"dup swap"};
    return st.source().text() == "dup swap" && st.source().in() == 0 &&
           st.base() == 10 && st.state() == 0;
}());

static_assert([] {
    forth_state<8, 8, 8, 32> st{};
    st.source().set_in(3);
    st.set_base(16);
    st.set_state(1);
    return st.source().in() == 3 && st.base() == 16 && st.state() == 1;
}());

static_assert([] {
    forth_state<8, 8, 8, 32> st{};
    auto push = st.data().push(42);
    if (!push.has_value()) {
        return false;
    }
    auto r = apply_primitive(primitive::comma, st);
    return r.has_value() && st.data().depth() == 0 &&
           st.data_space().size() == 1 &&
           st.data_space().fetch(addr{0}).value() == 42;
}());

// Merge criteria (static_assert, immediately-invoked-lambda pattern), step
// F29's own parsing-word primitives (D19/D21): PARSE/WORD copy text off
// SOURCE into freshly allotted data-space cells and advance >IN; CHAR reads
// a name's first character without touching the data space at all; COUNT/
// TYPE are the read side, over exactly the counted-string shape WORD wrote.

static_assert([] {
    std::string_view src = "hello\" world";
    forth_state<8, 8, 16, 32> st{src};
    auto push = st.data().push(static_cast<cell>('"'));
    if (!push.has_value()) {
        return false;
    }
    auto r = apply_primitive(primitive::parse, st);
    if (!r.has_value()) {
        return false;
    }
    auto u = st.data().pop();
    auto a = st.data().pop();
    if (!u.has_value() || !a.has_value() || u.value() != 5) {
        return false;
    }
    for (cell i = 0; i < 5; ++i) {
        auto ch = st.data_space().fetch(addr{a.value() + i});
        if (!ch.has_value() ||
            ch.value() != static_cast<cell>(src[static_cast<std::size_t>(i)])) {
            return false;
        }
    }
    return st.source().in() == 6 &&
           st.source().cursor_at_in().remaining() == " world";
}());

static_assert([] {
    // WORD skips leading delimiters (the leading spaces here) first, unlike
    // PARSE, and stores a counted string rather than an address/length pair.
    forth_state<8, 8, 16, 32> st{"  abc def"};
    auto push = st.data().push(static_cast<cell>(' '));
    if (!push.has_value()) {
        return false;
    }
    auto r = apply_primitive(primitive::word, st);
    if (!r.has_value()) {
        return false;
    }
    auto a = st.data().pop();
    if (!a.has_value()) {
        return false;
    }
    auto count_cell = st.data_space().fetch(addr{a.value()});
    if (!count_cell.has_value() || count_cell.value() != 3) {
        return false;
    }
    char const expected[3] = {'a', 'b', 'c'};
    for (cell i = 0; i < 3; ++i) {
        auto ch = st.data_space().fetch(addr{a.value() + 1 + i});
        if (!ch.has_value() ||
            ch.value() !=
                static_cast<cell>(expected[static_cast<std::size_t>(i)])) {
            return false;
        }
    }
    return st.source().cursor_at_in().remaining() == "def";
}());

static_assert([] {
    forth_state<8, 8, 8, 32> st{"  ab cd"};
    auto r = apply_primitive(primitive::char_, st);
    if (!r.has_value()) {
        return false;
    }
    auto v = st.data().pop();
    return v.has_value() && v.value() == static_cast<cell>('a') &&
           st.source().cursor_at_in().remaining() == " cd";
}());

static_assert([] {
    forth_state<8, 8, 8, 32> st{};
    auto a = st.data_space().allot(4); // 1 count cell + 3 characters.
    if (!a.has_value()) {
        return false;
    }
    if (!st.data_space().store(a.value(), 3).has_value() ||
        !st.data_space()
             .store(addr{static_cast<cell>(a.value()) + 1},
                    static_cast<cell>('h'))
             .has_value() ||
        !st.data_space()
             .store(addr{static_cast<cell>(a.value()) + 2},
                    static_cast<cell>('i'))
             .has_value() ||
        !st.data_space()
             .store(addr{static_cast<cell>(a.value()) + 3},
                    static_cast<cell>('!'))
             .has_value()) {
        return false;
    }
    if (!st.data().push(static_cast<cell>(a.value())).has_value()) {
        return false;
    }
    auto r = apply_primitive(primitive::count, st);
    if (!r.has_value()) {
        return false;
    }
    auto u = st.data().pop();
    auto c_addr = st.data().pop();
    return u.has_value() && c_addr.has_value() && u.value() == 3 &&
           c_addr.value() == static_cast<cell>(a.value()) + 1;
}());

static_assert([] {
    forth_state<8, 8, 8, 32> st{};
    auto a = st.data_space().allot(2);
    if (!a.has_value()) {
        return false;
    }
    if (!st.data_space().store(a.value(), static_cast<cell>('h')).has_value() ||
        !st.data_space()
             .store(addr{static_cast<cell>(a.value()) + 1},
                    static_cast<cell>('i'))
             .has_value()) {
        return false;
    }
    if (!st.data().push(static_cast<cell>(a.value())).has_value() ||
        !st.data().push(2).has_value()) {
        return false;
    }
    auto r = apply_primitive(primitive::type_, st);
    if (!r.has_value()) {
        return false;
    }
    auto const &out = st.output();
    return out.size() == 2 && out[0] == 'h' && out[1] == 'i';
}());

static_assert([] {
    // ABORT"'s own runtime primitive (step F31, DIV-0017's revisit/
    // DIV-0018): a zero flag drops the message silently; a nonzero flag
    // prints it, then fails with the specific "condition met" diagnosis
    // vm.hpp's own op::prim dispatch always recognizes and turns into a real
    // `THROW -2` (caught, or an uncaught diagnosed THROW carrying -2) --
    // apply_primitive itself, tested directly here, only needs to signal
    // that recognizable condition, not perform the unwind itself.
    forth_state<8, 8, 8, 32> st{};
    auto a = st.data_space().allot(4);
    if (!a.has_value()) {
        return false;
    }
    char const msg[4] = {'b', 'o', 'o', 'm'};
    for (cell i = 0; i < 4; ++i) {
        if (!st.data_space()
                 .store(addr{static_cast<cell>(a.value()) + i},
                        static_cast<cell>(msg[static_cast<std::size_t>(i)]))
                 .has_value()) {
            return false;
        }
    }
    // Stack order matches what the compiled ABORT" sequence leaves behind:
    // the flag (pushed by whatever code preceded ABORT") sits below the
    // addr/len pair ABORT"'s own compiled form pushes on top of it.
    if (!st.data().push(0).has_value() ||
        !st.data().push(static_cast<cell>(a.value())).has_value() ||
        !st.data().push(4).has_value()) {
        return false;
    }
    auto quiet = apply_primitive(primitive::abort_quote, st);
    if (!quiet.has_value() || !st.output().empty()) {
        return false;
    }
    if (!st.data().push(1).has_value() ||
        !st.data().push(static_cast<cell>(a.value())).has_value() ||
        !st.data().push(4).has_value()) {
        return false;
    }
    auto loud = apply_primitive(primitive::abort_quote, st);
    return !loud.has_value() && st.output().size() == 4 &&
           std::string_view{loud.error().message} == "ABORT\" condition met";
}());

// Merge criterion (static_assert, immediately-invoked-lambda pattern), step
// F31's own catch_ok primitive: pops CATCH's own 3-cell handler frame
// (prev-handler, saved data depth, resume ip -- the last two unused here),
// restores handler_depth from the first, and pushes 0.

static_assert([] {
    forth_state<8, 8, 8, 32> st{};
    st.set_handler_depth(3);
    if (!st.returns().push(7).has_value() ||  // prev-handler
        !st.returns().push(2).has_value() ||  // saved data depth (unused)
        !st.returns().push(99).has_value()) { // resume ip (unused)
        return false;
    }
    auto r = apply_primitive(primitive::catch_ok, st);
    if (!r.has_value() || st.returns().depth() != 0 ||
        st.handler_depth() != 7) {
        return false;
    }
    auto flag = st.data().pop();
    return flag.has_value() && flag.value() == 0;
}());

} // namespace smd::forth::machine

#endif
