// src/smd/forth/machine/eval_direct.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/eval_direct.hpp>
#include <smd/forth/machine/eval_direct.hpp> // test 2nd include OK

#include <smd/forth/elaborator/elaborate.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/reader/read_program.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::elaborator::elaborate;
using smd::forth::machine::eval_program;
using smd::forth::reader::read_program;

TEST_CASE("EvalDirectTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: these tests never need the header's production defaults.
constexpr int test_max_nodes = 256;
constexpr int test_max_body = 32;
constexpr int test_max_name = 16;
constexpr int test_max_parse_depth = 8;
constexpr int test_max_words = 64;
constexpr int test_max_data = 16;
constexpr int test_max_warnings = 8;
constexpr int test_stack_depth = 16;
constexpr int test_rstack_depth = 8;
constexpr int test_out = 64;

constexpr auto parse(std::string_view source) {
    return read_program<test_max_nodes, test_max_body, test_max_name,
                        test_max_parse_depth>(source);
}

/// Runs the whole read -> elaborate -> eval pipeline (F13's own merge
/// criteria are explicitly whole-pipeline static_asserts). @pre both parsing
/// and elaboration succeed -- callers that need to inspect an elaboration
/// failure should call @ref elaborate directly instead.
constexpr auto run_program(std::string_view source, int fuel = 100000) {
    auto tree = parse(source);
    auto unit =
        elaborate<test_max_nodes, test_max_body, test_max_name, test_max_words,
                  test_max_data, test_max_warnings>(tree.value(), source);
    return eval_program<test_max_nodes, test_max_body, test_max_name,
                        test_max_words, test_max_data, test_max_warnings,
                        test_stack_depth, test_rstack_depth, test_out>(
        unit.value(), fuel);
}

/// True if @p out's contents equal @p expect, character for character.
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

} // namespace

// -- Merge criteria (docs/forth-plan.md Step F13) ---------------------------

// `: SQUARED DUP * ;  4 SQUARED` -- stack [16]. Exercises core_prim (DUP,
// *) and a bare literal push; SQUARED itself is called directly at the top
// level (not through another definition), so this also stands in as the
// base case core_call recursion builds on.
static_assert([] {
    auto result = run_program(": SQUARED DUP * ;  4 SQUARED");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 16;
}());

// `: ABS DUP 0< IF NEGATE THEN ;  -7 ABS` -- stack [7]. Exercises core_if
// with an empty ELSE arm (the canonical Forth-2012 idiom F12's own
// AbsIfNoElseTypeChecks test already proves elaborates).
static_assert([] {
    auto result = run_program(": ABS DUP 0< IF NEGATE THEN ;  -7 ABS");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 7;
}());

// `: COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN` -- stack [],
// output "3 2 1 ". Exercises core_begin_until as real repetition (not just
// abstract depth tracking), the `.` output primitive (D10, wired this step),
// and `1-` (also wired this step -- see DIV-0007: the plan's own word list
// omits it, but this merge criterion's own literal text requires it).
static_assert([] {
    auto result = run_program(": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;"
                              " 3 COUNTDOWN");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 0 && output_equals(state.output(), "3 2 1 ");
}());

// Budget exhaustion: `: SPIN BEGIN FALSE UNTIL ; SPIN` with a small fuel
// value is a diagnosed error, not a hang.
static_assert([] {
    auto result = run_program(": SPIN BEGIN FALSE UNTIL ; SPIN", /*fuel=*/10);
    return !result.has_value() &&
           result.error().message ==
               std::string_view{"evaluation budget exhausted"};
}());

// A generous fuel budget does not falsely diagnose an ordinary terminating
// program.
static_assert(
    run_program(": SQUARED DUP * ;  4 SQUARED", /*fuel=*/1000).has_value());

// -- EXIT unwinds to the enclosing definition's own boundary -----------------
//
// `: F DUP 0< IF EXIT THEN DROP ;` is DIV-0006's own worked counterexample:
// F12's stack-effect checker computes one effect for the whole definition
// without reconciling the early-EXIT path against the fall-through path.
// F13 actually executes both paths and observes the real, differing depths
// -- exactly the behavior DIV-0006 predicts, not a bug in F13.

static_assert([] {
    // Early-exit path: EXIT fires before DROP ever runs, leaving one cell.
    auto result = run_program(": F DUP 0< IF EXIT THEN DROP ;  -3 F");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == -3;
}());

