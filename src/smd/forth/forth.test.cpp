// src/smd/forth/forth.test.cpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/forth.hpp>

#include <smd/forth/forth.hpp> // test 2nd include OK

#include <smd/forth/foundation/static_vector.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::compiled_forth;

TEST_CASE("ForthTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

/// True if @p out's contents equal @p expect, character for character.
/// Mirrors machine/eval_direct.test.cpp's and machine/vm.test.cpp's
/// identical helper -- every backend's tests check output the same way.
template <int MaxOut>
constexpr auto
output_equals(smd::forth::foundation::static_vector<char, MaxOut> const &out,
              std::string_view expect) -> bool {
    if (out.size() != static_cast<int>(expect.size())) {
        return false;
    }
    for (int i = 0; i < out.size(); ++i) {
        if (out[i] != expect[static_cast<std::size_t>(i)]) {
            return false;
        }
    }
    return true;
}

// docs/forth-plan.md's own Step F15 merge criterion, verbatim:
// `compiled_forth<": SQUARED DUP * ; 4 SQUARED">.stack()` static_asserts to
// `[16]`. `compiled_forth<Source>` alone (no further template arguments)
// uses every capacity's own production default.
constexpr auto squared_program = compiled_forth<": SQUARED DUP * ;  4 SQUARED">;

// Parity with eval_direct.test.cpp's/vm.test.cpp's COUNTDOWN program:
// exercises .output() and .stack() together through the public API.
constexpr auto countdown_program =
    compiled_forth<": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  "
                   "3 COUNTDOWN">;

} // namespace

// 19adbd89-9edc-48c8-9244-66f20044b81c
// -- Step F15's own merge criterion --------------------------------------
static_assert([] {
    auto s = squared_program.stack();
    return s.size() == 1 && s[0] == 16;
}());

TEST_CASE("ForthTest - SquaredMergeCriterion") {
    auto s = squared_program.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 16);
}
// 19adbd89-9edc-48c8-9244-66f20044b81c end

static_assert([] {
    return output_equals(countdown_program.output(), "3 2 1 ") &&
           countdown_program.stack().size() == 0;
}());

TEST_CASE("ForthTest - CountdownOutputAndStack") {
    CHECK(output_equals(countdown_program.output(), "3 2 1 "));
    CHECK(countdown_program.stack().size() == 0);
}

// .run() itself: the full result<forth_state>, not just the convenience
// accessors that discard the error channel.
static_assert([] {
    auto r = squared_program.run();
    return r.has_value() && r.value().data().depth() == 1 &&
           r.value().data().peek(0).value() == 16;
}());

TEST_CASE("ForthTest - RunReturnsForthState") {
    auto r = squared_program.run();
    REQUIRE(r.has_value());
    CHECK(r.value().data().depth() == 1);
    CHECK(r.value().data().peek(0).value() == 16);
}

// A *runtime* error (division by zero) is diagnosed through .run()'s
// returned result, not a compile error -- only a syntax/elaboration failure
// is a hard compile error (see neg_compile_syntax_error.cpp for that half
// of the contract, checked at CMake configure time via try_compile()).
static_assert([] {
    constexpr auto div_zero_program = compiled_forth<"1 0 /">;
    return !div_zero_program.run().has_value();
}());

TEST_CASE("ForthTest - RuntimeErrorIsDiagnosedNotAHardCompileError") {
    constexpr auto div_zero_program = compiled_forth<"1 0 /">;
    auto r = div_zero_program.run();
    REQUIRE_FALSE(r.has_value());
    CHECK(div_zero_program.stack().size() == 0);
}

// Overriding a capacity beyond its default still works -- compiled_forth's
// own capacities are template parameters with defaults (D2), not hardcoded
// constants.
static_assert([] {
    constexpr auto tiny_stack_program =
        compiled_forth<"1 2 3", 4096, 1024, 64, 32, 32, 256, 1024, 64,
                       /*StackDepth=*/2>;
    // Three pushes against a two-cell-deep data stack: the third push
    // overflows, so .run() diagnoses an error and .stack() reports empty.
    return !tiny_stack_program.run().has_value() &&
           tiny_stack_program.stack().size() == 0;
}());
