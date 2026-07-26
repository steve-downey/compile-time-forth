// src/smd/forth/interpreter/interp.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_INTERP_HPP
#define SRC_SMD_FORTH_INTERPRETER_INTERP_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/interpreter/input_source.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/reader/forth_chars.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::interpreter {

// Step F24 (docs/forth-plan-2.md): the Forth-2012 section 3.4 outer text
// interpreter, interpret state only (D13). There is no `:` yet (that is
// F25), so STATE never actually leaves 0 in this step -- the field exists
// now, and is tested to exist and default correctly, because forth_state
// growing STATE/BASE/SOURCE/>IN together is the point of D13, not something
// to phase in word by word.
//
// D19 places "combinators below the word, owning scanning and
// classification" under parser<F>; this step's own number-per-BASE
// classification (is_number_token_in_base/token_to_cell_in_base below)
// follows that placement, generalizing reader::is_number_token/
// token_to_cell (forth_chars.hpp, fixed to decimal per D8's original scope)
// rather than editing them -- see docs/compiler_architecture.org's Phase 1
// section for the D19 token-layer location decision this step recorded.

/// The interpreter's own Forth machine state: @ref machine::forth_state (the
/// stacks, data space, and output buffer) plus the three fields D13 adds on
/// top of it -- an @ref input_source (`SOURCE`/`>IN`), `BASE` (default 10),
/// and `STATE` (0 = interpreting; F25 is what first writes a nonzero value).
/// Composition, not inheritance or an in-place edit of @ref machine::
/// forth_state: every other consumer of @ref machine::forth_state (@ref
/// machine::run, @ref machine::eval_program, `forth.hpp`'s
/// `forth_program`) keeps working unchanged against the narrower type, and
/// this wider one stays a literal, trivially destructible aggregate exactly
/// like its inner @ref machine::forth_state already is (D3).
///
/// @tparam MaxDepth  Data stack capacity, in cells (@ref machine::
///                   forth_state).
/// @tparam MaxRDepth Return stack capacity, in cells, likewise.
/// @tparam MaxData   Data space capacity, in cells, likewise.
/// @tparam MaxOut    Output buffer capacity, in characters, likewise.
/// @tparam MaxName   Maximum scanned-token length, in characters -- shared
///                   with the @ref machine::dictionary this state's own
///                   @ref interpret is run against, since a token longer
///                   than a dictionary name could never match one anyway.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut,
          int MaxName = 32>
class forth_state {
  public:
    /// The wrapped @ref machine::forth_state type. Qualified from the
    /// global namespace rather than written as the shorter `machine::...`:
    /// this class declares its own member function named @ref machine
    /// below, and resolving the alias unambiguously here (rather than
    /// relying on point-of-declaration lookup ordering) keeps the two
    /// `machine` names from ever being a source of confusion.
    using machine_state =
        ::smd::forth::machine::forth_state<MaxDepth, MaxRDepth, MaxData,
                                           MaxOut>;

    constexpr forth_state() = default;

    /// Constructs a forth_state whose @ref source is positioned at the start
    /// of @p text, with `BASE` 10 and `STATE` 0 (interpreting).
    constexpr explicit forth_state(std::string_view text) : source_{text} {}

    /// The wrapped machine state: stacks, data space, output buffer.
    [[nodiscard]] constexpr auto machine() -> machine_state &;
    /// The wrapped machine state: stacks, data space, output buffer.
    [[nodiscard]] constexpr auto machine() const -> machine_state const &;

    /// `SOURCE`/`>IN` (D13, D19): see @ref input_source.
    [[nodiscard]] constexpr auto source() -> input_source &;
    /// `SOURCE`/`>IN` (D13, D19): see @ref input_source.
    [[nodiscard]] constexpr auto source() const -> input_source const &;

    /// `BASE`: the radix number-per-BASE classification and formatting use.
    /// Defaults to 10.
    [[nodiscard]] constexpr auto base() const -> int;
    /// Sets `BASE`. No range check here (F24 has no `HEX`/`DECIMAL` word to
    /// route through one); a caller-supplied value outside 2..36 simply
    /// makes every subsequent number classification fail, which is a
    /// diagnosed "unknown word" at the interpreter loop, not UB.
    constexpr auto set_base(int value) -> void;

    /// `STATE`: 0 while interpreting (the only value this step ever
    /// produces), nonzero while compiling (F25).
    [[nodiscard]] constexpr auto state() const -> int;
    /// Sets `STATE`. Unused by this step's own @ref interpret, which never
    /// leaves interpret state; present because D13 puts `STATE` in
    /// forth_state now, not as a later retrofit.
    constexpr auto set_state(int value) -> void;