static_assert([] {
    // Fall-through path: IF's condition is false, DROP runs, zero cells left.
    auto result = run_program(": F DUP 0< IF EXIT THEN DROP ;  7 F");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 0;
}());

// A call is its own definition boundary: EXIT inside a callee never unwinds
// past the call that invoked it.
static_assert([] {
    auto result = run_program(": INNER DUP 0< IF EXIT THEN DROP ; "
                              ": OUTER INNER 99 ; "
                              "-3 OUTER");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    // INNER's own EXIT leaves -3 on the stack and returns to OUTER; OUTER
    // keeps running and pushes 99 -- if EXIT had incorrectly unwound past
    // the call, 99 would never be pushed.
    return state.data().depth() == 2 && state.data().peek(0).value() == 99 &&
           state.data().peek(1).value() == -3;
}());

// -- BEGIN...WHILE...REPEAT is real repetition, not just depth tracking -----

static_assert([] {
    auto result = run_program(": UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ;"
                              " UPTO3");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 3;
}());

// -- Counted loops (docs/forth-plan.md Step F17) ----------------------------

// `: SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO` -- stack [15]. Exercises DO,
// LOOP, I, and 1+ (DIV-0010).
static_assert([] {
    auto result = run_program(": SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 15;
}());

// `: FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5` -- stack [5].
// Exercises LEAVE as a one-shot forward exit out of the loop.
static_assert([] {
    auto result =
        run_program(": FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 5;
}());

// Nested loops and `J`: sum J over 3x3 iterations = 3*(0+1+2) = 9.
static_assert([] {
    auto result = run_program(": TENS 0 3 0 DO 3 0 DO J + LOOP LOOP ;  TENS");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 9;
}());

// `+LOOP`: `: EVENS10 0 10 0 DO I + 2 +LOOP ;` sums 0+2+4+6+8 = 20.
static_assert([] {
    auto result = run_program(": SUMEVEN 0 10 0 DO I + 2 +LOOP ;  SUMEVEN");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 20;
}());

// A top-level DO...LOOP now runs (F17); the empty-body loop leaves nothing.
static_assert([] {
    auto result = run_program("5 0 DO LOOP");
    if (!result.has_value()) {
        return false;
    }
    return result.value().data().depth() == 0;
}());

// `EXIT` inside a `DO` loop without `UNLOOP` is still diagnosed at
// elaboration (diagnosis 4), so it never reaches the evaluator.
static_assert([] {
    auto tree = parse(": BAD 10 0 DO I 5 = IF EXIT THEN LOOP ; BAD");
    auto unit = elaborate<test_max_nodes, test_max_body, test_max_name,
                          test_max_words, test_max_data, test_max_warnings>(
        tree.value(), ": BAD 10 0 DO I 5 = IF "
                      "EXIT THEN LOOP ; BAD");
    return !unit.has_value();
}());

// `UNLOOP EXIT` inside a `DO` loop is accepted (the UNLOOP clears the
// requirement) and runs: FIRST returns the first index whose square exceeds
// 8 (i.e. 3), leaving via UNLOOP EXIT with that index already on the stack.
static_assert([] {
    auto result = run_program(": FIRST 10 0 DO I DUP DUP * 8 > IF UNLOOP EXIT "
                              "THEN DROP LOOP -1 ;  FIRST");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 3;
}());

// -- Memory words (Step F16 merge criteria) ----------------------------------
//
// `VARIABLE X  5 X !  X @ 3 + X !  X @` -- stack [8], and
// `7 CONSTANT LUCKY  LUCKY LUCKY +` -- stack [14], both in one program per
// the plan's own combined merge criterion. `@`/`!`/`+!` round-trip through a
// real forth_state data space seeded from unit.data_space (this step's own
// wiring), not just literal addresses.
static_assert([] {
    auto result = run_program("VARIABLE X  5 X !  X @ 3 + X !  X @ "
                              "7 CONSTANT LUCKY  LUCKY LUCKY +");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 2 && state.data().peek(0).value() == 14 &&
           state.data().peek(1).value() == 8;
}());

// `CREATE BUF 4 ALLOT` -- BUF is usable as a base address: storing through
// BUF and BUF+3 (the last of the four ALLOT'd cells) and reading both back
// proves ALLOT actually extended the data space past CREATE's own address,
// not merely recorded it (elaborate_create allots nothing itself).
static_assert([] {
    auto result = run_program("CREATE BUF 4 ALLOT "
                              "10 BUF ! 20 BUF 3 + ! "
                              "BUF @ BUF 3 + @ +");
    if (!result.has_value()) {
        return false;
    }
    auto const &state = result.value();
    return state.data().depth() == 1 && state.data().peek(0).value() == 30;
}());

// An out-of-bounds store is a diagnosed error, never UB (D7): X is a
// one-cell VARIABLE, so X + 5 lands past the data space's own high-water
// mark.
static_assert([] {
    auto result = run_program("VARIABLE X  99 X 5 + !");
    return !result.has_value();
}());

TEST_CASE("EvalDirectTest - MemoryWordsMergeCriterion") {
    auto result = run_program("VARIABLE X  5 X !  X @ 3 + X !  X @ "
                              "7 CONSTANT LUCKY  LUCKY LUCKY +");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    REQUIRE(state.data().depth() == 2);
    CHECK(state.data().peek(0).value() == 14);
    CHECK(state.data().peek(1).value() == 8);
}

TEST_CASE("EvalDirectTest - CreateAllotMergeCriterion") {
    auto result = run_program("CREATE BUF 4 ALLOT "
                              "10 BUF ! 20 BUF 3 + ! "
                              "BUF @ BUF 3 + @ +");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 30);
}

TEST_CASE("EvalDirectTest - OutOfBoundsStoreIsDiagnosed") {
    auto result = run_program("VARIABLE X  99 X 5 + !");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("EvalDirectTest - SquaredMergeCriterion") {
    auto result = run_program(": SQUARED DUP * ;  4 SQUARED");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 16);
}

TEST_CASE("EvalDirectTest - AbsMergeCriterion") {
    auto result = run_program(": ABS DUP 0< IF NEGATE THEN ;  -7 ABS");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 7);
}

