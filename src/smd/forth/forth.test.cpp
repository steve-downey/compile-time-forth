// src/smd/forth/forth.test.cpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/forth.hpp>

#include <smd/forth/forth.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

using smd::forth::compiled_forth;

TEST_CASE("ForthTest - HeaderIsIdempotent") { REQUIRE(true); }

// caa1756c-1684-4c8a-9cf9-cbd9dbf6bc21
// docs/forth-plan.md Step F15's own merge criterion, verbatim:
// `compiled_forth<": SQUARED DUP * ;  4 SQUARED">.stack()` static-asserts
// to `[16]` -- a one-element stack snapshot whose only cell is 16.
static_assert([] {
    auto s = compiled_forth<": SQUARED DUP * ;  4 SQUARED">.stack();
    return s.size() == 1 && s[0] == 16;
}());

TEST_CASE("ForthTest - SquaredMergeCriterion") {
    auto s = compiled_forth<": SQUARED DUP * ;  4 SQUARED">.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 16);
}
// caa1756c-1684-4c8a-9cf9-cbd9dbf6bc21 end

// A multi-cell stack, to prove .stack() is a real snapshot (bottom to top,
// matching machine::primitive::dot_s's own printed order) and not merely a
// single scalar.
static_assert([] {
    auto s = compiled_forth<"1 2 3">.stack();
    return s.size() == 3 && s[0] == 1 && s[1] == 2 && s[2] == 3;
}());

TEST_CASE("ForthTest - StackIsBottomToTopSnapshot") {
    auto s = compiled_forth<"1 2 3">.stack();
    REQUIRE(s.size() == 3);
    CHECK(s[0] == 1);
    CHECK(s[1] == 2);
    CHECK(s[2] == 3);
}

// .output(): the hello-example-shaped case, a program that prints rather
// than merely leaving a stack behind.
static_assert([] {
    auto out = compiled_forth<": GREET 42 . ;  GREET">.output();
    return out.size() == 3 && out[0] == '4' && out[1] == '2' && out[2] == ' ';
}());

TEST_CASE("ForthTest - OutputMergeCriterion") {
    auto out = compiled_forth<": GREET 42 . ;  GREET">.output();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == '4');
    CHECK(out[1] == '2');
    CHECK(out[2] == ' ');
}

// .run(): the full result<forth_state> surface, success case.
static_assert([] {
    auto r = compiled_forth<": SQUARED DUP * ;  4 SQUARED">.run();
    if (!r.has_value()) {
        return false;
    }
    return r.value().data().depth() == 1 &&
           r.value().data().peek(0).value() == 16;
}());

TEST_CASE("ForthTest - RunSucceeds") {
    auto r = compiled_forth<": SQUARED DUP * ;  4 SQUARED">.run();
    REQUIRE(r.has_value());
    CHECK(r.value().data().depth() == 1);
    CHECK(r.value().data().peek(0).value() == 16);
}

// .run() propagates a genuine runtime failure (budget exhaustion) rather
// than silently succeeding or hard-failing compilation -- only a bad
// *compile-time* result (a failed parse/elaborate/codegen) is a hard
// compile error (see test_neg_syntax_error.cpp); a program that compiles
// fine but does not terminate within its fuel budget is an ordinary
// runtime error, exactly as machine::run itself diagnoses it.
static_assert([] {
    auto r = compiled_forth<": SPIN BEGIN FALSE UNTIL ; SPIN">.run(
        /*fuel=*/10);
    return !r.has_value();
}());

TEST_CASE("ForthTest - RunPropagatesRuntimeError") {
    auto r = compiled_forth<": SPIN BEGIN FALSE UNTIL ; SPIN">.run(/*fuel=*/10);
    REQUIRE_FALSE(r.has_value());
}

// Capacities are template parameters with defaults (D2), not hardcoded: a
// caller may override the pipeline's own capacities (here, MaxCode) while
// still getting the same [2] stack result.
static_assert([] {
    auto s = compiled_forth<": ONE_PLUS_ONE 1 1 + ;  ONE_PLUS_ONE",
                            /*MaxCode=*/512>.stack();
    return s.size() == 1 && s[0] == 2;
}());

TEST_CASE("ForthTest - CapacitiesAreOverridable") {
    auto s = compiled_forth<": ONE_PLUS_ONE 1 1 + ;  ONE_PLUS_ONE",
                            /*MaxCode=*/512>.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 2);
}
