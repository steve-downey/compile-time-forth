// src/smd/forth/machine/forth_state.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/forth_state.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <limits>
#include <string_view>

using smd::forth::machine::apply_primitive;
using smd::forth::machine::cell;
using smd::forth::machine::flag_false;
using smd::forth::machine::flag_true;
using smd::forth::machine::forth_state;
using smd::forth::machine::primitive;

namespace {

using small_state = forth_state<8, 8, 8, 32>;

/// Runs @p op against a freshly seeded state (bottom-to-top order matches
/// argument order) and returns the resulting top of the data stack.
/// @pre op succeeds and leaves at least one cell on the data stack.
constexpr auto run(primitive op, std::initializer_list<cell> seed) -> cell {
    small_state state;
    for (auto v : seed) {
        (void)state.data().push(v);
    }
    (void)apply_primitive(op, state);
    return state.data().pop().value();
}

constexpr auto succeeds(primitive op, std::initializer_list<cell> seed)
    -> bool {
    small_state state;
    for (auto v : seed) {
        (void)state.data().push(v);
    }
    return apply_primitive(op, state).has_value();
}

} // namespace

// -- Arithmetic and logic ---------------------------------------------------

static_assert(run(primitive::plus, {2, 3}) == 5);
static_assert(run(primitive::minus, {5, 3}) == 2);
static_assert(run(primitive::star, {4, 5}) == 20);
static_assert(run(primitive::slash, {7, 2}) == 3);
static_assert(run(primitive::slash, {-7, 2}) == -3); // symmetric, truncating
static_assert(run(primitive::mod_, {7, 2}) == 1);
static_assert(run(primitive::mod_, {-7, 2}) == -1); // symmetric, truncating
static_assert(run(primitive::negate, {5}) == -5);
static_assert(run(primitive::abs_, {-5}) == 5);
static_assert(run(primitive::abs_, {5}) == 5);
static_assert(run(primitive::min_, {3, 7}) == 3);
static_assert(run(primitive::max_, {3, 7}) == 7);
static_assert(run(primitive::and_, {0b1100, 0b1010}) == 0b1000);
static_assert(run(primitive::or_, {0b1100, 0b1010}) == 0b1110);
static_assert(run(primitive::xor_, {0b1100, 0b1010}) == 0b0110);
static_assert(run(primitive::invert, {0}) == -1);
static_assert(run(primitive::lshift, {1, 4}) == 16);
static_assert(run(primitive::rshift, {16, 4}) == 1);
static_assert(run(primitive::one_minus, {3}) == 2);
static_assert(run(primitive::one_minus, {0}) == -1);

static_assert([] {
    // Division by zero is a diagnosed error, not UB.
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(0);
    return !apply_primitive(primitive::slash, state).has_value();
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(0);
    return !apply_primitive(primitive::mod_, state).has_value();
}());

// -- Comparison --------------------------------------------------------------

static_assert(run(primitive::zero_equal, {0}) == flag_true);
static_assert(run(primitive::zero_equal, {1}) == flag_false);
static_assert(run(primitive::zero_less, {-1}) == flag_true);
static_assert(run(primitive::zero_less, {1}) == flag_false);
static_assert(run(primitive::equal, {3, 3}) == flag_true);
static_assert(run(primitive::equal, {3, 4}) == flag_false);
static_assert(run(primitive::not_equal, {3, 4}) == flag_true);
static_assert(run(primitive::not_equal, {3, 3}) == flag_false);
static_assert(run(primitive::less, {3, 4}) == flag_true);
static_assert(run(primitive::less, {4, 3}) == flag_false);
static_assert(run(primitive::greater, {4, 3}) == flag_true);
static_assert(run(primitive::greater, {3, 4}) == flag_false);
static_assert(run(primitive::less_equal, {3, 3}) == flag_true);
static_assert(run(primitive::less_equal, {4, 3}) == flag_false);
static_assert(run(primitive::greater_equal, {3, 3}) == flag_true);
static_assert(run(primitive::greater_equal, {3, 4}) == flag_false);
static_assert(run(primitive::true_, {}) == flag_true);
static_assert(run(primitive::false_, {}) == flag_false);