TEST_CASE("EvalDirectTest - CountdownMergeCriterion") {
    auto result = run_program(": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;"
                              " 3 COUNTDOWN");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 0);
    CHECK(output_equals(state.output(), "3 2 1 "));
}

TEST_CASE("EvalDirectTest - BudgetExhaustionIsDiagnosed") {
    auto result = run_program(": SPIN BEGIN FALSE UNTIL ; SPIN", /*fuel=*/10);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == std::string_view{"evaluation budget "
                                                     "exhausted"});
}

TEST_CASE("EvalDirectTest - ExitUnwindsOnlyToItsOwnCallBoundary") {
    auto result = run_program(": INNER DUP 0< IF EXIT THEN DROP ; "
                              ": OUTER INNER 99 ; "
                              "-3 OUTER");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    REQUIRE(state.data().depth() == 2);
    CHECK(state.data().peek(0).value() == 99);
    CHECK(state.data().peek(1).value() == -3);
}

TEST_CASE("EvalDirectTest - WhileLoopIsRealRepetition") {
    auto result = run_program(": UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ;"
                              " UPTO3");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 3);
}

TEST_CASE("EvalDirectTest - SumToCountedLoop") {
    auto result = run_program(": SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 15);
}

TEST_CASE("EvalDirectTest - Find5Leave") {
    auto result =
        run_program(": FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 5);
}

TEST_CASE("EvalDirectTest - NestedLoopJ") {
    auto result = run_program(": TENS 0 3 0 DO 3 0 DO J + LOOP LOOP ;  TENS");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 9);
}

TEST_CASE("EvalDirectTest - PlusLoop") {
    auto result = run_program(": SUMEVEN 0 10 0 DO I + 2 +LOOP ;  SUMEVEN");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 20);
}

TEST_CASE("EvalDirectTest - UnloopExitAcceptedAndRuns") {
    auto result = run_program(": FIRST 10 0 DO I DUP DUP * 8 > IF UNLOOP EXIT "
                              "THEN DROP LOOP -1 ;  FIRST");
    REQUIRE(result.has_value());
    auto const &state = result.value();
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 3);
}

TEST_CASE("EvalDirectTest - ExitInDoWithoutUnloopDiagnosed") {
    auto tree = parse(": BAD 10 0 DO I 5 = IF EXIT THEN LOOP ; BAD");
    auto unit = elaborate<test_max_nodes, test_max_body, test_max_name,
                          test_max_words, test_max_data, test_max_warnings>(
        tree.value(), ": BAD 10 0 DO I 5 = IF EXIT THEN LOOP ; BAD");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error().message ==
          std::string_view{"EXIT inside a DO loop requires UNLOOP"});
}
