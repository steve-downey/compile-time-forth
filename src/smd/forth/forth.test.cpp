// src/smd/forth/forth.test.cpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/forth.hpp>

#include <smd/forth/forth.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::compiled_forth;

TEST_CASE("ForthTest - HeaderIsIdempotent") { REQUIRE(true); }

// caa1756c-1684-4c8a-9cf9-cbd9dbf6bc21
// docs/forth-plan.md Step F15's own merge criterion, verbatim, still true
// after step F26 retargets compiled_forth onto D15's session image:
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

// .run(): the whole built session image (step F26 retargets this from the
// R1-era result<forth_state> to interpreter::session -- see forth.hpp's own
// top-of-file comment and DIV-0014). Unlike the old .run(), this never fails
// at the call site: any failure already happened, at compile time, when
// compiled_forth's own namespace-scope constexpr initializer built the
// session (test_neg_syntax_error.cpp exercises that failure side).
static_assert([] {
    auto const &img = compiled_forth<": SQUARED DUP * ;  4 SQUARED">.run();
    return img.stack.size() == 1 && img.stack[0] == 16;
}());

TEST_CASE("ForthTest - RunReturnsTheBuiltSessionImage") {
    auto const &img = compiled_forth<": SQUARED DUP * ;  4 SQUARED">.run();
    REQUIRE(img.stack.size() == 1);
    CHECK(img.stack[0] == 16);
}

// docs/forth-plan-2.md's own F26 note: a program that "compiles fine but
// does not terminate within its fuel budget" used to be an *ordinary
// runtime* error from the old R1-era .run(fuel) -- distinct from a hard
// compile error, and exercised here with SPIN (`BEGIN FALSE UNTIL`) under a
// deliberately small fuel. That distinction no longer exists: under D13/D14
// there is no separate "compile" phase from "run the top level" (building
// compiled_forth's own session *is* running it), so a budget-exhausted
// top-level loop is now a hard *compile* error too, caught by the same
// build-time evaluation as any other diagnosed failure. SPIN itself also
// needs control flow (BEGIN/UNTIL), which F25's interpret() does not have
// yet regardless (F27 is what adds it) -- its source text and expected
// "budget exhaustion" behavior are preserved verbatim in
// interpreter/control_flow_corpus.hpp::spin_program for F27 to reproduce
// through interpret() directly, where a recoverable (non-hard-error) runtime
// result is still the right shape (interpreter::build_session's own
// foundation::result<session> return channel). See DIV-0014.

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

// Step F16's own merge criterion, through the public one-shot API: no
// forth_program/compiled_forth change was needed beyond the F26 retarget
// itself -- VARIABLE/CONSTANT are now interpreter-recognized defining words
// (interp.hpp, F26), so this program runs through interpreter::interpret
// exactly like any other.
static_assert([] {
    auto s = compiled_forth<"VARIABLE X  5 X !  X @ 3 + X !  X @ "
                            "7 CONSTANT LUCKY  LUCKY LUCKY +">.stack();
    return s.size() == 2 && s[0] == 8 && s[1] == 14;
}());

TEST_CASE("ForthTest - MemoryWordsMergeCriterion") {
    auto s = compiled_forth<"VARIABLE X  5 X !  X @ 3 + X !  X @ "
                            "7 CONSTANT LUCKY  LUCKY LUCKY +">.stack();
    REQUIRE(s.size() == 2);
    CHECK(s[0] == 8);
    CHECK(s[1] == 14);
}

// Step F27: the public one-shot API can run control-flow programs again --
// F26 retargeted compiled_forth onto the session image before interpret()
// had any control flow at all, so every program below was unreachable
// through this header until this step (see this file's own SPIN comment,
// above, for why SPIN itself specifically -- a program that never
// terminates -- still cannot go through compiled_forth<Source>, control
// flow or not: a hard compile error is the correct outcome for it either
// way, just for budget exhaustion rather than an unknown-word diagnosis).

static_assert([] {
    auto s = compiled_forth<": ABS DUP 0< IF NEGATE THEN ;  -7 ABS">.stack();
    return s.size() == 1 && s[0] == 7;
}());

TEST_CASE("ForthTest - AbsControlFlowMergeCriterion") {
    auto s = compiled_forth<": ABS DUP 0< IF NEGATE THEN ;  -7 ABS">.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 7);
}

static_assert([] {
    auto out =
        compiled_forth<": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  "
                       "3 COUNTDOWN">.output();
    return out.size() == 6 &&
           std::string_view{out.begin(),
                            static_cast<std::size_t>(out.size())} == "3 2 1 ";
}());

TEST_CASE("ForthTest - CountdownControlFlowMergeCriterion") {
    auto out =
        compiled_forth<": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  "
                       "3 COUNTDOWN">.output();
    REQUIRE(out.size() == 6);
    CHECK(std::string_view{out.begin(), static_cast<std::size_t>(out.size())} ==
          "3 2 1 ");
}

static_assert([] {
    auto s =
        compiled_forth<": SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO">.stack();
    return s.size() == 1 && s[0] == 15;
}());

TEST_CASE("ForthTest - SumtoControlFlowMergeCriterion") {
    auto s =
        compiled_forth<": SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO">.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 15);
}

// Step F28: execution tokens and defining words, through the public one-shot
// API -- no forth_program/compiled_forth change was needed here either, for
// the same reason as F27's own control-flow criteria above: interpret()
// itself is what gained `CREATE`/`DOES>`/`'`/`EXECUTE` (interp.hpp, D18), and
// compiled_forth<Source> already runs any program through interpret()
// unchanged.

static_assert([] {
    auto s = compiled_forth<": CONSTANT2 CREATE , DOES> @ ;  "
                            "42 CONSTANT2 LIFE  LIFE">.stack();
    return s.size() == 1 && s[0] == 42;
}());

TEST_CASE("ForthTest - ConstantTwoDoesMergeCriterion") {
    auto s = compiled_forth<": CONSTANT2 CREATE , DOES> @ ;  "
                            "42 CONSTANT2 LIFE  LIFE">.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 42);
}

static_assert([] {
    auto s = compiled_forth<": SQUARED DUP * ;  5 ' SQUARED EXECUTE">.stack();
    return s.size() == 1 && s[0] == 25;
}());

TEST_CASE("ForthTest - TickExecuteMergeCriterion") {
    auto s = compiled_forth<": SQUARED DUP * ;  5 ' SQUARED EXECUTE">.stack();
    REQUIRE(s.size() == 1);
    CHECK(s[0] == 25);
}