// -- Data stack manipulation ---------------------------------------------

static_assert([] {
    small_state state;
    (void)state.data().push(5);
    (void)apply_primitive(primitive::dup, state);
    return state.data().depth() == 2 && state.data().pop().value() == 5 &&
           state.data().pop().value() == 5;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(5);
    (void)state.data().push(6);
    (void)apply_primitive(primitive::drop, state);
    return state.data().depth() == 1 && state.data().pop().value() == 5;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)apply_primitive(primitive::swap, state);
    return state.data().pop().value() == 1 && state.data().pop().value() == 2;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)apply_primitive(primitive::over, state);
    // 1 2 -- 1 2 1
    return state.data().pop().value() == 1 && state.data().pop().value() == 2 &&
           state.data().pop().value() == 1;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)state.data().push(3);
    (void)apply_primitive(primitive::rot, state);
    // 1 2 3 -- 2 3 1
    return state.data().pop().value() == 1 && state.data().pop().value() == 3 &&
           state.data().pop().value() == 2;
}());

static_assert([] {
    // ?DUP duplicates only a nonzero top.
    small_state state;
    (void)state.data().push(5);
    (void)apply_primitive(primitive::question_dup, state);
    return state.data().depth() == 2;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(0);
    (void)apply_primitive(primitive::question_dup, state);
    return state.data().depth() == 1;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)apply_primitive(primitive::nip, state);
    // 1 2 -- 2
    return state.data().depth() == 1 && state.data().pop().value() == 2;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)apply_primitive(primitive::tuck, state);
    // 1 2 -- 2 1 2
    return state.data().pop().value() == 2 && state.data().pop().value() == 1 &&
           state.data().pop().value() == 2;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)state.data().push(3);
    (void)apply_primitive(primitive::depth, state);
    return state.data().pop().value() == 3;
}());

// -- Return stack ------------------------------------------------------------

static_assert([] {
    small_state state;
    (void)state.data().push(9);
    (void)apply_primitive(primitive::to_r, state);
    return state.data().depth() == 0 && state.returns().depth() == 1;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(9);
    (void)apply_primitive(primitive::to_r, state);
    (void)apply_primitive(primitive::r_from, state);
    return state.returns().depth() == 0 && state.data().pop().value() == 9;
}());

static_assert([] {
    small_state state;
    (void)state.data().push(9);
    (void)apply_primitive(primitive::to_r, state);
    (void)apply_primitive(primitive::r_fetch, state);
    // R@ leaves the value on the return stack too.
    return state.returns().depth() == 1 && state.data().pop().value() == 9;
}());

// -- Underflow and overflow are diagnosed, not UB ----------------------------

static_assert(!succeeds(primitive::plus, {}));    // underflow: empty stack
static_assert(!succeeds(primitive::plus, {1}));   // underflow: only one operand
static_assert(!succeeds(primitive::dup, {}));     // underflow
static_assert(!succeeds(primitive::rot, {1, 2})); // underflow: needs three

static_assert([] {
    // Overflow: DUP on an already-full data stack.
    forth_state<1, 1, 1, 1> state;
    (void)state.data().push(1);
    return !apply_primitive(primitive::dup, state).has_value();
}());

static_assert([] {
    // Overflow: >R on an already-full return stack.
    forth_state<2, 1, 1, 1> state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)state.returns().push(9);
    (void)apply_primitive(primitive::to_r, state);
    return !apply_primitive(primitive::to_r, state).has_value();
}());

// -- Output buffer -------------------------------------------------------

static_assert([] {
    forth_state<4, 4, 4, 8> state;
    return state.emit_char('A').has_value() && state.output().size() == 1 &&
           state.output()[0] == 'A';
}());

static_assert([] {
    forth_state<4, 4, 4, 32> state;
    (void)state.emit_cell(-42);
    auto const &out = state.output();
    return out.size() == 4 && out[0] == '-' && out[1] == '4' && out[2] == '2' &&
           out[3] == ' ';
}());