  private:
    machine_state machine_{};
    input_source source_{};
    int base_{10};
    int state_{0};
};

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::machine()
    -> machine_state & {
    return machine_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::machine() const
    -> machine_state const & {
    return machine_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::source()
    -> input_source & {
    return source_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::source() const
    -> input_source const & {
    return source_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::base() const
    -> int {
    return base_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::set_base(int value)
    -> void {
    base_ = value;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::state() const
    -> int {
    return state_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::set_state(int value)
    -> void {
    state_ = value;
}

namespace detail {

static_assert(std::is_trivially_destructible_v<forth_state<64, 64, 1024, 256>>);

} // namespace detail

/// Returns the value of digit @p c in @p base, or -1 if @p c is not a valid
/// digit in that base. Letters above `9` are `A`..`Z` -- already uppercase,
/// since @ref reader::scan_word folds every token before this ever sees it
/// (D8) -- so base 16's `FF` and base 36's `Z` both work without a separate
/// lowercase branch.
[[nodiscard]] constexpr auto digit_value(char c, int base) -> int {
    int value = -1;
    if (c >= '0' && c <= '9') {
        value = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        value = c - 'A' + 10;
    }
    if (value < 0 || value >= base) {
        return -1;
    }
    return value;
}

/// True iff @p text is an optional leading `-` followed by one or more
/// valid digits in @p base, and nothing else -- @ref reader::is_number_token
/// generalized from decimal to `BASE` (D19): at `base == 10` the two agree
/// on every input (this header's own tests check that directly), since
/// letters are never valid decimal digits either way.
[[nodiscard]] constexpr auto is_number_token_in_base(std::string_view text,
                                                     int base) -> bool {
    std::size_t i = 0;
    if (!text.empty() && text[0] == '-') {
        i = 1;
    }
    if (i == text.size()) {
        return false;
    }
    for (; i < text.size(); ++i) {
        if (digit_value(text[i], base) < 0) {
            return false;
        }
    }
    return true;
}

/// Converts @p text into its signed value in @p base.
/// @pre is_number_token_in_base(text, base)
[[nodiscard]] constexpr auto token_to_cell_in_base(std::string_view text,
                                                   int base) -> machine::cell {
    bool const negative = !text.empty() && text[0] == '-';
    std::size_t i = negative ? 1 : 0;
    machine::cell value = 0;
    for (; i < text.size(); ++i) {
        value = value * base + digit_value(text[i], base);
    }
    return negative ? -value : value;
}

// Merge criteria (static_assert, immediately-invoked-lambda pattern):
// base-10 agreement with reader::is_number_token/token_to_cell, and a
// representative base-16 case.

static_assert(is_number_token_in_base("-1", 10) ==
              reader::is_number_token("-1"));
static_assert(is_number_token_in_base("1-", 10) ==
              reader::is_number_token("1-"));
static_assert(is_number_token_in_base("-", 10) == reader::is_number_token("-"));
static_assert(is_number_token_in_base("42", 10) ==
              reader::is_number_token("42"));
static_assert(is_number_token_in_base("DUP", 10) ==
              reader::is_number_token("DUP"));
static_assert(token_to_cell_in_base("42", 10) == reader::token_to_cell("42"));
static_assert(token_to_cell_in_base("-1", 10) == reader::token_to_cell("-1"));

static_assert(is_number_token_in_base("FF", 16));
static_assert(token_to_cell_in_base("FF", 16) == 255);
static_assert(!is_number_token_in_base("FG", 16));
static_assert(is_number_token_in_base("-FF", 16));
static_assert(token_to_cell_in_base("-FF", 16) == -255);

/// Decrements @p fuel, the text interpreter loop's own step budget (D22):
/// distinct from @ref machine::consume_vm_fuel (which only bounds a
/// *compiled* program's VM execution) and from @ref machine::consume_fuel
/// (which only bounds the direct evaluator) -- this one bounds @ref
/// interpret's own outer loop, so a pathological interpret-time source
/// (arbitrarily many tokens, none of which ever exhausts the data stack or
/// any other already-fueled subsystem) cannot run the constant evaluator
/// past its budget undiagnosed. Diagnoses exhaustion at @p pos rather than
/// looping forever. Called once per token @ref interpret actually
/// processes, so a budget of @p fuel bounds the total number of tokens any
/// one interpretation may consume.
[[nodiscard]] constexpr auto consume_interp_fuel(int &fuel,
                                                 foundation::source_pos pos)
    -> machine::status {
    if (fuel <= 0) {
        return foundation::parse_error{
            pos, "interpreter execution budget exhausted"};
    }
    --fuel;
    return std::monostate{};
}

// aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62
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
// aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62 end

} // namespace smd::forth::interpreter

#endif