static_assert([] {
    forth_state<4, 4, 4, 32> state;
    (void)state.emit_cell(0);
    auto const &out = state.output();
    return out.size() == 2 && out[0] == '0' && out[1] == ' ';
}());

static_assert([] {
    // Output buffer overflow is diagnosed.
    forth_state<4, 4, 4, 1> state;
    (void)state.emit_char('A');
    return !state.emit_char('B').has_value();
}());

// -- Output words (D10, wired this step) -------------------------------------

static_assert([] {
    // `.` pops and prints its decimal rendering plus a trailing space.
    small_state state;
    (void)state.data().push(42);
    (void)apply_primitive(primitive::dot, state);
    auto const &out = state.output();
    return state.data().depth() == 0 && out.size() == 3 && out[0] == '4' &&
           out[1] == '2' && out[2] == ' ';
}());

static_assert([] {
    // `.S` prints the whole stack, bottom to top, without consuming it.
    small_state state;
    (void)state.data().push(1);
    (void)state.data().push(2);
    (void)state.data().push(3);
    (void)apply_primitive(primitive::dot_s, state);
    auto const &out = state.output();
    return state.data().depth() == 3 && out.size() == 6 && out[0] == '1' &&
           out[1] == ' ' && out[2] == '2' && out[3] == ' ' && out[4] == '3' &&
           out[5] == ' ';
}());

static_assert([] {
    // `EMIT` pops a cell and prints it as a raw character (no trailing
    // space, unlike `.`).
    small_state state;
    (void)state.data().push(cell{'A'});
    (void)apply_primitive(primitive::emit, state);
    auto const &out = state.output();
    return state.data().depth() == 0 && out.size() == 1 && out[0] == 'A';
}());

static_assert([] {
    // `CR` prints a newline and touches nothing on the data stack.
    small_state state;
    (void)apply_primitive(primitive::cr, state);
    auto const &out = state.output();
    return state.data().depth() == 0 && out.size() == 1 && out[0] == '\n';
}());

static_assert([] {
    // `.` on an empty stack is a diagnosed underflow, not UB.
    small_state state;
    return !apply_primitive(primitive::dot, state).has_value();
}());

TEST_CASE("ForthStateTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("ForthStateTest - DotPrintsDecimalWithTrailingSpace") {
    small_state state;
    CHECK(state.data().push(-5).has_value());
    REQUIRE(apply_primitive(primitive::dot, state).has_value());
    auto const &out = state.output();
    std::string_view rendered{out.begin(),
                              static_cast<std::size_t>(out.size())};
    CHECK(rendered == "-5 ");
    CHECK(state.data().depth() == 0);
}

TEST_CASE("ForthStateTest - DataAndReturnStacksAreIndependent") {
    small_state state;
    CHECK(state.data().push(1).has_value());
    CHECK(state.returns().push(2).has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.returns().depth() == 1);
}

TEST_CASE("ForthStateTest - DataSpaceIsPresentAndSized") {
    small_state state;
    CHECK(state.data_space().size() == 0);
}

TEST_CASE("ForthStateTest - EmitCellMatchesForthFormat") {
    forth_state<4, 4, 4, 32> state;
    REQUIRE(state.emit_cell(-42).has_value());
    auto const &out = state.output();
    std::string_view rendered{out.begin(),
                              static_cast<std::size_t>(out.size())};
    CHECK(rendered == "-42 ");
}

TEST_CASE("ForthStateTest - EmitCellExtremeValues") {
    forth_state<4, 4, 4, 32> state;
    REQUIRE(state.emit_cell(std::numeric_limits<cell>::min()).has_value());
    auto const &out = state.output();
    std::string_view rendered{out.begin(),
                              static_cast<std::size_t>(out.size())};
    CHECK(rendered == "-9223372036854775808 ");
}

TEST_CASE("ForthStateTest - ApplyPrimitivePlus") {
    small_state state;
    CHECK(state.data().push(2).has_value());
    CHECK(state.data().push(3).has_value());
    REQUIRE(apply_primitive(primitive::plus, state).has_value());
    CHECK(state.data().pop().value() == 5);
}
